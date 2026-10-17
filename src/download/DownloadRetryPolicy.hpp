#pragma once

#include <QNetworkReply>
#include <QByteArray>
#include <QtGlobal>
#include <QDateTime>
#include <algorithm>

namespace DownloadRetryPolicy {
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
/// ★ 2026-10-17：Fabric 1.20.1 的整段资源补齐被**一个** 10 KB 的资源对象打断，错误是
///   `wrong version number`（一个本来该是 TLS 的连接回了一段明文 —— 中间设备 / 被复用的
///   死连接都会长这样）。它不是证书判断失败（不是过期、主机名不对、自签名），重试一次
///   大概率就换到一条正常连接；而真正的证书问题重试一万次也一样，所以**不在这一档**。
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
} // namespace DownloadRetryPolicy
