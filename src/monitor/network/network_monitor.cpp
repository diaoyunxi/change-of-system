#include "monitor/network/network_monitor.h"

#include "platform/platform_detection.h"
#include "utils/logger.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace changeos {
namespace monitor {

NetworkMonitor::NetworkMonitor() = default;
NetworkMonitor::~NetworkMonitor() { stop(); }

bool NetworkMonitor::is_available() const {
    return platform::is_unix() || platform::is_windows();
}

bool NetworkMonitor::on_start() {
    COS_LOG_INFO("Network monitor starting");
    first_scan_.store(true);
    runner_ = std::make_unique<utils::PeriodicRunner>();
    runner_->start([this]() { tick(); },
                   std::chrono::milliseconds(poll_interval_ms_));
    return true;
}

bool NetworkMonitor::on_stop() {
    if (runner_) runner_->stop();
    runner_.reset();
    COS_LOG_INFO("Network monitor stopped");
    return true;
}

std::vector<NetworkConnection> NetworkMonitor::snapshot_connections() {
    std::vector<NetworkConnection> result;

#if defined(COS_PLATFORM_LINUX) || defined(COS_PLATFORM_UNIX) || defined(COS_PLATFORM_ANDROID) || defined(COS_PLATFORM_CHROMEOS) || defined(COS_PLATFORM_FYDEOS)
    auto parse_file = [&](const std::string& path, const std::string& proto) {
        std::ifstream file(path);
        if (!file) return;
        std::string line;
        std::getline(file, line);
        while (std::getline(file, line)) {
            std::istringstream iss(line);
            std::string local, remote, state_tok;
            int i = 0;
            std::string token;
            while (iss >> token) {
                if (i == 1) local = token;
                else if (i == 2) remote = token;
                else if (i == 3) state_tok = token;
                ++i;
            }
            if (!local.empty() && !remote.empty()) {
                NetworkConnection conn;
                conn.protocol = proto;
                conn.local_address = local;
                conn.remote_address = remote;
                conn.state = state_tok;
                result.push_back(conn);
            }
        }
    };
    parse_file("/proc/net/tcp", "tcp");
    parse_file("/proc/net/udp", "udp");
    parse_file("/proc/net/tcp6", "tcp6");
    parse_file("/proc/net/udp6", "udp6");
#else
    (void)result;
#endif

    return result;
}

void NetworkMonitor::tick() {
    auto current = snapshot_connections();

    auto make_key = [](const NetworkConnection& c) {
        return c.protocol + "|" + c.local_address + "|" + c.remote_address;
    };

    std::set<std::string> current_keys;
    std::set<std::string> previous_keys;

    for (auto& c : current) current_keys.insert(make_key(c));

    // 在锁内读取 previous_connections_ 的副本，避免无锁读取导致的数据竞争
    std::vector<NetworkConnection> previous_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        previous_copy = previous_connections_;
    }
    for (auto& c : previous_copy) previous_keys.insert(make_key(c));

    if (!first_scan_.exchange(false)) {
        for (auto& c : current) {
            std::string key = make_key(c);
            if (previous_keys.find(key) == previous_keys.end()) {
                Event ev;
                ev.category = EventCategory::Network;
                ev.type = EventType::NetworkConnectionOpened;
                ev.source = "network";
                ev.target = c.remote_address;
                ev.summary = "Connection opened: " + c.protocol + " "
                    + c.local_address + " -> " + c.remote_address;
                ev.attributes["protocol"] = c.protocol;
                ev.attributes["local"] = c.local_address;
                ev.attributes["remote"] = c.remote_address;
                ev.attributes["state"] = c.state;
                ev.platform = platform::name();
                ev.host = platform::hostname();
                emit(ev);
            }
        }

        // 使用锁内复制的 previous_copy，避免在迭代过程中数据被其他线程修改
        for (auto& c : previous_copy) {
            std::string key = make_key(c);
            if (current_keys.find(key) == current_keys.end()) {
                Event ev;
                ev.category = EventCategory::Network;
                ev.type = EventType::NetworkConnectionClosed;
                ev.source = "network";
                ev.target = c.remote_address;
                ev.summary = "Connection closed: " + c.protocol + " "
                    + c.local_address + " -> " + c.remote_address;
                ev.attributes["protocol"] = c.protocol;
                ev.attributes["local"] = c.local_address;
                ev.attributes["remote"] = c.remote_address;
                ev.platform = platform::name();
                ev.host = platform::hostname();
                emit(ev);
            }
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    previous_connections_ = std::move(current);
}

} // namespace monitor
} // namespace changeos
