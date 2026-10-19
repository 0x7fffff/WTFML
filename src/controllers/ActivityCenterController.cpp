#include "ActivityCenterController.hpp"
#include <QTimer>
#include <algorithm>

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
            [this](Tasks::TaskId id, bool) {
                m_queue.removeIf([id](const QueuedJob &job) { return job.id == id; });
                for (auto it = m_runningLanes.begin(); it != m_runningLanes.end();)
                    if (it.value() == id) it = m_runningLanes.erase(it); else ++it;
                scheduleDispatch(); emit activityChanged();
            });
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

Tasks::TaskId ActivityCenterController::enqueue(Tasks::TaskKind kind, const QString &title, const QString &target,
    const QList<Tasks::TaskStep> &steps, const QString &lane, QObject *owner, std::function<void(Tasks::TaskId)> start) {
    if (lane.isEmpty() || owner == nullptr || !start || m_queue.size() >= 64) return Tasks::invalidTaskId();
    const auto id = m_tasks.begin(kind, title, target, steps, defaultCapabilities(kind), true);
    m_queue.append({id, lane, owner, std::move(start)});
    m_tasks.setDetail(id, tr("等待执行；可拖动手柄调整同队列内的顺序。"));
    connect(owner, &QObject::destroyed, this, [this, id] {
        // 这里只能保证“尚未调用执行器”的任务被安全取消；运行任务由执行器自身收尾。
        // 不能把已进入原子提交的操作在 owner 析构时伪报为取消。
        if (m_tasks.has(id) && m_tasks.snapshot(id).state == Tasks::TaskState::Queued) m_tasks.cancel(id);
    });
    scheduleDispatch(); return id;
}
bool ActivityCenterController::isQueued(Tasks::TaskId id) const {
    return m_tasks.snapshot(id).state == Tasks::TaskState::Queued &&
        std::any_of(m_queue.cbegin(), m_queue.cend(), [id](const QueuedJob &job) { return job.id == id && job.owner; });
}
bool ActivityCenterController::moveQueued(Tasks::TaskId id, int delta) {
    if ((delta != -1 && delta != 1) || !isQueued(id)) return false;
    for (int i = 0; i < m_queue.size(); ++i) if (m_queue[i].id == id) {
        for (int j = i + delta; j >= 0 && j < m_queue.size(); j += delta) {
            if (m_queue[j].lane != m_queue[i].lane || !isQueued(m_queue[j].id)) continue;
            m_queue.swapItemsAt(i, j); emit activityChanged(); return true;
        }
        return false;
    }
    return false;
}
void ActivityCenterController::scheduleDispatch() {
    if (m_dispatchPending) return;
    m_dispatchPending = true;
    QTimer::singleShot(0, this, [this] { m_dispatchPending = false; dispatch(); });
}
void ActivityCenterController::dispatch() {
    // 启动前先摘出回调，执行器可以同步完成/取消甚至排入下一项，不能保留容器迭代器。
    for (;;) {
        int ready = -1;
        for (int i = 0; i < m_queue.size(); ++i) if (!m_runningLanes.contains(m_queue[i].lane)) { ready = i; break; }
        if (ready < 0) return;
        const auto job = m_queue.takeAt(ready);
        if (!job.owner || m_tasks.snapshot(job.id).state != Tasks::TaskState::Queued) { m_tasks.cancel(job.id); continue; }
        m_runningLanes.insert(job.lane, job.id);
        if (!m_tasks.startQueued(job.id)) { m_runningLanes.remove(job.lane); continue; }
        // startQueued 发信号后所有者可能被同步关闭；未调用执行器前仍可安全取消。
        if (!job.owner) { m_tasks.cancel(job.id); continue; }
        job.start(job.id);
    }
}

Tasks::TaskId ActivityCenterController::begin(Tasks::TaskKind kind, const QString &title,
                                               const QString &targetId,
                                               const QList<Tasks::TaskStep> &steps) {
    return m_tasks.begin(kind, title, targetId, steps, defaultCapabilities(kind));
}

bool ActivityCenterController::bindSession(Tasks::TaskId id, int sessionId) {
    return m_tasks.setSessionId(id, sessionId);
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

bool ActivityCenterController::markViewed(const QString &key) {
    bool ok = false;
    const auto id = key.toInt(&ok);
    if (!ok || !m_tasks.has(id)) return false;
    const auto state = m_tasks.snapshot(id).state;
    // 等待选择不能因为“看过”而消失，只随业务完成选择后离开 WaitingUser。
    if (state != Tasks::TaskState::Failed && state != Tasks::TaskState::Interrupted) return false;
    if (!m_tasks.acknowledgeAttention(id)) return false;
    m_unreadFailures.remove(id);
    emit activityChanged();
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
    const auto displayText = [](QString text) {
        // Historical Qt errors remain intact on disk; localise their known UI spelling.
        text.replace(QStringLiteral("Operation canceled"), QStringLiteral("网络请求已中止"), Qt::CaseInsensitive);
        text.replace(QStringLiteral("Operation cancelled"), QStringLiteral("网络请求已中止"), Qt::CaseInsensitive);
        return text;
    };
    result.detail = displayText(task.detail);
    result.percent = Tasks::percentOf(task);
    result.state = presentationState(task.state);
    for (int i = 0; i < m_queue.size(); ++i) if (m_queue[i].id == task.id && isQueued(task.id)) {
        result.queueGroup = m_queue[i].lane; result.queueOrder = i; break;
    }
    result.unreadFailure = task.state == Tasks::TaskState::Failed
                           && isUnreadFailure(task.id);
    result.attentionAcknowledged = task.attentionAcknowledged;
    result.finishedAt = task.finishedAt;
    result.startedAt = task.startedAt;
    result.errorSummary = displayText(task.errorSummary);
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
        result.steps.append({step.name, presentationStepState(step.state), displayText(step.note)});
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
    std::stable_sort(result.begin(), result.end(), [](const TaskCenter::Item &a, const TaskCenter::Item &b) {
        const auto rank = [](const TaskCenter::Item &item) {
            return item.state == TaskCenter::State::Queued ? 1 :
                (item.state == TaskCenter::State::Running || item.state == TaskCenter::State::WaitingUser || item.state == TaskCenter::State::Paused ? 0 : 2);
        };
        if (rank(a) != rank(b)) return rank(a) < rank(b);
        return a.state == TaskCenter::State::Queued && b.state == TaskCenter::State::Queued && a.queueOrder < b.queueOrder;
    });
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
