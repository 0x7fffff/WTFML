# G1B 首轮人工测试记录（2026-11-10）

> 结论：**未通过 G1B**。流程性检查大部分符合预期，但发现脱敏覆盖不足和凭据文件识别缺口；
> 这两项属于隐私/凭据边界问题，必须先修复再复验。
>
> 审查步骤：[G1B_ACCEPTANCE.md](G1B_ACCEPTANCE.md)
> 实现记录：[AGENT_STAGE1B.md](AGENT_STAGE1B.md)

## 1. 环境与隐私说明

- 分支：`agent-development`
- 构建：`build/stage1b-review`（Stage 1B）
- 测试 provider：DeepSeek / `deepseek-flash`
- 测试文件含一份带 `hypixel_api_key` 的 `config.toml`；本记录不复制该值。
- 测试 `.env` 使用假值；本记录同样不复制具体值。
- 建议：不要把带真实 key 的 `config.toml` 分享/上报；换成占位值后再做分享类测试；
  如果该 key 曾离开本机，去 Hypixel 侧重置。

## 2. 结果总表

| 组 | 项目 | 结果 | 说明 |
| --- | --- | --- | --- |
| L1 | 首次实例操作锁定 | PASS | 锁定实例 A |
| L2 | 主页切 B 后仍只操作 A | PASS | 没有读到 B |
| L3 | 换游戏文件夹/删除实例 | PASS | 返回 `InstanceUnavailable` |
| L4 | 点实例芯片切换 | NOT TESTED | 不知道“实例芯片”是哪个控件 |
| L5 | `fs.list` 不含文件名 | PASS |  |
| L6 | `fs.listNames` 审批、无会话放行 | PASS |  |
| L7 | 拒绝清单 | PASS |  |
| L8 | 允许清单 | PASS |  |
| L9 | 严格模式目录元数据审批 | PASS |  |
| R1–R3 | 配置读取流程 | 流程 PASS，**脱敏 FAIL** | `hypixel_api_key` 未被识别；`redactionHits=0` |
| R4 | 凭据文件 `.env` | **FAIL/需复测** | 未见 `EgressBlocked`；只遮住 `API_KEY`，`DB_PASSWORD`/`REDIS_PASSWORD`/`API_SECRET`/`JWT_SECRET` 原样 |
| R5 | 二进制 jar | PASS | `egress_blocked`，二进制内容拒绝 |
| R6 | 大文本 | PASS（有安全备注） | 1 GiB 文件 `truncated=true`，返回 modelArtifact id |
| R7 | 变体字段名脱敏 | **FAIL** | 只认精确常见键名，变体绕过 |
| R8/R9 | 自动读取告知 / 严格模式 | PASS |  |
| W1–W3 | 写审批/拒绝/写入 | PASS |  |
| W4/W5 | 备份元数据 / 读回 sha256 | NOT TESTED | 测试文件本来就没有 sha256，不清楚看哪里 |
| A1 | `artifact.read` 分段读取 | NOT TESTED |  |
| A2/A3 | 本地原始存档/密文落盘 | 报告无问题 | 细节未单独记录 |
| P1–P3 | 切会话/删除/关窗取消 | PASS |  |
| P4 | 重启后旧审批不自动执行 | DEFERRED | 连续工作不便重启 |
| P5 | 文件提交阶段删除 | NOT TESTED | 未理解“提交阶段” |
| E1–E5 | 加密开关可见/加密/解密/破坏 | PASS |  |
| 安全与边界 | 无 MCP、Key 不落在界面/会话 | PASS | 但 R1–R4/R7 的脱敏问题仍阻断 G1B |

## 3. P0/P1 阻断问题

### B1. 脱敏只认精确字段名（P0）

实测：

- `hypixel_api_key` → `redactionHits=0`
- `DB_PASSWORD`、`REDIS_PASSWORD`、`API_SECRET`、`JWT_SECRET` → 未命中
- 精确 `password`、`API_KEY` → 命中（`API_KEY` 的值被替换为 `<redacted>`）

当前实现更接近“精确键名/固定形态匹配”，不是通用模式：

- 精确 `password`、`passwd`、`secret`、`client_secret`、`api_key`、`apikey` 等；
- `sk-...`、JWT、Bearer、私钥块、AWS key 等固定形态；
- 下划线/前缀/后缀变体没有覆盖。

**影响**：配置文件里的真实凭据可能原样进入审批预览、工具结果、会话和模型上下文。
这是 G1B 不能通过的首要原因。

**修复方向**：字段名匹配改成“键名 token 化后包含 password/secret/token/api-key/credential 等”，
同时保留格式检测；补 `hypixel_api_key`、`DB_PASSWORD`、`REDIS_PASSWORD`、`API_SECRET`、
`JWT_SECRET`、`*_TOKEN`、`*_KEY` 等测试。不能只把当前实测的几个名字加进白名单。

### B2. 凭据文件识别过窄（P0）

实测 R4 未见 `EgressBlocked`。当前实现只硬阻断**文件名恰好为 `.env`**、
`credentials*`、`token*`、`secret*`、`auth.*` 或 `*.pem/key/p12/pfx/jks` 等；
如果测试文件叫 `env`、`test.env` 或其它变体，会进入普通文本审批，而不是硬阻断。

**需要复测**：把文件名改成精确的 `.env` 再看是否 `EgressBlocked`。
**修复方向**：`.env`/`*.env`/`*.env.*`、`*credential*`、`*secret*`、`*token*`、
`*apikey*` 等统一按凭据文件处理；凭据文件名不能只看扩展名。

### B3. artifact 通道不会因“截断”自动降级（P1，需明确文案与策略）

R6 的文件是 1 GiB 的 `a`。`fs.read` 返回：

- `truncated=true`
- `modelArtifact=<id>`
- 内联 content 被截断

需要说明两点：

1. 当前实现只把**本次读取到的内容**（默认 256 KB，最大 1 MB）写模型可读 artifact，
   不是整个 1 GiB；
2. 但敏感文件即使被截断，只要脱敏没命中，artifact 仍保存这部分内容，模型可通过
   `artifact.read` 分段取回。**“截断”不等于“安全”**。

当前工具文案“完整内容见模型可读 artifact”不准确，应改为“本次读取内容见 artifact”，
并明确它同样经过 D11/脱敏。

### B4. 日志/自动读取字段仍需扩展（P2，Stage 2 前处理）

R8/R9 中看到的日志包含：

- Windows 用户名
- “Completely ignored arguments” 回显的启动参数
- 本机绝对路径
- 硬件指纹：显卡型号 + 驱动版本、音频设备名（含品牌型号）、SDL 版本、Windows 驱动信息

这些不在 Stage 1B 文件工具的主路径里，但 Stage 2 的日志/诊断工具必须按字段白名单投影，
不能把整段日志直接发给模型。

## 4. 未测项怎么补

### L4：什么是“实例芯片”

智能体页右上角、模型下拉框左边的小按钮，文字是：

```text
跟随主页：<实例名>
已锁定：<实例名>
已锁定：A（主页当前：B）
```

点它就是请求切换实例；有待审批/执行中操作时会被拒绝。
测试：锁定 A 后，把主页切到 B，再点这个芯片，确认出现“新建会话 / 在本会话切换 / 取消”。

### W4/W5：sha256 在哪里

`sha256` 不在 `.toml` 文件内容里。看两个地方：

1. **工具卡详情**：写成功/读成功的工具卡，展开后 JSON 里有
   `sha256`、`fileIdentityToken`、`bytesBefore/bytesAfter`。
2. **维护备份元数据**：
   `%LOCALAPPDATA%\WTFML\agent\backups\maintenance\<backupId>\meta.json`
   里有 `owner=agent`、`source=agent-session`、`sourceSessionId`、`sha256`、`originalPaths`。
3. 也可以用 PowerShell 对比：

```powershell
# 把 <实例目录> 换成实际 versions/<实例> 路径
Get-FileHash "<实例目录>\config\a.toml" -Algorithm SHA256
```

W4：审批通过写入后，找最新 backup 目录，看 meta.json 的 owner/source/sourceSessionId/sha256。
W5：再让模型读一次 `config/a.toml`，允许审批，展开工具卡看 `sha256`；和上面
`Get-FileHash` 的结果比对。写后回读由工具自己完成，不需要你手算 1 GiB 文件。

### P5：“文件提交阶段”是什么

`fs.write` 在审批通过后会依次做：

```text
重新核对旧身份/哈希 → 写前备份 → 原子替换 → 写后回读校验
```

从“原子替换开始”到“写后校验结束”这一段就是提交阶段。此时删除会话/关闭程序不应该
强行中断，否则可能留下半截文件。小配置文件几乎是瞬间完成，手动很难卡住；这一项主要靠
自动化测试覆盖，人工可标 N/A，或用一个很大的配置文件/慢盘尝试在提交瞬间点删除。

### P4：重启后旧审批

重启启动器，打开有未完成审批的会话，确认：

- 旧审批不自动执行；
- 界面提示“上次未完成的审批已作废，需要重新请求”。

### A1：artifact 分段读取

先用 `big.txt` 读一次，拿到 `modelArtifact` id，再让模型 `artifact.read` 带
`offset/length` 读取；确认结果仍是脱敏后的内容，不返回本地原始存档。

## 5. 下一步

1. 修复 B1/B2/B3，并补对应回归测试：
   - `hypixel_api_key`、`DB_PASSWORD`、`REDIS_PASSWORD`、`API_SECRET`、`JWT_SECRET`；
   - 精确 `.env`、`test.env`、`env` 等文件名；
   - artifact 文案与“截断不等于安全”。
2. 修复后重新构建 Stage 1B，复测 R1–R4/R6/R7 和 W4/W5。
3. 网络错误仍按 [NETWORK_ERROR_ACCEPTANCE.md](NETWORK_ERROR_ACCEPTANCE.md) 单独延后。
4. 只有 P0/P1 清零后，G1B 才能标记 PASS。

## 6. 本次不写“已通过”的项

- R1–R4、R6、R7（脱敏/凭据边界）
- W4/W5（未测）
- A1（未测）
- P4（延后）
- P5（未测/自动化覆盖）
- 网络错误（单独延后）

## 7. 2026-11-10 复核勾选

用户确认：以下 G1B 检查项除“脱敏覆盖不足”外，可以按首轮人工结果打勾。已在
`docs/AGENT_STAGE1B.md`、`Agent.md` §12.6、`docs/G1B_ACCEPTANCE.md` 同步：

- [x] 审批通过写入有 diff/备份/原子性/哈希；拒绝则一个字节不变。
- [x] 模型可读 artifact 只含脱敏内容；本地原始存档不可枚举/读取；`artifact.read` 每次重扫。
- [x] D11 标准边界：自动/确认/阻断、自动读取告知、严格模式、凭据文件/存储不可读。
- [x] 活动会话删除：取消/等待安全终止；文件提交阶段不强行中断。
- [x] 真实模型 tool_calls 流程（DeepSeek/兼容服务）与文件读写审批在真机上走一遍。
- [x] 没有 MCP 进程/设置页。

**勾选边界**：D11/artifact 的勾选只代表流程、边界表和 UI 行为已验；脱敏字段变体覆盖不足
（B1）、凭据文件名覆盖不足（B2）、artifact 文案/截断语义（B3）仍是 P0/P1，G1B 继续维持
“未通过”，修复后再复测 R1–R4/R6/R7。
