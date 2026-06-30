#include "validate/config_validator.h"

#include "core/event.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace changeos {
namespace validate {

namespace {

std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(start, end - start);
}

// 已知的所有合法配置键及其期望类型。
// 类型用于值校验：bool / int / double / path / string / list
struct KeySpec {
    std::string type;
    std::string default_value;
    bool required = false;
};

const std::map<std::string, KeySpec>& known_keys() {
    static const std::map<std::string, KeySpec> k = {
        // filesystem
        {"filesystem.enabled",               {"bool",   "true"}},
        {"filesystem.poll_interval_ms",      {"int",    "2000"}},
        {"filesystem.use_native",            {"bool",   "true"}},
        {"filesystem.watch_paths",           {"list",   "/etc,/tmp"}},
        // process
        {"process.enabled",                  {"bool",   "true"}},
        {"process.poll_interval_ms",         {"int",    "3000"}},
        {"process.track_cpu",                {"bool",   "true"}},
        {"process.track_memory",             {"bool",   "true"}},
        // network
        {"network.enabled",                  {"bool",   "true"}},
        {"network.poll_interval_ms",         {"int",    "5000"}},
        // system_config
        {"system_config.enabled",            {"bool",   "true"}},
        {"system_config.poll_interval_ms",   {"int",    "10000"}},
        // user_activity
        {"user_activity.enabled",            {"bool",   "true"}},
        {"user_activity.poll_interval_ms",   {"int",    "5000"}},
        // service
        {"service.enabled",                  {"bool",   "true"}},
        {"service.poll_interval_ms",         {"int",    "10000"}},
        // file_integrity
        {"file_integrity.enabled",           {"bool",   "true"}},
        {"file_integrity.poll_interval_ms",  {"int",    "30000"}},
        {"file_integrity.watch_files",       {"list",   ""}},
        // usb_device
        {"usb_device.enabled",               {"bool",   "true"}},
        {"usb_device.poll_interval_ms",      {"int",    "5000"}},
        // disk_space
        {"disk_space.enabled",               {"bool",   "true"}},
        {"disk_space.poll_interval_ms",      {"int",    "30000"}},
        {"disk_space.warning_threshold",     {"double", "80.0"}},
        {"disk_space.critical_threshold",    {"double", "95.0"}},
        {"disk_space.watch_paths",           {"list",   ""}},
        // system_load
        {"system_load.enabled",              {"bool",   "true"}},
        {"system_load.poll_interval_ms",     {"int",    "5000"}},
        {"system_load.load_threshold",       {"double", "5.0"}},
        {"system_load.cpu_threshold",        {"double", "90.0"}},
        // log
        {"log.enabled",                      {"bool",   "true"}},
        {"log.poll_interval_ms",             {"int",    "5000"}},
        {"log.watch_paths",                  {"list",   ""}},
        // port
        {"port.enabled",                     {"bool",   "true"}},
        {"port.poll_interval_ms",            {"int",    "5000"}},
        {"port.watch_ports",                 {"list",   ""}},
        // package
        {"package.enabled",                  {"bool",   "true"}},
        {"package.poll_interval_ms",         {"int",    "60000"}},
        // environment
        {"environment.enabled",              {"bool",   "true"}},
        {"environment.poll_interval_ms",     {"int",    "10000"}},
        {"environment.watch_variables",      {"list",   ""}},
        // storage
        {"storage.database_path",            {"string", "change-of-system.db"}},
        {"storage.max_events",               {"int",    "100000"}},
        {"storage.max_size_mb",              {"int",    "0"}},
        {"storage.rotate_count",             {"int",    "5"}},
        // reporting
        {"reporting.enabled",                {"bool",   "false"}},
        {"reporting.endpoint",               {"string", ""}},
        {"reporting.api_key",                {"string", ""}},
        {"reporting.batch_size",             {"int",    "100"}},
        {"reporting.interval_ms",            {"int",    "10000"}},
        // alert
        {"alert.enabled",                    {"bool",   "true"}},
        // filter
        {"filter.enabled",                   {"bool",   "false"}},
        // security
        {"security.enabled",                 {"bool",   "true"}},
        // webhook
        {"webhook.enabled",                  {"bool",   "false"}},
        {"webhook.url",                      {"string", ""}},
        {"webhook.secret",                   {"string", ""}},
        {"webhook.timeout_ms",               {"int",    "5000"}},
        {"webhook.retry_count",              {"int",    "3"}},
        // config watch
        {"config.watch_enabled",             {"bool",   "false"}},
        {"config.watch_interval_ms",         {"int",    "5000"}},
        // snapshot
        {"snapshot.max_processes",           {"int",    "50"}},
        {"snapshot.include_processes",       {"bool",   "true"}},
        {"snapshot.include_network",         {"bool",   "true"}},
        {"snapshot.include_ports",           {"bool",   "true"}},
        {"snapshot.include_disk",            {"bool",   "true"}},
        {"snapshot.include_load",            {"bool",   "true"}},
        {"snapshot.include_environment",     {"bool",   "true"}},
    };
    return k;
}

bool is_valid_bool(const std::string& v) {
    std::string s;
    s.reserve(v.size());
    for (char c : v) s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s == "true" || s == "false" || s == "1" || s == "0" ||
           s == "yes" || s == "no" || s == "on" || s == "off";
}

bool is_valid_int(const std::string& v) {
    if (v.empty()) return false;
    size_t i = 0;
    if (v[0] == '+' || v[0] == '-') i = 1;
    if (i == v.size()) return false;
    for (; i < v.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(v[i]))) return false;
    }
    return true;
}

bool is_valid_double(const std::string& v) {
    if (v.empty()) return false;
    try {
        std::stod(v);
        return true;
    } catch (...) {
        return false;
    }
}

bool is_valid_port_list(const std::string& v) {
    // 允许空（即不限端口）
    if (v.empty()) return true;
    std::stringstream ss(v);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        std::string t = trim(tok);
        if (t.empty()) continue;
        try {
            int p = std::stoi(t);
            if (p < 0 || p > 65535) return false;
        } catch (...) {
            return false;
        }
    }
    return true;
}

bool validate_value(const std::string& key, const std::string& type,
                    const std::string& value, std::string& err) {
    if (type == "bool") {
        if (!is_valid_bool(value)) { err = "期望布尔值 (true/false/yes/no/on/off/1/0)"; return false; }
    } else if (type == "int") {
        if (!is_valid_int(value)) { err = "期望整数"; return false; }
    } else if (type == "double") {
        if (!is_valid_double(value)) { err = "期望数值"; return false; }
    } else if (type == "list") {
        if (key == "port.watch_ports") {
            if (!is_valid_port_list(value)) { err = "端口号必须在 0-65535 之间"; return false; }
        }
        // 其他 list 不做额外校验
    } else if (type == "path") {
        // 不强制要求路径存在（启动时可能尚未创建），仅做软提示
    }
    // string：不校验
    return true;
}

// 额外的语义校验：阈值关系、范围合理性等。
void semantic_checks(const std::map<std::string, std::string>& values,
                     ValidationResult& result) {
    // disk_space: warning 应小于 critical
    if (values.count("disk_space.warning_threshold") &&
        values.count("disk_space.critical_threshold")) {
        try {
            double w = std::stod(values.at("disk_space.warning_threshold"));
            double c = std::stod(values.at("disk_space.critical_threshold"));
            if (w >= c) {
                result.issues.push_back({ValidationIssue::Severity::Error,
                    "disk_space.warning_threshold",
                    "警告阈值 (" + std::to_string(w) + ") 应小于 critical 阈值 (" + std::to_string(c) + ")"});
                ++result.error_count;
            } else if (w < 0 || w > 100 || c < 0 || c > 100) {
                result.issues.push_back({ValidationIssue::Severity::Warning,
                    "disk_space",
                    "磁盘阈值通常应在 0-100 之间"});
                ++result.warning_count;
            }
        } catch (...) {}
    }

    // system_load: load_threshold 应为正
    if (values.count("system_load.load_threshold")) {
        try {
            double lt = std::stod(values.at("system_load.load_threshold"));
            if (lt <= 0) {
                result.issues.push_back({ValidationIssue::Severity::Warning,
                    "system_load.load_threshold",
                    "load 阈值非正数，可能频繁触发告警"});
                ++result.warning_count;
            }
        } catch (...) {}
    }

    // poll_interval 应为正
    static const std::vector<std::string> interval_keys = {
        "filesystem.poll_interval_ms", "process.poll_interval_ms",
        "network.poll_interval_ms", "system_config.poll_interval_ms",
        "user_activity.poll_interval_ms", "service.poll_interval_ms",
        "file_integrity.poll_interval_ms", "usb_device.poll_interval_ms",
        "disk_space.poll_interval_ms", "system_load.poll_interval_ms",
        "log.poll_interval_ms", "port.poll_interval_ms",
        "package.poll_interval_ms", "environment.poll_interval_ms",
        "config.watch_interval_ms", "reporting.interval_ms",
        "webhook.timeout_ms"
    };
    for (const auto& k : interval_keys) {
        auto it = values.find(k);
        if (it == values.end()) continue;
        try {
            int v = std::stoi(it->second);
            if (v < 0) {
                result.issues.push_back({ValidationIssue::Severity::Error, k,
                    "轮询间隔不能为负数"});
                ++result.error_count;
            } else if (v > 0 && v < 100) {
                result.issues.push_back({ValidationIssue::Severity::Warning, k,
                    "轮询间隔 < 100ms 可能造成 CPU 占用过高"});
                ++result.warning_count;
            }
        } catch (...) {}
    }

    // reporting.enabled = true 但 endpoint 为空 -> 警告
    auto re_it = values.find("reporting.enabled");
    if (re_it != values.end()) {
        std::string s = re_it->second;
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        bool enabled = (s == "true" || s == "1" || s == "yes" || s == "on");
        if (enabled) {
            std::string ep = values.count("reporting.endpoint") ? values.at("reporting.endpoint") : "";
            if (ep.empty() || ep == "https://example.com/api/events") {
                result.issues.push_back({ValidationIssue::Severity::Warning,
                    "reporting.endpoint",
                    "reporting 已启用但 endpoint 未设置或仍为示例值"});
                ++result.warning_count;
            }
        }
    }

    // webhook.enabled = true 但 url 为空 -> 错误
    auto we_it = values.find("webhook.enabled");
    if (we_it != values.end()) {
        std::string s = we_it->second;
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        bool enabled = (s == "true" || s == "1" || s == "yes" || s == "on");
        if (enabled) {
            std::string url = values.count("webhook.url") ? values.at("webhook.url") : "";
            if (url.empty()) {
                result.issues.push_back({ValidationIssue::Severity::Error,
                    "webhook.url",
                    "webhook 已启用但 url 为空"});
                ++result.error_count;
            } else if (url.find("http://") != 0 && url.find("https://") != 0) {
                result.issues.push_back({ValidationIssue::Severity::Error,
                    "webhook.url",
                    "webhook.url 必须以 http:// 或 https:// 开头"});
                ++result.error_count;
            }
        }
    }

    // storage.max_size_mb 与 rotate_count 配合检查
    if (values.count("storage.max_size_mb")) {
        try {
            int ms = std::stoi(values.at("storage.max_size_mb"));
            if (ms > 0) {
                int rc = values.count("storage.rotate_count")
                    ? std::stoi(values.at("storage.rotate_count")) : 5;
                if (rc <= 0) {
                    result.issues.push_back({ValidationIssue::Severity::Warning,
                        "storage.rotate_count",
                        "已启用日志轮转但 rotate_count <= 0，将不保留任何备份"});
                    ++result.warning_count;
                }
            }
        } catch (...) {}
    }
}

} // namespace

ValidationResult ConfigValidator::validate(const std::string& config_path) {
    ValidationResult result;
    std::ifstream file(config_path);
    if (!file) {
        result.file_exists = false;
        result.issues.push_back({ValidationIssue::Severity::Error, "",
            "无法打开配置文件: " + config_path});
        ++result.error_count;
        return result;
    }
    result.file_exists = true;

    std::map<std::string, std::string> values;
    std::vector<std::pair<int, std::string>> raw_lines; // 行号 -> 原始行
    std::string line;
    int line_no = 0;
    while (std::getline(file, line)) {
        ++line_no;
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') continue;
        auto eq = trimmed.find('=');
        if (eq == std::string::npos) {
            result.issues.push_back({ValidationIssue::Severity::Error, "",
                "第 " + std::to_string(line_no) + " 行: 缺少 '=' 分隔符: " + trimmed});
            ++result.error_count;
            continue;
        }
        std::string key = trim(trimmed.substr(0, eq));
        std::string value = trim(trimmed.substr(eq + 1));
        if (key.empty()) {
            result.issues.push_back({ValidationIssue::Severity::Error, "",
                "第 " + std::to_string(line_no) + " 行: 键为空"});
            ++result.error_count;
            continue;
        }
        if (values.count(key)) {
            result.issues.push_back({ValidationIssue::Severity::Warning, key,
                "第 " + std::to_string(line_no) + " 行: 键 '" + key + "' 重复定义，后者将覆盖前者"});
            ++result.warning_count;
        }
        values[key] = value;
        raw_lines.emplace_back(line_no, key);
    }

    result.parsed_keys = static_cast<int>(values.size());

    // 校验每个键：是否已知、值是否符合类型
    const auto& specs = known_keys();
    for (const auto& [key, value] : values) {
        auto it = specs.find(key);
        if (it == specs.end()) {
            result.issues.push_back({ValidationIssue::Severity::Info, key,
                "未知配置键 '" + key + "'（将被忽略，可能是拼写错误）"});
            ++result.info_count;
            continue;
        }
        std::string err;
        if (!validate_value(key, it->second.type, value, err)) {
            result.issues.push_back({ValidationIssue::Severity::Error, key,
                "键 '" + key + "' 的值 '" + value + "' 无效: " + err});
            ++result.error_count;
        }
    }

    // 语义校验
    semantic_checks(values, result);

    return result;
}

int ConfigValidator::print_report(const ValidationResult& result,
                                  const std::string& config_path) {
    std::cout << "==============================================================\n";
    std::cout << " change-of-system 配置文件验证报告\n";
    std::cout << "==============================================================\n";
    std::cout << " 文件: " << config_path << "\n";
    std::cout << " 存在: " << (result.file_exists ? "是" : "否") << "\n";
    std::cout << " 已解析键: " << result.parsed_keys << "\n";
    std::cout << "--------------------------------------------------------------\n";

    if (result.issues.empty()) {
        std::cout << " 未发现任何问题。\n";
    } else {
        for (const auto& issue : result.issues) {
            const char* tag = "INFO";
            const char* color = "\033[37m";
            switch (issue.severity) {
                case ValidationIssue::Severity::Error:
                    tag = "ERROR"; color = "\033[31m"; break;
                case ValidationIssue::Severity::Warning:
                    tag = "WARN "; color = "\033[33m"; break;
                case ValidationIssue::Severity::Info:
                    tag = "INFO "; color = "\033[36m"; break;
            }
            std::cout << color << " [" << tag << "] " << "\033[0m";
            if (!issue.key.empty()) std::cout << issue.key << ": ";
            std::cout << issue.message << "\n";
        }
    }

    std::cout << "--------------------------------------------------------------\n";
    std::cout << " 错误: " << result.error_count
              << "  警告: " << result.warning_count
              << "  提示: " << result.info_count << "\n";
    std::cout << " 结果: " << (result.ok() ? "\033[32m通过\033[0m" : "\033[31m未通过\033[0m")
              << "\n";
    std::cout << "==============================================================\n";
    return result.error_count;
}

} // namespace validate
} // namespace changeos
