#pragma once

#include "core/event.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <vector>

namespace changeos {
namespace alert {

enum class AlertSeverity {
    Info,
    Warning,
    Critical,
    Emergency
};

std::string severity_name(AlertSeverity s);

struct Alert {
    std::uint64_t id = 0;
    Timestamp timestamp = now();
    std::string rule_name;
    std::string message;
    AlertSeverity severity = AlertSeverity::Warning;
    Event trigger_event;
    bool acknowledged = false;
    std::map<std::string, std::string> metadata;
};

struct AlertRule {
    std::string name;
    std::string description;
    bool enabled = true;
    AlertSeverity severity = AlertSeverity::Warning;

    // Filter conditions
    std::vector<EventCategory> categories;
    std::vector<EventType> types;
    std::string source_pattern;  // regex pattern
    std::string target_pattern;  // regex pattern
    std::string summary_pattern; // regex pattern

    // Threshold conditions
    int threshold_count = 1;           // trigger after N events
    int threshold_window_ms = 60000;   // within this time window (ms)

    // Cooldown to prevent alert spam
    int cooldown_ms = 300000;  // 5 minutes default

    // Custom condition function
    std::function<bool(const Event&)> custom_condition;

    std::string message_template;  // {{source}}, {{target}}, {{summary}}
};

class AlertManager {
public:
    using AlertCallback = std::function<void(const Alert&)>;

    AlertManager();
    ~AlertManager();

    // Rule management
    void add_rule(const AlertRule& rule);
    void remove_rule(const std::string& name);
    void enable_rule(const std::string& name, bool enabled = true);
    std::vector<AlertRule> get_rules() const;
    void clear_rules();

    // Event processing
    void process_event(const Event& event);

    // Alert management
    std::vector<Alert> get_active_alerts() const;
    std::vector<Alert> get_alerts(std::int64_t from_ms = 0, std::int64_t to_ms = 0) const;
    void acknowledge_alert(std::uint64_t alert_id);
    void clear_alerts();

    // Callbacks
    void on_alert(AlertCallback cb);

    // Statistics
    std::size_t total_alerts() const;
    std::size_t active_alerts_count() const;

private:
    bool matches_rule(const AlertRule& rule, const Event& event);
    void trigger_alert(const AlertRule& rule, const Event& event);
    std::string format_message(const std::string& tmpl, const Event& event);
    bool is_in_cooldown(const std::string& rule_name);

    mutable std::mutex mutex_;
    std::vector<AlertRule> rules_;
    std::vector<Alert> alerts_;
    std::vector<AlertCallback> callbacks_;

    // Tracking for threshold and cooldown
    std::map<std::string, std::vector<Timestamp>> rule_events_;
    std::map<std::string, Timestamp> last_triggered_;

    std::uint64_t next_alert_id_ = 1;
};

// Predefined alert rules factory
namespace rules {

AlertRule high_cpu_usage();
AlertRule suspicious_process();
AlertRule critical_file_change();
AlertRule network_anomaly();
AlertRule config_tampering();
AlertRule rapid_file_changes();

} // namespace rules

} // namespace alert
} // namespace changeos
