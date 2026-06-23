#pragma once

#include "core/monitor.h"
#include "utils/periodic_runner.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace changeos {
namespace monitor {

struct SystemLoadInfo {
    double load_1min = 0.0;
    double load_5min = 0.0;
    double load_15min = 0.0;
    double cpu_usage_percent = 0.0;
    std::int64_t total_memory = 0;
    std::int64_t used_memory = 0;
    double memory_usage_percent = 0.0;
};

class SystemLoadMonitor : public Monitor {
public:
    SystemLoadMonitor();
    ~SystemLoadMonitor() override;

    std::string name() const override { return "system_load"; }
    std::string description() const override {
        return "Monitors system load averages and CPU/memory usage.";
    }
    bool is_available() const override;
    bool supports_native_events() const override { return false; }

    void set_load_threshold(double threshold) { load_threshold_ = threshold; }
    void set_cpu_threshold(double threshold) { cpu_threshold_ = threshold; }

protected:
    bool on_start() override;
    bool on_stop() override;

private:
    void tick();
    SystemLoadInfo get_system_load();

    std::unique_ptr<utils::PeriodicRunner> runner_;
    SystemLoadInfo previous_info_;
    std::mutex mutex_;
    std::atomic<bool> first_scan_{true};
    double load_threshold_ = 5.0;  // High load threshold (1-min average)
    double cpu_threshold_ = 90.0;   // High CPU usage threshold
    std::atomic<bool> high_load_state_{false};
};

} // namespace monitor
} // namespace changeos