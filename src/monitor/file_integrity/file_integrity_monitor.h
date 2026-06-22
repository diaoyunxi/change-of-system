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

struct FileHash {
    std::string path;
    std::string hash;
    std::uint64_t size = 0;
    std::uint64_t mtime = 0;
};

class FileIntegrityMonitor : public Monitor {
public:
    FileIntegrityMonitor();
    ~FileIntegrityMonitor() override;

    std::string name() const override { return "file_integrity"; }
    std::string description() const override {
        return "Monitors file integrity using SHA-256 hashing.";
    }
    bool is_available() const override;
    bool supports_native_events() const override { return false; }

    void add_watch_file(const std::string& path);
    void remove_watch_file(const std::string& path);
    const std::vector<std::string>& watch_files() const;

protected:
    bool on_start() override;
    bool on_stop() override;

private:
    void tick();
    std::string compute_hash(const std::string& path);
    std::uint64_t get_file_size(const std::string& path);
    std::uint64_t get_file_mtime(const std::string& path);

    std::unique_ptr<utils::PeriodicRunner> runner_;
    std::vector<std::string> watch_files_;
    std::map<std::string, FileHash> previous_hashes_;
    std::mutex mutex_;
    std::atomic<bool> first_scan_{true};
};

} // namespace monitor
} // namespace changeos