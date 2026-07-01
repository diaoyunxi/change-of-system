#include "snapshot_diff/snapshot_diff.h"

#include "utils/logger.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace changeos {
namespace snapshot_diff {

namespace {

// ----------------- 极简 JSON 解析器（仅用于 snapshot 文件） -------------
// 支持对象、数组、字符串、数字、布尔、null。不支持注释。
// 解析失败时返回 Null。

class JsonValue;
using JsonObject = std::map<std::string, JsonValue>;
using JsonArray = std::vector<JsonValue>;

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool bool_val = false;
    double num_val = 0.0;
    std::string str_val;
    JsonArray arr_val;
    JsonObject obj_val;

    bool is_object() const { return type == Type::Object; }
    bool is_array() const { return type == Type::Array; }
    bool is_string() const { return type == Type::String; }
    bool is_number() const { return type == Type::Number; }

    const JsonValue* find(const std::string& key) const {
        if (type != Type::Object) return nullptr;
        auto it = obj_val.find(key);
        return it == obj_val.end() ? nullptr : &it->second;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : text_(text), pos_(0) {}

    bool parse(JsonValue& out) {
        skip_ws();
        return parse_value(out);
    }

    std::string error() const { return error_; }

private:
    const std::string& text_;
    size_t pos_;
    std::string error_;

    void skip_ws() {
        while (pos_ < text_.size()) {
            char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++pos_; continue; }
            break;
        }
    }

    bool parse_value(JsonValue& v) {
        skip_ws();
        if (pos_ >= text_.size()) { error_ = "unexpected end"; return false; }
        char c = text_[pos_];
        if (c == '{') return parse_object(v);
        if (c == '[') return parse_array(v);
        if (c == '"') return parse_string(v.str_val) && (v.type = JsonValue::Type::String, true);
        if (c == 't' || c == 'f') return parse_bool(v);
        if (c == 'n') return parse_null(v);
        if (c == '-' || (c >= '0' && c <= '9')) return parse_number(v);
        error_ = "unexpected char '" + std::string(1, c) + "'";
        return false;
    }

    bool parse_object(JsonValue& v) {
        v.type = JsonValue::Type::Object;
        ++pos_; // {
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == '}') { ++pos_; return true; }
        while (true) {
            skip_ws();
            std::string key;
            if (!parse_string(key)) return false;
            skip_ws();
            if (pos_ >= text_.size() || text_[pos_] != ':') { error_ = "expected ':'"; return false; }
            ++pos_;
            JsonValue val;
            if (!parse_value(val)) return false;
            v.obj_val[std::move(key)] = std::move(val);
            skip_ws();
            if (pos_ >= text_.size()) { error_ = "unterminated object"; return false; }
            char nc = text_[pos_];
            if (nc == ',') { ++pos_; continue; }
            if (nc == '}') { ++pos_; return true; }
            error_ = "expected ',' or '}'";
            return false;
        }
    }

    bool parse_array(JsonValue& v) {
        v.type = JsonValue::Type::Array;
        ++pos_; // [
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == ']') { ++pos_; return true; }
        while (true) {
            JsonValue val;
            if (!parse_value(val)) return false;
            v.arr_val.push_back(std::move(val));
            skip_ws();
            if (pos_ >= text_.size()) { error_ = "unterminated array"; return false; }
            char nc = text_[pos_];
            if (nc == ',') { ++pos_; continue; }
            if (nc == ']') { ++pos_; return true; }
            error_ = "expected ',' or ']'";
            return false;
        }
    }

    bool parse_string(std::string& out) {
        if (pos_ >= text_.size() || text_[pos_] != '"') { error_ = "expected string"; return false; }
        ++pos_;
        out.clear();
        while (pos_ < text_.size()) {
            char c = text_[pos_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos_ >= text_.size()) { error_ = "bad escape"; return false; }
                char e = text_[pos_++];
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        if (pos_ + 4 > text_.size()) { error_ = "bad \\u"; return false; }
                        // 简化处理：仅取 4 位 hex，基本 BMP 处理。不做完整 UTF-8 编码。
                        out += "?";
                        pos_ += 4;
                        break;
                    }
                    default: out += e; break;
                }
            } else {
                out += c;
            }
        }
        error_ = "unterminated string";
        return false;
    }

    bool parse_number(JsonValue& v) {
        size_t start = pos_;
        if (pos_ < text_.size() && text_[pos_] == '-') ++pos_;
        while (pos_ < text_.size() && (std::isdigit(static_cast<unsigned char>(text_[pos_])) ||
               text_[pos_] == '.' || text_[pos_] == 'e' || text_[pos_] == 'E' ||
               text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
        std::string s = text_.substr(start, pos_ - start);
        try { v.num_val = std::stod(s); v.type = JsonValue::Type::Number; return true; }
        catch (...) { error_ = "bad number: " + s; return false; }
    }

    bool parse_bool(JsonValue& v) {
        if (text_.compare(pos_, 4, "true") == 0) {
            v.type = JsonValue::Type::Bool; v.bool_val = true; pos_ += 4; return true;
        }
        if (text_.compare(pos_, 5, "false") == 0) {
            v.type = JsonValue::Type::Bool; v.bool_val = false; pos_ += 5; return true;
        }
        error_ = "bad bool";
        return false;
    }

    bool parse_null(JsonValue& v) {
        if (text_.compare(pos_, 4, "null") == 0) {
            v.type = JsonValue::Type::Null; pos_ += 4; return true;
        }
        error_ = "bad null";
        return false;
    }
};

// ----------------- 工具函数 -------------

bool read_file(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream oss;
    oss << f.rdbuf();
    out = oss.str();
    return true;
}

std::string json_escape_str(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else out += c;
        }
    }
    return out;
}

// 将 JSON 值重新序列化为紧凑字符串（用于显示标量）。
std::string scalar_to_string(const JsonValue& v) {
    switch (v.type) {
        case JsonValue::Type::String: return v.str_val;
        case JsonValue::Type::Number: {
            std::ostringstream oss;
            oss << v.num_val;
            return oss.str();
        }
        case JsonValue::Type::Bool:   return v.bool_val ? "true" : "false";
        case JsonValue::Type::Null:   return "null";
        default: return "<complex>";
    }
}

// 提取对象中可作为"主键"的字段：优先 pid / port / device+mount_point / address / name。
std::string extract_key(const JsonObject& obj) {
    auto get = [&](const std::string& k) -> std::string {
        auto it = obj.find(k);
        if (it == obj.end()) return "";
        return scalar_to_string(it->second);
    };
    std::string pid = get("pid");
    if (!pid.empty()) return "pid:" + pid;
    std::string port = get("port");
    if (!port.empty()) {
        std::string proto = get("protocol");
        std::string addr = get("address");
        return "port:" + proto + ":" + addr + ":" + port;
    }
    std::string dev = get("device");
    std::string mount = get("mount_point");
    if (!dev.empty() || !mount.empty()) return "disk:" + mount + "(" + dev + ")";
    std::string addr = get("local_address");
    std::string raddr = get("remote_address");
    if (!addr.empty() || !raddr.empty()) return "conn:" + addr + "->" + raddr;
    std::string name = get("name");
    if (!name.empty()) return "name:" + name;
    // 退化：返回所有标量字段的拼接
    std::string fallback;
    for (const auto& [k, v] : obj) {
        if (v.is_string() || v.is_number()) fallback += k + "=" + scalar_to_string(v) + ";";
    }
    return fallback;
}

// 将数组的每个元素（假设是对象）按主键索引。
std::map<std::string, JsonObject> index_array(const JsonValue& arr) {
    std::map<std::string, JsonObject> out;
    if (!arr.is_array()) return out;
    for (const auto& item : arr.arr_val) {
        if (!item.is_object()) continue;
        std::string k = extract_key(item.obj_val);
        if (k.empty()) continue;
        out.emplace(std::move(k), item.obj_val);
    }
    return out;
}

// 比较两个对象，返回所有不同的字段名。
std::vector<std::string> diff_fields(const JsonObject& a, const JsonObject& b) {
    std::vector<std::string> diffs;
    for (const auto& [k, va] : a) {
        auto it = b.find(k);
        if (it == b.end()) { diffs.push_back(k + " (removed)"); continue; }
        if (va.is_string() || va.is_number() || va.type == JsonValue::Type::Bool ||
            va.type == JsonValue::Type::Null) {
            if (scalar_to_string(va) != scalar_to_string(it->second)) {
                diffs.push_back(k + ": " + scalar_to_string(va) + " -> " + scalar_to_string(it->second));
            }
        }
    }
    for (const auto& [k, vb] : b) {
        if (a.find(k) == a.end()) diffs.push_back(k + " (added: " + scalar_to_string(vb) + ")");
    }
    return diffs;
}

struct SectionDiff {
    std::vector<std::string> added;     // 在 b 中新增
    std::vector<std::string> removed;   // 在 b 中移除
    std::vector<std::pair<std::string, std::string>> changed; // (key, 变化描述)
};

SectionDiff diff_array_section(const JsonValue& a, const JsonValue& b) {
    SectionDiff d;
    auto idx_a = index_array(a);
    auto idx_b = index_array(b);
    for (const auto& [k, va] : idx_a) {
        auto it = idx_b.find(k);
        if (it == idx_b.end()) {
            d.removed.push_back(k);
        } else {
            auto fields = diff_fields(va, it->second);
            if (!fields.empty()) {
                std::string desc;
                for (size_t i = 0; i < fields.size(); ++i) {
                    if (i) desc += "; ";
                    desc += fields[i];
                }
                d.changed.emplace_back(k, desc);
            }
        }
    }
    for (const auto& [k, vb] : idx_b) {
        if (idx_a.find(k) == idx_a.end()) d.added.push_back(k);
    }
    return d;
}

// 比较两个对象型 section（如 system / environment）。
std::vector<std::pair<std::string, std::string>> diff_object_section(const JsonValue& a, const JsonValue& b) {
    std::vector<std::pair<std::string, std::string>> out;
    if (!a.is_object() || !b.is_object()) return out;
    auto fields = diff_fields(a.obj_val, b.obj_val);
    for (auto& f : fields) out.emplace_back("(field)", std::move(f));
    return out;
}

// 提取顶层 snapshot 对象。
const JsonValue* extract_snapshot(const JsonValue& root) {
    if (!root.is_object()) return nullptr;
    auto it = root.obj_val.find("snapshot");
    return it == root.obj_val.end() ? nullptr : &it->second;
}

const JsonValue* extract_sections(const JsonValue* snap) {
    if (!snap || !snap->is_object()) return nullptr;
    auto it = snap->obj_val.find("sections");
    return it == snap->obj_val.end() ? nullptr : &it->second;
}

const JsonValue* extract_meta_field(const JsonValue* snap, const std::string& key) {
    if (!snap || !snap->is_object()) return nullptr;
    auto it = snap->obj_val.find(key);
    return it == snap->obj_val.end() ? nullptr : &it->second;
}

} // namespace

int SnapshotDiff::run(const DiffOptions& opts) {
    if (opts.path_a.empty() || opts.path_b.empty()) {
        std::cerr << "错误: 需要两个快照文件路径\n";
        return 2;
    }

    std::string text_a, text_b;
    if (!read_file(opts.path_a, text_a)) {
        std::cerr << "错误: 无法读取快照 A: " << opts.path_a << "\n";
        return 2;
    }
    if (!read_file(opts.path_b, text_b)) {
        std::cerr << "错误: 无法读取快照 B: " << opts.path_b << "\n";
        return 2;
    }

    JsonValue root_a, root_b;
    JsonParser pa(text_a), pb(text_b);
    if (!pa.parse(root_a)) {
        std::cerr << "错误: 快照 A JSON 解析失败: " << pa.error() << "\n";
        return 2;
    }
    if (!pb.parse(root_b)) {
        std::cerr << "错误: 快照 B JSON 解析失败: " << pb.error() << "\n";
        return 2;
    }

    const JsonValue* snap_a = extract_snapshot(root_a);
    const JsonValue* snap_b = extract_snapshot(root_b);
    if (!snap_a || !snap_b) {
        std::cerr << "错误: 快照缺少 'snapshot' 顶层对象\n";
        return 2;
    }
    const JsonValue* secs_a = extract_sections(snap_a);
    const JsonValue* secs_b = extract_sections(snap_b);
    if (!secs_a || !secs_b) {
        std::cerr << "错误: 快照缺少 'sections' 字段\n";
        return 2;
    }

    // 时间戳元数据
    auto get_ts = [](const JsonValue* snap) -> std::string {
        const JsonValue* v = extract_meta_field(snap, "timestamp");
        return v ? scalar_to_string(*v) : "?";
    };
    std::string ts_a = get_ts(snap_a);
    std::string ts_b = get_ts(snap_b);

    // 收集差异
    struct SectionResult {
        std::string name;
        bool is_array;
        SectionDiff array_diff;
        std::vector<std::pair<std::string, std::string>> obj_diff;
    };
    std::vector<SectionResult> results;
    bool any_diff = false;

    static const std::vector<std::pair<std::string, bool>> sections = {
        {"system",          false},
        {"disks",           true},
        {"processes",       true},
        {"connections",     true},
        {"listening_ports", true},
        {"environment",     false},
    };

    for (const auto& [name, is_array] : sections) {
        SectionResult r;
        r.name = name;
        r.is_array = is_array;
        const JsonValue* va = secs_a->find(name);
        const JsonValue* vb = secs_b->find(name);
        if (!va && !vb) continue;
        if (!va || !vb) {
            // 整个 section 在一侧缺失
            if (!va) r.obj_diff.emplace_back("(section)", "added in B");
            else     r.obj_diff.emplace_back("(section)", "removed in B");
            any_diff = true;
            results.push_back(std::move(r));
            continue;
        }
        if (is_array) {
            r.array_diff = diff_array_section(*va, *vb);
            if (!r.array_diff.added.empty() || !r.array_diff.removed.empty() ||
                !r.array_diff.changed.empty()) {
                any_diff = true;
            }
        } else {
            r.obj_diff = diff_object_section(*va, *vb);
            if (!r.obj_diff.empty()) any_diff = true;
        }
        results.push_back(std::move(r));
    }

    // 元数据差异
    std::vector<std::pair<std::string, std::string>> meta_diffs;
    static const std::vector<std::string> meta_keys = {
        "host", "platform", "architecture", "user", "uptime_seconds"
    };
    for (const auto& k : meta_keys) {
        const JsonValue* va = extract_meta_field(snap_a, k);
        const JsonValue* vb = extract_meta_field(snap_b, k);
        if (va && vb && scalar_to_string(*va) != scalar_to_string(*vb)) {
            meta_diffs.emplace_back(k, scalar_to_string(*va) + " -> " + scalar_to_string(*vb));
            any_diff = true;
        } else if (va && !vb) {
            meta_diffs.emplace_back(k, "removed");
            any_diff = true;
        } else if (!va && vb) {
            meta_diffs.emplace_back(k, "added: " + scalar_to_string(*vb));
            any_diff = true;
        }
    }

    // 输出
    if (opts.json) {
        std::cout << "{\n";
        std::cout << "  \"snapshot_a\": \"" << json_escape_str(ts_a) << "\",\n";
        std::cout << "  \"snapshot_b\": \"" << json_escape_str(ts_b) << "\",\n";
        std::cout << "  \"has_differences\": " << (any_diff ? "true" : "false") << ",\n";
        std::cout << "  \"metadata\": [";
        for (size_t i = 0; i < meta_diffs.size(); ++i) {
            if (i) std::cout << ", ";
            std::cout << "{\"field\":\"" << json_escape_str(meta_diffs[i].first)
                      << "\",\"change\":\"" << json_escape_str(meta_diffs[i].second) << "\"}";
        }
        std::cout << "],\n";
        std::cout << "  \"sections\": {";
        bool first = true;
        for (const auto& r : results) {
            if (first) { first = false; } else { std::cout << ","; }
            std::cout << "\n    \"" << r.name << "\": {";
            if (r.is_array) {
                std::cout << "\"added\":" << r.array_diff.added.size()
                          << ",\"removed\":" << r.array_diff.removed.size()
                          << ",\"changed\":" << r.array_diff.changed.size();
                if (opts.verbose) {
                    std::cout << ",\"added_items\":[";
                    for (size_t i = 0; i < r.array_diff.added.size(); ++i) {
                        if (i) std::cout << ",";
                        std::cout << "\"" << json_escape_str(r.array_diff.added[i]) << "\"";
                    }
                    std::cout << "],\"removed_items\":[";
                    for (size_t i = 0; i < r.array_diff.removed.size(); ++i) {
                        if (i) std::cout << ",";
                        std::cout << "\"" << json_escape_str(r.array_diff.removed[i]) << "\"";
                    }
                    std::cout << "],\"changed_items\":[";
                    for (size_t i = 0; i < r.array_diff.changed.size(); ++i) {
                        if (i) std::cout << ",";
                        std::cout << "{\"key\":\"" << json_escape_str(r.array_diff.changed[i].first)
                                  << "\",\"change\":\"" << json_escape_str(r.array_diff.changed[i].second) << "\"}";
                    }
                    std::cout << "]";
                }
            } else {
                std::cout << "\"changed\":" << r.obj_diff.size();
                if (opts.verbose) {
                    std::cout << ",\"items\":[";
                    for (size_t i = 0; i < r.obj_diff.size(); ++i) {
                        if (i) std::cout << ",";
                        std::cout << "{\"field\":\"" << json_escape_str(r.obj_diff[i].first)
                                  << "\",\"change\":\"" << json_escape_str(r.obj_diff[i].second) << "\"}";
                    }
                    std::cout << "]";
                }
            }
            std::cout << "}";
        }
        std::cout << "\n  }\n}\n";
    } else {
        std::cout << "==============================================================\n";
        std::cout << " change-of-system 快照差异对比\n";
        std::cout << "==============================================================\n";
        std::cout << " 快照 A: " << opts.path_a << "  (" << ts_a << ")\n";
        std::cout << " 快照 B: " << opts.path_b << "  (" << ts_b << ")\n";
        std::cout << "--------------------------------------------------------------\n";

        if (!meta_diffs.empty()) {
            std::cout << " 元数据变更:\n";
            for (const auto& [k, v] : meta_diffs) {
                std::cout << "   " << k << ": " << v << "\n";
            }
            std::cout << "--------------------------------------------------------------\n";
        }

        if (results.empty()) {
            std::cout << " 未发现任何可比较的 section。\n";
        }
        for (const auto& r : results) {
            std::cout << " [" << r.name << "]\n";
            if (r.is_array) {
                std::cout << "   新增: " << r.array_diff.added.size()
                          << "  移除: " << r.array_diff.removed.size()
                          << "  变更: " << r.array_diff.changed.size() << "\n";
                if (opts.verbose) {
                    for (const auto& k : r.array_diff.added)
                        std::cout << "     + " << k << "\n";
                    for (const auto& k : r.array_diff.removed)
                        std::cout << "     - " << k << "\n";
                    for (const auto& [k, desc] : r.array_diff.changed)
                        std::cout << "     ~ " << k << "  (" << desc << ")\n";
                }
            } else {
                std::cout << "   变更字段数: " << r.obj_diff.size() << "\n";
                if (opts.verbose) {
                    for (const auto& [k, desc] : r.obj_diff)
                        std::cout << "     ~ " << desc << "\n";
                }
            }
        }
        std::cout << "--------------------------------------------------------------\n";
        std::cout << " 结论: " << (any_diff ? "存在差异" : "无差异") << "\n";
        std::cout << "==============================================================\n";
    }

    return any_diff ? 1 : 0;
}

} // namespace snapshot_diff
} // namespace changeos
