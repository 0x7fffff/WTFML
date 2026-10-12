#include "JavaRuntimeController.hpp"

#include "settings/GlobalJavaSettings.hpp"

#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

namespace {
struct DownloadResult {
    int major = 0;
    bool ok = false;
    QString javaPath;
    QMap<int, QString> installed;
};
}  // namespace

JavaRuntimeController::JavaRuntimeController(QObject *parent) : QObject(parent) {}

void JavaRuntimeController::setBusy(bool busy) {
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged(busy);
}

void JavaRuntimeController::detectInstalled() {
    if (m_busy) {
        return;
    }
    setBusy(true);
    emit statusChanged(tr("正在检测本机已安装的 Java..."));

    auto *watcher = new QFutureWatcher<QMap<int, QString>>(this);
    connect(watcher, &QFutureWatcher<QMap<int, QString>>::finished, this, [this, watcher] {
        const QMap<int, QString> versions = watcher->result();
        watcher->deleteLater();
        setBusy(false);
        emit detectionFinished(versions);
    });
    watcher->setFuture(
        QtConcurrent::run([] { return GlobalJavaSettings::scanLocalJavaVersions(); }));
}

void JavaRuntimeController::downloadRuntime(int major) {
    if (m_busy || major <= 0) {
        return;
    }
    m_batch = false;
    m_cancelRequested = false;
    m_pendingMajors = {major};
    m_okCount = 0;
    m_failCount = 0;
    setBusy(true);
    startNextDownload();
}

void JavaRuntimeController::downloadMissing(const QList<int> &majors) {
    if (m_busy) {
        return;
    }
    m_batch = true;
    m_cancelRequested = false;
    m_pendingMajors.clear();
    for (int major : majors) {
        if (major > 0 && !m_pendingMajors.contains(major)) {
            m_pendingMajors.append(major);
        }
    }
    m_okCount = 0;
    m_failCount = 0;
    if (m_pendingMajors.isEmpty()) {
        emit downloadSequenceFinished(true, 0, 0, false);
        return;
    }
    setBusy(true);
    startNextDownload();
}

void JavaRuntimeController::cancelAfterCurrent() {
    m_cancelRequested = true;
    m_pendingMajors.clear();
}

void JavaRuntimeController::startNextDownload() {
    if (m_cancelRequested || m_pendingMajors.isEmpty()) {
        const bool cancelled = m_cancelRequested;
        const bool batch = m_batch;
        setBusy(false);
        emit downloadSequenceFinished(batch, m_okCount, m_failCount, cancelled);
        return;
    }

    const int major = m_pendingMajors.takeFirst();
    emit statusChanged(tr("正在下载 Java %1 运行时（界面可以继续使用）...").arg(major));

    auto *watcher = new QFutureWatcher<DownloadResult>(this);
    connect(watcher, &QFutureWatcher<DownloadResult>::finished, this, [this, watcher] {
        const DownloadResult result = watcher->result();
        watcher->deleteLater();
        if (result.ok) {
            ++m_okCount;
        } else {
            ++m_failCount;
        }
        emit detectionFinished(result.installed);
        emit runtimeFinished(result.major, result.ok, result.javaPath, m_batch);
        startNextDownload();
    });
    watcher->setFuture(QtConcurrent::run([major] {
        DownloadResult result;
        result.major = major;
        result.ok = GlobalJavaSettings::downloadJavaFromMojang(major, &result.javaPath);
        result.installed = GlobalJavaSettings::scanLocalJavaVersions();
        return result;
    }));
}
