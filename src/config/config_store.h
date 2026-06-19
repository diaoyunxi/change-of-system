#pragma once

#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace changeos {
namespace config {

class ConfigStore {
public:
    static ConfigStore& instance() {
        static ConfigStore store;
        return store;
    }

    void load(const std::string& path);
    void save(const std::string& path) const;

    std::string get(const std::string& key,
                    const std::string& default_value = {}) const;
    int get_int(const std::string& key, int default_value = 0) const;
    bool get_bool(const std::string& key, bool default_value = false) const;
    std::vector<std::string> get_list(const std::string& key,
        const std::vector<std::string>& default_value = {}) const;

    void set(const std::string& key, const std::string& value);
    void set_int(const std::string& key, int value);
    void set_bool(const std::string& key, bool value);
    void set_list(const std::string& key,
                  const std::vector<std::string>& value);

    bool has(const std::string& key) const;

    std::string file_path() const { return file_path_; }

private:
    ConfigStore();

    mutable std::mutex mutex_;
    std::map<std::string, std::string> values_;
    std::string file_path_;
};

} // namespace config
} // namespace changeos
