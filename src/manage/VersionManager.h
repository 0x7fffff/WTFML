#pragma once

#include <QObject>
#include <QStringList>
#include <QJsonObject>
#include "../download/DownloadManager.h"

class VersionManager : public QObject
{
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
    void versionDownloadProgress(const QString &versionId, const QString &fileName, qint64 bytesReceived, qint64 bytesTotal);
    void versionDownloadFinished(const QString &versionId, bool success, const QString &errorMessage = QString());

private slots:
    void onVersionManifestFetched(const QList<MinecraftVersion> &versions);
    void onVersionJsonFetched(const QJsonObject &versionJson);
    void onDownloadProgress(const QString &fileName, qint64 bytesReceived, qint64 bytesTotal);
    void onDownloadFinished(const QString &fileName, bool success, const QString &errorMessage = QString());

private:
    DownloadManager *downloadManager;
    QMap<QString, QJsonObject> versionJsons; // versionId -> versionJson
};
