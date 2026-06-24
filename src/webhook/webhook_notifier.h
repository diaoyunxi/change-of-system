#pragma once

#include "alert/alert_manager.h"
#include "security/security_auditor.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace changeos {
namespace webhook {

struct WebhookConfig {
    std::string name;
    std::string url;
    std::string secret;  // For HMAC signature
    std::map<std::string, std::string> headers;
    bool enabled = true;
    int timeout_ms = 5000;
    int retry_count = 3;
    int retry_delay_ms = 1000;

    // Filter options
    std::vector<alert::AlertSeverity> min_severities;
    std::vector<security::SecurityEventType> security_event_types;
    bool send_alerts = true;
    bool send_security_events = true;
};

struct WebhookPayload {
    std::string id;
    std::string timestamp;
    std::string event_type;  // "alert" or "security_event"
    std::string source;
    std::string target;
    std::string summary;
    int severity;
    std::map<std::string, std::string> details;
    std::string signature;
};

struct WebhookResult {
    std::uint64_t id = 0;
    std::string webhook_name;
    std::string url;
    bool success = false;
    int http_status = 0;
    std::string error_message;
    std::chrono::system_clock::time_point timestamp;
    std::int64_t duration_ms = 0;
};

class WebhookNotifier {
public:
    using ResultCallback = std::function<void(const WebhookResult&)>;

    WebhookNotifier();
    ~WebhookNotifier();

    // Configuration
    void add_webhook(const WebhookConfig& config);
    void remove_webhook(const std::string& name);
    void enable_webhook(const std::string& name, bool enabled = true);
    std::vector<WebhookConfig> get_webhooks() const;
    void clear_webhooks();

    // Event handlers
    void on_alert(const alert::Alert& alert);
    void on_security_event(const security::SecurityEvent& event);

    // Manual send
    bool send_webhook(const std::string& name, const WebhookPayload& payload);
    void send_all_webhooks(const WebhookPayload& payload);

    // Callbacks
    void on_result(ResultCallback cb);

    // Statistics
    std::size_t total_sent() const;
    std::size_t total_success() const;
    std::size_t total_failed() const;
    std::vector<WebhookResult> get_recent_results(std::size_t limit = 100) const;

private:
    WebhookPayload create_payload(const alert::Alert& alert);
    WebhookPayload create_payload(const security::SecurityEvent& event);
    std::string serialize_payload(const WebhookPayload& payload);
    std::string compute_signature(const std::string& secret, const std::string& data);
    bool http_post(const std::string& url, const std::string& body,
                   const std::map<std::string, std::string>& headers,
                   int timeout_ms, int& http_status, std::string& error);
    bool should_send_alert(const WebhookConfig& config, const alert::Alert& alert);
    bool should_send_security_event(const WebhookConfig& config,
                                    const security::SecurityEvent& event);

    mutable std::mutex mutex_;
    std::vector<WebhookConfig> webhooks_;
    std::vector<WebhookResult> results_;
    std::vector<ResultCallback> callbacks_;

    std::size_t total_sent_ = 0;
    std::size_t total_success_ = 0;
    std::size_t total_failed_ = 0;
    std::uint64_t next_result_id_ = 1;
};

} // namespace webhook
} // namespace changeos