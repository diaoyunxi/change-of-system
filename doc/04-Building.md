> [← 返回文档目录](../README.md) ｜ 本分册由 `README.md` 的「Building」章节拆分而来。

## Building

Requirements:

- CMake >= 3.16
- C++17 compiler (GCC, Clang, MSVC)
- libcurl — **optional**, required for the auto-update checker and webhook/HTTP reporting (auto-detected via `find_package(CURL)`)
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
| `COS_USE_REMOTE_REPORTING` | `ON`  | Enable HTTP reporting, webhooks, and the auto-update checker (requires libcurl) |
| `COS_WARNINGS_AS_ERRORS` | `OFF`   | Promote compiler warnings to errors       |

> **Note:** The auto-update checker and the webhook/remote-reporting module are
> compiled in only when libcurl is found **and** `COS_USE_REMOTE_REPORTING=ON`.
> If libcurl is missing (or the option is `OFF`), the checker is compiled out
> and `check_for_update()` returns a no-op `UpdateInfo` instead of making any
> network request.

Output binaries (in `build/`, auto-created during compilation):

- `change-of-system` — headless CLI daemon.
- `change-of-system-gui` — Qt-based dashboard (if Qt5 was found).
