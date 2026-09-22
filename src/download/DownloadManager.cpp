/**
 * @file DownloadManager.cpp
 * @brief 为启动器执行版本文件和 asset 下载逻辑。
 * @author LoserKugua
 * @date 2026-04-10
 * @note 进行网络文件下载并将文件存储至 .minecraft 文件夹。
 *
 * 从mojang服务器获取元数据，处理网络回应，将版本数据和资产存储到本地 .minecraft 路径。
 */

#include "DownloadManager.hpp"

#include <QDebug>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonParseError>
#include <QStandardPaths>
#include <QTemporaryFile>

#include "../launcher/LibraryRules.hpp"
#include "../util/AppPaths.hpp"

namespace {

/// Mojang 官方的版本清单地址。
constexpr char kOfficialVersionManifest[] =
    "https://launchermeta.mojang.com/mc/game/version_manifest.json";

/// 镜像源（BMCLAPI，第三方服务）的基址。
constexpr char kMirrorBase[] = "https://bmclapi2.bangbang93.com";

}  // namespace

QString DownloadManager::mirrorUrl(const QString &officialUrl) {
    const QUrl url(officialUrl);
    const QString host = url.host();
    const QString path = url.path();

    // 版本清单：BMCLAPI 的路径形状和官方**完全一样**，换 host 即可。
    if (host == QLatin1String("launchermeta.mojang.com")) {
        return QLatin1String(kMirrorBase) + path;
    }
    // 资源对象：官方 /ab/abcdef...（前两位是子目录名）→ 镜像 /assets/ab/abcdef...
    if (host == QLatin1String("resources.download.minecraft.net")) {
        return QLatin1String(kMirrorBase) + QStringLiteral("/assets") + path;
    }
    // 库文件：官方就是标准 maven 布局 /com/foo/bar/1.0/bar-1.0.jar → 镜像 /maven/...
    if (host == QLatin1String("libraries.minecraft.net")) {
        return QLatin1String(kMirrorBase) + QStringLiteral("/maven") + path;
    }
    // 认不出来就原样返回（理由见头文件）。
    return officialUrl;
}

QString DownloadManager::mirrorVersionJsonUrl(const QString &versionId) {
    return QLatin1String(kMirrorBase) + QStringLiteral("/version/") + versionId +
           QStringLiteral("/json");
}

QString DownloadManager::mirrorClientJarUrl(const QString &versionId) {
    // NOTE: 官方地址里的那个 sha1 在这里用不上 —— 镜像不按 sha1 存，按版本号。
    //       文件本身的校验没丢：downloadFile() 照样拿 version.json 里的 sha1 比。
    return QLatin1String(kMirrorBase) + QStringLiteral("/version/") + versionId +
           QStringLiteral("/client");
}

QString DownloadManager::mirrorAssetIndexUrl(const QString &assetIndexId) {
    return QLatin1String(kMirrorBase) + QStringLiteral("/indexes/") + assetIndexId +
           QStringLiteral(".json");
}

QString DownloadManager::officialVersionManifestUrl() {
    return QString::fromLatin1(kOfficialVersionManifest);
}

QString DownloadManager::endpoint(const QString &officialUrl) const {
    return m_useMirror ? mirrorUrl(officialUrl) : officialUrl;
}

namespace {
// 同时最多发出去的资源对象数。取 6 = Qt 每个主机的连接上限，是为了让"排队"这件事
// 只发生在**我们**手里：发出去的请求都立刻拿得到连接，30 秒的传输超时才有意义。
// 调大只会把请求重新塞进 Qt 内部那个会超时的队列，调小则白白浪费带宽。
// 实测（本机网络，1.18.2 的 3071 个对象）：不限流时 2545 个报 Operation timed out；
// 限流后每个对象约 2.5 秒，全程约 20 分钟且不再超时。
constexpr int kMaxConcurrentAssetDownloads = 6;
}  // namespace

DownloadManager::DownloadManager(QObject *parent)
    : QObject(parent), networkManager(new QNetworkAccessManager(this)), pendingDownloads(0) {
    // 为信号注册元类型
    qRegisterMetaType<MinecraftVersion>("MinecraftVersion");
    qRegisterMetaType<Library>("Library");
    qRegisterMetaType<AssetIndex>("AssetIndex");
    qRegisterMetaType<QList<MinecraftVersion>>("QList<MinecraftVersion>");
}

DownloadManager::~DownloadManager() {
    // abort() 有可能同步触发 finished，处理函数会走完一套收尾甚至发信号。
    // 对象正在析构，那些回调不该再跑，用这个标志在 checkAllDownloadsFinished
    // 和槽函数入口处挡掉。
    m_shuttingDown = true;
    // 清除当前的下载
    for (auto reply : activeDownloads.keys()) {
        reply->abort();
        reply->deleteLater();
    }
    activeDownloads.clear();
    // 队列里的作业一个请求都没发出去，abort 那套对它们无效，只能直接丢掉。
    m_pendingAssetJobs.clear();
}

void DownloadManager::fetchVersionManifest() {
    QUrl url(endpoint(officialVersionManifestUrl()));
    QNetworkRequest request(url);
    // 元数据请求也要有超时：下载期间 UI 会禁用按钮，卡死的连接会让它永远禁用下去。
    request.setTransferTimeout(30000);
    QNetworkReply *reply = networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this,
            &DownloadManager::onVersionManifestReplyFinished);
}

QList<MinecraftVersion> DownloadManager::getAvailableVersions() const { return availableVersions; }

void DownloadManager::fetchVersionJson(const QString &versionId) {
    for (const MinecraftVersion &version : availableVersions) {
        if (version.id == versionId) {
            // 镜像源下要拿版本号重拼（官方那条 URL 里的 sha1 镜像不认），所以这里
            // 用不上 version.url。
            const QString effectiveUrl =
                m_useMirror ? mirrorVersionJsonUrl(versionId) : version.url;
            // 具名 QUrl 只是和下面几处保持一致的写法，**不是**为了躲"最令人烦恼的解析"：
            // 这一行本身编译无警告（实测），`QUrl url(x);` 是对象。会歧义的是
            // `QNetworkRequest request(QUrl(x))` 那种"参数写成了类型名(值)"的形状，
            // 详细说明在 onAssetIndexDownloaded() 上面。
            const QUrl versionQUrl(effectiveUrl);
            QNetworkRequest request(versionQUrl);
            request.setTransferTimeout(30000);
            QNetworkReply *reply = networkManager->get(request);
            connect(reply, &QNetworkReply::finished, this,
                    &DownloadManager::onVersionJsonReplyFinished);
            return;
        }
    }
    emit versionJsonFetched(QJsonObject());
}

QJsonObject DownloadManager::getVersionJson() const { return currentVersionJson; }

void DownloadManager::beginBatch() {
    // 上一批还没收尾就又被开始了，先替它收尾，否则它的完成信号会丢。
    if (m_batchOpen) {
        qWarning() << "beginBatch() called while a batch was still open, closing it first.";
        endBatch();
    }
    m_totalJobs = 0;
    m_doneJobs = 0;
    m_failedJobs = 0;
    m_skippedJobs = 0;
    m_failedPaths.clear();
    m_currentFile.clear();
    m_batchEmitted = false;
    m_batchOpen = true;
}

void DownloadManager::endBatch() {
    m_batchOpen = false;
    // 这一个调用是关键：所有文件都已存在于磁盘、一个网络作业都没发出去的批次
    // 也会在这里被正常收尾，不会卡住。
    checkAllDownloadsFinished();
}

void DownloadManager::abandonBatch() {
    m_batchOpen = false;
    // 置真 = "这一批的完成信号已经发过了"：checkAllDownloadsFinished() 之后一个字都不会发
    // （那几个交完货才回来的回复会走到那里，然后立刻返回）。
    m_batchEmitted = true;
    m_currentFile.clear();
}

bool DownloadManager::isBusy() const { return m_batchOpen || pendingDownloads > 0; }

DownloadStats DownloadManager::stats() const {
    DownloadStats s;
    s.total = m_totalJobs;
    s.done = m_doneJobs;
    s.failed = m_failedJobs;
    s.skipped = m_skippedJobs;
    s.failedPaths = m_failedPaths;
    return s;
}

bool DownloadManager::fileAlreadyValid(const QString &path, const QString &expectedSha1,
                                       qint64 expectedSize, VerifyMode mode) {
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        return false;
    }
    if (mode == VerifyMode::ExistsOnly) {
        return true;
    }
    // SizeAndSha1：先比字节数。大小对不上一眼就知道要重下，省掉一次全文件哈希。
    // expectedSize <= 0 表示调用方也不知道该多大，只能退化成只比 SHA1。
    if (expectedSize > 0 && info.size() != expectedSize) {
        return false;
    }
    if (expectedSha1.isEmpty()) {
        return true;
    }
    return calculateFileSha1(path) == expectedSha1;
}

void DownloadManager::downloadFile(const QString &url, const QString &destinationPath,
                                   const QString &expectedSha1, qint64 expectedSize,
                                   VerifyMode mode) {
    // 一单一发：库、jar、索引这些批次都只有几十个作业，没有排队的必要。
    scheduleFile(url, destinationPath, expectedSha1, expectedSize, mode, false);
}

void DownloadManager::scheduleFile(const QString &url, const QString &destinationPath,
                                   const QString &expectedSha1, qint64 expectedSize,
                                   VerifyMode mode, bool throttled) {
    // 每个作业都重置一次"已发射"，不管是跳过还是真下。不带批次的用法（一次 fetch、
    // 一次 assets）没有 beginBatch 来重置它，只能靠这里。
    m_batchEmitted = false;
    m_totalJobs++;

    const QString fileName = QFileInfo(destinationPath).fileName();
    if (url.isEmpty() || destinationPath.isEmpty()) {
        qWarning() << "Refused download with empty url or destination:" << url << destinationPath;
        m_doneJobs++;
        m_failedJobs++;
        if (!destinationPath.isEmpty() && !m_failedPaths.contains(destinationPath)) {
            m_failedPaths << destinationPath;
        }
        emit downloadFinished(fileName, false, QStringLiteral("empty url or destination"));
        emitBatchProgress(destinationPath);
        checkAllDownloadsFinished();
        return;
    }

    if (fileAlreadyValid(destinationPath, expectedSha1, expectedSize, mode)) {
        // 跳过也要计入 done/total，否则进度条的分母会对不上，最后一批还会少发一次信号。
        m_doneJobs++;
        m_skippedJobs++;
        emit downloadFinished(fileName, true, QString());
        emitBatchProgress(destinationPath);
        checkAllDownloadsFinished();
        return;
    }

    if (throttled) {
        m_pendingAssetJobs.enqueue(AssetJob{url, destinationPath, expectedSha1});
        pumpAssetQueue();
    } else {
        startDownload(QUrl(url), destinationPath, expectedSha1);
    }
}

void DownloadManager::pumpAssetQueue() {
    // 判据用 activeDownloads.size() 而不是另开一个计数器：它是"当下真在飞的作业"
    // 的唯一事实，别的下载（客户端 jar 之类）占着连接时这里自然少发几个。
    while (!m_pendingAssetJobs.isEmpty()
           && activeDownloads.size() < kMaxConcurrentAssetDownloads) {
        const AssetJob job = m_pendingAssetJobs.dequeue();
        startDownload(QUrl(job.url), job.destinationPath, job.expectedSha1);
    }
}

void DownloadManager::startDownload(const QUrl &url, const QString &destinationPath,
                                    const QString &expectedSha1) {
    QNetworkRequest request(url);
    // 没有超时的话，一个卡死的连接会让 pendingDownloads 永远归不了零。UI 在"下载中"
    // 会禁用按钮，那就从"日志刷屏"变成"永久卡死"，比原来的问题更糟。
    request.setTransferTimeout(30000);
    QNetworkReply *reply = networkManager->get(request);
    activeDownloads[reply] = destinationPath;
    if (!expectedSha1.isEmpty()) {
        expectedSha1s[reply] = expectedSha1;
    }
    pendingDownloads++;
    connect(reply, &QNetworkReply::finished, this, &DownloadManager::onDownloadReplyFinished);
    connect(reply, &QNetworkReply::downloadProgress, this,
            &DownloadManager::onDownloadReplyProgress);
}

void DownloadManager::handleDownloadError(QNetworkReply *reply, const QString &errorMessage) {
    const QString destPath = activeDownloads.value(reply);
    qWarning() << "Download failed:" << destPath << errorMessage;
    m_failedJobs++;
    // 记完整路径而不是 QFileInfo::fileName()：批量里有 5000 多个 40 位哈希文件名，
    // 只说哈希对排查毫无帮助。
    if (!destPath.isEmpty() && !m_failedPaths.contains(destPath)) {
        m_failedPaths << destPath;
    }
    emit downloadFinished(QFileInfo(destPath).fileName(), false, errorMessage);
}

void DownloadManager::checkAllDownloadsFinished() {
    // m_batchOpen：批次还没封口，等 endBatch() 再判定。
    // m_batchEmitted：这一批已经报过了，别再报。
    // pendingDownloads > 0：还有作业在飞。
    // m_pendingAssetJobs：还排着队的资源对象也是"没下完"，漏掉这一条会在还差几千个
    // 文件的时候发出 allDownloadsFinished。
    if (m_shuttingDown || m_batchOpen || m_batchEmitted || pendingDownloads > 0
        || !m_pendingAssetJobs.isEmpty()) {
        return;
    }
    m_batchEmitted = true;
    emit allDownloadsFinished(m_failedJobs == 0);
}

void DownloadManager::emitBatchProgress(const QString &currentFile) {
    if (!currentFile.isEmpty()) {
        m_currentFile = currentFile;
    }
    emit batchProgress(m_doneJobs, m_totalJobs, m_failedJobs, m_currentFile);
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
        // 这里经 endpoint() 过一道：Mojang 自家 libraries.minecraft.net 上的库走镜像，
        // 别的 maven（Forge/Fabric）不在 BMCLAPI 的代理范围里，endpoint() 会原样返回。
        downloadFile(endpoint(lib.url), destPath, lib.sha1, lib.size, VerifyMode::SizeAndSha1);
    }
}

void DownloadManager::downloadAssets(const AssetIndex &assetIndex) {
    if (assetIndex.url.isEmpty() || assetIndex.id.isEmpty()) {
        qWarning() << "Asset index is missing url or id, nothing to download.";
        return;
    }

    // 第一步，下载资产索引 JSON 文件。
    // 目录**不接参数**：索引和下面 parseAndDownloadAssets() 写 objects 用的必须是同一个
    // 根（AppPaths::assetsDir()，也就是 --assetsDir 指的地方），否则游戏去
    // <assetsDir>/indexes/<id>.json 找索引会扑空 —— 之前这里收了一个 basePath 参数，
    // 而 objects 那边一直是另算的，等于把"资源在哪儿"这个知识存了两份。
    const QString indexPath = AppPaths::assetsDir() + "/indexes/" + assetIndex.id + ".json";
    QDir().mkpath(QFileInfo(indexPath).absolutePath());

    m_batchEmitted = false;
    m_totalJobs++;
    m_currentFile = indexPath;

    // NOTE: 索引文件必须无条件重取（走不了"已存在就跳过"）。Mojang 修订索引后
    // SHA1 和 objects 都会变，只按存在性判断会永远沿用旧索引，新资源一个都下不到。
    //
    // 镜像源下按索引号重拼。这一条映射是整套镜像里最没把握的一个（/indexes/<id>.json），
    // 但拼错了会"响亮地失败"而不是静默出错数据：下面的 expectedSha1s 拿官方 sha1 校验，
    // 内容不对就是一条 SHA1 mismatch 的失败清单。
    const QString indexUrl = m_useMirror ? mirrorAssetIndexUrl(assetIndex.id) : assetIndex.url;
    // NOTE: 这几个 URL 先落到具名的 QUrl 变量上，不写 `QNetworkRequest request(QUrl(x))`：
    //       那一行会被编译器当成函数声明（最令人烦恼的解析）—— 它读成"形参名叫 x 的
    //       QUrl 类型参数"，于是 request 成了函数名。原来那句能编过只是因为括号里
    //       是 `assetIndex.url` 这种带点的表达式，没法当形参名。
    const QUrl indexQUrl(indexUrl);
    QNetworkRequest request(indexQUrl);
    request.setTransferTimeout(30000);
    QNetworkReply *indexReply = networkManager->get(request);
    activeDownloads[indexReply] = indexPath;
    if (!assetIndex.sha1.isEmpty()) {
        expectedSha1s[indexReply] = assetIndex.sha1;
    }
    pendingDownloads++;
    connect(indexReply, &QNetworkReply::finished, this, &DownloadManager::onAssetIndexDownloaded);
    connect(indexReply, &QNetworkReply::downloadProgress, this,
            &DownloadManager::onDownloadReplyProgress);
}

void DownloadManager::onAssetIndexDownloaded() {
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) {
        qWarning() << "Asset index download finished with invalid reply.";
        return;
    }
    if (m_shuttingDown) {
        return;
    }

    const QString indexPath = activeDownloads.value(reply);
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
            // 和 onDownloadReplyFinished() 一样必须核 SHA1：镜像源下这条地址是拿索引号
            // **拼**出来的（/indexes/<id>.json），拼错时回来的可能是一份别的索引，也可能是
            // 一个 404 页面 —— 不拦住就会拿它去下几千个内容不对的资源。期望值在
            // downloadAssets() 里就登记好了（version.json 的 assetIndex.sha1）。
            const QString expectedSha1 = expectedSha1s.value(reply);
            if (expectedSha1.isEmpty() || verifyFileSha1(indexPath, expectedSha1)) {
                success = true;
                parseAndDownloadAssets(indexPath);
            } else {
                // 坏文件不留盘：它是"资源列表"本身，留在那儿没有任何价值，只会误导排查。
                errorMessage = QStringLiteral("SHA1 verification failed");
                file.remove();
            }
        } else {
            errorMessage = QStringLiteral("Failed to save asset index file");
        }
    } else {
        errorMessage = reply->errorString();
    }

    if (success) {
        emit downloadFinished(indexPath, true, QString());
    } else {
        // 索引下载失败也要计入失败，否则整批会被报成"全部完成"。
        // NOTE: 必须在 activeDownloads.remove() 之前调用 —— handleDownloadError
        // 靠这张表反查目标路径来记失败清单。
        handleDownloadError(reply, errorMessage);
    }

    activeDownloads.remove(reply);
    expectedSha1s.remove(reply);
    reply->deleteLater();
    // NOTE: 计数只对登记过的 reply 生效，上面两道 early return 挡住了没登记的。
    --pendingDownloads;
    if (pendingDownloads < 0) {
        qWarning() << "pendingDownloads went negative, clamping to 0.";
        pendingDownloads = 0;
    }
    m_doneJobs++;

    emitBatchProgress(indexPath);
    checkAllDownloadsFinished();
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

    QString basePath = AppPaths::assetsDir();
    QString objectsPath = basePath + "/objects";

    for (auto it = objects.begin(); it != objects.end(); ++it) {
        QString assetName = it.key();
        QJsonObject assetObj = it.value().toObject();
        QString hash = assetObj["hash"].toString();

        if (hash.length() != 40) {
            qWarning() << "Invalid hash for asset" << assetName << ":" << hash;
            continue;
        }

        QString subfolder = hash.left(2);
        QString assetPath = objectsPath + "/" + subfolder + "/" + hash;

        // 官方地址拼出来再经 endpoint() 落到实际源上 —— 两边的路径形状不一样
        // （镜像多了 /assets 前缀），换算规则只写一份，就在 mirrorUrl() 里。
        const QString assetUrl = endpoint(QStringLiteral("https://resources.download.minecraft.net/") +
                                          subfolder + QStringLiteral("/") + hash);
        QDir().mkpath(QFileInfo(assetPath).absolutePath());
        // 文件名就是 SHA1，所以只按存在性判断：assets 目录已经 3 GB 出头，
        // 每次点下载都把 5000 多个对象重新哈希一遍会让界面卡死好几秒。
        scheduleFile(assetUrl, assetPath, hash, -1, VerifyMode::ExistsOnly,
                     /*throttled=*/true);
    }
}

void DownloadManager::downloadClientJar(const QString &versionId, const QString &url,
                                        const QString &destinationPath, const QString &expectedSha1,
                                        qint64 expectedSize) {
    const QString effectiveUrl = m_useMirror ? mirrorClientJarUrl(versionId) : url;
    downloadFile(effectiveUrl, destinationPath, expectedSha1, expectedSize,
                 VerifyMode::SizeAndSha1);
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

        // 先过平台规则，不适用于本机的库连下载都不必
        if (!LibraryRules::libraryAllowed(libObj)) {
            continue;
        }

        // NOTE: 1.12.2 里有 2 个库完全没有 artifact（只有 classifiers），
        // 旧代码会拿空 url 和 "dir/" 这样的空 path 去发起下载，产生幽灵请求。
        //
        // 解析（含 maven 风格的库）在 LibraryRules::artifactFor() —— 启动时拼 classpath
        // 用的是同一个函数，两处的语义不会各自漂移。
        //
        // NOTE: 这里用 valid()（要**有下载地址**）是对的，和 classpath 那边的 hasPath() 不是
        //       一回事：OptiFine 那两个库没有地址、盘上本来就有，它们不该出现在下载列表里
        //       —— 拿 hasPath() 当判据会造出两条 url 为空的幽灵请求。
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
        // 这里直接返回空，行为与改动前完全一致。
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

void DownloadManager::onVersionManifestReplyFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (reply->error() == QNetworkReply::NoError) {
        QJsonParseError error;
        QJsonDocument json = QJsonDocument::fromJson(reply->readAll(), &error);
        if (error.error == QJsonParseError::NoError) {
            parseVersionManifest(json);
        } else {
            qDebug() << "JSON parse error:" << error.errorString();
        }
    } else {
        qDebug() << "Network error:" << reply->errorString();
    }
    // NOTE: 失败路径也必须发信号。VersionManager::fetchVersionDetails 靠这个信号
    // 接着去取 version.json，漏发就会让整个"下载"流程停在半路：UI 已经进了
    // "下载中"状态，却永远等不到终态，按钮一直是灰的。
    // 失败时发的是上一次成功的结果（可能为空），调用方据此自行判断。
    emit versionManifestFetched(availableVersions);
    reply->deleteLater();
}

void DownloadManager::onVersionJsonReplyFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (reply->error() == QNetworkReply::NoError) {
        QJsonParseError error;
        QJsonDocument json = QJsonDocument::fromJson(reply->readAll(), &error);
        if (error.error == QJsonParseError::NoError) {
            parseVersionJson(json);
            emit versionJsonFetched(currentVersionJson);
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
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) {
        qWarning() << "Download finished with invalid reply.";
        return;
    }
    if (m_shuttingDown) {
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
                    errorMessage = QStringLiteral("SHA1 verification failed");
                    file.remove();
                }
            } else {
                success = true;
            }
        } else {
            errorMessage = QStringLiteral("Failed to open file for writing");
        }
    } else {
        errorMessage = reply->errorString();
    }

    if (success) {
        emit downloadFinished(QFileInfo(destPath).fileName(), true, QString());
    } else {
        // NOTE: 要在 activeDownloads.remove() 之前，handleDownloadError 靠它反查路径。
        handleDownloadError(reply, errorMessage);
    }

    activeDownloads.remove(reply);
    expectedSha1s.remove(reply);
    reply->deleteLater();
    // NOTE: 计数只对登记过的 reply 生效，上面两道 early return 挡住了没登记的。
    --pendingDownloads;
    if (pendingDownloads < 0) {
        qWarning() << "pendingDownloads went negative, clamping to 0.";
        pendingDownloads = 0;
    }
    m_doneJobs++;

    emitBatchProgress(destPath);
    // 腾出一条连接就补一个排队的资源对象，然后才判定整批是否下完（顺序不能反：
    // 先判会把"队列里还有、但当下一个都没在飞"误判成整批结束）。
    pumpAssetQueue();
    checkAllDownloadsFinished();
}

void DownloadManager::onDownloadReplyProgress(qint64 bytesReceived, qint64 bytesTotal) {
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    QString destPath = activeDownloads.value(reply);
    emit downloadProgress(QFileInfo(destPath).fileName(), bytesReceived, bytesTotal);
    // 单发大文件（41 MB 的客户端 jar）期间也要让进度条动起来，否则界面像是卡住了。
    emitBatchProgress(destPath);
}