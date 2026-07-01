#pragma once

#include "core/event.h"

#include <atomic>
#include <string>

namespace changeos {

class MonitorEngine;

namespace tail {

struct TailOptions {
    // 可选：按事件类别过滤（filesystem / process / network / ...）。
    // 空字符串表示显示所有类别。
    std::string category_filter;
    // 可选：按来源子串过滤（大小写不敏感）。
    std::string source_filter;
    // 可选：按 summary/target/source 子串过滤（大小写不敏感）。
    std::string keyword_filter;
    // 是否启用 ANSI 颜色输出（按严重程度着色）。
    bool color = false;
    // 是否输出 JSON 行（每行一个 JSON 对象），便于管道处理。
    bool json = false;
    // 输出前先打印的最近事件数量（0 表示只输出启动后产生的新事件）。
    int initial_recent = 0;
};

class TailWatcher {
public:
    // 启动 MonitorEngine 后调用此函数进入实时事件流模式。
    // 阻塞直到 g_running 被置为 false（例如 Ctrl+C）。
    // 返回退出码（0 表示正常结束）。
    static int run(::changeos::MonitorEngine& engine, const TailOptions& opts,
                   std::atomic<bool>& running);
};

} // namespace tail
} // namespace changeos
