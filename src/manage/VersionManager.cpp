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
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStandardPaths>

VersionManager::VersionManager(QObject *parent)
    : QObject(parent), downloadManager(new DownloadManager(this)) {
    connect(downloadManager, &DownloadManager::versionManifestFetched, this,
            &VersionManager::onVersionManifestFetched);
    connect(downloadManager, &DownloadManager::versionJsonFetched, this,
            &VersionManager::onVersionJsonFetched);
    // NOTE: 这里以前还接了一个 downloadFinished -> onDownloadFinished，把每个文件的
    // 完成都转发成 versionDownloadFinished，于是下载一个版本会在日志里刷出几千行
    // 一模一样的话。现在只认整批结束的信号。
    connect(downloadManager, &DownloadManager::allDownloadsFinished, this,
            &VersionManager::onAllDownloadsFinished);
    connect(downloadManager, &DownloadManager::batchProgress, this,
            [this](int done, int total, int failed, const QString &currentFile) {
                emit versionDownloadStats(done, total, failed, currentFile);
            });
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
            // 判据是客户端 jar 在不在：<id>/<id>.jar 齐了才算装好。光有个版本目录
            // （下载中途失败留下的）不算。
            QString jarPath = versionsPath + "/" + versionDir + "/" + versionDir + ".jar";
            if (QFile::exists(jarPath)) {
                versions.append(versionDir);
            }
        }
    }

    // NOTE: 这里以前在没有任何已安装版本时会塞一个 "自定义版本" 进去，好让旧版
    // QListWidget 不至于空着。那是界面层的占位职责，混进数据层之后调用方还得反过来
    // 判 versionId == "自定义版本" 才能把它拒掉（MainWindow 里写了两处）。
    // 现在空就是空：界面用下拉框的 placeholderText 表达"没有可选版本"。
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
    m_pendingVersionId = versionId;
    if (downloadManager->getAvailableVersions().isEmpty()) {
        // 列表还没拉过：先拉列表，续接在 onVersionManifestFetched 里做。
        // NOTE: 这里以前是临时 connect 一个 lambda、在里面 disconnect(conn)，
        // 而 conn 是局部变量、被按引用捕获 —— 信号在函数返回之后才发，那句
        // disconnect 读的是已经失效的栈内存，实测直接段错误。改成记在成员里。
        m_fetchJsonAfterManifest = versionId;
        downloadManager->fetchVersionManifest();
    } else {
        downloadManager->fetchVersionJson(versionId);
    }
}

bool VersionManager::isDownloading() const { return downloadManager->isBusy(); }

void VersionManager::downloadVersion(const QString &versionId, const QString &destinationPath) {
    // NOTE: 这两道早退必须在 beginBatch() 之前。beginBatch() 与 endBatch() 之间
    // 不允许有任何提前 return —— 一旦漏掉 endBatch()，m_batchOpen 就永远是 true，
    // allDownloadsFinished 永不发射，UI 会一直停在"下载中"、按钮永久变灰。
    if (!versionJsons.contains(versionId)) {
        qWarning() << "Version JSON not available for" << versionId << ". Fetch details first.";
        // 也必须给 UI 一个终态，否则它会一直等这批下载结束。
        emit versionDownloadFinished(versionId, false, tr("尚未获取到该版本的 version.json。"));
        return;
    }
    if (downloadManager->isBusy()) {
        qWarning() << "A download batch is already running, refusing to start another.";
        emit versionDownloadFinished(versionId, false, tr("已有下载任务正在进行，请等它结束。"));
        return;
    }

    m_currentVersionDownloading = versionId;
    downloadManager->beginBatch();

    const QJsonObject &versionJson = versionJsons[versionId];
    const QString basePath = QCoreApplication::applicationDirPath() + "/.minecraft";

    // 把 version.json 落盘。缺了它，下一次启动时 getVersionJson() 取不到
    // javaVersion / assetIndex，会静默退回错误的 Java 版本和资产索引 ——
    // 26.1 需要 Java 25，退成 Java 21 就是 UnsupportedClassVersionError。
    if (QDir().mkpath(destinationPath)) {
        const QString jsonPath = destinationPath + "/" + versionId + ".json";
        QFile jsonFile(jsonPath);
        if (jsonFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            jsonFile.write(QJsonDocument(versionJson).toJson(QJsonDocument::Indented));
            jsonFile.close();
            qDebug() << "Saved version JSON to" << jsonPath;
        } else {
            qWarning() << "Failed to save version JSON to" << jsonPath
                       << jsonFile.errorString();
        }
    }

    // Download client jar
    if (versionJson.contains("downloads") && versionJson["downloads"].isObject()) {
        const QJsonObject downloads = versionJson["downloads"].toObject();
        if (downloads.contains("client") && downloads["client"].isObject()) {
            const QJsonObject client = downloads["client"].toObject();
            const QString url = client["url"].toString();
            const QString sha1 = client["sha1"].toString();
            const qint64 size = client["size"].toVariant().toLongLong();

            const QString jarPath = destinationPath + "/" + versionId + ".jar";
            // 带上 size：重新点下载时先比字节数，对得上才算"已经下过"，
            // 省掉一次 41 MB 的全文件哈希。
            downloadManager->downloadClientJar(url, jarPath, sha1, size);
        }
    }

    // Download libraries
    QList<Library> libraries = downloadManager->extractLibraries(versionJson);
    downloadManager->downloadLibraries(libraries, basePath + "/libraries");

    // Download assets
    AssetIndex assetIndex = downloadManager->extractAssetIndex(versionJson);
    downloadManager->downloadAssets(assetIndex, basePath + "/assets");

    downloadManager->endBatch();
}

void VersionManager::downloadVersionLibraries(const QString &versionId, const QString &basePath) {
    // 同 downloadVersion：早退只能在 beginBatch() 之前
    if (!versionJsons.contains(versionId)) {
        qWarning() << "Version JSON not available for" << versionId << ". Fetch details first.";
        emit versionDownloadFinished(versionId, false, tr("尚未获取到该版本的 version.json。"));
        return;
    }

    m_currentVersionDownloading = versionId;
    downloadManager->beginBatch();
    const QJsonObject &versionJson = versionJsons[versionId];
    QList<Library> libraries = downloadManager->extractLibraries(versionJson);
    downloadManager->downloadLibraries(libraries, basePath);
    downloadManager->endBatch();
}

void VersionManager::downloadVersionAssets(const QString &versionId, const QString &basePath) {
    if (!versionJsons.contains(versionId)) {
        qWarning() << "Version JSON not available for" << versionId << ". Fetch details first.";
        emit versionDownloadFinished(versionId, false, tr("尚未获取到该版本的 version.json。"));
        return;
    }

    m_currentVersionDownloading = versionId;
    downloadManager->beginBatch();
    const QJsonObject &versionJson = versionJsons[versionId];
    AssetIndex assetIndex = downloadManager->extractAssetIndex(versionJson);
    downloadManager->downloadAssets(assetIndex, basePath);
    downloadManager->endBatch();
}

QJsonObject VersionManager::getVersionJson(const QString &versionId) const {
    const auto cached = versionJsons.constFind(versionId);
    if (cached != versionJsons.constEnd()) {
        return cached.value();
    }

    // 内存里没有就回读磁盘。下载时已经把 JSON 落盘，所以跨会话启动也能拿到
    // javaVersion 和 assetIndex，不需要先在同一次会话里点一次"下载"。
    const QString jsonPath = QCoreApplication::applicationDirPath() +
                             QStringLiteral("/.minecraft/versions/%1/%1.json").arg(versionId);
    QFile jsonFile(jsonPath);
    if (!jsonFile.exists() || !jsonFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Version JSON not found on disk:" << jsonPath;
        return QJsonObject();
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonFile.readAll(), &parseError);
    jsonFile.close();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "Failed to parse" << jsonPath << ":" << parseError.errorString();
        return QJsonObject();
    }

    // 填回缓存，同一次会话里不再重复读盘
    versionJsons[versionId] = doc.object();
    return doc.object();
}

VersionDisplayParts VersionManager::displayParts(const QString &versionId) const {
    // id 永远用参数填，不从文件里取：元数据文件是可以被用户手改的，而 id 是真正
    // 进 --version 和文件路径的那个值，必须由调用方（本地扫描 / 在线清单）决定。
    VersionDisplayParts parts;
    parts.id = versionId;

    const QString metaPath = QCoreApplication::applicationDirPath() +
                             QStringLiteral("/.minecraft/versions/%1/wtfml.meta").arg(versionId);
    QFile metaFile(metaPath);
    if (!metaFile.exists()) {
        // 绝大多数版本没有这个文件，不是异常，也别打日志——refreshInstalledVersions()
        // 会对着每个已安装版本调一次，打日志就是刷屏。
        return parts;
    }
    if (!metaFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Version metadata exists but cannot be opened:" << metaPath;
        return parts;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll(), &parseError);
    metaFile.close();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        // 手写的文件写错格式很常见。整个文件当没有，但不静默——这是用户手写的，
        // 不告诉他写错了他会一直以为生效了。
        qWarning() << "Failed to parse version metadata" << metaPath << ":" << parseError.errorString();
        return parts;
    }

    const QJsonObject meta = doc.object();

    // 逐字段读。类型不对时给一句告警：QJsonValue::toString() 对数字/对象会安静地
    // 返回空串，手写 JSON 时把 "name" 写成数字是最容易犯的错，不提示的话表现成
    // "明明写了却不生效"。
    const auto readString = [&meta, &metaPath](const char *key) -> QString {
        const QJsonValue value = meta.value(QLatin1String(key));
        if (value.isUndefined() || value.isNull()) {
            return QString();
        }
        if (!value.isString()) {
            qWarning() << "Version metadata field" << key << "is not a string in" << metaPath
                       << "— ignored";
            return QString();
        }
        return value.toString();
    };

    parts.customName = readString("name");
    parts.loaderName = readString("loaderName");
    parts.loaderVersion = readString("loaderVersion");

    // 预留：按实例覆盖全局的显示格式。现在没有任何代码会写它，但读的这一侧先接好，
    // 以后做"分开实例管理"时只要往 wtfml.meta 里写 "nameFormat" 就能生效。
    //
    // 语义是**整份替换**：没写的字段取 VersionNameOptions 的 struct 默认值，而不是取
    // 全局设置里的值。理由是这个类在数据层，看不见 LauncherSettings，为了一个还没人写的
    // 字段去建立这条依赖不划算；而且全局那四个开关今天恒等于默认值，两条语义在当前
    // 完全等价。TODO: 以后真做"逐字段覆盖全局"时，得把全局 options 作为参数传进来。
    // 四个开关全缺省时不构造 override，免得一个空对象把全局设置顶掉。
    const QJsonValue nameFormat = meta.value(QLatin1String("nameFormat"));
    if (nameFormat.isObject()) {
        const QJsonObject formatObj = nameFormat.toObject();
        VersionNameOptions overrideOptions;
        bool anySet = false;
        const auto readFlag = [&formatObj, &anySet](const char *key, bool &target) {
            const QJsonValue value = formatObj.value(QLatin1String(key));
            if (value.isBool()) {
                target = value.toBool();
                anySet = true;
            }
        };
        readFlag("showCustomName", overrideOptions.showCustomName);
        readFlag("showRealVersion", overrideOptions.showRealVersion);
        readFlag("showLoaderName", overrideOptions.showLoaderName);
        readFlag("showLoaderVersion", overrideOptions.showLoaderVersion);
        if (anySet) {
            parts.nameFormatOverride = overrideOptions;
        }
    } else if (!nameFormat.isUndefined() && !nameFormat.isNull()) {
        qWarning() << "Version metadata field nameFormat is not an object in" << metaPath
                   << "— ignored";
    }

    // NOTE: 这里刻意**不缓存**。getVersionJson() 能缓存是因为版本 JSON 一旦下载就不再变，
    // 而这个文件将来会被"重命名"功能改写——缓存就会变成"改了名不生效"的 bug。
    // N 只有个位数，而 refreshInstalledVersions() 一次会话最多被调三次。
    return parts;
}

void VersionManager::onVersionManifestFetched(const QList<MinecraftVersion> &versions) {
    QStringList versionIds;
    for (const MinecraftVersion &version : versions) {
        versionIds.append(version.id);
    }
    emit versionsFetched(versionIds);

    if (m_fetchJsonAfterManifest.isEmpty()) {
        return;
    }
    const QString pending = m_fetchJsonAfterManifest;
    m_fetchJsonAfterManifest.clear();
    // 列表里没有这个版本（或者列表压根没拉到）时，fetchVersionJson 会立刻回一个
    // 空 JSON，由 onVersionJsonFetched 转成终态信号 —— 请求方总能拿到结果。
    downloadManager->fetchVersionJson(pending);
}

void VersionManager::onVersionJsonFetched(const QJsonObject &versionJson) {
    if (versionJson.contains("id")) {
        QString versionId = versionJson["id"].toString();
        versionJsons[versionId] = versionJson;
        m_pendingVersionId.clear();
        emit versionDetailsFetched(versionId, versionJson);
        return;
    }

    // 拿不到版本 JSON（manifest 里没有这个版本，或者网络失败）：这里必须回一个终态。
    // 静默丢弃会让 UI 一直停在"正在获取版本信息"，而按钮已经被禁用 ——
    // 那就成了死界面。
    if (m_pendingVersionId.isEmpty()) {
        qDebug() << "Ignored a version JSON reply that nobody asked for.";
        return;
    }
    const QString pending = m_pendingVersionId;
    m_pendingVersionId.clear();
    qWarning() << "Failed to fetch version JSON for" << pending;
    emit versionDetailsFetched(pending, QJsonObject());
}

void VersionManager::onAllDownloadsFinished(bool success) {
    const DownloadStats stats = downloadManager->stats();

    QString message;
    if (success) {
        message = stats.skipped > 0
                      ? tr("%1 个文件已全部就绪（其中 %2 个本来就存在，已跳过）。")
                            .arg(stats.total)
                            .arg(stats.skipped)
                      : tr("%1 个文件已全部就绪。").arg(stats.total);
    } else {
        // 失败清单只列前 10 项，剩下的折成一行 —— 5000 个 asset 全列出来没人看。
        const QStringList shown = stats.failedPaths.mid(0, 10);
        QString list = shown.join(QLatin1Char('\n'));
        if (stats.failedPaths.size() > shown.size()) {
            list += QLatin1Char('\n') +
                    tr("……还有 %1 个").arg(stats.failedPaths.size() - shown.size());
        }
        message = tr("%1 个文件下载失败：").arg(stats.failed) + QLatin1Char('\n') + list;
    }

    // 一批只发一次。改动前这里由每个文件的 downloadFinished 触发，
    // 下载一个版本会在日志里刷出几千行一模一样的话。
    emit versionDownloadFinished(m_currentVersionDownloading, success, message);
}
