/**
 * @file TaskHistory.cpp
 * @brief 任务历史落盘的实现（形状与取舍见头文件）。
 * @author 0x7fffff
 * @date 2026-10-12
 */

#include "TaskHistory.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "util/AppPaths.hpp"

namespace Tasks {
namespace {

/// 盘上最多留几条**已完成**的。50 条足够回答"上一次那次更新怎么样了"，而这份文件是每完成
/// 一件事就重写一遍的小文件（比留着几千行更值得）。
constexpr int kFinishedLimit = 50;

/// 种类 / 状态在盘上的**字面量**。写成 `QStringLiteral("running")` 这种一眼能读懂的词，
/// 不写 `int(state)` —— 后者加一个枚举值时会把已有那份文件里的数字全读歪。
const char *kindKey(TaskKind kind) {
    switch (kind) {
    case TaskKind::Install:
        return "install";
    case TaskKind::Download:
        return "download";
    case TaskKind::Launch:
        return "launch";
    case TaskKind::Import:
        return "import";
    case TaskKind::Export:
        return "export";
    case TaskKind::Java:
        return "java";
    case TaskKind::Integrity:
        return "integrity";
    case TaskKind::Other:
        break;
    }
    return "other";
}

TaskKind kindFromKey(const QString &key) {
    if (key == QLatin1String("install")) {
        return TaskKind::Install;
    }
    if (key == QLatin1String("download")) {
        return TaskKind::Download;
    }
    if (key == QLatin1String("launch")) {
        return TaskKind::Launch;
    }
    if (key == QLatin1String("import")) {
        return TaskKind::Import;
    }
    if (key == QLatin1String("export")) {
        return TaskKind::Export;
    }
    if (key == QLatin1String("java")) {
        return TaskKind::Java;
    }
    if (key == QLatin1String("integrity")) {
        return TaskKind::Integrity;
    }
    return TaskKind::Other;
}

const char *stateKey(TaskState state) {
    switch (state) {
    case TaskState::Queued:
        return "queued";
    case TaskState::Running:
        return "running";
    case TaskState::WaitingUser:
        return "waiting-user";
    case TaskState::Paused:
        return "paused";
    case TaskState::Succeeded:
        return "succeeded";
    case TaskState::Failed:
        return "failed";
    case TaskState::Cancelled:
        return "cancelled";
    case TaskState::Interrupted:
        return "interrupted";
    }
    return "interrupted";
}

TaskState stateFromKey(const QString &key) {
    if (key == QLatin1String("queued")) {
        return TaskState::Queued;
    }
    if (key == QLatin1String("running")) {
        return TaskState::Running;
    }
    if (key == QLatin1String("waiting-user")) {
        return TaskState::WaitingUser;
    }
    if (key == QLatin1String("paused")) {
        return TaskState::Paused;
    }
    if (key == QLatin1String("succeeded")) {
        return TaskState::Succeeded;
    }
    if (key == QLatin1String("failed")) {
        return TaskState::Failed;
    }
    if (key == QLatin1String("cancelled")) {
        return TaskState::Cancelled;
    }
    // 认不出来的一律当"已中断"：那是**唯一**一种"我们知道自己在猜"还猜得对的答案 ——
    // 认不出来的多半就是以后新加的那种活动状态，而它在这一次启动里确实没在跑。
    return TaskState::Interrupted;
}

}  // namespace

QString defaultHistoryPath() {
    return AppPaths::minecraftDir() + QStringLiteral("/wtfml.tasks");
}

int finishedHistoryLimit() {
    return kFinishedLimit;
}

QByteArray toJson(const QList<TaskSnapshot> &tasks) {
    QJsonArray array;
    for (const TaskSnapshot &task : tasks) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), QString::number(task.id));
        object.insert(QStringLiteral("kind"), QLatin1String(kindKey(task.kind)));
        object.insert(QStringLiteral("state"), QLatin1String(stateKey(task.state)));
        object.insert(QStringLiteral("title"), task.title);
        if (!task.targetId.isEmpty()) {
            object.insert(QStringLiteral("targetId"), task.targetId);
        }
        if (task.startedAt.isValid()) {
            object.insert(QStringLiteral("startedAt"),
                          task.startedAt.toString(Qt::ISODateWithMs));
        }
        if (task.finishedAt.isValid()) {
            object.insert(QStringLiteral("finishedAt"),
                          task.finishedAt.toString(Qt::ISODateWithMs));
        }
        if (!task.errorSummary.isEmpty()) {
            object.insert(QStringLiteral("error"), task.errorSummary);
        }
        array.append(object);
    }
    return QJsonDocument(array).toJson(QJsonDocument::Indented);
}

QList<TaskSnapshot> fromJson(const QByteArray &bytes, bool *ok) {
    QList<TaskSnapshot> tasks;
    bool parsed = false;
    const QJsonDocument document = QJsonDocument::fromJson(bytes);
    if (document.isArray()) {
        parsed = true;
        const QJsonArray array = document.array();
        tasks.reserve(array.size());
        for (const QJsonValue &value : array) {
            if (!value.isObject()) {
                continue;
            }
            const QJsonObject object = value.toObject();
            TaskSnapshot task;
            // 号是**字符串**存的（quint64 进 QJsonValue 会被当 double，超过 2^53 就不是
            // 原值了）。眼下号小得很，可这个坑不该等到号真的变大了才踩。
            task.id = object.value(QStringLiteral("id")).toString().toULongLong();
            if (task.id == invalidTaskId()) {
                continue;  // 没有号的那一行认不出是谁，当它不在
            }
            task.kind = kindFromKey(object.value(QStringLiteral("kind")).toString());
            task.state = stateFromKey(object.value(QStringLiteral("state")).toString());
            task.title = object.value(QStringLiteral("title")).toString();
            task.targetId = object.value(QStringLiteral("targetId")).toString();
            task.errorSummary = object.value(QStringLiteral("error")).toString();
            task.startedAt = QDateTime::fromString(
                object.value(QStringLiteral("startedAt")).toString(), Qt::ISODateWithMs);
            task.finishedAt = QDateTime::fromString(
                object.value(QStringLiteral("finishedAt")).toString(), Qt::ISODateWithMs);
            tasks.append(task);
        }
    }
    if (ok != nullptr) {
        *ok = parsed;
    }
    return tasks;
}

void markInterrupted(QList<TaskSnapshot> &tasks) {
    for (TaskSnapshot &task : tasks) {
        if (!isTerminalState(task.state)) {
            task.state = TaskState::Interrupted;
            // ★ 结束时间**故意不补**（原来是补 `now`，2026-10-12 撤掉）。
            //
            //   那不是"读回来时顺手填一个"，那是**编一个时间**：盘上那条是上一回过活的，
            //   它到底什么时候停的一概不知。补上 `now` 有两个看得见的坏处：
            //     · 收起态那一格判定"刚完成"看的就是 `finishedAt`（见
            //       `Tasks::collapsedSummary()` 的 `recentWindowMs`），补出来的时间让它
            //       在**每一次开启动器**的时候冒充"刚刚完成的"，底栏会顶着一条上次没跑完
            //       的任务说「（已完成）」—— 它明明是失败的形状；
            //     · 任务中心"已完成"那一栏的时分也跟着一起说谎。
            //   留着无效时间戳，两处都退回"不知道什么时候完的"（那一行不写时间），
            //   而"没跑完"这件事本身由 `Interrupted` 这个状态说，不靠时间戳说。
            task.finishedAt = QDateTime();
        }
    }
}

QList<TaskSnapshot> load(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QList<TaskSnapshot>();
    }
    const QByteArray bytes = file.readAll();
    file.close();
    QList<TaskSnapshot> tasks = fromJson(bytes, nullptr);
    markInterrupted(tasks);
    return tasks;
}

bool save(const QString &path, const QList<TaskSnapshot> &tasks) {
    const QFileInfo info(path);
    if (!info.absoluteDir().exists() && !QDir().mkpath(info.absolutePath())) {
        return false;
    }
    // QSaveFile：写侧走临时文件 + 原子改名，理由见头文件那段（写到一半断电最多丢掉这一次）。
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray bytes = toJson(tasks);
    if (file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

}  // namespace Tasks
