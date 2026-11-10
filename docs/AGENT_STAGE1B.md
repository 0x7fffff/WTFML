# 智能体 Stage 1B 实现与验收

> 日期：2026-11-10  
> 范围：受控文件工具 + 审批 + D11 外发策略 + 实例锁定 + 维护备份（**不接 MCP**）。  
> 状态：**离屏自动验收通过；G1B 真机人工审查尚未打勾**。  
> 上一阶段：[Stage 1A](AGENT_STAGE1A.md)；设计原文：[Agent.md](../Agent.md)。

## 1. 这一阶段做了什么

- **路径/对象底座**：`AgentWinPath` 做字符串快速拒绝 + Win32 句柄级校验：
  `CreateFileW`（目录 `FILE_FLAG_BACKUP_SEMANTICS`、最终组件
  `FILE_FLAG_OPEN_REPARSE_POINT`）→ `GetFinalPathNameByHandleW` 核最终路径 →
  `FileIdInfo` 取卷序列 + 文件 ID → 拒绝 reparse/junction/symlink。写入前记录身份，
  提交前重新核对父目录/目标身份和哈希；对象被替换则拒绝。
- **文件工具**：
  - `fs.list`：只返回目录元数据（文件/目录数、大小合计、是否超过 2000 项），**不含文件名**；
  - `fs.listNames`：完整文件名清单，每次审批，最多 2000 项；
  - `fs.read`：只读锁定实例内文本；默认 256 KB、最大 1 MB；NUL/控制字符判二进制；
    返回 sha256 + 不透明 `fileIdentityToken`；大文本转模型可读 artifact；
  - `fs.write`：只允许 `config/**`、`defaultconfigs/**`、`options.txt`；必须带
    `expectedSha256` + `fileIdentityToken`；每次审批；写前 DPAPI 备份、原子替换、写后回读；
  - `artifact.read`：只能读模型可读 artifact；每次重新脱敏；本地原始存档 id 不可读，
    错误不泄露存在性。
- **D11 外发策略**：日志/崩溃报告/锁定实例元数据/目录元数据自动；配置/完整文件名清单/
  其他文本/完整实例列表逐次确认；凭据文件/凭据存储/二进制硬阻断；严格模式把自动整列改为
  逐次确认；自动读取在 UI 提示“可能发送给当前模型服务”。
- **实例锁定**：第一次实例工具调用锁定当前主页实例并先落盘；锁定后不跟随主页切换；
  换游戏文件夹/实例删除 → `InstanceUnavailable`；显式切换需确认（可新建会话）。
- **审批**：只读自动；文件读确认/写/命令/外部 MCP 逐次；**没有会话级目录放行**；
  审批请求持久化但不存“已批准”布尔，重启后作废、必须重新请求。
- **工具线上名**：内部名（`fs.read`）在发给 provider 前映射成 OpenAI/DeepSeek 允许的
  `fs_read`（`^[A-Za-z0-9_-]+$`）；模型回调时再映射回内部名。
- **错误体保留**：流式请求遇到非 2xx 时也保留供应商 JSON 错误体，界面不再只剩
  “HTTP 400”。
- **工具循环**：模型返回 `tool_calls` 后逐次校验/执行/回灌；需要审批时 Loop 进入
  `AwaitingApproval`；通过后继续请求，拒绝以 `ApprovalDenied` 工具结果回灌。
- **维护备份**：`backups/maintenance/<id>/`，内容 DPAPI 加密，元数据带
  `owner=agent` / `source=agent-session` / `sourceSessionId` / 实例 id / 相对路径 / sha256；
  删除会话不删除备份；Agent 只清理 `owner=agent`。
- **UI**：工具卡（待执行/执行中/完成/失败，可展开详情）、审批卡（目标/摘要/diff/预览/
  备份/外发说明/允许/拒绝）、实例芯片、严格模式开关。

## 2. 代码位置

| 模块 | 文件 |
| --- | --- |
| 路径/对象身份 | `src/agent/AgentWinPath.*` |
| 外发策略 | `src/agent/AgentEgressPolicy.*` |
| 审批矩阵 | `src/agent/AgentApprovalPolicy.*` |
| 文件工具 | `src/agent/AgentFileTools.*` |
| artifact | `src/agent/AgentArtifactStore.*` |
| 维护备份 | `src/agent/AgentBackupIndex.*` |
| 实例锁定 | `src/agent/AgentInstanceLock.*` |
| 工具 schema/引擎 | `src/agent/AgentTool.hpp`、`AgentToolRegistry.cpp`、`AgentFileToolEngine.*` |
| Loop | `src/agent/AgentLoop.*`（新增 `AwaitingApproval`/`ExecutingTool`、工具结果回灌） |
| UI | `ui/AgentToolCard.*`、`ui/AgentApprovalCard.*`、`ui/AgentPage.*`、`ui/AgentSettingsPage.*` |
| 接线 | `src/controllers/AgentController.*`、`src/MainWindow.*` |

**依赖边界**：`ui/Agent*` 仍只 include `ui/` 与值类型，不 include `src/`。

## 3. 盘上格式补充

```text
agent/
  artifacts/<sessionId>/<id>.enc          # DPAPI；模型可读/本地原始存档
  artifacts/<sessionId>/<id>.meta.json    # modelReadable/size/sha256（不含内容）
  backups/maintenance/<backupId>/
    original.enc                          # DPAPI 备份内容
    meta.json                             # owner/source/sourceSessionId/哈希/路径
```

- 模型可读 artifact 写入前脱敏，读取时再次脱敏；
- 本地原始存档的 id 不在模型可见 id 空间里，模型请求一律“不可读”；
- 审批请求存 `pendingApproval`，但不存 `approved`；恢复会话时作废并提示重新请求。

## 4. 安全边界与已知限制

- 句柄校验 + 写前/写后身份比对只能**缩小** TOCTOU 窗口，Windows 没有等价 `openat`；
  文档和 UI **不宣称彻底解决**。
- `fs.write` 只开放文本配置白名单；`versions/libraries/assets/java/Servers` 原始写拒绝。
- 二进制/jar/zip 不读内容；不提供任意命令、不接 MCP。
- Stage 1B 仍没有 `logs.tail` / `diagnostics.*` / `instance.context` / `backup.restore`
  （按 Agent.md 属于 Stage 2 工具面）。
- 大文本转 artifact 的上限是 16 KB 内联；超出部分只给模型可读 artifact id。
- 活动会话删除：有待审批/执行中先取消；文件提交阶段检测到 `committing()` 时拒绝删除并提示。

## 5. 自动验收记录（2026-11-10 本机）

构建目录：`build/verify`（MinGW 13.1 / Qt 6.10.1，Release）。

| 套件 | 结果 |
| --- | --- |
| `wtfml.core-agent` | **306/306** |
| `wtfml.ui-agent` | **37/37** |
| `wtfml.main-window-agent` | **31/31** |
| 既有 `wtfml.ui` / `wtfml.main-window` / `wtfml.core` / `wtfml.token-protection-fault` | 通过 |
| `test-modules` / `test-runner` / `ci-partitions` | 通过 |
| 产品 `WTFMinecraftLauncher` 构建 | 通过 |

覆盖点：

- 路径：绝对路径、`..`、盘符、UNC、ADS、保留名、尾点/空格、控制字符、保护目录；
  句柄级最终路径/文件 ID、内容变化后 token 失效、junction 逃逸；junction 夹具无法创建时
  明确 SKIP，不伪装成通过。
- 外发：边界表、严格模式、配置确认、凭据文件硬阻断、日志/配置内容脱敏。
- 审批：写/清单逐次、无会话级目录放行、请求不存 `approved`。
- artifact：模型可读/本地原始分离、DPAPI 密文、读取重新脱敏、本地 id 不可读且错误不泄露。
- 实例锁：首次锁定、换游戏根不可用、同名不同 id、显式切换。
- 备份：owner/source/sourceSessionId、元数据不含内容、删除会话不删备份、来源会话删除后仍可读、
  `owner=user` 不被误删。
- 工具：fs.list 不含文件名、listNames/read/write 审批、拒绝不改盘、对象变化拒绝、
  二进制/凭据/穿越/保护目录拒绝、大文本 artifact、artifact.read 重扫。
- 真主窗口：假模型返回 tool_calls → 审批卡 → 通过 → 工具结果卡片 → 会话落盘；
  待审批时取消并删除活动会话。

## 6. G1B 人工审查清单（未打勾）

- [ ] 路径攻击矩阵在真机（Windows 10/11、中文/空格/长路径）复核；junction/symlink/挂载点。
- [ ] 句柄级最终路径/文件 ID/reparse 校验；写入前后对象替换；不宣称彻底解决 TOCTOU。
- [ ] 审批通过写入有 diff/备份/原子性/哈希；拒绝则一个字节不变。
- [ ] 模型可读 artifact 只含脱敏内容；本地原始存档不可枚举/读取；`artifact.read` 每次重扫。
- [ ] D11 标准边界：自动/确认/阻断、自动读取告知、严格模式、凭据文件/存储不可读。
- [ ] 活动会话删除：取消/等待安全终止；文件提交阶段不强行中断。
- [ ] 备份创建随写审批；删除会话不删备份；`owner=agent` 独立清理；来源会话删除后元数据可读。
- [ ] 真实模型 tool_calls 流程（DeepSeek/兼容服务）与文件读写审批在真机上走一遍。
- [ ] 没有 MCP 进程/设置页。

## 7. 没有跑 / 不能算通过

真实第三方模型服务、真实 Windows 凭据管理器主路径（自动测试仍强制 DPAPI 后备）、
真实文件系统竞争/杀毒软件占用、真机高 DPI/读屏、真实大实例的配置写入与回退、
junction 无法创建时的平台夹具。以上不写成已通过。
