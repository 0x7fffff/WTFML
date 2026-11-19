# WTFML 智能体（Agent）

本文给开发和测试看智能体当前已经实现的行为、数据边界和代码位置。聊天底座细节见 [AGENT_STAGE1A.md](AGENT_STAGE1A.md)，受控文件工具见 [AGENT_STAGE1B.md](AGENT_STAGE1B.md)，安全边界见 [SECURITY_BOUNDARIES.md](SECURITY_BOUNDARIES.md)，未完成项见 [ROADMAP.md](../ROADMAP.md)。

## 1. 当前范围

智能体是启动器内置的实例维护助手。当前已实现：

- 自由对话：Provider、流式回复、会话、停止/重试、会话恢复、整会话加密。
- 锁定实例内的受控文件工具：目录元数据、完整文件名清单、文本读取、文本配置写入、模型 artifact 读取。
- 逐次审批、D11 外发策略、实例锁定、维护备份和工具结果卡片。

未实现：维护工作流、MCP、联网搜索/抓取、诊断命令、任意命令。下面只描述已经存在的行为。

## 2. 用户可见行为

- 导航第五项「智能体」，启动仍停在主页。
- 左侧会话栏：新建、选择、搜索过滤、重命名、固定、删除；标题取第一条用户消息，超长留前 23 字加「…」。
- 聊天顶行：会话标题、API 状态胶囊、实例胶囊、`⋯` 菜单、API 设置。`⋯` 菜单含重命名、固定、加密存储、导出 Markdown、删除。
- 聊天区：流式正文、推理内容折叠、停止、错误/中断提示、重试；Markdown 只允许白名单标签，原始 HTML/script/iframe/远程图片不执行，链接不自动打开。
- 输入区：Enter 发送、Shift+Enter 换行；流式中发送变停止；切换会话、切到别的选项卡、关窗都取消在飞请求。
- 加密存储：会话 `⋯` 菜单打开；正文改成 DPAPI `.enc`，标题和时间仍为明文。
- 文件工具：只对锁定实例生效。工具卡显示待执行/执行中/完成/失败；需要读取配置、完整文件名或写入时弹审批卡，显示目标、摘要、diff/预览、备份提示和外发说明。
- 实例胶囊：未锁定时跟随主页选中实例；首次实例工具调用后锁定；换游戏文件夹或实例删除显示不可用；显式切换需确认。
- 严格模式：设置页打开后，自动读取类别也逐次确认。

## 3. 架构与代码

| 层 | 文件 |
| --- | --- |
| 值类型/会话 | `src/agent/AgentTypes.*`、`AgentSession.*` |
| 设置/路径 | `src/agent/AgentSettings.*`、`AgentPaths.*` |
| 凭据 | `src/agent/AgentSecretStore.*`、`src/util/SecretProtector.*` |
| 会话存储/加密 | `src/agent/AgentSessionStore.*`、`AgentSessionCrypto.*` |
| Redaction/egress | `src/agent/AgentRedaction.*`、`AgentEgressPolicy.*` |
| Provider | `src/agent/AgentModelClient.hpp`、`OpenAiCompatibleClient.*`、`AgentProviderAdapters.*`、`AgentSseParser.*` |
| 上下文/Loop | `src/agent/AgentContextBuilder.*`、`AgentLoop.*` |
| 工具 | `src/agent/AgentTool.hpp`、`AgentToolRegistry.cpp`、`AgentFileToolEngine.*`、`AgentFileTools.*` |
| 路径/实例锁 | `src/agent/AgentWinPath.*`、`AgentInstanceLock.*` |
| artifact/备份 | `src/agent/AgentArtifactStore.*`、`AgentBackupIndex.*` |
| 接线 | `src/controllers/AgentController.*` |
| UI | `ui/AgentPage.*`、`AgentMessageView.*`、`AgentComposer.*`、`AgentSettingsPage.*`、`AgentSecretEdit.*`、`AgentMarkdown.*`、`AgentToolCard.*`、`AgentApprovalCard.*`、`AgentViewTypes.hpp` |

依赖规则：

- `ui/Agent*` 只 include `ui/` 与值类型，不 include `src/`。
- `src/agent/` 不 include `ui/` 或 `MainWindow`；业务能力通过工具宿主/控制器接入。
- 模型网络只走 `AgentModelClient`；凭据只走 `AgentSecretStore`；会话落盘只走 `AgentSessionStore`。

## 4. 会话与存储

默认数据目录 `%LOCALAPPDATA%\WTFML\agent`。测试用 `QCoreApplication::setProperty("wtfml.testAgentDataDir", <临时目录>)` 隔离。

```text
agent/
  sessions/
    <uuid>.meta.json        # 明文元数据：标题/时间/固定/加密标志/bodySha256
    <uuid>.json             # 默认明文正文
    <uuid>.enc              # 可选整会话 DPAPI 密文
    .recovery/<uuid>/...    # 最近 3 份稳定点快照
  artifacts/<uuid>/         # 模型可读/本地原始 artifact
  backups/maintenance/      # 文件维护备份
```

- 正文先写、元数据最后写，元数据带 `bodySha256`；两次提交之间崩溃时以正文为准重算元数据。
- 正文损坏改名为 `<id>.json.corrupt-<时间戳>`，列表显示「损坏，已隔离」，不当空会话覆盖。
- 单会话正文上限 8 MB / 2000 条消息；超限停止保存并提示导出或新建会话。
- 启动时 `streaming` 消息恢复成 `interrupted`，不自动重发。
- 删除先登记 `deletionState=pending`，再删正文/快照/artifacts/元数据；活动请求先取消；不删文件维护备份。
- 会话默认明文，不含密钥；整会话加密只保护落盘，不改变发送给模型的内容。

## 5. 上下文与 Loop

- 上下文预算保守估算 token：CJK 按 1，其他字符按 0.3；输出预留扣掉后再裁剪。
- 裁剪从最新消息往前装，保留最后一条用户消息；assistant/tool 配对不拆开；被省略的旧消息位置插入「更早的 N 条已省略」。
- 同一会话同时最多一个模型请求和一个工具；工具串行执行。
- 模型返回 `tool_calls` 后逐个校验名称/参数；需要审批时进入 `AwaitingApproval`，用户拒绝时以 `ApprovalDenied` 工具结果回灌。
- 首字节前网络错误/429/5xx 最多重试 2 次；流开始后失败保留已收到文本并标「已中断」，不自动重发。
- 工具线上名使用 `^[A-Za-z0-9_-]+$`；内部 `fs.read` 映射为 `fs_read`，模型回调时再映射回内部名。

## 6. Provider 与网络

- OpenAI-compatible 基础兼容子集：`POST <base>/chat/completions`；`messages`、`tools`、`tool_choice:auto`；流式 SSE 或非流式 JSON。
- 协议适配项只允许枚举：认证头、流式开关、`max_tokens` / `max_completion_tokens`、system/developer role、reasoning 回传、temperature 省略、tool_choice 省略。
- Base URL 只接受 `https://`；loopback `http://` 需显式勾选并持续提示；拒绝 userinfo、片段、查询串。
- 不跟随重定向，不忽略 TLS 证书错误；API Key 只进认证头，不进 URL、日志、会话或错误。
- 响应总额 16 MB、SSE 单行 1 MB；非 2xx 的供应商 JSON 错误体保留并显示安全摘要。
- 不支持：OpenAI Responses API、Anthropic content blocks、任意 header/URL 模板、请求体脚本、响应正则、供应商 SDK 插件。
- 网络错误矩阵仍待人工验收，见 [ACCEPTANCE_LOG](../archive/ACCEPTANCE_LOG.md) 的「延后：网络错误」。

## 7. 凭据

- API Key 只落 Windows 凭据管理器，主路径不可用时用 DPAPI 后备；两条路都失败拒绝保存并保留旧值，不降级为明文或 QSettings 明文。
- 界面默认星号遮挡，不主动回显；已保存时输入框留空，占位文字说明「留空不修改」。
- Key 不写日志、会话 JSON、错误卡片或导出文件。
- 换 Windows 用户或换机器后凭据读不到，需要重新输入。

## 8. 数据外发与脱敏

所有进入模型请求的文件、日志、工具结果先过 `AgentEgressPolicy`，再按需过 `AgentRedaction`。

### 8.1 外发边界

| 数据类型 | 默认行为 |
| --- | --- |
| 日志、崩溃报告、锁定实例元数据、目录元数据、全局实例总数 | 自动读取；首次使用提示「可能发送给当前模型服务」 |
| 配置文件、完整文件名清单、其他文本、全局实例完整列表 | 逐次确认，确认后只发脱敏片段；没有会话级目录放行 |
| 凭据文件、凭据存储、二进制内容 | 硬阻断，不读取、不发送 |
| 严格模式 | 自动读取类别改为逐次确认；硬阻断不变 |

- 自动读取仍受大小、截断、脱敏和上下文预算限制。
- 每次读取/写入审批都重新判断，不因先前审批豁免。
- 本地模型与云端模型在设置页分开说明；WTFML 不做遥测。

### 8.2 当前脱敏方案

`AgentRedaction` 以内容为主、文件名为辅：

- 键名切成词根：`_`、`-`、`.` 断开，小写/数字后紧跟大写再切一刀；`PASSWORD` 这类全大写串不拆词。
- 任一相邻词根属于 `password`、`passwd`、`pwd`、`secret`、`token`、`credential`、`credentials`、`cookie`、`csrf`、`session`、`apikey`，或相邻两个词根组成 `api/key`、`key/api`、`private/key`、`access/key`，就把值替换成 `<redacted>`，保留键名。
- 值少于 6 个字符的字段不遮，避免把 `password = true` 这类说明文字误伤。
- 固定形态同时命中：`Authorization: Bearer …`、`Bearer …`、`sk-…`、JWT（`eyJ…`）、AWS `AKIA…`、PEM 私钥块。
- 配置、其他文本只发脱敏片段；日志/崩溃报告自动读取时也先脱敏，命中处数在工具卡显示。
- 凭据文件名按 dotenv 家族（`.env`、`*.env`、`*.env.*`）、证书后缀（`pem/key/p12/pfx/jks`）和 `credential`/`secret`/`token`/`apikey`/`api_key`/`api-key` 词根判定；不含这些词根的文件名进入普通文本审批。

限制：词根表之外的自造字段名仍要靠固定令牌形态兜底；日志里的用户名、启动参数回显、绝对路径和硬件指纹留给后续日志/诊断字段投影。

## 9. 文件工具与审批

- 工具只接受相对路径；实例来自会话 `instanceLock`，不接受模型传入的实例 id。
- `fs.list` 只发目录元数据，不返回文件名；`fs.listNames` 每次审批。
- `fs.read` 默认 256 KB、最大 1 MB；NUL/控制字符判二进制；凭据文件硬阻断；返回 sha256 + `fileIdentityToken`。
- `fs.write` 只开放 `config/**`、`defaultconfigs/**`、`options.txt`；必须带旧哈希和身份 token；写前 DPAPI 备份、原子替换、写后回读；对象/内容变化拒绝。
- 路径校验：字符串拒绝绝对路径、`..`、盘符、UNC、ADS、保留名、尾点/空格、控制字符；已存在路径再用 `CreateFileW` + `GetFinalPathNameByHandleW` + `FileIdInfo` + `FileAttributeTagInfo` 核最终路径、卷序列/文件 ID、reparse。句柄校验降低 TOCTOU 竞争窗口，不宣称彻底解决。
- 保护目录 `versions/**`、`libraries/**`、`assets/**`、`java/**`、`Servers/**` 原始写拒绝。
- 模型可读 artifact 只存脱敏内容并 DPAPI 加密；本地原始存档 id 不在模型可见空间；`artifact.read` 每次重新脱敏。
- 大文本 `truncated=true` 时，artifact 只保存本次读取窗口（默认 256 KB、最大 1 MB），不是整个原文件。
- 审批请求持久化但不存「已批准」布尔；重启后作废并重新请求。
- 维护备份 `owner=agent`，元数据保留 `sourceSessionId`；删除会话不删备份；`owner=launcher/user/shared` 不允许 Agent 清理。

## 10. 失败与限制

- 网络、协议、配额、取消、审批拒绝、工具错误都在对话里显示分类、人话和下一步；不伪造成功。
- 文件对象在提交前变化、路径越界、reparse/junction、凭据文件、二进制内容都明确阻断。
- 活动会话删除先取消/等待；文件提交阶段拒绝删除并提示。
- 未实现阶段见 [ROADMAP.md](../ROADMAP.md)：维护工作流、MCP、联网工具、诊断命令。

## 11. 测试入口

- `verify_agent`：SSE、适配、会话、加密、凭据、上下文、Loop、Markdown、路径、外发、审批、artifact、备份、文件工具。
- `ui_check_agent`：智能体页、设置页、工具卡、审批卡、加密开关、导航与布局。
- `mw_check_agent`：真 `MainWindow` + 假模型，覆盖发送→流式→完成、工具审批、实例锁定、停止/删除。
- 命令、模块边界和超时见 [docs/dev/TESTING.md](../dev/TESTING.md)。

## 12. 未覆盖

| 项 | 状态 |
| --- | --- |
| 网络错误矩阵（401/403、429、断网、流中断、重定向、TLS、代理、400 错误体） | 待人工执行 |
| 真实文件系统竞争/杀毒软件占用 | 未覆盖 |
| 真机高 DPI/读屏 | 未覆盖 |
| 日志字段投影（用户名/参数/路径/硬件指纹） | 留给后续阶段 |
