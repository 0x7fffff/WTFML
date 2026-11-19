#pragma once

#include <QObject>

class InstanceSettingsDialog;
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
    /// 存档页的批量复制 / 删除（世界是文件夹，不走文件批量那一套）。
    void applyWorldBatch(InstanceSettingsDialog &dialog, QString instanceId, const QStringList &paths,
                         bool remove);

    MainWindow *m_owner = nullptr;
};
