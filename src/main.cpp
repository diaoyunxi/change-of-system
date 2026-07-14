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

// 确保 COS_VERSION 宏已定义（由 CMake 传递，回退为 "unknown"）
#ifndef COS_VERSION
#define COS_VERSION "unknown"
#endif

namespace {

/// 版本号（从 CMakeLists.txt 的 PROJECT_VERSION 宏获取，保证一致性）
static const char* VERSION = COS_VERSION;

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

// 检查是否为一次性/非交互模式（跳过更新检查）
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

/// 命令行参数解析结果结构体
struct ParsedArgs {
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

    // tail / validate-config / snapshot-diff / info
    bool tail_mode = false;
    changeos::tail::TailOptions tail_opts;
    bool validate_config_mode = false;
    std::string validate_config_path;
    bool snapshot_diff_mode = false;
    changeos::snapshot_diff::DiffOptions snapshot_diff_opts;
    bool info_mode = false;
    changeos::info::InfoOptions info_opts;

    bool show_help = false;
    bool show_version = false;
    bool parse_error = false;
};

/// 解析命令行参数，提取到独立函数以降低 main() 复杂度
ParsedArgs parse_args(int argc, char** argv) {
    ParsedArgs args;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            args.show_help = true;
        } else if (arg == "-V" || arg == "--version") {
            args.show_version = true;
        } else if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            args.config_path = argv[++i];
        } else if ((arg == "-d" || arg == "--daemon")) {
            args.daemon_mode = true;
        } else if (arg == "--pid-file" && i + 1 < argc) {
            args.pid_file = argv[++i];
        } else if (arg == "--export" && i + 1 < argc) {
            args.export_path = argv[++i];
        } else if (arg == "--report" && i + 1 < argc) {
            args.report_path = argv[++i];
        } else if (arg == "--reload-config") {
            args.reload_config = true;
        } else if (arg == "--snapshot") {
            args.snapshot_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                args.snapshot_path = argv[++i];
            }
        } else if (arg == "--diagnose") {
            args.diagnose_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                args.diagnose_path = argv[++i];
            }
        } else if (arg == "--query") {
            args.query_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                args.query_keyword = argv[++i];
            }
        } else if (arg == "--query-category" && i + 1 < argc) {
            args.query_opts.category = argv[++i];
        } else if (arg == "--query-source" && i + 1 < argc) {
            args.query_opts.source = argv[++i];
        } else if (arg == "--query-from" && i + 1 < argc) {
            try { args.query_opts.from_unix_ms = std::stoll(argv[++i]); }
            catch (...) { std::cerr << "Invalid --query-from value\n"; args.parse_error = true; }
        } else if (arg == "--query-to" && i + 1 < argc) {
            try { args.query_opts.to_unix_ms = std::stoll(argv[++i]); }
            catch (...) { std::cerr << "Invalid --query-to value\n"; args.parse_error = true; }
        } else if (arg == "--query-limit" && i + 1 < argc) {
            try { args.query_opts.limit = std::stoi(argv[++i]); }
            catch (...) { std::cerr << "Invalid --query-limit value\n"; args.parse_error = true; }
        } else if (arg == "--query-offset" && i + 1 < argc) {
            try { args.query_opts.offset = std::stoi(argv[++i]); }
            catch (...) { std::cerr << "Invalid --query-offset value\n"; args.parse_error = true; }
        } else if (arg == "--query-json") {
            args.query_opts.json_output = true;
        } else if (arg == "--tail") {
            args.tail_mode = true;
        } else if (arg == "--tail-category" && i + 1 < argc) {
            args.tail_opts.category_filter = argv[++i];
        } else if (arg == "--tail-source" && i + 1 < argc) {
            args.tail_opts.source_filter = argv[++i];
        } else if (arg == "--tail-keyword" && i + 1 < argc) {
            args.tail_opts.keyword_filter = argv[++i];
        } else if (arg == "--tail-recent" && i + 1 < argc) {
            try { args.tail_opts.initial_recent = std::stoi(argv[++i]); }
            catch (...) { std::cerr << "Invalid --tail-recent value\n"; args.parse_error = true; }
        } else if (arg == "--tail-color") {
            args.tail_opts.color = true;
        } else if (arg == "--tail-json") {
            args.tail_opts.json = true;
        } else if (arg == "--validate-config") {
            args.validate_config_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                args.validate_config_path = argv[++i];
            }
        } else if (arg == "--snapshot-diff") {
            args.snapshot_diff_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                args.snapshot_diff_opts.path_a = argv[++i];
            }
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                args.snapshot_diff_opts.path_b = argv[++i];
            }
        } else if (arg == "--snapshot-diff-json") {
            args.snapshot_diff_opts.json = true;
        } else if (arg == "--snapshot-diff-verbose") {
            args.snapshot_diff_opts.verbose = true;
        } else if (arg == "--info") {
            args.info_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                std::string section = argv[++i];
                if (section == "json") args.info_opts.json = true;
                else if (section == "color") args.info_opts.color = true;
            }
        } else if (arg == "--info-json") {
            args.info_mode = true;
            args.info_opts.json = true;
        } else if (arg == "--info-color") {
            args.info_mode = true;
            args.info_opts.color = true;
        }
    }

    return args;
}

} // namespace

int main(int argc, char** argv) {
    // 对一次性命令跳过交互式更新检查
    if (!is_one_shot_mode(argc, argv)) {
        auto update_info = changeos::updater::check_for_update(VERSION);
        changeos::updater::prompt_update(update_info);
    }

    // 解析命令行参数（提取为独立函数，降低 main() 复杂度）
    ParsedArgs args = parse_args(argc, argv);

    if (args.show_help) {
        print_usage(argv[0]);
        return 0;
    }
    if (args.show_version) {
        std::cout << "change-of-system v" << VERSION << "\n";
        return 0;
    }
    if (args.parse_error) {
        return 1;
    }

    // --- 配置验证模式：检查配置文件并退出 ---
    if (args.validate_config_mode) {
        std::string path = args.validate_config_path;
        if (path.empty()) path = args.config_path;
        if (path.empty()) path = "config.ini";
        auto result = changeos::validate::ConfigValidator::validate(path);
        int errors = changeos::validate::ConfigValidator::print_report(result, path);
        return errors > 0 ? 1 : 0;
    }

    // --- 快照差异模式：比较两个快照并退出 ---
    if (args.snapshot_diff_mode) {
        return changeos::snapshot_diff::SnapshotDiff::run(args.snapshot_diff_opts);
    }

    // --- 系统信息模式：打印快速摘要并退出 ---
    if (args.info_mode) {
        return changeos::info::SystemInfo::run(args.info_opts);
    }

    // --- 快照模式：捕获状态并退出（不启动引擎）---
    if (args.snapshot_mode) {
        changeos::MonitorEngine engine;
        if (!engine.initialize(args.config_path)) {
            std::cerr << "Failed to initialize monitor engine\n";
            return 1;
        }
        changeos::snapshot::SnapshotConfig sc;
        sc.output_path = (args.snapshot_path.empty() || args.snapshot_path == "-")
                         ? std::string() : args.snapshot_path;
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

    // --- 诊断模式：自测并退出 ---
    if (args.diagnose_mode) {
        changeos::MonitorEngine engine;
        if (!engine.initialize(args.config_path)) {
            std::cerr << "Failed to initialize monitor engine\n";
            return 1;
        }
        auto result = engine.run_diagnostic(args.diagnose_path);
        return result.unavailable > 0 ? 2 : 0;
    }

    // --- 查询模式：搜索存储并退出 ---
    if (args.query_mode) {
        changeos::MonitorEngine engine;
        if (!engine.initialize(args.config_path)) {
            std::cerr << "Failed to initialize monitor engine\n";
            return 1;
        }
        args.query_opts.keyword = args.query_keyword;
        engine.query_events(args.query_opts);
        return 0;
    }

    // 检查是否已有实例运行
    if (args.daemon_mode && changeos::utils::Daemon::is_already_running(args.pid_file)) {
        std::cerr << "Another instance is already running (PID: "
                  << changeos::utils::Daemon::get_running_pid(args.pid_file) << ")\n";
        return 1;
    }

    // 守护进程化
    if (args.daemon_mode) {
        std::cout << "Starting in daemon mode...\n";
        if (!changeos::utils::Daemon::daemonize()) {
            std::cerr << "Failed to daemonize\n";
            return 1;
        }
        if (!changeos::utils::Daemon::write_pid_file(args.pid_file)) {
            std::cerr << "Failed to write PID file\n";
            return 1;
        }
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::signal(SIGHUP, on_sighup);

    changeos::MonitorEngine engine;
    if (!engine.initialize(args.config_path)) {
        std::cerr << "Failed to initialize monitor engine\n";
        return 1;
    }

    // tail 模式下不注册默认 stdout 回调，避免重复打印
    if (!args.tail_mode) {
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

    // --- Tail 模式：进入实时事件流 ---
    if (args.tail_mode) {
        return changeos::tail::TailWatcher::run(engine, args.tail_opts, g_running);
    }

    // 处理导出请求
    if (!args.export_path.empty()) {
        // 添加 npos 检查，防止无扩展名时 substr 溢出
        auto dot_pos = args.export_path.find_last_of('.');
        std::string ext = (dot_pos != std::string::npos)
            ? args.export_path.substr(dot_pos + 1) : "";
        changeos::export_::ExportFormat format = changeos::export_::ExportFormat::CSV;
        if (ext == "json") {
            format = changeos::export_::ExportFormat::JSON;
        }

        if (engine.export_events(args.export_path, format)) {
            std::cout << "Events exported to " << args.export_path << "\n";
        } else {
            std::cerr << "Failed to export events\n";
        }
    }

    // 处理报告生成
    if (!args.report_path.empty()) {
        changeos::report::ReportConfig config;
        config.output_path = args.report_path;

        if (engine.generate_report(config)) {
            std::cout << "Report generated: " << args.report_path << "\n";
        } else {
            std::cerr << "Failed to generate report\n";
        }
    }

    // 处理配置重载
    if (args.reload_config) {
        engine.reload_config();
        std::cout << "Configuration reloaded\n";
    }

    while (g_running.load()) {
        if (g_reload_config.exchange(false)) {
            std::cout << "\nReloading configuration (SIGHUP received)...\n";
            engine.reload_config();
            std::cout << "Configuration reloaded\n";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << "\nShutting down...\n";
    engine.stop_all();

    // 清理 PID 文件
    if (args.daemon_mode) {
        changeos::utils::Daemon::remove_pid_file(args.pid_file);
    }

    std::cout << "Stopped cleanly. Goodbye.\n";
    return 0;
}
