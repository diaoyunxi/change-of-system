#pragma once

#include "core/monitor.h"
#include "utils/periodic_runner.h"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace changeos {
namespace monitor {

struct DiskInfo {
    std::string mount_point;
    std::string device;
    std::int64_t total_bytes = 0;
    std::int64_t used_bytes = 0;
    std::int64_t free_bytes = 0;
    double usage_percent = 0.0;
};

class DiskSpaceMonitor : public Monitor {
public:
    DiskSpaceMonitor();
    ~DiskSpaceMonitor() override;

    std::string name() const override { return "disk_space"; }
    std::string description() const override {
        return "Monitors disk space usage and alerts on high usage.";
    }
    bool is_available() const override;
    bool supports_native_events() const override { return false; }

    void set_warning_threshold(double percent) { warning_threshold_ = percent; }
    void set_critical_threshold(double percent) { critical_threshold_ = percent; }
    void set_watch_paths(const std::vector<std::string>& paths) { watch_paths_ = paths; }

protected:
    bool on_start() override;
    bool on_stop() override;

private:
    void tick();
    std::map<std::string, DiskInfo> scan_disk_space();

    std::unique_ptr<utils::PeriodicRunner> runner_;
    std::map<std::string, DiskInfo> previous_state_;
    std::mutex mutex_;
    std::atomic<bool> first_scan_{true};
    double warning_threshold_ = 80.0;  // 80% usage triggers warning
    double critical_threshold_ = 95.0; // 95% usage triggers critical
    std::vector<std::string> watch_paths_;
};

} // namespace monitor
} // namespace changeos