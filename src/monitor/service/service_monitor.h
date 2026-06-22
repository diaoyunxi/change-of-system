#pragma once

#include "core/monitor.h"
#include "utils/periodic_runner.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace changeos {
namespace monitor {

enum class ServiceState {
    Unknown,
    Running,
    Stopped,
    Failed,
    Starting,
    Stopping
};

std::string service_state_name(ServiceState state);

struct ServiceInfo {
    std::string name;
    std::string display_name;
    ServiceState state = ServiceState::Unknown;
    std::string description;
    std::string exec_start;
    int pid = 0;
    bool enabled = false;
};

class ServiceMonitor : public Monitor {
public:
    ServiceMonitor();
    ~ServiceMonitor() override;

    std::string name() const override { return "service"; }
    std::string description() const override {
        return "Monitors system service state changes.";
    }
    bool is_available() const override;
    bool supports_native_events() const override { return false; }

protected:
    bool on_start() override;
    bool on_stop() override;

private:
    void tick();
    std::vector<ServiceInfo> snapshot();

    std::unique_ptr<utils::PeriodicRunner> runner_;
    std::map<std::string, ServiceInfo> previous_;
    std::mutex mutex_;
    std::atomic<bool> first_scan_{true};
};

} // namespace monitor
} // namespace changeos