# 导航收口与实例元数据：盘点与结论

给要动 `OverlayHost` 页面导航、嵌套事件循环或 `wtfml.meta` / `.wtfml-server` 读写的人看：这两项"渐进收口"当年的盘点快照与收口结论（2026-10-28 ～ 11-04）。
由 NESTED_EVENT_LOOPS_AUDIT 与 INSTANCE_META_INVENTORY 合并压缩而成；快照里的行号已失效，不再保留，按函数名在代码里找。

## 一、嵌套事件循环（快照 `a51159f`，2026-10-31；收口 2026-11-02）

事件循环重入分两类：

- **A. 有意等用户**：`OverlayHost::execPage()`，调用方等用户把页收掉才往下走（原 `QDialog::exec()`）。
- **B. 等后台**：UI 线程上等工作线程、网络回复、子进程退出。

### A 类：已全部迁走

盘点时 12 个调用点（任务详情、体检报告、工具箱、更新中心、服务器控制台、服务器设置、Java 管理、账号与外观、分组设置、预设编辑器、资源详情页、实例设置），11-02 全部改走 `OverlayHost::showPage()`（摆上去就返回，收页由覆盖层自己收；返回箭头接成 `reject()`，与页自己的 accept/reject 一起落到 `QDialog::finished`）。**产品代码里已无 `execPage()` 调用方**，`OverlayHost::execPage()` 这个 API 还留在 `ui/OverlayHost.hpp`。

- 收页后的清理接在 `OverlayHost::pageClosed(QWidget *)` 上（所有收页路最终汇到 `closeOverlay()`，该钩子只在这一处发），接收方按页面对象认人，用 `QDialog::result()` 读结果（确定 = `Accepted`）。资源详情页的 `ResourcePageController::open()` 多收一个 `onClosed(int)`；实例设置页的十几个 `connect` 改捕挂在对话框上的 `InstanceSettingsSession`。
- 路由栈：从实例设置进下载页/详情页的来路收成一条 `NavigationReturn`（根页＋实例＋页键＋滚动位置），返回时先切回原根页再按页键摆回去，详见 [拆分记录](../dev/REFACTORING.md)。
- 约束：`OverlayHost::openPage()` 在 `m_mode != None` 时返回 `nullptr`，一层只放一页（收在别的选项卡上的页也算占着）。页面按钮里收页必须走 `dismiss()`（提示层发 `promptDismissed`、页面层发 `backPressed`、对话框 `reject()`）；直接调 `closeOverlay()` 只拆外壳、不发信号。
- 对应用例：`UiOverlayNavigationChecks` 的 O2c、`MwWorkspaceCheck`、`runIntegrityScanTests` 的 T33.2、`runJavaManageTests` 的 T26、`MwAccountAppearanceChecks` 的 Z2 / Z2b。

### B 类：UI 线程上等后台（盘点结论，仍是现状）

| 位置 | 等什么 | 用户输入 | 超时 |
| --- | --- | --- | --- |
| `ImportController::runOffUiThread()` | 解析整合包的工作线程 | 会送回 | 无（`m_importFlow` 守卫） |
| `ImportController::repairFromReport()` | `QFutureWatcher` | 不送回 | 循环无超时，工作侧 900 s |
| `LaunchController::runOffUiThread()` | natives / Java 工作线程 | 会送回 | 无（`m_startInProgress` + `m_abortRequested`） |
| `PresetControllerEditor::leaveEditor()` | `saveEditor()` 完成回调 | 会送回 | 无（`m_editorSaving` / `m_editorPrompting`） |
| `HttpClient` | Control 名额（25 ms 轮询）；网络回复 | 会送回 | 15 s 默认 |
| `GlobalJavaSettings` | `runProcess()` 子进程；下载回复 | 子进程不送回 | 5 s；下载 30 s |
| `ServerPing::lookupSrv()` | DNS | 会送回 | ≤ 2 s |
| `VersionManager` 析构 | `m_modpackOverridesWork`（`QFuture`） | 不送回 | **无超时**（先置取消标志并 `cancel()`，外部解压进程见标志会退出） |
| `VersionManager` / `VersionManagerLoaderInstall` | 安装器进程（kill 后） | 不送回 | 5 s |
| `ZipUtil` 的 PowerShell 子进程 | `runPowershell` / `readEntry` / `extractEntryToFile` / `listEntries` | 不送回 | 60 s / 180 s / 300 s / 900 s / 1800 s 各档，强杀后再等 5 s |
| `ServerProcess` 析构 / `stopAndWait()` | 服务端子进程 | 不送回 | 3 s；优雅 `gracefulMs + 5 s`，强杀再 5 s |
| `MainWindow::enterSyncWait()`、`refreshAccountSkin()`、`GameWindowSettings::maximizedContentSize()` | 先排一次重绘 / 离屏探测排版 | 不送回 | 无 |

结论：
1. **会送回用户输入的**（上表前几行与 HttpClient、GlobalJavaSettings 下载、ServerPing）有重入可能，正确性只靠各自的忙标志，少一个就会让用户点两下跑同一段路两遍。
2. **不送回输入的**不重入，但界面在等待期间完全不动；分钟级的集中在 ZipUtil 各档、`refreshAccountSkin()` 之后的两次同步网络调用、`VersionManager` 析构里没超时的 `waitForFinished()`。
3. 这些同步等待要改非阻塞，须先把工作挪到后台，否则卡顿只是换位置；部分已在仓库外《WTFML-测试笔记》的待办里（该笔记未入库）。

## 二、`wtfml.meta` 读写者与判据（2026-10-28 盘点；2026-10-31 收口）

路径常量只有一份：`src/util/AppPaths.hpp` 的 `versionMetaFileName()` / `versionMetaPath()`。

**写**：中央入口 `VersionManager::updateVersionMeta()`，合并写（先读现有对象作底，再盖要改的键；`null` 表示删键），走 `QSaveFile` 并在 `DirectorySessionLock` 之下。调用方：`ensureInstanceId()`（`instanceId`）、`renameInstance()`（`name`，失败回滚还原）、整合包导入收尾与 `writeLoaderMeta()`（`realVersion`/`loaderName`/`loaderVersion`）、`InstanceSettingsController`（Java/窗口覆盖、`skipJavaVersionWarning`、`icon`、`settingsPages`）、`LaunchController`（`javaPath`）、`MainWindow`（`skipJavaVersionWarning`）。

**下层只有一个实现**：`src/manage/InstanceMetaFile.{hpp,cpp}` 的 `mergeWrite(path, fields, error)`，按路径而不是按"当前数据目录的哪个 id"工作，暂存/候选目录和别的游戏文件夹里的那份也能用。`updateVersionMeta()` 只管"哪一份＋目录锁"。原来三个直写者已改走它：`VersionManager::copyInstance()`（写副本暂存目录，换 `name`/新 `instanceId`）、`ModpackCompletion`（补加载器三格）、`InstanceUpgrade::applyCoreToCandidate()`（开头先 `InstanceMetaFile::read()` 验证读得懂以便早失败，末尾补三格＋`upgradeTransactionId`）。写出的是缩进 JSON，会统一用户原有排版，不能据排版判断写入者。

**读**：唯一解析口 `VersionManager::readVersionMeta()`（不在/打不开/解不动都回空对象，后两者置 `unreadable`）；`instanceId()` / `versionIcon()` / `skipJavaVersionWarning()` / `versionJvmArgs()` / `displayParts()` 都经它。整份对象 `versionMeta()` 的消费方：`MainWindow`（Java 覆盖）、`LaunchController`（窗口/Java 覆盖）、`InstanceDialogController`、`MainWindowViewData`（`type`）。只问"在不在"的：`InstanceFiles`（有 meta = 导入完成）、`InstanceTransferController`（复制完核对）、`ModpackExport`（打包排除）。

**判据**：
- 格式版本 `schemaVersion`，当前 1（`InstanceMetaFile::kSchemaVersion`，每次写补上；没有该键按 1）。比 1 新或不是整数 ⇒ 只读不写，写侧拒绝并报版本号（更新的启动器写的文件，按 1 的规矩合并会误解新字段再盖回去）。用例 `VersionMetaWriteTests` O9–O11。
- 原子写：`QSaveFile`，断电只留旧内容，无半截 JSON。
- 未知字段保得住：写侧一律"读进来再盖"；文件不存在才从空对象起。
- 读取上限 16 MiB，超过按读不懂处理。
- 损坏：拒绝写、不备份、不隔离（`VersionMetaWriteTests` O3）。**没有 `.bak` / 上一版恢复**：坏文件原地保留由用户手修，不做自动备份再猜测。

## 三、服务端记录 `.wtfml-server`（同日顺带查出，2026-11-04 复核已修）

原问题：`ServerModel::toJson` 照结构体重建，未知字段活不过一次读—改—写；右键"异常退出后自动重启"那一格 `readMeta()` 不看 `problemOut`，读不懂时拿空 `Info` 写回，会把一份读不懂的记录顶成最小合法记录（游戏版本/内存/端口全丢）。

现状（`ServerRegistryTests`）：`setRestartOnCrash()` 先看 `readMeta()` 的 `problemOut`，读不懂就一个字不写并把原因回给调用方，文件不存在才照写（ZX8.5）；`writeMeta()` 以盘上那份为底，先清模型拥有的键（`ServerModel::ownedMetaKeys()`）再盖上已知字段，未知字段原样保留（ZX8.6）；读取上限 4 MiB（ZX8.7）。

## 四、同类文件扫描（2026-11-04）

| 文件 | 未知字段 | 读取上限 | 结论 |
| --- | --- | --- | --- |
| `wtfml.meta` | 保得住（读—合并—写） | 16 MiB | 已收口 |
| `.wtfml-server` | 保得住（ZX8.6） | 4 MiB（ZX8.7） | 已收口 |
| `wtfml.folders` | 保得住（整份文档当底） | 8 MiB（Q16） | 补上限 |
| `servers.dat` | 保得住（改 NBT 文档本身） | 16 MiB | 无需改 |
| `server.properties` | 保得住（逐行原样保留） | 16 MiB | 无需改 |

启动历史、任务历史这类由我们生成、schema 自带版本的账本仍整份重建，不为未知字段做合并写。
