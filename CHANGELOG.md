# Changelog

All notable changes to this project will be documented in this file.

## [0.4.0] - 2026-07-01

### Added

#### Live Tail Mode
- **Tail Watcher**: 实时事件流，类似 `tail -f`
  - 启动所有监控器后将新事件实时打印到 stdout
  - 支持按类别（`--tail-category`）、来源（`--tail-source`）、关键字（`--tail-keyword`）过滤
  - 可选 ANSI 颜色输出（`--tail-color`），按事件严重程度着色（删除/严重=红、新增=绿、修改=黄、用户=青）
  - 可选 JSON 行输出（`--tail-json`），每行一个 JSON 对象，便于 `jq` 管道处理
  - `--tail-recent <n>` 可在开始流式输出前先打印最近 N 条历史事件
  - 按 Ctrl+C 干净退出

#### Config Validation
- **Config Validator**: 启动前预检配置文件
  - 语法检查：缺失 `=`、空键、重复键
  - 类型检查：布尔、整数、浮点、端口列表（0-65535）
  - 语义检查：阈值关系（warning < critical）、轮询间隔为正、< 100ms 给出 CPU 占用警告、reporting/webhook 启用时端点非空、URL 协议合法等
  - 未知键检测：拼写错误的配置键会给出提示
  - 命令行：`--validate-config [path]`（路径优先级：参数 > -c > config.ini）
  - 退出码：0 = 通过，1 = 存在错误（脚本友好）
  - 彩色输出区分 ERROR / WARN / INFO

#### Snapshot Diff
- **Snapshot Diff**: 对比两个快照 JSON 文件并输出差异
  - 内置极简 JSON 解析器，无外部依赖
  - 按主键（pid / port / device+mount / address / name）索引数组元素，智能匹配增删改
  - 比较元数据（host / platform / architecture / user / uptime）
  - 比较 sections：system（对象字段）、disks / processes / connections / listening_ports（数组）、environment
  - 文本输出含进度条式摘要与可选详细条目（`--snapshot-diff-verbose`）
  - JSON 输出（`--snapshot-diff-json`）便于程序化处理
  - 退出码：0 = 无差异，1 = 存在差异，2 = 错误（文件无法读取等）
  - 适用于基线漂移检测、变更审计、前后对比

#### System Info
- **System Info**: 一键式系统信息概览（类似 neofetch 的快速摘要）
  - 元数据：时间、主机名、用户、OS 发行版、内核版本、平台/架构、运行时间
  - CPU：型号、核心数、当前频率
  - 系统负载：1/5/15 分钟
  - 内存：总量/已用/可用 + 使用率进度条
  - 磁盘：每个挂载点的总量/已用/可用 + 使用率进度条
  - 概要：进程数、监听端口数
  - 命令行：`--info [section]`（section 可选 `json` 或 `color`）
  - 也支持 `--info-json` / `--info-color` 独立标志
  - 彩色模式按使用率着色（≥95% 红、≥80% 黄、其余绿）

### Changed
- Bumped project version to 0.4.0
- 新增模块：`tail/`、`validate/`、`snapshot_diff/`、`info/`
- 新增 CLI 标志：`--tail`、`--tail-category`、`--tail-source`、`--tail-keyword`、
  `--tail-recent`、`--tail-color`、`--tail-json`、`--validate-config`、
  `--snapshot-diff`、`--snapshot-diff-json`、`--snapshot-diff-verbose`、
  `--info`、`--info-json`、`--info-color`
- 新增的 one-shot 命令（`--validate-config`、`--snapshot-diff`、`--info`）跳过启动更新检查
- 在 tail 模式下不再注册默认 stdout 事件回调，避免事件被重复打印

### Technical Details
- 新类：`TailWatcher`、`ConfigValidator`、`SnapshotDiff`、`SystemInfo`
- `tail/` 复用 `MonitorEngine::on_event()` 与 `recent_events()`，无侵入式改动
- `validate/` 独立解析配置文件（不修改全局 `ConfigStore`），内置已知键表与类型校验
- `snapshot_diff/` 自带 JSON 解析器，不依赖第三方库
- `info/` 直接读取 `/proc/{cpuinfo,meminfo,loadavg,mounts,sys/kernel/osrelease}`、`/etc/os-release`、`sysconf`、`statvfs`，无监控器启动开销

## [0.3.0] - 2026-06-30

### Added

#### System Snapshot Mode
- **Snapshot Generator**: One-shot capture of the current system state as JSON
  - Captures metadata (timestamp, host, platform, architecture, user, uptime)
  - System load (1/5/15-min load averages, memory, swap)
  - Disk usage for all real mount points (filters pseudo-filesystems)
  - Top-N processes by memory (pid, ppid, name, user, cmdline)
  - Network connections parsed from `/proc/net/{tcp,udp,tcp6,udp6}`
  - Listening ports (TCP LISTEN + unconnected UDP)
  - Curated environment variables (PATH, HOME, USER, SHELL, ...)
  - Command-line: `--snapshot [path]` (omit path or `-` for stdout)
  - Ideal for baselines, audits, and point-in-time system records

#### Diagnostic / Self-Test Mode
- **Diagnostic Runner**: Verify monitor availability and sub-system health
  - Reports each monitor's availability, poll interval, and native-event support
  - Reports status of storage, reporter, alert manager, event filter,
    statistics, security auditor, and webhook notifier
  - Storage event count displayed for at-a-glance verification
  - Command-line: `--diagnose [path]` (omit path for stdout)
  - Exits with code 2 if any monitor is unavailable (script-friendly)

#### Event Query
- **Event Query**: Search stored events from the command line
  - Filter by keyword (matches summary, target, or source — case-insensitive)
  - Filter by category (`filesystem`, `process`, `network`, `system_config`,
    `hardware`, `system`)
  - Filter by source substring
  - Filter by time range (`--query-from` / `--query-to` as unix milliseconds)
  - Pagination via `--query-offset` and `--query-limit`
  - Human-readable or `--query-json` output
  - Results sorted most-recent-first

#### Storage Log Rotation
- **Automatic Rotation**: Cap storage file growth and keep backups
  - New config keys: `storage.max_size_mb` and `storage.rotate_count`
  - When the active file exceeds `max_size_mb`, it is renamed to `<path>.1`,
    older backups are shifted (`.<n>` → `.<n+1>`), and a fresh file is opened
  - Up to `rotate_count` backups are retained; the oldest is removed
  - Byte counter is seeded from existing file size, so rotation is correct
    across daemon restarts
  - Disabled by default (`max_size_mb = 0`)

### Changed
- Fixed storage round-trip: `parse_event` now restores `Event::category` and
  `Event::type` from the serialized names (previously always `Unknown`), so
  category-based queries and exports are accurate
- Added reverse lookups `category_from_name()` / `type_from_name()` to the
  Event API
- `MonitorEngine` now exposes `monitors()`, `capture_snapshot()`,
  `query_events()`, and `run_diagnostic()` for programmatic access
- `Storage` interface gained `configure_rotation()` (no-op default; backed by
  the file storage)
- One-shot CLI modes (`--snapshot`, `--diagnose`, `--query`, `--export`,
  `--report`, `--reload-config`, `--version`, `--help`) now skip the
  interactive update check for faster, non-interactive operation
- Bumped project version to 0.3.0

### Technical Details
- New modules: `snapshot/`, `query/`, `diagnostic/`
- New classes: `SnapshotGenerator`, `EventQuery`, `DiagnosticRunner`
- New CLI flags: `--snapshot`, `--diagnose`, `--query`, `--query-category`,
  `--query-source`, `--query-from`, `--query-to`, `--query-limit`,
  `--query-offset`, `--query-json`
- Snapshot module reads `/proc/{loadavg,meminfo,uptime,mounts,net/*}` and
  `/proc/<pid>/{status,cmdline}` directly — no monitor start required

## [0.2.0] - Data export, reports, hot-reload, auto-update

### Added

#### Data Export Module
- **Event Exporter**: Export events to CSV or JSON format
  - CSV export with proper escaping and field formatting
  - JSON export with structured event data
  - Command-line interface: `--export <path>`
  - Automatic format detection based on file extension
  - Preserves all event metadata including attributes

#### Report Generation
- **Report Generator**: Generate comprehensive text reports
  - Summary section with event counts by category
  - Statistics section with rates and top sources/targets
  - Recent events section with detailed event information
  - Configurable report sections and time periods
  - Command-line interface: `--report <path>`
  - Automatic timestamp and duration formatting

#### Configuration Hot-Reload
- **Config Watcher**: Watch and reload configuration at runtime
  - Monitor configuration file for changes
  - Automatic reload when file is modified
  - Update monitor poll intervals without restart
  - Configurable check interval (default: 5000ms)
  - Command-line interface: `--reload-config`
  - Configuration options:
    - `config.watch_enabled`: Enable/disable watcher
    - `config.watch_interval_ms`: Check interval in milliseconds

#### Auto-Update Checker
- **Updater Module**: Automatically check for newer GitHub releases on startup
  - Queries the GitHub Releases API, falls back to the Tags API
  - Semantic version comparison (strips `v` prefix, compares dotted integers)
  - Prompts the user (y/N) and optionally runs `git pull` to self-update
  - Gated behind `COS_USE_REMOTE_REPORTING` (requires libcurl); compiles out cleanly otherwise
  - New `-V` / `--version` CLI flag to print the current version and exit

### Changed
- Updated MonitorEngine to support export, report, and config reload
- Added new CLI options for export, report, and reload-config
- Updated documentation with new features and usage examples
- Enhanced config.ini.example with new configuration options

### Technical Details
- New modules: `export/`, `report/`, `updater/`
- New classes: `EventExporter`, `ReportGenerator`, `ConfigWatcher`, updater `check_for_update()` / `prompt_update()`
- Extended `MonitorEngine` with `export_events()`, `generate_report()`, `reload_config()`
- Added `config_watcher_` member to MonitorEngine
- Added libcurl detection (`find_package(CURL)`) in `src/CMakeLists.txt`; `COS_USE_REMOTE_REPORTING` now also gates the update checker and webhook HTTP code via a real compile definition
- Bumped project version to 0.2.0
- Updated CMakeLists.txt to include new source files

## [0.1.0] - Initial Release

### Core Features
- Real-time system change monitoring
- Filesystem, process, network, and system configuration tracking
- Cross-platform support (Linux, macOS, Windows, Android, ChromeOS, FydeOS, iOS)
- Event filtering and alerting system
- Statistics collection and analysis
- Security audit and webhook notifications
