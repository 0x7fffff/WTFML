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
#include "VersionDisplay.hpp"

class VersionManager : public QObject {
    Q_OBJECT

public:
    explicit VersionManager(QObject *parent = nullptr);
    ~VersionManager();

    // Version management
    // 扫 .minecraft/versions，返回磁盘上真正装好了的版本号（以 <id>.jar 存在为准）。
    // 一个都没有时返回空列表，不再塞 "自定义版本" 占位项。
    //
    // warnings 是可选的收集器：当某个目录里**有 jar、但 jar 名和目录名对不上**时，
    // 往里追加一句说明。这种实例在真机上真实存在（用户手动改过目录名），判据不放松，
    // 但用户得知道为什么它没出现在列表里。传 nullptr 时行为与以前完全一致。
    QStringList installedVersions(QStringList *warnings = nullptr) const;
    QStringList availableVersions() const;

    // Version fetching from Mojang API
    void fetchAvailableVersions();
    void fetchVersionDetails(const QString &versionId);

    // Version downloading
    void downloadVersion(const QString &versionId, const QString &destinationPath);

    // 是否有一批下载正在进行（含"正在校验并跳过已存在文件"的整批）。
    // UI 用它来决定"启动游戏"能不能点。
    bool isDownloading() const;

    // 下载源：官方 / 镜像。转发给内部的 DownloadManager，界面只跟这一层打交道。
    // 改完立刻对**之后发出的**请求生效：库和客户端 jar 的地址在批次开头一次算完，那部分
    // 不会改道；但 5000+ 个 asset 的地址是解析完 asset index 才拼的（而索引回复要等网络），
    // 所以下载中途切源会让资源那一半整体换源。别以为这条路走不通 —— 设置页那个下拉框
    // 在下载期间并没有被禁用。
    void setUseMirror(bool useMirror);

    // Version JSON access
    QJsonObject getVersionJson(const QString &versionId) const;

    // 读某个版本参与"显示名"的那些部分（自定义名、模组加载器名/版本）。
    // 数据源是 <id>/wtfml.meta 这个可选文件，读不到就全是空串、显示名退化成纯版本号。
    // `id` 永远用参数填，不从文件里取。
    //
    // NOTE: 文件名是 wtfml.meta 而不是 wtfml.json，这是必须的——<id>.json 是版本 JSON
    // 的既定名字，万一真去下载一个 id 就叫 "wtfml" 的版本，versions/wtfml/wtfml.json
    // 会同时是版本 JSON 和元数据文件。后果不是崩溃，是更糟的静默错：getVersionJson()
    // 读到一份没有 "id" 字段的对象**仍然返回它**，调用方只判 isEmpty() 就会一路绿灯
    // 走到兜底的 assetIndex/javaMajorVersion，也就是"静默选错 Java"那个老 bug。
    // 换个扩展名，这个撞名在结构上就不可能发生。
    VersionDisplayParts displayParts(const QString &versionId) const;

    // 读某个版本自定义的 JVM 参数（wtfml.meta 里可选的 "jvmArgs" 字符串数组）。
    // 读不到就是空列表，调用方不必区分"文件没有"和"字段没有"。
    //
    // 用途：PCL 允许用户自定义 Java 启动参数，这里预留"按版本"的那半边 ——
    // "全局"那半边是 GlobalJavaSettings::extraJvmArgs()，早就生效了。
    // NOTE: 参数本身可以做任何事（包括把 JVM 起不来），不做白名单校验 —— 这是用户
    //       自己写的文件、自己指定的版本，和直接改命令行是一回事。
    QStringList versionJvmArgs(const QString &versionId) const;

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
    // 读 <id>/wtfml.meta 并解析成对象。文件不存在、打不开、格式错都返回空对象
    // （格式错会告警）。displayParts() 和 versionJvmArgs() 共用这一份读盘逻辑，
    // 免得把"什么算坏文件、坏了要不要吭声"的判断抄两遍、慢慢抄歪。
    // metaPathOut 非空时回填路径，供调用方在告警里指出是哪个文件。
    QJsonObject readVersionMeta(const QString &versionId, QString *metaPathOut = nullptr) const;

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
