#pragma once

#include "core/event.h"

#include <cstdint>
#include <string>
#include <vector>

namespace changeos {
namespace snapshot {

struct SnapshotConfig {
    std::string output_path;             // Path to write the snapshot file (empty = stdout)
    bool include_processes = true;       // Include process list
    bool include_network = true;         // Include network connections
    bool include_ports = true;           // Include listening ports
    bool include_disk = true;            // Include disk usage
    bool include_load = true;            // Include system load / CPU / memory
    bool include_environment = true;     // Include environment variables
    int max_processes = 50;              // Cap process list size (top by memory)
};

class SnapshotGenerator {
public:
    // Capture a one-shot system snapshot and write it as JSON.
    // Returns true on success.
    static bool generate(const SnapshotConfig& config);

private:
    static std::string json_escape(const std::string& s);
    static std::string capture_metadata();
    static std::string capture_system_load();
    static std::string capture_disk_usage();
    static std::string capture_processes(int max_count);
    static std::string capture_network();
    static std::string capture_ports();
    static std::string capture_environment();
};

} // namespace snapshot
} // namespace changeos
