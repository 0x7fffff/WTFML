cmake_minimum_required(VERSION 3.16)
project(WTFMLLauncher VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/Zc:__cplusplus)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Core Gui)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS ui)

set(SOURCE_FILES
    src/main.cpp
    src/MainWindow.cpp
    src/launcher/LauncherManager.cpp
    src/auth/AuthManager.cpp
    src/manage/FolderManager.cpp
    src/manage/VersionManager.cpp
    src/settings/GlobalJavaSettings.cpp
    src/download/DownloadManager.cpp
    src/download/IntegrityChecker.cpp
)

qt_add_executable(WTFMLLauncher
    ${SOURCE_FILES}
    ui/MainWindow.ui
)

target_include_directories(WTFMLLauncher PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(WTFMLLauncher PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Core
    Qt6::Gui
)
