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
        /// 记 / 摘"这一份实例正在搬文件"（唯一出处是 `MainWindow::installingVersionIds()`）。
        /// 还原一份实例会把整个目录**换掉**，这期间它不能被启动、不能被删。
        std::function<void(const QString &, bool)> setFileOperationBusy;
        /// 盘上的实例清单变了（还原之后 meta / 图标 / json 全都是快照里的那一份）。
        std::function<void()> refreshInstances;
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
    /// 从快照还原一整个实例。
    ///
    /// @param dialog 从实例设置页点进来时那一页（可为空）。还原会把目录里的文件整个
    ///        换掉，而那一页的控件里还是**还原之前**的值 —— 留着它，用户随便改一格就会
    ///        把旧值写回去。所以还原期间它先被禁用，动过盘之后直接收掉。
    void restoreInstance(const QString &versionId, InstanceSettingsDialog *dialog = nullptr);

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
    /// 还原实例那一趟：忙标记记的是哪一份、要不要在收尾时收掉开着的那一页。
    QString m_restoreInstanceId;
    QPointer<InstanceSettingsDialog> m_restoreDialog;
};
