/**
 * @file InstanceFilesPageCapabilities.cpp
 * @brief 这一页能做什么（勾选 / 删 / 编辑 / 批量）、几颗按钮写着什么、列哪个目录。
 *
 * 【为什么单独一个翻译单元】
 *
 * 同一个 `InstanceFilesPage` 被八页用着（模组 / 资源包 / 光影 / 数据包 / 存档 / 截图 /
 * 日志 / 结构），每页能做的事不一样：模组能勾、存档能进世界、有的页只能看。这一片就是那
 * 一整套"能力开关 + 那几颗按钮的文字与显隐"—— 外面（`InstanceFilesDialog` 那一层）按页
 * 把它们拨一遍，里面每一处都读这几个标志。放在一个文件里，加一页时不必满文件翻"这一颗
 * 按钮凭什么露着"。
 *
 * 【几条规矩】
 *
 * - 能力只由外面拨，这一层不自己猜（页上绝不出现"看着能按、按下去没反应"的键）；
 * - 按钮文字为空 = 这一颗**不露**（`setVisible(!text.isEmpty())`），这是"这一页没有这件
 *   事"的唯一说法；
 * - `setDirectory()` 只记账；看不见的那几页先欠着（`m_refreshPending`），轮到它时再列。
 */

#include "InstanceFilesPage.hpp"

#include <QAbstractItemView>
#include <QListWidget>
#include <QPushButton>

void InstanceFilesPage::setMode(Mode mode) {
    m_mode = mode;
    refresh();
}

void InstanceFilesPage::setRowIconsVisible(bool visible) {
    m_rowIconsVisible = visible;
    refresh();
}

void InstanceFilesPage::setCanToggle(bool canToggle) {
    m_canToggle = canToggle;
    refresh();
}

void InstanceFilesPage::setCanDelete(bool canDelete) {
    m_canDelete = canDelete;
    refresh();
}

void InstanceFilesPage::setCanEdit(bool canEdit) {
    m_canEdit = canEdit;
    refresh();
}

void InstanceFilesPage::setBatchMode(bool batchMode) {
    if (m_batchMode == batchMode) {
        return;
    }
    m_batchMode = batchMode;
    // 多选 ⇄ 单选（见 .hpp 里 setBatchMode 那段）。**Ctrl+A 通不通就由这一句说了算**：
    // `QAbstractItemView::keyPressEvent()` 里那一发 `QKeySequence::SelectAll` 是**照发不误**的
    // （判据只有 `selectionMode() != NoSelection`），真正拦住它的是 `selectAll()` 自己那句
    // "`SingleSelection` 什么都不做" —— 单选的视图上那一发就是**什么都不发生**，这正是
    // 不开批量的那几页该有的样子。
    //
    // **用 `MultiSelection`（点一下就切换）**：单击切换选中、Ctrl+A 全选、
    // 拖动那半边见 `InstanceFilesPageEventFilter.cpp`：
    // 项上直接拖动 = 拖出文件，Ctrl+拖动 = 框选扫过即取反。
    m_list->setSelectionMode(batchMode ? QAbstractItemView::MultiSelection
                                       : QAbstractItemView::SingleSelection);
    if (!batchMode) {
        // 关掉的时候把选择清干净：不清的话 `m_selectedPaths` 里压着一份"用户根本看不见的
        // 选中"（那一列按钮也全没了），哪一天再开回来会突然冒出来几条亮着的行。
        m_list->clearSelection();
        m_selectedPaths.clear();
    }
    // 详情按钮的建/不建是 makeRow 说了算 ⇒ 得重列一遍（这一趟不读盘，只重建行）。
    rebuildRows();
}

void InstanceFilesPage::setEmptyText(const QString &text) {
    m_emptyText = text;
    applyState();
}

void InstanceFilesPage::setImportText(const QString &text, const QString &tip) {
    if (m_import == nullptr) {
        return;
    }
    m_import->setText(text);
    m_import->setToolTip(text.isEmpty() ? QString() : tip);
    m_import->setVisible(!text.isEmpty());
}

void InstanceFilesPage::setJoinText(const QString &text, const QString &tip) {
    if (m_join == nullptr) {
        return;
    }
    m_join->setText(m_worldLayout && !text.isEmpty() ? tr("加入世界") : text);
    m_join->setVisible(!text.isEmpty());
    m_joinTip = text.isEmpty() ? QString() : tip;
    updateJoinEnabled();
}

void InstanceFilesPage::updateJoinEnabled() {
    if (m_join == nullptr) {
        return;
    }
    // 「加入」要有**一个明确的世界**才谈得上 —— 那只有选中那一行能给（这一页不是批量那四页，
    // 选择模式是单选）。灰着比"按下去什么也不发生"诚实（和服务器列表那两颗同一条规矩）。
    //
    // NOTE: 这一页上它只在**存档**那一页露着（见 setJoinText()），而别的几页上它也照旧跟
    // 着选中状态走 —— 藏着的按钮 enabled 是什么都不影响屏幕上任何东西。
    const bool hasWorld = !currentPath().isEmpty();
    m_join->setEnabled(hasWorld);
    if (!m_joinTip.isEmpty()) {
        m_join->setToolTip(hasWorld ? m_joinTip : joinSelectFirstHint());
    }
}

void InstanceFilesPage::setBatchText(const QString &text, const QString &tip) {
    if (m_batch == nullptr) {
        return;
    }
    m_batch->setText(text);
    m_batch->setToolTip(text.isEmpty() ? QString() : tip);
    m_batch->setVisible(!text.isEmpty());
}

void InstanceFilesPage::setDownloadText(const QString &text, const QString &tip) {
    if (m_download == nullptr) {
        return;
    }
    m_download->setText(text);
    m_download->setToolTip(text.isEmpty() ? QString() : tip);
    // 空串 = 这一页没有这颗按钮（见 .hpp）。**藏起来**而不是"不建"：这几页是同一段循环
    // 搭出来的，建/不建会让"这一页该有什么"散成两处知识。露不露由 isVisibleTo() 量得出来，
    // 用户看到的也是同一件事。
    m_download->setVisible(!text.isEmpty());
}

void InstanceFilesPage::setDirectory(const QString &dir) {
    m_directory = dir;
    // NOTE: **欠账这件事只跟"看不看得见"有关，跟数据安不安全无关**：那一份份后到的数据
    // （图标 `m_icons` / 能点开的身份 `m_resourceTips` / 可更新的名单 `m_outdatedPaths`）
    // 全都是**先记账、再找行**（见 setIconForPath() 那几段的 NOTE）—— 这一页没有行的时候
    // 账照样记着，下一趟 refresh() 摆出来的那一行自然是对的。
    if (!isCurrentPage()) {
        m_refreshPending = true;
        return;
    }
    refresh();
}


