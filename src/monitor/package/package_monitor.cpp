#include "monitor/package/package_monitor.h"
#include "utils/periodic_runner.h"

#include <fstream>
#include <sstream>
#include <cstdlib>
#include <memory>
#include <chrono>

namespace changeos {
namespace monitor {

PackageMonitor::PackageMonitor() : package_manager_(PackageManager::Unknown) {
}

PackageMonitor::~PackageMonitor() {
    stop();
}

bool PackageMonitor::on_start() {
    package_manager_ = detect_package_manager();
    if (package_manager_ == PackageManager::Unknown) {
        return false; // No supported package manager found
    }
    
    runner_ = std::make_unique<utils::PeriodicRunner>();
    runner_->start([this]() { poll(); },
                   std::chrono::milliseconds(poll_interval_ms_));
    return true;
}

bool PackageMonitor::on_stop() {
    if (runner_) runner_->stop();
    runner_.reset();
    return true;
}

PackageMonitor::PackageManager PackageMonitor::detect_package_manager() {
#if defined(__linux__)
    // Check for package managers in order of preference
    if (std::system("command -v apt >/dev/null 2>&1") == 0) {
        return PackageManager::APT;
    } else if (std::system("command -v dnf >/dev/null 2>&1") == 0) {
        return PackageManager::DNF;
    } else if (std::system("command -v yum >/dev/null 2>&1") == 0) {
        return PackageManager::YUM;
    } else if (std::system("command -v pacman >/dev/null 2>&1") == 0) {
        return PackageManager::PACMAN;
    } else if (std::system("command -v zypper >/dev/null 2>&1") == 0) {
        return PackageManager::ZYPPER;
    }
#elif defined(__APPLE__)
    if (std::system("command -v brew >/dev/null 2>&1") == 0) {
        return PackageManager::BREW;
    } else if (std::system("command -v port >/dev/null 2>&1") == 0) {
        return PackageManager::PORT;
    }
#endif
    return PackageManager::Unknown;
}

std::map<std::string, PackageMonitor::PackageInfo> PackageMonitor::snapshot_packages() {
    std::map<std::string, PackageInfo> result;

    std::string command;
    
    switch (package_manager_) {
        case PackageManager::APT:
            command = "dpkg-query -W -f='${Package} ${Version} ${Architecture}\\n' 2>/dev/null";
            break;
        case PackageManager::DNF:
        case PackageManager::YUM:
            command = "rpm -qa --queryformat '%{NAME} %{VERSION}-%{RELEASE} %{ARCH}\\n' 2>/dev/null";
            break;
        case PackageManager::PACMAN:
            command = "pacman -Q 2>/dev/null | awk '{print $1, $2, \"x86_64\"}'";
            break;
        case PackageManager::ZYPPER:
            command = "rpm -qa --queryformat '%{NAME} %{VERSION}-%{RELEASE} %{ARCH}\\n' 2>/dev/null";
            break;
        case PackageManager::BREW:
            command = "brew list --versions 2>/dev/null";
            break;
        case PackageManager::PORT:
            command = "port installed 2>/dev/null | tail -n +3 | awk '{print $1, $2, \"universal\"}'";
            break;
        default:
            return result;
    }

    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
    if (!pipe) {
        return result;
    }

    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), pipe.get()) != nullptr) {
        std::istringstream iss(buffer);
        PackageInfo info;
        if (iss >> info.name >> info.version >> info.architecture) {
            result[info.name] = info;
        }
    }

    return result;
}

void PackageMonitor::poll() {
    static std::map<std::string, PackageInfo> last_snapshot;

    auto current = snapshot_packages();

    // Detect new packages
    for (const auto& [name, info] : current) {
        if (last_snapshot.find(name) == last_snapshot.end()) {
            Event e;
            e.category = EventCategory::System;
            e.type = EventType::ConfigValueChanged;
            e.source = "package_monitor";
            e.target = name;
            e.summary = "Package installed: " + name + " " + info.version;
            e.attributes["action"] = "install";
            e.attributes["version"] = info.version;
            e.attributes["architecture"] = info.architecture;
            emit(e);
        }
    }

    // Detect removed packages
    for (const auto& [name, info] : last_snapshot) {
        if (current.find(name) == current.end()) {
            Event e;
            e.category = EventCategory::System;
            e.type = EventType::ConfigValueChanged;
            e.source = "package_monitor";
            e.target = name;
            e.summary = "Package removed: " + name + " " + info.version;
            e.attributes["action"] = "remove";
            e.attributes["version"] = info.version;
            emit(e);
        }
    }

    // Detect updated packages
    for (const auto& [name, info] : current) {
        auto it = last_snapshot.find(name);
        if (it != last_snapshot.end() && it->second.version != info.version) {
            Event e;
            e.category = EventCategory::System;
            e.type = EventType::ConfigValueChanged;
            e.source = "package_monitor";
            e.target = name;
            e.summary = "Package updated: " + name + " " + 
                       it->second.version + " -> " + info.version;
            e.attributes["action"] = "update";
            e.attributes["old_version"] = it->second.version;
            e.attributes["new_version"] = info.version;
            emit(e);
        }
    }

    last_snapshot = current;
}

} // namespace monitor
} // namespace changeos
