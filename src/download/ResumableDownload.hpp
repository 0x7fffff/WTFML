#pragma once
#include <QObject>
#include <QPointer>
#include <QSharedPointer>
#include <QAtomicInt>
#include <QMap>
#include <QUrl>
#include <functional>

class DownloadManager;
class QNetworkReply;
class QLockFile;

// 大文件协调器：探测/分段仍复用 DownloadManager；缓存校验和合并在后台执行。
// 缓存只记录已完整落盘且有摘要的分段，绝不把一次未完成响应当成续传数据。
class ResumableDownload final : public QObject {
    Q_OBJECT
public:
    explicit ResumableDownload(QObject *parent = nullptr);
    ~ResumableDownload() override;
    void start(const QUrl &url, const QString &target, const QString &sha1 = {}, qint64 size = -1,
               bool overwrite = true, const QString &cacheRoot = {});
    bool cancel();
    bool isRunning() const { return m_running; }
    bool canCancel() const;
    static QString cacheDirectory(const QUrl &url, const QString &target, const QString &root = {});
signals:
    void progress(qint64 done, qint64 total);
    void statusChanged(const QString &message);
    void finished(bool success, const QString &error);
    void checkpointSaved(qint64 reusableBytes);
private:
    enum class Stage { Probe, Prepare, Parts, Switching, Single, Assemble, Done };
    void probe(int attempt = 0);
    void prepareCache();
    void downloadParts();
    void startSingle();
    void assemble();
    void finish(bool success, const QString &error);
    bool saveManifest(QString *error);
    QString partPath(int index) const;
    qint64 partLength(int index) const;
    qint64 completedBytes() const;
    void emitProgress();
    void clearCacheFiles() const;
    DownloadManager *m_files = nullptr;
    QPointer<QNetworkReply> m_probe;
    QSharedPointer<QLockFile> m_lock;
    QSharedPointer<QAtomicInt> m_abort;
    Stage m_stage = Stage::Done;
    bool m_running = false, m_overwrite = true;
    quint64 m_generation = 0;
    QUrl m_url, m_source;
    QString m_target, m_sha1, m_cache, m_urlHash;
    qint64 m_expectedSize = -1, m_total = -1, m_partSize = 0;
    int m_count = 0;
    QByteArray m_etag;
    QMap<int, QString> m_complete;
    QMap<int, qint64> m_live;
};
