#include "platform/platform_detection.h"

#include <cstring>
#include <sstream>
#include <thread>

#if defined(COS_PLATFORM_LINUX) || defined(COS_PLATFORM_UNIX) || defined(COS_PLATFORM_ANDROID)
#include <sys/utsname.h>
#include <unistd.h>
#include <pwd.h>
#endif

#if defined(COS_PLATFORM_WINDOWS)
#include <windows.h>
#include <lmcons.h>
#endif

#if defined(COS_PLATFORM_MACOS) || defined(COS_PLATFORM_IOS)
#include <sys/sysctl.h>
#include <unistd.h>
#endif

namespace changeos {
namespace platform {

Type current() {
#if defined(COS_PLATFORM_ANDROID)
    return Type::Android;
#elif defined(COS_PLATFORM_IOS)
    return Type::iOS;
#elif defined(COS_PLATFORM_CHROMEOS)
    return Type::ChromeOS;
#elif defined(COS_PLATFORM_FYDEOS)
    return Type::FydeOS;
#elif defined(COS_PLATFORM_LINUX)
    return Type::Linux;
#elif defined(COS_PLATFORM_MACOS)
    return Type::macOS;
#elif defined(COS_PLATFORM_WINDOWS)
    return Type::Windows;
#elif defined(COS_PLATFORM_UNIX)
    return Type::UnixLike;
#else
    return Type::Unknown;
#endif
}

std::string to_string(Type t) {
    switch (t) {
        case Type::Linux: return "Linux";
        case Type::macOS: return "macOS";
        case Type::Windows: return "Windows";
        case Type::Android: return "Android";
        case Type::iOS: return "iOS";
        case Type::ChromeOS: return "ChromeOS";
        case Type::FydeOS: return "FydeOS";
        case Type::UnixLike: return "Unix";
        default: return "Unknown";
    }
}

std::string name() {
    return to_string(current());
}

std::string version() {
    std::ostringstream oss;
#if defined(COS_PLATFORM_LINUX) || defined(COS_PLATFORM_UNIX) || defined(COS_PLATFORM_ANDROID)
    struct utsname info{};
    if (uname(&info) == 0) {
        oss << info.release;
    }
#elif defined(COS_PLATFORM_MACOS) || defined(COS_PLATFORM_IOS)
    char buffer[256] = {0};
    size_t len = sizeof(buffer);
    if (sysctlbyname("kern.osrelease", buffer, &len, nullptr, 0) == 0) {
        oss << buffer;
    }
#elif defined(COS_PLATFORM_WINDOWS)
    OSVERSIONINFOEX info{sizeof(OSVERSIONINFOEX)};
    if (GetVersionEx(reinterpret_cast<OSVERSIONINFO*>(&info))) {
        oss << info.dwMajorVersion << "." << info.dwMinorVersion;
    }
#else
    oss << "0.0";
#endif
    return oss.str();
}

std::string architecture() {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#elif defined(__arm__) || defined(_M_ARM)
    return "arm";
#elif defined(__i386__) || defined(_M_IX86)
    return "i386";
#else
    return "unknown";
#endif
}

std::string hostname() {
    char buffer[256] = {0};
#if defined(COS_PLATFORM_WINDOWS)
    DWORD len = sizeof(buffer);
    if (!GetComputerNameA(buffer, &len)) {
        std::strncpy(buffer, "unknown", sizeof(buffer) - 1);
    }
#else
    if (gethostname(buffer, sizeof(buffer)) != 0) {
        std::strncpy(buffer, "unknown", sizeof(buffer) - 1);
    }
#endif
    return std::string(buffer);
}

std::string username() {
    char buffer[256] = {0};
#if defined(COS_PLATFORM_WINDOWS)
    DWORD len = sizeof(buffer);
    if (!GetUserNameA(buffer, &len)) {
        std::strncpy(buffer, "unknown", sizeof(buffer) - 1);
    }
#else
    uid_t uid = getuid();
    struct passwd* pw = getpwuid(uid);
    if (pw && pw->pw_name) {
        std::strncpy(buffer, pw->pw_name, sizeof(buffer) - 1);
    } else {
        const char* user_env = std::getenv("USER");
        if (user_env) {
            std::strncpy(buffer, user_env, sizeof(buffer) - 1);
        }
    }
#endif
    return std::string(buffer);
}

bool is_windows() {
    return current() == Type::Windows;
}

bool is_unix() {
    Type t = current();
    return t == Type::Linux || t == Type::macOS || t == Type::UnixLike
        || t == Type::Android || t == Type::iOS || t == Type::ChromeOS || t == Type::FydeOS;
}

bool is_mobile() {
    Type t = current();
    return t == Type::Android || t == Type::iOS;
}

bool has_native_filesystem_watcher() {
    Type t = current();
    return t == Type::Linux || t == Type::macOS || t == Type::Windows
        || t == Type::ChromeOS || t == Type::FydeOS;
}

bool has_native_process_monitor() {
    Type t = current();
    return t == Type::Linux || t == Type::macOS || t == Type::Windows
        || t == Type::ChromeOS || t == Type::FydeOS;
}

} // namespace platform
} // namespace changeos
