#pragma once

#include "core/event.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace changeos {
namespace stats {

struct EventStats {
    std::uint64_t total_events = 0;
    std::uint64_t events_per_category[5] = {0};  // Indexed by EventCategory
    std::uint64_t events_per_type[20] = {0};     // Indexed by EventType

    // Time-based statistics
    std::uint64_t events_last_minute = 0;
    std::uint64_t events_last_hour = 0;
    std::uint64_t events_last_day = 0;

    // Rate calculations
    double events_per_second = 0.0;
    double events_per_minute = 0.0;

    // Top sources/targets
    std::vector<std::pair<std::string, std::uint64_t>> top_sources;
    std::vector<std::pair<std::string, std::uint64_t>> top_targets;

    Timestamp start_time;
    Timestamp last_update;
};

struct TimeSeriesPoint {
    Timestamp timestamp;
    std::uint64_t count;
};

struct TimeSeries {
    std::vector<TimeSeriesPoint> points;
    std::chrono::seconds resolution;
    std::chrono::seconds retention;
};

class StatisticsCollector {
public:
    StatisticsCollector();
    ~StatisticsCollector();

    // Record an event
    void record(const Event& event);

    // Get current statistics
    EventStats get_stats() const;

    // Get time series data
    TimeSeries get_time_series(std::chrono::seconds resolution,
                               std::chrono::seconds duration) const;

    // Reset statistics
    void reset();

    // Get events count by category
    std::uint64_t count_by_category(EventCategory category) const;
    std::uint64_t count_by_type(EventType type) const;

    // Get events count in time range
    std::uint64_t count_in_range(Timestamp from, Timestamp to) const;

    // Get top N sources/targets
    std::vector<std::pair<std::string, std::uint64_t>>
        top_sources(int n = 10) const;
    std::vector<std::pair<std::string, std::uint64_t>>
        top_targets(int n = 10) const;

    // Export statistics as JSON
    std::string to_json() const;

private:
    void update_rates();
    void prune_time_buckets();

    mutable std::mutex mutex_;

    // Counters
    std::atomic<std::uint64_t> total_events_{0};
    std::map<EventCategory, std::atomic<std::uint64_t>> category_counts_;
    std::map<EventType, std::atomic<std::uint64_t>> type_counts_;

    // Source/target tracking
    std::map<std::string, std::uint64_t> source_counts_;
    std::map<std::string, std::uint64_t> target_counts_;

    // Time-based buckets (for rate calculation)
    std::map<std::int64_t, std::uint64_t> minute_buckets_;  // key = minute timestamp
    std::map<std::int64_t, std::uint64_t> hour_buckets_;    // key = hour timestamp

    Timestamp start_time_;
    mutable Timestamp last_update_;
};

// Utility functions for statistics formatting
std::string format_count(std::uint64_t count);
std::string format_rate(double rate);
std::string format_duration(std::chrono::seconds seconds);

} // namespace stats
} // namespace changeos
