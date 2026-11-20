/**
 * @file ItemViewContextMenu.hpp
 * @brief 条目视图（列表 / 树）的「菜单键 / Shift+F10」入口：把键盘来源的右键菜单请求统一弹在当前行附近。
 *
 * 为什么要自己接：Qt 把菜单键 / Shift+F10 转成键盘来源的 `QContextMenuEvent` 发给带焦点的视图，
 * 坐标取的是 `inputMethodQuery(ImCursorRectangle)` 的中心；没有当前行时那个矩形是空的，
 * 菜单就落在视口左上角（甚至落到别的行上）。这里截住键盘来源的那一发，换算成
 * 「当前行附近的视口坐标」再发 `customContextMenuRequested`，各处现有的右键处理函数
 * （都按视口坐标 `itemAt(pos)` / `viewport()->mapToGlobal(pos)`）一行不用改。
 *
 * 用法：视图设成 `Qt::CustomContextMenu` 并接好 `customContextMenuRequested` 之后，调一次
 * `ItemViewContextMenu::install(view)`。鼠标右键不经过这里。
 */
#pragma once

#include <QAbstractItemView>
#include <QContextMenuEvent>
#include <QEvent>
#include <QObject>
#include <QPoint>
#include <QRect>

namespace ItemViewContextMenu {

/// 菜单键弹出的落点（视口坐标）：当前行偏左一点、竖直居中；先把当前行滚进视口。
/// 没有当前行时落在视口左上角往里一点（空白处菜单）。
inline QPoint keyboardAnchor(QAbstractItemView *view) {
    const QModelIndex current = view->currentIndex();
    if (current.isValid()) {
        view->scrollTo(current);
        const QRect rect = view->visualRect(current);
        if (rect.isValid()) {
            return QPoint(rect.left() + qMin(rect.width() / 2, 48), rect.center().y());
        }
    }
    return QPoint(16, 16);
}

/// 挂在视图上的过滤器：只处理键盘来源的 ContextMenu，且只在视图用 `Qt::CustomContextMenu` 时。
class KeyboardTrigger : public QObject {
public:
    explicit KeyboardTrigger(QAbstractItemView *view) : QObject(view), m_view(view) {
        setObjectName(QStringLiteral("wtfmlItemViewContextMenuKey"));
        view->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (watched != m_view || event->type() != QEvent::ContextMenu) return false;
        const auto *menu = static_cast<QContextMenuEvent *>(event);
        if (menu->reason() != QContextMenuEvent::Keyboard || m_view->contextMenuPolicy() != Qt::CustomContextMenu) {
            return false;
        }
        Q_EMIT m_view->customContextMenuRequested(keyboardAnchor(m_view));
        return true;
    }

private:
    QAbstractItemView *m_view;
};

inline void install(QAbstractItemView *view) {
    if (view == nullptr || view->findChild<QObject *>(QStringLiteral("wtfmlItemViewContextMenuKey")) != nullptr) {
        return;
    }
    new KeyboardTrigger(view);
}

}  // namespace ItemViewContextMenu
