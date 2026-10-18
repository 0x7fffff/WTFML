# MainWindow 回归验证：offscreen 起真的 MainWindow，验"启动游戏"按钮第二行
# 和"已安装版本"下拉框的同步。ui_check 只搭 MainWindowUI、拿不到同步逻辑，
# 而这条同步有一个真陷阱：refreshInstalledVersions() 里重灌下拉框时信号被
# QSignalBlocker 挡掉了，光接 currentTextChanged 会在"刷新"这条路径上漏掉。
# 只有把真的 MainWindow 立起来才验得到。

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
# 2026-09-28：assets/icons.qrc（四个加载器的真图）也要走 RCC。产品那份 CMakeLists
# 早就是开的，这三个验证工程原来只开了 AUTOMOC —— 不开的话 .qrc 会被**静默忽略**：
# 编译链接全都过，只是 `:/assets/loader/*.png` 全是空图（症状见下面那行的注释）。
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Core Gui Concurrent)

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
get_filename_component(REPO_ROOT ${REPO_ROOT} REALPATH)

set(HEADER_FILES
    ${REPO_ROOT}/src/MainWindow.hpp
    ${REPO_ROOT}/src/controllers/JavaRuntimeController.hpp
    ${REPO_ROOT}/src/controllers/JavaSettingsController.hpp
    ${REPO_ROOT}/src/controllers/PresetController.hpp
    ${REPO_ROOT}/src/controllers/LauncherPreferencesController.hpp
    ${REPO_ROOT}/src/controllers/CrashReportController.hpp
    ${REPO_ROOT}/src/controllers/IntegrityController.hpp
    ${REPO_ROOT}/src/controllers/BackupController.hpp
    ${REPO_ROOT}/src/controllers/ServerPingController.hpp
    ${REPO_ROOT}/src/controllers/FolderController.hpp
    ${REPO_ROOT}/src/controllers/ModpackExportController.hpp
    ${REPO_ROOT}/src/controllers/InstanceFileController.hpp
    ${REPO_ROOT}/src/controllers/InstanceTransferController.hpp
    ${REPO_ROOT}/src/controllers/InstanceDialogController.hpp
    ${REPO_ROOT}/src/controllers/InstanceEditorController.hpp
    ${REPO_ROOT}/src/controllers/ImportController.hpp
    ${REPO_ROOT}/src/controllers/GameSessionController.hpp
    ${REPO_ROOT}/src/controllers/LaunchController.hpp
    ${REPO_ROOT}/src/controllers/ConnectionsController.hpp
    ${REPO_ROOT}/src/controllers/InstanceIconController.hpp
    ${REPO_ROOT}/src/controllers/ActivityCenterController.hpp
    ${REPO_ROOT}/src/controllers/TaskDetailController.hpp
    ${REPO_ROOT}/src/controllers/HomeQuickController.hpp
    ${REPO_ROOT}/src/controllers/InstanceMaintenanceController.hpp
    ${REPO_ROOT}/src/controllers/ServerWorkspaceController.hpp
    ${REPO_ROOT}/src/controllers/ServerConsoleController.hpp
    ${REPO_ROOT}/src/controllers/ServerSettingsController.hpp
    ${REPO_ROOT}/src/controllers/ThemeSettingsController.hpp
    ${REPO_ROOT}/src/controllers/PlayerSkinController.hpp
    # 「上一次启动」那本账的驱动层（2026-10-15 从 MainWindow 搬出来的）：MainWindow.hpp 里
    # 那三个给验证工程摆现场的 inline 入口直接调它的静态函数和成员函数，少了这个头就编不过。
    ${REPO_ROOT}/src/controllers/LaunchHistoryController.hpp
    # 「实例设置」写侧那一套（2026-10-15 从 MainWindow 搬出来的）：MainWindow.hpp 里
    # showInstanceSettingsNotice() 的签名按 const 引用用它的嵌套类型 Notice。
    ${REPO_ROOT}/src/controllers/InstanceSettingsController.hpp
    # 「实例设置」那几页要的那几格事实（2026-10-15 从 MainWindow 搬出来的）：
    # MainWindow.cpp 里那个 `refreshInstanceDirs` 和 `structureSourcesFor()` 都按名字用它的
    # 嵌套类型 WorldStructures。
    ${REPO_ROOT}/src/controllers/InstancePagesController.hpp
    # 「服务器列表」那一页上会写盘的那三件事（同上）：MainWindow.hpp 里
    # showServerListNotice() 的签名按 const 引用用它的嵌套类型 Notice。
    ${REPO_ROOT}/src/controllers/ServerListController.hpp
    ${REPO_ROOT}/src/launcher/LauncherManager.hpp
    ${REPO_ROOT}/src/launcher/LibraryRules.hpp
    ${REPO_ROOT}/src/auth/AuthManager.hpp
    ${REPO_ROOT}/src/auth/AccountStore.hpp
    ${REPO_ROOT}/src/auth/SkinApi.hpp
    ${REPO_ROOT}/src/auth/TokenStore.hpp
    ${REPO_ROOT}/src/net/HttpClient.hpp
    ${REPO_ROOT}/src/manage/FolderManager.hpp
    ${REPO_ROOT}/src/manage/InstanceFiles.hpp
    ${REPO_ROOT}/src/manage/DroppedResource.hpp
    ${REPO_ROOT}/src/manage/ModpackImport.hpp
    ${REPO_ROOT}/src/manage/ModpackExport.hpp
    ${REPO_ROOT}/src/manage/VersionManager.hpp
    ${REPO_ROOT}/src/manage/VersionDisplay.hpp
    ${REPO_ROOT}/src/manage/VersionIntegrity.hpp
    # 更新中心那两层（2026-10-13，§7.1）：MainWindow.cpp 直接调它们，少了会在链接那一步报
    # 一串 `UpdatePolicy::…` / `UpdateRollback::…` 的 undefined reference。
    ${REPO_ROOT}/src/manage/UpdatePolicy.hpp
    ${REPO_ROOT}/src/manage/UpdateRollback.hpp
    ${REPO_ROOT}/src/manage/BackupFiles.hpp
    ${REPO_ROOT}/src/manage/WorldFiles.hpp
    # 世界只读概况（§9.6）：InstanceEditorController 翻译 WorldView 时直接调它。
    ${REPO_ROOT}/src/manage/WorldMap.hpp
    # 启动历史账本（§5.1）：主页“最近游玩”排的数据源，MainWindow.cpp 直接调它。
    ${REPO_ROOT}/src/manage/LaunchHistory.hpp
    ${REPO_ROOT}/src/settings/GameWindowSettings.hpp
    ${REPO_ROOT}/src/settings/GlobalJavaSettings.hpp
    ${REPO_ROOT}/src/settings/LauncherSettings.hpp
    ${REPO_ROOT}/src/download/DownloadManager.hpp
    ${REPO_ROOT}/src/util/AppPaths.hpp
    ${REPO_ROOT}/src/util/ZipUtil.hpp
)

# 和主工程同样的源文件，只是不含 src/main.cpp（那个有自己的 main）。
set(SOURCE_FILES
    ${REPO_ROOT}/src/MainWindow.cpp
    ${REPO_ROOT}/src/controllers/JavaRuntimeController.cpp
    ${REPO_ROOT}/src/controllers/JavaSettingsController.cpp
    # 「实例预设」那一页的控制器（2026-10-18）：MainWindow 构造里 new 它，少了这一个
    # 文件链接期报的是 `undefined reference to PresetController::…`。它调的
    # `src/presets/` 那一层在下面（`PresetStore` / 四个适配器 / 计划 / 提取 / 应用）。
    ${REPO_ROOT}/src/controllers/PresetController.cpp
    ${REPO_ROOT}/src/controllers/LauncherPreferencesController.cpp
    ${REPO_ROOT}/src/controllers/CrashReportController.cpp
    ${REPO_ROOT}/src/controllers/IntegrityController.cpp
    ${REPO_ROOT}/src/controllers/BackupController.cpp
    ${REPO_ROOT}/src/controllers/ServerPingController.cpp
    ${REPO_ROOT}/src/controllers/FolderController.cpp
    ${REPO_ROOT}/src/controllers/ModpackExportController.cpp
    ${REPO_ROOT}/src/controllers/InstanceFileController.cpp
    ${REPO_ROOT}/src/controllers/InstanceTransferController.cpp
    ${REPO_ROOT}/src/controllers/InstanceDialogController.cpp
    ${REPO_ROOT}/src/controllers/InstanceEditorController.cpp
    ${REPO_ROOT}/src/controllers/ImportController.cpp
    ${REPO_ROOT}/src/controllers/GameSessionController.cpp
    ${REPO_ROOT}/src/controllers/LaunchController.cpp
    ${REPO_ROOT}/src/controllers/ConnectionsController.cpp
    ${REPO_ROOT}/src/controllers/InstanceIconController.cpp
    ${REPO_ROOT}/src/controllers/ActivityCenterController.cpp
    ${REPO_ROOT}/src/controllers/TaskDetailController.cpp
    ${REPO_ROOT}/src/controllers/HomeQuickController.cpp
    ${REPO_ROOT}/src/controllers/InstanceMaintenanceController.cpp
    ${REPO_ROOT}/src/controllers/ServerWorkspaceController.cpp
    ${REPO_ROOT}/src/controllers/ServerConsoleController.cpp
    ${REPO_ROOT}/src/controllers/ServerSettingsController.cpp
    ${REPO_ROOT}/src/controllers/ThemeSettingsController.cpp
    ${REPO_ROOT}/src/controllers/PlayerSkinController.cpp
    # 「上一次启动」那本账的驱动层（同上）：少了这一个文件会在链接那一步报一串
    # `LaunchHistoryController::…` 的 undefined reference（它调的 ServerListFile / NbtUtil
    # 下面都已经在清单里了）。
    ${REPO_ROOT}/src/controllers/LaunchHistoryController.cpp
    # 「实例设置」写侧那一套（同上）。它调的 InstanceGroupStore / VersionManager /
    # InstanceFiles 下面都已经在清单里了。
    ${REPO_ROOT}/src/controllers/InstanceSettingsController.cpp
    # 「实例设置」那几页要的那几格事实（同上）。它只要 AppPaths / ResourceCatalog /
    # VersionManager —— 下面都已经在清单里了。
    ${REPO_ROOT}/src/controllers/InstancePagesController.cpp
    # 「服务器列表」那一页上会写盘的那三件事（同上）。它只要 ServerListFile ——
    # 那个已经在清单里了。
    ${REPO_ROOT}/src/controllers/ServerListController.cpp
    ${REPO_ROOT}/src/launcher/LauncherManager.cpp
    ${REPO_ROOT}/src/auth/AuthManager.cpp
    ${REPO_ROOT}/src/auth/AccountStore.cpp
    ${REPO_ROOT}/src/auth/SkinApi.cpp
    ${REPO_ROOT}/src/auth/TokenStore.cpp
    ${REPO_ROOT}/src/net/HttpClient.cpp
    ${REPO_ROOT}/src/manage/DefaultSkin.cpp
    ${REPO_ROOT}/src/manage/FolderManager.cpp
    ${REPO_ROOT}/src/manage/InstanceFiles.cpp
    ${REPO_ROOT}/src/manage/DroppedResource.cpp
    ${REPO_ROOT}/src/manage/ModpackImport.cpp
    # 导出整合包那一条路（VersionManager 里五个方法直接调它）：少了这一个文件，
    # mw_check 会在链接那一步报一串 `ModpackExport::…` 的 undefined reference。
    ${REPO_ROOT}/src/manage/ModpackExport.cpp
    # 实例预设那一层（2026-10-18）：PresetController 直接调它。判据本身的断言在
    # `verify` 的 PA–PG 那几节，这儿只是为了让 mw_check 链接得过。
    ${REPO_ROOT}/src/presets/PresetStore.cpp
    ${REPO_ROOT}/src/presets/VanillaOptions.cpp
    ${REPO_ROOT}/src/presets/ServerListPreset.cpp
    ${REPO_ROOT}/src/presets/GenericFiles.cpp
    ${REPO_ROOT}/src/presets/MalilibKeys.cpp
    ${REPO_ROOT}/src/presets/PresetPlanner.cpp
    ${REPO_ROOT}/src/presets/PresetCapture.cpp
    ${REPO_ROOT}/src/presets/PresetApply.cpp
    # 结构 / 原理图的元数据与方块统计（2026-10-18）：InstanceDialogController 在
    # 「原理图」那一页接上了它，少了这一个文件链接期报 `SchematicInfo::read` 未定义。
    ${REPO_ROOT}/src/schematic/SchematicInfo.cpp
    # Replay 那一页的信息格（2026-10-18）：InstanceDialogController 接上了它。
    ${REPO_ROOT}/src/replay/ReplayInfo.cpp
    # 「启动诊断」那一页（2026-10-18）：同一个控制器接上了它。
    ${REPO_ROOT}/src/diagnose/StartupDiagnosis.cpp
    # 「迁移」/「升级」那两页（2026-10-18，§9.9 第 15 项）：同一个控制器接上了计划层与
    # 落盘层；少了这两个文件链接期报 `TransferPlanner::plan` / `TransferApply::apply`
    # 未定义。
    ${REPO_ROOT}/src/operations/TransferPlanner.cpp
    ${REPO_ROOT}/src/operations/TransferApply.cpp

    ${REPO_ROOT}/src/manage/InstanceGroupStore.cpp
    ${REPO_ROOT}/src/manage/PlayerSkinStore.cpp
    # 离线账号那张随机皮肤：AF 段要拿它预置"已经挑好的一张"，还要读它记在哪儿。
    ${REPO_ROOT}/src/manage/RandomSkinStore.cpp
    ${REPO_ROOT}/src/manage/VersionManager.cpp
    ${REPO_ROOT}/src/manage/VersionDisplay.cpp
    # 体检那一层：VersionManager::checkInstanceIntegrity() 直接调它，少了这一个文件会在
    # 链接那一步报一串 `VersionIntegrity::…` 的 undefined reference。
    ${REPO_ROOT}/src/manage/VersionIntegrity.cpp
    ${REPO_ROOT}/src/manage/UpdatePolicy.cpp
    ${REPO_ROOT}/src/manage/UpdateRollback.cpp
    # 备份那一层（§4.4）：MainWindow 里打存档备份 / 建实例快照 / 还原那几条路直接调它。
    # 少了这一个文件会在链接那一步报一串 `BackupFiles::…` 的 undefined reference。
    ${REPO_ROOT}/src/manage/BackupFiles.cpp
    ${REPO_ROOT}/src/manage/WorldFiles.cpp
    ${REPO_ROOT}/src/manage/WorldMap.cpp
    # 「上一次启动」那一层（§5.1）：少了这一个文件会在链接那一步报一串
    # `LaunchHistory::…` 的 undefined reference。
    ${REPO_ROOT}/src/manage/LaunchHistory.cpp
    ${REPO_ROOT}/src/settings/EffectiveJavaSettings.cpp
    ${REPO_ROOT}/src/settings/GameWindowSettings.cpp
    ${REPO_ROOT}/src/settings/GlobalJavaSettings.cpp
    ${REPO_ROOT}/src/settings/LauncherSettings.cpp
    ${REPO_ROOT}/src/download/DownloadManager.cpp
    ${REPO_ROOT}/src/download/ResumableDownload.cpp
    ${REPO_ROOT}/src/download/ResumableDownload.hpp
    # 下载诊断层（2026-10-17）：上面两个 .cpp 现在无条件调它，不进来就是链接期
    # undefined reference（enabled() 在普通构建里是编译期 false）。
    ${REPO_ROOT}/src/util/DownloadDiagnostics.cpp
    ${REPO_ROOT}/src/util/DownloadDiagnostics.hpp
    # 下载页那四页的取数（搜索 / 版本列表 / 图标）。MainWindow.cpp 里那条路要它们 ——
    # 少了这两个文件，mw_check 会在链接那一步报一串"undefined reference"。
    ${REPO_ROOT}/src/download/ResourceCatalog.cpp
    ${REPO_ROOT}/src/download/ResourceStore.cpp
    # 前置模组 / 更新检测那两套判据（2026-09-28）：MainWindow.cpp 直接调它们。少了这一个
    # 文件，链接期报的是一串 `ModDependencies::…` 的 undefined reference。
    ${REPO_ROOT}/src/download/ModDependencies.cpp
    # ResourceStore 现在按内容解图（webp 走系统 WIC），所以也链它 —— 这里正是
    # icon-live 那条真网络探针要验的那条路（见 main.cpp）。
    ${REPO_ROOT}/src/util/ImageDecode.cpp
    ${REPO_ROOT}/src/util/SystemMemory.cpp
    ${REPO_ROOT}/src/util/ZipUtil.cpp
    # 实例设置「文件管理」里的服务器列表读 servers.dat（2026-09-26）。MainWindow.cpp 直接
    # 调 NbtUtil::readServerList()。
    ${REPO_ROOT}/src/util/NbtUtil.cpp
    # NbtTree（世界设置那条路，2026-09-28）：WorldFiles.cpp 直接调它，少了这一个文件
    # 链接期报一串 `undefined reference to NbtTree::parse`。
    ${REPO_ROOT}/src/util/NbtTree.cpp
    # 服务器列表那一页的「测速」（2026-09-27）：MainWindow::pingServers() 直接调它。少了
    # 这一个文件，mw_check 会在链接那一步报一串 `ServerPing::…` 的 undefined reference。
    ${REPO_ROOT}/src/util/ServerPing.cpp
    # 工具箱那五件工具（2026-10-16）：MainWindow.cpp 里那几句把底栏面板接到
    # ToolboxController 上，而 controller 自己要用这四个纯逻辑模块 + 那一页。少任何一个都会在
    # 链接那一步报一串 undefined reference。
    ${REPO_ROOT}/src/controllers/ToolboxController.cpp
    ${REPO_ROOT}/src/controllers/DirectDownloadController.cpp
    ${REPO_ROOT}/src/controllers/ToolboxController.hpp
    ${REPO_ROOT}/ui/ToolboxPage.cpp
    ${REPO_ROOT}/ui/ToolboxPage.hpp
    ${REPO_ROOT}/src/tools/TextFormat.cpp
    ${REPO_ROOT}/src/tools/CoordConvert.cpp
    ${REPO_ROOT}/src/tools/FileHash.cpp
    ${REPO_ROOT}/src/tools/AnvilPlanner.cpp
    ${REPO_ROOT}/src/tools/Beacon.cpp
    ${REPO_ROOT}/src/tools/Brewing.cpp
    ${REPO_ROOT}/src/tools/Banner.cpp
    ${REPO_ROOT}/src/tools/ArmorTrim.cpp
    # 「导出错误报告」那一份文本（2026-09-28）：MainWindow.cpp 直接调它。
    ${REPO_ROOT}/src/util/ErrorReport.cpp
    # 游戏异常退出那几句话（2026-10-12，coding plan §4.3）：MainWindow::buildCrashRecord()
    # 直接调它。少了这一个文件会报一串 `GameExitDiagnosis::…` 的 undefined reference。
    ${REPO_ROOT}/src/diag/GameExitDiagnosis.cpp
    # 测速的「上次问到的那一份」（2026-09-27）：MainWindow 里那一趟先读它垫底、
    # 问回来一条写一条。少了这一个文件会报 ServerStatusCache::… 的 undefined reference。
    # ServerListFile（2026-10-10）：MainWindow.cpp 里服务器列表那一页的编辑那一发
    # 直接调它（只改 name / ip 两格）。少了这一个文件会报 ServerListFile 的 undefined reference。
    ${REPO_ROOT}/src/manage/ServerListFile.cpp
    ${REPO_ROOT}/src/manage/ServerStatusCache.cpp
    # 「文件管理」那几页的图标反查（2026-09-27）：MainWindow 在后台线程里调它。
    ${REPO_ROOT}/src/manage/InstanceIconLookup.cpp
    ${REPO_ROOT}/ui/AccountSkinDialog.cpp
    # 「游戏日志」窗是 2026-09-24 加的，MainWindow.cpp 直接 new 它 —— 少了这一个文件，
    # mw_check 会在链接那一步报"undefined reference to GameLogWindow::…"。
    ${REPO_ROOT}/ui/GameLogWindow.cpp
    # 运行输出共用模型（2026-10-18，§9.8）：GameLogWindow / ServerConsolePage 都直接用它。
    ${REPO_ROOT}/ui/RuntimeOutputModel.cpp
    ${REPO_ROOT}/ui/MainWindowUI.cpp
    # 「最大内存」那一行那根条（2026-09-26）：MainWindowUI.cpp 和
    # InstanceSettingsDialog.cpp 都直接 new 它，少了这一个文件会在链接那一步报一串
    # undefined reference to MemorySlider::…。
    ${REPO_ROOT}/ui/MemorySlider.cpp

    ${REPO_ROOT}/ui/GroupDialog.cpp
    ${REPO_ROOT}/ui/GroupListDelegate.cpp
    ${REPO_ROOT}/ui/InstanceSettingsDialog.cpp
    ${REPO_ROOT}/ui/InstanceFilesPage.cpp
    ${REPO_ROOT}/ui/ResourcePackPage.cpp
    # 「配置文件」那一页（2026-09-27）：编辑器那一格和它那套纯函数（认 BOM / 认行尾 /
    # 认是不是 UTF-8）都是 ui/ 的东西，InstanceSettingsDialog 直接 new 它。少了这两个
    # 文件，链接期报的是 `undefined reference to ConfigFileEditorPage::…`。
    ${REPO_ROOT}/ui/ConfigFileText.cpp
    ${REPO_ROOT}/ui/ConfigFileEditorPage.cpp
    # 「条目」那一格（2026-09-27）：见 ui_check 那份清单里同一段。
    ${REPO_ROOT}/ui/ConfigEntries.cpp
    # 「世界设置」那一页（2026-09-28）：见 ui_check 那份清单里同一段。
    ${REPO_ROOT}/ui/WorldSettingsPage.cpp
    # 规则那一格名字上盖的官方中文（2026-09-29）：WorldSettingsPage 直接调它。
    ${REPO_ROOT}/ui/GameRuleNames.cpp
    ${REPO_ROOT}/ui/JavaMismatchDialog.cpp
    ${REPO_ROOT}/ui/JavaManageDialog.cpp
    # 「实例预设」整页（2026-10-18）：PresetController 直接 new 它。
    ${REPO_ROOT}/ui/PresetManagePage.cpp
    ${REPO_ROOT}/ui/SkinModel.cpp
    ${REPO_ROOT}/ui/VersionIcons.cpp
    # 四个加载器的标志那几张真图（2026-09-28）：qrc 不列在这儿，
    # `:/assets/loader/*.png` 就是空图，loaderMark() 一路退回草方块
    # （ui_check 那条「四张标志互不相同」会红，产品自己看着一切正常）。
    ${REPO_ROOT}/assets/icons.qrc
    ${REPO_ROOT}/ui/ToggleSwitch.cpp
    ${REPO_ROOT}/ui/MotdText.cpp
    ${REPO_ROOT}/ui/VersionTreeDelegate.cpp
    ${REPO_ROOT}/ui/ResourceDetailPage.cpp
    ${REPO_ROOT}/ui/InstanceGroupDelegate.cpp
    ${REPO_ROOT}/ui/OverlayHost.cpp
    ${REPO_ROOT}/ui/InlineMessage.cpp
    ${REPO_ROOT}/ui/SkinViewer.cpp
    # 顶上那条自己画的标题栏 + 外框挂件（2026-09-25，自定义外框那一趟）：
    # MainWindowUI.cpp 直接 new 它们两个，少了这两行会在链接那一步报
    # `undefined reference to TitleBar::TitleBar(QWidget*)` / `WindowChrome::…`。
    ${REPO_ROOT}/ui/TitleBar.cpp
    ${REPO_ROOT}/ui/Theme.cpp
    # 动效那一层（2026-09-29）：按钮按下 / 换页 / 展开折叠。MainWindowUI.cpp 和
    # OverlayHost.cpp 都直接调它，少了这一个文件会在链接那一步报一串 undefined reference
    # to Anim::…。
    ${REPO_ROOT}/ui/Animations.cpp
    # 主题里那两张背景图（2026-09-28）：`ui/ThemeBackground.cpp` 是四个页面和主页左栏直接
    # new 的那个容器，`src/util/ThemeImage.cpp` 是 MainWindow 收图那一步调的（挑回来的图
    # 拷进数据目录、顺手缩到 2560 以内）。少哪一个都会在链接那一步报一串 undefined reference。
    ${REPO_ROOT}/ui/ThemeBackground.cpp
    ${REPO_ROOT}/src/util/ThemeImage.cpp
    # 任务中心那一套（2026-10-12）：面板本身（MainWindowUI.cpp 直接 new 它）和账本
    # （MainWindow 直接 new 它 + 读它）。少哪一个都会在链接那一步报一串 undefined reference。
    ${REPO_ROOT}/ui/TaskCenterPanel.cpp
    ${REPO_ROOT}/ui/TaskDetailPage.cpp
    ${REPO_ROOT}/ui/TaskProgressBar.cpp
    # 完整性检查报告页（2026-10-12，coding plan §4.1）：MainWindow.cpp 直接 new 它。
    ${REPO_ROOT}/ui/IntegrityReportPage.cpp
    # 更新中心那一页（2026-10-13，§7.1）：MainWindow.cpp 直接 new 它。
    ${REPO_ROOT}/ui/UpdateCenterPage.cpp
    # 「新建服务端」那一页（2026-10-13，§6.2 / M4）：MainWindow.cpp 直接 new 它。
    ${REPO_ROOT}/ui/NewServerPage.cpp
    # 服务端控制台那一页（2026-10-13，§6.2 / M4-3）：U36 整页驱动它。
    ${REPO_ROOT}/ui/ServerConsolePage.cpp
    # 「服务端设置」那一页（2026-10-13，§6.3 / M4-4）：MainWindow.cpp 直接 new 它，T42
    # 整页驱动它（表单 / 原文 / 备份 / 体检四页 + 保存带）。
    ${REPO_ROOT}/ui/ServerSettingsPage.cpp
    # 服务端工作区（2026-10-13，§6 / M4）：MainWindow.cpp 里建服务端 / 重扫列表那几条路
    # 直接调它。少了这一个文件会在链接那一步报一串 `ServerRegistry::…` 的 undefined
    # reference（`ServerModel.hpp` 是纯头，不用列）。
    ${REPO_ROOT}/src/server/ServerRegistry.cpp
    # 服务端安装那四段的驱动层（2026-10-13，§6.2 / M4-2）：MainWindow.cpp 构造里直接 new
    # 它、也直接接它那几条信号。少了这一个文件会在链接那一步报一串
    # `ServerInstallController::…` 的 undefined reference（判据那一半
    # `ServerInstallRun.hpp` 是纯头，不用列）。
    ${REPO_ROOT}/src/server/ServerInstallController.cpp
    # 服务端"跑"那一层（2026-10-13，§6.2 的操作 / M4-3）：T41 直接 new 它、拿本工程自己的
    # exe 当"服务端"跑一整趟（`--fake-server`）。少了这一个文件会在链接那一步报一串
    # `ServerProcess::…` 的 undefined reference（判据那一半 `ServerRun.hpp` 是纯头，不用列）。
    ${REPO_ROOT}/src/server/ServerProcess.cpp
    # 服务端设置那两张表 + 那一次真监听（2026-10-13，§6.3 / M4-4）：MainWindow.cpp 里那
    # 几条路（摆表单 / 保存 / 体检）直接调它们。少了这两个文件会在链接那一步报一串
    # `ServerPropertiesForm::…` / `ServerPortCheck::…` 的 undefined reference
    # （`ServerCheck.hpp` 是纯头，不用列）。
    ${REPO_ROOT}/src/server/ServerPropertiesForm.cpp
    ${REPO_ROOT}/src/server/ServerPortCheck.cpp
    # 玩家名单的 JSON 判据（2026-10-18，§9.7）：`ServerSettingsController` 读写的就是它。
    ${REPO_ROOT}/src/server/ServerPlayers.cpp
    # 崩溃自动重启的判据（2026-10-18，§9.7）：`MainWindow::onServerExited()` 问的就是它。
    ${REPO_ROOT}/src/server/ServerRestart.cpp
    # 服务端资源包（2026-10-18，§9.7）：`MainWindow::pickServerResourcePack()` 用它。
    ${REPO_ROOT}/src/server/ServerResourcePack.cpp
    # 服务端内容（2026-10-18，§9.7）：`MainWindow::refreshServerContent()` 用它。
    ${REPO_ROOT}/src/server/ServerContent.cpp
    ${REPO_ROOT}/src/tasks/TaskManager.cpp
    ${REPO_ROOT}/src/tasks/TaskHistory.cpp
    ${REPO_ROOT}/ui/WindowChrome.cpp
    ${HEADER_FILES}
)

# VersionInheritance 是"带模组加载器的版本（inheritsFrom）"那次改动引入的。别的分支上
# 没有这两个文件（VersionManager / InstanceFiles 也不会有那些调用），所以存在才加 ——
# 缺了就让对应用例在运行时报 FAIL，而不是让 CMake 直接配置失败。
if(EXISTS ${REPO_ROOT}/src/launcher/VersionInheritance.cpp)
    list(APPEND SOURCE_FILES ${REPO_ROOT}/src/launcher/VersionInheritance.cpp
                             ${REPO_ROOT}/src/launcher/VersionInheritance.hpp)
endif()

# AsciiPathAlias 是"Java 8 打不开非 ASCII 路径"那次修复（2026-09-28）引入的。
# **LauncherManager.cpp 和 MainWindow.cpp 都直接调它**，所以少这一个文件时链接期报的是
# 一串 `undefined reference to AsciiPathAlias::…` —— 编译过得去，别以为只是头文件的事。
# 理由同上一段：别的分支上没有这两个文件，存在才加。
if(EXISTS ${REPO_ROOT}/src/launcher/AsciiPathAlias.cpp)
    list(APPEND SOURCE_FILES ${REPO_ROOT}/src/launcher/AsciiPathAlias.cpp
                             ${REPO_ROOT}/src/launcher/AsciiPathAlias.hpp)
endif()

# LoaderMeta.hpp 是"Fabric 安装"那次改动引入的纯头文件（地址怎么拼、回复怎么挑）。
# 见上面同一段的注释：存在才加，别的分支上没有它。
if(EXISTS ${REPO_ROOT}/src/download/LoaderMeta.hpp)
    list(APPEND SOURCE_FILES ${REPO_ROOT}/src/download/LoaderMeta.hpp)
endif()


add_executable(mw_check main.cpp ${SOURCE_FILES})

if(MINGW)
    target_compile_options(mw_check PRIVATE -Wa,-mbig-obj)
endif()

target_include_directories(mw_check PRIVATE ${REPO_ROOT}/src ${REPO_ROOT}/ui)
target_link_libraries(mw_check PRIVATE Qt6::Widgets Qt6::Network Qt6::Core Qt6::Gui Qt6::Concurrent)
# The integration driver is another very large assertion table; compiling it
# without optimization keeps verification builds practical on MinGW.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(mw_check PRIVATE -O0)
endif()
# AuthManager.cpp 落盘走 DPAPI，验证工程也编译它，所以同样要链 crypt32。
# 后面三个是 ImageDecode.cpp 要的（起 COM / SHCreateMemStream / WIC 本体），
# 见主工程 CMakeLists.txt 里同一段。
target_link_libraries(mw_check PRIVATE crypt32 ole32 shlwapi windowscodecs dwmapi)

add_test(NAME wtfml.main-window COMMAND mw_check)
add_test(NAME wtfml.workspace COMMAND mw_check workspace-check)
add_test(NAME wtfml.activity-selection COMMAND mw_check activity-selection-check)
add_test(NAME wtfml.download-retry COMMAND mw_check download-retry-check)
add_test(NAME wtfml.high-speed-download COMMAND mw_check high-speed-download-check)
add_test(NAME wtfml.segmented-download COMMAND mw_check segmented-download-check)
add_test(NAME wtfml.tool-features COMMAND mw_check tool-features-check)
set_tests_properties(wtfml.tool-features PROPERTIES
    LABELS "ui;tools;queue"
    TIMEOUT 90
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;${WTFML_TEST_RUNTIME_ENVIRONMENT}"
)
set_tests_properties(wtfml.segmented-download PROPERTIES
    LABELS "network;integration;resume"
    TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;${WTFML_TEST_RUNTIME_ENVIRONMENT}"
)
set_tests_properties(wtfml.high-speed-download PROPERTIES
    LABELS "network;integration;toolbox"
    TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;${WTFML_TEST_RUNTIME_ENVIRONMENT}"
)
set_tests_properties(wtfml.download-retry PROPERTIES
    LABELS "network;integration"
    TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;${WTFML_TEST_RUNTIME_ENVIRONMENT}"
)
set_tests_properties(wtfml.activity-selection PROPERTIES
    LABELS "ui;integration"
    TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;${WTFML_TEST_RUNTIME_ENVIRONMENT}"
)
set_tests_properties(wtfml.workspace PROPERTIES
    LABELS "ui;integration"
    TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;${WTFML_TEST_RUNTIME_ENVIRONMENT}"
)
set_tests_properties(wtfml.main-window PROPERTIES
    LABELS "ui;integration"
    TIMEOUT 240
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;${WTFML_TEST_RUNTIME_ENVIRONMENT}"
)

