#pragma once

#include <string>
#include <vector>

namespace changeos {

class MonitorEngine;

namespace diagnostic {

struct DiagnosticResult {
    int total = 0;
    int available = 0;
    int unavailable = 0;
    bool storage_ok = false;
    bool reporter_ok = false;
    bool alert_ok = false;
    bool filter_ok = false;
    bool stats_ok = false;
    bool security_ok = false;
    bool webhook_ok = false;
};

class DiagnosticRunner {
public:
    // Run a self-test against an initialized MonitorEngine and write a
    // human-readable report to the given path (or stdout if empty).
    // Does NOT start the monitors; only inspects their availability and the
    // state of the engine's sub-systems.
    static DiagnosticResult run(MonitorEngine& engine,
                                const std::string& output_path);
};

} // namespace diagnostic
} // namespace changeos
