/**
 * @file TaskSnapshot.hpp
 * @brief 一项任务的**完整样子**，以及"底栏收起态该显示谁"那几条纯规则。
 * @author 0x7fffff
 * @date 2026-10-12
 *
 * coding plan §2.2：「主任务与步骤分开」。一项任务 = 一个用户意图（"安装 1.21.4 Fabric"、
 * "启动 生存整合包"），它内部**可以有几千个下载作业** —— 那些是步骤，不是任务。
 *
 * 【这一层是什么、不是什么】
 *
 *   * 是：任务的**形状**（种类 / 状态 / 进度 / 步骤 / 能力）和几条纯函数（谁优先、百分比
 *     怎么算、收起态显示谁）。
 *   * 不是：任务的账本。谁在跑、跑到哪儿、什么时候结束，全在 `TaskManager` 里。
 *
 * ★ 这个头文件**只有值**：一个 QObject 都没有、一个信号都没有，也不碰盘。所以 `verify`
 *   能直接喂一组快照进去钉那几条规则（见 `tests/main.cpp` 的 AL 节）。
 *
 * 【为什么状态是一个枚举，不是几个 bool】
 *
 * coding plan §2.1 点名的：「不要再用几个 bool 拼状态」。`(inFlight, ok, cancelled)` 那种
 * 组合能拼出 8 种，其中一半是没有意义的（"在跑但成功"、"取消且失败"），而每加一个场景就
 * 要多立一条"这两个同时为真时按谁"的暗规则 —— 那些规则没人找得到，只能靠读代码。
 */

#pragma once

#include <QDateTime>
#include <QList>
#include <QSet>
#include <QString>

#include "TaskId.hpp"

namespace Tasks {

/// 这件事是干什么的。**只用来分类和排优先级**，界面上的按钮不靠它猜（见
/// `TaskCapabilities` 那段）。
enum class TaskKind {
    Install,    // 装一个版本 / 给实例装加载器 / 补全一个实例（repair）
    Download,   // 下资源（模组 / 资源包 / 光影 / 数据包）
    Launch,     // 启动一个实例
    Import,     // 导入整合包
    Export,     // 导出整合包
    Java,       // Java 的扫描 / 下载
    Integrity,  // 完整性扫描
    Crash,      // 游戏异常退出之后补的那一项（§4.3）。**一建出来就是终态**，从不"在跑"
    Backup,     // 打一份存档 / 实例快照（§4.4），或者从一份备份还原回去
    Other,
};

/// 任务的状态。八个，一个不多一个不少 —— 和 coding plan §2.1 那段逐字对齐。
enum class TaskState {
    Queued,       // 排着队，还没轮到（第一版只有单批次，这个状态基本只在盘上那半出现）
    Running,      // 正在跑
    WaitingUser,  // **卡在等用户**（弹了个提示、要用户选一下）。收起态里它排第一
    Paused,       // 暂停。第一版**实现不了**（见 §3.2「第一版不做暂停」），枚举先留着
    Succeeded,    // 成功
    Failed,       // 失败（带 `TaskSnapshot::errorSummary` 那句原因）
    Cancelled,    // 用户取消的
    Interrupted,  // 启动器上次异常退出时它还没跑完（**读盘时**才出现，见 TaskHistory）
};

/// 一步的状态。
enum class TaskStepState {
    Pending,
    Running,
    Done,
    Failed,
};

/// 任务里的一步（coding plan §2.2：「元数据、下载、校验、安装器、收尾」）。
struct TaskStep {
    QString name;                      // 「校验文件」「准备 Java」
    TaskStepState state = TaskStepState::Pending;
    QString note;                      // 失败原因 / 这一步的补充说明，可空
};

/// 这项任务**允许**什么操作。按钮不靠任务种类猜（coding plan §2.2 末段）。
///
/// ★ 为什么要有这一层而不是"看 kind 出按钮"：同是 `Install`，Forge 安装器跑到"不可安全
///   中断的那一段"时**不能取消**（§3.2），而普通下载任何时候都能。拿 kind 猜的话，界面
///   就得把"什么时候不能取消"再推一遍，而那个知识只在下命令的那一层手上有。
struct TaskCapabilities {
    bool canCancel = false;
    bool canPause = false;   // 第一版恒 false
    bool canRetry = false;   // M1 里恒 false（取消/重试是 M2）
    bool canOpenTarget = false;
    bool canOpenLog = false;
    /// 「导出错误报告」（§4.3）。**只有游戏异常退出那一项**为真 —— 报告要的是一份会话事实
    /// （Java 版本 / 内存参数 / 退出码），别的那几类任务没有这些东西。
    bool canExportReport = false;
    /// 「打开崩溃报告目录」（§4.3）。同样只有游戏异常退出那一项为真。
    bool canOpenCrashDir = false;
};

/// 一项任务在**某一刻**的样子。
struct TaskSnapshot {
    TaskId id = invalidTaskId();
    TaskKind kind = TaskKind::Other;
    TaskState state = TaskState::Queued;
    QString title;       // 「安装 1.21.4 Fabric」
    QString detail;      // 「正在下载资源对象…」
    QString targetId;    // 实例 id / 服务端 id，可空
    qint64 doneUnits = 0;
    qint64 totalUnits = 0;  // <= 0 表示不可测（**不许编造百分比**，§2.1 末段）
    qint64 bytesDone = 0;
    qint64 bytesTotal = 0;  // <= 0 表示未知
    QDateTime startedAt;
    QDateTime finishedAt;
    QList<TaskStep> steps;
    TaskCapabilities capabilities;
    QString errorSummary;   // 失败那一句话（只有 Failed 有）
};

// —— 下面几条都是**纯函数**（不看时钟、不看盘、不看 QObject）——

/// 还在进行中（会占着"当前工作"那一栏）。
inline bool isActiveState(TaskState state) {
    return state == TaskState::Queued || state == TaskState::Running
           || state == TaskState::WaitingUser || state == TaskState::Paused;
}

/// 已经收尾了（会进"已完成"那一栏 / 任务历史）。
inline bool isTerminalState(TaskState state) {
    return !isActiveState(state);
}

/// 这一项报得出百分比吗。`totalUnits <= 0` 就是**不可测** —— 返回 -1，调用方画不确定态。
///
/// ★ 这条是 §3.4 验收里那句「无 Content-Length 的 Fabric 下载显示'已下载多少'，不显示假
///   百分比」的唯一出处。四分五裂地各写一遍"总数小于等于 0 就别画百分号"迟早会漏一处。
inline int percentOf(const TaskSnapshot &task) {
    if (task.totalUnits <= 0) {
        return -1;
    }
    const qint64 done = task.doneUnits < 0 ? 0 : task.doneUnits;
    const int percent = int(done * 100 / task.totalUnits);
    return percent < 0 ? 0 : (percent > 100 ? 100 : percent);
}

/// 收起态那格"显示谁"的优先级（**数值小的先显示**）；返回 -1 = 这一项不参与收起态。
///
/// coding plan §3.1 给的那一串：`WaitingUser > Failed(未读) > Launch > Install/Repair >
/// Download > Export`。**这只是"收起时展示谁"，不会隐藏列表里的其他任务**（原话）。
///
/// @param unreadFailure 这一项是"用户还没看过的失败"。未读的失败比"正在启动"还靠前 ——
///        用户没看见的那件事最该先被他看见；看过了就退回它那一类自己的位置。
///        成功/取消/中断那三档**只在 `recentWindowMs` 之内**才露面（§3.1「完成任务短暂
///        显示后收进历史」），由 `collapsedSummary()` 那边判，这里不管时间。
inline int summaryPriority(const TaskSnapshot &task, bool unreadFailure) {
    if (isActiveState(task.state)) {
        if (task.state == TaskState::WaitingUser) {
            return 0;
        }
        switch (task.kind) {
        case TaskKind::Launch:
            return 2;
        case TaskKind::Install:
            return 3;
        case TaskKind::Download:
            return 4;
        case TaskKind::Import:
            return 3;  // 导入整合包 = "装一整套东西"，和安装同一档
        case TaskKind::Export:
        // `Backup`（§4.4）和导出同一档：都是"本地打包一个目录"，只读盘、没有网络、
        // 也就没有"等着它"的那种紧迫感 —— 让位给上面那几档。
        case TaskKind::Backup:
            return 5;
        case TaskKind::Java:
        case TaskKind::Integrity:
        case TaskKind::Other:
        // `Crash` 是"一建出来就已经结束"的一项（见枚举上那句），走到这儿只可能是盘上
        // 读回来的一份坏数据；按最低那一档摆着，不抢任何一格。
        case TaskKind::Crash:
            return 6;
        }
        return 6;
    }
    if (task.state == TaskState::Failed) {
        // 未读的失败排第二（在 WaitingUser 之后、Launch 之前），看过的失败退到最后一档
        // —— 它还在（最近那个时间窗里），只是不再抢那一格。
        return unreadFailure ? 1 : 8;
    }
    return 8;  // Succeeded / Cancelled / Interrupted：只在"刚完成"那个窗口里露一下
}

/// 收起态那一格要画的东西。
struct TaskSummary {
    TaskId id = invalidTaskId();  // 0 = 一个任务都没有（画「暂无后台任务」）
    TaskState state = TaskState::Queued;
    TaskKind kind = TaskKind::Other;
    QString title;
    QString detail;
    int percent = -1;      // < 0 = 不可测
    int extraCount = 0;    // 「另有 N 项」（除了显示的这一项以外，还有几项在进行中）
    bool hasFailure = false;  // 红点：用户还没看过的失败任务存在
};

/**
 * @brief 收起态显示谁 —— **唯一出处**（coding plan §3.1）。
 *
 * @param tasks 账本里所有任务的快照（顺序不限）。
 * @param unreadFailures 用户**还没看过**的那几件失败任务的号。
 * @param now 现在。**传进来是为了可测** —— 这一层不看时钟。
 * @param recentWindowMs 「刚完成」还能占着那一格多久（§3.1「完成任务短暂显示后收进历史」）。
 *        传 0 = 完全不显示已完成的任务。
 *
 * 规则（§3.1 那三条，逐条对上）：
 *   · 一项都没有 ⇒ `id == 0`（调用方画低强调度的「暂无后台任务」，**不画空进度条**）；
 *   · 有活动的 ⇒ 取优先级最高的那一项；同一档取**后发起的**（同一档里后点的那件事才是
 *     用户刚做的）；剩下的活动任务数写进 `extraCount`；
 *   · 红点看的是**有没有未读的失败**，跟显示的是哪一项无关 —— 正在下载而上一件事失败了，
 *     两个信号都该看得见。
 */
inline TaskSummary collapsedSummary(const QList<TaskSnapshot> &tasks,
                                    const QSet<TaskId> &unreadFailures,
                                    const QDateTime &now,
                                    qint64 recentWindowMs) {
    TaskSummary summary;
    summary.hasFailure = !unreadFailures.isEmpty();

    int activeCount = 0;
    for (const TaskSnapshot &task : tasks) {
        if (isActiveState(task.state)) {
            ++activeCount;
        }
    }

    const TaskSnapshot *best = nullptr;
    int bestPriority = 0;
    QDateTime bestStarted;
    for (const TaskSnapshot &task : tasks) {
        int priority = -1;
        const bool unreadFailure =
            (task.state == TaskState::Failed) && unreadFailures.contains(task.id);
        if (isActiveState(task.state) || unreadFailure) {
            priority = summaryPriority(task, unreadFailure);
        } else if (task.state == TaskState::Succeeded || task.state == TaskState::Failed
                   || task.state == TaskState::Cancelled
                   || task.state == TaskState::Interrupted) {
            // 「刚完成」那一档：只在时间窗里露面。没时间戳（理论上不会）就当它早过期。
            const bool recent =
                recentWindowMs > 0 && task.finishedAt.isValid()
                && task.finishedAt.msecsTo(now) >= 0 && task.finishedAt.msecsTo(now) <= recentWindowMs;
            if (recent) {
                priority = summaryPriority(task, unreadFailure);
            }
        }
        if (priority < 0) {
            continue;
        }
        if (best == nullptr || priority < bestPriority
            || (priority == bestPriority && task.startedAt > bestStarted)) {
            best = &task;
            bestPriority = priority;
            bestStarted = task.startedAt;
        }
    }

    if (best == nullptr) {
        return summary;
    }
    summary.id = best->id;
    summary.state = best->state;
    summary.kind = best->kind;
    summary.title = best->title;
    summary.detail = best->detail;
    summary.percent = percentOf(*best);
    // 「另有 N 项」数的是**还在进行中**的那几项（显示的这一项是活动的话要减掉它自己）。
    summary.extraCount = activeCount - (isActiveState(best->state) ? 1 : 0);
    if (summary.extraCount < 0) {
        summary.extraCount = 0;
    }
    return summary;
}

}  // namespace Tasks
