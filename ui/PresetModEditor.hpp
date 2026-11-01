#pragma once
#include <QDialog>
#include <QList>
class QTableWidget;
class QLabel;
class QCheckBox;

class PresetModEditor : public QDialog {
public:
    struct Entry {
        QString platform, projectId, name, gameVersion, loader;
        bool required = true;
    };
    explicit PresetModEditor(const QList<Entry> &entries, QWidget *parent = nullptr);
    QList<Entry> entries() const;
private:
    void append(const Entry &entry);
    /// 这一行是不是「两个平台都没认出来」：平台那格还停在「未知来源（请选择）」。
    bool unrecognizedRow(int row) const;
    /// 照「只看没认出来的」那一格把行藏起来（数据一行都不动）。
    void applyUnknownFilter();
    QTableWidget *m_table;
    QLabel *m_problem;
    QCheckBox *m_onlyUnknown = nullptr;
};
