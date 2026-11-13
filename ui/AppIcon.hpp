/**
 * @file AppIcon.hpp
 * @brief 应用图标：给的是窗口 / 任务栏 / Alt+Tab 用的那一份（自绘标题栏不挂这份 QIcon）。
 *
 * 三处图标不是同一份来源，改的时候别只改一处：
 *   * **exe 文件本身**（资源管理器、固定到任务栏）用 `assets/app/wtfml.ico`，
 *     由 `res/wtfml.rc` 编成资源、ID 1；
 *   * **运行中的窗口**（主窗口、各独立窗口、任务栏、Alt+Tab）用 qrc 里那四张 PNG，
 *     `QApplication::setWindowIcon()` 一次铺给所有顶层窗口；
 *   * 自绘标题栏 `ui/TitleBar` 不挂窗口图标；导航轨左上角的品牌标画的是 `wtfml-24.svg`
 *     那条 W 路径，跟主题着色（暮色雪山用原色），和这份 QIcon 无关。
 *
 * 运行时为什么不直接读那份 .ico：QIcon 读 .ico 要挂 imageformats 插件，而四个尺寸
 * 各一张 PNG 就是 `addFile(size)` 四行，系统要哪一档就给哪一档原图，不用缩放。
 */

#pragma once

#include <QIcon>
#include <QSize>
#include <QString>

/// 运行时的应用图标（16/32/48/256 四档 PNG 都在 `assets/icons.qrc` 里）。
/// @note `Q_INIT_RESOURCE` 不能写在命名空间里（见 `ui/Glass.cpp` 同一段），
///       所以这个函数是全局的，不是某个 namespace 的成员。
/// @note 缺一档时 QIcon 会从最近的一档缩放、不报错；`ui_check --bootstrap-check`
///       把四档资源在不在、`availableSizes()` 齐不齐钉住。
inline QIcon wtfmlWindowIcon() {
    Q_INIT_RESOURCE(icons);
    QIcon icon;
    for (const int size : {16, 32, 48, 256}) {
        icon.addFile(QStringLiteral(":/assets/app/wtfml-%1.png").arg(size), QSize(size, size));
    }
    return icon;
}
