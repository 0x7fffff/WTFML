/**
 * @file DownloadTaskLedgerController.hpp
 * @brief 「正在跑的这一批下载/安装是哪一件任务、失败清单归谁」—— 原来摊在 `MainWindow`
 *        的七格成员上（`m_downloadTaskId` / `m_downloadBatchOwnerTaskId` /
 *        `m_downloadParentTask` / `m_downloadParentStep` / `m_resourceBatchLabel` /
 *        `m_downloadCancelInFlight` / `m_progressThrottle`）和三条函数里
 *        （`beginDownloadTask()` / `setDownloadTaskParent()` 与 `setDownloadUiBusy(false)`
 *        的那半截）。
 *
 * 【为什么这七格是一套】
 *
 * 它们只在**同一段时间**里有意义：从 `setDownloadUiBusy(true)` 起到那一条终态信号为止。
 * 分开看每一格都像个小开关，合起来才是"这一批下载"这个对象：
 *
 *  * `task` —— 账本里代表这一批的那一行（进度、取消、失败都挂在它身上）；
 *  * `owner` —— 那一格失败清单归谁（「重试」资格的出处，见 `finishTask()` 那段的 NOTE）；
 *  * `parent` / `parentStep` —— 这一批是**哪位用户意图**底下的一步（实例更新），
 *    没有父任务时它就是独立的一行；
 *  * `resourceLabel` —— 下载页那几类资源按名字认领终态信号（`resourceDownloadFinished`）；
 *  * `userCancelInFlight` —— 这一发终态是不是用户自己按的取消（取消与真失败同一条信号）；
 *  * `progressThrottle` —— 进度按网络分片到达，200 ms 才刷一次界面。
 *
 * 【名字为什么不是 `DownloadBatchController`】
 *
 * 那个名字已经被**检查点/断点恢复**那一层占了（`src/controllers/DownloadBatchController.*`：
 * 接 `DownloadManager` 的 receipt、暂停继续、续装意图）。这一层只管**账本上那一行**，
 * 两件事不重叠 —— 别合并，也别改名去凑。
 *
 * 【这一层一条判据都不自己写】
 *
 * 账本是 `ActivityCenterController`；"这一批是替谁做的"那三个 id 仍在主窗口上；能不能取消在
 * `VersionManager`。这一层只把"这一批是谁"记下来，并按它收尾。
 */

#pragma once

#include "tasks/TaskId.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QString>

class MainWindow;

class DownloadTaskLedgerController final : public QObject {
public:
    explicit DownloadTaskLedgerController(MainWindow *owner);

    // ---------- 这一批在账本里的那一行 ----------
    /// 当前这一批的任务号（无效号 = 没有这一批在跑）。**一批一个号**：从
    /// `setDownloadUiBusy(true)` 起、到那一条终态信号为止。下载页那几类资源（模组 /
    /// 数据包 / 资源包 / 光影 / 整合包）和「重试」那一批也在这上面挂一件事 —— 它们同样进
    /// 任务中心：下大文件时用户看得到进度，失败了也有地方能点「重试」。
    Tasks::TaskId task() const { return m_taskId; }
    /// 只换号（父任务底下另起的子行、资源下载那一类先记账后认领）。
    void setTask(Tasks::TaskId task) { m_taskId = task; }
    /// 认领一个**已经建好**的号（资源下载 / 「重试」那一批）：它同时成为失败清单的主人。
    void adopt(Tasks::TaskId task);
    /// 只松开号。
    void clearTask() { m_taskId = Tasks::invalidTaskId(); }
    /// 号、主人、资源名一起松开（这一批收尾了，三者只在这一批里成对有效）。
    void clear();

    // ---------- 失败清单归谁（「重试」资格的出处） ----------
    Tasks::TaskId owner() const { return m_ownerTaskId; }
    /// 这一件任务是不是"这一批的主人"（`finishTask()` 判重的唯一出处）。
    bool owns(Tasks::TaskId id) const {
        return id != Tasks::invalidTaskId() && id == m_ownerTaskId;
    }
    void setOwner(Tasks::TaskId owner) { m_ownerTaskId = owner; }
    /// 主人的号**只在它还等于 @p owner 时**才松开 —— 别的任务收尾不许把资格挪走。
    void clearOwnerIf(Tasks::TaskId owner);
    void clearOwner() { m_ownerTaskId = Tasks::invalidTaskId(); }

    // ---------- 起手 / 收尾 ----------
    /// 界面刚置忙那一刻：按"这一批是替谁做的"分档，在账本里记一件任务（原
    /// `MainWindow::beginDownloadTask()`）。已经有号且那一行还活着时什么都不做。
    void begin();
    /// 界面刚置闲那一刻：**兜底**收尾（数据层哪一次没发终态时，账本上会留一件永远
    /// 在跑的任务），并松开号。对已经收过尾的那一件 `finishTask()` 什么都不做
    /// （终态只落一次，见 `Tasks::TaskManager::finish()`）。
    void finish(const QString &fallbackError);

    // ---------- 挂在哪位用户意图底下 ----------
    /// 让接下来那一批下载挂到 @p parent 的第 @p step 步底下。父任务收尾由调用方负责 ——
    /// 这一批只是那件意图里的一步（「实例更新」把核心准备并进同一个用户意图：源实例更新
    /// 只该在任务中心出现一行）。
    void setParent(Tasks::TaskId parent, int step);
    Tasks::TaskId parent() const { return m_parentTask; }
    int parentStep() const { return m_parentStep; }
    /// 父任务自己收尾了：把这一批退回"独立成行"（否则下一批会挂到一件已经收掉的任务
    /// 底下，那一行永远等不到终态）。
    void dropParentIf(Tasks::TaskId closed);

    // ---------- 下载页那几类资源的名字 ----------
    /// `resourceDownloadFinished(label, …)` 回来时按它认领那件任务。空 = 当前没有
    /// 资源下载在跑。
    const QString &resourceLabel() const { return m_resourceLabel; }
    void setResourceLabel(const QString &label) { m_resourceLabel = label; }

    // ---------- 用户自己按的取消 ----------
    /// 取消和真失败走**同一条**终态信号，槽里分不出来；这一位只在那一小段里为真。
    /// 版本下载那一路是同步收尾的（信号在 `cancelActiveDownload()` 返回之前发完），
    /// 所以置真—调用—还原就够；实例更新那一路要留到它自己的终态信号回来为止，
    /// 所以给的是一对裸的 set，不是 RAII。
    bool userCancelInFlight() const { return m_userCancelInFlight; }
    void setUserCancelInFlight(bool inFlight) { m_userCancelInFlight = inFlight; }

    // ---------- 进度节流 ----------
    /// 进度按网络分片到达（5000 多个 asset）：距上一次刷新不到 200 ms 就不刷（返回 false）。
    /// 不这么做的话界面会疯狂重排状态栏，反而卡住。
    bool takeProgressTick();
    /// 新一批起手时把节流清掉，否则头 200 ms 的进度会丢。
    void resetProgressThrottle() { m_progressThrottle.invalidate(); }

private:
    MainWindow &m_owner;
    /// 这一批在账本里的那一行（无效号 = 没有这一批）。
    Tasks::TaskId m_taskId = Tasks::invalidTaskId();
    /// 那一格失败清单**归账本里的哪一件任务**（"重试"资格的出处，见 `finishTask()`）。
    ///
    /// 绝大多数时候它就等于 `m_taskId`；两处不能现读的地方靠它：
    /// ① 「更新中心」那一批挂在主窗口的 `m_updateBatch.task` 上，而那一批收尾时
    /// `m_updateBatch` 已经先被清空了；② 资源下载 / 重试那一批的号会在成功收尾后立刻清掉。
    /// 没有它的话，任何一件**别的**任务（启动、导出、服务端安装、复制实例…）在这一刻收尾，
    /// 都会把"这份失败清单是谁的"改成它自己 —— 于是真失败那一件上的「重试」消失，而
    /// 点别人的「重试」重做的却是这一批文件。
    Tasks::TaskId m_ownerTaskId = Tasks::invalidTaskId();
    /// 这一批要挂到哪个父任务底下（无效号 = 独立成行）与它作第几步。
    Tasks::TaskId m_parentTask = Tasks::invalidTaskId();
    int m_parentStep = -1;
    /// 资源下载那一批的 label（见 `resourceLabel()`）。
    QString m_resourceLabel;
    /// 这一发终态是不是**用户自己按的取消**（只在那一个同步调用栈里为真）。
    ///
    /// `VersionManager::cancelActiveDownload()` 收尾时走的是和"真失败"**同一条**终态信号
    /// （`versionDownloadFinished` / `resourceDownloadFinished`，success=false），于是那几个
    /// 槽把它当成失败：账本记成 Failed、点亮底栏那颗"没看过的失败"红点，还当场弹一句
    /// 「下载失败」—— 用户刚点完取消。账本里本来就有一档 `Cancelled`（服务端安装那条路
    /// 就是这么收的），这里只是补上"哪一发是取消"这个**只有主窗口知道**的事实。
    bool m_userCancelInFlight = false;
    /// 进度节流（见 `takeProgressTick()`）。
    QElapsedTimer m_progressThrottle;
};
