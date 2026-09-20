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

    // 是否有一批下载正在进行（含"正在校验并跳过已存在文件"的整批）。
    // UI 用它来决定"启动游戏"能不能点。
    bool isDownloading() const;

    // Version JSON access
    QJsonObject getVersionJson(const QString &versionId) const;

signals:
    void versionsFetched(const QStringList &versions);
    void versionDetailsFetched(const QString &versionId, const QJsonObject &versionJson);
    // 进度只走这个信号，UI 把它显示成进度条，不再往日志里写。
    void versionDownloadStats(int done, int total, int failed, const QString &currentFile);
    // 语义是"整批结束"，一次下载只会发一次（改动前是每完成一个文件发一次）。
    // errorMessage 在成功时是给用户看的汇总，失败时是失败文件清单。
    void versionDownloadFinished(const QString &versionId, bool success,
                                 const QString &errorMessage = QString());

private slots:
    void onVersionManifestFetched(const QList<MinecraftVersion> &versions);
    void onVersionJsonFetched(const QJsonObject &versionJson);
    void onAllDownloadsFinished(bool success);

private:
    DownloadManager *downloadManager;
    // versionId -> versionJson。mutable 是因为 getVersionJson() 是 const 的，
    // 但它在回读磁盘后要顺手填充这个缓存。
    mutable QMap<QString, QJsonObject> versionJsons;
    QString m_currentVersionDownloading;
    // 正在取 version.json 的版本号。拿到手的 JSON 里没有 "id" 字段时（网络失败、
    // manifest 里没有这个版本），就靠它告诉 UI"这次失败了"，否则 UI 会一直等下去。
    QString m_pendingVersionId;
    // 版本列表还没拉过时要先拉列表，拿到之后再回来取 version.json。
    // 这里记下待取的版本号，续接动作放在 onVersionManifestFetched 里。
    QString m_fetchJsonAfterManifest;
};
