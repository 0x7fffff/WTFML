# 文档索引

仓库根目录只放 [README.md](../README.md)（安装、构建、运行、目录结构）。其余文档都在 `docs/` 下：

- 顶层：产品约定总账 [FEATURES.md](FEATURES.md)、未完成工作 [ROADMAP.md](ROADMAP.md)。
- [features/](features/)：各功能的约定、设计与现行边界。
- [dev/](dev/)：开发者手册（测试、性能、重构、工具用法、注释规范）。
- [archive/](archive/)：一次性的验收、审查、计划记录。数字和结论是当时的快照，当前行为以 `features/` 与 [FEATURES.md](FEATURES.md) 为准。

## 顶层

| 文档 | 内容 |
| --- | --- |
| [FEATURES.md](FEATURES.md) | “现在仍然成立”的行为、硬约束、数据约定、验证边界总账，按功能分节，不记流水 |
| [ROADMAP.md](ROADMAP.md) | 主计划：只列未完成/部分完成的项（P0、P1 回归、升级页 U1–U8、P2、P3、暂缓表、约束） |

## features/ 功能约定与设计

| 文档 | 内容 |
| --- | --- |
| [AGENT.md](features/AGENT.md) | 智能体（Agent）设计与开发计划原稿 |
| [AGENT_STAGE1A.md](features/AGENT_STAGE1A.md) | 智能体 Stage 1A 实现与验收：Provider + Loop + 会话 + 基础聊天 UI（无工具对话） |
| [AGENT_STAGE1B.md](features/AGENT_STAGE1B.md) | 智能体 Stage 1B 实现与验收：受控文件工具、审批、D11 外发策略、实例锁定、维护备份 |
| [DIRECTORY_SESSION_LOCK.md](features/DIRECTORY_SESSION_LOCK.md) | 同一数据目录只允许一个窗口：锁的实现、目录/文件被占用、写入口审计、复制实例先暂存后发布、破坏性操作矩阵 |
| [HIGH_SPEED_DOWNLOAD.md](features/HIGH_SPEED_DOWNLOAD.md) | 高速下载：暂停/显式继续/跨进程恢复、分段与限速、本机文件复用、速度与剩余时间 |
| [MODPACK_IMPORT.md](features/MODPACK_IMPORT.md) | 整合包导入的支持范围与安全边界：声明式启动补丁（GTNH）、MCLib AssetDirector 首启资源预取、缺项报告 |
| [PRESETS.md](features/PRESETS.md) | 实例预设的使用方式与边界：清单校验、综合预设投影、模组执行保护、查询/确认/恢复 |
| [INSTANCE_UPGRADE.md](features/INSTANCE_UPGRADE.md) | 实例版本更新：向导、模组适配替换/声明覆盖、两种模组判断分开显示、原地升级与回滚的数据保护 |
| [UPGRADE_ACCEPTANCE.md](features/UPGRADE_ACCEPTANCE.md) | U8 真机验收清单：1000×750 布局、兼容性弹窗、ACL/占用/空间、取消重入中断、历史回滚 |
| [MANUAL_RECOVERY.md](features/MANUAL_RECOVERY.md) | 人工恢复目录：用旧目录替换当前目录的入口、事务登记、运行状态检查 |
| [SECURITY_BOUNDARIES.md](features/SECURITY_BOUNDARIES.md) | 外部输入变成盘上路径/解码的边界盘点：会变成路径的名字、按声明尺寸分配的图片、`powershell.exe` 解析 |

## dev/ 开发者手册

| 文档 | 内容 |
| --- | --- |
| [COMMENT_STYLE.md](dev/COMMENT_STYLE.md) | 注释规范：写“为什么/约束”、不写“做了什么”；文件头、行内、类注释、TODO/FIXME/NOTE、正反例 |
| [TESTING.md](dev/TESTING.md) | 测试怎么跑：`tools/test.ps1` 各套件、CI 分片、超时与环境变量、模块与夹具边界、新增用例约定 |
| [PERFORMANCE.md](dev/PERFORMANCE.md) | 性能基线：构建耗时、启动/就绪、CI 分片缓存、界面交互与并发下载的量法与数字 |
| [REFACTORING.md](dev/REFACTORING.md) | 大文件按状态所有权拆分的决策日志：去向、缝合线理由、当轮测试证据 |
| [UI_MODERNIZE.md](dev/UI_MODERNIZE.md) | `tools/ui_modernize.py` 用法：audit/apply/verify、`ui-modernize.json` 配置档、截图迭代法 |
| [NETWORK_DIAGNOSTICS.md](dev/NETWORK_DIAGNOSTICS.md) | `WTFMLNetworkDiag.exe` 的测试员用法（`package.ps1` 会把它复制成压缩包里的“使用说明.md”，文件名不能改） |

### dev/perf-notes/ 性能专项记录

性能专项记录，数字是写成当日的快照；量法与现行基线看 [PERFORMANCE.md](dev/PERFORMANCE.md)。

| 文档 | 内容 |
| --- | --- |
| [REQUEST_BUDGETS.md](dev/perf-notes/REQUEST_BUDGETS.md) | 元数据请求的并发名额（`RequestBudget::ControlGet`，4 个）与截止：渠道 × 名额 × 截止表（不是耗时预算） |
| [MAIN_THREAD_STALLS.md](dev/perf-notes/MAIN_THREAD_STALLS.md) | 主线程卡顿：探针采栈、`symbolize-stalls.ps1`、已量到的卡顿、起线程偶尔要 3 秒、后台加载时界面响应 |
| [PRESET_LIST_IO.md](dev/perf-notes/PRESET_LIST_IO.md) | 预设列表读盘：摘要缓存、并行冷读、盘上摘要、热缓存让 A/B 空转的坑 |
| [LIST_PAINT.md](dev/perf-notes/LIST_PAINT.md) | 大列表首绘/重列/滚动、先排 item 后挂控件、现行分帧建行、反选批量选择 |
| [RECOVERY_PERFORMANCE.md](dev/perf-notes/RECOVERY_PERFORMANCE.md) | 恢复与续装的响应性：九步后台化（校验、快照、接续、恢复记录） |
| [RELEASE_SIZE.md](dev/perf-notes/RELEASE_SIZE.md) | 发布体积：去符号 / `-Os` / LTO 实测、`prepare-release-copy.ps1` |

## archive/ 一次性记录（归档）

| 文档 | 内容 |
| --- | --- |
| [ACCEPTANCE_LOG.md](archive/ACCEPTANCE_LOG.md) | 验收流水 2026-10-25 ～ 11-10：大实例/USB HDD/VHD 满盘/权限与事务、服务端与下载恢复、P1 收尾、11-09 真机（6508/6508、1 GiB 升级）、Agent G1A/G1B 与待复核的网络错误项 |
| [REVIEW_LOG.md](archive/REVIEW_LOG.md) | 审查与计划复核流水：补充设计稿校正、项目检查与 UI 统一、实例文案复核、11-05 复核（服务端默认值回归已修）、11-09 状态 |
| [NAV_AND_META_AUDIT.md](archive/NAV_AND_META_AUDIT.md) | 导航与元数据收口：`execPage()` 迁到 `OverlayHost::showPage()`、后台同步等待盘点、`wtfml.meta` / `.wtfml-server` 读写者与判据 |
| [ROADMAP_ARCHIVE_20261105.md](archive/ROADMAP_ARCHIVE_20261105.md) | 11-05 清理前主计划的压缩快照：保留旧编号、P2/P3 完成流水与实测数字 |
| [PLAYER_SIMULATION_PLAN.md](archive/PLAYER_SIMULATION_PLAN.md) | 玩家路径模拟：隔离与预算、场景与覆盖、11-06 真实跑结果与未覆盖项 |
