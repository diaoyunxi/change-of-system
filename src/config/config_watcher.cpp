#include "config/config_watcher.h"
#include "utils/logger.h"

#include <sys/stat.h>
#include <chrono>

namespace changeos {
namespace config {

ConfigWatcher::ConfigWatcher() = default;

ConfigWatcher::~ConfigWatcher() {
    stop_watching();
}

std::chrono::system_clock::time_point ConfigWatcher::get_file_modified_time(const std::string& path) {
    struct stat file_stat;
    if (stat(path.c_str(), &file_stat) == 0) {
        return std::chrono::system_clock::from_time_t(file_stat.st_mtime);
    }
    return std::chrono::system_clock::time_point{};
}

void ConfigWatcher::watch_loop() {
    while (watching_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms_));
        
        if (!watching_.load()) break;
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto current_modified = get_file_modified_time(config_path_);
        
        if (current_modified != last_modified_ && current_modified != std::chrono::system_clock::time_point{}) {
            COS_LOG_INFO("Configuration file changed, reloading: " + config_path_);
            last_modified_ = current_modified;
            
            if (callback_) {
                try {
                    callback_();
                    COS_LOG_INFO("Configuration reloaded successfully");
                } catch (const std::exception& e) {
                    COS_LOG_ERROR("Failed to reload configuration: " + std::string(e.what()));
                }
            }
        }
    }
}

void ConfigWatcher::start_watching(const std::string& config_path,
                                  ReloadCallback callback,
                                  int check_interval_ms) {
    stop_watching();
    
    std::lock_guard<std::mutex> lock(mutex_);
    config_path_ = config_path;
    callback_ = std::move(callback);
    check_interval_ms_ = check_interval_ms;
    last_modified_ = get_file_modified_time(config_path);
    
    watching_.store(true);
    watch_thread_ = std::thread(&ConfigWatcher::watch_loop, this);
    
    COS_LOG_INFO("Started watching configuration file: " + config_path);
}

void ConfigWatcher::stop_watching() {
    if (watching_.load()) {
        watching_.store(false);
        if (watch_thread_.joinable()) {
            watch_thread_.join();
        }
        COS_LOG_INFO("Stopped watching configuration file");
    }
}

} // namespace config
} // namespace changeos
