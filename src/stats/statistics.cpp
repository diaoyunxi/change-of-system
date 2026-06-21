#include "statistics.h"

#include "utils/logger.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace changeos {
namespace stats {

StatisticsCollector::StatisticsCollector()
    : start_time_(now()), last_update_(now()) {
    // Initialize category counts
    category_counts_[EventCategory::Filesystem] = 0;
    category_counts_[EventCategory::Process] = 0;
    category_counts_[EventCategory::Network] = 0;
    category_counts_[EventCategory::SystemConfig] = 0;
    category_counts_[EventCategory::Unknown] = 0;
}

StatisticsCollector::~StatisticsCollector() = default;

void StatisticsCollector::record(const Event& event) {
    total_events_++;
    category_counts_[event.category]++;
    type_counts_[event.type]++;

    std::lock_guard<std::mutex> lock(mutex_);
    source_counts_[event.source]++;
    target_counts_[event.target]++;

    // Update time buckets
    auto unix_min = to_unix_ms(event.timestamp) / 60000;
    auto unix_hour = to_unix_ms(event.timestamp) / 3600000;
    minute_buckets_[unix_min]++;
    hour_buckets_[unix_hour]++;

    last_update_ = now();

    // Prune old buckets periodically
    if (minute_buckets_.size() > 120) {  // Keep 2 hours of minute data
        prune_time_buckets();
    }
}

EventStats StatisticsCollector::get_stats() const {
    EventStats stats;
    stats.total_events = total_events_.load();
    stats.start_time = start_time_;
    stats.last_update = last_update_;

    // Category counts
    stats.events_per_category[0] = category_counts_.at(EventCategory::Filesystem);
    stats.events_per_category[1] = category_counts_.at(EventCategory::Process);
    stats.events_per_category[2] = category_counts_.at(EventCategory::Network);
    stats.events_per_category[3] = category_counts_.at(EventCategory::SystemConfig);
    stats.events_per_category[4] = category_counts_.at(EventCategory::Unknown);

    // Time-based counts
    auto now_ms = to_unix_ms(now());
    auto one_min_ago = (now_ms - 60000) / 60000;
    auto one_hour_ago = (now_ms - 3600000) / 3600000;
    auto one_day_ago = (now_ms - 86400000) / 3600000;

    std::lock_guard<std::mutex> lock(mutex_);

    // Events in last minute
    for (const auto& [min, count] : minute_buckets_) {
        if (min >= one_min_ago) {
            stats.events_last_minute += count;
        }
    }

    // Events in last hour
    for (const auto& [hour, count] : hour_buckets_) {
        if (hour >= one_hour_ago) {
            stats.events_last_hour += count;
        }
    }

    // Events in last day
    for (const auto& [hour, count] : hour_buckets_) {
        if (hour >= one_day_ago) {
            stats.events_last_day += count;
        }
    }

    // Calculate rates
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now() - start_time_).count();
    if (elapsed > 0) {
        stats.events_per_second = static_cast<double>(stats.total_events) / elapsed;
        stats.events_per_minute = stats.events_per_second * 60.0;
    }

    // Top sources/targets
    stats.top_sources = top_sources(10);
    stats.top_targets = top_targets(10);

    return stats;
}

TimeSeries StatisticsCollector::get_time_series(
        std::chrono::seconds resolution,
        std::chrono::seconds duration) const {

    TimeSeries series;
    series.resolution = resolution;
    series.retention = duration;

    auto now_ms = to_unix_ms(now());
    auto start_ms = now_ms - std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

    std::lock_guard<std::mutex> lock(mutex_);

    if (resolution == std::chrono::seconds(60)) {
        auto start_min = start_ms / 60000;
        auto end_min = now_ms / 60000;

        for (auto min = start_min; min <= end_min; ++min) {
            TimeSeriesPoint point;
            point.timestamp = Timestamp{} + std::chrono::milliseconds(min * 60000);
            auto it = minute_buckets_.find(min);
            point.count = (it != minute_buckets_.end()) ? it->second : 0;
            series.points.push_back(point);
        }
    } else if (resolution == std::chrono::seconds(3600)) {
        auto start_hour = start_ms / 3600000;
        auto end_hour = now_ms / 3600000;

        for (auto hour = start_hour; hour <= end_hour; ++hour) {
            TimeSeriesPoint point;
            point.timestamp = Timestamp{} + std::chrono::milliseconds(hour * 3600000);
            auto it = hour_buckets_.find(hour);
            point.count = (it != hour_buckets_.end()) ? it->second : 0;
            series.points.push_back(point);
        }
    }

    return series;
}

void StatisticsCollector::reset() {
    std::lock_guard<std::mutex> lock(mutex_);

    total_events_ = 0;
    for (auto& [cat, count] : category_counts_) {
        count = 0;
    }
    type_counts_.clear();
    source_counts_.clear();
    target_counts_.clear();
    minute_buckets_.clear();
    hour_buckets_.clear();

    start_time_ = now();
    last_update_ = now();
}

std::uint64_t StatisticsCollector::count_by_category(EventCategory category) const {
    return category_counts_.at(category);
}

std::uint64_t StatisticsCollector::count_by_type(EventType type) const {
    auto it = type_counts_.find(type);
    return (it != type_counts_.end()) ? it->second.load() : 0;
}

std::uint64_t StatisticsCollector::count_in_range(Timestamp from, Timestamp to) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto from_min = to_unix_ms(from) / 60000;
    auto to_min = to_unix_ms(to) / 60000;

    std::uint64_t count = 0;
    for (const auto& [min, c] : minute_buckets_) {
        if (min >= from_min && min <= to_min) {
            count += c;
        }
    }

    return count;
}

std::vector<std::pair<std::string, std::uint64_t>>
StatisticsCollector::top_sources(int n) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::pair<std::string, std::uint64_t>> result(
        source_counts_.begin(), source_counts_.end());

    std::sort(result.begin(), result.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    if (static_cast<int>(result.size()) > n) {
        result.resize(n);
    }

    return result;
}

std::vector<std::pair<std::string, std::uint64_t>>
StatisticsCollector::top_targets(int n) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::pair<std::string, std::uint64_t>> result(
        target_counts_.begin(), target_counts_.end());

    std::sort(result.begin(), result.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    if (static_cast<int>(result.size()) > n) {
        result.resize(n);
    }

    return result;
}

std::string StatisticsCollector::to_json() const {
    auto stats = get_stats();

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);

    oss << "{\n";
    oss << "  \"total_events\": " << stats.total_events << ",\n";
    oss << "  \"events_per_second\": " << stats.events_per_second << ",\n";
    oss << "  \"events_per_minute\": " << stats.events_per_minute << ",\n";
    oss << "  \"events_last_minute\": " << stats.events_last_minute << ",\n";
    oss << "  \"events_last_hour\": " << stats.events_last_hour << ",\n";
    oss << "  \"events_last_day\": " << stats.events_last_day << ",\n";

    oss << "  \"by_category\": {\n";
    oss << "    \"filesystem\": " << stats.events_per_category[0] << ",\n";
    oss << "    \"process\": " << stats.events_per_category[1] << ",\n";
    oss << "    \"network\": " << stats.events_per_category[2] << ",\n";
    oss << "    \"system_config\": " << stats.events_per_category[3] << ",\n";
    oss << "    \"unknown\": " << stats.events_per_category[4] << "\n";
    oss << "  },\n";

    oss << "  \"top_sources\": [";
    for (std::size_t i = 0; i < stats.top_sources.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << "{\"source\": \"" << stats.top_sources[i].first
            << "\", \"count\": " << stats.top_sources[i].second << "}";
    }
    oss << "],\n";

    oss << "  \"top_targets\": [";
    for (std::size_t i = 0; i < stats.top_targets.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << "{\"target\": \"" << stats.top_targets[i].first
            << "\", \"count\": " << stats.top_targets[i].second << "}";
    }
    oss << "]\n";

    oss << "}\n";

    return oss.str();
}

void StatisticsCollector::prune_time_buckets() {
    auto now_ms = to_unix_ms(now());

    // Keep only last 2 hours of minute data
    auto min_cutoff = (now_ms - 7200000) / 60000;
    for (auto it = minute_buckets_.begin(); it != minute_buckets_.end(); ) {
        if (it->first < min_cutoff) {
            it = minute_buckets_.erase(it);
        } else {
            ++it;
        }
    }

    // Keep only last 30 days of hour data
    auto hour_cutoff = (now_ms - 2592000000) / 3600000;
    for (auto it = hour_buckets_.begin(); it != hour_buckets_.end(); ) {
        if (it->first < hour_cutoff) {
            it = hour_buckets_.erase(it);
        } else {
            ++it;
        }
    }
}

std::string format_count(std::uint64_t count) {
    if (count >= 1000000000) {
        return std::to_string(count / 1000000000) + "B";
    } else if (count >= 1000000) {
        return std::to_string(count / 1000000) + "M";
    } else if (count >= 1000) {
        return std::to_string(count / 1000) + "K";
    }
    return std::to_string(count);
}

std::string format_rate(double rate) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);

    if (rate >= 1000000) {
        oss << (rate / 1000000) << "M/s";
    } else if (rate >= 1000) {
        oss << (rate / 1000) << "K/s";
    } else if (rate >= 1) {
        oss << rate << "/s";
    } else if (rate >= 0.0167) {
        oss << (rate * 60) << "/min";
    } else if (rate >= 0.000278) {
        oss << (rate * 3600) << "/hr";
    } else {
        oss << rate << "/s";
    }

    return oss.str();
}

std::string format_duration(std::chrono::seconds seconds) {
    auto secs = seconds.count();

    if (secs < 60) {
        return std::to_string(secs) + "s";
    } else if (secs < 3600) {
        return std::to_string(secs / 60) + "m " + std::to_string(secs % 60) + "s";
    } else if (secs < 86400) {
        return std::to_string(secs / 3600) + "h " +
               std::to_string((secs % 3600) / 60) + "m";
    } else {
        return std::to_string(secs / 86400) + "d " +
               std::to_string((secs % 86400) / 3600) + "h";
    }
}

} // namespace stats
} // namespace changeos
