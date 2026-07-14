#include "webhook_notifier.h"
#include "utils/logger.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#ifdef COS_USE_REMOTE_REPORTING
#include <curl/curl.h>
#endif

namespace changeos {
namespace webhook {

namespace {

std::string timestamp_to_iso8601(std::chrono::system_clock::time_point tp) {
    auto time = std::chrono::system_clock::to_time_t(tp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        tp.time_since_epoch()) % 1000;
    // 使用线程安全的 gmtime_r（POSIX）或 gmtime_s（Windows）替代 std::gmtime
    struct tm tm_buf;
#if defined(_WIN32) || defined(_MSC_VER)
    gmtime_s(&tm_buf, &time);
#else
    gmtime_r(&time, &tm_buf);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);
    std::string result = buf;
    result += "." + std::to_string(ms.count()) + "Z";
    return result;
}

std::string generate_uuid() {
    static std::atomic<std::uint64_t> counter(0);
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    std::stringstream ss;
    ss << std::hex << ms << "-" << (++counter);
    return ss.str();
}

std::string severity_to_string(alert::AlertSeverity s) {
    switch (s) {
        case alert::AlertSeverity::Info: return "info";
        case alert::AlertSeverity::Warning: return "warning";
        case alert::AlertSeverity::Critical: return "critical";
        case alert::AlertSeverity::Emergency: return "emergency";
        default: return "unknown";
    }
}

int severity_to_int(alert::AlertSeverity s) {
    switch (s) {
        case alert::AlertSeverity::Info: return 1;
        case alert::AlertSeverity::Warning: return 2;
        case alert::AlertSeverity::Critical: return 3;
        case alert::AlertSeverity::Emergency: return 4;
        default: return 0;
    }
}

std::string escape_json(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    std::stringstream ss;
                    ss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<int>(c);
                    result += ss.str();
                } else {
                    result += c;
                }
        }
    }
    return result;
}

} // anonymous namespace

WebhookNotifier::WebhookNotifier() = default;
WebhookNotifier::~WebhookNotifier() = default;

void WebhookNotifier::add_webhook(const WebhookConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    webhooks_.erase(
        std::remove_if(webhooks_.begin(), webhooks_.end(),
            [&config](const WebhookConfig& w) { return w.name == config.name; }),
        webhooks_.end());
    webhooks_.push_back(config);
}

void WebhookNotifier::remove_webhook(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    webhooks_.erase(
        std::remove_if(webhooks_.begin(), webhooks_.end(),
            [&name](const WebhookConfig& w) { return w.name == name; }),
        webhooks_.end());
}

void WebhookNotifier::enable_webhook(const std::string& name, bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& w : webhooks_) {
        if (w.name == name) {
            w.enabled = enabled;
            break;
        }
    }
}

std::vector<WebhookConfig> WebhookNotifier::get_webhooks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return webhooks_;
}

void WebhookNotifier::clear_webhooks() {
    std::lock_guard<std::mutex> lock(mutex_);
    webhooks_.clear();
}

void WebhookNotifier::on_alert(const alert::Alert& alert) {
    std::vector<WebhookConfig> webhooks_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        webhooks_copy = webhooks_;
    }

    WebhookPayload payload = create_payload(alert);

    for (const auto& webhook : webhooks_copy) {
        if (webhook.enabled && webhook.send_alerts &&
            should_send_alert(webhook, alert)) {
            send_webhook(webhook.name, payload);
        }
    }
}

void WebhookNotifier::on_security_event(const security::SecurityEvent& event) {
    std::vector<WebhookConfig> webhooks_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        webhooks_copy = webhooks_;
    }

    WebhookPayload payload = create_payload(event);

    for (const auto& webhook : webhooks_copy) {
        if (webhook.enabled && webhook.send_security_events &&
            should_send_security_event(webhook, event)) {
            send_webhook(webhook.name, payload);
        }
    }
}

bool WebhookNotifier::send_webhook(const std::string& name, const WebhookPayload& payload) {
    WebhookConfig config;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(webhooks_.begin(), webhooks_.end(),
            [&name](const WebhookConfig& w) { return w.name == name; });
        if (it == webhooks_.end()) {
            COS_LOG_ERROR("Webhook not found: " + name);
            return false;
        }
        // 将 config 赋值移到锁内，避免迭代器在锁释放后失效
        config = *it;
    }

    if (!config.enabled) {
        return false;
    }

    std::string body = serialize_payload(payload);

    // Add signature if secret is configured
    std::map<std::string, std::string> headers = config.headers;
    if (!config.secret.empty()) {
        std::string sig = compute_signature(config.secret, body);
        headers["X-Signature"] = sig;
    }
    headers["Content-Type"] = "application/json";
    headers["User-Agent"] = "change-of-system/1.0";

    // 日志中脱敏 Authorization 头，避免凭证泄露
    std::string log_headers_summary;
    for (const auto& kv : headers) {
        if (kv.first == "Authorization") {
            log_headers_summary += kv.first + ": [REDACTED], ";
        } else {
            log_headers_summary += kv.first + ": " + kv.second + ", ";
        }
    }

    int http_status = 0;
    std::string error;
    bool success = false;

    for (int attempt = 0; attempt < config.retry_count; ++attempt) {
        auto start = std::chrono::system_clock::now();
        success = http_post(config.url, body, headers, config.timeout_ms, http_status, error);
        auto end = std::chrono::system_clock::now();
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        WebhookResult result;
        result.id = next_result_id_++;
        result.webhook_name = config.name;
        result.url = config.url;
        result.success = success;
        result.http_status = http_status;
        result.error_message = error;
        result.timestamp = end;
        result.duration_ms = duration_ms;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            results_.push_back(result);
            if (results_.size() > 1000) {
                results_.erase(results_.begin(), results_.begin() + (results_.size() - 1000));
            }
            total_sent_++;
            if (success) {
                total_success_++;
            } else {
                total_failed_++;
            }

            for (const auto& cb : callbacks_) {
                cb(result);
            }
        }

        if (success) {
            COS_LOG_INFO("Webhook sent successfully: " + config.name + " -> " + config.url);
            break;
        }

        if (attempt < config.retry_count - 1) {
            COS_LOG_WARN("Webhook failed (attempt " + std::to_string(attempt + 1) + "/" + std::to_string(config.retry_count) + "): " + config.name + " - " + error);
            std::this_thread::sleep_for(std::chrono::milliseconds(config.retry_delay_ms));
        }
    }

    if (!success) {
        COS_LOG_ERROR("Webhook failed after " + std::to_string(config.retry_count) + " attempts: " + config.name + " - " + error);
    }

    return success;
}

void WebhookNotifier::send_all_webhooks(const WebhookPayload& payload) {
    std::vector<WebhookConfig> webhooks_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        webhooks_copy = webhooks_;
    }

    for (const auto& webhook : webhooks_copy) {
        if (webhook.enabled) {
            send_webhook(webhook.name, payload);
        }
    }
}

void WebhookNotifier::on_result(ResultCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_.push_back(std::move(cb));
}

std::size_t WebhookNotifier::total_sent() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_sent_;
}

std::size_t WebhookNotifier::total_success() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_success_;
}

std::size_t WebhookNotifier::total_failed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_failed_;
}

std::vector<WebhookResult> WebhookNotifier::get_recent_results(std::size_t limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (results_.size() <= limit) {
        return results_;
    }
    return std::vector<WebhookResult>(results_.end() - limit, results_.end());
}

WebhookPayload WebhookNotifier::create_payload(const alert::Alert& alert) {
    WebhookPayload payload;
    payload.id = std::to_string(alert.id);
    payload.timestamp = timestamp_to_iso8601(alert.timestamp);
    payload.event_type = "alert";
    payload.source = alert.trigger_event.source;
    payload.target = alert.trigger_event.target;
    payload.summary = alert.message;
    payload.severity = severity_to_int(alert.severity);
    payload.details["rule_name"] = alert.rule_name;
    payload.details["severity"] = severity_to_string(alert.severity);
    for (const auto& kv : alert.metadata) {
        payload.details[kv.first] = kv.second;
    }
    return payload;
}

WebhookPayload WebhookNotifier::create_payload(const security::SecurityEvent& event) {
    WebhookPayload payload;
    payload.id = std::to_string(event.id);
    payload.timestamp = timestamp_to_iso8601(event.timestamp);
    payload.event_type = "security_event";
    payload.source = event.source;
    payload.target = event.target;
    payload.summary = event.summary;
    payload.severity = event.severity;
    payload.details["type"] = security::security_event_type_name(event.type);
    for (const auto& kv : event.details) {
        payload.details[kv.first] = kv.second;
    }
    return payload;
}

std::string WebhookNotifier::serialize_payload(const WebhookPayload& payload) {
    std::stringstream ss;
    ss << "{";
    ss << "\"id\":\"" << escape_json(payload.id) << "\",";
    ss << "\"timestamp\":\"" << escape_json(payload.timestamp) << "\",";
    ss << "\"event_type\":\"" << escape_json(payload.event_type) << "\",";
    ss << "\"source\":\"" << escape_json(payload.source) << "\",";
    ss << "\"target\":\"" << escape_json(payload.target) << "\",";
    ss << "\"summary\":\"" << escape_json(payload.summary) << "\",";
    ss << "\"severity\":" << payload.severity << ",";
    ss << "\"details\":{";
    bool first = true;
    for (const auto& kv : payload.details) {
        if (!first) ss << ",";
        ss << "\"" << escape_json(kv.first) << "\":\"" << escape_json(kv.second) << "\"";
        first = false;
    }
    ss << "}";
    if (!payload.signature.empty()) {
        ss << ",\"signature\":\"" << escape_json(payload.signature) << "\"";
    }
    ss << "}";
    return ss.str();
}

std::string WebhookNotifier::compute_signature(const std::string& secret, const std::string& data) {
    // 使用 OpenSSL HMAC-SHA256 计算签名，替代不安全的 std::hash
    unsigned char* result = HMAC(
        EVP_sha256(),
        secret.c_str(), static_cast<int>(secret.size()),
        reinterpret_cast<const unsigned char*>(data.c_str()), data.size(),
        nullptr, nullptr);
    if (!result) {
        COS_LOG_ERROR("HMAC-SHA256 签名计算失败");
        return "";
    }
    // 将 32 字节的 HMAC 输出转换为十六进制字符串
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < 32; ++i) {
        ss << std::setw(2) << static_cast<unsigned int>(result[i]);
    }
    return ss.str();
}

bool WebhookNotifier::http_post(const std::string& url, const std::string& body,
                               const std::map<std::string, std::string>& headers,
                               int timeout_ms, int& http_status, std::string& error) {
#ifdef COS_USE_REMOTE_REPORTING
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "Failed to initialize CURL";
        return false;
    }

    struct curl_slist* chunk = nullptr;
    for (const auto& kv : headers) {
        std::string header = kv.first + ": " + kv.second;
        chunk = curl_slist_append(chunk, header.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    // SSL 证书验证默认启用（安全最佳实践）
    // 仅在测试环境可通过配置关闭
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    CURLcode res = curl_easy_perform(curl);

    long status = 0;
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        http_status = static_cast<int>(status);
    } else {
        error = curl_easy_strerror(res);
        http_status = 0;
    }

    curl_slist_free_all(chunk);
    curl_easy_cleanup(curl);

    return res == CURLE_OK && status >= 200 && status < 300;
#else
    // Without CURL, we can't make HTTP requests
    error = "HTTP support not compiled in (COS_USE_REMOTE_REPORTING=OFF)";
    http_status = 0;
    return false;
#endif
}

bool WebhookNotifier::should_send_alert(const WebhookConfig& config, const alert::Alert& alert) {
    if (config.min_severities.empty()) {
        return true;
    }
    return std::find(config.min_severities.begin(), config.min_severities.end(),
                    alert.severity) != config.min_severities.end();
}

bool WebhookNotifier::should_send_security_event(const WebhookConfig& config,
                                                 const security::SecurityEvent& event) {
    if (config.security_event_types.empty()) {
        return true;
    }
    return std::find(config.security_event_types.begin(), config.security_event_types.end(),
                    event.type) != config.security_event_types.end();
}

} // namespace webhook
} // namespace changeos