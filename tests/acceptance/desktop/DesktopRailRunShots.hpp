#pragma once
// 导航轨运行状态入口（#85）真窗口样张：默认窗口 1024×620，深色 / 翠绿各拍
// 图标轨空闲、图标轨一个进程、图标轨三个进程加面板、展开轨空闲、展开轨三个进程、任务中心。
// 进程行是写进界面的演示数据。开发工具，不进启动器。
#include "Animations.hpp"
#include "ActivityPopover.hpp"
#include "DesktopDesignShots.hpp"
#include "MainWindow.hpp"
#include "MainWindowPageLayout.hpp"
#include "MainWindowUI.hpp"
#include "TitleBar.hpp"
#include "Theme.hpp"
#include "VersionIcons.hpp"

#include <QGuiApplication>
#include <QScreen>

namespace RailRunShots {

inline QList<MainWindowUI::PanelRow> demoRows(int count) {
    const QColor ink = Theme::tokens().text;
    const int iconSize = MainWindowUI::bottomBarPanelActionIconSize();
    const auto row = [&](const QString &key, const QString &title, const QString &subtitle, const QString &block) {
        MainWindowUI::PanelRow r;
        r.key = key;
        r.icon = VersionIcons::isoBlock(VersionIcons::isoBlockKindFor(block), MainWindowUI::instanceTreeIconSize() * 2);
        r.title = title;
        r.subtitle = subtitle;
        MainWindowUI::PanelAction stop, settings, log;
        stop.icon = VersionIcons::power(iconSize, ink);
        stop.tooltip = QStringLiteral("结束");
        settings.icon = VersionIcons::gear(iconSize, Theme::tokens().primary);
        settings.tooltip = QStringLiteral("设置");
        log.icon = VersionIcons::line(QStringLiteral("log"), iconSize, ink);
        log.tooltip = QStringLiteral("日志");
        r.actions = {stop, settings, log};
        return r;
    };
    const QList<MainWindowUI::PanelRow> all{
        row(QStringLiteral("3"), QStringLiteral("1.20.1-Forge 47.2.0"), QStringLiteral("本次 4 分钟 · 累计 3 小时 12 分钟"), QStringLiteral("1.20.1-Forge")),
        row(QStringLiteral("2"), QStringLiteral("1.20.1-Forge 47.2.0"), QStringLiteral("本次 21 分钟 · 累计 3 小时 12 分钟"), QStringLiteral("1.20.1-Forge")),
        row(QStringLiteral("server:1"), QStringLiteral("生存服务端"), QStringLiteral("服务端 · 1 小时 5 分钟"), QStringLiteral("1.20.4"))};
    return all.mid(0, count);
}

inline void run(QApplication &app, MainWindow &window, const QString &root) {
    static QStringList themes{QStringLiteral("dark"), QStringLiteral("emerald")};
    static int index = 0;
    static std::function<void()> step;
    step = [&app, &window, root]() {
        if (index >= themes.size()) {
            app.exit(0);
            return;
        }
        const QString theme = themes.at(index++);
        Theme::setSpec(Theme::presetSpec(theme));
        Theme::apply(app);
        window.showNormal();
        window.resize(MainWindowPageLayout::kMainWindowDefaultWidth, MainWindowPageLayout::kMainWindowDefaultHeight);
        MainWindowUI *ui = window.ui();
        ui->getTabWidget()->setCurrentIndex(MainWindowUI::homeTabIndex());
        auto *shownRows = new int(0);
        ui->setBottomBarPanelContent([shownRows](bool account) { return account ? QList<MainWindowUI::PanelRow>() : demoRows(*shownRows); },
                                     [](bool, const QString &, int) {});
        const auto setRunning = [ui, shownRows](int count) {
            *shownRows = count;
            ui->setRunPanelAvailable(count > 0);
            ui->setRunStateCount(count);
            ui->getRunStateLabel()->setText(count > 0 ? MainWindowUI::runStateRunningCountText(count)
                                                      : MainWindowUI::runStateNotRunningText());
            ui->getRunStateLabel()->setVisible(true);
            ui->getRunStateMarkLabel()->setPixmap(MainWindowUI::runStateMarkPixmap(
                count > 0 ? MainWindowUI::RunStateMark::Running : MainWindowUI::RunStateMark::Idle,
                ui->getBottomBar()->palette()));
            ui->getRunStateMarkLabel()->setVisible(true);
        };
        // 真屏截取：弹层是独立顶层窗口，window.grab() 抓不到。
        const auto shot = [&window, root, theme](const QString &what) {
            QApplication::processEvents();
            const QPixmap pm = window.screen()->grabWindow(0, window.x(), window.y(), window.width(), window.height());
            pm.save(QStringLiteral("%1/rail-%2-%3-%4x%5.png").arg(root, what, theme).arg(window.width()).arg(window.height()));
        };
        struct Stage { int delay; std::function<void()> work; };
        auto *stages = new QList<Stage>{
            {300, [ui, setRunning]() { ui->titleBar()->setCollapsed(true); setRunning(0); }},
            {500, [shot]() { shot(QStringLiteral("collapsed-idle")); }},
            {100, [setRunning]() { setRunning(1); }},
            {500, [shot]() { shot(QStringLiteral("collapsed-one")); }},
            {100, [setRunning, ui]() { setRunning(3); ui->getRunStateButton()->click(); }},
            {700, [shot]() { shot(QStringLiteral("collapsed-three-panel")); }},
            {100, [ui]() { for (QWidget *top : QApplication::topLevelWidgets()) if (top->windowType() == Qt::Popup && top->isVisible()) top->close(); }},
            {300, [ui, setRunning]() { ui->titleBar()->setCollapsed(false); setRunning(0); }},
            {700, [shot]() { shot(QStringLiteral("expanded-idle")); }},
            {100, [setRunning]() { setRunning(3); }},
            {500, [shot]() { shot(QStringLiteral("expanded-three")); }},
            {100, [ui]() { ui->titleBar()->setCollapsed(true); if (auto *s = ui->getTaskSummaryButton()) s->click(); }},
            {500, [&window]() { ActivityPopover::openAllIn(&window); }},
            {900, [&window, shot]() {
                 if (QWidget *p = window.findChild<QWidget *>(QStringLiteral("activityPopover")); p && p->isVisible()) p->close();
                 shot(QStringLiteral("task-center"));
             }},
            {200, [&window]() { if (window.ui()->overlay() && window.ui()->overlay()->isOpen()) window.ui()->overlay()->dismiss(); }},
        };
        auto *at = new int(0);
        auto *next = new std::function<void()>;
        *next = [stages, at, next, &window]() {
            if (*at >= stages->size()) { QTimer::singleShot(300, &window, [] { step(); }); return; }
            const Stage s = stages->at((*at)++);
            QTimer::singleShot(s.delay, &window, [s, next]() { s.work(); (*next)(); });
        };
        (*next)();
    };
    QTimer::singleShot(1500, &window, [] { step(); });
}

}  // namespace RailRunShots
