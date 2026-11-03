# 大文件职责拆分记录

## 2026-10-26 本轮边界

本轮以行为保持为前提，不改变布局、任务规则或安装策略；不是运行性能结论。

| 原入口 | 拆分前行数 | 拆分后行数 | 本轮职责去向 |
| --- | ---: | ---: | --- |
| src/MainWindow.cpp | 11417 | 10576 | ResourceSearchController、InstanceUpdateCheckController |
| ui/MainWindowUI.cpp | 6256 | 1602 | MainWindowDownloadPage、MainWindowSettingsPage、MainWindowPanels、共享 PageLayout |
| ui/InstanceSettingsDialog.cpp | 4802 | 3812 | InstanceSettingsServers、InstanceSettingsLifecycle |
| tests/core_modules/ModpackImportTests.cpp | 3272 | 926 | 导出格式、平台反查、打包/身份识别流水线、主题图像检查 |

资源搜索控制器持有实际发送的查询词；更新检测控制器持有运行中的请求集合、页面弱引用和工作线程。
主窗口保留原有接线入口及跨页面结果缓存，在销毁 UI 前调用控制器等待线程收尾。
翻译继续使用 MainWindow 的上下文；没有引入第二份查询/运行状态。

UI 的共享布局、表单/提示构造和尺寸定义只有一份实现；页面分文件后仍按原顺序创建。
ui/MainWindowPages.cmake 是产品与两个 UI 验证层共用的源码清单，防止新增页面只加入一侧。
整合包检查仍属于 core-resources，原有354处 check 及执行顺序保留；不减少用例。
测试提示探针实现从 Support.hpp 移入 PromptProbe.cpp，其他断言文件只见声明。

本轮一并修复测试探针：多窗口时不能用第一个打开的页面代替真正的提示；
不能只在0/300毫秒各找一次。现按请求代次等待，20秒上限，命中后立即收尾，过期则退出。
新增另一窗口保留页面、提示晚于300毫秒出现的夹具，并有独立2秒兜底。

## 本轮验证

- `20261026-184714-611067b1`：8个专用UI分片通过。
- `20261026-190617-e39e6dbe`：工作区/活动中心2个分片通过，包含延迟提示与多窗口夹具。
- `20261026-190844-451f23e2`：完整主窗口2707/2707通过，91.65秒；包括搜索、更新检测、控制器与线程退出。
- `20261026-191423-324c6db0`：core-resources 825/825通过，27.11秒。
- `20261026-191541-0cd0bd59`：完整UI 3119/3119通过，11.19秒。
- `20261026-191604-bf1baa87`：正式启动器/网络诊断构建及 network-diag通过。
- `20261026-191742-a0564831`：9个相关专用集成分片8个通过；升级仍是已记录的4条保留目录失败（Windows错误5），未改为通过。

额外核对UI构建器53个、实例设置129个原有成员函数的函数体及签名完全保持；
测试模块大小/归属检查与运行器影响映射自检通过。构建每阶段300秒，测试有独立90～240秒上限。
初次缺少共享文字构造函数/完整类型头导致的编译失败，以及修探针前的主窗口240秒超时均保留日志。
不将这次源文件拆分的行数变化当成编译或运行加速数据。

## 2026-10-28 ⑧ `controllers/UpdateCenterController`

「更新中心」那一页的**全部状态**原来挂在 `MainWindow` 上：那一本「忽略 / 固定」的账
（`UpdatePolicy::Store`）、页面对象、以及"这一页现在说的是哪个实例"。三样必须同进同退
（开页记 id、动作按 id 认人、关页清掉；账一动就得把名单重算一遍再把开着的页面重摆），
而它们和主窗口上其余几百个成员摆在一起时，"漏掉其中一条"的症状很难认 —— 表现是用户按了
「忽略」，屏幕上那一行照旧写着「可更新到 X」，而名单里其实已经剔掉了。

搬走的：`openUpdateCenter()` / `updateCenterReportForPage()`（改名 `reportFor()`）/
`rebuildInstanceOutdatedPaths()`（改名 `rebuildOutdatedPaths()`）/ `saveUpdatePolicy()`
（改名 `savePolicy()`）/ 四个 `onUpdateCenter*`，连同那三个成员。三处重复的"开着的这一页
重摆一次"收成一个 `refreshPageIfOpen()`。

留在主窗口的：

- 四条**薄壳**（`openUpdateCenter` / `updateCenterReportForPage` / `rebuildInstanceOutdatedPaths`
  / `saveUpdatePolicy`）—— `InstanceUpdateCheckController::finishUpdateCheck()` 和实例设置页
  按名字叫它们，壳只是"主窗口仍然认这个名字"。
- `m_updateBatch` 与 `onUpdateBatchFileFinished()` / `finishUpdateBatch()` —— 那一批挂在
  下载器回来的路上，和这一页开没开无关。
- `m_instanceUpdateResults` / `m_instanceUpdateTargets` / `m_instanceOutdatedPaths` ——
  检测那一层（`InstanceUpdateCheckController`）与文件管理页也读写它们，生命周期比这一页长。

`InstanceUpdateCheckController` 里唯一那句直接读账的写法跟着改成
`m_owner.m_updateCenter->policy()`（`policy()` 是这本账对外的**唯一入口**）。

缝合线仍然是"一套状态 + 动它的那几条"，不是行数：`src/MainWindow.cpp` 10,734 → 10,429 行。

验证：`ctest -j1` **39/39 通过**（388.04 s，含 `wtfml.main-window` 84.36 s、
`wtfml.ui` 9.82 s）；直跑 `mw_check.exe` **2716/2716**（与上一轮基线同数）；
`ui_check.exe` offscreen **3121/3121**。真屏直跑 `ui_check.exe` 有 1 条字体度量断言红
（文件夹齿轮菜单"字形落点往里 6 px"，实测 5,5），offscreen 与 ctest 都绿 —— 真屏字体回退
造成的，与本次搬迁无关（没碰 `ui/`）。

## 2026-10-28 ⑨ `ui/InstanceSettingsDialog`：按"一页一摊"分 TU

主 .cpp 3,813 行里最长的是三个"搭页"函数（概览 597 / 启动 423 / 文件八页 460），它们的
共同点是**各自只在一次打开对话框时跑一遍**，彼此只共享几样搭控件的小工具。于是按
"一页一摊"搬成三个翻译单元 —— **类没动**（同一个类，定义分散在四份 .cpp），所以断言一条
都不用改：

| 文件 | 行数 | 这一摊是什么 |
| --- | ---: | --- |
| `ui/InstanceSettingsDialog.cpp` | 2,112 | 外壳与接线：构造、左栏导航、堆叠页、脏状态、拖放、拒绝关页 |
| `ui/InstanceSettingsOverviewPage.cpp` | 684 | 「概览」那四行 + 它底下的维护入口 |
| `ui/InstanceSettingsLaunchPage.cpp` | 510 | 「启动」那一栏（Java / 内存 / 窗口 / 高级选项） |
| `ui/InstanceSettingsFilePages.cpp` | 547 | 「文件管理」那八页（服务器列表 + 七份 `InstanceFilesPage`） |
| `ui/InstanceSettingsDialogInternals.hpp` | 260 | 四个 .cpp 共用的搭控件小工具 |

搬进那个内部头的，原来是主 .cpp 顶上**匿名命名空间**里的一整套：每行三颗的按钮规格
（`prepareActionButton()` / `newActionGrid()` / `layoutActionCells()` / `layoutActionButtons()`）、
`addTitledRow()`、`MenuPopup`（更改图标 / 分组那两个浮层的壳）、`menuEntryStyle()`、
`kIconPreviewSize` 与那三个浮层留白。匿名命名空间是**内部链接**，拆完之后另外三个 .cpp
根本看不见它们 —— 各抄一份的话，"恒定宽度"那几条规矩（少一颗的那一行不摊开、宽不钉像素）
只要改漏一处就静默不一致，而屏幕上表现为"这几颗按钮宽窄对不上"。所以函数一律 `inline`。

只在一个 TU 里用的东西**没有**往上搬：`kMemorySpinWidth` 等四个宽度跟着「启动」那一份、
`kIconCandidateSize` / `kIconCandidatesPerRow` 跟着「概览」那一份、`fileKeyCanToggle()`
跟着文件那几页、`titleTextFor()` / `addNavColumn()` / `pageKeyAtStackIndex()` 留在外壳。

三个新 .cpp 进 `ui/MainWindowPages.cmake`（产品与两个 UI 验证层共用的那一张表）——
漏掉这一处不会编译失败，是链接期一串 `InstanceSettingsDialog::…` undefined reference。

验证：全量 `ctest -j1` **39/39**（381.48 s）；直跑 `mw_check.exe` **2716/2716**；
`ui_check.exe` offscreen **3121/3121**（实例设置那一块的大部分断言都在 `ui_check` 里）。

## 2026-10-28 ⑩ `controllers/ServerSettingsPageController`

「服务端设置」那一页的六格状态原来全挂在 `MainWindow` 上：页对象、"这一页现在说的是哪个
服务端"、能不能改（服务端在跑 = 不能）、体检里"那个端口现在有人占着吗"的结论，外加两格
**只写不读**的原文账。它们必须同进同退 —— 服务端一启动，`setEditable()`、体检单、保存带
三处要**同一次**跟着变，漏一条的症状是"服务端明明停了，那一页还是灰的"。

搬走的八条：`openServerSettingsPage()` / `refreshServerSettingsPage()` /
`updateServerSettingsSaveState()` / `mergeServerSettingsEdits()` / `saveServerSettings()` /
`revertServerSettings()` / `serverCheckFacts()` / `runServerCheck()`，连同那六格成员
（改名成 `open()` / `refresh()` / `updateSaveState()` / `mergeEdits()` / `save()` /
`revert()` / `checkFacts()` / `runCheck()`）。页面对象仍然懒建、仍然挂在主窗口底下
（`new ServerSettingsPage(&m_owner)`）—— 生命周期归主窗口，控制器只拿一个不拥有的指针。

留在主窗口的只有**两条薄壳**，因为还有三处按名字叫它们：`openServerSettingsPage()`（服务端
列表的右键菜单 + mw_check 的 T42，**经元对象系统驱动，所以继续留在 `private slots:`**）、
`refreshServerSettingsPage()`（`BackupController` 的宿主回调 + `ServerManagementActions`
里那五处"改完盘上那份、把页面重摆一遍"）。其余六条搬走之后**一个外人都没有**，所以没留壳。

两处顺手清掉的真东西：

* `m_serverSettingsRaw` / `m_serverSettingsOriginal` 是**只写不读**的 —— 头文件上那句"这一层
  手里始终握着唯一会被写回盘上的原文"早就不成立了（真正写盘的是页面那份 `rawText()`，
  `save()` 读的就是它）。顺着搬迁一起删。
* `backupSizeText()` 那份"12.4 MB"的格式在**三处**各有一份（`MainWindow` 的匿名
  namespace、`ServerManagementActions` 的匿名 namespace、以及搬出来之后还要第四份），
  现在收进 `BackupFiles::sizeText()`；「安全快照」那个 `恢复前-` 记号同理
  （`BackupFiles::nameIsSafety()`，和 `sortBackupsNewestFirst()` 用同一个常量）。
* 那一节顶上还留着一整段**没有函数跟着的**文档注释（"盘上那一份 `server.properties` 的
  原文…"紧挨着 `}  // namespace`）和另一段"一个服务端世界的备份在盘上认哪一串前缀"——
  都是前几轮搬走函数时留下的孤儿，一起删。

`ServerManagementActions` 里 28 处 `owner->m_serverSettingsPage / …Id / …Editable` 改走
控制器的访问器（`settingsPage()` / `serverId()` / `editable()`）。**访问器不叫 `page()`**：
调用方每一段开头都是 `ServerSettingsPage *page = owner->m_serverSettings->…`，而 C++ 里
声明符的名字在初始化式之前就进了作用域 —— 写成 `page()` 会被解析成"对那个还没初始化的
局部变量取成员"，报的是 `has no member named 'page'`，读报错信息反而不像那么回事。

新 .cpp 要进两张表（根 `CMakeLists.txt` 与 `tests/mw_check/CMakeLists.txt`）；
`tests/CMakeLists.txt` 的 `wtfml_core_runtime` **不用**加 —— 它不编 `MainWindow`。

缝合线是"一套状态 + 动它的那几条"：`src/MainWindow.cpp` 10,429 → 10,065 行。

验证：全量 `ctest -j1` **39/39**（398.83 s）；直跑 `mw_check.exe` **2716/2716**（与上一轮
基线同数）；`ui_check.exe` offscreen **3121/3121**。

## 2026-10-28 ⑪ `controllers/InstallPageController`

安装页上有四格状态是**成套的**：这一页正对着哪个原版号、选中了哪一条加载器、每条加载器
钉住的版本号、以及"用户自己改过名称框没有"。它们一起决定三件看得见的事 —— 那棵树长什么样
（哪条选中、其余几条"与 X 不兼容"并变灰、选中那条第二行小字写它钉的哪一版）、名称框里默认
写什么、点「安装」时装什么。原来散在 `MainWindow` 上，于是"进安装页要清哪几样"得读
`openInstallPage()` 全文才知道，漏一样就是"上次顺手选的 Forge 跟着下一个版本进来了"那种
说不清的症状。

搬走的十四条：`openInstallPage()` / `backToVersionTree()` / `defaultInstanceName()` /
`selectedLoaderIndex()` / `loaderItem()` / `pinnedLoaderVersion()` / `updateLoaderRows()` /
`selectLoader()` / `requestAllLoaderVersions()` / `fillLoaderVersionItems()` /
`onLoaderVersionsFetched()` / `updateInstallName()` / `updateInstallNameHint()` /
`onInstallNameEdited()`，连同四格成员（改名成 `openPage()` / `backToVersionTree()` /
`defaultInstanceName()` / `selectedLoader()` / `loaderItem()` / `pinnedLoaderVersion()` /
`updateLoaderRows()` / `selectLoader()` / `requestAllLoaderVersions()` /
`fillLoaderVersionItems()` / `onLoaderVersionsFetched()` / `updateInstallName()` /
`updateInstallNameHint()` / `noteNameEdited()`，成员是 `m_gameVersion` / `m_selectedLoader` /
`m_chosenVersion` / `m_nameTouched`）。

留在主窗口的只有**四个薄壳**，因为 moc 或验证工程按名字叫它们：`openInstallPage`
（`ConnectionsController` 的两条入口 + mw_check 的 `QMetaObject::invokeMethod`）、
`backToVersionTree`（`&MainWindow::backToVersionTree` 直接当槽连）、`onInstallNameEdited`、
`onLoaderVersionsFetched`（同样是 `invokeMethod` 驱动的）。其余十条搬走之后一个外人都没有
（`ConnectionsController` 里那三处自己改成 `m_owner->m_installPage->…`），所以没留壳。

`m_chosenVersion` 有**一个写侧**在 `ConnectionsController`（点一条版本号那一下"先钉再选"），
所以控制器补了一个 `setPinnedLoaderVersion()` —— 它只记账、不改选中，两条的顺序和原来一样。

`InstallPageController` 也是主窗口的 `friend`（它要读 `m_ui` / `m_versionManager` /
`m_downloadInFlight`，还要在进页时叫一声 `updateInstallPresetSummary()`）。**实例预设那一行
没有跟着搬**：`m_installPresetChoices` 与那几个 `m_installPendingPresets` /
`m_installPendingPresetInstance` / `m_installPresetFingerprints` / `…ModsReady/Ok/Report`
是一套账，被 `DownloadInstallContinuation` 与 `InstanceUpdateFlow` 读，动它要连着两处一起
改，单独一趟。

缝合线仍然是"一套状态 + 动它的那几条"：`src/MainWindow.cpp` 10,064 → 9,589 行，
`src/controllers/InstallPageController.cpp` 504 行。

验证：全量 `ctest -j1` **39/39**；直跑 `mw_check.exe` **2716/2716**（与上一轮基线同数）；
`ui_check.exe` offscreen **3121/3121**。

## 2026-10-28 ⑫ 「实例预设」那一行搬进 `InstallPageController`

安装页那棵树上「预设」那一行是另一格**成套的**状态：四类里各选了哪一份（`m_presetChoices`）、
点它开的那张紧凑弹窗、以及那一行第二行小字上的摘要。三样同进同退 —— 弹窗点「确定选择」
要整张表重建、重建完摘要要立刻跟着变；取消 / 「前往预设管理」则一个字都不能写。

搬走的四条：`installPresetRowItem()`（→ `presetRowItem()`）/ `updateInstallPresetSummary()`
（→ `updatePresetSummary()`）/ `currentInstallPresetSelections()`（→
`currentPresetSelections()`）/ `openInstallPresetPicker()`（→ `openPresetPicker()`），连同
`m_installPresetChoices`（→ `m_presetChoices`）。主窗口只留 `openInstallPresetPicker()` 一条
薄壳（`ConnectionsController` 里点那一行走的是它）。

**只搬了这一半**：`m_installPendingPresets` / `…PresetInstance` / `…Fingerprints` /
`…ModsReady/Ok/Report` 仍住在主窗口 —— 那是"这一次安装**已经排上**的预设"，跟着下载收尾
走（`DownloadInstallContinuation` / `InstanceUpdateFlow` 读），和"用户在页面上挑了哪几份"
是两件事。`onInstallButtonClicked()` 与 `pickInstallerJava()` 同理，留给下一趟。

顺手的一处：弹窗的父控件原来写的是 `this`（主窗口）；搬过来之后 `this` 是个 `QObject`、
不是 `QWidget`，所以那一格显式写成 `&m_owner`。

`src/MainWindow.cpp` 9,589 → 9,314 行；`src/controllers/InstallPageController.cpp` 504 → 796 行。

## 2026-10-28 ⑬ `controllers/ServerRunController`：服务端起 / 停 / 退出之后怎么办

服务端那一摊里有一组**四格成套**的状态：谁的世界正在备份 / 还原（`m_serverFileBusy`）、
这一轮连着崩了几次（`m_serverRestartAttempts`）、这一次退出是不是我们让它停的
（`m_serverStopRequested`）、以及它几点起来的（`m_serverStartedAt`）。四格只决定两件
看得见的事 —— 那一行上写着什么、以及"崩了之后要不要自己再拉起来"。它们和主窗口里其余
几百个成员摆在一起时，漏一格的症状很难认：最典型的是 **自动重启次数不清零**，一台服务端
崩到第三次之后就再也不自己起来了，而日志只说"次数用完了"。

搬走的四条：`startServer()`（→ `start()`，带 `automatic` 那条自动重启的路）/ `stopServer()`
（→ `stop()`）/ `forceKillServer()`（→ `forceKill()`）/ `onServerExited()`
（→ `onExited()`），连同上面那四格状态。**没有留薄壳** —— 这四个名字只被 `MainWindow.cpp`
自己和自己那几处接线调用，没有 moc / `QMetaObject::invokeMethod` / 校验代码按名字点它们，
所以声明直接删掉、调用点改指控制器。

**一条判据都没跟着搬**：命令怎么拼、宽限多久才许强杀在 `server/ServerRun.hpp`（verify ZZ），
该不该重启、歇几秒在 `ServerRestart::decide()`（verify ZAF），进程本身在
`ServerWorkspaceController` 的 `ServerProcess` 里（`mw_check` 的 T41 拿本工程自己的 exe
当"服务端"跑整趟）。这一层只把"按了哪颗按钮"翻成一个动作、把退出回调翻成"要不要重启"。

`src/MainWindow.cpp` 9,314 → 9,115 行；`src/controllers/ServerRunController.cpp` 237 行。

## 2026-10-28 ⑭ `controllers/DownloadCompletionController`：下载收尾与"这一趟排上了哪几份预设"

公共终态信号 `VersionManager::versionDownloadFinished(id, ok, msg)` 是**一条信号、六种来路**：
普通版本下载、装一个带名字的实例、导入整合包、「补全文件」、GTNH 首启资源预取，以及"装完
之后再把预设落上去"那一趟。分辨它们靠的是主窗口上"这一次是替谁做的"那几个标记，收尾那
一段三百多行全在主窗口里。

搬走的是**成套的那一份状态**加它唯一的消费者：`m_installPendingPresets`（点「安装」那一刻
定死的 (预设 id, 是否已确认「覆盖」)）/ `m_installPendingPresetInstance` /
`m_installPresetFingerprints` / `m_installPresetModsReady` / `…ModsOk` / `…ModsReport`，
连同 `onVersionDownloadFinished()`（→ `onFinished()`）。

这一套**不是**"用户在页面上挑了哪几份"（那在 `InstallPageController::m_presetChoices`）：
写它的是三处 —— 安装按钮、`DownloadInstallContinuation` 的断点续装、`InstanceUpdateFlow`
的升级那一趟；读它的是收尾这一发。所以它跟着**下载**走，不跟着页面走。

`src/MainWindow.cpp` 9,115 → 8,781 行；`src/controllers/DownloadCompletionController.cpp` 376 行。
两个 CMake 表都登记了新 .cpp（`tests/CMakeLists.txt` 不用 —— 它不编 `MainWindow`）。

逐行核对过一遍：把 `HEAD` 的 `onVersionDownloadFinished()` 与新的 `onFinished()` 各自去掉
注释、去掉 `m_owner.` 前缀之后做并排比对，**只剩改名**（`m_installPendingPresets` →
`m_pendingPresets`…）与 `tr(` → `MainWindow::tr(`，没有少搬/多搬的分支。

验收：`ctest` **39/39**、真屏 `mw_check` **2716/2716**、离屏 `ui_check` **3121/3121**。

## 2026-10-28 ⑮ `InstallPageController::installClicked()`：点「安装」那一刻的判断

安装页那一层早就有（`InstallPageController`：装哪个原版、选哪条加载器、钉了哪一版、名字
被改过没有），但它**只记选择、不发请求** —— 真正把选择变成一次安装的还是主窗口里那
170 行的 `onInstallButtonClicked()`。那一段读的全是这一页的状态（`gameVersion()` /
`selectedLoader()` / `pinnedLoaderVersion()` / `currentPresetSelections()`），摆在主窗口
里等于"谁都知道这一页长什么样，只有它自己不算"。

搬走的是**整段流程**，一句判断都没改：四道前置闸（有没有别的下载在跑 / 原版号在不在 /
名字合不合法 / 盘上已经有一份时问"校验并修复"）、摊开预设计划并让用户确认（写进
`DownloadCompletionController`，见 ⑭）、挑安装器要的 Java，最后**先记
`m_installingInstanceId` 再 `setDownloadUiBusy(true)`**（顺序的原因逐字留在那段的 NOTE 里）。

`pickInstallerJava()` 跟着一起走，并**改成了公开**：它还有第二个调用者 ——
`InstanceUpdateFlow` 准备核心那一趟（参数里那个加载器下标本来就是安装页那棵树的下标）。
参数怎么翻枚举、要哪个 Java 的判据仍在 `VersionManager::loaderKindFromIndex()` 与
`JavaSettingsController::installerJavaFor()`，这一层只负责问与弹窗。

接线改了一条：`getInstallButton()->clicked` 从 `&MainWindow::onInstallButtonClicked`
改为 `m_installPage, &InstallPageController::installClicked`。主窗口里那两个声明直接删掉
（没有 moc / `QMetaObject::invokeMethod` / 校验代码按名字点它们）。

`src/MainWindow.cpp` 8,781 → 8,610 行；`src/controllers/InstallPageController.cpp` 795 → 977 行。

逐行核对过：`HEAD` 的 `onInstallButtonClicked()` / `pickInstallerJava()` 与搬过来的两份，
去掉注释与接收者前缀之后只剩 14 处 API 改名（`selectedLoader()`、
`currentPresetSelections()`、`m_downloadCompletion->begin()`），`pickInstallerJava()` 零差异。

验收：`ctest` **39/39**、真屏 `mw_check` **2716/2716**、离屏 `ui_check` **3121/3121**。

## 2026-10-28 ⑯a `DownloadTaskLedgerController`：「这一批下载的账」

搬的是**一套只在一段时间里有意义的状态**：从 `setDownloadUiBusy(true)` 起到那一条终态信号
为止。分开看每一格都像个小开关，合起来才是"这一批下载"这个对象 —— 所以它们一起走：

| 原来在 `MainWindow` 上 | 现在 |
| --- | --- |
| `m_downloadTaskId` | `task()` / `setTask()` / `clearTask()` |
| `m_downloadBatchOwnerTaskId`（失败清单归谁、「重试」资格） | `owner()` / `owns()` / `setOwner()` / `clearOwnerIf()` / `clearOwner()` |
| `m_downloadParentTask` + `m_downloadParentStep` | `setParent()` / `parent()` / `parentStep()` / `dropParentIf()` |
| `m_resourceBatchLabel` | `resourceLabel()` / `setResourceLabel()` |
| `m_downloadCancelInFlight` | `userCancelInFlight()` / `setUserCancelInFlight()` |
| `m_progressThrottle` | `takeProgressTick()` / `resetProgressThrottle()` |
| `beginDownloadTask()` | `begin()` |
| `setDownloadTaskParent()` | `setParent()` |
| `setDownloadUiBusy(false)` 里那半截兜底 | `finish(fallbackError)` |

一条判据都没跟着搬：账本还是 `ActivityCenterController`，"这一批是替谁做的"仍是主窗口上那
四个 id，能不能取消仍是 `VersionManager`。这一层只记"这一批是谁"，并按它收尾。

**为什么叫 `DownloadTaskLedgerController` 而不是 `DownloadBatchController`**：后者已经被
**检查点 / 断点恢复**那一层占了（`src/controllers/DownloadBatchController.*`，接
`DownloadManager` 的 receipt、暂停继续、续装意图）。两件事不重叠，别合并。

接线：`ConnectionsController` / `ImportController` / `InstallPageController` /
`InstanceUpdateFlow` / `DownloadInstallContinuation` / `ResourcePageController` /
`DownloadCompletionController` 七处共 30 余个调用点改成问账本控制器；
`MainWindow::downloadTaskId()` 这个访问器、`beginDownloadTask()`、`setDownloadTaskParent()`
三个声明一起删掉。`MainWindow.cpp` 8,610 → 8,540 行。

语义比对：HEAD 的 `beginDownloadTask()` 与新的 `begin()` 归一化后**只差成员改名**（父任务那
一条 `beginChild(` 的换行也变了，参数一个没动）。

验证：ctest **39/39**（39 项）、真屏 `mw_check` **2716/2716**、offscreen `ui_check` **3121/3121**。
（迁移前 `MainWindow.cpp` 8,610 行，迁移后 8,540 行。）

## 2026-10-28 ⑰ `ServerMenuController`：「服务端」那一页的右键菜单

菜单上八项，七项只是转发（开控制台 / 开目录 / 设置页 / 修复 / 强制结束 / 删除都各有出处），
真正有判断的只有两处 —— 那一处就是这一片的价值：

- **用不上的那几项不摆**（而不是摆一颗灰的）：装好了就没有「修复」、没在跑就没有「强制结束」；
- 「异常退出后自动重启」是**长期设置**：勾没勾读盘上那份 `.wtfml-server`，
  勾一下就写回去；取消勾选还要把这一轮的崩溃计数清掉（再打开时从第 1 次算起）。

接线：`ConnectionsController` 里 `customContextMenuRequested` 直接接
`&ServerMenuController::showAt`；`MainWindow::showServerContextMenu()` /
`runServerMenuAction()` 两个声明删掉。`MainWindow.cpp` 8,540 → 8,447 行。

**顺带修掉一个真问题**（不是重构，是行为修正，单独一条用例）：

原来那一格是 `readMeta()` + `writeMeta()` 自己拼的，而 `readMeta()` 在"文件在、但读不懂"时
回的是**空** `Info` —— 照它写回去等于用一份最小合法记录**顶掉**盘上那份记录（游戏版本 /
内存 / 端口全没），而且一句提示都没有（`writeMeta()` 那边只看写没写成）。
现在这一格走 `ServerRegistry::setRestartOnCrash()`：先看 `readMeta()` 的 `problem`，
读不懂就原样留着、把那句话回给调用方（界面弹「没记住」）。文件本来就不在
（用户从外面拖进来的服务端）不算问题，照写。
用例：`tests/core_modules/ServerRegistryTests.cpp` 的 ZX8.5（坏文件在 ⇒ 回 false、
问题非空、**字节一个没动**；修好之后照常写、而且同一份记录里别的格还在）。

验证：ctest **39/39**（39 项）、真屏 `mw_check` **2716/2716**、offscreen `ui_check` **3121/3121**。

## 2026-10-28 ⑱ `ResourceInstallController`：「把这一份资源装进那个实例」

原来是 `MainWindow::installResourceVersionNow()` 那二百来行。搬走的是**这一批下载的
全部前置判断**，每一条都带着"为什么不能反过来写"：

- **落点**：实例里那四类子目录，还是整合包的缓存目录（整合包不能进 `versions/`——
  那底下每一个子目录都会被当成一个实例）；
- **问不问「存哪儿」**：设置里没勾 **或** 这一栏不是从实例里点进来的。两条来路读的是
  同一份来路（`m_downloadReturn`），和「← 返回实例设置」那颗按钮同一个出处；
- **谁在动盘**：默认是那个实例；用户挑了别处的目录时必须清空（不锁任何人启动）；
- **加载器对不上**：只管模组，判据刻意做窄（认不出的加载器 / 多加载器 / 纯原版都放行）；
- **任务号先建**：盘上都已经有的时候那一批会**同步**收尾，先建号才认领得到终态；
  忙态先问一遍，被拒的白问不在账本上留一行红字。

两个调用点（`installResourceVersion()` 与前置检查回来那一趟）改成直接问
`m_resourceInstall->installNow(...)`，`MainWindow.hpp` 那个声明与它的长注释一起删掉。
`MainWindow.cpp` 8,447 → 8,245 行。搬的是同一段代码：归一化后逐行比对只有
`this` → `&owner`、`tr(` → `MainWindow::tr(`、成员前加 `owner.` 三类差异（脚本
`build/tmp-wtfml-diff-18.py`）。

**自证到什么程度**：这一段**没有** mw_check 用例直接钉住（"存哪儿"那一支要真对话框），
所以凭的是上面那句逐行比对 + 三个验证目标没红；真正跑过的行为断言仍是资源详情页 /
下载页那几条既有用例。

验证：ctest **39/39**（39 项）、真屏 `mw_check` **2716/2716**、offscreen `ui_check` **3121/3121**。

## 2026-10-28 ⑲ 前置依赖检查 → 同一个 `ResourceInstallController`

⑱ 搬走的是"真正下那一段"，留下的那五条函数是它的**上半截**：点下载 → 看这一版声明的
必装前置里还缺哪几个 → 一条不缺就直接下、有缺的先问名字再逐条问版本 → 弹一个框 →
按用户那一按回到 `installNow()`。它们共享一份状态（`DependencyCheck`：正问着哪几个、
还差几发回信、算的是哪个实例），而那份状态只在这一趟点击到"下好了"之间有 意义。

搬法：五条函数与 `DependencyCheck` 整段进控制器，`MainWindow` 只留
`installResourceVersion()` 一条薄壳（详情页与搜索页按名字叫它，接线一个字没动）。
`ResourceStore::versionsFinished` / `projectsFinished` 两条连接从 `MainWindow` 改接到
控制器实例上 —— 那本来就是"谁的回复谁接走"（三条 `versionsFinished` 各按自己的钥匙认人）。

**没跟着搬的**（它们不是这一趟的私事）：`dependencyInstanceId()` /
`downloadCameFromInstance()` 是"要装进哪个实例"的唯一出处，`dependencyTargetFor()` /
`installedModKeys()` 更新检测那一趟也在用。

`MainWindow.cpp` 8,245 → 8,053 行（另删掉两段早就没有声明的旧注释 —— 那两件事现在
写在 `controllers/ResourcePageController.hpp` 里）。五条函数归一化后逐行比对只有行宽、
`this` → `owner.`、`m_dependencyCheck` → `m_check` 三类差异（脚本
`build/tmp-wtfml-diff-19.py`）。

验证：ctest **39/39**（39 项）、真屏 `mw_check` **2716/2716**、offscreen `ui_check` **3121/3121**。

## 尚未收口

- MainWindow 中剩余的协调职责及过大的 API/状态头（更新中心、服务端设置、安装页、
  服务端起停、下载收尾、安装按钮、这一批下载的账、服务端菜单都已搬出，「实例预设」那一行也搬了）；
  剩下的大头是导航/实例元数据（那份清单见 `docs/INSTANCE_META_INVENTORY.md`）
  与实例设置的编辑控件 / 脏状态边界。
- 「这一批下载的账」七格已搬进 `DownloadTaskLedgerController`（⑯a），但
  `m_repairingInstanceId` / `m_installingInstanceId` / `m_importingInstanceId` /
  `m_assetPrefetchInstanceId` 那四个"这一批是替谁做的"仍留在主窗口上 —— 它们同时是
  `installingVersionIds()`（「谁不能启动」的唯一出处）的判据，"账本记谁"与"谁能启动"
  是两件事，要搬得先想清楚这一层怎么不变成第二个判据。
- InstanceSettingsDialog 的编辑控件与**脏状态**边界仍在外壳那一份（概览/启动/文件那几页已分 TU），
  下一步要连着"改了哪几项就写哪几项"一起看，不能只搬控件。
- UiContentPagesChecks 等大测试仍共享长寿命夹具，先明确夹具生命周期再分领域，避免机械搬运改变事件时序。
- VersionManager 已分为核心、加载器安装、整合包导入、实例识别与导出，本轮没有再按行数切割它。

## 之前的记录（保留原验收口径）

以下为本轮前主计划中的原文，其数字是历史结果，不代表本轮重新验收。

- [ ] **重构｜大文件职责**：按控制器/页面/纯逻辑拆 MainWindow、MainWindowUI、VersionManager、InstanceSettingsDialog 和大测试模块。
  当前约 758/381/342/274 KB，仅为维护热点线索，不是性能结论；保持行为及测试归属，不做纯机械拆分。
  2026-10-25 已在 `MainWindow` 上拆出两片（都是"按职责"、不是按行数切），`MainWindow.cpp`
  13,949 → 11,505 行、38/38 全绿、`mw_check` 2701/2701：
  ① `controllers/MainWindowViewData`：各控件的**视图模型**（实例树 / 分组行 / 底栏面板行 / 版本下拉框），
  纯函数、不碰网络与盘，顺手删掉九条早已没人调的重名助手；
  ② `controllers/ResourcePageController`：「资源详情」那一页的**全部状态**（`ResourcePageState`
  整个搬过去）与开页 / 拨勾选框 / 筛分组 / 收尾那几条；主窗口只留四条薄壳
  （`onResourceDownloadFinished` 被 `ConnectionsController` 按名字接走，另三条别处直接叫）。
  控制器**构造里就建**、页面对象仍懒建 —— 图标与版本列表那两发会比"开过页"早到，
  状态里没有"还没建"这一档。
  ③ `controllers/DownloadFilterController`：下载页每一页顶上那一行筛子的**灌与读**
  （来源 / 版本 / 加载器 / 类型 + 左栏分类那一行 + "这一页空着才搜"）。那一行"内容全在这儿灌、
  读也只从这几个访问器读"的规矩现在有了唯一的门；搜索本身（请求钥匙、结果怎么摆行）仍留主窗口。
  两次搬运后 `MainWindow.cpp` 13,949 → 11,306 行，`mw_check` 2701/2701、38/38 全绿。
  2026-10-26 ④ `ui/MainWindowUI`：`setupUi()` 里那 1,590 行按**一页一摊**拆成
  `setupHomePage()` / `setupInstancePage()` / `setupDownloadPage()` / `setupSettingsTab()`
  四个函数（只共享已经挂好的 `tabWidget`，各自的局部变量谁也不外流）。`setupUi()` 现在
  219 行，只剩"窗口与标题栏、按契约顺序摆四页、导航接线、底栏、覆盖层、外框"。
  实测那份"一直排第一"的热点其实看错了口径：`setupSettingsDownloadPanel` 只 136 行。
  ⑤ 再把 `setupDownloadPage` 那一页里真正不同的三摊拆开：分类列表/页头/堆叠页外壳留在
  `setupDownloadPage`，第 0 页「游戏」进 `setupDownloadGamePage()`（版本树 + 安装页 + 页头
  显隐那两条 connect），其余几页那段循环进 `setupDownloadResourcePages()`。
  三摊之间只共享已经挂好的 `downloadStack` / `gameStack` / `downloadHeader`，没有局部变量要传；
  "第 0 页先加、其余按表顺序往后加"这条下标契约就落在 `setupDownloadPage` 里那两行调用上。
  现在 `ui/MainWindowUI.cpp` 里最长的是 `setupSettingsLookPanel()`（434 行）、其次
  `setupDownloadGamePage()`（370）与 `setupInstancePage()`（364）。
  ⑥ `ui/InstanceSettingsDialog`：构造函数那 1,631 行按"一栏一摊"搬出三摊 ——
  `setupOverviewPage()`（595）、`setupLaunchPage()`（419）、`setupFilePages()`（434），
  构造函数只剩"两栏外壳 + 三行调用 + 接线/灌值"。原来那三摊是**同一个函数里换着用一份
  `grid`、`row` 从 0 数起**（注释里就写着这条），现在每摊自己 `addForm()` 一份；
  只有五样"这一次打开对话框的现场"要传进去（目录名、全局值、追加 JVM 参数、提醒开关、窗口档位），
  不存成成员。
  ⑦ `manage/VersionManager`：146 个成员函数、单函数最长 238 行 ⇒ 是聚合大、不是局部烂，
  于是按"一份状态机一摊"把 6,137 行拆成四份 .cpp + 一份内部头：
  核心（`VersionManager.cpp` 2,197 行：版本清单 / 版本 json / 下载编排 / 批次收尾 / 实例元数据）、
  加载器安装（`VersionManagerLoaderInstall.cpp` 2,101）、整合包导入（`VersionManagerModpack.cpp` 742）、
  实例识别与导出（`VersionManagerExport.cpp` 1,046）。**类没动**（同一个类，定义分散在四个
  翻译单元），所以断言一条都不用改：38/38 全绿、`verify` 5954/5954。
  原来住在本文件顶上匿名命名空间里的 17 个助手（`copyTreeInto()` / `installerOutputTail()` /
  `readVersionJsonAt()` 等）搬进 `VersionManagerInternals.hpp`：匿名命名空间是**内部链接**，
  拆完另外三份根本看不见它们；各抄一份的话"只改一处"就是静默不一致（`copyTreeInto()` 自己
  那段注释写的正是这件事）。两个写法上的坑：头文件里的函数必须 `inline`，那两个
  `const QString` 也得写 `inline` —— 不写不在编译期报错，是链接期一串 duplicate symbol。
  缝合线挑的是**状态机的归属**（`LoaderInstall` 只在安装那一份里走动，`ModpackInstall` /
  `NamedInstance` 只在导入那一份里），不是行数；唯一横跨几摊的是批次收尾那个岔口
  `onAllDownloadsFinished()`，它留在核心这一份里，谁要接下去都在那一处一眼看全。
  这三份 .cpp 要同时进三张源文件表（主目标、`tests/CMakeLists.txt` 的 `wtfml_core_runtime`、
  `tests/mw_check/CMakeLists.txt`）—— 漏一张不在编译期报错，是链接期一串
  `VersionManager::…` 的 undefined reference。


## 2026-10-29 独立复核

发现 `ResourceInstallController` 继承 QObject 但构造未传 parent，MainWindow 仅保留裸指针，
关闭主窗口不会释放该控制器。补 `QObject(owner)`，工作区回归通过真实主窗口构造/销毁检查
恰有一份控制器且 QPointer 归零。专项 `20261029-011542-e157c114`，工作区/活动选择 2/2。
本轮独立完整回归 `20261029-011742-adabd43e`：非视觉 38/38，测试阶段 376.20 秒；
视觉 `20261029-012814-027533df`：3121/3121，10.05 秒。
正式构建通过（`build/review-release-20261029.log`）；模块静态检查和 diff 检查通过。
既有拆分历史断言数不当作本轮独立验收；本轮未重复真机/实网矩阵。

## 2026-10-29 实例文件选择模块

`InstanceFilesSelection.cpp` 统一选择状态、批量操作接线和操作栏布局，状态仍由原类持有。
原文件 2803 → 2407 行，选择模块 415 行。三套构建源列表均已登记，扫描/行构建尚未拆完。
同时合并启用/禁用统计，并将逐行反选改为选择模型的一次事务，避免每行重算整套状态。
模组专项、1000 条目测量、完整 UI 3123/3123 和 Release 构建通过；
测量范围、证据及尚未完成项见 [选择性能](P2_SELECTION_PERFORMANCE_20261029.md)。

## 2026-10-31 后台完成不再驱逐用户正看着的那一页

审 `execPage()` 那一片时确认的一个真缺陷：后台弄完的事（更新检测回来、更新装完、
体检跑完）走的是 `runWhenOverlayFree()` —— 它的规矩是**先把占着这一层的那一页收掉**
再开新的。于是"检查更新在后台跑着，用户翻进了实例设置"的结果是：结果一回来，他那一页
自己没了（实例设置那页还会连带丢掉翻到哪儿、滚到哪儿，虽然字段是改了就写的）。

修改（只有这一条，不做页面栈）：

- `OverlayHost::queuePageOpen(show)`：空着就当场调，**被占着就排着**（整页、提示、
  **收着的页**都算占着 —— 收着的那一页用户切回去还指望它原样在）。收干净之后下一轮
  才调，最多排 4 发，超了丢最老的。收页时排的那一发和提示那条排水路数完全相同
  （`QTimer::singleShot(0)` + 先排下一次再摆这一发），不新造时序。
- `MainWindow::openPageWhenIdle()` 是它在主窗口这一侧的出口，和
  `runWhenOverlayFree()` **并存**：用户自己点的那些照旧驱逐（"我点了，把挡路的收掉"），
  后台完成的三处改用新的（`InstanceUpdateCheckController::finishUpdateCheck()`、
  更新收尾后重开更新中心、`IntegrityController` 的体检报告页，后者的 `Host` 注释同步）。
- 用例在 `tests/ui_check/modules/UiOverlayNavigationChecks.cpp` 的 O2b：被占着时那一发
  一句都不执行、用户那一页的标题和层级不动、别人照样开不进来；收掉之后那一发自己出来，
  而且不会跑第二遍；空着时是当场执行（返回 false）。

仍然**没有**做的那一半：`execPage()` 的嵌套事件循环、根页来路与"逐根页迁移明确路由栈
（返回恢复进入前状态）"——那会改动每一页的返回语义和所有阻塞调用点，不在一轮里顺手做。


## 2026-10-31 四档安装占用收进 `InstallOccupancy`

「大文件按状态所有权拆分」里点名剩下的一摊：主窗口上那四个"这一批下载是替谁做的"
`QString`（导入 / 安装 / 补全 / 预取）。它们本身没几行，麻烦的是**判据被抄了好几遍**：

- "是不是正在改这个实例"这条三段 OR 在四个地方各写一次（补全入口的守卫、体检的
  `targetBeingModified`、预取的守卫、文件页/预设那一侧的占用集合）；
- 优先链（导入 > 安装 > 补全 > 预取）在任务账那边写了两遍 —— 标题一遍、目标一遍，
  两处还得保持一致；
- 收尾那一段四条 `x == m_xxxInstanceId` 各自再清一次。

只要有一处漏了某一档，症状就是用户能直接看见的："装着的实例还能点启动"、"这一批
认错了自己是哪一类、收尾说了别人的话"。

修改：新增 `src/controllers/InstallOccupancy.hpp`（纯值、一个 TU 都不多），四档收进去，
问法只在这里定义一次 ——

- `active()`：现在说话的那一档（优先级就是 `kOrder` 那张表），任务账的标题/目标改用它；
- `modifying(id)` / `anyModifying()`：导入 / 安装 / 补全（**预取不算** —— 它写共享的
  `assets/asset_director`，实例目录一个字都不动）；
- `instanceIds()`：四档全部非空 id（并进 `installingVersionIds()`，预取也在内 ——
  它照样不该被启动）；
- `clearFor(id)`：收尾时清掉**所有对得上的档**并返回最高优先级的那一档。真有两档撞了
  同一个 id（不该发生）时两档都松开：只松一档的话另一档会永远占着，那个实例从此再也
  启动不了。

写法上：`setBatch()` 是唯一写入口（"先记占用再置忙"那个顺序写出来就看得见是哪一档），
`Batch::None` 不是一档（写它不算数）。主窗口只留一个 `InstallOccupancy m_installOccupancy;`
成员，那一处的旧注释（为什么界面要自己记这一笔）改成一段总说明；预设那一侧窄的闸仍
只认**导入**这一档（`is(Batch::Import, id)`），没有被顺手放宽成 `modifying()`。

验证：新增 `tests/core_modules/InstallOccupancyTests.cpp`（OC1–OC8，16 条）钉住优先级、
"预取不算正在改它"、`clearFor()` 的两条边界（对不上的不动、撞 id 两档都松）。
`verify` 6113 → 6129、`verify_instances` 1581 → 1597；`verify_resources` 913/913、
`ui_check` 3133/3133、`mw_check` 2722/2722 全绿。
顺手补上 `owners.json` 里漏登记的 `ResourceStoreBudgetTests.cpp`（`fa95720` 加的文件，
`check-test-modules.ps1` 在这一次之前就已经红着）。

还剩：实例设置的编辑控件与脏状态（那一摊的状态所有权在 `InstanceSettingsController`
那一侧），本轮没动。

## 2026-10-31 「导出启动脚本」的正文收进 `LaunchScriptExport`

「大文件按状态所有权拆分」里另一摊：`MainWindow::exportLaunchScript()`。它前半段是问用户、
取令牌、算 Java 值，后半段是一大段内联代码加一个文件内的 `static QString batchQuote()`，
把一串启动参数拼成 cmd 认的 .bat 正文。拼的规矩全是**看不见的**：

- 哪些字符要让 cmd 包一层引号（空格/制表符/空串/`&|<>()^"`），而 `!` 故意不动（脚本没开
  延迟展开，转义反而多出一个 `^`）；
- `%` 必须写两遍（引号里照样展开）；
- 注释要按**本地代码页**写得出来，英文版 Windows 上换成英文（`toLocal8Bit()` 会把汉字变成
  `?`）；
- 正版那一路令牌换成 `%WTFML_MC_ACCESS_TOKEN%` 引用，原始令牌一个字节都不落盘；
- 拼完再过一道已知凭据，对得上就整份不写。

以前这些只有"开对话框、真令牌、双击脚本"才验得到 —— 错一个字节用户双击时才看得见。

修改：新增 `src/launcher/LaunchScriptExport.hpp`（纯头文件，值进、文本出）：

- `quote(arg)`：上面那两条引号规矩（原 `batchQuote`，连注释一起搬过来）；
- `compose(Input)`：`Input` 只有四个值（数据目录 / Java 程序 / 已经展开好的参数 / 令牌与
  离线标志），拼出整份正文；返回**空串** = 里面有已知凭据。程序名为空时的裸 `java` 兜底
  也挪进来了（写 `""` 出去是个跑不起来的脚本）。

主窗口那边只剩"凑值 + 写文件 + 说结果"：`QSaveFile`、"写不进去怎么报"、"哪几件事要提一句"
都留在原处（那些是界面层的事）。行为不变，唯一的写法差异是令牌替换多了 `!token.isEmpty()`
这一道守卫（正版这一路走到那里时令牌必然非空；离线不替换）。

验证：新增 `tests/core_modules/LaunchScriptExportTests.cpp`（模块 `launch`，LX1–LX5，19 条）
钉住引号规矩、离线那一份的结构、空程序名兜底、正版令牌替换（且引用本身不被 `%` 那条规矩
写成两遍）、以及"带已知凭据整份不写 / 去掉那一条照常拼得出来"。`verify` 6129 → 6148、
`verify_launch` 310 → 329；`verify_resources` 913/913、`ui_check` 3133/3133、
`mw_check` 2722/2722 全绿，`check-test-modules.ps1` / `check-ci-partitions.ps1` 通过。

## 2026-11-01 八页改成非阻塞开页（`OverlayHost::showPage()`）

「导航/实例元数据渐进收口」里"逐根页迁移明确路由栈"的头八页：任务详情、体检报告、
工具箱、更新中心、服务器控制台、服务器设置、Java 管理、账号与外观。挑它们的标准是同一条：
**开页那一句之后不收任何结果**，以前却要卡在嵌套事件循环里等用户点返回。那一页开着的
时候，"下面"的每一句（重摆内容、再点一行、后台收尾）都得按"这个循环还在转"来读 —— 这
正是那一项要收掉的那类时序。

修改：

- `OverlayHost::showPage(dialog, title, ownerTab)`：`execPage()` 的**非阻塞**兄弟。摆上去
  就返回；返回箭头接成 `reject()`，这一页自己的 accept/reject（Esc、页上那颗「关闭」）
  一起落到 `QDialog::finished` —— 那里收掉这一层。`dismiss()` / `evict()` / `closeOverlay()`
  走的仍是原来那几条路，非阻塞页在 `closeTopLayer()` 里**不需要特判**（没有循环要醒）。
- 已经有一页占着这一层时返回 false，**而且**不碰正开着的那一页（同一颗按钮被按两下时，
  旧写法会把眼前这一页收进寄存处 —— 页自己没了，比"没开成"糟）。
- 页面对象不删：收页时和其它对话框一样"还回去"，同一个对象再摆上来照旧（`m_pageWidget`
  只记"现在这一页是不是 `showPage()` 开的"，顺带把两条收页接线一起断掉）。
- `OverlayHost::pageClosed(QWidget *page)`：所有收页路都汇到 `closeOverlay()`，所以收页钩子
  只发那一处（带的就是刚才摆着的那一个对话框；`execPage()` 那一档从内容区里认）。以前靠
  "`execPage()` 返回之后"做收尾的调用方改接这一发 —— 更新中心 / 服务器控制台 /
  服务器设置页各自用它清掉"这一页说的是谁"（`m_instanceId` / `m_serverId`），接的时候按
  页面对象认人，别的页收掉不会误伤。
- `TaskDetailController::open()`、`IntegrityController::openReport()`、
  `ToolboxController::open()`、`UpdateCenterController::open()`、
  `ServerConsoleController::open()`、`ServerSettingsPageController::open()`、
  `JavaSettingsController::openManagePage()`、`MainWindow::onAccountSkinButtonClicked()`
  改走它，注释里"必须在 `execPage()` 之前"那几段删掉 —— 现在开完就回来了；`TaskDetailPage`
  文件头、`MainWindow::openTaskDetailPage()` 的 NOTE 同步。八处摆不上时都留一句日志
  （不是静默失败），那三处有"说的是谁"的当场清掉。

验证：`UiOverlayNavigationChecks` 的 O2c（12 条：QDialog 默认是顶层窗口 → 摆上去之后
嵌进内容区、返回 / Esc / `evict()` 三条收页路都收干净、重复摆被拒且不动眼前那一页、
三条收页路各发一次 `pageClosed()` 且带的是这一个对话框）；
`MwWorkspaceCheck` 4 条（真控制器非阻塞开、一层只放一页、「关闭」和返回箭头各收一次、
同一页对象能再摆上来）；体检报告页那条由 `runIntegrityScanTests` 的 T33.2 量
（扫完自己开页、页眉是「完整性检查」、顶上那句总述说的是这个实例）；Java 管理页那条
`runJavaManageTests` 的 T26 与账号与外观那页 `MwAccountAppearanceChecks` 的 Z2 / Z2b
改成"点完按钮再转事件等探针"，不再假设 `click()` 会阻塞。
`A` 类调用点 12 → 4，见 [嵌套事件循环盘点](NESTED_EVENT_LOOPS_AUDIT.md)。

### 2026-11-02 「分组设置…」也改了（第一张"要结果"的非阻塞页）

`MainWindow::openGroupDialogNow()` 原来要 `execPage()` 的返回值：`Accepted` 才
`applyGroupList()`。现在改成 `showPage()` + `pageClosed()` 钩子，结果用
`QDialog::result()` 读（同一个值），对话框改成每次开新建一份、收页后 `deleteLater()`：

- 钩子接在**对话框**上（不是 `this`）当上下文，`Qt::SingleShotConnection` —— 这一页收掉
  之后对话框就删了，连接跟着一起没；接在 `this` 上的话每开一次会多留一条永远不会匹配上
  的连接。
- 取消 / 返回箭头 / Esc / 被别的页顶掉都是 `Rejected`，磁盘一个字节不动（和原来一样）。
- 摆不上时（覆盖层刚被占）删掉新对话框并留一句日志。

验证：`MwInstanceGroupsChecks` 的 TGD（点那颗按钮点完就返回、页真的摆上来；在页上新建组、
点「确定」→ 真的写进 `wtfml.folders`；再开一次、新建组、点「取消」→ 一个字节都没落盘）。
这一段以前写不出来 —— 注释里"要过 `exec()` 这个模态循环，offscreen 下驱动不了"说的就是它。

### 2026-11-02 预设编辑器也改了（结果走回调）

`PresetController::Host` 上原来那格 `execPage(dialog, title, ownerTab)`（返回值 = `execPage`
的返回值）换成了 `showPage(dialog, title, ownerTab, onClosed)`：摆上去就返回 `bool`，
`onClosed(int)` 在这一页收掉时叫一次，参数是 `QDialog::result()`。主窗口那一侧接的是
`OverlayHost::showPage()` + `pageClosed()` 钩子（同分组设置那一套：接在对话框上、
`Qt::SingleShotConnection`）。

`openEditor()` 原来排在 `execPage()` 返回之后的收尾（`m_editor` 清空 / 刷新列表 /
`hide()` + `deleteLater()`）整段挪进 `onClosed`；摆不上时删掉新页并留一句状态。
收页语义没变：`closeEditor()` 照旧 `accept()`，`reject()` 照旧被页面接管成"问要不要保存"。
`captureForEditor()` 里"开完立刻给页面上提示"那一句照旧在 `openEditor()` 返回之后读
`m_editor` —— 现在开页是同步摆上去的，这一条仍然成立。

验证：`PresetPreviewChecks` 里那一段从"排一发 `singleShot` 进嵌套循环"改成了顺序代码
（开页返回时页面就在屏幕上、存草稿之后页面真的收掉了、重开草稿拿到同一份文档），
假宿主换成一条同语义的 `showPage`（`finished` → `onClosed`）。

### 2026-11-02 资源详情页也改了（要返回值的那一页走回调）

`ResourcePageController::open()` 原来返回 `execPage()` 的结果（`kResourcePageNotOpened`
= 没开出来那一档），调用方开完立刻读 `state().open` 往下走。现在改成 `bool` + 一个
`std::function<void(int)> onClosed`：

- 返回 `true` = 这一页摆上去了；`false` = 没开出来（覆盖层正忙 / 那一类的键不认识 /
  那把钥匙不在表里）—— 和以前 `kResourcePageNotOpened` 那一档同义，`state().open`
  在 `showPage()` 之前就落下来，所以"开完立刻读"的地方一行都不用改。
- `onClosed(int)` 接在 `OverlayHost::pageClosed()` 上（按页面对象认人、
  `Qt::SingleShotConnection`），参数是 `QDialog::result()`：`Rejected` = 返回箭头，
  `Accepted` = 页上那颗「模组设置」。

主窗口那侧只有一个真正的调用方要判结果：`openFileResourcePage()`（从实例文件页点
「详情」进来的那一路）。它的收尾原来排在 `execPage()` 返回之后 —— 现在整段挪进
`onClosed`，而且**再排一发** `QTimer::singleShot(0, …)` 才去开实例设置：`onClosed`
是在 `closeOverlay()` 发 `pageClosed()` 的栈里跑的，直接在里面开另一页（实例设置还是
`execPage`）等于在收页的半路上又开一层嵌套循环。

验证：`runInstanceFileResourceTests` 的 T24 里那几发探针从"排在详情页那层嵌套循环里"
改成"排到下一轮 + 没开就再排自己一轮"（`overlay->pageOpen()` 判据）；
`runDependencyAndBatchTests` 的 T31 收尾同理（产品把"退回实例设置"排到下一轮，
收它那一发必须等它真起来）。

### 2026-11-02 实例设置也改了（最后一处 `execPage()`）

`InstanceDialogController::open()` 是 A 类里最后、也最大的一处：`open()` 里那十几个
`connect` 的 lambda 都按引用捕着**栈上**的 `dialog`、`targetId`、`refreshInstanceDirs`、
`serverEdits`、`exportPreset`。`showPage()` 摆上去就返回 ⇒ 函数一退这些全悬空。

没有逐条改捕获（那是几十处、而且很脆），而是把这五个量搬进一个
`InstanceSettingsSession : QObject`：`targetId` / `refreshInstanceDirs` /
`exportPreset` 是成员，`serverEdits` 由它的析构函数 `delete`，整个 session
`setParent(dialog)`。关键在于 C++ 的捕获语义 —— 捕获引用变量捕的是**被引用的对象**
而不是那个引用变量本身，所以 `[&dialog]` / `[&targetId]` 一个字都不用改，只是它们
指的对象从栈上换到了堆上。

收页之后的两件事接在 `OverlayHost::pageClosed(QWidget *)` 上（上下文对象是 `dialog`，
所以它一收掉连接跟着断）：`exportPreset` 为真时排一发 `singleShot(0)` 去开预设编辑器，
最后 `dialog.deleteLater()`。`showPage()` 没开出来（覆盖层正忙）那一档照样
`deleteLater()`，不再有"返回值"要判。

验证：`mw_check` 全绿（2735/2735）、`mw_check_workspace` 全绿（45/45）、全套 ctest 39 项
全绿。用例侧要改的都是"探针排在嵌套循环里"这个假设：凡是在 `invokeMethod(openInstanceSettings)`
之前挂 `QTimer::singleShot(0, …)` 的，改成开完页**当场**跑（`showPage()` 返回时页面已经在
屏幕上了）；凡是靠"页收掉之后 `deleteLater` 马上生效"的，补一发
`QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete)` ——
`processEvents()` 不递 `DeferredDelete`，不冲的话覆盖层的停放区里还留着一份旧页，
`findChild<InstanceSettingsDialog *>()` 会把那份已经收掉的交回来。

### 2026-11-02 返回实例设置：三份散落的来路收成一条 `NavigationReturn`

「逐根页迁移明确路由栈（返回恢复进入前状态）」那一项里，真正缺的不是"把来路存下来"
（那三样各自都存了），而是**它们不是一份东西**：

- `m_downloadFromInstanceId` / `m_downloadFromPageKey`：从实例设置进下载页的那一趟记的是
  实例和页键，但**归哪颗根页**没记 —— 返回时 `InstanceDialogController::open()` 拿
  "当下停在哪颗根页"现算。从主页快捷入口进去的那一趟，回来时人已经被摆在下载页上，
  于是算成「实例」：用户明明是从主页进来的，回来却落在另一颗根页上。
- `MainWindow::FileScrollReturn`：从资源详情页返回时的那一份（实例 / 页键 / 顶行身份 /
  偏移），只有那一支在写。

另外还有两条真缺陷：

- 「← 返回实例设置」那条路（`returnToInstanceSettingsFromDownload()`）**不量滚动位置**——
  详情页那一路量了，下载页这一路没有，所以从下载页回去必然落回顶端。
- 升级那一趟是在 `openDownloadCategory()` **之后**补写 `m_downloadFromPageKey = "upgrade"`：
  中间那一段状态是错的，而且只有调用方知道要补。
- 详情页被**别的根页**上的操作 `evict()` 掉时，`openFileResourcePage()` 的 `pageClosed`
  回调照样拿旧来路去开实例设置 —— 和用户刚点开的那一页抢覆盖层。

修改：

- 新增 `src/controllers/NavigationReturn.hpp`（纯值）：`rootTab` / `instanceId` /
  `pageKey` / `scrollIdentity` / `scrollDelta`，`valid()` 与 `hasScroll()` 各只回答一件事。
  `instanceSettings(...)` 是"从实例设置出发"的那一份的构造口。
- `MainWindow` 上留**两个**位子、形状相同但寿命不同：`m_downloadReturn`（"这一次进下载页
  是从哪个实例点进来的"——返回键露不露、已安装版本选谁、详情页当哪个当前实例）和
  `m_instanceReturn`（"回实例设置时把哪一页、滚到哪儿摆回去"）。下载页按返回时前者整个
  挪进后者；`openInstanceSettingsNow()` 在 `showPage()` **之前**消费后者（对不上实例 /
  页键就整份作废），用完就清。
- 两条离开实例设置的路都在**离开那一刻**现量滚动位置（`fileResourceRequested` 与
  `browseDownloadsRequested`）：回来时那是另一个对话框、另一批 `InstanceFilesPage`，
  位置只有这一刻还活着。
- `returnToInstanceSettingsFromDownload()` 先切回 `origin.rootTab` 再开页 —— 根页不是
  现算的。
- 升级入口直接带着 `pageKey = "upgrade"` 进下载页，不再事后改状态。
- `openFileResourcePage()` 的 `pageClosed` 回调加一道闸：收页之后如果当前根页已经不是
  进来时那一颗（= 这一页是被别的根页上的操作驱逐的），**不再**开实例设置。

验证：`mw_check` 新增 T25.10（11 条）——把实例的 `mods/` 造出 40 条、从**主页**开实例设置、
滚到中间、点「下载新模组」、按「← 返回实例设置」，断言落回**主页**、页键还是 `mod`、
顶上那一行还是离开时那一行且滚动条不在 0。`mw_check` 2746/2746、全套 ctest 39 项全绿
（`mw_check_workspace` 45/45）。


## 2026-11-03 `ui/InstanceFilesPage`：「列表里那一行」这一摊分出去

`InstanceFilesPage.cpp` 原来 2447 行，里面并着四摊互相看不见的状态：扫描（`refresh()` 那一趟
自己的递归与过滤）、筛选（状态下拉框和那几颗钮）、行构建（`m_icons` / `m_resourceTips` 两套
异步缓存 + `setItemWidget()` 托管的整棵行控件 + 悬停与行尾那几格）、选择与批量（已在
`InstanceFilesSelection.cpp`）。前三摊里只有**行**这一摊的形状是"每重摆一次就整套重建，而
图标和提示是之后异步补进来的" —— 它和扫描/筛选的数据流方向相反，接线也不共享，所以按状态
所有权拆的第三刀落在它身上：`ui/InstanceFilesPageRows.cpp`（794 行）。

- 搬过去的是 `rebuildRows()` / `makeRow()`，行尾那几格（`toggleButtonAt()` /
  `deleteButtonAt()` / `detailButtonAt()` / `editButtonAt()` / `nameLabelAt()`），以及
  `m_icons` / `m_resourceTips` 的读写（`iconForPath()` / `setIconForPath()` /
  `setResourceForPath()`）、详情钮状态（`applyDetailButtonState()` /
  `setRowDetailVisible()`）和悬停（`syncRowHover()`）。
- 只在这几处用的小工具（`makeSmallLine()` / `placeholderIcon()` / `scaleToIconSlot()` /
  `directoryThumbnail()`）跟着走，仍旧住在该 .cpp 的匿名命名空间里 —— 它们不调用
  `InstanceFilesPage` 的私有成员，别的翻译单元也用不着。
- 两处都要用的是 `makeSecondary()`：原来住在主 .cpp 顶上的匿名命名空间（内部链接），拆开后
  另一个翻译单元根本看不见。**搬进新头** `ui/InstanceFilesPageInternals.hpp`，函数写成
  `inline` —— 头文件里有定义、被两个 .cpp 各包含一次的话，重定义是链接期的事。没有两边各抄
  一份：它钉的是"次要文字取 `QPalette::PlaceholderText`"这一档颜色，抄开后改一处、漏一处，
  屏幕上就是"同一页里两种灰"，而代码里看不出哪一处才是对的。
- `directoryThumbnail()` 上面那段说明原来和函数隔着一整个函数体（前一次拆选择模块时被切开
  的），这次跟着函数一起回来了。
- 类接口一个字没改（`InstanceFilesPage.hpp` 只改了一处过时注释里的文件名）；三条构建源列表
  （主 `CMakeLists.txt`、`tests/mw_check`、`tests/ui_check`）各自登记新的 .cpp —— 少登记一条
  不是编译错，是链接期一串 `undefined reference`。
- 拆分时漏掉的一处 include：外壳少了 `#include "MainWindowUI.hpp"`（`WheelGuard` —— 状态下拉
  框不许被滚轮改），编译期报 `'WheelGuard' has not been declared`。重建一次就现形，没有别的
  症状。

行数：主 .cpp 2447 → 1691，新文件 794，`InstanceFilesPageInternals.hpp` 44。

验证：完整重建通过；`ui_check` 3165/3165（offscreen）、`mw_check` 2756/2756（真屏）；
全套 ctest 39 项全绿。


## 2026-11-03 `src/MainWindow.cpp`：「设置」选项卡那一片接线分出去

`MainWindow.cpp` 8187 行里，4215–4731 这 517 行是一条**只认两样东西**的接线：`MainWindowUI`
上那些控件，和"改了这个值要落到哪个键上"。后者逐项归 `LauncherPreferencesController`
（启动器行为 / 内存 / 下载源与限速 / 代理 / 版本名格式 / 额外 JVM 参数）或
`ThemeSettingsController`（背景图与配色）。它不碰启动、下载批次、实例树 —— 和文件里其它
几片（任务中心、更新中心、账号与外观）没有共享状态。

搬到 `src/MainWindowSettings.cpp`（同一个类，另一个翻译单元）：

- `applySettingsToUi()`（把盘上的设置灌回控件）、`pushVersionNameOptionsToUi()`；
  Java 与内存那一组（`refreshJavaList()` / `applyMemoryLimits()` / `clampXmsToXmx()` …）；
  下载与启动器行为那一组 `on…Changed()`；主题那一组 `onTheme…()`；以及
  `syncDownload*()` / `onCurseForgeApiKeyEdited()` / `onApplyExtraJvmArgsClicked()`。
- 三个文件内静态的"档位 → 文案"（下载并发 / 限速 / 代理）跟着走：只在这一片里用。
- 落盘时机**没有变**，仍然逐项写在自己那个函数上面；这一段里没有一处把几项攒起来写。

边界停在 `applyLauncherBehaviourAfterLaunch()`（启动之后窗口怎么收）与它后面的
`onVersionsFetched()` / 首页那一排：它们的主语是"启动"和"首页"，不是"设置页上那颗控件"。
类接口一个字没改；两条源列表（主 `CMakeLists.txt`、`tests/mw_check`）各自登记新文件 ——
`ui_check` 不编 `MainWindow`，没有第三条。

行数：`src/MainWindow.cpp` 8187 → 7673，新文件 562。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。


## 2026-11-03 `src/MainWindow.cpp`：服务端那两页的接线分出去

`MainWindow.cpp` 里 3279–4025 这 747 行是一整片**服务端**的接线：列表重扫、点一行之后
发生什么、控制台那几行、设置页那几组动作、修 / 删 / 起停，以及「新建服务端」那一页
（`openNewServerPage()` / 三档那一串 / `installServer()` / `pickServerInstallerJava()`）。
它和文件其余部分只共享"主窗口上那几个 controller 成员"，没有别的共享状态。

搬到 `src/MainWindowServer.cpp`：

- 文件顶上那个匿名命名空间整段跟着走：`loaderMarkIndexForServerFlavor()`（服务端八档挂哪
  张图）与 `kMaxLoggedServerLineChars`（日志里一行最多留多少字）都只给这一片用。内链接的
  东西不跨翻译单元 —— 留在原地是"一边报 defined but not used、另一边报没声明"。
- `refreshServerList()` / `openServerConsole()` / `updateServerConsole()` /
  `repairServer()` / `removeServer()`、设置页那十几条（玩家 / 内容 / 世界备份 / EULA /
  检查）`serverLaunchPlan()` / `updateNewServerSummary()`。
- 类接口一个字没改；两条源列表（主 `CMakeLists.txt`、`tests/mw_check`）登记新文件。

边界：`setInstanceMode()` 留在原处 —— 它是页面切换，只是切到服务端那一页时顺手重扫一遍；
判据一条都没动（命令怎么拼在 `server/ServerRun.hpp`、装到哪一步在
`download/ServerInstall.hpp`、EULA 在 `server/ServerCheck.hpp`）。

行数：`src/MainWindow.cpp` 7673 → 6930，新文件 805。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。


## 2026-11-03 `src/MainWindow.cpp`：任务中心那一片分出去

`MainWindow.cpp` 里 1524–1849 这 326 行是"账本 → 界面"的唯一一道门：底栏中间那格的摘要
（`updateBottomBarTaskSummary()`）、任务中心面板那几行（`taskCenterItems()`）、
"取消 / 重试是不是真会做点什么"（`cancelWouldWork()` / `retryWouldWork()`）、详情页上
那颗按钮的分派，以及 `beginTask()` / `finishTask()` / `setUpdateFlowCancel()` 这几条收尾。

搬到 `src/MainWindowTasks.cpp`：

- 判据一条都不在这一层：收起态显示谁、优先级、"刚完成那一项还露多久"在
  `Tasks::collapsedSummary()`。"按取消真的会做点什么吗"必须和 `onTaskDetailAction()` 里
  那条分派**逐字对齐** —— 两条因此留在同一个文件里、隔几行，改一条时另一条就在上面。
- `m_syncWaitStatus` 那层"压倒"仍然只在 `updateBottomBarTaskSummary()` 里加：整条底栏上
  唯一一处"同步等待盖在账本上面"。
- 类接口没改；两条源列表（主 `CMakeLists.txt`、`tests/mw_check`）登记新文件。

行数：`src/MainWindow.cpp` 6930 → 6607，新文件 372。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。


## 2026-11-03 `src/MainWindow.cpp`：「脸上那张图」分出去

`MainWindow.cpp` 里 1528–1723 这 196 行只认一张图：`PlayerSkinController` 手上那套外观
（贴图 / 披风 / 细手臂），和它落到窗口上那几处 —— 底栏头像、主页那个小人、账号与外观
那个窗里的提示，以及 `accountFacePixmap()` 那条只读本地缓存的头像。

搬到 `src/MainWindowSkin.cpp`：

- 四条薄壳 `onSkinProfileReady()` / `onSkinTextureReady()` / `onRandomSkinReady()` /
  `applyPlayerSkin()` 的名字**一个字没动**：mw_check 的 X 节与 AF5/AF6/AF7 是按名字
  `QMetaObject::invokeMethod` 打的（全程不联网验完整条外观路径的唯一办法）。换翻译单元
  不影响按名字调用，改名会。
- `startPlayerSkinLoad()`（全仓"把外观拉一遍"的唯一出口，六处调用点）与
  `onPlayerSkinChanged()`（"现在显示的是哪一套"的唯一出口）整条跟着走。
- 类接口没改；两条源列表（主 `CMakeLists.txt`、`tests/mw_check`）登记新文件。

行数：`src/MainWindow.cpp` 6607 → 6414，新文件 241。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。


## 2026-11-03 `src/MainWindow.cpp`：实例树 / 已安装版本 / «启动» 那一行分出去

`MainWindow.cpp` 里 1606–2200 这 595 行，主语是"界面上列出来的那些实例和版本"：

- `installingVersionIds()`：**谁现在不能启动**的唯一出处。三个来源（`m_fileOperationInstanceIds`
  这份后台搬文件的名单、`DownloadBatchController::isRestoring()` 期间全部实例、`VersionManager`
  的 `busyVersionId()`）在这里合流；界面别处只问它，不许自己再拼一遍。
- `refreshInstalledVersions()` / `rebuildInstanceTree()`：已安装版本下拉框与分组树。
- `updateInstanceTreeRunState()` / `updateStartButtonText()`：运行态，"启动"那颗按钮上那句字。
- `restoreLastLaunchedVersion()` / `refreshAvailableVersions()` / `openInstallPage()` /
  `backToVersionTree()`：上一次启动记的是谁、在线版本清单、以及从版本清单点进安装页。

搬到 `src/MainWindowInstanceTree.cpp`，为什么单独一个翻译单元：这一片"启动按钮为什么灰着"
是用户最常问的，判据（`installingVersionIds()` + 那几个 busy 计数）和投影（树、按钮文字）
在同一处，不必在整扇窗里翻。判据规则本身不在这一层 —— 分组与排序在
`manage/InstanceFilter`，显示名在 `manage/VersionDisplay` 的 `displayParts()`。

- 类接口没改；两条源列表（主 `CMakeLists.txt`、`tests/mw_check`）登记新文件。
- `eventFilter()`（下一段）**没有**跟着走：它管的是整扇窗的滚轮 / 悬停，不是这一片。

行数：`src/MainWindow.cpp` 6414 → 5822，新文件 641。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。


## 2026-11-03 `src/MainWindow.cpp`：「账号与外观」那一扇分出去

`MainWindow.cpp` 里 2593–3247 这 655 行，原来的分节横幅就叫「账号与外观」：从
`onAccountSkinButtonClicked()`（那一扇窗的开关）到 `onLoginFailed()`（登录失败必须弹窗）。
主语是"现在是谁、他长什么样"：

- `offlineIdentity()`：离线身份的唯一出处；`accountSkinStatusText()` / `refreshAccountSkin()`
  把这一扇窗的预览与提示铺一遍。
- 登录生命周期：`onLoginStarted()` / `onLoginSucceeded()` / `onLoginFailed()` —— 各自之后
  要刷新什么（底栏、按钮、外观、面板快照）在这里，皮肤贴图落到窗口上那几处在
  `MainWindowSkin.cpp`。
- 账号动作：切号 / 移除 / 登出、离线账号的皮肤（挑一张 / 恢复随机）、正版上传皮肤、
  选披风 / 隐藏披风。尺寸那条规矩仍然只在 `SkinApi::isAcceptableSkinSize()`。

一并搬走的还有两处，理由写在新文件头：

- `onBottomBarPanelAction()`：底栏两块上拉面板**唯一**的入口，一个函数同时分派账号行与
  「正在跑的游戏」行，拆不开；它夹在账号这一串动作中间，跑着的游戏那一支只是转给
  `stopGameSession()` / `openInstanceSettings()` / `GameSessionController`。
- `onGameLogForSession()`：日志窗那个兼容入口（底栏与验证工程按名字调用）。

匿名命名空间里的 `kMaxPickedSkinBytes` / `kPickedSkinLimits` 只有这一片里的两条皮肤路径
（正版上传、离线手动挑）用，跟着用户一起走。

- 类接口没改；两条源列表（主 `CMakeLists.txt`、`tests/mw_check`）登记新文件。

行数：`src/MainWindow.cpp` 5822 → 5170，新文件 704。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。


## 2026-11-03 `src/MainWindow.cpp`：实例分组那一片分出去

`MainWindow.cpp` 里 2893–3303 这 411 行，主语只有一个：`InstanceGroupStore` 里那几张表
（组、成员、展开状态）。从 `onGroupInstanceClicked()`（开「分组设置」）到
`openGroupMenuFor()`（组行行尾那颗齿轮的菜单），每一件事的落点都是那个 store：

- 开窗时从**当前那张树**上数组里有多少实例（不重新扫盘），确定时整份重写；
- `clearGroupMembers()`、「清空成员」；
- `setSelectedInstanceHomeGroup()` / `setInstanceHomeGroup()`：把实例挪进 / 挪出组；
- `renameGroupTo()` / `deleteGroupById()` / `rememberGroupExpanded()`：改名、删除、记住展开；
- `openGroupMenuFor()`：菜单第一项能不能点、挑目标那个框里哪一格灰着，都来自
  `groupRowFor()` 那份快照；锚点是 `InstanceGroupDelegate::gearRect()` 那个格子。

- 分组与实例树之间只经 `refreshInstalledVersions()` / `rebuildInstanceTree()` 说话，
  不直接摸树。
- 类接口没改；两条源列表（主 `CMakeLists.txt`、`tests/mw_check`）登记新文件。

行数：`src/MainWindow.cpp` 5170 → 4759，新文件 411（含文件头）。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。


## 2026-11-03 `src/MainWindow.cpp`：实例这一行能做的事分出去

`MainWindow.cpp` 里 2625–2886 这 262 行，主语是**一个实例本身**（不是它的文件、也不是它的
分组）：改名、固定 / 取消固定、删除，以及两条把 controller 的事实说成人话的投影。

搬到 `src/MainWindowInstanceActions.cpp`：

- `applyRename()`：验证工程按**名字**驱动这个槽（`QMetaObject::invokeMethod(this,
  "applyRename", …)`，mw_check 的 S 段），薄壳的名字不能改。
- `pinInstance()` / `unpinInstance()`、`confirmDeleteInstance()` / `deleteInstance()`：
  "别的账号还在跑 / 还在下载安装"的判据一律问 `installingVersionIds()` 与 `VersionManager`，
  这一层不另拼一份。
- `showInstanceSettingsNotice()` / `showServerListNotice()`："哪一档 → 哪句标题、哪句正文"
  的两张表。验证工程按原文对数，改一个字就是回归 —— 分出来之后这类改动只在一个文件里审。

- 类接口没改；两条源列表（主 `CMakeLists.txt`、`tests/mw_check`）登记新文件。

行数：`src/MainWindow.cpp` 4762 → 4500，新文件 262（另加文件头）。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。


## 2026-11-03 `src/MainWindow.cpp`：实例文件与实例设置的读写分出去

`MainWindow.cpp` 里 3358–3639 这 282 行，主语是"某个实例目录里的东西"：

- `updateInstanceFiles()`：页键 → 哪一份页面数据（这一片最大的一段）；
- `toggleInstanceFiles()` / `deleteInstanceFiles()`、`deleteInstanceFile()` /
  `removeInstanceFileUnasked()`：批量开关 / 删除（走 controller，回收站在那边）；
- `copyInstanceTo()` / `copyInstanceToInBackground()`、`importWorldInto()` /
  `renameInstanceWorld()`：拷贝与导入；
- `requestInstanceFileIcons()` / `pushInstanceFileUpdates()`：图标与"有更新"那两条投影；
- `applyInstanceSettings()`：设置落盘，以及对话框现读成一份 `Edit` 的那一半。

搬到 `src/MainWindowInstanceFiles.cpp`。判据不在这一层：页键与页表的形状在
`InstanceSettingsDialog` / `InstanceFilesPage`，盘上的读写、事务登记与回收站在各自的
controller。`applyInstanceSettings()` 后面紧跟着的匿名命名空间（导出那两份文件的默认目录、
写 meta 那个助手）**没有**跟着走 —— 它属于下一节「日志窗那两颗导出」。

- 类接口没改；两条源列表（主 `CMakeLists.txt`、`tests/mw_check`）登记新文件。

行数：`src/MainWindow.cpp` 4503 → 4221，新文件 282（另加文件头）。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。


## 2026-11-03 `src/MainWindow.cpp`：日志 / 崩溃 / 导出那一片分出去

`MainWindow.cpp` 里 3362–3678 这 317 行，主语是"刚刚那一次到底发生了什么"：

- `reportGameCrash()`：异常退出之后把这一趟的启动事实和日志摆成 `CrashReportController`
  要的那一份（会话一结束，`LauncherManager` 那份记录和日志窗里的两段日志就都没了，
  所以必须在活的时侯留）；
- `exportCrashReport()` / `openCrashReportsDir()` / `exportGameLog()` /
  `exportGameErrorReport()`：日志窗那两颗导出 + 错误报告；
- `exportLaunchScript()` / `exportModpack()`：启动脚本与整合包导出。

搬到 `src/MainWindowSessionExport.cpp`。匿名命名空间（导出那两份文件的默认目录、写 meta
那个助手）跟着用户一起走 —— 上一节「实例文件」的分割线正是划在它前面的。保存框留在这一层
的理由不变：`ui/` 不认识数据目录。

- 类接口没改；两条源列表（主 `CMakeLists.txt`、`tests/mw_check`）登记新文件。

行数：`src/MainWindow.cpp` 4224 → 3907，新文件 317（另加文件头）。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。


## 2026-11-03 `src/MainWindow.cpp`：「实例设置」背后的页面数据与动作分出去

`MainWindow.cpp` 里 3139–3278 这 140 行（含「世代备份」那一小节的分节注释与匿名命名空间），
主语是**对话框要什么、按下之后交给谁**：

- `instanceIdOfPath()`：从盘上路径反查是哪一页、哪个实例；
- 配置文件：打开 / 保存 / 恢复 + `kConfigBackupGenerations`（每个实例留最近 20 份；
  注释里写清了 20 是拍的、不是量出来的）；
- 世界设置：进 / 出 / 落盘、`backupWorldLevelData()`；
- `instanceHasMod()` / `instanceShaderUsable()` / `instanceModsUsable()`：三档可用性判据；
- 服务器列表：读、`serverListProblem()`、`pingServers()`；
- `toggleInstanceFile()` / `toggleInstanceFileForTest()`：文件页那两个开关（后者是验证工程
  按名字用的）。

搬到 `src/MainWindowInstanceSettingsWiring.cpp`。盘上的读写归 `InstanceEditorController`
与 `ServerPingController`，这一层只做"哪一个键 → 哪一件事"的分派。

- 类接口没改；两条源列表（主 `CMakeLists.txt`、`tests/mw_check`）登记新文件。

行数：`src/MainWindow.cpp` 3910 → 3770，新文件 140（另加文件头）。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。


## 2026-11-03 `src/MainWindow.cpp`：下载页的来路分出去

`MainWindow.cpp` 里 2771–3035 这 265 行，主语只有一件：`m_downloadReturn`（"从哪个实例、
哪一页点进来的"）。

- `openDownloadCategory()`：登记来路、按那一栏翻页；
- `continueOpenDownloadCategory()`：排到下一轮再开（带过去的是那几个字符串，不是上下文）；
- `fillDownloadFiltersFromInstance()`：把实例那一栏带过去（版本 / 加载器 / 分类）；
- `returnToInstanceSettingsFromDownload()`：取走来路并开回原页；
- `onNavigationApplied()`：顶部那两颗里的两下 —— 按「实例」= 返回、重点「下载」= 回主页。

搬到 `src/MainWindowDownloadOrigin.cpp`。第二颗判据为什么量 `reselected` 而不是
`currentIndex()` 的注释原样跟着搬 —— 那是 mw_check T25.8 唯一能量出来的那条。

- 类接口没改；两条源列表（主 `CMakeLists.txt`、`tests/mw_check`）登记新文件。

行数：`src/MainWindow.cpp` 3770 → 3505，新文件 265（另加文件头）。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。


## 2026-11-03 `src/MainWindow.cpp`：启动那条路与主页「最近玩的」分出去

`MainWindow.cpp` 里 2367–2591 这 225 行，主语是"这一次要不要启动 / 启动之后窗口怎么收"：

- `onVersionsFetched()`：清单到齐之后摆进界面；
- `launchIdentityKey()` / `confirmLaunchWhileRunning()` / `launchArguments()`：按下那颗按钮
  之前的判据与参数；
- `onStartButtonClicked()`：唯一入口（真正起进程在 `GameSessionController`）；
- `appendLaunchRecord()` / `updateHomeRecent()` / `openHomeInstancePicker()`：启动之后主页
  「最近玩的」那一条与它的选择框；
- `applyLauncherBehaviourAfterLaunch()`：窗口行为（最小化 / 退出）。

搬到 `src/MainWindowLaunch.cpp`。两处顺手收拾：

- 主 `CMakeLists.txt` / `tests/mw_check` 里 `MainWindowDownloadOrigin.cpp` 被登记了两遍
  （上一节插串了），这次一并去掉 —— CMake 会去重，所以前一次构建没炸，但清单不该长这样。

- 类接口没改；两条源列表（主 `CMakeLists.txt`、`tests/mw_check`）登记新文件。

行数：`src/MainWindow.cpp` 3511 → 3289，新文件 264（另加文件头）。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。


## 2026-11-03 `src/MainWindow.cpp`：左侧「游戏文件夹」那一列分出去

`MainWindow.cpp` 里 3063–3173 这 111 行，主语是"现在用的是哪个游戏文件夹"：

- `onFolderSelectionChanged()`：选中的那一格落到 `FolderManager`，整扇窗跟着换
  （路径、实例树、按钮、下拉框）；
- `refreshFolderList()` / `updateFolderButtons()` / `folderTitle()`：那一列画成什么样；
- `folderDisplayNameOf()`（文件内 `static`）：显示名一条规则说完（含"目录名即身份"那个坑），
  只有这一片用，跟着搬；
- `applyCurrentFolderToPaths()`：当前文件夹摆进几个"默认目录"；
- `syncDownloadReuseSources()`：下载页能复用哪几个来源。

搬到 `src/MainWindowFolderColumn.cpp`。

- 类接口没改；两条源列表（主 `CMakeLists.txt`、`tests/mw_check`）登记新文件。

行数：`src/MainWindow.cpp` 3289 → 3175，新文件 111（另加文件头）。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。


## 2026-11-03 `src/MainWindow.cpp`：启动前的 Java 与内存决定分出去

`MainWindow.cpp` 里 2946–3061 这 116 行，主语是"这一次启动的 Java 与内存"：

- `askAboutJavaMismatch()`：实例要求的版本与手头那颗 Java 对不上时问一句（用哪颗 / 取消）；
- `resolveMemoryForLaunch()`：xms/xmx 从哪儿来（手动档照抄，自动档现算一份给这次用）；
- `instanceModCount()`：只给自动档数模组用（.hpp 那条 NOTE：只在这一支数）。

搬到 `src/MainWindowLaunchJava.cpp`。两条纪律原样跟着搬：自动档**不写全局、也不写回
meta** —— 写了下次读出来就是"自定义 + 一个用户没填过的数"。

顺手收拾：两条源列表里 `MainWindowFolderColumn.cpp` 又出现了两遍（上一节插串了），
这次去重 —— CMake 会去重所以构建没炸，但清单不该长这样。

- 类接口没改；两条源列表（主 `CMakeLists.txt`、`tests/mw_check`）登记新文件。

行数：`src/MainWindow.cpp` 3181 → 3068，新文件 116（另加文件头）。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。


## 2026-11-03 `src/MainWindow.cpp`：补齐与修复（修复文件 / 预取资源）分出去

`MainWindow.cpp` 里 2745–2847 这 103 行：`repairInstanceFiles()` 与
`maybePrefetchAssetDirector()`。主语是"还差什么、现在能不能补"—— 两件事都要先过那几道
守卫（另有实例在下载 / 正在启动 / 已经占着同一批），补完还要把忙态与任务收回来，收尾的
两套分支写反一处就是"任务一直转圈"。放在一个文件里，守卫与收尾可以上下对着看。

搬到 `src/MainWindowRepair.cpp`。占用状态只有 `InstallOccupancy` 一家；盘上怎么修在
`VersionManager`，资源预取在 `IntegrityController`。

- 类接口没改；两条源列表（主 `CMakeLists.txt`、`tests/mw_check`）登记新文件。

行数：`src/MainWindow.cpp` 3068 → 2968，新文件 103（另加文件头）。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。


## 2026-11-03 `src/MainWindow.cpp`：实例体检与备份 / 恢复分出去

`MainWindow.cpp` 里 2749–2844 这 96 行（含「完整性检查与修复」两条分节线）：

- `integrityScanningVersion()` / `submitIntegrityCheck()` / `onIntegrityScanProgress()` /
  `onIntegrityScanFinished()`：体检那四发；
- `integrityReportForPage()` / `openIntegrityReportPage()` / `onIntegrityReportAction()` /
  `copyIntegrityReport()`：报告页；
- `openBackupFolder()` / `backupWorldNow()` / `restoreWorldBackup()` / `snapshotInstance()` /
  `restoreInstanceSnapshot()`：世界备份与实例快照；
- `startBackupRestore()`：**按名字**的兼容入口 —— mw_check T35.3 / T35.4 用
  `QMetaObject::invokeMethod` 驱动它，`invokeMethod` 找不到就返回 false、断言会静默失效。
  薄壳的名字一个字没改。

搬到 `src/MainWindowIntegrityBackup.cpp`。

- 类接口没改；两条源列表（主 `CMakeLists.txt`、`tests/mw_check`）登记新文件。

行数：`src/MainWindow.cpp` 2968 → 2875，新文件 96（另加文件头）。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。


## 2026-11-03 `src/MainWindow.cpp`：从实例文件页点开一个资源分出去

`MainWindow.cpp` 里 2553–2653 这 101 行：`openFileResourcePage()`（按来路摆详情页 ——
只有 `InstanceFiles` 这一条路才可能露「模组设置」）与 `openInstanceFileResource()`
（先 `dismiss()` 收掉实例设置那一页，再把三个**字符串**排到下一轮去开新页）。

搬到 `src/MainWindowFileResource.cpp`。那句"下一轮里这一页早就没了，不能等到那时再查
这一条是哪个资源"的注释原样跟着搬 —— 这是这段顺序不能动的理由。

- 类接口没改；两条源列表（主 `CMakeLists.txt`、`tests/mw_check`）登记新文件。

行数：`src/MainWindow.cpp` 2875 → 2774，新文件 101（另加文件头）。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。


## 2026-11-03 `src/MainWindow.cpp`：进实例设置 / 快速游玩分出去

`MainWindow.cpp` 里 2463–2547 这 85 行，主语是"用户要从哪一页进那个实例"：

- `openInstanceSettings()`：全窗唯一的入口（空 id 先判 ⇒ 普通提示；被占着就静默让位，
  排到下一轮再开）；
- `openInstanceSettingsNow()`：让位之后的那一发；
- `instanceSupportsQuickPlay()` / `startQuickPlay()` / `startQuickPlayNow()`：服务器列表与
  主页直达的那条路（真的起进程在 `LaunchController`）；
- `instanceFileDirs()` / `instanceCommonFolderKeys()`：那扇窗要的目录表。

搬到 `src/MainWindowInstanceEntry.cpp`。

- 类接口没改；两条源列表（主 `CMakeLists.txt`、`tests/mw_check`）登记新文件。

行数：`src/MainWindow.cpp` 2777 → 2692，新文件 85（另加文件头）。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。


## 2026-11-03 `src/MainWindow.cpp`：覆盖层「能不能开新页」那一片分出去

`MainWindow.cpp` 里 2479–2555 这 77 行，主语是覆盖层的**占用**：

- `overlayAcceptsNewPage()`：只回答"空着没有"，一个字节都不往外弹；
- `runWhenOverlayFree()`：让位的唯一出处（静默收掉占着的那一层，把"开"排到下一轮）；
- `openPageWhenIdle()`：排到下一轮；
- `warnOverlayBusy()`：「顶上那四颗导航一次只放得下一个页面」那句话。

全窗所有开页的调用点都从这里过 —— 分出来之后，"页到底什么时候才开"只在一个文件里翻。
页面本身由 `ui/OverlayHost` 管（`showPage()` 不等旧页收完就返回，所以才有"排一轮"）。

- 类接口没改；两条源列表（主 `CMakeLists.txt`、`tests/mw_check`）登记新文件。

行数：`src/MainWindow.cpp` 2695 → 2618，新文件 77（另加文件头）。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。


## 2026-11-03 `src/MainWindow.cpp`：游戏会话那几发事件分出去

`MainWindow.cpp` 里 2507–2545 这 39 行，是主窗口与 `GameSessionController` 之间那条**单线**：
`onProcessOutput()` / `onProcessError()` / `onSessionDiscarded()` / `showGameLogLine()` /
`ensureGameLogWindow()` / `onGameFinished()`、`launchingInstanceName()` /
`launchingStatusName()`、`confirmStopGame()` / `stopGameSession()`。每一发都是薄壳，
放在一个文件里，"主窗口到底从会话那里接了几发、各自转给谁"一眼看得完。

搬到 `src/MainWindowSessionEvents.cpp`。

- 类接口没改；两条源列表（主 `CMakeLists.txt`、`tests/mw_check`）登记新文件。

行数：`src/MainWindow.cpp` 2621 → 2582，新文件 39（另加文件头）。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。


## 2026-11-03 `src/MainWindow.cpp`：导入整合包与拖放那一段分出去

`MainWindow.cpp` 里 2511–2562 这 52 行，主语是"用户扔进来一个文件"：

- `dragEnterEvent()` / `dropEvent()` / `handleWindowDrop()`：窗口那一层；
- `handleInstanceSettingsDrop()`：实例设置页那一层（同一个文件落在不同页上意思不同）；
- `onImportModpackClicked()` / `importModpackFromFile()` / `applyModpackImport()` /
  `importDroppedModpack()`：整合包那一路；
- `installLocalResourceFile()`：单个资源文件。

搬到 `src/MainWindowDropImport.cpp`。判据不在这一层：`.jar` 只看后缀、`.zip` 才读条目表
（`ModpackImport` 那一层的规矩），落盘在 `ImportController`。

- 类接口没改；两条源列表（主 `CMakeLists.txt`、`tests/mw_check`）登记新文件。

行数：`src/MainWindow.cpp` 2585 → 2533，新文件 52（另加文件头）。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。


## 2026-11-03 `src/MainWindow.cpp`：实例更新检测与更新中心分出去

`MainWindow.cpp` 里 2082–2283 这 202 行（分节横幅「实例更新检测」起），主语是"这个实例
有哪些资源有新版本、要不要换"。它是一条**顺序不能换**的链：

- 检测：`checkInstanceUpdates()` + 那几发回调（图标反查 / 版本清单 / 最新版）；
- 可更新名单：`rebuildInstanceOutdatedPaths()` / `saveUpdatePolicy()`；
- 更新中心那一页：`openUpdateCenter()` / `updateCenterReportForPage()` / `setInstanceMode()`；
- 换文件与收尾：`onUpdateBatchFileFinished()` / `finishUpdateBatch()`。

搬到 `src/MainWindowUpdateCenter.cpp`。两条说死的规矩跟着搬：检测这一趟不自动下载、不自动
改盘；收尾之后再开一次更新中心走 `openPageWhenIdle()`（后台收尾跑了好几分钟，用户早翻走
了，不驱逐他正看着的那一页）。

- 类接口没改；两条源列表（主 `CMakeLists.txt`、`tests/mw_check`）登记新文件。

行数：`src/MainWindow.cpp` 2536 → 2362，新文件 202（另加文件头）。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。

## 2026-11-03 `src/MainWindow.cpp`：底栏那一行 / 同步等待 / 下载进度投影分出去

`MainWindow.cpp` 里 1252–1593 这 342 行，主语是"底栏这会儿该露什么"：

- 同步等待：`enterSyncWait()` / `leaveSyncWait()` —— 点启动那一趟里 UI 线程在等网络或
  等进程时，自己转一圈事件、让窗口照常重绘，别当场挂成「(无响应)」；
- 全局忙态：`setDownloadUiBusy()`，挡住会打架的操作；
- 那一行与左下角那颗方块：`updateBottomBar()` / `updateBottomBarRunState()`，
  外加句柄名 `instanceDisplayName()`；
- 三根进度条：`updateDownloadProgressUi()` / `onVersionDownloadStats()`。

搬到 `src/MainWindowBottomBar.cpp`。这一片搬过去的三条规矩都在别的层做着，这里只照抄：

- 方块三态与「正在启动优先」在 `MainWindowUI::RunStateMark`（含取色
  `runStateMarkColor()` / `runStateMarkPixmap()`）；
- 任务那一段的摘要只有 `Tasks::collapsedSummary()` 一个出处；
- 会话事实来自 `LauncherManager` / `GameSessionController`，
  下载事实来自 `DownloadTaskLedgerController` / `DownloadCompletionController`，
  这一层只把它们摆成底栏的样子。

- 类接口没改；两条源列表（主 `CMakeLists.txt`、`tests/mw_check`）登记新文件。

行数：`src/MainWindow.cpp` 2337 → 1998，新文件 342（另加文件头）。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。

## 2026-11-03 `src/MainWindow.cpp`：行内小按钮的事件路由分出去

`MainWindow.cpp` 里 1271–1490 这 220 行，就是 `eventFilter()` 那一支。它管的全是**画在
行里的假按钮**——不是 QWidget，是代理画出来的一格，靠视口上的过滤器认"点中了没"：

- 加载器那行右边的「取消」叉（`VersionTreeDelegate::clearRect()`）；
- 组行行尾的齿轮（`InstanceGroupDelegate::gearRect()`）；
- 实例行行尾三格：齿轮 / 置顶（↑ 与 ↓ 共用一格）/ 垃圾桶；
- 下载页左栏分类行尾的刷新圈（`VersionTreeDelegate::refreshRect()`）；
- 资源表第一层行身（打开详情页），箭头那一格交回展开 / 收起。

搬到 `src/MainWindowEventFilter.cpp`。两条反复出现在注释里的规矩跟着一起搬，之后加一格
按钮时不用在别处翻：

- **按下就动手**，松开与双击只负责被吃掉；
- **双击也要接**——点得快时第二发及以后是 `MouseButtonDblClick`，不接就漏给"点中了这一
  行"那条路（连点垃圾桶的第二下会变成进实例设置）。

判据仍不在这一层：哪一格在 `VersionTreeDelegate` / `InstanceGroupDelegate`（过滤器读的是
画它们的同一个函数）；点了算哪件事仍是 `MainWindow` 的槽。

- 类接口没改；两条源列表（主 `CMakeLists.txt`、`tests/mw_check`）登记新文件。

行数：`src/MainWindow.cpp` 1998 → 1781，新文件 220（另加文件头）。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。

## 2026-11-03 `src/MainWindow.cpp`：窗口生命周期（析构 / 外框消息 / 关窗）分出去

`MainWindow.cpp` 里 1045–1246 这 202 行，主语是"这个窗口要没了（或者正要没）的时候，
还有什么不能半路丢下"：

- `~MainWindow()`：四趟后台活（体检 / 备份 / 图标反查 / 检查更新）一条一条等过来。
  少等一条就是 Qt 的 `QThread: Destroyed while thread is still running` 直接 abort ——
  注释里那张单子（"这一层起的，析构里必须一个一个等过来"）跟着一起搬；
- `nativeEvent()`：外框那两条消息转给 `ui/WindowChrome`；
- `closeEvent()`：还有游戏 / 服务端在跑、或者启动正走到一半，要不要问一句。

搬到 `src/MainWindowLifecycle.cpp`。怎么等、等多久仍收在各控制器自己的 `shutdown()` /
`waitForLookup()` 里；这两条原生消息怎么回仍在 `ui/WindowChrome`。

- 类接口没改；两条源列表（主 `CMakeLists.txt`、`tests/mw_check`）登记新文件。

行数：`src/MainWindow.cpp` 1781 → 1582，新文件 202（另加文件头）。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。

## 2026-11-03 `src/MainWindow.cpp`：一键装前置的判据分出去

`MainWindow.cpp` 里 1236–1325 这 90 行，主语是"点「装上前置」那一下，目标到底是什么"：

- `dependencyInstanceId()`：详情页开着的实例优先，否则下载页顶上那个下拉框。
  判据带 `open`，是为了不让关页之后残留的旧值顶替下拉框；
- `downloadCameFromInstance()`：这一条是不是从某个实例追过来的（装完回哪儿）；
- `dependencyTargetFor()`：游戏版本 / 加载器从**那个实例自己**的 meta 读、不读页面上的
  chip；加载器名归一成小写那一套（`loaderGroupKey()`），递显示名的症状是接口不报错、
  只是筛不出东西；
- `installedModKeys()`："认过没有"（`recognizedInstanceIs()`）与"认出几个"是两件事。

搬到 `src/MainWindowResourceDependency.cpp`。判据仍在外层：版本号怎么读是
`VersionManager::instanceGameVersion()`；"这一条是哪个资源"的表住在
`InstanceIconController`；CurseForge 的根地址只有 `ModpackImport::curseForgeApiBase()` 一处。

- 类接口没改；两条源列表（主 `CMakeLists.txt`、`tests/mw_check`）登记新文件。

行数：`src/MainWindow.cpp` 1582 → 1495，新文件 90（另加文件头）。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。

## 2026-11-03 `ui/InstanceFilesPage.cpp`：搜索与状态筛选分出去

`InstanceFilesPage.cpp` 里"这一页现在筛着什么、哪几条算中"自成一片，搬到
`ui/InstanceFilesPageFilter.cpp`：

- 两个条件：`m_searchText` / `m_stateFilter`，以及那份"哪些条有新版本"的名单
  （`m_outdatedPaths` + `m_updateChecked`）——`setSearchText()` / `setStateFilter()` /
  `setUpdateResult()` / `setCanFilterUpdates()`；
- 两套控件（下拉框 / 四颗带条数的按钮）的填与摆：`filterAt()`、`fillStateCombo()`、
  `fillStateButtons()`、`stateFilterCount()`、`stateFilterVisible()`、
  `ensureStateFilterVisible()`；
- 判据 `entryMatchesFilter()` —— 它读的正是上面那三位，跟着一起走。

三条规矩跟着搬过去（文件头里写全了）：判据只有 `stateFilterVisible()` 一处、条件不许停在
一档摆不出来的东西上；填控件时挡信号；条件一变就 `rebuildRows()`，例外只有
`setUpdateResult()` 那句"名单本身没变就不重排"。

- 类接口没改；三份源列表（主 `CMakeLists.txt`、`tests/mw_check`、`tests/ui_check`）登记新文件。

行数：`ui/InstanceFilesPage.cpp` 1691 → 1415，新文件 280（另加文件头）。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。

## 2026-11-03 `ui/InstanceFilesPage.cpp`：重列前后的滚动锚点分出去

`InstanceFilesPage.cpp` 里 893–971 这 79 行，主语是"重列之后回到原来那个位置"：

- 状态只有 `m_scrollAnchor` 一位：最上面那一行的**身份**（`identityPath()`）加它离视口
  顶端的像素；
- 记：`rememberScrollAnchor()` / `captureScrollPosition()`；
- 带走再带回来：`scrollPosition()`（现算，读的不是锚点） / `setScrollPosition()`（只写，
  不顺手摆 —— 那一刻表还是空的）；
- 摆：`restoreScrollAnchor()`，摆完就丢（一次性的）。

搬到 `ui/InstanceFilesPageScroll.cpp`。两条说死的规矩跟着搬：**空表不动手**（`refresh()`
与 `rebuildRows()` 各叫一次，第二次看见的是空表，记下去就抹了第一份真锚点）；
**锚点一次性的**（留着的话后面任何一次重排都可能再还一遍）。

- 类接口没改；三份源列表（主 `CMakeLists.txt`、`tests/mw_check`、`tests/ui_check`）登记新文件。

行数：`ui/InstanceFilesPage.cpp` 1415 → 1339，新文件 79（另加文件头）。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。

## 2026-11-03 `ui/InstanceFilesPage.cpp`：列表下面那条「这一份是什么」分出去

`InstanceFilesPage.cpp` 里 1025–1105 这 81 行，主语是"列表下面那一格正在讲哪一条"。
它是**异步**的：点中一条就把路径记进 `m_infoPath`、起定时器、先摆「正在读取…」；
说明由外面算好再递回来。所以这一片的一半状态是"过期答案"：

- `setInfoPanelEnabled()`：这一页有没有这一格。关掉时**一个字都不留**（不是只藏起来，
  再打开时从「正在读取…」重新来）；
- `updateInfoPanel()`：判据是「**正好选了一条、而且它是文件**」——选两个、选一个目录、
  一条没选，都不谈"这一份"；已经在为这一条等答案就不重复读盘；
- `setInfoText()`：答案回来先比路径，不是屏幕上选着的那条就丢掉。

搬到 `ui/InstanceFilesPageInfoPanel.cpp`。这一层仍只记账、只摆字：哪些文件有说明、
说明怎么算都是 src/ 那一层的事。

- 类接口没改；三份源列表（主 `CMakeLists.txt`、`tests/mw_check`、`tests/ui_check`）登记新文件。

行数：`ui/InstanceFilesPage.cpp` 1339 → 1261，新文件 81（另加文件头）。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。

## 2026-11-03 `ui/InstanceFilesPage.cpp`：「结构 / 原理图」那两排来源分出去

`InstanceFilesPage.cpp` 里 736–826 这 91 行，主语是那一页顶上那两排下拉框（每个有结构的
存档一排、投影一排）。和别的七页不一样：它一次列**好几个目录**，而排数是**每一趟现算**
的 —— 这一趟三个存档有结构就是三排加一排投影，下一趟可能只剩一排。

搬到 `ui/InstanceFilesPageStructures.cpp`：`setStructureSources()`（整块拆了重建）与
`selectedStructurePath()`（按"第几排"读回选中的目录）。几条规矩跟着走：

- 拆旧时**先藏再排删除**（`deleteLater()` 要等下一趟事件循环，不先藏的话屏幕上会看见
  上一趟那一排闪一下）；
- 排头宽度**统一成最宽的那一个**，量的是递进来的 title 而不是一个写死的词；
- 一个来源一行都没有 ⇒ 摆一行灰的「无可用的结构」，它不是一个能选中的值；
- 换一个来源 = 换一份要列的东西 ⇒ 当场重列；填 items 时先填、再 connect。

`scanDirectories()` 留在原文件（它读这一份来源，但说的是"这一页列哪几个目录"）。

- 类接口没改；三份源列表（主 `CMakeLists.txt`、`tests/mw_check`、`tests/ui_check`）登记新文件。

行数：`ui/InstanceFilesPage.cpp` 1261 → 1173，新文件 91（另加文件头）。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。

## 2026-11-03 `ui/InstanceFilesPage.cpp`：能力开关与按钮文字分出去

同一个 `InstanceFilesPage` 被八页用着，每页能做的事不一样。`InstanceFilesPage.cpp` 里
604–734 这 131 行就是那一整套"这一页能做什么"：

- 能力：`setMode()` / `setRowIconsVisible()` / `setCanToggle()` / `setCanDelete()` /
  `setCanEdit()` / `setBatchMode()`；
- 那几颗按钮的文字与显隐：`setEmptyText()` / `setImportText()` / `setJoinText()` /
  `updateJoinEnabled()` / `setBatchText()` / `setDownloadText()`；
- 列哪个目录：`setDirectory()`（只记账，看不见的页先欠着）。

搬到 `ui/InstanceFilesPageCapabilities.cpp`。规矩跟着走：能力只由外面拨，这一层不自己
猜；**按钮文字为空 = 这一颗不露**（这是"这一页没有这件事"的唯一说法）。

- 类接口没改；三份源列表（主 `CMakeLists.txt`、`tests/mw_check`、`tests/ui_check`）登记新文件。

行数：`ui/InstanceFilesPage.cpp` 1173 → 1045，新文件 131（另加文件头）。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。

## 2026-11-03 `ui/InstanceFilesPage.cpp`：底下那行目录统计分出去

"每个目录多大、世界上次什么时候玩的"这一片搬到 `ui/InstanceFilesPageDirectoryStats.cpp`：
匿名命名空间里的 `sumFilesUnder()` / `directoryBytes()` / `worldLastPlayed()`（86 行），
以及 `fillDirectoryStats()`（30 行）。

两条说死的规矩跟着一起搬，都是踩过的坑：

- **不跟链接走**：`isSymLink() || isJunction()` 两条一起判，下潜**之前**判 —— Windows 的
  junction 不是符号链接，只判前者照样进得去，进去之后每个文件都是一条独立结果，数会莫名
  其妙翻几倍；
- **世界看 `level.dat` 的时间**，不是目录自己的：目录时间只在增删条目时变，用户连着玩三
  个小时它可能一动不动。

`fillDirectoryStats()` 仍**一趟只算一行**、算完自己排下一趟（一次算完的话行已经摆在屏幕
上，用户看到的是一个点了没反应的界面）。这一层仍只 include Qt，不碰 AppPaths。

- 类接口没改；三份源列表（主 `CMakeLists.txt`、`tests/mw_check`、`tests/ui_check`）登记新文件。

行数：`ui/InstanceFilesPage.cpp` 1045 → 932，新文件 116（另加文件头）。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。

## 2026-11-03 `ui/InstanceFilesPage.cpp`：扫描（`refresh()`）分出去

`refresh()` 这 120 行是这一页 `m_entries` 的唯一产地 —— 建行、筛选、统计、"这一条是哪个
文件"那张账都从它读。搬到 `ui/InstanceFilesPageRefresh.cpp`：

- 路径那一格写"这一页正在列哪几个文件夹"（「结构 / 原理图」那一页不止一个）；
- 清表、清统计队列（**先清队列再清表**：只清表的话下一趟会拿着悬空路径白跑）；
- 三种模式（全部 / 只要目录 / 只要文件）、禁用后缀那一档；
- 列完交给 `rebuildRows()`。

几条踩过的坑跟着搬进文件头：

- `m_list->clear()` **必须挡信号** —— 它发的那一次 `itemSelectionChanged` 会把用户的
  选择抹平，`rebuildRows()` 想按路径还回去时手里那份账已经空了（真机踩到）；
- 条目**故意不给文字**（行里是透明的 `setItemWidget()` 控件，代理画的字会从底下露出来
  变成重影）；
- 一个来源不见了就跳过它接着列别的。

- 类接口没改；三份源列表（主 `CMakeLists.txt`、`tests/mw_check`、`tests/ui_check`）登记新文件。

行数：`ui/InstanceFilesPage.cpp` 932 → 815，新文件 120（另加文件头）。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。

## 2026-11-03 `ui/InstanceFilesPage.cpp`：这一页摆成哪一档分出去

`applyState()` 这 76 行是这一页所有显隐的唯一一处，主语是"四档里现在是哪一种"：
不可用 / 空 / 筛完一条不剩 / 有东西。搬到 `ui/InstanceFilesPageState.cpp`。

四档下同一件事的答案不同，所以改动它要一次看全：

- 搜索 / 状态那两栏在这一页用不上、或者目录本来就空时整块收起来，**筛完一条不剩时留着**
  （用户正要靠它们把条件改回来）；
- 下拉框与四颗按钮互相显隐（开了批量的页摆四颗，没开的摆下拉框）；
- 空/不可用那一段话要说清"是筛没的"还是"本来就空"；「可更新」那一档还要分清"还没查
  过"与"查过了没有"；
- 底下那条悬浮操作条跟着列表可见性走 —— 判据要的是**上面刚摆好的** `m_list`，所以必须
  排在它后面。

判据仍不在这一层：哪一档摆得出来（`stateFilterVisible()`）与哪些条算中
（`entryMatchesFilter()`）都在筛选那一份。

- 类接口没改；三份源列表（主 `CMakeLists.txt`、`tests/mw_check`、`tests/ui_check`）登记新文件。

行数：`ui/InstanceFilesPage.cpp` 815 → 742，新文件 76（另加文件头）。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。

## 2026-11-03 `ui/InstanceFilesPage.cpp`：行控件上的事件路由分出去

`eventFilter()` 那 63 行搬到 `ui/InstanceFilesPageEventFilter.cpp`。这一页的行不是普通
条目，而是 `setItemWidget()` 摆上去的真控件，底下那条悬浮操作条又**不在布局里**，于是有
一批位置与悬停状态没有别人帮着算：

- 页面 / 列表视口变大小 ⇒ 重摆那条浮条；
- 鼠标进出一行 ⇒ 露出 / 收起那一行的「详情」（Enter 往祖先那串发，进的是行里哪个孩子也算）；
- 行上右键 ⇒ 开这一条的详情页，**且保留上一页的选择**。

几条踩过的坑跟着搬进文件头：认行靠行控件上的动态属性（浮条没有这一格，自动被排除）；
右键那一发必须吃掉且 `accept()` 不能省（Qt 判的是 `res && isAccepted()`）；`path` 必须在
`emit` 之前取出来（接的人会收掉这一页）。

- 类接口没改；三份源列表（主 `CMakeLists.txt`、`tests/mw_check`、`tests/ui_check`）登记新文件。

行数：`ui/InstanceFilesPage.cpp` 742 → 682，新文件 63（另加文件头）。

验证：完整重建通过；`mw_check` 2756/2756（真屏）；全套 ctest 39 项全绿。
