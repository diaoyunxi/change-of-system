#include "export/event_exporter.h"
#include "core/event.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <cstdio>

namespace changeos {
namespace export_ {

std::string EventExporter::format_timestamp(Timestamp ts) {
    auto time_t_val = std::chrono::system_clock::to_time_t(ts);
    std::tm tm_val;
    localtime_r(&time_t_val, &tm_val);
    
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_val);
    return std::string(buf);
}

std::string EventExporter::escape_csv_field(const std::string& field) {
    bool needs_quotes = false;
    for (char c : field) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            needs_quotes = true;
            break;
        }
    }
    
    if (!needs_quotes) {
        return field;
    }
    
    std::string escaped = "\"";
    for (char c : field) {
        if (c == '"') {
            escaped += "\"\"";
        } else {
            escaped += c;
        }
    }
    escaped += "\"";
    return escaped;
}

std::string EventExporter::json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

bool EventExporter::export_to_csv(const std::vector<Event>& events, 
                                  const std::string& output_path) {
    std::ofstream file(output_path);
    if (!file) {
        return false;
    }
    
    // Write CSV header
    file << "id,timestamp,category,type,source,target,summary,host,platform,attributes\n";
    
    // Write events
    for (const auto& event : events) {
        file << event.id << ","
             << escape_csv_field(format_timestamp(event.timestamp)) << ","
             << escape_csv_field(category_name(event.category)) << ","
             << escape_csv_field(type_name(event.type)) << ","
             << escape_csv_field(event.source) << ","
             << escape_csv_field(event.target) << ","
             << escape_csv_field(event.summary) << ","
             << escape_csv_field(event.host) << ","
             << escape_csv_field(event.platform) << ",";
        
        // Format attributes as key=value pairs
        std::string attrs;
        for (const auto& [key, value] : event.attributes) {
            if (!attrs.empty()) attrs += "; ";
            attrs += key + "=" + value;
        }
        file << escape_csv_field(attrs) << "\n";
    }
    
    file.close();
    return file.good();
}

bool EventExporter::export_to_json(const std::vector<Event>& events, 
                                   const std::string& output_path) {
    std::ofstream file(output_path);
    if (!file) {
        return false;
    }
    
    file << "[\n";
    
    for (size_t i = 0; i < events.size(); ++i) {
        const auto& event = events[i];
        
        file << "  {\n";
        file << "    \"id\": " << event.id << ",\n";
        file << "    \"timestamp\": \"" << format_timestamp(event.timestamp) << "\",\n";
        file << "    \"category\": \"" << json_escape(category_name(event.category)) << "\",\n";
        file << "    \"type\": \"" << json_escape(type_name(event.type)) << "\",\n";
        file << "    \"source\": \"" << json_escape(event.source) << "\",\n";
        file << "    \"target\": \"" << json_escape(event.target) << "\",\n";
        file << "    \"summary\": \"" << json_escape(event.summary) << "\",\n";
        file << "    \"host\": \"" << json_escape(event.host) << "\",\n";
        file << "    \"platform\": \"" << json_escape(event.platform) << "\",\n";
        
        // Attributes object
        file << "    \"attributes\": {";
        bool first = true;
        for (const auto& [key, value] : event.attributes) {
            if (!first) file << ",";
            file << "\n      \"" << json_escape(key) << "\": \"" << json_escape(value) << "\"";
            first = false;
        }
        if (!event.attributes.empty()) {
            file << "\n    ";
        }
        file << "}\n";
        
        file << "  }";
        if (i < events.size() - 1) {
            file << ",";
        }
        file << "\n";
    }
    
    file << "]\n";
    file.close();
    return file.good();
}

bool EventExporter::export_events(const std::vector<Event>& events,
                                 const std::string& output_path,
                                 ExportFormat format) {
    switch (format) {
        case ExportFormat::CSV:
            return export_to_csv(events, output_path);
        case ExportFormat::JSON:
            return export_to_json(events, output_path);
        default:
            return false;
    }
}

} // namespace export_
} // namespace changeos
