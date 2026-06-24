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

} // namespace changeos
