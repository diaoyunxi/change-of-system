#include "monitor/file_integrity/file_integrity_monitor.h"

#include "platform/platform_detection.h"
#include "utils/logger.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>

#ifdef __APPLE__
#include <CommonCrypto/CommonCrypto.h>
#elif defined(_WIN32)
#include <Windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#else
#include <openssl/sha.h>
#endif

namespace changeos {
namespace monitor {

FileIntegrityMonitor::FileIntegrityMonitor() = default;
FileIntegrityMonitor::~FileIntegrityMonitor() { stop(); }

bool FileIntegrityMonitor::is_available() const {
    return true;
}

void FileIntegrityMonitor::add_watch_file(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& f : watch_files_) {
        if (f == path) return;
    }
    watch_files_.push_back(path);
}

void FileIntegrityMonitor::remove_watch_file(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find(watch_files_.begin(), watch_files_.end(), path);
    if (it != watch_files_.end()) {
        watch_files_.erase(it);
    }
}

const std::vector<std::string>& FileIntegrityMonitor::watch_files() const {
    return watch_files_;
}

bool FileIntegrityMonitor::on_start() {
    COS_LOG_INFO("File integrity monitor starting");
    first_scan_.store(true);
    runner_ = std::make_unique<utils::PeriodicRunner>();
    runner_->start([this]() { tick(); },
                   std::chrono::milliseconds(poll_interval_ms_));
    return true;
}

bool FileIntegrityMonitor::on_stop() {
    if (runner_) runner_->stop();
    runner_.reset();
    COS_LOG_INFO("File integrity monitor stopped");
    return true;
}

std::string FileIntegrityMonitor::compute_hash(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return "";

#ifdef __APPLE__
    unsigned char digest[SHA256_DIGEST_LENGTH];
    CC_SHA256_CTX ctx;
    CC_SHA256_Init(&ctx);

    char buf[8192];
    while (file.read(buf, sizeof(buf))) {
        CC_SHA256_Update(&ctx, buf, file.gcount());
    }
    CC_SHA256_Update(&ctx, buf, file.gcount());
    CC_SHA256_Final(digest, &ctx);

#elif defined(_WIN32)
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    NTSTATUS status;
    DWORD cbHash, cbData;
    unsigned char digest[32];

    status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (!BCRYPT_SUCCESS(status)) return "";

    status = BCryptCreateHash(hAlg, &hHash, NULL, 0, NULL, 0, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }

    char buf[8192];
    while (file.read(buf, sizeof(buf))) {
        cbData = static_cast<DWORD>(file.gcount());
        BCryptHashData(hHash, reinterpret_cast<PUCHAR>(buf), cbData, 0);
    }
    cbData = static_cast<DWORD>(file.gcount());
    BCryptHashData(hHash, reinterpret_cast<PUCHAR>(buf), cbData, 0);

    status = BCryptGetProperty(hHash, BCRYPT_HASH_LENGTH, reinterpret_cast<PBYTE>(&cbHash), sizeof(cbHash), &cbData, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }

    BCryptFinishHash(hHash, digest, sizeof(digest), 0);
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

#else
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256_CTX ctx;
    SHA256_Init(&ctx);

    char buf[8192];
    while (file.read(buf, sizeof(buf))) {
        SHA256_Update(&ctx, buf, file.gcount());
    }
    SHA256_Update(&ctx, buf, file.gcount());
    SHA256_Final(digest, &ctx);
#endif

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 32; ++i) {
        oss << std::setw(2) << static_cast<int>(digest[i]);
    }
    return oss.str();
}

std::uint64_t FileIntegrityMonitor::get_file_size(const std::string& path) {
    try {
        return std::filesystem::file_size(path);
    } catch (...) {
        return 0;
    }
}

std::uint64_t FileIntegrityMonitor::get_file_mtime(const std::string& path) {
    try {
        auto ftime = std::filesystem::last_write_time(path);
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            sctp.time_since_epoch()).count();
    } catch (...) {
        return 0;
    }
}

void FileIntegrityMonitor::tick() {
    std::vector<std::string> files_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        files_copy = watch_files_;
    }

    if (files_copy.empty()) return;

    std::map<std::string, FileHash> current_hashes;
    for (const auto& path : files_copy) {
        if (!std::filesystem::exists(path)) continue;

        FileHash fh;
        fh.path = path;
        fh.hash = compute_hash(path);
        fh.size = get_file_size(path);
        fh.mtime = get_file_mtime(path);

        current_hashes[path] = fh;
    }

    if (first_scan_.exchange(false)) {
        std::lock_guard<std::mutex> lock(mutex_);
        previous_hashes_ = std::move(current_hashes);
        return;
    }

    std::map<std::string, FileHash> prev_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        prev_copy = previous_hashes_;
    }

    for (const auto& [path, fh] : current_hashes) {
        if (prev_copy.find(path) == prev_copy.end()) {
            Event ev;
            ev.category = EventCategory::Filesystem;
            ev.type = EventType::FileCreated;
            ev.source = "file_integrity";
            ev.target = path;
            ev.summary = "New file added to integrity monitoring: " + path;
            ev.attributes["path"] = path;
            ev.attributes["hash"] = fh.hash;
            ev.attributes["size"] = std::to_string(fh.size);
            ev.platform = platform::name();
            ev.host = platform::hostname();
            emit(ev);
        } else {
            auto prev_fh = prev_copy[path];
            if (prev_fh.hash != fh.hash) {
                Event ev;
                ev.category = EventCategory::Filesystem;
                ev.type = EventType::FileModified;
                ev.source = "file_integrity";
                ev.target = path;
                ev.summary = "File integrity changed: " + path;
                ev.attributes["path"] = path;
                ev.attributes["old_hash"] = prev_fh.hash;
                ev.attributes["new_hash"] = fh.hash;
                ev.attributes["old_size"] = std::to_string(prev_fh.size);
                ev.attributes["new_size"] = std::to_string(fh.size);
                ev.platform = platform::name();
                ev.host = platform::hostname();
                emit(ev);
            } else if (prev_fh.size != fh.size) {
                Event ev;
                ev.category = EventCategory::Filesystem;
                ev.type = EventType::FileModified;
                ev.source = "file_integrity";
                ev.target = path;
                ev.summary = "File size changed but hash unchanged: " + path;
                ev.attributes["path"] = path;
                ev.attributes["hash"] = fh.hash;
                ev.attributes["old_size"] = std::to_string(prev_fh.size);
                ev.attributes["new_size"] = std::to_string(fh.size);
                ev.platform = platform::name();
                ev.host = platform::hostname();
                emit(ev);
            }
        }
    }

    for (const auto& [path, fh] : prev_copy) {
        if (current_hashes.find(path) == current_hashes.end()) {
            Event ev;
            ev.category = EventCategory::Filesystem;
            ev.type = EventType::FileDeleted;
            ev.source = "file_integrity";
            ev.target = path;
            ev.summary = "Monitored file deleted: " + path;
            ev.attributes["path"] = path;
            ev.attributes["last_hash"] = fh.hash;
            ev.attributes["last_size"] = std::to_string(fh.size);
            ev.platform = platform::name();
            ev.host = platform::hostname();
            emit(ev);
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        previous_hashes_ = std::move(current_hashes);
    }
}

} // namespace monitor
} // namespace changeos