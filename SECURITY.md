# 安全策略

## 报告安全漏洞

如果你发现了安全漏洞，请通过以下方式报告：

1. **请勿**在公开的 GitHub Issue 中报告安全漏洞
2. 请通过 GitHub 的 [Security Advisories](https://github.com/diaoyunxi/change-of-system/security/advisories/new) 页面提交报告

## 安全范围

以下属于本项目的安全关注点：

- 自动更新模块的 shell 注入（CWE-78）
- 文件完整性监控的绕过
- 配置文件注入
- 监控器权限提升
- Webhook URL SSRF（CWE-918）
