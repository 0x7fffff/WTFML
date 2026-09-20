#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H

/**
 * @file DownloadManager.hpp
 * @brief WTFML的下载与版本列表功能声明。
 * @author LoserKugua
 * @date 2026-04-10
 * @note 定义版本和 asset 的数据结构和信号。
 *
 * 负责获取 mojang 版本列表、 library 下载、 asset 和客户端 jar 文件、验证 SHA1 完整性。
 */

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariant>

// Minecraft 数据的结构
struct MinecraftVersion {
    QString id;
    QString type;
    QString url;
    QString time;
    QString releaseTime;
};

struct Library {
    QString name;
    QString url;
    QString sha1;
    qint64 size = 0;
    QString path;
    // 1.18.2 及以下的 natives 是 downloads.classifiers 里的独立 jar，
    // 它不进 classpath，而是启动时解压到 versions/<id>/natives/。
    bool isNative = false;
    // 该 jar 内需要跳过的条目前缀，来自 library 的 extract.exclude（通常是 META-INF/）。
    QStringList extractExclude;
};

struct AssetIndex {
    QString id;
    QString sha1;
    qint64 size;
    QString url;
    qint64 totalSize;
};

struct DownloadInfo {
    QString url;
    QString sha1;
    qint64 size;
    QString path;
};

/**
 * @brief 决定一个已经存在于磁盘上的文件要不要重新下载。
 *
 * 重新点"下载"时用它来跳过已经完好的文件，避免整包重下。
 */
enum class VerifyMode {
    Always,       // 无条件重下（用于需要刷新的索引文件）
    ExistsOnly,   // 文件存在就算好（用于海量 asset，全量哈希会让界面卡住几秒）
    SizeAndSha1,  // 先比字节数，再比 SHA1（用于客户端 jar 和 library）
};

/**
 * @brief 一批下载任务的统计，供 UI 显示进度和汇总失败原因。
 */
struct DownloadStats {
    int total = 0;    // 本批登记的任务总数（含被跳过的）
    int done = 0;     // 已结束的（成功、失败、跳过都算）
    int failed = 0;   // 其中失败的
    int skipped = 0;  // 其中因为文件已存在而跳过的
    QString currentFile;      // 最近一次有动静的文件，供状态栏显示
    QStringList failedPaths;  // 失败文件的完整路径，比 40 位哈希文件名可读
};

class DownloadManager : public QObject {
    Q_OBJECT

public:
    explicit DownloadManager(QObject *parent = nullptr);
    ~DownloadManager();

    // 版本列表操作
    void fetchVersionManifest();
    QList<MinecraftVersion> getAvailableVersions() const;
    QString getLatestVersion(const QString &type = "release") const;

    // 版本 JSON 文件操作
    void fetchVersionJson(const QString &versionId);
    QJsonObject getVersionJson() const;

    // 下载操作
    // @param expectedSize 文件应有的字节数，传 -1 表示未知（旧版本 JSON 和 natives
    //        classifier 都没有 size 字段），此时退化成只比 SHA1。
    // @param mode 见 VerifyMode，决定已存在的文件要不要重下。
    void downloadFile(const QString &url, const QString &destinationPath,
                      const QString &expectedSha1 = QString(), qint64 expectedSize = -1,
                      VerifyMode mode = VerifyMode::SizeAndSha1);
    void downloadLibraries(const QList<Library> &libraries, const QString &basePath);
    void downloadAssets(const AssetIndex &assetIndex, const QString &basePath);
    void downloadClientJar(const QString &url, const QString &destinationPath,
                           const QString &expectedSha1, qint64 expectedSize = -1);

    // 批次记账。beginBatch() / endBatch() 之间的所有下载被看作一批，
    // 整批结束时 allDownloadsFinished 只会发射一次；两者之间不允许提前 return，
    // 否则 m_batchOpen 永远为真，完成信号永不发射，UI 会一直停在"下载中"。
    void beginBatch();
    void endBatch();
    bool isBusy() const;
    DownloadStats stats() const;

    // Utility methods 实用方法？？//需要进一步完善注释
    bool verifyFileSha1(const QString &filePath, const QString &expectedSha1);
    QString calculateFileSha1(const QString &filePath);
    void parseVersionManifest(const QJsonDocument &json);
    void parseVersionJson(const QJsonDocument &json);
    QList<Library> extractLibraries(const QJsonObject &versionJson);
    AssetIndex extractAssetIndex(const QJsonObject &versionJson);

signals:
    void versionManifestFetched(const QList<MinecraftVersion> &versions);
    void versionJsonFetched(const QJsonObject &versionJson);
    void downloadProgress(const QString &fileName, qint64 bytesReceived, qint64 bytesTotal);
    void downloadFinished(const QString &fileName, bool success,
                          const QString &errorMessage = QString());
    // 进度用标量发，避免每来一个网络分片就拷贝一份 DownloadStats。
    void batchProgress(int done, int total, int failed, const QString &currentFile);
    void allDownloadsFinished(bool success);

private slots:
    void onVersionManifestReplyFinished();
    void onVersionJsonReplyFinished();
    void onDownloadReplyFinished();
    void onDownloadReplyProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onAssetIndexDownloaded();

private:
    QNetworkAccessManager *networkManager;
    QList<MinecraftVersion> availableVersions;
    QJsonObject currentVersionJson;
    QMap<QNetworkReply *, QString>
        activeDownloads;  // reply -> destination path   回应目标路径//需要进一步翻译
    QMap<QNetworkReply *, QString> expectedSha1s;  // reply -> expected SHA1  回应期望的 SHA1
    int pendingDownloads;

    // 批次记账
    int m_totalJobs = 0;
    int m_doneJobs = 0;
    int m_failedJobs = 0;
    int m_skippedJobs = 0;
    QStringList m_failedPaths;
    QString m_currentFile;
    bool m_batchOpen = false;
    bool m_batchEmitted = false;
    // 析构时 abort() 可能同步回调进 finished 处理函数，用它挡住
    bool m_shuttingDown = false;

    void startDownload(const QUrl &url, const QString &destinationPath,
                       const QString &expectedSha1 = QString());
    void handleDownloadError(QNetworkReply *reply, const QString &errorMessage);
    void checkAllDownloadsFinished();
    void emitBatchProgress(const QString &currentFile);
    bool fileAlreadyValid(const QString &path, const QString &expectedSha1, qint64 expectedSize,
                          VerifyMode mode);
    void parseAndDownloadAssets(const QString &indexPath);
};

Q_DECLARE_METATYPE(MinecraftVersion)
Q_DECLARE_METATYPE(Library)
Q_DECLARE_METATYPE(AssetIndex)
Q_DECLARE_METATYPE(DownloadInfo)

#endif  // DOWNLOADMANAGER_H