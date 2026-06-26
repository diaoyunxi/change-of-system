#pragma once

#include "core/monitor.h"
#include "utils/periodic_runner.h"

#include <map>
#include <memory>
#include <string>

namespace changeos {
namespace monitor {

class PackageMonitor : public Monitor {
public:
    PackageMonitor();
    ~PackageMonitor() override;

    std::string name() const override { return "package"; }
    std::string description() const override {
        return "Monitor package installations, updates, and removals";
    }

    void set_check_interval_ms(int ms) { set_poll_interval_ms(ms); }

protected:
    bool on_start() override;
    bool on_stop() override;

private:
    struct PackageInfo {
        std::string name;
        std::string version;
        std::string architecture;
    };

    void poll();
    std::map<std::string, PackageInfo> snapshot_packages();
    
    enum class PackageManager {
        Unknown,
        APT,        // Debian/Ubuntu
        DNF,        // Fedora/RHEL 8+
        YUM,        // RHEL/CentOS 7
        PACMAN,     // Arch Linux
        ZYPPER,     // openSUSE
        BREW,       // macOS Homebrew
        PORT,       // macOS MacPorts
    };

    PackageManager detect_package_manager();
    PackageManager package_manager_;
    std::unique_ptr<utils::PeriodicRunner> runner_;
};

} // namespace monitor
} // namespace changeos
