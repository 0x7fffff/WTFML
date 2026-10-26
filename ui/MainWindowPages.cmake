# Page-specific implementations are shared by product and both UI test layers.
# Keep one list so adding a page cannot silently omit a verification target.
set(WTFML_UI_PAGE_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/MainWindowPageLayout.cpp
    ${CMAKE_CURRENT_LIST_DIR}/MainWindowDownloadPage.cpp
    ${CMAKE_CURRENT_LIST_DIR}/MainWindowSettingsPage.cpp
    ${CMAKE_CURRENT_LIST_DIR}/MainWindowPanels.cpp
    ${CMAKE_CURRENT_LIST_DIR}/InstanceSettingsLifecycle.cpp
    ${CMAKE_CURRENT_LIST_DIR}/InstanceSettingsServers.cpp
)
