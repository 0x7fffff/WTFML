#pragma once
#include <QObject>
#include <QPointer>
#include <QElapsedTimer>
#include <QSet>
#include <QHash>
#include <QJsonObject>
#include <memory>
#include "tasks/TaskId.hpp"
#include "download/DirectDownloadRequest.hpp"
#include "download/DownloadRate.hpp"

class ActivityCenterController;
class ResumableDownload;
class DownloadToolDialog;
class OverlayHost;
class QWidget;
class QLockFile;

class DirectDownloadController final : public QObject {
public:
    DirectDownloadController(ActivityCenterController *activity, QWidget *window, QObject *parent = nullptr);
    ~DirectDownloadController() override;
    void open();
    bool start(const DirectDownloadRequest &request, bool allowOverwrite, QString *error);
    bool owns(Tasks::TaskId id) const;
    bool canCancel(Tasks::TaskId id) const;
    bool cancel(Tasks::TaskId id);
    bool pause(Tasks::TaskId id);
    bool resume(Tasks::TaskId id);
    void shutdown();
    Tasks::TaskId taskId() const { return m_task; }
    Tasks::TaskId lastSubmittedTaskId() const { return m_lastSubmitted; }
private:
    OverlayHost *promptHost();
    struct Recovery { DirectDownloadRequest request; QString token; QJsonObject original; };
    QHash<Tasks::TaskId, Recovery> m_recoveries;
    QString m_recoveryPath;
    bool m_recoveryReadable = true;
    bool m_recoveryLoaded = false;
    std::unique_ptr<QLockFile> m_recoveryLock;
    bool saveRecovery();
    void loadRecovery();
    void syncPauseCapability();
    void manageCache(int action);
    bool m_cacheBusy = false;
    bool m_cacheTrimPending = false;
    void startJob(Tasks::TaskId id, const DirectDownloadRequest &request, bool allowOverwrite);
    QHash<Tasks::TaskId, QString> m_pendingPaths;
    Tasks::TaskId m_lastSubmitted = Tasks::invalidTaskId();
    bool m_stopping = false;
    void finish(bool success, const QString &error);
    void updateProgress(qint64 done, qint64 total);
    ActivityCenterController *m_activity;
    QWidget *m_window;
    QPointer<DownloadToolDialog> m_dialog;
    QPointer<OverlayHost> m_prompt;
    ResumableDownload *m_download = nullptr;
    Tasks::TaskId m_task = Tasks::invalidTaskId();
    QSet<Tasks::TaskId> m_ownedTasks;
    bool m_running = false;
    QString m_path;
    QElapsedTimer m_rateClock;
    DownloadRate m_rate;
    qint64 m_transferred = 0, m_lastRateUi = -1000;
    qint64 m_lastDone = 0, m_lastTotal = -1;
    bool m_showTransferTelemetry = false;
    QString m_status;
};
