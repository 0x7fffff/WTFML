#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H

/**
 * @file DownloadManager.hpp
 * @brief Declares download and version manifest handling for WTFML.
 * @author LoserKugua
 * @date 2026-04-10
 * @note Defines the data structures and signals for version and asset downloads.
 *
 * Responsible for fetching Mojang version manifests, downloading libraries,
 * assets and client jars, and verifying SHA1 integrity.
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

// Structs for Minecraft data
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

    // Version manifest operations
    void fetchVersionManifest();
    QList<MinecraftVersion> getAvailableVersions() const;
    QString getLatestVersion(const QString &type = "release") const;

    // Version JSON operations
    void fetchVersionJson(const QString &versionId);
    QJsonObject getVersionJson() const;

    // Download operations
    void downloadFile(const QString &url, const QString &destinationPath,
                      const QString &expectedSha1 = QString());
    void downloadLibraries(const QList<Library> &libraries, const QString &basePath);
    void downloadAssets(const AssetIndex &assetIndex, const QString &basePath);
    void downloadClientJar(const QString &url, const QString &destinationPath,
                           const QString &expectedSha1);

    // Utility methods
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
    QMap<QNetworkReply *, QString> activeDownloads;  // reply -> destination path
    QMap<QNetworkReply *, QString> expectedSha1s;    // reply -> expected SHA1
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