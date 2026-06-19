#pragma once

#include "core/monitor.h"
#include "utils/periodic_runner.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace changeos {
namespace monitor {

class SystemConfigMonitor : public Monitor {
public:
    SystemConfigMonitor();
    ~SystemConfigMonitor() override;

    std::string name() const override { return "system_config"; }
    std::string description() const override {
        return "Monitors system-wide configuration changes.";
    }
    bool is_available() const override { return true; }
    bool supports_native_events() const override { return false; }

    void add_config_file(const std::string& path);
    std::vector<std::string> config_files() const;

protected:
    bool on_start() override;
    bool on_stop() override;

private:
    void tick();

    std::vector<std::string> config_files_;
    std::map<std::string, std::int64_t> previous_hashes_;
    std::unique_ptr<utils::PeriodicRunner> runner_;
    mutable std::mutex mutex_;
    std::atomic<bool> first_scan_{true};
};

} // namespace monitor
} // namespace changeos
