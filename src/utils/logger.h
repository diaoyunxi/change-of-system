#pragma once

#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace changeos {
namespace utils {

enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error
};

class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    void set_file(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) file_.close();
        file_.open(path, std::ios::app);
    }

    void set_console_enabled(bool enabled) {
        std::lock_guard<std::mutex> lock(mutex_);
        console_enabled_ = enabled;
    }

    void log(LogLevel level, const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string level_str;
        switch (level) {
            case LogLevel::Debug: level_str = "DEBUG"; break;
            case LogLevel::Info: level_str = "INFO"; break;
            case LogLevel::Warn: level_str = "WARN"; break;
            case LogLevel::Error: level_str = "ERROR"; break;
        }
        auto now = std::chrono::system_clock::now();
        auto time_t_val = std::chrono::system_clock::to_time_t(now);
        char buf[64];
        // 使用线程安全的 localtime_r（POSIX）或 localtime_s（Windows）替代 std::localtime
        struct tm tm_buf;
#if defined(_WIN32) || defined(_MSC_VER)
        localtime_s(&tm_buf, &time_t_val);
#else
        localtime_r(&time_t_val, &tm_buf);
#endif
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
        std::string full = std::string("[") + buf + "] [" + level_str + "] " + message + "\n";
        if (console_enabled_) {
            std::fprintf(stderr, "%s", full.c_str());
        }
        if (file_.is_open()) {
            file_ << full;
            file_.flush();
        }
    }

private:
    Logger() = default;
    std::mutex mutex_;
    std::ofstream file_;
    bool console_enabled_{true};
};

} // namespace utils
} // namespace changeos

#include <chrono>
#include <cstdio>
#include <ctime>

#define COS_LOG_DEBUG(msg) ::changeos::utils::Logger::instance().log(::changeos::utils::LogLevel::Debug, msg)
#define COS_LOG_INFO(msg)  ::changeos::utils::Logger::instance().log(::changeos::utils::LogLevel::Info, msg)
#define COS_LOG_WARN(msg)  ::changeos::utils::Logger::instance().log(::changeos::utils::LogLevel::Warn, msg)
#define COS_LOG_ERROR(msg) ::changeos::utils::Logger::instance().log(::changeos::utils::LogLevel::Error, msg)
