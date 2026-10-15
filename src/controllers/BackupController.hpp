#pragma once

#include "manage/BackupFiles.hpp"
#include "tasks/TaskId.hpp"

#include <QAtomicInt>
#include <QObject>
#include <QPointer>

#include <functional>

class ActivityCenterController;
class InstanceSettingsDialog;
class OverlayHost;
class QThread;
class VersionManager;

class BackupController final : public QObject {
    Q_OBJECT

public:
    struct Host {
        std::function<QString(const QString &)> displayName;
        std::function<QString(const QString &)> instanceIdOfPath;
        std::function<bool(const QString &)> isRunning;
        std::function<bool(const QString &)> isBeingModified;
        std::function<void(const QString &)> log;
        std::function<void(InstanceSettingsDialog *, const QString &)> refreshWorld;
        std::function<void(const QString &)> refreshServer;
    };

    BackupController(VersionManager *versions, ActivityCenterController *activity,
                     OverlayHost *overlay, Host host, QObject *parent = nullptr);
    ~BackupController() override;

    bool busy() const;
    void shutdown();
    void openFolder(BackupFiles::Kind kind);
    void backupWorld(const QString &worldDir);
    void restoreWorld(const QString &worldDir, InstanceSettingsDialog *dialog = nullptr);
    void snapshotInstance(const QString &versionId);
    void restoreInstance(const QString &versionId);

    Tasks::TaskId startWrite(const BackupFiles::Manifest &manifest, const QString &sourceDir,
                             const QString &targetZip, const QString &title,
                             const QString &targetId, const QString &successTitle,
                             const QString &refreshServerId = QString());
    Tasks::TaskId startRestore(const QString &zipPath, const QString &targetParentDir,
                               const QString &title, const QString &targetId,
                               const QString &successTitle,
                               const QString &refreshServerId = QString());

private:
    static QString sizeText(qint64 bytes);
    static QString pickLine(const QString &zipPath);
    QString pickZip(const QString &dir, const QString &label, const QString &what);
    QString displayName(const QString &id) const;
    void log(const QString &message) const;
    void writeFinished(const BackupFiles::WriteResult &result, const QString &successTitle);
    void restoreFinished(const BackupFiles::RestoreResult &result, const QString &successTitle);

    VersionManager *m_versions = nullptr;
    ActivityCenterController *m_activity = nullptr;
    OverlayHost *m_overlay = nullptr;
    Host m_host;
    Tasks::TaskId m_taskId = Tasks::invalidTaskId();
    QThread *m_thread = nullptr;
    QAtomicInt m_abort{0};
    QString m_refreshWorldDir;
    QString m_refreshServerId;
    QPointer<InstanceSettingsDialog> m_refreshDialog;
};
