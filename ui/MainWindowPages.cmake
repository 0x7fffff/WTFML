# Page-specific implementations are shared by product and both UI test layers.
# Keep one list so adding a page cannot silently omit a verification target.
set(WTFML_UI_PAGE_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/PresetModReview.cpp
    ${CMAKE_CURRENT_LIST_DIR}/MainWindowPageLayout.cpp
    ${CMAKE_CURRENT_LIST_DIR}/MainWindowDownloadPage.cpp
    ${CMAKE_CURRENT_LIST_DIR}/MainWindowSettingsPage.cpp
    ${CMAKE_CURRENT_LIST_DIR}/SettingsPageParts.cpp
    ${CMAKE_CURRENT_LIST_DIR}/MainWindowPanels.cpp
    ${CMAKE_CURRENT_LIST_DIR}/InstanceSettingsLifecycle.cpp
    ${CMAKE_CURRENT_LIST_DIR}/InstanceSettingsServers.cpp
    # 「实例设置」按"一页一摊"分的三份（同一个类的实现，共用 InstanceSettingsDialogInternals.hpp）
    ${CMAKE_CURRENT_LIST_DIR}/InstanceSettingsOverviewPage.cpp
    ${CMAKE_CURRENT_LIST_DIR}/InstanceSettingsLaunchPage.cpp
    ${CMAKE_CURRENT_LIST_DIR}/InstanceSettingsFilePages.cpp
)
