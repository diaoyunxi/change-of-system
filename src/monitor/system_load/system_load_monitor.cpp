#include "monitor/system_load/system_load_monitor.h"
#include "core/event.h"
#include "platform/platform_detection.h"
#include "utils/logger.h"

#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <pdh.h>
#pragma comment(lib, "pdh.lib")
#else
#include <unistd.h>
#include <sys/sysinfo.h>
#endif

namespace changeos {
namespace monitor {

SystemLoadMonitor::SystemLoadMonitor() = default;
SystemLoadMonitor::~SystemLoadMonitor() = default;

bool SystemLoadMonitor::is_available() const {
    return true;
}

bool SystemLoadMonitor::on_start() {
    runner_ = std::make_unique<utils::PeriodicRunner>();
    runner_->start([this]() { tick(); },
                   std::chrono::milliseconds(poll_interval_ms_));
    COS_LOG_INFO("System load monitor started");
    return true;
}

bool SystemLoadMonitor::on_stop() {
    if (runner_) {
        runner_->stop();
    }
    COS_LOG_INFO("System load monitor stopped");
    return true;
}

void SystemLoadMonitor::tick() {
    auto current_info = get_system_load();

    std::lock_guard<std::mutex> lock(mutex_);

    if (first_scan_) {
        previous_info_ = current_info;
        first_scan_ = false;
        return;
    }

    // Check for high system load
    bool currently_high = current_info.load_1min >= load_threshold_ ||
                          current_info.cpu_usage_percent >= cpu_threshold_;

    if (currently_high && !high_load_state_.load()) {
        high_load_state_ = true;
        Event e;
        e.category = EventCategory::System;
        e.type = EventType::SystemLoadHigh;
        e.source = "system_load_monitor";
        e.target = "system";
        e.attributes["load_1min"] = std::to_string(current_info.load_1min);
        e.attributes["load_5min"] = std::to_string(current_info.load_5min);
        e.attributes["load_15min"] = std::to_string(current_info.load_15min);
        e.attributes["cpu_usage_percent"] = std::to_string(current_info.cpu_usage_percent);
        e.attributes["memory_usage_percent"] = std::to_string(current_info.memory_usage_percent);
        e.summary = "High system load detected: load=" +
                    std::to_string(current_info.load_1min) +
                    ", cpu=" + std::to_string(static_cast<int>(current_info.cpu_usage_percent)) + "%";
        emit(e);
        COS_LOG_WARN("High system load detected");
    }
    else if (!currently_high && high_load_state_.load()) {
        high_load_state_ = false;
        Event e;
        e.category = EventCategory::System;
        e.type = EventType::SystemLoadNormal;
        e.source = "system_load_monitor";
        e.target = "system";
        e.attributes["load_1min"] = std::to_string(current_info.load_1min);
        e.attributes["load_5min"] = std::to_string(current_info.load_5min);
        e.attributes["load_15min"] = std::to_string(current_info.load_15min);
        e.attributes["cpu_usage_percent"] = std::to_string(current_info.cpu_usage_percent);
        e.attributes["memory_usage_percent"] = std::to_string(current_info.memory_usage_percent);
        e.summary = "System load returned to normal: load=" +
                    std::to_string(current_info.load_1min) +
                    ", cpu=" + std::to_string(static_cast<int>(current_info.cpu_usage_percent)) + "%";
        emit(e);
        COS_LOG_INFO("System load returned to normal");
    }

    previous_info_ = current_info;
}

SystemLoadInfo SystemLoadMonitor::get_system_load() {
    SystemLoadInfo info;

#ifdef __linux__
    // Read load averages from /proc/loadavg
    std::ifstream loadavg("/proc/loadavg");
    if (loadavg.is_open()) {
        loadavg >> info.load_1min >> info.load_5min >> info.load_15min;
    }

    // Read CPU usage from /proc/stat
    std::ifstream stat("/proc/stat");
    std::string line;
    if (std::getline(stat, line)) {
        std::istringstream iss(line);
        std::string cpu_label;
        std::int64_t user, nice, system, idle, iowait, irq, softirq, steal;
        iss >> cpu_label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;

        std::int64_t total = user + nice + system + idle + iowait + irq + softirq + steal;
        std::int64_t active = user + nice + system + irq + softirq + steal;

        // Calculate CPU usage (simple approximation)
        static std::int64_t prev_total = 0, prev_active = 0;
        std::int64_t delta_total = total - prev_total;
        std::int64_t delta_active = active - prev_active;

        if (delta_total > 0) {
            info.cpu_usage_percent = (static_cast<double>(delta_active) /
                                      static_cast<double>(delta_total)) * 100.0;
        }

        prev_total = total;
        prev_active = active;
    }

    // Read memory info from /proc/meminfo
    std::ifstream meminfo("/proc/meminfo");
    std::int64_t mem_total = 0, mem_free = 0, mem_available = 0, mem_buffers = 0, mem_cached = 0;
    while (std::getline(meminfo, line)) {
        if (line.find("MemTotal:") == 0) {
            std::istringstream iss(line);
            std::string label;
            iss >> label >> mem_total;
        } else if (line.find("MemFree:") == 0) {
            std::istringstream iss(line);
            std::string label;
            iss >> label >> mem_free;
        } else if (line.find("MemAvailable:") == 0) {
            std::istringstream iss(line);
            std::string label;
            iss >> label >> mem_available;
        } else if (line.find("Buffers:") == 0) {
            std::istringstream iss(line);
            std::string label;
            iss >> label >> mem_buffers;
        } else if (line.find("Cached:") == 0) {
            std::istringstream iss(line);
            std::string label;
            iss >> label >> mem_cached;
        }
    }

    info.total_memory = mem_total * 1024; // Convert from KB to bytes
    std::int64_t used = mem_total - mem_available;
    info.used_memory = used * 1024;
    if (mem_total > 0) {
        info.memory_usage_percent = (static_cast<double>(used) /
                                     static_cast<double>(mem_total)) * 100.0;
    }

#elif defined(_WIN32)
    // Windows implementation
    // Get load average (not directly available, use CPU usage instead)
    MEMORYSTATUSEX mem_status;
    mem_status.dwLength = sizeof(mem_status);
    if (GlobalMemoryStatusEx(&mem_status)) {
        info.memory_usage_percent = static_cast<double>(mem_status.dwMemoryLoad);
        info.total_memory = static_cast<std::int64_t>(mem_status.ullTotalPhys);
        info.used_memory = static_cast<std::int64_t>(mem_status.ullTotalPhys - mem_status.ullAvailPhys);
    }

    // Get CPU usage using PDH
    static PDH_HQUERY cpu_query = nullptr;
    static PDH_HCOUNTER cpu_counter = nullptr;

    if (!cpu_query) {
        PdhOpenQuery(nullptr, 0, &cpu_query);
        PdhAddEnglishCounter(cpu_query, L"\\Processor(_Total)\\% Processor Time", 0, &cpu_counter);
        PdhCollectQueryData(cpu_query);
    } else {
        PDH_FMT_COUNTERVALUE counter_val;
        PdhCollectQueryData(cpu_query);
        PdhGetFormattedCounterValue(cpu_counter, PDH_FMT_DOUBLE, nullptr, &counter_val);
        info.cpu_usage_percent = counter_val.doubleValue;
    }

    // Windows doesn't have traditional load averages
    info.load_1min = info.cpu_usage_percent / 100.0;
    info.load_5min = info.cpu_usage_percent / 100.0;
    info.load_15min = info.cpu_usage_percent / 100.0;

#elif defined(__APPLE__)
    // macOS implementation
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        info.load_1min = static_cast<double>(si.loads[0]) / (1 << SI_LOAD_SHIFT);
        info.load_5min = static_cast<double>(si.loads[1]) / (1 << SI_LOAD_SHIFT);
        info.load_15min = static_cast<double>(si.loads[2]) / (1 << SI_LOAD_SHIFT);
        info.total_memory = si.totalram * si.mem_unit;
        info.used_memory = (si.totalram - si.freeram) * si.mem_unit;
        if (si.totalram > 0) {
            info.memory_usage_percent = (static_cast<double>(si.totalram - si.freeram) /
                                         static_cast<double>(si.totalram)) * 100.0;
        }
    }

    // For CPU usage, use host_processor_info (simplified)
    FILE* pipe = popen("top -l 1 | grep 'CPU usage' | awk '{print $3}'", "r");
    if (pipe) {
        char buffer[64];
        if (fgets(buffer, sizeof(buffer), pipe)) {
            info.cpu_usage_percent = std::stod(buffer);
        }
        pclose(pipe);
    }
#endif

    return info;
}

} // namespace monitor
} // namespace changeos