#include "Support.hpp"
#include "MainWindowPageLayout.hpp"

#include <QMainWindow>
#include <QScreen>

// 155：主窗口默认 1024×620、最小 900×585、每次启动导航收起、记住窗口大小（界面这一侧：夹值、
// 最大化时取还原后的大小、设置·外观「窗口大小 · 恢复默认」的置灰与效果）。落盘那一半在
// core_modules/LaunchOptionsTests.cpp 的 ZD2b。
int UiWindowSizeCheck(QApplication &app) {
    using namespace MainWindowPageLayout;
    const auto pump = [&app] {
        app.processEvents();
        app.processEvents();
    };
    const QSize def(kMainWindowDefaultWidth, kMainWindowDefaultHeight);
    const QSize min(kMainWindowMinWidth, kMainWindowMinHeight);
    check(def == QSize(1024, 620) && min == QSize(900, 585), "★ 155 默认 1024×620、最小 900×585",
          QStringLiteral("%1x%2 / %3x%4").arg(def.width()).arg(def.height()).arg(min.width()).arg(min.height()));

    // ---- 夹值：最小尺寸 ≤ 结果 ≤ 屏幕可用区；可用区比最小还小时最小尺寸说了算 ----
    const QSize big(1920, 1040);
    check(MainWindowUI::fittedWindowSize(QSize(1100, 700), big) == QSize(1100, 700), "155 屏幕放得下：记的大小原样用");
    check(MainWindowUI::fittedWindowSize(QSize(300, 200), big) == min, "★ 155 记的值比最小还小：夹到最小尺寸");
    check(MainWindowUI::fittedWindowSize(QSize(5000, 3000), big) == big, "★ 155 记的值比屏幕可用区大：夹到可用区",
          QStringLiteral("%1x%2").arg(MainWindowUI::fittedWindowSize(QSize(5000, 3000), big).width())
              .arg(MainWindowUI::fittedWindowSize(QSize(5000, 3000), big).height()));
    check(MainWindowUI::fittedWindowSize(QSize(1300, 300), big) == QSize(1300, 585), "155 宽高各夹各的");
    check(MainWindowUI::fittedWindowSize(def, QSize(800, 600)) == QSize(900, 600),
          "155 屏幕比最小尺寸还窄：不缩到最小值以下");

    QMainWindow window;
    MainWindowUI ui(&window);
    const QSize avail = window.screen()->availableGeometry().size();
    const QSize fittedDefault = MainWindowUI::fittedWindowSize(def, avail);
    check(window.size() == fittedDefault && window.minimumSize() == min, "★ 155 主窗口按默认大小开、最小尺寸已设",
          QStringLiteral("%1x%2（可用区 %3x%4）").arg(window.width()).arg(window.height()).arg(avail.width()).arg(avail.height()));
    check(ui.titleBar()->isCollapsed() && ui.titleBar()->width() == TitleBar::collapsedRailWidth(),
          "★ 155 默认启动导航收起", QString::number(ui.titleBar()->width()));

    MainWindowUI::restoreWindowSize(&window, QSize());
    check(window.size() == fittedDefault, "155 没记过大小：按默认开");
    MainWindowUI::restoreWindowSize(&window, QSize(100, 100));
    check(window.size() == min, "155 记的值过小：启动时夹到最小尺寸");
    MainWindowUI::restoreWindowSize(&window, QSize(20000, 20000));
    check(window.size() == avail.expandedTo(min), "155 记的值过大：启动时夹到屏幕可用区",
          QStringLiteral("%1x%2").arg(window.width()).arg(window.height()));

    // ---- 窗口大小一变就通知；最大化时记还原后的大小 ----
    int notified = 0;
    ui.setWindowSizeHandler([&notified] { ++notified; });
    window.show();
    pump();
    const QSize custom = QSize(min.width() + 37, min.height() + 11);
    window.resize(custom);
    pump();
    check(notified > 0 && MainWindowUI::rememberedWindowSize(&window) == custom, "★ 155 改大小后通知存盘，记的是当前大小",
          QStringLiteral("通知 %1 次，记 %2x%3").arg(notified).arg(MainWindowUI::rememberedWindowSize(&window).width())
              .arg(MainWindowUI::rememberedWindowSize(&window).height()));
    QPushButton *reset = ui.getWindowSizeResetButton();
    check(reset != nullptr && reset->objectName() == MainWindowUI::windowSizeResetButtonName() &&
              reset->text() == QStringLiteral("恢复默认"),
          "155 设置·外观有「窗口大小 · 恢复默认」");
    if (reset == nullptr) return 0;
    check(reset->isEnabled(), "★ 155 不是默认大小时「恢复默认」可点");

    notified = 0;
    window.showMaximized();
    pump();
    check(window.isMaximized() && notified > 0 && MainWindowUI::rememberedWindowSize(&window) == custom,
          "★ 155 最大化时记的是还原后的大小（不是最大化的大小）",
          QStringLiteral("最大化 %1，窗 %2x%3，记 %4x%5").arg(window.isMaximized()).arg(window.width()).arg(window.height())
              .arg(MainWindowUI::rememberedWindowSize(&window).width()).arg(MainWindowUI::rememberedWindowSize(&window).height()));
    check(reset->isEnabled(), "155 最大化时「恢复默认」可点");

    // ---- 恢复默认：先还原，再回默认大小、在当前屏幕可用区居中，按钮置灰 ----
    MainWindowUI::resetWindowSize(&window);
    pump();
    const QRect area = window.screen()->availableGeometry();
    const QPoint center = window.frameGeometry().center();
    check(!window.isMaximized() && window.size() == fittedDefault, "★ 155 恢复默认：还原最大化并回到默认大小",
          QStringLiteral("最大化 %1，%2x%3").arg(window.isMaximized()).arg(window.width()).arg(window.height()));
    // offscreen 有系统窗框，边距要等平台回报才准（产品窗口是自绘窗框，没有这层）：容差放到窗框宽。
    const int slack = qMax(window.frameGeometry().width() - window.width(),
                           window.frameGeometry().height() - window.height()) + 1;
    check(qAbs(center.x() - area.center().x()) <= slack && qAbs(center.y() - area.center().y()) <= slack,
          "155 恢复默认后在当前屏幕可用区居中",
          QStringLiteral("窗中心 %1,%2 / 可用区中心 %3,%4（容差 %5）").arg(center.x()).arg(center.y())
              .arg(area.center().x()).arg(area.center().y()).arg(slack));
    check(MainWindowUI::isDefaultWindowSize(&window) && !reset->isEnabled(), "★ 155 已是默认大小：「恢复默认」置灰");
    window.resize(window.width() + 20, window.height());
    pump();
    check(reset->isEnabled(), "★ 155 窗口大小一变，「恢复默认」跟着变回可点");
    MainWindowUI::resetWindowSize(&window);
    pump();
    check(!reset->isEnabled(), "155 再点恢复默认又置灰");
    window.close();
    return 0;
}
