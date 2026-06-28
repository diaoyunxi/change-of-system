#include "core/monitor_engine.h"
#include "platform/platform_detection.h"
#include "updater/updater.h"
#include "utils/logger.h"
#include "utils/daemon.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

static const char* VERSION = "0.2.0";

std::atomic<bool> g_running{true};
std::atomic<bool> g_reload_config{false};

void on_signal(int) {
    g_running.store(false);
}

void on_sighup(int) {
    g_reload_config.store(true);
}

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
        << "Options:\n"
        << "  -c, --config <path>   Path to configuration file\n"
        << "  -d, --daemon          Run as daemon (background)\n"
        << "  --pid-file <path>     PID file path (default: /var/run/change-of-system.pid)\n"
        << "  --no-daemon           Run in the foreground (default)\n"
        << "  --export <path>       Export events to file (CSV or JSON)\n"
        << "  --report <path>       Generate report to file\n"
        << "  --reload-config       Reload configuration file\n"
        << "  -V, --version         Show version information and exit\n"
        << "  -h, --help            Show this help message\n"
        << "\n"
        << "Platform: " << changeos::platform::name() << "\n";
}

} // namespace

int main(int argc, char** argv) {
    // Check for updates
    auto update_info = changeos::updater::check_for_update(VERSION);
    changeos::updater::prompt_update(update_info);

    std::string config_path;
    std::string export_path;
    std::string report_path;
    std::string pid_file = "/var/run/change-of-system.pid";
    bool reload_config = false;
    bool daemon_mode = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-V" || arg == "--version") {
            std::cout << "change-of-system v" << VERSION << "\n";
            return 0;
        } else if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            config_path = argv[++i];
        } else if ((arg == "-d" || arg == "--daemon")) {
            daemon_mode = true;
        } else if (arg == "--pid-file" && i + 1 < argc) {
            pid_file = argv[++i];
        } else if (arg == "--export" && i + 1 < argc) {
            export_path = argv[++i];
        } else if (arg == "--report" && i + 1 < argc) {
            report_path = argv[++i];
        } else if (arg == "--reload-config") {
            reload_config = true;
        }
    }

    // Check if already running
    if (daemon_mode && changeos::utils::Daemon::is_already_running(pid_file)) {
        std::cerr << "Another instance is already running (PID: " 
                  << changeos::utils::Daemon::get_running_pid(pid_file) << ")\n";
        return 1;
    }

    // Daemonize if requested
    if (daemon_mode) {
        std::cout << "Starting in daemon mode...\n";
        if (!changeos::utils::Daemon::daemonize()) {
            std::cerr << "Failed to daemonize\n";
            return 1;
        }
        if (!changeos::utils::Daemon::write_pid_file(pid_file)) {
            std::cerr << "Failed to write PID file\n";
            return 1;
        }
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::signal(SIGHUP, on_sighup);

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
        // Check for SIGHUP reload request
        if (g_reload_config.exchange(false)) {
            std::cout << "\nReloading configuration (SIGHUP received)...\n";
            engine.reload_config();
            std::cout << "Configuration reloaded\n";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << "\nShutting down...\n";
    engine.stop_all();
    
    // Clean up PID file
    if (daemon_mode) {
        changeos::utils::Daemon::remove_pid_file(pid_file);
    }
    
    std::cout << "Stopped cleanly. Goodbye.\n";
    return 0;
}
