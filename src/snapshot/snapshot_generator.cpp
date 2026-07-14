#include "snapshot/snapshot_generator.h"

#include "platform/platform_detection.h"
#include "utils/logger.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#if defined(COS_PLATFORM_LINUX) || defined(COS_PLATFORM_UNIX) || defined(COS_PLATFORM_ANDROID) || defined(COS_PLATFORM_CHROMEOS) || defined(COS_PLATFORM_FYDEOS)
#include <sys/statvfs.h>
#include <unistd.h>
#endif

#if defined(COS_PLATFORM_LINUX) || defined(COS_PLATFORM_ANDROID) || defined(COS_PLATFORM_CHROMEOS) || defined(COS_PLATFORM_FYDEOS)
#include <sys/sysinfo.h>
#endif

namespace changeos {
namespace snapshot {

namespace {

std::string format_iso8601(Timestamp ts) {
    auto t = std::chrono::system_clock::to_time_t(ts);
    char buf[32];
    // 使用线程安全的 gmtime_r（POSIX）或 gmtime_s（Windows）替代 std::gmtime
    struct tm gm_buf;
#if defined(_WIN32) || defined(_MSC_VER)
    gmtime_s(&gm_buf, &t);
#else
    gmtime_r(&t, &gm_buf);
#endif
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &gm_buf);
    return std::string(buf);
}

std::int64_t uptime_seconds() {
#if defined(COS_PLATFORM_LINUX) || defined(COS_PLATFORM_ANDROID) || defined(COS_PLATFORM_CHROMEOS) || defined(COS_PLATFORM_FYDEOS)
    struct sysinfo si;
    if (sysinfo(&si) == 0) return static_cast<std::int64_t>(si.uptime);
#endif
    std::ifstream in("/proc/uptime");
    double up = 0.0, idle = 0.0;
    if (in >> up >> idle) return static_cast<std::int64_t>(up);
    return 0;
}

// Parse a /proc/net/* file: skips header, returns rows of whitespace-separated tokens.
std::vector<std::vector<std::string>> read_proc_net(const std::string& path) {
    std::vector<std::vector<std::string>> rows;
    std::ifstream in(path);
    if (!in) return rows;
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (first) { first = false; continue; } // skip header
        std::istringstream iss(line);
        std::vector<std::string> tokens;
        std::string tok;
        while (iss >> tok) tokens.push_back(tok);
        if (!tokens.empty()) rows.push_back(std::move(tokens));
    }
    return rows;
}

std::string hex_ip_to_string(const std::string& hex) {
    if (hex.size() != 8) return hex;
    unsigned int val = 0;
    try { val = static_cast<unsigned int>(std::stoul(hex, nullptr, 16)); }
    catch (...) { return hex; }
    // /proc/net stores IPv4 as little-endian hex
    unsigned char b[4];
    b[0] = val & 0xFF;
    b[1] = (val >> 8) & 0xFF;
    b[2] = (val >> 16) & 0xFF;
    b[3] = (val >> 24) & 0xFF;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    return std::string(buf);
}

std::string hex_port_to_string(const std::string& hex) {
    try {
        unsigned int port = static_cast<unsigned int>(std::stoul(hex, nullptr, 16));
        return std::to_string(port);
    } catch (...) { return hex; }
}

} // namespace

std::string SnapshotGenerator::json_escape(const std::string& s) {
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

std::string SnapshotGenerator::capture_metadata() {
    std::ostringstream oss;
    oss << "    \"timestamp\": \"" << format_iso8601(now()) << "\",\n";
    oss << "    \"timestamp_unix_ms\": " << to_unix_ms(now()) << ",\n";
    oss << "    \"host\": \"" << json_escape(platform::hostname()) << "\",\n";
    oss << "    \"platform\": \"" << json_escape(platform::name()) << "\",\n";
    oss << "    \"platform_version\": \"" << json_escape(platform::version()) << "\",\n";
    oss << "    \"architecture\": \"" << json_escape(platform::architecture()) << "\",\n";
    oss << "    \"user\": \"" << json_escape(platform::username()) << "\",\n";
    oss << "    \"uptime_seconds\": " << uptime_seconds();
    return oss.str();
}

std::string SnapshotGenerator::capture_system_load() {
    std::ostringstream oss;
    oss << "      \"system\": {\n";

    double load1 = 0.0, load5 = 0.0, load15 = 0.0;
    std::int64_t mem_total = 0, mem_available = 0;
    std::int64_t swap_total = 0, swap_free = 0;

#if defined(COS_PLATFORM_LINUX) || defined(COS_PLATFORM_ANDROID) || defined(COS_PLATFORM_CHROMEOS) || defined(COS_PLATFORM_FYDEOS)
    {
        std::ifstream la("/proc/loadavg");
        if (la) la >> load1 >> load5 >> load15;
    }
    {
        std::ifstream mi("/proc/meminfo");
        std::string line;
        std::int64_t mem_free = 0, buffers = 0, cached = 0;
        while (std::getline(mi, line)) {
            std::istringstream iss(line);
            std::string label;
            std::int64_t val = 0;
            iss >> label >> val;
            if (label == "MemTotal:") mem_total = val * 1024;
            else if (label == "MemFree:") mem_free = val * 1024;
            else if (label == "MemAvailable:") mem_available = val * 1024;
            else if (label == "Buffers:") buffers = val * 1024;
            else if (label == "Cached:") cached = val * 1024;
            else if (label == "SwapTotal:") swap_total = val * 1024;
            else if (label == "SwapFree:") swap_free = val * 1024;
        }
        if (mem_available == 0 && mem_total > 0) {
            mem_available = mem_free + buffers + cached;
        }
    }
#endif

    double mem_usage = 0.0;
    if (mem_total > 0) {
        mem_usage = (static_cast<double>(mem_total - mem_available) /
                     static_cast<double>(mem_total)) * 100.0;
    }
    double swap_usage = 0.0;
    std::int64_t swap_used = swap_total - swap_free;
    if (swap_total > 0) {
        swap_usage = (static_cast<double>(swap_used) / static_cast<double>(swap_total)) * 100.0;
    }

    oss << "        \"load_1min\": " << load1 << ",\n";
    oss << "        \"load_5min\": " << load5 << ",\n";
    oss << "        \"load_15min\": " << load15 << ",\n";
    oss << "        \"memory_total_bytes\": " << mem_total << ",\n";
    oss << "        \"memory_available_bytes\": " << mem_available << ",\n";
    oss << "        \"memory_used_bytes\": " << (mem_total - mem_available) << ",\n";
    oss << "        \"memory_usage_percent\": " << mem_usage << ",\n";
    oss << "        \"swap_total_bytes\": " << swap_total << ",\n";
    oss << "        \"swap_used_bytes\": " << swap_used << ",\n";
    oss << "        \"swap_usage_percent\": " << swap_usage << "\n";
    oss << "      }";
    return oss.str();
}

std::string SnapshotGenerator::capture_disk_usage() {
    std::ostringstream oss;
    oss << "      \"disks\": [\n";

#if defined(COS_PLATFORM_LINUX) || defined(COS_PLATFORM_UNIX) || defined(COS_PLATFORM_ANDROID) || defined(COS_PLATFORM_CHROMEOS) || defined(COS_PLATFORM_FYDEOS)
    // Parse /proc/mounts to get real mount points (filter pseudo filesystems)
    std::vector<std::pair<std::string, std::string>> mounts;
    {
        std::ifstream in("/proc/mounts");
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream iss(line);
            std::string device, mount, fstype, opts;
            int d1, d2;
            iss >> device >> mount >> fstype >> opts >> d1 >> d2;
            if (device.empty() || mount.empty()) continue;
            // Skip pseudo filesystems
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
    }

    bool first = true;
    for (const auto& [device, mount] : mounts) {
        struct statvfs sv{};
        if (statvfs(mount.c_str(), &sv) != 0) continue;
        std::int64_t total = static_cast<std::int64_t>(sv.f_blocks) * sv.f_frsize;
        std::int64_t free_ = static_cast<std::int64_t>(sv.f_bavail) * sv.f_frsize;
        std::int64_t used = total - free_;
        double usage = 0.0;
        if (total > 0) usage = (static_cast<double>(used) / static_cast<double>(total)) * 100.0;
        if (!first) oss << ",\n";
        first = false;
        oss << "        {\n";
        oss << "          \"device\": \"" << json_escape(device) << "\",\n";
        oss << "          \"mount_point\": \"" << json_escape(mount) << "\",\n";
        oss << "          \"total_bytes\": " << total << ",\n";
        oss << "          \"used_bytes\": " << used << ",\n";
        oss << "          \"free_bytes\": " << free_ << ",\n";
        oss << "          \"usage_percent\": " << usage << "\n";
        oss << "        }";
    }
#endif
    oss << "\n      ]";
    return oss.str();
}

std::string SnapshotGenerator::capture_processes(int max_count) {
    std::ostringstream oss;
    oss << "      \"processes\": [\n";

    struct ProcEntry {
        std::int64_t pid = 0;
        std::string name;
        std::string user;
        std::int64_t memory_bytes = 0;
        std::int64_t ppid = 0;
        std::string command_line;
    };

    std::vector<ProcEntry> procs;

#if defined(COS_PLATFORM_LINUX) || defined(COS_PLATFORM_ANDROID) || defined(COS_PLATFORM_CHROMEOS) || defined(COS_PLATFORM_FYDEOS)
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("/proc", ec)) {
        if (!entry.is_directory()) continue;
        std::string name = entry.path().filename().string();
        if (name.empty() || name.find_first_not_of("0123456789") != std::string::npos) continue;
        ProcEntry p;
        try { p.pid = std::stoll(name); } catch (...) { continue; }

        std::ifstream status(entry.path() / "status");
        if (status) {
            std::string line;
            while (std::getline(status, line)) {
                if (line.rfind("Name:", 0) == 0) {
                    p.name = line.substr(5);
                    while (!p.name.empty() && (p.name.front() == '\t' || p.name.front() == ' '))
                        p.name.erase(p.name.begin());
                } else if (line.rfind("PPid:", 0) == 0) {
                    try { p.ppid = std::stoll(line.substr(5)); } catch (...) {}
                } else if (line.rfind("Uid:", 0) == 0) {
                    // First UID is real UID
                    std::istringstream iss(line.substr(4));
                    int uid = -1;
                    iss >> uid;
                    p.user = "uid=" + std::to_string(uid);
                } else if (line.rfind("VmRSS:", 0) == 0) {
                    std::istringstream iss(line.substr(6));
                    long long kb = 0;
                    iss >> kb;
                    p.memory_bytes = kb * 1024;
                }
            }
        }

        std::ifstream cmdline(entry.path() / "cmdline");
        if (cmdline) {
            std::ostringstream cs;
            char c;
            while (cmdline.get(c)) cs << (c == '\0' ? ' ' : c);
            p.command_line = cs.str();
            while (!p.command_line.empty() && p.command_line.back() == ' ')
                p.command_line.pop_back();
        }
        if (p.name.empty() && p.command_line.empty()) continue;
        procs.push_back(std::move(p));
    }
#endif

    // Sort by memory descending, take top N
    std::sort(procs.begin(), procs.end(),
              [](const ProcEntry& a, const ProcEntry& b) {
                  return a.memory_bytes > b.memory_bytes;
              });
    if (max_count > 0 && static_cast<int>(procs.size()) > max_count) {
        procs.resize(max_count);
    }

    bool first = true;
    for (const auto& p : procs) {
        if (!first) oss << ",\n";
        first = false;
        oss << "        {\n";
        oss << "          \"pid\": " << p.pid << ",\n";
        oss << "          \"ppid\": " << p.ppid << ",\n";
        oss << "          \"name\": \"" << json_escape(p.name) << "\",\n";
        oss << "          \"user\": \"" << json_escape(p.user) << "\",\n";
        oss << "          \"memory_bytes\": " << p.memory_bytes << ",\n";
        oss << "          \"command_line\": \"" << json_escape(p.command_line) << "\"\n";
        oss << "        }";
    }
    oss << "\n      ]";
    return oss.str();
}

std::string SnapshotGenerator::capture_network() {
    std::ostringstream oss;
    oss << "      \"connections\": [\n";

    bool first = true;
    for (const char* proto_file : {"/proc/net/tcp", "/proc/net/udp", "/proc/net/tcp6", "/proc/net/udp6"}) {
        auto rows = read_proc_net(proto_file);
        std::string proto = proto_file;
        for (const auto& tok : rows) {
            if (tok.size() < 4) continue;
            // Columns: sl local_address local_state remote_address ...
            std::string local = tok.size() > 1 ? tok[1] : "";
            std::string state = tok.size() > 3 ? tok[3] : "";
            std::string remote = tok.size() > 2 ? tok[2] : "";
            // local format "0100007F:1F90"
            auto colon = local.find(':');
            if (colon == std::string::npos) continue;
            std::string local_ip = hex_ip_to_string(local.substr(0, colon));
            std::string local_port = hex_port_to_string(local.substr(colon + 1));
            auto rcolon = remote.find(':');
            std::string remote_ip = (rcolon != std::string::npos)
                ? hex_ip_to_string(remote.substr(0, rcolon)) : "";
            std::string remote_port = (rcolon != std::string::npos)
                ? hex_port_to_string(remote.substr(rcolon + 1)) : "";
            if (!first) oss << ",\n";
            first = false;
            oss << "        {\n";
            oss << "          \"protocol\": \"" << json_escape(proto.substr(10)) << "\",\n";
            oss << "          \"local_address\": \"" << local_ip << ":" << local_port << "\",\n";
            oss << "          \"remote_address\": \"" << remote_ip << ":" << remote_port << "\",\n";
            oss << "          \"state\": \"" << json_escape(state) << "\"\n";
            oss << "        }";
        }
    }
    oss << "\n      ]";
    return oss.str();
}

std::string SnapshotGenerator::capture_ports() {
    std::ostringstream oss;
    oss << "      \"listening_ports\": [\n";

    bool first = true;
    for (const char* proto_file : {"/proc/net/tcp", "/proc/net/tcp6", "/proc/net/udp", "/proc/net/udp6"}) {
        auto rows = read_proc_net(proto_file);
        std::string proto = proto_file;
        for (const auto& tok : rows) {
            if (tok.size() < 4) continue;
            std::string state = tok[3];
            // TCP state 0A = LISTEN; UDP is connectionless so state 07 = "unconnected" (listen)
            bool is_listen = false;
            if (std::string(proto_file).find("tcp") != std::string::npos) {
                is_listen = (state == "0A");
            } else {
                // For UDP, a remote of 00000000:0000 means listening
                std::string remote = tok.size() > 2 ? tok[2] : "";
                is_listen = (remote == "00000000:0000" || remote.find(":0000") != std::string::npos);
            }
            if (!is_listen) continue;
            std::string local = tok[1];
            auto colon = local.find(':');
            if (colon == std::string::npos) continue;
            std::string local_ip = hex_ip_to_string(local.substr(0, colon));
            std::string local_port = hex_port_to_string(local.substr(colon + 1));
            if (!first) oss << ",\n";
            first = false;
            oss << "        {\n";
            oss << "          \"protocol\": \"" << json_escape(proto.substr(10)) << "\",\n";
            oss << "          \"address\": \"" << local_ip << "\",\n";
            oss << "          \"port\": " << local_port << "\n";
            oss << "        }";
        }
    }
    oss << "\n      ]";
    return oss.str();
}

std::string SnapshotGenerator::capture_environment() {
    std::ostringstream oss;
    oss << "      \"environment\": {\n";
    // 捕获一组常用的环境变量
    static const char* vars[] = {
        "PATH", "HOME", "USER", "LOGNAME", "SHELL", "TERM", "LANG", "LC_ALL",
        "HOSTNAME", "PWD", "LD_LIBRARY_PATH", "LD_PRELOAD"
    };
    // 敏感环境变量名（其值需要脱敏，防止泄露库注入路径等敏感信息）
    static const std::set<std::string> sensitive_vars = {
        "LD_PRELOAD", "LD_LIBRARY_PATH"
    };
    bool first = true;
    for (const char* v : vars) {
        const char* val = std::getenv(v);
        if (!val) continue;
        std::string value(val);
        // 对敏感环境变量值做脱敏：仅保留前8个字符 + *** 标记
        if (sensitive_vars.count(v) && value.length() > 8) {
            value = value.substr(0, 8) + "***";
        }
        if (!first) oss << ",\n";
        first = false;
        oss << "        \"" << v << "\": \"" << json_escape(value) << "\"";
    }
    oss << "\n      }";
    return oss.str();
}

bool SnapshotGenerator::generate(const SnapshotConfig& config) {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"snapshot\": {\n";
    oss << capture_metadata() << ",\n";
    oss << "    \"sections\": {\n";

    std::vector<std::string> sections;
    if (config.include_load)         sections.push_back(capture_system_load());
    if (config.include_disk)         sections.push_back(capture_disk_usage());
    if (config.include_processes)    sections.push_back(capture_processes(config.max_processes));
    if (config.include_network)      sections.push_back(capture_network());
    if (config.include_ports)        sections.push_back(capture_ports());
    if (config.include_environment)  sections.push_back(capture_environment());

    for (size_t i = 0; i < sections.size(); ++i) {
        oss << sections[i];
        if (i + 1 < sections.size()) oss << ",";
        oss << "\n";
    }

    oss << "    }\n";
    oss << "  }\n";
    oss << "}\n";

    const std::string& out = oss.str();
    if (config.output_path.empty()) {
        std::fwrite(out.data(), 1, out.size(), stdout);
        return true;
    }
    std::ofstream f(config.output_path, std::ios::trunc);
    if (!f) {
        COS_LOG_ERROR("Failed to open snapshot output file: " + config.output_path);
        return false;
    }
    f << out;
    if (!f) {
        COS_LOG_ERROR("Failed to write snapshot: " + config.output_path);
        return false;
    }
    COS_LOG_INFO("Snapshot written to " + config.output_path);
    return true;
}

} // namespace snapshot
} // namespace changeos
