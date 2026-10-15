#pragma once

#include "tasks/TaskManager.hpp"
#include "TaskCenterPanel.hpp"

#include <QObject>
#include <QSet>

#include <functional>

/**
 * Owns the activity ledger and the cross-task decisions used by the activity
 * centre.  Executors still live in their own domains; during the incremental
 * migration they may update the ledger through tasks().
 */
class ActivityCenterController final : public QObject {
    Q_OBJECT

public:
    explicit ActivityCenterController(QObject *parent = nullptr);

    Tasks::TaskManager *tasks() { return &m_tasks; }
    const Tasks::TaskManager *tasks() const { return &m_tasks; }

    void loadHistory();
    void clearFinished();

    Tasks::TaskId begin(Tasks::TaskKind kind, const QString &title,
                        const QString &targetId = QString(),
                        const QList<Tasks::TaskStep> &steps = {});
    void finish(Tasks::TaskId id, bool success, const QString &errorSummary,
                bool retryPayloadAvailable);

    Tasks::TaskSummary collapsedSummary(const QDateTime &now, qint64 recentWindowMs) const;
    bool isUnreadFailure(Tasks::TaskId id) const;
    bool markAllViewed();
    bool retryWouldWork(const Tasks::TaskSnapshot &task, bool retryPayloadAvailable) const;
    void markRetryConsumed();

    TaskCenter::Item item(const Tasks::TaskSnapshot &task, bool cancelAvailable,
                          bool retryPayloadAvailable, bool crashRecordAvailable) const;
    QList<TaskCenter::Item> items(
        const std::function<bool(const Tasks::TaskSnapshot &)> &cancelAvailable,
        bool retryPayloadAvailable, const QSet<Tasks::TaskId> &crashRecordIds,
        bool *viewStateChanged = nullptr);
    bool itemForKey(const QString &key, TaskCenter::Item *out,
                    const std::function<bool(const Tasks::TaskSnapshot &)> &cancelAvailable,
                    bool retryPayloadAvailable,
                    const QSet<Tasks::TaskId> &crashRecordIds) const;

    static TaskCenter::State presentationState(Tasks::TaskState state);

    static Tasks::TaskCapabilities defaultCapabilities(Tasks::TaskKind kind);

signals:
    void activityChanged();
    void historyChanged();

private:
    Tasks::TaskManager m_tasks;
    Tasks::TaskId m_retryableTaskId = Tasks::invalidTaskId();
    QSet<Tasks::TaskId> m_unreadFailures;
};
