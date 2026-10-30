#pragma once
#include <QHash>
#include <QJsonObject>
#include <QWidget>
#include <functional>
class QLineEdit;
class QListWidget;
class QStackedWidget;
class QCheckBox;
class QLabel;
class QTableWidget;
class PresetOptionsEditor;
class PresetModEditor;

// One in-memory document; no sub-page writes to disk.
class PresetEditorPage final : public QWidget {
  public:
    explicit PresetEditorPage(const QJsonObject &document, QWidget *parent = nullptr);
    QJsonObject document() const;
    void setDocument(const QJsonObject &document);
    bool isDirty() const;
    void markSaved(const QJsonObject &document);
    void showProblem(const QString &text);
    std::function<void(bool)> saveRequested;
    std::function<bool(const QString &)> confirmReset;
    std::function<void()> backRequested;

  private:
    void rebuildPages(const QJsonObject &document);
    void addPack(const QString &part);
    QJsonObject m_original, m_base;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_icon = nullptr;
    QListWidget *m_navigation = nullptr;
    QStackedWidget *m_pages = nullptr;
    QLabel *m_problem = nullptr;
    PresetOptionsEditor *m_options = nullptr;
    PresetOptionsEditor *m_keys = nullptr;
    PresetModEditor *m_mods = nullptr;
    QHash<QString, QCheckBox *> m_enabled;
    QHash<QString, QTableWidget *> m_packs;
};
