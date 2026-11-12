/**
 * @file UpgradeReportDialog.hpp
 * @brief 兼容性报告弹窗（U3 的 UI 部分）：分组列表 + 搜索 + 只看某组 + 展开正常项。
 *
 * 判断全在 `operations/UpgradeReport`（分组、折叠、筛选、必要项计数），这里只摆控件、
 * 把用户动作翻译成一次 `refresh()`。
 */
#pragma once

#include "ChromeDialog.hpp"
#include <QList>

#include "operations/UpgradeReport.hpp"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QTreeWidget;

class UpgradeReportDialog final : public ChromeDialog {
    Q_OBJECT

public:
    explicit UpgradeReportDialog(QWidget *parent = nullptr);

    void setRows(QList<UpgradeReport::Row> rows);

    static QString searchName();
    static QString bucketFilterName();
    static QString showCompatibleName();
    static QString treeName();
    static QString summaryName();

private:
    void refresh();

    QList<UpgradeReport::Row> m_rows;
    QLineEdit *m_search = nullptr;
    QComboBox *m_bucket = nullptr;
    QCheckBox *m_showCompatible = nullptr;
    QTreeWidget *m_tree = nullptr;
    QLabel *m_summary = nullptr;
};
