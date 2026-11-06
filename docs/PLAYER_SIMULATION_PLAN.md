# 玩家路径模拟：范围、预算与执行计划

> 目标：不用手点界面，把"普通玩家从第一次打开到日常使用"的路径自动跑一遍，外加压力场景。
> 所有数据落在 `D:\Desktop\the_project` 下，**不写 C: 用户目录**；每轮结束清掉大件。

## 1. 隔离与预算

- 运行前把 `TEMP/TMP` 指到 `D:\Desktop\the_project\.tmp-test`（现有测试脚本已这么做）。
- 每个场景一个根：`D:\Desktop\the_project\.tmp-test\player-<场景>-<时间戳>`；
- 单轮磁盘上限 **2 GB**（下载缓存、整合包、实例都在根内），跑完保留日志、删掉大件；
- 网络允许（官方源慢时用镜像/本地假接口）；默认用**本地假接口**（mw_check 那套），
  只有"真实源"场景才走外网，且限量（≤3 个版本、≤1 个整合包）。

## 2. 场景矩阵

| # | 玩家动作 | 现状 | 计划 |
| --- | --- | --- | --- |
| S1 | 首次打开（空配置、无 Java、无账号） | 部分（各模块分别覆盖） | 新场景：空沙盒 → 首启不崩、引导可见 |
| S2 | 配置 Java（自动/手动、内存、版本提醒） | 已覆盖（`MwJavaPreferencesChecks`、`MwMemoryPreferencesChecks`） | 复用 |
| S3 | 下载/安装版本（原版、Forge、Fabric、NeoForge） | 已覆盖假源（`MwInstalledVersionsChecks`、`SegmentInstallChecks`） | 复用 + 一轮真实源 |
| S4 | 离线登录 + 换号 | 已覆盖（`MwLoginCompletionChecks`） | 复用 |
| S5 | 启动游戏（命令组装、Java 选择、失败提示） | 已覆盖（`runLaunchGuardTests`、`runInstanceStartActionsTests`） | 复用；真实启动一轮 |
| S6 | 加模组/材质包/光影/存档 | 部分（实例文件页、模组面板） | 新场景：拷入→识别→启用→改存档→回滚 |
| S7 | 导入整合包（zip/文件夹） | 已覆盖解析（`MwModpackImportChecks`） | 复用 + 小样本真包 |
| S8 | 下载模组（Modrinth/CurseForge） | 已覆盖假源（`PresetInstallChecks`） | 复用 |
| S9 | 下载安装整合包 | 部分 | 复用导入 + 安装器夹具 |
| S10 | 修改存档（复制/删除/备份/恢复） | 部分（备份与文件页） | 新场景：写坏→恢复 |
| S11 | 5 个大实例同时启动 | `MwTaskStressChecks` 覆盖任务面 | 新场景：内存预算 + 低内存提示 |
| S12 | 10 个并行下载/安装任务 | `MwTaskStressChecks` 覆盖 30 项 | 复用 + 限速/取消 |
| S13 | 磁盘吃紧 | 有空间预算判据（升级/备份） | 新场景：小预算下拒绝而不是写爆 |

## 3. 执行方式

1. 先跑一遍现有全套（`ctest -LE visual`）作为基线；
2. 新增 `tests/mw_check/modules/PlayerJourneyChecks.cpp`：按 S1→S10 串成一条"玩家脚本"，
   每一步断言界面状态与盘上结果；
3. 压力场景单独一个模块（S11–S13），带 `QStorageInfo` 预算与内存上限；
4. 每轮记录：耗时、峰值工作集、写入字节、失败点；日志放 `.ci-logs/player-*/`。

## 4. 需要用户配合的

- 真实启动游戏需要：Java 21（本机已有 Microsoft OpenJDK 21）+ 一个离线账号；
- 真实整合包导入如果要"大包"，请指定一个本地 zip 路径（否则用仓库内小样本）；
- 运行期间不要开另一个启动器（会话锁是按数据目录的；测试用隔离目录，不冲突）。
## 执行记录

### 2026-11-06 · 并发下载压力（`tools/player_stress.ps1 -Jobs 10`）

- 10 个 `resource-live` 进程并发（Modrinth：模组×2/材质包/光影/数据包轮换），
  每个独立 TEMP，单进程上限 180 秒；
- 结果：**10/10 通过**，墙钟约 **7 秒**（10 个进程耗时合计 63 秒），
  退出码 0；沙盒目录由探针自清，**残留 0 MB**；
- 第一次跑用 `jei` 当样本时 2 条失败：它缺 Fabric API，产品正确地弹
  「还有前置没装」而探针不会点——换成无依赖的 `lithium` 后全绿。
  结论：并发路径没问题，探针选样本要避开有前置的模组。

### 五实例内存预算

- 纯算术与文案由 `MB1`/`MB2`（`verify`）钉住：五份 4 GB 合计 20 GB 对 20 GB 可用
  时提醒、对 64 GB 不提醒；
- 真起五个游戏进程的压测**没做**：需要真实 Java + 真实版本文件，代价与风险都高，
  留待人工按 `UPGRADE_ACCEPTANCE` 那类清单在真机执行。
### 2026-11-06 · 首次开启（真 exe + 隔离 acceptance profile）

- 用产品自己的 `--acceptance-profile`（隔离 settings/game，本机不碰注册表与真实 .minecraft）：
  `build/release-mingw13/WTFMinecraftLauncher.exe --acceptance-profile <root>`，`closeAfterMs=8000`；
- 结果：**exit 0、8.4 秒**，`startup-*.json` 里 `mainWindowShown=true`、`screenshot=true`；
- 产物：`settings/WTFML/WTFML.ini`（首次写入默认设置）、`game/logs/wtfml.log`、窗口截图
  `window-*.png`（主页空态、账号「Player 未登录」、底部「暂无实例或服务端」都正常）；
- 日志显示首启就跟上了在线版本列表（`bmclapi2`/`launchermeta` 都走 HTTP/2），没有卡死。
### 2026-11-06 · 第二次开启（设置持久化）与"没有 Java"

- 同一 acceptance profile 再启一次：**exit 0、8.4 秒**，`startup-71700.json` 里
  `mainWindowShown=true`；`WTFML.ini` 被正常更新（窗口几何等），说明设置是读盘再写的；
- 把 `PATH` 里所有含 `java` 的目录去掉、清掉 `JAVA_HOME`，用全新 profile 再启：
  **exit 0、9.4 秒**，主窗口正常显示。产品在"只是打开启动器"这一步不依赖 Java
  （真正挑 Java 是在点启动时），所以没有弹奇怪的错误，也没有卡死。
### 2026-11-06 · 真实安装原版 1.8.9（产品 VersionManager）

- 新探针 `mw_check.exe install-live <版本> <root> [mirror]`：走产品自己的
  `fetchVersionDetails` → `downloadVersion`，沙盒根在 `.tmp-test/player-install-1.8.9b`；
- 第一次：770 个文件下到 769，唯一失败是
  `libraries/net/java/jinput/jinput/2.0.5/jinput-2.0.5.jar`（瞬时网络失败；
  官方与 BMCLAPI 两条 URL 复查都是 200）；
- 第二次（同一目录重跑）：**只补下那 1 个文件就成功**，日志原话
  「1 个文件已全部就绪（另有 769 个已存在，已跳过）」；`1.8.9.json` 与 `1.8.9.jar` 都在；
- 体积 **138.9 MB / 760 个文件**，全部在 D 盘沙盒里。
### 2026-11-06 · 真实启动游戏（离线账号 Steve + 1.8.9）

- 新探针 `mw_check.exe launch-live <版本> <root> [秒数]`：产品自己构造 `MainWindow` →
  选版本 → `onStartButtonClicked`，Java 钉到 `jre-legacy`（Java 8），离线账号用
  `AccountStore::addOffline("Steve")`；
- 结果：**PASS**，游戏进程 pid=15396 起来了，观察 20 秒后终止；
- 游戏自己的输出（stderr 里能看到）：`OpenAL initialized`、`Sound engine started`，
  说明不是"进程起来就死"，是真的进了主循环；
- 收尾时那条 `QProcess: Destroyed while process ... still running` 是"外部 Terminate"
  造成的测试产物（产品正常退出走 `stopGame()`），不是玩家路径。
### 2026-11-06 · 往真实实例里加模组/材质包/存档（中文名）

- 新探针 `mw_check.exe instance-files-live <root> <版本> <源 jar>`：在 `1.8.9` 沙盒实例里
  放入 `mods/测试模组.jar`（真实 GTNH 的 1.7.10 jar，只读拷贝）、`resourcepacks/测试材质包.zip`、
  `saves/测试存档/level.dat` + `region/r.0.0.mca`；
- 用产品自己的 `Presets::scanInstalledMods()` 扫：**1 个模组**，`problem` 为空；
- 存档/材质包按玩家实际路径落地（中文目录、中文文件名都正常）；
- 改存档：把 `level.dat` 写坏再写回，读回内容与原文一致 —— **PASS**；
- 结论：中文路径 + 中文文件名 + 模组扫描 + 存档读写在真实实例上都没问题。
### 2026-11-06 · 并发压力第二轮 / 完整套件

- `player_stress.ps1 -Jobs 10` 再跑两轮：两轮都是 **10/10 通过**，
  十个进程耗时合计 79.4s / 51.4s（墙钟各约 8–10 秒），沙盒残留 **0 MB**；
- 完整套件（`ctest -LE visual`，38 项）：**38/38 通过，383.57 秒**。
### 2026-11-06 · 真实导入一个最小 Fabric 整合包（绕开 Forge maven）

- 本地拼了一个 902 字节的 `.mrpack`：`modrinth.index.json` 指向 Modrinth 上真实的
  Sodium（1.21.1 Fabric，1.5 MB，带 sha1/大小）+ 一个 `overrides/config/tiny-test.txt`；
- `mw_check.exe modpack-import-live ... TinyFabric` 真导入，**81 秒、exit 0**；
- 日志过程：装 Fabric 0.16.14 → 下原版 1.21.1（**3982 个文件，76 秒，0 失败**）→
  装 Fabric maven 库 → 下清单里 1 个模组 → 解压 1 个覆盖文件 → 写出
  `TinyFabric.json` / `wtfml.meta` / `wtfml.import-ready.json` / `wtfml.import-report.json`；
- 结论：**Fabric 路线的整合包导入是通的**；Forge 那条卡在 `maven.minecraftforge.net`
  （环境问题）。探针按惯例自清沙盒，实例目录只在日志里留证。
### 2026-11-06 · 五个真实实例并行启动（不是算术，是真进程）

- 把装好的 1.8.9 复制成 5 份独立实例（`Stress8-1..5`，各改 id/json/jar 名），
  新探针 `mw_check.exe launch-many-live <root> <id1,..,id5> [秒数]` 逐个点启动；
- 结果：**5/5 全部起来**（pid 53224 / 74364 / 3356 / 73596 / 22736），
  观察 25 秒后各自读工作集：
  **845 / 872 / 907 / 899 / 891 MB，合计 4417 MB**；
- 终止后无残留 java 进程；5 份副本测完已删，沙盒恢复到只剩 `1.8.9`。
- 结论：五实例并行在产品里没有互相卡死/串号，内存占用与各自 1024 MB 堆相符
  （工作集略低于 Xmx 是正常的，堆还没长满）。
### 2026-11-06 · 收尾：探针搬家 + 全量

- `mw_check/main.cpp` 触到 4000 行上限（`check-test-modules` 拦下），把五条真网络/真进程探针
  搬到 `tests/mw_check/modules/LiveProbes.cpp`（`runLiveProbeMode`），`main.cpp` 回到 3699 行；
- 搬完重跑：`wtfml.test-modules` 通过、`instance-files-live` 复跑通过；
- 完整套件：**38/38 通过，382.62 秒**。

### 覆盖结论

| 场景 | 证据 |
| --- | --- |
| 首次开启 / 二次开启 / 无 Java | acceptance profile 真跑，exit 0，截图与 startup json |
| 离线账号 | `AccountStore::addOffline("Steve")` + 真启动 |
| 下载版本 | 1.8.9 真装 770 文件 139 MB，瞬时失败可续传 |
| 启动游戏 | 真进程进主循环（OpenAL / Sound engine） |
| 加模组/材质包/存档（中文名）、改坏恢复 | `instance-files-live` 全 PASS |
| 下载模组/资源 | Modrinth 四类真实安装各 ~3.5 s |
| 整合包导入 | Fabric 最小包真导入 81 s；Forge 被 maven 网络所阻 |
| 五实例并行 | 5/5 起，工作集合计 4417 MB |
| 10 并发下载 | 三轮 10/10，墙钟 8–10 s，残留 0 MB |
| 低内存提醒 | `MB1`/`MB2` 判据 + 启动前确认框 |

未覆盖且原因在外部：Forge 整合包（`maven.minecraftforge.net` 不可达）、
Java 自动安装（Adoptium 被断、Qt 走代理下载 aka.ms 超时）。
