#pragma once
#include <QObject>
#include <QPointer>
#include <QSharedPointer>
#include <QAtomicInt>
#include <QElapsedTimer>
#include <QMap>
#include <QUrl>
#include <functional>
#include "NetworkManagerFactory.hpp"

class DownloadManager;
class QNetworkReply;
class QLockFile;
class QTimer;

// 大文件协调器：探测/分段仍复用 DownloadManager；缓存校验和合并在后台执行。
// 缓存只记录已完整落盘且有摘要的分段，绝不把一次未完成响应当成续传数据。
class ResumableDownload final : public QObject {
    Q_OBJECT
public:
    /// 安全分段的边界（唯一出处）：小于 4 MiB 不值得多开连接；超过 8192 个 4 MiB 分段
    /// 已经超出协调器的上限。`DownloadManager` 的自动升级判据也引用这两个数。
    static constexpr qint64 kMinimumBytes = 4 * 1024 * 1024;
    static constexpr qint64 kMaximumBytes = 8192LL * 4 * 1024 * 1024;
    /// 一个协调器同时发几条 Range 请求。
    static constexpr int kPartConcurrency = 4;
    explicit ResumableDownload(QObject *parent = nullptr, NetworkManagerFactory networkFactory = {});
    ~ResumableDownload() override;
    void start(const QUrl &url, const QString &target, const QString &sha1 = {}, qint64 size = -1,
               bool overwrite = true, const QString &cacheRoot = {});
    bool cancel();
    bool isRunning() const { return m_running; }
    bool canCancel() const;
    /// 是否已经真正发过网络请求。为 false 时 `finished(false, …)` 只可能是本地准备失败
    /// （地址/路径无效、缓存目录不可写、同一下载的缓存被占用），调用方应改走普通下载，
    /// 而不是把文件判成源站失败。
    bool networkAttempted() const { return m_networkAttempted; }

    /// 下载速度上限（字节/秒，0 = 不限；，§9.2 限速）。
    ///
    /// 协调器内部那几段 Range 请求走的是它自己那个 `DownloadManager`（`m_files`），所以
    /// **光给外面那个下载器设限是不够的**：大于 4 MiB 的文件会被升级成分段作业，绕到这儿
    /// 来。限值由 `DownloadManager::startSegmentedDownload()` 在 `start()` 之前推过来，
    /// 改设置时也会转发给已经在跑的那些（见 `DownloadManager::setSpeedLimitBytesPerSecond`）。
    /// 工具箱里那个「直链下载」不推这一项 —— 它是用户手动开的另一件工具。
    void setSpeedLimitBytesPerSecond(qint64 bytesPerSecond);

    /// 把外层那个下载器交给协调器：`start()` 建自己那份 `DownloadManager`
    /// 时和它**共用一个令牌桶** —— 分段作业合计起来才等于设置里的那个上限，而不是每个
    /// 作业各拿满一份。`DownloadManager::startSegmentedDownload()` 在 start() 之前调。
    void setSpeedLimitBucketOwner(DownloadManager *owner) { m_speedBucketOwner = owner; }

    static QString cacheDirectory(const QUrl &url, const QString &target, const QString &root = {});
signals:
    void progress(qint64 done, qint64 total);
    void transferBytes(qint64 bytes);
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
    /// 最后一层兜底：探测/分段阶段**长时间一点进展都没有**时，先补一次缺的段，再不行就
    /// 报错收尾。每条回复自己的看门狗（DownloadManager）已经把"单条链路"管住了；这一条
    /// 管的是记账层面的死角 —— 比如哪次批次信号没来、pending 没归零。没有它，那类问题
    /// 的表现就是"进度条永远停着、没有错误"，用户只能重启（ 那次二十分钟）。
    void checkStuck();
    static constexpr qint64 kStuckGiveUpMs = 5 * 60 * 1000;
    QElapsedTimer m_progressClock;
    QTimer *m_stuckTimer = nullptr;
    DownloadManager *m_files = nullptr;
    NetworkManagerFactory m_networkFactory;
    /// 速度上限（字节/秒，0 = 不限；见 setSpeedLimitBytesPerSecond()）。`m_files` 每次
    /// start() 都会重建，所以这个值留在协调器身上、建好之后当场推过去。
    qint64 m_speedLimitBytesPerSecond = 0;
    /// 外层那个下载器（共用令牌桶的对方）。本对象是它的子对象，活得比它短。
    DownloadManager *m_speedBucketOwner = nullptr;
    QPointer<QNetworkReply> m_probe;
    QSharedPointer<QLockFile> m_lock;
    QSharedPointer<QAtomicInt> m_abort;
    Stage m_stage = Stage::Done;
    bool m_running = false, m_overwrite = true, m_networkAttempted = false;
    /// 分段那一轮失败之后**只补缺失分段**的那一次机会用掉了没有（见 .cpp 的失败分支）。
    bool m_partsRetried = false;
    /// 已经发现分段失败、但这一批还有别的分段在飞：等 allDownloadsFinished 再补。
    bool m_partsRetryPending = false;
    quint64 m_generation = 0;
    QUrl m_url, m_source;
    QString m_target, m_sha1, m_cache, m_urlHash;
    qint64 m_expectedSize = -1, m_total = -1, m_partSize = 0;
    int m_count = 0;
    QByteArray m_etag;
    QMap<int, QString> m_complete;
    QMap<int, qint64> m_live;
};
