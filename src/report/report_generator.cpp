#include "report/report_generator.h"
#include "core/event.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <algorithm>

namespace changeos {
namespace report {

ReportGenerator::ReportGenerator(const stats::StatisticsCollector* stats)
    : stats_(stats) {}

std::string ReportGenerator::format_timestamp(Timestamp ts) {
    auto time_t_val = std::chrono::system_clock::to_time_t(ts);
    std::tm tm_val;
    localtime_r(&time_t_val, &tm_val);
    
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_val);
    return std::string(buf);
}

std::string ReportGenerator::format_duration(std::chrono::seconds duration) {
    auto hours = std::chrono::duration_cast<std::chrono::hours>(duration);
    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration - hours);
    auto seconds = duration - hours - minutes;
    
    std::ostringstream oss;
    if (hours.count() > 0) {
        oss << hours.count() << "h ";
    }
    if (minutes.count() > 0 || hours.count() > 0) {
        oss << minutes.count() << "m ";
    }
    oss << seconds.count() << "s";
    return oss.str();
}

std::string ReportGenerator::generate_summary_section(const std::vector<Event>& events) {
    std::ostringstream oss;
    oss << "=== SUMMARY ===\n\n";
    
    if (events.empty()) {
        oss << "No events recorded in this period.\n\n";
        return oss.str();
    }
    
    // Count by category
    std::map<std::string, int> category_counts;
    for (const auto& event : events) {
        category_counts[category_name(event.category)]++;
    }
    
    oss << "Total Events: " << events.size() << "\n";
    oss << "Events by Category:\n";
    for (const auto& [category, count] : category_counts) {
        oss << "  - " << category << ": " << count << "\n";
    }
    oss << "\n";
    
    // Time range
    auto first_time = events.front().timestamp;
    auto last_time = events.back().timestamp;
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(last_time - first_time);
    
    oss << "Time Range: " << format_timestamp(first_time) << " to " 
        << format_timestamp(last_time) << "\n";
    oss << "Duration: " << format_duration(duration) << "\n\n";
    
    return oss.str();
}

std::string ReportGenerator::generate_statistics_section() {
    std::ostringstream oss;
    oss << "=== STATISTICS ===\n\n";
    
    if (!stats_) {
        oss << "Statistics not available.\n\n";
        return oss.str();
    }
    
    auto summary = stats_->get_stats();
    
    oss << "Total Events Recorded: " << summary.total_events << "\n";
    oss << "Events per Second: " << std::fixed << std::setprecision(2) 
        << summary.events_per_second << "\n";
    oss << "Events per Minute: " << std::fixed << std::setprecision(2) 
        << summary.events_per_minute << "\n\n";
    
    if (!summary.top_sources.empty()) {
        oss << "Top Sources:\n";
        int count = 0;
        for (const auto& [source, event_count] : summary.top_sources) {
            if (count++ >= 10) break;
            oss << "  " << source << ": " << event_count << " events\n";
        }
        oss << "\n";
    }
    
    if (!summary.top_targets.empty()) {
        oss << "Top Targets:\n";
        int count = 0;
        for (const auto& [target, event_count] : summary.top_targets) {
            if (count++ >= 10) break;
            oss << "  " << target << ": " << event_count << " events\n";
        }
        oss << "\n";
    }
    
    return oss.str();
}

std::string ReportGenerator::generate_events_section(const std::vector<Event>& events) {
    std::ostringstream oss;
    oss << "=== RECENT EVENTS ===\n\n";
    
    if (events.empty()) {
        oss << "No events to display.\n\n";
        return oss.str();
    }
    
    for (const auto& event : events) {
        oss << "[" << format_timestamp(event.timestamp) << "] "
            << category_name(event.category) << " | "
            << type_name(event.type) << "\n";
        oss << "  Source: " << event.source << "\n";
        oss << "  Target: " << event.target << "\n";
        oss << "  Summary: " << event.summary << "\n";
        
        if (!event.attributes.empty()) {
            oss << "  Attributes:\n";
            for (const auto& [key, value] : event.attributes) {
                oss << "    " << key << ": " << value << "\n";
            }
        }
        oss << "\n";
    }
    
    return oss.str();
}

bool ReportGenerator::generate_text_report(const ReportConfig& config,
                                          const std::vector<Event>& recent_events) {
    std::ofstream file(config.output_path);
    if (!file) {
        return false;
    }
    
    // Header
    file << "========================================\n";
    file << "  " << config.title << "\n";
    file << "========================================\n";
    file << "Generated: " << format_timestamp(now()) << "\n\n";
    
    // Summary section
    if (config.include_summary) {
        file << generate_summary_section(recent_events);
    }
    
    // Statistics section
    if (config.include_statistics) {
        file << generate_statistics_section();
    }
    
    // Events section
    if (config.include_recent_events) {
        std::vector<Event> events_to_show = recent_events;
        if (events_to_show.size() > static_cast<size_t>(config.max_recent_events)) {
            events_to_show.resize(config.max_recent_events);
        }
        file << generate_events_section(events_to_show);
    }
    
    // Footer
    file << "========================================\n";
    file << "End of Report\n";
    file << "========================================\n";
    
    file.close();
    return file.good();
}

bool ReportGenerator::generate(const ReportConfig& config) {
    // This would normally query events from storage for the time period
    // For now, we'll use an empty list as placeholder
    std::vector<Event> events;
    return generate_text_report(config, events);
}

} // namespace report
} // namespace changeos
