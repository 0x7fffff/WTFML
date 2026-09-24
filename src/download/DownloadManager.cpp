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
/// 连着几跳"没动"才算数。两跳 = 10 秒 —— 一条**已经在吐字节**的链子停 10 秒，够说明它不行了。
///
/// NOTE: 这里以前还有半句理由「一次 DNS + TLS 建连（本机 1~3 秒）不该被误判」，那半句现在
///       由 countsTowardStallStrike() 承担 —— 但**那句结论 2026-09-24 改对了，原文写反了**：
///       判据是 `bytesSeen > 0 && mirrorStalled(...)`（见那个函数的实现，理由在 .hpp 里），
///       一个字节都还没收到的回复进不了这条判据。所以"一条刚发出去的回复可能几十秒才轮到它
///       吐第一个字节"这件事**不会**被算成失速，那几十秒里 bytesSeen 一直是 0。照原文
///      （「照样会被这条判据算成失速」）去改，会去修一个不存在的问题。
constexpr int kStallStrikesToDemote = 2;

/// 从**官方**那条版本 json 的地址里取出 sha1。
///
/// 官方那条的形状是 `https://piston-meta.mojang.com/v1/packages/<sha1>/<id>.json`，
/// 也就是从末尾往前找**第一个正好 40 位十六进制的路径段** —— 不写死"倒数第二段"：
/// 末尾段的形状（`1.20.1.json` 还是别的）不值得当成约定。
/// 找不到合适的那一段（镜像自拼的地址、地址形状变了、空串）就返回空串，
/// 调用方按"没有期望值 → 不核 sha1"处理（和下载其它文件时 expectedSha1 为空一个口径）。
QString sha1FromVersionUrl(const QString &url) {
    const QStringList segments = url.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (int i = segments.size() - 1; i >= 0; --i) {
        const QString candidate = segments.at(i);
        if (candidate.size() != 40) {
            continue;
        }
        bool looksLikeSha1 = true;
        for (const QChar &ch : candidate) {
            const QChar lower = ch.toLower();
            if (!ch.isDigit() && (lower < QLatin1Char('a') || lower > QLatin1Char('f'))) {
                looksLikeSha1 = false;
                break;
            }
        }
        if (looksLikeSha1) {
            return candidate.toLower();
        }
    }
    return QString();
}

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
    // 镜像源在本类里就是 BMCLAPI 那一个主机（调用点全是自拼镜像地址的那几处，见头文件）。
    return mirrorDemotedFor(QUrl(QString::fromLatin1(kMirrorBase)).host());
}

bool DownloadManager::mirrorDemotedFor(const QString &host) const {
    if (host.isEmpty()) {
        return false;  // 查不到主机 = 没有"这个主机的降级记录"，按没降级算
    }
    return mirrorDemotedAt(m_clock.elapsed(), m_mirrorDemotedUntilByHost.value(host, 0));
}

void DownloadManager::demoteMirrorForSlowDownload(const QString &host) {
    if (host.isEmpty()) {
        return;  // 连主机名都没有，降级不知道该降到谁头上；宁可什么都不做
    }
    m_mirrorDemotedUntilByHost[host] =
        demoteDeadline(m_clock.elapsed(), m_mirrorDemotedUntilByHost.value(host, 0));
    qInfo() << "The mirror is too slow (under" << kStallMinBytesPerTick << "bytes in"
            << kStallTickMs << "ms); using the official download source for the next"
            << kMirrorDemoteMs / 1000 << "seconds. Host demoted:" << host;
}

bool DownloadManager::mirrorDemotedAt(qint64 nowMs, qint64 demotedUntilMs) {
    return demotedUntilMs != 0 && nowMs < demotedUntilMs;
}

bool DownloadManager::mirrorStalled(qint64 bytesDelta, qint64 minBytesPerTick) {
    return bytesDelta < minBytesPerTick;
}

bool DownloadManager::countsTowardStallStrike(qint64 bytesSeen, qint64 bytesDelta,
                                              qint64 minBytesPerTick) {
    // 一次都没收到过字节 = 这条回复还没开始传，不是"传得慢"。理由和实测见头文件。
    return bytesSeen > 0 && mirrorStalled(bytesDelta, minBytesPerTick);
}

qint64 DownloadManager::demoteDeadline(qint64 nowMs, qint64 currentUntilMs) {
    return qMax(currentUntilMs, nowMs + kMirrorDemoteMs);
}

bool DownloadManager::mayIssueAnotherDownload(int activeCount, int queuedCount) {
    // 排空的队列不用问；到顶了不管还排着多少条都不发 —— 后一半是这条函数存在的全部理由。
    return queuedCount > 0 && activeCount < kMaxConcurrentDownloads;
}

bool DownloadManager::mayIssueAnotherDownloadToHost(int activeForHost, bool hostKnownHttp2) {
    // 已知 h2 的主机不受这条管（一条 TCP 连接上多路复用，放几条归全局上限说）。
    // 还不知道的主机按 Qt 每主机的连接上限守着 —— 多发的那些会进 Qt 内部那个会超时的队列。
    return hostKnownHttp2 || activeForHost < kMaxConcurrentPerHost;
}

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

    // "这个主机走不走 h2"只在这一处问，四条 finished 槽里一条都不问。
    // NOTE: 这条信号的连接**必须比我们自己那些 finished 连接早**，而它确实是：它是
    //       QNetworkAccessManager 在造回复时就接好的，我们是在造出来之后才接的。
    //       顺序反过来的话，第一条回复收尾时 pumpDownloadQueue() 看到的还是"这个主机
    //       没证明过 h2"，白等一轮往返（不会错，只是慢）。
    connect(networkManager, &QNetworkAccessManager::finished, this,
            &DownloadManager::noteReplyProtocol);
}

DownloadManager::~DownloadManager() {
    // abort() 有可能同步触发 finished，处理函数会走完一套收尾甚至发信号。
    // 对象正在析构，那些回调不该再跑，用这个标志在 checkAllDownloadsFinished
    // 和槽函数入口处挡掉。
    m_shuttingDown = true;
    // 挂在 QNetworkAccessManager::finished 上那条（noteReplyProtocol）在这里断掉。
    // 理由和别的槽不一样：那个槽会写 m_http2Hosts，而**成员是在析构函数体跑完之后**才销毁的、
    // manager 这个子对象还要更晚（在 QObject 基类析构里）—— 中间那一段 manager 只要再发一次
    // finished（它析构时删掉子回复就可能发），那个槽就会往一个已经没了的 QSet 里插。
    // m_shuttingDown 挡得住别的槽，挡不住"成员已经销毁"这件事，只能断开。
    disconnect(networkManager, nullptr, this, nullptr);
    // 清除当前的下载
    for (auto reply : activeDownloads.keys()) {
        reply->abort();
        reply->deleteLater();
    }
    activeDownloads.clear();
    // 队列里的作业一个请求都没发出去，abort 那套对它们无效，只能直接丢掉。
    m_pendingJobs.clear();
    m_officialFallbacks.clear();
    m_stallMeters.clear();
    m_metadataReplies.clear();
}

void DownloadManager::fetchVersionManifest() {
    const QString officialUrl = officialVersionManifestUrl();
    QUrl url(endpoint(officialUrl));
    QNetworkRequest request(url);
    // 元数据请求也要有超时：下载期间 UI 会禁用按钮，卡死的连接会让它永远禁用下去。
    request.setTransferTimeout(30000);
    QNetworkReply *reply = networkManager->get(request);
    // 元数据作业：**不落盘、也不计进 pendingDownloads / m_totalJobs**（进度条上它不是一个
    // 作业），但必须进 m_stallMeters，否则卡住的这条没人管。登记进这张表就是为了让看门狗
    // 收它的时候知道"别动那套计数"（见 DownloadManager.hpp 里 m_metadataReplies 的说明）。
    MetadataJob job;
    job.label = QStringLiteral("version_manifest.json");
    m_metadataReplies[reply] = job;
    // 元数据**必须**进看门狗，而且这里**不看是不是走镜像**：它一发卡住，下游的版本 json /
    // 资源索引 / 几千个资源对象一个都还没排上，整条流程就那么挂着（2026-09-22 的 NeoForge
    // 26.2 实测就是这么挂的）。以前这两条登记都写在"有官方备用地址"那个 if 里面，于是
    // **官方源下两条元数据请求根本没人看着**：30 秒的传输超时只管"一个字节都没有"，
    // 而细水长流（每秒几个字节、永远不结束）正好是看门狗要管的那一种。
    //
    // NOTE: lastSignOfLifeMs 必须显式写成**现在**：StallMeter 的默认值是 0，而应用跑过
    //       90 秒之后 0 会被算成"已经 90 秒没动静"—— 第一个 tick（最多 5 秒后）就会把这条
    //       刚发出去的请求判成卡死。另外三处登记（startDownload / downloadAssets /
    //       retryWithOfficialUrl）都是显式写当前时刻的，这两处以前跟着那个 if 一起漏了。
    m_stallMeters[reply].lastSignOfLifeMs = m_clock.elapsed();
    connect(reply, &QNetworkReply::downloadProgress, this,
            &DownloadManager::onDownloadReplyProgress);
    // 这两个地址不同 = 这条走的是镜像 → 记下官方那条地址，取不回来时按它再试一次。
    if (shouldRecordOfficialFallback(url.toString(), officialUrl)) {
        m_officialFallbacks[reply] = officialUrl;
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
            // 和版本清单同理：元数据作业，不计进那套计数、但要被看门狗看着（无条件）。
            MetadataJob job;
            job.label = QStringLiteral("version_json");
            // 这一条该是哪个版本、以及它该是什么字节 —— 官方那条 URL 就是
            // `.../v1/packages/<sha1>/<id>.json`，两样都在里面。**下载完要核**（见
            // onVersionJsonReplyFinished）：这条 json 决定用哪个 Java、读哪个资源索引，
            // 半份或者别的版本那份会让游戏起不来，而那种失败现场看不出原因。
            // NOTE: 镜像那条路由是自己拿版本号拼的（不带 sha1），所以镜像下 sha1 为空
            //       = 不核 sha1，但 id 照样核。
            job.expectedId = versionId;
            job.expectedSha1 = sha1FromVersionUrl(version.url);
            m_metadataReplies[reply] = job;
            // 和版本清单同理：镜像这条路由会 302 到一条只有几百字节每秒的链子上，
            // 没有备用地址的话它既不失败也不完成。
            if (shouldRecordOfficialFallback(versionQUrl.toString(), version.url)) {
                m_officialFallbacks[reply] = version.url;
            }
            m_stallMeters[reply].lastSignOfLifeMs = m_clock.elapsed();
            connect(reply, &QNetworkReply::downloadProgress, this,
                    &DownloadManager::onDownloadReplyProgress);
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
    // 还排着队的作业**不发了**：这一批已经放弃了，让它们接着跑等于"用户点了取消，
    // 几百 MB 还在后台往盘上写"。已经在飞的那几条照旧跑完（那几条的 NOTE 见 hpp）。
    // NOTE: 队列曾经只装资源对象，而资源对象只在批次里排，所以漏掉这一句不要紧；
    //       现在库、整合包文件也走这条队列，"放弃"就必须把队列一起清掉。
    m_pendingJobs.clear();
    // 置真 = "这一批的完成信号已经发过了"：checkAllDownloadsFinished() 之后一个字都不会发
    // （那几个交完货才回来的回复会走到那里，然后立刻返回）。
    m_batchEmitted = true;
    m_currentFile.clear();
}

bool DownloadManager::isBusy() const {
    // 队列也得算："还排着几百个没发出去"当然还是忙 —— 漏掉它的表现是界面解禁、
    // 用户以为下载完了，然后再点一次又叠一批上去。
    return m_batchOpen || pendingDownloads > 0 || !m_pendingJobs.isEmpty();
}

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
    // 所有下载都走这条排队路（理由见 mayIssueAnotherDownload）：作业少的时候队列当场就
    // 把它发出去，和"直接发"一模一样；作业一多就只有 kMaxConcurrentDownloads 条在飞。
    scheduleFile(url, destinationPath, expectedSha1, expectedSize, mode, officialUrl);
}

void DownloadManager::scheduleFile(const QString &url, const QString &destinationPath,
                                   const QString &expectedSha1, qint64 expectedSize,
                                   VerifyMode mode, const QString &officialUrl) {
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

    // 排进队列之后立刻试着发一次：队列是空的、连接也有富余的时候，这一句就把它发出去了
    // （所以"只下一两个文件"的那些调用点感觉不到队列存在）。
    m_pendingJobs.enqueue(
        PendingJob{url, destinationPath, expectedSha1, officialUrl, QUrl(url).host()});
    pumpDownloadQueue();
}

void DownloadManager::pumpDownloadQueue() {
    // 判据用 activeDownloads.size() 而不是另开一个计数器：它是"当下真在飞的作业"
    // 的唯一事实 —— 别的下载（资源索引、客户端 jar）占着连接时这里自然少发几个。
    //
    // 两条判据都要过：全局还有位置（mayIssueAnotherDownload）+ **这个主机**还有位置
    // （mayIssueAnotherDownloadToHost）。后面那条是 kMaxConcurrentDownloads 调大之后
    // 必须补上的：全局放 24 条，但一个还没证明自己走 h2 的主机只能放 6 条，否则多出来的
    // 会进 Qt 内部那个会超时的队列（见那两条常量的注释）。
    //
    // 队头那个作业的主机满了、**不等于**后面没有了：整合包导入的队列就是几个 CDN 交错排的
    // （见 preferredUrlOrder），队头这条排不上而后面那条能排上的情况很常见。所以这里跳过
    // 去后面的作业找（i 往前走），而不是一条堵住就整个不发了。跳过只是换一个发送次序，
    // 不丢作业、也不改变谁先谁后之外的任何事。
    int i = 0;
    while (i < m_pendingJobs.size()
           && mayIssueAnotherDownload(activeDownloads.size(), m_pendingJobs.size())) {
        const PendingJob &candidate = m_pendingJobs.at(i);
        if (!mayIssueAnotherDownloadToHost(activeCountForHost(candidate.host),
                                           m_http2Hosts.contains(candidate.host))) {
            ++i;
            continue;
        }
        // takeAt 之后上面那个引用就悬了，所以先拷出来再删。
        const PendingJob job = m_pendingJobs.takeAt(i);
        startDownload(QUrl(job.url), job.destinationPath, job.expectedSha1, job.officialUrl);
    }
}

int DownloadManager::activeCountForHost(const QString &host) const {
    int n = 0;
    for (auto it = activeDownloads.constBegin(); it != activeDownloads.constEnd(); ++it) {
        // 用**请求自己**的 URL（`request().url()`）：这是"我们这条发给谁"的原话，和队列那头
        // 的 `PendingJob::host` 是同一个东西，两边才比得到一起。别用 `reply->url()` ——
        // 那个在跳转之后未必还是我们请求的那个地址（镜像那条 302 到别处之后，按它算就会把
        // 额度记到另一个主机头上，而发给镜像的那几条一条都没算进去，守卫等于没生效）。
        if (it.key()->request().url().host() == host) {
            ++n;
        }
    }
    return n;
}

void DownloadManager::noteReplyProtocol(QNetworkReply *reply) {
    if (!reply || m_shuttingDown) {
        return;
    }
    const QVariant h2 = reply->attribute(QNetworkRequest::Http2WasUsedAttribute);
    if (h2.isValid() && h2.toBool()) {
        const QString host = reply->request().url().host();
        if (!host.isEmpty() && !m_http2Hosts.contains(host)) {
            m_http2Hosts.insert(host);
            // 这条只是给排查用的：并发上不去的时候，第一件要问的事就是"这个主机认不认 h2"。
            qDebug() << "Host confirmed HTTP/2 (its per-host cap is now lifted):" << host;
        }
    }
}

void DownloadManager::startDownload(const QUrl &url, const QString &destinationPath,
                                    const QString &expectedSha1, const QString &officialUrl) {
    // 这条作业的**首选地址那个主机**正被降级、而它还知道备用地址：**直接改道**，别等它慢
    // 10 秒再退。这条判断必须在这里、不能只在"挑地址"的地方：资源对象是几千个一次性排进
    // 队列的，它们身上的镜像地址在排队那一刻就拼好了（见 PendingJob），等它们一个个撞上慢
    // 判据再退等于把 10 秒乘以几千。
    //
    // NOTE: 问的是**这条地址自己那个主机**，不是"镜像源降没降级"。整合包的"镜像"是每条文件
    //       各自的首选地址（见 mirrorDemotedFor()），问全局那一个的话，mediafilez 慢会把
    //       cdn.modrinth.com 上的作业一起改道 —— 那正是 2026-09-23 那次导入垮掉的地方。
    QUrl target = url;
    QString fallback = officialUrl;
    if (mirrorDemotedFor(target.host()) && !officialUrl.isEmpty() &&
        officialUrl != url.toString()) {
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
    }
    // **每一条**发出去的回复都进这张表（不只是有备用地址那些）：看门狗靠 lastSignOfLifeMs
    // 判"发出去之后一点动静都没有"，那种回复不管有没有退路都不该一直攥着连接
    // （见 kNoProgressGiveUpMs）。有备用地址的那些另外还会走"太慢就换一条路"那条判据。
    m_stallMeters[reply].lastSignOfLifeMs = m_clock.elapsed();
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
    // m_pendingJobs：还排着队的作业也是"没下完"，漏掉这一条会在还差几百个文件的
    // 时候发出 allDownloadsFinished。
    if (m_shuttingDown || m_batchOpen || m_batchEmitted || pendingDownloads > 0
        || !m_pendingJobs.isEmpty()) {
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
    // 索引这条也进"在飞"表：它要是发出去之后一直没动静，同样得有人把它判失败 ——
    // 索引拿不到 = 清单里那几千个资源对象一个都不知道要下谁，整批会静静地什么都不干。
    m_stallMeters[indexReply].lastSignOfLifeMs = m_clock.elapsed();
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
    // 和 onDownloadReplyFinished() 一样：失败这条路上也要把在飞表摘干净（理由见那边）。
    m_stallMeters.remove(reply);
    m_officialFallbacks.remove(reply);
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

    // 备用地址那个主机现在满着的话，**别硬发**：硬发的那些会被 Qt 排进它内部那个队列，而
    // 30 秒的传输超时**从请求建好就开始算** —— 于是它们会在拿到连接之前成批报
    // `Operation timed out`，而且**这一次死是真死**：重发出去那条身上已经没有备用地址了
    // （take 已经把它拿走），超时就是作业失败。
    // 2026-09-23 那次导入就是 13 条一起退回官方、9 条这么死在 65 秒那一跳上（35 秒 abort 的
    // 那 8 条 +30 秒整）。所以放回**我们自己**的队列里等泵那一头腾位置，判据和别的作业
    // 用的是同一个 mayIssueAnotherDownloadToHost()。
    //
    // NOTE: 只认**文件**作业这一条路。退回官方之后它就是一条普通作业了（没有备用地址、
    //       落盘路径和 sha1 都在身上），队列放得下。资源索引那条是元数据作业，得带着
    //       MetadataJob 和它自己的完成槽走；而它一次只会有一条、堆不起来 —— 为了它给
    //       PendingJob 塞上元数据字段，换来的收益是零。
    //
    // NOTE: 这一段**没有端到端用例**（验证工程里那个 V6f 只钉了降级那两条，钉不到这里）。
    //       要真区分出"我们拦住了"和"Qt 拦住了"，得让**同时**退回官方的作业数超过
    //       kMaxConcurrentPerHost —— 也就是一次 tick 里要 abort 掉 6 条以上。而镜像那一侧
    //       自己也受这条按主机守卫管着（假服务器都只会说 HTTP/1.1，拿不到"已证明 h2"那个
    //       豁免），所以在飞的永远不超过 6 条、能同时退回的最多 6 条，正好等于官方那个主机
    //       的上限：拦与不拦都是 6 条上链路，测试是绿的但什么都没证明。
    //       真要做到，得有一台**真会协商 h2 的假镜像**（h2 主机才有 24 条豁免），本机做不到。
    //       所以要改写这一段，请按"这条判据没被用例守着"来对待。
    const QString retryHost = QUrl(officialUrl).host();
    if (finishedSlot == &DownloadManager::onDownloadReplyFinished &&
        !mayIssueAnotherDownloadToHost(activeCountForHost(retryHost),
                                       m_http2Hosts.contains(retryHost))) {
        const QString expectedSha1 = expectedSha1s.take(reply);
        activeDownloads.remove(reply);
        m_stallMeters.remove(reply);
        // 计数在这里还回去：这条作业回到"排队中"了，发送那一步（startDownload）会再加回来。
        // 调用方拿到 true 会立刻 return，不会走它自己那次 --pendingDownloads —— 两处只能有
        // 一处减，减两次数就对不上（症状是整批提前宣布下完）。
        --pendingDownloads;
        if (pendingDownloads < 0) {
            qWarning() << "pendingDownloads went negative, clamping to 0.";
            pendingDownloads = 0;
        }
        // 摘完表再收尾：这条回复已经结账了，晚到的 finished 会在入口处被"没登记"挡掉。
        reply->deleteLater();
        m_pendingJobs.enqueue(
            PendingJob{officialUrl, destinationPath, expectedSha1, QString(), retryHost});
        // 别的作业可能刚腾出位置（也可能这一条立刻就能发出去），顺手泵一次。
        pumpDownloadQueue();
        qInfo() << "The fallback host is busy; this retry waits in our own queue instead:"
                << officialUrl;
        return true;
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
    // "这条是元数据作业"这一整条记录（名字 + 该是哪个版本 + 该是什么字节）要**搬**到重发
    // 出去的那一条上（不是复制）：搬完原来那个指针就从表里没了，不会留下一个指向已释放
    // 对象的键（同 :907 那段 NOTE 的担心）。核 sha1/id 的那两步在重发的回复回来时照做 ——
    // 换了一条路不等于可以不核。
    if (const MetadataJob job = m_metadataReplies.take(reply); !job.label.isEmpty()) {
        m_metadataReplies[retry] = job;
    }
    // 看门狗的记账**不搬**：重发出去的这条走官方，没有退路，"太慢就 abort 换一条路"
    // 对它不成立。
    m_stallMeters.remove(reply);
    // 但"卡死"那条兜底照样管它：发出去之后一直没动静的回复，不管走哪条路都得有人判它失败，
    // 不然整批永远收不了尾。
    m_stallMeters[retry].lastSignOfLifeMs = m_clock.elapsed();
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
        scheduleFile(assetUrl, assetPath, hash, -1, VerifyMode::ExistsOnly, officialAssetUrl);
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
    // 退出途中不碰任何成员（同别的槽）：abort() 会让 finished 同步回调，那时父对象
    // （就是本对象）已经在析构里了。销毁前那一句 disconnect() 只挡得住队列里的，挡不住这种。
    if (reply == nullptr || m_shuttingDown) {
        return;
    }
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
    m_metadataReplies.remove(reply);
    // NOTE: 这张表**每一条收尾的路都要摘干净**（这条是成功路径，以前漏了它）。
    //       键是回复指针，Qt 释放之后下一批复用同一个地址，残留的键会让一条**没有**
    //       备用地址的新回复被判成"有退路"（看门狗那条慢镜像判据），然后拿着一份**陈旧**的
    //       官方地址去重发 —— 重发到上一个版本 json 的地址上。
    //
    // NOTE: 摘的时机是**下面那段内容核完之后**，不是紧跟 m_stallMeters（2026-09-24 挪的）：
    //       镜像的那份清单"HTTP 200 + 截了一半/根本不是 JSON"是它的典型坏法，那种失败也该
    //       退回官方源再试一次 —— 先摘掉就没有退路了。
    if (reply->error() == QNetworkReply::NoError) {
        QJsonParseError error;
        QJsonDocument json = QJsonDocument::fromJson(reply->readAll(), &error);
        if (error.error == QJsonParseError::NoError) {
            parseVersionManifest(json);
        } else {
            qWarning() << "版本清单下载回来解不开，按失败处理：" << error.errorString();
            // 解不开 = 内容和"网络失败"一样不可用：有备用地址就换一条路再试一次
            // （镜像那条路会 302 到一个半死不活的链子上，回来半截是常事）。
            if (retryWithOfficialUrl(reply, QStringLiteral("version_manifest.json"),
                                     &DownloadManager::onVersionManifestReplyFinished)) {
                return;
            }
        }
    } else {
        qDebug() << "Network error:" << reply->errorString();
    }
    m_officialFallbacks.remove(reply);
    // NOTE: 失败路径也必须发信号。VersionManager::fetchVersionDetails 靠这个信号
    // 接着去取 version.json，漏发就会让整个"下载"流程停在半路：UI 已经进了
    // "下载中"状态，却永远等不到终态，按钮一直是灰的。
    // 失败时发的是上一次成功的结果（可能为空），调用方据此自行判断。
    emit versionManifestFetched(availableVersions);
    reply->deleteLater();
}

void DownloadManager::onVersionJsonReplyFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    // 退出途中不碰任何成员（同别的槽，见 onDownloadReplyFinished 那一条）。
    if (reply == nullptr || m_shuttingDown) {
        return;
    }
    activeDownloads.remove(reply);
    if (reply->error() != QNetworkReply::NoError &&
        retryWithOfficialUrl(reply, QStringLiteral("version_json"),
                             &DownloadManager::onVersionJsonReplyFinished)) {
        return;
    }
    // take 而不是 remove：这条作业记着"该是哪个版本、该是什么字节"，下面要拿它核。
    const MetadataJob job = m_metadataReplies.take(reply);
    m_stallMeters.remove(reply);
    // NOTE: 备用地址**不在这里摘**（2026-09-24 挪到下面那段内容核之后）：内容级失败
    //       （sha1 / JSON / id 对不上）和网络失败一样该退回官方源再试一次，而镜像源
    //       "HTTP 200 + 内容不对"正是它的典型坏法。先摘掉的话那种失败直接成了硬失败
    //       —— 而这条作业的姊妹路径（资源索引）一直是"success == false 就退"的口径。
    if (reply->error() == QNetworkReply::NoError) {
        const QByteArray bytes = reply->readAll();
        // **下回来的东西要核**（以前一个字都不核，只要 JSON 解得开就当成功）。
        // 这一份 json 决定用哪个 Java、读哪个资源索引、启动参数长什么样：半份、或者
        // 别的版本那一份，都会让游戏起不来，而且现场看不出原因（"版本信息缺失"都算不上，
        // 它是一个能解析的、错的 json）。姊妹路径资源索引一直是核 SHA1 的，这里对齐。
        QString problem;
        if (!job.expectedSha1.isEmpty()) {
            const QString actual = QString::fromLatin1(
                QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
            if (actual != job.expectedSha1) {
                problem = QStringLiteral("sha1 对不上（期望 %1，实际 %2）")
                              .arg(job.expectedSha1, actual);
            }
        }
        QJsonParseError parseError;
        const QJsonDocument json = QJsonDocument::fromJson(bytes, &parseError);
        if (problem.isEmpty() && parseError.error != QJsonParseError::NoError) {
            problem = QStringLiteral("不是合法的 JSON：%1").arg(parseError.errorString());
        }
        if (problem.isEmpty() && !job.expectedId.isEmpty()) {
            const QString id = json.object().value(QStringLiteral("id")).toString();
            if (id != job.expectedId) {
                problem = QStringLiteral("里面写的版本号是「%1」，取的是「%2」")
                              .arg(id.isEmpty() ? QStringLiteral("（没有这个字段）") : id,
                                   job.expectedId);
            }
        }
        if (problem.isEmpty()) {
            parseVersionJson(json);
            emit versionJsonFetched(currentVersionJson);
        } else {
            // 内容不对 → 先按"换一条路再试"处理：有备用地址（= 这条走的是镜像）就重发一次，
            // 这次不算失败。重发出去那条带着**同一份** MetadataJob（该是哪个版本、该是什么
            // 字节都还在），所以核 sha1/id 那两步在它回来时照做。
            //
            // NOTE: 作业记录得**放回去**再退：retryWithOfficialUrl() 是 take 出来搬到新回复上
            //       的（它只在"表格里有这条"时才搬），不放回去这一搬就是空的 —— 表现是重发
            //       回来那条不再核 sha1/id，正是这一段要防的东西。
            m_metadataReplies[reply] = job;
            if (retryWithOfficialUrl(reply, QStringLiteral("version_json"),
                                     &DownloadManager::onVersionJsonReplyFinished)) {
                return;
            }
            m_metadataReplies.remove(reply);
            qWarning() << "版本 json 下载回来不对，而且没有备用地址，按失败处理：" << problem;
            emit versionJsonFetched(QJsonObject());
        }
    } else {
        qDebug() << "Network error:" << reply->errorString();
        emit versionJsonFetched(QJsonObject());
    }
    m_officialFallbacks.remove(reply);
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
    // 这两张也必须在**每一条**路上摘干净。键是回复指针，Qt 释放之后下一批下载可能落在
    // 同一个地址上，残留的 lastSignOfLifeMs/bytesSeen 会被算成"那条新回复"的账 ——
    // 表现是一条刚发出去的下载立刻被判成卡死。成功那条支路里已经摘过一次，这里是失败
    // 与"退回官方"那两条支路。
    m_stallMeters.remove(reply);
    m_officialFallbacks.remove(reply);
    reply->deleteLater();
    // NOTE: 计数只对登记过的 reply 生效，上面两道 early return 挡住了没登记的。
    --pendingDownloads;
    if (pendingDownloads < 0) {
        qWarning() << "pendingDownloads went negative, clamping to 0.";
        pendingDownloads = 0;
    }
    m_doneJobs++;

    emitBatchProgress(destPath);
    // 腾出一条连接就补一个排队的作业，然后才判定整批是否下完（顺序不能反：
    // 先判会把"队列里还有、但当下一个都没在飞"误判成整批结束）。
    pumpDownloadQueue();
    checkAllDownloadsFinished();
}

void DownloadManager::onStallWatchdogTick() {
    if (m_shuttingDown) {
        return;
    }
    const qint64 now = m_clock.elapsed();
    // 就地处理会同步触发 finished（abort 那条路就是这么走的）、进而改这几张表，
    // 所以先把要动的攒起来。
    QList<QNetworkReply *> stalled;  // 走镜像、有退路、太慢 -> abort 它换官方那条
    QList<QNetworkReply *> wedged;   // 发出去之后一点动静都没有 -> 判失败
    for (auto it = m_stallMeters.begin(); it != m_stallMeters.end(); ++it) {
        QNetworkReply *reply = it.key();
        StallMeter &meter = it.value();
        // (0) 进账从**回复自己**身上取，不信 downloadProgress 的报数时机。
        //
        //     Qt 的 downloadProgress 不是"到了字节就报"：2026-09-23 用探针量过（Qt 6.10.1），
        //     响应头和 body 同一段到达、之后链子停住的那种形状，它**只在超时那一刻报一次**
        //     （1 KB / 64 KB / 200 KB 都一样，客户端读不读都一样）；连续细流才会逐片报。
        //     `bytesAvailable()` 那边是 494 ms 就有值的 —— 所以头 + 1 KB 然后卡住的镜像回复，
        //     在旧口径下这一跳的进账永远是 0，看门狗对它一声不吭（既不算失速、也不算卡死），
        //     只能干等 Qt 那 30 秒传输超时。改成问回复自己之后，这种回复 10 秒就被判失速、
        //     降级它自己的那个主机、退回官方地址（正是实测里该发生的事）。
        //     产品这条路上中途不读回复（收尾才 readAll），所以 bytesAvailable() 就是"到手的字节"。
        const qint64 availableNow = reply->bytesAvailable();
        if (availableNow > meter.bytesSeen) {
            meter.bytesSeen = availableNow;
            // 有新字节 = 还有动静，"卡死"那条兜底的倒计时清掉。细水长流（镜像 302 到 MinIO
            // 那种 0.85~10 KB/s）靠这句活着，它该由"太慢"那条判据管，不该被兜底掐掉。
            meter.lastSignOfLifeMs = now;
        }
        // (1) 卡死兜底。**不看有没有退路** —— 没有退路的回复以前是没人管的，而它一旦永远
        //     不结束，pendingDownloads 就永远归不了零：整批永不收尾、界面永远停在"下载中"、
        //     连个错误都没有（2026-09-22 那次 422 个文件的导入就这么挂了二十分钟，进度条
        //     停在 1%，用户只能重启）。
        if (now - meter.lastSignOfLifeMs > m_noProgressGiveUpMs) {
            wedged.append(reply);
            continue;
        }
        // (2) 慢镜像那条只对"有退路"的回复成立：abort 掉它 = 换一条路再试；没有退路的
        //     abort 掉就是纯粹把这次下载弄失败。
        //     NOTE: "一个字节都还没收到"的不算数（判据在 countsTowardStallStrike 里，理由和
        //           2026-09-23 那次实测都写在头文件）—— 那种回复的进账天然是 0，会把
        //           "还在建连"误判成"链子太慢"。
        if (m_officialFallbacks.contains(reply)) {
            if (countsTowardStallStrike(meter.bytesSeen, meter.bytesSeen - meter.lastTickBytes,
                                        kStallMinBytesPerTick)) {
                if (++meter.strikes >= kStallStrikesToDemote) {
                    stalled.append(reply);
                }
            } else {
                meter.strikes = 0;
            }
        }
        meter.lastTickBytes = meter.bytesSeen;
    }
    if (!stalled.isEmpty()) {
        // 先降级再 abort：abort 出来的失败会走 onDownloadReplyFinished，那里面退回官方时
        // 要读的就是"已经降级了"这个状态（排队的那几千个资源对象同理，见 startDownload()）。
        //
        // 降级是**按主机**的，而且降的是**这一条回复自己所在的那个主机**：一次 tick 里失速的
        // 回复可能来自好几个 CDN（整合包那条路就是），只降它们各自那个。降错方向的代价是
        // 2026-09-23 那次事故：一个慢 CDN 上的 8 条回复把别的 CDN 一起关了 300 秒。
        // 同一个主机只降一次（顺延本身是幂等的，这里只是别把日志刷满）。
        QSet<QString> demotedHosts;
        for (QNetworkReply *reply : stalled) {
            const QString host = reply->request().url().host();
            if (!host.isEmpty() && !demotedHosts.contains(host)) {
                demotedHosts.insert(host);
                demoteMirrorForSlowDownload(host);
            }
        }
        for (QNetworkReply *reply : stalled) {
            qInfo() << "Aborting a stalled mirror download and retrying it from the official url:"
                    << jobLabel(reply);
            // 只 abort，不在这里收尾：finished 那个槽会拿到 OperationCanceledError 并按
            // "镜像那条没成"处理（退回官方重发，作业还是同一个、不算失败）。
            reply->abort();
        }
    }
    for (QNetworkReply *reply : wedged) {
        qWarning() << "No data for" << (m_noProgressGiveUpMs / 1000) << "seconds, giving up:"
                   << jobLabel(reply);
        failWedgedDownload(reply);
    }
}

QString DownloadManager::jobLabel(QNetworkReply *reply) const {
    if (const QString label = m_metadataReplies.value(reply).label; !label.isEmpty()) {
        return label;
    }
    return activeDownloads.value(reply);
}

void DownloadManager::failWedgedDownload(QNetworkReply *reply) {
    // 元数据作业要单独收：它没进过 pendingDownloads / m_totalJobs / m_doneJobs（见
    // fetchVersionManifest()），走下面那一套会凭空少算一个正在飞的文件作业，让
    // checkAllDownloadsFinished() 提前宣布整批下完 —— 界面报"下载完成"的时候文件还在路上。
    // 连接还是要收干净：摘表 + abort + 释放，这一步和文件作业一模一样。
    if (const MetadataJob job = m_metadataReplies.take(reply); !job.label.isEmpty()) {
        qWarning() << "No data for" << (m_noProgressGiveUpMs / 1000)
                   << "seconds, giving up on the metadata request:" << job.label;
        activeDownloads.remove(reply);
        m_stallMeters.remove(reply);
        m_officialFallbacks.remove(reply);
        // 摘完表再 abort：abort 可能同步回调进 finished 处理函数，那两个槽开头就会把
        // "没登记的回复"忽略掉（元数据那两个槽靠 activeDownloads 认不出已经是自己的回复，
        // 但它们只发信号、不动计数，多发一次是安全的：发的是上一次成功的结果 / 空对象）。
        reply->abort();
        reply->deleteLater();
        // 腾出来的连接要还回去（pumpDownloadQueue 用 activeDownloads.size() 当连接预算），
        // 但不碰计数、也不报进度：这条本来就一个作业都不算。
        pumpDownloadQueue();
        return;
    }

    const QString destPath = activeDownloads.value(reply);
    if (destPath.isEmpty()) {
        return;  // 这一条已经结过账了（比如同一跳里被"慢镜像"那段先处理掉了）
    }
    // 顺序不能反：handleDownloadError() 靠 activeDownloads 反查路径来记失败清单，
    // 所以先报错、再摘表。摘完表之后哪怕 abort() 真的憋出一个迟到的 finished，
    // 那两个处理函数开头也会把"没登记的回复"直接忽略掉 —— 不会重复计数。
    handleDownloadError(reply,
                        QStringLiteral("No data received within %1 s, gave up")
                            .arg(m_noProgressGiveUpMs / 1000));
    activeDownloads.remove(reply);
    expectedSha1s.remove(reply);
    m_stallMeters.remove(reply);
    m_officialFallbacks.remove(reply);
    // abort() 还是要叫一声：能在连接层面收干净最好（收不干净也不影响上面的记账）。
    reply->abort();
    reply->deleteLater();
    --pendingDownloads;
    if (pendingDownloads < 0) {
        qWarning() << "pendingDownloads went negative, clamping to 0.";
        pendingDownloads = 0;
    }
    m_doneJobs++;

    emitBatchProgress(destPath);
    // 和正常收尾同一个顺序：先补一个排队的作业，再判整批是否下完（顺序不能反，
    // 理由见 onDownloadReplyFinished 尾部）。
    pumpDownloadQueue();
    checkAllDownloadsFinished();
}

void DownloadManager::onDownloadReplyProgress(qint64 bytesReceived, qint64 bytesTotal) {
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    // 在看门狗眼皮底下的回复要记两笔：累计字节（判"太慢"用）和"现在还有动静"（判"卡死"
    // 用）。**只有** startDownload()/downloadAssets()/retryWithOfficialUrl() 里登记过的
    // 回复会进这张表。
    if (auto meter = m_stallMeters.find(reply); meter != m_stallMeters.end()) {
        // 取大的那个：正常情况下两个口径一样，但看门狗那一跳是从 bytesAvailable() 取的高水位
        // （理由见 onStallWatchdogTick 的 (0)），别让它被一次迟到的小数字往回拨。
        meter->bytesSeen = qMax(meter->bytesSeen, bytesReceived);
        // 收到字节就是把"卡死"的倒计时清零 —— 一条细水长流、但一直在掉字节的下载（镜像那种
        // 0.85~10 KB/s 的链子）不该被这条兜底掐掉，它该由"太慢"那条判据管。
        meter->lastSignOfLifeMs = m_clock.elapsed();
    }
    QString destPath = activeDownloads.value(reply);
    emit downloadProgress(QFileInfo(destPath).fileName(), bytesReceived, bytesTotal);
    // 单发大文件（41 MB 的客户端 jar）期间也要让进度条动起来，否则界面像是卡住了。
    emitBatchProgress(destPath);
}