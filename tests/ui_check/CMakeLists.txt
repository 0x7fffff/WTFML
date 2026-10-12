# UI 回归验证：offscreen 起一个 QMainWindow，断言布局几何和控件行为。

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
# 2026-09-28：assets/icons.qrc（四个加载器的真图）也要走 RCC。产品那份 CMakeLists
# 早就是开的，这三个验证工程原来只开了 AUTOMOC —— 不开的话 .qrc 会被**静默忽略**：
# 编译链接全都过，只是 `:/assets/loader/*.png` 全是空图（症状见下面那行的注释）。
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
get_filename_component(REPO_ROOT ${REPO_ROOT} REALPATH)

# GroupDialog.cpp 也编进来：它和 MainWindowUI 同一个层次（纯界面、不依赖 src/），
# 而且 MainWindowUI 里那条"组在界面上写成什么"的规则就是从这个类取的。
add_executable(ui_check main.cpp
                                # Z11 那两条原生消息的契约断言要拼 MSG：单独一个 TU，
                                # 免得 main.cpp 去 include <windows.h>（理由见那个文件头）。
                                chrome_messages.cpp
                                ${REPO_ROOT}/ui/AccountSkinDialog.cpp
                                ${REPO_ROOT}/ui/MainWindowUI.cpp
                                # 「最大内存」那一行那根条（2026-09-26）：MainWindowUI.cpp 和
                                # InstanceSettingsDialog.cpp 都直接 new 它，少了这一个文件会
                                # 在链接那一步报一串 undefined reference to MemorySlider::…
                                ${REPO_ROOT}/ui/MemorySlider.cpp
                                ${REPO_ROOT}/ui/GroupDialog.cpp
                                ${REPO_ROOT}/ui/GroupListDelegate.cpp
                                # 内容区覆盖层 + 内嵌提示面板（2026-09-25）：设置界面和所有提示
                                # 都开在这一层里，不再另开独立窗口。它们是纯界面（不认识业务），
                                # 所以归这一层验。
                                ${REPO_ROOT}/ui/OverlayHost.cpp
                                ${REPO_ROOT}/ui/InlineMessage.cpp
                                ${REPO_ROOT}/ui/InstanceSettingsDialog.cpp
    ${REPO_ROOT}/ui/InstanceFilesPage.cpp
    # 「配置文件」那一页（2026-09-27）：编辑器那一格和它那套纯函数（认 BOM / 认行尾 /
    # 认是不是 UTF-8）都是 ui/ 的东西，InstanceSettingsDialog 直接 new 它。少了这两个
    # 文件，链接期报的是 `undefined reference to ConfigFileEditorPage::…`。
    ${REPO_ROOT}/ui/ConfigFileText.cpp
    ${REPO_ROOT}/ui/ConfigFileEditorPage.cpp
    # 「条目」那一格（2026-09-27）：一条 `键 = 值` 是怎么从正文里认出来的（键路径、类型、
    # 值在哪几个字符上）—— ConfigFileEditorPage 直接调它，所以这一份也得编进来。
    ${REPO_ROOT}/ui/ConfigEntries.cpp
    # 「世界设置」那一页（2026-09-28）：和「配置文件」那一页同一个来路 ——
    # InstanceSettingsDialog 直接 new 它，少了这一个文件链接期报一串
    # `undefined reference to WorldSettingsPage::…`。
    ${REPO_ROOT}/ui/WorldSettingsPage.cpp
    # 规则那一格名字上盖的官方中文（2026-09-29）：WorldSettingsPage 直接调它。
    ${REPO_ROOT}/ui/GameRuleNames.cpp

                                ${REPO_ROOT}/ui/JavaMismatchDialog.cpp
                                ${REPO_ROOT}/ui/JavaManageDialog.cpp
                                ${REPO_ROOT}/ui/SkinModel.cpp
                                ${REPO_ROOT}/ui/VersionIcons.cpp
                                # 四个加载器的标志那几张真图（2026-09-28）：qrc 不列在这儿，
                                # `:/assets/loader/*.png` 就是空图，loaderMark() 一路退回草方块
                                # （ui_check 那条「四张标志互不相同」会红，产品自己看着一切正常）。
                                ${REPO_ROOT}/assets/icons.qrc
                                # 「启用 / 禁用」那颗开关（2026-09-27）和 MOTD 那套配色
                                # （MotdText 被 VersionTreeDelegate 引到）。
                                ${REPO_ROOT}/ui/ToggleSwitch.cpp
                                ${REPO_ROOT}/ui/MotdText.cpp
                                ${REPO_ROOT}/ui/VersionTreeDelegate.cpp
                                ${REPO_ROOT}/ui/ResourceDetailPage.cpp
                                ${REPO_ROOT}/ui/InstanceGroupDelegate.cpp
                                # 下载页那四页的"点的是哪一类"和"去问哪一类"是靠一个字符串对
                                # 起来的（MainWindowUI::downloadCategoryKeys() ==
                                # ResourceCatalog::kindKey()）。这条等号只有把两边都编进同一个
                                # 可执行文件里才钉得住 —— 主验证工程不编 UI，钉不了。
                                # ResourceStore 不在其列：那是异步 I/O，还要一个真的
                                # QGuiApplication 才画得出图标。
                                ${REPO_ROOT}/src/download/ResourceCatalog.cpp
                                # 前置模组 / 更新检测那两套判据的**字**（2026-09-28）。这一层
                                # 只读它那几个 *Text()（"说不清"这三个字的唯一出处）——
                                # 界面上的 tooltip 和它必须是同一句话，这一条两边都在同一个
                                # 可执行文件里才钉得住。它自己只依赖 ResourceCatalog，所以
                                # 跟着那一份进来就行。
                                ${REPO_ROOT}/src/download/ModDependencies.cpp
                                # ModpackImport 是**因为 ResourceCatalog 才进来的**（2026-09-25）：
                                # 加了 CurseForge 这个来源之后，它拼接口地址要问
                                # ModpackImport::curseForgeApiBase()（那条社区反代地址的唯一
                                # 出处），于是这一层的"纯函数"这话不再成立 —— 少了这两个文件
                                # 链接就过不去，而且报的是 `undefined reference to
                                # ModpackImport::curseForgeApiBase` 这种看着跟界面毫无关系的
                                # 一行。InstanceFiles 是 ModpackImport 自己要的（和 mw_check /
                                # ui_shot 那两份清单一致）。
                                ${REPO_ROOT}/src/manage/ModpackImport.cpp
                                ${REPO_ROOT}/src/manage/ModpackImport.hpp
                                ${REPO_ROOT}/src/manage/InstanceFiles.cpp
                                # 「游戏日志」窗（2026-09-24）。它是纯界面（不认识"会话"，
                                # 见它的文件头），所以归这一层验最合适：给它几行字，看它
                                # 写没写上去、跟不跟到底、清空/关闭两颗钮管不管用。
                                ${REPO_ROOT}/ui/GameLogWindow.cpp
                                ${REPO_ROOT}/ui/SkinViewer.cpp
                                ${REPO_ROOT}/ui/TitleBar.cpp
                                ${REPO_ROOT}/ui/Theme.cpp
                                # 动效那一层（2026-09-29）：按钮按下 / 换页 / 展开折叠。
                                # MainWindowUI.cpp 和 OverlayHost.cpp 都直接调它。
                                ${REPO_ROOT}/ui/Animations.cpp
                                # 「先铺底色、再铺一张图」的容器（2026-09-28）：四个页面和主页
                                # 左栏现在直接 new 它，少了这一个文件会在链接那一步报
                                # `undefined reference to ThemeBackground::ThemeBackground(…)`。
                                ${REPO_ROOT}/ui/ThemeBackground.cpp
                                ${REPO_ROOT}/ui/WindowChrome.cpp)

# InstanceFiles 那一层要的（`VersionInheritance::parentIdOf` / `maxDepth`）——
# 少了它，链接期报的是 `undefined reference to VersionInheritance::…`。和 mw_check /
# ui_shot 那两份清单同一个形状：**有才加**，别的分支上没有这个文件时这一句不生效。
if(EXISTS ${REPO_ROOT}/src/launcher/VersionInheritance.cpp)
    target_sources(ui_check PRIVATE ${REPO_ROOT}/src/launcher/VersionInheritance.cpp
                                    ${REPO_ROOT}/src/launcher/VersionInheritance.hpp)
endif()

# 给 Java 8 换 ASCII 路径那一套（2026-09-28）。和上面 VersionInheritance 同一个形状：
# **有才加**。少了它，链接期报的是 `undefined reference to AsciiPathAlias::…` ——
# 而它自己又要 ZipUtil（读 jar 里的 class 文件头）。
if(EXISTS ${REPO_ROOT}/src/launcher/AsciiPathAlias.cpp)
    target_sources(ui_check PRIVATE ${REPO_ROOT}/src/launcher/AsciiPathAlias.cpp
                                    ${REPO_ROOT}/src/launcher/AsciiPathAlias.hpp)
endif()
# ZipUtil 是 InstanceFiles.cpp 要的（2026-09-28 起：`mainClassJavaMajor()` 要读 jar 里
# 那个 class 文件的头 6、7 字节来判这个主类编给哪个 Java）。少了它链接期报的是
# `undefined reference to ZipUtil::readEntry(QString const&, QString const&, …)` ——
# 一行看着和界面毫无关系的错。
if(EXISTS ${REPO_ROOT}/src/util/ZipUtil.cpp)
    target_sources(ui_check PRIVATE ${REPO_ROOT}/src/util/ZipUtil.cpp
                                    ${REPO_ROOT}/src/util/ZipUtil.hpp)
endif()

target_include_directories(ui_check PRIVATE ${REPO_ROOT}/src ${REPO_ROOT}/ui)
target_link_libraries(ui_check PRIVATE Qt6::Widgets)
# This assertion-heavy translation unit is intentionally large. Optimizing it
# does not make the checks more representative, but adds several minutes to a
# clean MinGW CI build.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(ui_check PRIVATE -O0)
endif()
# dwmapi 是 ui/WindowChrome.cpp 要的（DwmSetWindowAttribute，把圆角明说一遍），
# 见主工程 CMakeLists.txt 里同一段。少了它会在链接那一步报 undefined reference。
target_link_libraries(ui_check PRIVATE dwmapi)

add_test(NAME wtfml.ui COMMAND ui_check)
set_tests_properties(wtfml.ui PROPERTIES
    LABELS "ui;visual"
    TIMEOUT 180
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;${WTFML_TEST_RUNTIME_ENVIRONMENT}"
)

