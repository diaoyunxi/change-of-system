#pragma once

#include <string>

namespace changeos {
namespace utils {

class Daemon {
public:
    static bool daemonize();
    static bool write_pid_file(const std::string& path);
    static void remove_pid_file(const std::string& path);
    static bool is_already_running(const std::string& pid_file);
    static int get_running_pid(const std::string& pid_file);

private:
    static bool create_pid_directory(const std::string& pid_file);
};

} // namespace utils
} // namespace changeos
