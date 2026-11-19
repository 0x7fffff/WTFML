#include "controllers/DownloadTaskLedgerController.hpp"

#include "MainWindow.hpp"
#include "controllers/ActivityCenterController.hpp"
#include "controllers/InstallOccupancy.hpp"
#include "tasks/TaskManager.hpp"

namespace {
/// 进度刷新的最短间隔（毫秒）：进度按网络分片到达，一次安装能来几千发。
constexpr qint64 kProgressRefreshIntervalMs = 200;
}

DownloadTaskLedgerController::DownloadTaskLedgerController(MainWindow *owner)
    : QObject(owner), m_owner(*owner) {}

void DownloadTaskLedgerController::adopt(Tasks::TaskId task) {
    // 号与"主人"是同一个：这一批的失败清单就归它（见 `finishTask()`）。
    m_taskId = task;
    m_ownerTaskId = task;
}

void DownloadTaskLedgerController::clear() {
    // 号、资源名、主人一起松开：三者在账本里只在这一批之内成对有效。**不能**只清号 ——
    // 失败清单还攥着的时候，下一件任务收尾会把自己认成主人（见 `owner()` 那段）。
    m_taskId = Tasks::invalidTaskId();
    m_ownerTaskId = Tasks::invalidTaskId();
    m_resourceLabel.clear();
}

void DownloadTaskLedgerController::clearOwnerIf(Tasks::TaskId owner) {
    if (m_ownerTaskId == owner) {
        m_ownerTaskId = Tasks::invalidTaskId();
    }
}

void DownloadTaskLedgerController::setParent(Tasks::TaskId parent, int step) {
    m_parentTask = parent;
    m_parentStep = step;
}

void DownloadTaskLedgerController::dropParentIf(Tasks::TaskId closed) {
    if (closed == Tasks::invalidTaskId() || closed == m_parentTask) {
        m_parentTask = Tasks::invalidTaskId();
        m_parentStep = -1;
    }
}

bool DownloadTaskLedgerController::takeProgressTick() {
    if (m_progressThrottle.isValid() && m_progressThrottle.elapsed() < kProgressRefreshIntervalMs) {
        return false;
    }
    m_progressThrottle.start();
    return true;
}

void DownloadTaskLedgerController::begin() {
    // 同一批里 `setDownloadUiBusy(true)` 只该来一次；但要是上一批的号还留着（理论上不该），
    // 也不能拿它当新的使 —— 那一行的进度会从上一批的位置爬。
    if (m_taskId != Tasks::invalidTaskId() && m_owner.m_activityCenter->tasks()->has(m_taskId)) {
        return;
    }
    // 这一批到底是哪一类活：判据就是界面自己记的那四档占用（它们在**置忙之前**就写好了），
    // 而收尾那几条路（`DownloadCompletionController::onFinished`）用的也是它们 ——
    // 所以起手和收尾的分档天然一致，不会出现"起手记成安装、收尾按下载报"。
    // 优先级（导入 > 安装 > 补全 > 预取）只在 `InstallOccupancy::active()` 里写一遍。
    const InstallOccupancy::Active active = m_owner.m_installOccupancy.active();
    Tasks::TaskKind kind = Tasks::TaskKind::Download;
    QString title;
    switch (active.batch) {
    case InstallOccupancy::Batch::Import:
        kind = Tasks::TaskKind::Import;  // 导入 = "装一整套东西"，和安装同一档优先级
        title = MainWindow::tr("导入整合包 %1").arg(m_owner.instanceDisplayName(active.instanceId));
        break;
    case InstallOccupancy::Batch::Install:
        kind = Tasks::TaskKind::Install;
        title = MainWindow::tr("安装 %1").arg(m_owner.instanceDisplayName(active.instanceId));
        break;
    case InstallOccupancy::Batch::Repair:
        kind = Tasks::TaskKind::Install;
        title = MainWindow::tr("校验并补全 %1").arg(m_owner.instanceDisplayName(active.instanceId));
        break;
    case InstallOccupancy::Batch::Prefetch:
        // 预取 = "把这些文件拿下来"，不是装东西；它下的是共享的 assets/asset_director。
        kind = Tasks::TaskKind::Download;
        title = MainWindow::tr("预取「%1」的首次启动资源").arg(m_owner.instanceDisplayName(active.instanceId));
        break;
    case InstallOccupancy::Batch::None:
        // 兜底那一支：今天从界面上**走不到**（能置忙的三条路都会先写上那四档之一 ——
        // 版本下载没有单独的入口，它是那几条路里的一段）。留着是因为它是个安全的默认：
        // 以后多一条"不特定属于某个实例的下载"，它会得到一个说不岔的标题，而不是一条
        // 空标题的任务。种类给 `Download`（"把文件拿下来"）而不是 `Install`。
        title = MainWindow::tr("下载游戏文件");
        break;
    }
    const QString target = active.instanceId;
    // 父任务那一支：先把这一批挂到父任务的某一步底下。父任务认不出来（已被收尾 /
    // 被清除）时退回独立成行 —— 宁可在任务中心多一行，也不能让这批下载没有账。
    Tasks::TaskId child = Tasks::invalidTaskId();
    if (m_parentTask != Tasks::invalidTaskId()) {
        child = m_owner.m_activityCenter->tasks()->beginChild(
            m_parentTask, kind, title, target, {}, Tasks::TaskCapabilities(), m_parentStep);
        if (child != Tasks::invalidTaskId()) {
            m_taskId = child;
        }
    }
    if (child == Tasks::invalidTaskId()) {
        m_taskId = m_owner.beginTask(kind, title, target);
    }
    // 这一批就是账本里这个号 —— 收尾时"重试"资格只认它（见 `finishTask()`）。
    m_ownerTaskId = m_taskId;
}

void DownloadTaskLedgerController::finish(const QString &fallbackError) {
    // 这一批到此为止。正常情况下那条终态信号（`DownloadCompletionController::onFinished`）
    // 已经按成败收过尾了，这一句是**兜底**：数据层要是在某一路没发出终态（被拒绝的批次、
    // 中途退出的那几路），账本上就会留一件"永远在跑"的任务，底栏那一格从此一直显示它。
    //
    // NOTE: 对已经收过尾的那一件，`finishTask()` 什么都不做（终态只落一次，见
    // TaskManager::finish）—— 所以这一句不会把成功改写成失败、也不会多一颗红点。
    m_owner.finishTask(m_taskId, false, fallbackError);
    m_taskId = Tasks::invalidTaskId();
}
