/**
 * @file InstanceFilesPageInfoPanel.cpp
 * @brief 列表下面那条"这一份是什么"：记一条正在等的路径，把算好的说明摆上去。
 *
 * 【为什么单独一个翻译单元】
 *
 * 这一格是**异步**的：用户点中一条，这一层把路径记进 `m_infoPath`、把定时器起起来、
 * 先摆「正在读取…」；答案由外面那一位算好（读 gzip NBT、认 Litematica 的位打包都是 src/
 * 的事）再递回来。于是有一半的状态是"过期答案"——用户在等的这一条和屏幕上选着的那条
 * 已经不是同一个了。这一片的全部内容就是这笔账：
 *
 * - `setInfoPanelEnabled()`：这一页有没有这一格（关掉时一个字都不留，不是只藏起来）；
 * - `updateInfoPanel()`：判据"**正好选了一条、而且它是文件**"—— 选两个、选一个目录、
 *   一条没选都不谈"这一份"；已经在为这一条等答案就不重复读盘；
 * - `setInfoText()`：答案回来时先比路径，不是屏幕上那一条就丢掉。
 *
 * 【这一层的边界】
 *
 * 只记账、只摆字。哪些文件有说明、说明怎么算，都在 src/ 那一层。
 */

#include "InstanceFilesPage.hpp"

#include "InstanceFilesPageInternals.hpp"

#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QTimer>

// ---------- 列表下面那条"这一份是什么"----------
//
// 这一层只记账、只摆字（理由见 .hpp 里那一段）：读 gzip NBT、认 Litematica 的位打包
// 都是 src/ 的事，由接的人算好递回来。

void InstanceFilesPage::setInfoPanelEnabled(bool enabled) {
    m_infoDetails->setVisible(enabled);
    if (m_infoPanelEnabled == enabled) {
        return;
    }
    m_infoPanelEnabled = enabled;
    if (!enabled) {
        // 关掉 = 这一页一个字都不留（不是只藏起来：再打开时得从"正在读取…"重新来）。
        if (m_infoTimer != nullptr) {
            m_infoTimer->stop();
        }
        m_infoPath.clear();
        if (m_info != nullptr) {
            m_info->clear();
            m_info->setToolTip(QString());
            m_info->setVisible(false);
        }
        return;
    }
    updateInfoPanel();
}

void InstanceFilesPage::setInfoText(const QString &path, const QString &text,
                                    const QString &toolTip) {
    if (m_info == nullptr || !m_infoPanelEnabled) {
        return;
    }
    // 过期答案（理由见 .hpp）：用户已经换到别的行上了，这一份讲的不是屏幕上选着的那条。
    if (path != m_infoPath) {
        return;
    }
    m_info->setText(text);
    m_info->setToolTip(toolTip);
    m_info->setVisible(!text.isEmpty());
}

void InstanceFilesPage::updateInfoPanel() {
    if (m_info == nullptr || !m_infoPanelEnabled) {
        return;
    }
    // 判据：**正好选了一条、而且它是文件**（选两个、选一个目录、一条没选 —— 都不谈"这一份"）。
    QString path;
    bool isDir = false;
    if (m_list != nullptr) {
        const QList<QListWidgetItem *> selected = m_list->selectedItems();
        if (selected.size() == 1 && selected.first() != nullptr) {
            path = selected.first()->data(pathRole()).toString();
            const Entry *entry = entryForPath(path);
            isDir = entry != nullptr && entry->isDir;
        }
    }
    m_infoDetails->setEnabled(!path.isEmpty() && !isDir);
    if (path.isEmpty() || isDir) {
        if (m_infoTimer != nullptr) {
            m_infoTimer->stop();
        }
        m_infoPath.clear();
        m_info->clear();
        m_info->setToolTip(QString());
        m_info->setVisible(false);
        return;
    }
    // 已经在为这一条等答案 / 已经摆着它的答案 ⇒ 不重复读盘（重列、开筛选都会走到这儿）。
    if (path == m_infoPath) {
        return;
    }
    m_infoPath = path;
    m_info->setText(infoPendingText());
    m_info->setToolTip(QString());
    // 列表本身藏着的时候（目录空 / 筛完一条不剩）不露 —— 那种画面下不可能有选中的一行，
    // 这里只是兜底：applyState() 也会把它收起来。
    m_info->setVisible(true);
    if (m_infoTimer != nullptr) {
        m_infoTimer->start();
    }
}
