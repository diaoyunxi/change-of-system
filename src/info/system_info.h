#pragma once

#include <string>

namespace changeos {
namespace info {

struct InfoOptions {
    // 是否启用 ANSI 颜色输出。
    bool color = false;
    // 是否输出 JSON 格式。
    bool json = false;
};

class SystemInfo {
public:
    // 采集并打印当前系统的关键信息（平台、CPU、内存、磁盘、网络、运行时间等）。
    // 类似 neofetch 的快速摘要，但聚焦于监控场景。
    // 返回 0 表示成功。
    static int run(const InfoOptions& opts);
};

} // namespace info
} // namespace changeos
