/**
 * @file MainWindowIntegrityBackup.cpp
 * @brief 实例体检与备份 / 恢复：完整性检查那几发、报告页、世界备份、实例快照。
 *
 * 【为什么单独一个翻译单元】
 *
 * 这一片的主语是"这个实例现在完整吗、出事之后能退回到哪一刻"：
 * `integrityScanningVersion()` / `submitIntegrityCheck()` / 两发进度与终态、
 * `integrityReportForPage()` 与报告页的动作、`openBackupFolder()` / `backupWorldNow()` /
 * `restoreWorldBackup()`、`snapshotInstance()` / `restoreInstanceSnapshot()`，以及
 * `startBackupRestore()` 那个**按名字**的兼容入口。
 *
 * 【那个薄壳的名字不能改】
 *
 * mw_check 是照着名字驱动 `startBackupRestore` 的（T35.3 / T35.4：验"还原那一趟跑完了"
 * "别人的包 ⇒ 账本上是失败"）。`invokeMethod` 找不到就返回 false，断言会静默失效 ——
 * 所以实现挪走之后这里留着同名薄壳。这是这一层和验证工程之间唯一的约定。
 *
 * 【判据不在这一层】
 *
 * 扫描与报告在 `IntegrityController` / `VersionIntegrity`；备份与还原在
 * `BackupController` / `BackupFiles`；任务账本由 `startBackupRestore()` 所在的那一侧落。
 */

#include "MainWindow.hpp"

#include "..\ui\IntegrityReportPage.hpp"
#include "controllers/BackupController.hpp"
#include "controllers/IntegrityController.hpp"
#include "manage/VersionManager.hpp"
#include "manage/VersionIntegrity.hpp"
#include "server/ServerInstallRun.hpp"
#include "tasks/TaskId.hpp"

// ---------------------------------------------------------------------------
// 完整性检查与修复
// ---------------------------------------------------------------------------

bool MainWindow::integrityScanningVersion(const QString &versionId) const {
    return m_integrityController->scanning(versionId);
}

void MainWindow::submitIntegrityCheck(const QString &versionId) {
    IntegrityController::BusyFacts busy;
    busy.targetBeingModified = m_installOccupancy.modifying(versionId);
    busy.anotherDownloadRunning = m_downloadInFlight || m_versionManager->isDownloading()
                                  || (m_serverInstall != nullptr
                                      && m_serverInstall->ownsDownloadBatch());
    m_integrityController->submit(versionId, busy);
}

void MainWindow::onIntegrityScanProgress(int checked, int total, int groupIndex) {
    m_integrityController->progress(checked, total, groupIndex);
}

void MainWindow::onIntegrityScanFinished(const VersionIntegrity::Report &report) {
    m_integrityController->finished(report);
}

IntegrityReportPage::Report MainWindow::integrityReportForPage(
    const VersionIntegrity::Report &report, const QString &instanceName) const {
    IntegrityReportPage::Report out;
    out.instanceName = instanceName;
    out.summary = report.summary();
    out.error = report.error;
    for (VersionIntegrity::Group group : VersionIntegrity::allGroups()) {
        IntegrityReportPage::Group item;
        item.name = VersionIntegrity::groupName(group);
        item.missing = report.missingIn(group);
        item.corrupt = report.corruptIn(group);
        for (const VersionIntegrity::Verdict &v : report.problemsInGroup(group))
            item.rows.append({v.label, v.detail(), v.path});
        out.groups.append(item);
    }
    return out;
}

void MainWindow::openIntegrityReportPage(const QString &versionId) {
    m_integrityController->openReport(versionId);
}

void MainWindow::onIntegrityReportAction(IntegrityReportPage::Action action) {
    m_integrityController->reportAction(int(action));
}

void MainWindow::copyIntegrityReport() {
    m_integrityController->copyReport();
}
// ===========================================================================
// 世界与实例备份
// ===========================================================================
//
// 四个入口（世界页三颗 + 概览那两颗）都收在这一段里。**干活的那一层在
// `src/manage/BackupFiles`**（走目录、算 sha1、起 PowerShell 打包、还原之前做安全快照），
// 这一段只负责：把描述拼出来、起一趟后台活、把结果记进任务账本并弹给用户。

void MainWindow::openBackupFolder(BackupFiles::Kind kind) {
    m_backupController->openFolder(kind);
}

void MainWindow::backupWorldNow(const QString &worldDir) {
    m_backupController->backupWorld(worldDir);
}

void MainWindow::restoreWorldBackup(const QString &worldDir) {
    m_backupController->restoreWorld(worldDir);
}

void MainWindow::snapshotInstance(const QString &versionId) {
    m_backupController->snapshotInstance(versionId);
}

void MainWindow::restoreInstanceSnapshot(const QString &versionId,
                                         InstanceSettingsDialog *dialog) {
    m_backupController->restoreInstance(versionId, dialog);
}

Tasks::TaskId MainWindow::startBackupRestore(const QString &zipPath, const QString &targetParentDir,
                                            const QString &title, const QString &targetId,
                                            const QString &successTitle) {
    // 这一条正是"搬功能之前先想清楚谁还按名字喊它"：**mw_check 是照着名字驱动它的**
    // （`QMetaObject::invokeMethod(&w, "startBackupRestore", …)`，T35.3 / T35.4 那一节
    // 拿它验"还原那一趟跑完了""别人的包 ⇒ 账本上是失败"）。invokeMethod 找不到就返回
    // false，断言会静默失效，屏幕上看着像"还原功能坏了"。所以实现挪走时这里要留一个
    // 同名的薄壳 —— 这是这一层和验证工程之间唯一的那条约定（见 tests/mw_check 那一节的说明）。
    //
    // 这一发**一个界面调用点都没有**（界面上那两条路是上面那两个：世界页
    // 「恢复备份…」和概览「从快照恢复…」，zip 和落点由它们自己决定）。
    return m_backupController->startRestore(zipPath, targetParentDir, title, targetId, successTitle);
}
