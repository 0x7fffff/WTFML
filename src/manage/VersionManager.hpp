/**
 * @file VersionManager.hpp
 * @brief Declares version discovery, metadata fetching, and download orchestration.
 * @author LoserKugua
 * @date 2026-04-10
 * @note 该模块负责版本列表、版本详情获取和版本下载管理。
 */
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QStringList>

#include "../download/DownloadManager.hpp"

class VersionManager : public QObject {
    Q_OBJECT

public:
    explicit VersionManager(QObject *parent = nullptr);
    ~VersionManager();

    // Version management
    QStringList installedVersions() const;
    QStringList availableVersions() const;
    QString getLatestVersion(const QString &type = "release") const;

    // Version fetching from Mojang API
    void fetchAvailableVersions();
    void fetchVersionDetails(const QString &versionId);

    // Version downloading
    void downloadVersion(const QString &versionId, const QString &destinationPath);
    void downloadVersionLibraries(const QString &versionId, const QString &basePath);
    void downloadVersionAssets(const QString &versionId, const QString &basePath);

    // Version JSON access
    QJsonObject getVersionJson(const QString &versionId) const;

signals:
    void versionsFetched(const QStringList &versions);
    void versionDetailsFetched(const QString &versionId, const QJsonObject &versionJson);
    void versionDownloadProgress(const QString &versionId, const QString &fileName,
                                 qint64 bytesReceived, qint64 bytesTotal);
    void versionDownloadFinished(const QString &versionId, bool success,
                                 const QString &errorMessage = QString());

private slots:
    void onVersionManifestFetched(const QList<MinecraftVersion> &versions);
    void onVersionJsonFetched(const QJsonObject &versionJson);
    void onDownloadProgress(const QString &fileName, qint64 bytesReceived, qint64 bytesTotal);
    void onDownloadFinished(const QString &fileName, bool success,
                            const QString &errorMessage = QString());

private:
    DownloadManager *downloadManager;
    // versionId -> versionJson。mutable 是因为 getVersionJson() 是 const 的，
    // 但它在回读磁盘后要顺手填充这个缓存。
    mutable QMap<QString, QJsonObject> versionJsons;
    QString m_currentVersionDownloading;
};
