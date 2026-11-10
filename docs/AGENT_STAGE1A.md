# 智能体 Stage 1A 实现与验收

> 日期：2026-11-10  
> 范围：Provider + Loop + 会话 + 基础聊天 UI（**无工具对话**）。  
> 状态：**自动验收通过；2026-11-10 人工 G1A 除网络错误外全部通过。**  
> 网络错误延后：[NETWORK_ERROR_ACCEPTANCE.md](NETWORK_ERROR_ACCEPTANCE.md)。  
> 设计原文：[Agent.md](../Agent.md)；当前功能边界以 [FEATURES.md](../FEATURES.md) 为准。

## 1. 这一阶段做了什么

用户可以：

- 在导航第五项「智能体」里新建/选择/重命名/固定/删除会话；
- 在「API 与模型设置」里配置 OpenAI-compatible provider、模型、上下文/输出上限、
  超时、部署类型和枚举化协议适配项；
- 用 Windows 凭据管理器保存 API Key；主路径不可用时用 DPAPI 后备；两条路都失败时
  **拒绝保存并保留旧值**；
- 发送消息，接收流式正文/推理内容，停止、重试，切换会话或离开页面时取消在飞请求；
- 关闭重开后恢复会话、标题、固定状态和整会话加密状态；
- 可选把单个会话正文加密成 `.enc`（DPAPI，绑定当前 Windows 用户/机器）。

这一阶段**没有**做：任何文件/日志/实例工具、审批卡、MCP 进程、技能工作流状态机、
联网搜索/抓取、命令执行。`instanceLock` 字段先留空，实例芯片只显示“跟随主页：未选择实例”。

## 2. 代码位置

| 层 | 文件 |
| --- | --- |
| 值类型/枚举 | `src/agent/AgentTypes.*`、`src/agent/AgentSession.*` |
| 设置（非密） | `src/agent/AgentSettings.*` |
| 路径 | `src/agent/AgentPaths.*` |
| 凭据 | `src/agent/AgentSecretStore.*`、`src/util/SecretProtector.*` |
| 会话落盘/加密 | `src/agent/AgentSessionStore.*`、`src/agent/AgentSessionCrypto.*` |
| 脱敏 | `src/agent/AgentRedaction.*` |
| Provider | `src/agent/AgentModelClient.hpp`、`src/agent/OpenAiCompatibleClient.*`、`src/agent/AgentProviderAdapters.*`、`src/agent/AgentSseParser.*` |
| 上下文/Loop/技能骨架 | `src/agent/AgentContextBuilder.*`、`src/agent/AgentLoop.*`、`src/agent/AgentSkill.*` |
| 接线 | `src/controllers/AgentController.*` |
| UI | `ui/AgentPage.*`、`ui/AgentMessageView.*`、`ui/AgentComposer.*`、`ui/AgentSettingsPage.*`、`ui/AgentSecretEdit.*`、`ui/AgentMarkdown.*`、`ui/AgentViewTypes.hpp` |
| 入口 | `ui/MainWindowUI.*` 第五个选项卡；`src/MainWindow.*` 构造/析构接线 |

**依赖边界**：`ui/Agent*` 不 include `src/`；它只认 `ui/AgentViewTypes.hpp` 里的 DTO。
`AgentController` 是唯一把核心层消息/元数据/profile 翻译成 DTO 的地方。

## 3. 盘上格式

默认数据目录：`%LOCALAPPDATA%\WTFML\agent`。测试用
`QCoreApplication::setProperty("wtfml.testAgentDataDir", <临时目录>)` 隔离。

```text
agent/
  sessions/
    <uuid>.meta.json        # 明文元数据（标题/时间/固定/加密标志/bodySha256/...）
    <uuid>.json             # 默认明文正文
    <uuid>.enc              # 可选整会话 DPAPI 密文（WTFML-AGENT-SESSION-1\n + 密文）
    .recovery/<uuid>/...    # 最近 3 份稳定点快照
  artifacts/<uuid>/         # 本阶段不写；删除会话时按目录清理
  backups/maintenance/      # Stage 1B/2 使用
  mcp/                      # Stage 2 使用
```

- 正文先写、元数据最后写；元数据带 `bodySha256`。两次提交之间崩溃时以正文为准重算元数据。
- 正文损坏会改名为 `<id>.json.corrupt-<时间戳>`，列表显示“损坏，已隔离”，不当作空会话覆盖。
- 会话正文上限 8 MB / 2000 条消息；超限停止保存并在界面提示。
- 启动时 `streaming` 消息恢复成 `interrupted`，不自动重发。
- 删除先登记 `deletionState=pending`，再删正文/快照/artifacts/元数据；活动请求先取消。
  **不删除文件维护备份**（Stage 1B 起才有）。

## 4. 网络与凭据边界

- 只接受 `https://`；`http://` 只允许 loopback 且用户显式勾选“允许本地明文 HTTP”。
- 拒绝 userinfo、片段、查询串；不跟随重定向；不忽略 TLS 错误。
- API Key 只进认证头（`Authorization: Bearer` 或固定 `api-key`），不进 URL/日志/会话/错误。
- 响应总额上限 16 MB；SSE 单行上限 1 MB。
- 429/5xx/首字节前网络错误最多重试 2 次；流已开始后失败保留部分文本、不自动重发。
- UI 消息先过 `AgentRedaction`；命中疑似凭据时在本机遮挡后再落盘/发送，并提示用户。
- `AgentSecretStore` 测试钩子只通过 QCoreApplication 属性启用，产品入口不设置。

## 5. 自动验收记录（2026-11-10 本机）

构建目录：`build/verify`（MinGW 13.1 / Qt 6.10.1，Release）。

| 套件 | 命令 | 结果 |
| --- | --- | --- |
| 核心 A 组 | `ctest -R ^wtfml.core-agent$` | **145/145** |
| 智能体 UI | `ctest -R ^wtfml.ui-agent$` | **28/28** |
| 真主窗口 + 假模型 | `ctest -R ^wtfml.main-window-agent$` | **22/22** |
| 既有 UI 回归 | `ctest -R ^wtfml.ui$` | 通过（五页导航/标题栏 7 颗按钮断言已更新） |
| 既有主窗口回归 | `ctest -R ^wtfml.main-window$` | 通过 |
| 凭据保护故障注入 | `ctest -R ^wtfml.token-protection-fault$` | 通过（`TokenStore` 已改为委托 `SecretProtector`） |
| 核心基线 | `ctest -R ^wtfml.core$` | 通过 |
| 工具链 | `test-modules` / `test-runner` / `ci-partitions` | 通过 |
| 产品构建 | `cmake --build build/agent-product --target WTFMinecraftLauncher` | 通过 |

覆盖点：

- SSE 分块边界、CRLF、注释、`[DONE]`、多行 data、UTF-8 拆包、超长行。
- 协议适配：认证头、流式开关、`max_tokens` 字段、system role、reasoning 回传、
  temperature/tool_choice 省略；URL 规则。
- 会话：元数据/正文分离、往返、bodySha256、恢复快照最多 3 份、损坏隔离、2000 条上限、
  pending 删除。
- 加密：明文正文删除、密文不含明文、坏密文拒绝且不回退、关闭加密恢复明文。
- 凭据：DPAPI 后备往返、盘上无明文、两条路失败拒绝保存且旧值保留、清除。
- 上下文：CJK 估算、裁剪、最后一条保留、省略标记。
- Loop：完成、首字节前重试、部分流不重发、取消、无 key 不发请求、重试不重复 user。
- UI：五页导航、双栏几何、窄页面侧栏 216、空态、发送/停止、消息 Markdown、
  script/img 不执行、Key 默认遮挡/眼睛、URL 预览、协议适配折叠、外发说明、无 MCP 栏。
- 真主窗口：页面/控制器存在、假模型发送→流式→完成、会话落盘、标题、会话目录 grep 不到
  API Key、停止后中断落盘、shutdown 不 busy。

**没有跑 / 不能算通过**：真实第三方模型服务、真实 Windows 凭据管理器主路径（自动测试强制走
DPAPI 后备）、TLS/代理/429 真实网络、真机高 DPI/读屏、双击产品走一遍聊天。见 G1A 清单。

## 6. G1A 人工审查清单（2026-11-10 结果）

- [x] 明文 key 不出现在日志/会话/错误/导出/测试输出（人工已确认）。
- [x] Windows 凭据管理器主路径可用；DPAPI 后备只在主路径失败时写入；两者都失败拒绝保存。
  （主路径人工确认；后备/双失败由自动测试覆盖。）
- [x] 密码输入默认遮挡；眼睛只在用户操作期间显示明文；失焦/保存后恢复。
- [x] 非 https 远程 Base URL 被拒绝；loopback HTTP 需显式确认并持续提示。
- [x] key 不进 URL；重定向/证书错误检查**延后**到网络错误验收，不写成已通过。
- [x] 会话默认明文，整会话加密可选且失败不半加密；换用户/机器显示无法解密。
  （2026-11-10 补：加密开关已移到聊天头部下方单独一行，显示当前明文/加密状态。）
- [x] 严格 G1A 构建（`27070e5`）没有文件工具/MCP 进程；模型不能修改系统提示/API 配置。
- [x] 真机走一遍新建会话→发送→停止→重开恢复；中文/空格路径、高 DPI、读屏基础检查通过。

## 7. 已知限制（Stage 1A 范围内）

- 设置页当前只编辑**一个活动 provider**；多 profile 的列表/切换 UI 还没做（盘格式已支持列表）。
- 模型下拉只有当前一个模型；会话级多模型切换 UI 等到有 provider 列表后再补。
- 重命名仍使用 `QInputDialog`，不是行内编辑。
- 推理内容折叠、错误卡片重试已实现；Stage 2 的计划卡/工具卡/审批卡不在本阶段。
- Stage 1A 的实例芯片只显示“跟随主页：未选择实例”；真正的锁定在 Stage 1B 接工具时启用。
