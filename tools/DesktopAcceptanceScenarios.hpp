#pragma once
// Controlled geometry fixtures for the native acceptance app; never launches a game.
#include "controllers/ActivityCenterController.hpp"
#include "OverlayHost.hpp"
#include <QAbstractButton>
#include <QImage>
#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QTreeWidget>
#include <QSet>

namespace DesktopAcceptance {
inline void closeTransient(MainWindow &window) {
    if (auto *popup = QApplication::activePopupWidget()) popup->close();
    window.ui()->overlay()->evict();
}
inline void mouseClick(QWidget *widget) {
    const QPointF local(widget->rect().center());
    const QPointF global(widget->mapToGlobal(local.toPoint()));
    QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(widget, &press);
    QApplication::sendEvent(widget, &release);
}
inline Tasks::TaskId seedTasks(MainWindow &window) {
    auto *activity = window.findChild<ActivityCenterController *>();
    if (!activity) return Tasks::invalidTaskId();
    const QList<Tasks::TaskStep> steps{
        {"检查完整性", Tasks::TaskStepState::Done, "核心文件校验完成"},
        {"下载文件", Tasks::TaskStepState::Running, "正在下载受控测试文件，不连接游戏服务器"},
        {"登录", Tasks::TaskStepState::Pending, {}},
        {"等待启动", Tasks::TaskStepState::Pending, {}}};
    const auto id = activity->begin(Tasks::TaskKind::Install,
        QStringLiteral("安装：长名称实例－Fabric 与资源、模组及配置预设组合的完整显示检查"), "layout-fixture", steps);
    activity->tasks()->setProgress(id, 37, 100);
    activity->tasks()->setDetail(id, QStringLiteral("正在校验：这是用于确认窄窗口、换行、省略号与右侧详情布局的长文本。"));
    const auto done = activity->begin(Tasks::TaskKind::Download, "已完成的受控下载夹具");
    activity->finish(done, true, {}, false);
    const auto failed = activity->begin(Tasks::TaskKind::Launch, "模拟错误：受控布局夹具");
    activity->finish(failed, false, "此为布局测试错误，不代表实际游戏崩溃。长错误内容用于检查换行与可用操作。", false);
    return id;
}
inline QString background(const QString &root) {
    QImage image(640, 400, QImage::Format_RGB32);
    QPainter painter(&image);
    for (int y = 0; y < image.height(); y += 40)
        for (int x = 0; x < image.width(); x += 40)
            painter.fillRect(x, y, 40, 40, ((x / 40 + y / 40) % 2) ? QColor("#3e7369") : QColor("#c9dfd9"));
    painter.end();
    const auto path = root + "/controlled-background.png";
    return image.save(path) ? path : QString();
}
inline void longHome(MainWindow &window) {
    MainWindowUI::HomeQuickItem item;
    item.key = "layout-fixture";
    item.name = QStringLiteral("用于检查省略显示与窄窗口的超长实例名称－不连接真实游戏");
    item.detail = QStringLiteral("Minecraft 1.20.1 · Fabric · 受控长文本夹具");
    item.current = item.favorite = item.recent = true;
    window.ui()->setHomeQuickRows({item}, {item}, {item});
}
inline bool scene(MainWindow &window, int index, Tasks::TaskId task) {
    auto *ui = window.ui();
    closeTransient(window);
    ui->getTabWidget()->setCurrentIndex(index < 4 ? index : 0);
    if (index == 0) longHome(window);
    if (index == 1) {
        auto *list = ui->getInstanceList();
        list->clear();
        for (int i = 0; i < 16; ++i) {
            auto *item = new QTreeWidgetItem(list);
            item->setText(0, QStringLiteral("受控列表 %1－名称很长时仍需要保留右侧按钮和版本信息").arg(i));
            item->setText(1, "Minecraft 1.20.1 / Fabric 0.19.3");
        }
    }
    if (index < 4) return true;
    if (index == 4) { ui->getHomeInstanceSelectButton()->click(); return true; }
    if (index == 5 || index == 6) {
        ui->getTaskSummaryButton()->click();
        if (index == 6) QTimer::singleShot(120, &window, [&window, task] {
            for (auto *row : window.findChildren<QWidget *>())
                if (row->isVisible() && row->property(MainWindowUI::taskCenterPanelRowKeyProperty()).toString() == QString::number(task)) {
                    mouseClick(row); break;
                }
        });
        return true;
    }
    if (index == 7) { ui->getToolboxButton()->click(); return true; }
    if (index == 8) { ui->getAccountSkinButton()->click(); return true; }
    if (index == 9) return QMetaObject::invokeMethod(&window, "onAccountSkinButtonClicked", Qt::DirectConnection);
    return false;
}
inline QJsonObject keyboard(MainWindow &window) {
    QWidget *first = nullptr;
    for (auto *button : window.findChildren<QAbstractButton *>())
        if (button->isVisible() && button->isEnabled() && (button->focusPolicy() & Qt::TabFocus)) { first = button; break; }
    if (!first) return {{"ok", false}, {"problem", "no visible keyboard-focusable control"}};
    window.activateWindow();
    window.raise();
    first->setFocus(Qt::TabFocusReason);
    QSet<QWidget *> visited;
    bool visible = true;
    for (int i = 0; i < 12; ++i) {
        auto *focus = QApplication::focusWidget();
        if (!focus || !focus->isVisible() || !focus->isEnabled()) { visible = false; break; }
        visited.insert(focus);
        QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
        QApplication::sendEvent(focus, &tab);
    }
    return {{"ok", visible && visited.size() > 1}, {"visitedControls", visited.size()}};
}
} // namespace DesktopAcceptance
