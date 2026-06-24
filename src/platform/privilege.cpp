#include "platform/privilege.h"
#include "platform/platform_detection.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

#if defined(COS_PLATFORM_LINUX) || defined(COS_PLATFORM_UNIX) || defined(COS_PLATFORM_ANDROID) || defined(COS_PLATFORM_MACOS) || defined(COS_PLATFORM_IOS)
#include <unistd.h>
#include <sys/types.h>
#endif

#if defined(COS_PLATFORM_WINDOWS)
#include <windows.h>
#include <shellapi.h>
#endif

namespace changeos {
namespace platform {

PrivilegeLevel current_privilege_level() {
#if defined(COS_PLATFORM_WINDOWS)
    BOOL is_admin = FALSE;
    PSID admin_group = nullptr;
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    
    if (AllocateAndInitializeSid(&authority, 2,
        SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0, &admin_group)) {
        CheckTokenMembership(nullptr, admin_group, &is_admin);
        FreeSid(admin_group);
    }
    
    return is_admin ? PrivilegeLevel::Administrator : PrivilegeLevel::User;
    
#elif defined(COS_PLATFORM_LINUX) || defined(COS_PLATFORM_MACOS) || defined(COS_PLATFORM_UNIX) || defined(COS_PLATFORM_ANDROID) || defined(COS_PLATFORM_IOS)
    uid_t uid = getuid();
    uid_t euid = geteuid();
    
    if (uid == 0 || euid == 0) {
        return PrivilegeLevel::Root;
    }
    
    return PrivilegeLevel::User;
    
#else
    return PrivilegeLevel::Unknown;
#endif
}

bool is_privileged() {
    PrivilegeLevel level = current_privilege_level();
    return level == PrivilegeLevel::Root || level == PrivilegeLevel::Administrator;
}

std::string privilege_level_name(PrivilegeLevel level) {
    switch (level) {
        case PrivilegeLevel::User: return "User";
        case PrivilegeLevel::Administrator: return "Administrator";
        case PrivilegeLevel::Root: return "Root";
        default: return "Unknown";
    }
}

bool can_escalate_privileges() {
    if (is_privileged()) {
        return false; // Already privileged
    }
    
#if defined(COS_PLATFORM_WINDOWS)
    // Windows: Can always attempt UAC elevation
    return true;
    
#elif defined(COS_PLATFORM_LINUX) || defined(COS_PLATFORM_MACOS) || defined(COS_PLATFORM_UNIX)
    // Unix-like: Check if sudo is available
    int result = std::system("which sudo > /dev/null 2>&1");
    return result == 0;
    
#elif defined(COS_PLATFORM_ANDROID)
    // Android: Check if su binary exists
    int result = std::system("which su > /dev/null 2>&1");
    return result == 0;
    
#elif defined(COS_PLATFORM_IOS)
    // iOS: Check if jailbroken (cydia or su exists)
    int result = std::system("test -e /Applications/Cydia.app || which su > /dev/null 2>&1");
    return result == 0;
    
#else
    return false;
#endif
}

bool escalate_privileges(bool silent) {
    if (is_privileged()) {
        return true; // Already privileged
    }
    
    if (!can_escalate_privileges()) {
        if (!silent) {
            std::cerr << "Cannot escalate privileges on this system\n";
        }
        return false;
    }
    
#if defined(COS_PLATFORM_WINDOWS)
    // Windows: Re-execute with UAC elevation
    return reexecute_as_admin();
    
#elif defined(COS_PLATFORM_LINUX) || defined(COS_PLATFORM_MACOS) || defined(COS_PLATFORM_UNIX)
    // Unix-like: Use sudo
    if (!silent) {
        std::cout << "Requesting root privileges via sudo...\n";
    }
    // Note: This will prompt for password if needed
    return true; // Actual escalation happens via reexecute_as_admin
    
#elif defined(COS_PLATFORM_ANDROID)
    // Android: Use su
    if (!silent) {
        std::cout << "Requesting root privileges via su...\n";
    }
    return true;
    
#else
    if (!silent) {
        std::cerr << "Privilege escalation not supported on this platform\n";
    }
    return false;
#endif
}

bool reexecute_as_admin(const std::string& args) {
    if (is_privileged()) {
        return true;
    }
    
#if defined(COS_PLATFORM_WINDOWS)
    char exe_path[MAX_PATH];
    GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    
    SHELLEXECUTEINFOA sei = {0};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = "runas";
    sei.lpFile = exe_path;
    sei.lpParameters = args.c_str();
    sei.nShow = SW_SHOWNORMAL;
    
    HINSTANCE result = ShellExecuteA(nullptr, &sei);
    return reinterpret_cast<intptr_t>(result) > 32;
    
#elif defined(COS_PLATFORM_LINUX) || defined(COS_PLATFORM_MACOS) || defined(COS_PLATFORM_UNIX)
    // Get current executable path
    char exe_path[4096];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1) {
        // Fallback: try to get from argv[0] (not reliable)
        std::cerr << "Cannot determine executable path for re-execution\n";
        return false;
    }
    exe_path[len] = '\0';
    
    std::string cmd = "sudo ";
    cmd += exe_path;
    if (!args.empty()) {
        cmd += " ";
        cmd += args;
    }
    
    int result = std::system(cmd.c_str());
    return result == 0;
    
#elif defined(COS_PLATFORM_ANDROID)
    // Android: Use su -c
    std::string cmd = "su -c '";
    cmd += "/data/local/tmp/change-of-system"; // Typical location
    if (!args.empty()) {
        cmd += " ";
        cmd += args;
    }
    cmd += "'";
    
    int result = std::system(cmd.c_str());
    return result == 0;
    
#else
    std::cerr << "Re-execution as admin not supported on this platform\n";
    return false;
#endif
}

} // namespace platform
} // namespace changeos
