#include "Support.hpp"
#include "ListRowDelegate.hpp"
#include "VersionIcons.hpp"

#include <QHoverEvent>
#include <QMouseEvent>

int UiListRowDelegateChecks() {
    // 设计稿「列表行」：60 高；悬停才出快捷操作（文件夹 / 更多 / 启动胶囊）；选中 = 淡底 + 左侧 3px 指示条。
    QListWidget list;
    list.setFrameShape(QFrame::NoFrame);
    list.resize(520, 3 * ListRowDelegate::rowHeight() + 4);
    ListRowDelegate *delegate = ListRowDelegate::install(&list);
    for (int i = 0; i < 3; ++i) {
        auto *item = new QListWidgetItem(QStringLiteral("实例 %1").arg(i), &list);
        item->setData(Qt::DecorationRole, VersionIcons::isoBlock(i, ListRowDelegate::coverSize()));
        item->setData(ListRowDelegate::SubtitleRole, QStringLiteral("Fabric 0.16.9 · 1.21.4"));
        item->setData(ListRowDelegate::MetaRole, QStringLiteral("昨天"));
    }
    list.setCurrentRow(2);
    list.show();
    QApplication::processEvents();

    const QRect row0 = list.visualItemRect(list.item(0));
    const QRect row1 = list.visualItemRect(list.item(1));
    const QRect row2 = list.visualItemRect(list.item(2));
    check(row0.height() == ListRowDelegate::rowHeight(), "LR1 列表行 60 高", QString::number(row0.height()));

    const QRect folder = ListRowDelegate::actionRect(row1, ListRowDelegate::Action::Folder);
    const QRect more = ListRowDelegate::actionRect(row1, ListRowDelegate::Action::More);
    const QRect launch = ListRowDelegate::actionRect(row1, ListRowDelegate::Action::Launch);
    check(folder.right() < more.left() && more.right() < launch.left() && row1.contains(launch) &&
              launch.width() > launch.height(),
          "LR2 快捷操作从左到右：文件夹 / 更多 / 启动胶囊，都在行内");

    const QColor primary = Theme::tokens().primary;
    const auto grab = [&list]() { return list.viewport()->grab().toImage(); };
    QImage before = grab();
    check(before.pixelColor(launch.center()) != primary, "LR3 没悬停时不画启动胶囊");

    const QPoint at = row1.center();
    QHoverEvent hover(QEvent::HoverMove, at, list.viewport()->mapToGlobal(at), QPoint(-1, -1));
    QApplication::sendEvent(list.viewport(), &hover);
    QApplication::processEvents();
    const QImage hovered = grab();
    const QPoint pillProbe(launch.left() + 4, launch.center().y());
    check(hovered.pixelColor(pillProbe) == primary, "LR4 悬停那一行出现主色启动胶囊",
          hovered.pixelColor(pillProbe).name());
    // 令牌带透明度，量"和常态底色不同"而不是等于令牌原值。
    const QPoint hoverProbe(row1.left() + 2, row1.center().y());
    check(hovered.pixelColor(hoverProbe) != before.pixelColor(hoverProbe), "LR5 悬停那一行铺淡底",
          hovered.pixelColor(hoverProbe).name() + QStringLiteral(" / 常态 ") + before.pixelColor(hoverProbe).name());

    const QColor bar = hovered.pixelColor(row2.left() + 4, row2.center().y());
    const QColor selectedBg = hovered.pixelColor(row2.left() + 20, row2.top() + 4);
    // tokens-r2 §6：选中 = 淡底 + 强调字，左侧不再画 3px 指示条 ⇒ 原来条所在那一列就是选中底。
    check(bar == selectedBg && bar != Theme::tokens().deep, "LR6 选中那一行左侧没有指示条（同一档淡底）",
          bar.name() + QStringLiteral(" / 选中底 ") + selectedBg.name());
    const QColor plainBg = hovered.pixelColor(row0.left() + 20, row0.top() + 4);
    check(selectedBg != plainBg && selectedBg != hovered.pixelColor(hoverProbe), "LR7 选中那一行铺强调色淡底（和常态、悬停都不同）",
          selectedBg.name() + QStringLiteral(" / 常态 ") + plainBg.name());

    int fired = 0, firedRow = -1;
    ListRowDelegate::Action firedAction = ListRowDelegate::Action::Folder;
    QObject::connect(delegate, &ListRowDelegate::actionTriggered,
                     [&](const QModelIndex &index, ListRowDelegate::Action action) {
                         ++fired; firedRow = index.row(); firedAction = action;
                     });
    const QPoint click = launch.center();
    QMouseEvent press(QEvent::MouseButtonPress, click, list.viewport()->mapToGlobal(click), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, click, list.viewport()->mapToGlobal(click), Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(list.viewport(), &press);
    QApplication::sendEvent(list.viewport(), &release);
    check(fired == 1 && firedRow == 1 && firedAction == ListRowDelegate::Action::Launch,
          "LR8 点启动胶囊发出 actionTriggered(第 2 行, Launch)", QString::number(fired));
    return 0;
}
