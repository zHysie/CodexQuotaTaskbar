# CodexQuotaTaskbar v0.1.8

发布日期：2026-08-18

本版本新增「额度来源」切换：除继续支持 Codex（ChatGPT 登录）外，可以切换为智谱 GLM（Claude Code 配置）来源，在同一个任务栏窗口查看 GLM Coding Plan 的 5 小时与周额度。它保留 v0.1.7 的睡眠恢复宽限、TrafficMonitor 托管子窗口识别、稳定避让、开机启动重试、额度显示、设置格式和只读网络行为。

## 本次更新

- 右键菜单新增「额度来源」二选一子菜单：`Codex（ChatGPT 登录）` 与 `智谱 GLM（Claude Code 配置）`；任一时刻只显示一个来源，也只对激活来源发起网络请求，选择持久化保存。
- 智谱来源只读本机 Claude Code 设置文件（`%CLAUDE_CONFIG_DIR%\settings.json` 或 `%USERPROFILE%\.claude\settings.json`）中的 API Key 与服务站点两个键，典型场景为通过 cc-switch 等供应商切换工具配置的 GLM Coding Plan；文件变化自动重新读取，不修改该文件。
- 智谱额度查询仅访问 `https://open.bigmodel.cn/api/monitor/usage/quota/limit`（国际站自动路由 `api.z.ai`），沿用主机精确白名单、禁用重定向、证书校验、响应大小上限、有限超时与凭证短生命周期等全部既有安全约束；配置缺失、Key 失效或非智谱配置时显示脱敏状态。
- 智谱来源复用现有 `5h` / `1W` 两行三列渲染、单/双行模式、颜色告警与标签开关；Tooltip 标题为「智谱 GLM 额度」，显示重置倒计时并在服务端返回时附一行月度 MCP 用量。
- 切换来源立即触发新来源刷新；两个来源各自维护最后成功快照与失败退避计数，切回旧来源先显示其缓存数据。
- 配置结构升级为 `SchemaVersion=3`（新增 `ActiveProvider`）；旧配置缺省 Codex，行为与历史版本一致。

## 验证状态

本机 Windows 11 build `10.0.22631.6199`、96 DPI、单显示器 1920×1080、底部任务栏环境已完成：

- Visual Studio 2022 x64 Release 全量构建和 CTest 7/7；新增智谱解析（窗口分类、兜底启发式、越界拒绝、毫秒/秒重置时间、可选 MCP 用量）、智谱凭证读取（路径回退、站点映射、非智谱域名拒绝）、来源切换隔离（激活 Codex 时智谱端点零请求，反之亦然）与 GLM 状态文案断言全部通过。
- 使用应用自身代码只读完成智谱真实数据链路验证：真实读取本机 Claude 配置并真实请求智谱额度接口，HTTP 200、解析成功；多轮采样中 5 小时剩余百分比随真实消耗递减，行为与上游一致。诊断仅输出状态码与百分比，未输出 Key、请求头或响应正文。
- 真实桌面验证：智谱来源任务栏实际显示 `5h 62%` / `1W 95%`，与同时刻智谱接口真实数据一致；切回 Codex 来源显示 `5h --%` / `1W 69%`，与既有 Codex 行为一致；连续 6 次启动-优雅退出循环全部通过。
- `TaskbarProbe` 返回 `supported`；`TaskbarInteractionProbe` 21/21（含右键菜单弹出与前台恢复）；`TaskbarStabilityProbe` 19/19；`AppLifecycleProbe` 47 毫秒优雅退出。
- Windows 2022 / Visual Studio 2022 的构建、测试与打包以 v0.1.8 标签对应的 GitHub Actions 为最终门槛。

## 已知限制

- 智谱额度接口属于未公开后端接口（智谱官方 Claude Code 插件与 cc-switch 亦调用该路径），没有长期兼容性承诺；`percentage` 按“已用百分比”语义处理，该语义依据智谱官方插件与 cc-switch 双源实现交叉确认，尚未与 cc-switch 界面数值做人工逐点比对。
- 智谱团队版（需要组织/项目标识）暂不支持；同时显示两个来源、为智谱实现账号体系属于后续版本内容。
- 本版本未重新执行真实睡眠/唤醒、真实注销/登录、截图触发器、任务栏自动隐藏、Explorer 真实重启、具体游戏、125% / 150% / 200% DPI、双显示器、主显示器切换和干净 Windows 用户环境；这些项目只保留历史结果，不扩大 v0.1.8 支持承诺。
- 当前 EXE 未进行代码签名，Windows 可能显示“未知发布者”或 SmartScreen 提示。
- 使用的仍是未公开后端接口（`chatgpt.com/backend-api/wham/` 与智谱 `/api/monitor/usage/quota/limit`），没有长期兼容性承诺。
- 没有安装器、自动更新、Windows 10 支持、多账号、Token 自动刷新或多任务栏显示。

## 安装

可直接下载 `CodexQuotaTaskbar-v0.1.8-win-x64.exe`，也可以下载 `CodexQuotaTaskbar-v0.1.8-win-x64.zip` 后解压运行，并使用 `SHA256SUMS.txt` 核对两者。目标电脑需安装 Microsoft Visual C++ 2015–2026 x64 运行库。从旧版本升级时先退出软件，再解压并覆盖原文件；已有设置会自动迁移（额度来源默认 Codex）。

完整发布包只允许包含正式 EXE、README、发行说明、MIT License、第三方声明和 nlohmann/json MIT License，不得包含凭证、账号数据、响应转储、测试固件、参考仓库或构建缓存。
