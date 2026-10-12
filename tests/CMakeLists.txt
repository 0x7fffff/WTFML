# 核心回归验证：直接编译生产代码，用控制台断言覆盖文件、网络解析和启动规则。

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network Gui)

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
get_filename_component(REPO_ROOT ${REPO_ROOT} REALPATH)

set(PROJECT_SOURCES
    # auth/ 这一段是「多账号 + 换皮肤披风」那次改动加进来的。
    #   - TokenStore / AccountStore：凭据落盘的字节格式（含 DPAPI）和多账号列表。
    #     这一层**必须**在真 Qt 下验：盘上那份字节的最外层是 @ByteArray(...) 那种
    #     包装，手搓一个"看起来对"的字节串等于自证（见 memory 里那条「令牌存盘的
    #     字节格式」）。
    #   - SkinApi：换皮肤披风那几条接口的解析与话术，全是纯函数（不联网）。
    #   - AuthManager：只为 SkinApi 那句 403 话术转发（appRegistrationHint 只能有
    #     一个出处），verify 自己不构造它。
    ${REPO_ROOT}/src/auth/AccountStore.cpp
    ${REPO_ROOT}/src/auth/AccountStore.hpp
    ${REPO_ROOT}/src/auth/AuthManager.cpp
    ${REPO_ROOT}/src/auth/AuthManager.hpp
    ${REPO_ROOT}/src/auth/SkinApi.cpp
    ${REPO_ROOT}/src/auth/SkinApi.hpp
    ${REPO_ROOT}/src/auth/TokenStore.cpp
    ${REPO_ROOT}/src/auth/TokenStore.hpp
    ${REPO_ROOT}/src/net/HttpClient.cpp
    ${REPO_ROOT}/src/net/HttpClient.hpp
    ${REPO_ROOT}/src/download/DownloadManager.cpp
    ${REPO_ROOT}/src/download/DownloadManager.hpp
    ${REPO_ROOT}/src/launcher/LauncherManager.cpp
    ${REPO_ROOT}/src/launcher/LauncherManager.hpp
    ${REPO_ROOT}/src/launcher/LibraryRules.hpp
    # DefaultSkin：离线名字 → 官方默认皮肤那张图（Java 那个 UUID.hashCode() 的实现、
    # 18 元表的下标、两套规矩各自的路径、缓存文件名怎么拼）。**这一份跑的是真 JAR** ——
    # 工程自己造一个假的版本目录 + 假 JAR（Compress-Archive），把"判据是 JAR、不是版本号"
    # 这条也钉住了。真值表来自本机 JVM（见那边的注释）。
    ${REPO_ROOT}/src/manage/DefaultSkin.cpp
    ${REPO_ROOT}/src/manage/DefaultSkin.hpp
    ${REPO_ROOT}/src/manage/InstanceFiles.cpp
    # FolderManager.cpp 是**被 VersionManager.cpp 拽进来的**：删实例那条路
    # （VersionManager::deleteInstance）用的是 FolderManager.hpp 里那个自由函数
    # removeTreeWithoutFollowingLinks()，实现就在这个 .cpp 里。少了它，链接期报的是
    # 一条 `undefined reference to removeTreeWithoutFollowingLinks(QString const&)`
    # —— 编译过得去，别以为只是头文件的事。
    ${REPO_ROOT}/src/manage/FolderManager.cpp
    ${REPO_ROOT}/src/manage/InstanceFiles.hpp
    ${REPO_ROOT}/src/manage/InstanceGroupStore.cpp
    ${REPO_ROOT}/src/manage/InstanceGroupStore.hpp
    ${REPO_ROOT}/src/manage/DroppedResource.cpp
    ${REPO_ROOT}/src/manage/ModpackImport.cpp
    ${REPO_ROOT}/src/manage/DroppedResource.hpp
    ${REPO_ROOT}/src/manage/ModpackImport.hpp
    ${REPO_ROOT}/src/manage/ModpackExport.cpp
    ${REPO_ROOT}/src/manage/ModpackExport.hpp
    ${REPO_ROOT}/src/manage/PlayerSkinStore.cpp
    ${REPO_ROOT}/src/manage/PlayerSkinStore.hpp
    # RandomSkinStore：离线账号那张随机皮肤的地址怎么拼、列表怎么解、挑下标越界了怎么办、
    # 盘上记的那一行还算不算数。全是纯函数，一条都不联网（真去要去要 MineSkin 那个端点，
    # 见 mw_check 的 AH 段之外那几条一次性探针）。
    ${REPO_ROOT}/src/manage/RandomSkinStore.cpp
    ${REPO_ROOT}/src/manage/RandomSkinStore.hpp
    ${REPO_ROOT}/src/manage/VersionManager.cpp
    ${REPO_ROOT}/src/manage/VersionManager.hpp
    ${REPO_ROOT}/src/manage/VersionDisplay.cpp
    ${REPO_ROOT}/src/manage/VersionDisplay.hpp
    # 体检那一层（ZN 节）：纯值进、结论出 —— 一个文件都不下，所以能直接对着一个搭出来的
    # 临时游戏文件夹跑。
    ${REPO_ROOT}/src/manage/VersionIntegrity.cpp
    ${REPO_ROOT}/src/manage/VersionIntegrity.hpp
    # 备份那一层（§4.4）：打一份包、读清单、还原。和 WorldFiles 同一个理由放进验证工程 ——
    # 「清单读得回来吗」「还原之前那份安全快照做出来没有」「解压到一半失败时旧的那一份还在不在」
    # 这三件事都能绕开界面直接钉死，而且全是真的建 zip、真的解回来。
    ${REPO_ROOT}/src/manage/BackupFiles.cpp
    ${REPO_ROOT}/src/manage/BackupFiles.hpp
    # WorldFiles：「导入世界」和存档改名那两个改盘的动作。放进来是为了绕开原生选择框
    # （测试驱动不了它）直接调函数 —— 重名挡住没有、拷一半失败会不会留半份，都在这里钉。
    ${REPO_ROOT}/src/manage/WorldFiles.cpp
    ${REPO_ROOT}/src/manage/WorldFiles.hpp
    # 「上一次启动」那本账（§5.1）：格式怎么读写、时间怎么说成人话、目标没了怎么降级、
    # 上限怎么截 —— 全是纯函数，**一条都不用起界面**就能逐格念。
    ${REPO_ROOT}/src/manage/LaunchHistory.cpp
    ${REPO_ROOT}/src/manage/LaunchHistory.hpp
    ${REPO_ROOT}/src/settings/EffectiveJavaSettings.cpp
    ${REPO_ROOT}/src/settings/EffectiveJavaSettings.hpp
    ${REPO_ROOT}/src/settings/GlobalJavaSettings.cpp
    ${REPO_ROOT}/src/settings/GameWindowSettings.cpp
    ${REPO_ROOT}/src/settings/GameWindowSettings.hpp
    ${REPO_ROOT}/src/settings/GlobalJavaSettings.hpp
    ${REPO_ROOT}/src/settings/LauncherSettings.cpp
    ${REPO_ROOT}/src/settings/LauncherSettings.hpp
    ${REPO_ROOT}/src/util/AppPaths.hpp
    ${REPO_ROOT}/src/util/SystemMemory.cpp
    # ServerPing：服务器列表那个"连上去问一句"（状态协议）。ZH 节**真起一台假服务器**
    # 连一次 —— 纯函数那半只证明"我按我写的规则拆了包"，协议那头对不对得看那一趟。
    ${REPO_ROOT}/src/util/ServerPing.cpp
    ${REPO_ROOT}/src/util/ServerPing.hpp
    # ServerListFile（2026-10-10）：servers.dat 的读（转 NbtUtil）和「只动 name / ip
    # 两格」的写。它要 NbtUtil（读）和 ZipUtil（gzip 那一层）—— 那两样都在上面。
    ${REPO_ROOT}/src/manage/ServerListFile.cpp
    ${REPO_ROOT}/src/manage/ServerListFile.hpp
    ${REPO_ROOT}/src/launcher/QuickPlay.hpp
    ${REPO_ROOT}/src/util/ZipUtil.cpp
    ${REPO_ROOT}/src/util/ZipUtil.hpp
    ${REPO_ROOT}/src/util/NbtUtil.cpp
    ${REPO_ROOT}/src/util/NbtUtil.hpp
    # NbtTree：`level.dat` 那一棵"能改的树"（世界设置那条路的判据层）。放进来是为了能
    # 拿手工拼的字节去喂它 —— "改一格别的地方一个字节不变""越界的长度当场拒绝"这些
    # 全靠断言钉死，造一个真存档来验是验不到这些角落的。
    ${REPO_ROOT}/src/util/NbtTree.cpp
    ${REPO_ROOT}/src/util/NbtTree.hpp
    # ErrorReport：日志窗底下那颗「导出错误报告…」那一份文本怎么拼（纯函数：时间由调用方
    # 传进来，所以报告头部那几行能被断言钉死）。
    ${REPO_ROOT}/src/util/ErrorReport.cpp
    ${REPO_ROOT}/src/util/ErrorReport.hpp
    # GameExitDiagnosis：游戏异常退出之后那几句话（coding plan §4.3）。**纯函数**（值进、
    # 字符串出，不看时钟、不读盘、不起进程）—— "哪条提示是从哪个字段推出来的"这件事只能
    # 在这里逐格念一遍（真机上要真崩一次才走得到，见下面 ZP 节）。
    ${REPO_ROOT}/src/diag/GameExitDiagnosis.cpp
    ${REPO_ROOT}/src/diag/GameExitDiagnosis.hpp
)

# VersionArguments.hpp 是"启动参数按 version.json 展开"那次改动引入的。这个文件在各
# 分支上不一定存在（例如只含下载修复的分支），所以存在才加进来 —— 否则 CMake 会因为找不到
# 文件直接配置失败，而不是让对应用例在运行时如实地报 FAIL（那才是想要的信号）。
if(EXISTS ${REPO_ROOT}/src/launcher/VersionArguments.hpp)
    list(APPEND PROJECT_SOURCES ${REPO_ROOT}/src/launcher/VersionArguments.hpp)
endif()

# VersionInheritance 是"带模组加载器的版本（inheritsFrom）"那次改动引入的。理由同上：
# 别的分支上没有这两个文件，存在才加，缺了就让对应用例在运行时报 FAIL。
if(EXISTS ${REPO_ROOT}/src/launcher/VersionInheritance.cpp)
    list(APPEND PROJECT_SOURCES ${REPO_ROOT}/src/launcher/VersionInheritance.cpp
                                ${REPO_ROOT}/src/launcher/VersionInheritance.hpp)
endif()

# AsciiPathAlias 是"Java 8 打不开非 ASCII 路径"那次修复引入的（真机上 `20w14∞` 和中文名的
# 1.8 整合包起不来）。理由同上：别的分支上没有这两个文件，存在才加。
if(EXISTS ${REPO_ROOT}/src/launcher/AsciiPathAlias.cpp)
    list(APPEND PROJECT_SOURCES ${REPO_ROOT}/src/launcher/AsciiPathAlias.cpp
                                ${REPO_ROOT}/src/launcher/AsciiPathAlias.hpp)
endif()

# LoaderMeta.hpp 是"Fabric 安装"那次改动引入的（纯头文件：地址怎么拼、回复怎么挑）。
# 理由同上：别的分支上没有它，存在才加。
if(EXISTS ${REPO_ROOT}/src/download/LoaderMeta.hpp)
    list(APPEND PROJECT_SOURCES ${REPO_ROOT}/src/download/LoaderMeta.hpp)
endif()

# ServerInstall 是"服务端安装链路"那次改动（2026-09-28）引入的纯函数层：八档服务端各要
# 下什么、要不要跑安装器、装完的入口是什么、启动参数怎么拼。理由同 LoaderMeta ——
# 一整个文件都是判据，离线喂数据就能钉住；它只依赖 LoaderMeta，不拖任何网络进来。
if(EXISTS ${REPO_ROOT}/src/download/ServerInstall.hpp)
    list(APPEND PROJECT_SOURCES ${REPO_ROOT}/src/download/ServerInstall.hpp)
endif()

# ResourceCatalog 是"下载页搜模组/光影/资源包/整合包"那次改动引入的（纯函数：地址怎么拼、
# 接口回的那段 JSON 怎么读）。理由同上：别的分支上没有它，存在才加。
if(EXISTS ${REPO_ROOT}/src/download/ResourceCatalog.cpp)
    list(APPEND PROJECT_SOURCES ${REPO_ROOT}/src/download/ResourceCatalog.cpp
                                ${REPO_ROOT}/src/download/ResourceCatalog.hpp)
endif()

# ModDependencies 是"前置模组一键下载 + 实例更新检测"那次改动（2026-09-28）引入的纯函数层：
# 缺哪些前置 / 去哪问 / 挑哪一版 / 盘上这一条是不是最新。理由同 ResourceCatalog ——
# 一整个文件都是判据，离线喂数据就能钉住（这一节里的是 R1/R2 两带）。
# **它只依赖 ResourceCatalog**（不含 QNetwork 那一套），所以放在这里不会把网络拖进来。
if(EXISTS ${REPO_ROOT}/src/download/ModDependencies.cpp)
    list(APPEND PROJECT_SOURCES ${REPO_ROOT}/src/download/ModDependencies.cpp
                                ${REPO_ROOT}/src/download/ModDependencies.hpp)
endif()

# ResourceStore 是"下载页取数 + 图标缓存"那一层。这里**只链、不构造**：它一构造就建
# QNetworkAccessManager，而这一节验的全是纯函数/文件层（缓存路径怎么拼、过期怎么判、
# 存了读得回来不回来、哪几种失败算"主机级"）。图标那条网络路仍然只在 mw_check 的
# resource-live 里真跑（那一条要联外网）。
if(EXISTS ${REPO_ROOT}/src/download/ResourceStore.cpp)
    list(APPEND PROJECT_SOURCES ${REPO_ROOT}/src/download/ResourceStore.cpp
                                ${REPO_ROOT}/src/download/ResourceStore.hpp)
endif()

# ImageDecode 是"列表图标走系统 WIC 解 webp"那次改动引入的（Qt 先试，Qt 认不得的交给
# WIC）。这条能离线验：喂一段真 webp 字节进去看解不解得出来（webpAvailable() 是"这台机器
# 现在能不能解"，不是常量，所以那条断言要按它分支，见 main.cpp 的 ZC 段）。
# 理由同上：别的分支上没有它，存在才加。
if(EXISTS ${REPO_ROOT}/src/util/ImageDecode.cpp)
    list(APPEND PROJECT_SOURCES ${REPO_ROOT}/src/util/ImageDecode.cpp
                                ${REPO_ROOT}/src/util/ImageDecode.hpp)
endif()

# 任务账本那一套（2026-10-12，coding plan §2 / §3）。它**一个 QWidget 都不碰、一个网络都
# 不开**，所以 verify 这一节能直接 new 一个、喂几条假任务进去，把那几条纯规则钉死：
# 「终态只落一次」「号从盘上续」「没跑完的读回来算已中断」。
# 理由同上：别的分支上没有它，存在才加。
if(EXISTS ${REPO_ROOT}/src/tasks/TaskManager.cpp)
    list(APPEND PROJECT_SOURCES ${REPO_ROOT}/src/tasks/TaskManager.cpp
                                ${REPO_ROOT}/src/tasks/TaskManager.hpp
                                ${REPO_ROOT}/src/tasks/TaskHistory.cpp
                                ${REPO_ROOT}/src/tasks/TaskHistory.hpp
                                ${REPO_ROOT}/src/tasks/TaskSnapshot.hpp
                                ${REPO_ROOT}/src/tasks/TaskId.hpp)
endif()

# ThemeImage 是设置页「背景图」那两行收图的那一层（2026-09-28，用户点名要的"能换背景图"）。
# 它能离线验：V5s 那一段在一个临时数据根目录里真存真删，只看盘上少了哪一个文件。
# 理由同上：别的分支上没有它，存在才加。
if(EXISTS ${REPO_ROOT}/src/util/ThemeImage.cpp)
    list(APPEND PROJECT_SOURCES ${REPO_ROOT}/src/util/ThemeImage.cpp
                                ${REPO_ROOT}/src/util/ThemeImage.hpp)
endif()

add_executable(verify main.cpp ${PROJECT_SOURCES})

target_include_directories(verify PRIVATE ${REPO_ROOT}/src)
target_link_libraries(verify PRIVATE Qt6::Core Qt6::Network Qt6::Gui)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(verify PRIVATE -O0)
endif()

# TokenStore 的 DPAPI（CryptProtectData）在这个库里；Gui 是 AuthManager.cpp 里的
# QDesktopServices（verify 自己不构造 AuthManager，只是把它的一个静态函数链进来）。
target_link_libraries(verify PRIVATE crypt32)

# ImageDecode.cpp 那三个（见主工程 CMakeLists.txt 里同一段）：ole32 起 COM、shlwapi 要
# SHCreateMemStream、windowscodecs 是 WIC 本体。MinGW 上这一句不加的话，链接期报的是一串
# `undefined reference to IID_IWICImagingFactory` 之类 —— 不是运行期，别指望跑起来才发现。
if(WIN32)
    target_link_libraries(verify PRIVATE ole32 shlwapi windowscodecs)
endif()

# 验证代码按仓库根定位 tests/meta。
target_compile_definitions(verify PRIVATE REPO_ROOT_PATH="${REPO_ROOT}")

add_test(NAME wtfml.core COMMAND verify)
set_tests_properties(wtfml.core PROPERTIES
    LABELS "core"
    TIMEOUT 300
)

# Windows resolves libstdc++ from PATH.  Developers often have another MinGW
# installed globally; loading that runtime beside a Qt built with MinGW 13 can
# crash before a test reaches main().  Prepend the runtime that belongs to the
# compiler CMake actually selected, and the directory containing Qt6Core.dll.
get_filename_component(WTFML_TEST_COMPILER_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
get_target_property(WTFML_TEST_QT_CORE Qt6::Core IMPORTED_LOCATION_RELEASE)
if(NOT WTFML_TEST_QT_CORE)
    get_target_property(WTFML_TEST_QT_CORE Qt6::Core IMPORTED_LOCATION)
endif()
get_filename_component(WTFML_TEST_QT_BIN "${WTFML_TEST_QT_CORE}" DIRECTORY)
set(WTFML_TEST_PATH "${WTFML_TEST_COMPILER_BIN};${WTFML_TEST_QT_BIN};$ENV{PATH}")
string(REPLACE ";" "\\;" WTFML_TEST_PATH "${WTFML_TEST_PATH}")
set_property(TEST wtfml.core APPEND PROPERTY
    ENVIRONMENT "PATH=${WTFML_TEST_PATH}"
)
set(WTFML_TEST_RUNTIME_ENVIRONMENT "PATH=${WTFML_TEST_PATH}")

add_subdirectory(ui_check)
add_subdirectory(mw_check)

