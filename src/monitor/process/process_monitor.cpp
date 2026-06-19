#include "monitor/process/process_monitor.h"

#include "platform/platform_detection.h"
#include "utils/logger.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace changeos {
namespace monitor {

ProcessMonitor::ProcessMonitor() = default;
ProcessMonitor::~ProcessMonitor() { stop(); }

bool ProcessMonitor::is_available() const {
    return platform::has_native_process_monitor();
}

bool ProcessMonitor::on_start() {
    COS_LOG_INFO("Process monitor starting");
    first_scan_.store(true);
    runner_ = std::make_unique<utils::PeriodicRunner>();
    runner_->start([this]() { tick(); },
                   std::chrono::milliseconds(poll_interval_ms_));
    return true;
}

bool ProcessMonitor::on_stop() {
    if (runner_) runner_->stop();
    runner_.reset();
    COS_LOG_INFO("Process monitor stopped");
    return true;
}

std::vector<ProcessInfo> ProcessMonitor::snapshot() {
    std::vector<ProcessInfo> result;

#if defined(COS_PLATFORM_LINUX) || defined(COS_PLATFORM_UNIX) || defined(COS_PLATFORM_ANDROID) || defined(COS_PLATFORM_CHROMEOS) || defined(COS_PLATFORM_FYDEOS)
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("/proc", ec)) {
        if (!entry.is_directory()) continue;
        std::string name = entry.path().filename().string();
        if (name.empty() || name.find_first_not_of("0123456789") != std::string::npos) {
            continue;
        }
        std::int64_t pid = 0;
        try {
            pid = std::stoll(name);
        } catch (...) {
            continue;
        }
        ProcessInfo info;
        info.pid = pid;

        std::ifstream status(entry.path() / "status");
        if (status) {
            std::string line;
            while (std::getline(status, line)) {
                if (line.rfind("Name:", 0) == 0) {
                    info.name = line.substr(5);
                    while (!info.name.empty() && info.name.front() == '\t') {
                        info.name.erase(info.name.begin());
                    }
                }
            }
        }

        std::ifstream cmdline(entry.path() / "cmdline");
        if (cmdline) {
            std::ostringstream oss;
            char c;
            while (cmdline.get(c)) {
                if (c == '\0') oss << ' ';
                else oss << c;
            }
            info.command_line = oss.str();
        }

        std::ifstream statm(entry.path() / "statm");
        if (statm) {
            long long size = 0;
            statm >> size;
            info.memory_bytes = size * 4096LL;
        }

        if (!info.name.empty() || !info.command_line.empty()) {
            result.push_back(info);
        }
    }
#else
    (void)result;
#endif

    return result;
}

void ProcessMonitor::tick() {
    auto current = snapshot();
    std::map<std::int64_t, ProcessInfo> current_map;
    for (auto& p : current) current_map[p.pid] = p;

    if (first_scan_.exchange(false)) {
        std::lock_guard<std::mutex> lock(mutex_);
        previous_ = std::move(current_map);
        return;
    }

    std::map<std::int64_t, ProcessInfo> prev_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        prev_copy = previous_;
    }

    for (const auto& [pid, info] : current_map) {
        if (prev_copy.find(pid) == prev_copy.end()) {
            Event ev;
            ev.category = EventCategory::Process;
            ev.type = EventType::ProcessStarted;
            ev.source = "process";
            ev.target = info.name + "(" + std::to_string(pid) + ")";
            ev.summary = "Process started: " + info.name;
            ev.attributes["pid"] = std::to_string(pid);
            ev.attributes["name"] = info.name;
            ev.attributes["cmdline"] = info.command_line;
            ev.attributes["memory_bytes"] = std::to_string(info.memory_bytes);
            ev.platform = platform::name();
            ev.host = platform::hostname();
            emit(ev);
        }
    }

    for (const auto& [pid, info] : prev_copy) {
        if (current_map.find(pid) == current_map.end()) {
            Event ev;
            ev.category = EventCategory::Process;
            ev.type = EventType::ProcessStopped;
            ev.source = "process";
            ev.target = info.name + "(" + std::to_string(pid) + ")";
            ev.summary = "Process stopped: " + info.name;
            ev.attributes["pid"] = std::to_string(pid);
            ev.attributes["name"] = info.name;
            ev.platform = platform::name();
            ev.host = platform::hostname();
            emit(ev);
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        previous_ = std::move(current_map);
    }
}

} // namespace monitor
} // namespace changeos
