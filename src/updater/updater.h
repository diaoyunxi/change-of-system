#pragma once

#include <string>

namespace changeos {
namespace updater {

struct UpdateInfo {
    bool available = false;
    std::string current_version;
    std::string latest_version;
    std::string release_url;
    std::string error;
};

/// Check GitHub for a newer release. Returns UpdateInfo.
/// Tries the Releases API first, falls back to Tags.
UpdateInfo check_for_update(const std::string& current_version);

/// Print update info to stdout and prompt the user.
/// If an update is available, asks whether to update via stdin.
/// If user confirms, attempts `git pull` in the executable's directory.
void prompt_update(const UpdateInfo& info);

} // namespace updater
} // namespace changeos
