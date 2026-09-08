# CodexQuotaTaskbar v0.1.9

发布日期：2026-09-08

本版本修复 Windows 11 任务栏内部控件短暂未就绪时，程序在启动阶段过早弹出“无法读取任务栏轻量结构签名”并退出的问题。它保留 v0.1.8 的 Codex / 智谱 GLM 额度来源、只读网络边界、任务栏嵌入、睡眠恢复宽限、TrafficMonitor 识别和全部显示设置。

## 本次更新

- 暂时性任务栏就绪失败的启动探测由最多 6 次扩展为 21 次：第一次失败后等待 1.5 秒，之后每次等待 3 秒，重试间隔累计 58.5 秒。
- 等待期间继续处理 Windows 消息，不阻塞退出；每次完整探测前仍销毁可能部分附着的旧子窗口，避免与 Explorer UI Automation 形成等待环。
- 明确不支持的任务栏方向、安全空白不足和外部障碍耗尽空间仍立即报错，不会被延长重试掩盖。
- 暂时性失败重试耗尽后，错误提示保留最后一次脱敏原因，并建议稍后重启程序或重启 Windows 资源管理器。
- 启动策略测试新增接近一分钟的等待上限，以及在最后一次允许探测时恢复的边界覆盖。

## 验证状态

本机 Windows 11 build `10.0.26200.9278`、96 DPI、单显示器 2560×1440、底部任务栏环境已完成：

- Visual Studio 2022 Build Tools 17.14.39、MSVC 19.44、Windows SDK 10.0.26100.0 的 x64 Release 全量构建和 CTest 7/7。
- `TaskbarProbe` 返回 `supported`；`TaskbarInteractionProbe` 21/21；`TaskbarStabilityProbe` 19/19；`AppLifecycleProbe` 156 毫秒优雅退出。
- 启动策略自动化覆盖暂时失败后恢复、在最后一次允许探测时恢复、重试耗尽，以及永久不支持和空间不足立即返回。
- 出现问题的正式 v0.1.8 EXE 已核对 SHA-256，与 GitHub Release 附件一致；同一 Explorer 进程随后恢复正常，排除文件损坏和永久结构不兼容。
- Windows 2022 / Visual Studio 2022 的构建、测试与打包以 v0.1.9 标签对应的 GitHub Actions 为最终门槛。

## 已知限制

- 真实 Shell 子树持续延迟超过旧 13.5 秒上限的场景尚未再次出现；自动化已覆盖延迟恢复策略，但不能替代真实登录或受控 Explorer 竞态验证。
- 本版本未重新执行真实睡眠/唤醒、真实注销/登录、截图触发器、任务栏自动隐藏、Explorer 真实重启、具体游戏、125% / 150% / 200% DPI、双显示器、主显示器切换和干净 Windows 用户环境；这些项目只保留历史结果，不扩大 v0.1.9 支持承诺。
- 当前 EXE 未进行代码签名，Windows 可能显示“未知发布者”或 SmartScreen 提示。
- 使用的仍是未公开后端接口（`chatgpt.com/backend-api/wham/` 与智谱 `/api/monitor/usage/quota/limit`），没有长期兼容性承诺。
- 没有安装器、自动更新、Windows 10 支持、多账号、Token 自动刷新或多任务栏显示。

## 安装

可直接下载 `CodexQuotaTaskbar-v0.1.9-win-x64.exe`，也可以下载 `CodexQuotaTaskbar-v0.1.9-win-x64.zip` 后解压运行，并使用 `SHA256SUMS.txt` 核对两者。目标电脑需安装 Microsoft Visual C++ 2015–2026 x64 运行库。从旧版本升级时先退出软件，再解压并覆盖原文件；已有设置保持不变。

完整发布包只允许包含正式 EXE、README、发行说明、MIT License、第三方声明和 nlohmann/json MIT License，不得包含凭证、账号数据、响应转储、测试固件、参考仓库或构建缓存。
