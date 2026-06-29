#pragma once

#include "core/event.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace changeos {
namespace storage {

struct QueryFilter {
    std::int64_t from_unix_ms = 0;
    std::int64_t to_unix_ms = 0;
    std::string category;
    std::string source;
    std::string keyword;
    int limit = 100;
    int offset = 0;
};

class Storage {
public:
    virtual ~Storage() = default;

    virtual bool open(const std::string& connection_string) = 0;
    virtual void close() = 0;
    virtual bool is_open() const = 0;

    virtual std::int64_t count() const = 0;
    virtual void insert(const Event& event) = 0;
    virtual void insert_batch(const std::vector<Event>& events) {
        for (const auto& e : events) insert(e);
    }
    virtual std::vector<Event> query(const QueryFilter& filter) const = 0;
    virtual void iterate(std::function<void(const Event&)> cb) const = 0;
    virtual void prune(std::int64_t max_events) = 0;

    // Configure automatic rotation of the underlying storage file. When the
    // current file exceeds max_bytes, it is renamed to <path>.1, older backups
    // are shifted, and a fresh file is opened. Up to max_files backups are kept.
    // Backends that do not support rotation treat this as a no-op.
    virtual void configure_rotation(std::int64_t max_bytes, int max_files) {
        (void)max_bytes;
        (void)max_files;
    }
};

std::unique_ptr<Storage> create_sqlite_storage();

} // namespace storage
} // namespace changeos
