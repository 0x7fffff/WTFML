#include "DownloadManager.h"
#include <QDebug>
#include <QEventLoop>
#include <QJsonParseError>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QCoreApplication>
#include <QFileInfo>

DownloadManager::DownloadManager(QObject *parent)
    : QObject(parent), networkManager(new QNetworkAccessManager(this)), pendingDownloads(0) {
    // Register meta types for signals
    qRegisterMetaType<MinecraftVersion>("MinecraftVersion");
    qRegisterMetaType<Library>("Library");
    qRegisterMetaType<AssetIndex>("AssetIndex");
    qRegisterMetaType<DownloadInfo>("DownloadInfo");
    qRegisterMetaType<QList<MinecraftVersion>>("QList<MinecraftVersion>");
}

DownloadManager::~DownloadManager() {
    // Clean up active downloads
    for (auto reply : activeDownloads.keys()) {
        reply->abort();
        reply->deleteLater();
    }
}

void DownloadManager::fetchVersionManifest() {
    QUrl url("https://launchermeta.mojang.com/mc/game/version_manifest.json");
    QNetworkRequest request(url);
    QNetworkReply *reply = networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, &DownloadManager::onVersionManifestReplyFinished);
}

QList<MinecraftVersion> DownloadManager::getAvailableVersions() const {
    return availableVersions;
}

QString DownloadManager::getLatestVersion(const QString &type) const {
    for (const MinecraftVersion &version : availableVersions) {
        if (version.type == type) {
            return version.id;
        }
    }
    return QString();
}

void DownloadManager::fetchVersionJson(const QString &versionId) {
    for (const MinecraftVersion &version : availableVersions) {
        if (version.id == versionId) {
            QUrl url(version.url);
            QNetworkRequest request(url);
            QNetworkReply *reply = networkManager->get(request);
            connect(reply, &QNetworkReply::finished, this, &DownloadManager::onVersionJsonReplyFinished);
            return;
        }
    }
    emit versionJsonFetched(QJsonObject());
}

QJsonObject DownloadManager::getVersionJson() const {
    return currentVersionJson;
}

void DownloadManager::downloadFile(const QString &url, const QString &destinationPath, const QString &expectedSha1) {
    QUrl qurl(url);
    QNetworkRequest request(qurl);
    QNetworkReply *reply = networkManager->get(request);
    activeDownloads[reply] = destinationPath;
    if (!expectedSha1.isEmpty()) {
        expectedSha1s[reply] = expectedSha1;
    }
    pendingDownloads++;
    connect(reply, &QNetworkReply::finished, this, &DownloadManager::onDownloadReplyFinished);
    connect(reply, &QNetworkReply::downloadProgress, this, &DownloadManager::onDownloadReplyProgress);
    qDebug() << "Started download:" << url << "->" << destinationPath;
}

void DownloadManager::downloadLibraries(const QList<Library> &libraries, const QString &basePath) {
    for (const Library &lib : libraries) {
        QString url = lib.url;
        QString destPath = basePath + "/" + lib.path;
        QDir().mkpath(QFileInfo(destPath).absolutePath());
        downloadFile(url, destPath, lib.sha1);
    }
}

void DownloadManager::downloadAssets(const AssetIndex &assetIndex, const QString &basePath) {
    // First, download the asset index JSON
    QString indexUrl = assetIndex.url;
    QString indexPath = basePath + "/indexes/" + assetIndex.id + ".json";
    QDir().mkpath(QFileInfo(indexPath).absolutePath());
    
    // Download the index file
    QUrl qurl(indexUrl);
    QNetworkRequest request(qurl);
    QNetworkReply *indexReply = networkManager->get(request);
    activeDownloads[indexReply] = indexPath;
    if (!assetIndex.sha1.isEmpty()) {
        expectedSha1s[indexReply] = assetIndex.sha1;
    }
    pendingDownloads++;
    connect(indexReply, &QNetworkReply::finished, this, &DownloadManager::onAssetIndexDownloaded);
    connect(indexReply, &QNetworkReply::downloadProgress, this, &DownloadManager::onDownloadReplyProgress);
    qDebug() << "Started downloading asset index:" << indexUrl << "->" << indexPath;
}

void DownloadManager::onAssetIndexDownloaded() {
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        qWarning() << "Asset index download finished with invalid reply.";
        return;
    }

    QString indexPath = activeDownloads.value(reply);
    if (indexPath.isEmpty()) {
        qDebug() << "Ignored finished asset index reply that is not a tracked download.";
        reply->deleteLater();
        return;
    }

    bool success = false;
    QString errorMessage;

    if (reply->error() == QNetworkReply::NoError) {
        QFile file(indexPath);
        QDir().mkpath(QFileInfo(indexPath).absolutePath());
        if (file.open(QIODevice::WriteOnly)) {
            file.write(reply->readAll());
            file.close();
            success = true;
            qDebug() << "Asset index downloaded successfully:" << indexPath;
            
            // Now parse the index and download assets
            parseAndDownloadAssets(indexPath);
        } else {
            errorMessage = "Failed to save asset index file";
        }
    } else {
        errorMessage = reply->errorString();
    }

    activeDownloads.remove(reply);
    expectedSha1s.remove(reply);
    pendingDownloads--;
    
    if (!success) {
        qWarning() << "Asset index download failed:" << errorMessage;
        emit downloadFinished(indexPath, false, errorMessage);
    } else {
        emit downloadFinished(indexPath, true, QString());
    }
    
    reply->deleteLater();
}

void DownloadManager::parseAndDownloadAssets(const QString &indexPath) {
    QFile file(indexPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open asset index file:" << indexPath;
        return;
    }

    QJsonParseError error;
    QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &error);
    file.close();

    if (error.error != QJsonParseError::NoError) {
        qWarning() << "Failed to parse asset index JSON:" << error.errorString();
        return;
    }

    QJsonObject indexObj = json.object();
    QJsonObject objects = indexObj["objects"].toObject();
    
    QString basePath = QCoreApplication::applicationDirPath() + "/.minecraft/assets";
    QString objectsPath = basePath + "/objects";
    
    qDebug() << "Starting asset downloads for" << objects.size() << "files";
    
    for (auto it = objects.begin(); it != objects.end(); ++it) {
        QString assetName = it.key();
        QJsonObject assetObj = it.value().toObject();
        QString hash = assetObj["hash"].toString();
        qint64 size = assetObj["size"].toVariant().toLongLong();
        
        if (hash.length() != 40) {
            qWarning() << "Invalid hash for asset" << assetName << ":" << hash;
            continue;
        }
        
        // Create subfolder based on first two characters of hash
        QString subfolder = hash.left(2);
        QString assetPath = objectsPath + "/" + subfolder + "/" + hash;
        
        // Check if file already exists
        if (QFileInfo(assetPath).exists()) {
            qDebug() << "Asset already exists, skipping:" << assetName;
            continue;
        }
        
        // Download the asset
        QString assetUrl = "https://resources.download.minecraft.net/" + subfolder + "/" + hash;
        QDir().mkpath(QFileInfo(assetPath).absolutePath());
        
        downloadFile(assetUrl, assetPath, hash);
        qDebug() << "Downloading asset:" << assetName << "->" << assetPath;
    }
}

void DownloadManager::downloadClientJar(const QString &url, const QString &destinationPath, const QString &expectedSha1) {
    downloadFile(url, destinationPath, expectedSha1);
}

bool DownloadManager::verifyFileSha1(const QString &filePath, const QString &expectedSha1) {
    QString actualSha1 = calculateFileSha1(filePath);
    return actualSha1 == expectedSha1;
}

QString DownloadManager::calculateFileSha1(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    QCryptographicHash hash(QCryptographicHash::Sha1);
    if (hash.addData(&file)) {
        return hash.result().toHex();
    }
    return QString();
}

void DownloadManager::parseVersionManifest(const QJsonDocument &json) {
    availableVersions.clear();
    QJsonObject obj = json.object();
    QJsonArray versions = obj["versions"].toArray();
    for (const QJsonValue &value : versions) {
        QJsonObject vObj = value.toObject();
        MinecraftVersion version;
        version.id = vObj["id"].toString();
        version.type = vObj["type"].toString();
        version.url = vObj["url"].toString();
        version.time = vObj["time"].toString();
        version.releaseTime = vObj["releaseTime"].toString();
        availableVersions.append(version);
    }
}

void DownloadManager::parseVersionJson(const QJsonDocument &json) {
    currentVersionJson = json.object();
}

QList<Library> DownloadManager::extractLibraries(const QJsonObject &versionJson) {
    QList<Library> libraries;
    QJsonArray libs = versionJson["libraries"].toArray();
    for (const QJsonValue &value : libs) {
        QJsonObject libObj = value.toObject();
        Library lib;
        lib.name = libObj["name"].toString();
        QJsonObject downloads = libObj["downloads"].toObject();
        QJsonObject artifact = downloads["artifact"].toObject();
        lib.url = artifact["url"].toString();
        lib.sha1 = artifact["sha1"].toString();
        lib.size = artifact["size"].toVariant().toLongLong();
        lib.path = artifact["path"].toString();
        libraries.append(lib);
    }
    return libraries;
}

AssetIndex DownloadManager::extractAssetIndex(const QJsonObject &versionJson) {
    AssetIndex index;
    QJsonObject assetIndex = versionJson["assetIndex"].toObject();
    index.id = assetIndex["id"].toString();
    index.sha1 = assetIndex["sha1"].toString();
    index.size = assetIndex["size"].toVariant().toLongLong();
    index.url = assetIndex["url"].toString();
    index.totalSize = assetIndex["totalSize"].toVariant().toLongLong();
    return index;
}

void DownloadManager::onVersionManifestReplyFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (reply->error() == QNetworkReply::NoError) {
        QJsonParseError error;
        QJsonDocument json = QJsonDocument::fromJson(reply->readAll(), &error);
        if (error.error == QJsonParseError::NoError) {
            parseVersionManifest(json);
            emit versionManifestFetched(availableVersions);
        } else {
            qDebug() << "JSON parse error:" << error.errorString();
        }
    } else {
        qDebug() << "Network error:" << reply->errorString();
    }
    reply->deleteLater();
}

void DownloadManager::onVersionJsonReplyFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (reply->error() == QNetworkReply::NoError) {
        QJsonParseError error;
        QJsonDocument json = QJsonDocument::fromJson(reply->readAll(), &error);
        if (error.error == QJsonParseError::NoError) {
            parseVersionJson(json);
            emit versionJsonFetched(currentVersionJson);
            
            // Auto-start downloading required files
            QString basePath = QCoreApplication::applicationDirPath() + "/.minecraft";
            
            // Download client JAR
            QJsonObject downloads = currentVersionJson["downloads"].toObject();
            QJsonObject client = downloads["client"].toObject();
            if (!client.isEmpty()) {
                QString clientUrl = client["url"].toString();
                QString clientSha1 = client["sha1"].toString();
                QString versionId = currentVersionJson["id"].toString();
                QString clientPath = basePath + "/versions/" + versionId + "/" + versionId + ".jar";
                qDebug() << "Downloading client JAR..." << clientPath;
                downloadFile(clientUrl, clientPath, clientSha1);
            }
            
            // Download libraries
            QList<Library> libraries = extractLibraries(currentVersionJson);
            qDebug() << "Downloading" << libraries.size() << "libraries...";
            downloadLibraries(libraries, basePath + "/libraries");
            
            // Download asset index
            AssetIndex assetIndex = extractAssetIndex(currentVersionJson);
            qDebug() << "Downloading assets...";
            downloadAssets(assetIndex, basePath + "/assets");
        } else {
            qDebug() << "JSON parse error:" << error.errorString();
            emit versionJsonFetched(QJsonObject());
        }
    } else {
        qDebug() << "Network error:" << reply->errorString();
        emit versionJsonFetched(QJsonObject());
    }
    reply->deleteLater();
}

void DownloadManager::onDownloadReplyFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        qWarning() << "Download finished with invalid reply.";
        return;
    }

    QString destPath = activeDownloads.value(reply);
    if (destPath.isEmpty()) {
        qDebug() << "Ignored finished reply that is not a tracked download.";
        reply->deleteLater();
        return;
    }

    bool success = false;
    QString errorMessage;

    if (reply->error() == QNetworkReply::NoError) {
        QFile file(destPath);
        QDir().mkpath(QFileInfo(destPath).absolutePath());
        if (file.open(QIODevice::WriteOnly)) {
            file.write(reply->readAll());
            file.close();
            QString expectedSha1 = expectedSha1s.value(reply);
            if (!expectedSha1.isEmpty()) {
                if (verifyFileSha1(destPath, expectedSha1)) {
                    success = true;
                } else {
                    errorMessage = "SHA1 verification failed";
                    file.remove();
                }
            } else {
                success = true;
            }
        } else {
            errorMessage = "Failed to open file for writing";
        }
    } else {
        errorMessage = reply->errorString();
    }

    qDebug() << "Download finished:" << destPath << "success=" << success << "error=" << errorMessage;
    emit downloadFinished(QFileInfo(destPath).fileName(), success, errorMessage);
    activeDownloads.remove(reply);
    expectedSha1s.remove(reply);
    reply->deleteLater();

    pendingDownloads--;
    if (activeDownloads.isEmpty() && pendingDownloads <= 0) {
        bool allSuccess = true;
        emit allDownloadsFinished(allSuccess);
    }
}

void DownloadManager::onDownloadReplyProgress(qint64 bytesReceived, qint64 bytesTotal) {
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    QString destPath = activeDownloads.value(reply);
    emit downloadProgress(QFileInfo(destPath).fileName(), bytesReceived, bytesTotal);
}