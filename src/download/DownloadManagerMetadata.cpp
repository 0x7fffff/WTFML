/**
 * @file DownloadManagerMetadata.cpp
 * @brief 元数据这一层：版本清单 / version.json / 资源索引读成内存结构，再变成一串作业。
 *
 * 这里**不碰网络**：报告 "该下什么东西"（库、资源对象、客户端 jar），具体的请求、
 * 重试、限速留给 `DownloadManager.cpp`。搬出去之后，"读清单"与"发请求"各是一个翻译单元，
 * 解析坏掉时不用在一堆回复状态里找。
 */

#include "DownloadManager.hpp"

#include "AssetNames.hpp"
#include "../launcher/LibraryRules.hpp"
#include "../util/AppPaths.hpp"
#include "../util/DownloadDiagnostics.hpp"

#include <QDebug>
#include <QDir>
#include <QFileInfo>

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

        if (!LibraryRules::libraryAllowed(libObj)) {
            continue;
        }

        // NOTE: 1.12.2 里有 2 个库完全没有 artifact（只有 classifiers），
        // 不能拿空 url 和 "dir/" 这样的空 path 去发起下载，否则会产生幽灵请求。
        //
        // 解析（含 maven 风格的库）在 LibraryRules::artifactFor() —— 启动时拼 classpath
        // 用的是同一个函数，两处的语义不会各自漂移。
        //
        // NOTE: 这里用 valid()（要**有下载地址**）是对的，和 classpath 那边的 hasPath() 不是
        // 一回事：OptiFine 那两个库没有地址、盘上本来就有，它们不该出现在下载列表里
        // —— 拿 hasPath() 当判据会造出两条 url 为空的幽灵请求。
        const LibraryRules::Artifact artifact = LibraryRules::artifactFor(libObj);
        if (artifact.valid()) {
            Library lib;
            lib.name = libObj["name"].toString();
            lib.url = artifact.url;
            lib.sha1 = artifact.sha1;
            lib.size = artifact.size;
            lib.path = artifact.path;
            libraries.append(lib);
        }

        // 旧格式（1.18.2 及以下）的 natives：classifier 是独立 jar，需要下载后解压。
        // 1.19+ 没有 natives 字段（classifier 写在 name 里走 artifact 分支），
        // 这里直接返回空。
        const LibraryRules::NativeClassifier classifier = LibraryRules::nativeClassifier(libObj);
        if (classifier.valid()) {
            Library native;
            native.name = libObj["name"].toString() + QLatin1Char(':') +
                          QStringLiteral("natives");
            native.url = classifier.url;
            native.sha1 = classifier.sha1;
            native.path = classifier.path;
            native.isNative = true;
            native.extractExclude = classifier.excludes;
            libraries.append(native);
        }
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

void DownloadManager::downloadLibraries(const QList<Library> &libraries, const QString &basePath) {
    for (const Library &lib : libraries) {
        const QString destPath = basePath + "/" + lib.path;
        if (lib.path.isEmpty()) {
            continue;
        }
        QDir().mkpath(QFileInfo(destPath).absolutePath());
        // 空 url 交给 downloadFile 记一笔失败：这种库缺了游戏必然起不来，
        // 静默跳过会让"下载完成"变成谎话。
        // 这里经 endpoint() 过一道把地址落到当前下载源上（换算规则只写一份）。
        // 第三个参数是那条**官方**地址：镜像上取不回来时按它再试一次。Forge 那几个
        // maven 也认得了（见 mirrorUrl()），认不出来的才会原样返回。
        downloadFile(endpoint(lib.url), destPath, lib.sha1, lib.size, VerifyMode::SizeAndSha1,
                     lib.url);
    }
}

bool DownloadManager::parseAndDownloadAssets(const QString &indexPath, QString *problemOut) {
    if (problemOut != nullptr) {
        problemOut->clear();
    }
    QFile file(indexPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open asset index file:" << indexPath;
        if (problemOut != nullptr) {
            *problemOut = QStringLiteral("Failed to read the asset index file");
        }
        return false;
    }

    QJsonParseError error;
    QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &error);
    file.close();

    if (error.error != QJsonParseError::NoError) {
        qWarning() << "Failed to parse asset index JSON:" << error.errorString();
        if (problemOut != nullptr) {
            *problemOut = QStringLiteral("Asset index is not valid JSON: %1")
                              .arg(error.errorString());
        }
        return false;
    }

    QJsonObject indexObj = json.object();
    QJsonObject objects = indexObj["objects"].toObject();

    // 能解析成 JSON 不等于"这就是资源索引"：镜像/代理偶尔会回一段合法 JSON 的错误体
    // （{"error":...}），少了这一道就会安静地下 0 个对象、整批报成功。
    if (objects.isEmpty()) {
        qWarning() << "Asset index has no objects:" << indexPath;
        if (problemOut != nullptr) {
            *problemOut = QStringLiteral("Asset index contains no objects");
        }
        return false;
    }

    if (DownloadDiag::enabled()) {
        DownloadDiag::line(QStringLiteral("asset-index parsed path=%1 objects=%2")
                               .arg(QDir::toNativeSeparators(indexPath))
                               .arg(objects.size()));
    }

    QString basePath = AppPaths::assetsDir();
    QString objectsPath = basePath + "/objects";

    int scheduled = 0;
    for (auto it = objects.begin(); it != objects.end(); ++it) {
        QString assetName = it.key();
        QJsonObject assetObj = it.value().toObject();
        QString hash = assetObj["hash"].toString();

        // 哈希同时是**文件名**（`objects/<前两位>/<整个哈希>`）：只判长度不够 ——
        // 一串 40 个字符的 `../` 拼接照样能让 `objects/` 底下拼出一条回退路径。
        // 判"40 位十六进制"之后，前两位那一段必然是两位十六进制，跑不出 objects/。
        if (!AssetNames::isSha1Hex(hash)) {
            qWarning() << "Invalid hash for asset" << assetName << ":" << hash;
            continue;
        }

        QString subfolder = hash.left(2);
        QString assetPath = objectsPath + "/" + subfolder + "/" + hash;

        // 官方地址先拼出来：它既是"换成镜像之前的那条地址"，也是镜像取不回来时的备用
        // 地址（这 5000 多个对象里总会有一小撮落在慢节点上，见 m_officialFallbacks）。
        // 换算规则只写一份，就在 mirrorUrl() 里 —— 两边的路径形状不一样（镜像多了
        // /assets 前缀）。
        const QString officialAssetUrl =
            QStringLiteral("https://resources.download.minecraft.net/") + subfolder +
            QStringLiteral("/") + hash;
        const QString assetUrl = endpoint(officialAssetUrl);
        QDir().mkpath(QFileInfo(assetPath).absolutePath());
        // 文件名就是 SHA1，所以只按存在性判断：assets 目录已经 3 GB 出头，
        // 每次点下载都把 5000 多个对象重新哈希一遍会让界面卡死好几秒。
        scheduleFile(assetUrl, assetPath, hash, -1, VerifyMode::ExistsOnly, officialAssetUrl);
        ++scheduled;
    }
    // 结构对、但一个对象都没排上（每条 hash 都不是 40 位）—— 和上面"没有 objects"同一种
    // 故障，只是藏得更深。放它过去的话整批照样以 0 个资源报成功。
    if (scheduled == 0) {
        qWarning() << "Asset index has no usable objects:" << indexPath;
        if (problemOut != nullptr) {
            *problemOut = QStringLiteral("Asset index contains no usable objects");
        }
        return false;
    }
    return true;
}

void DownloadManager::downloadClientJar(const QString &versionId, const QString &url,
                                        const QString &destinationPath, const QString &expectedSha1,
                                        qint64 expectedSize) {
    const QString effectiveUrl = mirrorPreferred() ? mirrorClientJarUrl(versionId) : url;
    // 第三个参数是官方那条地址（镜像那条是**按版本号拼**出来的，形状完全不一样，
    // 没法由它反推回去）。几十 MB 的 jar 要是取到一半超时，退一步比整批重来划算。
    const QString alternate = !m_useMirror && !expectedSha1.isEmpty() ? mirrorClientJarUrl(versionId) : url;
    downloadFile(effectiveUrl, destinationPath, expectedSha1, expectedSize,
                 VerifyMode::SizeAndSha1, alternate);
}
