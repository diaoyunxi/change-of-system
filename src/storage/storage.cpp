#include "storage/storage.h"

#include "utils/logger.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>
#include <vector>

namespace changeos {
namespace storage {

namespace {

std::string serialize_event(const Event& event) {
    std::ostringstream oss;
    oss << to_unix_ms(event.timestamp) << "\t"
        << category_name(event.category) << "\t"
        << type_name(event.type) << "\t"
        << event.source << "\t"
        << event.target << "\t"
        << event.host << "\t"
        << event.platform << "\t";

    std::string attrs_line;
    for (auto it = event.attributes.begin(); it != event.attributes.end(); ++it) {
        if (!attrs_line.empty()) attrs_line += "||";
        attrs_line += it->first + "=" + it->second;
    }

    std::string summary = event.summary;
    for (auto& c : attrs_line) if (c == '\n') c = ' ';
    for (auto& c : summary) if (c == '\n') c = ' ';

    oss << attrs_line << "\t" << summary;
    return oss.str();
}

Event parse_event(const std::string& line) {
    Event ev;
    std::vector<std::string> tokens;
    std::string token;
    for (char c : line) {
        if (c == '\t') {
            tokens.push_back(token);
            token.clear();
        } else {
            token += c;
        }
    }
    tokens.push_back(token);

    if (tokens.size() < 9) return ev;
    try {
        std::int64_t ms = std::stoll(tokens[0]);
        ev.timestamp = std::chrono::system_clock::time_point(
            std::chrono::milliseconds(ms));
    } catch (...) {}
    ev.source = tokens[3];
    ev.target = tokens[4];
    ev.host = tokens[5];
    ev.platform = tokens[6];

    std::string attrs = tokens[7];
    std::string cur;
    std::string key;
    for (size_t i = 0; i < attrs.size(); ++i) {
        if (attrs[i] == '|' && i + 1 < attrs.size() && attrs[i + 1] == '|') {
            if (!key.empty()) {
                ev.attributes[key] = cur;
                key.clear();
                cur.clear();
            }
            ++i;
            continue;
        }
        if (key.empty() && attrs[i] == '=') {
            key = cur;
            cur.clear();
        } else {
            cur += attrs[i];
        }
    }
    if (!key.empty()) ev.attributes[key] = cur;

    ev.summary = tokens[8];
    return ev;
}

class FileStorage : public Storage {
public:
    ~FileStorage() override { close(); }

    bool open(const std::string& connection_string) override {
        std::lock_guard<std::mutex> lock(mutex_);
        path_ = connection_string;
        file_.open(path_, std::ios::app);
        return file_.is_open();
    }

    void close() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) file_.close();
    }

    bool is_open() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return file_.is_open();
    }

    std::int64_t count() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ifstream in(path_);
        std::int64_t c = 0;
        std::string line;
        while (std::getline(in, line)) if (!line.empty()) ++c;
        return c;
    }

    void insert(const Event& event) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!file_.is_open()) return;
        file_ << serialize_event(event) << "\n";
        file_.flush();
    }

    std::vector<Event> query(const QueryFilter& filter) const override {
        std::vector<Event> result;
        std::lock_guard<std::mutex> lock(mutex_);
        std::ifstream in(path_);
        if (!in) return result;
        std::string line;
        std::int64_t seen = 0;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            Event ev = parse_event(line);
            auto ms = to_unix_ms(ev.timestamp);
            if (filter.from_unix_ms > 0 && ms < filter.from_unix_ms) continue;
            if (filter.to_unix_ms > 0 && ms > filter.to_unix_ms) continue;
            if (!filter.keyword.empty()
                && ev.summary.find(filter.keyword) == std::string::npos
                && ev.target.find(filter.keyword) == std::string::npos) {
                continue;
            }
            if (seen++ < static_cast<std::int64_t>(filter.offset)) continue;
            result.push_back(ev);
            if (static_cast<int>(result.size()) >= filter.limit) break;
        }
        return result;
    }

    void iterate(std::function<void(const Event&)> cb) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ifstream in(path_);
        if (!in) return;
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            cb(parse_event(line));
        }
    }

    void prune(std::int64_t max_events) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ifstream in(path_);
        if (!in) return;
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(in, line)) if (!line.empty()) lines.push_back(line);
        if (static_cast<std::int64_t>(lines.size()) <= max_events) return;
        std::vector<std::string> keep(lines.end() - max_events, lines.end());
        in.close();
        std::ofstream out(path_, std::ios::trunc);
        for (auto& l : keep) out << l << "\n";
    }

private:
    mutable std::mutex mutex_;
    std::string path_;
    std::ofstream file_;
};

} // namespace

std::unique_ptr<Storage> create_sqlite_storage() {
    return std::make_unique<FileStorage>();
}

} // namespace storage
} // namespace changeos
