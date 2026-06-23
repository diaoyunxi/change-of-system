#pragma once

#include "core/monitor.h"
#include "utils/periodic_runner.h"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace changeos {
namespace monitor {

struct UsbDeviceInfo {
    std::string device_id;
    std::string vendor;
    std::string product;
    std::string serial;
    std::string mount_point;
    std::int64_t size_bytes = 0;
};

class UsbDeviceMonitor : public Monitor {
public:
    UsbDeviceMonitor();
    ~UsbDeviceMonitor() override;

    std::string name() const override { return "usb_device"; }
    std::string description() const override {
        return "Monitors USB device insertion and removal events.";
    }
    bool is_available() const override;
    bool supports_native_events() const override { return false; }

protected:
    bool on_start() override;
    bool on_stop() override;

private:
    void tick();
    std::map<std::string, UsbDeviceInfo> scan_usb_devices();

    std::unique_ptr<utils::PeriodicRunner> runner_;
    std::map<std::string, UsbDeviceInfo> previous_devices_;
    std::mutex mutex_;
    std::atomic<bool> first_scan_{true};
};

} // namespace monitor
} // namespace changeos