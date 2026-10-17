#pragma once
#include <QObject>
#include <QPointer>
#include <QElapsedTimer>
#include <QSet>
#include "tasks/TaskId.hpp"
#include "download/DirectDownloadRequest.hpp"

class ActivityCenterController;
class ResumableDownload;
class DownloadToolDialog;
class QWidget;

class DirectDownloadController final : public QObject {
public:
    DirectDownloadController(ActivityCenterController *activity, QWidget *window, QObject *parent = nullptr);
    ~DirectDownloadController() override;
    void open();
    bool start(const DirectDownloadRequest &request, bool allowOverwrite, QString *error);
    bool owns(Tasks::TaskId id) const;
    bool canCancel(Tasks::TaskId id) const;
    bool cancel(Tasks::TaskId id);
    void shutdown();
    Tasks::TaskId taskId() const { return m_task; }
private:
    void finish(bool success, const QString &error);
    ActivityCenterController *m_activity;
    QWidget *m_window;
    QPointer<DownloadToolDialog> m_dialog;
    ResumableDownload *m_download = nullptr;
    Tasks::TaskId m_task = Tasks::invalidTaskId();
    QSet<Tasks::TaskId> m_ownedTasks;
    bool m_running = false;
    QString m_path;
    QElapsedTimer m_rateClock;
    qint64 m_lastBytes = 0;
    QString m_status;
};
