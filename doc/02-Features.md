> [← 返回文档目录](../README.md) ｜ 本分册由 `README.md` 的「Features」章节拆分而来。

## Features

### Core Monitoring
- **Filesystem Monitor**: Track file creations, modifications, deletions, and permission changes
- **Process Monitor**: Monitor process starts, stops, CPU spikes, and memory spikes
- **Network Monitor**: Track TCP/UDP connections and bandwidth usage
- **System Config Monitor**: Detect configuration file changes
- **User Activity Monitor**: Track user login/logout events and session information
- **Service Monitor**: Monitor systemd service state changes (running/stopped/failed)
- **File Integrity Monitor**: Verify file integrity using SHA-256 hashing
- **USB Device Monitor**: Detect USB device insertion and removal events
- **Disk Space Monitor**: Monitor disk usage with configurable warning/critical thresholds
- **System Load Monitor**: Track system load averages, CPU usage, and memory usage
- **Log Monitor**: Monitor log files for pattern matches and anomalies with regex support
- **Port Monitor**: Track open ports and listening services, detect new/closed ports and state changes
- **Package Monitor**: Monitor package installations, updates, and removals (supports APT, DNF, YUM, Pacman, Zypper, Homebrew, MacPorts)
- **Environment Monitor**: Track environment variable changes (set, modified, unset)

### Daemon Mode
- Run as a background daemon with `-d` or `--daemon` flag
- PID file management to prevent multiple instances
- SIGHUP signal handler for configuration reload without restart
- Automatic PID file cleanup on shutdown
- Configurable PID file location with `--pid-file` option

### Alert System
- Rule-based alerting with customizable conditions
- Support for threshold-based triggers (e.g., alert after N events in time window)
- Configurable cooldown periods to prevent alert spam
- Multiple severity levels: Info, Warning, Critical, Emergency
- Predefined alert rules for common scenarios:
  - High CPU usage detection
  - Suspicious process detection
  - Critical file change monitoring
  - Network anomaly detection
  - Configuration tampering alerts
  - Rapid file change detection

### Event Filtering
- Reduce noise with configurable filter rules
- Support for regex patterns on source, target, and summary fields
- Filter actions: Allow, Deny, or Modify events
- Predefined filters for common use cases:
  - Ignore temporary files
  - Ignore log file changes
  - Ignore browser cache directories
  - Ignore localhost connections

### Statistics & Analytics
- Real-time event statistics collection
- Time-series data for trend analysis
- Top sources and targets tracking
- Event rate calculations (events per second/minute)
- JSON export for integration with external tools

### Security Audit
- Real-time security event detection
- Failed login attempt tracking with threshold alerts
- Privilege escalation monitoring
- Sensitive file access detection
- Suspicious process identification (netcat, nmap, etc.)
- Network anomaly detection (suspicious outbound connections)
- Firewall change monitoring
- User database modification alerts
- Predefined security rules for common attack patterns

### Webhook Notifications
- HTTP webhook integration for external alerting systems
- Support for multiple webhook endpoints
- HMAC signature verification for secure delivery
- Configurable retry logic and timeouts
- Severity-based filtering
- JSON payload format for easy integration
- Works with Slack, Discord, PagerDuty, custom endpoints

### Data Export
- Export events to CSV or JSON format
- Command-line interface for on-demand export
- Preserves all event metadata and attributes
- Suitable for external analysis and archiving

### Report Generation
- Generate comprehensive text reports
- Include event summaries, statistics, and recent events
- Configurable report sections and time periods
- Automatic timestamp and duration formatting

### Configuration Hot-Reload
- Watch configuration file for changes
- Automatically reload configuration without restarting
- Update monitor intervals and settings on-the-fly
- Configurable check interval

### Auto-Update
- Automatic version check against the latest GitHub release on startup
- Queries the GitHub Releases API (falls back to the Tags API)
- Compares semantic versions and prompts the user when a newer release exists
- Optional one-step self-update via `git pull` run from the executable's directory
- New `-V` / `--version` flag prints the current version and exits
- Requires libcurl (gated by the `COS_USE_REMOTE_REPORTING` CMake option)

### System Snapshot
- One-shot capture of the current system state as a JSON document
- Records metadata (timestamp, host, platform, architecture, user, uptime)
- Captures system load, memory/swap, disk usage, top-N processes,
  network connections, listening ports, and curated environment variables
- `--snapshot [path]` writes to a file (omit path or use `-` for stdout)
- Does not start the monitors — useful for baselines and point-in-time audits

### Diagnostic / Self-Test
- `--diagnose [path]` reports the availability of every monitor and the
  health of all sub-systems (storage, reporter, alerts, filter, statistics,
  security, webhook)
- Exits with code 2 if any monitor is unavailable, so it can be used in
  health-check scripts

### Event Query
- `--query [keyword]` searches persisted events from the command line
- Filters: `--query-category <name>`, `--query-source <substr>`,
  `--query-from <unix_ms>`, `--query-to <unix_ms>`
- Pagination: `--query-limit <n>`, `--query-offset <n>`
- `--query-json` switches to structured JSON output
- Results are sorted most-recent-first

### Storage Log Rotation
- Cap storage growth with `storage.max_size_mb` and `storage.rotate_count`
- When the active file exceeds the limit it is rotated to `<path>.1`, older
  backups are shifted, and a fresh file is opened
- Disabled by default (`storage.max_size_mb = 0`)

### Live Tail Mode
- Real-time event stream (`tail -f` style) to stdout
- Filter by category (`--tail-category`), source substring (`--tail-source`),
  or keyword (`--tail-keyword`, matches summary/target/source)
- Optional ANSI color output (`--tail-color`) that color-codes events by
  severity (red = deletion/critical, green = creation, yellow = modification,
  cyan = user activity)
- Optional one-JSON-object-per-line output (`--tail-json`) for piping into `jq`
- `--tail-recent <n>` replays the last N stored events before live streaming
- Clean Ctrl+C shutdown

### Config Validation
- Pre-flight check of a config file before starting the daemon
- Syntax checks: missing `=`, empty keys, duplicate keys
- Type checks: bool / int / double / port list (0-65535)
- Semantic checks: threshold relationships (warning < critical), positive
  poll intervals, sub-100ms intervals warned as CPU-heavy, reporting/webhook
  endpoint presence and URL scheme
- Unknown-key detection surfaces likely typos
- `--validate-config [path]` exits 0 if OK, 1 if errors found
- Path precedence: explicit arg > `-c/--config` > `config.ini`

### Snapshot Diff
- Compare two snapshot JSON files and print the differences
- Built-in minimal JSON parser (no third-party dependency)
- Indexes array elements by primary key (pid / port / device+mount /
  address / name) to intelligently match additions, removals, and changes
- Compares metadata (host / platform / architecture / user / uptime) and
  every section (system, disks, processes, connections, listening_ports,
  environment)
- Text output with counts and optional per-item detail
  (`--snapshot-diff-verbose`)
- JSON output (`--snapshot-diff-json`) for programmatic consumption
- Exit codes: 0 = identical, 1 = differences, 2 = error (unreadable file)
- Useful for baseline-drift detection, change audits, before/after comparison

### System Info
- One-shot quick system summary (neofetch-style, monitoring-focused)
- Metadata: timestamp, hostname, user, OS distro, kernel, platform/arch,
  uptime
- CPU: model, core count, current frequency
- System load (1/5/15-min)
- Memory: total/used/available + usage bar
- Disks: per-mount total/used/free + usage bar
- Summary: process count, listening port count
- `--info [section]` (section optional: `json` or `color`)
- Also `--info-json` / `--info-color` standalone flags
- Color mode shades usage bars by severity (≥95% red, ≥80% yellow, else green)
