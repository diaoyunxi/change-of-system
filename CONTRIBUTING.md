# 贡献指南

感谢你对 change-of-system 项目的关注！

## 开发环境

- **编译器：** GCC 11+ 或 Clang 14+（C++17）
- **构建系统：** CMake 3.16+
- **平台：** Linux（主要）、macOS

## 构建步骤

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## 项目结构

```
src/
├── alert/           # 告警管理
├── config/          # 配置存储与监控
├── core/            # 核心事件和监控引擎
├── diagnostic/      # 诊断功能
├── export/          # 事件导出
├── filter/          # 事件过滤
├── gui/             # 图形界面
├── monitor/         # 各类监控器（磁盘、文件、网络、进程等）
├── report/          # 报告生成
├── security/        # 安全审计
├── snapshot/        # 快照生成与对比
├── updater/         # 自动更新
└── webhook/         # Webhook 通知
```

## 安全注意事项

- 更新模块避免使用 `std::system()`，优先 `fork()+execlp()`（CWE-78）
- 监控器权限最小化
- 日志中避免记录敏感信息

## 提交 Pull Request

1. Fork 本仓库并创建功能分支
2. 确保编译通过且无警告
3. 遵循 Conventional Commits 规范提交
