/**
 * @file InstanceFilesPageScroll.cpp
 * @brief 重列前后那一份滚动锚点：记下"最上面那一行是谁"，重列之后把它摆回去。
 *
 * 【为什么单独一个翻译单元】
 *
 * 这一页每次重列（`refresh()` / `rebuildRows()`）都把控件整个清掉重建，滚动位置随之归零
 * —— 用户勾了一个文件、页面重排之后跳回顶上，是最常见的抱怨。这一片就是那笔账：
 *
 * - `m_scrollAnchor`（一行**身份** + 它离视口顶端的像素）；
 * - `rememberScrollAnchor()` / `captureScrollPosition()`：记；
 * - `scrollPosition()` / `setScrollPosition()`：给"进详情页再回来"那条路带走再带回来；
 * - `restoreScrollAnchor()`：摆，摆完就丢掉（一次性的）。
 *
 * 【两条说死的规矩】
 *
 * 一、**空表不动手**：`rememberScrollAnchor()` 会被叫两次（`refresh()` 在 clear 之前、
 *     `rebuildRows()` 在它自己的 clear 之前），第二次看见的是空表 —— 记下去就把第一份
 *    真锚点抹了；
 * 二、**锚点是一次性的**：这一趟还完就丢。留着的话，后面任何一次"没有重建列表的重排"
 *    都可能莫名其妙地把它再还一遍。
 *
 * 【判据不在这一层】
 *
 * 行的身份是 `identityPath()`（和"还选择"、"换图标"用的是同一个）；按行滚动时
 * `delta` 恒为 0，还原走"摆回那一行"，否则按像素差补。
 */

#include "InstanceFilesPage.hpp"

#include "InstanceFilesPageInternals.hpp"

#include <QAbstractItemView>
#include <QListWidget>
#include <QScrollBar>

void InstanceFilesPage::rememberScrollAnchor() {
    if (m_list == nullptr) {
        return;
    }
    // 空表（刚 clear 过、或者这一页本来就没东西）**不动手** —— 见 .hpp 里那段：这一句会
    // 被叫两次（refresh() 在 clear 之前、rebuildRows() 在它自己的 clear 之前），第二次
    // 看见的正是空表，记下去的话就把第一份真锚点抹了。
    if (m_list->count() == 0) {
        return;
    }
    m_scrollAnchor = captureScrollPosition();
}

InstanceFilesPage::ScrollPosition InstanceFilesPage::captureScrollPosition() const {
    ScrollPosition position;
    if (m_list == nullptr || m_list->count() == 0) {
        return position;
    }
    // "最上面那一行" = 第一行**至少露出一个像素**的。用 `itemAt(0, 0)` 不行：视口最上面
    // 那一个像素可能落在行与行之间的空档里（那一句会回 nullptr，锚点就没了）。
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem *item = m_list->item(row);
        if (item == nullptr) {
            continue;
        }
        const QRect rect = m_list->visualItemRect(item);
        // 排在前面的行（滚上去的那些）整个在视口上方 ⇒ `bottom() <= 0`。
        if (rect.bottom() <= 0) {
            continue;
        }
        position.identity = identityPath(item->data(pathRole()).toString());
        // 它当时离视口顶端多少像素。**按行滚动**（这一页用的就是 Qt 默认的
        // `ScrollPerItem`）时行总是对齐在顶端 ⇒ 这个数恒为 0，还原走的是"摆回那一行"那条
        // 路（见 restoreScrollAnchor 里那段 ★★）。
        position.delta = rect.top();
        return position;
    }
    // 一行都没露（视口比一行还矮，或者列表是空的）：没有位置。
    return position;
}

InstanceFilesPage::ScrollPosition InstanceFilesPage::scrollPosition() const {
    // 现算，不读 `m_scrollAnchor` —— 见 .hpp 里这一段（用户点详情按钮那一刻它多半空着）。
    return captureScrollPosition();
}

void InstanceFilesPage::setScrollPosition(const ScrollPosition &position) {
    // 只写这一位、**不**顺手去摆滚动条：这一刻这一页还没列过（调用点在 showPage()
    // 之前），表是空的，摆也没处摆。真正摆那一下仍旧是 restoreScrollAnchor() 的事
    // （rebuildRows() 末尾那个 0 毫秒定时器里），它读的就是这一位。
    m_scrollAnchor = position;
}

void InstanceFilesPage::restoreScrollAnchor() {
    if (m_list == nullptr || m_scrollAnchor.identity.isEmpty()) {
        return;
    }
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem *item = m_list->item(row);
        if (item == nullptr) {
            continue;
        }
        if (identityPath(item->data(pathRole()).toString()) != m_scrollAnchor.identity) {
            continue;
        }
        // 那一行**现在**在哪儿（此刻滚动条还在重列之后的位置上，通常是 0）。
        QScrollBar *bar = m_list->verticalScrollBar();
        if (m_list->verticalScrollMode() == QAbstractItemView::ScrollPerItem) {
            bar->setValue(row);
        } else {
            bar->setValue(bar->value() + m_list->visualItemRect(item).top() -
                          m_scrollAnchor.delta);
        }
        break;
    }
    // 锚点是一次性的：这一趟还完了就丢掉。留着的话，下面任何一次"没有重建列表的重排"
    // 都可能莫名其妙地把它再还一遍（比如用户自己滚了之后又触发一次 applyState）。
    m_scrollAnchor = ScrollPosition();
}
