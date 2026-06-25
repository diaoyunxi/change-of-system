#include "core/monitor_engine.h"
#include "platform/platform_detection.h"
#include "utils/logger.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {
std::atomic<bool> g_running{true};

void on_signal(int) {
    g_running.store(false);
}

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
        << "Options:\n"
        << "  -c, --config <path>   Path to configuration file\n"
        << "  --no-daemon           Run in the foreground (default)\n"
        << "  --export <path>       Export events to file (CSV or JSON)\n"
        << "  --report <path>       Generate report to file\n"
        << "  --reload-config       Reload configuration file\n"
        << "  -h, --help            Show this help message\n"
        << "\n"
        << "Platform: " << changeos::platform::name() << "\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string config_path;
    std::string export_path;
    std::string report_path;
    bool reload_config = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--export" && i + 1 < argc) {
            export_path = argv[++i];
        } else if (arg == "--report" && i + 1 < argc) {
            report_path = argv[++i];
        } else if (arg == "--reload-config") {
            reload_config = true;
        }
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    changeos::MonitorEngine engine;
    if (!engine.initialize(config_path)) {
        std::cerr << "Failed to initialize monitor engine\n";
        return 1;
    }

    engine.on_event([](const changeos::Event& e) {
        std::cout << "[EVENT] " << changeos::category_name(e.category)
                  << " | " << changeos::type_name(e.type)
                  << " | " << e.target
                  << " | " << e.summary << "\n";
    });

    if (!engine.start_all()) {
        std::cerr << "Failed to start monitor engine\n";
        return 1;
    }

    std::cout << "change-of-system started on " << changeos::platform::name()
              << " (" << changeos::platform::architecture() << ")\n";
    std::cout << "Host: " << changeos::platform::hostname()
              << " | User: " << changeos::platform::username() << "\n";
    std::cout << "Press Ctrl+C to stop...\n";

    // Handle export request
    if (!export_path.empty()) {
        std::string ext = export_path.substr(export_path.find_last_of('.') + 1);
        changeos::export_::ExportFormat format = changeos::export_::ExportFormat::CSV;
        if (ext == "json") {
            format = changeos::export_::ExportFormat::JSON;
        }
        
        if (engine.export_events(export_path, format)) {
            std::cout << "Events exported to " << export_path << "\n";
        } else {
            std::cerr << "Failed to export events\n";
        }
    }

    // Handle report generation
    if (!report_path.empty()) {
        changeos::report::ReportConfig config;
        config.output_path = report_path;
        
        if (engine.generate_report(config)) {
            std::cout << "Report generated: " << report_path << "\n";
        } else {
            std::cerr << "Failed to generate report\n";
        }
    }

    // Handle config reload
    if (reload_config) {
        engine.reload_config();
        std::cout << "Configuration reloaded\n";
    }

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << "\nShutting down...\n";
    engine.stop_all();
    std::cout << "Stopped cleanly. Goodbye.\n";
    return 0;
}
