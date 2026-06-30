#pragma once

#include <string>

namespace changeos {
namespace snapshot_diff {

struct DiffOptions {
    // 两个快照文件路径（必填）。
    std::string path_a;
    std::string path_b;
    // 是否输出 JSON 格式（默认为人类可读文本）。
    bool json = false;
    // 是否在文本输出中显示各部分的详细条目（默认仅显示计数摘要）。
    bool verbose = false;
};

class SnapshotDiff {
public:
    // 比较两个 snapshot JSON 文件并输出差异。
    // 返回：0 = 无差异；1 = 有差异；2 = 错误（文件无法读取等）。
    static int run(const DiffOptions& opts);
};

} // namespace snapshot_diff
} // namespace changeos
