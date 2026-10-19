/**
 * @file TaskManager.cpp
 * @brief 任务账本的实现（形状与取舍见头文件）。
 * @author 0x7fffff
 * @date 2026-10-12
 */

#include "TaskManager.hpp"

#include <algorithm>

#include "TaskHistory.hpp"

namespace {

/// 「已完成」那一栏的顺序：最近的在前。
///
/// ★ 只比 `finishedAt` 是不够的 —— 批量下载/批量文件操作会在同一毫秒里收掉几十项
///   （`QDateTime` 只到毫秒），并列时 `std::sort` 给出的是**未定义**的相对顺序。
///   面板每次重画都重排一遍，并列出会让这些行在两次重画之间换位；落盘截断那头
///   更实际：`finishedHistoryLimit()` 卡在第 50 条边上时，丢掉的是"并列里的任意一条"，
///   而不是最旧那一条。号是单调发的 ⇒ 同毫秒里号大的就是后发起的，拿它当第二把尺。
bool finishedNewerFirst(const Tasks::TaskSnapshot &a, const Tasks::TaskSnapshot &b) {
    if (a.finishedAt != b.finishedAt) {
        return a.finishedAt > b.finishedAt;
    }
    return a.id > b.id;
}

}  // namespace

namespace Tasks {

TaskManager::TaskManager(QObject *parent) : QObject(parent) {
    // NOTE: `m_historyPath` 留空 = 使用启动器级默认账本；测试可以显式指到临时文件。
}

TaskManager::~TaskManager() = default;

QString TaskManager::resolvedHistoryPath() const {
    return m_historyPath.isEmpty() ? defaultHistoryPath() : m_historyPath;
}

int TaskManager::loadHistory() {
    QList<TaskSnapshot> loaded = load(resolvedHistoryPath());
    // 读回来的是**上一份账本**（含当时还在跑的那几项 —— 它们这会儿已经是 `Interrupted`）。
    // 当前账本整个换掉：这个方法只在启动时调一次，那时候手上本来就没有东西。
    m_tasks = std::move(loaded);
    m_nextId = 1;
    for (const TaskSnapshot &task : m_tasks) {
        m_nextId = std::max(m_nextId, task.id + 1);
    }
    emit historyChanged();
    return m_tasks.size();
}

bool TaskManager::saveHistory() const {
    // 已完成的按上限截断：**留最近的**（列表已经按时间排过，这里自己数一遍，不依赖顺序）。
    QList<TaskSnapshot> keep;
    keep.reserve(m_tasks.size());
    QList<TaskSnapshot> finishedTasks;
    for (const TaskSnapshot &task : m_tasks) {
        if (isTerminalState(task.state)) {
            finishedTasks.append(task);
        } else {
            keep.append(task);
        }
    }
    std::sort(finishedTasks.begin(), finishedTasks.end(), finishedNewerFirst);
    const int limit = finishedHistoryLimit();
    for (int i = 0; i < finishedTasks.size() && i < limit; ++i) {
        keep.append(finishedTasks.at(i));
    }
    return save(resolvedHistoryPath(), keep);
}

TaskId TaskManager::begin(TaskKind kind, const QString &title, const QString &targetId,
                          const QList<TaskStep> &steps, const TaskCapabilities &capabilities, bool queued) {
    TaskSnapshot task;
    task.id = m_nextId++;
    task.kind = kind;
    task.state = queued ? TaskState::Queued : TaskState::Running;
    task.title = title;
    task.targetId = targetId;
    task.steps = steps;
    task.capabilities = capabilities;
    if (!queued) task.startedAt = QDateTime::currentDateTime();
    m_tasks.append(task);
    touch(task.id, /*persist=*/true);
    return task.id;
}

bool TaskManager::startQueued(TaskId id) {
    auto *task = find(id);
    if (task == nullptr || task->state != TaskState::Queued) return false;
    task->state = TaskState::Running;
    task->startedAt = QDateTime::currentDateTime();
    touch(id, true);
    return true;
}

TaskSnapshot *TaskManager::find(TaskId id) {
    if (id == invalidTaskId()) {
        return nullptr;
    }
    for (TaskSnapshot &task : m_tasks) {
        if (task.id == id) {
            return &task;
        }
    }
    return nullptr;
}

TaskSnapshot TaskManager::snapshot(TaskId id) const {
    for (const TaskSnapshot &task : m_tasks) {
        if (task.id == id) {
            return task;
        }
    }
    return TaskSnapshot();
}

bool TaskManager::has(TaskId id) const {
    return id != invalidTaskId() && snapshot(id).id == id;
}

void TaskManager::touch(TaskId id, bool persist) {
    if (persist) {
        saveHistory();
    }
    emit taskChanged(id);
}

/// 三个收尾入口的公共部分。
///
/// ★ 「终态只能落一次」在这里兜住：**已经收尾的、找不到的，一律回 false 并且一个信号都不发**。
///   上游那几条路（下载批次收尾、进程结束、用户点取消）都可能在同一个状态上被触发两遍 ——
///   真按第二遍再记一次，"同一件事出现两行历史""进度条上的数字比总数还大"就是从这里来的。
bool TaskManager::finish(TaskId id, TaskState state, const QString &errorSummary) {
    TaskSnapshot *task = find(id);
    if (task == nullptr || isTerminalState(task->state)) {
        return false;
    }
    task->state = state;
    task->errorSummary = errorSummary;
    task->finishedAt = QDateTime::currentDateTime();
    if (state == TaskState::Failed) {
        for (auto &step : task->steps)
            if (step.state == TaskStepState::Running) step.state = TaskStepState::Failed;
    }
    // ★ 收尾之后**只**收掉"干活时才有的"那两位，其余几位原样留着。
    //
    //   这里原来是一句 `task->capabilities = TaskCapabilities()`（M1 时确实没有可点的），
    //   后来 §4.2 的「重试」和 §4.3 的崩溃项都是**收尾之后才露**的按钮：
    //     · 「重试」：`ActivityCenterController::finish()` 在 `fail()` **之前**把 `canRetry`
    //       写进快照（`setCapabilities()` 只收活动态），结果下一句 `fail()` 走进这里又把它
    //       整份抹掉 ⇒ `Tasks::retryOffered()` 恒 false，界面上永远没有那颗按钮；
    //     · 崩溃项（`CrashReportController::report()`）建出来那一刻就带着
    //       `canExportReport` / `canOpenCrashDir`，随即 `finish()` —— 一起抹掉的话详情页
    //       上「导出错误报告」「打开崩溃报告目录」「打开日志」一颗都不露。
    //   「取消 / 暂停」反过来只在活动态有意义（`ActivityCenterController::item()` 那边也
    //   再按状态挡一道），收尾时清掉它们是这里唯一该做的事。
    task->capabilities.canCancel = false;
    task->capabilities.canPause = false;
    task->doneUnits = (state == TaskState::Succeeded) ? task->totalUnits : task->doneUnits;
    saveHistory();
    emit taskChanged(id);
    emit taskFinished(id, state == TaskState::Succeeded);
    emit historyChanged();
    return true;
}

bool TaskManager::succeed(TaskId id) {
    return finish(id, TaskState::Succeeded, QString());
}

bool TaskManager::fail(TaskId id, const QString &errorSummary) {
    return finish(id, TaskState::Failed, errorSummary);
}

bool TaskManager::cancel(TaskId id) {
    return finish(id, TaskState::Cancelled, QString());
}

bool TaskManager::acknowledgeAttention(TaskId id) {
    auto *task = find(id);
    if (task == nullptr || task->attentionAcknowledged ||
        (task->state != TaskState::Failed && task->state != TaskState::Interrupted)) return false;
    task->attentionAcknowledged = true;
    touch(id, true);
    return true;
}

bool TaskManager::setDetail(TaskId id, const QString &detail) {
    TaskSnapshot *task = find(id);
    if (task == nullptr || !isActiveState(task->state) || task->detail == detail) {
        return false;
    }
    task->detail = detail;
    // **不落盘**：详情是"此刻在干什么"，盘上那一份是摘要（见 TaskHistory.hpp）。
    touch(id, /*persist=*/false);
    return true;
}

bool TaskManager::setSessionId(TaskId id, int sessionId) {
    // 只活在内存里（理由见 TaskSnapshot::sessionId）：这里 touch 的 persist 是 false。
    // 收尾之后也允许补：崩溃报告那一项可能在进程已经退出之后才和会话对上。
    //
    // ★ 一项任务只对**一份**会话。已经对上了还要换个号 = 调用方那边把两件事搞混了，
    //   这里不替它改口（改了口之后"这一项说的是哪一次运行"就再也说不清）。
    TaskSnapshot *task = find(id);
    if (task == nullptr || sessionId <= 0 || task->sessionId != 0) {
        return false;
    }
    task->sessionId = sessionId;
    touch(id, /*persist=*/false);
    return true;
}

bool TaskManager::setSteps(TaskId id, const QList<TaskStep> &steps) {
    TaskSnapshot *task = find(id);
    if (task == nullptr || !isActiveState(task->state)) {
        return false;
    }
    task->steps = steps;
    touch(id, /*persist=*/false);
    return true;
}

bool TaskManager::setStepState(TaskId id, int index, TaskStepState state, const QString &note) {
    TaskSnapshot *task = find(id);
    if (task == nullptr || !isActiveState(task->state) || index < 0
        || index >= task->steps.size()) {
        return false;
    }
    task->steps[index].state = state;
    task->steps[index].note = note;
    touch(id, /*persist=*/false);
    return true;
}

bool TaskManager::setProgress(TaskId id, qint64 doneUnits, qint64 totalUnits) {
    TaskSnapshot *task = find(id);
    if (task == nullptr || !isActiveState(task->state)) {
        return false;
    }
    if (task->doneUnits == doneUnits && task->totalUnits == totalUnits) {
        return false;  // 一模一样的一帧不用再喊一遍（下载进度是每 200 ms 来一次的）
    }
    task->doneUnits = doneUnits;
    task->totalUnits = totalUnits;
    touch(id, /*persist=*/false);
    return true;
}

bool TaskManager::setBytes(TaskId id, qint64 bytesDone, qint64 bytesTotal) {
    TaskSnapshot *task = find(id);
    if (task == nullptr || !isActiveState(task->state)) {
        return false;
    }
    if (task->bytesDone == bytesDone && task->bytesTotal == bytesTotal) {
        return false;
    }
    task->bytesDone = bytesDone;
    task->bytesTotal = bytesTotal;
    touch(id, /*persist=*/false);
    return true;
}

bool TaskManager::setCapabilities(TaskId id, const TaskCapabilities &capabilities) {
    TaskSnapshot *task = find(id);
    if (task == nullptr || !isActiveState(task->state)) {
        return false;
    }
    task->capabilities = capabilities;
    touch(id, /*persist=*/false);
    return true;
}

void TaskManager::clearFinished() {
    const int before = m_tasks.size();
    m_tasks.removeIf([](const TaskSnapshot &task) { return isTerminalState(task.state); });
    if (m_tasks.size() == before) {
        return;  // 本来就没有已完成的东西：不落盘也不发信号
    }
    saveHistory();
    emit historyChanged();
}

QList<TaskSnapshot> TaskManager::active() const {
    QList<TaskSnapshot> result;
    for (const TaskSnapshot &task : m_tasks) {
        if (isActiveState(task.state)) {
            result.append(task);
        }
    }
    // 台账里的顺序就是发起的顺序 —— 只有一处往里面加东西（`begin()`），所以它天然有序。
    return result;
}

QList<TaskSnapshot> TaskManager::finished() const {
    QList<TaskSnapshot> result;
    for (const TaskSnapshot &task : m_tasks) {
        if (isTerminalState(task.state)) {
            result.append(task);
        }
    }
    std::sort(result.begin(), result.end(), finishedNewerFirst);
    return result;
}

QList<TaskSnapshot> TaskManager::all() const {
    QList<TaskSnapshot> result = active();
    result.append(finished());
    return result;
}

}  // namespace Tasks
