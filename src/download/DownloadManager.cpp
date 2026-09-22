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
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QTimer>

#include <cstdio>

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

// ---------------------------------------------------------------------------
// 失速看门狗的三个数（实测依据见 DownloadManager.hpp 里 mirrorPreferred() 的注释）
// ---------------------------------------------------------------------------
/// 多久看一次"在飞的下载动没动"。5 秒是"慢到该退"和"慢但在动"之间的分辨力：
/// 官方那条 209 KB 的版本清单 2.9 秒（每跳 ~350 KB），镜像那条 21 秒（每跳 ~50 KB）。
constexpr int kStallTickMs = 5000;
/// 一跳少于这么多字节就算"没动"。取 64 KB = 12.8 KB/s 的下限：镜像那条慢链子实测
/// 0.85~10 KB/s（都不够），而它在 maven 那一支上 0.9 秒给 64 KB（够）。
constexpr qint64 kStallMinBytesPerTick = 64 * 1024;
/// 连着几跳"没动"才算数。两跳 = 10 秒 —— 一次 DNS + TLS 建连（本机 1~3 秒）不该被误判。
constexpr int kStallStrikesToDemote = 2;

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
    //
    // Forge 那两个 host 是**同一个形状**，所以并在一起认：
    //   maven.minecraftforge.net → 库和安装器 jar（/net/minecraftforge/...）
    //   files.minecraftforge.net → 那个 1.7 KB 的推广表（/net/minecraftforge/forge/promotions_slim.json）
    // 2026-09-22 实测：这两条镜像上都 200，而且是**同一个文件** —— 26.2-65.1.0 的安装器 jar
    // 官方和镜像都恰好 7855558 字节；老版本也在（1.20.1-47.4.10、1.18.2-40.2.0 都 200）。
    if (host == QLatin1String("libraries.minecraft.net") ||
        host == QLatin1String("maven.minecraftforge.net") ||
        host == QLatin1String("files.minecraftforge.net")) {
        return QLatin1String(kMirrorBase) + QStringLiteral("/maven") + path;
    }
    // NeoForge 的 maven：官方把 releases / snapshots 分成两支，而镜像只收了 releases ——
    // 所以**只认这一支**，snapshots 那边原样返回。拼一个镜像上不存在的地址，症状是
    // "下载失败"，还会让人以为是我们的地址拼错了，比老老实实走官方难查得多。
    if (host == QLatin1String("maven.neoforged.net") &&
        path.startsWith(QLatin1String("/releases/"))) {
        return QLatin1String(kMirrorBase) + QStringLiteral("/maven/") + path.mid(10);
    }
    // 认不出来就原样返回（理由见头文件）。
    return officialUrl;
}

QString DownloadManager::mirrorMavenRootUrl() {
    // 结尾那个斜杠**不能省**：这个字符串是原样交给 Forge / NeoForge 安装器的 `--mirror` 的，
    // 而安装器拿它直接拼路径。漏了斜杠的表现（实测）：它会去要 `.../mavennet/minecraftforge/...`，
    // 镜像上 404，然后它自己退回官方 —— 看着像"镜像没起作用"，其实是我们给错了。
    return QLatin1String(kMirrorBase) + QStringLiteral("/maven/");
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
    return mirrorPreferred() ? mirrorUrl(officialUrl) : officialUrl;
}

bool DownloadManager::mirrorPreferred() const {
    return m_useMirror && !isMirrorDemoted();
}

bool DownloadManager::isMirrorDemoted() const {
    return mirrorDemotedAt(m_clock.elapsed(), m_mirrorDemotedUntil);
}

void DownloadManager::demoteMirrorForSlowDownload() {
    m_mirrorDemotedUntil = demoteDeadline(m_clock.elapsed(), m_mirrorDemotedUntil);
    qInfo() << "The mirror is too slow (under" << kStallMinBytesPerTick << "bytes in"
            << kStallTickMs << "ms); using the official download source for the next"
            << kMirrorDemoteMs / 1000 << "seconds.";
}

bool DownloadManager::mirrorDemotedAt(qint64 nowMs, qint64 demotedUntilMs) {
    return demotedUntilMs != 0 && nowMs < demotedUntilMs;
}

bool DownloadManager::mirrorStalled(qint64 bytesDelta, qint64 minBytesPerTick) {
    return bytesDelta < minBytesPerTick;
}

qint64 DownloadManager::demoteDeadline(qint64 nowMs, qint64 currentUntilMs) {
    return qMax(currentUntilMs, nowMs + kMirrorDemoteMs);
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

    // 看门狗从对象建好就开始跳：没有"在看之下"的回复时它什么都不做（一次 QHash 判空），
    // 所以不需要在每条下载的开始/结束处去起停它。
    m_clock.start();
    m_stallWatchdog = new QTimer(this);
    m_stallWatchdog->setInterval(kStallTickMs);
    connect(m_stallWatchdog, &QTimer::timeout, this, &DownloadManager::onStallWatchdogTick);
    m_stallWatchdog->start();
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
    m_officialFallbacks.clear();
    m_stallMeters.clear();
}

void DownloadManager::fetchVersionManifest() {
    const QString officialUrl = officialVersionManifestUrl();
    QUrl url(endpoint(officialUrl));
    QNetworkRequest request(url);
    // 元数据请求也要有超时：下载期间 UI 会禁用按钮，卡死的连接会让它永远禁用下去。
    request.setTransferTimeout(30000);
    QNetworkReply *reply = networkManager->get(request);
    // 这两个地址不同 = 这条走的是镜像 → 登记官方备用地址 + 交给看门狗看着。
    // 元数据**必须**有这条：它一发卡住，下游的版本 json / 资源索引 / 几千个资源对象
    // 一个都还没排上，整条流程就那么挂着（2026-09-22 的 NeoForge 26.2 实测就是这么挂的）。
    if (shouldRecordOfficialFallback(url.toString(), officialUrl)) {
        m_officialFallbacks[reply] = officialUrl;
        m_stallMeters[reply] = StallMeter{};
        connect(reply, &QNetworkReply::downloadProgress, this,
                &DownloadManager::onDownloadReplyProgress);
    }
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
                mirrorPreferred() ? mirrorVersionJsonUrl(versionId) : version.url;
            // 具名 QUrl 只是和下面几处保持一致的写法，**不是**为了躲"最令人烦恼的解析"：
            // 这一行本身编译无警告（实测），`QUrl url(x);` 是对象。会歧义的是
            // `QNetworkRequest request(QUrl(x))` 那种"参数写成了类型名(值)"的形状，
            // 详细说明在 onAssetIndexDownloaded() 上面。
            const QUrl versionQUrl(effectiveUrl);
            QNetworkRequest request(versionQUrl);
            request.setTransferTimeout(30000);
            QNetworkReply *reply = networkManager->get(request);
            // 和版本清单同理：镜像这条路由现在会 302 到一条只有几百字节每秒的链子上，
            // 没有备用地址的话它既不失败也不完成。
            if (shouldRecordOfficialFallback(versionQUrl.toString(), version.url)) {
                m_officialFallbacks[reply] = version.url;
                m_stallMeters[reply] = StallMeter{};
                connect(reply, &QNetworkReply::downloadProgress, this,
                        &DownloadManager::onDownloadReplyProgress);
            }
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
                                   VerifyMode mode, const QString &officialUrl) {
    // 一单一发：库、jar、索引这些批次都只有几十个作业，没有排队的必要。
    scheduleFile(url, destinationPath, expectedSha1, expectedSize, mode, false, officialUrl);
}

void DownloadManager::scheduleFile(const QString &url, const QString &destinationPath,
                                   const QString &expectedSha1, qint64 expectedSize,
                                   VerifyMode mode, bool throttled,
                                   const QString &officialUrl) {
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
        m_pendingAssetJobs.enqueue(AssetJob{url, destinationPath, expectedSha1, officialUrl});
        pumpAssetQueue();
    } else {
        startDownload(QUrl(url), destinationPath, expectedSha1, officialUrl);
    }
}

void DownloadManager::pumpAssetQueue() {
    // 判据用 activeDownloads.size() 而不是另开一个计数器：它是"当下真在飞的作业"
    // 的唯一事实，别的下载（客户端 jar 之类）占着连接时这里自然少发几个。
    while (!m_pendingAssetJobs.isEmpty()
           && activeDownloads.size() < kMaxConcurrentAssetDownloads) {
        const AssetJob job = m_pendingAssetJobs.dequeue();
        startDownload(QUrl(job.url), job.destinationPath, job.expectedSha1, job.officialUrl);
    }
}

void DownloadManager::startDownload(const QUrl &url, const QString &destinationPath,
                                    const QString &expectedSha1, const QString &officialUrl) {
    // 镜像正被降级、而这条作业知道官方那条地址：**直接改道**，别等它慢 10 秒再退。
    // 这条判断必须在这里、不能只在"挑地址"的地方：资源对象是几千个一次性排进队列的，
    // 它们身上的镜像地址在排队那一刻就拼好了（见 AssetJob），等它们一个个撞上慢判据
    // 再退等于把 10 秒乘以几千。
    QUrl target = url;
    QString fallback = officialUrl;
    if (isMirrorDemoted() && !officialUrl.isEmpty() && officialUrl != url.toString()) {
        target = QUrl(officialUrl);
        fallback.clear();  // 已经走官方了，"退回官方"这回事不存在
    }
    QNetworkRequest request(target);
    // 没有超时的话，一个卡死的连接会让 pendingDownloads 永远归不了零。UI 在"下载中"
    // 会禁用按钮，那就从"日志刷屏"变成"永久卡死"，比原来的问题更糟。
    request.setTransferTimeout(30000);
    QNetworkReply *reply = networkManager->get(request);
    activeDownloads[reply] = destinationPath;
    if (!expectedSha1.isEmpty()) {
        expectedSha1s[reply] = expectedSha1;
    }
    // 走的是镜像、而且调用方知道官方那条地址是什么 —— 记下来，这一发取不回来时
    // 拿它再试一次（见 m_officialFallbacks 和 retryWithOfficialUrl()）。
    // NOTE: 两个地址相同就不记：那种情况下"退回官方"等于原样再发一次，只会把
    //       同一个失败重复一遍，还多花一个超时。
    if (shouldRecordOfficialFallback(target.toString(), fallback)) {
        m_officialFallbacks[reply] = fallback;
        // 在看之下：这条有退路，慢了就 abort 它换官方那条，不用一路等下去。
        m_stallMeters[reply] = StallMeter{};
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
        // 这里经 endpoint() 过一道把地址落到当前下载源上（换算规则只写一份）。
        // 第三个参数是那条**官方**地址：镜像上取不回来时按它再试一次（实测有一次
        // Forge/NeoForge 的库就是这么慢下来才失败的）。注意 Forge 那几个 maven 从
        // 2026-09-22 起也认得了（见 mirrorUrl()），认不出来的才会原样返回。
        downloadFile(endpoint(lib.url), destPath, lib.sha1, lib.size, VerifyMode::SizeAndSha1,
                     lib.url);
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
    // 镜像源下按索引号重拼。这一条映射是整套镜像里最没把握的一个（/indexes/<id>.json）。
    //
    // 2026-09-22 实测：这个路由**整个是坏的** —— /indexes/{legacy,1.18,1.20,1.21,5,32}.json
    // 全是 404「Not Found」，而同一时刻同一个 host 上 /version/<id>/json 和 /version/<id>/client
    // 都是 200、资源对象 /assets/<hash> 也是 200。也就是说不是我们拼错了，是它那一边这条路由
    // 当下没了（它自己的文档里还写着这个地址，以后可能会回来）。
    //
    // 所以这里**登记一份官方地址当备用**：镜像那份不管因为什么没拿到（404 / 内容不对 /
    // 超时），就退回官方再试一次 —— 见 onAssetIndexDownloaded()。不这么做的话，镜像源下
    // 任何一个版本都装不全（索引拿不到 → 那几千个资源对象一个都不知道要下谁），
    // 而用户的感受是"换到镜像源之后下载全失败了"。
    const QString indexUrl =
        mirrorPreferred() ? mirrorAssetIndexUrl(assetIndex.id) : assetIndex.url;
    // NOTE: 这几个 URL 先落到具名的 QUrl 变量上，不写 `QNetworkRequest request(QUrl(x))`：
    //       那一行会被编译器当成函数声明（最令人烦恼的解析）—— 它读成"形参名叫 x 的
    //       QUrl 类型参数"，于是 request 成了函数名。原来那句能编过只是因为括号里
    //       是 `assetIndex.url` 这种带点的表达式，没法当形参名。
    const QUrl indexQUrl(indexUrl);
    QNetworkRequest request(indexQUrl);
    request.setTransferTimeout(30000);
    QNetworkReply *indexReply = networkManager->get(request);
    activeDownloads[indexReply] = indexPath;
    if (mirrorPreferred() && !assetIndex.url.isEmpty()) {
        // 只在镜像源下登记（官方源下 indexUrl 已经是它，没有"备用"这回事）。
        m_officialFallbacks[indexReply] = assetIndex.url;
    }
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
        m_officialFallbacks.remove(reply);
        m_stallMeters.remove(reply);
        emit downloadFinished(indexPath, true, QString());
    } else if (retryWithOfficialUrl(reply, indexPath, &DownloadManager::onAssetIndexDownloaded)) {
        // 镜像那条路没成，已经换成官方地址重发了 —— 这一次**不算失败**：作业还在这一个
        // 里，计数和失败清单都不动，直接返回（下面那些收尾是给"这个 reply 真的结账"用的）。
        return;
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

bool DownloadManager::retryWithOfficialUrl(QNetworkReply *reply, const QString &destinationPath,
                                           void (DownloadManager::*finishedSlot)()) {
    // 备用地址只在"这条作业走的是镜像"时登记过。没登记 = 这条就是官方地址，
    // 没得再退，交给调用方按失败处理。
    const QString officialUrl = m_officialFallbacks.take(reply);
    if (officialUrl.isEmpty()) {
        return false;
    }
    // 清理掉的原因得说清楚：这一步走的是"换一条路再试"，不是"这次作业失败"。
    // 所以 pendingDownloads **不加也不减**（作业一直只有一个，只是换了条连接），
    // m_doneJobs / m_failedJobs 也都不动 —— 都由重发出去的那一次说了算。
    // NOTE: 新那条回复**不登记**备用地址（take 已经把它拿走了），所以不会来回退。
    qInfo() << "Mirror download failed, retrying from the official url:" << officialUrl;
    const QUrl url(officialUrl);
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    QNetworkReply *retry = networkManager->get(request);
    const QString expectedSha1 = expectedSha1s.take(reply);
    activeDownloads.remove(reply);
    activeDownloads[retry] = destinationPath;
    // 看门狗的记账**不搬**：重发出去的这条走官方，没有退路，abort 它只会把下载弄失败。
    m_stallMeters.remove(reply);
    if (!expectedSha1.isEmpty()) {
        expectedSha1s[retry] = expectedSha1;
    }
    // 接哪个槽由调用方说：索引有自己那一个（成功了还得解析索引、往下发几千个对象），
    // 别的都走通用那个。接错了的表现不明显 —— 索引会被当成普通文件落盘就完事。
    connect(retry, &QNetworkReply::finished, this, finishedSlot);
    connect(retry, &QNetworkReply::downloadProgress, this,
            &DownloadManager::onDownloadReplyProgress);
    reply->deleteLater();
    return true;
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
        scheduleFile(assetUrl, assetPath, hash, -1, VerifyMode::ExistsOnly,
                     /*throttled=*/true, officialAssetUrl);
    }
}

void DownloadManager::downloadClientJar(const QString &versionId, const QString &url,
                                        const QString &destinationPath, const QString &expectedSha1,
                                        qint64 expectedSize) {
    const QString effectiveUrl = mirrorPreferred() ? mirrorClientJarUrl(versionId) : url;
    // 第三个参数是官方那条地址（镜像那条是**按版本号拼**出来的，形状完全不一样，
    // 没法由它反推回去）。几十 MB 的 jar 要是取到一半超时，退一步比整批重来划算。
    downloadFile(effectiveUrl, destinationPath, expectedSha1, expectedSize,
                 VerifyMode::SizeAndSha1, url);
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
    // 元数据的回复平时不在 activeDownloads 里（不落盘），只有"从镜像退到官方"重发出来的
    // 那一条在。这里统一摘一遍，省得在每条路上都写一次。
    activeDownloads.remove(reply);
    // 镜像那条没成（失败 / 被看门狗 abort）→ 用官方地址重发一次，这次不算失败。
    if (reply->error() != QNetworkReply::NoError &&
        retryWithOfficialUrl(reply, QStringLiteral("version_manifest.json"),
                             &DownloadManager::onVersionManifestReplyFinished)) {
        return;
    }
    m_stallMeters.remove(reply);
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
    activeDownloads.remove(reply);
    if (reply->error() != QNetworkReply::NoError &&
        retryWithOfficialUrl(reply, QStringLiteral("version_json"),
                             &DownloadManager::onVersionJsonReplyFinished)) {
        return;
    }
    m_stallMeters.remove(reply);
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
        m_officialFallbacks.remove(reply);
        m_stallMeters.remove(reply);
        emit downloadFinished(QFileInfo(destPath).fileName(), true, QString());
    } else if (retryWithOfficialUrl(reply, destPath, &DownloadManager::onDownloadReplyFinished)) {
        // 镜像那条路没成，已经换成官方地址重发了 —— 这一次**不算失败**：作业还在这一个
        // 里，计数和失败清单都不动，直接返回（下面那些收尾是给"这个 reply 真的结账"用的）。
        return;
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

void DownloadManager::onStallWatchdogTick() {
    if (m_shuttingDown || m_officialFallbacks.isEmpty()) {
        return;
    }
    // 只走"在看之下"的那些回复（= 走了镜像、而且知道官方那条地址）。就地 abort 会同步
    // 触发 finished、进而改 m_officialFallbacks / m_stallMeters，所以先把要 abort 的攒起来。
    QList<QNetworkReply *> stalled;
    for (auto it = m_stallMeters.begin(); it != m_stallMeters.end(); ++it) {
        QNetworkReply *reply = it.key();
        if (!m_officialFallbacks.contains(reply)) {
            continue;  // 已经不在看之下了（重发出去的那一条就属于这种）
        }
        StallMeter &meter = it.value();
        if (mirrorStalled(meter.bytesSeen - meter.lastTickBytes, kStallMinBytesPerTick)) {
            if (++meter.strikes >= kStallStrikesToDemote) {
                stalled.append(reply);
            }
        } else {
            meter.strikes = 0;
        }
        meter.lastTickBytes = meter.bytesSeen;
    }
    if (stalled.isEmpty()) {
        return;
    }
    // 先降级再 abort：abort 出来的失败会走 onDownloadReplyFinished，那里面退回官方时
    // 要读的就是"已经降级了"这个状态（排队的那几千个资源对象同理，见 startDownload()）。
    demoteMirrorForSlowDownload();
    for (QNetworkReply *reply : stalled) {
        qInfo() << "Aborting a stalled mirror download and retrying it from the official url:"
                << activeDownloads.value(reply);
        // 只 abort，不在这里收尾：finished 那个槽会拿到 OperationCanceledError 并按
        // "镜像那条没成"处理（退回官方重发，作业还是同一个、不算失败）。
        reply->abort();
    }
}

void DownloadManager::onDownloadReplyProgress(qint64 bytesReceived, qint64 bytesTotal) {
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    // 在看之下的回复要记一份进度给看门狗看。**只有**它们会进这张表（startDownload 里登记）。
    if (auto meter = m_stallMeters.find(reply); meter != m_stallMeters.end()) {
        meter->bytesSeen = bytesReceived;
    }
    QString destPath = activeDownloads.value(reply);
    emit downloadProgress(QFileInfo(destPath).fileName(), bytesReceived, bytesTotal);
    // 单发大文件（41 MB 的客户端 jar）期间也要让进度条动起来，否则界面像是卡住了。
    emitBatchProgress(destPath);
}