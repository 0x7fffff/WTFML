#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>

class MainWindow;
class InstanceSettingsDialog;

class InstanceEditorController final : public QObject {
    Q_OBJECT
public:
    explicit InstanceEditorController(MainWindow *owner);
    QString latestConfigBackup(const QString &instanceId, const QString &path) const;
    void pruneConfigBackups(const QString &instanceId, int keep);
    void openConfigFile(InstanceSettingsDialog *dialog, const QString &path);
    void saveConfigFile(InstanceSettingsDialog *dialog, const QString &path, const QByteArray &bytes);
    void restoreConfigFile(InstanceSettingsDialog *dialog, const QString &path);
    bool backupWorldLevelData(const QString &worldDir, QString *problemOut);
    QString latestWorldBackup(const QString &worldDir) const;
    void showWorldSettingsPage(InstanceSettingsDialog *dialog, const QString &worldDir);
    void leaveWorldSettings(InstanceSettingsDialog *dialog);
    void saveWorldSettings(InstanceSettingsDialog *dialog);
    void leaveConfigEditor(InstanceSettingsDialog *dialog);
    bool confirmDiscardConfigEdits();
    bool confirmDiscardWorldEdits();
private:
    MainWindow *m_owner = nullptr;
};

