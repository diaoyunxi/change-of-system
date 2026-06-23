#include "monitor/disk_space/disk_space_monitor.h"
#include "core/event.h"
#include "platform/platform_detection.h"
#include "utils/logger.h"

#include <algorithm>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/statvfs.h>
#include <mntent.h>
#endif

namespace changeos {
namespace monitor {

DiskSpaceMonitor::DiskSpaceMonitor() = default;
DiskSpaceMonitor::~DiskSpaceMonitor() = default;

bool DiskSpaceMonitor::is_available() const {
    return true;
}

bool DiskSpaceMonitor::on_start() {
    runner_ = std::make_unique<utils::PeriodicRunner>();
    runner_->start([this]() { tick(); },
                   std::chrono::milliseconds(poll_interval_ms_));
    COS_LOG_INFO("Disk space monitor started");
    return true;
}

bool DiskSpaceMonitor::on_stop() {
    if (runner_) {
        runner_->stop();
    }
    COS_LOG_INFO("Disk space monitor stopped");
    return true;
}

void DiskSpaceMonitor::tick() {
    auto current_state = scan_disk_space();

    std::lock_guard<std::mutex> lock(mutex_);

    if (first_scan_) {
        previous_state_ = current_state;
        first_scan_ = false;
        return;
    }

    for (const auto& [mount, info] : current_state) {
        auto prev_it = previous_state_.find(mount);
        double prev_usage = prev_it != previous_state_.end() ?
                            prev_it->second.usage_percent : 0.0;

        // Check for critical threshold
        if (info.usage_percent >= critical_threshold_ &&
            prev_usage < critical_threshold_) {
            Event e;
            e.category = EventCategory::System;
            e.type = EventType::DiskSpaceCritical;
            e.source = "disk_space_monitor";
            e.target = info.mount_point;
            e.attributes["device"] = info.device;
            e.attributes["total_bytes"] = std::to_string(info.total_bytes);
            e.attributes["used_bytes"] = std::to_string(info.used_bytes);
            e.attributes["free_bytes"] = std::to_string(info.free_bytes);
            e.attributes["usage_percent"] = std::to_string(info.usage_percent);
            e.summary = "Disk space critical: " + info.mount_point + " at " +
                        std::to_string(static_cast<int>(info.usage_percent)) + "% usage";
            emit(e);
            COS_LOG_WARN("Disk space critical: " + info.mount_point);
        }
        // Check for warning threshold
        else if (info.usage_percent >= warning_threshold_ &&
                 prev_usage < warning_threshold_) {
            Event e;
            e.category = EventCategory::System;
            e.type = EventType::DiskSpaceWarning;
            e.source = "disk_space_monitor";
            e.target = info.mount_point;
            e.attributes["device"] = info.device;
            e.attributes["total_bytes"] = std::to_string(info.total_bytes);
            e.attributes["used_bytes"] = std::to_string(info.used_bytes);
            e.attributes["free_bytes"] = std::to_string(info.free_bytes);
            e.attributes["usage_percent"] = std::to_string(info.usage_percent);
            e.summary = "Disk space warning: " + info.mount_point + " at " +
                        std::to_string(static_cast<int>(info.usage_percent)) + "% usage";
            emit(e);
            COS_LOG_INFO("Disk space warning: " + info.mount_point);
        }
        // Check for significant space change (more than 5% change)
        else if (prev_it != previous_state_.end()) {
            double usage_change = std::abs(info.usage_percent - prev_usage);
            if (usage_change >= 5.0) {
                Event e;
                e.category = EventCategory::System;
                e.type = EventType::DiskSpaceChanged;
                e.source = "disk_space_monitor";
                e.target = info.mount_point;
                e.attributes["device"] = info.device;
                e.attributes["total_bytes"] = std::to_string(info.total_bytes);
                e.attributes["used_bytes"] = std::to_string(info.used_bytes);
                e.attributes["free_bytes"] = std::to_string(info.free_bytes);
                e.attributes["usage_percent"] = std::to_string(info.usage_percent);
                e.attributes["previous_percent"] = std::to_string(prev_usage);
                e.summary = "Disk space changed: " + info.mount_point + " from " +
                            std::to_string(static_cast<int>(prev_usage)) + "% to " +
                            std::to_string(static_cast<int>(info.usage_percent)) + "%";
                emit(e);
            }
        }
    }

    previous_state_ = current_state;
}

std::map<std::string, DiskInfo> DiskSpaceMonitor::scan_disk_space() {
    std::map<std::string, DiskInfo> disks;

#ifdef __linux__
    // Read mounted filesystems from /proc/mounts or /etc/mtab
    FILE* mounts = setmntent("/proc/mounts", "r");
    if (!mounts) {
        mounts = setmntent("/etc/mtab", "r");
    }
    if (mounts) {
        struct mntent* mnt;
        while ((mnt = getmntent(mounts)) != nullptr) {
            // Skip pseudo filesystems
            std::string fs_type = mnt->mnt_type;
            if (fs_type == "proc" || fs_type == "sysfs" || fs_type == "devfs" ||
                fs_type == "tmpfs" || fs_type == "devtmpfs" ||
                fs_type == "cgroup" || fs_type == "cgroup2" ||
                fs_type == "pstore" || fs_type == "securityfs" ||
                fs_type == "mqueue" || fs_type == "debugfs" ||
                fs_type == "tracefs" || fs_type == "configfs" ||
                fs_type == "fusectl" || fs_type == "hugetlbfs" ||
                fs_type == "autofs" || fs_type == "binfmt_misc" ||
                fs_type == "overlay") {
                continue;
            }

            // Skip if watch_paths is specified and this path is not in the list
            if (!watch_paths_.empty()) {
                bool found = false;
                for (const auto& p : watch_paths_) {
                    if (mnt->mnt_dir == p) {
                        found = true;
                        break;
                    }
                }
                if (!found) continue;
            }

            struct statvfs stat;
            if (statvfs(mnt->mnt_dir, &stat) == 0) {
                DiskInfo info;
                info.mount_point = mnt->mnt_dir;
                info.device = mnt->mnt_fsname;

                std::int64_t block_size = static_cast<std::int64_t>(stat.f_frsize);
                info.total_bytes = stat.f_blocks * block_size;
                info.free_bytes = stat.f_bavail * block_size;
                info.used_bytes = info.total_bytes - info.free_bytes;

                if (info.total_bytes > 0) {
                    info.usage_percent = (static_cast<double>(info.used_bytes) /
                                          static_cast<double>(info.total_bytes)) * 100.0;
                }

                disks[info.mount_point] = info;
            }
        }
        endmntent(mounts);
    }
#elif defined(_WIN32)
    // Windows implementation using GetDiskFreeSpaceEx
    char drives[256];
    if (GetLogicalDriveStringsA(sizeof(drives), drives)) {
        char* drive = drives;
        while (*drive) {
            std::string drive_str(drive);

            // Skip if watch_paths is specified and this drive is not in the list
            if (!watch_paths_.empty()) {
                bool found = false;
                for (const auto& p : watch_paths_) {
                    if (drive_str == p || drive_str.substr(0, 2) == p.substr(0, 2)) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    drive += strlen(drive) + 1;
                    continue;
                }
            }

            ULARGE_INTEGER free_bytes, total_bytes, free_bytes_available;
            if (GetDiskFreeSpaceExA(drive, &free_bytes_available, &total_bytes, &free_bytes)) {
                DiskInfo info;
                info.mount_point = drive_str;
                info.device = drive_str;
                info.total_bytes = static_cast<std::int64_t>(total_bytes.QuadPart);
                info.free_bytes = static_cast<std::int64_t>(free_bytes.QuadPart);
                info.used_bytes = info.total_bytes - info.free_bytes;

                if (info.total_bytes > 0) {
                    info.usage_percent = (static_cast<double>(info.used_bytes) /
                                          static_cast<double>(info.total_bytes)) * 100.0;
                }

                disks[info.mount_point] = info;
            }

            drive += strlen(drive) + 1;
        }
    }
#elif defined(__APPLE__)
    // macOS implementation using statvfs
    FILE* mounts = setmntent("/etc/mtab", "r");
    if (mounts) {
        struct mntent* mnt;
        while ((mnt = getmntent(mounts)) != nullptr) {
            std::string fs_type = mnt->mnt_type;
            if (fs_type == "devfs" || fs_type == "autofs" || fs_type == "nfs") {
                continue;
            }

            struct statvfs stat;
            if (statvfs(mnt->mnt_dir, &stat) == 0) {
                DiskInfo info;
                info.mount_point = mnt->mnt_dir;
                info.device = mnt->mnt_fsname;

                std::int64_t block_size = static_cast<std::int64_t>(stat.f_frsize);
                info.total_bytes = stat.f_blocks * block_size;
                info.free_bytes = stat.f_bavail * block_size;
                info.used_bytes = info.total_bytes - info.free_bytes;

                if (info.total_bytes > 0) {
                    info.usage_percent = (static_cast<double>(info.used_bytes) /
                                          static_cast<double>(info.total_bytes)) * 100.0;
                }

                disks[info.mount_point] = info;
            }
        }
        endmntent(mounts);
    }
#endif

    return disks;
}

} // namespace monitor
} // namespace changeos