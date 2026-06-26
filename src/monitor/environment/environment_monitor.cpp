#include "monitor/environment/environment_monitor.h"
#include "utils/periodic_runner.h"

#include <cstdlib>
#include <cstring>
#include <chrono>

extern char** environ;

namespace changeos {
namespace monitor {

EnvironmentMonitor::EnvironmentMonitor() : track_all_(false) {
    // Default important environment variables to watch
    watch_variables_ = {
        "PATH", "HOME", "USER", "SHELL", "LANG", "LC_ALL",
        "LD_LIBRARY_PATH", "PYTHONPATH", "JAVA_HOME",
        "HTTP_PROXY", "HTTPS_PROXY", "NO_PROXY",
        "SSH_AUTH_SOCK", "GPG_AGENT_INFO"
    };
}

EnvironmentMonitor::~EnvironmentMonitor() {
    stop();
}

bool EnvironmentMonitor::on_start() {
    runner_ = std::make_unique<utils::PeriodicRunner>();
    runner_->start([this]() { poll(); },
                   std::chrono::milliseconds(poll_interval_ms_));
    return true;
}

bool EnvironmentMonitor::on_stop() {
    if (runner_) runner_->stop();
    runner_.reset();
    return true;
}

void EnvironmentMonitor::set_watch_variables(const std::set<std::string>& vars) {
    watch_variables_ = vars;
    track_all_ = vars.empty();
}

std::map<std::string, std::string> EnvironmentMonitor::snapshot_environment() {
    std::map<std::string, std::string> result;

    if (environ == nullptr) {
        return result;
    }

    for (char** env = environ; *env != nullptr; ++env) {
        std::string entry(*env);
        auto eq_pos = entry.find('=');
        if (eq_pos != std::string::npos) {
            std::string name = entry.substr(0, eq_pos);
            std::string value = entry.substr(eq_pos + 1);
            
            // If tracking all variables, or if this is in our watch list
            if (track_all_ || watch_variables_.find(name) != watch_variables_.end()) {
                result[name] = value;
            }
        }
    }

    return result;
}

void EnvironmentMonitor::poll() {
    static std::map<std::string, std::string> last_snapshot;

    auto current = snapshot_environment();

    // Detect new environment variables
    for (const auto& [name, value] : current) {
        if (last_snapshot.find(name) == last_snapshot.end()) {
            Event e;
            e.category = EventCategory::System;
            e.type = EventType::ConfigValueChanged;
            e.source = "environment_monitor";
            e.target = name;
            e.summary = "Environment variable set: " + name;
            e.attributes["action"] = "set";
            e.attributes["value"] = value;
            emit(e);
        }
    }

    // Detect removed environment variables
    for (const auto& [name, value] : last_snapshot) {
        if (current.find(name) == current.end()) {
            Event e;
            e.category = EventCategory::System;
            e.type = EventType::ConfigValueChanged;
            e.source = "environment_monitor";
            e.target = name;
            e.summary = "Environment variable unset: " + name;
            e.attributes["action"] = "unset";
            e.attributes["old_value"] = value;
            emit(e);
        }
    }

    // Detect changed environment variables
    for (const auto& [name, value] : current) {
        auto it = last_snapshot.find(name);
        if (it != last_snapshot.end() && it->second != value) {
            Event e;
            e.category = EventCategory::System;
            e.type = EventType::ConfigValueChanged;
            e.source = "environment_monitor";
            e.target = name;
            e.summary = "Environment variable changed: " + name;
            e.attributes["action"] = "changed";
            e.attributes["old_value"] = it->second;
            e.attributes["new_value"] = value;
            emit(e);
        }
    }

    last_snapshot = current;
}

} // namespace monitor
} // namespace changeos
