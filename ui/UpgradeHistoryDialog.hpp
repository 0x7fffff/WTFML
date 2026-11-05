/**
 * @file UpgradeHistoryDialog.hpp
 * @brief 「更新记录」弹窗（U6 的 UI 部分）：列出这个实例自己那份升级历史。
 *
 * 数据来自 `operations/UpgradeHistory::read()`（新的在前）；这里只做展示：
 * 一行 = 时间 + 前后版本 + 结果，选中之后下面显示完整细节（事务号、来源/目标、
 * 类别、复制/跳过计数与失败原因）。
 */
#pragma once

#include <QDialog>
#include <QList>

#include "operations/UpgradeHistory.hpp"

class QLabel;
class QPushButton;
class QTreeWidget;

class UpgradeHistoryDialog final : public QDialog {
    Q_OBJECT

public:
    explicit UpgradeHistoryDialog(QWidget *parent = nullptr);

    void setEntries(QList<UpgradeHistory::Entry> entries);

    static QString treeName();
    static QString detailName();
    static QString emptyName();
    static QString rollbackName();

signals:
    /// 用户选了"回滚上次原地升级…"（只有历史里存在可恢复快照的条目时才可用）。
    void rollbackRequested();

private:
    void rebuild();

    QList<UpgradeHistory::Entry> m_entries;
    QTreeWidget *m_tree = nullptr;
    QLabel *m_detail = nullptr;
    QLabel *m_empty = nullptr;
    QPushButton *m_rollback = nullptr;
};


