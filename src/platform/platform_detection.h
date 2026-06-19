#pragma once

#include <string>

namespace changeos {
namespace platform {

enum class Type {
    Linux,
    macOS,
    Windows,
    Android,
    iOS,
    ChromeOS,
    FydeOS,
    UnixLike,
    Unknown
};

Type current();
std::string to_string(Type t);
std::string name();
std::string version();
std::string architecture();
std::string hostname();
std::string username();

bool is_windows();
bool is_unix();
bool is_mobile();
bool has_native_filesystem_watcher();
bool has_native_process_monitor();

} // namespace platform
} // namespace changeos
