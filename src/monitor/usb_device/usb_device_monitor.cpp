#include "monitor/usb_device/usb_device_monitor.h"
#include "core/event.h"
#include "platform/platform_detection.h"
#include "utils/logger.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#pragma comment(lib, "setupapi.lib")
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace changeos {
namespace monitor {

UsbDeviceMonitor::UsbDeviceMonitor() = default;
UsbDeviceMonitor::~UsbDeviceMonitor() = default;

bool UsbDeviceMonitor::is_available() const {
    return true;
}

bool UsbDeviceMonitor::on_start() {
    runner_ = std::make_unique<utils::PeriodicRunner>();
    runner_->start([this]() { tick(); },
                   std::chrono::milliseconds(poll_interval_ms_));
    COS_LOG_INFO("USB device monitor started");
    return true;
}

bool UsbDeviceMonitor::on_stop() {
    if (runner_) {
        runner_->stop();
    }
    COS_LOG_INFO("USB device monitor stopped");
    return true;
}

void UsbDeviceMonitor::tick() {
    auto current_devices = scan_usb_devices();

    std::lock_guard<std::mutex> lock(mutex_);

    if (first_scan_) {
        previous_devices_ = current_devices;
        first_scan_ = false;
        return;
    }

    // Detect inserted devices
    for (const auto& [id, info] : current_devices) {
        if (previous_devices_.find(id) == previous_devices_.end()) {
            Event e;
            e.category = EventCategory::Hardware;
            e.type = EventType::UsbDeviceInserted;
            e.source = "usb_device_monitor";
            e.target = info.device_id;
            e.attributes["vendor"] = info.vendor;
            e.attributes["product"] = info.product;
            e.attributes["serial"] = info.serial;
            e.attributes["mount_point"] = info.mount_point;
            e.attributes["size_bytes"] = std::to_string(info.size_bytes);
            e.summary = "USB device inserted: " + info.vendor + " " + info.product;
            emit(e);
            COS_LOG_INFO("USB device inserted: " + info.device_id);
        }
    }

    // Detect removed devices
    for (const auto& [id, info] : previous_devices_) {
        if (current_devices.find(id) == current_devices.end()) {
            Event e;
            e.category = EventCategory::Hardware;
            e.type = EventType::UsbDeviceRemoved;
            e.source = "usb_device_monitor";
            e.target = info.device_id;
            e.attributes["vendor"] = info.vendor;
            e.attributes["product"] = info.product;
            e.attributes["serial"] = info.serial;
            e.attributes["mount_point"] = info.mount_point;
            e.attributes["size_bytes"] = std::to_string(info.size_bytes);
            e.summary = "USB device removed: " + info.vendor + " " + info.product;
            emit(e);
            COS_LOG_INFO("USB device removed: " + info.device_id);
        }
    }

    previous_devices_ = current_devices;
}

std::map<std::string, UsbDeviceInfo> UsbDeviceMonitor::scan_usb_devices() {
    std::map<std::string, UsbDeviceInfo> devices;

#ifdef __linux__
    // Scan /sys/bus/usb/devices/ for USB devices
    const char* usb_path = "/sys/bus/usb/devices";
    DIR* dir = opendir(usb_path);
    if (!dir) {
        return devices;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        // Skip . and .. and interface entries
        if (entry->d_name[0] == '.' || strchr(entry->d_name, ':') != nullptr) {
            continue;
        }

        std::string device_path = std::string(usb_path) + "/" + entry->d_name;

        // Read device information
        UsbDeviceInfo info;
        info.device_id = entry->d_name;

        auto read_file = [&device_path](const char* filename) -> std::string {
            std::ifstream f(device_path + "/" + filename);
            std::string content;
            if (f.is_open()) {
                std::getline(f, content);
                // Trim whitespace
                while (!content.empty() && (content.back() == '\n' || content.back() == '\r')) {
                    content.pop_back();
                }
            }
            return content;
        };

        info.vendor = read_file("manufacturer");
        info.product = read_file("product");
        info.serial = read_file("serial");

        if (info.vendor.empty()) info.vendor = "Unknown";
        if (info.product.empty()) info.product = "Unknown";

        // Try to find mount point
        std::ifstream mounts("/proc/mounts");
        std::string line;
        while (std::getline(mounts, line)) {
            if (line.find("/dev/sd") != std::string::npos ||
                line.find("/dev/usb") != std::string::npos) {
                std::istringstream iss(line);
                std::string device, mount_point;
                iss >> device >> mount_point;
                info.mount_point = mount_point;
                break;
            }
        }

        // Read size from size file (in blocks, 512 bytes each)
        std::string size_str = read_file("size");
        if (!size_str.empty()) {
            try {
                info.size_bytes = std::stoll(size_str) * 512;
            } catch (...) {
                info.size_bytes = 0;
            }
        }

        // Only add actual devices (not root hubs)
        if (info.device_id.length() >= 3 && info.device_id.find("-") != std::string::npos) {
            devices[info.device_id] = info;
        }
    }
    closedir(dir);
#elif defined(_WIN32)
    // Windows implementation using SetupAPI
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVINTERFACE_USB_DEVICE,
                                             nullptr, nullptr,
                                             DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo != INVALID_HANDLE_VALUE) {
        SP_DEVINFO_DATA devInfoData;
        devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

        for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
            UsbDeviceInfo info;

            char deviceId[MAX_PATH];
            SetupDiGetDeviceInstanceIdA(hDevInfo, &devInfoData, deviceId, MAX_PATH, nullptr);
            info.device_id = deviceId;

            // Get device description
            char desc[MAX_PATH];
            DWORD size;
            if (SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devInfoData, SPDRP_DEVICEDESC,
                                                   nullptr, (PBYTE)desc, MAX_PATH, &size)) {
                info.product = desc;
            }

            // Get manufacturer
            char mfg[MAX_PATH];
            if (SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devInfoData, SPDRP_MFG,
                                                   nullptr, (PBYTE)mfg, MAX_PATH, &size)) {
                info.vendor = mfg;
            }

            if (info.vendor.empty()) info.vendor = "Unknown";
            if (info.product.empty()) info.product = "Unknown";

            devices[info.device_id] = info;
        }
        SetupDiDestroyDeviceInfoList(hDevInfo);
    }
#elif defined(__APPLE__)
    // macOS: Use system_profiler output (simplified)
    // In a real implementation, use IOKit
    FILE* pipe = popen("system_profiler SPUSBDataType 2>/dev/null", "r");
    if (pipe) {
        char buffer[1024];
        std::string result;
        while (fgets(buffer, sizeof(buffer), pipe)) {
            result += buffer;
        }
        pclose(pipe);
        // Parse the output - simplified version
        // Real implementation would use IOKit framework
    }
#endif

    return devices;
}

} // namespace monitor
} // namespace changeos