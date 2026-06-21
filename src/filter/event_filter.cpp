#include "event_filter.h"

#include "utils/logger.h"

#include <algorithm>

namespace changeos {
namespace filter {

EventFilter::EventFilter() = default;
EventFilter::~EventFilter() = default;

void EventFilter::add_rule(const FilterRule& rule) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Remove existing rule with same name
    auto it = std::find_if(rules_.begin(), rules_.end(),
        [&](const FilterRule& r) { return r.name == rule.name; });
    if (it != rules_.end()) {
        *it = rule;
    } else {
        rules_.push_back(rule);
    }
    // Sort by priority (higher first)
    std::sort(rules_.begin(), rules_.end(),
        [](const FilterRule& a, const FilterRule& b) {
            return a.priority > b.priority;
        });
    COS_LOG_INFO("Filter rule added: " + rule.name);
}

void EventFilter::remove_rule(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    rules_.erase(
        std::remove_if(rules_.begin(), rules_.end(),
            [&](const FilterRule& r) { return r.name == name; }),
        rules_.end());
}

void EventFilter::enable_rule(const std::string& name, bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(rules_.begin(), rules_.end(),
        [&](const FilterRule& r) { return r.name == name; });
    if (it != rules_.end()) {
        it->enabled = enabled;
    }
}

void EventFilter::clear_rules() {
    std::lock_guard<std::mutex> lock(mutex_);
    rules_.clear();
}

std::vector<FilterRule> EventFilter::get_rules() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rules_;
}

bool EventFilter::process(Event& event) {
    total_processed_++;

    std::vector<FilterRule> rules_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        rules_copy = rules_;
    }

    for (const auto& rule : rules_copy) {
        if (!rule.enabled) continue;

        if (matches_rule(rule, event)) {
            switch (rule.action) {
                case FilterAction::Allow:
                    total_allowed_++;
                    return true;

                case FilterAction::Deny:
                    total_denied_++;
                    COS_LOG_DEBUG("Event denied by rule: " + rule.name);
                    return false;

                case FilterAction::Modify:
                    apply_modifications(rule, event);
                    total_modified_++;
                    // Continue processing other rules
                    break;
            }
        }
    }

    // Default: allow if no rules matched
    total_allowed_++;
    return true;
}

bool EventFilter::would_allow(const Event& event) const {
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& rule : rules_) {
        if (!rule.enabled) continue;

        if (matches_rule(rule, event)) {
            return rule.action != FilterAction::Deny;
        }
    }

    return true;
}

bool EventFilter::matches_rule(const FilterRule& rule, const Event& event) const {
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

    // Check attribute patterns
    for (const auto& [key, pattern] : rule.attribute_patterns) {
        auto it = event.attributes.find(key);
        if (it == event.attributes.end()) {
            return false;
        }
        try {
            std::regex re(pattern, std::regex::ECMAScript | std::regex::icase);
            if (!std::regex_search(it->second, re)) {
                return false;
            }
        } catch (const std::regex_error&) {
            // Invalid regex, skip
        }
    }

    // Check custom match
    if (rule.custom_match) {
        if (!rule.custom_match(event)) {
            return false;
        }
    }

    return true;
}

void EventFilter::apply_modifications(const FilterRule& rule, Event& event) {
    // Apply attribute modifications
    for (const auto& [key, value] : rule.attribute_modifications) {
        event.attributes[key] = value;
    }

    // Apply summary replacement
    if (!rule.summary_replacement.empty()) {
        event.summary = rule.summary_replacement;
    }
}

std::size_t EventFilter::total_processed() const {
    return total_processed_.load();
}

std::size_t EventFilter::total_allowed() const {
    return total_allowed_.load();
}

std::size_t EventFilter::total_denied() const {
    return total_denied_.load();
}

std::size_t EventFilter::total_modified() const {
    return total_modified_.load();
}

void EventFilter::reset_stats() {
    total_processed_ = 0;
    total_allowed_ = 0;
    total_denied_ = 0;
    total_modified_ = 0;
}

// Predefined filter rules
namespace rules {

FilterRule ignore_temp_files() {
    FilterRule rule;
    rule.name = "ignore_temp_files";
    rule.description = "Ignore file events in /tmp directory";
    rule.action = FilterAction::Deny;
    rule.priority = 10;
    rule.categories = {EventCategory::Filesystem};
    rule.target_pattern = "^/tmp/.*";
    return rule;
}

FilterRule ignore_log_files() {
    FilterRule rule;
    rule.name = "ignore_log_files";
    rule.description = "Ignore file events for log files";
    rule.action = FilterAction::Deny;
    rule.priority = 10;
    rule.categories = {EventCategory::Filesystem};
    rule.target_pattern = "\\.(log|log\\.[0-9]+)$";
    return rule;
}

FilterRule ignore_system_processes() {
    FilterRule rule;
    rule.name = "ignore_system_processes";
    rule.description = "Ignore events from common system processes";
    rule.action = FilterAction::Deny;
    rule.priority = 5;
    rule.categories = {EventCategory::Process};
    rule.source_pattern = "^(systemd|kworker|ksoftirqd|migration|rcu_|watchdog)";
    return rule;
}

FilterRule ignore_localhost_connections() {
    FilterRule rule;
    rule.name = "ignore_localhost_connections";
    rule.description = "Ignore localhost network connections";
    rule.action = FilterAction::Deny;
    rule.priority = 5;
    rule.categories = {EventCategory::Network};
    rule.target_pattern = "^(127\\.|::1|localhost)";
    return rule;
}

FilterRule only_critical_events() {
    FilterRule rule;
    rule.name = "only_critical_events";
    rule.description = "Only allow critical event types";
    rule.action = FilterAction::Allow;
    rule.priority = 100;
    rule.types = {
        EventType::FileDeleted,
        EventType::FilePermissionChanged,
        EventType::ConfigValueChanged,
        EventType::UserLoggedIn,
        EventType::UserLoggedOut
    };
    return rule;
}

FilterRule ignore_browser_cache() {
    FilterRule rule;
    rule.name = "ignore_browser_cache";
    rule.description = "Ignore browser cache directory changes";
    rule.action = FilterAction::Deny;
    rule.priority = 10;
    rule.categories = {EventCategory::Filesystem};
    rule.target_pattern = "(Cache|cache|\\.cache|\\.mozilla|\\.chrome|\\.config/google-chrome)";
    return rule;
}

} // namespace rules

} // namespace filter
} // namespace changeos
