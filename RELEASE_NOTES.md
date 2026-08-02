# CodexQuotaTaskbar v0.1.7

发布日期：2026-08-03

这是修复 Windows 睡眠恢复时任务栏结构暂时未就绪、程序误报“无法读取任务栏轻量结构签名”并退出的维护版本。它保留 v0.1.6 的 TrafficMonitor 托管子窗口识别、稳定避让、开机启动重试、额度显示、设置格式和只读网络行为。

## 本次更新

- 收到 Windows 睡眠恢复通知后增加 15 秒有界宽限，避免 Explorer 已恢复主任务栏、但通知区或任务按钮子树尚未稳定时立即升级为致命错误。
- 宽限期间保留现有任务栏子窗口，不隐藏、不销毁、不重新附着；同时清除休眠前遗留的结构变化和外部碰撞样本，避免旧样本参与恢复后的连续确认。
- 以 1 秒间隔等待恢复宽限结束；之后重新启用原有轻量校验、连续结构确认和最多 3 次有界硬重附着。真正持续存在的任务栏故障仍会给出原因并退出，不会被无限忽略。
- 新增恢复策略回归测试，覆盖 15 秒截止边界、宽限期内延后、截止后恢复正常校验和 1 秒复核间隔。

## 验证状态

本机 Windows 11 build `26200.8973`、96 DPI、单显示器 2560×1440、底部任务栏环境已完成：

- Visual Studio 2022 x64 Release 全量构建和 CTest 7/7；恢复策略以及原有启动、生产逻辑、布局、碰撞、渲染和解析测试全部通过。
- `TaskbarProbe` 返回 `supported`；运行中的 TrafficMonitor 仍被识别为外部障碍，额度窗口与其保持 8 像素安全间距。
- `TaskbarInteractionProbe` 21/21、`TaskbarStabilityProbe` 19/19 通过；Tooltip、右键菜单、无激活、全屏软事件、同一窗口稳定性和模拟 `TaskbarCreated` 行为正常。
- 受控投递 `PBT_APMRESUMEAUTOMATIC` 后连续 17 秒采样，进程数保持 1，额度窗口 HWND、父窗口和矩形均无变化。
- Windows 2022 / Visual Studio 2022 的构建、测试与打包以 v0.1.7 标签对应的 GitHub Actions 为最终门槛。

## 已知限制

- 本版本尚未执行真实睡眠/唤醒回归。受控恢复消息验证了新分支和窗口稳定性，但不能完整复现 Explorer 的真实恢复时序；因此不得把睡眠恢复写成已经通过。
- v0.1.5 的首次附着有界重试功能保留并通过自动化，但真实 Windows 注销/登录路径仍未完成端到端复核。
- 本版本未重新执行截图触发器、任务栏自动隐藏、Explorer 真实重启、具体游戏、125% / 150% / 200% DPI、双显示器、主显示器切换和干净 Windows 用户环境；这些项目只保留历史结果，不扩大 v0.1.7 支持承诺。
- 当前 EXE 未进行代码签名，Windows 可能显示“未知发布者”或 SmartScreen 提示。
- 使用的是 OpenAI 未公开的 `chatgpt.com/backend-api/wham/` 后端路径，没有长期兼容性承诺。
- 没有安装器、自动更新、Windows 10 支持、多账号、Token 自动刷新或多任务栏显示。

## 安装

可直接下载 `CodexQuotaTaskbar-v0.1.7-win-x64.exe`，也可以下载 `CodexQuotaTaskbar-v0.1.7-win-x64.zip` 后解压运行，并使用 `SHA256SUMS.txt` 核对两者。目标电脑需安装 Microsoft Visual C++ 2015–2026 x64 运行库。

完整发布包只允许包含正式 EXE、README、发行说明、MIT License、第三方声明和 nlohmann/json MIT License，不得包含凭证、账号数据、响应转储、测试固件、参考仓库或构建缓存。
