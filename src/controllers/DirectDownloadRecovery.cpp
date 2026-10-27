#include "DirectDownloadController.hpp"
#include "ActivityCenterController.hpp"
#include "download/ResumableDownload.hpp"
#include "auth/TokenStore.hpp"
#include "DownloadToolDialog.hpp"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QLockFile>

namespace {
constexpr qint64 limit = 4 * 1024 * 1024;
bool safeParent(const QString &path) {
    for (QString p = QFileInfo(path).absolutePath();;) {
        const QFileInfo info(p);
        if (!info.isDir() || info.isSymLink() || info.isJunction()) return false;
        const QString next = info.absolutePath();
        if (next == p) return true;
        p = next;
    }
}
}
bool DirectDownloadController::saveRecovery() {
    if (!m_recoveryReadable || QFileInfo(m_recoveryPath).isSymLink()) return false;
    const auto parent = QFileInfo(m_recoveryPath).absolutePath();
    for (QString p = parent;;) {
        const QFileInfo info(p);
        if (info.isSymLink() || info.isJunction() || (info.exists() && !info.isDir())) return false;
        const auto next = info.absolutePath();
        if (next == p) break;
        p = next;
    }
    if (!QDir().mkpath(parent) || !safeParent(m_recoveryPath)) return false;
    if (!m_recoveryLock) {
        auto lock = std::make_unique<QLockFile>(m_recoveryPath + ".lock"); lock->setStaleLockTime(0);
        if (!lock->tryLock(0)) return false;
        m_recoveryLock = std::move(lock);
    }
    QJsonArray jobs;
    for (auto it = m_recoveries.cbegin(); it != m_recoveries.cend(); ++it)
        jobs.append(QJsonObject{{"task", QString::number(it.key())}, {"token", it->token},
            {"url", it->request.url.toString(QUrl::FullyEncoded)}, {"path", it->request.path}});
    const auto bytes = TokenStore::protect(QJsonDocument(QJsonObject{{"format", 1}, {"jobs", jobs}}).toJson());
#ifdef Q_OS_WIN
    // Signed URLs may contain credentials. Refuse the legacy obfuscation fallback.
    if (!bytes.startsWith("WTFML-PROTECTED-1\n")) return false;
#endif
    if (bytes.isEmpty() || bytes.size() > limit) return false;
    QSaveFile file(m_recoveryPath);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
void DirectDownloadController::loadRecovery() {
    if (!m_activity || m_stopping || m_recoveryLoaded) return;
    m_recoveryLoaded = true;
    QFile file(m_recoveryPath);
    if (!file.exists()) return;
    auto lock = std::make_unique<QLockFile>(m_recoveryPath + ".lock"); lock->setStaleLockTime(0);
    if (!lock->tryLock(0)) { m_recoveryReadable = false; return; }
    m_recoveryLock = std::move(lock);
    if (!safeParent(m_recoveryPath) || QFileInfo(m_recoveryPath).isSymLink() ||
        !file.open(QIODevice::ReadOnly) || file.size() > limit) { m_recoveryReadable = false; return; }
    const auto doc = QJsonDocument::fromJson(TokenStore::unprotect(file.readAll()));
    const auto object = doc.object();
    if (!doc.isObject() || object.value("format") != 1 || !object.value("jobs").isArray() ||
        object.value("jobs").toArray().size() > 64) { m_recoveryReadable = false; return; }
    QHash<Tasks::TaskId, Recovery> parsed;
    QSet<QString> paths, tokens;
    for (const auto &v : object.value("jobs").toArray()) {
        const auto row = v.toObject(); bool idOk = false;
        const auto id = row.value("task").toString().toULongLong(&idOk);
        const QString path = row.value("path").toString(), token = row.value("token").toString();
        DirectDownloadRequest request; QString problem;
        if (!v.isObject() || !idOk || id == Tasks::invalidTaskId() || token.isEmpty() || token.size() > 128 ||
            parsed.contains(id) || tokens.contains(token) || paths.contains(path.toCaseFolded()) ||
            !DirectDownloadRequest::parse(row.value("url").toString(), QFileInfo(path).absolutePath(),
                QFileInfo(path).fileName(), &request, &problem) || request.path != path) {
            m_recoveryReadable = false; return;
        }
        parsed.insert(id, {request, token}); paths.insert(path.toCaseFolded()); tokens.insert(token);
    }
    for (auto it = parsed.cbegin(); it != parsed.cend(); ++it) {
        Tasks::TaskId id = it.key();
        const auto old = m_activity->tasks()->snapshot(id);
        if (m_activity->tasks()->has(id) && old.kind == Tasks::TaskKind::Download &&
            old.targetId == QFileInfo(it->request.path).absolutePath() &&
            (old.state == Tasks::TaskState::Succeeded || old.state == Tasks::TaskState::Cancelled))
            continue; // A stale journal write must not resurrect an acknowledged terminal task.
        if (!m_activity->tasks()->has(id) || old.kind != Tasks::TaskKind::Download ||
            old.targetId != QFileInfo(it->request.path).absolutePath() || !Tasks::isTerminalState(old.state)) {
            id = m_activity->begin(Tasks::TaskKind::Download, tr("待继续下载 · %1").arg(QFileInfo(it->request.path).fileName()),
                QFileInfo(it->request.path).absolutePath(),
                {{tr("下载文件"), Tasks::TaskStepState::Pending, {}}, {tr("校验并保存"), Tasks::TaskStepState::Pending, {}}});
            m_activity->finish(id, false, tr("上次下载未完成，请明确点击继续；不会自动下载。"), false);
        }
        m_recoveries.insert(id, it.value()); m_ownedTasks.insert(id);
        m_activity->tasks()->setRecovery(id, it->token, true);
    }
    saveRecovery();
}
void DirectDownloadController::syncPauseCapability() {
    if (!m_activity || !m_running || !m_download) return;
    auto caps = m_activity->tasks()->snapshot(m_task).capabilities;
    caps.canPause = m_recoveries.contains(m_task) && m_download->canCancel();
    m_activity->tasks()->setCapabilities(m_task, caps);
}
bool DirectDownloadController::pause(Tasks::TaskId id) {
    if (!m_activity || id != m_task || !m_running || !m_download || !m_recoveries.contains(id) || !saveRecovery()) return false;
    syncPauseCapability();
    if (!m_activity->tasks()->snapshot(id).capabilities.canPause || !m_download->cancel()) return false;
    m_running = false;
    if (!m_activity->tasks()->pause(id)) return false;
    m_activity->tasks()->setRecovery(id, m_recoveries.value(id).token, true);
    m_activity->tasks()->setDetail(id, tr("已暂停；完整分段保留。继续时重新取得地址并校验缓存，未完成分段可能重下。"));
    m_status = tr("已暂停，可在任务中心继续下载。");
    if (m_dialog) { m_dialog->setBusy(false); m_dialog->status->setText(m_status); }
    m_activity->releaseLane(id);
    return true;
}
bool DirectDownloadController::resume(Tasks::TaskId id) {
    if (!m_activity || m_stopping || !m_recoveries.contains(id)) return false;
    const auto record = m_recoveries.value(id);
    if (!safeParent(record.request.path) || QFileInfo::exists(record.request.path)) {
        m_activity->tasks()->setRecovery(id, record.token, false, tr("目标已出现文件或目录已变化，请重新预览下载，未覆盖。"));
        return false;
    }
    if (!saveRecovery()) return false;
    const bool queued = m_activity->queueRecovery(id, this, [this, record](Tasks::TaskId task) {
        // Re-probe the original URL, never reuse a persisted redirect/CDN address.
        if (QFileInfo::exists(record.request.path) || !safeParent(record.request.path)) {
            m_activity->finish(task, false, tr("目标在排队期间变化，未继续下载。"), false); return;
        }
        startJob(task, record.request, false);
    });
    if (queued) m_pendingPaths.insert(id, record.request.path);
    return queued;
}
