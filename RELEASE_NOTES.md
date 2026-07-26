# CodexQuotaTaskbar v0.1.2

发布日期：2026-07-27

这是针对任务栏显示位置跳动、短暂消失，以及全屏游戏或截图时误触发重挂的稳定性修复版本。它继续保持只读访问 Codex 额度、不写回 Token、不消费重置机会的安全边界。

## 本次修复

- 外部任务栏障碍只从桌面顶层窗口采样，排除子窗口、DWM 隐藏窗口、Tooltip、菜单、全屏覆盖窗口、异常几何和本程序窗口，避免把游戏或截图工具的底部子窗口误判为 TrafficMonitor 一类任务栏工具。
- `SHOW`、`HIDE`、`LOCATIONCHANGE`、显示配置消息和睡眠恢复只触发轻量复核，不再立即隐藏并销毁额度窗口。
- TrafficMonitor 或系统托盘内容引起的通知区边界伸缩按软安全边界处理，不再误判为 Shell 结构重建；边界扩张时保持当前位置，持续挤压时才原位避让。
- 检测到前台根窗口覆盖主显示器时暂停软布局调整；额度窗口保持原句柄和位置，任务栏自动隐藏时随任务栏隐藏和恢复，后台额度刷新继续运行。
- 当前窗口位置安全时始终保持不动，即使外部工具缩小或短暂消失也不向右抢位。只有连续 3 次、每次间隔 2 秒都确认真实碰撞时，才用同一 HWND 向最近的左侧安全位置移动一次；小于 2 DIP 的变化按抖动忽略。
- 只有 Explorer 的 `TaskbarCreated`、宿主或父子关系失效，以及连续确认的真实任务栏/DPI 结构变化才进入有界硬重附着；硬重探测前仍先销毁旧子窗口，最多重试 3 次。
- 新增任务栏稳定性探针，用于采样 HWND、父子关系、坐标和可见性，并自动复现“全屏窗口＋底部子窗口”场景。

## 验证结果

- Windows build `10.0.26200.8894`、96 DPI、单显示器 2560×1440、底部任务栏环境下，Visual Studio 2022 x64 Release 构建与 CTest 6/6 通过；碰撞测试 27/27，`TaskbarProbe` 返回 `supported`。
- 自动全屏复现完成 10/10 轮、每轮 19 项检查；PrintScreen、SnippingTool Ctrl+N 真实截图遮罩和 Edge F11 各完成 10/10 轮，期间额度窗口 HWND 与坐标保持稳定。
- TrafficMonitor PID `18896` 连续观察 300 秒、采样 9662 次，任务按钮增减观察 18 秒、采样 580 次；两组 HWND、父子关系、坐标和可见性异常计数均为 0。
- 任务栏自动隐藏后，同一 HWND 在原坐标 69 毫秒内恢复。Explorer 从 PID `16988` 重启为 `43760` 时，应用 PID `38596` 和实例数 1 保持不变，6.654 秒内只产生一次新 HWND。
- `TaskbarInteractionProbe` 21/21 通过；`AppLifecycleProbe` 确认应用在 203 毫秒内清理退出。

## 已知限制

- 本机 `ms-screenclip` 关联损坏，Win+Shift+S 会弹出“打开方式”，因此该快捷键直接路径未通过；本轮以 SnippingTool Ctrl+N 验证真实截图遮罩行为。
- 本轮未指定或验证具体游戏；125% / 150% / 200% DPI、双显示器和睡眠恢复未重新复核，只保留 v0.1.1 的历史结果。
- 干净 Windows 用户环境及其他 Windows 11 内部版本尚未完成真实验证。
- 当前 EXE 未进行代码签名，Windows 可能显示“未知发布者”或 SmartScreen 提示。
- 使用的是 OpenAI 未公开的 `chatgpt.com/backend-api/wham/` 后端路径，没有长期兼容性承诺；接口变化时软件可能暂时无法刷新。
- 没有安装器、自动更新、Windows 10 支持、多账号、Token 自动刷新或多任务栏显示。

## 安装

正式发布后可直接下载 `CodexQuotaTaskbar-v0.1.2-win-x64.exe`，也可以下载 `CodexQuotaTaskbar-v0.1.2-win-x64.zip` 后解压运行，并使用 `SHA256SUMS.txt` 核对两者。目标电脑需安装 Microsoft Visual C++ 2015–2026 x64 运行库。

完整发布包包含正式 EXE、README、发行说明、MIT License、第三方声明和 nlohmann/json MIT License，不包含凭证、账号数据、响应转储、测试固件、参考仓库或构建缓存。
