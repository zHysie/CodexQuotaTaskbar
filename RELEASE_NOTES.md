# CodexQuotaTaskbar v0.1.4

发布日期：2026-07-28

这是增加“单额度隐藏标识”选项的维护版本。它保持固定列、任务栏宿主和只读额度接口不变。

## 本次更新

- 在右键菜单「显示内容」中新增「单项时显示标识（5h / 1W）」，默认开启。
- 只显示 5 小时额度或周额度时，可关闭该选项并隐藏 `5h` / `1W`；只跳过标签绘制，数值与 `%` 保持原固定列，不居中、不改变窗口宽度或 HWND。
- 同时显示两个额度时菜单项置灰，两个标签始终显示；隐藏偏好会保存，重新切回单额度时继续生效。
- 设置文件升级为 `SchemaVersion=2` 并新增 `ShowSingleQuotaLabel`；旧版配置缺少该键时默认显示标签，保持原有外观。
- DirectWrite 与 GDI 降级路径使用相同判定；状态文案、Tooltip、告警颜色和失败 `!` 标记不变。

## 验证状态

本机 Windows 11 build `22631.6199`、96 DPI、单显示器环境已完成：

- Visual Studio 2026 x64 Release 全量构建和 CTest 6/6；Schema 1/2、持久化、单双额度、`--%`、`100%`、告警及 DirectWrite/GDI 共用规则均有自动化覆盖。
- TaskbarProbe `supported`，稳定性探针连续 10 次 19/19，180 秒观察 5748 个样本无 HWND、父窗口、位置、可见性或几何异常，交互探针 21/21。
- 单周、单 5 小时、双额度、单双行、菜单状态、同一 HWND 原位重绘和重启持久化真实通过；真实接口数值随当时额度变化，`72%` / `85%` 示例由自动化覆盖。
- 自动全屏、PrintScreen、Edge F11 各 10/10；Win+Shift+S 直接路径 8/10，失败的 2 轮由 SnippingTool Ctrl+N 真实截图遮罩补测通过。任务栏自动隐藏和 Explorer 真实重启通过。
- 生命周期探针确认应用在 62 毫秒内清理退出且额度窗口消失；随后已恢复任务开始前运行的 v0.1.2。

本机未安装 TrafficMonitor，产品负责人明确豁免 v0.1.4 的本轮复核；本版本不据此扩大 TrafficMonitor 兼容性承诺。Windows 2022 / Visual Studio 2022 的构建与测试由正式标签对应的 GitHub Actions 作为最终门槛。

v0.1.3 的 CTest 6/6、碰撞测试 32/32、真实通知区复位与正式 Release 已完成，只作为历史回归基线；v0.1.2 已完成的全屏、截图、TrafficMonitor、任务栏自动隐藏、Explorer 重启、交互和生命周期验证同样不得冒充 v0.1.4 结果。

## 已知限制

- 本机 `ms-screenclip` 关联不稳定；本轮 Win+Shift+S 直接路径 8/10，另外 2 轮弹出“打开方式”，已使用 SnippingTool Ctrl+N 完成真实截图遮罩补测。
- v0.1.4 未重新运行 TrafficMonitor，相关兼容性只保留 v0.1.2 的历史验证结果。
- 具体游戏、125% / 150% / 200% DPI、双显示器、睡眠恢复、干净 Windows 用户环境及其他 Windows 11 内部版本，只有在 v0.1.4 实际复核后才能扩大支持承诺。
- 当前 EXE 未进行代码签名，Windows 可能显示“未知发布者”或 SmartScreen 提示。
- 使用的是 OpenAI 未公开的 `chatgpt.com/backend-api/wham/` 后端路径，没有长期兼容性承诺。
- 没有安装器、自动更新、Windows 10 支持、多账号、Token 自动刷新或多任务栏显示。

## 安装

可直接下载 `CodexQuotaTaskbar-v0.1.4-win-x64.exe`，也可以下载 `CodexQuotaTaskbar-v0.1.4-win-x64.zip` 后解压运行，并使用 `SHA256SUMS.txt` 核对两者。目标电脑需安装 Microsoft Visual C++ 2015–2026 x64 运行库。

完整发布包只允许包含正式 EXE、README、发行说明、MIT License、第三方声明和 nlohmann/json MIT License，不得包含凭证、账号数据、响应转储、测试固件、参考仓库或构建缓存。
