#pragma once

#include <QNetworkReply>
#include <QCoreApplication>
#include <QByteArray>
#include <QtGlobal>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QUrl>
#include <QRegularExpression>
#include <algorithm>

namespace DownloadRetryPolicy {
// UI text must not depend on whether Qt's translation catalogue was deployed.
// A network abort does not prove that the user cancelled the task.
inline QString displayError(QNetworkReply::NetworkError error, const QString &fallback) {
    if (error == QNetworkReply::OperationCanceledError)
        return QCoreApplication::translate("DownloadRetryPolicy", "网络请求已中止（可能由超时、换源或取消操作触发）。");
    if (error == QNetworkReply::TimeoutError)
        return QCoreApplication::translate("DownloadRetryPolicy", "网络请求超时，请检查网络后重试。");
    // Qt embeds the full request URL in many network errors. Never copy signed
    // queries, fragments or embedded credentials into logs/task history.
    QString safe = fallback;
    static const QRegularExpression urls(QStringLiteral("https?://[^\\s<>\\\"']+"), QRegularExpression::CaseInsensitiveOption);
    QList<QRegularExpressionMatch> matches;
    auto it = urls.globalMatch(fallback);
    while (it.hasNext()) matches.append(it.next());
    for (auto i = matches.crbegin(); i != matches.crend(); ++i) {
        const auto url = QUrl(i->captured());
        safe.replace(i->capturedStart(), i->capturedLength(),
            url.adjusted(QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment).toString());
    }
    return safe;
}
// All download managers (including nested range workers) share origin cooldowns.
// Monotonic deadlines avoid wall-clock changes; unrelated origins keep running.
struct OriginThrottle {
    QDeadlineTimer blocked;
    QDeadlineTimer recovery;
};
inline QMutex throttleMutex;
inline QHash<QString, OriginThrottle> originThrottles;
inline QString originKey(const QUrl &url) {
    return url.scheme().toLower() + QStringLiteral("://") + url.host().toLower()
        + QLatin1Char(':') + QString::number(url.port(url.scheme() == QStringLiteral("https") ? 443 : 80));
}
inline void coolDown(const QUrl &url, int milliseconds) {
    QMutexLocker lock(&throttleMutex);
    for (auto it = originThrottles.begin(); it != originThrottles.end();) {
        if (it->recovery.hasExpired()) it = originThrottles.erase(it);
        else ++it;
    }
    const auto key = originKey(url);
    auto &state = originThrottles[key];
    const int duration = qBound(500, milliseconds, 60000);
    if (state.blocked.remainingTime() < duration) state.blocked = QDeadlineTimer(duration);
    state.recovery = QDeadlineTimer(duration + 60000);
}
inline qint64 coolingDownMs(const QUrl &url) {
    QMutexLocker lock(&throttleMutex);
    const auto it = originThrottles.find(originKey(url));
    if (it == originThrottles.end()) return 0;
    if (it->recovery.hasExpired()) { originThrottles.erase(it); return 0; }
    return qMax<qint64>(0, it->blocked.remainingTime());
}
inline bool recovering(const QUrl &url) {
    QMutexLocker lock(&throttleMutex);
    const auto it = originThrottles.constFind(originKey(url));
    return it != originThrottles.cend() && !it->recovery.hasExpired();
}
// 只重试暂时性的网络/服务器故障；权限、404、证书与用户取消不重试。
inline bool transient(QNetworkReply::NetworkError error, int status) {
    if (status >= 400) return status == 408 || status == 429 || status == 500 ||
                            status == 502 || status == 503 || status == 504;
    return error == QNetworkReply::TimeoutError || error == QNetworkReply::TemporaryNetworkFailureError ||
           error == QNetworkReply::RemoteHostClosedError || error == QNetworkReply::ConnectionRefusedError ||
           error == QNetworkReply::HostNotFoundError || error == QNetworkReply::NetworkSessionFailedError ||
           error == QNetworkReply::ProxyConnectionClosedError || error == QNetworkReply::ProxyTimeoutError ||
           error == QNetworkReply::ProtocolFailure; // HTTP/2 连接中断也会以协议错误返回。
}
/// 这一次 SSL 握手失败是不是"换一条连接再试一次就可能好"的那一类。
///
/// Fabric 1.20.1 的整段资源补齐被**一个** 10 KB 的资源对象打断，错误是
/// `wrong version number`（一个本来该是 TLS 的连接回了一段明文 —— 中间设备 / 被复用的
/// 死连接都会长这样）。它不是证书判断失败（不是过期、主机名不对、自签名），重试一次
/// 大概率就换到一条正常连接；而真正的证书问题重试一万次也一样，所以**不在这一档**。
inline bool sslHandshakeTransient(const QByteArray &errorString) {
    const QByteArray text = errorString.toLower();
    for (const char *hint : {"wrong version number", "unexpected eof", "unexpected message",
                             "connection reset", "connection closed"}) {
        if (text.contains(hint)) return true;
    }
    return false;
}

/// 带回复的版本：`transient()` 管不到 SSL 握手，这里补上上面那一档。
///
/// 为什么不直接把 SslHandshakeFailedError 塞进 `transient()`：那个函数是"按错误码"的
/// 判据，把证书失败也一并变成可重试，等于悄悄放宽了一条安全边界（verify 的 V6 那一带
/// 钉着 `transient(SslHandshakeFailedError, 0) == false`）。
inline bool transientReply(const QNetworkReply *reply) {
    if (reply == nullptr) return false;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (transient(reply->error(), status)) return true;
    return reply->error() == QNetworkReply::SslHandshakeFailedError &&
           sslHandshakeTransient(reply->errorString().toUtf8());
}

inline int delayMs(int attempts, const QByteArray &retryAfter) {
    if (attempts >= 3) return -1;
    bool ok = false;
    const auto seconds = retryAfter.trimmed().toLongLong(&ok);
    if (ok && seconds >= 0) return int(std::clamp<qint64>(std::min<qint64>(seconds, 60) * 1000, 500, 60000));
    const auto date = QDateTime::fromString(QString::fromLatin1(retryAfter), Qt::RFC2822Date);
    if (date.isValid()) return int(std::clamp<qint64>(QDateTime::currentDateTimeUtc().msecsTo(date), 500, 60000));
    return 500 * (1 << attempts);
}

/// 429（Too Many Requests）专用的退避。
///
/// 服务器已经明说"你太快了"，而 `delayMs()` 那条通用退避是 500/1000/2000 毫秒 ——
/// 的诊断日志里一条分段在 3 秒内连吃 4 个 429，正是这个形状：重试跑得比
/// 限流窗口还快，三条命全撞在同一条链路上。没有 `Retry-After` 时从 2 秒起步、翻倍、
/// 8 秒封顶；服务器给了具体时间就听它的（`delayMs()` 已经夹在 0.5~60 秒）。
///
/// 单独开一个函数而不是改 `delayMs()`：其它错误码的退避节奏没有理由跟着变。
inline int throttleDelayMs(int attempts, const QByteArray &retryAfter) {
    if (attempts >= 3) return -1;
    if (!retryAfter.trimmed().isEmpty()) return delayMs(attempts, retryAfter);
    return int(std::min<qint64>(8000, 2000LL << std::min(attempts, 2)));
}
} // namespace DownloadRetryPolicy
