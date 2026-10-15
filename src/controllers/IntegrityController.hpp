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
