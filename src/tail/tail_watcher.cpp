#include "tail/tail_watcher.h"

#include "core/monitor_engine.h"
#include "core/event.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace changeos {
namespace tail {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

bool matches_filter(const Event& e, const TailOptions& opts) {
    if (!opts.category_filter.empty()) {
        if (to_lower(category_name(e.category)) != to_lower(opts.category_filter)) {
            return false;
        }
    }
    if (!opts.source_filter.empty()) {
        if (to_lower(e.source).find(to_lower(opts.source_filter)) == std::string::npos) {
            return false;
        }
    }
    if (!opts.keyword_filter.empty()) {
        std::string needle = to_lower(opts.keyword_filter);
        if (to_lower(e.summary).find(needle) == std::string::npos &&
            to_lower(e.target).find(needle) == std::string::npos &&
            to_lower(e.source).find(needle) == std::string::npos) {
            return false;
        }
    }
    return true;
}

// 根据事件类型选择 ANSI 颜色，便于人眼快速区分严重程度。
const char* color_for_event(const Event& e) {
    switch (e.type) {
        case EventType::FileDeleted:
        case EventType::ProcessStopped:
        case EventType::NetworkConnectionClosed:
        case EventType::UsbDeviceRemoved:
        case EventType::DiskSpaceCritical:
        case EventType::SystemLoadHigh:
        case EventType::LogAnomalyDetected:
            return "\033[31m"; // 红色 - 严重/删除
        case EventType::FileCreated:
        case EventType::ProcessStarted:
        case EventType::NetworkConnectionOpened:
        case EventType::UsbDeviceInserted:
            return "\033[32m"; // 绿色 - 新增
        case EventType::FileModified:
        case EventType::FilePermissionChanged:
        case EventType::ConfigValueChanged:
        case EventType::ServiceStateChanged:
        case EventType::DiskSpaceWarning:
        case EventType::LogPatternMatched:
            return "\033[33m"; // 黄色 - 修改/警告
        case EventType::UserLoggedIn:
        case EventType::UserLoggedOut:
            return "\033[36m"; // 青色 - 用户活动
        default:
            return "\033[37m"; // 白色 - 其它
    }
}

const char* RESET = "\033[0m";

std::string format_iso8601(Timestamp ts) {
    auto t = std::chrono::system_clock::to_time_t(ts);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    return std::string(buf);
}

std::string json_escape(const std::string& s) {
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

void print_event(const Event& e, const TailOptions& opts) {
    if (opts.json) {
        // 单行 JSON，便于 jq / 管道处理
        std::string out = "{";
        out += "\"ts\":\"" + json_escape(format_iso8601(e.timestamp)) + "\",";
        out += "\"ts_unix_ms\":" + std::to_string(to_unix_ms(e.timestamp)) + ",";
        out += "\"category\":\"" + json_escape(category_name(e.category)) + "\",";
        out += "\"type\":\"" + json_escape(type_name(e.type)) + "\",";
        out += "\"source\":\"" + json_escape(e.source) + "\",";
        out += "\"target\":\"" + json_escape(e.target) + "\",";
        out += "\"summary\":\"" + json_escape(e.summary) + "\",";
        out += "\"host\":\"" + json_escape(e.host) + "\",";
        out += "\"platform\":\"" + json_escape(e.platform) + "\"";
        out += "}\n";
        std::fwrite(out.data(), 1, out.size(), stdout);
    } else if (opts.color) {
        std::string line = "[" + format_iso8601(e.timestamp) + "] ";
        const char* color = color_for_event(e);
        line += std::string(color);
        line += category_name(e.category) + " | " + type_name(e.type);
        line += RESET;
        line += " | " + e.target + " | " + e.summary;
        if (!e.source.empty()) line += "  <" + e.source + ">";
        line += "\n";
        std::fwrite(line.data(), 1, line.size(), stdout);
    } else {
        std::string line = "[" + format_iso8601(e.timestamp) + "] ";
        line += category_name(e.category) + " | " + type_name(e.type);
        line += " | " + e.target + " | " + e.summary;
        if (!e.source.empty()) line += "  <" + e.source + ">";
        line += "\n";
        std::fwrite(line.data(), 1, line.size(), stdout);
    }
    std::fflush(stdout);
}

} // namespace

int TailWatcher::run(MonitorEngine& engine, const TailOptions& opts,
                     std::atomic<bool>& running) {
    // 先输出启动横幅
    if (!opts.json) {
        std::cout << "=== change-of-system 实时事件流 ===\n";
        if (!opts.category_filter.empty())
            std::cout << "类别过滤: " << opts.category_filter << "\n";
        if (!opts.source_filter.empty())
            std::cout << "来源过滤: " << opts.source_filter << "\n";
        if (!opts.keyword_filter.empty())
            std::cout << "关键字过滤: " << opts.keyword_filter << "\n";
        std::cout << "按 Ctrl+C 退出...\n";
    }

    // 可选：先打印最近 N 条历史事件
    if (opts.initial_recent > 0) {
        auto recent = engine.recent_events(opts.initial_recent);
        for (const auto& e : recent) {
            if (matches_filter(e, opts)) print_event(e, opts);
        }
    }

    // 注册事件回调。回调在 monitor 线程中执行，因此只需做过滤与打印。
    std::mutex print_mutex;
    engine.on_event([&](const Event& e) {
        if (!matches_filter(e, opts)) return;
        std::lock_guard<std::mutex> lock(print_mutex);
        print_event(e, opts);
    });

    // 阻塞直到主线程收到退出信号
    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (!opts.json) {
        std::cout << "\n退出实时事件流...\n";
    }
    return 0;
}

} // namespace tail
} // namespace changeos
