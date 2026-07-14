#include "storage/storage.h"

#include "utils/logger.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
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
    ev.category = category_from_name(tokens[1]);
    ev.type = type_from_name(tokens[2]);
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
        if (file_.is_open()) {
            // 从现有文件大小初始化字节计数器，使轮转在重启后正常工作
            std::error_code ec;
            auto sz = std::filesystem::file_size(path_, ec);
            bytes_written_ = ec ? 0 : static_cast<std::int64_t>(sz);
            // 从现有文件初始化事件计数器，避免每次 count() 都遍历文件
            event_count_ = 0;
            std::ifstream in(path_);
            std::string line;
            while (std::getline(in, line)) if (!line.empty()) ++event_count_;
        }
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

    void configure_rotation(std::int64_t max_bytes, int max_files) override {
        std::lock_guard<std::mutex> lock(mutex_);
        rotate_max_bytes_ = max_bytes > 0 ? max_bytes : 0;
        rotate_max_files_ = max_files > 0 ? max_files : 0;
    }

    std::int64_t count() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        // 使用内存计数器，避免每次调用都遍历整个文件
        return event_count_;
    }

    void insert(const Event& event) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!file_.is_open()) return;
        std::string line = serialize_event(event);
        file_ << line << "\n";
        file_.flush();
        bytes_written_ += static_cast<std::int64_t>(line.size()) + 1;
        ++event_count_;  // 维护内存事件计数器
        maybe_rotate_locked();
    }

    // 注意：当前 query() 采用全文件线性扫描，无时间戳索引。
    // 对于大规模数据场景，建议使用 SQLite 存储后端或添加时间戳索引文件以提升查询性能。
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
        // 使用内存计数器判断是否需要裁剪，避免不必要的文件遍历
        if (event_count_ <= max_events) return;

        std::int64_t skip = event_count_ - max_events;

        // 使用临时文件流式写入保留的事件，避免全量加载到内存
        std::string tmp_path = path_ + ".prune.tmp";
        std::ifstream in(path_);
        if (!in) return;
        std::ofstream out(tmp_path, std::ios::trunc);
        if (!out) {
            in.close();
            return;
        }

        std::int64_t current = 0;
        std::int64_t new_bytes = 0;
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            if (current++ < skip) continue;  // 跳过旧事件
            out << line << "\n";
            new_bytes += static_cast<std::int64_t>(line.size()) + 1;
        }
        in.close();
        out.close();

        // 关闭当前写入文件，原子性替换原文件
        if (file_.is_open()) file_.close();
        std::error_code ec;
        std::filesystem::rename(tmp_path, path_, ec);
        if (ec) {
            // 替换失败，清理临时文件并重新打开原文件
            std::filesystem::remove(tmp_path, ec);
            file_.open(path_, std::ios::app);
            return;
        }
        file_.open(path_, std::ios::app);
        bytes_written_ = new_bytes;
        event_count_ = max_events;
    }

private:
    // Caller must hold mutex_.
    void maybe_rotate_locked() {
        if (rotate_max_bytes_ <= 0 || rotate_max_files_ <= 0) return;
        if (bytes_written_ < rotate_max_bytes_) return;
        if (path_.empty()) return;

        // Close the active file before renaming.
        if (file_.is_open()) file_.close();

        std::error_code ec;
        // Delete the oldest backup, shift the rest up.
        std::string oldest = path_ + "." + std::to_string(rotate_max_files_);
        std::filesystem::remove(oldest, ec);

        for (int i = rotate_max_files_ - 1; i >= 1; --i) {
            std::string src = path_ + "." + std::to_string(i);
            std::string dst = path_ + "." + std::to_string(i + 1);
            std::error_code rec;
            if (std::filesystem::exists(src, rec)) {
                std::filesystem::rename(src, dst, rec);
            }
        }

        // Move the current file to .1
        std::string first_backup = path_ + ".1";
        std::filesystem::rename(path_, first_backup, ec);

        // Open a fresh file.
        file_.open(path_, std::ios::app);
        bytes_written_ = 0;
        event_count_ = 0;  // 轮转后新文件为空，重置事件计数器
        if (!file_.is_open()) {
            COS_LOG_ERROR("Failed to reopen storage file after rotation: " + path_);
        } else {
            COS_LOG_INFO("Rotated storage file (max_bytes=" +
                         std::to_string(rotate_max_bytes_) +
                         ", backups=" + std::to_string(rotate_max_files_) + ")");
        }
    }

    mutable std::mutex mutex_;
    std::string path_;
    std::ofstream file_;
    std::int64_t bytes_written_ = 0;
    std::int64_t event_count_ = 0;  // 内存事件计数器，避免 count() 遍历整个文件
    std::int64_t rotate_max_bytes_ = 0;
    int rotate_max_files_ = 0;
};

} // namespace

std::unique_ptr<Storage> create_default_storage() {
    return std::make_unique<FileStorage>();
}

} // namespace storage
} // namespace changeos
