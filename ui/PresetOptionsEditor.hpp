#pragma once
#include <QDialog>
#include <QList>
class QTableWidget;
class PresetOptionsEditor : public QDialog {
public:
    struct Entry { QString key, value; };
    explicit PresetOptionsEditor(const QList<Entry> &entries, QWidget *parent = nullptr);
    QList<Entry> entries() const;
private:
    void append(const Entry &entry);
    QTableWidget *m_table;
};
