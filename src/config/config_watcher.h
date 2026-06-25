#pragma once

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>

namespace changeos {
namespace config {

class ConfigWatcher {
public:
    using ReloadCallback = std::function<void()>;
    
    ConfigWatcher();
    ~ConfigWatcher();
    
    void start_watching(const std::string& config_path, 
                       ReloadCallback callback,
                       int check_interval_ms = 5000);
    void stop_watching();
    
    bool is_watching() const { return watching_.load(); }
    
private:
    void watch_loop();
    
    std::string config_path_;
    ReloadCallback callback_;
    int check_interval_ms_ = 5000;
    
    std::thread watch_thread_;
    std::atomic<bool> watching_{false};
    
    std::chrono::system_clock::time_point last_modified_;
    std::mutex mutex_;
    
    std::chrono::system_clock::time_point get_file_modified_time(const std::string& path);
};

} // namespace config
} // namespace changeos
