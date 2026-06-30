#pragma once

#include <string>
#include <vector>

namespace changeos {
namespace validate {

struct ValidationIssue {
    enum class Severity { Error, Warning, Info };
    Severity severity = Severity::Info;
    std::string key;     // 相关的配置键
    std::string message; // 人类可读说明
};

struct ValidationResult {
    std::vector<ValidationIssue> issues;
    int error_count = 0;
    int warning_count = 0;
    int info_count = 0;
    // 配置文件中实际解析出的键值对总数
    int parsed_keys = 0;
    // 配置文件是否存在
    bool file_exists = false;
    // 是否可继续启动（即没有 Error 级别问题）
    bool ok() const { return error_count == 0; }
};

class ConfigValidator {
public:
    // 解析并验证给定路径的配置文件。
    // 不修改全局 ConfigStore，独立进行语法/语义检查。
    static ValidationResult validate(const std::string& config_path);

    // 将验证结果格式化为人类可读文本并输出到 stdout。
    // 返回结果的 error_count（0 表示通过）。
    static int print_report(const ValidationResult& result,
                            const std::string& config_path);
};

} // namespace validate
} // namespace changeos
