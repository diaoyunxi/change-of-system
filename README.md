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
| Process     | Process started / stopped                    |
| Network     | TCP/UDP connection opened / closed           |
| System      | Config file hash change (/etc/hosts, /etc/resolv.conf, …) |

## Architecture

```
┌────────────┐   ┌────────────┐   ┌────────────┐
│ Filesystem │   │  Process   │   │  Network   │ ... (Monitor)
│  Monitor   │   │  Monitor   │   │  Monitor   │
└────┬───────┘   └─────┬──────┘   └─────┬──────┘
     │                 │                │
     └─────────┬───────┘                │
               ▼                        ▼
          ┌─────────────────────────────────────┐
          │         MonitorEngine               │  ← routes events
          └──┬─────────────┬─────────────┬─────┘
             ▼             ▼             ▼
        ┌─────────┐  ┌──────────┐  ┌───────────┐
        │ Storage │  │ Reporter │  │  Qt GUI   │
        │(SQLite/ │  │ (HTTP)   │  │ / CLI     │
        │ File)   │  └──────────┘  └───────────┘
        └─────────┘
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

storage.database_path = change-of-system.log
storage.max_events = 100000

reporting.enabled = false
reporting.endpoint = https://example.com/api/events
reporting.api_key =
reporting.batch_size = 100
reporting.interval_ms = 10000
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
  storage/           Storage interface + default file/SQLite backend
  reporting/         HTTP batch reporter (JSON)
  gui/               Qt5 dashboard
  config/            Simple INI-style config store
  platform/          OS/arch detection
  utils/             Logger, periodic runner helpers
cmake/               Platform-detection helper
```

## License

MIT. See `LICENSE`.
