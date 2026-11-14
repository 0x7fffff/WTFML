/**
 * @file UiInstanceFilesTestHelpers.hpp
 * @brief ui_check 里几个"造夹具"的小工具（跨模块共用一份，别各写一遍）。
 *
 * 为什么单独一层：`UiContentPagesChecks.cpp` 里原本有 `mkfile` / `mkfile2` / `mkfileB` /
 * `mkfileR` 四份内容一样的 lambda（都是"建目录 + 写一块字节"），拆出去的模块又各要一份。
 * 复制到第四遍的时候它就成了一份**会各自漂移的判据**：某一份改了写入字节数，另一份断言
 * 量的仍是旧值。所以收在这里，谁要谁 include。
 *
 * NOTE: 这些只是测试夹具，不碰产品代码；写失败**不报错**（和原来那几个 lambda 一样）——
 * 需要确认写成功的断言自己再 check 一次。
 */
#pragma once

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

/// 建出 @p path 的父目录并写 @p bytes 个 @p fill 字节（默认 4096 个 'x'）。
inline void uiMkFile(const QString &path, int bytes = 4096, char fill = 'x') {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QByteArray(bytes, fill));
    }
}
#include "InstanceFilesPage.hpp"

#include <QApplication>
#include <QColor>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPixmap>

#include <memory>

// ---- 行里那几格怎么读 ----
//
// 行是**代理画的**（InstanceFilesPage::RowDelegate），没有行控件可拿。名字在条目的
// `Qt::AccessibleTextRole` 上（代理画的、读屏读的都是它；DisplayRole 故意空着），
// 所以照着条目读。

/// 这一行的显示名（含「（已禁用）」和更新箭头）。
inline QString uiItemText(InstanceFilesPage *page, int row) {
    QListWidgetItem *item = page != nullptr ? page->list()->item(row) : nullptr;
    return item != nullptr ? item->data(Qt::AccessibleTextRole).toString() : QString();
}

/// 这一行行首那张图（条目的 DecorationRole；没有图标栏的页上是空的）。
inline QPixmap uiItemIcon(InstanceFilesPage *page, int row) {
    QListWidgetItem *item = page != nullptr ? page->list()->item(row) : nullptr;
    return item != nullptr ? item->data(Qt::DecorationRole).value<QPixmap>() : QPixmap();
}

/// 行尾的某一格（`[详情][开关][笔][垃圾桶]` 之一）。行是画的，这个小壳替测试问页面：
/// 点不点得动、悬停提示、开关开没开；click() 走 InstanceFilesPage::clickRowAction()
/// —— 合成真鼠标（移过去、按下、松开），和用户同一条事件路。
class UiRowCell {
public:
    UiRowCell(InstanceFilesPage *page, int row, InstanceFilesPage::RowAction action)
        : m_page(page), m_row(row), m_action(action) {}
    bool isEnabled() const { return m_page->rowActionEnabled(m_row, m_action); }
    QString toolTip() const { return m_page->rowActionToolTip(m_row, m_action); }
    /// 那一格在视口里的矩形。
    QRect geometry() const { return m_page->rowActionRect(m_row, m_action); }
    /// 开关那一格：开着 = 游戏扫得到（条目的勾选位，读屏也读它）。
    bool isChecked() const {
        const QListWidgetItem *item = m_page->list()->item(m_row);
        return item != nullptr && item->checkState() == Qt::Checked;
    }
    /// 此刻画没画：「详情」只在悬停那一行画，别的格子一直画着。
    bool isShown() const {
        return m_action != InstanceFilesPage::RowAction::Detail || m_page->rowDetailShown(m_row);
    }
    void click() const { m_page->clickRowAction(m_row, m_action); }

private:
    InstanceFilesPage *m_page;
    int m_row;
    InstanceFilesPage::RowAction m_action;
};
using UiRowCellPtr = std::shared_ptr<UiRowCell>;

/// 第 @p row 行的那一格；这一行没有这一格（页上没开、或者是文件夹）回 nullptr。
inline UiRowCellPtr uiRowCell(InstanceFilesPage *page, int row, InstanceFilesPage::RowAction action) {
    if (page == nullptr || page->list() == nullptr || page->list()->item(row) == nullptr) return nullptr;
    if (page->rowActionRect(row, action).isNull()) return nullptr;
    return std::make_shared<UiRowCell>(page, row, action);
}
inline UiRowCellPtr uiToggleAt(InstanceFilesPage *page, int row) {
    return uiRowCell(page, row, InstanceFilesPage::RowAction::Toggle);
}
inline UiRowCellPtr uiDeleteAt(InstanceFilesPage *page, int row) {
    return uiRowCell(page, row, InstanceFilesPage::RowAction::Delete);
}
inline UiRowCellPtr uiEditAt(InstanceFilesPage *page, int row) {
    return uiRowCell(page, row, InstanceFilesPage::RowAction::Edit);
}
inline UiRowCellPtr uiDetailAt(InstanceFilesPage *page, int row) {
    return uiRowCell(page, row, InstanceFilesPage::RowAction::Detail);
}

/// 这一行在 @p role 上记的是不是真（禁用 / 可更新那些角色位）。
inline bool uiItemFlag(InstanceFilesPage *page, int row, int role) {
    QListWidgetItem *item = page != nullptr ? page->list()->item(row) : nullptr;
    return item != nullptr && item->data(role).toBool();
}

/// 这一行记的**盘上那个真路径**（禁用那条也带着 `.disabled` 后缀）。
inline QString uiItemPath(InstanceFilesPage *page, int row) {
    QListWidgetItem *item = page != nullptr ? page->list()->item(row) : nullptr;
    return item != nullptr ? item->data(InstanceFilesPage::pathRole()).toString() : QString();
}

