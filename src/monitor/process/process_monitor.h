#pragma once

#include "core/monitor.h"
#include "utils/periodic_runner.h"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace changeos {
namespace monitor {

struct ProcessInfo {
    std::int64_t pid = 0;
    std::string name;
    std::string command_line;
    double cpu_percent = 0.0;
    std::int64_t memory_bytes = 0;
    std::int64_t start_time = 0;
    std::string user;
};

class ProcessMonitor : public Monitor {
public:
    ProcessMonitor();
    ~ProcessMonitor() override;

    std::string name() const override { return "process"; }
    std::string description() const override {
        return "Monitors process lifecycle and resource usage.";
    }
    bool is_available() const override;
    bool supports_native_events() const override { return false; }

protected:
    bool on_start() override;
    bool on_stop() override;

private:
    void tick();
    std::vector<ProcessInfo> snapshot();

    std::unique_ptr<utils::PeriodicRunner> runner_;
    std::map<std::int64_t, ProcessInfo> previous_;
    std::mutex mutex_;
    std::atomic<bool> first_scan_{true};
};

} // namespace monitor
} // namespace changeos
