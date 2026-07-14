#include "monitor/user_activity/user_activity_monitor.h"

#include "platform/platform_detection.h"
#include "utils/logger.h"

#include <fstream>
#include <sstream>
#include <string>

namespace changeos {
namespace monitor {

UserActivityMonitor::UserActivityMonitor() = default;
UserActivityMonitor::~UserActivityMonitor() { stop(); }

bool UserActivityMonitor::is_available() const {
    return platform::has_native_user_monitor();
}

bool UserActivityMonitor::on_start() {
    COS_LOG_INFO("User activity monitor starting");
    first_scan_.store(true);
    runner_ = std::make_unique<utils::PeriodicRunner>();
    runner_->start([this]() { tick(); },
                   std::chrono::milliseconds(poll_interval_ms_));
    return true;
}

bool UserActivityMonitor::on_stop() {
    if (runner_) runner_->stop();
    runner_.reset();
    COS_LOG_INFO("User activity monitor stopped");
    return true;
}

std::vector<UserSession> UserActivityMonitor::snapshot() {
    std::vector<UserSession> result;

#if defined(COS_PLATFORM_LINUX) || defined(COS_PLATFORM_UNIX) || defined(COS_PLATFORM_ANDROID) || defined(COS_PLATFORM_CHROMEOS) || defined(COS_PLATFORM_FYDEOS) || defined(COS_PLATFORM_MACOS)
    std::ifstream wtmp("/var/log/wtmp");
    if (!wtmp) {
        std::ifstream utmp("/var/run/utmp");
        if (!utmp) {
            return result;
        }
    }

    std::ifstream who("/var/run/utmp");
    if (who) {
        std::string line;
        while (std::getline(who, line)) {
            if (line.size() < 62) continue;

            UserSession sess;
            sess.username = line.substr(0, 32);
            while (!sess.username.empty() && sess.username.back() == ' ') {
                sess.username.pop_back();
            }
            if (sess.username.empty() || sess.username == "LOGIN") continue;

            sess.tty = line.substr(32, 12);
            while (!sess.tty.empty() && sess.tty.back() == ' ') {
                sess.tty.pop_back();
            }

            // 使用 strnlen 确保不越界读取未终止的字符串
            std::string host = line.substr(44, 16);
            // 截断到第一个空字符（如果有的话）
            auto null_pos = host.find('\0');
            if (null_pos != std::string::npos) {
                host = host.substr(0, null_pos);
            }
            while (!host.empty() && host.back() == ' ') {
                host.pop_back();
            }
            sess.remote_host = host;

            std::string time_str = line.substr(62);
            try {
                sess.login_time = std::stoll(time_str);
            } catch (...) {
                sess.login_time = 0;
            }

            sess.process_id = sess.tty + ":" + sess.username;
            result.push_back(sess);
        }
    } else {
        std::ifstream lastlog("/var/log/lastlog");
        if (lastlog) {
            std::string line;
            while (std::getline(lastlog, line)) {
                if (line.empty()) continue;

                size_t pos = line.find(':');
                if (pos == std::string::npos) continue;

                UserSession sess;
                sess.username = line.substr(0, pos);

                size_t next_pos = line.find(':', pos + 1);
                if (next_pos != std::string::npos) {
                    sess.tty = line.substr(pos + 1, next_pos - pos - 1);
                }

                sess.process_id = sess.tty + ":" + sess.username;
                result.push_back(sess);
            }
        }
    }
#else
    (void)result;
#endif

    return result;
}

void UserActivityMonitor::tick() {
    auto current = snapshot();
    std::map<std::string, UserSession> current_map;
    for (auto& s : current) {
        current_map[s.process_id] = s;
    }

    if (first_scan_.exchange(false)) {
        std::lock_guard<std::mutex> lock(mutex_);
        previous_ = std::move(current_map);
        return;
    }

    std::map<std::string, UserSession> prev_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        prev_copy = previous_;
    }

    for (const auto& [id, sess] : current_map) {
        if (prev_copy.find(id) == prev_copy.end()) {
            Event ev;
            ev.category = EventCategory::SystemConfig;
            ev.type = EventType::UserLoggedIn;
            ev.source = "user_activity";
            ev.target = sess.username;
            ev.summary = "User logged in: " + sess.username;
            ev.attributes["username"] = sess.username;
            ev.attributes["tty"] = sess.tty;
            ev.attributes["remote_host"] = sess.remote_host;
            ev.attributes["login_time"] = std::to_string(sess.login_time);
            ev.platform = platform::name();
            ev.host = platform::hostname();
            emit(ev);
        }
    }

    for (const auto& [id, sess] : prev_copy) {
        if (current_map.find(id) == current_map.end()) {
            Event ev;
            ev.category = EventCategory::SystemConfig;
            ev.type = EventType::UserLoggedOut;
            ev.source = "user_activity";
            ev.target = sess.username;
            ev.summary = "User logged out: " + sess.username;
            ev.attributes["username"] = sess.username;
            ev.attributes["tty"] = sess.tty;
            ev.attributes["remote_host"] = sess.remote_host;
            ev.platform = platform::name();
            ev.host = platform::hostname();
            emit(ev);
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        previous_ = std::move(current_map);
    }
}

} // namespace monitor
} // namespace changeos