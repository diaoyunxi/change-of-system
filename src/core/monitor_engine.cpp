#include "core/monitor_engine.h"

#include "config/config_store.h"
#include "monitor/filesystem/filesystem_monitor.h"
#include "monitor/network/network_monitor.h"
#include "monitor/process/process_monitor.h"
#include "monitor/system_config/system_config_monitor.h"
#include "reporting/reporter.h"
#include "storage/storage.h"
#include "utils/logger.h"

namespace changeos {

MonitorEngine::MonitorEngine() = default;
MonitorEngine::~MonitorEngine() {
    stop_all();
}

bool MonitorEngine::initialize(const std::string& config_path) {
    auto& cfg = config::ConfigStore::instance();
    if (!config_path.empty()) cfg.load(config_path);

    fs_ = std::make_unique<monitor::FilesystemMonitor>();
    proc_ = std::make_unique<monitor::ProcessMonitor>();
    net_ = std::make_unique<monitor::NetworkMonitor>();
    cfg_ = std::make_unique<monitor::SystemConfigMonitor>();

    int fs_interval = cfg.get_int("filesystem.poll_interval_ms", 3000);
    int proc_interval = cfg.get_int("process.poll_interval_ms", 3000);
    int net_interval = cfg.get_int("network.poll_interval_ms", 5000);
    int sys_interval = cfg.get_int("system_config.poll_interval_ms", 10000);

    fs_->set_poll_interval_ms(fs_interval);
    proc_->set_poll_interval_ms(proc_interval);
    net_->set_poll_interval_ms(net_interval);
    cfg_->set_poll_interval_ms(sys_interval);

    auto watch_paths = cfg.get_list("filesystem.watch_paths", {"/tmp"});
    for (auto& p : watch_paths) fs_->add_watch_path(p);

    storage_ = storage::create_sqlite_storage();
    std::string storage_path = cfg.get("storage.database_path",
                                        "change-of-system.log");
    if (!storage_->open(storage_path)) {
        COS_LOG_WARN("Failed to open storage backend: " + storage_path);
    } else {
        COS_LOG_INFO("Storage backend opened: " + storage_path);
    }

    reporter_ = std::make_unique<reporting::Reporter>();
    reporter_->configure(
        cfg.get("reporting.endpoint"),
        cfg.get("reporting.api_key"),
        cfg.get_int("reporting.batch_size", 100),
        cfg.get_int("reporting.interval_ms", 10000));
    reporter_->set_enabled(cfg.get_bool("reporting.enabled", false));

    auto route = [this](const Event& e) { route_event(e); };
    fs_->on_event(route);
    proc_->on_event(route);
    net_->on_event(route);
    cfg_->on_event(route);

    return true;
}

bool MonitorEngine::start_all() {
    if (running_.exchange(true)) return false;
    COS_LOG_INFO("MonitorEngine starting all monitors");
    if (fs_ && fs_->is_available()) fs_->start();
    if (proc_ && proc_->is_available()) proc_->start();
    if (net_ && net_->is_available()) net_->start();
    if (cfg_ && cfg_->is_available()) cfg_->start();
    if (reporter_) reporter_->start();
    return true;
}

bool MonitorEngine::stop_all() {
    if (!running_.exchange(false)) return false;
    COS_LOG_INFO("MonitorEngine stopping all monitors");
    if (fs_) fs_->stop();
    if (proc_) proc_->stop();
    if (net_) net_->stop();
    if (cfg_) cfg_->stop();
    if (reporter_) reporter_->stop();
    if (storage_) storage_->close();
    return true;
}

void MonitorEngine::on_event(EventCallback cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    callbacks_.push_back(std::move(cb));
}

void MonitorEngine::route_event(const Event& event) {
    if (storage_) storage_->insert(event);
    if (reporter_ && reporter_->enabled()) reporter_->enqueue(event);

    {
        std::lock_guard<std::mutex> lock(recent_mutex_);
        recent_.push_back(event);
        if (recent_.size() > 5000) recent_.erase(recent_.begin(),
                                                 recent_.end() - 5000);
    }

    std::vector<EventCallback> snapshot;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        snapshot = callbacks_;
    }
    for (auto& cb : snapshot) {
        try { cb(event); } catch (...) {}
    }
}

monitor::FilesystemMonitor& MonitorEngine::filesystem_monitor() { return *fs_; }
monitor::ProcessMonitor& MonitorEngine::process_monitor() { return *proc_; }
monitor::NetworkMonitor& MonitorEngine::network_monitor() { return *net_; }
monitor::SystemConfigMonitor& MonitorEngine::system_config_monitor() { return *cfg_; }

storage::Storage* MonitorEngine::storage() { return storage_.get(); }
reporting::Reporter* MonitorEngine::reporter() { return reporter_.get(); }

std::vector<Event> MonitorEngine::recent_events(int limit) const {
    std::lock_guard<std::mutex> lock(recent_mutex_);
    int start = static_cast<int>(recent_.size()) - limit;
    if (start < 0) start = 0;
    return std::vector<Event>(recent_.begin() + start, recent_.end());
}

} // namespace changeos
