#include "monitor/filesystem/filesystem_monitor.h"

#include "platform/platform_detection.h"
#include "utils/logger.h"

#include <algorithm>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace changeos {
namespace monitor {

FilesystemMonitor::FilesystemMonitor() = default;
FilesystemMonitor::~FilesystemMonitor() {
    stop();
}

bool FilesystemMonitor::supports_native_events() const {
    return platform::has_native_filesystem_watcher() && use_native_events_;
}

void FilesystemMonitor::add_watch_path(const std::string& path) {
    std::lock_guard<std::mutex> lock(paths_mutex_);
    for (const auto& p : watch_paths_) {
        if (p == path) return;
    }
    watch_paths_.push_back(path);
}

void FilesystemMonitor::remove_watch_path(const std::string& path) {
    std::lock_guard<std::mutex> lock(paths_mutex_);
    watch_paths_.erase(
        std::remove(watch_paths_.begin(), watch_paths_.end(), path),
        watch_paths_.end());
}

std::vector<std::string> FilesystemMonitor::watch_paths() const {
    std::lock_guard<std::mutex> lock(paths_mutex_);
    return watch_paths_;
}

bool FilesystemMonitor::on_start() {
    COS_LOG_INFO("Filesystem monitor starting");
    first_scan_.store(true);
    runner_ = std::make_unique<utils::PeriodicRunner>();
    runner_->start([this]() { poll_loop(); },
                   std::chrono::milliseconds(poll_interval_ms_));
    return true;
}

bool FilesystemMonitor::on_stop() {
    if (runner_) runner_->stop();
    runner_.reset();
    COS_LOG_INFO("Filesystem monitor stopped");
    return true;
}

void FilesystemMonitor::poll_loop() {
    scan_and_diff();
}

FileSnapshot FilesystemMonitor::snapshot_of(const std::string& path) {
    FileSnapshot snap;
    snap.path = path;
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        snap.exists = false;
        return snap;
    }
    snap.exists = true;
    snap.is_directory = fs::is_directory(path, ec);
    if (!snap.is_directory) {
        std::error_code size_ec;
        auto sz = fs::file_size(path, size_ec);
        if (!size_ec) snap.size = static_cast<std::int64_t>(sz);
    }
    auto ftime = fs::last_write_time(path, ec);
    if (!ec) {
        auto since_epoch = ftime.time_since_epoch();
        snap.mtime = std::chrono::duration_cast<std::chrono::seconds>(
            since_epoch).count();
    }
    return snap;
}

void FilesystemMonitor::scan_and_diff() {
    std::vector<std::string> paths;
    {
        std::lock_guard<std::mutex> lock(paths_mutex_);
        paths = watch_paths_;
    }

    std::map<std::string, FileSnapshot> current;

    for (const auto& root : paths) {
        std::error_code ec;
        if (!fs::exists(root, ec)) continue;
        try {
            if (fs::is_directory(root, ec)) {
                for (auto it = fs::recursive_directory_iterator(
                         root,
                         fs::directory_options::skip_permission_denied, ec);
                     it != fs::recursive_directory_iterator();
                     it.increment(ec)) {
                    if (ec) break;
                    const auto& entry = *it;
                    auto snap = snapshot_of(entry.path().string());
                    current[snap.path] = snap;
                }
            } else {
                auto snap = snapshot_of(root);
                current[snap.path] = snap;
            }
        } catch (...) {
            continue;
        }
    }

    if (first_scan_.exchange(false)) {
        previous_snapshots_ = std::move(current);
        return;
    }

    for (const auto& [path, cur] : current) {
        auto prev_it = previous_snapshots_.find(path);
        Event ev;
        ev.category = EventCategory::Filesystem;
        ev.source = "filesystem";
        ev.target = path;
        ev.platform = platform::name();
        ev.host = platform::hostname();

        if (prev_it == previous_snapshots_.end()) {
            ev.type = EventType::FileCreated;
            ev.summary = "File created: " + path;
            ev.attributes["size"] = std::to_string(cur.size);
            emit(ev);
        } else {
            const auto& prev = prev_it->second;
            if (prev.mtime != cur.mtime || prev.size != cur.size) {
                ev.type = EventType::FileModified;
                ev.summary = "File modified: " + path;
                ev.attributes["size_old"] = std::to_string(prev.size);
                ev.attributes["size_new"] = std::to_string(cur.size);
                emit(ev);
            }
            if (prev.permissions != cur.permissions) {
                ev.type = EventType::FilePermissionChanged;
                ev.summary = "File permission changed: " + path;
                emit(ev);
            }
        }
    }

    for (const auto& [path, prev] : previous_snapshots_) {
        if (current.find(path) == current.end()) {
            Event ev;
            ev.category = EventCategory::Filesystem;
            ev.type = EventType::FileDeleted;
            ev.source = "filesystem";
            ev.target = path;
            ev.summary = "File deleted: " + path;
            ev.platform = platform::name();
            ev.host = platform::hostname();
            emit(ev);
        }
    }

    previous_snapshots_ = std::move(current);
}

} // namespace monitor
} // namespace changeos
