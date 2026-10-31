#pragma once

#include "manage/VersionIntegrity.hpp"
#include "tasks/TaskId.hpp"

#include <QAtomicInt>
#include <QObject>

#include <functional>

class ActivityCenterController;
class IntegrityReportPage;
class OverlayHost;
class QThread;
class VersionManager;
class QWidget;

class IntegrityController final : public QObject {
    Q_OBJECT

public:
    struct BusyFacts {
        bool targetBeingModified = false;
        bool anotherDownloadRunning = false;
    };
    struct Host {
        std::function<QString(const QString &)> displayName;
        std::function<void(const QString &)> log;
        /// 体检跑完之后开报告页。**后台**完成的那一发 ⇒ 走
        /// `MainWindow::openPageWhenIdle()`：不驱逐用户正看着的那一页，等它收干净再开。
        std::function<void(std::function<void()>)> openWhenOverlayFree;
        std::function<void(const QString &)> repair;
    };

    IntegrityController(VersionManager *versions, ActivityCenterController *activity,
                        OverlayHost *overlay, QWidget *pageParent, Host host,
                        QObject *parent = nullptr);
    ~IntegrityController() override;

    bool scanning(const QString &versionId) const;
    void submit(const QString &versionId, const BusyFacts &busy);
    void openReport(const QString &versionId);
    void shutdown();
    void progress(int checked, int total, int groupIndex);
    void finished(const VersionIntegrity::Report &report);
    void reportAction(int action);
    void copyReport();

    /// 最近这一趟体检给 @p versionId 点名的那些文件（要修的，绝对路径）。**没体检过、
    /// 或者最近一趟体检的不是它 ⇒ 空表**。
    ///
    /// 唯一读者是补全（MainWindow::repairInstanceFiles → VersionManager::repairVersionFiles）：
    /// 体检说谁坏了，这一批就把它当"要下"，两边不再互相打脸（见 setFlaggedPaths 那段）。
    /// 别的实例体检出来的清单**不会**串到这里来 —— 补全一个实例时它是干净的。
    QStringList flaggedPaths(const QString &versionId) const;

private:
    QString displayName(const QString &id) const;
    void log(const QString &message) const;

    VersionManager *m_versions = nullptr;
    ActivityCenterController *m_activity = nullptr;
    OverlayHost *m_overlay = nullptr;
    QWidget *m_pageParent = nullptr;
    Host m_host;
    Tasks::TaskId m_taskId = Tasks::invalidTaskId();
    QString m_scanVersionId;
    QThread *m_thread = nullptr;
    QAtomicInt m_abort{0};
    VersionIntegrity::Report m_report;
    QString m_reportVersionId;
    IntegrityReportPage *m_page = nullptr;
};
