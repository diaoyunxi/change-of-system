#include "info/system_info.h"

#include "platform/platform_detection.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#if defined(__linux__) || defined(__ANDROID__)
#  include <sys/sysinfo.h>
#  include <sys/statvfs.h>
#  include <unistd.h>
#endif

namespace changeos {
namespace info {

namespace {

std::string format_iso8601(std::chrono::system_clock::time_point ts) {
    auto t = std::chrono::system_clock::to_time_t(ts);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    return std::string(buf);
}

std::string read_first_line(const std::string& path) {
    std::ifstream in(path);
    std::string s;
    if (std::getline(in, s)) {
        // 去除行尾换行
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        return s;
    }
    return "";
}

std::string read_all(const std::string& path) {
    std::ifstream in(path);
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else out += c;
        }
    }
    return out;
}

// 读取 /proc/cpuinfo 中的 model name（仅 Linux）。
std::string cpu_model_name() {
#if defined(__linux__) || defined(__ANDROID__)
    std::ifstream in("/proc/cpuinfo");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("model name", 0) == 0) {
            auto colon = line.find(':');
            if (colon != std::string::npos) return trim(line.substr(colon + 1));
        }
    }
#endif
    return "unknown";
}

// CPU 核心数
int cpu_count() {
#if defined(__linux__) || defined(__ANDROID__)
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n > 0) return static_cast<int>(n);
#endif
    return 1;
}

// CPU 当前频率（MHz），来自 /proc/cpuinfo
std::string cpu_frequency() {
#if defined(__linux__) || defined(__ANDROID__)
    std::ifstream in("/proc/cpuinfo");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("cpu MHz", 0) == 0) {
            auto colon = line.find(':');
            if (colon != std::string::npos) {
                double mhz = 0.0;
                try { mhz = std::stod(trim(line.substr(colon + 1))); } catch (...) {}
                if (mhz > 0) {
                    std::ostringstream oss;
                    oss.precision(2);
                    oss << std::fixed << mhz << " MHz";
                    return oss.str();
                }
            }
        }
    }
#endif
    return "unknown";
}

// 内核版本（uname -r）
std::string kernel_version() {
#if defined(__linux__) || defined(__ANDROID__)
    std::ifstream in("/proc/sys/kernel/osrelease");
    std::string s;
    if (std::getline(in, s)) {
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        return s;
    }
#endif
    return platform::version();
}

// 操作系统发行版（来自 /etc/os-release 的 PRETTY_NAME）
std::string os_pretty_name() {
#if defined(__linux__) || defined(__ANDROID__)
    std::ifstream in("/etc/os-release");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("PRETTY_NAME=", 0) == 0) {
            std::string v = line.substr(12);
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
            return v;
        }
    }
#endif
    return platform::name();
}

// 系统运行时间（人类可读）
std::string uptime_human() {
#if defined(__linux__) || defined(__ANDROID__)
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        long up = si.uptime;
        long days = up / 86400;
        long hours = (up % 86400) / 3600;
        long mins = (up % 3600) / 60;
        long secs = up % 60;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%ldd %ldh %ldm %lds", days, hours, mins, secs);
        return buf;
    }
#endif
    return "unknown";
}

// 系统负载（1/5/15 分钟）
std::string loadavg_str(double& l1, double& l5, double& l15) {
    l1 = l5 = l15 = 0.0;
#if defined(__linux__) || defined(__ANDROID__)
    std::ifstream in("/proc/loadavg");
    if (in) in >> l1 >> l5 >> l15;
#endif
    std::ostringstream oss;
    oss << l1 << " " << l5 << " " << l15;
    return oss.str();
}

// 内存信息（KB -> GB 显示）
struct MemInfo {
    std::int64_t total = 0;
    std::int64_t available = 0;
    std::int64_t used = 0;
    double usage_percent = 0.0;
};

MemInfo read_meminfo() {
    MemInfo m;
#if defined(__linux__) || defined(__ANDROID__)
    std::ifstream in("/proc/meminfo");
    std::string line;
    std::int64_t mem_free = 0, buffers = 0, cached = 0;
    while (std::getline(in, line)) {
        std::istringstream iss(line);
        std::string label;
        std::int64_t val = 0;
        iss >> label >> val;
        if (label == "MemTotal:") m.total = val * 1024;
        else if (label == "MemFree:") mem_free = val * 1024;
        else if (label == "MemAvailable:") m.available = val * 1024;
        else if (label == "Buffers:") buffers = val * 1024;
        else if (label == "Cached:") cached = val * 1024;
    }
    if (m.available == 0 && m.total > 0) m.available = mem_free + buffers + cached;
    m.used = m.total - m.available;
    if (m.total > 0) m.usage_percent = (double)m.used / (double)m.total * 100.0;
#endif
    return m;
}

std::string format_bytes(std::int64_t b) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    int idx = 0;
    double v = static_cast<double>(b);
    while (v >= 1024.0 && idx < 5) { v /= 1024.0; ++idx; }
    std::ostringstream oss;
    oss.precision(2);
    oss << std::fixed << v << " " << units[idx];
    return oss.str();
}

// 磁盘信息（仅根分区及主要挂载点）
struct DiskInfo {
    std::string device;
    std::string mount;
    std::int64_t total = 0;
    std::int64_t used = 0;
    std::int64_t free_ = 0;
    double usage_percent = 0.0;
};

std::vector<DiskInfo> read_disks() {
    std::vector<DiskInfo> out;
#if defined(__linux__) || defined(__ANDROID__)
    std::vector<std::pair<std::string, std::string>> mounts;
    std::ifstream in("/proc/mounts");
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream iss(line);
        std::string device, mount, fstype, opts;
        int d1, d2;
        iss >> device >> mount >> fstype >> opts >> d1 >> d2;
        if (device.empty() || mount.empty()) continue;
        static const std::set<std::string> pseudo = {
            "proc", "sysfs", "devtmpfs", "tmpfs", "devpts", "cgroup", "cgroup2",
            "mqueue", "securityfs", "debugfs", "tracefs", "fusectl", "configfs",
            "pstore", "bpf", "hugetlbfs", "rpc_pipefs", "autofs", "binfmt_misc",
            "fuse.gvfsd-fuse", "fuse.snapfuse", "squashfs", "overlay"
        };
        if (pseudo.count(fstype)) continue;
        if (mount.find("/snap/") == 0) continue;
        mounts.emplace_back(device, mount);
    }
    for (const auto& [device, mount] : mounts) {
        struct statvfs sv{};
        if (statvfs(mount.c_str(), &sv) != 0) continue;
        DiskInfo d;
        d.device = device;
        d.mount = mount;
        d.total = (std::int64_t)sv.f_blocks * sv.f_frsize;
        d.free_ = (std::int64_t)sv.f_bavail * sv.f_frsize;
        d.used = d.total - d.free_;
        if (d.total > 0) d.usage_percent = (double)d.used / (double)d.total * 100.0;
        out.push_back(std::move(d));
    }
#endif
    return out;
}

// 监听端口数量
int listening_port_count() {
    int count = 0;
#if defined(__linux__) || defined(__ANDROID__)
    for (const char* proto : {"/proc/net/tcp", "/proc/net/tcp6"}) {
        std::ifstream in(proto);
        std::string line;
        bool first = true;
        while (std::getline(in, line)) {
            if (first) { first = false; continue; }
            // 第 4 列是 state，0A = LISTEN
            std::istringstream iss(line);
            std::string sl, local, remote, state;
            iss >> sl >> local >> remote >> state;
            if (state == "0A") ++count;
        }
    }
#endif
    return count;
}

// 进程总数
int process_count() {
    int count = 0;
#if defined(__linux__) || defined(__ANDROID__)
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("/proc", ec)) {
        if (!entry.is_directory()) continue;
        std::string name = entry.path().filename().string();
        if (!name.empty() && name.find_first_not_of("0123456789") == std::string::npos) {
            ++count;
        }
    }
#endif
    return count;
}

// ANSI 颜色
const char* C_RESET  = "\033[0m";
const char* C_BOLD   = "\033[1m";
const char* C_CYAN   = "\033[36m";
const char* C_GREEN  = "\033[32m";
const char* C_YELLOW = "\033[33m";
const char* C_RED    = "\033[31m";
const char* C_BLUE   = "\033[34m";
const char* C_MAGENTA= "\033[35m";

const char* color_for_usage(double pct) {
    if (pct >= 95.0) return C_RED;
    if (pct >= 80.0) return C_YELLOW;
    return C_GREEN;
}

void print_bar(double pct, int width = 30) {
    int filled = static_cast<int>(pct / 100.0 * width + 0.5);
    if (filled > width) filled = width;
    std::cout << "[";
    for (int i = 0; i < filled; ++i) std::cout << "#";
    for (int i = filled; i < width; ++i) std::cout << "-";
    std::cout << "] " << pct << "%";
}

} // namespace

int SystemInfo::run(const InfoOptions& opts) {
    // 采集数据
    std::string now_str = format_iso8601(std::chrono::system_clock::now());
    std::string host = platform::hostname();
    std::string user = platform::username();
    std::string plat = platform::name();
    std::string arch = platform::architecture();
    std::string os_name = os_pretty_name();
    std::string kernel = kernel_version();
    std::string cpu_model = cpu_model_name();
    int cpu_n = cpu_count();
    std::string cpu_freq = cpu_frequency();
    std::string uptime = uptime_human();
    double l1 = 0, l5 = 0, l15 = 0;
    std::string loadavg = loadavg_str(l1, l5, l15);
    MemInfo mem = read_meminfo();
    auto disks = read_disks();
    int procs = process_count();
    int ports = listening_port_count();

    if (opts.json) {
        std::cout << "{\n";
        std::cout << "  \"timestamp\": \"" << json_escape(now_str) << "\",\n";
        std::cout << "  \"host\": \"" << json_escape(host) << "\",\n";
        std::cout << "  \"user\": \"" << json_escape(user) << "\",\n";
        std::cout << "  \"platform\": \"" << json_escape(plat) << "\",\n";
        std::cout << "  \"os\": \"" << json_escape(os_name) << "\",\n";
        std::cout << "  \"kernel\": \"" << json_escape(kernel) << "\",\n";
        std::cout << "  \"architecture\": \"" << json_escape(arch) << "\",\n";
        std::cout << "  \"uptime\": \"" << json_escape(uptime) << "\",\n";
        std::cout << "  \"cpu\": {\n";
        std::cout << "    \"model\": \"" << json_escape(cpu_model) << "\",\n";
        std::cout << "    \"cores\": " << cpu_n << ",\n";
        std::cout << "    \"frequency\": \"" << json_escape(cpu_freq) << "\"\n";
        std::cout << "  },\n";
        std::cout << "  \"load\": {\n";
        std::cout << "    \"load_1min\": " << l1 << ",\n";
        std::cout << "    \"load_5min\": " << l5 << ",\n";
        std::cout << "    \"load_15min\": " << l15 << "\n";
        std::cout << "  },\n";
        std::cout << "  \"memory\": {\n";
        std::cout << "    \"total_bytes\": " << mem.total << ",\n";
        std::cout << "    \"used_bytes\": " << mem.used << ",\n";
        std::cout << "    \"available_bytes\": " << mem.available << ",\n";
        std::cout << "    \"usage_percent\": " << mem.usage_percent << "\n";
        std::cout << "  },\n";
        std::cout << "  \"disks\": [\n";
        for (size_t i = 0; i < disks.size(); ++i) {
            const auto& d = disks[i];
            std::cout << "    {\n";
            std::cout << "      \"device\": \"" << json_escape(d.device) << "\",\n";
            std::cout << "      \"mount_point\": \"" << json_escape(d.mount) << "\",\n";
            std::cout << "      \"total_bytes\": " << d.total << ",\n";
            std::cout << "      \"used_bytes\": " << d.used << ",\n";
            std::cout << "      \"free_bytes\": " << d.free_ << ",\n";
            std::cout << "      \"usage_percent\": " << d.usage_percent << "\n";
            std::cout << "    }";
            if (i + 1 < disks.size()) std::cout << ",";
            std::cout << "\n";
        }
        std::cout << "  ],\n";
        std::cout << "  \"process_count\": " << procs << ",\n";
        std::cout << "  \"listening_ports\": " << ports << "\n";
        std::cout << "}\n";
        return 0;
    }

    // 文本输出
    const char* c_title  = opts.color ? C_CYAN : "";
    const char* c_label  = opts.color ? C_BLUE : "";
    const char* c_value  = opts.color ? C_BOLD : "";
    const char* c_reset  = opts.color ? C_RESET : "";

    std::cout << c_title << "==============================================================" << c_reset << "\n";
    std::cout << c_title << "  change-of-system  系统信息概览" << c_reset << "\n";
    std::cout << c_title << "==============================================================" << c_reset << "\n";
    std::cout << c_label << "  时间:        " << c_reset << c_value << now_str << c_reset << "\n";
    std::cout << c_label << "  主机名:      " << c_reset << c_value << host << c_reset << "\n";
    std::cout << c_label << "  当前用户:    " << c_reset << c_value << user << c_reset << "\n";
    std::cout << c_label << "  操作系统:    " << c_reset << c_value << os_name << c_reset << "\n";
    std::cout << c_label << "  内核版本:    " << c_reset << c_value << kernel << c_reset << "\n";
    std::cout << c_label << "  平台/架构:   " << c_reset << c_value << plat << " / " << arch << c_reset << "\n";
    std::cout << c_label << "  运行时间:    " << c_reset << c_value << uptime << c_reset << "\n";
    std::cout << c_title << "--------------------------------------------------------------" << c_reset << "\n";
    std::cout << c_title << "  CPU" << c_reset << "\n";
    std::cout << c_label << "  型号:        " << c_reset << c_value << cpu_model << c_reset << "\n";
    std::cout << c_label << "  核心数:      " << c_reset << c_value << cpu_n << c_reset << "\n";
    std::cout << c_label << "  当前频率:    " << c_reset << c_value << cpu_freq << c_reset << "\n";
    std::cout << c_title << "--------------------------------------------------------------" << c_reset << "\n";
    std::cout << c_title << "  系统负载" << c_reset << "\n";
    std::cout << c_label << "  1/5/15 分钟: " << c_reset << c_value << loadavg << c_reset << "\n";
    std::cout << c_title << "--------------------------------------------------------------" << c_reset << "\n";
    std::cout << c_title << "  内存" << c_reset << "\n";
    std::cout << c_label << "  总量:        " << c_reset << c_value << format_bytes(mem.total) << c_reset << "\n";
    std::cout << c_label << "  已用:        " << c_reset << format_bytes(mem.used) << "  (";
    if (opts.color) std::cout << color_for_usage(mem.usage_percent);
    std::cout << format_bytes(mem.available) << " 可用)" << c_reset << "\n";
    if (opts.color) std::cout << "  " << color_for_usage(mem.usage_percent);
    print_bar(mem.usage_percent);
    std::cout << c_reset << "\n";
    std::cout << c_title << "--------------------------------------------------------------" << c_reset << "\n";
    std::cout << c_title << "  磁盘" << c_reset << "\n";
    if (disks.empty()) {
        std::cout << "  (无可用磁盘信息)\n";
    }
    for (const auto& d : disks) {
        std::cout << c_label << "  " << d.mount << c_reset << "  [" << d.device << "]\n";
        std::cout << "    总量: " << format_bytes(d.total)
                  << "  已用: " << format_bytes(d.used)
                  << "  可用: " << format_bytes(d.free_) << "\n";
        if (opts.color) std::cout << "    " << color_for_usage(d.usage_percent);
        print_bar(d.usage_percent);
        std::cout << c_reset << "\n";
    }
    std::cout << c_title << "--------------------------------------------------------------" << c_reset << "\n";
    std::cout << c_title << "  概要" << c_reset << "\n";
    std::cout << c_label << "  进程数:      " << c_reset << c_value << procs << c_reset << "\n";
    std::cout << c_label << "  监听端口数:  " << c_reset << c_value << ports << c_reset << "\n";
    std::cout << c_title << "==============================================================" << c_reset << "\n";
    return 0;
}

} // namespace info
} // namespace changeos
