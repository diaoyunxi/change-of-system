#include "monitor/port/port_monitor.h"
#include "utils/periodic_runner.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>

namespace changeos {
namespace monitor {

PortMonitor::PortMonitor() {
    // Default ports to watch
    watch_ports_ = {22, 80, 443, 3306, 5432, 6379, 8080, 8443};
}

PortMonitor::~PortMonitor() {
    stop();
}

bool PortMonitor::on_start() {
    runner_ = std::make_unique<utils::PeriodicRunner>();
    runner_->start([this]() { poll(); },
                   std::chrono::milliseconds(poll_interval_ms_));
    return true;
}

bool PortMonitor::on_stop() {
    if (runner_) runner_->stop();
    runner_.reset();
    return true;
}

void PortMonitor::set_watch_ports(const std::set<int>& ports) {
    watch_ports_ = ports;
}

std::map<int, PortMonitor::PortInfo> PortMonitor::snapshot_ports() {
    std::map<int, PortInfo> result;

#if defined(__linux__)
    // Parse /proc/net/tcp and /proc/net/tcp6 for TCP connections
    auto parse_proc_net = [this, &result](const std::string& path, const std::string& proto) {
        std::ifstream file(path);
        if (!file.is_open()) return;

        std::string line;
        std::getline(file, line); // Skip header

        while (std::getline(file, line)) {
            std::istringstream iss(line);
            std::string sl, local_addr, remote_addr, state;
            iss >> sl >> local_addr >> remote_addr >> state;

            // Parse local address:port
            auto colon_pos = local_addr.find(':');
            if (colon_pos == std::string::npos) continue;

            std::string port_hex = local_addr.substr(colon_pos + 1);
            int port = 0;
            try { port = std::stoi(port_hex, nullptr, 16); }
            catch (...) { continue; }

            // Only track ports we're interested in
            if (watch_ports_.find(port) == watch_ports_.end()) continue;

            PortInfo info;
            info.port = port;
            info.protocol = proto;
            info.address = local_addr.substr(0, colon_pos);

            // State: 0A = LISTEN, 01 = ESTABLISHED
            if (state == "0A") {
                info.state = "LISTEN";
            } else if (state == "01") {
                info.state = "ESTABLISHED";
            } else {
                info.state = "OTHER";
            }

            info.process = "unknown";
            result[port] = info;
        }
    };

    parse_proc_net("/proc/net/tcp", "TCP");
    parse_proc_net("/proc/net/tcp6", "TCP6");
    parse_proc_net("/proc/net/udp", "UDP");

#elif defined(__APPLE__) || defined(__FreeBSD__)
    // Use netstat on macOS/BSD
    // This is a simplified implementation
    FILE* fp = popen("netstat -an -p tcp 2>/dev/null", "r");
    if (fp) {
        char buffer[1024];
        while (fgets(buffer, sizeof(buffer), fp)) {
            std::string line(buffer);
            // Parse netstat output for listening ports
            // Format: tcp4/6 0 0 *.port *.* LISTEN
            for (int port : watch_ports_) {
                if (line.find("." + std::to_string(port)) != std::string::npos) {
                    PortInfo info;
                    info.port = port;
                    info.protocol = "TCP";
                    info.state = line.find("LISTEN") != std::string::npos ? "LISTEN" : "OTHER";
                    info.address = "*";
                    info.process = "unknown";
                    result[port] = info;
                }
            }
        }
        pclose(fp);
    }
#endif

    return result;
}

void PortMonitor::poll() {
    static std::map<int, PortInfo> last_snapshot;

    auto current = snapshot_ports();

    // Detect new listening ports
    for (const auto& [port, info] : current) {
        if (last_snapshot.find(port) == last_snapshot.end()) {
            Event e;
            e.category = EventCategory::Network;
            e.type = EventType::NetworkConnectionOpened;
            e.source = "port_monitor";
            e.target = std::to_string(port);
            e.summary = "Port " + std::to_string(port) + " is now " + info.state;
            e.attributes["protocol"] = info.protocol;
            e.attributes["state"] = info.state;
            e.attributes["address"] = info.address;
            emit(e);
        }
    }

    // Detect closed ports
    for (const auto& [port, info] : last_snapshot) {
        if (current.find(port) == current.end()) {
            Event e;
            e.category = EventCategory::Network;
            e.type = EventType::NetworkConnectionClosed;
            e.source = "port_monitor";
            e.target = std::to_string(port);
            e.summary = "Port " + std::to_string(port) + " is no longer listening";
            e.attributes["protocol"] = info.protocol;
            e.attributes["previous_state"] = info.state;
            emit(e);
        }
    }

    // Detect state changes
    for (const auto& [port, info] : current) {
        auto it = last_snapshot.find(port);
        if (it != last_snapshot.end() && it->second.state != info.state) {
            Event e;
            e.category = EventCategory::Network;
            e.type = EventType::NetworkConnectionOpened;
            e.source = "port_monitor";
            e.target = std::to_string(port);
            e.summary = "Port " + std::to_string(port) + " state changed: " + 
                       it->second.state + " -> " + info.state;
            e.attributes["protocol"] = info.protocol;
            e.attributes["previous_state"] = it->second.state;
            e.attributes["new_state"] = info.state;
            emit(e);
        }
    }

    last_snapshot = current;
}

} // namespace monitor
} // namespace changeos
