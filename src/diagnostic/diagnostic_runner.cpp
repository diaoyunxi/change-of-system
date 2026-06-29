#include "diagnostic/diagnostic_runner.h"

#include "core/monitor.h"
#include "core/monitor_engine.h"
#include "platform/platform_detection.h"
#include "reporting/reporter.h"
#include "utils/logger.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace changeos {
namespace diagnostic {

namespace {

std::string status_str(bool ok) {
    return ok ? "OK" : "FAIL";
}

} // namespace

DiagnosticResult DiagnosticRunner::run(MonitorEngine& engine,
                                       const std::string& output_path) {
    DiagnosticResult result{};

    std::string report;
    report += "==============================================================\n";
    report += " change-of-system diagnostic report\n";
    report += "==============================================================\n";
    report += " Platform       : " + platform::name() + " (" + platform::architecture() + ")\n";
    report += " Host           : " + platform::hostname() + "\n";
    report += " User           : " + platform::username() + "\n";
    report += " OS version     : " + platform::version() + "\n";
    report += "--------------------------------------------------------------\n";
    report += " Monitor availability:\n";
    report += "--------------------------------------------------------------\n";

    auto monitors = engine.monitors();
    result.total = static_cast<int>(monitors.size());
    for (auto* m : monitors) {
        if (!m) continue;
        bool ok = m->is_available();
        if (ok) ++result.available; else ++result.unavailable;
        char line[256];
        std::snprintf(line, sizeof(line),
                      "  %-22s  [%s]  interval=%dms  native=%s\n",
                      m->name().c_str(),
                      status_str(ok).c_str(),
                      m->poll_interval_ms(),
                      m->supports_native_events() ? "yes" : "no");
        report += line;
        report += "    " + m->description() + "\n";
    }

    report += "--------------------------------------------------------------\n";
    report += " Sub-system status:\n";
    report += "--------------------------------------------------------------\n";

    auto* storage = engine.storage();
    result.storage_ok = (storage != nullptr && storage->is_open());
    char sl[128];
    std::snprintf(sl, sizeof(sl), "  %-22s  [%s]  events=%lld\n",
                  "storage", status_str(result.storage_ok).c_str(),
                  storage ? static_cast<long long>(storage->count()) : 0LL);
    report += sl;

    auto* reporter = engine.reporter();
    result.reporter_ok = (reporter != nullptr);
    report += "  ";
    report += "reporter";
    report += "            [";
    report += status_str(result.reporter_ok);
    report += "]  enabled=";
    report += (reporter && reporter->enabled()) ? "yes" : "no";
    report += "\n";

    auto* alert = engine.alert_manager();
    result.alert_ok = (alert != nullptr);
    report += "  ";
    report += "alert_manager";
    report += "       [";
    report += status_str(result.alert_ok);
    report += "]\n";

    auto* filter = engine.event_filter();
    result.filter_ok = (filter != nullptr);
    report += "  ";
    report += "event_filter";
    report += "        [";
    report += status_str(result.filter_ok);
    report += "]\n";

    auto* stats = engine.statistics();
    result.stats_ok = (stats != nullptr);
    report += "  ";
    report += "statistics";
    report += "         [";
    report += status_str(result.stats_ok);
    report += "]\n";

    auto* security = engine.security_auditor();
    result.security_ok = (security != nullptr);
    report += "  ";
    report += "security_auditor";
    report += "    [";
    report += status_str(result.security_ok);
    report += "]\n";

    auto* webhook = engine.webhook_notifier();
    result.webhook_ok = (webhook != nullptr);
    report += "  ";
    report += "webhook_notifier";
    report += "    [";
    report += status_str(result.webhook_ok);
    report += "]\n";

    report += "--------------------------------------------------------------\n";
    char summary[256];
    std::snprintf(summary, sizeof(summary),
                  " Summary: %d/%d monitors available, %d unavailable\n",
                  result.available, result.total, result.unavailable);
    report += summary;
    report += "==============================================================\n";

    if (output_path.empty()) {
        std::fwrite(report.data(), 1, report.size(), stdout);
    } else {
        std::ofstream f(output_path, std::ios::trunc);
        if (!f) {
            COS_LOG_ERROR("Failed to open diagnostic output file: " + output_path);
            // Fall back to stdout
            std::fwrite(report.data(), 1, report.size(), stdout);
        } else {
            f << report;
            COS_LOG_INFO("Diagnostic report written to " + output_path);
        }
    }

    return result;
}

} // namespace diagnostic
} // namespace changeos
