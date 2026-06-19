#include "monitor/system_config/system_config_monitor.h"

#include "platform/platform_detection.h"
#include "utils/logger.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace changeos {
namespace monitor {

namespace {

std::int64_t simple_hash(const std::string& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return -1;
    std::ifstream file(path, std::ios::binary);
    if (!file) return -1;
    std::int64_t h = 0;
    char c;
    while (file.get(c)) {
        h = (h * 31) + static_cast<std::uint8_t>(c);
    }
    auto ftime = std::filesystem::last_write_time(path, ec);
    if (!ec) {
        auto since_epoch = ftime.time_since_epoch();
        h ^= std::chrono::duration_cast<std::chrono::seconds>(
            since_epoch).count();
    }
    return h;
}

} // namespace

SystemConfigMonitor::SystemConfigMonitor() = default;
SystemConfigMonitor::~SystemConfigMonitor() { stop(); }

void SystemConfigMonitor::add_config_file(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (std::find(config_files_.begin(), config_files_.end(), path)
        == config_files_.end()) {
        config_files_.push_back(path);
    }
}

std::vector<std::string> SystemConfigMonitor::config_files() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_files_;
}

bool SystemConfigMonitor::on_start() {
    COS_LOG_INFO("System config monitor starting");
    if (config_files_.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
#if defined(COS_PLATFORM_LINUX) || defined(COS_PLATFORM_UNIX)
        config_files_.push_back("/etc/hosts");
        config_files_.push_back("/etc/resolv.conf");
        config_files_.push_back("/etc/passwd");
        config_files_.push_back("/etc/hostname");
#elif defined(COS_PLATFORM_MACOS)
        config_files_.push_back("/etc/hosts");
        config_files_.push_back("/etc/resolv.conf");
#endif
    }
    first_scan_.store(true);
    runner_ = std::make_unique<utils::PeriodicRunner>();
    runner_->start([this]() { tick(); },
                   std::chrono::milliseconds(poll_interval_ms_));
    return true;
}

bool SystemConfigMonitor::on_stop() {
    if (runner_) runner_->stop();
    runner_.reset();
    COS_LOG_INFO("System config monitor stopped");
    return true;
}

void SystemConfigMonitor::tick() {
    std::vector<std::string> files;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        files = config_files_;
    }

    std::map<std::string, std::int64_t> current;
    for (auto& f : files) current[f] = simple_hash(f);

    if (first_scan_.exchange(false)) {
        previous_hashes_ = std::move(current);
        return;
    }

    for (const auto& [path, h] : current) {
        auto it = previous_hashes_.find(path);
        if (it == previous_hashes_.end() || it->second != h) {
            Event ev;
            ev.category = EventCategory::SystemConfig;
            ev.type = EventType::ConfigValueChanged;
            ev.source = "system_config";
            ev.target = path;
            ev.summary = "Config file changed: " + path;
            ev.attributes["file"] = path;
            ev.platform = platform::name();
            ev.host = platform::hostname();
            emit(ev);
        }
    }

    previous_hashes_ = std::move(current);
}

} // namespace monitor
} // namespace changeos
