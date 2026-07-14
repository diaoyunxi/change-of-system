#include "updater/updater.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <vector>

#ifdef COS_USE_REMOTE_REPORTING
#include <curl/curl.h>
#endif

namespace changeos {
namespace updater {

namespace {

#ifdef COS_USE_REMOTE_REPORTING

size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    std::string* response = static_cast<std::string*>(userp);
    response->append(static_cast<char*>(contents), total_size);
    return total_size;
}

std::string http_get(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "change-of-system-updater");
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);

    // 检查 HTTP 响应状态码
    long http_code = 0;
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code != 200) {
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            return "";
        }
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) return "";
    return response;
}

std::string extract_json_string(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";

    pos += search.size();
    // Skip whitespace and colon
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t')) pos++;
    if (pos >= json.size() || json[pos] != '"') return "";
    pos++; // skip opening quote

    std::string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            pos++;
        }
        result += json[pos];
        pos++;
    }
    return result;
}

int compare_versions(const std::string& v1, const std::string& v2) {
    auto strip_v = [](const std::string& s) -> std::string {
        if (!s.empty() && s[0] == 'v') return s.substr(1);
        return s;
    };

    std::string s1 = strip_v(v1);
    std::string s2 = strip_v(v2);

    std::vector<int> parts1, parts2;
    std::stringstream ss1(s1), ss2(s2);
    std::string token;

    while (std::getline(ss1, token, '.')) {
        try { parts1.push_back(std::stoi(token)); }
        catch (...) { parts1.push_back(0); }
    }
    while (std::getline(ss2, token, '.')) {
        try { parts2.push_back(std::stoi(token)); }
        catch (...) { parts2.push_back(0); }
    }

    size_t max_len = std::max(parts1.size(), parts2.size());
    for (size_t i = 0; i < max_len; i++) {
        int a = i < parts1.size() ? parts1[i] : 0;
        int b = i < parts2.size() ? parts2[i] : 0;
        if (a > b) return 1;
        if (a < b) return -1;
    }
    return 0;
}

#endif // COS_USE_REMOTE_REPORTING

} // anonymous namespace

UpdateInfo check_for_update(const std::string& current_version) {
    UpdateInfo info;
    info.current_version = current_version;

#ifdef COS_USE_REMOTE_REPORTING
    // Try Releases API first
    std::string response = http_get(
        "https://api.github.com/repos/diaoyunxi/change-of-system/releases/latest");

    if (!response.empty()) {
        std::string tag = extract_json_string(response, "tag_name");
        std::string url = extract_json_string(response, "html_url");
        if (!tag.empty()) {
            info.latest_version = tag;
            info.release_url = url;
            if (compare_versions(tag, current_version) > 0) {
                info.available = true;
            }
            return info;
        }
    }

    // Fallback to Tags API
    response = http_get(
        "https://api.github.com/repos/diaoyunxi/change-of-system/tags");
    if (!response.empty()) {
        std::string tag = extract_json_string(response, "name");
        if (!tag.empty()) {
            info.latest_version = tag;
            info.release_url = "https://github.com/diaoyunxi/change-of-system/releases/tag/" + tag;
            if (compare_versions(tag, current_version) > 0) {
                info.available = true;
            }
        }
    }
#else
    info.error = "HTTP support not compiled in (COS_USE_REMOTE_REPORTING=OFF)";
#endif

    return info;
}

void prompt_update(const UpdateInfo& info) {
    if (!info.available) {
        if (!info.error.empty()) {
            std::cout << "[Updater] " << info.error << "\n";
        }
        return;
    }

    std::cout << "\n=============================================\n";
    std::cout << "  发现新版本！\n";
    std::cout << "  当前版本: v" << info.current_version << "\n";
    std::cout << "  最新版本: " << info.latest_version << "\n";
    std::cout << "=============================================\n";
    std::cout << "更新详情: " << info.release_url << "\n";
    std::cout << "是否立即更新？(y/N): ";

    std::string choice;
    std::getline(std::cin, choice);

    if (choice == "y" || choice == "Y") {
        std::cout << "正在更新...\n";
        int result = std::system("git pull 2>&1");
        if (result == 0) {
            std::cout << "更新成功！请重新编译并运行程序。\n";
        } else {
            std::cout << "自动更新失败，请手动访问:\n  " << info.release_url << "\n";
        }
    }
}

} // namespace updater
} // namespace changeos
