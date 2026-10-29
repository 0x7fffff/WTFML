#pragma once
#include "tasks/TaskId.hpp"
#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <functional>
#include <atomic>
#include <memory>

class ActivityCenterController;
class DownloadManager;

// Connects file checkpoints to the activity ledger. It never reconstructs a Java process.
class DownloadBatchController final : public QObject {
public:
    DownloadBatchController(ActivityCenterController *activity, DownloadManager *downloads,
        std::function<Tasks::TaskId()> activeTask, QObject *parent = nullptr);
    ~DownloadBatchController() override;
    void load();
    bool owns(Tasks::TaskId id) const;
    bool canCancel(Tasks::TaskId id) const;
    bool pause(Tasks::TaskId id);
    bool resume(Tasks::TaskId id);
    bool cancel(Tasks::TaskId id);
    void shutdown();
    bool isRestoring() const { return (m_restoring && m_current != Tasks::invalidTaskId()) || m_verifying != Tasks::invalidTaskId(); }
    void setRestoreAllowed(std::function<bool()> check) { m_restoreAllowed = std::move(check); }
    void setContinuation(std::function<QJsonObject()> snapshot, std::function<bool(Tasks::TaskId, const QJsonObject &)> resume,
        std::function<bool(const QJsonObject &, const std::atomic_bool *)> validate = {}) {
        m_continuationSnapshot = std::move(snapshot); m_continue = std::move(resume);
        m_continuationValidation = std::move(validate);
    }
private:
    void sync();
    void complete(bool success);
    ActivityCenterController *m_activity;
    DownloadManager *m_downloads;
    std::function<Tasks::TaskId()> m_activeTask;
    std::function<bool()> m_restoreAllowed;
    std::function<QJsonObject()> m_continuationSnapshot;
    std::function<bool(Tasks::TaskId, const QJsonObject &)> m_continue;
    std::function<bool(const QJsonObject &, const std::atomic_bool *)> m_continuationValidation;
    QHash<Tasks::TaskId, QString> m_records;
    QString m_root;
    Tasks::TaskId m_current = Tasks::invalidTaskId();
    Tasks::TaskId m_verifying = Tasks::invalidTaskId();
    std::shared_ptr<std::atomic_bool> m_verifyCancel;
    quint64 m_verifyGeneration = 0;
    bool m_restoring = false, m_stopping = false;
};
