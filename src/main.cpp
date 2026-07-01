#include "core/monitor_engine.h"
#include "config/config_store.h"
#include "platform/platform_detection.h"
#include "updater/updater.h"
#include "utils/logger.h"
#include "utils/daemon.h"
#include "tail/tail_watcher.h"
#include "validate/config_validator.h"
#include "snapshot_diff/snapshot_diff.h"
#include "info/system_info.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

static const char* VERSION = "0.4.0";

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
        << "  -c, --config <path>       Path to configuration file\n"
        << "  -d, --daemon              Run as daemon (background)\n"
        << "  --pid-file <path>         PID file path (default: /var/run/change-of-system.pid)\n"
        << "  --no-daemon               Run in the foreground (default)\n"
        << "  --export <path>           Export events to file (CSV or JSON)\n"
        << "  --report <path>           Generate report to file\n"
        << "  --reload-config           Reload configuration file\n"
        << "  --snapshot [path]         Capture a one-shot system state snapshot and exit.\n"
        << "                            If path is omitted or '-' writes JSON to stdout.\n"
        << "  --diagnose [path]         Run a self-test of monitors and sub-systems, then exit.\n"
        << "                            If path is omitted writes the report to stdout.\n"
        << "  --query [keyword]         Query stored events and print to stdout.\n"
        << "  --query-category <name>   Filter query by category (filesystem/process/...)\n"
        << "  --query-source <substr>   Filter query by source substring\n"
        << "  --query-from <unix_ms>    Query events at or after this timestamp (ms)\n"
        << "  --query-to <unix_ms>      Query events at or before this timestamp (ms)\n"
        << "  --query-limit <n>         Max events to print (default 50, 0 = unlimited)\n"
        << "  --query-offset <n>        Skip first N matches (default 0)\n"
        << "  --query-json              Output query results as JSON\n"
        << "  --tail                    Live event stream (like tail -f). Starts monitors and\n"
        << "                            prints events as they occur. Press Ctrl+C to stop.\n"
        << "  --tail-category <name>    Filter tail events by category\n"
        << "  --tail-source <substr>    Filter tail events by source substring\n"
        << "  --tail-keyword <substr>   Filter tail events by keyword (summary/target/source)\n"
        << "  --tail-recent <n>         Print last N events before live streaming (default 0)\n"
        << "  --tail-color              Enable ANSI color output for tail mode\n"
        << "  --tail-json               Output tail events as one JSON object per line\n"
        << "  --validate-config [path]  Validate config file and exit. If path omitted uses\n"
        << "                            the value from -c/--config (or config.ini).\n"
        << "                            Exits with 0 if OK, 1 if errors found.\n"
        << "  --snapshot-diff <a> <b>   Compare two snapshot JSON files and print the diff.\n"
        << "                            Exits 0 = identical, 1 = differences, 2 = error.\n"
        << "  --snapshot-diff-json      Output snapshot diff as JSON\n"
        << "  --snapshot-diff-verbose   Show detailed per-item changes in the diff\n"
        << "  --info [section]          Print a quick system info summary and exit.\n"
        << "                            Optional section: 'json' for JSON output, 'color'\n"
        << "                            for colored output, or omit for plain text.\n"
        << "  -V, --version             Show version information and exit\n"
        << "  -h, --help                Show this help message\n"
        << "\n"
        << "Platform: " << changeos::platform::name() << "\n";
}

// Returns true if any of the one-shot / non-interactive modes is requested,
// so the update check can be skipped for them.
bool is_one_shot_mode(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-V" || a == "--version" || a == "-h" || a == "--help" ||
            a == "--snapshot" || a == "--diagnose" || a == "--query" ||
            a == "--export" || a == "--report" || a == "--reload-config" ||
            a == "--validate-config" || a == "--snapshot-diff" ||
            a == "--info") {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    // Skip the interactive update check for one-shot commands.
    if (!is_one_shot_mode(argc, argv)) {
        auto update_info = changeos::updater::check_for_update(VERSION);
        changeos::updater::prompt_update(update_info);
    }

    std::string config_path;
    std::string export_path;
    std::string report_path;
    std::string pid_file = "/var/run/change-of-system.pid";
    std::string snapshot_path;
    bool snapshot_mode = false;
    std::string diagnose_path;
    bool diagnose_mode = false;
    bool query_mode = false;
    std::string query_keyword;
    changeos::query::QueryOptions query_opts;
    bool reload_config = false;
    bool daemon_mode = false;

    // 新功能：tail / validate-config / snapshot-diff / info
    bool tail_mode = false;
    changeos::tail::TailOptions tail_opts;
    bool validate_config_mode = false;
    std::string validate_config_path;
    bool snapshot_diff_mode = false;
    changeos::snapshot_diff::DiffOptions snapshot_diff_opts;
    bool info_mode = false;
    changeos::info::InfoOptions info_opts;

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
        } else if (arg == "--snapshot") {
            snapshot_mode = true;
            // Optional path argument: only consume if next token is not another flag.
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                snapshot_path = argv[++i];
            }
        } else if (arg == "--diagnose") {
            diagnose_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                diagnose_path = argv[++i];
            }
        } else if (arg == "--query") {
            query_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                query_keyword = argv[++i];
            }
        } else if (arg == "--query-category" && i + 1 < argc) {
            query_opts.category = argv[++i];
        } else if (arg == "--query-source" && i + 1 < argc) {
            query_opts.source = argv[++i];
        } else if (arg == "--query-from" && i + 1 < argc) {
            try { query_opts.from_unix_ms = std::stoll(argv[++i]); }
            catch (...) { std::cerr << "Invalid --query-from value\n"; return 1; }
        } else if (arg == "--query-to" && i + 1 < argc) {
            try { query_opts.to_unix_ms = std::stoll(argv[++i]); }
            catch (...) { std::cerr << "Invalid --query-to value\n"; return 1; }
        } else if (arg == "--query-limit" && i + 1 < argc) {
            try { query_opts.limit = std::stoi(argv[++i]); }
            catch (...) { std::cerr << "Invalid --query-limit value\n"; return 1; }
        } else if (arg == "--query-offset" && i + 1 < argc) {
            try { query_opts.offset = std::stoi(argv[++i]); }
            catch (...) { std::cerr << "Invalid --query-offset value\n"; return 1; }
        } else if (arg == "--query-json") {
            query_opts.json_output = true;
        } else if (arg == "--tail") {
            tail_mode = true;
        } else if (arg == "--tail-category" && i + 1 < argc) {
            tail_opts.category_filter = argv[++i];
        } else if (arg == "--tail-source" && i + 1 < argc) {
            tail_opts.source_filter = argv[++i];
        } else if (arg == "--tail-keyword" && i + 1 < argc) {
            tail_opts.keyword_filter = argv[++i];
        } else if (arg == "--tail-recent" && i + 1 < argc) {
            try { tail_opts.initial_recent = std::stoi(argv[++i]); }
            catch (...) { std::cerr << "Invalid --tail-recent value\n"; return 1; }
        } else if (arg == "--tail-color") {
            tail_opts.color = true;
        } else if (arg == "--tail-json") {
            tail_opts.json = true;
        } else if (arg == "--validate-config") {
            validate_config_mode = true;
            // 可选路径参数：仅当下一个 token 不是 flag 时才消费
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                validate_config_path = argv[++i];
            }
        } else if (arg == "--snapshot-diff") {
            snapshot_diff_mode = true;
            // 需要两个路径参数
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                snapshot_diff_opts.path_a = argv[++i];
            }
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                snapshot_diff_opts.path_b = argv[++i];
            }
        } else if (arg == "--snapshot-diff-json") {
            snapshot_diff_opts.json = true;
        } else if (arg == "--snapshot-diff-verbose") {
            snapshot_diff_opts.verbose = true;
        } else if (arg == "--info") {
            info_mode = true;
            // 可选 section：'json' / 'color' / 任意字符串
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                std::string section = argv[++i];
                if (section == "json") info_opts.json = true;
                else if (section == "color") info_opts.color = true;
                // 其它值忽略，向后兼容
            }
        } else if (arg == "--info-json") {
            info_mode = true;
            info_opts.json = true;
        } else if (arg == "--info-color") {
            info_mode = true;
            info_opts.color = true;
        }
    }

    // --- Validate config mode: check config file and exit ----------------
    if (validate_config_mode) {
        // 路径优先级：显式参数 > -c/--config > 默认 config.ini
        std::string path = validate_config_path;
        if (path.empty()) path = config_path;
        if (path.empty()) path = "config.ini";
        auto result = changeos::validate::ConfigValidator::validate(path);
        int errors = changeos::validate::ConfigValidator::print_report(result, path);
        return errors > 0 ? 1 : 0;
    }

    // --- Snapshot diff mode: compare two snapshots and exit --------------
    if (snapshot_diff_mode) {
        return changeos::snapshot_diff::SnapshotDiff::run(snapshot_diff_opts);
    }

    // --- System info mode: print quick summary and exit ------------------
    if (info_mode) {
        return changeos::info::SystemInfo::run(info_opts);
    }

    // --- Snapshot mode: capture state and exit (no engine start) ----------
    if (snapshot_mode) {
        changeos::MonitorEngine engine;
        if (!engine.initialize(config_path)) {
            std::cerr << "Failed to initialize monitor engine\n";
            return 1;
        }
        changeos::snapshot::SnapshotConfig sc;
        sc.output_path = (snapshot_path.empty() || snapshot_path == "-")
                         ? std::string() : snapshot_path;
        auto& cfg = changeos::config::ConfigStore::instance();
        sc.max_processes = cfg.get_int("snapshot.max_processes", 50);
        sc.include_processes = cfg.get_bool("snapshot.include_processes", true);
        sc.include_network = cfg.get_bool("snapshot.include_network", true);
        sc.include_ports = cfg.get_bool("snapshot.include_ports", true);
        sc.include_disk = cfg.get_bool("snapshot.include_disk", true);
        sc.include_load = cfg.get_bool("snapshot.include_load", true);
        sc.include_environment = cfg.get_bool("snapshot.include_environment", true);

        if (!engine.capture_snapshot(sc)) {
            std::cerr << "Failed to capture snapshot\n";
            return 1;
        }
        return 0;
    }

    // --- Diagnostic mode: self-test and exit -----------------------------
    if (diagnose_mode) {
        changeos::MonitorEngine engine;
        if (!engine.initialize(config_path)) {
            std::cerr << "Failed to initialize monitor engine\n";
            return 1;
        }
        auto result = engine.run_diagnostic(diagnose_path);
        // Non-zero exit if any monitor is unavailable, to aid scripting.
        return result.unavailable > 0 ? 2 : 0;
    }

    // --- Query mode: search storage and exit -----------------------------
    if (query_mode) {
        changeos::MonitorEngine engine;
        if (!engine.initialize(config_path)) {
            std::cerr << "Failed to initialize monitor engine\n";
            return 1;
        }
        query_opts.keyword = query_keyword;
        engine.query_events(query_opts);
        return 0;
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

    // 在 tail 模式下不注册默认的 stdout 回调，避免事件被重复打印；
    // TailWatcher 内部会注册自己的回调。
    if (!tail_mode) {
        engine.on_event([](const changeos::Event& e) {
            std::cout << "[EVENT] " << changeos::category_name(e.category)
                      << " | " << changeos::type_name(e.type)
                      << " | " << e.target
                      << " | " << e.summary << "\n";
        });
    }

    if (!engine.start_all()) {
        std::cerr << "Failed to start monitor engine\n";
        return 1;
    }

    std::cout << "change-of-system started on " << changeos::platform::name()
              << " (" << changeos::platform::architecture() << ")\n";
    std::cout << "Host: " << changeos::platform::hostname()
              << " | User: " << changeos::platform::username() << "\n";
    std::cout << "Press Ctrl+C to stop...\n";

    // --- Tail mode: 进入实时事件流，阻塞直到收到退出信号 ---------------
    if (tail_mode) {
        // 静音默认的启动横幅已在上方打印，tail 自身有更聚焦的提示
        return changeos::tail::TailWatcher::run(engine, tail_opts, g_running);
    }

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
