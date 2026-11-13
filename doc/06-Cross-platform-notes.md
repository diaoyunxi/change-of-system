> [← 返回文档目录](../README.md) ｜ 本分册由 `README.md` 的「Cross-platform notes」章节拆分而来。

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
