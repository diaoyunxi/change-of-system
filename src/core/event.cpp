#include "core/event.h"

namespace changeos {

std::string category_name(EventCategory c) {
    switch (c) {
        case EventCategory::Filesystem: return "filesystem";
        case EventCategory::Process: return "process";
        case EventCategory::Network: return "network";
        case EventCategory::SystemConfig: return "system_config";
        case EventCategory::Hardware: return "hardware";
        case EventCategory::System: return "system";
        default: return "unknown";
    }
}

std::string type_name(EventType t) {
    switch (t) {
        case EventType::FileCreated: return "file_created";
        case EventType::FileModified: return "file_modified";
        case EventType::FileDeleted: return "file_deleted";
        case EventType::FileMoved: return "file_moved";
        case EventType::FilePermissionChanged: return "file_permission_changed";

        case EventType::ProcessStarted: return "process_started";
        case EventType::ProcessStopped: return "process_stopped";
        case EventType::ProcessCpuSpike: return "process_cpu_spike";
        case EventType::ProcessMemorySpike: return "process_memory_spike";

        case EventType::NetworkConnectionOpened: return "network_connection_opened";
        case EventType::NetworkConnectionClosed: return "network_connection_closed";
        case EventType::NetworkBandwidthSpike: return "network_bandwidth_spike";
        case EventType::NetworkDnsQuery: return "network_dns_query";

        case EventType::ConfigValueChanged: return "config_value_changed";
        case EventType::ServiceStateChanged: return "service_state_changed";
        case EventType::UserLoggedIn: return "user_logged_in";
        case EventType::UserLoggedOut: return "user_logged_out";

        case EventType::UsbDeviceInserted: return "usb_device_inserted";
        case EventType::UsbDeviceRemoved: return "usb_device_removed";

        case EventType::DiskSpaceWarning: return "disk_space_warning";
        case EventType::DiskSpaceCritical: return "disk_space_critical";
        case EventType::DiskSpaceChanged: return "disk_space_changed";
        case EventType::SystemLoadHigh: return "system_load_high";
        case EventType::SystemLoadNormal: return "system_load_normal";

        case EventType::LogPatternMatched: return "log_pattern_matched";
        case EventType::LogAnomalyDetected: return "log_anomaly_detected";

        default: return "unknown";
    }
}

EventCategory category_of(EventType t) {
    switch (t) {
        case EventType::FileCreated:
        case EventType::FileModified:
        case EventType::FileDeleted:
        case EventType::FileMoved:
        case EventType::FilePermissionChanged:
            return EventCategory::Filesystem;

        case EventType::ProcessStarted:
        case EventType::ProcessStopped:
        case EventType::ProcessCpuSpike:
        case EventType::ProcessMemorySpike:
            return EventCategory::Process;

        case EventType::NetworkConnectionOpened:
        case EventType::NetworkConnectionClosed:
        case EventType::NetworkBandwidthSpike:
        case EventType::NetworkDnsQuery:
            return EventCategory::Network;

        case EventType::ConfigValueChanged:
        case EventType::ServiceStateChanged:
        case EventType::UserLoggedIn:
        case EventType::UserLoggedOut:
            return EventCategory::SystemConfig;

        case EventType::UsbDeviceInserted:
        case EventType::UsbDeviceRemoved:
            return EventCategory::Hardware;

        case EventType::DiskSpaceWarning:
        case EventType::DiskSpaceCritical:
        case EventType::DiskSpaceChanged:
        case EventType::SystemLoadHigh:
        case EventType::SystemLoadNormal:
        case EventType::LogPatternMatched:
        case EventType::LogAnomalyDetected:
            return EventCategory::System;

        default:
            return EventCategory::Unknown;
    }
}

EventCategory category_from_name(const std::string& name) {
    if (name == "filesystem")    return EventCategory::Filesystem;
    if (name == "process")       return EventCategory::Process;
    if (name == "network")       return EventCategory::Network;
    if (name == "system_config") return EventCategory::SystemConfig;
    if (name == "hardware")      return EventCategory::Hardware;
    if (name == "system")        return EventCategory::System;
    return EventCategory::Unknown;
}

EventType type_from_name(const std::string& name) {
    if (name == "file_created")             return EventType::FileCreated;
    if (name == "file_modified")            return EventType::FileModified;
    if (name == "file_deleted")             return EventType::FileDeleted;
    if (name == "file_moved")               return EventType::FileMoved;
    if (name == "file_permission_changed")  return EventType::FilePermissionChanged;

    if (name == "process_started")          return EventType::ProcessStarted;
    if (name == "process_stopped")          return EventType::ProcessStopped;
    if (name == "process_cpu_spike")        return EventType::ProcessCpuSpike;
    if (name == "process_memory_spike")     return EventType::ProcessMemorySpike;

    if (name == "network_connection_opened")  return EventType::NetworkConnectionOpened;
    if (name == "network_connection_closed")  return EventType::NetworkConnectionClosed;
    if (name == "network_bandwidth_spike")    return EventType::NetworkBandwidthSpike;
    if (name == "network_dns_query")          return EventType::NetworkDnsQuery;

    if (name == "config_value_changed")    return EventType::ConfigValueChanged;
    if (name == "service_state_changed")   return EventType::ServiceStateChanged;
    if (name == "user_logged_in")          return EventType::UserLoggedIn;
    if (name == "user_logged_out")         return EventType::UserLoggedOut;

    if (name == "usb_device_inserted")     return EventType::UsbDeviceInserted;
    if (name == "usb_device_removed")      return EventType::UsbDeviceRemoved;

    if (name == "disk_space_warning")      return EventType::DiskSpaceWarning;
    if (name == "disk_space_critical")     return EventType::DiskSpaceCritical;
    if (name == "disk_space_changed")      return EventType::DiskSpaceChanged;
    if (name == "system_load_high")        return EventType::SystemLoadHigh;
    if (name == "system_load_normal")      return EventType::SystemLoadNormal;

    if (name == "log_pattern_matched")     return EventType::LogPatternMatched;
    if (name == "log_anomaly_detected")    return EventType::LogAnomalyDetected;

    return EventType::Unknown;
}

} // namespace changeos
