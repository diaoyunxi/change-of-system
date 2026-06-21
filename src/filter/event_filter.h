#pragma once

#include "core/event.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <vector>

namespace changeos {
namespace filter {

enum class FilterAction {
    Allow,      // Allow the event to pass through
    Deny,       // Block the event
    Modify      // Modify event attributes
};

struct FilterRule {
    std::string name;
    std::string description;
    bool enabled = true;
    FilterAction action = FilterAction::Allow;
    int priority = 0;  // Higher priority rules are evaluated first

    // Filter conditions (all must match for rule to apply)
    std::vector<EventCategory> categories;
    std::vector<EventType> types;
    std::string source_pattern;  // regex pattern
    std::string target_pattern;  // regex pattern
    std::string summary_pattern; // regex pattern

    // Attribute filters (key = attribute name, value = regex pattern)
    std::map<std::string, std::string> attribute_patterns;

    // Custom filter function
    std::function<bool(const Event&)> custom_match;

    // For Modify action
    std::map<std::string, std::string> attribute_modifications;
    std::string summary_replacement;
};

class EventFilter {
public:
    using EventModifier = std::function<void(Event&)>;

    EventFilter();
    ~EventFilter();

    // Rule management
    void add_rule(const FilterRule& rule);
    void remove_rule(const std::string& name);
    void enable_rule(const std::string& name, bool enabled = true);
    void clear_rules();
    std::vector<FilterRule> get_rules() const;

    // Process event through filter chain
    // Returns true if event should be allowed, false if denied
    bool process(Event& event);

    // Check if event would be allowed (without modifying)
    bool would_allow(const Event& event) const;

    // Statistics
    std::size_t total_processed() const;
    std::size_t total_allowed() const;
    std::size_t total_denied() const;
    std::size_t total_modified() const;
    void reset_stats();

private:
    bool matches_rule(const FilterRule& rule, const Event& event) const;
    void apply_modifications(const FilterRule& rule, Event& event);

    mutable std::mutex mutex_;
    std::vector<FilterRule> rules_;

    // Statistics
    std::atomic<std::size_t> total_processed_{0};
    std::atomic<std::size_t> total_allowed_{0};
    std::atomic<std::size_t> total_denied_{0};
    std::atomic<std::size_t> total_modified_{0};
};

// Predefined filter rules factory
namespace rules {

FilterRule ignore_temp_files();
FilterRule ignore_log_files();
FilterRule ignore_system_processes();
FilterRule ignore_localhost_connections();
FilterRule only_critical_events();
FilterRule ignore_browser_cache();

} // namespace rules

} // namespace filter
} // namespace changeos
