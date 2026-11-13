> [← 返回文档目录](../README.md) ｜ 本分册由 `README.md` 的「Running」章节拆分而来。

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

Capture a one-shot system snapshot:

```bash
# Write JSON snapshot to a file
./change-of-system --snapshot snapshot.json

# Print snapshot to stdout
./change-of-system --snapshot -
```

Run a diagnostic self-test:

```bash
# Print the report to stdout (exit code 2 if any monitor is unavailable)
./change-of-system --diagnose

# Write the report to a file
./change-of-system --diagnose diagnose.txt
```

Query stored events:

```bash
# Most recent 20 events
./change-of-system --query --query-limit 20

# Filesystem events containing "passwd", as JSON
./change-of-system --query passwd --query-category filesystem --query-json

# Events from a specific source within a time window
./change-of-system --query --query-source package_monitor \
    --query-from 1782750000000 --query-to 1782750900000 --query-limit 100
```

Stream events live (like `tail -f`):

```bash
# All events, plain text
./change-of-system --config config.ini --tail

# Only filesystem events, colored, with last 50 historical events first
./change-of-system --config config.ini --tail --tail-category filesystem \
    --tail-color --tail-recent 50

# All events as one JSON object per line, piped to jq
./change-of-system --config config.ini --tail --tail-json | jq .
```

Validate a config file before starting:

```bash
# Validate the default config.ini
./change-of-system --validate-config

# Validate a specific file (exits 0 if OK, 1 if errors)
./change-of-system --validate-config /etc/change-of-system/config.ini
```

Compare two snapshots:

```bash
# Capture two snapshots at different times
./change-of-system --snapshot before.json
# ... make changes to the system ...
./change-of-system --snapshot after.json

# Show what changed (exit 0 = identical, 1 = differs, 2 = error)
./change-of-system --snapshot-diff before.json after.json

# Detailed per-item changes
./change-of-system --snapshot-diff before.json after.json --snapshot-diff-verbose

# Machine-readable JSON output
./change-of-system --snapshot-diff before.json after.json --snapshot-diff-json
```

Print a quick system info summary:

```bash
# Plain text summary
./change-of-system --info

# JSON output
./change-of-system --info json

# Colored output (usage bars shaded by severity)
./change-of-system --info color
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
