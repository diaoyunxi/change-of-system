#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace changeos {
namespace utils {

class PeriodicRunner {
public:
    using Task = std::function<void()>;

    PeriodicRunner() = default;
    ~PeriodicRunner() { stop(); }

    PeriodicRunner(const PeriodicRunner&) = delete;
    PeriodicRunner& operator=(const PeriodicRunner&) = delete;

    void start(Task task, std::chrono::milliseconds interval) {
        task_ = std::move(task);
        interval_ = interval;
        running_.store(true);
        thread_ = std::thread([this]() { run_loop(); });
    }

    void stop() {
        if (!running_.exchange(false)) return;
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
    }

    bool is_running() const { return running_.load(); }

private:
    void run_loop() {
        while (running_.load()) {
            try {
                if (task_) task_();
            } catch (...) {
            }
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, interval_, [this]() { return !running_.load(); });
        }
    }

    Task task_;
    std::chrono::milliseconds interval_{1000};
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

} // namespace utils
} // namespace changeos
