#include "security_auditor.h"
#include "utils/logger.h"

#include <algorithm>
#include <set>
#include <sstream>

namespace changeos {
namespace security {

std::string security_event_type_name(SecurityEventType t) {
    switch (t) {
        case SecurityEventType::FailedLogin: return "failed_login";
        case SecurityEventType::SuccessfulLogin: return "successful_login";
        case SecurityEventType::BruteForceAttempt: return "brute_force_attempt";
        case SecurityEventType::SuspiciousLoginLocation: return "suspicious_login_location";
        case SecurityEventType::PrivilegeEscalation: return "privilege_escalation";
        case SecurityEventType::SudoUsage: return "sudo_usage";
        case SecurityEventType::RootAccess: return "root_access";
        case SecurityEventType::SensitiveFileAccess: return "sensitive_file_access";
        case SecurityEventType::PermissionChange: return "permission_change";
        case SecurityEventType::SuidBitSet: return "suid_bit_set";
        case SecurityEventType::PortScan: return "port_scan";
        case SecurityEventType::SuspiciousOutbound: return "suspicious_outbound";
        case SecurityEventType::FirewallChange: return "firewall_change";
        case SecurityEventType::SuspiciousProcess: return "suspicious_process";
        case SecurityEventType::ProcessInjection: return "process_injection";
        case SecurityEventType::HiddenProcess: return "hidden_process";
        case SecurityEventType::UserCreated: return "user_created";
        case SecurityEventType::UserDeleted: return "user_deleted";
        case SecurityEventType::GroupMembershipChange: return "group_membership_change";
        case SecurityEventType::PasswordChange: return "password_change";
        case SecurityEventType::KernelModuleLoad: return "kernel_module_load";
        case SecurityEventType::ServiceStarted: return "service_started";
        case SecurityEventType::ServiceStopped: return "service_stopped";
        default: return "unknown";
    }
}

SecurityAuditor::SecurityAuditor() {
    // Add default rules
    add_rule(rules::failed_login_threshold());
    add_rule(rules::privilege_escalation());
    add_rule(rules::sensitive_file_access());
    add_rule(rules::suspicious_process());
    add_rule(rules::network_anomaly());
    add_rule(rules::firewall_change());
    add_rule(rules::user_modification());
}

SecurityAuditor::~SecurityAuditor() = default;

void SecurityAuditor::add_rule(const SecurityRule& rule) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Remove existing rule with same name
    rules_.erase(
        std::remove_if(rules_.begin(), rules_.end(),
            [&rule](const SecurityRule& r) { return r.name == rule.name; }),
        rules_.end());
    rules_.push_back(rule);
}

void SecurityAuditor::remove_rule(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    rules_.erase(
        std::remove_if(rules_.begin(), rules_.end(),
            [&name](const SecurityRule& r) { return r.name == name; }),
        rules_.end());
}

void SecurityAuditor::enable_rule(const std::string& name, bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& rule : rules_) {
        if (rule.name == name) {
            rule.enabled = enabled;
            break;
        }
    }
}

std::vector<SecurityRule> SecurityAuditor::get_rules() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rules_;
}

void SecurityAuditor::clear_rules() {
    std::lock_guard<std::mutex> lock(mutex_);
    rules_.clear();
}

void SecurityAuditor::process_event(const Event& event) {
    switch (event.category) {
        case EventCategory::Process:
            analyze_process_event(event);
            break;
        case EventCategory::Filesystem:
            analyze_file_event(event);
            break;
        case EventCategory::Network:
            analyze_network_event(event);
            break;
        case EventCategory::SystemConfig:
            analyze_system_event(event);
            break;
        default:
            break;
    }
}

void SecurityAuditor::analyze_file_event(const Event& event) {
    // 敏感文件路径列表，使用精确匹配而非子串匹配
    // 以 '/' 结尾的路径使用前缀匹配（目录），其他使用精确匹配
    static const std::vector<std::string> sensitive_dirs = {
        "/root/", "/.ssh/", "/etc/cron."
    };
    static const std::vector<std::string> sensitive_files = {
        "/etc/passwd", "/etc/shadow", "/etc/sudoers",
        "/etc/ssh/sshd_config", "/etc/crontab", "/var/log/auth"
    };

    bool is_sensitive = false;

    // 目录前缀匹配（路径以敏感目录开头）
    for (const auto& dir : sensitive_dirs) {
        if (event.target.size() >= dir.size() &&
            event.target.compare(0, dir.size(), dir) == 0) {
            is_sensitive = true;
            break;
        }
    }

    // 精确文件路径匹配（或路径前缀 + '/' 表示子路径）
    if (!is_sensitive) {
        for (const auto& file : sensitive_files) {
            if (event.target == file ||
                (event.target.size() > file.size() &&
                 event.target.compare(0, file.size(), file) == 0 &&
                 event.target[file.size()] == '/')) {
                is_sensitive = true;
                break;
            }
        }
    }

    if (is_sensitive) {
        SecurityEvent se;
        se.timestamp = now();
        se.type = SecurityEventType::SensitiveFileAccess;
        se.source = event.source;
        se.target = event.target;
        se.summary = "Sensitive file access: " + event.target;
        se.severity = 3;
        se.details["event_type"] = type_name(event.type);
        se.details["original_summary"] = event.summary;
        report_security_event(se);
    }

    // Check for SUID bit changes
    if (event.type == EventType::FilePermissionChanged) {
        auto it = event.attributes.find("mode");
        if (it != event.attributes.end() &&
            it->second.find("SUID") != std::string::npos) {
            SecurityEvent se;
            se.timestamp = now();
            se.type = SecurityEventType::SuidBitSet;
            se.source = event.source;
            se.target = event.target;
            se.summary = "SUID bit set on file: " + event.target;
            se.severity = 3;
            report_security_event(se);
        }
    }
}

void SecurityAuditor::analyze_process_event(const Event& event) {
    // 可疑进程完整名称列表，使用完整进程名匹配而非子串匹配
    static const std::set<std::string> suspicious_names = {
        "nc", "ncat", "netcat", "nc.exe",
        "nmap", "masscan", "zmap",
        "meterpreter", "metasploit",
        "keylogger", "sniffer",
        "rootkit", "backdoor"
    };

    // 提取进程名（取路径的最后一部分），进行完整名称匹配
    std::string proc_name = event.target;
    auto last_slash = proc_name.find_last_of('/');
    if (last_slash != std::string::npos) {
        proc_name = proc_name.substr(last_slash + 1);
    }
    std::transform(proc_name.begin(), proc_name.end(), proc_name.begin(), ::tolower);

    // 使用精确匹配替代子串匹配，避免 "nc" 误匹配 "sync" 等进程
    if (suspicious_names.count(proc_name)) {
        SecurityEvent se;
        se.timestamp = now();
        se.type = SecurityEventType::SuspiciousProcess;
        se.source = event.source;
        se.target = event.target;
        se.summary = "Suspicious process detected: " + event.target;
        se.severity = 4;
        se.details["matched_pattern"] = proc_name;
        se.details["original_summary"] = event.summary;
        report_security_event(se);
    }

    // Check for privilege escalation
    if (event.type == EventType::ProcessStarted) {
        auto it = event.attributes.find("user");
        if (it != event.attributes.end() &&
            (it->second == "root" || it->second == "0")) {
            SecurityEvent se;
            se.timestamp = now();
            se.type = SecurityEventType::RootAccess;
            se.source = event.source;
            se.target = event.target;
            se.summary = "Process started as root: " + event.target;
            se.severity = 2;
            report_security_event(se);
        }
    }
}

void SecurityAuditor::analyze_network_event(const Event& event) {
    // Check for suspicious outbound connections
    static const std::vector<std::string> suspicious_ports = {
        "4444", "5555", "6666", "7777", "8888", "9999",
        "1234", "31337", "12345"
    };

    auto port_it = event.attributes.find("remote_port");
    if (port_it != event.attributes.end()) {
        for (const auto& port : suspicious_ports) {
            if (port_it->second == port) {
                SecurityEvent se;
                se.timestamp = now();
                se.type = SecurityEventType::SuspiciousOutbound;
                se.source = event.source;
                se.target = event.target;
                se.summary = "Suspicious outbound connection to port " + port;
                se.severity = 4;
                se.details["port"] = port;
                se.details["remote_address"] = event.attributes.count("remote_addr") ?
                    event.attributes.at("remote_addr") : "unknown";
                report_security_event(se);
                break;
            }
        }
    }
}

void SecurityAuditor::analyze_system_event(const Event& event) {
    // Check for firewall changes
    if (event.target.find("iptables") != std::string::npos ||
        event.target.find("firewall") != std::string::npos ||
        event.target.find("ufw") != std::string::npos ||
        event.target.find("firewalld") != std::string::npos) {
        SecurityEvent se;
        se.timestamp = now();
        se.type = SecurityEventType::FirewallChange;
        se.source = event.source;
        se.target = event.target;
        se.summary = "Firewall configuration changed: " + event.summary;
        se.severity = 3;
        report_security_event(se);
    }

    // Check for user modifications
    if (event.target.find("/etc/passwd") != std::string::npos ||
        event.target.find("/etc/shadow") != std::string::npos ||
        event.target.find("/etc/group") != std::string::npos) {
        SecurityEvent se;
        se.timestamp = now();
        se.type = SecurityEventType::UserCreated;
        se.source = event.source;
        se.target = event.target;
        se.summary = "User database modified: " + event.summary;
        se.severity = 3;
        report_security_event(se);
    }
}

void SecurityAuditor::report_security_event(const SecurityEvent& event) {
    std::vector<SecurityEventCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        SecurityEvent e = event;
        e.id = next_event_id_++;
        events_.push_back(e);

        // Keep only last 1000 events
        if (events_.size() > 1000) {
            events_.erase(events_.begin(), events_.begin() + (events_.size() - 1000));
        }

        callbacks = callbacks_;
        check_rules(e);
    }

    for (const auto& cb : callbacks) {
        cb(event);
    }

    COS_LOG_INFO("[Security] " + security_event_type_name(event.type) + ": " + event.summary);
}

void SecurityAuditor::check_rules(const SecurityEvent& event) {
    auto now_time = std::chrono::system_clock::now();

    for (const auto& rule : rules_) {
        if (!rule.enabled) continue;

        // Check event type match
        bool type_match = rule.event_types.empty() ||
            std::find(rule.event_types.begin(), rule.event_types.end(), event.type) !=
                rule.event_types.end();

        if (!type_match) continue;

        // Check custom condition
        if (rule.condition && !rule.condition(event)) continue;

        // Check cooldown
        if (is_in_cooldown(rule.name)) continue;

        // Record event for threshold
        record_rule_event(rule.name);

        // Check threshold
        auto& event_times = rule_events_[rule.name];
        int count = 0;
        for (const auto& t : event_times) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now_time - t).count();
            if (elapsed < rule.threshold_window_ms) {
                count++;
            }
        }

        if (count >= rule.threshold_count) {
            COS_LOG_WARN("[Security] Rule triggered: " + rule.name + " - " + event.summary);
            last_triggered_[rule.name] = now_time;
        }
    }
}

bool SecurityAuditor::is_in_cooldown(const std::string& rule_name) {
    auto it = last_triggered_.find(rule_name);
    if (it == last_triggered_.end()) return false;

    // Find the rule's cooldown
    for (const auto& rule : rules_) {
        if (rule.name == rule_name) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now() - it->second).count();
            return elapsed < rule.cooldown_ms;
        }
    }
    return false;
}

void SecurityAuditor::record_rule_event(const std::string& rule_name) {
    auto now_time = std::chrono::system_clock::now();
    rule_events_[rule_name].push_back(now_time);

    // Find the rule's window
    int window_ms = 60000;  // default 1 minute
    for (const auto& rule : rules_) {
        if (rule.name == rule_name) {
            window_ms = rule.threshold_window_ms;
            break;
        }
    }

    // Clean old events
    auto& times = rule_events_[rule_name];
    times.erase(
        std::remove_if(times.begin(), times.end(),
            [now_time, window_ms](const std::chrono::system_clock::time_point& t) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now_time - t).count();
                return elapsed > window_ms;
            }),
        times.end());
}

void SecurityAuditor::on_security_event(SecurityEventCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_.push_back(std::move(cb));
}

std::size_t SecurityAuditor::total_security_events() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return events_.size();
}

std::vector<SecurityEvent> SecurityAuditor::get_recent_events(std::size_t limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (events_.size() <= limit) {
        return events_;
    }
    return std::vector<SecurityEvent>(events_.end() - limit, events_.end());
}

// Predefined rules
namespace rules {

SecurityRule failed_login_threshold() {
    SecurityRule rule;
    rule.name = "failed_login_threshold";
    rule.description = "Alert on multiple failed login attempts";
    rule.severity = 3;
    rule.event_types = {SecurityEventType::FailedLogin};
    rule.threshold_count = 5;
    rule.threshold_window_ms = 60000;  // 1 minute
    rule.cooldown_ms = 300000;  // 5 minutes
    return rule;
}

SecurityRule privilege_escalation() {
    SecurityRule rule;
    rule.name = "privilege_escalation";
    rule.description = "Alert on privilege escalation events";
    rule.severity = 4;
    rule.event_types = {SecurityEventType::PrivilegeEscalation, SecurityEventType::RootAccess};
    rule.threshold_count = 1;
    rule.threshold_window_ms = 60000;
    rule.cooldown_ms = 300000;
    return rule;
}

SecurityRule sensitive_file_access() {
    SecurityRule rule;
    rule.name = "sensitive_file_access";
    rule.description = "Alert on sensitive file access";
    rule.severity = 3;
    rule.event_types = {SecurityEventType::SensitiveFileAccess};
    rule.threshold_count = 1;
    rule.threshold_window_ms = 60000;
    rule.cooldown_ms = 300000;
    return rule;
}

SecurityRule suspicious_process() {
    SecurityRule rule;
    rule.name = "suspicious_process";
    rule.description = "Alert on suspicious process detection";
    rule.severity = 4;
    rule.event_types = {SecurityEventType::SuspiciousProcess};
    rule.threshold_count = 1;
    rule.threshold_window_ms = 60000;
    rule.cooldown_ms = 300000;
    return rule;
}

SecurityRule network_anomaly() {
    SecurityRule rule;
    rule.name = "network_anomaly";
    rule.description = "Alert on network security anomalies";
    rule.severity = 3;
    rule.event_types = {SecurityEventType::SuspiciousOutbound, SecurityEventType::PortScan};
    rule.threshold_count = 1;
    rule.threshold_window_ms = 60000;
    rule.cooldown_ms = 300000;
    return rule;
}

SecurityRule firewall_change() {
    SecurityRule rule;
    rule.name = "firewall_change";
    rule.description = "Alert on firewall configuration changes";
    rule.severity = 3;
    rule.event_types = {SecurityEventType::FirewallChange};
    rule.threshold_count = 1;
    rule.threshold_window_ms = 60000;
    rule.cooldown_ms = 300000;
    return rule;
}

SecurityRule user_modification() {
    SecurityRule rule;
    rule.name = "user_modification";
    rule.description = "Alert on user database modifications";
    rule.severity = 3;
    rule.event_types = {SecurityEventType::UserCreated, SecurityEventType::UserDeleted,
                        SecurityEventType::GroupMembershipChange, SecurityEventType::PasswordChange};
    rule.threshold_count = 1;
    rule.threshold_window_ms = 60000;
    rule.cooldown_ms = 300000;
    return rule;
}

} // namespace rules

} // namespace security
} // namespace changeos