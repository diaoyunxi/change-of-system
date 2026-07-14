#include "utils/daemon.h"
#include "utils/logger.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

namespace changeos {
namespace utils {

bool Daemon::daemonize() {
    // First fork
    pid_t pid = fork();
    if (pid < 0) {
        COS_LOG_ERROR("First fork failed");
        return false;
    }
    if (pid > 0) {
        // Parent process exits
        exit(EXIT_SUCCESS);
    }

    // Create new session
    if (setsid() < 0) {
        COS_LOG_ERROR("setsid failed");
        return false;
    }

    // Second fork
    pid = fork();
    if (pid < 0) {
        COS_LOG_ERROR("Second fork failed");
        return false;
    }
    if (pid > 0) {
        // First child exits
        exit(EXIT_SUCCESS);
    }

    // Set file mode creation mask (restrictive: only owner has permissions)
    umask(0077);

    // Change working directory
    if (chdir("/") < 0) {
        COS_LOG_WARN("chdir to / failed");
    }

    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Redirect standard file descriptors to /dev/null
    int fd = open("/dev/null", O_RDWR);
    if (fd != -1) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > 2) {
            close(fd);
        }
    }

    COS_LOG_INFO("Daemonized successfully");
    return true;
}

bool Daemon::create_pid_directory(const std::string& pid_file) {
    // Extract directory path from pid_file
    std::string dir_path;
    size_t last_slash = pid_file.find_last_of('/');
    if (last_slash != std::string::npos) {
        dir_path = pid_file.substr(0, last_slash);
    } else {
        return true; // No directory, just filename
    }

    // Check if directory exists
    struct stat st;
    if (stat(dir_path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }

    // Create directory
    return mkdir(dir_path.c_str(), 0755) == 0;
}

bool Daemon::write_pid_file(const std::string& pid_file) {
    if (!create_pid_directory(pid_file)) {
        COS_LOG_ERROR("Failed to create PID file directory");
        return false;
    }

    std::ofstream file(pid_file);
    if (!file.is_open()) {
        COS_LOG_ERROR("Failed to open PID file: " + pid_file);
        return false;
    }

    file << getpid() << std::endl;
    file.close();

    COS_LOG_INFO("PID file written: " + pid_file + " (PID: " + std::to_string(getpid()) + ")");
    return true;
}

void Daemon::remove_pid_file(const std::string& pid_file) {
    if (unlink(pid_file.c_str()) == 0) {
        COS_LOG_INFO("PID file removed: " + pid_file);
    } else {
        COS_LOG_WARN("Failed to remove PID file: " + pid_file);
    }
}

bool Daemon::is_already_running(const std::string& pid_file) {
    int pid = get_running_pid(pid_file);
    if (pid <= 0) {
        return false;
    }

    // Check if process is running
    if (kill(pid, 0) == 0) {
        return true;
    }

    return false;
}

int Daemon::get_running_pid(const std::string& pid_file) {
    std::ifstream file(pid_file);
    if (!file.is_open()) {
        return -1;
    }

    int pid;
    file >> pid;
    file.close();

    return pid;
}

} // namespace utils
} // namespace changeos
