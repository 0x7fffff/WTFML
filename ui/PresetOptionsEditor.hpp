#pragma once
#include <QDialog>
#include <QList>
class QTableWidget;
class PresetOptionsEditor : public QDialog {
public:
    struct Entry { QString key, value; };
    enum class Mode { All, Options, Keys };
    explicit PresetOptionsEditor(const QList<Entry> &entries, QWidget *parent = nullptr, Mode mode = Mode::All);
    QList<Entry> entries() const;
private:
    void append(const Entry &entry);
    QTableWidget *m_table;
};
