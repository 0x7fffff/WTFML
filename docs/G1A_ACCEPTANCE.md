# G1A 人工验收流程（Stage 1A）

> 状态：**2026-11-10 人工审查：除网络错误外全部通过；网络错误单独延后**。  
> 网络错误延后记录：[NETWORK_ERROR_ACCEPTANCE.md](NETWORK_ERROR_ACCEPTANCE.md)。  
> 适用设计：[Agent.md](../Agent.md) §12.6 / §14 Stage 1A  
> 实现记录：[AGENT_STAGE1A.md](AGENT_STAGE1A.md)  
> 后续阶段：[AGENT_STAGE1B.md](AGENT_STAGE1B.md)

## 0. 先分清两个版本

G1A 的原始定义是 **“无工具聊天”**：不读文件、不调用工具、不接 MCP。  
当前 `agent-development` HEAD 已经包含 Stage 1B 文件工具，因此：

| 目的 | 应使用 | 能否算严格 G1A |
| --- | --- | --- |
| 严格 G1A 审批 | Stage 1A 提交 `27070e5` 单独构建 | 能 |
| 当前 HEAD 的回归检查 | `9c1a0bb` / `build/g1a-review-fix` | **不能**；文件工具项属于 G1B |
| G1A + G1B 联合验收 | 当前 HEAD | 需分别记录 G1A 回归和 G1B 结果 |

**建议**：G1A 用 Stage 1A 提交单独构建；Stage 1B 另走 G1B，不把两者混成一次通过。

严格 G1A 构建（不动当前分支）：

```powershell
cd D:\code\WTFML
git worktree add build/g1a-stage1a-src 27070e5
cmake -S build/g1a-stage1a-src -B build/g1a-stage1a -G "MinGW Makefiles" `
  -DCMAKE_PREFIX_PATH=D:/Qt/6.10.1/mingw_64 `
  -DCMAKE_CXX_COMPILER=D:/Qt/Tools/mingw1310_64/bin/g++.exe `
  -DCMAKE_BUILD_TYPE=Release -DWTFML_BUILD_TESTS=OFF `
  -DWTFML_STRICT_WARNINGS=ON -DWTFML_WARNINGS_AS_ERRORS=OFF
cmake --build build/g1a-stage1a --target WTFMinecraftLauncher -j 4
```

运行：

```powershell
$env:PATH = "D:\Qt\6.10.1\mingw_64\bin;D:\Qt\Tools\mingw1310_64\bin;$env:PATH"
.\build\g1a-stage1a\WTFMinecraftLauncher.exe
```

审查结束可移除临时工作树：

```powershell
git worktree remove build/g1a-stage1a-src
```

## 1. 角色与输入

| 项 | 要求 |
| --- | --- |
| 实现者 | 提供 commit、构建日志、自动测试结果、已知限制 |
| 审查者 | 独立执行人工清单；不修改代码除非另开修复提交 |
| 测试 provider | 真实 OpenAI-compatible 服务（如 DeepSeek）或约定好的测试端点 |
| 测试 key | **专用、可随时撤销**的 key；不截图、不写进提交/文档 |
| 环境 | Windows 10/11，记录版本、缩放、语言、是否干净用户 |
| 测试数据 | 新建测试会话；不拿重要会话做加密损坏测试 |
| 产物 | G1A 记录、证据目录、缺陷清单、PASS/FAIL 结论 |

## 2. 总流程

```text
冻结版本
  → 自动门禁（测试/构建/文档链接）
  → 构建 Stage 1A 产物
  → 准备测试 key / 测试会话
  → A 启动与导航
  → B API/URL
  → C 凭据与遮挡
  → D 聊天与生命周期
  → E 会话存储与加密
  → F 网络错误与安全边界
  → G 无工具 / 无 MCP
  → H 真机 / DPI / 无障碍
  → 汇总判定
       PASS → G1A 签字
       FAIL → P0/P1 缺陷 → 修复 → 重跑受影响项 + 回归
```

自动门禁不是 G1A 通过条件，但必须先绿；自动测试不能替代人工结论。

## 3. 自动门禁（审查前）

在 Stage 1A 提交上执行并记录：

- [ ] `verify_agent` 全绿（Stage 1A 基线 145/145；当前 HEAD 已扩展）。
- [ ] `ui_check` / `mw_check` 相关回归全绿。
- [ ] `token_protection_fault` 通过。
- [ ] 产品 Release 构建 0 error。
- [ ] `check-test-modules`、`check-doc-links` 通过。
- [ ] 记录 commit、构建命令、exe SHA-256。

```powershell
Get-FileHash .\build\g1a-stage1a\WTFMinecraftLauncher.exe -Algorithm SHA256
```

## 4. A：启动与导航

| ID | 操作 | 期望 | 证据 |
| --- | --- | --- | --- |
| A1 | 启动 | 默认停在主页 | 截图 |
| A2 | 看顶部导航 | 主页/管理/下载/设置/智能体 五颗 | 截图 |
| A3 | 900×620 与最小宽度 | 五颗居中、不重叠、不压窗口按钮 | 截图/尺寸 |
| A4 | 进入智能体 | 双栏、空态、隐私说明可见 | 截图 |

## 5. B：API 与 URL

| ID | 操作 | 期望 |
| --- | --- | --- |
| B1 | Base URL 填 `http://example.com/v1` | 拒绝远程明文 HTTP |
| B2 | 填 `http://127.0.0.1:<port>` 未勾本地 HTTP | 拒绝 |
| B3 | 勾选“允许本地明文 HTTP” | 允许并持续警告 |
| B4 | URL 带 userinfo / 查询串 / 片段 | 拒绝 |
| B5 | 远程 https + 真实 provider | 保存成功 |
| B6 | 测试连接 | 只显示成功/HTTP 状态/耗时，不显示 key；404 不得显示成成功 |
| B7 | 重定向 / 证书错误 | 直接失败；不跟随重定向、不忽略证书 |

## 6. C：凭据与遮挡

| ID | 操作 | 期望 |
| --- | --- | --- |
| C1 | 查看 API Key 输入框 | 默认星号遮挡 |
| C2 | 点眼睛 | 临时显示明文 |
| C3 | 失焦/保存 | 恢复遮挡，不回显已保存值 |
| C4 | 保存后看状态 | Windows 凭据管理器（或明确后备状态） |
| C5 | 凭据管理器查看 `WTFML/Agent/<profileId>` | 存在；内容不可在 UI 回显 |
| C6 | 清除 key 并保存 | 凭据删除，状态变未配置 |
| C7 | 强制主路径不可用的场景 | 自动测试覆盖 DPAPI 后备；产品不提供人工故障开关，人工只验主路径/文案 |

**G1A 必须确认**：日志、会话 JSON、错误卡片、导出文件里都没有明文 key。

## 7. D：聊天与生命周期

| ID | 操作 | 期望 |
| --- | --- | --- |
| D1 | 新建会话并发消息 | 流式正文出现 |
| D2 | 流式中点停止 | 立即停止，保留部分内容并标“已中断” |
| D3 | 停止后重试 | 由用户触发新请求，不伪造成功 |
| D4 | 切换会话 | 原会话在飞请求取消 |
| D5 | 切到其他选项卡 | 在飞请求取消 |
| D6 | 关窗时正在流式 | 有界退出，不崩、不挂 |
| D7 | 重开程序 | 会话、标题、固定状态恢复 |
| D8 | 请求中删除会话 | 先取消/等待安全终止，不强行中断 |
| D9 | 错误消息 | 显示分类、人话、重试/设置入口，不显示 key/header |

## 8. E：会话存储与加密

| ID | 操作 | 期望 |
| --- | --- | --- |
| E1 | 默认会话 | `%LOCALAPPDATA%\WTFML\agent\sessions\<id>.json` 明文 |
| E2 | 元数据 | `<id>.meta.json` 可列；不含 key/正文全文 |
| E3 | 开启“加密本会话” | `.json` 删除，出现 `.enc` |
| E4 | 重开程序 | 当前 Windows 用户可读 |
| E5 | 手工破坏 `.enc`（仅测试会话） | 显示“无法解密”，绝不回退明文 |
| E6 | 加密失败 | 保留原明文并报错，不半加密 |

## 9. F：网络错误与安全（延后，不纳入本轮 G1A）

网络错误本轮未执行：401/403、429、断网、流中失败、重定向、证书错误、代理、400 错误体显示。
这些检查单独移到 [NETWORK_ERROR_ACCEPTANCE.md](NETWORK_ERROR_ACCEPTANCE.md)，以后用本机可控服务
或真实 provider 补充；未完成前不计入 G1A 通过项。

## 10. G：无工具 / 无 MCP（严格 G1A 才可打勾）

| ID | 操作 | 期望 |
| --- | --- | --- |
| G1 | 让模型“读 config/a.toml” | 模型无文件工具权限，不读文件 |
| G2 | 进程列表 | 没有 MCP 子进程 |
| G3 | 设置页 | 没有 MCP 服务器一栏 |
| G4 | 提示注入 | 模型不能修改系统提示/API 配置/审批策略 |

> 当前 HEAD 已包含 Stage 1B 文件工具；这一组只能在 `27070e5` 构建上打勾，或在 G1B 另行审查。

## 11. H：真机 / DPI / 无障碍

- [ ] Windows 10/11 当前用户下凭据管理器正常。
- [ ] 游戏目录含中文、空格、长路径时智能体页可用。
- [ ] 125% / 150% / 200% 缩放不裁切、不横向滚动、不重叠。
- [ ] 系统“减少动态效果”生效。
- [ ] 读屏基础检查：按钮有名称；密码框不读出明文。
- [ ] 关闭/重开、切换会话/选项卡无残留定时器或崩溃。

## 12. 判定规则

| 等级 | 例子 | 结论 |
| --- | --- | --- |
| P0 阻断 | 明文 key 落盘/进日志；凭据降级明文；URL 安全校验被绕过；加密失败回退明文；崩溃/数据丢失 | **FAIL** |
| P1 严重 | 停止/取消无效；会话恢复失败；错误不可见；无配置仍可发送；锁屏/切页后串会话 | **FAIL**，修复后再验 |
| P2 一般 | 文案、对齐、状态提示不准确；非关键布局问题 | 可 CONDITIONAL，但需记录并限期修复 |
| P3 轻微 | 视觉细节、无关紧要的间距 | 不阻断 G1A，记录即可 |

判定：

- PASS：P0/P1 为 0，清单全部完成，自动门禁全绿；网络错误如果延后，必须单独列明。
- CONDITIONAL：仅 P2/P3，且有明确修复计划与负责人。
- FAIL：存在 P0/P1 或关键项未测。

## 13. 缺陷处理与复验

1. 记录现场：步骤、期望、实际、截图/日志、commit、build hash。
2. 按 P0/P1/P2 定级；P0/P1 必须新提交修复。
3. 修复后重跑：
   - 原失败项；
   - 受影响的安全项（URL/凭据/会话/加密至少一组）；
   - 自动门禁。
4. 不复用旧通过的截图；首次失败记录保留，不用复测覆盖。

## 14. 审批产物模板

```text
G1A 审批记录
- 分支 / commit：
- 构建目录 / exe SHA-256：
- 构建命令：
- Windows 版本 / 缩放 / 语言：
- 测试 provider host（不含 key）：
- 测试日期 / 审查人：
- 自动门禁：verify_agent __/__，ui/mw __，构建 __
- A 启动导航：PASS/FAIL
- B API/URL：PASS/FAIL
- C 凭据：PASS/FAIL
- D 聊天生命周期：PASS/FAIL
- E 会话加密：PASS/FAIL
- F 网络错误：PASS/FAIL
- G 无工具/无 MCP：PASS/FAIL/N/A（当前 HEAD）
- H 真机/DPI/无障碍：PASS/FAIL
- P0/P1 缺陷：
- P2/P3 缺陷：
- 结论：PASS / CONDITIONAL / FAIL
- 附件：截图目录、grep 结果、日志位置
```

## 15. 当前状态与下一步

- **G1A 人工结果（2026-11-10）**：除网络错误外全部通过；审查期间确认没有文件工具/MCP，
  且 API Key 未出现在日志/会话/界面。网络错误延后到
  [NETWORK_ERROR_ACCEPTANCE.md](NETWORK_ERROR_ACCEPTANCE.md)。
- **会话加密可见性修复**：加密开关从右上角挤在一起的位置移到聊天头部下方单独一行，
  显示“加密本会话（DPAPI）”和“当前会话：明文 JSON / 已加密（DPAPI）”。
- Stage 1A 自动验收已过；Stage 1B 自动验收已过。
- 当前 HEAD `9c1a0bb` 已修复：
  - DeepSeek/OpenAI 工具 function name 不允许点号的问题（线上名改为 `fs_read` 等）；
  - 流式请求收到非 2xx 时错误 JSON 被丢弃、界面只剩 HTTP 400 的问题。
- Stage 1B 构建用于 G1B 审查，不再作为严格 G1A 构建。
