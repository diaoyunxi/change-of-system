#pragma once

#include "core/event.h"
#include <string>
#include <vector>

namespace changeos {
namespace export_ {

enum class ExportFormat {
    CSV,
    JSON
};

class EventExporter {
public:
    static bool export_to_csv(const std::vector<Event>& events, 
                              const std::string& output_path);
    static bool export_to_json(const std::vector<Event>& events, 
                               const std::string& output_path);
    static bool export_events(const std::vector<Event>& events,
                             const std::string& output_path,
                             ExportFormat format);
    
private:
    static std::string escape_csv_field(const std::string& field);
    static std::string format_timestamp(Timestamp ts);
};

} // namespace export_
} // namespace changeos
