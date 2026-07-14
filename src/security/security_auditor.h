#pragma once

#include "core/event.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace changeos {
namespace security {

enum class SecurityEventType {
    // Authentication events
    FailedLogin,
    SuccessfulLogin,
    BruteForceAttempt,
    SuspiciousLoginLocation,

    // Privilege events
    PrivilegeEscalation,
    SudoUsage,
    RootAccess,

    // File security events
    SensitiveFileAccess,
    PermissionChange,
    SuidBitSet,

    // Network security events
    PortScan,
    SuspiciousOutbound,
    FirewallChange,

    // Process security events
    SuspiciousProcess,
    ProcessInjection,
    HiddenProcess,

    // User security events
    UserCreated,
    UserDeleted,
    GroupMembershipChange,
    PasswordChange,

    // System security events
    KernelModuleLoad,
    ServiceStarted,
    ServiceStopped,

    Unknown
};

std::string security_event_type_name(SecurityEventType t);

struct SecurityEvent {
    std::uint64_t id = 0;
    std::chrono::system_clock::time_point timestamp;
    SecurityEventType type = SecurityEventType::Unknown;
    std::string source;
    std::string target;
    std::string summary;
    int severity = 1;  // 1=info, 2=warning, 3=high, 4=critical
    std::map<std::string, std::string> details;
    std::string host;
    std::string platform;
};

struct SecurityRule {
    std::string name;
    std::string description;
    bool enabled = true;
    int severity = 2;
    std::vector<SecurityEventType> event_types;
    std::function<bool(const SecurityEvent&)> condition;
    int threshold_count = 1;
    int threshold_window_ms = 60000;
    int cooldown_ms = 300000;
};

class SecurityAuditor {
public:
    using SecurityEventCallback = std::function<void(const SecurityEvent&)>;

    SecurityAuditor();
    ~SecurityAuditor();

    // Rule management
    void add_rule(const SecurityRule& rule);
    void remove_rule(const std::string& name);
    void enable_rule(const std::string& name, bool enabled = true);
    std::vector<SecurityRule> get_rules() const;
    void clear_rules();

    // Event processing
    void process_event(const Event& event);
    void report_security_event(const SecurityEvent& event);

    // Callbacks
    void on_security_event(SecurityEventCallback cb);

    // Statistics
    std::size_t total_security_events() const;
    std::vector<SecurityEvent> get_recent_events(std::size_t limit = 100) const;

private:
    void analyze_file_event(const Event& event);
    void analyze_process_event(const Event& event);
    void analyze_network_event(const Event& event);
    void analyze_system_event(const Event& event);

    void check_rules(const SecurityEvent& event);
    bool is_in_cooldown(const std::string& rule_name);
    void record_rule_event(const std::string& rule_name);

    mutable std::mutex mutex_;
    std::vector<SecurityRule> rules_;
    std::vector<SecurityEvent> events_;
    std::vector<SecurityEventCallback> callbacks_;

    // Tracking for threshold and cooldown
    std::map<std::string, std::vector<std::chrono::system_clock::time_point>> rule_events_;
    std::map<std::string, std::chrono::system_clock::time_point> last_triggered_;

    std::uint64_t next_event_id_ = 1;
};

// Predefined security rules
namespace rules {

SecurityRule failed_login_threshold();
SecurityRule privilege_escalation();
SecurityRule sensitive_file_access();
SecurityRule suspicious_process();
SecurityRule network_anomaly();
SecurityRule firewall_change();
SecurityRule user_modification();

} // namespace rules

} // namespace security
} // namespace changeos