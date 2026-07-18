# CodexQuotaTaskbar v0.1.1

发布日期：2026-07-18

这是 v0.1.0 的兼容性与显示改进版本，继续保持只读访问 Codex 额度、不写回 Token、不消费重置机会的安全边界。

## 本次更新

- “可用重置”仍显示当前次数，但到期信息只显示最早一项，格式为本地时间 `yy/MM/dd HH 时 mm 分`，不再展开全部日期。
- 任一尚可用重置缺少有效到期时间时显示「最早到期：--」，避免把其他已知时间误称为真正最早时间。
- 加固 `reset_after_seconds` 的整数范围与加法溢出检查；异常值不会破坏已经有效的额度数据。
- GitHub Actions 固定到支持 Node.js 24 的动作版本。

## 验证结果

- Visual Studio 2022 x64 Release 构建与 CTest 6/6 通过。
- Windows build `10.0.26200.8737`：150% DPI、双显示器扩展、主显示器切换和任务栏自动隐藏真实通过；每种稳定状态下交互探针 21/21。
- 主屏切换后全桌面只有一个额度窗口，旧主任务栏没有残留；DPI 从 96 切换到 144 后窗口尺寸由 84×42 调整为 126×62。
- 恢复到 Windows build `10.0.26200.8875`、100% DPI 单显示器后，交互探针再次 21/21 通过。
- 用户随后手动确认 125% / 200% DPI 与睡眠唤醒恢复均正常；至此 100% / 125% / 150% / 200% DPI、双显示器主屏切换、任务栏自动隐藏和睡眠恢复均已完成真实桌面验证。

## 已知限制

- 干净 Windows 用户环境及其他 Windows 11 内部版本尚未完成真实验证。
- 当前 EXE 未进行代码签名，Windows 可能显示“未知发布者”或 SmartScreen 提示。
- 使用的是 OpenAI 未公开的 `chatgpt.com/backend-api/wham/` 后端路径，没有长期兼容性承诺；接口变化时软件可能暂时无法刷新。
- 没有安装器、自动更新、Windows 10 支持、多账号、Token 自动刷新或多任务栏显示。

## 安装

下载 `CodexQuotaTaskbar-v0.1.1-win-x64.zip`，核对 Release 页面中的 SHA-256，解压后运行 `CodexQuotaTaskbar.exe`。目标电脑需安装 Microsoft Visual C++ 2015–2022 x64 运行库。

发布包包含正式 EXE、README、发行说明、MIT License、第三方声明和 nlohmann/json MIT License，不包含凭证、账号数据、响应转储、测试固件、参考仓库或构建缓存。
