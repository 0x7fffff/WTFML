#pragma once

#include "diag/GameExitDiagnosis.hpp"
#include "tasks/TaskId.hpp"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>

#include <functional>

class ActivityCenterController;
class OverlayHost;
class QWidget;

class CrashReportController final : public QObject {
    Q_OBJECT

public:
    struct LaunchFacts {
        QString instanceId;
        QString instanceName;
        QString accountName;
        qint64 processId = 0;
        qint64 startedAtMs = 0;
        int requiredJavaMajor = 0;
        QString javaPath;
        int xmxMb = 0;
        int xmsMb = 0;
        QString loaderName;
        QString loaderVersion;
    };

    struct Host {
        std::function<void(const QString &)> log;
    };

    CrashReportController(ActivityCenterController *activity, OverlayHost *overlay,
                          QWidget *dialogParent, Host host, QObject *parent = nullptr);

    Tasks::TaskId report(int sessionId, int exitCode, bool crashed,
                         const LaunchFacts &launch, const QString &errorLog,
                         const QString &fullLog);
    QSet<Tasks::TaskId> recordIds() const;
    bool hasRecord(Tasks::TaskId id) const;
    int sessionIdForTask(Tasks::TaskId id) const;
    void exportReport(const QString &key);
    void openReportsDirectory();

private:
    struct Record {
        GameExitDiagnosis::Facts facts;
        int sessionId = 0;
        QString accountName;
        qint64 processId = 0;
        QDateTime startedAt;
        QString errorExcerpt;
        QString logTail;
        int errorLines = 0;
        int totalLines = 0;
    };

    Record buildRecord(int sessionId, int exitCode, bool crashed,
                       const LaunchFacts &launch, const QString &errorLog,
                       const QString &fullLog) const;
    static int lineCount(const QString &text);
    static QString lastLines(const QString &text, int maxLines);
    static QString exportDirectory();
    static QString writeExportFile(const QString &path, const QString &text);
    void log(const QString &message) const;

    ActivityCenterController *m_activity = nullptr;
    OverlayHost *m_overlay = nullptr;
    QWidget *m_dialogParent = nullptr;
    Host m_host;
    QHash<Tasks::TaskId, Record> m_records;
};
