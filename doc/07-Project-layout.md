> [← 返回文档目录](../README.md) ｜ 本分册由 `README.md` 的「Project layout」章节拆分而来。

## Project layout

```
src/
  core/              MonitorEngine, Event model, Monitor base class
  monitor/
    filesystem/
    process/
    network/
    system_config/
    user_activity/   User login/logout activity monitoring
    service/         Systemd service state monitoring
    file_integrity/  SHA-256 file integrity verification
    usb_device/      USB device insertion/removal monitoring
    disk_space/      Disk space usage monitoring
    system_load/     System load and CPU/memory monitoring
    log/             Log file pattern matching and anomaly detection
    port/            Open port and listening service monitoring
    package/         Package installation/update/removal monitoring
    environment/     Environment variable change monitoring
  storage/           Storage interface + default file/SQLite backend
  reporting/         HTTP batch reporter (JSON)
  alert/             Alert system with rule-based triggers
  filter/            Event filtering engine
  stats/             Statistics collection and analysis
  security/          Security audit and event detection
  webhook/           HTTP webhook notification system
  updater/           GitHub release auto-update checker
  export/            Event data export (CSV, JSON)
  report/            Text report generation
  snapshot/          One-shot system state snapshot (JSON)
  query/             Stored-event query with filters
  diagnostic/        Monitor / sub-system self-test
  tail/              Live event stream (tail -f style)
  validate/          Config file pre-flight validation
  snapshot_diff/     Compare two snapshot JSON files
  info/              Quick system info summary
  gui/               Qt5 dashboard
  config/            Simple INI-style config store + hot-reload watcher
  platform/          OS/arch detection
  utils/             Logger, periodic runner helpers
cmake/               Platform-detection helper
```
