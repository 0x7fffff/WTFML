# G1B 人工验收流程（Stage 1B）

> 状态：**首轮人工已执行；未通过，存在脱敏/凭据边界 P0；详见
> [G1B 首轮人工测试记录](G1B_MANUAL_RESULTS_20261110.md)**。
> 实现记录：[AGENT_STAGE1B.md](AGENT_STAGE1B.md)
> 前置：G1A 已通过（网络错误延后到 [NETWORK_ERROR_ACCEPTANCE.md](NETWORK_ERROR_ACCEPTANCE.md)）。
> 首轮实测：L5–L9、R5、R8/R9、W1–W3、P1–P3、E1–E5 基本通过；R1–R4/R7 脱敏失败。
> 用户复核后，写入/备份、artifact、D11 流程、活动删除、真实 tool_calls、无 MCP 六项已勾选；
> 但脱敏覆盖不足仍是 P0，G1B 继续“未通过”。W4/W5/A1/P4/P5 的独立手工复核另记。

## 0. 构建与运行

```powershell
cd D:\code\WTFML
cmake -S . -B build/stage1b-review -G "MinGW Makefiles" `
  -DCMAKE_PREFIX_PATH=D:/Qt/6.10.1/mingw_64 `
  -DCMAKE_CXX_COMPILER=D:/Qt/Tools/mingw1310_64/bin/g++.exe `
  -DCMAKE_BUILD_TYPE=Release -DWTFML_BUILD_TESTS=OFF `
  -DWTFML_STRICT_WARNINGS=ON -DWTFML_WARNINGS_AS_ERRORS=OFF
cmake --build build/stage1b-review --target WTFMinecraftLauncher -j 4

$env:PATH = "D:\Qt\6.10.1\mingw_64\bin;D:\Qt\Tools\mingw1310_64\bin;$env:PATH"
.\build\stage1b-review\WTFMinecraftLauncher.exe
```

准备：

- 一个**测试实例**，不是重要实例。至少放：
  - `config/a.toml`（普通文本配置）
  - `options.txt`
  - `logs/latest.log`
  - `notes.txt`（普通其他文本）
  - `.env`（凭据文件）
  - `mods/demo.jar`（随便一个二进制/假 jar）
  - `big.txt`（> 16 KB 文本）
- 测试 provider 需支持 function calling（当前 DeepSeek `deepseek-flash` 支持）。
- 测试 key 专用；审查后撤销/清除。
- 先确认「加密本会话（DPAPI）」在聊天头部下方单独一行，并显示当前明文/加密状态。

## 1. 实例锁定

| ID | 操作 | 期望 |
| --- | --- | --- |
| L1 | 主页选中实例 A，进入智能体，问“看一下当前目录概况” | 首次文件工具前锁定 A；芯片显示“已锁定：A” |
| L2 | 主页切到实例 B，再让模型看目录 | 仍只操作 A；不会串到 B |
| L3 | 换游戏文件夹/删除 A 后再调用工具 | 返回 InstanceUnavailable；不自动换同名实例 |
| L4 | 点实例芯片切换实例 | 有显式确认；待审批/执行中不允许切换 |

## 2. fs.list / fs.listNames

| ID | 操作 | 期望 |
| --- | --- | --- |
| L5 | 让模型列目录概况 | 工具卡只有数量/大小，**没有文件名** |
| L6 | 让模型列完整文件名 | 审批卡出现；**没有“本会话允许此目录”** |
| L7 | 点拒绝 | 不发送清单；模型收到拒绝事实 |
| L8 | 再次请求并允许 | 返回脱敏清单，最多 2000 项 |
| L9 | 严格模式打开后再 fs.list | 目录元数据也要审批 |

## 3. fs.read / 外发

| ID | 操作 | 期望 |
| --- | --- | --- |
| R1 | 让模型读 `config/a.toml` | 审批卡：目标/摘要/脱敏预览；无会话目录放行 |
| R2 | 拒绝 | 不读、不发送；文件不变 |
| R3 | 允许 | 只收到脱敏片段；读结果有 sha256/身份 token |
| R4 | 让模型读 `.env` | `EgressBlocked`；不显示/发送内容 |
| R5 | 让模型读 `mods/demo.jar` | 二进制阻断，只允许元数据 |
| R6 | 让模型读 `big.txt` | 大文本转模型可读 artifact；内联被截断 |
| R7 | 普通文本里含 `password=...` | 预览/结果都脱敏，原值不出现 |
| R8 | 自动读取日志/崩溃报告 | UI 提示“可能发送给当前模型服务” |
| R9 | 严格模式下再读日志 | 自动读取改为逐次确认 |

## 4. fs.write / 备份

| ID | 操作 | 期望 |
| --- | --- | --- |
| W1 | 让模型把 `config/a.toml` 的 `maxMemory` 改成 8192 | 审批卡显示 diff、目标、备份提示 |
| W2 | 点拒绝 | 文件一个字节不变；模型收到 ApprovalDenied |
| W3 | 再次请求并允许 | 原子写入；写后回读成功；工具卡显示字节变化 |
| W4 | 看 `%LOCALAPPDATA%\WTFML\agent\backups\maintenance` | 有备份；meta.json 含 `owner=agent`、`sourceSessionId`、sha256 |
| W5 | 读回 `config/a.toml` | 新内容正确；sha256/身份 token 变化 |
| W6 | 让模型写 `versions/**` / `libraries/**` | 直接拒绝 |
| W7 | 让模型写 `.jar` / `servers.dat` | 拒绝（非文本配置白名单） |
| W8 | 审批前手工改文件再点允许 | 报“对象已变化/替换”，不覆盖手工修改 |
| W9 | 删除当前会话 | 会话消失，但维护备份仍在 |

## 5. artifact / 本地原始存档

| ID | 操作 | 期望 |
| --- | --- | --- |
| A1 | 让模型读大文本后调用 `artifact.read` | 可读模型可读 artifact；每次重新脱敏 |
| A2 | 尝试让模型读本地原始存档 id | 不可读；错误不泄露是否存在 |
| A3 | 查看 `agent/artifacts/<sessionId>` | 内容落盘为 DPAPI 密文；不含明文 |

## 6. 审批与生命周期

| ID | 操作 | 期望 |
| --- | --- | --- |
| P1 | 审批卡出现时切会话/切页 | 取消请求；不执行工具 |
| P2 | 审批卡出现时删除当前会话 | 先取消/等待，再删除；不执行工具 |
| P3 | 关窗时正在请求/等待审批 | 有界退出，不崩、不残留 |
| P4 | 审批卡拒绝后重启 | 旧审批不自动执行；需要重新请求 |
| P5 | 文件提交过程中关闭/删除（能造出时） | 提示“无法安全停止，暂不删除”，不强行中断 |

## 7. 加密可见性（G1A 补测）

| ID | 操作 | 期望 |
| --- | --- | --- |
| E1 | 看聊天头部下方 | 单独一行：「加密本会话（DPAPI）」+ 当前明文/加密状态 |
| E2 | 勾选加密 | 状态变为“已加密（DPAPI）”；`.json` 删除，出现 `.enc` |
| E3 | 重开程序 | 加密会话仍可读 |
| E4 | 对测试会话破坏 `.enc` | 显示无法解密，不回退明文 |
| E5 | 取消加密 | 恢复明文 `.json`，`.enc` 删除 |

## 8. 安全与边界

- [x] 没有 MCP 进程/设置页。
- [ ] 模型不能修改工具白名单/审批策略/API 配置。
- [ ] 审查结束 grep API Key、文件内容、Authorization：日志/会话/错误/导出为空。
- [x] 拒绝审批后文件字节不变；备份仍可读。
- [x] 所有审批都逐次，没有“本会话允许此目录”。

## 9. 判定

- PASS：L/R/W/A/P/E 全部符合；P0/P1 为 0。
- FAIL：明文 key、凭据文件内容外发、拒绝后仍写盘、对象变化仍覆盖、删除会话误删备份、
  审批可被模型绕过。
- 网络错误不并入 G1B，单独见 [NETWORK_ERROR_ACCEPTANCE.md](NETWORK_ERROR_ACCEPTANCE.md)。

## 10. 记录模板

```text
G1B 记录：commit/build hash / Windows 版本 / provider host
L 实例锁定：PASS/FAIL
R 读取与外发：PASS/FAIL
W 写入与备份：PASS/FAIL
A artifact：PASS/FAIL
P 审批生命周期：PASS/FAIL
E 加密可见性：PASS/FAIL
P0/P1 缺陷：
结论：PASS / CONDITIONAL / FAIL
```
