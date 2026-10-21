add_library(wtfml_network_diag_core STATIC
    ../../src/download/DownloadManager.cpp ../../src/download/DownloadManager.hpp
    # 「从别的游戏文件夹里借文件」：DownloadManager 会调它。
    ../../src/download/LocalFileReuse.cpp ../../src/download/LocalFileReuse.hpp
    ../../src/download/ResumableDownload.cpp ../../src/download/ResumableDownload.hpp
    ../../src/util/DownloadDiagnostics.cpp)
target_include_directories(wtfml_network_diag_core PUBLIC ../../src)
target_link_libraries(wtfml_network_diag_core PUBLIC Qt6::Core Qt6::Network Qt6::Gui)
set_target_properties(wtfml_network_diag_core PROPERTIES AUTOMOC ON)
add_executable(WTFMLNetworkDiag WIN32 main.cpp NetworkMonitor.hpp)
target_link_libraries(WTFMLNetworkDiag PRIVATE wtfml_network_diag_core Qt6::Widgets)
if(MINGW)
    target_compile_options(WTFMLNetworkDiag PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
if(WTFML_BUILD_TESTS)
    add_test(NAME wtfml.network-diag COMMAND WTFMLNetworkDiag --self-test)
    set_tests_properties(wtfml.network-diag PROPERTIES TIMEOUT 45 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "network;diagnostics;ci-app")
endif()
