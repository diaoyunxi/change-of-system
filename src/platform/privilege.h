#pragma once

#include <string>

namespace changeos {
namespace platform {

enum class PrivilegeLevel {
    User,
    Administrator,
    Root,
    Unknown
};

PrivilegeLevel current_privilege_level();
bool is_privileged();
std::string privilege_level_name(PrivilegeLevel level);

bool can_escalate_privileges();
bool escalate_privileges(bool silent = false);

bool reexecute_as_admin(const std::string& args = "");

} // namespace platform
} // namespace changeos
