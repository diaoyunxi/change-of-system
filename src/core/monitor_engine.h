#pragma once

#include "core/event.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace changeos {

namespace monitor {
class FilesystemMonitor;
class ProcessMonitor;
class NetworkMonitor;
class SystemConfigMonitor;
}

namespace storage {
class Storage;
}

namespace reporting {
class Reporter;
}

class MonitorEngine {
public:
    using EventCallback = std::function<void(const Event&)>;

    MonitorEngine();
    ~MonitorEngine();

    bool initialize(const std::string& config_path = {});
    bool start_all();
    bool stop_all();
    bool is_running() const { return running_.load(); }

    void on_event(EventCallback cb);

    monitor::FilesystemMonitor& filesystem_monitor();
    monitor::ProcessMonitor& process_monitor();
    monitor::NetworkMonitor& network_monitor();
    monitor::SystemConfigMonitor& system_config_monitor();

    storage::Storage* storage();
    reporting::Reporter* reporter();

    std::vector<Event> recent_events(int limit = 100) const;

private:
    void route_event(const Event& event);

    std::unique_ptr<monitor::FilesystemMonitor> fs_;
    std::unique_ptr<monitor::ProcessMonitor> proc_;
    std::unique_ptr<monitor::NetworkMonitor> net_;
    std::unique_ptr<monitor::SystemConfigMonitor> cfg_;

    std::unique_ptr<storage::Storage> storage_;
    std::unique_ptr<reporting::Reporter> reporter_;

    mutable std::mutex callbacks_mutex_;
    std::vector<EventCallback> callbacks_;

    mutable std::mutex recent_mutex_;
    std::vector<Event> recent_;
    std::atomic<bool> running_{false};
};

} // namespace changeos
