#include "alert_manager.h"

#include "utils/logger.h"

#include <algorithm>
#include <sstream>

namespace changeos {
namespace alert {

std::string severity_name(AlertSeverity s) {
    switch (s) {
        case AlertSeverity::Info: return "INFO";
        case AlertSeverity::Warning: return "WARNING";
        case AlertSeverity::Critical: return "CRITICAL";
        case AlertSeverity::Emergency: return "EMERGENCY";
        default: return "UNKNOWN";
    }
}

AlertManager::AlertManager() = default;
AlertManager::~AlertManager() = default;

void AlertManager::add_rule(const AlertRule& rule) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Remove existing rule with same name
    auto it = std::find_if(rules_.begin(), rules_.end(),
        [&](const AlertRule& r) { return r.name == rule.name; });
    if (it != rules_.end()) {
        *it = rule;
    } else {
        rules_.push_back(rule);
    }
    COS_LOG_INFO("Alert rule added: " + rule.name);
}

void AlertManager::remove_rule(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    rules_.erase(
        std::remove_if(rules_.begin(), rules_.end(),
            [&](const AlertRule& r) { return r.name == name; }),
        rules_.end());
    rule_events_.erase(name);
    last_triggered_.erase(name);
}

void AlertManager::enable_rule(const std::string& name, bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(rules_.begin(), rules_.end(),
        [&](const AlertRule& r) { return r.name == name; });
    if (it != rules_.end()) {
        it->enabled = enabled;
    }
}

std::vector<AlertRule> AlertManager::get_rules() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rules_;
}

void AlertManager::clear_rules() {
    std::lock_guard<std::mutex> lock(mutex_);
    rules_.clear();
    rule_events_.clear();
    last_triggered_.clear();
}

void AlertManager::process_event(const Event& event) {
    std::vector<AlertRule> rules_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        rules_copy = rules_;
    }

    for (const auto& rule : rules_copy) {
        if (!rule.enabled) continue;
        if (matches_rule(rule, event)) {
            // Track event for threshold
            {
                std::lock_guard<std::mutex> lock(mutex_);
                rule_events_[rule.name].push_back(event.timestamp);
            }

            // Check threshold
            bool should_trigger = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto& events = rule_events_[rule.name];
                Timestamp cutoff = now() - std::chrono::milliseconds(rule.threshold_window_ms);

                // Remove old events
                events.erase(
                    std::remove_if(events.begin(), events.end(),
                        [&](const Timestamp& t) { return t < cutoff; }),
                    events.end());

                if (static_cast<int>(events.size()) >= rule.threshold_count) {
                    should_trigger = true;
                }
            }

            if (should_trigger && !is_in_cooldown(rule.name)) {
                trigger_alert(rule, event);
            }
        }
    }
}

bool AlertManager::matches_rule(const AlertRule& rule, const Event& event) {
    // Check category
    if (!rule.categories.empty()) {
        if (std::find(rule.categories.begin(), rule.categories.end(),
                      event.category) == rule.categories.end()) {
            return false;
        }
    }

    // Check type
    if (!rule.types.empty()) {
        if (std::find(rule.types.begin(), rule.types.end(),
                      event.type) == rule.types.end()) {
            return false;
        }
    }

    // Check source pattern
    if (!rule.source_pattern.empty()) {
        try {
            std::regex re(rule.source_pattern, std::regex::ECMAScript | std::regex::icase);
            if (!std::regex_search(event.source, re)) {
                return false;
            }
        } catch (const std::regex_error&) {
            // Invalid regex, skip
        }
    }

    // Check target pattern
    if (!rule.target_pattern.empty()) {
        try {
            std::regex re(rule.target_pattern, std::regex::ECMAScript | std::regex::icase);
            if (!std::regex_search(event.target, re)) {
                return false;
            }
        } catch (const std::regex_error&) {
            // Invalid regex, skip
        }
    }

    // Check summary pattern
    if (!rule.summary_pattern.empty()) {
        try {
            std::regex re(rule.summary_pattern, std::regex::ECMAScript | std::regex::icase);
            if (!std::regex_search(event.summary, re)) {
                return false;
            }
        } catch (const std::regex_error&) {
            // Invalid regex, skip
        }
    }

    // Check custom condition
    if (rule.custom_condition) {
        if (!rule.custom_condition(event)) {
            return false;
        }
    }

    return true;
}

void AlertManager::trigger_alert(const AlertRule& rule, const Event& event) {
    Alert alert;
    alert.id = next_alert_id_++;
    alert.timestamp = now();
    alert.rule_name = rule.name;
    alert.severity = rule.severity;
    alert.trigger_event = event;
    alert.message = format_message(rule.message_template, event);

    std::vector<AlertCallback> callbacks_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        alerts_.push_back(alert);
        last_triggered_[rule.name] = alert.timestamp;
        callbacks_copy = callbacks_;
    }

    COS_LOG_WARN("ALERT [" + severity_name(rule.severity) + "] " +
                 rule.name + ": " + alert.message);

    for (auto& cb : callbacks_copy) {
        try { cb(alert); } catch (...) {}
    }
}

std::string AlertManager::format_message(const std::string& tmpl, const Event& event) {
    if (tmpl.empty()) {
        return "Alert triggered for event: " + event.summary;
    }

    std::string result = tmpl;
    size_t pos;

    // Replace placeholders
    while ((pos = result.find("{{source}}")) != std::string::npos) {
        result.replace(pos, 10, event.source);
    }
    while ((pos = result.find("{{target}}")) != std::string::npos) {
        result.replace(pos, 10, event.target);
    }
    while ((pos = result.find("{{summary}}")) != std::string::npos) {
        result.replace(pos, 11, event.summary);
    }
    while ((pos = result.find("{{category}}")) != std::string::npos) {
        result.replace(pos, 12, category_name(event.category));
    }
    while ((pos = result.find("{{type}}")) != std::string::npos) {
        result.replace(pos, 8, type_name(event.type));
    }

    return result;
}

bool AlertManager::is_in_cooldown(const std::string& rule_name) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = last_triggered_.find(rule_name);
    if (it == last_triggered_.end()) return false;

    // Find rule's cooldown
    auto rule_it = std::find_if(rules_.begin(), rules_.end(),
        [&](const AlertRule& r) { return r.name == rule_name; });
    if (rule_it == rules_.end()) return false;

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now() - it->second).count();

    return elapsed < rule_it->cooldown_ms;
}

std::vector<Alert> AlertManager::get_active_alerts() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Alert> result;
    for (const auto& a : alerts_) {
        if (!a.acknowledged) {
            result.push_back(a);
        }
    }
    return result;
}

std::vector<Alert> AlertManager::get_alerts(std::int64_t from_ms, std::int64_t to_ms) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Alert> result;

    for (const auto& a : alerts_) {
        auto ts = to_unix_ms(a.timestamp);
        if (from_ms > 0 && ts < from_ms) continue;
        if (to_ms > 0 && ts > to_ms) continue;
        result.push_back(a);
    }

    return result;
}

void AlertManager::acknowledge_alert(std::uint64_t alert_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& a : alerts_) {
        if (a.id == alert_id) {
            a.acknowledged = true;
            break;
        }
    }
}

void AlertManager::clear_alerts() {
    std::lock_guard<std::mutex> lock(mutex_);
    alerts_.clear();
}

void AlertManager::on_alert(AlertCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_.push_back(std::move(cb));
}

std::size_t AlertManager::total_alerts() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return alerts_.size();
}

std::size_t AlertManager::active_alerts_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::count_if(alerts_.begin(), alerts_.end(),
        [](const Alert& a) { return !a.acknowledged; });
}

// Predefined rules
namespace rules {

AlertRule high_cpu_usage() {
    AlertRule rule;
    rule.name = "high_cpu_usage";
    rule.description = "Triggered when a process uses high CPU";
    rule.severity = AlertSeverity::Warning;
    rule.types = {EventType::ProcessCpuSpike};
    rule.threshold_count = 3;
    rule.threshold_window_ms = 60000;
    rule.cooldown_ms = 300000;
    rule.message_template = "High CPU usage detected: {{source}} - {{summary}}";
    return rule;
}

AlertRule suspicious_process() {
    AlertRule rule;
    rule.name = "suspicious_process";
    rule.description = "Triggered when suspicious process activity is detected";
    rule.severity = AlertSeverity::Critical;
    rule.categories = {EventCategory::Process};
    rule.source_pattern = "(nc|netcat|ncat|nmap|wireshark|tcpdump)";
    rule.threshold_count = 1;
    rule.cooldown_ms = 600000;
    rule.message_template = "Suspicious process detected: {{source}}";
    return rule;
}

AlertRule critical_file_change() {
    AlertRule rule;
    rule.name = "critical_file_change";
    rule.description = "Triggered when critical system files are modified";
    rule.severity = AlertSeverity::Critical;
    rule.types = {EventType::FileModified, EventType::FileDeleted};
    rule.target_pattern = "(/etc/passwd|/etc/shadow|/etc/sudoers|/etc/ssh/sshd_config)";
    rule.threshold_count = 1;
    rule.cooldown_ms = 60000;
    rule.message_template = "Critical file changed: {{target}}";
    return rule;
}

AlertRule network_anomaly() {
    AlertRule rule;
    rule.name = "network_anomaly";
    rule.description = "Triggered when unusual network activity is detected";
    rule.severity = AlertSeverity::Warning;
    rule.categories = {EventCategory::Network};
    rule.types = {EventType::NetworkConnectionOpened};
    rule.threshold_count = 50;
    rule.threshold_window_ms = 10000;
    rule.cooldown_ms = 300000;
    rule.message_template = "Network anomaly: {{threshold_count}} connections in {{threshold_window_ms}}ms";
    return rule;
}

AlertRule config_tampering() {
    AlertRule rule;
    rule.name = "config_tampering";
    rule.description = "Triggered when system configuration is modified";
    rule.severity = AlertSeverity::Critical;
    rule.categories = {EventCategory::SystemConfig};
    rule.threshold_count = 1;
    rule.cooldown_ms = 60000;
    rule.message_template = "System configuration changed: {{summary}}";
    return rule;
}

AlertRule rapid_file_changes() {
    AlertRule rule;
    rule.name = "rapid_file_changes";
    rule.description = "Triggered when many file changes occur rapidly";
    rule.severity = AlertSeverity::Warning;
    rule.categories = {EventCategory::Filesystem};
    rule.threshold_count = 100;
    rule.threshold_window_ms = 5000;
    rule.cooldown_ms = 300000;
    rule.message_template = "Rapid file changes detected: {{threshold_count}} changes in {{threshold_window_ms}}ms";
    return rule;
}

} // namespace rules

} // namespace alert
} // namespace changeos
