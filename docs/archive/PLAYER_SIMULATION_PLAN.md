# 玩家路径模拟：范围、预算与 2026-11-06 执行结果

给要复跑"普通玩家从第一次打开到日常使用"这条链、或查它跑到什么程度的人看。所有数据落在 `D:\Desktop\the_project` 下，不写 C: 用户目录。

## 1. 隔离与预算

- 运行前把 `TEMP`/`TMP` 指到 `D:\Desktop\the_project\.tmp-test`（现有测试脚本已这么做）；每个场景一个根 `.tmp-test\player-<场景>-<时间戳>`。
- 单轮磁盘上限 2 GB（下载缓存、整合包、实例都在根内），跑完保留日志、删大件。
- 默认走本地假接口（mw_check 那套）；"真实源"场景才走外网，限量（≤3 个版本、≤1 个整合包）。
- 真实启动游戏需要 Java 21（本机有 Microsoft OpenJDK 21）和一个离线账号；大整合包请指定本地 zip 路径，否则用仓库内小样本；运行期间不要开另一个启动器（会话锁按数据目录，测试用隔离目录，不冲突）。

## 2. 场景与覆盖

| # | 玩家动作 | 覆盖 |
| --- | --- | --- |
| S1 | 首次打开（空配置、无 Java、无账号） | 真 exe + acceptance profile：exit 0、8.4 秒，`mainWindowShown=true`，主页空态/账号「Player 未登录」/底部「暂无实例或服务端」正常；日志显示首启跟上在线版本列表（`bmclapi2`/`launchermeta` HTTP/2） |
| S1b | 第二次开启、PATH 去掉所有 java 且清 `JAVA_HOME` | exit 0、8.4 / 9.4 秒；`WTFML.ini` 读盘再写；只是打开启动器不依赖 Java（挑 Java 在点启动时） |
| S2 | 配置 Java | `MwJavaPreferencesChecks`、`MwMemoryPreferencesChecks` |
| S3 | 下载/安装版本 | 假源：`MwInstalledVersionsChecks`、`SegmentInstallChecks`；真实：1.8.9 经 `mw_check.exe install-live`，770 个文件 138.9 MB / 760 个文件，首次下到 769（`jinput-2.0.5.jar` 瞬时网络失败，官方与 BMCLAPI 复查都是 200），同目录重跑只补 1 个即成功 |
| S4 | 离线登录 + 换号 | `MwLoginCompletionChecks` |
| S5 | 启动游戏 | `runLaunchGuardTests`、`runInstanceStartActionsTests`；真实：`launch-live`（离线 Steve + 1.8.9，Java 钉 `jre-legacy`）PASS，pid 起来后 stderr 见 `OpenAL initialized`、`Sound engine started`，观察 20 秒后终止 |
| S6 | 加模组/材质包/存档（中文名） | `instance-files-live`：`mods/测试模组.jar`（真实 GTNH 1.7.10 jar 只读拷贝）、`resourcepacks/测试材质包.zip`、`saves/测试存档/`；`Presets::scanInstalledMods()` 扫到 1 个模组、`problem` 为空；`level.dat` 写坏再写回读回一致，PASS |
| S7 / S9 | 导入/安装整合包 | 解析：`MwModpackImportChecks`；真实：本地拼的 902 字节 `.mrpack`（指向 Modrinth 真实 Sodium 1.21.1 Fabric 1.5 MB＋一个 overrides 文件），`modpack-import-live` 81 秒 exit 0（装 Fabric 0.16.14 → 下原版 1.21.1 共 3982 个文件 76 秒 0 失败 → 装 Fabric maven 库 → 下 1 个模组 → 解压覆盖，写出 `TinyFabric.json`/`wtfml.meta`/`wtfml.import-ready.json`/`wtfml.import-report.json`）；Fabric 路线通，Forge 路线被 `maven.minecraftforge.net` 不可达所阻 |
| S8 | 下载模组/资源 | 假源 `PresetInstallChecks`；真实 `resource-live` Modrinth 四类各约 3.5 秒 |
| S10 | 修改存档（写坏→恢复） | 同 S6 与备份/文件页既有覆盖 |
| S11 | 5 个大实例同时启动 | 算术与文案 `MB1`/`MB2`（`verify`：五份 4 GB 合计 20 GB 对 20 GB 可用时提醒，对 64 GB 不提醒）；真进程：1.8.9 复制 5 份（`Stress8-1..5`），`launch-many-live` 5/5 起来，25 秒后工作集 845 / 872 / 907 / 899 / 891 MB，合计 4417 MB，无串号、无残留 java 进程；副本已删 |
| S12 | 10 个并行下载/安装 | `tools/player_stress.ps1 -Jobs 10`（10 个 `resource-live` 进程并发，独立 TEMP，单进程上限 180 秒）：三轮均 10/10，墙钟 7–10 秒，残留 0 MB；`MwTaskStressChecks` 覆盖 30 项任务面 |
| S13 | 磁盘吃紧 | 升级/备份的空间预算判据；满盘实测见 [ACCEPTANCE_LOG](ACCEPTANCE_LOG.md) 的 VHD 满盘 |

完整套件（`ctest -LE visual`，38 项）：38/38 通过，383.57 秒；探针搬家后 382.62 秒。

## 3. 执行中的发现

- 并发下载探针第一次用 `jei` 当样本 2 条失败：它缺 Fabric API，产品正确弹"还有前置没装"而探针不会点；换无依赖的 `lithium` 后全绿。**探针样本要避开有前置的模组。**
- `launch-live` 收尾的 `QProcess: Destroyed while process ... still running` 是外部 Terminate 造成的测试产物，产品正常退出走 `stopGame()`。
- `tests/mw_check/main.cpp` 触到 4000 行上限（`check-test-modules` 拦下），五条真网络/真进程探针（`install-live`、`launch-live`、`instance-files-live`、`modpack-import-live`、`launch-many-live`）搬到 `tests/mw_check/modules/LiveProbes.cpp`（`runLiveProbeMode`），`main.cpp` 回到 3699 行。

## 4. 未覆盖

| 项 | 原因 |
| --- | --- |
| 原计划的 `PlayerJourneyChecks.cpp`（把 S1→S10 串成一条玩家脚本） | 未建；改用上面的各条探针与既有模块分别覆盖 |
| Forge 整合包导入 | `maven.minecraftforge.net` 不可达 |
| Java 自动安装 | Adoptium 被断，Qt 走代理下载 aka.ms 超时 |
| 单进程之外的低内存/磁盘吃紧组合 | 仅算术判据与 VHD 满盘，没有真实低内存环境 |
