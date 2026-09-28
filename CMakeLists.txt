cmake_minimum_required(VERSION 3.16)
project(WTFMinecraftLauncher VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/Zc:__cplusplus)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Core Gui)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(HEADER_FILES
    src/MainWindow.hpp
    src/launcher/LauncherManager.hpp
    src/launcher/LibraryRules.hpp
    src/launcher/ProcessPriority.hpp
    src/launcher/VersionInheritance.hpp
    src/auth/AuthManager.hpp
    src/auth/AccountStore.hpp
    src/auth/SkinApi.hpp
    src/auth/TokenStore.hpp
    src/net/HttpClient.hpp
    src/manage/FolderManager.hpp
    src/manage/InstanceFiles.hpp
    src/manage/InstanceIconLookup.hpp
    src/manage/ModpackImport.hpp
    src/manage/ModpackExport.hpp
    src/manage/ServerStatusCache.hpp
    src/manage/VersionManager.hpp
    src/manage/VersionDisplay.hpp
    src/settings/GlobalJavaSettings.hpp
    src/settings/LauncherSettings.hpp
    src/download/DownloadManager.hpp
    src/download/LoaderMeta.hpp
    src/download/ResourceCatalog.hpp
    src/download/ModDependencies.hpp
    src/download/ResourceStore.hpp
    src/util/AppPaths.hpp
    src/util/ImageDecode.hpp
    src/util/SystemMemory.hpp
    src/util/ZipUtil.hpp
    src/util/NbtUtil.hpp
    src/util/ServerPing.hpp
    src/util/ErrorReport.hpp
    ui/ConfigFileText.hpp
    ui/ConfigFileEditorPage.hpp
    ui/ConfigEntries.hpp
    ui/MotdText.hpp
    ui/ToggleSwitch.hpp
    ui/JavaManageDialog.hpp
)

set(SOURCE_FILES
    src/main.cpp
    src/MainWindow.cpp
    src/launcher/LauncherManager.cpp
    src/launcher/VersionInheritance.cpp
    src/auth/AuthManager.cpp
    src/auth/AccountStore.cpp
    src/auth/SkinApi.cpp
    src/auth/TokenStore.cpp
    src/net/HttpClient.cpp
    src/manage/FolderManager.cpp
    src/manage/InstanceFiles.cpp
    src/manage/InstanceIconLookup.cpp
    src/manage/InstanceGroupStore.cpp
    src/manage/ModpackImport.cpp
    src/manage/ModpackExport.cpp
    src/manage/ServerStatusCache.cpp
    src/manage/DefaultSkin.cpp
    src/manage/PlayerSkinStore.cpp
    src/manage/RandomSkinStore.cpp
    src/manage/VersionManager.cpp
    src/manage/VersionDisplay.cpp
    src/settings/EffectiveJavaSettings.cpp
    src/settings/GlobalJavaSettings.cpp
    src/settings/LauncherSettings.cpp
    src/download/DownloadManager.cpp
    src/download/ResourceCatalog.cpp
    src/download/ModDependencies.cpp
    src/download/ResourceStore.cpp
    src/util/ImageDecode.cpp
    src/util/SystemMemory.cpp
    src/util/ZipUtil.cpp
    src/util/NbtUtil.cpp
    src/util/ServerPing.cpp
    src/util/ErrorReport.cpp
    ui/AccountSkinDialog.cpp
    ui/GameLogWindow.cpp
    ui/MainWindowUI.cpp
    ui/MemorySlider.cpp
    ui/MotdText.cpp
    ui/ToggleSwitch.cpp
    ui/GroupDialog.cpp
    ui/GroupListDelegate.cpp
    ui/InstanceSettingsDialog.cpp
    ui/InstanceFilesPage.cpp
    ui/ConfigFileText.cpp
    ui/ConfigFileEditorPage.cpp
    ui/ConfigEntries.cpp
    ui/InlineMessage.cpp
    ui/JavaMismatchDialog.cpp
    ui/JavaManageDialog.cpp
    ui/OverlayHost.cpp
    ui/SkinModel.cpp
    ui/VersionIcons.cpp
    ui/VersionTreeDelegate.cpp
    ui/ResourceDetailPage.cpp
    ui/InstanceGroupDelegate.cpp
    ui/SkinViewer.cpp
    ui/TitleBar.cpp
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

target_include_directories(WTFMinecraftLauncher PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/ui      
)

target_link_libraries(WTFMinecraftLauncher PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Core
    Qt6::Gui
)

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