#include "DirectDownloadController.hpp"
#include "ActivityCenterController.hpp"
#include "download/ResumableDownload.hpp"
#include "DownloadToolDialog.hpp"
#include "download/DownloadCache.hpp"
#include <QSettings>
#include <QThread>
#include <QFileInfo>
#include <QMessageBox>
#include <QLocale>
#include <algorithm>

DirectDownloadController::DirectDownloadController(ActivityCenterController *activity, QWidget *window, QObject *parent)
    : QObject(parent), m_activity(activity), m_window(window) {
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
    if (!QDir::isAbsolutePath(request.path)) return reject(tr("目标文件必须是绝对路径。"));
    DirectDownloadRequest validated;
    if (!DirectDownloadRequest::parse(request.url.toString(), QFileInfo(request.path).absolutePath(), QFileInfo(request.path).fileName(), &validated, error)) return false;
    if (!allowOverwrite && QFileInfo::exists(validated.path)) return reject(tr("目标文件已存在，请确认覆盖或更换文件名。"));
    const auto samePath = [&](const QString &path) { return QDir::cleanPath(path).compare(QDir::cleanPath(validated.path), Qt::CaseInsensitive) == 0; };
    if ((m_running && samePath(m_path)) || std::any_of(m_pendingPaths.cbegin(), m_pendingPaths.cend(), samePath))
        return reject(tr("该保存路径已有进行中或排队的下载，请更换文件名。"));
    const auto id = m_activity->enqueue(Tasks::TaskKind::Download,
        tr("直链下载 · %1").arg(QFileInfo(validated.path).fileName()), QFileInfo(validated.path).absolutePath(),
        {{tr("下载文件"), Tasks::TaskStepState::Pending, {}}, {tr("校验并保存"), Tasks::TaskStepState::Pending, {}}},
        QStringLiteral("direct-download"), this, [this, validated, allowOverwrite](Tasks::TaskId job) { startJob(job, validated, allowOverwrite); });
    if (id == Tasks::invalidTaskId()) return reject(tr("等待队列已满，请稍后再添加。"));
    m_lastSubmitted = id; m_ownedTasks.insert(id); m_pendingPaths.insert(id, validated.path);
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
    m_status = tr("正在连接；进度可在任务中心查看。关闭工具窗口后下载继续。");
    m_activity->tasks()->setDetail(m_task, m_status);
    m_rateClock.start(); m_rateSamples.clear(); m_rateSamples.append({0, 0});
    if (m_dialog) { m_dialog->setBusy(true); m_dialog->showProgress(0, 0, m_status); }
    connect(m_download, &ResumableDownload::progress, this, [this](qint64 done, qint64 total) {
        if (!m_running) return;
        // ★ 预计剩余按**最近五秒的平均速度**算，不用"上一拍和这一拍"的瞬时速度：分段下载
        //   天然一跳一跳的（一段收尾、下一段才起来），瞬时速度能差好几倍，剩余时间就跟着乱跳。
        //   采样每 200 ms 最多一条；淘汰窗口外的旧样本时**至少留两条**，这样 first() 永远是
        //   窗口起点（或窗口左边最近的那一条），两端之差除以真实跨度就是平均速度。
        constexpr qint64 kRateWindowMs = 5000;
        const qint64 now = m_rateClock.elapsed();
        if (now - m_rateSamples.constLast().first >= 200) {
            m_rateSamples.append({now, done});
            while (m_rateSamples.size() > 2 && now - m_rateSamples.at(1).first >= kRateWindowMs)
                m_rateSamples.removeFirst();
        }
        double speed = 0;
        if (m_rateSamples.size() >= 2) {
            const auto &first = m_rateSamples.constFirst();
            const qint64 span = now - first.first;
            if (span > 0) speed = double(qMax<qint64>(0, done - first.second)) * 1000.0 / double(span);
        }
        if (now < 200) return;
        const auto size = [](qint64 bytes) { return QLocale().formattedDataSize(qMax<qint64>(0, bytes)); };
        m_status = tr("%1 / %2 · %3/s").arg(size(done), total > 0 ? size(total) : tr("大小未知"), size(qint64(speed)));
        if (total > done && speed > 0) m_status += tr(" · 预计剩余 %1 秒").arg(qint64((total - done) / speed));
        m_activity->tasks()->setBytes(m_task, done, total);
        m_activity->tasks()->setProgress(m_task, done, total);
        m_activity->tasks()->setDetail(m_task, m_status);
        if (m_dialog) m_dialog->showProgress(done, total, m_status);
    });
    connect(m_download, &ResumableDownload::statusChanged, this, [this](const QString &message) {
        if (!m_running) return;
        m_status = message;
        m_activity->tasks()->setDetail(m_task, message);
        if (m_dialog) m_dialog->status->setText(message);
    });
    connect(m_download, &ResumableDownload::finished, this, [this](bool success, const QString &message) { finish(success, message); });
    m_download->start(validated.url, validated.path, {}, -1, allowOverwrite);
    manageCache(1);
}

void DirectDownloadController::finish(bool success, const QString &error) {
    if (!m_running) return;
    m_running = false;
    m_status = success ? tr("下载完成：%1").arg(QDir::toNativeSeparators(m_path)) : tr("下载失败：%1").arg(error);
    m_activity->tasks()->setStepState(m_task, 0, success ? Tasks::TaskStepState::Done : Tasks::TaskStepState::Failed, error);
    m_activity->tasks()->setStepState(m_task, 1, success ? Tasks::TaskStepState::Done : Tasks::TaskStepState::Failed, error);
    if (success) {
        const auto bytes = QFileInfo(m_path).size();
        m_activity->tasks()->setBytes(m_task, bytes, bytes);
        m_activity->tasks()->setProgress(m_task, bytes, bytes);
    }
    m_activity->tasks()->setDetail(m_task, m_status);
    m_activity->finish(m_task, success, error, false);
    if (m_dialog) { m_dialog->setBusy(false); m_dialog->showProgress(success ? 1 : 0, 1, m_status); }
}

bool DirectDownloadController::owns(Tasks::TaskId id) const { return m_ownedTasks.contains(id); }
bool DirectDownloadController::canCancel(Tasks::TaskId id) const {
    return m_activity != nullptr && owns(id) && (m_activity->isQueued(id) || (id == m_task && m_running && m_download && m_download->canCancel()));
}
bool DirectDownloadController::cancel(Tasks::TaskId id) {
    if (!owns(id)) return false;
    if (m_activity != nullptr && m_activity->isQueued(id)) { m_pendingPaths.remove(id); m_activity->tasks()->cancel(id); return true; }
    if (id != m_task || !m_running || m_download == nullptr) return true;
    // ★ 2026-10-20：`canCancel()` 为假、或者 `cancel()` 回 false，都是**这一步真的停不下来**
    //   （分段作业已经进了校验 / 原子提交那一段 —— 见 `ResumableDownload::cancel()`：提交
    //   半路打断只会留一个半截文件）。以前这两条路照样 `return true` 且一句话不说：用户
    //   看到的是"按了取消、进度条还在走、什么都没发生"。这里把那句话摆到小窗和任务中心
    //   那一行上，然后**仍然按 true 收场**（这一下就是冲这一项来的，别让调用方再往别的
    //   分支上找；那不是它的事）。
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
    m_activity->tasks()->cancel(m_task);
    m_status = tr("已取消，原文件未替换；完整分段已保留，重开程序后用同一链接和目标可继续。");
    if (m_dialog) { m_dialog->setBusy(false); m_dialog->showProgress(0, 1, m_status); }
    return true;
}
void DirectDownloadController::shutdown() {
    if (m_stopping) return;
    m_stopping = true;
    const auto waiting = m_pendingPaths.keys();
    for (auto id : waiting) cancel(id);
    if (m_running) cancel(m_task);
}
