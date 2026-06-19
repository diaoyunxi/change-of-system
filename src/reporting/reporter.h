#pragma once

#include "core/event.h"
#include "utils/periodic_runner.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace changeos {
namespace reporting {

class Reporter {
public:
    Reporter();
    ~Reporter();

    void configure(const std::string& endpoint,
                   const std::string& api_key,
                   int batch_size = 100,
                   int interval_ms = 10000);

    void start();
    void stop();
    bool is_running() const { return running_.load(); }

    void enqueue(const Event& event);
    void enqueue_batch(const std::vector<Event>& events);

    void set_enabled(bool enabled) { enabled_.store(enabled); }
    bool enabled() const { return enabled_.load(); }

    std::size_t queue_size() const;

private:
    void tick();
    void flush_batch();
    std::string serialize(const std::vector<Event>& events);
    bool send_http_post(const std::string& payload);

    std::string endpoint_;
    std::string api_key_;
    int batch_size_ = 100;
    int interval_ms_ = 10000;

    std::atomic<bool> enabled_{false};
    std::atomic<bool> running_{false};
    mutable std::mutex queue_mutex_;
    std::vector<Event> queue_;
    std::unique_ptr<utils::PeriodicRunner> runner_;
};

} // namespace reporting
} // namespace changeos
