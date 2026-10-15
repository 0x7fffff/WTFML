#pragma once

#include <QObject>

class MainWindow;

// Owns the construction and signal wiring of the large instance-settings page.
// MainWindow keeps a compatibility entry point while this controller coordinates
// the page's domain controllers and file-management actions.
class InstanceDialogController final : public QObject {
    Q_OBJECT
public:
    explicit InstanceDialogController(MainWindow *owner);
    void open(const QString &versionId, const QString &pageKey);

private:
    MainWindow *m_owner = nullptr;
};
