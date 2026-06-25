# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

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

### Changed
- Updated MonitorEngine to support export, report, and config reload
- Added new CLI options for export, report, and reload-config
- Updated documentation with new features and usage examples
- Enhanced config.ini.example with new configuration options

### Technical Details
- New modules: `export/`, `report/`
- New classes: `EventExporter`, `ReportGenerator`, `ConfigWatcher`
- Extended `MonitorEngine` with `export_events()`, `generate_report()`, `reload_config()`
- Added `config_watcher_` member to MonitorEngine
- Updated CMakeLists.txt to include new source files

## [0.1.0] - Initial Release

### Core Features
- Real-time system change monitoring
- Filesystem, process, network, and system configuration tracking
- Cross-platform support (Linux, macOS, Windows, Android, ChromeOS, FydeOS, iOS)
- Event filtering and alerting system
- Statistics collection and analysis
- Security audit and webhook notifications
