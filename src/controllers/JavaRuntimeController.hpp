#pragma once

#include <QList>
#include <QMap>
#include <QObject>
#include <QString>

/**
 * Runs Java discovery and runtime installation away from the GUI thread.
 *
 * The controller deliberately owns the operation state instead of MainWindow:
 * the window only presents results, while this class serialises downloads and
 * honours a request to stop a batch after the current file has finished.
 */
class JavaRuntimeController final : public QObject {
    Q_OBJECT

public:
    explicit JavaRuntimeController(QObject *parent = nullptr);

    bool isBusy() const { return m_busy; }

    void detectInstalled();
    void downloadRuntime(int major);
    void downloadMissing(const QList<int> &majors);
    void cancelAfterCurrent();

signals:
    void busyChanged(bool busy);
    void statusChanged(const QString &message);
    void detectionFinished(const QMap<int, QString> &versions);
    void runtimeFinished(int major, bool ok, const QString &javaPath, bool partOfBatch);
    void downloadSequenceFinished(bool batch, int okCount, int failCount, bool cancelled);

private:
    void setBusy(bool busy);
    void startNextDownload();

    bool m_busy = false;
    bool m_batch = false;
    bool m_cancelRequested = false;
    QList<int> m_pendingMajors;
    int m_okCount = 0;
    int m_failCount = 0;
};
