# 主计划归档（2026-11-05）

给要查"某项 P2/P3 工作当时做到哪、测出什么数字、怎么决定的"的人看。这是 `main @ d937e22`（核对日期 2026-11-03）时主计划的压缩快照，保留旧编号和完成流水；当前待办只看 [ROADMAP](../ROADMAP.md)。
各项详细测量在 [性能基线](../dev/PERFORMANCE.md) 与 `dev/perf-notes/` 下的专题文档。

编号约定：原 2.1.1–2.1.7、2.2.1–2.2.3、2.2.5–2.2.6 已完成并移出，编号不重新分配；优先级 P0 发布阻断、P1 核心流程与数据/恢复闭环、P2 性能/架构/健壮性/日常体验、P3 低优先级。依据见 [REVIEW_LOG](REVIEW_LOG.md)。

## 1. P0：无发布阻断待办

错误终态、原件保护、取消/进程中断、满盘与保守恢复已在确认范围内验收（[ACCEPTANCE_LOG](ACCEPTANCE_LOG.md) 2026-10-25 节）。HDD 间歇拒绝未复现，按用户决定暂缓（原 P3）。
约束：不预先删原件、不放宽完整校验、不自动清理未知残留；硬件断电、真实杀毒组合、所有 I/O 故障位置不作无限发布门槛；掉盘预检不保证容量预留，也不保证失联设备的系统调用立即返回。

## 2. P1：核心流程与恢复闭环

原 §2.1、§2.2（含 2.2.4）2026-10-28 完成并移出。服务端静默分支、原故障现场重试、预设/服务端接续、资源更新、真实 Forge＋预设、USB HDD 见 ACCEPTANCE_LOG 2026-10-27～28 节。验证：主窗口 2716/2716、网络/预设 5/5、HDD 2/2，正式构建已更新。
已确认规则：必要项缺失/未知须处理；替代确认仅本次有效并展示必要前置；部分预设应用保留实例、明确警告且允许启动；恢复只重试未完成步骤；安装器/提交阶段不暂停，外部目标变化要求重新预览；重启后不自动执行 Java。

## 3. P1：本机范围收尾（2026-10-28）

完成登录凭据审计、150% 原生界面/键盘/并发日志、测试隔离与保守并行、最终本机回归和部署包：非 visual 38/38（395.53 秒）、visual 3121/3121；两平台公开下载摘要校验；隔离 Fabric 1.21.11 主菜单及正常退出；独立部署包启动/日志/互斥与诊断自检通过。产物摘要 `build/p1-final-artifacts-20261028.json`，部署包 `build/p1-final-bundle-20261028/`（本地产物，未发布）。详见 ACCEPTANCE_LOG 2026-10-28 节、[测试说明](../dev/TESTING.md)。长任务设独立截止，单程序不超过 900 秒。

## 4. P2：性能、架构、健壮性与日常体验

- [x] **恢复大批量响应性**：安装接续校验、预设首次/暂停/跨重启快照、任务中心首次文件恢复与暂停批次目标核对已后台化，保留单作业、取消/销毁、忙态/写锁与提交前复核（[恢复响应性](../dev/perf-notes/P2_RECOVERY_PERFORMANCE_20261028.md)）。关键结果：
  - 初始化配置/核心原件比对后台化；首次建初始化快照后台化（同一棵树同步 88.6 ms → 3.7 ms，算不出来整条接续意图置空，按普通下载收尾）；
  - 已存在完整文件的跳过校验/首次原件指纹登记后台化（`precheckReadsFileBody()` 分界，核对期间不许暂停；32 MiB 调度调用 1.6 ms、全量 SHA-1 47 ms）；
  - 恢复记录 50,000 条 / 22.3 MB：存 1.34 → 0.34 秒、读 1.41 → 0.40 秒（DPAPI 原占八成），盘上写 `WTFML-PROTECTED-2`（zlib 压缩后加密，文件 1.35 MB，读侧仍认旧档）；存/读仍在界面线程；
  - 其它预设应用配置读取仍同步，继续迁移须保留预览与执行间的指纹门禁。
- [ ] **低速、请求预算与实网来源**：未完成部分是当前网络下实测 Modrinth/CurseForge 无备用源、签名 URL 过期与表示变化；额外网络/代理矩阵暂缓。已落地：普通下载共享预算/低速比较/有限重试、本地 Range/ETag/摘要边界、分段协调器持续低速比较（窗口内合计速率＋换源后按 ETag 决定旧分段）。2026-10-31 元数据请求全部统一到 `RequestBudget::ControlGet`（加载器版本表、服务端与加载器安装、平台兼容、皮肤、资源商店、Java 管理；GET 与带小段正文的 POST 共用排队/截止/删除即取消），仍自管超时的只剩正文（普通下载、Java 运行时压缩包）。不混拼未经证明一致的表示。见 [高速下载](../features/HIGH_SPEED_DOWNLOAD.md)、[网络诊断](../dev/NETWORK_DIAGNOSTICS.md)、[元数据](../dev/perf-notes/P2_METADATA_BUDGET_20261031.md)、[兼容](../dev/perf-notes/P2_COMPAT_BUDGET_20261031.md)、[皮肤](../dev/perf-notes/P2_SKIN_BUDGET_20261031.md)、[资源商店](../dev/perf-notes/P2_RESOURCE_BUDGET_20261031.md)、[安装与 Java](../dev/perf-notes/P2_INSTALL_BUDGET_20261031.md)。
- [ ] **运行/构建剩余测量**（只剩 CI 上的 sccache 效果，本机未安装 sccache）。已量：
  - 共享头扇出时间成本（11-01）：`touch` + 12 路重编，`AppPaths.hpp` 411 TU / 297 s、`Theme.hpp` 328 / 238 s、`MainWindowUI.hpp` 271 / 218 s；1.4 秒的底座是 Qt 控件头，项目头只加 0.1–0.4 秒，砍项目头没有意义。`WTFML_USE_PCH`（默认 ON，只给主程序，预编译 `QWidget`）使产品全量构建 95 → 75 s；`<QtWidgets>` 整模块 `.gch`（425 MB、读回 3445 ms）反而更慢，未采用；验证工程不挂 PCH。带测试的全量构建无 A/B。
  - 主线程卡顿栈（`StallSampler` + `tools/symbolize-stalls.ps1`）：预设 store 同步读 1000 份 2.3 s（`PresetStore.cpp`）、同步写每份一次 `FlushFileBuffers` 共 1.7 s、首次画中文查字形回退 280 ms、`scan:java` 主线程等子进程 100 ms、造 200 行控件 80 ms（[卡住时的调用栈](../dev/perf-notes/P2_STALL_STACKS_20261101.md)）。
  - 多任务负载（空载 vs 后台读 1000 份 manifest 2.3 s）：点翻页 2.9–3.3 → 3.6–3.9 ms，整窗重画 3.3–3.5 → 3.9–4.8 ms，心跳最大间隔 37–44 ms（[多任务负载](../dev/perf-notes/P2_CONCURRENT_LOAD_20261101.md)）。
- [x] **大文件按状态所有权拆分**：MainWindow 安装/服务端/资源更新协调、实例设置概览/启动/脏状态、实例文件页与下载协调等按"一次迁一套状态和接线"拆出，详细记录在 [拆分记录](../dev/REFACTORING.md)。节点：10-29 实例文件选择/批量操作/操作栏布局集中到 `InstanceFilesSelection.cpp`，行构建分出 `InstanceFilesPageRows.cpp`；10-31 四类安装占用 id 收进 `controllers/InstallOccupancy`、「导出启动脚本」的 .bat 正文收进 `launcher/LaunchScriptExport`（19 条断言钉住引号、`%` 写两遍、令牌换环境变量引用、已知凭据不写）；11-04 实例设置脏状态判据成纯函数 `ui/InstanceSettingsSignature`（U9c）、`apply()` 先问 `InstanceMetaFile::fieldsAlreadyEqual()`（O13）；11-04 修全局 Java 内存档位多窗口互相覆盖（`save()` 发现盘上已被别人切成手动时不写 Xmx/Xms，L5）。
- [x] **导航/实例元数据渐进收口**：`manage/InstanceMetaFile` 成为 `wtfml.meta` 唯一读/合并写（`schemaVersion = 1`）；后台完成（更新检测/更新收尾/体检）改走 `queuePageOpen()` + `openPageWhenIdle()`，被占着就排队，不再顶掉用户正看的页；`execPage()` 产品调用点清零（11-02），路由栈落在 `controllers/NavigationReturn`。盘点与结论见 [NAV_AND_META_AUDIT](NAV_AND_META_AUDIT.md)。
- [ ] **复杂预设/大列表**：当时已有目录缓存、模型筛选、后台详情/干跑、旧结果作废；文件页启用/禁用统计合并，反选从逐行通知改为单次批量选择（1000 条 1279.164 ms/1000 次 → 1.430 ms/1 次，仅同步调用测量，见 [选择性能](../dev/perf-notes/P2_SELECTION_PERFORMANCE_20261029.md)）。本阶段改进：
  - 实例文件页首绘（`WTFML_FILES_PAINT_PROBE_ROWS` 探针）：200/500/1000 行建表 110/361/1537 ms，根因是 `setItemWidget()` 与 `addItem()` 交错导致整表平方级重算几何；`rebuildRows()` 改先排 item 再一趟挂控件，1000 行 1.39 s → 0.29 s（修后 200/500/1000 行 46/126/286 ms）；游戏规则表（`WorldSettingsPage::rebuildRuleRows()`）同改。再压要靠模型委托/可见范围虚拟化（后于 11-09 做了窗口化，见 ACCEPTANCE_LOG）。见 [大列表实测](../dev/perf-notes/P2_LARGE_LIST_PAINT_20261031.md)；
  - `VersionManager::readVersionMeta()` 按"在不在/修改时刻/字节数"缓存，200 实例 visible 923 → 840 ms、最长阻塞 204 → 150 ms（失效条件 O12）；
  - 安装确认弹窗五趟扫盘合成一趟（`installPickerData()`）；
  - `VersionManager::instanceTraits()` 一次答加载器/版本类型/频道：每轮标记 35–63 → 1–2 ms，visible 815.6 → 654.6 ms、最长阻塞 148.0 → 19.8 ms；
  - `InstanceFiles::scanDir()` 两趟 readdir 合成一趟：每轮 scan 18–23 → 7–10 ms；
  - 主页账号卡片小箭头 `⌄`（U+2304）默认字体无此字形，Qt 首次量它做字体回退 268 ms，改为 `VersionIcons::chevron()` 位图，visible 670.0 → 383.4 ms；随后界面自己写的 `✓ ✗ ⚠ ☑ ☐ ▶` 全换成主字体有的 `√ × ※ ■ □`（唯一出处 `ui/TextMarks.hpp`，单字符 4 ms vs 272–294 ms，`ui_check` I1 钉住）。规矩：启动第一屏别用默认字体里没有的字形；
  - 预设列表摘要缓存（键为 manifest 字节数＋修改时刻，上限 4096 份）：热读 2.3 s → 11–15 ms；冷读并行（份数 ≥ 8 且线程池多于一个线程）2.3 s → 0.40 s（PA12、PA13），见 [摘要缓存](../dev/perf-notes/P2_PRESET_LIST_CACHE_20261101.md)、[并行读取](../dev/perf-notes/P2_PRESET_READ_PARALLEL_20261101.md)。写那 1.7 s 与"冷读只读一份预算好的摘要文件"当时未做。
- [ ] **文件/服务器列表及外部工具**：批量/详情/迁移长错误、真实 ACL、共享占用、MOTD/未知状态与非常用属性；长路径清单和 ZIP/GZip 后端已落地。11-04 `.wtfml-server` 改合并写（ZX8.6）、读取上限 4 MiB（ZX8.7）、`wtfml.folders` 上限 8 MiB（Q16）；11-01 删除路径（`moveToRecycleBin()`）把"权限不允许"与"被别的程序占着"分开说（预检 `CreateFileW(path, DELETE, …)`，判据 ZAP1/ZAP2/ZAO6）。
- [ ] **局部布局/主题**：10-29 统一字号 12/14/18/28、圆角/焦点环、进入/按压时长，清中点分隔与重复说明，修下载筛选组布局；预设管理/安装选择/模组确认、设置页、实例管理各页文案精简；维护合并为检查与诊断、日志、备份与操作（迁移和升级保留独立页面）；侧栏选中块统一直角；设置背景控件分行；游戏版本清单双源并发；主页列表展开收紧启动区；服务端下载页列表按需同步；预设游戏选项/原版按键改详细控件清单；服务端安装器/加载器改异步版本列表（UI 3126/3126）；换主题不再给所有滚动条重挂样式（`Theme::styleScrollBars()`，换主题 40–55 → 25–29 ms）。仍待：轻回弹、完整 hover 过渡、新样式的复杂页面/真实 DPI/减少动态效果验收；更新向导布局重设按用户要求暂缓。见 [REVIEW_LOG](REVIEW_LOG.md)、[预设工作区](../features/PRESETS.md)。
- [ ] **发布体积与依赖**：动态版去符号省 24.45%；静态单文件发布版同样处理省 41.1%（73,531,415 → 43,288,540 字节，导入清单逐条相同）。`tools/prepare-release-copy.ps1` 只对副本 `--strip-unneeded`，调试信息用 `objcopy --only-keep-debug` 另留并挂 debuglink，写 `release-copy.json`。去符号副本启动没有变慢（189 → 185 ms 中位）；`-Os` 再省约 4.9 MB 但启动慢约 50 ms，不采用；`-flto=auto` 在本工具链链接不过（16 个自家符号被判未定义），不引入。低端 GPU/干净机器的软件渲染与驱动依赖未验；安装程序暂不实施，不未经兼容验收删 DLL。见 [发布体积实测](../dev/perf-notes/P2_RELEASE_SIZE_20261101.md)。
- [x] **外部输入的名字与图片**（10-31）：版本 json 的资源索引号/对象哈希、资源和升级审计里的文件名先过"一段路径"规矩再拼盘上路径；网上下来的图标/服务器图标/皮肤先问声明尺寸再解码（图标 4096 / 16 MP，背景图与实例图标另给宽一档，皮肤 256²）；拒掉时都有终态或一句话。起 PowerShell 不再用裸名（`CreateProcess` 会先看当前目录与 PATH），五处统一走 `PowerShell::executable()` 的系统绝对路径；Java 下载的 `Expand-Archive` 改 `-LiteralPath`。见 [安全边界](../features/SECURITY_BOUNDARIES.md)。

## 5. P3：低优先级

- [x] **分类默认预设**：四类各一格（`index.json` 的 `defaults`，键在 = 表过态、值空 = 不要默认）；旧 `default` 原样读回，只在自己那一类没表过态时推导，混合预设不推导；安装页区分"没表态"与"不要"。见 [PRESETS](../features/PRESETS.md)、`PresetStoreTests` PA14。
- [ ] **预设提取补充**：剩实包验收与"内部旧投影先建引用核对再回收"。11-04：「模组」类预设可提取（`wtfml.preset-mod-install.json` 里的平台身份，逐件按 sha1 核对实际文件，认不出的件数报给用户，不联网；PG1b）；编辑器「从实例提取」已有指纹反查（sha1＋CurseForge 指纹，`ModpackExport::curseForgeFingerprint`，V5d；两边认出时跟 Modrinth，PG8）与"只看没认出来的"人工整理（`modReferences`，PH5）。模组专用配置/按键不纳入预设范围。
- [x] **人工恢复目录**：可回滚事务、检查运行状态、保留双方、不清理未知暂存。
- [ ] **工具箱增强**：
  - 附魔求解：`Anvil::plan()` 是子集 DP＋帕累托剪枝，每层留 12 条状态；ZAD8 用 560 组随机 4–5 件算例证明等于穷举最优。每层额度提到 64 时 10 件跑到 231 秒仍未结束，已回滚。11-04 起 `Plan::approximate` 在额度真的切掉过候选时置真，界面备注说"这是近似最优"（ZAD9）。6 件 12 组中 11 组未标近似且等于穷举最优，7 件 3 组全精确；8 件抽 1 组会触发额度，只验"标出来"。契约：标了近似不保证最优，没标必须最优；更高件数未验；
  - 多信标共享底座已落地（`Beacon::sharedBaseBlocks()`：4 级 2/3/4/9 颗 = 188/212/216/276 块，1 级 2/4 颗 = 12/16 块，ZAI1b）；
  - 剩独立窗口/尺寸记忆、酿造/旗帜/纹饰版本边界，搜索/彩蛋先确认。
- [ ] **额外包格式**：单模组 ZIP 不当整合包（V0、V5z）；FTB 与 CurseForge 同一套 manifest，不需要单独适配器（`splitCurseForgeLoaderId()`，ZG2b）；旧 Technic/老式 Forge 包把加载器放在 `bin/modpack.jar`，`detectArchiveLayout()` 识别 `ArchiveLayout::legacyLauncherJar`，导入时提示补装加载器，不推断版本；`+jarMods`/`+files`/`addnatives`/`+traits`/`+luac` 各要求启动器多一种能力，现点名该格并说明原因拒绝，真支持须先做启动器侧能力并单独立项；多实例懒人包要真实样本定规则，当前明确"不猜"。
- [ ] **国际化**：QTranslator/.ts 与完整文案/布局验证，未排期。

## 6. 暂缓（当时登记）

| 项目（原等级） | 暂缓原因 | 恢复条件 |
| --- | --- | --- |
| 正版登录真实全链（P1） | 无项目 App ID，缺注册/审核与真实授权；构建预检与本机凭据审计已完成 | App ID 和审核就绪，用户可完成授权 |
| 额外系统显示/辅助功能复测（P1） | 需人工切换 100%/125% 缩放或真实减少动态效果设置；三档基础 72 张与 150% 原生专项有效 | 用户可切换系统设置 |
| 历史探针首次异常首因（P1） | 日志不足以定位首次 gzip 夹具失败、FileNotFoundError、段错；空列表保护已补 | 有可复现步骤/调用栈 |
| Actions 缓存/分片及线上耗时（P1/P2） | 用户要求先不处理 CI 账单影响 | Actions 可运行或用户要求恢复 |
| 多网络/代理对照矩阵（P2） | 缺额外网络/代理环境 | 有环境或测试员新报告 |
| 正式更新通道（P2） | 等正版登录全流程完成后再做 | 登录通过后定发布策略（SHA-256/签名、独立更新器、原子替换/回滚、手动确认） |
| HDD 恢复后重试升级偶发拒绝（P3） | 10 轮及 `build/p0-recheck-20261027-155720.json` 均未复现 | 再出现时保留现场，区分复制/扫描/数量/路径/大小/哈希，不放宽校验 |
| Windows 文件关联 | 当时用户暂缓（**后已实现**：0.5.0 起，见 [FEATURES](../FEATURES.md)） | — |

历史探针原记录：`build/test-runs/20261025-124239-f3c949ad/`、`build/p0-20261025/hdd-verify.json`、`build/preset-scale-nvme-20261027.log`、HDD 原失败 `build/p0-closure-20261025-154237/hdd.json`。

## 7. 范围、决策与完成标准（当时）

- 当前主页及后续用户决定优先于旧两排卡片稿；世界备份在存档，维护/创作默认折叠。
- 通用世界编辑、区块删除、投影渲染/转换、Scanner Mod、Mod 配置 API、云同步/插件市场不自动纳入近期；破坏性世界编辑先备份；安全/跳过 Mod 启动须先明确可回滚语义。
- 创建进程不能证明主菜单/有效会话，三种历史时间须另定检测与降级规则。
- 原游戏、复制来的游戏、原稿和失败证据不随计划整理删除；不用真实账户/存档做破坏性测试。
- 完成须有行为、错误/取消/恢复语义和模块验证；真机/实网/外部条件分别记录，未验收不勾完。
- 同一意图一套进度，未知兼容不猜，恢复不扩大授权；完成后从主计划移除，细节归子系统文档。
