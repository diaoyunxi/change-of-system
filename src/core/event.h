#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace changeos {

using Timestamp = std::chrono::system_clock::time_point;

inline Timestamp now() {
    return std::chrono::system_clock::now();
}

inline std::int64_t to_unix_ms(Timestamp t) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        t.time_since_epoch()).count();
}

enum class EventCategory {
    Filesystem,
    Process,
    Network,
    SystemConfig,
    Hardware,      // USB and other hardware events
    System,        // System-level metrics (disk, load)
    Unknown
};

enum class EventType {
    FileCreated,
    FileModified,
    FileDeleted,
    FileMoved,
    FilePermissionChanged,

    ProcessStarted,
    ProcessStopped,
    ProcessCpuSpike,
    ProcessMemorySpike,

    NetworkConnectionOpened,
    NetworkConnectionClosed,
    NetworkBandwidthSpike,
    NetworkDnsQuery,

    ConfigValueChanged,
    ServiceStateChanged,
    UserLoggedIn,
    UserLoggedOut,

    // Hardware events
    UsbDeviceInserted,
    UsbDeviceRemoved,

    // System events
    DiskSpaceWarning,
    DiskSpaceCritical,
    DiskSpaceChanged,
    SystemLoadHigh,
    SystemLoadNormal,

    // Log events
    LogPatternMatched,
    LogAnomalyDetected,

    Unknown
};

std::string category_name(EventCategory c);
std::string type_name(EventType t);
EventCategory category_of(EventType t);

struct Event {
    std::uint64_t id = 0;
    Timestamp timestamp = now();
    EventCategory category = EventCategory::Unknown;
    EventType type = EventType::Unknown;
    std::string source;
    std::string target;
    std::map<std::string, std::string> attributes;
    std::string summary;
    std::string host;
    std::string platform;
};

using EventList = std::vector<Event>;

} // namespace changeos
