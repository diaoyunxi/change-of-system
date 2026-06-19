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
        << "  -h, --help            Show this help message\n"
        << "\n"
        << "Platform: " << changeos::platform::name() << "\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string config_path;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            config_path = argv[++i];
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

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << "\nShutting down...\n";
    engine.stop_all();
    std::cout << "Stopped cleanly. Goodbye.\n";
    return 0;
}
