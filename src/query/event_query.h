#pragma once

#include "core/event.h"
#include "storage/storage.h"

#include <cstdint>
#include <string>

namespace changeos {
namespace query {

struct QueryOptions {
    std::string keyword;            // Match against summary or target (empty = any)
    std::string category;           // Category name filter (empty = any)
    std::string source;             // Source substring filter (empty = any)
    std::int64_t from_unix_ms = 0;  // Inclusive lower time bound (0 = no bound)
    std::int64_t to_unix_ms = 0;    // Inclusive upper time bound (0 = no bound)
    int limit = 50;                 // Max events to print (0 = unlimited, capped)
    int offset = 0;                 // Skip first N matches
    bool json_output = false;       // true = JSON, false = human-readable
};

class EventQuery {
public:
    // Run a query against the given storage backend and print results to stdout.
    // Returns the number of matching events printed.
    static int run(storage::Storage* storage, const QueryOptions& opts);

    // Parse a category name (case-insensitive) into an EventCategory, or
    // return EventCategory::Unknown if it does not match any known category.
    static EventCategory parse_category(const std::string& name);
};

} // namespace query
} // namespace changeos
