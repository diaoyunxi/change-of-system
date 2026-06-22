#include "core/monitor_engine.h"

#include "config/config_store.h"
#include "monitor/filesystem/filesystem_monitor.h"
#include "monitor/network/network_monitor.h"
#include "monitor/process/process_monitor.h"
#include "monitor/system_config/system_config_monitor.h"
#include "monitor/user_activity/user_activity_monitor.h"
#include "monitor/service/service_monitor.h"
#include "monitor/file_integrity/file_integrity_monitor.h"
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
    user_ = std::make_unique<monitor::UserActivityMonitor>();
    service_ = std::make_unique<monitor::ServiceMonitor>();
    integrity_ = std::make_unique<monitor::FileIntegrityMonitor>();

    int fs_interval = cfg.get_int("filesystem.poll_interval_ms", 3000);
    int proc_interval = cfg.get_int("process.poll_interval_ms", 3000);
    int net_interval = cfg.get_int("network.poll_interval_ms", 5000);
    int sys_interval = cfg.get_int("system_config.poll_interval_ms", 10000);
    int user_interval = cfg.get_int("user_activity.poll_interval_ms", 5000);
    int service_interval = cfg.get_int("service.poll_interval_ms", 10000);
    int integrity_interval = cfg.get_int("file_integrity.poll_interval_ms", 30000);

    fs_->set_poll_interval_ms(fs_interval);
    proc_->set_poll_interval_ms(proc_interval);
    net_->set_poll_interval_ms(net_interval);
    cfg_->set_poll_interval_ms(sys_interval);
    user_->set_poll_interval_ms(user_interval);
    service_->set_poll_interval_ms(service_interval);
    integrity_->set_poll_interval_ms(integrity_interval);

    auto watch_paths = cfg.get_list("filesystem.watch_paths", {"/tmp"});
    for (auto& p : watch_paths) fs_->add_watch_path(p);

    auto integrity_files = cfg.get_list("file_integrity.watch_files", {});
    for (auto& f : integrity_files) integrity_->add_watch_file(f);

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

    // Initialize alert manager
    alert_manager_ = std::make_unique<alert::AlertManager>();
    if (cfg.get_bool("alert.enabled", true)) {
        // Add default alert rules
        alert_manager_->add_rule(alert::rules::high_cpu_usage());
        alert_manager_->add_rule(alert::rules::suspicious_process());
        alert_manager_->add_rule(alert::rules::critical_file_change());
        alert_manager_->add_rule(alert::rules::network_anomaly());
        alert_manager_->add_rule(alert::rules::config_tampering());
        alert_manager_->add_rule(alert::rules::rapid_file_changes());
        COS_LOG_INFO("Alert manager initialized with default rules");
    }

    // Initialize event filter
    event_filter_ = std::make_unique<filter::EventFilter>();
    if (cfg.get_bool("filter.enabled", false)) {
        // Add default filter rules
        event_filter_->add_rule(filter::rules::ignore_temp_files());
        event_filter_->add_rule(filter::rules::ignore_log_files());
        event_filter_->add_rule(filter::rules::ignore_browser_cache());
        COS_LOG_INFO("Event filter initialized with default rules");
    }

    // Initialize statistics collector
    statistics_ = std::make_unique<stats::StatisticsCollector>();
    COS_LOG_INFO("Statistics collector initialized");

    auto route = [this](const Event& e) { route_event(e); };
    fs_->on_event(route);
    proc_->on_event(route);
    net_->on_event(route);
    cfg_->on_event(route);
    user_->on_event(route);
    service_->on_event(route);
    integrity_->on_event(route);

    return true;
}

bool MonitorEngine::start_all() {
    if (running_.exchange(true)) return false;
    COS_LOG_INFO("MonitorEngine starting all monitors");
    if (fs_ && fs_->is_available()) fs_->start();
    if (proc_ && proc_->is_available()) proc_->start();
    if (net_ && net_->is_available()) net_->start();
    if (cfg_ && cfg_->is_available()) cfg_->start();
    if (user_ && user_->is_available()) user_->start();
    if (service_ && service_->is_available()) service_->start();
    if (integrity_ && integrity_->is_available()) integrity_->start();
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
    if (user_) user_->stop();
    if (service_) service_->stop();
    if (integrity_) integrity_->stop();
    if (reporter_) reporter_->stop();
    if (storage_) storage_->close();
    return true;
}

void MonitorEngine::on_event(EventCallback cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    callbacks_.push_back(std::move(cb));
}

void MonitorEngine::route_event(const Event& event) {
    // Apply event filter
    Event filtered_event = event;
    if (event_filter_ && !event_filter_->process(filtered_event)) {
        // Event was filtered out
        return;
    }

    // Record statistics
    if (statistics_) {
        statistics_->record(filtered_event);
    }

    // Process alerts
    if (alert_manager_) {
        alert_manager_->process_event(filtered_event);
    }

    // Store event
    if (storage_) storage_->insert(filtered_event);
    if (reporter_ && reporter_->enabled()) reporter_->enqueue(filtered_event);

    {
        std::lock_guard<std::mutex> lock(recent_mutex_);
        recent_.push_back(filtered_event);
        if (recent_.size() > 5000) recent_.erase(recent_.begin(),
                                                 recent_.end() - 5000);
    }

    std::vector<EventCallback> snapshot;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        snapshot = callbacks_;
    }
    for (auto& cb : snapshot) {
        try { cb(filtered_event); } catch (...) {}
    }
}

monitor::FilesystemMonitor& MonitorEngine::filesystem_monitor() { return *fs_; }
monitor::ProcessMonitor& MonitorEngine::process_monitor() { return *proc_; }
monitor::NetworkMonitor& MonitorEngine::network_monitor() { return *net_; }
monitor::SystemConfigMonitor& MonitorEngine::system_config_monitor() { return *cfg_; }
monitor::UserActivityMonitor& MonitorEngine::user_activity_monitor() { return *user_; }
monitor::ServiceMonitor& MonitorEngine::service_monitor() { return *service_; }
monitor::FileIntegrityMonitor& MonitorEngine::file_integrity_monitor() { return *integrity_; }

storage::Storage* MonitorEngine::storage() { return storage_.get(); }
reporting::Reporter* MonitorEngine::reporter() { return reporter_.get(); }
alert::AlertManager* MonitorEngine::alert_manager() { return alert_manager_.get(); }
filter::EventFilter* MonitorEngine::event_filter() { return event_filter_.get(); }
stats::StatisticsCollector* MonitorEngine::statistics() { return statistics_.get(); }

std::vector<Event> MonitorEngine::recent_events(int limit) const {
    std::lock_guard<std::mutex> lock(recent_mutex_);
    int start = static_cast<int>(recent_.size()) - limit;
    if (start < 0) start = 0;
    return std::vector<Event>(recent_.begin() + start, recent_.end());
}

} // namespace changeos
