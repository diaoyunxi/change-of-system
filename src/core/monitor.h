#pragma once

#include "core/event.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace changeos {

using EventCallback = std::function<void(const Event&)>;

class Monitor {
public:
    virtual ~Monitor() = default;

    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual bool is_available() const { return true; }
    virtual bool supports_native_events() const { return true; }

    bool start();
    bool stop();
    bool is_running() const { return running_.load(); }

    void on_event(EventCallback cb);

    void set_poll_interval_ms(int ms) { poll_interval_ms_ = ms; }
    int poll_interval_ms() const { return poll_interval_ms_; }

    void set_use_native_events(bool use) { use_native_events_ = use; }
    bool use_native_events() const { return use_native_events_; }

protected:
    virtual bool on_start() { return true; }
    virtual bool on_stop() { return true; }

    void emit(const Event& event);

    std::atomic<bool> running_{false};
    bool use_native_events_{true};
    int poll_interval_ms_{3000};

private:
    std::mutex callbacks_mutex_;
    std::vector<EventCallback> callbacks_;
};

} // namespace changeos
