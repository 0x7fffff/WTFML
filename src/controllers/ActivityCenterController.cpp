#include "ActivityCenterController.hpp"

#include "InstanceFilesPage.hpp"

namespace {

TaskCenter::StepState presentationStepState(Tasks::TaskStepState state) {
    switch (state) {
    case Tasks::TaskStepState::Pending:
        return TaskCenter::StepState::Pending;
    case Tasks::TaskStepState::Running:
        return TaskCenter::StepState::Running;
    case Tasks::TaskStepState::Done:
        return TaskCenter::StepState::Done;
    case Tasks::TaskStepState::Failed:
        return TaskCenter::StepState::Failed;
    }
    return TaskCenter::StepState::Pending;
}

}  // namespace

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

TaskCenter::State ActivityCenterController::presentationState(Tasks::TaskState state) {
    switch (state) {
    case Tasks::TaskState::Queued:
        return TaskCenter::State::Queued;
    case Tasks::TaskState::Running:
        return TaskCenter::State::Running;
    case Tasks::TaskState::WaitingUser:
        return TaskCenter::State::WaitingUser;
    case Tasks::TaskState::Paused:
        return TaskCenter::State::Paused;
    case Tasks::TaskState::Succeeded:
        return TaskCenter::State::Succeeded;
    case Tasks::TaskState::Failed:
        return TaskCenter::State::Failed;
    case Tasks::TaskState::Cancelled:
        return TaskCenter::State::Cancelled;
    case Tasks::TaskState::Interrupted:
        return TaskCenter::State::Interrupted;
    }
    return TaskCenter::State::Interrupted;
}

TaskCenter::Item ActivityCenterController::item(const Tasks::TaskSnapshot &task,
                                                 bool cancelAvailable,
                                                 bool retryPayloadAvailable,
                                                 bool crashRecordAvailable) const {
    TaskCenter::Item result;
    result.key = QString::number(task.id);
    result.title = task.title;
    result.detail = task.detail;
    result.percent = Tasks::percentOf(task);
    result.state = presentationState(task.state);
    result.unreadFailure = task.state == Tasks::TaskState::Failed
                           && isUnreadFailure(task.id);
    result.finishedAt = task.finishedAt;
    result.startedAt = task.startedAt;
    result.errorSummary = task.errorSummary;
    if (task.bytesTotal > 0) {
        result.bytesText = tr("已下载 %1 / %2")
                               .arg(InstanceFilesPage::formatSize(task.bytesDone),
                                    InstanceFilesPage::formatSize(task.bytesTotal));
    }

    result.capabilities.cancel = task.capabilities.canCancel
                                 && Tasks::isActiveState(task.state) && cancelAvailable;
    result.capabilities.pause = task.capabilities.canPause
                                && Tasks::isActiveState(task.state);
    result.capabilities.retry = retryWouldWork(task, retryPayloadAvailable);
    result.capabilities.openLog = task.capabilities.canOpenLog;
    result.capabilities.openTarget = task.capabilities.canOpenTarget;
    result.capabilities.exportReport = task.capabilities.canExportReport
                                       && crashRecordAvailable;
    result.capabilities.openCrashDir = task.capabilities.canOpenCrashDir;

    result.steps.reserve(task.steps.size());
    for (const Tasks::TaskStep &step : task.steps) {
        result.steps.append({step.name, presentationStepState(step.state), step.note});
    }
    return result;
}

QList<TaskCenter::Item> ActivityCenterController::items(
    const std::function<bool(const Tasks::TaskSnapshot &)> &cancelAvailable,
    bool retryPayloadAvailable, const QSet<Tasks::TaskId> &crashRecordIds,
    bool *viewStateChanged) {
    const QList<Tasks::TaskSnapshot> snapshots = m_tasks.all();
    QList<TaskCenter::Item> result;
    result.reserve(snapshots.size());
    for (const Tasks::TaskSnapshot &task : snapshots) {
        result.append(item(task, cancelAvailable && cancelAvailable(task),
                           retryPayloadAvailable, crashRecordIds.contains(task.id)));
    }
    const bool changed = markAllViewed();
    if (viewStateChanged != nullptr) {
        *viewStateChanged = changed;
    }
    return result;
}

bool ActivityCenterController::itemForKey(
    const QString &key, TaskCenter::Item *out,
    const std::function<bool(const Tasks::TaskSnapshot &)> &cancelAvailable,
    bool retryPayloadAvailable, const QSet<Tasks::TaskId> &crashRecordIds) const {
    bool ok = false;
    const Tasks::TaskId id = key.toInt(&ok);
    if (!ok || out == nullptr || !m_tasks.has(id)) {
        return false;
    }
    const Tasks::TaskSnapshot task = m_tasks.snapshot(id);
    *out = item(task, cancelAvailable && cancelAvailable(task), retryPayloadAvailable,
                crashRecordIds.contains(id));
    return true;
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
