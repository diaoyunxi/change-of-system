#include "core/monitor.h"

namespace changeos {

bool Monitor::start() {
    if (running_.exchange(true)) {
        return false;
    }
    return on_start();
}

bool Monitor::stop() {
    if (!running_.exchange(false)) {
        return false;
    }
    return on_stop();
}

void Monitor::on_event(EventCallback cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    callbacks_.push_back(std::move(cb));
}

void Monitor::emit(const Event& event) {
    std::vector<EventCallback> snapshot;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        snapshot = callbacks_;
    }
    for (auto& cb : snapshot) {
        try {
            cb(event);
        } catch (...) {
        }
    }
}

} // namespace changeos
