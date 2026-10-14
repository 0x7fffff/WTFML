#include "ActivityCenterController.hpp"

ActivityCenterController::ActivityCenterController(QObject *parent)
    : QObject(parent), m_tasks() {
    connect(&m_tasks, &Tasks::TaskManager::taskChanged, this,
            [this](Tasks::TaskId) { emit activityChanged(); });
    connect(&m_tasks, &Tasks::TaskManager::taskFinished, this,
            [this](Tasks::TaskId, bool) { emit activityChanged(); });
    connect(&m_tasks, &Tasks::TaskManager::historyChanged, this, [this] {
        emit historyChanged();
        emit activityChanged();
    });
}

void ActivityCenterController::loadHistory() {
    m_tasks.loadHistory();
}

void ActivityCenterController::clearFinished() {
    m_tasks.clearFinished();
}

Tasks::TaskId ActivityCenterController::begin(Tasks::TaskKind kind, const QString &title,
                                               const QString &targetId,
                                               const QList<Tasks::TaskStep> &steps) {
    return m_tasks.begin(kind, title, targetId, steps, defaultCapabilities(kind));
}

void ActivityCenterController::finish(Tasks::TaskId id, bool success,
                                      const QString &errorSummary,
                                      bool retryPayloadAvailable) {
    if (id == Tasks::invalidTaskId() || !m_tasks.has(id)) {
        return;
    }
    if (success) {
        if (m_retryableTaskId == id) {
            m_retryableTaskId = Tasks::invalidTaskId();
        }
        m_tasks.succeed(id);
        return;
    }

    if (retryPayloadAvailable) {
        Tasks::TaskCapabilities caps = m_tasks.snapshot(id).capabilities;
        caps.canRetry = true;
        m_tasks.setCapabilities(id, caps);
        m_retryableTaskId = id;
    }
    if (m_tasks.fail(id, errorSummary)) {
        m_unreadFailures.insert(id);
        emit activityChanged();
    }
}

Tasks::TaskSummary ActivityCenterController::collapsedSummary(const QDateTime &now,
                                                               qint64 recentWindowMs) const {
    return Tasks::collapsedSummary(m_tasks.all(), m_unreadFailures, now, recentWindowMs);
}

bool ActivityCenterController::isUnreadFailure(Tasks::TaskId id) const {
    return m_unreadFailures.contains(id);
}

bool ActivityCenterController::markAllViewed() {
    if (m_unreadFailures.isEmpty()) {
        return false;
    }
    m_unreadFailures.clear();
    return true;
}

bool ActivityCenterController::retryWouldWork(const Tasks::TaskSnapshot &task,
                                               bool retryPayloadAvailable) const {
    return Tasks::retryOffered(task, m_retryableTaskId, retryPayloadAvailable);
}

void ActivityCenterController::markRetryConsumed() {
    m_retryableTaskId = Tasks::invalidTaskId();
}

Tasks::TaskCapabilities ActivityCenterController::defaultCapabilities(Tasks::TaskKind kind) {
    Tasks::TaskCapabilities caps;
    switch (kind) {
    case Tasks::TaskKind::Download:
    case Tasks::TaskKind::Install:
    case Tasks::TaskKind::Import:
        caps.canCancel = true;
        break;
    case Tasks::TaskKind::Launch:
    case Tasks::TaskKind::Export:
    case Tasks::TaskKind::Backup:
    case Tasks::TaskKind::Java:
    case Tasks::TaskKind::Integrity:
    case Tasks::TaskKind::Crash:
    case Tasks::TaskKind::Other:
        break;
    }
    if (kind == Tasks::TaskKind::Crash) {
        caps.canExportReport = true;
        caps.canOpenCrashDir = true;
    }
    caps.canOpenLog = true;
    caps.canOpenTarget = true;
    return caps;
}
