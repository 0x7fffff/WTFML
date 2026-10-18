#pragma once
#include <QDialog>
#include <QList>
class QTableWidget;
class QLabel;

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
    QTableWidget *m_table;
    QLabel *m_problem;
};
