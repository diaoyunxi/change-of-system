#pragma once

#include "core/monitor.h"
#include "utils/periodic_runner.h"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <vector>

namespace changeos {
namespace monitor {

struct LogPattern {
    std::string name;
    std::regex pattern;
    std::string severity;  // info, warning, error, critical
    bool enabled = true;
};

struct LogWatch {
    std::string path;
    std::uint64_t last_position = 0;
    std::uint64_t last_inode = 0;
    bool active = true;
};

class LogMonitor : public Monitor {
public:
    LogMonitor();
    ~LogMonitor() override;

    std::string name() const override { return "log"; }
    std::string description() const override {
        return "Monitors log files for pattern matches and anomalies.";
    }
    bool is_available() const override;
    bool supports_native_events() const override { return false; }

    // Configuration
    void add_watch_path(const std::string& path);
    void add_pattern(const std::string& name, const std::string& pattern,
                     const std::string& severity = "info");
    void clear_patterns();
    void set_max_line_length(std::size_t len) { max_line_length_ = len; }

protected:
    bool on_start() override;
    bool on_stop() override;

private:
    void tick();
    void process_log_file(LogWatch& watch);
    void check_patterns(const std::string& line, const std::string& source_file);
    bool file_exists_and_readable(const std::string& path);
    std::uint64_t get_file_size(const std::string& path);
    std::uint64_t get_file_inode(const std::string& path);

    std::unique_ptr<utils::PeriodicRunner> runner_;
    std::vector<LogWatch> watches_;
    std::vector<LogPattern> patterns_;
    std::mutex mutex_;
    std::size_t max_line_length_ = 65536;  // 64KB max line length
};

} // namespace monitor
} // namespace changeos