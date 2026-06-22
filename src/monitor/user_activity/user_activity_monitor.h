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

struct UserSession {
    std::string username;
    std::string tty;
    std::string remote_host;
    std::int64_t login_time = 0;
    std::string process_id;
};

class UserActivityMonitor : public Monitor {
public:
    UserActivityMonitor();
    ~UserActivityMonitor() override;

    std::string name() const override { return "user_activity"; }
    std::string description() const override {
        return "Monitors user login/logout activity.";
    }
    bool is_available() const override;
    bool supports_native_events() const override { return false; }

protected:
    bool on_start() override;
    bool on_stop() override;

private:
    void tick();
    std::vector<UserSession> snapshot();

    std::unique_ptr<utils::PeriodicRunner> runner_;
    std::map<std::string, UserSession> previous_;
    std::mutex mutex_;
    std::atomic<bool> first_scan_{true};
};

} // namespace monitor
} // namespace changeos