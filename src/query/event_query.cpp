#include "query/event_query.h"

#include "utils/logger.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <sstream>
#include <string>
#include <vector>

namespace changeos {
namespace query {

namespace {

std::string to_lower(const std::string& s) {
    std::string out = s;
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string format_ts(Timestamp ts) {
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

bool contains_ci(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    return to_lower(haystack).find(to_lower(needle)) != std::string::npos;
}

} // namespace

EventCategory EventQuery::parse_category(const std::string& name) {
    std::string n = to_lower(name);
    if (n == "filesystem")    return EventCategory::Filesystem;
    if (n == "process")       return EventCategory::Process;
    if (n == "network")       return EventCategory::Network;
    if (n == "system_config" || n == "systemconfig") return EventCategory::SystemConfig;
    if (n == "hardware")      return EventCategory::Hardware;
    if (n == "system")        return EventCategory::System;
    return EventCategory::Unknown;
}

int EventQuery::run(storage::Storage* storage, const QueryOptions& opts) {
    if (!storage || !storage->is_open()) {
        std::fprintf(stderr, "Storage backend is not open; cannot query.\n");
        return 0;
    }

    // We use iterate() so we can apply category/source/keyword filters
    // consistently regardless of backend capabilities.
    std::vector<Event> matches;
    EventCategory cat_filter = parse_category(opts.category);

    storage->iterate([&](const Event& e) {
        auto ms = to_unix_ms(e.timestamp);
        if (opts.from_unix_ms > 0 && ms < opts.from_unix_ms) return;
        if (opts.to_unix_ms > 0 && ms > opts.to_unix_ms) return;
        if (cat_filter != EventCategory::Unknown && e.category != cat_filter) return;
        if (!opts.source.empty() && !contains_ci(e.source, opts.source)) return;
        if (!opts.keyword.empty()) {
            if (!contains_ci(e.summary, opts.keyword) &&
                !contains_ci(e.target, opts.keyword) &&
                !contains_ci(e.source, opts.keyword)) {
                return;
            }
        }
        matches.push_back(e);
    });

    // Most recent first
    std::sort(matches.begin(), matches.end(),
              [](const Event& a, const Event& b) {
                  return a.timestamp > b.timestamp;
              });

    int offset = std::max(0, opts.offset);
    int limit = opts.limit > 0 ? opts.limit : static_cast<int>(matches.size());
    if (offset >= static_cast<int>(matches.size())) {
        if (!opts.json_output) {
            std::printf("Query returned 0 events (offset=%d, total_matches=%zu)\n",
                        offset, matches.size());
        }
        return 0;
    }

    int end = std::min(offset + limit, static_cast<int>(matches.size()));
    int printed = 0;

    if (opts.json_output) {
        std::printf("[\n");
        for (int i = offset; i < end; ++i) {
            const Event& e = matches[i];
            std::printf("  {\n");
            std::printf("    \"timestamp\": \"%s\",\n", format_ts(e.timestamp).c_str());
            std::printf("    \"timestamp_unix_ms\": %lld,\n",
                        static_cast<long long>(to_unix_ms(e.timestamp)));
            std::printf("    \"category\": \"%s\",\n", category_name(e.category).c_str());
            std::printf("    \"type\": \"%s\",\n", type_name(e.type).c_str());
            std::printf("    \"source\": \"%s\",\n", json_escape(e.source).c_str());
            std::printf("    \"target\": \"%s\",\n", json_escape(e.target).c_str());
            std::printf("    \"host\": \"%s\",\n", json_escape(e.host).c_str());
            std::printf("    \"platform\": \"%s\",\n", json_escape(e.platform).c_str());
            std::printf("    \"summary\": \"%s\",\n", json_escape(e.summary).c_str());
            std::printf("    \"attributes\": {");
            bool first = true;
            for (const auto& [k, v] : e.attributes) {
                if (!first) std::printf(", ");
                first = false;
                std::printf("\"%s\": \"%s\"",
                            json_escape(k).c_str(),
                            json_escape(v).c_str());
            }
            std::printf("}\n");
            std::printf("  }%s\n", (i + 1 < end) ? "," : "");
        }
        std::printf("]\n");
    } else {
        std::printf("=== Event query results (showing %d of %zu matches) ===\n",
                    end - offset, matches.size());
        for (int i = offset; i < end; ++i) {
            const Event& e = matches[i];
            std::printf("[%s] %s/%s src=%s target=%s\n",
                        format_ts(e.timestamp).c_str(),
                        category_name(e.category).c_str(),
                        type_name(e.type).c_str(),
                        e.source.c_str(),
                        e.target.c_str());
            if (!e.summary.empty()) {
                std::printf("    %s\n", e.summary.c_str());
            }
            ++printed;
        }
    }

    COS_LOG_INFO("Query matched " + std::to_string(matches.size()) +
                 " events, printed " + std::to_string(end - offset));
    return printed;
}

} // namespace query
} // namespace changeos
