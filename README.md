# CodexQuotaTaskbar

CodexQuotaTaskbar v0.1.1 是一个 Windows 11 x64 原生常驻工具。它读取当前 Windows 用户已有的 Codex 登录状态，只向 `chatgpt.com` 的两个固定只读额度路径发起 HTTPS 请求，并在当前主显示器的主任务栏内显示 5 小时和周剩余额度。

任务栏嵌入是唯一界面形态；无法安全识别连续空白区时，程序会提示原因并退出，不会降级为悬浮窗。

## 安装与运行

1. 从 GitHub Releases 下载 `CodexQuotaTaskbar-v0.1.1-win-x64.zip`。
2. 将 zip 解压到固定目录，不要直接在压缩包内运行。
3. 双击 `CodexQuotaTaskbar.exe`。程序没有安装向导或主窗口，启动后直接显示在主任务栏中。
4. 如果尚未登录 Codex，程序会显示“未登录”；完成 Codex 登录后可在右键菜单选择“立即刷新”。
5. 需要随 Windows 登录自动启动时，在右键菜单开启“开机启动”。移动 EXE 后，应重新关闭并开启一次该选项。

v0.1.1 没有安装器、自动更新和代码签名。Windows 可能显示“未知发布者”或 SmartScreen 提示；请只从本仓库 Release 下载并核对 SHA-256。目标电脑需安装 Microsoft Visual C++ 2015–2022 x64 运行库。

升级时先退出软件，将新版本解压并覆盖原文件，再重新运行。卸载时先关闭“开机启动”并退出，然后删除程序目录；如需同时清除设置，可删除 `%APPDATA%\CodexQuotaTaskbar\`。

## 显示与交互

默认上下两行显示：

```text
5h 85%
1W 72%
```

百分比表示剩余额度。悬停使用 Windows 原生 Tooltip 显示重置倒计时、可用重置次数及最早到期时间（本地时间，精确到分钟）、账号、套餐和最后成功更新时间；其余到期时间不展开。左键无操作；右键可立即刷新、切换单行/双行、开关显示项、修改刷新间隔和颜色、设置开机启动、重新检测任务栏或退出。

只显示一个额度时，单行和上下两行使用相同字号与固定三列布局；切换模式会在原窗口中立即重绘。

## 设置与数据安全

非敏感偏好保存在：

```text
%APPDATA%\CodexQuotaTaskbar\settings.ini
```

程序按以下顺序只读查找登录文件：

1. `%CODEX_HOME%\auth.json`（仅当 `CODEX_HOME` 已设置）
2. `%USERPROFILE%\.codex\auth.json`

程序不写回 Token、不自动刷新 Token、不调用重置机会消费接口、不记录请求头、完整响应、Token 或邮箱，也不收集遥测。网络访问严格限制为：

```text
https://chatgpt.com/backend-api/wham/usage
https://chatgpt.com/backend-api/wham/rate-limit-reset-credits
```

这两个地址属于未公开后端接口，没有长期稳定性承诺；接口变化时软件可能暂时无法刷新。

## 已验证范围

v0.1.1 已验证基线：Windows 11 x64、100% DPI、单显示器、底部非自动隐藏主任务栏。VS2022 x64 Release、CTest 6/6、任务栏交互探针 21/21、单实例、Explorer 重启恢复和清理退出均已通过。

补充真实验证覆盖 Windows build `10.0.26200.8737` 下的 150% DPI、双显示器扩展、主显示器切换和任务栏自动隐藏，以及 build `10.0.26200.8875` 下恢复为 100% DPI 单显示器后的完整交互。125% / 200% DPI、睡眠恢复、干净 Windows 用户环境和其他 Windows 11 内部版本尚未完成真实验证。

## 从源码构建

要求 Windows 11 x64、Visual Studio 2022 Build Tools、“使用 C++ 的桌面开发”、CMake 和 Windows SDK。在仓库根目录运行：

```powershell
.\build.cmd
```

正式程序位于 `build\Release\CodexQuotaTaskbar.exe`。构建脚本同时运行全部 CTest。

## 许可证

项目采用 [MIT License](LICENSE)。第三方依赖与许可证见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
