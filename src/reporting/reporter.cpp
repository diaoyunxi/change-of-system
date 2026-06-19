#include "reporting/reporter.h"

#include "utils/logger.h"

#include <chrono>
#include <sstream>
#include <string>

namespace changeos {
namespace reporting {

Reporter::Reporter() = default;
Reporter::~Reporter() { stop(); }

void Reporter::configure(const std::string& endpoint,
                         const std::string& api_key,
                         int batch_size,
                         int interval_ms) {
    endpoint_ = endpoint;
    api_key_ = api_key;
    batch_size_ = batch_size;
    interval_ms_ = interval_ms;
}

void Reporter::start() {
    if (running_.exchange(true)) return;
    COS_LOG_INFO("Remote reporter starting: " + endpoint_);
    runner_ = std::make_unique<utils::PeriodicRunner>();
    runner_->start([this]() { tick(); },
                   std::chrono::milliseconds(interval_ms_));
}

void Reporter::stop() {
    if (!running_.exchange(false)) return;
    if (runner_) runner_->stop();
    runner_.reset();
    flush_batch();
    COS_LOG_INFO("Remote reporter stopped");
}

void Reporter::enqueue(const Event& event) {
    if (!enabled_.load()) return;
    std::lock_guard<std::mutex> lock(queue_mutex_);
    queue_.push_back(event);
}

void Reporter::enqueue_batch(const std::vector<Event>& events) {
    if (!enabled_.load()) return;
    std::lock_guard<std::mutex> lock(queue_mutex_);
    queue_.insert(queue_.end(), events.begin(), events.end());
}

std::size_t Reporter::queue_size() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return queue_.size();
}

void Reporter::tick() {
    flush_batch();
}

void Reporter::flush_batch() {
    std::vector<Event> batch;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (queue_.empty()) return;
        auto end = queue_.size() > static_cast<std::size_t>(batch_size_)
            ? queue_.begin() + batch_size_
            : queue_.end();
        batch.assign(queue_.begin(), end);
        queue_.erase(queue_.begin(), end);
    }

    if (batch.empty()) return;

    std::string payload = serialize(batch);
    bool ok = send_http_post(payload);
    if (!ok) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_.insert(queue_.begin(), batch.begin(), batch.end());
        COS_LOG_WARN("Failed to send report; re-queued "
                     + std::to_string(batch.size()) + " events");
    } else {
        COS_LOG_INFO("Sent " + std::to_string(batch.size())
                     + " events to remote endpoint");
    }
}

std::string Reporter::serialize(const std::vector<Event>& events) {
    std::ostringstream oss;
    oss << "[\n";
    for (size_t i = 0; i < events.size(); ++i) {
        const auto& e = events[i];
        oss << "  {";
        oss << "\"ts\":" << to_unix_ms(e.timestamp) << ",";
        oss << "\"category\":\"" << category_name(e.category) << "\",";
        oss << "\"type\":\"" << type_name(e.type) << "\",";
        oss << "\"source\":\"" << e.source << "\",";
        oss << "\"target\":\"" << e.target << "\",";
        oss << "\"host\":\"" << e.host << "\",";
        oss << "\"platform\":\"" << e.platform << "\",";
        oss << "\"summary\":\"" << e.summary << "\"";
        oss << "}";
        if (i + 1 < events.size()) oss << ",";
        oss << "\n";
    }
    oss << "]\n";
    return oss.str();
}

bool Reporter::send_http_post(const std::string& payload) {
    if (endpoint_.empty()) return false;
    COS_LOG_INFO("Remote reporter would POST "
                 + std::to_string(payload.size()) + " bytes to "
                 + endpoint_);
    return true;
}

} // namespace reporting
} // namespace changeos
