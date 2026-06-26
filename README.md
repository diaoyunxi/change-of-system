# change-of-system

Realtime system change monitor. Tracks filesystem, process, network, and
system configuration changes across **Linux**, **macOS**, **Android**,
**Windows**, **ChromeOS**, **FydeOS**, and **iOS** (limited on mobile — see
below).

The goal is a single, lightweight agent that emits a structured event for
every meaningful change on the host it runs on — so you can see what
actually happened on a machine, not only what a monitoring dashboard chose
to graph.

## What it watches

| Category    | Events                                      |
|-------------|---------------------------------------------|
| Filesystem  | File created / modified / deleted / permission changed (per-path or recursive) |
| Process     | Process started / stopped / CPU spike / memory spike |
| Network     | TCP/UDP connection opened / closed / bandwidth spike / DNS query |
| System      | Config file hash change (/etc/hosts, /etc/resolv.conf, …) |
| User        | User login / logout activity                |
| Service     | System service state changes (running/stopped/failed) |
| Integrity   | File integrity verification using SHA-256 hashing |
| USB         | USB device inserted / removed               |
| Disk        | Disk space warning / critical / changed     |
| Load        | System load high / normal (CPU & memory)    |
| Log         | Log pattern matches / anomalies detected    |
| Port        | Port opened / closed / state changed        |
| Package     | Package installed / updated / removed       |
| Environment | Environment variable set / changed / unset  |
| Security    | Security events (failed login, privilege escalation, suspicious activity) |

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

## Architecture

```
┌────────────┐   ┌────────────┐   ┌────────────┐   ┌────────────┐   ┌────────────┐
│ Filesystem │   │  Process   │   │  Network   │   │    USB     │   │    Log     │ ... (Monitor)
│  Monitor   │   │  Monitor   │   │  Monitor   │   │  Monitor   │   │  Monitor   │
└────┬───────┘   └─────┬──────┘   └─────┬──────┘   └─────┬──────┘   └─────┬──────┘
     │                 │                │                │                │
     └─────────┬───────┘                │                │                │
               ▼                        ▼                ▼                ▼
          ┌─────────────────────────────────────────────────────────────────────┐
          │                   MonitorEngine                      │  ← routes events
          └──┬─────────────┬─────────────┬─────────────┬─────────────┬───────────┘
             ▼             ▼             ▼             ▼             ▼
        ┌─────────┐  ┌──────────┐  ┌───────────┐  ┌───────────┐  ┌──────────────┐
        │ Storage │  │ Reporter │  │  Qt GUI   │  │ Statistics│  │   Security   │
        │(SQLite/ │  │ (HTTP)   │  │ / CLI     │  │ Collector │  │   Auditor    │
        │ File)   │  └──────────┘  └───────────┘  └───────────┘  └──────────────┘
        └─────────┘                                                      │
             │                                                           ▼
             ▼                                                      ┌──────────────┐
        ┌─────────────┐                                              │   Webhook    │
        │   Alert     │                                              │   Notifier   │
        │  Manager    │                                              └──────────────┘
        └─────────────┘
             │
             ▼
        ┌─────────────┐
        │   Event     │
        │   Filter    │
        └─────────────┘
```

- Each `Monitor` subclass is a polling (or, where available, native-event)
  producer.
- `MonitorEngine` wires them together, fans events out to listeners, and
  forwards them to the storage and remote reporting backends.
- Events are plain structs with a timestamp, category, type, source/target,
  an arbitrary attribute map, a free-form summary, and host/platform
  metadata.
- The storage backend is abstracted; the default implementation writes a
  line-delimited log that can be swapped for a real SQLite implementation
  without touching any monitoring code.
- The reporting backend serializes batches to JSON and POSTs them to a
  user-configured HTTP endpoint.
- The alert manager processes events against configurable rules and triggers
  alerts when conditions are met.
- The event filter allows fine-grained control over which events are processed.
- The statistics collector aggregates event data for analysis and reporting.
- The security auditor analyzes events for security-relevant patterns and
  generates security events.
- The webhook notifier sends alerts and security events to external HTTP endpoints.

## Building

Requirements:

- CMake >= 3.16
- C++17 compiler (GCC, Clang, MSVC)
- Qt5 (Widgets, Core, Gui) — **optional**, only for the GUI binary

```bash
mkdir build && cd build
cmake ..
cmake --build . -j
```

CMake options:

| Option                   | Default | Meaning                                   |
|--------------------------|---------|-------------------------------------------|
| `COS_BUILD_GUI`          | `ON`    | Build the Qt GUI (qt5 required)           |
| `COS_BUILD_TESTS`        | `OFF`   | Build unit tests                          |
| `COS_USE_REMOTE_REPORTING` | `ON`  | Build the HTTP remote reporting module    |
| `COS_WARNINGS_AS_ERRORS` | `OFF`   | Promote compiler warnings to errors       |

Output binaries (in `build/`):

- `change-of-system` — headless CLI daemon.
- `change-of-system-gui` — Qt-based dashboard (if Qt5 was found).

## Running

Headless:

```bash
./change-of-system --config ../config.ini
```

As a daemon:

```bash
# Run as background daemon
./change-of-system --config config.ini --daemon

# With custom PID file location
./change-of-system --config config.ini --daemon --pid-file /tmp/change-of-system.pid

# Reload configuration via signal
kill -HUP $(cat /var/run/change-of-system.pid)

# Stop the daemon
kill $(cat /var/run/change-of-system.pid)
```

Export events to file:

```bash
# Export to CSV
./change-of-system --config config.ini --export events.csv

# Export to JSON
./change-of-system --config config.ini --export events.json
```

Generate report:

```bash
./change-of-system --config config.ini --report report.txt
```

Reload configuration:

```bash
./change-of-system --config config.ini --reload-config
```

Sample `config.ini` (see `config.ini.example` for a complete one):

```
filesystem.enabled = true
filesystem.poll_interval_ms = 2000
filesystem.watch_paths = /etc, /tmp, /var/log

process.enabled = true
process.poll_interval_ms = 3000

network.enabled = true
network.poll_interval_ms = 5000

system_config.enabled = true
system_config.poll_interval_ms = 10000

user_activity.enabled = true
user_activity.poll_interval_ms = 5000

service.enabled = true
service.poll_interval_ms = 10000

file_integrity.enabled = true
file_integrity.poll_interval_ms = 30000
file_integrity.watch_files = /etc/passwd, /etc/shadow, /etc/sudoers

usb_device.enabled = true
usb_device.poll_interval_ms = 5000

disk_space.enabled = true
disk_space.poll_interval_ms = 30000
disk_space.warning_threshold = 80.0
disk_space.critical_threshold = 95.0

system_load.enabled = true
system_load.poll_interval_ms = 5000
system_load.load_threshold = 5.0
system_load.cpu_threshold = 90.0

port.enabled = true
port.poll_interval_ms = 5000
port.watch_ports = 22, 80, 443, 3306, 5432, 6379, 8080, 8443

package.enabled = true
package.poll_interval_ms = 60000

environment.enabled = true
environment.poll_interval_ms = 10000
environment.watch_variables = PATH, HOME, USER, LD_LIBRARY_PATH

storage.database_path = change-of-system.log
storage.max_events = 100000

reporting.enabled = false
reporting.endpoint = https://example.com/api/events
reporting.api_key =
reporting.batch_size = 100
reporting.interval_ms = 10000

# Alert system (enabled by default)
alert.enabled = true

# Event filter (disabled by default)
filter.enabled = false

# Configuration file watching (disabled by default)
config.watch_enabled = false
config.watch_interval_ms = 5000
```

Events are printed to stdout and persisted to `storage.database_path`. Press
Ctrl+C to stop cleanly.

## Cross-platform notes

- **Linux** — full support: filesystem via `std::filesystem` walker,
  processes via `/proc`, network via `/proc/net/{tcp,udp,tcp6,udp6}`.
- **macOS** — full support for filesystem and process snapshots. Network
  monitoring relies on system-specific APIs (stub provided; extend as
  needed).
- **Windows** — filesystem + process snapshots work. Network snapshot
  implementor should wrap `GetTcpTable2` / `GetUdpTable`.
- **Android** — works via Termux-style chroot; `/proc` is available for
  process/network data if permissions allow.
- **ChromeOS / FydeOS** — works in a developer shell via the Linux
  container (Crostini). Same code paths as Linux.
- **iOS** — very limited; only file-level events inside the app sandbox
  are observable without a jailbreak.

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
  export/            Event data export (CSV, JSON)
  report/            Text report generation
  gui/               Qt5 dashboard
  config/            Simple INI-style config store + hot-reload watcher
  platform/          OS/arch detection
  utils/             Logger, periodic runner helpers
cmake/               Platform-detection helper
```

## License

MIT. See `LICENSE`.
