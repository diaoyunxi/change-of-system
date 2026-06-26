#pragma once

#include "core/monitor.h"
#include "utils/periodic_runner.h"

#include <map>
#include <memory>
#include <set>
#include <string>

namespace changeos {
namespace monitor {

class PortMonitor : public Monitor {
public:
    PortMonitor();
    ~PortMonitor() override;

    std::string name() const override { return "port"; }
    std::string description() const override {
        return "Monitor open ports and listening services";
    }

    void set_watch_ports(const std::set<int>& ports);
    void set_check_interval_ms(int ms) { set_poll_interval_ms(ms); }

protected:
    bool on_start() override;
    bool on_stop() override;

private:
    struct PortInfo {
        int port;
        std::string protocol; // TCP, UDP
        std::string state;    // LISTEN, ESTABLISHED, etc.
        std::string process;
        std::string address;
    };

    void poll();
    std::map<int, PortInfo> snapshot_ports();
    std::set<int> watch_ports_;
    std::unique_ptr<utils::PeriodicRunner> runner_;
};

} // namespace monitor
} // namespace changeos
