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
