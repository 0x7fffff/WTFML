#include "DirectDownloadController.hpp"
#include "ActivityCenterController.hpp"
#include "download/ResumableDownload.hpp"
#include "DownloadToolDialog.hpp"
#include <QFileInfo>
#include <QMessageBox>
#include <QLocale>

DirectDownloadController::DirectDownloadController(ActivityCenterController *activity, QWidget *window, QObject *parent)
    : QObject(parent), m_activity(activity), m_window(window) {}

DirectDownloadController::~DirectDownloadController() { shutdown(); delete m_dialog.data(); }

void DirectDownloadController::open() {
    if (!m_dialog) {
        m_dialog = new DownloadToolDialog(m_window);
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
}

bool DirectDownloadController::start(const DirectDownloadRequest &request, bool allowOverwrite, QString *error) {
    const auto reject = [error](const QString &text) { if (error) *error = text; return false; };
    if (m_running) return reject(tr("已有直链下载正在进行，请先取消或等待完成。"));
    if (!m_activity) return reject(tr("任务中心尚未就绪。"));
    if (!QDir::isAbsolutePath(request.path)) return reject(tr("目标文件必须是绝对路径。"));
    DirectDownloadRequest validated;
    if (!DirectDownloadRequest::parse(request.url.toString(), QFileInfo(request.path).absolutePath(), QFileInfo(request.path).fileName(), &validated, error)) return false;
    if (!allowOverwrite && QFileInfo::exists(validated.path)) return reject(tr("目标文件已存在，请确认覆盖或更换文件名。"));
    delete m_download;
    m_download = new ResumableDownload(this);
    m_path = validated.path;
    m_running = true;
    m_task = m_activity->begin(Tasks::TaskKind::Download, tr("直链下载 · %1").arg(QFileInfo(m_path).fileName()),
        QFileInfo(m_path).absolutePath(), {{tr("下载文件"), Tasks::TaskStepState::Running, {}},
                                         {tr("校验并保存"), Tasks::TaskStepState::Pending, {}}});
    auto caps = m_activity->tasks()->snapshot(m_task).capabilities;
    m_ownedTasks.insert(m_task);
    caps.canOpenLog = false;
    m_activity->tasks()->setCapabilities(m_task, caps);
    m_status = tr("正在连接；进度可在任务中心查看。关闭工具窗口后下载继续。");
    m_activity->tasks()->setDetail(m_task, m_status);
    m_rateClock.start(); m_lastBytes = 0;
    if (m_dialog) { m_dialog->setBusy(true); m_dialog->showProgress(0, 0, m_status); }
    connect(m_download, &ResumableDownload::progress, this, [this](qint64 done, qint64 total) {
        if (!m_running || m_rateClock.elapsed() < 200) return;
        const qint64 elapsed = m_rateClock.restart();
        const double speed = double(qMax<qint64>(0, done - m_lastBytes)) * 1000 / qMax<qint64>(1, elapsed);
        m_lastBytes = done;
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
    if (error) error->clear();
    return true;
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
bool DirectDownloadController::canCancel(Tasks::TaskId id) const { return id == m_task && owns(id) && m_running && m_download && m_download->canCancel(); }
bool DirectDownloadController::cancel(Tasks::TaskId id) {
    if (!owns(id)) return false;
    if (canCancel(id)) {
        if (!m_download->cancel()) return true;
        m_running = false;
        m_activity->tasks()->cancel(m_task);
        m_status = tr("已取消，原文件未替换；完整分段已保留，重开程序后用同一链接和目标可继续。");
        if (m_dialog) { m_dialog->setBusy(false); m_dialog->showProgress(0, 1, m_status); }
    }
    return true;
}
void DirectDownloadController::shutdown() { if (m_running) cancel(m_task); }
