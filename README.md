# CodexQuotaTaskbar

在 Windows 11 任务栏中直接查看 Codex 剩余额度，无需打开网页或额外窗口。

<p align="center">
  <img src="assets/taskbar-display.png" alt="CodexQuotaTaskbar 在任务栏中显示额度" width="94">
</p>

> 百分比表示剩余额度。当前上游未返回 5 小时额度时，`5h` 会显示 `--%`，周额度仍可正常显示。

## 功能一览

- 原生嵌入 Windows 11 主任务栏，不使用悬浮窗
- 显示 5 小时额度与周额度，支持单行或上下两行
- 悬停查看重置时间、可用重置次数及最早到期时间、账号套餐和最后更新时间
- 右键即可刷新、调整显示内容、刷新间隔和颜色
- 支持当前 Windows 用户开机启动
- 只读使用现有 Codex 登录状态，不收集遥测

<p align="center">
  <img src="assets/taskbar-tooltip.png" alt="CodexQuotaTaskbar 悬停额度详情" width="250">
  <img src="assets/taskbar-menu.png" alt="CodexQuotaTaskbar 右键菜单" width="267">
</p>

## 下载与运行

### 方式一：直接下载 EXE

1. 点击 [直接下载 `CodexQuotaTaskbar-v0.1.1-win-x64.exe`](https://github.com/zHysie/CodexQuotaTaskbar/releases/download/v0.1.1/CodexQuotaTaskbar-v0.1.1-win-x64.exe)。
2. 将下载的 EXE 保存或移动到一个固定目录。
3. 双击运行，程序会直接出现在主任务栏中。

### 方式二：下载完整压缩包

1. 下载 [`CodexQuotaTaskbar-v0.1.1-win-x64.zip`](https://github.com/zHysie/CodexQuotaTaskbar/releases/download/v0.1.1/CodexQuotaTaskbar-v0.1.1-win-x64.zip)。
2. 解压到固定目录，不要直接在压缩包内运行。
3. 双击解压后的 `CodexQuotaTaskbar.exe`。

如需随 Windows 登录自动启动，在右键菜单中开启“开机启动”。

运行环境：Windows 11 x64，目标电脑需安装 Microsoft Visual C++ 2015–2026 x64 运行库。

v0.1.1 暂无安装器、自动更新和代码签名。Windows 可能显示“未知发布者”或 SmartScreen 提示，请只从本仓库 Release 下载并核对 SHA-256。

## 使用说明

默认上下两行显示：

```text
5h 85%
1W 72%
```

悬停会显示额度重置倒计时、可用重置次数及最早到期时间（本地时间，精确到分钟）、账号、套餐和最后成功更新时间；其余到期时间不展开。左键无操作；右键可立即刷新、切换显示模式、选择显示项目、修改刷新间隔和颜色、设置开机启动、重新检测任务栏或退出。

升级时先退出软件，再解压并覆盖原文件。卸载时先关闭“开机启动”并退出，然后删除程序目录；如需同时清除设置，可删除 `%APPDATA%\CodexQuotaTaskbar\`。

## 隐私与数据安全

程序按以下顺序只读查找登录文件：

1. `%CODEX_HOME%\auth.json`（仅当 `CODEX_HOME` 已设置）
2. `%USERPROFILE%\.codex\auth.json`

程序不写回或自动刷新 Token，不调用重置机会消费接口，不记录请求头、完整响应、Token 或邮箱，也不收集遥测。网络访问严格限制为：

```text
https://chatgpt.com/backend-api/wham/usage
https://chatgpt.com/backend-api/wham/rate-limit-reset-credits
```

这两个地址属于未公开后端接口，没有长期稳定性承诺；上游接口变化时，部分额度可能暂时显示为 `--%`。

## 支持范围与已知限制

v0.1.1 已验证基线为 Windows 11 x64、100% DPI、单显示器、底部非自动隐藏主任务栏。VS2022 x64 Release、CTest 6/6、任务栏交互探针 21/21、单实例、Explorer 重启恢复和清理退出均已通过。

补充真实验证覆盖 Windows build `10.0.26200.8737` 下的 150% DPI、双显示器扩展、主显示器切换和任务栏自动隐藏，以及 build `10.0.26200.8875` 下恢复为 100% DPI 单显示器后的完整交互。用户随后手动确认 125% / 200% DPI 与睡眠唤醒恢复均正常；干净 Windows 用户环境和其他 Windows 11 内部版本尚未完成真实验证。

## 从源码构建

需要 Visual Studio 2022 Build Tools、“使用 C++ 的桌面开发”、CMake 和 Windows SDK。在仓库根目录运行：

```powershell
.\build.cmd
```

正式程序位于 `build\Release\CodexQuotaTaskbar.exe`，构建脚本会同时运行全部 CTest。

## 许可证

项目采用 [MIT License](LICENSE)。第三方依赖与许可证见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
