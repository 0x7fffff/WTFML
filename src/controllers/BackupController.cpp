#include "BackupController.hpp"

#include "ActivityCenterController.hpp"
#include "util/AppPaths.hpp"
#include "manage/VersionManager.hpp"
#include "InlineMessage.hpp"
#include "InstanceSettingsDialog.hpp"
#include "OverlayHost.hpp"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QThread>
#include <QUrl>

#include <algorithm>
#include <functional>

BackupController::BackupController(VersionManager *versions, ActivityCenterController *activity,
                                   OverlayHost *overlay, Host host, QObject *parent)
    : QObject(parent), m_versions(versions), m_activity(activity), m_overlay(overlay),
      m_host(std::move(host)) {}

BackupController::~BackupController() {
    shutdown();
}

bool BackupController::busy() const {
    return m_taskId != Tasks::invalidTaskId();
}

void BackupController::shutdown() {
    m_abort.storeRelaxed(1);
    if (m_thread != nullptr) {
        m_thread->wait();
    }
}

QString BackupController::displayName(const QString &id) const {
    return m_host.displayName ? m_host.displayName(id) : id;
}

void BackupController::log(const QString &message) const {
    if (m_host.log) {
        m_host.log(message);
    }
}

QString BackupController::sizeText(qint64 bytes) {
    if (bytes >= 1024LL * 1024 * 1024) {
        return QStringLiteral("%1 GB").arg(QString::number(double(bytes) / (1024.0 * 1024 * 1024), 'f', 2));
    }
    if (bytes >= 1024 * 1024) {
        return QStringLiteral("%1 MB").arg(QString::number(double(bytes) / (1024.0 * 1024), 'f', 1));
    }
    return QStringLiteral("%1 KB").arg(QString::number(double(bytes) / 1024.0, 'f', 1));
}

QString BackupController::pickLine(const QString &zipPath) {
    const QFileInfo info(zipPath);
    return tr("%1（%2，%3）")
        .arg(info.fileName(), sizeText(info.size()),
             info.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
}

void BackupController::openFolder(BackupFiles::Kind kind) {
    const QString dir = BackupFiles::backupDir(kind);
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

QString BackupController::pickZip(const QString &dir, const QString &label, const QString &what) {
    const QString prefix = BackupFiles::backupFilePrefix(label);
    QStringList zips;
    const QFileInfoList entries =
        QDir(dir).entryInfoList(QStringList{QStringLiteral("*.zip")}, QDir::Files, QDir::Name);
    for (const QFileInfo &entry : entries) {
        if (entry.fileName().startsWith(prefix, Qt::CaseInsensitive)) {
            zips.append(entry.absoluteFilePath());
        }
    }
    if (zips.isEmpty()) {
        InlineMessage::information(
            m_overlay, tr("还没有备份"),
            tr("%1还没有备份过，所以没有能还原的东西。\n\n备份放在这儿：%2\n"
               "（在这一页上先做一份备份，再来还原。）")
                .arg(what, QDir::toNativeSeparators(dir)));
        return {};
    }
    std::sort(zips.begin(), zips.end(), std::greater<QString>());
    QStringList items;
    for (const QString &zip : std::as_const(zips)) {
        items.append(pickLine(zip));
    }
    int row = -1;
    if (!InlineMessage::pickFromList(m_overlay, tr("挑一份备份"),
                                     tr("要还原哪一份？（越上面越新）"), items, 0, &row)
        || row < 0 || row >= zips.size()) {
        return {};
    }
    return zips.at(row);
}

void BackupController::backupWorld(const QString &worldDir) {
    if (worldDir.isEmpty()) return;
    if (busy()) {
        InlineMessage::information(m_overlay, tr("现在做不了"),
                                   tr("已经有一份备份或者还原在跑了，等它完了再来。"));
        return;
    }
    const QString instanceId = m_host.instanceIdOfPath ? m_host.instanceIdOfPath(worldDir) : QString();
    if (!instanceId.isEmpty() && m_host.isRunning && m_host.isRunning(instanceId)) {
        InlineMessage::warning(m_overlay, tr("备份世界"),
                               tr("游戏正在运行。先关掉游戏再备份 —— 游戏开着的时候每隔几秒就在往存档里写，"
                                  "这时候抄出来的那一份可能是不完整的。"));
        return;
    }
    const QString worldName = QFileInfo(worldDir).fileName();
    const QDateTime now = QDateTime::currentDateTime();
    BackupFiles::Manifest manifest;
    manifest.kind = BackupFiles::Kind::World;
    manifest.root = worldName;
    manifest.label = worldName;
    manifest.worldName = worldName;
    manifest.sourceInstanceId = instanceId;
    manifest.sourceInstanceName = instanceId.isEmpty() ? QString() : displayName(instanceId);
    manifest.gameVersion = instanceId.isEmpty() ? QString() : m_versions->instanceGameVersion(instanceId);
    manifest.createdAt = now;
    manifest.scope = QStringList{tr("这个存档的整个文件夹")};
    const QString zip = QDir(BackupFiles::backupDir(BackupFiles::Kind::World))
                            .filePath(BackupFiles::backupFileName(worldName, now));
    startWrite(manifest, worldDir, zip, tr("备份世界「%1」").arg(worldName), instanceId,
               tr("备份完成"));
}

void BackupController::restoreWorld(const QString &worldDir, InstanceSettingsDialog *dialog) {
    if (worldDir.isEmpty()) return;
    if (busy()) {
        InlineMessage::information(m_overlay, tr("现在做不了"),
                                   tr("已经有一份备份或者还原在跑了，等它完了再来。"));
        return;
    }
    const QString instanceId = m_host.instanceIdOfPath ? m_host.instanceIdOfPath(worldDir) : QString();
    if (!instanceId.isEmpty() && m_host.isRunning && m_host.isRunning(instanceId)) {
        InlineMessage::warning(m_overlay, tr("恢复备份"),
                               tr("游戏正在运行。先关掉游戏再还原 —— 不然它退出的时候会把"
                                  "内存里那份旧存档重新写一遍，把刚还原出来的东西盖掉。"));
        return;
    }
    const QString worldName = QFileInfo(worldDir).fileName();
    const QString zip = pickZip(BackupFiles::backupDir(BackupFiles::Kind::World), worldName,
                                tr("这个世界"));
    if (zip.isEmpty()) return;
    if (!InlineMessage::question(
            m_overlay, tr("恢复备份"),
            tr("「%1」现在的文件会被备份里那一份**整个换掉**。\n\n"
               "还原之前会自动给现在这一份留一份安全快照，所以这一步是能退回来的。\n\n"
               "要还原的是这一份：\n%2")
                .arg(worldName, QDir::toNativeSeparators(zip)),
            tr("还原"), tr("取消"), false, true)) {
        return;
    }
    m_refreshWorldDir = worldDir;
    m_refreshDialog = dialog;
    startRestore(zip, QFileInfo(worldDir).absolutePath(), tr("还原世界「%1」").arg(worldName),
                 instanceId, tr("还原完成"));
}

void BackupController::snapshotInstance(const QString &versionId) {
    if (versionId.isEmpty()) return;
    if (busy()) {
        InlineMessage::information(m_overlay, tr("现在做不了"),
                                   tr("已经有一份备份或者还原在跑了，等它完了再来。"));
        return;
    }
    if (m_host.isBeingModified && m_host.isBeingModified(versionId)) {
        InlineMessage::information(m_overlay, tr("现在做不了"),
                                   tr("「%1」这会儿正在装（或者正在导入 / 正在检查）。等它完了再做快照 —— "
                                      "要打进去的正是这会儿正在被改的那些文件。")
                                       .arg(displayName(versionId)));
        return;
    }
    if (m_host.isRunning && m_host.isRunning(versionId)) {
        InlineMessage::information(m_overlay, tr("现在做不了"),
                                   tr("「%1」正在运行。先关掉游戏再做快照 —— 游戏开着的时候"
                                      "存档、日志这些文件一直在变。")
                                       .arg(displayName(versionId)));
        return;
    }
    const int picked = InlineMessage::ask(
        m_overlay, MessageKind::Question, tr("创建快照"),
        tr("快照会把「%1」整个目录打成一个压缩包，需要的时候可以整体还原回来。\n\n"
           "默认**不**打包 assets 和 libraries —— 那两部分是能重新下回来的资源文件和游戏库"
           "（通常几百 MB 到几个 GB），不打它们快照会小很多。")
            .arg(displayName(versionId)),
        {tr("创建快照"), tr("完整快照（含可重新下载的部分）"), tr("取消")}, 0, -1, false);
    if (picked != 0 && picked != 1) return;
    const bool complete = picked == 1;
    const QString instanceDir = AppPaths::versionDir(versionId);
    if (!QFileInfo(instanceDir).isDir()) {
        InlineMessage::warning(m_overlay, tr("创建快照"),
                               tr("找不到这个实例的目录：\n%1")
                                   .arg(QDir::toNativeSeparators(instanceDir)));
        return;
    }
    const QString dirName = QFileInfo(instanceDir).fileName();
    const QDateTime now = QDateTime::currentDateTime();
    BackupFiles::Manifest manifest;
    manifest.kind = BackupFiles::Kind::Instance;
    manifest.root = dirName;
    manifest.label = dirName;
    manifest.sourceInstanceId = versionId;
    manifest.sourceInstanceName = displayName(versionId);
    manifest.gameVersion = m_versions->instanceGameVersion(versionId);
    manifest.complete = complete;
    manifest.createdAt = now;
    if (complete) {
        manifest.scope = QStringList{tr("整个实例目录（包含能重新下回来的资源文件和游戏库）")};
    } else {
        manifest.excluded = QStringList{QStringLiteral("assets"), QStringLiteral("libraries")};
        manifest.scope = QStringList{tr("实例目录（模组、配置、存档、日志…）"),
                                     tr("没打包：assets、libraries（能重新下回来）")};
    }
    const QString zip = QDir(BackupFiles::backupDir(BackupFiles::Kind::Instance))
                            .filePath(BackupFiles::backupFileName(dirName, now));
    startWrite(manifest, instanceDir, zip,
               tr("创建「%1」的快照").arg(displayName(versionId)), versionId,
               tr("快照创建完成"));
}

void BackupController::restoreInstance(const QString &versionId) {
    if (versionId.isEmpty()) return;
    if (busy()) {
        InlineMessage::information(m_overlay, tr("现在做不了"),
                                   tr("已经有一份备份或者还原在跑了，等它完了再来。"));
        return;
    }
    if ((m_host.isBeingModified && m_host.isBeingModified(versionId))
        || (m_host.isRunning && m_host.isRunning(versionId))) {
        InlineMessage::information(m_overlay, tr("现在做不了"),
                                   tr("「%1」这会儿正在装，或者正在运行。等它停了再还原 —— 这一步会把整个实例目录"
                                      "换掉。")
                                       .arg(displayName(versionId)));
        return;
    }
    const QString instanceDir = AppPaths::versionDir(versionId);
    const QString zip = pickZip(BackupFiles::backupDir(BackupFiles::Kind::Instance),
                                QFileInfo(instanceDir).fileName(), tr("这个实例"));
    if (zip.isEmpty()) return;
    if (!InlineMessage::question(
            m_overlay, tr("从快照恢复"),
            tr("「%1」现在的文件会被快照里那一份**整个换掉** —— 模组、配置、存档全算。\n\n"
               "还原之前会自动给现在这一份留一份安全快照，所以这一步是能退回来的。\n\n"
               "要还原的是这一份：\n%2")
                .arg(displayName(versionId), QDir::toNativeSeparators(zip)),
            tr("还原"), tr("取消"), false, true)) {
        return;
    }
    startRestore(zip, AppPaths::versionsDir(),
                 tr("还原「%1」的快照").arg(displayName(versionId)), versionId,
                 tr("还原完成"));
}

Tasks::TaskId BackupController::startWrite(const BackupFiles::Manifest &manifest,
                                            const QString &sourceDir, const QString &targetZip,
                                            const QString &title, const QString &targetId,
                                            const QString &successTitle,
                                            const QString &refreshServerId) {
    QList<Tasks::TaskStep> steps{{tr("统计并核对文件"), Tasks::TaskStepState::Running, {}},
                                 {tr("打包"), Tasks::TaskStepState::Pending, {}}};
    m_taskId = m_activity->begin(Tasks::TaskKind::Backup, title, targetId, steps);
    m_refreshServerId = refreshServerId;
    log(tr("%1：%2 → %3").arg(title, QDir::toNativeSeparators(sourceDir),
                              QDir::toNativeSeparators(targetZip)));
    m_abort.storeRelaxed(0);
    if (m_thread != nullptr) {
        m_thread->deleteLater();
        m_thread = nullptr;
    }
    m_thread = QThread::create([this, manifest, sourceDir, targetZip, successTitle] {
        const auto result = BackupFiles::writeBackup(manifest, sourceDir, targetZip);
        if (m_abort.loadRelaxed() == 0) {
            QMetaObject::invokeMethod(this, [this, result, successTitle] {
                writeFinished(result, successTitle);
            }, Qt::QueuedConnection);
        }
    });
    m_thread->setParent(this);
    m_thread->start();
    return m_taskId;
}

Tasks::TaskId BackupController::startRestore(const QString &zipPath,
                                              const QString &targetParentDir,
                                              const QString &title, const QString &targetId,
                                              const QString &successTitle,
                                              const QString &refreshServerId) {
    QList<Tasks::TaskStep> steps{{tr("读清单"), Tasks::TaskStepState::Running, {}},
                                 {tr("把现在这一份收起来"), Tasks::TaskStepState::Pending, {}},
                                 {tr("解出备份里的文件"), Tasks::TaskStepState::Pending, {}},
                                 {tr("逐文件核对"), Tasks::TaskStepState::Pending, {}}};
    m_taskId = m_activity->begin(Tasks::TaskKind::Backup, title, targetId, steps);
    m_refreshServerId = refreshServerId;
    log(tr("%1：%2 → %3").arg(title, QDir::toNativeSeparators(zipPath),
                              QDir::toNativeSeparators(targetParentDir)));
    m_abort.storeRelaxed(0);
    if (m_thread != nullptr) {
        m_thread->deleteLater();
        m_thread = nullptr;
    }
    m_thread = QThread::create([this, zipPath, targetParentDir, successTitle] {
        const auto result = BackupFiles::restoreBackup(zipPath, targetParentDir);
        if (m_abort.loadRelaxed() == 0) {
            QMetaObject::invokeMethod(this, [this, result, successTitle] {
                restoreFinished(result, successTitle);
            }, Qt::QueuedConnection);
        }
    });
    m_thread->setParent(this);
    m_thread->start();
    return m_taskId;
}

void BackupController::writeFinished(const BackupFiles::WriteResult &result,
                                     const QString &successTitle) {
    const Tasks::TaskId id = m_taskId;
    m_taskId = Tasks::invalidTaskId();
    if (id != Tasks::invalidTaskId() && m_activity->tasks()->has(id)) {
        if (result.ok) {
            m_activity->tasks()->setStepState(id, 0, Tasks::TaskStepState::Done);
            m_activity->tasks()->setStepState(id, 1, Tasks::TaskStepState::Done);
            m_activity->tasks()->setDetail(id, tr("共 %1 个文件，%2")
                                                   .arg(result.manifest.fileCount())
                                                   .arg(sizeText(result.manifest.totalBytes)));
            m_activity->finish(id, true, {}, false);
        } else {
            const int failedStep = result.manifest.files.isEmpty() ? 0 : 1;
            m_activity->tasks()->setStepState(id, failedStep, Tasks::TaskStepState::Failed,
                                              result.problem);
            m_activity->finish(id, false, result.problem, false);
        }
    }
    if (result.ok) {
        log(tr("%1：%2").arg(successTitle, tr("共 %1 个文件，%2")
                                               .arg(result.manifest.fileCount())
                                               .arg(sizeText(result.manifest.totalBytes))));
        InlineMessage::information(m_overlay, successTitle,
                                   tr("已经打好一份了：\n%1\n\n共 %2 个文件，%3。")
                                       .arg(QDir::toNativeSeparators(result.zipPath))
                                       .arg(result.manifest.fileCount())
                                       .arg(sizeText(result.manifest.totalBytes)));
    } else {
        log(tr("%1 没成：%2").arg(successTitle, result.problem));
        InlineMessage::warning(m_overlay, successTitle, result.problem);
    }
    if (!m_refreshServerId.isEmpty() && m_host.refreshServer) {
        m_host.refreshServer(m_refreshServerId);
    }
    m_refreshServerId.clear();
}

void BackupController::restoreFinished(const BackupFiles::RestoreResult &result,
                                       const QString &successTitle) {
    const Tasks::TaskId id = m_taskId;
    m_taskId = Tasks::invalidTaskId();
    const auto stepOf = [](BackupFiles::RestoreResult::Stage stage) {
        switch (stage) {
        case BackupFiles::RestoreResult::Stage::ReadManifest: return 0;
        case BackupFiles::RestoreResult::Stage::Space:
        case BackupFiles::RestoreResult::Stage::SafetySnapshot:
        case BackupFiles::RestoreResult::Stage::RemoveOld: return 1;
        case BackupFiles::RestoreResult::Stage::Extract: return 2;
        case BackupFiles::RestoreResult::Stage::Verify:
        case BackupFiles::RestoreResult::Stage::Ok: return 3;
        }
        return 3;
    };
    if (id != Tasks::invalidTaskId() && m_activity->tasks()->has(id)) {
        if (result.ok) {
            for (int i = 0; i < 4; ++i) m_activity->tasks()->setStepState(id, i, Tasks::TaskStepState::Done);
            m_activity->tasks()->setDetail(id, tr("核对过 %1 个文件，全都对得上").arg(result.checked));
            m_activity->finish(id, true, {}, false);
        } else {
            const int failed = stepOf(result.stage);
            for (int i = 0; i < 4; ++i) {
                m_activity->tasks()->setStepState(
                    id, i, i < failed ? Tasks::TaskStepState::Done
                                      : i == failed ? Tasks::TaskStepState::Failed
                                                    : Tasks::TaskStepState::Pending,
                    i == failed ? result.problem : QString());
            }
            m_activity->finish(id, false, result.problem, false);
        }
    }
    if (result.ok) {
        QString text = tr("已经还原好了。核对过 %1 个文件，全都对得上。").arg(result.checked);
        if (!result.safetyZip.isEmpty()) {
            text += tr("\n\n还原之前的「现在这一份」也留着，在这儿：\n%1")
                        .arg(QDir::toNativeSeparators(result.safetyZip));
        }
        log(tr("%1：%2").arg(successTitle, tr("核对过 %1 个文件").arg(result.checked)));
        InlineMessage::information(m_overlay, successTitle, text);
    } else {
        log(tr("%1 没成：%2").arg(successTitle, result.problem));
        InlineMessage::warning(m_overlay, successTitle, result.problem);
    }
    const bool diskChanged = result.ok || result.stage == BackupFiles::RestoreResult::Stage::RemoveOld
                             || result.stage == BackupFiles::RestoreResult::Stage::Extract
                             || result.stage == BackupFiles::RestoreResult::Stage::Verify;
    if (diskChanged && !m_refreshWorldDir.isEmpty() && !m_refreshDialog.isNull()
        && m_host.refreshWorld) {
        m_host.refreshWorld(m_refreshDialog.data(), m_refreshWorldDir);
    }
    m_refreshWorldDir.clear();
    m_refreshDialog = nullptr;
    if (diskChanged && !m_refreshServerId.isEmpty() && m_host.refreshServer) {
        m_host.refreshServer(m_refreshServerId);
    }
    m_refreshServerId.clear();
}
