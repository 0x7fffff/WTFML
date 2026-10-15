#include "controllers/IntegrityController.hpp"

#include "controllers/ActivityCenterController.hpp"
#include "IntegrityReportPage.hpp"
#include "InlineMessage.hpp"
#include "OverlayHost.hpp"
#include "manage/VersionManager.hpp"

#include <QClipboard>
#include <QGuiApplication>
#include <QMetaObject>
#include <QThread>
#include <QTimer>

#include <utility>

IntegrityController::IntegrityController(VersionManager *versions,
                                         ActivityCenterController *activity,
                                         OverlayHost *overlay, QWidget *pageParent,
                                         Host host, QObject *parent)
    : QObject(parent), m_versions(versions), m_activity(activity), m_overlay(overlay),
      m_pageParent(pageParent), m_host(std::move(host)) {}

IntegrityController::~IntegrityController() { shutdown(); }

QString IntegrityController::displayName(const QString &id) const {
    return m_host.displayName ? m_host.displayName(id) : id;
}

void IntegrityController::log(const QString &message) const {
    if (m_host.log) m_host.log(message);
}

bool IntegrityController::scanning(const QString &versionId) const {
    return !versionId.isEmpty() && m_scanVersionId == versionId;
}

QStringList IntegrityController::flaggedPaths(const QString &versionId) const {
    // 报告只有**一份**（最近那一趟）。问的是别的实例时一个字都不回：把 A 的清单交给 B 的
    // 补全，会让 B 去重下几个跟它没关系的文件（而且那些路径在 B 那边根本对不上号）。
    if (versionId.isEmpty() || m_reportVersionId != versionId) {
        return QStringList();
    }
    return m_report.problemPaths();
}

void IntegrityController::shutdown() {
    m_abort.storeRelaxed(1);
    if (m_thread != nullptr) m_thread->wait();
}

void IntegrityController::submit(const QString &versionId, const BusyFacts &busy) {
    if (versionId.isEmpty() || scanning(versionId)) return;
    if (!m_scanVersionId.isEmpty()) {
        InlineMessage::information(m_overlay, tr("现在做不了"),
            tr("「%1」正在做完整性检查，等它完了再检查这个实例。")
                .arg(displayName(m_scanVersionId)));
        return;
    }
    if (busy.targetBeingModified) {
        InlineMessage::information(m_overlay, tr("现在做不了"),
            tr("「%1」这会儿正在装（或者正在导入 / 正在检查）。等它完了再来。")
                .arg(displayName(versionId)));
        return;
    }
    if (busy.anotherDownloadRunning) {
        InlineMessage::information(m_overlay, tr("现在做不了"),
                                   tr("已经有一个下载或者安装任务在跑了，等它结束再检查这个实例。"));
        return;
    }
    const QString inheritanceProblem = m_versions->inheritanceProblem(versionId);
    if (!inheritanceProblem.isEmpty()) {
        log(tr("无法体检 %1 的文件：版本继承链不完整").arg(versionId));
        InlineMessage::warning(m_overlay, tr("版本不完整"), inheritanceProblem);
        return;
    }
    QString planError;
    const QList<VersionIntegrity::ExpectedFile> files =
        m_versions->integrityPlan(versionId, &planError);
    if (!planError.isEmpty()) {
        log(tr("无法体检 %1 的文件：%2").arg(versionId, planError));
        InlineMessage::warning(m_overlay, tr("版本信息缺失"), planError);
        return;
    }
    QList<Tasks::TaskStep> steps;
    for (VersionIntegrity::Group group : VersionIntegrity::allGroups())
        steps.append({VersionIntegrity::groupName(group), Tasks::TaskStepState::Pending, {}});
    m_taskId = m_activity->begin(Tasks::TaskKind::Integrity,
                                 tr("检查「%1」的文件").arg(displayName(versionId)),
                                 versionId, steps);
    m_activity->tasks()->setProgress(m_taskId, 0, qint64(files.size()));
    log(tr("开始体检「%1」的文件（%2 个）...").arg(displayName(versionId)).arg(files.size()));
    m_scanVersionId = versionId;
    m_abort.storeRelaxed(0);
    if (m_thread != nullptr) {
        m_thread->deleteLater();
        m_thread = nullptr;
    }
    m_thread = QThread::create([this, files] {
        constexpr int stride = 64;
        VersionIntegrity::Report report;
        report.checked = int(files.size());
        int groupIndex = 0;
        int lastReported = 0;
        for (int i = 0; i < files.size(); ++i) {
            if (m_abort.loadRelaxed() != 0) return;
            const VersionIntegrity::ExpectedFile &file = files.at(i);
            if (VersionIntegrity::Verdict verdict = VersionIntegrity::check(file); !verdict.ok())
                report.problems.append(verdict);
            const int currentGroup = VersionIntegrity::groupIndex(file.group);
            const bool boundary = currentGroup != groupIndex;
            groupIndex = currentGroup;
            if (boundary || i - lastReported >= stride || i + 1 == files.size()) {
                lastReported = i;
                QMetaObject::invokeMethod(this,
                    [this, checked = i + 1, total = int(files.size()), currentGroup] {
                        progress(checked, total, currentGroup);
                    }, Qt::QueuedConnection);
            }
        }
        QMetaObject::invokeMethod(this, [this, report] { finished(report); }, Qt::QueuedConnection);
    });
    m_thread->setParent(this);
    m_thread->start();
}

void IntegrityController::progress(int checked, int total, int groupIndex) {
    if (m_taskId == Tasks::invalidTaskId() || !m_activity->tasks()->has(m_taskId)) return;
    m_activity->tasks()->setProgress(m_taskId, checked, total);
    const int count = int(VersionIntegrity::allGroups().size());
    for (int i = 0; i < count; ++i)
        m_activity->tasks()->setStepState(
            m_taskId, i, i < groupIndex ? Tasks::TaskStepState::Done
                        : i == groupIndex ? Tasks::TaskStepState::Running
                                          : Tasks::TaskStepState::Pending);
}

void IntegrityController::finished(const VersionIntegrity::Report &report) {
    const QString versionId = m_scanVersionId;
    m_scanVersionId.clear();
    if (m_taskId != Tasks::invalidTaskId() && m_activity->tasks()->has(m_taskId)) {
        for (int i = 0; i < int(VersionIntegrity::allGroups().size()); ++i)
            m_activity->tasks()->setStepState(m_taskId, i, Tasks::TaskStepState::Done);
        m_activity->tasks()->setDetail(m_taskId, report.summary());
        m_activity->finish(m_taskId, report.error.isEmpty(), report.error, false);
    }
    m_taskId = Tasks::invalidTaskId();
    if (versionId.isEmpty()) return;
    log(report.error.isEmpty()
            ? tr("体检「%1」完成：%2").arg(displayName(versionId), report.summary())
            : tr("体检「%1」没跑起来：%2").arg(displayName(versionId), report.error));
    m_report = report;
    m_reportVersionId = versionId;
    if (m_host.openWhenOverlayFree)
        m_host.openWhenOverlayFree([this, versionId] { openReport(versionId); });
}

void IntegrityController::openReport(const QString &versionId) {
    if (m_overlay == nullptr || m_reportVersionId != versionId) return;
    if (m_page == nullptr) {
        m_page = new IntegrityReportPage(m_pageParent);
        connect(m_page, &IntegrityReportPage::actionRequested, this,
                [this](IntegrityReportPage::Action action) { reportAction(int(action)); });
    }
    IntegrityReportPage::Report view;
    view.instanceName = displayName(versionId);
    view.summary = m_report.summary();
    view.error = m_report.error;
    for (VersionIntegrity::Group group : VersionIntegrity::allGroups()) {
        IntegrityReportPage::Group item;
        item.name = VersionIntegrity::groupName(group);
        item.missing = m_report.missingIn(group);
        item.corrupt = m_report.corruptIn(group);
        for (const VersionIntegrity::Verdict &v : m_report.problemsInGroup(group))
            item.rows.append({v.label, v.detail(), v.path});
        view.groups.append(item);
    }
    m_page->setReport(view);
    m_overlay->execPage(m_page, IntegrityReportPage::windowTitle());
}

void IntegrityController::reportAction(int action) {
    if (action == int(IntegrityReportPage::Action::CopyReport)) {
        copyReport();
        return;
    }
    if (action != int(IntegrityReportPage::Action::Repair) || m_reportVersionId.isEmpty()) return;
    const QString id = m_reportVersionId;
    if (m_page != nullptr) m_page->reject();
    QTimer::singleShot(0, this, [this, id] { if (m_host.repair) m_host.repair(id); });
}

void IntegrityController::copyReport() {
    if (m_reportVersionId.isEmpty()) return;
    QGuiApplication::clipboard()->setText(
        VersionIntegrity::plainReport(m_report, displayName(m_reportVersionId)));
    log(tr("完整性检查报告已经复制到剪贴板。"));
}
