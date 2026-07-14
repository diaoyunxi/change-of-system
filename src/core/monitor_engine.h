#pragma once

#include "core/event.h"
#include "core/monitor.h"
#include "alert/alert_manager.h"
#include "filter/event_filter.h"
#include "stats/statistics.h"
#include "security/security_auditor.h"
#include "webhook/webhook_notifier.h"
#include "export/event_exporter.h"
#include "report/report_generator.h"
#include "config/config_watcher.h"
#include "snapshot/snapshot_generator.h"
#include "query/event_query.h"
#include "diagnostic/diagnostic_runner.h"

#include <atomic>
#include <deque>
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
class UserActivityMonitor;
class ServiceMonitor;
class FileIntegrityMonitor;
class UsbDeviceMonitor;
class DiskSpaceMonitor;
class SystemLoadMonitor;
class LogMonitor;
class PortMonitor;
class PackageMonitor;
class EnvironmentMonitor;
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
    monitor::UserActivityMonitor& user_activity_monitor();
    monitor::ServiceMonitor& service_monitor();
    monitor::FileIntegrityMonitor& file_integrity_monitor();
    monitor::UsbDeviceMonitor& usb_device_monitor();
    monitor::DiskSpaceMonitor& disk_space_monitor();
    monitor::SystemLoadMonitor& system_load_monitor();
    monitor::LogMonitor& log_monitor();
    monitor::PortMonitor& port_monitor();
    monitor::PackageMonitor& package_monitor();
    monitor::EnvironmentMonitor& environment_monitor();

    storage::Storage* storage();
    reporting::Reporter* reporter();
    alert::AlertManager* alert_manager();
    filter::EventFilter* event_filter();
    stats::StatisticsCollector* statistics();
    security::SecurityAuditor* security_auditor();
    webhook::WebhookNotifier* webhook_notifier();

    // Returns pointers to every registered monitor. Pointers remain valid for
    // the lifetime of the engine. Used by the diagnostic runner and any tool
    // that needs to inspect monitor state without starting them.
    std::vector<Monitor*> monitors() const;

    std::vector<Event> recent_events(int limit = 100) const;

    // Export events to file
    bool export_events(const std::string& output_path, export_::ExportFormat format);
    // Generate a report
    bool generate_report(const report::ReportConfig& config);
    // Reload configuration
    void reload_config();
    // Enable config file watching
    void enable_config_watch();
    void disable_config_watch();

    // One-shot system state snapshot written as JSON.
    bool capture_snapshot(const snapshot::SnapshotConfig& config);
    // Query stored events and print results to stdout.
    int query_events(const query::QueryOptions& opts);
    // Run a diagnostic self-test and write a report (empty path = stdout).
    diagnostic::DiagnosticResult run_diagnostic(const std::string& output_path);

private:
    void route_event(const Event& event);
    // initialize 的子方法，拆分过长的初始化逻辑（优化建议21）
    void initialize_monitors_(config::ConfigStore& cfg);
    void initialize_storage_(config::ConfigStore& cfg);
    void initialize_subsystems_(config::ConfigStore& cfg, const std::string& config_path);

    std::unique_ptr<monitor::FilesystemMonitor> fs_;
    std::unique_ptr<monitor::ProcessMonitor> proc_;
    std::unique_ptr<monitor::NetworkMonitor> net_;
    std::unique_ptr<monitor::SystemConfigMonitor> cfg_;
    std::unique_ptr<monitor::UserActivityMonitor> user_;
    std::unique_ptr<monitor::ServiceMonitor> service_;
    std::unique_ptr<monitor::FileIntegrityMonitor> integrity_;
    std::unique_ptr<monitor::UsbDeviceMonitor> usb_;
    std::unique_ptr<monitor::DiskSpaceMonitor> disk_;
    std::unique_ptr<monitor::SystemLoadMonitor> load_;
    std::unique_ptr<monitor::LogMonitor> log_;
    std::unique_ptr<monitor::PortMonitor> port_;
    std::unique_ptr<monitor::PackageMonitor> pkg_;
    std::unique_ptr<monitor::EnvironmentMonitor> env_;

    std::unique_ptr<storage::Storage> storage_;
    std::unique_ptr<reporting::Reporter> reporter_;
    std::unique_ptr<alert::AlertManager> alert_manager_;
    std::unique_ptr<filter::EventFilter> event_filter_;
    std::unique_ptr<stats::StatisticsCollector> statistics_;
    std::unique_ptr<security::SecurityAuditor> security_auditor_;
    std::unique_ptr<webhook::WebhookNotifier> webhook_notifier_;
    std::unique_ptr<config::ConfigWatcher> config_watcher_;

    mutable std::mutex callbacks_mutex_;
    std::vector<EventCallback> callbacks_;

    mutable std::mutex recent_mutex_;
    // 使用 std::deque 替代 std::vector，实现 O(1) 头部删除
    std::deque<Event> recent_;
    std::atomic<bool> running_{false};
};

} // namespace changeos
