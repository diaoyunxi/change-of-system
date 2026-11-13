> [← 返回文档目录](../README.md) ｜ 本分册由 `README.md` 的「Architecture」章节拆分而来。

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
