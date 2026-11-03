/**
 * @file DownloadManagerMirror.cpp
 * @brief 下载源这一层：镜像地址怎么拼、什么时候把某个主机降级、降级多久。
 *
 * 和 `DownloadManager.cpp` 里那段"看门狗 / 落点探测"分开：这里只有**纯映射和判据**
 * （地址表、降级期、失速判据），谁的字节在飞、哪条回复超时留在外面。搬出去之后
 * `DownloadManager.cpp` 不再同时装着"一条回复怎么流动"和"源站怎么选"。
 */

#include "DownloadManager.hpp"

#include "../util/DownloadDiagnostics.hpp"

#include <QDebug>
#include <QNetworkReply>
#include <QUrl>

namespace {

/// Mojang 官方的版本清单地址。
constexpr char kOfficialVersionManifest[] =
    "https://launchermeta.mojang.com/mc/game/version_manifest.json";

/// 镜像源（BMCLAPI，第三方服务）的基址。
constexpr char kMirrorBase[] = "https://bmclapi2.bangbang93.com";

/// 这条失败是不是"根本连不上"（而不是"文件不在 / 内容不对"）。
///
/// 只用来回答一个问题：**官方那条路到底通不通**。所以 HTTP 状态码那一档一概不算，
/// 只有连接层面的错误才算 —— 尤其是 `TimeoutError`：Qt 的 transfer timeout 是"30 秒
/// 一个字节都没来"，官方 CDN 的失败往往就是它。
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
    // 而安装器拿它直接拼路径。漏了斜杠的表现：它会去要 `.../mavennet/minecraftforge/...`，
    // 镜像上 404，然后它自己退回官方 —— 看着像"镜像没起作用"，其实是我们给错了。
    return QLatin1String(kMirrorBase) + QStringLiteral("/maven/");
}

QString DownloadManager::mirrorVersionJsonUrl(const QString &versionId) {
    return QLatin1String(kMirrorBase) + QStringLiteral("/version/") + versionId +
           QStringLiteral("/json");
}

QString DownloadManager::mirrorClientJarUrl(const QString &versionId) {
    // NOTE: 官方地址里的那个 sha1 在这里用不上 —— 镜像不按 sha1 存，按版本号。
    // 文件本身的校验没丢：downloadFile() 照样拿 version.json 里的 sha1 比。
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

QString DownloadManager::versionManifestUrl() const {
    // 覆盖（验证工程）下不套镜像：那条地址就是测试自己的假服务器。
    if (!m_versionManifestUrlOverride.isEmpty()) {
        return m_versionManifestUrlOverride;
    }
    return mirrorUrl(officialVersionManifestUrl());
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

bool DownloadManager::mirrorDemotedAt(qint64 nowMs, qint64 demotedUntilMs) {
    return demotedUntilMs != 0 && nowMs < demotedUntilMs;
}

bool DownloadManager::mirrorStalled(qint64 bytesDelta, qint64 minBytesPerTick) {
    return bytesDelta < minBytesPerTick;
}

bool DownloadManager::countsTowardStallStrike(qint64 bytesSeen, qint64 bytesDelta,
                                              qint64 minBytesPerTick) {
    return bytesSeen > 0 && mirrorStalled(bytesDelta, minBytesPerTick);
}

qint64 DownloadManager::demoteDeadline(qint64 nowMs, qint64 currentUntilMs) {
    return qMax(currentUntilMs, nowMs + kMirrorDemoteMs);
}
