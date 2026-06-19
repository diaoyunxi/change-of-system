#pragma once

#include "core/monitor.h"
#include "utils/periodic_runner.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace changeos {
namespace monitor {

struct NetworkConnection {
    std::string protocol;
    std::string local_address;
    std::string remote_address;
    std::string state;
};

class NetworkMonitor : public Monitor {
public:
    NetworkMonitor();
    ~NetworkMonitor() override;

    std::string name() const override { return "network"; }
    std::string description() const override {
        return "Monitors network connections and bandwidth.";
    }
    bool is_available() const override;
    bool supports_native_events() const override { return false; }

protected:
    bool on_start() override;
    bool on_stop() override;

private:
    void tick();
    std::vector<NetworkConnection> snapshot_connections();

    std::unique_ptr<utils::PeriodicRunner> runner_;
    std::vector<NetworkConnection> previous_connections_;
    std::mutex mutex_;
    std::atomic<bool> first_scan_{true};
};

} // namespace monitor
} // namespace changeos
