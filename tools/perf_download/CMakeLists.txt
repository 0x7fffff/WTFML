# 并发下载基准（测量专用）：只编下载引擎那几个 .cpp，与产品 exe 共用同一份实现。
#
# 为什么不链产品主目标：主目标把整个界面、控制器、Widgets 都带进来了，而这里只需要
# QCoreApplication + 网络栈；少一层依赖，就能在随便一台机器上用 Release 重跑。
#
# 这个目录**不进 ctest**：它是量耗时的，不是断言。跑法与结论见 docs/dev/PERFORMANCE.md。
add_library(wtfml_perf_download_core STATIC
    ../../src/download/DownloadManager.cpp ../../src/download/DownloadManager.hpp
    # 下载引擎按状态所有权拆出去的那几个翻译单元：DownloadManager.cpp 会直接调它们，
    # 少一个就在链接期报一串 undefined reference（和 tests/CMakeLists.txt 同一份清单）。
    ../../src/download/DownloadManagerRecovery.cpp
    ../../src/download/DownloadManagerLocalReuse.cpp
    ../../src/download/DownloadManagerMirror.cpp
    ../../src/download/DownloadManagerThrottle.cpp
    ../../src/download/DownloadManagerSegmented.cpp
    ../../src/download/DownloadManagerMetadata.cpp
    ../../src/download/LocalFileReuse.cpp ../../src/download/LocalFileReuse.hpp
    ../../src/download/ResumableDownload.cpp ../../src/download/ResumableDownload.hpp
    # 恢复记录（DownloadCheckpoint）用的是 TokenStore 那层字节保护，路径校验落在
    # InstanceFiles 里。和 tools/network_diag 一样把这两块带上，否则链接期就断在
    # `TokenStore::protect` / `InstanceFiles::validatePathSegment`。
    ../../src/auth/TokenStore.cpp
    ../../src/manage/InstanceFiles.cpp
    ../../src/launcher/VersionInheritance.cpp
    ../../src/util/ZipUtil.cpp
    ../../src/util/DownloadDiagnostics.cpp)
target_include_directories(wtfml_perf_download_core PUBLIC ../../src)
target_link_libraries(wtfml_perf_download_core PUBLIC Qt6::Core Qt6::Network Qt6::Gui)
if(WIN32)
    target_link_libraries(wtfml_perf_download_core PRIVATE crypt32)
endif()
set_target_properties(wtfml_perf_download_core PROPERTIES AUTOMOC ON)

add_executable(WTFMLPerfDownload main.cpp HttpFileServer.cpp HttpFileServer.hpp)
target_link_libraries(WTFMLPerfDownload PRIVATE wtfml_perf_download_core Qt6::Core Qt6::Network)
if(MINGW)
    target_compile_options(WTFMLPerfDownload PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(wtfml_perf_download_core PRIVATE -Wall -Wextra)
endif()
