# CodexQuotaTaskbar v0.1.6

发布日期：2026-07-31

这是修复 TrafficMonitor 任务栏窗口识别遗漏的维护版本。它保留 v0.1.5 的开机启动有界重试、额度显示、设置格式和只读网络行为。

## 本次更新

- 修复 TrafficMonitor 使用“隐藏顶层载体 + 可见任务栏子窗”结构时，额度窗口未将其识别为外部障碍并发生重叠的问题。
- 障碍采样继续枚举有效桌面顶层窗口，同时只额外接纳由主任务栏托管、属于外部进程且实际落在安全区内的可见子窗口。
- 普通应用子窗口、全屏根窗口及其子窗口、截图遮罩、Tooltip、菜单、DWM cloaked 窗口、Explorer 和本程序窗口仍会排除，避免扩大误判范围。
- 保持原稳定策略：连续 3 次真实碰撞才使用同一 HWND 向左避让；TrafficMonitor 消失不触发向右回抢。
- 新增回归测试，分别覆盖普通子窗口排除和任务栏托管外部子窗口接纳。

## 验证状态

本机 Windows 11 build `26200.8973`、144 DPI、单显示器 2560×1600、底部任务栏环境已完成：

- Visual Studio 2022 x64 Release 全量构建和 CTest 7/7；v0.1.5 启动附着策略及原有生产逻辑、布局、碰撞、渲染和解析测试全部通过。
- `TaskbarProbe` 返回 `supported`，识别到 TrafficMonitor 外部障碍 1 个。额度窗口矩形为 `[1676,1533,1802,1595]`，TrafficMonitor 为 `[1814,1540,1965,1588]`，中间保留 12 像素安全间距。
- `TaskbarInteractionProbe` 21/21 通过；Tooltip、右键菜单、无激活、同一窗口重绘和模拟 `TaskbarCreated` 行为正常。
- 最终 300 秒 TrafficMonitor 共存观察采样 9633 次，HWND、父窗口、可见性、坐标和几何异常均为 0。此前一次观察记录到通知区扩张/收缩引起的同 HWND 左移与受限复位；探针已增加边界记录和规则测试，只允许复位量不超过通知区收缩量的纯水平移动。
- Windows 2022 / Visual Studio 2022 的构建、测试与打包以正式标签对应的 GitHub Actions 为最终门槛。

## 已知限制

- v0.1.5 的首次附着有界重试功能保留并通过自动化，但真实 Windows 注销/登录路径仍未完成端到端复核；该项继续作为已接受但未实测的风险。
- 本轮 PrintScreen 与 Edge F11 的首轮各完成 10/10；两次截图遮罩组合复跑中，产品 HWND、坐标和可见性没有失败，但 SnippingTool 触发器分别只完成 7/10、9/10，第二次的遗留截图窗口还使 Edge F11 只完成 9/10。产品负责人随后要求停止追加测试并直接发布，因此截图触发器、任务栏自动隐藏和 Explorer 重启不记为 v0.1.6 通过，只保留历史结果。
- 具体游戏、125% / 150% / 200% DPI、双显示器、主显示器切换、睡眠恢复、干净 Windows 用户环境及其他 Windows 11 内部版本仍未逐项验证。
- 当前 EXE 未进行代码签名，Windows 可能显示“未知发布者”或 SmartScreen 提示。
- 使用的是 OpenAI 未公开的 `chatgpt.com/backend-api/wham/` 后端路径，没有长期兼容性承诺。
- 没有安装器、自动更新、Windows 10 支持、多账号、Token 自动刷新或多任务栏显示。

## 安装

可直接下载 `CodexQuotaTaskbar-v0.1.6-win-x64.exe`，也可以下载 `CodexQuotaTaskbar-v0.1.6-win-x64.zip` 后解压运行，并使用 `SHA256SUMS.txt` 核对两者。目标电脑需安装 Microsoft Visual C++ 2015–2026 x64 运行库。

完整发布包只允许包含正式 EXE、README、发行说明、MIT License、第三方声明和 nlohmann/json MIT License，不得包含凭证、账号数据、响应转储、测试固件、参考仓库或构建缓存。
