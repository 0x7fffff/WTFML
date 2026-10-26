# 并发下载基准（测量专用）：只编下载引擎那几个 .cpp，与产品 exe 共用同一份实现。
#
# 为什么不链产品主目标：主目标把整个界面、控制器、Widgets 都带进来了，而这里只需要
# QCoreApplication + 网络栈；少一层依赖，就能在随便一台机器上用 Release 重跑。
#
# 这个目录**不进 ctest**：它是量耗时的，不是断言。跑法与结论见 docs/PERFORMANCE.md。
add_library(wtfml_perf_download_core STATIC
    ../../src/download/DownloadManager.cpp ../../src/download/DownloadManager.hpp
    ../../src/download/LocalFileReuse.cpp ../../src/download/LocalFileReuse.hpp
    ../../src/download/ResumableDownload.cpp ../../src/download/ResumableDownload.hpp
    ../../src/util/DownloadDiagnostics.cpp)
target_include_directories(wtfml_perf_download_core PUBLIC ../../src)
target_link_libraries(wtfml_perf_download_core PUBLIC Qt6::Core Qt6::Network Qt6::Gui)
set_target_properties(wtfml_perf_download_core PROPERTIES AUTOMOC ON)

add_executable(WTFMLPerfDownload main.cpp HttpFileServer.cpp HttpFileServer.hpp)
target_link_libraries(WTFMLPerfDownload PRIVATE wtfml_perf_download_core Qt6::Core Qt6::Network)
if(MINGW)
    target_compile_options(WTFMLPerfDownload PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(wtfml_perf_download_core PRIVATE -Wall -Wextra)
endif()
