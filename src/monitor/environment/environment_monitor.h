#pragma once

#include "core/monitor.h"
#include "utils/periodic_runner.h"

#include <map>
#include <memory>
#include <set>
#include <string>

namespace changeos {
namespace monitor {

class EnvironmentMonitor : public Monitor {
public:
    EnvironmentMonitor();
    ~EnvironmentMonitor() override;

    std::string name() const override { return "environment"; }
    std::string description() const override {
        return "Monitor environment variable changes";
    }

    void set_watch_variables(const std::set<std::string>& vars);
    void set_check_interval_ms(int ms) { set_poll_interval_ms(ms); }

protected:
    bool on_start() override;
    bool on_stop() override;

private:
    void poll();
    std::map<std::string, std::string> snapshot_environment();
    
    std::set<std::string> watch_variables_;
    bool track_all_;
    std::unique_ptr<utils::PeriodicRunner> runner_;
};

} // namespace monitor
} // namespace changeos
