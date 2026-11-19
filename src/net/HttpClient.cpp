/**
 * @file HttpClient.cpp
 * @brief 同步 HTTP 的实现（见 .hpp）。
 */

#include "HttpClient.hpp"
#include "RequestBudget.hpp"

#include <QEventLoop>
#include <QElapsedTimer>
#include <QPointer>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

HttpClient::Reply HttpClient::send(QNetworkAccessManager *manager, const QUrl &url, Verb verb,
                                   const QByteArray &body, const QByteArray &contentType,
                                   const QByteArray &bearer, int timeoutMs,
                                   const QList<QPair<QByteArray, QByteArray>> &extraHeaders) {
    Reply result;
    QElapsedTimer elapsed;
    elapsed.start();
    QPointer<QNetworkAccessManager> alive(manager);
    auto lease = RequestBudget::tryAcquire(RequestBudget::Lane::Control);
    if (!lease && alive && timeoutMs > 0) {
        QEventLoop waiting;
        QTimer poll;
        poll.setInterval(25);
        QObject::connect(&poll, &QTimer::timeout, &waiting, [&] {
            lease = RequestBudget::tryAcquire(RequestBudget::Lane::Control);
            if (lease || !alive || elapsed.elapsed() >= timeoutMs) waiting.quit();
        });
        QObject::connect(manager, &QObject::destroyed, &waiting, &QEventLoop::quit);
        poll.start();
        waiting.exec();
    }
    if (!alive || !lease || elapsed.elapsed() >= timeoutMs) {
        result.networkError = QObject::tr("等待网络请求超时或请求已取消。");
        return result;
    }
    QNetworkRequest request{url};
    if (!contentType.isEmpty()) {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QString::fromLatin1(contentType));
    }
    request.setRawHeader("Accept", "application/json");
    if (!bearer.isEmpty()) {
        request.setRawHeader("Authorization", QByteArray("Bearer ") + bearer);
    }
    // 额外的头放在最后设置：调用方明确给了什么就照原样盖上去（上面那几个是默认值）。
    for (const QPair<QByteArray, QByteArray> &header : extraHeaders) {
        request.setRawHeader(header.first, header.second);
    }

    QNetworkReply *reply = nullptr;
    switch (verb) {
        case Verb::Get:
            reply = manager->get(request);
            break;
        case Verb::Post:
            reply = manager->post(request, body);
            break;
        case Verb::Put:
            reply = manager->put(request, body);
            break;
        case Verb::Delete:
            // 这几种接口的 DELETE 都是"取消某种状态"，没有正文。
            reply = manager->sendCustomRequest(request, QByteArrayLiteral("DELETE"), QByteArray());
            break;
    }

    RequestBudget::attach(reply, lease);
    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeoutTimer.start(qMax(1, timeoutMs - int(elapsed.elapsed())));
    if (!reply->isFinished()) loop.exec();

    if (!reply->isFinished()) {
        // 超时：abort 之后这个 reply 上什么都没有了，别再去读它的正文
        reply->abort();
        reply->deleteLater();
        result.networkError = QObject::tr("连接超时。");
        return result;
    }

    result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.raw = reply->readAll();
    result.json = QJsonDocument::fromJson(result.raw).object();
    // 拿到 HTTP 响应（哪怕 4xx/5xx）就不算网络错误，只看有没有状态码
    if (result.status <= 0) {
        result.networkError = reply->errorString();
    }
    reply->deleteLater();
    return result;
}

QString HttpClient::throttledText(const QString &what) {
    // NOTE: 官方 429 的正文长这样：
    // {"path":"…","errorType":"TOO_MANY_REQUESTS","error":"TOO_MANY_REQUESTS","status":429}
    // 那个大写下划线的错误码**不端给用户**（和 401/403/404 那几句同一个口径：
    // 能照着做的那句话才给用户看）。用户要知道的只有两件事：现在先别再试了，
    // 以及这**不是**他的账号或者网络出了问题 —— 不然他会去换账号、重登，
    // 那两件事都不会让服务器少挡一会儿。
    return QObject::tr("%1：请求频率过高，服务器已暂时限制访问（HTTP 429）。\n请等待一两分钟后重试。")
        .arg(what);
}
