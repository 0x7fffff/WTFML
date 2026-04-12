/**
 * @file VersionManager.cpp
 * @brief Implements version discovery and download orchestration.
 * @author LoserKugua
 * @date 2026-04-10
 * @note 处理版本 metadata 和下载流程，避免 UI 中直接访问网络。
 *
 * Uses DownloadManager to fetch version manifests, version JSON, and to
 * download required libraries and assets for selected Minecraft versions.
 */

#include "VersionManager.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QStandardPaths>

VersionManager::VersionManager(QObject *parent)
    : QObject(parent), downloadManager(new DownloadManager(this)) {
    connect(downloadManager, &DownloadManager::versionManifestFetched, this,
            &VersionManager::onVersionManifestFetched);
    connect(downloadManager, &DownloadManager::versionJsonFetched, this,
            &VersionManager::onVersionJsonFetched);
    connect(downloadManager, &DownloadManager::downloadProgress, this,
            &VersionManager::onDownloadProgress);
    connect(downloadManager, &DownloadManager::downloadFinished, this,
            &VersionManager::onDownloadFinished);
}

VersionManager::~VersionManager() {
    // DownloadManager is automatically deleted as child
}

QStringList VersionManager::installedVersions() const {
    QStringList versions;

    // Scan for installed versions in .minecraft/versions
    QString versionsPath = QCoreApplication::applicationDirPath() + "/.minecraft/versions";
    QDir versionsDir(versionsPath);

    if (versionsDir.exists()) {
        QStringList versionDirs = versionsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &versionDir : versionDirs) {
            QString jarPath = versionsPath + "/" + versionDir + "/" + versionDir + ".jar";
            if (QFile::exists(jarPath)) {
                versions.append(versionDir);
            }
        }
    }

    // Add custom version option if no versions installed
    if (versions.isEmpty()) {
        versions.append(QStringLiteral("自定义版本"));
    }

    return versions;
}

QStringList VersionManager::availableVersions() const {
    QStringList versions;
    for (const MinecraftVersion &version : downloadManager->getAvailableVersions()) {
        versions.append(version.id);
    }
    return versions;
}

QString VersionManager::getLatestVersion(const QString &type) const {
    return downloadManager->getLatestVersion(type);
}

void VersionManager::fetchAvailableVersions() { downloadManager->fetchVersionManifest(); }

void VersionManager::fetchVersionDetails(const QString &versionId) {
    if (downloadManager->getAvailableVersions().isEmpty()) {
        QMetaObject::Connection conn;
        conn = connect(downloadManager, &DownloadManager::versionManifestFetched, this,
                       [this, versionId, &conn](const QList<MinecraftVersion> &) {
                           disconnect(conn);
                           downloadManager->fetchVersionJson(versionId);
                       });
        downloadManager->fetchVersionManifest();
    } else {
        downloadManager->fetchVersionJson(versionId);
    }
}

void VersionManager::downloadVersion(const QString &versionId, const QString &destinationPath) {
    m_currentVersionDownloading = versionId;

    // Get version JSON first
    if (!versionJsons.contains(versionId)) {
        qWarning() << "Version JSON not available for" << versionId << ". Fetch details first.";
        return;
    }

    const QJsonObject &versionJson = versionJsons[versionId];
    const QString basePath = QCoreApplication::applicationDirPath() + "/.minecraft";

    // Download client jar
    if (versionJson.contains("downloads") && versionJson["downloads"].isObject()) {
        const QJsonObject downloads = versionJson["downloads"].toObject();
        if (downloads.contains("client") && downloads["client"].isObject()) {
            const QJsonObject client = downloads["client"].toObject();
            QString url = client["url"].toString();
            QString sha1 = client["sha1"].toString();

            QString jarPath = destinationPath + "/" + versionId + ".jar";
            downloadManager->downloadClientJar(url, jarPath, sha1);
        }
    }

    // Download libraries
    QList<Library> libraries = downloadManager->extractLibraries(versionJson);
    downloadManager->downloadLibraries(libraries, basePath + "/libraries");

    // Download assets
    AssetIndex assetIndex = downloadManager->extractAssetIndex(versionJson);
    downloadManager->downloadAssets(assetIndex, basePath + "/assets");
}

void VersionManager::downloadVersionLibraries(const QString &versionId, const QString &basePath) {
    if (!versionJsons.contains(versionId)) {
        qWarning() << "Version JSON not available for" << versionId << ". Fetch details first.";
        return;
    }

    const QJsonObject &versionJson = versionJsons[versionId];
    QList<Library> libraries = downloadManager->extractLibraries(versionJson);
    downloadManager->downloadLibraries(libraries, basePath);
}

void VersionManager::downloadVersionAssets(const QString &versionId, const QString &basePath) {
    if (!versionJsons.contains(versionId)) {
        qWarning() << "Version JSON not available for" << versionId << ". Fetch details first.";
        return;
    }

    const QJsonObject &versionJson = versionJsons[versionId];
    AssetIndex assetIndex = downloadManager->extractAssetIndex(versionJson);
    downloadManager->downloadAssets(assetIndex, basePath);
}

QJsonObject VersionManager::getVersionJson(const QString &versionId) const {
    return versionJsons.value(versionId, QJsonObject());
}

void VersionManager::onVersionManifestFetched(const QList<MinecraftVersion> &versions) {
    QStringList versionIds;
    for (const MinecraftVersion &version : versions) {
        versionIds.append(version.id);
    }
    emit versionsFetched(versionIds);
}

void VersionManager::onVersionJsonFetched(const QJsonObject &versionJson) {
    if (versionJson.contains("id")) {
        QString versionId = versionJson["id"].toString();
        versionJsons[versionId] = versionJson;
        emit versionDetailsFetched(versionId, versionJson);
    }
}

void VersionManager::onDownloadProgress(const QString &fileName, qint64 bytesReceived,
                                        qint64 bytesTotal) {
    // For version downloads, we need to map back to version ID
    // This is a simplified implementation - in practice, you'd track which version is being
    // downloaded
    emit versionDownloadProgress("unknown", fileName, bytesReceived, bytesTotal);
}

void VersionManager::onDownloadFinished(const QString &fileName, bool success,
                                        const QString &errorMessage) {
    // Similar to progress, need to map back to version ID
    emit versionDownloadFinished(m_currentVersionDownloading, success, errorMessage);
}
