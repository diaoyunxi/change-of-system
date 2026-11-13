> [← 返回文档目录](../README.md) ｜ 本分册由 `README.md` 的「What it watches」章节拆分而来。

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
