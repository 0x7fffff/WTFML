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
#include "DownloadCache.hpp"
#include "DownloadRetryPolicy.hpp"
#include "ResumableDownload.hpp"
#include "StreamedFile.hpp"

#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QHttp1Configuration>
#include <QSaveFile>
#include <QTimer>
#include <QRegularExpression>

#include <cstdio>

#include <QJsonParseError>
#include <QStandardPaths>
#include <QTemporaryFile>

#include "../launcher/LibraryRules.hpp"
#include "../util/AppPaths.hpp"
#include "../util/DownloadDiagnostics.hpp"

namespace {

/// 把这个请求的 HTTP/1.1 通道数抬到我们自己的按主机上限。
///
/// Qt 默认每个主机只开 6 条 HTTP/1.1 连接，第 7 条起进 Qt 内部那个会超时的队列 ——
/// 这正是 `mayIssueAnotherDownloadToHost()` 要挡的东西。Qt 6.5 起按请求可以改这个数
/// （连接是按 URL 缓存的，**第一条**到某个主机的请求决定通道数），所以本类发出去的
/// 每一条请求都要带上它；漏一条就可能让这个主机的连接以 6 条通道建出来，
/// 后面那 6 条下载全被 Qt 排队（症状就是成批 `Operation timed out`）。
void configureHttp1(QNetworkRequest &request) {
    QHttp1Configuration http1;
    http1.setNumberOfConnectionsPerHost(DownloadManager::kMaxConcurrentPerHost);
    request.setHttp1Configuration(http1);
}

DownloadRange rangeOf(QNetworkReply *reply) {
    return {reply->property("rangeFirst").toLongLong(), reply->property("rangeLast").toLongLong(),
            reply->property("rangeTotal").toLongLong(), reply->property("rangeValidator").toByteArray()};
}

QString rangeProblem(QNetworkReply *reply, const DownloadRange &range) {
    if (!range.active()) return {};
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 200 || status == 416) return QStringLiteral("range:unsupported-or-changed");
    if (status != 206) return {};
    const QByteArray expected = "bytes " + QByteArray::number(range.first) + "-" + QByteArray::number(range.last) + "/" + QByteArray::number(range.total);
    if (reply->rawHeader("Content-Range").trimmed() != expected ||
        (!range.validator.isEmpty() && reply->rawHeader("ETag") != range.validator))
        return QStringLiteral("range:source-changed");
    return {};
}

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

// ---------------------------------------------------------------------------
// 下载限速那一个数（2026-10-18，§9.2）
// ---------------------------------------------------------------------------
/// 令牌泵多久放一次字节。100 ms 是"看起来连续"和"定时器开销"之间的折中：Windows 上
/// 这个间隔常常迟到 10~20 ms，所以令牌桶另外留了 1/4 秒的容量（见 burstBytes()）。
constexpr int kSpeedPumpMs = 100;

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

/// 这个目标路径**应该**是一个 zip（jar 就是 zip）。只认后缀，因为这是唯一还能从"我们要
/// 往哪儿写"推出内容的判据。
///
/// **用它的地方只有"一个别的判据都没有"的那两条路**（见 fileAlreadyValid() 与
/// onDownloadReplyFinished() 里各自的 NOTE）：有 sha1 或字节数的时候轮不到它 —— 那两种
/// 判据都比"文件头像不像 zip"强，也没必要多读一次盘。
///
/// NOTE: 只判 `.jar` / `.zip` 两个后缀是**故意收着**的。资源对象（`objects/ab/abcdef…`）
///       没有后缀、版本 json 是 `.json`、皮肤是 `.png` —— 拿"zip 头"去要求它们全是错的。
bool shouldBeZipArchive(const QString &path) {
    return path.endsWith(QLatin1String(".jar"), Qt::CaseInsensitive) ||
           path.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive);
}

/// 文件头是不是 zip 的（`PK\x03\x04` 本地文件头；空档案是 `PK\x05\x06`，一样认）。
///
/// 用来挡住"200 + 一段 HTML 错误页"这一类：那种正文既不是空、字节数也可能对不上任何人
/// 期望值（没人给过期望值），只有前四个字节能一眼说明它不是我们要的东西。
bool looksLikeZipArchive(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray head = file.read(4);
    return head.startsWith("PK\x03\x04") || head.startsWith("PK\x05\x06");
}

/// 这条作业**最终会打到哪个地址**上：首选地址那个主机正被降级、而这条作业知道备用地址时，
/// 就是备用地址（`startDownload()` 里那条"别等它慢 10 秒再退"的改道）。
///
/// ★★ 2026-09-27：这个判断**只能有一处**。它是"请求发给谁"和"按主机那道并发闸问谁"共同的
///    前提，两处各写一遍就会漂开 —— 漂开之后的形状正是修掉的那一个：pumpDownloadQueue()
///    按**首选主机**问额度，而 startDownload() 换了个主机发出去、activeCountForHost() 又把
///    回复记在**新**主机头上 ⇒ 首选主机那一侧永远数到 0、那道闸永远放行，一次泵的 while
///    循环就能往一个按设计只该放 kMaxConcurrentPerHost 条的主机上塞满
///    kMaxConcurrentDownloads(24) 条。而那条路的代价是实测过的：h1.1 主机上 24 条一起
///    发出，30 秒传输超时**从请求建好就开始算** ⇒ 成批 `Operation timed out`，而且改道那
///    一侧 fallback 已经被清空（本来就走官方了），超时就是作业真死。
QString effectiveDownloadUrl(const QString &url, const QString &officialUrl,
                             bool mirrorDemoted) {
    if (mirrorDemoted && !officialUrl.isEmpty() && officialUrl != url) {
        return officialUrl;
    }
    return url;
}

/// 这条失败是不是"根本连不上"（而不是"文件不在 / 内容不对"）。
///
/// 只用来回答一个问题：**官方那条路到底通不通**。所以 HTTP 状态码那一档一概不算，
/// 只有连接层面的错误才算 —— 尤其是 `TimeoutError`：Qt 的 transfer timeout 是"30 秒
/// 一个字节都没来"，在测试员那份日志里，官方 CDN 的每一条失败都是它。
bool unreachableError(QNetworkReply::NetworkError error) {
    switch (error) {
    case QNetworkReply::TimeoutError:
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::ProxyConnectionClosedError:
    case QNetworkReply::ProxyTimeoutError:
        return true;
    default:
        return false;
    }
}

/// 一个地址的"落点身份"：主机（小写）+ 端口。判"302 之后是不是换了一台落点"用它。
///
/// 只比 `host()` 不够：同一个主机名换端口也是换了服务（本地假服务器、反代都是这种），
/// 而 BMCLAPI 的落点里也确有同域不同端口的形状。入口那一侧做"降级"时仍按主机名记
/// （降级是"这只镜像先别用"，不是"这台机器"），只有这里的落点表用完整身份。
QString authorityKey(const QUrl &url) {
    QString key = url.host().toLower();
    if (key.isEmpty()) {
        return key;
    }
    const int port = url.port(-1);
    if (port > 0) {
        key += QLatin1Char(':');
        key += QString::number(port);
    }
    return key;
}

/// 特供版日志用：一条回复的**判定依据**（状态码 / 错误 / 协议 / 关键响应头）。
///
/// 只读。普通构建里调用方的 `DownloadDiag::enabled()` 为假，这个函数不会被调用；
/// 这里仍然不做任何副作用，免得"为了日志改状态"这类问题从这一层长出来。
QString diagReplyFacts(QNetworkReply *reply) {
    QStringList parts;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    parts << QStringLiteral("http=%1").arg(status > 0 ? QString::number(status) : QStringLiteral("-"));
    parts << QStringLiteral("error=%1(%2)")
                 .arg(QString::number(int(reply->error())), reply->errorString());
    const QVariant h2 = reply->attribute(QNetworkRequest::Http2WasUsedAttribute);
    parts << QStringLiteral("proto=%1")
                 .arg(h2.isValid() ? (h2.toBool() ? QStringLiteral("h2") : QStringLiteral("h1"))
                                   : QStringLiteral("?"));
    // 首字节时刻（从发出算）。没有这一格的话，"慢"只能在事后按总耗时猜；有了它，
    // "建连 + 302 + 排队"和"正文只有 2 KB/s"是两行不同的数。
    const qint64 startMs = reply->property("wtfmlStartMs").toLongLong();
    const QVariant firstByte = reply->property("wtfmlFirstByteMs");
    if (startMs > 0 && reply->property("wtfmlMeasureTtfb").toBool()) {
        parts << (firstByte.isValid()
                      ? QStringLiteral("ttfb=%1ms").arg(firstByte.toLongLong() - startMs)
                      : QStringLiteral("ttfb=never"));
    }
    const QString landedHost = reply->property("wtfmlLandedHost").toString();
    if (!landedHost.isEmpty()) {
        parts << QStringLiteral("landed=%1").arg(landedHost);
    }
    if (reply->url() != reply->request().url()) {
        parts << QStringLiteral("finalUrl=%1").arg(DownloadDiag::safeUrl(reply->url()));
    }
    // 这几条是排查源站 / 代理行为的全部依据：拿到半截（Content-Length 对不上）、
    // 被中间设备换掉（Server / Content-Type）、或者要求等一会儿（Retry-After）。
    for (const char *name : {"Content-Length", "Content-Range", "ETag", "Content-Type", "Server",
                             "Retry-After", "Location"}) {
        const QByteArray value = reply->rawHeader(name);
        if (!value.isEmpty()) {
            parts << QStringLiteral("%1=%2")
                         .arg(QString::fromLatin1(name), QString::fromLatin1(value).simplified());
        }
    }
    return parts.join(QLatin1Char(' '));
}

/// 一条回复从发出到现在多久（毫秒）。没登记过起点就回 -1。
qint64 diagElapsedMs(QNetworkReply *reply, qint64 now) {
    const qint64 start = reply->property("wtfmlStartMs").toLongLong();
    return start > 0 ? now - start : -1;
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
    if (DownloadDiag::enabled()) {
        DownloadDiag::line(QStringLiteral("watchdog: mirror host demoted host=%1 until-elapsed=%2ms "
                                          "threshold=%3 bytes/%4ms")
                               .arg(host)
                               .arg(m_mirrorDemotedUntilByHost.value(host))
                               .arg(kStallMinBytesPerTick)
                               .arg(kStallTickMs));
    }
}

void DownloadManager::noteReroutedReplyOutcome(QNetworkReply *reply, bool success,
                                               bool forceUnreachable) {
    const QString mirrorHost = reply->property("wtfmlReroutedFromHost").toString();
    if (mirrorHost.isEmpty()) {
        return;  // 这条本来就走官方 / 本来就在镜像上，跟"改道"无关
    }
    if (success) {
        // 官方还出得动字节 ⇒ 那只镜像主机继续被绕开，计数从头算。
        m_reroutedOfficialFailures.remove(mirrorHost);
        return;
    }
    if (!forceUnreachable && !unreachableError(reply->error())) {
        return;  // 404 / 校验不对 这类：文件的事，不是链路的事
    }
    const int strikes = m_reroutedOfficialFailures.value(mirrorHost) + 1;
    if (strikes < kReroutedFailuresToUndoDemotion) {
        m_reroutedOfficialFailures[mirrorHost] = strikes;
        return;
    }
    m_reroutedOfficialFailures.remove(mirrorHost);
    if (!m_mirrorDemotedUntilByHost.contains(mirrorHost)) {
        return;  // 降级期已经自然过掉了，没什么可撤
    }
    m_mirrorDemotedUntilByHost.remove(mirrorHost);
    qWarning() << "The official download source is unreachable too; undoing the demotion of"
               << mirrorHost << "so queued downloads go back to the mirror.";
    if (DownloadDiag::enabled()) {
        DownloadDiag::line(QStringLiteral("watchdog: demotion undone host=%1 reason=%2")
                               .arg(mirrorHost, reply->errorString()));
    }
}

void DownloadManager::noteDeadRedirectTarget(const QUrl &landed, const QUrl &entry,
                                             const QString &officialFallback) {
    const QString landedKey = authorityKey(landed);
    const QString entryKey = authorityKey(entry);
    if (landedKey.isEmpty() || entryKey.isEmpty() || landedKey == entryKey) {
        return;  // 没有 302、或者根本还在入口那一跳：这一笔不是"落点死了"
    }
    const QString mirrorHost = entry.host();
    const qint64 now = m_clock.elapsed();
    m_deadRedirectHosts[landedKey] = now + kDeadRedirectHostBanMs;
    // 窗口是一条先进先出的队列：只留最近 kDeadRedirectWindowMs 里那几条。
    while (!m_deadRedirectHits.isEmpty()
           && now - m_deadRedirectHits.first() > kDeadRedirectWindowMs) {
        m_deadRedirectHits.removeFirst();
    }
    m_deadRedirectHits.append(now);
    if (!officialFallback.isEmpty()) {
        m_officialProbeUrl = officialFallback;
    }
    if (DownloadDiag::enabled()) {
        DownloadDiag::line(QStringLiteral("dead redirect target: landed=%1 entry=%2 hits=%3/%4")
                               .arg(landedKey, entryKey)
                               .arg(m_deadRedirectHits.size())
                               .arg(kDeadRedirectHitsToProbe));
    }
    if (m_deadRedirectHits.size() < kDeadRedirectHitsToProbe) {
        return;
    }
    // 短时间内连着这么多条落在死节点上：**先探一下官方**再决定整条镜像要不要降级。
    // 2026-10-17 的两种真实网络都要照顾到 —— 本机校园网官方 0.4~1.6 秒一个对象（该切），
    // 测试员那条系统代理网络上官方整条不通（不该切，继续用镜像）。探测失败的话这一层
    // 什么都不做，和从前一样按作业级重排。
    probeOfficialRoute(mirrorHost);
}

void DownloadManager::probeOfficialRoute(const QString &mirrorHost) {
    if (mirrorHost.isEmpty() || m_officialProbeInFlight || m_officialProbeUrl.isEmpty()) {
        return;
    }
    const qint64 now = m_clock.elapsed();
    if (now < m_officialProbeFailedUntilMs) {
        return;  // 刚探过、没通：两分钟之内不再拿真实请求去撞
    }
    m_officialProbeInFlight = true;
    // HEAD：只要拿到 2xx 就说明这条路"接得上、答得快"，不必拉正文。官方资源 CDN 实测支持
    // （curl -I 200）。10 秒没答话就按"不通"处理 —— 探测本身也算延迟。
    QNetworkRequest request{QUrl(m_officialProbeUrl)};
    configureHttp1(request);
    request.setTransferTimeout(10000);
    QNetworkReply *probe = networkManager->head(request);
    if (DownloadDiag::enabled()) {
        DownloadDiag::line(QStringLiteral("official probe send url=%1 for mirror=%2")
                               .arg(DownloadDiag::safeUrl(m_officialProbeUrl), mirrorHost));
    }
    connect(probe, &QNetworkReply::finished, this, [this, probe, mirrorHost] {
        m_officialProbeInFlight = false;
        const int status = probe->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool reachable =
            probe->error() == QNetworkReply::NoError
            && (status == 0 || (status >= 200 && status < 300));
        const QString errorText = probe->errorString();
        probe->deleteLater();
        if (reachable) {
            // 官方探得通 ⇒ 把镜像入口降级 5 分钟：排队里的几千个作业从下一拍起直接走官方。
            // 之后官方要是也连不上，noteReroutedReplyOutcome() 那条会把它撤销。
            demoteMirrorForSlowDownload(mirrorHost);
            m_deadRedirectHits.clear();
            qInfo() << "The mirror keeps redirecting to dead nodes, while the official source"
                       " answers; using the official download source for the next"
                    << kMirrorDemoteMs / 1000 << "seconds.";
        } else {
            // 官方也不通（典型：系统代理那条网络）：不降级、继续用镜像，两分钟后再看。
            m_officialProbeFailedUntilMs = m_clock.elapsed() + kOfficialProbeCooldownMs;
            m_deadRedirectHits.clear();
            qInfo() << "The official source is unreachable as well; keeping the mirror:"
                    << errorText;
        }
        if (DownloadDiag::enabled()) {
            DownloadDiag::line(QStringLiteral("official probe result reachable=%1 status=%2 "
                                              "error=%3")
                                   .arg(reachable ? QStringLiteral("yes") : QStringLiteral("no"))
                                   .arg(status)
                                   .arg(errorText));
        }
    });
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
    return mayIssueAnotherDownload(activeCount, queuedCount, kMaxConcurrentDownloads);
}

bool DownloadManager::mayIssueAnotherDownload(int activeCount, int queuedCount, int cap) {
    // 排空的队列不用问；到顶了不管还排着多少条都不发 —— 后一半是这条函数存在的全部理由。
    return queuedCount > 0 && activeCount < cap;
}

void DownloadManager::setMaxConcurrentDownloads(int count) {
    // 归一化在判据层（唯一出处，见 DownloadConcurrency.hpp）：越界的值回默认，不夹到边上。
    const int normalized = DownloadConcurrency::normalize(count);
    if (normalized == m_maxConcurrentDownloads) {
        return;
    }
    m_maxConcurrentDownloads = normalized;
    if (DownloadDiag::enabled()) {
        DownloadDiag::line(QStringLiteral("concurrency set request=%1 normalized=%2 inflight=%3 "
                                          "queued=%4")
                               .arg(count)
                               .arg(normalized)
                               .arg(activeDownloads.size())
                               .arg(m_pendingJobs.size()));
    }
    // 调小时，已经在飞的请求不撤回，pump 会看见“已超过上限”而什么都不做；
    // 调大时，这一次 pump 会立即把新空出的槽位补上。否则设置页虽然说“立即生效”，
    // 队列却必须等到下一条回复才会加速，用户看到的行为与文案不一致。
    pumpDownloadQueue();
}

void DownloadManager::setSpeedLimitBytesPerSecond(qint64 bytesPerSecond) {
    // 归一化在判据层（唯一出处，见 DownloadSpeedLimit.hpp）：0 = 不限；越界回 0。
    const qint64 normalized = DownloadSpeedLimit::normalize(bytesPerSecond);
    if (normalized == m_speedLimitBytesPerSecond) {
        return;
    }
    m_speedLimitBytesPerSecond = normalized;
    if (normalized > 0) {
        // 从零开始：用户刚设完不该先看到"猛冲一下"（burst 只用来吸收定时器抖动）。
        m_speedTokens = 0;
        m_speedLastRefillMs = m_clock.elapsed();
        m_speedPump->start();
    } else {
        m_speedPump->stop();
        // 从"限速"切回"不限"：攒在缓冲区里的那些**当场**读完，不等到下一拍。
        // 快照的理由：写盘出错时下面会 abort，abort 可能同步走 finished、改这张表。
        const QList<QNetworkReply *> waiting = m_throttledReplies.values();
        m_throttledReplies.clear();
        for (QNetworkReply *reply : waiting) {
            const auto stream = m_streamedFiles.value(reply);
            if (!stream) {
                continue;
            }
            stream->drain(reply);
            if (!stream->error.isEmpty()) {
                reply->abort();
            }
        }
        m_speedTokens = 0;
    }
    // 分段作业（大于 4 MiB 那些）各自有一份内部下载器，绕在我们这张表之外 —— 已经在跑的
    // 也要当场改，不然"设置里改成 1 MB/s 了，大文件还在满速"（见 ResumableDownload 那个 setter）。
    const QList<ResumableDownload *> segmentTasks = m_segmentJobs.keys();
    for (ResumableDownload *task : segmentTasks) {
        if (task != nullptr) {
            task->setSpeedLimitBytesPerSecond(normalized);
        }
    }
    if (DownloadDiag::enabled()) {
        DownloadDiag::line(QStringLiteral("speedlimit set request=%1 normalized=%2 inflight=%3 "
                                          "waiting=%4")
                               .arg(bytesPerSecond)
                               .arg(normalized)
                               .arg(activeDownloads.size())
                               .arg(m_throttledReplies.size()));
    }
}

void DownloadManager::onSpeedPumpTick() {
    if (m_shuttingDown || m_speedLimitBytesPerSecond <= 0) {
        return;
    }
    const qint64 now = m_clock.elapsed();
    const qint64 elapsed = qMax<qint64>(0, now - m_speedLastRefillMs);
    m_speedLastRefillMs = now;
    const double capacity = double(DownloadSpeedLimit::burstBytes(m_speedLimitBytesPerSecond));
    m_speedTokens = qMin(capacity, m_speedTokens + double(m_speedLimitBytesPerSecond) *
                                                   double(elapsed) / 1000.0);
    if (m_throttledReplies.isEmpty()) {
        // 没有人在等：余额也别攒（不然下一批一开始就会冲一个 burst）。
        m_speedTokens = 0;
        return;
    }
    // 列"确实还有字节等着"的那些。两拍之间回复可能已经结束 —— 结束那一路自己会摘表，
    // 这里再判一次 isReadable，双保险（摘漏了的也不会被 read 到一个关掉的设备）。
    QList<QNetworkReply *> waiting;
    const QList<QNetworkReply *> registered = m_throttledReplies.values();
    waiting.reserve(registered.size());
    for (QNetworkReply *reply : registered) {
        if (!reply->isReadable() || reply->bytesAvailable() <= 0) {
            m_throttledReplies.remove(reply);
            continue;
        }
        waiting.append(reply);
    }
    // 平均分摊：每个等着的人最多拿"余额 ÷ 还剩几个人"。谁手上没那么多字节，余量自然
    // 落到后面几个人身上（remaining 是先减后用的）。一轮就够 —— 100 ms 之后再来一拍。
    int remaining = waiting.size();
    for (QNetworkReply *reply : waiting) {
        if (remaining <= 0 || m_speedTokens < 1.0) {
            break;
        }
        const qint64 share = qint64(m_speedTokens / double(remaining));
        --remaining;
        if (share <= 0) {
            continue;
        }
        const auto stream = m_streamedFiles.value(reply);
        if (!stream) {
            m_throttledReplies.remove(reply);
            continue;
        }
        const qint64 taken = stream->drainUpTo(reply, share);
        m_speedTokens -= double(taken);
        if (!stream->error.isEmpty()) {
            m_throttledReplies.remove(reply);
            reply->abort();
        } else if (reply->bytesAvailable() <= 0) {
            m_throttledReplies.remove(reply);
        }
    }
}

bool DownloadManager::mayIssueAnotherDownloadToHost(int activeForHost, bool hostKnownHttp2) {
    // 已知 h2 的主机不受这条管（一条 TCP 连接上多路复用，放几条归全局上限说）。
    // 还不知道的主机按 Qt 每主机的连接上限守着 —— 多发的那些会进 Qt 内部那个会超时的队列。
    return hostKnownHttp2 || activeForHost < kMaxConcurrentPerHost;
}

DownloadManager::DownloadManager(QObject *parent, NetworkManagerFactory networkFactory)
    : QObject(parent), networkManager(networkFactory ? networkFactory(this) : new QNetworkAccessManager(this)),
      m_networkFactory(std::move(networkFactory)), pendingDownloads(0) {
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
    // 限速泵**先不跳**：没设上限时它连一次空转都不该有（出厂就是不限速）。
    m_speedPump = new QTimer(this);
    m_speedPump->setInterval(kSpeedPumpMs);
    connect(m_speedPump, &QTimer::timeout, this, &DownloadManager::onSpeedPumpTick);

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
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    activeDownloads.clear();
    m_streamedFiles.clear();
    m_replyGenerations.clear();
    // 分段作业是子对象：先断信号（别让 cancel 的同步回调进来）再取消。已经获准原子提交的
    // 那一个还会在后台把文件写完，但它已经不会再碰这个对象。
    for (auto *task : m_segmentJobs.keys()) {
        disconnect(task, nullptr, this, nullptr);
        task->cancel();
    }
    m_segmentJobs.clear();
    // 队列里的作业一个请求都没发出去，abort 那套对它们无效，只能直接丢掉。
    m_pendingJobs.clear();
    m_officialFallbacks.clear();
    m_stallMeters.clear();
    m_metadataReplies.clear();
    // 限速泵攥着的都是上面刚 abort 过的回复：表本身只是指针，清掉就行（对象都快没了，
    // 但留着一份悬空指针没有半点用处）。
    m_throttledReplies.clear();
}

void DownloadManager::fetchVersionManifest() {
    const QString officialUrl = officialVersionManifestUrl();
    QUrl url(endpoint(officialUrl));
    QNetworkRequest request(url);
    configureHttp1(request);
    // 元数据请求也要有超时：下载期间 UI 会禁用按钮，卡死的连接会让它永远禁用下去。
    request.setTransferTimeout(30000);
    QNetworkReply *reply = networkManager->get(request);
    reply->setProperty("wtfmlStartMs", m_clock.elapsed());
    if (DownloadDiag::enabled()) {
        DownloadDiag::line(QStringLiteral("manifest send url=%1 official=%2 mirror=%3")
                               .arg(DownloadDiag::safeUrl(url), DownloadDiag::safeUrl(officialUrl),
                                    url.toString() == officialUrl ? QStringLiteral("no")
                                                                  : QStringLiteral("yes")));
    }
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
            configureHttp1(request);
            request.setTransferTimeout(30000);
            QNetworkReply *reply = networkManager->get(request);
            reply->setProperty("wtfmlStartMs", m_clock.elapsed());
            if (DownloadDiag::enabled()) {
                DownloadDiag::line(QStringLiteral("version-json send id=%1 url=%2 expectedSha1=%3")
                                       .arg(versionId, DownloadDiag::safeUrl(effectiveUrl),
                                            sha1FromVersionUrl(version.url).isEmpty()
                                                ? QStringLiteral("-")
                                                : sha1FromVersionUrl(version.url)));
            }
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

void DownloadManager::setFlaggedPaths(const QStringList &paths) { m_flaggedPaths = paths; }

void DownloadManager::beginBatch() {
    // 上一批还没收尾就又被开始了，先替它收尾，否则它的完成信号会丢。
    if (m_batchOpen) {
        qWarning() << "beginBatch() called while a batch was still open, closing it first.";
        endBatch();
    }
    // 代次加一：这条线**之后**发出去的回复才算这一批的。上一批还在飞的那几条回来时
    // 因此会被认出来（见 m_batchGeneration 的说明）。
    ++m_batchGeneration;
    m_totalJobs = 0;
    m_doneJobs = 0;
    m_failedJobs = 0;
    m_skippedJobs = 0;
    m_failedPaths.clear();
    m_failedJobInfos.clear();
    // "体检点名的那几条"只对**上一批**成立（见 setFlaggedPaths）：新的一批谁都没点名。
    m_flaggedPaths.clear();
    m_currentFile.clear();
    m_batchEmitted = false;
    m_batchOpen = true;
    if (DownloadDiag::enabled()) {
        DownloadDiag::line(QStringLiteral(
                               "batch begin gen=%1 mirror=%2 mirrorPreferred=%3 cap=%4 "
                               "official-manifest=%5")
                               .arg(m_batchGeneration)
                               .arg(m_useMirror ? QStringLiteral("on") : QStringLiteral("off"))
                               .arg(mirrorPreferred() ? QStringLiteral("yes") : QStringLiteral("no"))
                               .arg(m_maxConcurrentDownloads)
                               .arg(officialVersionManifestUrl()));
    }
}

void DownloadManager::endBatch() {
    m_batchOpen = false;
    if (DownloadDiag::enabled()) {
        DownloadDiag::line(QStringLiteral("batch end gen=%1 total=%2 done=%3 failed=%4 skipped=%5 "
                                          "inflight=%6 queued=%7")
                               .arg(m_batchGeneration)
                               .arg(m_totalJobs)
                               .arg(m_doneJobs)
                               .arg(m_failedJobs)
                               .arg(m_skippedJobs)
                               .arg(pendingDownloads)
                               .arg(m_pendingJobs.size()));
    }
    // 这一个调用是关键：所有文件都已存在于磁盘、一个网络作业都没发出去的批次
    // 也会在这里被正常收尾，不会卡住。
    checkAllDownloadsFinished();
}

void DownloadManager::abandonBatch() {
    if (DownloadDiag::enabled()) {
        DownloadDiag::line(QStringLiteral("batch abandoned gen=%1 total=%2 done=%3 failed=%4 "
                                          "skipped=%5 inflight=%6 queued=%7")
                               .arg(m_batchGeneration)
                               .arg(m_totalJobs)
                               .arg(m_doneJobs)
                               .arg(m_failedJobs)
                               .arg(m_skippedJobs)
                               .arg(pendingDownloads)
                               .arg(m_pendingJobs.size()));
    }
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
    // 这一批的交代（"点名的那几条"，见 setFlaggedPaths）跟着作废：批次都放弃了，它不该管到
    // 下一批上去（下一批可能是**不在批次里**的那几条裸 downloadFile）。
    m_flaggedPaths.clear();
}

void DownloadManager::cancelBatch() {
    if (DownloadDiag::enabled()) {
        DownloadDiag::line(QStringLiteral("batch cancelled gen=%1 oldGen=%2 total=%3 done=%4 "
                                          "failed=%5 skipped=%6 inflight=%7 queued=%8")
                               .arg(m_batchGeneration + 1)
                               .arg(m_batchGeneration)
                               .arg(m_totalJobs)
                               .arg(m_doneJobs)
                               .arg(m_failedJobs)
                               .arg(m_skippedJobs)
                               .arg(pendingDownloads)
                               .arg(m_pendingJobs.size()));
    }
    // 1. 还排着队的**不发了**。几百个作业留着的话，用户按了取消之后它们会一批批地继续
    //    发出去（每一次有回复收尾都会泵一遍队列），看着就是"取消了还在下"。
    m_pendingJobs.clear();
    // 2. 封口 + 加代次。这两句合起来的**唯一**意思是"这一批到此为止、之后回来的所有回复
    //    都与我无关"：m_batchEmitted 挡住收尾那一次发射，代次让已经在飞的那几条
    //    （下面 abort() 逼回来的那些）在 `onDownloadReplyFinished` / `handleDownloadError`
    //    里被认成 stale —— 不记账、不进失败清单、不发 downloadFinished、
    //    **也不退回官方地址重发**（retryWithOfficialUrl 那头看的就是同一件事）。
    m_batchOpen = false;
    m_batchEmitted = true;
    ++m_batchGeneration;
    m_currentFile.clear();
    // 同 abandonBatch()：取消了，"点名的那几条"也一起作废（见 setFlaggedPaths）。
    m_flaggedPaths.clear();
    // 3. ★ 已经在飞的那几条**真的掐掉**。必须在加完代次之后：abort() 会（在某些后端上同步）
    //    触发 finished，那些回调走的正是上面那句"stale 那条路"。
    //    NOTE: 先拷一份键再 abort —— 回调里会 `activeDownloads.remove(reply)`，边遍历边改
    //          Qt 的哈希表是未定义行为。回复本身不会被立刻 delete（收尾那几步都走
    //          `deleteLater()`），所以这一圈里那些指针一直是有效的。
    const QList<QNetworkReply *> inflight = activeDownloads.keys();
    for (QNetworkReply *reply : inflight) {
        reply->abort();
    }
    // 4. 分段作业没有回复可 abort，逐个取消。cancel() 返回 true 时**不会**再发
    //    finished（见 ResumableDownload::cancel），所以这里必须当场摘表并 deleteLater；
    //    返回 false = 已经进入原子提交（QSaveFile 的 commit），只能让它做完 —— 批次已经
    //    封口、代次也加了，它回来时只会清理，不会记账。isBusy() 会一直为真到提交结束，
    //    这是对的：文件还在往盘上写。
    const QList<ResumableDownload *> segmented = m_segmentJobs.keys();
    for (ResumableDownload *task : segmented) {
        const bool cancelled = task->cancel();
        if (DownloadDiag::enabled()) {
            DownloadDiag::line(QStringLiteral("segment cancel dest=%1 accepted=%2")
                                   .arg(QDir::toNativeSeparators(
                                            m_segmentJobs.value(task).destinationPath),
                                        cancelled ? QStringLiteral("yes") : QStringLiteral("no")));
        }
        if (cancelled) {
            m_segmentJobs.remove(task);
            task->deleteLater();
        }
    }
    // 5. 队列空了、批次封口了、在飞的那几条也被逼着收尾了 —— 这时候 isBusy() 必然回 false，
    //    界面（底栏那一格、下载页那几颗按钮）下一轮就会解禁。
}

QList<DownloadManager::FailedJob> DownloadManager::failedJobs() const { return m_failedJobInfos; }

bool DownloadManager::isBusy() const {
    // 队列也得算："还排着几百个没发出去"当然还是忙 —— 漏掉它的表现是界面解禁、
    // 用户以为下载完了，然后再点一次又叠一批上去。
    // 分段作业没有 QNetworkReply（不计 pendingDownloads），漏掉它会表现为"只剩大文件时
    // 界面显示空闲、取消按钮失效"。
    return m_batchOpen || pendingDownloads > 0 || !m_pendingJobs.isEmpty()
           || !m_segmentJobs.isEmpty();
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
    // ★ 体检点过名的那几条：不看盘上这份，直接当"要下"（见 setFlaggedPaths 的说明）。
    //   排在最前面 —— 这一句是"这条别人已经替我判过了"，比下面任何一条判据都靠前。
    if (mode == VerifyMode::AlwaysDownload || m_flaggedPaths.contains(path)) {
        return false;
    }
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        return false;
    }
    if (info.size() == 0) {
        // **0 字节的文件永远不算数**，哪一档都不算（2026-09-25 补）。
        //
        // NOTE: 这一条要在 ExistsOnly 前面。以前 ExistsOnly 是无条件 true，而"资源对象"
        //       那几千个就是这么登记的（见 downloadAssets）—— 于是写盘写到一半被中断留下
        //       的那个 0 字节文件会被永久当成"这个我已经下过了"，而且**再也不会重下**。
        //       一份真资源的字节数不可能是 0，所以这条判据不会误伤任何好东西。
        return false;
    }
    if (mode == VerifyMode::ExistsOnly) {
        // ★ "文件在就算好"这一档里，**该是 zip 的那一类还要真的是 zip**（2026-09-26 补）。
        //
        //   这一档喂的是两类东西：资源对象（文件名就是 sha1、没有后缀 —— 上面那条判据对
        //   它们一个字都不管）和**加载器安装器 jar**（`installs/forge-…-installer.jar`）。
        //   后者是全仓唯一"既没有 sha1、也没有字节数"的下载，于是它只有"存在"这一条判据。
        //   一份 200 + HTML 错误页要是落在这儿，`fileAlreadyValid()` 从此每次都点头 ⇒
        //   用户**永远装不上那个加载器**，而且每次都在同一个地方失败（重下也没用，
        //   因为根本不会重下）。前四个字节就是这时候唯一还说得上话的东西。
        return !shouldBeZipArchive(path) || looksLikeZipArchive(path);
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
                                   VerifyMode mode, const QString &officialUrl, const DownloadRange &range) {
    // 所有下载都走这条排队路（理由见 mayIssueAnotherDownload）：作业少的时候队列当场就
    // 把它发出去，和"直接发"一模一样；作业一多就只有 kMaxConcurrentDownloads 条在飞。
    scheduleFile(url, destinationPath, expectedSha1, expectedSize, mode, officialUrl, range);
}

void DownloadManager::scheduleFile(const QString &url, const QString &destinationPath,
                                   const QString &expectedSha1, qint64 expectedSize,
                                   VerifyMode mode, const QString &officialUrl, const DownloadRange &range) {
    // 每个作业都重置一次"已发射"，不管是跳过还是真下。不带批次的用法（一次 fetch、
    // 一次 assets）没有 beginBatch 来重置它，只能靠这里。
    m_batchEmitted = false;

    const QString fileName = QFileInfo(destinationPath).fileName();
    if (url.isEmpty() || destinationPath.isEmpty()) {
        qWarning() << "Refused download with empty url or destination:" << url << destinationPath;
        // 这一条**进分母**：它是"这一批该拿的、没拿到的"，进度条上它就是那个没完成的一格。
        // （和下面跳过那一支刚好相反 —— 那一支是"压根不需要拿"。）
        m_totalJobs++;
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
        // ★★ 盘上已经好好的那几个**只记 skipped、不进 done/total**（2026-10-15 改）
        //
        //   从前这里写的是「跳过也要计入 done/total，否则进度条的分母会对不上，最后一批还会
        //   少发一次信号」—— 后半句本来就是假的（整批结束的判据是 `pendingDownloads` /
        //   `m_pendingJobs` / `m_batchOpen`，见 checkAllDownloadsFinished，一个都不碰这两个
        //   计数器），前半句则是那个坑本身：分母因此变成"这个实例一共有多少文件"。
        //
        //   用户在真机上撞的就是它：一个装好的实例点一次「补全文件」，先看到进度条在开跑那
        //   一瞬间就满了（几千个"已完成"是问盘问出来的），收尾再报一句
        //   「n 个文件已全部就绪（其中 n-1 个本来就存在，已跳过）」—— 读起来像"补全了一个
        //   文件"，而那一个其实是永远要重取的资源索引（现在也不计了，见 downloadAssets）。
        //   用户要看的是**缺的那几个下到哪儿了**，那就让分母只剩它们。
        m_skippedJobs++;
        emit downloadFinished(fileName, true, QString());
        emitBatchProgress(destinationPath);
        checkAllDownloadsFinished();
        return;
    }

    m_totalJobs++;
    // 排进队列之后立刻试着发一次：队列是空的、连接也有富余的时候，这一句就把它发出去了
    // （所以"只下一两个文件"的那些调用点感觉不到队列存在）。
    const bool segmented = m_segmentEnabled && segmentedEligible(url, expectedSha1, expectedSize, range);
    m_pendingJobs.enqueue(
        PendingJob{url, destinationPath, expectedSha1, expectedSize, officialUrl,
                   QUrl(url).host(), 0, 0, range, segmented});
    pumpDownloadQueue();
}

void DownloadManager::pumpDownloadQueue() {
    // 判据用 activeDownloads.size() 而不是另开一个计数器：它是"当下真在飞的作业"
    // 的唯一事实 —— 别的下载（资源索引、客户端 jar）占着连接时这里自然少发几个。
    //
    // 两条判据都要过：全局还有位置（mayIssueAnotherDownload）+ **这个主机**还有位置
    // （mayIssueAnotherDownloadToHost）。后面那条是全局上限调大之后必须补上的：全局放
    // 24 条（现在是**用户在设置里挑的那一档**，§9.2），但一个还没证明自己走 h2 的主机
    // 只能放 6 条，否则多出来的会进 Qt 内部那个会超时的队列（见那两条常量的注释）。
    //
    // 队头那个作业的主机满了、**不等于**后面没有了：整合包导入的队列就是几个 CDN 交错排的
    // （见 preferredUrlOrder），队头这条排不上而后面那条能排上的情况很常见。所以这里跳过
    // 去后面的作业找（i 往前走），而不是一条堵住就整个不发了。跳过只是换一个发送次序，
    // 不丢作业、也不改变谁先谁后之外的任何事。
    int i = 0;
    while (i < m_pendingJobs.size()) {
        // 分段作业内部有 4 条连接，按真实连接数占全局额度；它不是 QNetworkReply，
        // 所以 activeDownloads 里看不到，必须单独加回来。
        const int streamActive = activeDownloads.size() + segmentConnections();
        if (!mayIssueAnotherDownload(streamActive, m_pendingJobs.size(), m_maxConcurrentDownloads)) break;
        const PendingJob &candidate = m_pendingJobs.at(i);
        if (candidate.readyAtMs > m_clock.elapsed()) { ++i; continue; }
        // ★★ 2026-09-27：问的是**这条作业真正会打到**的那个主机，不是它首选地址那个主机。
        //    首选主机正被降级时 startDownload() 会当场改道，而回复是按改道后的主机记账的
        //    （见 activeCountForHost 里那条 NOTE）—— 按首选主机问额度的话，那道闸对它永远是
        //    放行的（那一侧一条都不会再记上去），一次泵就能把备用主机塞满 24 条。
        //    判据和 startDownload() 里那条**同源**：都走 effectiveDownloadUrl()。
        const QString targetUrl =
            effectiveDownloadUrl(candidate.url, candidate.officialUrl,
                                 mirrorDemotedFor(candidate.host));
        const QString targetHost = QUrl(targetUrl).host();
        const bool hostHttp2 = m_http2Hosts.contains(targetHost);
        bool segmented = candidate.segmented && m_segmentEnabled;
        if (segmented) {
            // 分段作业的 4 条连接走 ResumableDownload 自己的 QNAM，不占外层这个 QNAM 的
            // 每主机额度（kMaxConcurrentPerHost 守的是 Qt 内部那个会超时的队列），所以
            // 这里只按全局连接预算判断。名额/预算不够就**当场按普通下载发出去**：
            // 让队头的大文件干等，会把后面几千个小文件一起卡住。
            // 名额跟着并发档位走（见 maxSegmentedJobsFor）；另加一条下限保护：
            // 全局只有 4~7 条连接时最多放一个分段作业，否则一条大文件就把整条队列占满。
            const int maxSegments = qMin(
                maxSegmentedJobsFor(m_maxConcurrentDownloads),
                qMax(1, m_maxConcurrentDownloads / ResumableDownload::kPartConcurrency));
            const bool budget = m_segmentJobs.size() < maxSegments
                                && streamActive + ResumableDownload::kPartConcurrency
                                       <= m_maxConcurrentDownloads;
            const auto scheme = QUrl(targetUrl).scheme();
            const bool schemeOk = scheme == QStringLiteral("http") || scheme == QStringLiteral("https");
            segmented = budget && schemeOk;
        }
        // ★ 降级成普通下载（候选是分段作业、但名额/预算不够）**也要过每主机那道闸**：
        //   它走的是外层这个 QNAM，多出来的会进 Qt 内部那个会超时的队列 —— 正是
        //   kMaxConcurrentPerHost 要挡的东西。这里跳过的是"这一条暂时发不出去"，
        //   不是"整队停住"：i 往前走，后面的作业照发（和上面那条循环同一个理由）。
        if (!segmented
            && !mayIssueAnotherDownloadToHost(activeCountForHost(targetHost), hostHttp2)) {
            ++i;
            continue;
        }
        // takeAt 之后上面那个引用就悬了，所以先拷出来再删。
        const PendingJob job = m_pendingJobs.takeAt(i);
        if (segmented) {
            startSegmentedDownload(job);
        } else {
            startDownload(QUrl(job.url), job.destinationPath, job.expectedSha1, job.expectedSize,
                          job.officialUrl, job.retryAttempts, job.range);
        }
    }
}

void DownloadManager::requeueDownloadJob(const PendingJob &job) {
    // 排头，不是队尾。理由和"为什么不会堵住别人"见头文件。
    m_pendingJobs.prepend(job);
}

bool DownloadManager::segmentedEligible(const QString &url, const QString &expectedSha1,
                                        qint64 expectedSize, const DownloadRange &range) {
    // 分段请求本身（range.active()）绝不能再被升级一次。
    if (range.active()) return false;
    if (expectedSize < ResumableDownload::kMinimumBytes ||
        expectedSize > ResumableDownload::kMaximumBytes) {
        return false;
    }
    if (expectedSha1.isEmpty()) return false;
    const QUrl target(url);
    return target.scheme() == QStringLiteral("http") || target.scheme() == QStringLiteral("https");
}

int DownloadManager::segmentConnections() const {
    return m_segmentJobs.size() * ResumableDownload::kPartConcurrency;
}

void DownloadManager::startSegmentedDownload(const PendingJob &job) {
    const QString targetUrl =
        effectiveDownloadUrl(job.url, job.officialUrl, mirrorDemotedFor(job.host));
    const bool rerouted = (targetUrl != job.url);

    auto *task = new ResumableDownload(this, m_networkFactory);
    // 限速要跟着这一条走：它在 start() 里会建自己那份下载器（见那个 setter 的说明）。
    task->setSpeedLimitBytesPerSecond(m_speedLimitBytesPerSecond);
    SegmentJob record;
    record.url = targetUrl;
    record.destinationPath = job.destinationPath;
    record.expectedSha1 = job.expectedSha1;
    record.expectedSize = job.expectedSize;
    // 已经因为镜像降级改道到官方了，"退回官方"这条退路就用掉了（和 startDownload 同源）。
    record.officialUrl = rerouted ? QString() : job.officialUrl;
    record.host = QUrl(targetUrl).host();
    record.officialRetried = job.officialRetried;
    record.plainRetried = job.plainRetried;
    record.generation = m_batchGeneration;
    // ★ 先登记、先连信号，再 start()：start() 在本地准备失败时会**同步**发 finished，
    //   晚一步就会漏掉那一次失败（那条作业会永远占着名额）。
    m_segmentJobs.insert(task, record);

    connect(task, &ResumableDownload::progress, this, [this, task](qint64 done, qint64 total) {
        const auto it = m_segmentJobs.constFind(task);
        if (it == m_segmentJobs.constEnd() || it->generation != m_batchGeneration) return;
        emit downloadProgress(QFileInfo(it->destinationPath).fileName(), done, total);
        emitBatchProgress(it->destinationPath);
    });
    connect(task, &ResumableDownload::finished, this,
            [this, task](bool success, const QString &error) {
                finishSegmentedJob(task, success, error);
            });

    if (DownloadDiag::enabled()) {
        DownloadDiag::line(QStringLiteral("segment start gen=%1 url=%2 dest=%3 sha1=%4 size=%5 "
                                          "parts=%6 official=%7")
                               .arg(m_batchGeneration)
                               .arg(DownloadDiag::safeUrl(targetUrl),
                                    QDir::toNativeSeparators(job.destinationPath),
                                    job.expectedSha1.isEmpty() ? QStringLiteral("-")
                                                               : job.expectedSha1)
                               .arg(job.expectedSize)
                               .arg(ResumableDownload::kPartConcurrency)
                               .arg(job.officialUrl.isEmpty()
                                        ? QStringLiteral("-")
                                        : DownloadDiag::safeUrl(job.officialUrl)));
    }

    // ResumableDownload 要求绝对路径；记录里仍保留调用方原来那条（信号/失败清单用它）。
    m_startingSegment = true;
    task->start(QUrl(targetUrl), QFileInfo(job.destinationPath).absoluteFilePath(),
                job.expectedSha1, job.expectedSize, m_allowOverwrite,
                m_rangeCacheRoot.isEmpty() ? DownloadCache::root() : m_rangeCacheRoot);
    m_startingSegment = false;
}

void DownloadManager::finishSegmentedJob(ResumableDownload *task, bool success,
                                         const QString &error) {
    if (!task) return;
    if (m_shuttingDown) {
        task->deleteLater();
        return;
    }
    // start() 同步发出来的 finished：等这一层调用栈退出去再结账，避免在 pump 的循环里
    // 再次进 pump、改动 m_pendingJobs 的当前位置。
    if (m_startingSegment) {
        QTimer::singleShot(0, this, [this, task, success, error] {
            finishSegmentedJob(task, success, error);
        });
        return;
    }
    const auto it = m_segmentJobs.find(task);
    if (it == m_segmentJobs.end()) {
        // 已经取消（cancel() 不会发 finished）或重复回调。
        task->deleteLater();
        return;
    }
    const SegmentJob record = *it;
    m_segmentJobs.erase(it);

    const QString fileName = QFileInfo(record.destinationPath).fileName();
    const bool stale = record.generation != m_batchGeneration;
    bool requeued = false;

    if (stale) {
        // 上一批的残留：文件本身照旧落盘，但计数 / 进度 / 完成信号一个字都不碰。
    } else if (success) {
        emit downloadFinished(fileName, true, QString());
    } else if (!task->networkAttempted() && !record.plainRetried) {
        // 本地准备失败（路径无效、缓存目录不可写、同一下载的缓存被占用）：这不是源站
        // 失败，改走普通下载，别把本来能下成的文件判死。
        PendingJob retry;
        retry.url = record.url;
        retry.destinationPath = record.destinationPath;
        retry.expectedSha1 = record.expectedSha1;
        retry.expectedSize = record.expectedSize;
        retry.officialUrl = record.officialUrl;
        retry.host = record.host;
        retry.segmented = false;
        retry.officialRetried = record.officialRetried;
        retry.plainRetried = true;
        requeueDownloadJob(retry);
        requeued = true;
    } else if (!record.officialUrl.isEmpty() && !record.officialRetried) {
        PendingJob retry;
        retry.url = record.officialUrl;
        retry.destinationPath = record.destinationPath;
        retry.expectedSha1 = record.expectedSha1;
        retry.expectedSize = record.expectedSize;
        retry.officialUrl = QString();  // 只退一次
        retry.host = QUrl(record.officialUrl).host();
        retry.segmented =
            m_segmentEnabled &&
            segmentedEligible(retry.url, retry.expectedSha1, retry.expectedSize, DownloadRange());
        retry.officialRetried = true;
        retry.plainRetried = record.plainRetried;
        requeueDownloadJob(retry);
        requeued = true;
    } else {
        m_failedJobs++;
        FailedJob failed;
        // ★ 记**入口地址**，不是 302 落点：failedJobs() 的重试会拿它再发一次，
        //   预签名落点几秒后就过期了。
        failed.url = record.url;
        failed.destinationPath = record.destinationPath;
        failed.expectedSha1 = record.expectedSha1;
        failed.expectedSize = record.expectedSize;
        failed.officialUrl = record.officialUrl;
        m_failedJobInfos.append(failed);
        if (!record.destinationPath.isEmpty() && !m_failedPaths.contains(record.destinationPath)) {
            m_failedPaths << record.destinationPath;
        }
        emit downloadFinished(fileName, false,
                              error.isEmpty() ? QStringLiteral("分段下载失败") : error);
    }

    if (DownloadDiag::enabled()) {
        DownloadDiag::line(QStringLiteral("segment finish gen=%1 dest=%2 result=%3 error=%4 "
                                          "stale=%5 retried=%6")
                               .arg(record.generation)
                               .arg(QDir::toNativeSeparators(record.destinationPath),
                                    success ? QStringLiteral("ok") : QStringLiteral("fail"),
                                    error.isEmpty() ? QStringLiteral("-") : error)
                               .arg(stale ? QStringLiteral("yes") : QStringLiteral("no"),
                                    requeued ? QStringLiteral("yes") : QStringLiteral("no")));
    }

    task->deleteLater();
    if (!stale && !requeued) {
        // 和 onDownloadReplyFinished 的收尾一致：done 是"已结账"，失败也含在内。
        m_doneJobs++;
        emitBatchProgress(record.destinationPath);
    }
    // 先腾出预算再判整批是否结束（顺序和 onDownloadReplyFinished 一样）。
    pumpDownloadQueue();
    checkAllDownloadsFinished();
}

int DownloadManager::activeCountForHost(const QString &host) const {
    int n = 0;
    for (auto it = activeDownloads.constBegin(); it != activeDownloads.constEnd(); ++it) {
        // 两把钥匙都要认，**不是二选一**：
        //  · `request().url()` 是"我们这条发给谁"的原话，和队列那头的 `PendingJob::host`
        //    是同一个东西。只认 `reply->url()` 的话，镜像 302 到别处之后这条就不再算在
        //    入口头上了 —— 发给镜像的那几条一条都没记账，入口那道闸等于没生效（2026-09-27
        //    那条 NOTE 说的就是这个）。
        //  · `reply->url()` 是**真正开着连接的那个主机**。2026-10-18 加这一半：BMCLAPI 的
        //    资源地址每条都 302 到它背后临时挑的 MinIO 落点，而落点全是 HTTP/1.1、每家只
        //    放 6 条连接。只记入口的话，20 多条作业可以同时压在**同一台落点**身上，多出来
        //    的进 Qt 内部队列（30 秒传输超时从请求建好就开始算）。入口和落点各算各的额度。
        const QNetworkReply *reply = it.key();
        if (reply->request().url().host() == host || reply->url().host() == host) {
            ++n;
        }
    }
    return n;
}

void DownloadManager::noteReplyProtocol(QNetworkReply *reply) {
    if (!reply || m_shuttingDown) {
        return;
    }
    if (reply->error() == QNetworkReply::ProtocolFailure) {
        const QString host = reply->request().url().host();
        m_http1OnlyHosts.insert(host);
        m_http2Hosts.remove(host);
    }
    const QVariant h2 = reply->attribute(QNetworkRequest::Http2WasUsedAttribute);
    if (h2.isValid() && h2.toBool() && reply->error() != QNetworkReply::ProtocolFailure) {
        const QString host = reply->request().url().host();
        if (!host.isEmpty() && !m_http1OnlyHosts.contains(host) && !m_http2Hosts.contains(host)) {
            m_http2Hosts.insert(host);
            // 这条只是给排查用的：并发上不去的时候，第一件要问的事就是"这个主机认不认 h2"。
            qDebug() << "Host confirmed HTTP/2 (its per-host cap is now lifted):" << host;
        }
    }
}

void DownloadManager::startDownload(const QUrl &url, const QString &destinationPath,
                                    const QString &expectedSha1, qint64 expectedSize,
                                    const QString &officialUrl, int retryAttempts, const DownloadRange &range) {
    // 这条作业的**首选地址那个主机**正被降级、而它还知道备用地址：**直接改道**，别等它慢
    // 10 秒再退。这条判断必须在这里、不能只在"挑地址"的地方：资源对象是几千个一次性排进
    // 队列的，它们身上的镜像地址在排队那一刻就拼好了（见 PendingJob），等它们一个个撞上慢
    // 判据再退等于把 10 秒乘以几千。
    //
    // NOTE: 问的是**这条地址自己那个主机**，不是"镜像源降没降级"。整合包的"镜像"是每条文件
    //       各自的首选地址（见 mirrorDemotedFor()），问全局那一个的话，mediafilez 慢会把
    //       cdn.modrinth.com 上的作业一起改道 —— 那正是 2026-09-23 那次导入垮掉的地方。
    // ★★ 2026-09-27：判据搬进了 effectiveDownloadUrl() —— **发出去的是谁**和
    //    pumpDownloadQueue() 那道按主机闸**问的是谁**必须是同一个判断，两处各写一遍就会
    //    漂开（漂开的形状见那个函数上面那段）。这里只把结果取出来用。
    const QString urlText = url.toString();
    const QString targetText =
        effectiveDownloadUrl(urlText, officialUrl, mirrorDemotedFor(url.host()));
    const bool rerouted = (targetText != urlText);
    QString fallback = rerouted ? QString() : officialUrl;  // 改了道就没有"退回官方"这回事
    // 花括号不是风格：`QNetworkRequest request(QUrl(targetText));` 会被当成一个函数声明
    // （most vexing parse），下一句 setTransferTimeout 直接编不过。
    QNetworkRequest request{QUrl(targetText)};
    configureHttp1(request);
    if (range.active()) {
        request.setRawHeader("Range", "bytes=" + QByteArray::number(range.first) + "-" + QByteArray::number(range.last));
        if (!range.validator.isEmpty()) request.setRawHeader("If-Range", range.validator);
        request.setRawHeader("Accept-Encoding", "identity");
    }
    if (m_http1OnlyHosts.contains(QUrl(targetText).host()))
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    // 没有超时的话，一个卡死的连接会让 pendingDownloads 永远归不了零。UI 在"下载中"
    // 会禁用按钮，那就从"日志刷屏"变成"永久卡死"，比原来的问题更糟。
    request.setTransferTimeout(30000);
    QNetworkReply *reply = networkManager->get(request);
    reply->setProperty("wtfmlRetryAttempts", retryAttempts);
    reply->setProperty("wtfmlStartMs", m_clock.elapsed());
    // 这条是"降级改道"发出去的 ⇒ 记住它是从哪只镜像主机改道过来的。改道之后身上没有
    // 备用地址（fallback 已清空），所以官方这条再失败就真的失败了；这个属性是
    // noteReroutedReplyOutcome() 唯一能知道"该把谁的降级撤掉"的地方。
    if (rerouted) {
        reply->setProperty("wtfmlReroutedFromHost", url.host());
    }
    reply->setProperty("rangeFirst", range.first);
    reply->setProperty("rangeLast", range.last);
    reply->setProperty("rangeTotal", range.total);
    reply->setProperty("rangeValidator", range.validator);
    reply->setReadBufferSize(256 * 1024);
    // ★★ 2026-10-17：这一发要是 302 转到一台**刚刚证明过是死的**落点上，别等首字节计时
    //     到期（12 秒）—— 当场 abort，让它去重新掷一台落点。代价只有一个往返。
    //     落点表见 noteDeadRedirectTarget()：那里记的是"接得上、一个字节都不给"的主机。
    const QUrl entryUrlForRedirect{QUrl(targetText)};
    const QString officialFallbackForRedirect = fallback;
    connect(reply, &QNetworkReply::redirected, this,
            [this, reply, entryUrlForRedirect, officialFallbackForRedirect](const QUrl &target) {
                if (m_shuttingDown || !replyIsFromCurrentBatch(reply)) {
                    return;
                }
                // 落点主机记在回复身上：之后每一条明细（停止 / 失败 / 完成）都能说出
                // "这一条最后是在谁身上出的问题"，不必再从 finalUrl 反推。
                reply->setProperty("wtfmlLandedHost", target.host());
                const bool banned =
                    m_deadRedirectHosts.value(authorityKey(target)) > m_clock.elapsed();
                if (DownloadDiag::enabled()) {
                    // 302 本身就是一条值得单独记的事件：BMCLAPI 的资源每条都转一手，
                    // "每个文件多一个往返"和"转到一台坏落点"在总耗时上长得一模一样，
                    // 只有这条能分开。elapsed 是**从发出到转手**用了多久。
                    DownloadDiag::line(
                        QStringLiteral("redirect entry=%1 -> landed=%2 elapsed=%3ms banned=%4")
                            .arg(DownloadDiag::safeUrl(entryUrlForRedirect),
                                 DownloadDiag::safeUrl(target),
                                 QString::number(diagElapsedMs(reply, m_clock.elapsed())),
                                 banned ? QStringLiteral("yes") : QStringLiteral("no")));
                }
                if (!banned) {
                    return;  // 没被拉黑 / 已经到期
                }
                noteDeadRedirectTarget(target, entryUrlForRedirect,
                                       officialFallbackForRedirect);
                reply->abort();
            });
    const auto stream = QSharedPointer<StreamedFile>::create(destinationPath, m_allowOverwrite);
    m_streamedFiles.insert(reply, stream);
    // 文件作业才量首字节（元数据那几条走的是别的槽，没有 readyRead 这一截）。
    // 有这一格，"ttfb=never"才只表示"真的一口都没吐"，而不是"这条没量"。
    if (DownloadDiag::enabled()) {
        reply->setProperty("wtfmlMeasureTtfb", true);
    }
    if (range.active()) connect(reply, &QNetworkReply::metaDataChanged, this, [reply, range] {
        // If-Range 失效时源站可以改发整文件；不要等大正文写完才发现不能拼接。
        if (!rangeProblem(reply, range).isEmpty()) reply->abort();
    });
    connect(reply, &QIODevice::readyRead, this, [this, reply, stream] {
        if (m_shuttingDown || !replyIsFromCurrentBatch(reply)) return;
        // 第一口数据到手的时刻。诊断日志靠它把"建连 + 302 + 首字节"与"正文传输"分开 ——
        // 总耗时一样慢的两种链子（每文件多一个往返 vs 带宽低）只有这一格能区分。
        if (DownloadDiag::enabled() && !reply->property("wtfmlFirstByteMs").isValid()) {
            reply->setProperty("wtfmlFirstByteMs", m_clock.elapsed());
        }
        if (!rangeProblem(reply, rangeOf(reply)).isEmpty()) { reply->abort(); return; }
        // ★ 2026-10-18（§9.2 下载限速）：设了上限就**只登记、不读** —— 真正的读取由
        //   onSpeedPumpTick() 按令牌放行。回复自己的 readBufferSize（256 KiB）就是闸门：
        //   我们不读，socket 那边很快就没人往下拉了（背压）。
        if (m_speedLimitBytesPerSecond > 0) {
            if (reply->isReadable()) {
                m_throttledReplies.insert(reply);
            }
            return;
        }
        stream->drain(reply);
        if (auto meter = m_stallMeters.find(reply); meter != m_stallMeters.end()) {
            meter->bytesSeen = qMax(meter->bytesSeen, stream->bytes);
            meter->lastSignOfLifeMs = m_clock.elapsed();
        }
        if (!stream->error.isEmpty()) reply->abort();
    });
    activeDownloads[reply] = destinationPath;
    // 这是"这一条属于第几批"的唯一一处登记（见 m_batchGeneration）。
    m_replyGenerations[reply] = m_batchGeneration;
    if (!expectedSha1.isEmpty()) {
        expectedSha1s[reply] = expectedSha1;
    }
    // 期望字节数单独一张表（不是"有 sha1 才算"）：**拿不到 sha1 的库正是最需要它的一类**
    // —— maven 风格的那几个（Forge / NeoForge / Fabric 的 loader）本来就没有 sha1，写盘那
    // 一步以前对它们只剩"HTTP 200 就算成功"这一条判据（见 onDownloadReplyFinished）。
    if (expectedSize > 0) {
        expectedSizes[reply] = expectedSize;
    }
    // 走的是镜像、而且调用方知道官方那条地址是什么 —— 记下来，这一发取不回来时
    // 拿它再试一次（见 m_officialFallbacks 和 retryWithOfficialUrl()）。
    // NOTE: 两个地址相同就不记：那种情况下"退回官方"等于原样再发一次，只会把
    //       同一个失败重复一遍，还多花一个超时。
    if (shouldRecordOfficialFallback(targetText, fallback)) {
        m_officialFallbacks[reply] = fallback;
    }
    // **每一条**发出去的回复都进这张表（不只是有备用地址那些）：看门狗靠 lastSignOfLifeMs
    // 判"发出去之后一点动静都没有"，那种回复不管有没有退路都不该一直攥着连接
    // （见 kNoProgressGiveUpMs）。有备用地址的那些另外还会走"太慢就换一条路"那条判据。
    m_stallMeters[reply].lastSignOfLifeMs = m_clock.elapsed();
    pendingDownloads++;
    if (DownloadDiag::enabled()) {
        QString rangeText = QStringLiteral("-");
        if (range.active()) {
            rangeText = QStringLiteral("%1-%2/%3 etag=%4")
                            .arg(range.first)
                            .arg(range.last)
                            .arg(range.total)
                            .arg(range.validator.isEmpty()
                                     ? QStringLiteral("-")
                                     : QString::fromLatin1(range.validator));
        }
        const QString targetHost = QUrl(targetText).host();
        DownloadDiag::line(QStringLiteral(
                               "send gen=%1 url=%2 dest=%3 range=%4 attempt=%5 inflight=%6 "
                               "queued=%7 host=%8 http2=%9 rerouted=%10 fallback=%11 "
                               "expectedSha1=%12 expectedSize=%13 hostInflight=%14")
                               .arg(m_batchGeneration)
                               .arg(DownloadDiag::safeUrl(targetText))
                               .arg(QDir::toNativeSeparators(destinationPath))
                               .arg(rangeText)
                               .arg(retryAttempts)
                               .arg(activeDownloads.size())
                               .arg(m_pendingJobs.size())
                               .arg(targetHost)
                               .arg(m_http2Hosts.contains(targetHost)
                                        ? QStringLiteral("yes")
                                        : (m_http1OnlyHosts.contains(targetHost)
                                               ? QStringLiteral("no")
                                               : QStringLiteral("?")))
                               // 一次 arg() 最多认 %1..%9；剩下五条另起一次。
                               .arg(rerouted ? QStringLiteral("yes -> official")
                                             : QStringLiteral("no"),
                                    fallback.isEmpty() ? QStringLiteral("-")
                                                       : DownloadDiag::safeUrl(fallback),
                                    expectedSha1.isEmpty() ? QStringLiteral("-") : expectedSha1,
                                    QString::number(expectedSize),
                                    QString::number(activeCountForHost(targetHost))));
    }
    connect(reply, &QNetworkReply::finished, this, &DownloadManager::onDownloadReplyFinished);
    connect(reply, &QNetworkReply::downloadProgress, this,
            &DownloadManager::onDownloadReplyProgress);
}

bool DownloadManager::replyIsFromCurrentBatch(QNetworkReply *reply) const {
    // 查不到记录的按**当前**这一批处理（默认值给当前代次）：批次之外的一次性作业
    // （版本清单、加载器版本列表、安装器下载……）本来就各算各的，从来不在批次里。
    return m_replyGenerations.value(reply, m_batchGeneration) == m_batchGeneration;
}

void DownloadManager::handleDownloadError(QNetworkReply *reply, const QString &errorMessage) {
    const QString destPath = activeDownloads.value(reply);
    if (DownloadDiag::enabled()) {
        const QFileInfo info(destPath);
        DownloadDiag::line(
            QStringLiteral("FAILED dest=%1 error=%2 expectedSha1=%3 expectedSize=%4 "
                           "onDisk=%5 elapsed=%6ms stale=%7 request=%8 %9")
                .arg(QDir::toNativeSeparators(destPath), errorMessage,
                     expectedSha1s.value(reply).isEmpty() ? QStringLiteral("-")
                                                          : expectedSha1s.value(reply),
                     QString::number(expectedSizes.value(reply, -1)),
                     info.exists() ? QStringLiteral("%1 bytes").arg(info.size())
                                   : QStringLiteral("(missing)"),
                     QString::number(diagElapsedMs(reply, m_clock.elapsed())),
                     replyIsFromCurrentBatch(reply) ? QStringLiteral("no") : QStringLiteral("yes"),
                     DownloadDiag::safeUrl(reply->request().url()), diagReplyFacts(reply)));
    }
    qWarning() << "Download failed:" << destPath << errorMessage;
    // 属于**已经被放弃的那一批**：只留上面那行日志，不进当前批的失败计数、也不往失败清单
    // 里塞（那两样都是给界面上"这一批下完没有"看的，见 m_batchGeneration）。
    if (!replyIsFromCurrentBatch(reply)) {
        qWarning() << "  (this reply belongs to an abandoned batch; not counted)";
        return;
    }
    m_failedJobs++;
    // ★ 这一条**也是重试要的那一份**（§4.2「失败任务保存失败文件列表和原始 URL 来源，重试
    //   只重做失败项」）。在这里登记而不是在 scheduleFile()：只有真失败了的才需要重做 ——
    //   排进去的几千个作业里绝大多数是好的，全留着等于把整批再存一份。
    //   NOTE: 那几张按 reply 存的小表（expectedSha1s / expectedSizes / m_officialFallbacks）
    //         在这**之后**才被摘掉（onDownloadReplyFinished 的收尾），所以这里读得到。
    //         同一份路径失败两次（镜像一次、官方一次）也不去重：后一条覆盖不了前一条的
    //         sha1/字节数，两条都重试一遍是安全的（第二次会走"已经下好了"那条路跳过）。
    FailedJob failed;
    failed.url = reply->url().toString();
    failed.destinationPath = destPath;
    failed.expectedSha1 = expectedSha1s.value(reply);
    failed.expectedSize = expectedSizes.value(reply, -1);
    failed.officialUrl = m_officialFallbacks.value(reply);
    if (failed.url.isEmpty() && !failed.officialUrl.isEmpty()) {
        failed.url = failed.officialUrl;
    }
    m_failedJobInfos.append(failed);
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
    // m_segmentJobs：分段作业没有 QNetworkReply，不计 pendingDownloads，必须单独算上。
    if (m_shuttingDown || m_batchOpen || m_batchEmitted || pendingDownloads > 0
        || !m_pendingJobs.isEmpty() || !m_segmentJobs.isEmpty()) {
        return;
    }
    m_batchEmitted = true;
    if (DownloadDiag::enabled()) {
        DownloadDiag::line(QStringLiteral("batch complete gen=%1 success=%2 total=%3 done=%4 "
                                          "failed=%5 skipped=%6")
                               .arg(m_batchGeneration)
                               .arg(m_failedJobs == 0 ? QStringLiteral("yes") : QStringLiteral("no"))
                               .arg(m_totalJobs)
                               .arg(m_doneJobs)
                               .arg(m_failedJobs)
                               .arg(m_skippedJobs));
        // 失败清单最多列 200 条：几千个资源对象一起失败时，前 200 条已经足够看出是
        // "整类地址都不通"还是"偶发几条"，无上限地抄下来只会把日志撑爆。
        constexpr int kMaxListed = 200;
        int listed = 0;
        for (const FailedJob &job : m_failedJobInfos) {
            if (listed++ >= kMaxListed) {
                DownloadDiag::line(QStringLiteral("batch failed-list truncated: %1 more")
                                       .arg(m_failedJobInfos.size() - kMaxListed));
                break;
            }
            DownloadDiag::line(QStringLiteral("  failed-job url=%1 dest=%2 expectedSha1=%3 "
                                              "expectedSize=%4 official=%5")
                                   .arg(DownloadDiag::safeUrl(job.url),
                                        QDir::toNativeSeparators(job.destinationPath),
                                        job.expectedSha1.isEmpty() ? QStringLiteral("-")
                                                                   : job.expectedSha1)
                                   .arg(job.expectedSize)
                                   .arg(job.officialUrl.isEmpty()
                                            ? QStringLiteral("-")
                                            : DownloadDiag::safeUrl(job.officialUrl)));
        }
    }
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
    // ★ 索引这一条**不进 done/total**（2026-10-15 改，见 DownloadStats 那段）。它是"清单"
    //   不是"这个实例的文件"，而且每次都要重取 —— 让它占一格的话，一个什么都没缺的实例点
    //   一次「补全文件」也会报出"下载了 1 个"，用户读到的就是那个说不通的「n-1 个已存在」。
    //   收尾那边照样等它（它进 pendingDownloads / activeDownloads），一个字节都没少做。
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
    configureHttp1(request);
    request.setTransferTimeout(30000);
    QNetworkReply *indexReply = networkManager->get(request);
    indexReply->setProperty("wtfmlStartMs", m_clock.elapsed());
    if (DownloadDiag::enabled()) {
        DownloadDiag::line(QStringLiteral(
                               "asset-index send id=%1 url=%2 official=%3 expectedSha1=%4")
                               .arg(assetIndex.id, DownloadDiag::safeUrl(indexUrl),
                                    assetIndex.url.isEmpty()
                                        ? QStringLiteral("-")
                                        : DownloadDiag::safeUrl(assetIndex.url),
                                    assetIndex.sha1.isEmpty() ? QStringLiteral("-")
                                                              : assetIndex.sha1));
    }
    activeDownloads[indexReply] = indexPath;
    // 和 startDownload() 一样登记代次：索引这条也有可能落在被放弃的那一批里（见
    // m_batchGeneration）。
    m_replyGenerations[indexReply] = m_batchGeneration;
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

    // 和 onDownloadReplyFinished() 同一个口径：属于已经放弃的那一批的索引，文件照样落盘，
    // 但不解析、不发信号、不记账（理由见 m_batchGeneration）。
    const bool stale = !replyIsFromCurrentBatch(reply);

    if (DownloadDiag::enabled()) {
        DownloadDiag::line(QStringLiteral("asset-index reply %1 bytes=%2 elapsed=%3ms stale=%4")
                               .arg(diagReplyFacts(reply))
                               .arg(reply->bytesAvailable())
                               .arg(diagElapsedMs(reply, m_clock.elapsed()))
                               .arg(stale ? QStringLiteral("yes") : QStringLiteral("no")));
    }

    bool success = false;
    QString errorMessage;

    if (reply->error() == QNetworkReply::NoError) {
        // 和 onDownloadReplyFinished() 是同一套：QSaveFile 原子落盘（半截的索引留在
        // indexPath 上后果更重 —— parseAndDownloadAssets() 会拿它当"资源列表"去下几千个
        // 东西，而它本身再也不会被重下）。
        QSaveFile file(indexPath);
        QDir().mkpath(QFileInfo(indexPath).absolutePath());
        const QByteArray body = reply->readAll();
        if (body.isEmpty()) {
            errorMessage = QStringLiteral("Server returned an empty body");
        } else if (file.open(QIODevice::WriteOnly)) {
            if (file.write(body) != body.size() || !file.commit()) {
                errorMessage = QStringLiteral("Failed to save asset index file");
            } else {
                // 和 onDownloadReplyFinished() 一样必须核 SHA1：镜像源下这条地址是拿索引号
                // **拼**出来的（/indexes/<id>.json），拼错时回来的可能是一份别的索引，也可能是
                // 一个 404 页面 —— 不拦住就会拿它去下几千个内容不对的资源。期望值在
                // downloadAssets() 里就登记好了（version.json 的 assetIndex.sha1）。
                const QString expectedSha1 = expectedSha1s.value(reply);
                if (expectedSha1.isEmpty() || verifyFileSha1(indexPath, expectedSha1)) {
                    success = true;
                    if (DownloadDiag::enabled()) {
                        DownloadDiag::line(QStringLiteral("asset-index saved path=%1 bytes=%2")
                                               .arg(QDir::toNativeSeparators(indexPath))
                                               .arg(body.size()));
                    }
                    // 已经放弃的那一批**不往下发**资源对象：那几千个作业会被算进**当前**
                    // 这一批的总数里（用户点了取消却看见进度条的分母又涨了几千）。
                    if (!stale) {
                        parseAndDownloadAssets(indexPath);
                    }
                } else {
                    // 坏文件不留盘：它是"资源列表"本身，留在那儿没有任何价值，只会误导排查。
                    errorMessage = QStringLiteral("SHA1 verification failed");
                    QFile::remove(indexPath);
                }
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
        if (!stale) {
            // 报**文件名**，和这个信号别处的每一发一个口径（失败那两处都是
            // QFileInfo(...).fileName()，见 handleDownloadError）。这一处原来报的是
            // indexPath（一整条绝对路径）—— 同一个作业成功时给一条路径、失败时给一个名字，
            // 收信号的那一侧就没法把"这一条报上来的名字"和"那一条报上来的名字"对上
            //（2026-09-25 修，这个信号的形参就叫 fileName）。
            emit downloadFinished(QFileInfo(indexPath).fileName(), true, QString());
        }
    } else if (!stale &&
               retryWithOfficialUrl(reply, indexPath, &DownloadManager::onAssetIndexDownloaded)) {
        // 镜像那条路没成，已经换成官方地址重发了 —— 这一次**不算失败**：作业还在这一个
        // 里，计数和失败清单都不动，直接返回（下面那些收尾是给"这个 reply 真的结账"用的）。
        return;
    } else {
        // 索引成功时不占文件进度；但失败必须临时占一格并同时结掉，否则统计会出现
        // `failed == 1, total == done == 0`，进度条和失败摘要彼此矛盾。它仍然只是失败摘要里的
        // 元数据项，不会在成功批次里冒充"下载了一个实例文件"。
        if (!stale) {
            ++m_totalJobs;
            ++m_doneJobs;
        }
        if (DownloadDiag::enabled()) {
            DownloadDiag::line(QStringLiteral("asset-index failed: %1").arg(errorMessage));
        }
        // NOTE: 必须在 activeDownloads.remove() 之前调用 —— handleDownloadError
        // 靠这张表反查目标路径来记失败清单。
        handleDownloadError(reply, errorMessage);
    }

    activeDownloads.remove(reply);
    m_replyGenerations.remove(reply);
    expectedSha1s.remove(reply);
    expectedSizes.remove(reply);
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
    if (!stale) {
        // 只有 `emitBatchProgress`：这一条**不配对地**改 done（它压根没进过 total，见
        // downloadAssets 里那段）。进度条那一格照样刷新一下当前文件名，免得停在上一个文件上。
        emitBatchProgress(indexPath);
    }
    checkAllDownloadsFinished();
}

bool DownloadManager::retryStalledFromMirror(QNetworkReply *reply,
                                             const QString &destinationPath) {
    if (!replyIsFromCurrentBatch(reply) || m_shuttingDown || m_batchEmitted) return false;
    // 只认**看门狗 abort 掉的**那条：error 是 Operation canceled，而不是 HTTP 失败。
    if (reply->error() != QNetworkReply::OperationCanceledError) return false;
    const QUrl mirrorEntry = reply->request().url();
    const QUrl landed = reply->url();
    // 只有 302 转手过的那一类才值得回入口重挑落点；没转手的 abort 是看门狗对"慢镜像"
    // 那一档做的，它本来就该按原来的顺序退官方。
    // ★ 2026-10-17：判"转没转手"用**主机 + 端口** —— 同一主机换端口的落点也算换了一台
    //   （见 authorityKey()），只比主机名会把这种当成"没转手"、直接退官方。
    if (landed.host().isEmpty() || authorityKey(landed) == authorityKey(mirrorEntry)) {
        return false;
    }
    if (!m_officialFallbacks.contains(reply)) return false;  // 本来就走官方的作业没得回
    // ★ 2026-10-18：**入口已经在降级期里就别再掷第二次**。降级只有两种来路，两种都说明
    //   "官方那条当下是能用的"：探针探通（probeOfficialRoute 成功）或者入口自己慢到被
    //   判死。这时候回镜像入口重赌一台落点，等于把用户刚等过的那 10~12 秒再花一遍；
    //   直接返回 false，让调用方按原来的顺序走 retryWithOfficialUrl()。
    //   NOTE: 官方那条真不通的网络不会走到这里 —— 探针失败不降级、降级还会被
    //         noteReroutedReplyOutcome() 撤掉（2026-10-17 测试员那条网络就是这一种），
    //         那边"回入口重掷"的老行为一个字都没动。
    if (mirrorDemotedFor(mirrorEntry.host())) return false;
    const int attempts = reply->property("wtfmlRetryAttempts").toInt();
    if (attempts >= 1) return false;  // 镜像只再试一次，之后照旧退官方
    const QString mirrorUrl = mirrorEntry.toString();
    requeueDownloadJob(PendingJob{mirrorUrl, destinationPath, expectedSha1s.value(reply),
                                     expectedSizes.value(reply, -1),
                                     m_officialFallbacks.value(reply), mirrorEntry.host(),
                                     attempts + 1, 0, rangeOf(reply),
                                     m_segmentEnabled && segmentedEligible(
                                         mirrorUrl, expectedSha1s.value(reply),
                                         expectedSizes.value(reply, -1), rangeOf(reply))});
    qInfo() << "Retrying a stalled mirror download from the mirror entry (it may pick another"
            << "backend this time):" << mirrorUrl;
    if (DownloadDiag::enabled()) {
        DownloadDiag::line(QStringLiteral("mirror retry after 302 stall dest=%1 url=%2 %3")
                               .arg(QDir::toNativeSeparators(destinationPath),
                                    DownloadDiag::safeUrl(mirrorEntry),
                                    diagReplyFacts(reply)));
    }
    return true;
}

bool DownloadManager::retryWithOfficialUrl(QNetworkReply *reply, const QString &destinationPath,
                                           void (DownloadManager::*finishedSlot)()) {
    // 备用地址只在"这条作业走的是镜像"时登记过。没登记 = 这条就是官方地址，
    // 没得再退，交给调用方按失败处理。
    const QString officialUrl = m_officialFallbacks.take(reply);
    if (officialUrl.isEmpty()) {
        return false;
    }
    // 已经放弃的那一批里的回复**不重发**：重发出去的那条会被登记成**当前**代次（它是在
    // 这一刻发出去的），于是它回来时会算进新批的计数 —— 正是这一套代次要挡的那种污染。
    // 备用地址已经在上面 take 走了，调用方随后照常收尾（那条路上不会记账、不发信号）。
    if (!replyIsFromCurrentBatch(reply)) {
        qInfo() << "Not retrying a reply from an abandoned batch:" << officialUrl;
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
    //       kMaxConcurrentPerHost —— 也就是一次 tick 里要 abort 掉整整一档以上。而镜像那一侧
    //       自己也受这条按主机守卫管着（假服务器都只会说 HTTP/1.1，拿不到"已证明 h2"那个
    //       豁免），所以在飞的永远不超过 kMaxConcurrentPerHost 条、能同时退回的最多也就这么
    //       多，正好等于官方那个主机的上限：拦与不拦都是一样多条上链路，测试是绿的但什么都
    //       没证明。
    //       真要做到，得有一台**真会协商 h2 的假镜像**（h2 主机才有 24 条豁免），本机做不到。
    //       所以要改写这一段，请按"这条判据没被用例守着"来对待。
    const QString retryHost = QUrl(officialUrl).host();
    // 文件换源统一重新排队：复用连接预算与流式写盘，不另开一条旁路。
    if (finishedSlot == &DownloadManager::onDownloadReplyFinished) {
        const QString expectedSha1 = expectedSha1s.take(reply);
        const qint64 expectedSize = expectedSizes.take(reply);
        activeDownloads.remove(reply);
        m_streamedFiles.remove(reply);
        m_replyGenerations.remove(reply);
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
        requeueDownloadJob(
            PendingJob{officialUrl, destinationPath, expectedSha1, expectedSize, QString(),
                       retryHost, 0, 0, rangeOf(reply),
                       m_segmentEnabled &&
                           segmentedEligible(officialUrl, expectedSha1, expectedSize,
                                             rangeOf(reply))});
        // 别的作业可能刚腾出位置（也可能这一条立刻就能发出去），顺手泵一次。
        pumpDownloadQueue();
        if (DownloadDiag::enabled()) {
            DownloadDiag::line(QStringLiteral("fallback queued (host busy): dest=%1 official=%2 "
                                              "inflight=%3 queued=%4")
                                   .arg(QDir::toNativeSeparators(destinationPath),
                                        DownloadDiag::safeUrl(officialUrl))
                                   .arg(activeDownloads.size())
                                   .arg(m_pendingJobs.size()));
        }
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
    configureHttp1(request);
    request.setTransferTimeout(30000);
    QNetworkReply *retry = networkManager->get(request);
    retry->setProperty("wtfmlStartMs", m_clock.elapsed());
    if (DownloadDiag::enabled()) {
        DownloadDiag::line(QStringLiteral("fallback resend: dest=%1 official=%2 "
                                          "previous=%3")
                               .arg(QDir::toNativeSeparators(destinationPath),
                                    DownloadDiag::safeUrl(officialUrl), diagReplyFacts(reply)));
    }
    const QString expectedSha1 = expectedSha1s.take(reply);
    const qint64 expectedSize = expectedSizes.take(reply);
    activeDownloads.remove(reply);
    m_replyGenerations.remove(reply);
    activeDownloads[retry] = destinationPath;
    m_replyGenerations[retry] = m_replyGenerations.value(reply, m_batchGeneration);
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
    if (expectedSize > 0) {
        expectedSizes[retry] = expectedSize;
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

    if (DownloadDiag::enabled()) {
        DownloadDiag::line(QStringLiteral("asset-index parsed path=%1 objects=%2")
                               .arg(QDir::toNativeSeparators(indexPath))
                               .arg(objects.size()));
    }

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
    m_replyGenerations.remove(reply);
    if (DownloadDiag::enabled()) {
        DownloadDiag::line(QStringLiteral("manifest reply %1 bytes=%2 elapsed=%3ms")
                               .arg(diagReplyFacts(reply))
                               .arg(reply->bytesAvailable())
                               .arg(diagElapsedMs(reply, m_clock.elapsed())));
    }
    // 镜像那条没成（失败 / 被看门狗 abort）→ 用官方地址重发一次，这次不算失败。
    if (reply->error() != QNetworkReply::NoError &&
        retryWithOfficialUrl(reply, QStringLiteral("version_manifest.json"),
                             &DownloadManager::onVersionManifestReplyFinished)) {
        return;
    }
    m_stallMeters.remove(reply);
    // NOTE: 这张表**每一条收尾的路都要摘干净**（这条是成功路径，以前漏了它）。
    //       键是回复指针，Qt 释放之后下一批复用同一个地址，残留的键会让一条**没有**
    //       备用地址的新回复被判成"有退路"（看门狗那条慢镜像判据），然后拿着一份**陈旧**的
    //       官方地址去重发 —— 重发到上一个版本 json 的地址上。
    //
    // NOTE: 摘的时机是**下面那段内容核完之后**，不是紧跟 m_stallMeters（2026-09-24 挪的）：
    //       镜像的那份清单"HTTP 200 + 截了一半/根本不是 JSON"是它的典型坏法，那种失败也该
    //       退回官方源再试一次 —— 先摘掉就没有退路了。
    //
    // NOTE(2026-09-25)：上面那句注释说的挪动**当时只做在了姊妹槽
    //       onVersionJsonReplyFinished() 里**（那边用的是 take），这一条一直还摘在内容核对
    //       之前 —— 于是"解不开 → retryWithOfficialUrl()"那条路上 take() 是空的，重发出去
    //       的那条身上只剩 activeDownloads 里那一笔。它要是再卡死 90 秒，failWedgedDownload()
    //       认不出这是元数据作业，按**文件**作业收尾：可它从没进过 pendingDownloads
    //       （见那个函数开头的 NOTE），那一下 --pendingDownloads 吃的是**别人**的计数
    //       （钳到 0），checkAllDownloadsFinished() 于是在文件还在路上时宣布整批下完。
    //       现在真挪到这儿，和姊妹槽一个口径。
    if (reply->error() == QNetworkReply::NoError) {
        QJsonParseError error;
        QJsonDocument json = QJsonDocument::fromJson(reply->readAll(), &error);
        if (error.error == QJsonParseError::NoError) {
            parseVersionManifest(json);
            if (DownloadDiag::enabled()) {
                DownloadDiag::line(
                    QStringLiteral("manifest parsed versions=%1").arg(availableVersions.size()));
            }
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
    // 走到这儿 = 这条作业真的收尾了（成功、或者没有退路可退了）。上面那两条 return 是
    // "换一条路再试"，作业已经被 retryWithOfficialUrl() 搬到新回复上，这里**不能**摘。
    m_metadataReplies.remove(reply);
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
    m_replyGenerations.remove(reply);
    if (DownloadDiag::enabled()) {
        DownloadDiag::line(QStringLiteral("version-json reply %1 bytes=%2 elapsed=%3ms")
                               .arg(diagReplyFacts(reply))
                               .arg(reply->bytesAvailable())
                               .arg(diagElapsedMs(reply, m_clock.elapsed())));
    }
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
            if (DownloadDiag::enabled()) {
                DownloadDiag::line(QStringLiteral("version-json parsed bytes=%1 id=%2 downloads=%3")
                                       .arg(bytes.size())
                                       .arg(json.object().value(QStringLiteral("id")).toString())
                                       .arg(json.object()
                                                .value(QStringLiteral("downloads"))
                                                .toObject()
                                                .keys()
                                                .join(QLatin1Char(','))));
            }
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
            if (DownloadDiag::enabled()) {
                DownloadDiag::line(QStringLiteral("version-json rejected: %1").arg(problem));
            }
            emit versionJsonFetched(QJsonObject());
        }
    } else {
        qDebug() << "Network error:" << reply->errorString();
        emit versionJsonFetched(QJsonObject());
    }
    m_officialFallbacks.remove(reply);
    reply->deleteLater();
}

bool DownloadManager::requeueTransientFailure(QNetworkReply *reply, const QString &destinationPath) {
    if (!replyIsFromCurrentBatch(reply) || m_shuttingDown || m_batchEmitted) return false;
    if (!DownloadRetryPolicy::transientReply(reply)) return false;
    // 这一条如果是从镜像降级改道到官方的，"官方连不上"就是撤销降级的证据（见
    // noteReroutedReplyOutcome）；不在这条路上记的话，超时这一档永远进不了那张表。
    noteReroutedReplyOutcome(reply, false);
    if (reply->error() == QNetworkReply::ProtocolFailure) {
        const QString host = reply->request().url().host();
        m_http1OnlyHosts.insert(host);
        m_http2Hosts.remove(host);
    }
    const int attempts = reply->property("wtfmlRetryAttempts").toInt();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray retryAfter = reply->rawHeader("Retry-After");
    // 429 走专用退避：通用那条 0.5/1/2 秒比限流窗口还快（见 throttleDelayMs 的注释）。
    int delay = attempts >= 3 ? -1
                              : (status == 429 ? DownloadRetryPolicy::throttleDelayMs(attempts, retryAfter)
                                               : DownloadRetryPolicy::delayMs(attempts, retryAfter));
    if (delay < 0) return false;
    const QString url = reply->request().url().toString();
    const qint64 readyAt = m_clock.elapsed() + delay;
    requeueDownloadJob(PendingJob{url, destinationPath, expectedSha1s.value(reply),
        expectedSizes.value(reply, -1), QString(), QUrl(url).host(), attempts + 1, readyAt, rangeOf(reply),
        m_segmentEnabled && segmentedEligible(url, expectedSha1s.value(reply),
                                              expectedSizes.value(reply, -1), rangeOf(reply))});
    const auto generation = m_batchGeneration;
    // QTimer 与 elapsed() 的取整边界可能不同；提前唤醒不能让队列错过唯一一次泵送。
    auto *wakeup = new QTimer(this);
    wakeup->setSingleShot(true);
    wakeup->setTimerType(Qt::PreciseTimer);
    connect(wakeup, &QTimer::timeout, this, [this, generation, readyAt, wakeup] {
        if (generation != m_batchGeneration || m_shuttingDown) { wakeup->deleteLater(); return; }
        const auto remaining = readyAt - m_clock.elapsed();
        if (remaining > 0) { wakeup->start(int(remaining)); return; }
        wakeup->deleteLater();
        pumpDownloadQueue();
        checkAllDownloadsFinished();
    });
    wakeup->start(delay);
    qInfo() << "Temporary download failure; retry" << attempts + 1 << "of 3 in" << delay << "ms:"
            << QUrl(url).adjusted(QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment);
    if (DownloadDiag::enabled()) {
        DownloadDiag::line(QStringLiteral("transient retry dest=%1 attempt=%2 delay=%3ms "
                                          "status=%4 retryAfter=%5 %6")
                               .arg(QDir::toNativeSeparators(destinationPath))
                               .arg(attempts + 1)
                               .arg(delay)
                               .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                                        .toString())
                               .arg(reply->rawHeader("Retry-After").isEmpty()
                                        ? QStringLiteral("-")
                                        : QString::fromLatin1(reply->rawHeader("Retry-After")))
                               .arg(diagReplyFacts(reply)));
    }
    return true;
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
    // 限速那条路上，"结束"意味着以后不会再有人泵它了 —— 当场摘表。下面那一大段里
    // 收尾用的是 stream->drain(reply)（不限量），缓冲区里剩的字节一个都不会漏。
    m_throttledReplies.remove(reply);

    // 这一条属于**已经被放弃的那一批**吗（用户点了取消、之后又开了一批）。它回来的文件照样
    // 落盘（数据是好的），但计数、失败清单、进度和"整批下完"的判定一个字都不许碰 ——
    // 否则上一批的残留会算进新批，甚至提前宣布整批下完。见 m_batchGeneration。
    const bool stale = !replyIsFromCurrentBatch(reply);

    bool success = false;
    bool retryQueued = false;
    QString errorMessage;

    const auto stream = m_streamedFiles.take(reply);
    const auto range = rangeOf(reply);
    const QString rangeError = rangeProblem(reply, range);
    if (stale) {
        errorMessage = QStringLiteral("Download cancelled or abandoned");
    } else if (stream && !stream->error.isEmpty()) {
        errorMessage = stream->error;
    } else if (!rangeError.isEmpty()) {
        errorMessage = rangeError;
    } else if (reply->error() != QNetworkReply::NoError) {
        errorMessage = reply->errorString();
    } else if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != (range.active() ? 206 : 200)) {
        errorMessage = QStringLiteral("Expected a complete HTTP 200 response");
    } else if (!stream) {
        errorMessage = QStringLiteral("Download temporary file is unavailable");
    } else {
        stream->drain(reply);
        errorMessage = stream->finish(expectedSha1s.value(reply), expectedSizes.value(reply, -1),
                                      !range.active() && shouldBeZipArchive(destPath));
        success = errorMessage.isEmpty();
    }
    if (DownloadDiag::enabled()) {
        const QString rangeText =
            range.active() ? QStringLiteral("%1-%2/%3").arg(range.first).arg(range.last).arg(range.total)
                           : QStringLiteral("-");
        DownloadDiag::line(
            QStringLiteral("reply dest=%1 result=%2 reason=%3 bytes=%4 expectedSize=%5 "
                           "elapsed=%6ms stale=%7 range=%8 %9")
                .arg(QDir::toNativeSeparators(destPath),
                     success ? QStringLiteral("ok") : QStringLiteral("fail"),
                     errorMessage.isEmpty() ? QStringLiteral("-") : errorMessage)
                .arg(stream ? stream->bytes : -1)
                .arg(QString::number(expectedSizes.value(reply, -1)))
                .arg(QString::number(diagElapsedMs(reply, m_clock.elapsed())))
                .arg(stale ? QStringLiteral("yes") : QStringLiteral("no"), rangeText,
                     diagReplyFacts(reply)));
    }
    if (success) {
        noteReroutedReplyOutcome(reply, true);
        m_officialFallbacks.remove(reply);
        m_stallMeters.remove(reply);
        // 落盘那一步不分成哪一批（数据本身是好东西），**报**这一步才分：上一批的完成信号
        // 在 abandonBatch() 那一刻就已经不是"这一批的事"了。
        if (!stale) {
            emit downloadFinished(QFileInfo(destPath).fileName(), true, QString());
        }
    } else if (!stale && retryStalledFromMirror(reply, destPath)) {
        // 看门狗 abort 掉的镜像作业（302 转手之后卡住的那一类）：先回**镜像入口**再试
        // 一次 —— 它会重新挑一台落点。以前这里直接退官方，而测试员那条网络上官方整条
        // 都不通，等于把还能用的镜像换成了死路。镜像只重试一次，之后仍按原来的顺序退官方。
        retryQueued = true;
    } else if (!stale &&
               retryWithOfficialUrl(reply, destPath, &DownloadManager::onDownloadReplyFinished)) {
        // 镜像那条路没成，已经换成官方地址重发了 —— 这一次**不算失败**：作业还在这一个
        // 里，计数和失败清单都不动，直接返回（下面那些收尾是给"这个 reply 真的结账"用的）。
        // NOTE: 已经放弃的那一批不在这里重发（重发出去的那条会被登记成当前代次，等于把一条
        //       上一批的作业塞进新批）—— retryWithOfficialUrl() 自己那头也拦了一道。
        return;
    } else if (!stale && requeueTransientFailure(reply, destPath)) {
        retryQueued = true; // 同一作业仍在排队，不增加总量，也不提前报失败。
    } else {
        // NOTE: 要在 activeDownloads.remove() 之前，handleDownloadError 靠它反查路径。
        //       它自己会认出 stale 的那几条（只留一行日志、不记账）。
        handleDownloadError(reply, errorMessage);
    }

    activeDownloads.remove(reply);
    m_replyGenerations.remove(reply);
    expectedSha1s.remove(reply);
    expectedSizes.remove(reply);
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
    if (!stale && !retryQueued) {
        m_doneJobs++;
        emitBatchProgress(destPath);
    }
    // 腾出一条连接就补一个排队的作业，然后才判定整批是否下完（顺序不能反：
    // 先判会把"队列里还有、但当下一个都没在飞"误判成整批结束）。
    // NOTE: 这两句**不按代次分**：连接预算是全类共用的，排队的作业也还等着发；而
    //       checkAllDownloadsFinished() 自己就会看 m_batchEmitted / m_batchOpen，
    //       上一批残留的回复把它叫醒一次不会让它多发一个信号（它们只是把 pendingDownloads
    //       还回来，而判据是"还有没有在飞的"）。
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
        const auto stream = m_streamedFiles.value(reply);
        const qint64 availableNow = reply->bytesAvailable() + (stream ? stream->bytes : 0);
        if (availableNow > meter.bytesSeen) {
            meter.bytesSeen = availableNow;
            // 有新字节 = 还有动静，"卡死"那条兜底的倒计时清掉。细水长流（镜像 302 到 MinIO
            // 那种 0.85~10 KB/s）靠这句活着，它该由"太慢"那条判据管，不该被兜底掐掉。
            meter.lastSignOfLifeMs = now;
        }
        // (0b) ★★ 2026-10-17（用户本机实测）：走镜像、有退路、**一个字节都没收到**的回复，
        //      12 秒就换路 —— 别等下面那条 90 秒的兜底。
        //
        //      根子是 BMCLAPI 的 302：每条资源地址都会转到它背后临时挑的一台落点
        //      （minio.749333.xyz / bmclapiN.sakura.sld.tw ...），而那几台里有的
        //      **TLS 接得上、一个字节都不给**（curl 实测 25 秒 0 字节）。这种回复
        //      Qt 的 30 秒不活动超时也掐不掉（握手 / h2 PING 的字节把它喂着了），
        //      所以从前只有 90 秒兜底会动它 —— 一条死落点占满一个并发位一分半钟。
        //      用户那次 24 位里常年有几条在等死节点，整批只剩一两文件/秒（751/5222、11 分钟）。
        //
        //      这里只换路、不判失败：abort 出来的 OperationCanceledError 会走
        //      onDownloadReplyFinished，那里先 retryStalledFromMirror() 重新掷一台落点
        //      （只掷一次），再不行就退官方。落到死落点这件事还要记一笔（noteDeadRedirectTarget），
        //      攒够了整条镜像先探官方、再决定降不降级。
        // ★ 2026-10-18 补：**分段 Range 请求也过这一条**。它自己身上没有备用地址，但协调器
        //   有"只补缺的段"那条退路；一条 302 到死落点的分段以前只有 90 秒那条兜底会动它，
        //   而协调器要等整批收尾才补段 —— 等于整条作业白等一分半。
        if (meter.bytesSeen == 0
            && (m_officialFallbacks.contains(reply) || rangeOf(reply).active())
            && now - meter.lastSignOfLifeMs >
                   (m_metadataReplies.contains(reply)
                        ? qMin(m_noFirstByteGiveUpMs, kMetadataNoFirstByteGiveUpMs)
                        : m_noFirstByteGiveUpMs)) {
            const QUrl landed = reply->url();
            const QUrl entry = reply->request().url();
            noteDeadRedirectTarget(landed, entry, m_officialFallbacks.value(reply));
            stalled.append(reply);
            continue;
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
        //     ★ 2026-10-18（用户那份 17:06 的诊断日志）：**分段请求（Range 作业）也算
        //       "有退路"** —— 它自己身上没有官方备用地址，但协调器有"只补缺的段"那条
        //       退路（见 ResumableDownload::downloadParts）。实测一条 4 MiB 的分段停在
        //       3.7/4.2 MiB、以几百字节每秒往终点爬，`bytesAvailable()` 一直在涨 ⇒ 90 秒
        //       的"卡死"兜底不响，而没备用地址又不进这一条 ⇒ 那条分段在 664 秒里一直攥着
        //       连接；协调器要等整批收尾才补段，于是整条分段作业永远不会结束（用户只能
        //       取消）。给 Range 作业也过这一条，abort 掉就是让协调器换一次落点重下。
        //     NOTE: "一个字节都还没收到"的不算数（判据在 countsTowardStallStrike 里，理由和
        //           2026-09-23 那次实测都写在头文件）—— 那种回复的进账天然是 0，会把
        //           "还在建连"误判成"链子太慢"。
        // ★ 2026-10-18（§9.2 下载限速）：**限速开着的时候这条整条不成立**。这条判据量的是
        //   "链子来得多快"，而限速正是我们自己把它按住的方式 —— 512 KiB/s 分给 24 条，
        //   每条每 5 秒只有 10 KB，比 kStallMinBytesPerTick（64 KB）还少 ⇒ 一开限速，
        //   整批镜像作业每隔 10 秒就被 abort 一遍（用户看到的形状是"限速一开就疯狂重试、
        //   根本下不完"）。"一个字节都不来"那两条（0b / 1）仍然照旧：那才是真的死了。
        if (m_speedLimitBytesPerSecond <= 0
            && (m_officialFallbacks.contains(reply) || rangeOf(reply).active())) {
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
        //
        // ★★ 2026-10-17（特供版诊断日志）：**经过 302 的回复不降请求的那个主机**。
        //    BMCLAPI 的资源对象地址（bmclapi2.bangbang93.com/assets/...）会 302 到它背后
        //    临时挑的一台镜像（minio.749333.xyz / bmclapi.hurtmc.com / sakura...）。慢的是
        //    落点那一台，不是入口；把入口降级 300 秒，排队里的**几千个**资源对象会一起改道
        //    官方 resources.download.minecraft.net —— 而测试员那条网络上官方资源 CDN 每个
        //    请求都吃满 30 秒超时（同一份日志：降级后 433 个官方请求 0 成功、400 个超时），
        //    十分钟只下完 246/5222 个文件。只给"没有转手"的那条降级，转手慢的那一条自己
        //    abort 后走官方重试即可（作业级，不连累别人）。
        //    同一个主机只降一次（顺延本身是幂等的，这里只是别把日志刷满）。
        QSet<QString> demotedHosts;
        for (QNetworkReply *reply : stalled) {
            // 分段请求（Range 作业）没有备用地址：它慢不代表"这个入口该降级"，而且它
            // 入口常常就是官方那一台 —— 把官方降 300 秒等于让排队里的镜像作业全改道，
            // 正是 2026-10-17 那条事故的翻版。它们只 abort、由协调器补段。
            if (!m_officialFallbacks.contains(reply)) {
                continue;
            }
            const QString host = reply->request().url().host();
            const QString finalHost = reply->url().host();
            if (!finalHost.isEmpty() && finalHost.compare(host, Qt::CaseInsensitive) != 0) {
                continue;
            }
            if (!host.isEmpty() && !demotedHosts.contains(host)) {
                demotedHosts.insert(host);
                demoteMirrorForSlowDownload(host);
            }
        }
        if (DownloadDiag::enabled()) {
            DownloadDiag::line(QStringLiteral("watchdog: aborting %1 stalled mirror reply(ies)")
                                   .arg(stalled.size()));
        }
        for (QNetworkReply *reply : stalled) {
            // 这一条**不一定是退官方**：302 转过手的作业会先回镜像入口重新掷一台落点
            // （见 retryStalledFromMirror），真退官方是掷完之后还没成的那一次。日志按实际
            // 可能性写，别把"换条路再试"说成"已经决定走官方"。
            // 分段请求（没有备用地址）abort 之后由 ResumableDownload 只补缺的那一段。
            if (m_officialFallbacks.contains(reply)) {
                qInfo() << "Aborting a stalled mirror download; the job retries from the mirror"
                           " entry or the official url:"
                        << jobLabel(reply);
            } else {
                qInfo() << "Aborting a stalled segmented-download part; the coordinator will"
                           " re-request the missing range:"
                        << jobLabel(reply);
            }
            if (DownloadDiag::enabled()) {
                DownloadDiag::line(QStringLiteral("watchdog: stalled job=%1 url=%2 %3")
                                       .arg(jobLabel(reply),
                                            DownloadDiag::safeUrl(reply->request().url()),
                                            diagReplyFacts(reply)));
            }
            // 只 abort，不在这里收尾：finished 那个槽会拿到 OperationCanceledError 并按
            // "镜像那条没成"处理（退回官方重发，作业还是同一个、不算失败）。
            reply->abort();
        }
    }
    for (QNetworkReply *reply : wedged) {
        qWarning() << "No data for" << (m_noProgressGiveUpMs / 1000) << "seconds:"
                   << jobLabel(reply);
        if (DownloadDiag::enabled()) {
            DownloadDiag::line(QStringLiteral("watchdog: wedged job=%1 url=%2 giveUp=%3ms %4")
                                   .arg(jobLabel(reply),
                                        DownloadDiag::safeUrl(reply->request().url()))
                                   .arg(m_noProgressGiveUpMs)
                                   .arg(diagReplyFacts(reply)));
        }
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
        if (DownloadDiag::enabled()) {
            DownloadDiag::line(QStringLiteral("watchdog: metadata wedged label=%1 url=%2 %3")
                                   .arg(job.label, DownloadDiag::safeUrl(reply->request().url()),
                                        diagReplyFacts(reply)));
        }
        activeDownloads.remove(reply);
        m_replyGenerations.remove(reply);
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
    // 90 秒一个字节都没有 = 这条链路根本不通。如果它是降级改道到官方的，这就是
    // "官方也不通"的证据 —— 不管下面走重排还是记失败，都得先把这一笔记上。
    noteReroutedReplyOutcome(reply, false, /*forceUnreachable=*/true);
    // ★★ 2026-10-17（无代理实测）：90 秒一个字节都没有，几乎都不是"这个文件没了"，
    //    而是这一次 302 落到了死节点上 —— 测试员那条网络下 5222 个对象只有 13 个这样，
    //    落点全是 18/19/20.749333.xyz 这几台；同一个入口（bmclapi2）的另外 5209 个
    //    对象都正常。**重新排回同一个地址**（BMCLAPI 每次会重新挑一台落点），别把它
    //    当永久失败 —— 否则整批 5222 个下完了还要为这 13 个报"安装失败"。
    //    NOTE: 走 `wtfmlRetryAttempts` 那套计数（和 transient 重试共用 3 次上限）：
    //          一个真死的入口必须能停下来，不然整批永远收不了尾。
    const int wedgedAttempts = reply->property("wtfmlRetryAttempts").toInt();
    const int wedgedDelay = DownloadRetryPolicy::delayMs(wedgedAttempts, {});
    // 只重排**走镜像、身上有备用地址**的那一类（测试员日志里那 13 个就是）：它们的地址
    // 是 BMCLAPI 入口，重发一次会重新挑一台落点。官方直连的作业没有"换一台落点"这回事，
    // 重发同一条死链路只是把失败往后拖 3 倍 —— 那种照旧到点判失败（V6i 钉着）。
    const bool mirrorJob = m_officialFallbacks.contains(reply);
    if (mirrorJob && !m_shuttingDown && wedgedDelay >= 0 && replyIsFromCurrentBatch(reply)) {
        const QString retryUrl = reply->request().url().toString();
        requeueDownloadJob(PendingJob{retryUrl, destPath, expectedSha1s.value(reply),
                                         expectedSizes.value(reply, -1),
                                         m_officialFallbacks.value(reply), QUrl(retryUrl).host(),
                                         wedgedAttempts + 1, m_clock.elapsed() + wedgedDelay,
                                         rangeOf(reply),
                                         m_segmentEnabled && segmentedEligible(
                                             retryUrl, expectedSha1s.value(reply),
                                             expectedSizes.value(reply, -1), rangeOf(reply))});
        const auto generation = m_batchGeneration;
        auto *wakeup = new QTimer(this);
        wakeup->setSingleShot(true);
        wakeup->setTimerType(Qt::PreciseTimer);
        connect(wakeup, &QTimer::timeout, this, [this, generation, wakeup] {
            if (generation != m_batchGeneration || m_shuttingDown) {
                wakeup->deleteLater();
                return;
            }
            wakeup->deleteLater();
            pumpDownloadQueue();
            checkAllDownloadsFinished();
        });
        wakeup->start(wedgedDelay);
        qWarning() << "No data for" << (m_noProgressGiveUpMs / 1000) << "seconds; retrying"
                   << (wedgedAttempts + 1) << "of 3 in" << wedgedDelay << "ms:" << jobLabel(reply);
        if (DownloadDiag::enabled()) {
            DownloadDiag::line(QStringLiteral("watchdog: wedged retry attempt=%1 delay=%2ms "
                                              "job=%3 url=%4 giveUp=%5ms %6")
                                   .arg(wedgedAttempts + 1)
                                   .arg(wedgedDelay)
                                   .arg(jobLabel(reply),
                                        DownloadDiag::safeUrl(reply->request().url()))
                                   .arg(m_noProgressGiveUpMs)
                                   .arg(diagReplyFacts(reply)));
        }
        // 作业回队列了：连接收干净、计数还回去，但**不算完成、也不进失败清单**（和
        // onDownloadReplyFinished() 里 requeueTransientFailure 那条同一个口径）。
        activeDownloads.remove(reply);
        m_streamedFiles.remove(reply);
        m_replyGenerations.remove(reply);
        expectedSha1s.remove(reply);
        expectedSizes.remove(reply);
        m_stallMeters.remove(reply);
        m_officialFallbacks.remove(reply);
        reply->abort();
        reply->deleteLater();
        --pendingDownloads;
        if (pendingDownloads < 0) {
            qWarning() << "pendingDownloads went negative, clamping to 0.";
            pendingDownloads = 0;
        }
        pumpDownloadQueue();
        checkAllDownloadsFinished();
        return;
    }

    // 顺序不能反：handleDownloadError() 靠 activeDownloads 反查路径来记失败清单，
    // 所以先报错、再摘表。摘完表之后哪怕 abort() 真的憋出一个迟到的 finished，
    // 那两个处理函数开头也会把"没登记的回复"直接忽略掉 —— 不会重复计数。
    handleDownloadError(reply,
                        QStringLiteral("No data received within %1 s, gave up")
                            .arg(m_noProgressGiveUpMs / 1000));
    // 问代次**必须在摘表之前**：摘完之后查不到记录，默认值就是"当前这一批"，那就等于没判。
    const bool currentBatch = replyIsFromCurrentBatch(reply);
    activeDownloads.remove(reply);
    m_streamedFiles.remove(reply);
    m_replyGenerations.remove(reply);
    expectedSha1s.remove(reply);
    expectedSizes.remove(reply);
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
    // 属于已经放弃的那一批：连接照样收干净，但计数和进度不碰（handleDownloadError 那头已经
    // 只留了一行日志，见 m_batchGeneration）。
    if (currentBatch) {
        m_doneJobs++;
        emitBatchProgress(destPath);
    }
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
    // ★ 属于**已经被放弃的那一批**的回复到此为止（2026-09-26 补）。上面那两笔看门狗的记账
    //   照记（它们判的是"这条链子还行不行"，和批次无关），但**一条进度都不许报**：
    //   用户点了取消、新的一批已经开起来了，而这两个 emit 会把上一批的文件名和字节数塞进
    //   新一批的进度里 —— `emitBatchProgress()` 那一头还会顺手把 `m_currentFile` 覆盖掉，
    //   界面上"正在下载 X"那一行于是指着上一批的文件，每文件那条进度条也跟着上一批的数字
    //   跳。同一条规矩在另外三个槽里都已经有了（handleDownloadError / failWedgedDownload /
    //   onDownloadReplyFinished），只有这一处漏了。
    if (!replyIsFromCurrentBatch(reply)) {
        return;
    }

    QString destPath = activeDownloads.value(reply);
    emit downloadProgress(QFileInfo(destPath).fileName(), bytesReceived, bytesTotal);
    // 单发大文件（41 MB 的客户端 jar）期间也要让进度条动起来，否则界面像是卡住了。
    emitBatchProgress(destPath);
}
