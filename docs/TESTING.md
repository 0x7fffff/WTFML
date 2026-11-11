# 测试与验证

## 2026-10-27：下载批次恢复与主计划复核

- 最后脱敏增量 `build/test-runs/20261027-205645-8abd2c9a`：network 五入口全通过，69.97秒。
  包含多URL凭据剥离和真实HTTP失败后任务历史不保存签名查询；普通诊断端点仍保留。
  此后只更新文档，没有继续改动产品代码。
- `build/test-runs/20261027-205222-bb92957e`：完整主窗口2715/2715通过，87.78秒；
  覆盖最终任务归属/初始化目标保护的接线增量。其后的网络错误文字脱敏仅重跑上述相关五模块。
  前一轮203538-f4e58d75同样2715/2715通过（82.51秒），不代替后一轮。
- 正式Release构建最后日志：build/2.2-release-privacy-final-20261027.log（stderr同名前缀），
  目标WTFMinecraftLauncher、6路、300秒截止，成功产物在build/release-mingw13/WTFMinecraftLauncher.exe。
  更早的2.2-release-final/2.2-release-guard-final日志分别是前一阶段，不冒充最后一次产物。
- 最终增量 `build/test-runs/20261027-204847-70e05350`：network 五入口全通过，72.03秒；
  新进程校验恢复沿用原任务ID，新增预设选项/配置/世界与数据包目标变更保护。
  资源更新现复用同一文件批次能力及更新前持久化回滚点；真实界面恢复仍单列待验收。
- `build/test-runs/20261027-203156-f60a1513`：network 五个入口全部通过，70.38 秒。
  命令为 tools/test.ps1 -Suite network -BuildJobs 6 -BuildTimeoutSeconds 300 -TestTimeoutSeconds 180。
  包括直链、重试、分段、安装分段和预设安装；新用例在 DownloadBatchChecks.cpp 中独立链接。
- 新用例覆盖：真实 VersionManager 资源/核心文件批次暂停接续与唯一终态、实例占用、
  取消退役记录、目标变化/发布时同名写入保护、覆盖下载、无来源摘要文件的完成身份记录，
  以及真实新 OS 进程恢复多文件与“补齐后第二次明确继续才调用安装接续”。
  文件响应来自本机 HTTP 服务，不将其写成公网下载或真实 Java 安装器验收。
- tools/check-test-modules.ps1 通过；主计划依据按域源码抽查和补充文档核对重排，
  并非全仓逐行安全审计。采纳/过时/待定项见 PLAN_REVIEW_20261027.md。

保留首次失败与中间结果：165214-935a60d2 误选为全量，已手动终止自身测试树，未算通过；
165616-fe60cfee 因受限执行出现 Permission denied，临时夹具也失败；允许必要访问后，
170634-845c9f62 四入口通过，直链旧等待把“失败后可取消恢复”误当仍在运行，已改为检查任务状态。
171208-dbdc7635 在追加接续代码时遇到 active 布尔误写成函数的编译错误，随后修正。
172002-8a0a0a0d 五入口通过（75.29秒），其后又补无摘要身份及收尾保护，最终以本节最新记录为准。
完整主窗口与正式构建结果另记，不把这些模块通过当作整仓、真机或全部执行器的验收。

## 2026-10-27：2.2剩余项推进

- `100704-f337ffaf`：core-presets736/736、core-activity148/148、core-servers718/718通过。
  早期预设733/733与后续三条语法位置/原件保护分开，不把可选规模分支算成默认核心断言。
- 实际规模：NVMe、USB HDD各150预设、1000真实文件×5实例，逐文件比对和无关文件保护通过；
  现场、时间与首次受限执行失败见docs/PRESETS.md。
- `103030-860cef89`：完整MainWindow2715/2715，包含属性外部修改和Java错误子进程保护。
  前一轮`101031-32c579aa`2714/2714发生在致命Java处理新增之前，不代替后一轮。
- 下载执行器`095547-23e80b42`四分片通过；`100156-9a40510f`、`102636-fdb1e99e`
  补真实新OS进程恢复、失效源换链接后四分片继续通过。
  `103824-02ec8dd0`进一步核对稳定账本目录与恢复记录互斥，四分片通过。
- 服务端真实安装和运行、界面导出、MainWindow向导见docs/SERVER_ACCEPTANCE_20261027.md，
  包括原始失败、修正和不同证据层次，不以假进程代替实网/真实Java。
- `104125-fc7b861f`：正式启动器、网络诊断构建与诊断回归通过；此后直链取消/收尾重入保护
  仍须按最终构建记录复核，不能把前一份产物冒充最新代码。
- `104607-fe17c80d`：`tools/test.ps1 -Suite visual -RunnerMode Compact -BuildJobs 6
  -BuildTimeoutSeconds 300 -TestTimeoutSeconds 180`，完整UI3121/3121通过，包含隐藏向导后的参数读取。
- `104844-11b869c5`：直链取消/阶段重置增量的网络四分片通过。
- 最终薄构建日志 `build/final-2.2-build-20261027.log`：正式WTFMinecraftLauncher与mw_check_modules
  在300秒截止内重新构建；随后仅运行 `ctest --test-dir build/verification-mingw13
  -R ^wtfml.high-speed-download$ --output-on-failure --timeout 60`，1.27秒通过。
  日志 `build/final-2.2-direct-20261027.log`，覆盖真实新进程恢复、换链接、暂停取消与旧记录不复活；
  不为最后两处直链修改重复不受影响的全部网络等待用例。

本节短编号均位于build/test-runs/20261027-…；构建每阶段300秒，测试180秒。
首次`094636-5423f98a`模块名download无效，没有开始测试；`094745-525e63c0`发现
首次创建数据目录前恢复记录无法保存，修正安全创建流程后复测通过，保留原失败。
真实NVMe受限执行退出-1073741819保留原日志；不因为放开测试所需访问后通过就宣称段错根因已定位。
服务端工具首次编译使用错误状态枚举Failed，改为产品定义Crashed；不是产品状态改名。
工具UI文件名选择时序误判与关闭向导丢参数的产品bug分别保留，前者修工具，后者修产品。

## 2026-10-27：2.1.7–2.2.3

本轮按用户确认的必要项门禁、单次替代、部分预设允许启动及只恢复未完成段执行。
实网受限文件与USB HDD/GTNH/FO记录见 `docs/MODPACK_IMPORT.md`；未用夹具替代真实受限文件。

- `20261027-092103-82f9606e`：core-presets 695/695、core-resources 900/900，全部通过。
- `20261027-091858-73070624`：preset-install通过，包含真实执行器的替代二次确认、拒绝、
  选择变化、发布前外部新建同名文件保留；旧场景与新场景均执行，非只跑新增断言。
- `20261027-091446-038c71eb`：upgrade、mod-action-panel通过；preset-install失败保留，
  原因是本轮暂存文件使用另一个QFile改名，Windows仍持有QTemporaryFile句柄。
  改为暂存对象自身rename后由上一条复测通过，没有取消目标不覆盖判据。
- `20261027-012446-efef1e9a`：完整MainWindow 2711/2711通过；此后部署改名和CF解析增量
  由上述相关模块复核，不宣称整仓最终版本所有入口全量重跑。
- `20261027-012031-89fd1cbc`：ci-ui八个入口通过。
- `20261027-092421-ee5c7925`：最终完整ui_check 3119/3119通过（10.31秒）。
- `20261027-092329-596c9e0c`：正式启动器和网络诊断构建成功，network-diag通过。
- 测试入口大小及所有权检查通过；每个构建阶段300秒、测试阶段180秒，有明确截止。

保留首次失败：`001701-e5a39c68` 缺生产provider链接、`002818-6bac57e4`解析接口错误、
`003431-60df2817`新报告使原删除空目录夹具失效、`005355-8cb880bd`及`005529-903024c1`
新增夹具编译问题，均已修正后复测。`013237-6658e6e4`资源清单字节不符为Windows换行转换，
通过.gitattributes固定清单LF并重新生成内嵌数据，资源900/900复测通过。
`014339-f39e4d0b`受限环境本机夹具连接Permission denied不作为产品结论；允许本机通信后复测。
`091653-1ae45edf`新增失败诊断参数编译问题已修正；`092208-18ce9205`正式-Werror编译发现
报告代码缩进歧义，拆行后正式构建通过。以上短编号均位于 `build/test-runs/20261027-…`。

真实导入初次漏传清单指纹，工具虽收到成功但stageProof=false，因此判失败；修正指纹后，
取消、下载期强制终止及新进程恢复全部通过。早期CRT退出后仍占用测试exe，改用Windows
进程终止入口并保留失败现场；验收中临时以新输出名链接同一对象，避免反复等待被占用文件。
这不是关闭杀毒或修改系统安全设置，也不把工具失败记作产品通过。

2026-10-25 P0 深化：真实 NTFS 权限矩阵、还原/整合包跨进程中断恢复、USB HDD 新版阶段矩阵及
未覆盖条件，见 [P0 验收记录](P0_ACCEPTANCE_20261025.md)。该记录明确保留首次失败，不能用复测覆盖。

2026-10-25 非 P1 增量：高级配色入口见 [使用说明](THEME_ADVANCED.md)。
`20261025-142308-afc83d2a` 的 `wtfml.theme` 通过；
`20261025-143826-e67ea5d7` 的主窗口回归 2687/2687，通过真实控制器核对编辑/保存/恢复。
首次沙箱运行 `20261025-143017-33091a97` 在本机下载中出现 `Permission denied` 并于 CTest 240 秒超时；
该失败保留，后一次是允许本机通信后的独立验证，不代表首次运行成功。多 DPI 真机未重验。

`20261025-144023-6f4d555d` 正式程序构建成功（增量构建 101.41 秒），网络诊断通过（18.94 秒）。
备份头依赖以正式目标的 `*.obj.d` 中 `BackupFiles.hpp` 命中数量核对：调整前 14，调整后 8。
这是依赖范围缩减，不是冷/增量速度对比，也不是运行性能结论。主计划保留相应测量余项。

2026-10-26 2.1.6a/2.1.6b/2.1.6c（GTNH 首启 MCLib AssetDirector 资源）：spike 结论 + 预取实现 +
真机冷缓存首启。行为、写入位置、上限与实测细节见[整合包导入](MODPACK_IMPORT.md)的
“MCLib AssetDirector 首启资源”与“2.1.6c 实测”两节。

新增/改动的代码：`src/download/AssetDirectorKnownSet.{hpp,cpp}`（清单校验、消费者识别、复用判据与
计划）、`src/download/AssetDirectorKnownSetData.cpp`（生成的嵌入数据，与
`assets/assetdirector/known-sets.json` 逐字节一致）、`VersionManager::assetDirectorPrefetchPlan()`
/`prefetchAssetDirectorResources()`、`MainWindow::maybePrefetchAssetDirector()`（导入成功后接线）、
`tools/assetdirector-knownset.py`（`--from-cache` 为主、字面量解析为交叉核对）。
断言在 `wtfml.core-resources`（AD1–AD7）与 `wtfml.main-window`（AA12 接线）。

### 离线回归（`build/verification-mingw13`，MinGW 13.1 / Qt 6.10.1 / Release / `-Werror`）

- `wtfml.core-resources` **899/899**、`wtfml.main-window` **2700/2700**（含 AA12）。
- `ctest -L ^ci-core$` 16/16；`verify_downloads 182`、`verify_install 378`、`verify_instances 1545`、
  `verify_launch 300`、`verify_baseline 195` 全通过；正式目标 `WTFMinecraftLauncher` 构建通过。
- 首次 ci-core 是**红的**并保留：`20261026-151824-5e875573` 的 `wtfml.test-modules` 报
  `Core assertion file has no domain owner: OperationReportTests.cpp`（2.1.1/2.1.2 留下的工具链
  缺口，与本次无关）。补 `"OperationReportTests.cpp": "instances"` 后重跑才全绿。
- 入口把这一组改动判成 `full`（`src/manage/VersionManager.cpp` 没有分类映射、`src/MainWindow.cpp`
  算共享）—— 本轮没跑完整 full/visual，只按受影响面跑了 ci-core 全量与 ci-integration 的 window
  分片；`ui_check` 那套没重跑（没动 `ui/`）。给 `assets/assetdirector/` 与
  `tools/assetdirector-knownset.py` 补了映射到 `core-resources` 的规则。

### 真机（2.1.6c，本机）

环境：空数据根（本轮现场 `D:\WTFtest\wtfml-216c-clean`）、只有启动器自己下的
Temurin 21.0.12.1（`mw_check.exe java-self-managed`，走
`GlobalJavaSettings::downloadJavaFromMojang`）、官方
`GT_New_Horizons_2.8.4_Java_17-25.zip`（595,584,698 B，sha1
`fa8a5a934e9a432b885055822c5453304baca005` / sha256 `8fecdf56…67828`，来源
`https://downloads.gtnewhorizons.com/Multi_mc_downloads/`）。启动日志与导入/探针输出留在
`build/gtnh-216c-evidence/`（两趟各一份，不随仓库发布）；原包与两套测试根是临时现场，清掉
不影响任何构建或测试，只是重跑要重新下载（按上面的哈希核对一下再开始）。导入走
`mw_check.exe multimc-import-live`（产品 `MainWindow::importModpackFromFile()`，两问按默认答），
启动走 `verify_instances.exe --launch-main-menu`（产品 `LauncherManager`）。

- 导入 42.7 s（1.7.10 原版/库/资产 + 解开 11972 个覆盖文件）；导入成功后产品**自己预取**
  1356 个对象 + 两份 JSON = 112.6 MiB 下载、复用 12 个，用时 20.5 s。
- 首启 `LAUNCH_MAIN_MENU ok=1 reason=sound-marker elapsedMs=36566`（36.6 s 到主菜单）。
- **启动期间 `assets/asset_director` 1358 个文件 / 118,056,787 B / 最新 mtime 17:03:47 一个都没变**，
  游戏日志里 **0** 条 `AssetDirector`/`Downloading resources`。
- 对照：只预取静态字面量集合的那一趟，AD 启动时又下 612 个对象（+12.5 MB）、到主菜单 152.1 s。
  清单因此改成以“已校验缓存”为准（1368 个对象 / 128,399,283 B）。

本轮同时改了两个验收探针（都是探针自身的问题，不是产品的）：
`verify_instances --launch-main-menu` 改成先 `prepareNatives()` 再 `checkPreLaunch()`（和产品启动路
一致，否则 1.7.10 第一次启动根本没有 natives 目录）；`mw_check` 新增 `multimc-import-live`
与 `java-self-managed` 两个一次性探针，并且**不能**用 `DirectConnection` 直接调
`importModpackFromFile()`（那两问是同步嵌套事件循环，定时器起不来会卡死）。

2026-10-26 合入 main（远程已到 `2e78fca`）：与远程那次「VersionManager 拆四个 .cpp / 主窗口
拆页」的改动合并，唯一冲突在 `VersionManager.cpp` 的匿名命名空间 —— 助手已搬进
`VersionManagerInternals.hpp`，本次把首启资源用的 `modJarNames()` 一并搬过去，业务代码没有
第二份。合并后 `build/verification-mingw13`：正式目标 `WTFMinecraftLauncher` 构建通过、
`wtfml.core-resources` 899/899、`wtfml.main-window` **2711/2711**、`verify_instances`
1570/1570、`ctest -L ^ci-core$` 16/16（报告 `20261026-210107-7fda3d51`）。

没跑的（不作通过）：进世界、多 Java 版本、多 DPI、慢盘/杀毒矩阵；预取失败与取消的真实磁盘现场
（只在离线夹具里验过：取消不删文件、下次跳过）；`tools/assetdirector-knownset.py` 的
`--efr-java` 静态分支本次没重跑（只用了 `--from-cache` 与 `--embed`）。

## 统一入口（Windows / PowerShell 7）

P2 Java 扫描后台化：`20261025-195745-67ed6c08` 主窗口 2696/2696 和 Java 管理专项通过。
测试使用受控扫描提供方检查事件响应、去重、作废及销毁；性能另用 Release 产品探针，
前后数据和配置恢复证据见 [性能基线](PERFORMANCE.md)第六节，不用离屏测试耗时宣称变快。

在仓库根目录运行，先确保 CMake、所用 MinGW 和 Qt 的 bin 目录在 PATH 中：

```powershell
./tools/test.ps1                         # 按 origin/main、未提交/未跟踪改动选择模块
./tools/test.ps1 -Suite affected -BaseRef HEAD~1  # 检查最近一次提交的影响
./tools/test.ps1 -Suite quick             # 明确运行固定快速专项
./tools/test.ps1 -Suite java             # Java 管理
./tools/test.ps1 -Suite mods             # 模组操作栏、迁移与依赖检查
./tools/test.ps1 -Suite upgrade          # 原地升级、完整快照和故障回滚
./tools/test.ps1 -Suite network          # 本机可控服务器下载测试，不访问外网
./tools/test.ps1 -Suite core             # 核心回归
./tools/test.ps1 -Suite window           # 主窗口集成回归
./tools/test.ps1 -Suite full             # 全部非 visual 回归（共享改动/发版兜底）
./tools/test.ps1 -Suite visual           # 完整历史 UI：含尚未定位的生命周期问题
./tools/test.ps1 -Suite quick -NoBuild    # 明确使用现有二进制，不保证对应最新源码
./tools/test.ps1 -Suite full -ListOnly    # 仅列测试，不构建、不执行，不代表验证通过
```

默认构建目录是 `build/verification-mingw13`。首次使用会按 `verification` preset 配置；
CI 使用 `-BuildDir build/ci -ConfigurePreset ci-verification`。已有构建目录沿用其配置，
不静默更换编译器。构建并行默认 2，可用 `-BuildJobs 4` 调整；测试仍串行。
CI 的配置/单目标构建预算显式放宽到 900 秒；本地默认仍是 600 秒。

### 核心套件不下载 Java 运行时（2026-11-04）

`verify.exe` 的「E. Java 运行时选择」一节以前直接调
`GlobalJavaSettings::ensureJavaForVersion()`；本机没有对应主版本时，那个函数最后会去
Mojang/Adoptium 下几百 MB 的运行时。于是"跑单元测试"等于"当场下一次 Java"——CI 上慢网络
偶尔跑满 900 秒的阶段上限，整个 `ci-core` 判死（2026-11-03 那次就是）。

现在这一节默认**只在本机找**（`ensureJavaForVersion(..., allowDownload=false)`）：找到就核对
版本，找不到就明确打印"本机没有，按设置未下载"，不碰网络。要验下载那条路时：

```powershell
$env:WTFML_TEST_JAVA_DOWNLOAD = "1"
./build/verification-mingw13/tests/verify.exe
```

同一处 `runProcess()` 的 5 秒超时现在会 `kill()` + 等待收尸：以前超时直接返回，子进程还活着
（Qt 打 `QProcess: Destroyed while process ... is still running.`，CI 上要另外终止孤儿进程）。

### CI 加速与分片

GitHub 将同一份非 visual 测试集拆成四个并行作业，各自内部仍串行跑测试：

| 入口 | 构建目标 | 验证范围 |
| --- | --- | --- |
| `-Suite ci-app` | 主程序、网络诊断工具 | 主程序编译及诊断工具自测 |
| `-Suite ci-core` | 所选 `verify_<域>` / `verify_baseline` | 核心、测试入口自检、分片完整性检查 |
| `-Suite ci-ui` | 所选 `ui_check_<专项>`；历史模块选 `ui_check` | 控件专项和完整驱动启动检查，不运行历史 visual |
| `-Suite ci-integration` | `mw_check` 及所选 `mw_check_<专项>` | 主窗口及全部集成专项 |

每份 CMake 测试目录自动附加所属分片标签，新测试无需复制维护一份名称白名单。
`wtfml.ci-partitions` 对比四组的并集与 full：缺项、重复、空组都会失败，不能靠漏登记提速。
全量仍直接枚举 CTest 的完整非 visual 集合；增量的具体选择另见下方“按改动选择模块”。
四路有一条失败，原来的 `build-and-test` 汇总检查仍失败；报告附件按分片区分，不互相覆盖。

仅 `ci-verification` 使用 Ninja 和 `-O1 -DNDEBUG`（取代高编译成本的默认 `-O3`）；
验证程序仍保留自己的 `-O0`，严格警告和 `-Werror` 不变。本地 verification/release 不变，
CI 产物不作为正式发布包或性能基准。已有 `build/ci` 若是 Makefiles 生成器，须另选一个
新的构建目录，不能在原目录直接切换生成器。

使用 Mozilla sccache 和 GitHub 仓库内的编译缓存，后续提交只编译缓存未命中的单元；
不缓存整个构建目录，不复用过期二进制跳过测试。缓存服务不可用时允许退回真实编译，
真实编译失败不会吞掉。每个作业仍用 2 个编译进程，避免私有仓库 runner 内存不足。
冷缓存首轮仍需完整编译；实际收益以远端耗时与缓存命中统计为准，不保证固定分钟数。
每次构建后（无论成败）都会输出一次 `sccache --show-stats` 并打印 `CMakeCache.txt` 里的
`CMAKE_CXX_COMPILER_LAUNCHER`，统计只记录、不判失败；缓存变量没生效时能在日志里直接看见，
不用等到"为什么 CI 没变快"再猜。

`ci-app` 在 Ninja 下先刷新已有配置的构建图，再枚举主程序对象，每 32 个编译单元为一个独立阶段，
最后仍构建完整主程序目标（含资源及链接）和诊断工具并执行测试。每批沿用构建超时，
不会重试失败批次；枚举为空直接失败。此举避免所有冷编译挤入同一 900 秒预算，
不减少编译工作量、不保证总耗时缩短；整个作业仍受工作流 45 分钟上限约束。

quick 包含脚本自测、Java 管理、模组操作栏、工作区、任务选择、主题、UI 刷新、生命周期和工具箱外观。
full 不包含完整 visual 套件，也不包含未登记到 CTest 的手动实网探针。
full 通过不等于多 DPI 实机、读屏软件或真实网络全链路已经验收。
full 也尚非严格断网测试：主窗口历史用例会触发后台 Mojang/镜像清单请求，仍需改成显式夹具或拦截。
本机模拟服务器需要允许 `127.0.0.1` 回环连接；受限沙箱里出现 `Permission denied` 时，
应保留失败日志并换用允许回环连接的测试环境，不能据此判断下载实现失效。

### network-diag 在本机怎么跑

`WTFMLNetworkDiag` 是**诊断工具本身**（带窗口）：不带参数运行会开窗等交互，看起来就是"停住"——
这不是挂起。本机验证走 ctest 那条同样的命令：

```powershell
$env:QT_QPA_PLATFORM='offscreen'
build-audit/tools/network_diag/WTFMLNetworkDiag.exe --self-test
```

2026-11-05 本机实测：**48 条全过、19.0 / 18.9 秒、退出码 0**，与 CI 的约 19 秒一致。

所以本机跑全量**不需要**再把 `network-diag` 排除掉。注意 ctest 的两种"排除 visual"不是一回事：

| 命令 | 选到几项 | 说明 |
| --- | --- | --- |
| `ctest -E visual` | 39 | 按**名字**排除；`wtfml.ui` 名字里没有 visual，所以它在里面 |
| `tools/test.ps1 -Suite full` / CI | 38 | 按**标签**排除（`-LE visual`）；`wtfml.ui` 带 `visual` 标签，不跑 |

2026-11-05 实测：`-Suite full`（38 项，含 network-diag）**全部通过、394.28 秒**；`ctest -E visual`
（39 项）也全过、410.98 秒。`wtfml.ui` 那套用 `-Suite visual` 单独跑。

**`-Suite network` 曾经在本机 3/6 失败，根因已查清并修好（2026-11-05）**：`tools/test.ps1` 原来把测试进程的
`TEMP` 指到仓库里的 `build/test-runs/<id>/temp`。在这种受限令牌环境（Windows 沙箱 / 某些企业策略）下，
子进程**只能写工作区**，`QTemporaryDir` 取默认路径就拿到"拒绝访问"，凡是这么干的模块直接返回 2
（症状：ctest 报 `Failed`、0.03 秒、断言只跑了 2 条）。现在 `tools/test.ps1` 改成：

- 默认用系统临时目录；
- 设了 `WTFML_TEST_TEMP` 就用它（受限环境指到工作区即可）：

```powershell
$env:WTFML_TEST_TEMP = 'D:\Desktop\the_project\.tmp-test'   # 本机示例
./tools/test.ps1 -Suite network -BuildDir D:\Desktop\the_project\build-audit -NoBuild
```

2026-11-05 复测：`-Suite network` 现在选 **6 项**（五个下载用例 + `network-diag`），**6/6 通过、107.94 秒**。

2026-11-05 复测：`-Suite network` **5/5 通过、89.16 秒**；`download-retry` / `preset-install` / `network-diag`
单独跑 ctest 也都过（3/3、35.08 秒）。
### 例行检查（改完测试文件跑一遍）

```powershell
./tools/check-test-modules.ps1                      # 测试入口/模块归属/大小上限
./tools/check-doc-links.ps1                         # 文档相对链接与源码里的 docs 引用
$env:WTFML_TEST_TEMP = 'D:\Desktop\the_project\.tmp-test'
./tools/test.ps1 -Suite quick -BuildDir D:\Desktop\the_project\build-audit -NoBuild
```

2026-11-05 实测：归属检查 **PASS**；`-Suite quick` **12/12、11.85 秒**；文档引用检查 **PASS**（129 条 markdown 相对链接 + 48 条源码里的 `docs/` 引用，0 缺失）。
## 按改动选择模块

`tools/test-impact.ps1` 是唯一的路径到测试模块映射。本地默认比较 `origin/main...HEAD`，同时合并
未提交和未跟踪文件；分支基准不同请传 `-BaseRef`，基准无法读取会失败，不静默漏测。
可用 `./tools/test-impact.ps1 -ChangedPath src/controllers/UpgradeAssessment.cpp` 查看计划，
或 `./tools/test.ps1 -Suite affected -BaseRef HEAD~1 -ListOnly` 只列出待运行测试。
报告保留 `impact.json`，写明改动文件、模块、测试名和扩大范围的原因。

- 文档改动或无改动：跳过构建和测试。
- 更新控制器：构建应用，运行更新专项和基础 UI 专项，不运行无关的核心/主窗口巨型用例。
- 模组、网络、预设、Java、工具箱等按关联选专项；不能用“只改一个文件”作为漏掉关联测试的理由。
- 共用主窗口、设置/网络/工具基础层、构建配置、未知路径：退回全部非 visual 测试。
- Actions 的 push/PR 使用同一映射；保留 `build-and-test` 必需检查名。计划失败不能假绿。
- 定时、手动工作流和发版标签仍使用全部四个分区，不通过名称白名单限制全量集合；完整历史 UI
  仍按原定时/visual 开关执行。原分片完整性检查保持不变。

CI 的 `-Modules 'upgrade,ui,app'` 是测试名与所属分区的交集，不会把其它分区的测试重复跑一遍。
`-Suite upgrade` 现在构建独立的 `mw_check_instance_upgrade`，不再顺带编译整个正式程序；产品构建由 affected 的 app 模块
或 CI 的 ci-app 分区承担。显式 `-Suite quick/full` 保留原用途。

测试源码已物理拆分，专项不再依赖编译/链接完整的 ui_check 或 mw_check。
CTest 注册同时生成 test-targets.txt；运行器先选测试，再按归属构建独立目标。
脚本测试无需构建 C++；未知/共用改动仍扩大范围，不承诺未经远端测量的加速比例。

## 人工验收探针（不进 CTest）

这两个脚本需要真机环境，故意不登记到 CTest；常规回归仍以临时目录夹具为主。

```powershell
# 真实产品双进程：第二个窗口必须被拒绝（需要交互式 Windows 桌面）
python tools/acceptance_directory_boundary.py `
  --exe build/verify/WTFMinecraftLauncher.exe `
  --qt-bin D:/Qt/6.10.1/mingw_64/bin

# 大实例复制 / 文件占用 / 升级取消 / 指定阶段外部终止 + 重启恢复
# --kill-stage 让探针停在生产事务的 publish 或 verify 阶段，再由脚本 TerminateProcess；
# 不用固定 sleep（快盘会跑完、慢盘可能还没到那一阶段）。
# --json-out 记录规模、每阶段耗时、卷类型和最长等待；恢复后脚本还会重跑一次完整升级。
# 把 --work-dir 指到慢盘，并用真实杀毒软件实时扫描，才是完整慢盘验收。
python tools/acceptance_large_instance.py `
  --verify-exe build/verify/tests/verify_instances.exe `
  --qt-bin D:/Qt/6.10.1/mingw_64/bin `
  --work-dir E:/wtfml-acceptance --payload-mb 1024 --files 6000 `
  --kill-stage publish --json-out build/acceptance-large.json
```

`verify_instances` 的 `--large-copy` / `--large-upgrade-cancel` / `--large-upgrade-hold` /
`--large-upgrade-complete` / `--upgrade-recover` 是上面第二个脚本使用的探针入口；
`--large-upgrade-hold` 会先打印 `UPGRADE_STAGE`，到指定阶段再打印 `UPGRADE_HOLD` 并停住，
父进程确认这一行后才杀。直接运行 `verify_instances` 不带参数仍然只跑常规回归。

本机 2026-10-24 实跑（1 GiB 单文件 + 6000 个小文件，普通 NTFS，不是慢盘）：大复制 3.61 秒；
文件独占（模拟杀毒/占用）失败 3.92 秒且不发布半成品；升级取消请求后 0.23 秒停下，原实例和
用户文件完整；kill 后恢复 0.03 秒。全部断言通过。真实慢盘和杀毒实时扫描仍应在发布机复跑。
真实双进程脚本同一天实跑 10/10 通过（第二个进程停在拒绝对话框、以 1 退出，日志未被截断）。

2026-10-25 复跑（同为 1 GiB＋6000 文件；D: 固定 NTFS / NVMe，Defender 实时保护为 True，
仍不是慢盘）：大复制 3.70 秒；文件独占失败 3.88 秒且不发布；取消 0.23 秒；外部终止落在
`publish` 阶段（阶段推进到 publish 用了 50.74 秒，随后 TerminateProcess，进程没有自己退出）；
重启恢复 `count=1`、0.04 秒，原 jar / 用户标记 / 1 GiB 载荷完整，未发布的新核心保留；
随后完整升级重试 52.61 秒成功。最长单步等待 52.61 秒（不含人为挂住的时间）。
`verify` 阶段用 16 MiB＋100 文件单独跑过同一条终止/恢复/重试路径。
JSON 与日志在 `build/acceptance-20261025/`。物理慢盘上的同一矩阵仍未复跑。

## 测试代码的模块边界

- tests/core_modules/：81 个原命名用例函数，各一个 .cpp；按下载、安装、实例、Java、账号、资源、
  启动、服务端、活动、预设、工具箱组织成 11 个独立可执行程序。verify_baseline 保留原 main 中的基础断言，
  12 个断言入口的并集覆盖旧核心集合；verify 兼容手动模式，CI 只检查它能链接和启动，不重复运行整套断言。
- 整份 `verify` 与分片的关系（2026-11-05 复核）：整份 `verify.exe` **6457/6457（122.1 秒）**；
  分片各自 exit 0（baseline 195、accounts 402、activity 148、launch 346、instances 1715、java 268、
  presets 867、resources 942、servers 762、toolbox 435、install 378、downloads 244）。
  **分片相加比整份大**：每个分片程序会各自再跑一遍模块级引导断言，所以"核心总量"以整份 `verify`
  或 `ctest` 为准，别拿分片求和当总量。- tests/ui_check/modules/：导航、主题、任务、配置编辑、内容页、账号、日志等各自一个编译单元。
  历史顺序测试继续由 ui_check 编排，保留共享窗口和执行顺序；7 个快速 UI 专项独立构建和注册。
  ui-bootstrap 只检查完整驱动能链接、启动和读取图标，保持历史断言的编译门禁；历史 visual 仍按原策略另跑。
- tests/mw_check/modules/：31 个原命名函数、25 个历史界面接线段和 4 个快速专项各自独立 .cpp。
  已有的下载、预设、更新、Java、模组等专项也各有独立入口，不再链接完整主窗口测试驱动。
- mw_check 的夹具头分三层：SupportCore.hpp 放断言与不碰界面的小工具，SupportFakes.hpp 放假外网与
  预置皮肤夹具，Support.hpp 放真窗口那套 UI 夹具与各节入口声明；Support.cpp 放共用的非模板实现。
  跨段变量显式传参，统计计数共享，历史测试不因拆文件而改变顺序或丢掉断言。
  三个原始入口保留编排及手动探针，不再堆放大段专项实现。
- 生产实现依次复用 wtfml_core_runtime、wtfml_ui_runtime、wtfml_mw_runtime，
  测试程序之间相同实现和 Qt 元对象不重复编译；图标资源在 UI 入口显式初始化。
  共享层分别构建，各阶段单独计时；同一专项的断言对象也供完整入口复用。

例如只改 Java 核心测试文件，可运行：

```powershell
./tools/test.ps1 -Suite affected -ChangedPath tests/core_modules/AsciiAliasTests.cpp
./tools/test.ps1 -Suite ci-core -Modules core-java
./tools/test.ps1 -Suite ci-ui -Modules theme
./tools/test.ps1 -Suite upgrade
```

完整入口仍串行；物理拆分不等于历史共享数据已完成隔离，也不代表历史堆损坏已解决。
原有 vm-batch / vm-fetch-fail / 联网安装等手动模式仍显式运行，不因拆分而偷偷加入常规 CI。

### 夹具生命周期（ui_check 的大模块）

"大测试共享长寿命夹具"这件事先要说清**谁活多久**，否则拆模块只能靠猜。当前事实（2026-11-04 复核）：

| 夹具 | 谁建的 | 活多久 | 模块怎么拿 |
| --- | --- | --- | --- |
| `QApplication` / 主题 / 字体 | `tests/ui_check/main.cpp` 开头 | 整个进程 | 按 `QApplication &` 传（只读） |
| `ui`（`MainWindowUI`）、`window`（`QMainWindow`） | `main.cpp` | 整个进程 | 按引用传 |
| `tabs` / `bar`（底栏）/ 实例下拉框等控件指针 | `main.cpp` 从 `ui` 取 | 整个进程（控件归 `ui` 所有） | 按 `T &` / `T *` 传；**只有真的会换掉这个指针**时才写 `T *&` |
| 各模块自己造的数据目录 | 模块内的 `QTemporaryDir`（`scratch` / `scratch2` …） | 到所在那一层大括号为止 | 不出模块 |

两条约定：

- **`*&` 是例外，不是默认。** 全仓只有一个模块真的会给形参重新赋值：`UiInstallFormLayoutChecks`
  换掉 `loaderCard` / `installFooter`（它把安装页重搭了一遍，后面的断言要看新控件）。
  其余模块的 `*&` 只是历史上照抄签名，读起来像"会换"，实际没有。
- **没人用的形参要删。** `UiContentPagesChecks(QFrame * &bar)` 从来没读过那个 `bar`
  （函数体里那个 `bar` 是 `mods->selectionBar()`，页内自己的控件），2026-11-04 已改成无参 ——
  留着它只会让下一个人以为这个模块依赖底栏。

已按这张表拆出来的（2026-11-04）：

| 从 | 搬到 | 拿什么当夹具 | 断言 |
| --- | --- | --- | --- |
| `UiContentPagesChecks.cpp` U10L（服务器页"现在什么样"，680 行） | `modules/UiServerPageChecks.cpp` | `InstanceSettingsDialog &dialog`（原来就是共用调用方那一个） | 85 条照跑 |
| 同上 U10Q（批量管理，460 行） | `modules/UiBatchManagementChecks.cpp` | 两个名字 + `InstanceSettingsDialog::GlobalValues &global` | 40 条照跑 |
| 同上 U10R（行尾次序 / 详情悬停 / 右键 / 开关显隐，320 行） | `modules/UiRowActionsChecks.cpp` | 同上三个显式形参 | 31 条照跑 |
| 同上 U10N（行尾「详情」进哪一页，225 行） | `modules/UiDetailEntryChecks.cpp` | 同上三个显式形参 | 14 条照跑 |
| 同上 U10M（「下载新 X」去哪一栏，125 行） | `modules/UiDownloadNewChecks.cpp` | 共用 `dialog` + 两个名字 + `global` | 32 条照跑 |
| 同上 U10f（空目录 / 用不上那两句，105 行） | `modules/UiEmptyPageChecks.cpp` | 两个名字 + `global` + 临时目录 `root` | 9 条照跑 |
| 同上 U10S（编辑 / 加入服务器 / 加入选中的世界，240 行） | `modules/UiQuickPlayChecks.cpp` | 共用 `dialog` + 两个名字 + `global`；`mkfile` 用共享的 `uiMkFile()` | 36 条照跑 |
| 同上 U10P（搜索 / 状态筛选 / 可更新 / 同页筛选动画，460 行） | `modules/UiSearchFilterChecks.cpp` | 共用 `dialog` + `modsDir`；行内容用共享的 `uiItemText()` 一类 | 38 条照跑 |
| 同上 U10k（长名字省略 / 从中间截，230 行） | `modules/UiElidedNameChecks.cpp` | 两个名字 + `global` + 临时目录 `root` | 8 条照跑 |
| 同上 U10M-2（拨开关 / 退回详情后不跳回顶，215 行） | `modules/UiScrollAnchorChecks.cpp` | 两个名字 + `global` | 3 条照跑 |
| 同上 U10O（存档页顶上两颗 / 统计小字 / 不跟 junction 走，175 行） | `modules/UiMediumPagesChecks.cpp` | 共用 `dialog` + `root` + `savesDir` | 31 条照跑 |
| 同上 U10d（三条开关，83 行） | `modules/UiToggleChecks.cpp` | 共用 `dialog` + `modsDir` | 8 条照跑 |
| 同上 U10b2（常用目录那一排分两段，87 行） | `modules/UiDirectoryButtonsChecks.cpp` | 共用 `dialog` | 8 条照跑 |

十八段全部拆完（同日）：`ui_check` 仍是 **3177/3177**、断言一条没少；`UiContentPagesChecks.cpp` 从 4060 行降到 294 行，只剩开头的夹具与 18 处调用。最后五段是 U10T / U10g / U10e / U10b / U10a。

**其余大模块的夹具（2026-11-05 复核）**：`UiContentPagesChecks` 拆完之后，把剩下几个大文件按
同一张表过了一遍 —— 没有第二个"共用长寿命夹具"的同类问题：

| 模块 | 夹具 | 结论 |
| --- | --- | --- |
| `UiThemePaletteChecks`（2048 行） | 收进程级的 `tabs` / `bar` | 天生要，比色就比那两个控件 |
| `UiInstanceGroupingChecks`（1081 行） | 收进程级的 `ui` / `instanceList` | 天生要，量的是主窗口那棵树 |
| `UiInstanceOverridesChecks`（1969 行） | 自建 25 个对话框，用完即弃 | 没有共享夹具问题 |
| `UiConfigEditorChecks`（1518 行） | 自建 3 个对话框 | 同上 |
| `UiSkinMeshChecks`（1091 行） | 自建控件树 | 同上 |
| mw_check `MwInstanceGroupsChecks`（1835 行） | 按场景自建 15 扇主窗口 | 同上 |
| mw_check `InstanceUpgradeChecks`（1738 行） | 纯事务/目录夹具 | 同上 |

所以按尺寸继续切它们**没有收益**；要动的话动机得是别的（比如编译时间、领域边界），
而不是"夹具生命周期"。
下一步要把模块按领域拆开时，按这张表判断"这个模块需不需要整份 `ui`"，而不是按文件大小切。
### 历史 UI 的验证边界

2026-10-22 本机 Qt 6.10.1 / MinGW 13.1、offscreen、未设置任何跳过变量时：
`build/verification-mingw13/tests/ui_check/ui_check.exe` 得到 **3118/3118**，退出码 0；
CTest 的 `wtfml.ui`（offscreen）同样通过（9.78 秒）。

此前 2026-10-21 在同一构建目录得到 3055/3108（53 条失败，另有一份拆分前二进制 14:07:54
逐项一致）。现在失败数降到 0、断言总数从 3108 涨到 3118（新增用例），说明那 51 条历史失败
是在 10-21 之后（含当前工作区未提交改动）逐条修掉的，不是靠放宽断言换绿色。旧日志仍保留在
`build/test-runs/20261021-190043-0beb72b5/` 和 `build/modular-ui-baseline.stdout.log`，
用于回看当时的失败标签。

offscreen 全绿仍不等于真机验收：多 DPI（100% / 125% / 150%）、透明背景、动效逐帧和真实
滚轮/光标位置相关的判据不在这一跑里；它们要么直跑 `mw_check.exe`（真屏），要么仍是未完成的
验收项（见 `CODING_PLAN_FULL.md` §2.1）。

`wtfml.test-modules` 检查入口不超过 4000 行、职责模块不超过 5000 行，禁止入口 include `.cpp`，
并检查核心用例的归属表。新增用例应放进相应模块，不通过抬高阈值继续堆积入口。

## P1 2.1.1–2.1.4 验证（2026-10-25）

A/B 阶段四项的自动化证据。运行方式：`cmake --build build/verify --target <target>`
后直接跑对应 exe（本机 Qt 6.10.1 / MinGW 13.1，离屏，串行）：

| 入口 | 结果 | 覆盖 |
| --- | --- | --- |
| `verify_install` | 378/378 | Quilt 列表排序/稳定档/profile 检查、键映射与导入支持 |
| `verify_activity` | 148/148 | 子任务不进任务中心；父任务步骤/取消/失败语义；`setRecovery` 只挂终态；历史只落父任务 |
| `verify_instances` | 1535/1535 | 报告 JSON 边界；暂存续做；同名「跳过已有项」；世界导入冲突；世界设置发布被占用 |
| `verify_resources` | 824/824 | 整合包收尾任一次提前退出都把持久报告退回未完成（含完成报告发布后取消）；Quilt 不再被导入/导出拒绝 |
| `mw_check_instance_upgrade` | 232 PASS / 0 FAIL | 副本模式只复制一次、无 ZIP 快照、源实例不动；父任务只收一个子任务；恢复入口补完/写失败报告；2.1.3 适配替换（平台发行版选择、变更预览、嵌套 JAR 随父替换、失败整趟回滚、变更日志） |
| `mw_check` | 2696/2696 | 主窗口完整集成（含三档同名冲突、任务中心接线、Quilt 导入判据更新） |
| `ui_check` | 3119/3119 | 历史 UI 全量（`QT_QPA_PLATFORM=offscreen`） |
| `ctest -R …` | 7/7 | lifecycle / resource-packs / theme / workspace / activity-selection / tool-features / mod-action-panel |

新增关键判据：

- 子任务（准备核心 / 复制 / 快照）**不进** `active()` / `all()`，也不写任务历史；父任务只在
  整趟结果确定时收尾，子任务失败只改对应步骤，不会把父任务偷偷报成成功。
- 副本模式：源实例复制前后逐字节一致；发布后目录通过路径/大小/SHA-1 校验；
  `cache/backups/instances` 里**没有**为副本新增 ZIP；恢复入口不提供原地回滚。
- 报告：`schema` / 上限 / 越界路径校验；`failureText()` 列出未完成项、原因与下一步；
  暂存自动清理只碰 UUID 形状的目录，有报告的暂存与用户目录不动。
- 世界导入：同名时「跳过已有项」不写目标；取消保留暂存、不发布半份；续做按内容
  （SHA-1，不只大小）跳过已拷好的文件。
- 世界设置：`level.dat` 被占用时发布失败、原文件一个字节不变、世界里不留临时文件。
- 2.1.4 声明覆盖：Quilt 目标（`quilt.mod.json` 的 minecraft/quilt_loader/java 范围）符合、
  quilt_loader 版本不满足时给出原因；旧 Forge `mcmod.info` 与 `@Mod` 注解（常量池解析）在
  Forge 目标下符合；嵌套 JAR 声明解析失败 → 未知且默认“请选择处理方式”；Java 要求高于当前
  Java 时默认禁用；加载器版本范围不满足时给出原因。未知原因可从 `upgradeUnknownMods` 取得并
  进入启动器日志。
- 2.1.3 适配替换（临时目录夹具）：平台发行版带安全文件/SHA-1 时逐项出现「替换为平台适配版」；
  预览列表包含旧文件 → 新文件、版本与哈希；选保留会退出替换计划。嵌套 JAR 不单独替换，
  父 JAR 换掉后嵌套内容随之更新。替换哈希不符时 `prepareAndCommit` 整趟失败：原实例
  `mods/old-mod.jar` 不变、不产生新文件/变更日志、事务暂存被清掉。成功后实例内
  `wtfml.update-changes.json` 有 `replace` 记录和嵌套说明；同名替换（新文件名与旧文件相同）
  同样先移除旧文件再写入校验过的新文件。
  真实的平台下载/必要前置解析由 `PresetModInstaller` 既有 40+ 场景覆盖；本轮未做真实
  平台冷缓存下载、真实游戏组合和慢盘验收（仍归 2.1.5）。
- 2.1.4 声明覆盖（临时目录夹具）：Quilt 版本列表排序/稳定档/profile 检查；`quilt-loader`
  键映射到 Quilt 且整合包导入不再拒绝；Quilt 声明（含 `quilt_loader`/`java` 范围）在 Quilt
  目标下符合、版本不满足时给出原因；旧 Forge `mcmod.info` 与 `@Mod` 注解在 Forge 目标下符合；
  嵌套 JAR 声明解析失败 → 未知且默认“请选择处理方式”；Java 要求高于当前 Java 时默认禁用；
  加载器版本范围不满足时给出原因。未真实联网安装 Quilt，也未在真实 Quilt/Fabric 混合模组集
  上验证运行兼容。

这些是临时目录 / 受控句柄夹具，不等于真实慢盘、杀毒实时扫描、断电或真实游戏组合验收；
真正游戏/加载器组合仍归 2.1.5。

## P1 2.1.5 真实升级、回滚与主菜单验收（2026-10-26）

本机 Windows 11 10.0.26200、D: 固定 NTFS（NVMe，Defender 实时保护 True），
使用既有真实版本目录的**干净副本**（未加载用户模组），离线身份启动到主菜单。
不是慢盘、杀毒实时扫描、断电或全部模组组合证据。

### 真实升级 / 回滚 / 主菜单

四个加载器都在临时根里跑生产事务（`InstanceUpgrade::prepareCopyAndCommit` /
`prepareAndCommit` / `rollback`），再用生产 `LauncherManager::generateJvmArguments` +
`startGame` 启动。主菜单判据是 `Sound engine started` / `OpenAL initialized`；Forge/NeoForge
只到主菜单（按人工确认，不强求进世界）。结果（秒）：

| 加载器 | Java | 复制更新 / 启动 | 原地更新 / 启动 | 回滚 / 启动 |
| --- | --- | --- | --- | --- |
| 原版 26.3 | 25 | 0.58 / 15.07 | 8.65 / 16.10 | 0.43 / 10.61 |
| Fabric 0.19.5 + 26.3 | 25 | 1.37 / 16.15 | 7.83 / 16.42 | 0.44 / 10.55 |
| Forge 47.4.10 + 1.20.1 | 17 | 0.36 / 13.91 | 3.09 / 14.67 | 0.40 / 13.96 |
| NeoForge 21.11.13-beta + 1.21.11 | 21 | 1.05 / 11.69 | 5.19 / 11.29 | 0.41 / 11.31 |

四种都以 `sound-marker` 结束。升级后写入的 `saves/AfterUpgrade/level.dat` 在回滚后
保留在 `cache/instance-upgrades/<txn>/replaced/`，原进度恢复。证据：
`build/acceptance-215/evidence.json` 与 `build/acceptance-215/launch-*.log`。

### 游戏目录改名后的恢复（2.1.5a）

Forge 实例在 `publish` 阶段模拟中断后，整个游戏根从 `forge` 改名为 `forge-moved`；
`recoverInterruptedInRoots` 0.04 秒完成恢复（2 条消息），原 jar 与用户进度完整，
随后启动到主菜单 14.07 秒。journal 中 `rootFingerprint` / `rootRelative` / `rootName` 已断言；
指纹被篡改时不动盘、输出“找不到匹配，请使用完整 ZIP 快照”提示。

探针入口（`verify_instances`，不进 CTest）：

```powershell
verify_instances --upgrade-copy <gameDir> <sourceId> <newId> <targetCoreId>
verify_instances --upgrade-inplace <gameDir> <instanceId> <targetCoreId>
verify_instances --upgrade-rollback <gameDir> <instanceId> <transactionId>
verify_instances --upgrade-inplace-crash <gameDir> <instanceId> <targetCoreId>
verify_instances --upgrade-recover <gameDir>
verify_instances --launch-main-menu <gameDir> <versionId> <javaPath> <xmx> <timeoutSec> <log>
```

`--launch-main-menu` 先看声音/资源强标记；否则要求游戏窗口出现且进程继续存活 20 秒。
这不是 UI 点击路径，但参数、Java、natives 和进程管理走生产 `LauncherManager`。

### 取消 / 发布阶段外部终止

同一轮用 `tools/acceptance_large_instance.py`（16 MiB＋100 文件，`--kill-stage publish`）
复跑：大复制 0.10 秒、占用失败 0.10 秒、取消 0.23 秒、publish 阶段外部终止后恢复 0.04 秒、
重试完整升级 1.48 秒，JSON 见 `build/acceptance-215/large.json`。物理慢盘/杀毒仍缺。

### 边界

- 实例是干净副本，未加载用户模组；主菜单通过不等于所有模组组合、进世界或长期玩法通过。
- 未做真实公网下载；启动使用本机既有 assets/libraries 和 Java 运行时。
- 大实例脚本本轮只有 16 MiB/100 文件；1 GiB/6000 文件与物理慢盘记录仍见前节和验收记录。

## 超时、日志和失败

- 构建前关闭正在运行的 `WTFMinecraftLauncher`。`-NoBuild` 和 `-ListOnly` 不关闭窗口。
- 配置和每个目标的构建默认各限 600 秒；整个 CTest 阶段默认限 900 秒。可分别用
  `-BuildTimeoutSeconds` / `-TestTimeoutSeconds` 调整，均不得超过 900 秒。
- CTest 内各专项原有超时继续生效。核心保留 900 秒预算，避免把 CI 慢误判为死锁。
- 详细断言输出写入日志，控制台只显示末尾摘要；每 30 秒显示阶段、已耗时间和最新日志。
  超时终止该进程树，阶段记录退出码 124，脚本返回非零。
- 每次报告放到独立的 `build/test-runs/<时间>-<随机号>/`：阶段 stdout/stderr、
  `summary.json`、CTest `results.xml`（CTest 正常收尾时生成；超时以 summary 和日志为准）。
  阶段失败后停止后续阶段，不自动复跑覆盖首次失败；
  CTest 本身仍会完成本组其余用例，便于一次收集多个问题。
- 脚本运行期间 TEMP/TMP 指向本次报告内的 temp，退出后恢复环境；临时材料保留供诊断。
  不把这些临时文件自动上传，避免大文件或敏感材料混进 CI 附件。
- 同一构建目录使用命名互斥锁，第二份脚本直接报错，不与首份争用产物。
  这不能阻止绕过脚本的手动构建；运行脚本时不要另开构建。
- 旧测试仍有固定数据目录，尚未完成逐测试隔离。因此不能擅自提高 CTest 并行度。
  具体形状：`mw_check` 系的沙盒是 `applicationDirPath()/check-settings` 与
  `applicationDirPath()/.minecraft`（后者是产品口径的"启动器级默认数据根"，
  `TaskHistory` 等账本就落在那儿），同目录下的各专项 exe 共用这两个位置 ——
  并行跑就会互删对方的设置与账本。要并行，得先让每个进程有独立的 applicationDirPath
  （或让产品支持测试专用的默认根注入），这不是调一下 `-j` 能解决的。
  2026-10-22 的 `-j2` 全量只作为观察：38/38 通过（246 s / 239 s），串行同样通过
  （213.94 s），并行没有带来净收益，也不作为"已安全并行"的证据。

GitHub 的 main 提交和 PR 按改动选择分片与测试；标签、定时和手动强制全量保留四路兜底。
每周和手动勾选 visual 时另跑完整 UI，不自动跳过已知问题。
同事件、同分支的新提交取消旧运行。日志与 JUnit 附件无论成功失败均尝试上传，保留 14 天。
本地和 CI 对齐 Qt 6.10.1 / MinGW 13.1；本地并不自动下载工具链。

## 新增测试约定

- 小专项实现放独立 `.cpp`，`.hpp` 只声明入口；不要再把整段实现 include 到巨型 main.cpp。
  用例放在所属模块，入口只负责夹具、调用顺序和退出状态。
- 每个专项登记到 CTest，设置标签、独立超时和运行环境，避免只在开发者手动命令中存在。
  使用 `wtfml_add_test(NAME ... COMMAND <独立目标>)` 同步登记构建归属；漏归属或重复归属明确失败。
- 每项使用独立临时目录、临时端口；夹具失败明确报错，不能让依赖该夹具的断言“假绿”。
- 功能断言和性能基线分离；不能用 offscreen、调试器或未优化构建的速度代表产品性能。
  构建/启动/CI 的既有基线与量法见 `docs/PERFORMANCE.md`。
- 已知崩溃保留首次证据、独立复现，不以“重跑直到绿”为验收规则。

## 测试入口自身的回归

`./tools/test-runner-check.ps1`（CTest 名 `wtfml.test-runner`）会用独立的带空格/中文路径
构造成功、断言失败、超时和空测试集四种情况，验证退出码、环境恢复、独立报告和 JUnit 生成。
这些故意失败的内部样例不混入普通 JUnit 报告；原始输出保存在
`build/test-runner-selfchecks/<随机号>/`，只有入口自身判断失败才使本项变红。

## 后续结构优化

### 2026-10-20 CI 失败核对

- 运行 `39304391380`、`39356133554`：主程序冷构建到约 85%/165 个对象时触发单阶段
  900 秒上限（退出 124），不是编译语法错误。现有 `ci-object-batches.ps1` 将 Ninja 对象
  分批构建，保持每阶段 900 秒保护，不通过无限延长超时解决。
- 运行 `39431626583`：其它分片成功，ci-app 构建成功，唯一失败断言为网络诊断的
  `expired node is reprobed once and missing ranges complete safely`；汇总检查失败属于级联。
  安装 Qt 时的 Python EACCES 警告没有使安装步骤失败，不是本次根因。
- 加固该专项的 HTTP 夹具：仅按请求行目标路径路由，不在请求头/查询串里匹配地址；
  完整请求只响应一次。保持严格的两次调度探测断言，次数不符但下载成功时也输出计数、结果与引擎日志。
  旧日志没有实际计数，因此不能据本地通过就宣称已经确定 CI 那次断言的唯一根因。
  夹具修正连续五轮通过（95.31 秒）；新增路由回归后重建，最终再连续三轮通过（57.09 秒）。
- 本机沙箱里另有 H1 协议记录断言失败，允许回环通信后通过；与远端的过期节点断言不是同一问题。

### 2026-10-22 假外网按请求目标路由

`tests/mw_check/modules/SupportFakes.hpp` 的 `FakeNetwork` 现在按请求行路径（去掉查询串）路由，
`hits`/`paths` 记每一发的次数与原文，`notFound` + `unrouted` 记没演到的请求并回 404。
构造 `MainWindow` 的两个入口（`wtfml.main-window`、`wtfml.workspace`）都在构造前把五条
`Launcher/*Base` 写进沙盒 INI，并在收尾断言 `notFound == 0`；断言只看次数，不看响应次序。

这条断言第一次跑就抓到一处夹具合同错误：`sessionServerBase()` 回的是 `…/session`，而产品
（`PlayerSkinStore::setEndpointOverrides()`）拿到的是完整档案前缀、之后直接拼 uuid，于是皮肤
档案请求打到 `/session/<uuid>`（真实 sessionserver 没有这条路由）上，一直被夹具 404 ——
界面表现只是"没有皮肤"，测试却全绿。夹具改为 `…/session/minecraft/profile` 后请求落回被
演到的那条路由。以后新增的网络入口若没被演到，这条断言会直接变红。

尚未完成：所有模块路径审核、隔离后的安全并行、多 DPI 截图基线和内存诊断工具链。
ui_check 那条历史堆损坏已收口（证据与复验见下面「历史崩溃复核」），不再算未完成项。

### 世界导入空间预检与预设读取边界

世界导入 ZJ7：通过只降低实际可用空间的预算参数，验证零空间、隐藏文件计入容量、
32 MiB 余量及空间恢复后成功导入；拒绝发生在暂存目录创建前，源存档不变。
`build/test-runs/20261024-231046-e2a4e5ac` 实例模块 1479/1479。
这不是实际填满磁盘或写入过程中其它程序耗尽磁盘的验收；空间检查不能预留磁盘。

预设索引现区分缺失与打开失败/读取失败，不再把已有不可读索引当新用户空索引。
索引上限 4 MiB、清单上限 16 MiB，读写两侧一致；超限保留原文件。
PA11 覆盖目录占用索引路径、超大索引和超大清单，不声称覆盖所有 ACL 场景。
PA3 补充排序/default 字段 null、对象、非字符串数组项及布尔值拒绝判据，防止错误类型被
当作空索引写回；缺字段仍兼容旧索引。
最终组合验证：`build/test-runs/20261024-231403-01d8b009`，实例 1479/1479、预设 678/678，
执行合计 22.29 秒。未进行真实磁盘写满、ACL 拒绝或大规模预设实机部署验收。

### 主窗口测试默认根隔离

`mw_check/main.cpp` 与共用的 `modules/ModuleMain.cpp` 在测试控制器初始化前建立每进程
`QTemporaryDir`，注入 `wtfml.testDefaultMinecraftDir` 应用属性并重定向默认 INI 设置。
这同时隔离默认游戏根及使用默认根的任务/历史账本；当前游戏根恢复为空时也只返回该沙盒。
正式程序入口不设置该属性，便携式目录规则不变。模块入口检查默认根及默认设置文件均在
独立目录内。各模块自行覆盖的固定路径、截图和子进程模式仍需审核，不能据此直接开启并行。

高速下载截图已移入该测试夹具目录，只作本轮渲染验证，不再覆盖 exe 旁的固定截图。
断点续传子进程同样先建立独立设置和默认数据根；其工作目录由父进程指定为本轮夹具，
故意 `_Exit` 留下的子沙盒由父夹具回收。下载目标和续传缓存仍显式传递，未改变恢复协议。
验证记录：`build/test-runs/20261024-213703-14906487`，下载重试、高速下载、分段续传、
安装分段接管四个模块全部通过，执行合计 63.76 秒；含子进程硬退出后的续传恢复。

工作区另起真实 `sandbox-probe` 子进程：父设置写哨兵，子进程确认读不到、写入自己的哨兵，
父进程再确认原值保留且读不到子值；同时核对默认数据与设置路径属于子目录，以及正常退出后
目录确已回收。探针不创建主窗口或联网，启动/结束等待分别限 5/15 秒，超时只结束该子进程。
验证：`build/test-runs/20261024-214635-43b35350`，工作区 36/36、任务选择 36/36。

2026-10-24 验证：工作区 32/32、任务选择 36/36
（`build/test-runs/20261024-190936-7867d634`）；主窗口 2670/2670，执行 93.22 秒
（`build/test-runs/20261024-191430-9e580103`）。均为串行运行，不是并发安全验收。

### 整合包最后提交标记占用（2026-10-24）

`ModpackCompletionTests.cpp` 的 ZG9 在 Windows 上使用允许读写共享、但不允许删除共享的
原生文件句柄占住 `wtfml.importing`，实际调用生产收尾流程。断言最后删除失败时返回失败、
报告退回未完成、启动门禁仍生效、安装文件和安装凭据保留；关闭句柄后，同一凭据在没有
原压缩包的情况下重试成功。这不是磁盘写满、断电或强制结束进程的模拟，后三者仍待验收。

ZG9 另外在取消回调中观察完成报告，等完成报告实际落盘后才请求取消，验证发布门禁前
仍检查取消、将报告退回未完成并保留标记；随后正常重试必须成功。生产流程增加写报告后
的取消/标记绑定重检，避免慢盘写报告期间收到的取消被最后一步忽略。

此前真机补齐流程已从实际启动器以离线 UIInstallTest 身份进入 Minecraft 1.21.11 / Fabric
主菜单并正常退出（退出码 0），未打开存档。截图位于本地忽略目录
`build/gtnh-install-probe/ui-completion/main-menu-after-completion.jpg`；该夹具不代表完整大型包验收。

### P0 世界写入与恢复探针（2026-10-25）

`WorldFiles::readLevelData` 确认读取期间文件一致，并记录原始 SHA-256；写入临时文件后、原子发布前
核对指纹，拒绝覆盖外部修改。此检查不是对任意外部写入者的事务锁。
实例模块 1483/1483：`build/test-runs/20261025-113734-2c8e0b97`。

`acceptance_large_instance.py` 要求空工作目录，失败/异常保留现场；载荷以 SHA-256 校验。
恢复探针使用生产 Publish 回调 `_Exit(86)`，要求 marker 和恢复 count>0，不再按固定时间杀进程。
16 MiB＋100 文件隔离复测通过，恢复 1 条事务，夹具保留在 `build/acceptance-p0-20261025-publish`。
这是流程回归，不是慢盘或断电验收；USB HDD 的独立记录见 `ACCEPTANCE_20261025.md`。

完整 UI 重建后 3119/3119（`build/test-runs/20261025-113931-d670e02a`）；再用 CTest
`--repeat until-fail:5 --timeout 60` 连续运行 5 次，48.28 秒全部通过，未启用 U10O/junction 跳过。
日志 `build/acceptance-p0-ui-repeat-20261025.log`。本轮没有复现历史堆损坏，不能据此认定根因已修。

### 历史崩溃复核（2026-10-24）

- **core / verify 的 WER 转储**：根因是 ZJ3 夹具用 `std::thread` 调 QtCore；已换 `QThread`，
  当前 core 测试不再产生该转储。
- **ui_check offscreen `c0000374`**：同一条 exe 把 `QAccessible::setActive(false)` 改成 `true`，
  连续 **10/10** 崩在 `InstanceSettingsDialog::~InstanceSettingsDialog()` 的
  `QHash<QString, QString>` 释放（`QArrayData::deref`）；保持 `false` 连续 **50/50** 通过
  （3119/3119）。offscreen 插件下 A11y 恒开，而产品 windows 插件只有读屏软件连着才开，
  所以这是验证环境隔离，不是产品读屏/A11y 验收。
- **PageHeap 下那条 `m_fileDirs` use-after-free：已定位并修掉**（2026-10-24）。
  用 `gflags /p /enable ui_check.exe /full` 跑完整 `ui_check`，会在
  `InstanceSettingsDialog::~InstanceSettingsDialog()` 释放 `m_fileDirs` 时崩在
  `QArrayData::deref()` 那句写上面 —— 那块 `QArrayData` 的页已经释放了。cdb 下抓到的
  现场是：崩的那一块正是**「游戏目录」那条路径**（`root + "/game"`，54 个字符），
  可它的释放点比崩点早一整节。

  根因在 `UiContentPagesChecks.cpp` 的 U10b 那一段（量「那一排快捷方式开哪个目录」）：

  ```cpp
  QObject::connect(&dialog, &Dlg::openInstanceFolderRequested, &dialog,
                   [&lastPath, &opens](const QString &path) { lastPath = path; ++opens; });
  ```

  上下文是 `&dialog`（活到整个 U10 块结束），`lastPath` / `opens` 却是**那一节栈上**的
  变量 —— 这一节一结束它们就没了，连接还留着。而产品把每一页的 `openFolderRequested`
  **信号接信号**转成 `InstanceSettingsDialog::openInstanceFolderRequested`
  （`createFilePage()` 那一段），于是 U10e 点一下模组页的「打开文件夹」，
  那一发就回到这个悬空 lambda：`lastPath = path` 的 `QString::operator=` 先释放旧值，
  而旧值那份 `QArrayData` 正是「游戏目录」那条路径 —— 提前放掉，几分钟后在析构里再碰它。
  正常入口下这一下只是悄悄把一块数据提前释放（**这就是那条“偶发 `c0000374`”的另一半**），
  只有 PageHeap 会把它变成当场 AV。

  修法：连接上下文改成**和被捕获变量同一个作用域**的栈上 `QObject`（`QObject scopeGuard;`
  + `connect(..., &scopeGuard, ...)`），块一结束连接就断。**规则**：`ui_check` 里凡是
  `[&]` 捕获栈变量的连接 / 定时器，上下文不许用活得比它久的对象（`&dialog`、`&window`、
  `overlay`、`page`）；定时器那一侧同理（见 `Support.hpp` 的 `dismissNextInline()`）。

- **顺手修掉一条夹具串扰**：`dismissNextInline()` 的 300 ms 兜底在 0 ms 那一发已经答完
  之后并不会自己撤销，于是**下一条**提示（300 ms 内开出来的）会被它当成自己的“用户答了”，
  按的还是它自己那一号按钮（默认第 0 颗 =「是」）。PageHeap 下能稳定看到
  `UiGroupEditorChecks` 的「恢复默认分组设置… ⇒ 点『否』」真的把清单换成了默认五组。
  现在按“票号”走：只有最近一次调用排下的两发有效，而且**兑现一次就作废**。

- **一条已排除的假设（2026-10-25）**：当年那条“约 25% 崩在 `InstanceSettingsDialog` 析构”
  还被猜过是 `ScrollBarStyler` 在 `QEvent::Show` 里**同步** `QScrollBar::setStyleSheet()`
  重入 style polish、让 `extra->style` 指向已释放的 `QStyleSheetStyle`（旧提交 `1a6ed8b`
  为此改过一版 0 毫秒延后，那版从未进过 `main`）。用同形探针（应用级 QSS + Show 过滤器里
  同步挂控件级样式 + 每轮改样式表 / 调色板逼 Qt 重建 `QStyleSheetStyle`，随后析构挂着
  样式表的滚动条）在 **full PageHeap** 下跑了 500 轮，延后版再跑 500 轮作对照，均无 AV；
  真实入口 PageHeap 6/6、正常入口 50/50 也都没事。所以**保持同步实现**、不改 0 毫秒延后
  —— 那会让新滚动条晚一个事件循环才有样式（第一帧先按 `palette(...)` 近似值画），
  换不到可测收益；真正的凶手是上面那条悬空连接与 A11y 路径。
  探针源码与二进制留在 `build/theme-reentrancy-probe/`（不进仓库）。

- **mw_check T17**：合成鼠标两发点实例行空白区时，`QTreeView` 在 `AnimatingState` 会把
  按下整条吞掉（`expandOrCollapseItemAtPos()` 开头那条 `state != NoState`，见
  `ui/RowActivation.hpp` 文件头），`itemClicked` 一发都不发 —— 与产品逻辑无关。改成和
  T10 同一条信号路（`clickTreeRow`），行内坐标与「不在三颗按钮上」仍由前提断言钉住。

- **复验**：PageHeap 下完整 `ui_check` 连跑 6 趟全部跑完，只剩 Z8 那条
  「两万行点一下 ≤ 250 ms」会红（252~300 ms —— PageHeap 的分配开销，正常入口 59~74 ms）；
  关掉 PageHeap 后正常入口 **50/50** 通过（`tools/stress-ui-offscreen.ps1 -Runs 50
  -StopOnFailure`），`wtfml.main-window`（全量 mw_check **2684/2684**）与
  `wtfml.instance-upgrade` 通过，`ui_check` 每趟 **3119/3119**。诊断结束已执行
  `gflags /p /disable ui_check.exe` —— PageHeap **不许**留在机器上
  （它会把正常入口的 Z8 变成假红，也拖慢一切）。
- 压力入口（不进 CTest）：

```powershell
./tools/stress-ui-offscreen.ps1 -Runs 50
```

它会逐趟保存日志，首失败保留在 `build/ui-stress/<时间>/` 并返回非零。
真实读屏/A11y 仍未验收。

凭据日志回归：`ci-core -Modules core-launch,core-accounts` 验证按值遮盖、所有分块边界、
逐字符输出、EOF 残片、重叠凭据和报告导出；`ci-integration -Modules window`
包含真实子进程 stdout/stderr 分块测试及在线 BAT 导出检查。仅使用合成凭据和隔离身份，
不打印实际账号令牌。游戏/模组自行写日志及系统进程参数不属于启动器输出过滤的保护范围。

2026-10-25 凭据修复验证：`20261025-202329-6593da47` 的 core-accounts **365/365**、
core-launch **300/300**；`20261025-203357-93148414` 的 main-window **2701/2701**。
首次受限环境运行 `20261025-202509-8681ed06` 因本机夹具权限失败在 180s 截止，保留原日志；
允许本机夹具后的首轮 `20261025-203151-a4d4d20b` 为 **2700/2701**，唯一失败为新增断言
未适配 Windows CRT 的 CRLF，修正测试比较后完整复测通过，生产输出仍保留原换行。
`20261025-203638-427941dc` 普通启动器构建及 network-diag 应用测试通过（19.06s）。

P2 任务行局部复用：`20261025-205703-6211941e` 的 ui-refresh/install-layout/lifecycle/theme
四模块通过；`20261025-205805-3251de49` 的 workspace/activity-selection 通过。
ui-refresh 新增持续进度下的未变行保留、滚动位置、选中详情、替换回调、调色板失效与移除释放检查。
性能用独立 Release 合成负载测量，数字及首失败记录见 `docs/PERFORMANCE.md` 第七节。
`20261025-210149-6ce322e2` 普通启动器构建与 network-diag 通过（18.97s）。

P2 大量预设：`20261025-213510-15e5e224` preset-install通过（1.15s），新增模型过滤、
默认身份、坏项、实际可见选择与后台目录加载检查；受控信号量等待最多3秒，测试自身最多4秒等待，
不等待全局线程池，不读写真实预设目录。删除页面后的断言等待实际加载状态结束，避免提前销毁掩盖迟到回调。
`20261025-213614-450707b8` 四个相关UI/主题模块通过（2.50s）。性能数据独立见性能文档第八节。
`20261025-213737-03997e20` 普通启动器构建与network-diag通过（18.96s）。

2026-10-26 合并 P1 与 VersionManager 拆分复核：144 个成员定义逐一比较，业务语句未变化。
独占构建后 `20261026-014245-afb441e5` ci-ui 8/8、
`20261026-014427-520e7bac` ci-integration 12/12（主窗口 2698/2698）、
`20261026-015352-bc469279` 正式启动器构建及 network-diag、
`20261026-015425-3101bd66` visual 全部通过。
集成测试新增控制器随窗口销毁、停用限速后唤醒等待队列的真实本机 HTTP 回归。
拆分完成前的 ci-core 16/16 位于 `20261026-011825-36230178`。
保留失败记录：`20261026-012538-9c9732a5` UI 链接失败，独占重新配置后未复现；
`20261026-014352-26b871f0` 新测试误用不存在的 cancelAll，改用 cancelBatch 后复测通过。
下载基准的参数溢出、非法数值与超时上限已检查；短负载仅验证入口可运行，不作为性能结论。

2026-10-26 模组页紧凑布局与预设后台刷新：
`20261026-020415-7c8d0888` mods/presets 两模块通过；
`20261026-020613-61c921fc` 补空目录安装入口、宽屏同行、窄/矮窗口操作可达性后 mods 通过；
`20261026-020636-08cbbc76` 正式构建及 network-diag 通过；
`20261026-020924-761aad4d` visual 3119/3119。
布局截图保存在 `build/mod-layout-review/`，使用合成模组文件，不含真实账号。
预设回归用信号量控制真实后台读取，验证增删改刷新不阻塞输入、完成后恢复操作并保留结果提示。
失败证据保留：`20261026-020120-88c99250` 误用模块别名（应为 mods）；
`20261026-020137-f9697cbf` 测试遗漏 setDownloadText 的提示参数；
`20261026-020729-afc9ce60` 3117/3119，两条旧布局断言仍要求已退役的独立刷新工具行，
更新为搜索与维护菜单同行、筛选为空时入口仍可用后复测全通过。

2026-10-26 P2 服务器收藏：`20261026-092219-218ed5bc` core-servers 718/718；
`20261026-092716-f8a13aeb` core-launch 309/309（含不可写 INI 的保存失败夹具）；
`20261026-092749-176298cd` 主窗口 2706/2706；
`20261026-093153-10b9ee0c` 正式构建与 network-diag 通过；
`20261026-093303-bf79ff84` visual 3119/3119。
真实窗口开页两次验证收藏持久化、隐藏行解除选择、测速下标不变、取消收藏后的空状态；
同时逐字节及修改时间核对 servers.dat 未变化。端点归一化覆盖大小写、IPv6、显式默认端口与非法地址。
首次集成构建 `20261026-092357-f7d3d543` 因新增测试引用作用域外 ui 变量并误用 panelName 无参接口失败，
修正测试查找后复测通过，原日志保留。

2026-10-26 P2 预设重复扫描：`20261026-093654-53701f76` core-presets 678/678，
保持索引顺序、未登记目录追加、坏项隔离等判据；`20261026-093752-c1d5c4c1` preset-install 通过。
新用例在临时默认数据根中提供1000个已加载名称（不创建对应清单），再走真实 controller 新建与刷新，
确认使用缓存名分配“新预设 1001”，目录读取器只用于初始加载和创建后刷新各一次。
测试不会将预设写入实际用户目录；controller 析构后恢复默认根属性。
`20261026-094048-728d32a6` 正式构建与 network-diag 通过。
本轮未新增端到端 Release 性能数据，不给出加速倍数；单份大清单和真实多任务负载仍保留在计划中。

2026-10-26 P2 预设详情后台读取：`20261026-113225-8ab8980d` preset-install 通过，
新增测试独立在 `tests/mw_check/PresetManifestChecks.cpp`，仍归属 presets 分片，避免扩大日常回归范围。
受控读取验证界面输入可响应、单读取与最新待选合并、旧结果丢弃、清除当前项/筛空、错误状态、
勾选保留、页面销毁及换根；默认读取器另用临时目录中的真实4 MiB清单与损坏JSON验证读盘/解析路径。
这不是端到端耗时基准；兼容预览准备及大量段落的界面渲染仍需后续处理。
`20261026-112931-afee8cbf` 正式构建与 network-diag 通过；测试模块大小/归属检查通过，
新测试文件的影响映射只选 preset-install。
首次 `20261026-112044-93b9b923` 新测试误用不存在的 setSearchText 接口而编译失败，
改为操作已有搜索框后复测通过；该失败日志保留。

2026-10-26 P2 应用计划后台预览：`20261026-121807-505c1567` preset-install 通过，
同时复验上一轮详情加载与4 MiB清单；详情与预览共用有界只读调度，分别只保留一个实际作业与最新待处理请求。
新检查独立在 `tests/mw_check/PresetPreviewChecks.cpp`，覆盖输入响应、取消、目标/预设切换、
运行状态变化、读取错误、游戏目录改变、列表移除目标、同ID列表刷新及页面销毁。
默认准备器用真实临时预设与 options.txt 生成计划，并核对内容、修改时间及目录树均未变化，
没有备份或暂存写入。`20261026-121924-72fabf0f` 正式构建及 network-diag 通过。
测试模块大小与归属检查通过，新检查仍在 presets 分片内；本轮未重新测量端到端 Release 性能。

2026-10-26 P2 Windows 长路径：`20261026-134526-d7250a6f` 的 core-instances、core-resources、
core-presets、core-toolbox 四分片全部通过（50.74s）。新增 LongPathTests 归 core-instances，
仅 Windows 执行，临时目录中验证超过260字符的模组/世界操作、真实独占句柄占用失败，以及
超过600字符目标的 ZIP 整包/子树解压、打包/条目读取/批量元数据/GZip；恶意条目不越界。
本机 LongPathsEnabled=0，未修改注册表；PowerShell5.1 后端普通长路径失败、扩展路径成功的探针
保存在 build/zip-long-path-probe.ps1。该结论仅覆盖这些入口，不代表外部 Java 或真实 ACL 验收。
首次132350发现 ZIP 准备失败只报退出码；133637新增用例误传参数导致编译失败；134222发现
扩展路径拼接 ZIP 的正斜杠会导致解压失败，统一物理分隔符后通过。失败报告均保留。

2026-11-03 长路径补「安装 / 外部 Java」：先在本机做 Windows 起进程的探针，结论是
**这条路上没有可做的修复，只有可做的解释**。本机 `LongPathsEnabled=0`（工程不允许改注册表
冒充支持），`longPathAware` 清单单独不起作用：`CreateFileW` / `GetFileAttributesW` 带
`\\?\` 前缀能打开超过 260 字符的路径；`CreateProcessW` 起超过 260 字符的 exe，裸路径、
加 `\\?\`（161 / 206）、只给命令行三种写法全部失败；`SetCurrentDirectoryW("\\?\…")`
失败（206），用相对路径起进程也不行；该卷没有 8.3 短名（`GetShortPathNameW` 原样返回长路径），
「换成短名再起」这条退路也不存在。探针源码与清单资源留在 `build/tmp-probe/`（不入库）。
因此产品侧只做了一件事：`src/launcher/ProcessPathLimit`（纯判据，`>=260` 个字符）在
**`QProcess::FailedToStart` 之后**给用户补一句「哪条路径、多少个字符、上限 260、挪到浅目录」，
覆盖游戏启动（程序 / `--gameDir` 工作目录）、服务端启动、加载器安装器、服务端安装器四处。
**不做预检拦截**：换一台开了长路径的机器，超长路径也许起得来，预检会误杀本来能启动的实例。
判据与提示的边界（259 / 260、挑最长那条、完整路径、本机分隔符）由 core-launch 的
`ProcessPathLimitTests` 钉住（见 ZPL 一节）。

2026-11-03 真屏 UI/DPI 巡检（当前系统 150%）：`build/tmp-desktop-extended-20261103/`
三主题 × 两尺寸 × 十场景共 60 张截图全部保存成功（DPR=1.5，逻辑 DPI 96，可用区 1707×1019），
secondary 文字调色板与当前主题 PlaceholderText 的失配数 0；四个主页/管理场景各一组受控 Tab
遍历（`visitedControls=6`，焦点始终可见且可用）；任务中心选中行 1、可见流程步骤 4；
工具箱/账号外观弹层截图（宽 322 / 264 px）落盘。背景图与「减少动态效果」仍按夹具强制，
不是 Windows 设置的真实切换。抽查主页长文本、管理列表、设置窄屏、下载窄屏、活动中心，
未见主控件遮挡；设置页截图顶部被切掉半行是第一组键盘遍历把内容区滚下去造成的，不是布局截断。
100% / 125% 的复杂场景仍需人工切换显示缩放后补测（§6 暂缓）。

2026-11-03 静态发布重建与软件渲染：同一份 `-O3` 静态单文件（`build/static-release`，
`CMAKE_PREFIX_PATH=D:/Qt/static-6.10.1/install`）重建后 74,103,888 字节；
`tools/prepare-release-copy.ps1` 去符号副本 43,707,356 字节（−30,396,532，−41.0%），
导入清单 49 条与原件逐条相同（没有 Qt6*.dll / libstdc++ / libgcc / libwinpthread），
见 `build/tmp-release-copy-20261103/release-copy.json`。原件比 11-01 那份大 572 KB（代码增长），
去符号比例不变。这份副本**真启动过**：`tools/perf-startup.ps1` 三跑 visible 199–204 ms、
ready 217–222 ms、无卡顿栈（`build/perf-runtime-20261103-200343/startup.json`）。
软件渲染：`QT_OPENGL=software` + Windows 平台跑基础采集
（`build/tmp-desktop-softwaregl-20261103/`），24 张全部保存、DPR=1.5、调色板失配 0，
画面与硬件渲染一致。低端 GPU / 干净机器上的软件渲染与显卡驱动依赖仍未验，不能外推。

2026-11-03 实网下载（本机当前无系统代理、无代理环境变量）：用产品自己的代码真跑了三条。
① `mw_check icon-live 20`：搜索 833 ms 回 20 条，20 张图标 1884 ms 全部回来（空手 0），
盘上落 webp 16 / png 4 共 206,963 字节；同一趟里再要一次 0 ms 同步回 20/20（一个请求不发）；
带 `If-None-Match` 核对那一发 304、正文 0 字节、767 ms（盘上整图 759 字节）。
② `mw_check cf-resolve-live`（默认 JEI 238222:8971390）：镜像
`https://mod.mcimirror.top/curseforge/v1` 728 ms 换回 1 条，给出真实
`edge.forgecdn.net` 地址与 sha1，没换到的三种计数都是 0。
③ `mw_check resource-live mod` 22/22：真网络搜 sodium → 选 26.3 → 下载
`sodium-fabric-0.9.3-beta.1+mc26.3.jar` 1,912,496 字节落进沙盒实例 `mods/`，弹「下载完成」。
限速 / 代理这两层本机只有 core-downloads 的 `DownloadSpeedLimitTests` / `DownloadProxyTests`
在本地夹具上通过；没有可用代理可试，**真代理链路与低速服务器矩阵仍未验**，§6 暂缓口径不变。
另记一笔：同一条 `icon-live` 连跑两次，第一次搜索 15001 ms / 0 条、第二次 833 ms / 20 条，
未再复现，既不记为通过也不当作回归。

`resource-live` 这一节原来是把左栏直接拨到那一类。现在点第三层会走到
`ResourceInstallController::installNow()`，**不是从实例设置点进来**时它会弹一个**原生**
「下载到哪个文件夹」框（产品本来的行为，不是缺陷）。探针点不掉原生框：第一次跑在
`ctest -j4` 同时进行时以 `std::system_error: Invalid argument` 终止（原因未定位，没有转储），
第二次挂在 `QFileDialog::getExistingDirectory()` 里出不来（gdb 栈可查）。两跑日志都保留、
都不算通过。现在这一节改成和用户一样从实例设置 → 模组页顶上那颗「下载新模组」进来
（`openDownloadCategory()` 由此登记来路，第三步不再问存哪儿），22/22 全绿。

2026-10-26 下载预算/低速比较：`20261026-135445-d6d0145f` 四个 network 集成分片全部通过
（64.95s，包含完整分段补段/重启恢复回归）。新增 RequestBudgetChecks 独立归 download-retry：
两个真实执行器在共享2个额度下完成12个文件，保留短请求不受大文件占满影响；测试额度收缩、
重复归还、等待超时与等待中取消。真实慢 HTTP 响应通过仅调整请求年龄触发生产看门狗，
验证备用源采样、切换、完整8 MiB文件摘要与最终字节，无样本拼接及额度泄漏。
纯规则同时覆盖短观察窗口、限速、不足样本和不够快不切换。线上跨网络效果仍待验收。
135223遗漏 QPointer 头、135245新测试遗漏 QCoreApplication 头导致编译失败，修正后复测通过，日志保留。

正式启动器及独立诊断的后续构建/运行检查：`20261026-135824-42f39617` 通过。

2026-10-26 共享专用运行器：默认 `-RunnerMode Auto`，局部模块选择保持独立构建；完整分区/完整运行
合并专用 UI/集成链接输出。可用 `-RunnerMode Standalone` 关闭，或 `Compact` 强制开启。
直接 CMake 配置对应 `-DWTFML_COMPACT_TEST_RUNNERS=ON/OFF`，默认 OFF。
共享模式下请用 CTest/统一测试脚本；旧专用目标名是构建别名，不应直接运行残留旧 exe。
每个 CTest 仍是单独进程，模块名通过参数传递；沙盒子进程及 range-resume-child 协议保留。
该模式第一次需要构建该层全部专用用例对象，适合完整分区运行；局部选择 Auto 不开启，避免增加冷编译工作。

`20261026-174158-67fe2e6a` 11个共享集成分片中10个通过，包含网络、预设、工作区、模组、Java和工具；
实例升级替换用例4条失败。独立模式 `20261026-174527-3e506976` 在相同4条失败，
诊断为保留原实例目录时改名被拒绝，不是共享分派差异；失败不能算全绿。
`20261026-175243-04fd597f` 8个共享UI分片全部通过（3.99s）。
运行器自检通过，覆盖模式选择、构建目标去重、超时、真实失败传递、空选择拒绝和环境恢复。

升级改名诊断：`20261026-174839-76bbbee5` 通过原生 Windows API 确认错误5，短暂重试后仍拒绝；
`20261026-175915-3d7e50b0` 保留失败夹具，避免只留下布尔断言。
退出测试进程后核对原实例目录属性正常、用户有完整控制权，同一目录在夹具内改名并移回成功。
这只能证明退出后可操作，不能据此判定运行时拒绝来自哪个进程，也不算根因已解决。

构建重链对照 `build/perf-test-links-20261026-181122` 的两种模式均零C++重编，
专用输出18→2，50.390s→31.150s；原始日志与局限见 PERFORMANCE.md。
预热超时与时间戳导致少链接的失败记录保留，没有将无效结果当作加速数据。
最终正式构建与 network-diag `20261026-181535-99ab0cbb` 通过；
局部 Auto 模式 `20261026-181610-78a03f11` 仅构建独立模组入口并通过（0.82s），未改成构建整个专用运行器。

2026-10-26 按职责拆分大文件：映射、行数及保留的历史记录见 [REFACTORING.md](REFACTORING.md)。
UI构建器的53个、实例设置的129个原有成员函数体/签名逐项核对保持，整合包354处check保留原顺序，
新导出/主题图像检查仍归core-resources；新控制器与提示探针补了相应影响映射。
主窗口 `20261026-190844-451f23e2` 2707/2707（91.65s）、
资源 `20261026-191423-324c6db0` 825/825（27.11s）、
完整UI `20261026-191541-0cd0bd59` 3119/3119（11.19s）及
正式构建/诊断 `20261026-191604-bf1baa87` 通过。
`20261026-191742-a0564831` 相关集成8/9分片通过；升级仍是原有4条Windows目录改名拒绝，报告与夹具保留。

本轮顺带修正测试提示探针：实现移到mw fixture的PromptProbe.cpp，避免每个断言文件各自编译。
只找“第一个打开的覆盖层”会漏掉另一窗口的提示，固定0/300ms两次尝试也会漏掉更迟出现的提示。
现扫描实际提示并按代次有界等待（20s），命中后收尾；新增450ms延迟、另一窗口保持页面的回归，独立2s兜底。
`20261026-190617-e39e6dbe` 两个工作区分片通过，完整主窗口也不再卡在账号提示。
`20261026-185141-d13d2ba0` 的240s超时、184531缺少文字helper、184937缺少完整类型头的失败均保留，未用盲重跑替代修复。

### 2026-10-26 实例升级 Windows 占用定位

旧替换JAR夹具的4条失败不是替换判据错误。只读诊断先确认移动前本进程只有事务锁，
文件删除访问权探针无拒绝；Windows Restart Manager 在 `20261026-235416-e00a7ff7`
明确返回持有测试JAR的 Visual Studio Code（PID 63824）。未关闭编辑器、未终止外部进程。
`195845-534c422a`、`200023-2117f64e`、`200225-63167a33`、`200454-ce7dad97`
的失败和保留夹具继续留在 build/test-runs，临时底层句柄枚举代码已移除。

成功夹具改到 QStandardPaths 用户缓存下的独立 QTemporaryDir，失败仍保留并打印路径；
没有删减4条原有替换/嵌套JAR/日志断言。Windows新增受控无删除共享的JAR句柄：
验证拒绝发布、原JAR逐字节一致、旧核心未变、完整快照存在、错误包含占用PID；
关闭句柄后继续走真实完整升级。

产品仅在保留原目录失败时动态查询系统 Restart Manager，根目录及mods目录最多128个文件、
最多32个占用进程；不会递归扫描存档、关闭进程或以未查到占用者作为继续发布的依据。
未知/过多/查询不可用时仍保留原始Windows错误。未接入原生API的其它平台行为不变。

首次修正验证 `20261026-235716-ecd81d8b` 升级分片通过（28.02秒）。
最终代码复测 `20261026-235901-e9fa733e` 通过（29.93秒）；随后合入远端
`a116fc3`（含游戏目录迁移/GTNH资源预取），升级分片再次通过：
`20261027-000049-2a2650ea`（28.83秒），包含迁移恢复与新增占用回归。
正式启动器/网络诊断构建与诊断回归 `20261027-000352-742d53f8` 通过。
本轮未重跑完整UI/全部核心，不将上述局部通过写作整仓全量验收。

## 2026-10-28：§2.1 / §2.2 收尾

- `20261028-080043-3fcca5f4`：网络/下载/预设五组 5/5，72.48 秒。
- `20261028-080259-731583c4`：主窗口 2716/2716，93.18 秒，新增资源更新暂停/原件/回滚接线。
- H: 独立现场 `WTFML-Acceptance-20261025/download-recovery-final-20261028`：文件恢复及预设均退出 0，
  分别 8.44/11.63 秒。`tools/acceptance_download_recovery.ps1` 拒绝覆盖已有目录，每程序 180 秒截止；
  使用前与本地构建一样加载 Qt/MinGW 运行库路径。
- Windows 真实主窗口、实际公网原版服务端及 Forge 1.20.1/47.2.0/Java17＋FOV 预设，
  记录和截图见 [恢复验收](DOWNLOAD_RECOVERY_ACCEPTANCE_20261027.md)，不启动游戏、不碰真实账号/存档。
- 原 Forge 服务端失败现场真实重新安装成功；缩短时钟的实际子进程另验静默重试/耗尽/总截止与再次安装。
- 正式启动器构建通过；`tools/check-test-modules.ps1` 与 `git diff --check` 通过。

单独定位更新中心：构建 `mw_check` 后运行 `mw_check.exe update-center-check`，
入口在默认根/INI 设置隔离初始化之后；调用方仍须设独立进程截止。该用例约 2 秒，
不必为一条暂停断言重复全量回归。失败与探针修正保留在上述验收文档，不删除红项后宣称通过。
本轮没有重跑不相关的全部核心、完整视觉/DPI或真实登录，不将它写成全项目最终发布验收。


## 2026-10-28：保守开放测试并行

`tools/test.ps1` 默认 `-TestJobs 2`，可用 `-TestJobs 1` 做串行回退。
CTest 仅允许已核对的 core-accounts、core-activity、core-toolbox 并行；其它注册测试
（包括独立注册的 network-diag）设置 RUN_SERIAL，保持独占。三个模块同时隔离 QSettings、
当前与默认游戏根，使用各自临时夹具，不共用辅助进程。

同机首次对照：并行 3/3，墙钟 6.86 秒（各进程合计 12.09 秒）；串行 3/3，9.50 秒。
这只证明该组合通过与一次实测，不能推断整套测试加速比例。
记录：`build/test-runs/20261028-123154-ac213ec8/`、`20261028-123449-c4843d1e/`。
运行器失败/超时/空选择自检也通过：`build/p1-runner-check-20261028.log`。
其它模块继续逐组审核后再开放，不直接提高全量并行度。


## P1 独立验收工具（2026-10-28）

先加载本机 Qt/MinGW 运行库路径，证据目录必须是新的绝对路径；不覆盖原有游戏或证据。
所有入口均有独立截止，下面的测试不需要真实账号。

```powershell
# 构建原生验收窗口（不进入默认构建目标）
cmake --build build/verification-mingw13 --target wtfml_desktop_acceptance --parallel 2
pwsh -NoProfile -File tools/acceptance_desktop.ps1 -Extended -Root build/new-native-ui-run
pwsh -NoProfile -File tools/acceptance_desktop.ps1 -LogStress -Root build/new-native-log-run

# 真实系统键盘事件观察；90 秒后自行退出，按 Tab/Shift-Tab，产出 input-report.json 和截图
build/verification-mingw13/tests/mw_check/wtfml_desktop_acceptance.exe --root D:/new-owned-evidence/keyboard --manual-input

# 真实产品入口/同目录互斥/日志并发；不改真实注册表配置
python tools/acceptance_directory_boundary.py --exe build/release-mingw13/WTFMinecraftLauncher.exe --root D:/new-owned-evidence/startup
# --in-place 用于已经部署好运行库的同一目录；省略时会创建三个独立安装位置

# 公网样本经生产下载引擎下载，另验大小与 SHA-1；最多约 180 秒
python tools/acceptance_public_downloads.py --exe build/release-mingw13/tools/network_diag/WTFMLNetworkDiag.exe --root D:/new-owned-evidence/network

# 指定文本样本审计（只扫描各目录顶层；不会递归遍历游戏）
python tools/audit_login_outputs.py --self-test --root build/new-native-log-run --report build/new-output-audit.json
```

`-Extended` 是三主题/两尺寸/十场景的受控几何与 Tab 事件测试，减少动态效果用内部标志模拟；
不替代真实系统缩放、真实内容或 Windows 设置切换。`-LogStress` 使用真实会话控制器与原生日志窗口，
两路各 25000 行，检查上限、分离、过滤、导出脱敏及心跳；输入为受控输出，不称作两个真实游戏。
`--manual-input` 不生成键盘事件，记录收到的原生事件、焦点和截图；没有 Tab 或焦点失败会返回非零。

游戏主菜单专项沿用已有隔离 Fabric 1.21.11 夹具；`--game-root` 要求该测试目录含
`p1-game-acceptance.json`，格式 `wtfml-isolated-game/1`，并提供 `--java-path`。
该入口只创建私有 DesktopTest 离线配置，正常退出记录 `game-exit.json`，300 秒截止；
主菜单必须另行观察，进程退出 0 不单独证明主菜单。不要给真实游戏目录添加测试标记。

Windows/MinGW 新增 `wtfml.token-protection-fault`：仅该测试进程的导入符号模拟 DPAPI 失败，
不替换系统 DLL、不注入其他进程，正式程序没有加密故障开关。
`-Suite core` 按 `core` 标签选择，也可用 `-Suite ci-core -Modules token-protection-fault` 单独运行。
核心 11 个入口、兼容入口及主窗口/UI 入口均隔离用户/系统配置与默认根；
仍只开放已验证的三个模块并行，其余独占。`check-test-modules.ps1` 防止入口隔离声明遗漏。

本轮结果、首次失败与范围限制见 [P1 验收](P1_ACCEPTANCE_20261028.md)。


























## 2026-11-10：智能体 Stage 1A 测试入口

新增独立核心域与两个专项 UI/集成模块；不联网、不写真实用户目录。

```powershell
# 核心 A 组：SSE/适配/会话/加密/凭据/上下文/Loop/技能/Markdown
./build/verification-mingw13/tests/verify_agent.exe

# 智能体页 + 设置页（offscreen）
./build/verification-mingw13/tests/ui_check/ui_check_agent.exe

# 真 MainWindow + 假模型客户端（不联网）
./build/verification-mingw13/tests/mw_check/mw_check_agent.exe
```

CTest 名称：`wtfml.core-agent`、`wtfml.ui-agent`、`wtfml.main-window-agent`。
每个测试用独立 `QTemporaryDir`；`AgentPaths` 通过
`QCoreApplication::setProperty("wtfml.testAgentDataDir", <临时目录>)` 隔离，
QSettings 也重定向到同一沙盒。凭据测试强制走 DPAPI 后备，不创建真实凭据管理器 target；
主路径由 G1A 真机人工验收。

本机结果：`verify_agent 145/145`、`ui_check_agent 28/28`、`mw_check_agent 22/22`；
`wtfml.ui`、`wtfml.main-window`、`wtfml.token-protection-fault`、核心基线、
`test-modules/test-runner/ci-partitions` 全部通过。细节与未跑项见
[Stage 1A 实现与验收](AGENT_STAGE1A.md)。

## 2026-11-10：智能体 Stage 1B 测试入口

沿用 `verify_agent` / `ui_check_agent` / `mw_check_agent`，不新增网络或真实用户目录依赖。
Stage 1B 用例覆盖：路径攻击矩阵与句柄级校验、外发边界表、审批矩阵、artifact 分离、
实例锁、维护备份生命周期、文件工具引擎、Loop 工具调用/审批/拒绝回灌、UI 工具卡/审批卡；
另有本地假服务器验证 400 错误体保留和工具线上名不含点号。

```powershell
./build/verification-mingw13/tests/verify_agent.exe
./build/verification-mingw13/tests/ui_check/ui_check_agent.exe
./build/verification-mingw13/tests/mw_check/mw_check_agent.exe
```

本机结果：`verify_agent 306/306`、`ui_check_agent 39/39`、`mw_check_agent 31/31`；
既有 `wtfml.ui`、`wtfml.main-window`、`wtfml.core`、`wtfml.token-protection-fault` 与
工具链检查通过。junction 夹具无法创建时输出 `SKIP`，不计入通过。真机 G1B 清单见
[Stage 1B 实现与验收](AGENT_STAGE1B.md)。

## 2026-11-10：G1A 人工审查结果

G1A 除网络错误外全部通过：导航/聊天 UI、API/URL 校验、Windows 凭据管理器主路径、
Key 遮挡与不回显、流式/停止/重试/切换取消、会话恢复、严格无工具/无 MCP、API Key 未出现在
日志/会话/界面。网络错误本轮没有条件执行，单独延后到
[网络错误验收](NETWORK_ERROR_ACCEPTANCE.md)，不写成已通过。

会话加密可见性已补：开关从右上角移到聊天头部下方单独一行，并显示当前明文/加密状态。
Stage 1B 构建可用 `build/stage1b-review/WTFMinecraftLauncher.exe` 审查；完整流程见
[G1A 验收流程](G1A_ACCEPTANCE.md)。

## 2026-11-10：G1B 首轮人工结果

首轮人工（`build/stage1b-review`）确认实例锁定、`fs.list/listNames`、审批拒绝/允许、
二进制拦截、自动读取告知、写入/拒绝/备份、切会话/删除/关窗、加密开关等路径基本正常。
但发现 P0 隐私问题，G1B **未通过**：

- 脱敏只命中精确常见字段名；`hypixel_api_key`、`DB_PASSWORD`、`REDIS_PASSWORD`、
  `API_SECRET`、`JWT_SECRET` 等变体未命中（`redactionHits=0` 或只命中 1 个）。
- 凭据文件识别过窄：测试 env 文件未触发 `EgressBlocked`。
- 1 GiB 文本 `truncated=true` + modelArtifact id；artifact 只保存本次读取窗口，但脱敏未命中时
  仍可被 `artifact.read` 取回；工具文案“完整内容见 artifact”不准确。

未测/延后：W4/W5（sha256 查看方式）、A1（artifact 分段）、P4（重启）、P5（提交阶段）、
网络错误。完整记录与补测方法见 [G1B 首轮人工测试记录](G1B_MANUAL_RESULTS_20261110.md)。
