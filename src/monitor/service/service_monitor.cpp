#include "monitor/service/service_monitor.h"

#include "platform/platform_detection.h"
#include "utils/logger.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace changeos {
namespace monitor {

std::string service_state_name(ServiceState state) {
    switch (state) {
        case ServiceState::Running: return "running";
        case ServiceState::Stopped: return "stopped";
        case ServiceState::Failed: return "failed";
        case ServiceState::Starting: return "starting";
        case ServiceState::Stopping: return "stopping";
        default: return "unknown";
    }
}

ServiceMonitor::ServiceMonitor() = default;
ServiceMonitor::~ServiceMonitor() { stop(); }

bool ServiceMonitor::is_available() const {
    return platform::has_native_service_monitor();
}

bool ServiceMonitor::on_start() {
    COS_LOG_INFO("Service monitor starting");
    first_scan_.store(true);
    runner_ = std::make_unique<utils::PeriodicRunner>();
    runner_->start([this]() { tick(); },
                   std::chrono::milliseconds(poll_interval_ms_));
    return true;
}

bool ServiceMonitor::on_stop() {
    if (runner_) runner_->stop();
    runner_.reset();
    COS_LOG_INFO("Service monitor stopped");
    return true;
}

std::vector<ServiceInfo> ServiceMonitor::snapshot() {
    std::vector<ServiceInfo> result;

#if defined(COS_PLATFORM_LINUX) || defined(COS_PLATFORM_ANDROID) || defined(COS_PLATFORM_CHROMEOS) || defined(COS_PLATFORM_FYDEOS)
    std::filesystem::path service_dir("/etc/systemd/system");
    if (!std::filesystem::exists(service_dir)) {
        service_dir = "/etc/init.d";
    }

    if (std::filesystem::exists(service_dir)) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(service_dir, ec)) {
            if (!entry.is_regular_file()) continue;

            std::string filename = entry.path().filename().string();
            if (filename.substr(0, 1) == ".") continue;

            ServiceInfo info;
            info.name = filename;

            std::ifstream file(entry.path());
            if (file) {
                std::string line;
                while (std::getline(file, line)) {
                    if (line.rfind("[Unit]", 0) == 0) {
                        continue;
                    } else if (line.rfind("Description=", 0) == 0) {
                        info.description = line.substr(12);
                    } else if (line.rfind("[Service]", 0) == 0) {
                        continue;
                    } else if (line.rfind("ExecStart=", 0) == 0) {
                        info.exec_start = line.substr(10);
                    } else if (line.rfind("[Install]", 0) == 0) {
                        continue;
                    } else if (line.rfind("WantedBy=", 0) == 0) {
                        info.enabled = true;
                    }
                }
            }

            std::filesystem::path status_file("/run/systemd/system/" + filename + ".wants");
            if (std::filesystem::exists(status_file)) {
                info.enabled = true;
            }

            std::string pid_file = "/var/run/" + filename;
            if (filename.size() > 8 && filename.substr(filename.size() - 8) == ".service") {
                pid_file = "/var/run/" + filename.substr(0, filename.size() - 8) + ".pid";
            }

            std::ifstream pid_stream(pid_file);
            if (pid_stream) {
                pid_stream >> info.pid;
                if (info.pid > 0) {
                    info.state = ServiceState::Running;
                } else {
                    info.state = ServiceState::Stopped;
                }
            } else {
                info.state = ServiceState::Stopped;
            }

            result.push_back(info);
        }
    }

    std::filesystem::path system_service_dir("/lib/systemd/system");
    if (std::filesystem::exists(system_service_dir)) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(system_service_dir, ec)) {
            if (!entry.is_regular_file()) continue;

            std::string filename = entry.path().filename().string();
            if (filename.substr(0, 1) == ".") continue;
            if (filename.find(".service") == std::string::npos) continue;

            bool already_exists = false;
            for (const auto& r : result) {
                if (r.name == filename) {
                    already_exists = true;
                    break;
                }
            }
            if (already_exists) continue;

            ServiceInfo info;
            info.name = filename;

            std::ifstream file(entry.path());
            if (file) {
                std::string line;
                while (std::getline(file, line)) {
                    if (line.rfind("[Unit]", 0) == 0) {
                        continue;
                    } else if (line.rfind("Description=", 0) == 0) {
                        info.description = line.substr(12);
                    } else if (line.rfind("[Service]", 0) == 0) {
                        continue;
                    } else if (line.rfind("ExecStart=", 0) == 0) {
                        info.exec_start = line.substr(10);
                    }
                }
            }

            std::string service_name = filename;
            if (service_name.size() > 8) {
                service_name = service_name.substr(0, service_name.size() - 8);
            }

            std::filesystem::path enabled_path("/etc/systemd/system/multi-user.target.wants/" + filename);
            if (std::filesystem::exists(enabled_path)) {
                info.enabled = true;
            }

            std::string pid_file = "/var/run/" + service_name + ".pid";
            std::ifstream pid_stream(pid_file);
            if (pid_stream) {
                pid_stream >> info.pid;
                if (info.pid > 0) {
                    info.state = ServiceState::Running;
                } else {
                    info.state = ServiceState::Stopped;
                }
            } else {
                info.state = ServiceState::Stopped;
            }

            result.push_back(info);
        }
    }
#endif

#if defined(COS_PLATFORM_MACOS)
    std::filesystem::path launchd_dir("/Library/LaunchDaemons");
    if (std::filesystem::exists(launchd_dir)) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(launchd_dir, ec)) {
            if (!entry.is_regular_file()) continue;

            std::string filename = entry.path().filename().string();
            if (filename.find(".plist") == std::string::npos) continue;

            ServiceInfo info;
            info.name = filename;
            info.state = ServiceState::Unknown;

            result.push_back(info);
        }
    }
#endif

    return result;
}

void ServiceMonitor::tick() {
    auto current = snapshot();
    std::map<std::string, ServiceInfo> current_map;
    for (auto& s : current) {
        current_map[s.name] = s;
    }

    if (first_scan_.exchange(false)) {
        std::lock_guard<std::mutex> lock(mutex_);
        previous_ = std::move(current_map);
        return;
    }

    std::map<std::string, ServiceInfo> prev_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        prev_copy = previous_;
    }

    for (const auto& [name, info] : current_map) {
        if (prev_copy.find(name) == prev_copy.end()) {
            Event ev;
            ev.category = EventCategory::SystemConfig;
            ev.type = EventType::ServiceStateChanged;
            ev.source = "service";
            ev.target = name;
            ev.summary = "Service state changed: " + name + " -> " + service_state_name(info.state);
            ev.attributes["service"] = name;
            ev.attributes["state"] = service_state_name(info.state);
            ev.attributes["description"] = info.description;
            ev.attributes["enabled"] = info.enabled ? "true" : "false";
            if (info.pid > 0) {
                ev.attributes["pid"] = std::to_string(info.pid);
            }
            ev.platform = platform::name();
            ev.host = platform::hostname();
            emit(ev);
        } else {
            auto prev_state = prev_copy[name].state;
            if (prev_state != info.state) {
                Event ev;
                ev.category = EventCategory::SystemConfig;
                ev.type = EventType::ServiceStateChanged;
                ev.source = "service";
                ev.target = name;
                ev.summary = "Service state changed: " + name + " (" +
                             service_state_name(prev_state) + " -> " +
                             service_state_name(info.state) + ")";
                ev.attributes["service"] = name;
                ev.attributes["old_state"] = service_state_name(prev_state);
                ev.attributes["new_state"] = service_state_name(info.state);
                ev.attributes["description"] = info.description;
                ev.platform = platform::name();
                ev.host = platform::hostname();
                emit(ev);
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        previous_ = std::move(current_map);
    }
}

} // namespace monitor
} // namespace changeos