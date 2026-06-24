#include "log_monitor.h"
#include "core/event.h"
#include "utils/logger.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <thread>

namespace changeos {
namespace monitor {

LogMonitor::LogMonitor() = default;
LogMonitor::~LogMonitor() = default;

bool LogMonitor::is_available() const {
    return true;
}

void LogMonitor::add_watch_path(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    LogWatch watch;
    watch.path = path;
    watch.last_position = 0;
    watch.last_inode = 0;
    watch.active = true;
    watches_.push_back(std::move(watch));
}

void LogMonitor::add_pattern(const std::string& name, const std::string& pattern,
                             const std::string& severity) {
    std::lock_guard<std::mutex> lock(mutex_);
    LogPattern lp;
    lp.name = name;
    try {
        lp.pattern = std::regex(pattern, std::regex::ECMAScript | std::regex::icase);
    } catch (const std::regex_error& e) {
        COS_LOG_ERROR("Invalid regex pattern '" + pattern + "': " + e.what());
        return;
    }
    lp.severity = severity;
    lp.enabled = true;
    patterns_.push_back(std::move(lp));
}

void LogMonitor::clear_patterns() {
    std::lock_guard<std::mutex> lock(mutex_);
    patterns_.clear();
}

bool LogMonitor::on_start() {
    runner_ = std::make_unique<utils::PeriodicRunner>();
    runner_->start([this]() { tick(); }, std::chrono::milliseconds(poll_interval_ms_));
    return true;
}

bool LogMonitor::on_stop() {
    if (runner_) {
        runner_->stop();
    }
    return true;
}

void LogMonitor::tick() {
    std::vector<LogWatch> watches_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        watches_copy = watches_;
    }

    for (auto& watch : watches_copy) {
        if (watch.active && file_exists_and_readable(watch.path)) {
            process_log_file(watch);
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& updated : watches_copy) {
            for (auto& w : watches_) {
                if (w.path == updated.path) {
                    w = updated;
                    break;
                }
            }
        }
    }
}

void LogMonitor::process_log_file(LogWatch& watch) {
    std::uint64_t current_inode = get_file_inode(watch.path);
    std::uint64_t current_size = get_file_size(watch.path);

    // File was rotated or truncated
    if (current_inode != 0 && watch.last_inode != 0 &&
        current_inode != watch.last_inode) {
        COS_LOG_INFO("Log file rotated: " + watch.path);
        watch.last_position = 0;
    }

    // File was truncated
    if (current_size < watch.last_position) {
        COS_LOG_INFO("Log file truncated: " + watch.path);
        watch.last_position = 0;
    }

    watch.last_inode = current_inode;

    std::ifstream file(watch.path, std::ios::binary);
    if (!file.is_open()) {
        return;
    }

    file.seekg(watch.last_position);
    std::string line;
    std::size_t bytes_read = 0;

    while (std::getline(file, line)) {
        if (line.length() > max_line_length_) {
            line.resize(max_line_length_);
        }
        check_patterns(line, watch.path);
        bytes_read += line.length() + 1;  // +1 for newline
    }

    watch.last_position = file.tellg();
    if (watch.last_position == std::streampos(-1)) {
        watch.last_position = current_size;
    }
}

void LogMonitor::check_patterns(const std::string& line, const std::string& source_file) {
    std::vector<LogPattern> patterns_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        patterns_copy = patterns_;
    }

    for (const auto& pattern : patterns_copy) {
        if (!pattern.enabled) continue;

        try {
            std::smatch match;
            if (std::regex_search(line, match, pattern.pattern)) {
                Event e;
                e.timestamp = now();
                e.category = EventCategory::SystemConfig;  // Using existing category
                e.type = EventType::ConfigValueChanged;    // Using existing type
                e.source = "log_monitor";
                e.target = source_file;
                e.summary = "[" + pattern.severity + "] " + pattern.name + ": " + line;
                e.attributes["pattern_name"] = pattern.name;
                e.attributes["severity"] = pattern.severity;
                e.attributes["matched_text"] = match.str();
                e.attributes["full_line"] = line.length() > 500 ? line.substr(0, 500) + "..." : line;

                emit(e);
            }
        } catch (const std::exception& ex) {
            COS_LOG_ERROR("Regex error for pattern '" + pattern.name + "': " + ex.what());
        }
    }
}

bool LogMonitor::file_exists_and_readable(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && (st.st_mode & S_IRUSR);
}

std::uint64_t LogMonitor::get_file_size(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return static_cast<std::uint64_t>(st.st_size);
    }
    return 0;
}

std::uint64_t LogMonitor::get_file_inode(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return static_cast<std::uint64_t>(st.st_ino);
    }
    return 0;
}

} // namespace monitor
} // namespace changeos