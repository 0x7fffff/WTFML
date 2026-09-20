cmake_minimum_required(VERSION 3.16)
project(WTFMinecraftLauncher VERSION 0.1 LANGUAGES CXX)

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
    src/auth/AuthManager.hpp
    src/manage/FolderManager.hpp
    src/manage/VersionManager.hpp
    src/manage/VersionDisplay.hpp
    src/settings/GlobalJavaSettings.hpp
    src/settings/LauncherSettings.hpp
    src/download/DownloadManager.hpp
    src/util/AppPaths.hpp
    src/util/SystemMemory.hpp
    src/util/ZipUtil.hpp
)

set(SOURCE_FILES
    src/main.cpp
    src/MainWindow.cpp
    src/launcher/LauncherManager.cpp
    src/auth/AuthManager.cpp
    src/manage/FolderManager.cpp
    src/manage/VersionManager.cpp
    src/manage/VersionDisplay.cpp
    src/settings/GlobalJavaSettings.cpp
    src/settings/LauncherSettings.cpp
    src/download/DownloadManager.cpp
    src/util/SystemMemory.cpp
    src/util/ZipUtil.cpp
    ui/MainWindowUI.cpp
    ${HEADER_FILES}
)

qt_add_executable(WTFMinecraftLauncher
    ${SOURCE_FILES}
)

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

# 令牌落盘用的 DPAPI（CryptProtectData）在这个库里。见 src/auth/AuthManager.cpp 的 protectToken()。
if(WIN32)
    target_link_libraries(WTFMinecraftLauncher PRIVATE crypt32)
endif()