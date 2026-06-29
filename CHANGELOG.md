# Changelog

All notable changes to this project will be documented in this file.

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
