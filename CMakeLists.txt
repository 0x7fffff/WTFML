cmake_minimum_required(VERSION 3.16)
project(WTFMinecraftLauncher VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/Zc:__cplusplus)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Core Gui Concurrent)

# The launcher used to keep its verification programs under build/, which meant
# they were invisible to source control and to CTest.  Keep normal developer
# builds lean, but provide one switch that enables the complete regression suite.
option(WTFML_BUILD_TESTS "Build the WTFML regression test executables" OFF)
option(WTFML_STRICT_WARNINGS "Enable additional compiler diagnostics" OFF)
option(WTFML_WARNINGS_AS_ERRORS "Treat launcher warnings as build errors" OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(HEADER_FILES
    src/MainWindow.hpp
    src/controllers/JavaRuntimeController.hpp
    src/controllers/JavaSettingsController.hpp
    src/controllers/LauncherPreferencesController.hpp
    src/controllers/CrashReportController.hpp
    src/controllers/IntegrityController.hpp
    src/controllers/BackupController.hpp
    src/controllers/ServerPingController.hpp
    src/controllers/FolderController.hpp
    src/controllers/ModpackExportController.hpp
    src/controllers/InstanceFileController.hpp
    src/controllers/InstanceTransferController.hpp
    src/controllers/InstanceDialogController.hpp
    src/controllers/InstanceEditorController.hpp
    src/controllers/ImportController.hpp
    src/controllers/GameSessionController.hpp
    src/controllers/LaunchController.hpp
    src/controllers/ConnectionsController.hpp
    src/controllers/InstanceIconController.hpp
    src/controllers/ActivityCenterController.hpp
    src/controllers/TaskDetailController.hpp
    src/controllers/HomeQuickController.hpp
    src/controllers/InstanceMaintenanceController.hpp
    src/controllers/ServerWorkspaceController.hpp
    src/controllers/ServerConsoleController.hpp
    src/controllers/ServerSettingsController.hpp
    src/controllers/ThemeSettingsController.hpp
    src/controllers/PlayerSkinController.hpp
    src/controllers/LaunchHistoryController.hpp
    src/controllers/InstanceSettingsController.hpp
    src/controllers/InstancePagesController.hpp
    src/controllers/ServerListController.hpp
    src/launcher/LauncherManager.hpp
    src/launcher/LibraryRules.hpp
    src/launcher/ProcessPriority.hpp
    src/launcher/VersionInheritance.hpp
    src/auth/AuthManager.hpp
    src/auth/AccountStore.hpp
    src/auth/OAuthEndpoints.hpp
    src/auth/SkinApi.hpp
    src/auth/TokenStore.hpp
    src/net/HttpClient.hpp
    src/manage/DroppedResource.hpp
    src/manage/FolderManager.hpp
    src/manage/InstanceFiles.hpp
    src/manage/InstanceIconLookup.hpp
    src/manage/ModpackImport.hpp
    src/manage/ModpackExport.hpp
    src/manage/ServerListFile.hpp
    src/manage/ServerStatusCache.hpp
    src/manage/VersionManager.hpp
    src/manage/VersionDisplay.hpp
    src/manage/VersionIntegrity.hpp
    src/manage/UpdatePolicy.hpp
    src/manage/UpdateRollback.hpp
    src/manage/BackupFiles.hpp
    src/manage/WorldFiles.hpp
    # 启动历史账本（2026-10-13，coding plan §5.1）：主页“最近游玩”排与统计的数据源。
    src/manage/LaunchHistory.hpp
    # 实例页那张树的搜索与筛选（2026-10-13，coding plan §7.2）：判据层，纯函数。
    src/manage/InstanceFilter.hpp
    # 实例预设（2026-10-14，coding plan §5–§6 / P1）：先落「值」和「账」两层，
    # 段里那点载荷归各个适配器，界面那一半等 P1 的管理页上线时再接。
    src/presets/PresetTypes.hpp
    src/presets/PresetStore.hpp
    # 原版设置那一段（2026-10-15，coding plan §6 P2）：`options.txt` 的保序模型 + 认得的
    # 键那张表 + 按键冲突 + "这一段怎么落到实例上"的计划。纯值层（字节进、字节出）。
    src/presets/VanillaOptions.hpp
    # 服务器列表那一段（2026-10-15，coding plan §6 P3）：`servers.dat` 的判据 —— 地址怎么算
    # 同一台、Append / Replace / Skip 各是什么、这一趟会动哪几条。写盘那一半仍旧只有一处
    # 实现（`src/manage/ServerListFile` 露出来的那几口），这一层一个 QFile 都不开。
    src/presets/ServerListPreset.hpp
    # 通用文件组那一段（2026-10-15，coding plan §6 P3）：`config/` / `defaultconfigs/` /
    # 资源包 / 光影 —— 我们看不懂里面的东西，所以只做整份的动作（缺了才拷 / 覆盖 / 跳过），
    # 三道路径闸和"这一项只能复制或覆盖"那记号都在这一份里。
    src/presets/GenericFiles.hpp
    # 把一份预设落到一个实例上这件事的**计划**那一层（2026-10-15，coding plan §5 的
    # `PresetPlanner` / §7 那张计划表 / 里程碑 M1 的「应用前可见精确计划」）：一段一行，
    # 按段的种类分派给上面三个适配器，再把各自的报告翻成同一张表。**只读，一个字节都不写。**
    src/presets/PresetPlanner.hpp
    src/settings/GlobalJavaSettings.hpp
    src/settings/GameWindowSettings.hpp
    src/settings/LauncherSettings.hpp
    src/download/DownloadManager.hpp
    src/download/LoaderMeta.hpp
    src/download/ServerInstall.hpp
    src/download/ResourceCatalog.hpp
    src/download/ModDependencies.hpp
    src/download/ResourceStore.hpp
    # 任务中心那一套（2026-10-12，coding plan §2）：账本的形状（TaskSnapshot / TaskId，
    # 纯值 + 几条纯函数）、账本自己（TaskManager）、以及它的落盘（TaskHistory）。
    src/tasks/TaskId.hpp
    src/tasks/TaskSnapshot.hpp
    src/tasks/TaskManager.hpp
    src/tasks/TaskHistory.hpp
    src/diag/GameExitDiagnosis.hpp
    # 服务端工作区（2026-10-13，coding plan §6 / 里程碑 M4）：一个服务端是什么（判据层，
    # 纯函数）、盘上那一份谁在住（扫目录 + 读写 .wtfml-server）。
    src/server/ServerModel.hpp
    src/server/ServerRegistry.hpp
    # 服务端安装那四段（2026-10-13，coding plan §6.2 / 里程碑 M4-2）：判据层（纯头：还差哪
    # 一份元数据、取回来怎么读、分成几步）+ 驱动层（取元数据 → 下载 → 跑安装器 → 核对）。
    src/server/ServerInstallRun.hpp
    src/server/ServerInstallController.hpp
    # 服务端的启动 / 停止那一套（2026-10-13，coding plan §6.2 的操作 + §6.3 的停止时序 /
    # 里程碑 M4-3）：判据层（纯头：那条命令怎么拼、起来没有、怎么停、宽限多久）+ 驱动层
    # （一条 QProcess，一个服务端一份）。
    src/server/ServerRun.hpp
    src/server/ServerProcess.hpp
    # 「服务端设置」那几层的判据（2026-10-13，coding plan §6.3 / 里程碑 M4-4）：
    # `server.properties` 常用字段那张表与它的值域（纯）、端口占用那一次真监听（薄）、
    # 体检（纯值进、结论出）。
    src/server/ServerPropertiesForm.hpp
    src/server/ServerPortCheck.hpp
    src/server/ServerCheck.hpp
    src/util/AppPaths.hpp
    src/util/ThemeImage.hpp
    src/util/ImageDecode.hpp
    src/util/SystemMemory.hpp
    src/util/ZipUtil.hpp
    src/util/NbtUtil.hpp
    src/util/NbtTree.hpp
    src/util/ServerPing.hpp
    src/util/ErrorReport.hpp
    # 工具箱那五件（2026-10-16，用户点名的那一批）：纯逻辑层，各自的"照什么规矩、不做什么"
    # 都写在头文件开头那一段里。
    src/tools/TextFormat.hpp
    src/tools/CoordConvert.hpp
    src/tools/FileHash.hpp
    src/tools/AnvilPlanner.hpp
    ui/ConfigFileText.hpp
    ui/ConfigFileEditorPage.hpp
    ui/WorldSettingsPage.hpp
    ui/GameRuleNames.hpp
    ui/Theme.hpp
    ui/ThemeBackground.hpp
    ui/ConfigEntries.hpp
    ui/MotdText.hpp
    ui/ToggleSwitch.hpp
    # 工具箱那一整页（五件小工具共用一页 + 顶上那排 chip，见它文件头那一段）。
    ui/ToolboxPage.hpp
    # 「服务端设置」那一整页（2026-10-13，coding plan §6.3 / M4-4）。
    ui/ServerSettingsPage.hpp
    ui/JavaManageDialog.hpp
    # 底栏中间那格点开之后弹出来的任务中心面板（页面/行的形状见它的文件头）。
    ui/TaskCenterPanel.hpp
    # 自绘的进度条（2026-10-13，coding plan §3.3「统一自绘任务进度组件」）：
    # 底栏 / 任务面板 / 详情页三处共用这一条，换掉原来各自的原生 QProgressBar。
    ui/TaskProgressBar.hpp
)

set(SOURCE_FILES
    src/main.cpp
    src/MainWindow.cpp
    src/controllers/JavaRuntimeController.cpp
    src/controllers/JavaSettingsController.cpp
    src/controllers/LauncherPreferencesController.cpp
    src/controllers/CrashReportController.cpp
    src/controllers/IntegrityController.cpp
    src/controllers/ToolboxController.cpp
    src/controllers/BackupController.cpp
    src/controllers/ServerPingController.cpp
    src/controllers/FolderController.cpp
    src/controllers/ModpackExportController.cpp
    src/controllers/InstanceFileController.cpp
    src/controllers/InstanceTransferController.cpp
    src/controllers/InstanceDialogController.cpp
    src/controllers/InstanceEditorController.cpp
    src/controllers/ImportController.cpp
    src/controllers/GameSessionController.cpp
    src/controllers/LaunchController.cpp
    src/controllers/ConnectionsController.cpp
    src/controllers/InstanceIconController.cpp
    src/controllers/ActivityCenterController.cpp
    src/controllers/TaskDetailController.cpp
    src/controllers/HomeQuickController.cpp
    src/controllers/InstanceMaintenanceController.cpp
    src/controllers/ServerWorkspaceController.cpp
    src/controllers/ServerConsoleController.cpp
    src/controllers/ServerSettingsController.cpp
    src/controllers/ThemeSettingsController.cpp
    src/controllers/PlayerSkinController.cpp
    src/controllers/LaunchHistoryController.cpp
    src/controllers/InstanceSettingsController.cpp
    src/controllers/InstancePagesController.cpp
    src/controllers/ServerListController.cpp
    src/launcher/LauncherManager.cpp
    src/launcher/AsciiPathAlias.cpp
    src/launcher/VersionInheritance.cpp
    src/auth/AuthManager.cpp
    src/auth/AccountStore.cpp
    src/auth/SkinApi.cpp
    src/auth/TokenStore.cpp
    src/net/HttpClient.cpp
    src/manage/DroppedResource.cpp
    src/manage/FolderManager.cpp
    src/manage/InstanceFiles.cpp
    src/manage/InstanceIconLookup.cpp
    src/manage/InstanceGroupStore.cpp
    src/manage/ModpackImport.cpp
    src/manage/ModpackExport.cpp
    src/manage/ServerStatusCache.cpp
    src/manage/ServerListFile.cpp
    src/manage/DefaultSkin.cpp
    src/manage/PlayerSkinStore.cpp
    src/manage/RandomSkinStore.cpp
    src/manage/VersionManager.cpp
    src/manage/VersionDisplay.cpp
    src/manage/VersionIntegrity.cpp
    src/manage/UpdatePolicy.cpp
    src/manage/UpdateRollback.cpp
    src/manage/BackupFiles.cpp
    src/manage/WorldFiles.cpp
    # 「上一次启动」那本账（2026-10-13，coding plan §5.1）。
    src/manage/LaunchHistory.cpp
    # 实例预设的盘那一层（2026-10-14，coding plan §5–§6 / P1）：下标 + 单份 + 那几件增删改。
    src/presets/PresetStore.cpp
    # 原版设置那一段的判据（2026-10-15，coding plan §6 P2）：见头文件那一位。
    src/presets/VanillaOptions.cpp
    # 服务器列表那一段的判据 + 落到实例上（2026-10-15，coding plan §6 P3）：见头文件那一位。
    src/presets/ServerListPreset.cpp
    # 通用文件组那一段的判据 + 落盘（2026-10-15，coding plan §6 P3）：见头文件那一位。
    src/presets/GenericFiles.cpp
    # 应用计划那一层（2026-10-15，coding plan §5 / §7）：见头文件那一位。
    src/presets/PresetPlanner.cpp
    src/settings/EffectiveJavaSettings.cpp
    src/settings/GlobalJavaSettings.cpp
    src/settings/GameWindowSettings.cpp
    src/settings/LauncherSettings.cpp
    src/download/DownloadManager.cpp
    src/download/ResourceCatalog.cpp
    src/download/ModDependencies.cpp
    src/download/ResourceStore.cpp
    # 任务中心那一套（2026-10-12，coding plan §2）：账本 + 它的落盘。
    src/tasks/TaskManager.cpp
    src/tasks/TaskHistory.cpp
    # 游戏异常退出那几句话（2026-10-12，coding plan §4.3）：纯规则，一张表。
    src/diag/GameExitDiagnosis.cpp
    src/server/ServerRegistry.cpp
    # 服务端安装的驱动层（2026-10-13，§6.2 / M4-2）：它带 QObject + QNetwork + QProcess
    # （判据那一半在 `ServerInstallRun.hpp`，是纯头）。
    src/server/ServerInstallController.cpp
    # 服务端的"跑"（2026-10-13，§6.2 的操作 / M4-3）：一个服务端一条 QProcess
    # （判据那一半在 `ServerRun.hpp`，是纯头）。
    src/server/ServerProcess.cpp
    # 服务端设置那两张表 + 那一次真监听（2026-10-13，§6.3 / M4-4；判据那一半在各自的头里）。
    src/server/ServerPropertiesForm.cpp
    src/server/ServerPortCheck.cpp
    src/util/ImageDecode.cpp
    src/util/ThemeImage.cpp
    src/util/SystemMemory.cpp
    src/util/ZipUtil.cpp
    src/util/NbtUtil.cpp
    src/util/NbtTree.cpp
    src/util/ServerPing.cpp
    src/util/ErrorReport.cpp
    src/tools/TextFormat.cpp
    src/tools/CoordConvert.cpp
    src/tools/FileHash.cpp
    src/tools/AnvilPlanner.cpp
    ui/AccountSkinDialog.cpp
    ui/GameLogWindow.cpp
    ui/MainWindowUI.cpp
    ui/MemorySlider.cpp
    ui/MotdText.cpp
    ui/ToolboxPage.cpp
    ui/ToggleSwitch.cpp
    ui/TaskCenterPanel.cpp
    ui/TaskDetailPage.cpp
    ui/TaskProgressBar.cpp
    ui/IntegrityReportPage.cpp
    ui/UpdateCenterPage.cpp
    ui/NewServerPage.cpp
    ui/ServerConsolePage.cpp
    ui/ServerSettingsPage.cpp
    ui/GroupDialog.cpp
    ui/GroupListDelegate.cpp
    ui/InstanceSettingsDialog.cpp
    ui/InstanceFilesPage.cpp
    ui/ResourcePackPage.cpp
    ui/ConfigFileText.cpp
    ui/ConfigFileEditorPage.cpp
    ui/WorldSettingsPage.cpp
    ui/GameRuleNames.cpp
    ui/ConfigEntries.cpp
    ui/InlineMessage.cpp
    ui/JavaMismatchDialog.cpp
    ui/JavaManageDialog.cpp
    ui/OverlayHost.cpp
    ui/SkinModel.cpp
    ui/VersionIcons.cpp
    # 四个加载器的标志那几张真图（2026-09-28）。CMAKE_AUTORCC 是开的，所以一个 .qrc
    # 列在这儿就够了、不用再写 qt_add_resources。验证工程也要各自列一份（见那几个
    # CMakeLists 里同一行的注释）：漏了的表现是那几张图**静默变回草方块**。
    assets/icons.qrc
    ui/VersionTreeDelegate.cpp
    ui/ResourceDetailPage.cpp
    ui/InstanceGroupDelegate.cpp
    ui/SkinViewer.cpp
    ui/TitleBar.cpp
    ui/Theme.cpp
    # 动效那一层（2026-09-29）：按下按钮 / 换页 / 展开折叠。
    ui/Animations.cpp
    ui/ThemeBackground.cpp
    ui/WindowChrome.cpp
    ${HEADER_FILES}
)

qt_add_executable(WTFMinecraftLauncher
    ${SOURCE_FILES}
)

# GUI 子系统（**不弹控制台窗口**）。
#
# 不设这一条的话，MinGW 出来的默认是 CUI（Windows 控制台程序），而双击一个 CUI 程序时
# Windows 会给它开一个黑框：用户 2026-09-23 报的「运行的时候弹了一串消息」就是它 ——
# 里面刷的是 [WTFML] 的启动日志和 Qt 自己那几句警告。验证工程的 exe（verify / ui_check /
# mw_check / ui_shot）**不受影响**：它们是各自 CMakeLists 里单独的 target，本来就该是
# 控制台程序（那一跑就是"看它打什么字"）。
#
# 代价是"那些话没人看得见"，由 src/main.cpp 的 installFileLog() 补上：
# <数据目录>/logs/wtfml.log，每次启动截断重写。
set_target_properties(WTFMinecraftLauncher PROPERTIES WIN32_EXECUTABLE TRUE)

# 还得把 Qt 自己那个入口（Qt6EntryPoint）关掉，否则 MinGW 这一套链不过去：
# 它那个 `WinMain → main(__argc, __argv)` 要 `__argc`/`__argv`，而这个工具链上它们
# 是 dllimport 的、没人提供 —— 症状是链接期一句
# `undefined reference to __imp___argc`（不是运行期，所以别指望跑起来才发现）。
#
# qt_no_entrypoint 是 Qt 自己认的那个开关（Qt6CoreTargets.cmake 里那段 genex 就是按它
# 决定要不要链 EntryPointPrivate），链上 MinGW 自带的 WinMain 之后 `int main()` 照旧是
# 入口，GUI 子系统这件事只由上面那条 WIN32_EXECUTABLE 决定。
set_target_properties(WTFMinecraftLauncher PROPERTIES qt_no_entrypoint TRUE)

# 版本号**只有一个出处 = 上面 `project(... VERSION ...)` 那一位**，由这儿递给源码。
#
# src/main.cpp 的 `setApplicationVersion(WTFML_VERSION)` 读它，值最后会走到两处：
#   * `src/launcher/LauncherManager.cpp` —— version.json 里 `${launcher_version}` 展开成
#     `-Dlauncher_version=`（盘上那一位空的时候回落到字面量 `1.0`）；
#   * `src/MainWindow.cpp` 的错误报告（`facts.launcherVersion`）。
# 从前这两处是**各写各的**（CMakeLists 写 0.1.0、main.cpp 写 1.0.0，早就不一致了），
# 发 0.2.0 的时候才发现——所以改成从这一个宏出来，别再写第二份字面量。
target_compile_definitions(WTFMinecraftLauncher PRIVATE WTFML_VERSION="${PROJECT_VERSION}")

# 正版登录那两格（coding plan §9.4「客户端 ID 和回调地址通过构建配置注入」）。
#
# 默认**空 = 用源码里那份兜底值**（`src/auth/AuthManager.cpp` 里那两个 `#ifdef` 的另一半），
# 所以不带 `-D` 构建出来的东西和从前逐字一样。要换成自己那份 Azure 应用注册时不用改源码：
#
#   cmake -DWTFML_CLIENT_ID=<你的应用 id> -DWTFML_REDIRECT_URI=http://localhost:38300/ …
#
# ★ 回调地址换了就等于换了监听端口：端口是 AuthManager 从这串地址里**算**出来的
#   （`OAuthEndpoints::callbackPort()`），别再去找第二个地方改。
# ★ 为什么不干脆把源码里那两个字面量删掉：删掉之后不带 `-D` 的构建会拿到空 client_id，
#   表现是"点登录、浏览器开了、微软说这个应用不存在" —— 比多一个开关更糟。
set(WTFML_CLIENT_ID "" CACHE STRING "Microsoft OAuth 客户端 ID（空 = 用源码里的兜底值）")
set(WTFML_REDIRECT_URI "" CACHE STRING "OAuth 回调地址，端口随之生效（空 = 用源码里的兜底值）")

if(WTFML_CLIENT_ID)
    target_compile_definitions(WTFMinecraftLauncher PRIVATE WTFML_CLIENT_ID="${WTFML_CLIENT_ID}")
endif()
if(WTFML_REDIRECT_URI)
    target_compile_definitions(WTFMinecraftLauncher PRIVATE WTFML_REDIRECT_URI="${WTFML_REDIRECT_URI}")
endif()

# MainWindow.cpp is still large while its controllers are being extracted.  A
# debug build can exceed PE/COFF's default section count under MinGW.
if(MINGW)
    target_compile_options(WTFMinecraftLauncher PRIVATE -Wa,-mbig-obj)
endif()

target_include_directories(WTFMinecraftLauncher PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/ui      
)

target_link_libraries(WTFMinecraftLauncher PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Core
    Qt6::Gui
    Qt6::Concurrent
)

if(WTFML_STRICT_WARNINGS)
    if(MSVC)
        target_compile_options(WTFMinecraftLauncher PRIVATE /W4 /permissive-)
    else()
        target_compile_options(WTFMinecraftLauncher PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()
if(WTFML_WARNINGS_AS_ERRORS)
    if(MSVC)
        target_compile_options(WTFMinecraftLauncher PRIVATE /WX)
    else()
        target_compile_options(WTFMinecraftLauncher PRIVATE -Werror)
    endif()
endif()

# 令牌落盘用的 DPAPI（CryptProtectData）在这个库里。见 src/auth/TokenStore.cpp 的 protect()。
#
# 另外三个是 src/util/ImageDecode.cpp 要的（**只有 Windows 上有**，所以整段套在 WIN32 里）：
#   * ole32          —— CoInitializeEx / CoCreateInstance（起 COM、拿 WIC 工厂）
#   * shlwapi        —— SHCreateMemStream（把内存里那串图标字节变成一个 IStream，不落盘）
#   * windowscodecs  —— WIC 本体（IWICImagingFactory 那些）
# 三个都是系统 DLL（Vista 起就在），**不发任何东西**：见 ImageDecode 文件头那段。
#
# dwmapi 是外框那一份要的（ui/WindowChrome.cpp 里的 DwmSetWindowAttribute，把圆角明说一遍）。
# 同样是系统 DLL（Vista 起就有）。
if(WIN32)
    target_link_libraries(WTFMinecraftLauncher PRIVATE crypt32 ole32 shlwapi windowscodecs dwmapi)
endif()

if(WTFML_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
