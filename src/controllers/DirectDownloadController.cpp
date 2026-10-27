#include "DirectDownloadController.hpp"
#include "ActivityCenterController.hpp"
#include "download/ResumableDownload.hpp"
#include "DownloadToolDialog.hpp"
#include "download/DownloadCache.hpp"
#include "util/AppPaths.hpp"
#include <QUuid>
#include <QLockFile>
#include <QSettings>
#include <QThread>
#include <QFileInfo>
#include <QMessageBox>
#include <QLocale>
#include <QTimer>
#include <algorithm>

DirectDownloadController::DirectDownloadController(ActivityCenterController *activity, QWidget *window, QObject *parent)
    : QObject(parent), m_activity(activity), m_window(window) {
    // Follow the launcher's stable task ledger, not the currently selected game folder.
    m_recoveryPath = (m_activity ? QFileInfo(m_activity->tasks()->historyPath()).absolutePath()
                                : AppPaths::defaultMinecraftDir()) + QStringLiteral("/wtfml.download-recovery");
    QTimer::singleShot(0, this, [this] { loadRecovery(); });
    auto *rateTimer = new QTimer(this);
    rateTimer->setInterval(1000);
    connect(rateTimer, &QTimer::timeout, this, [this] {
        if (m_running && m_showTransferTelemetry) updateProgress(m_lastDone, m_lastTotal);
    });
    rateTimer->start();
    if (m_activity != nullptr) connect(m_activity->tasks(), &Tasks::TaskManager::taskFinished, this,
        [this](Tasks::TaskId id, bool) { m_pendingPaths.remove(id); });
    if (m_activity != nullptr) connect(m_activity, &QObject::destroyed, this, [this] {
        // MainWindow 的 QObject 子对象销毁顺序不能成为任务账本的隐式寿命保证。
        m_activity = nullptr; m_stopping = true; m_running = false; m_pendingPaths.clear();
        if (m_download != nullptr) m_download->cancel();
    });
}

DirectDownloadController::~DirectDownloadController() { shutdown(); delete m_dialog.data(); }

void DirectDownloadController::open() {
    if (!m_dialog) {
        m_dialog = new DownloadToolDialog(m_window);
        QSettings settings;
        m_dialog->cacheLimit->setValue(settings.value(QStringLiteral("Toolbox/CacheLimitMiB"), 2048).toInt());
        m_dialog->directory->setText(settings.value(QStringLiteral("Toolbox/DownloadDirectory"), m_dialog->directory->text()).toString());
        connect(m_dialog->cacheLimit, &QSpinBox::valueChanged, this, [](int value) {
            QSettings().setValue(QStringLiteral("Toolbox/CacheLimitMiB"), value);
        });
        connect(m_dialog->directory, &QLineEdit::editingFinished, this, [this] {
            if (QDir(m_dialog->directory->text()).exists())
                QSettings().setValue(QStringLiteral("Toolbox/DownloadDirectory"), m_dialog->directory->text());
        });
        m_dialog->onCache = [this](int action) {
            if (action == 3 && QMessageBox::question(m_dialog, tr("清空闲置续传缓存"),
                tr("仅删除未被任务使用的完整分段和续传记录。已下载文件不受影响；清理后相关任务需要重新下载。"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
            manageCache(action);
        };
        m_dialog->onStart = [this] {
            DirectDownloadRequest request;
            QString error;
            if (!DirectDownloadRequest::parse(m_dialog->url->text(), m_dialog->directory->text(), m_dialog->name->text(), &request, &error)) {
                m_dialog->status->setText(error); return;
            }
            const bool exists = QFileInfo::exists(request.path);
            if (exists && QMessageBox::question(m_dialog, tr("确认替换文件"),
                    tr("%1 已存在。下载成功后替换它？取消或失败会保留原文件。").arg(QFileInfo(request.path).fileName()),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
            if (!start(request, exists, &error)) m_dialog->status->setText(error);
        };
        m_dialog->onCancel = [this] { cancel(m_task); };
    }
    m_dialog->setBusy(m_running);
    if (!m_status.isEmpty()) m_dialog->status->setText(m_status);
    m_dialog->show(); m_dialog->raise(); m_dialog->activateWindow();
    manageCache(0);
}

void DirectDownloadController::manageCache(int action) {
    if (m_stopping) return;
    if (m_cacheBusy) { if (action == 1) m_cacheTrimPending = true; return; }
    m_cacheBusy = true;
    const auto kind = action == 1 ? DownloadCache::Action::Trim : action == 2 ? DownloadCache::Action::ClearExpired
        : action == 3 ? DownloadCache::Action::ClearIdle : DownloadCache::Action::Inspect;
    QSettings settings;
    const qint64 limit = qint64(qBound(128, settings.value(QStringLiteral("Toolbox/CacheLimitMiB"), 2048).toInt(), 65536)) * 1024 * 1024;
    const auto result = QSharedPointer<DownloadCache::Result>::create();
    auto *worker = QThread::create([result, kind, limit] { *result = DownloadCache::run(kind, limit); });
    if (m_dialog) m_dialog->cacheStatus->setText(tr("正在后台整理缓存…"));
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &QThread::finished, this, [this, result] {
        m_cacheBusy = false;
        if (m_dialog) m_dialog->cacheStatus->setText(tr("续传缓存：%1 · 本次释放 %2 · 跳过占用 %3 · 失败 %4%5")
            .arg(QLocale().formattedDataSize(qMax<qint64>(0, result->bytes)), QLocale().formattedDataSize(result->removed))
            .arg(result->busy).arg(result->failed).arg(result->incomplete ? tr("（扫描达到安全限制，统计不完整）") : QString()));
        if (m_cacheTrimPending) { m_cacheTrimPending = false; manageCache(1); }
    });
    worker->start();
}

bool DirectDownloadController::start(const DirectDownloadRequest &request, bool allowOverwrite, QString *error) {
    const auto reject = [error](const QString &text) { if (error) *error = text; return false; };
    if (m_stopping) return reject(tr("启动器正在关闭，不能新增下载。"));
    if (!m_activity) return reject(tr("任务中心尚未就绪。"));
    loadRecovery();
    if (!allowOverwrite && m_recoveries.size() >= 64) return reject(tr("待恢复下载已达64项，请先继续或取消已有任务。"));
    if (!QDir::isAbsolutePath(request.path)) return reject(tr("目标文件必须是绝对路径。"));
    DirectDownloadRequest validated;
    if (!DirectDownloadRequest::parse(request.url.toString(), QFileInfo(request.path).absolutePath(), QFileInfo(request.path).fileName(), &validated, error)) return false;
    if (!allowOverwrite && QFileInfo::exists(validated.path)) return reject(tr("目标文件已存在，请确认覆盖或更换文件名。"));
    const auto samePath = [&](const QString &path) { return QDir::cleanPath(path).compare(QDir::cleanPath(validated.path), Qt::CaseInsensitive) == 0; };
    for (auto it = m_recoveries.begin(); it != m_recoveries.end(); ++it) {
        const auto state = m_activity->tasks()->snapshot(it.key()).state;
        if (samePath(it->request.path) && it->request.url != validated.url &&
            (state == Tasks::TaskState::Paused || Tasks::isTerminalState(state)) && !allowOverwrite) {
            const auto previous = it->request.url; it->request.url = validated.url;
            if (!saveRecovery()) { it->request.url = previous; return reject(tr("无法保存新的下载地址，未继续。")); }
            const auto id = it.key();
            if (!resume(id)) return reject(tr("目标已变化或任务无法继续，请检查任务中心提示。"));
            m_lastSubmitted = id;
            if (error) error->clear();
            return true;
        }
    }
    const bool reserved = std::any_of(m_recoveries.cbegin(), m_recoveries.cend(),
        [&](const Recovery &r) { return samePath(r.request.path); });
    if (reserved || (m_running && samePath(m_path)) || std::any_of(m_pendingPaths.cbegin(), m_pendingPaths.cend(), samePath))
        return reject(tr("该保存路径已有进行中或排队的下载，请更换文件名。"));
    const auto id = m_activity->enqueue(Tasks::TaskKind::Download,
        tr("直链下载 · %1").arg(QFileInfo(validated.path).fileName()), QFileInfo(validated.path).absolutePath(),
        {{tr("下载文件"), Tasks::TaskStepState::Pending, {}}, {tr("校验并保存"), Tasks::TaskStepState::Pending, {}}},
        QStringLiteral("direct-download"), this, [this, validated, allowOverwrite](Tasks::TaskId job) { startJob(job, validated, allowOverwrite); });
    if (id == Tasks::invalidTaskId()) return reject(tr("等待队列已满，请稍后再添加。"));
    m_lastSubmitted = id; m_ownedTasks.insert(id); m_pendingPaths.insert(id, validated.path);
    // Replacing an existing original needs a separate immutable backup contract.
    // Only absent-target jobs are recoverable; never silently bless changed originals.
    if (!allowOverwrite && m_recoveryReadable) {
        m_recoveries.insert(id, {validated, QUuid::createUuid().toString(QUuid::WithoutBraces)});
        if (!saveRecovery()) {
            m_recoveries.remove(id); m_activity->tasks()->cancel(id);
            return reject(tr("无法保存下载恢复记录，未开始下载。"));
        }
    }
    auto caps = m_activity->tasks()->snapshot(id).capabilities; caps.canOpenLog = false;
    m_activity->tasks()->setCapabilities(id, caps);
    if (!m_running) m_activity->startReady();
    else if (m_dialog) m_dialog->status->setText(tr("已加入等待队列，可在任务中心拖动调整顺序。"));
    if (error) error->clear();
    return true;
}

void DirectDownloadController::startJob(Tasks::TaskId id, const DirectDownloadRequest &validated, bool allowOverwrite) {
    m_pendingPaths.remove(id);
    m_task = id;
    delete m_download;
    m_download = new ResumableDownload(this);
    m_path = validated.path;
    m_running = true;
    m_activity->tasks()->setStepState(id, 0, Tasks::TaskStepState::Running);
    m_activity->tasks()->setStepState(id, 1, Tasks::TaskStepState::Pending);
    m_status = tr("正在连接；进度可在任务中心查看。关闭工具窗口后下载继续。");
    if (!m_recoveryReadable) m_status += tr("恢复记录不可用或被占用，原件已保留；本次下载不提供暂停和跨重启恢复。");
    m_activity->tasks()->setDetail(m_task, m_status);
    m_rateClock.start(); m_rate.reset(); m_transferred = 0; m_lastRateUi = -1000;
    m_lastDone = 0; m_lastTotal = -1; m_showTransferTelemetry = false;
    if (m_dialog) { m_dialog->setBusy(true); m_dialog->showProgress(0, 0, m_status); }
    connect(m_download, &ResumableDownload::progress, this, [this](qint64 done, qint64 total) {
        if (!m_running) return;
        m_showTransferTelemetry = true;
        updateProgress(done, total);
    });
    connect(m_download, &ResumableDownload::transferBytes, this, [this](qint64 bytes) {
        if (!m_running || bytes <= 0) return;
        if (m_transferred == 0) m_rate.reset(m_rateClock.elapsed());
        m_transferred += bytes;
    });
    connect(m_download, &ResumableDownload::statusChanged, this, [this](const QString &message) {
        if (!m_running) return;
        m_showTransferTelemetry = false; // Preserve connection/checksum/commit phase messages.
        m_status = message;
        syncPauseCapability();
        m_activity->tasks()->setDetail(m_task, message);
        if (m_dialog) m_dialog->status->setText(message);
    });
    connect(m_download, &ResumableDownload::finished, this, [this](bool success, const QString &message) { finish(success, message); });
    m_download->start(validated.url, validated.path, {}, -1, allowOverwrite);
    syncPauseCapability();
    manageCache(1);
}

void DirectDownloadController::updateProgress(qint64 done, qint64 total) {
    if (!m_running || m_activity == nullptr) return;
    m_lastDone = done; m_lastTotal = total;
    syncPauseCapability();
    m_activity->tasks()->setBytes(m_task, done, total);
    m_activity->tasks()->setProgress(m_task, done, total);
    const qint64 now = m_rateClock.elapsed();
    m_rate.sample(now, m_transferred);
    if (now - m_lastRateUi < 1000 && (total <= 0 || done < total)) return;
    m_lastRateUi = now;
    const auto size = [](qint64 bytes) { return QLocale().formattedDataSize(qMax<qint64>(0, bytes)); };
    const qint64 speed = m_rate.bytesPerSecond(now);
    m_status = tr("%1 / %2 · %3").arg(size(done), total > 0 ? size(total) : tr("大小未知"),
        speed < 0 ? tr("测量速度中…") : size(speed) + QStringLiteral("/s"));
    if (total > done) {
        const qint64 eta = m_rate.remainingSeconds(now, total - done);
        if (eta > 0) {
            const qint64 seconds = qMax<qint64>(5, ((eta + 2) / 5) * 5);
            const QString duration = seconds >= 3600
                ? tr("%1 小时 %2 分").arg(seconds / 3600).arg((seconds % 3600) / 60)
                : seconds >= 60 ? tr("%1 分 %2 秒").arg(seconds / 60).arg(seconds % 60)
                                : tr("%1 秒").arg(seconds);
            m_status += tr(" · 预计剩余约 %1").arg(duration);
        } else m_status += speed == 0 ? tr(" · 等待数据…") : tr(" · 正在估算剩余时间…");
    }
    m_activity->tasks()->setDetail(m_task, m_status);
    if (m_dialog) m_dialog->showProgress(done, total, m_status);
}

void DirectDownloadController::finish(bool success, const QString &error) {
    if (!m_running) return;
    const auto finishedTask = m_task;
    const auto finishedPath = m_path;
    m_running = false;
    const auto status = success ? tr("下载完成：%1").arg(QDir::toNativeSeparators(finishedPath)) : tr("下载失败：%1").arg(error);
    m_status = status;
    m_activity->tasks()->setStepState(finishedTask, 0, success ? Tasks::TaskStepState::Done : Tasks::TaskStepState::Failed, error);
    m_activity->tasks()->setStepState(finishedTask, 1, success ? Tasks::TaskStepState::Done : Tasks::TaskStepState::Failed, error);
    if (success) {
        const auto bytes = QFileInfo(finishedPath).size();
        m_activity->tasks()->setBytes(finishedTask, bytes, bytes);
        m_activity->tasks()->setProgress(finishedTask, bytes, bytes);
    }
    m_activity->tasks()->setDetail(finishedTask, status);
    m_activity->finish(finishedTask, success, error, false);
    if (success) { m_recoveries.remove(finishedTask); saveRecovery(); }
    else if (m_recoveries.contains(finishedTask))
        m_activity->tasks()->setRecovery(finishedTask, m_recoveries.value(finishedTask).token, true);
    if (m_dialog && m_task == finishedTask && !m_running) { m_dialog->setBusy(false); m_dialog->showProgress(success ? 1 : 0, 1, status); }
}

bool DirectDownloadController::owns(Tasks::TaskId id) const { return m_ownedTasks.contains(id); }
bool DirectDownloadController::canCancel(Tasks::TaskId id) const {
    return m_activity != nullptr && owns(id) && (m_activity->isQueued(id) ||
        m_activity->tasks()->snapshot(id).state == Tasks::TaskState::Paused ||
        (id == m_task && m_running && m_download && m_download->canCancel()));
}
bool DirectDownloadController::cancel(Tasks::TaskId id) {
    if (!owns(id)) return false;
    if (m_activity != nullptr && (m_activity->isQueued(id) || m_activity->tasks()->snapshot(id).state == Tasks::TaskState::Paused)) {
        m_pendingPaths.remove(id); m_recoveries.remove(id); saveRecovery(); m_activity->tasks()->cancel(id);
        m_activity->tasks()->setRecovery(id, {}, false);
        return true;
    }
    if (id != m_task || !m_running || m_download == nullptr) return true;
    const auto reportUnstoppable = [this]() {
        m_status = tr("已经进入收尾（校验 / 写盘），这一步停不下来，等它写完。");
        if (m_dialog) m_dialog->status->setText(m_status);
        if (m_activity != nullptr) m_activity->tasks()->setDetail(m_task, m_status);
    };
    if (!canCancel(id)) {
        reportUnstoppable();
        return true;
    }
    if (!m_download->cancel()) {
        reportUnstoppable();
        return true;
    }
    m_running = false;
    m_recoveries.remove(id); saveRecovery();
    m_activity->tasks()->cancel(m_task);
    m_status = tr("已取消，原文件未替换；完整分段已保留，重开程序后用同一链接和目标可继续。");
    if (m_dialog) { m_dialog->setBusy(false); m_dialog->showProgress(0, 1, m_status); }
    return true;
}
void DirectDownloadController::shutdown() {
    if (m_stopping) return;
    m_stopping = true;
    const auto waiting = m_pendingPaths.keys();
    for (auto id : waiting) if (!m_recoveries.contains(id)) cancel(id);
    if (m_running && !pause(m_task)) cancel(m_task);
    if (!m_running) m_recoveryLock.reset();
}
