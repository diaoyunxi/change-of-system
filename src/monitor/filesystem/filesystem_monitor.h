#pragma once

#include "core/monitor.h"
#include "utils/periodic_runner.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace changeos {
namespace monitor {

struct FileSnapshot {
    std::string path;
    bool exists = false;
    std::int64_t size = 0;
    std::int64_t mtime = 0;
    std::uint32_t permissions = 0;
    bool is_directory = false;
};

class FilesystemMonitor : public Monitor {
public:
    FilesystemMonitor();
    ~FilesystemMonitor() override;

    std::string name() const override { return "filesystem"; }
    std::string description() const override {
        return "Monitors filesystem changes (create/modify/delete/move/permission).";
    }
    bool is_available() const override { return true; }
    bool supports_native_events() const override;

    void add_watch_path(const std::string& path);
    void remove_watch_path(const std::string& path);
    std::vector<std::string> watch_paths() const;

protected:
    bool on_start() override;
    bool on_stop() override;

private:
    void poll_loop();
    void scan_and_diff();
    FileSnapshot snapshot_of(const std::string& path);

    mutable std::mutex paths_mutex_;
    std::vector<std::string> watch_paths_;
    std::map<std::string, FileSnapshot> previous_snapshots_;
    std::unique_ptr<utils::PeriodicRunner> runner_;

    bool native_enabled_ = false;
    std::atomic<bool> first_scan_{true};
};

} // namespace monitor
} // namespace changeos
