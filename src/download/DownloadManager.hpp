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
    qint64 size;
    QString path;
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
    void downloadFile(const QString &url, const QString &destinationPath,
                      const QString &expectedSha1 = QString());
    void downloadLibraries(const QList<Library> &libraries, const QString &basePath);
    void downloadAssets(const AssetIndex &assetIndex, const QString &basePath);
    void downloadClientJar(const QString &url, const QString &destinationPath,
                           const QString &expectedSha1);

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

    void startDownload(const QUrl &url, const QString &destinationPath,
                       const QString &expectedSha1 = QString());
    void handleDownloadError(QNetworkReply *reply, const QString &errorMessage);
    void checkAllDownloadsFinished();
    void parseAndDownloadAssets(const QString &indexPath);
};

Q_DECLARE_METATYPE(MinecraftVersion)
Q_DECLARE_METATYPE(Library)
Q_DECLARE_METATYPE(AssetIndex)
Q_DECLARE_METATYPE(DownloadInfo)

#endif  // DOWNLOADMANAGER_H