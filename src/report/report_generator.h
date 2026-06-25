#pragma once

#include "core/event.h"
#include "stats/statistics.h"
#include <string>
#include <vector>
#include <chrono>

namespace changeos {
namespace report {

struct ReportConfig {
    std::string title = "System Change Report";
    std::string output_path = "report.txt";
    int period_hours = 24;
    bool include_summary = true;
    bool include_statistics = true;
    bool include_recent_events = true;
    int max_recent_events = 50;
};

class ReportGenerator {
public:
    explicit ReportGenerator(const stats::StatisticsCollector* stats);
    
    bool generate(const ReportConfig& config);
    bool generate_text_report(const ReportConfig& config, 
                             const std::vector<Event>& recent_events);
    
private:
    const stats::StatisticsCollector* stats_;
    
    std::string format_duration(std::chrono::seconds duration);
    std::string format_timestamp(Timestamp ts);
    std::string generate_summary_section(const std::vector<Event>& events);
    std::string generate_statistics_section();
    std::string generate_events_section(const std::vector<Event>& events);
};

} // namespace report
} // namespace changeos
