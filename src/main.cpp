#include "core/monitor_engine.h"
#include "monitor/filesystem/filesystem_monitor.h"
#include "monitor/network/network_monitor.h"
#include "monitor/process/process_monitor.h"
#include "monitor/system_config/system_config_monitor.h"
#include "platform/platform_detection.h"
#include "storage/storage.h"
#include "utils/logger.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {
std::atomic<bool> g_running{true};

void on_signal(int) {
    g_running.store(false);
}

void print_usage(const char* prog) {
    std::cout << "change-of-system - Cross-platform system change monitor\n\n"
        << "Usage: " << prog << " [OPTIONS] [COMMAND]\n\n"
        << "Commands:\n"
        << "  run [OPTIONS]       Start monitoring (default if no command given)\n"
        << "  query [OPTIONS]     Query historical events from storage\n"
        << "  status              Show system info and storage statistics\n"
        << "  export [OPTIONS]    Export events to JSON file\n"
        << "  help                Show this help message\n"
        << "\n"
        << "Global Options:\n"
        << "  -c, --config <path>   Path to configuration file\n"
        << "  -h, --help            Show this help message\n"
        << "  --version             Show version information\n"
        << "\n"
        << "Run Options:\n"
        << "  --no-daemon           Run in the foreground (default)\n"
        << "  --json                Output events as JSON (one per line)\n"
        << "  --pretty              Pretty-print JSON output\n"
        << "\n"
        << "Query/Export Options:\n"
        << "  --from <ms>           Filter events from timestamp (Unix milliseconds)\n"
        << "  --to <ms>             Filter events to timestamp (Unix milliseconds)\n"
        << "  --keyword <str>       Filter by keyword in summary or target\n"
        << "  --category <str>      Filter by category (filesystem, process, network, system_config)\n"
        << "  --limit <n>           Limit number of results (default: 100)\n"
        << "  --offset <n>          Skip first N results (default: 0)\n"
        << "  --json                Output as JSON\n"
        << "  --pretty              Pretty-print JSON output\n"
        << "  -o, --output <path>   Output file path (for export command)\n"
        << "\n"
        << "Platform: " << changeos::platform::name() << "\n";
}

void print_version() {
    std::cout << "change-of-system version 0.1.0\n"
              << "Platform: " << changeos::platform::name() << " (" 
              << changeos::platform::architecture() << ")\n";
}

int cmd_run(const std::string& config_path, bool json_output, bool pretty) {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    changeos::MonitorEngine engine;
    if (!engine.initialize(config_path)) {
        std::cerr << "Failed to initialize monitor engine\n";
        return 1;
    }

    engine.on_event([&](const changeos::Event& e) {
        if (json_output) {
            std::cout << changeos::to_json(e, pretty) << "\n";
        } else {
            std::cout << "[EVENT] " << changeos::category_name(e.category)
                      << " | " << changeos::type_name(e.type)
                      << " | " << e.target
                      << " | " << e.summary << "\n";
        }
        std::cout.flush();
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

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << "\nShutting down...\n";
    engine.stop_all();
    std::cout << "Stopped cleanly. Goodbye.\n";
    return 0;
}

int cmd_query(const std::string& config_path, 
              const changeos::storage::QueryFilter& filter,
              bool json_output, bool pretty) {
    changeos::MonitorEngine engine;
    if (!engine.initialize(config_path)) {
        std::cerr << "Failed to initialize monitor engine\n";
        return 1;
    }

    auto* storage = engine.storage();
    if (!storage || !storage->is_open()) {
        std::cerr << "Storage backend not available\n";
        return 1;
    }

    auto events = storage->query(filter);

    if (json_output) {
        std::cout << changeos::to_json(events, pretty) << "\n";
    } else {
        for (const auto& e : events) {
            std::cout << "[" << changeos::to_unix_ms(e.timestamp) << "] "
                      << "[" << changeos::category_name(e.category) << "] "
                      << "[" << changeos::type_name(e.type) << "] "
                      << e.target << " - " << e.summary << "\n";
        }
        std::cout << "\nTotal: " << events.size() << " events\n";
    }

    return 0;
}

int cmd_status(const std::string& config_path) {
    changeos::MonitorEngine engine;
    if (!engine.initialize(config_path)) {
        std::cerr << "Failed to initialize monitor engine\n";
        return 1;
    }

    std::cout << "=== System Information ===\n"
              << "Platform: " << changeos::platform::name() << "\n"
              << "Architecture: " << changeos::platform::architecture() << "\n"
              << "Hostname: " << changeos::platform::hostname() << "\n"
              << "Username: " << changeos::platform::username() << "\n"
              << "OS Version: " << changeos::platform::version() << "\n"
              << "\n";

    auto* storage = engine.storage();
    if (storage && storage->is_open()) {
        std::cout << "=== Storage Statistics ===\n"
                  << "Status: Open\n"
                  << "Total events: " << storage->count() << "\n";
    } else {
        std::cout << "=== Storage Statistics ===\n"
                  << "Status: Not available\n";
    }

    std::cout << "\n=== Monitor Status ===\n"
              << "Engine running: " << (engine.is_running() ? "Yes" : "No") << "\n"
              << "Filesystem monitor: " 
              << (engine.filesystem_monitor().is_available() ? "Available" : "Unavailable") << "\n"
              << "Process monitor: " 
              << (engine.process_monitor().is_available() ? "Available" : "Unavailable") << "\n"
              << "Network monitor: " 
              << (engine.network_monitor().is_available() ? "Available" : "Unavailable") << "\n"
              << "System config monitor: " 
              << (engine.system_config_monitor().is_available() ? "Available" : "Unavailable") << "\n";

    return 0;
}

int cmd_export(const std::string& config_path,
               const changeos::storage::QueryFilter& filter,
               const std::string& output_path,
               bool pretty) {
    changeos::MonitorEngine engine;
    if (!engine.initialize(config_path)) {
        std::cerr << "Failed to initialize monitor engine\n";
        return 1;
    }

    auto* storage = engine.storage();
    if (!storage || !storage->is_open()) {
        std::cerr << "Storage backend not available\n";
        return 1;
    }

    auto events = storage->query(filter);
    std::string json = changeos::to_json(events, pretty);

    if (output_path.empty()) {
        std::cout << json << "\n";
    } else {
        std::ofstream out(output_path);
        if (!out) {
            std::cerr << "Failed to open output file: " << output_path << "\n";
            return 1;
        }
        out << json << "\n";
        std::cout << "Exported " << events.size() << " events to " << output_path << "\n";
    }

    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::string config_path;
    std::string command = "run";
    std::string output_path;
    std::string keyword;
    std::string category;
    bool json_output = false;
    bool pretty = false;
    int limit = 100;
    int offset = 0;
    std::int64_t from_ms = 0;
    std::int64_t to_ms = 0;

    // Parse global options and command
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--version") {
            print_version();
            return 0;
        } else if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "run" || arg == "query" || arg == "status" || 
                   arg == "export" || arg == "help") {
            command = arg;
        } else if (arg == "--json") {
            json_output = true;
        } else if (arg == "--pretty") {
            pretty = true;
            json_output = true; // pretty implies json
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            output_path = argv[++i];
        } else if (arg == "--from" && i + 1 < argc) {
            try { from_ms = std::stoll(argv[++i]); } catch (...) {}
        } else if (arg == "--to" && i + 1 < argc) {
            try { to_ms = std::stoll(argv[++i]); } catch (...) {}
        } else if (arg == "--keyword" && i + 1 < argc) {
            keyword = argv[++i];
        } else if (arg == "--category" && i + 1 < argc) {
            category = argv[++i];
        } else if (arg == "--limit" && i + 1 < argc) {
            try { limit = std::stoi(argv[++i]); } catch (...) {}
        } else if (arg == "--offset" && i + 1 < argc) {
            try { offset = std::stoi(argv[++i]); } catch (...) {}
        }
    }

    if (command == "help") {
        print_usage(argv[0]);
        return 0;
    }

    if (command == "run") {
        return cmd_run(config_path, json_output, pretty);
    } else if (command == "query") {
        changeos::storage::QueryFilter filter;
        filter.from_unix_ms = from_ms;
        filter.to_unix_ms = to_ms;
        filter.keyword = keyword;
        filter.category = category;
        filter.limit = limit;
        filter.offset = offset;
        return cmd_query(config_path, filter, json_output, pretty);
    } else if (command == "status") {
        return cmd_status(config_path);
    } else if (command == "export") {
        changeos::storage::QueryFilter filter;
        filter.from_unix_ms = from_ms;
        filter.to_unix_ms = to_ms;
        filter.keyword = keyword;
        filter.category = category;
        filter.limit = limit;
        filter.offset = offset;
        return cmd_export(config_path, filter, output_path, pretty);
    }

    std::cerr << "Unknown command: " << command << "\n";
    print_usage(argv[0]);
    return 1;
}
