#pragma once
#include <QDateTime>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QHostInfo>
#include <QHttp1Configuration>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QSslCipher>
#include <QSslConfiguration>
#include <QSslError>
#include <QTimer>
#include <functional>
#include <memory>

namespace NetDiag {
inline QString safeUrl(const QUrl &url) {
    QString result = url.adjusted(QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment).toString();
    if (url.hasQuery()) result += QStringLiteral("?<redacted>");
    return result;
}
inline QString redact(QString text) {
    static const QRegularExpression urls(QStringLiteral("https?://[^\\s\\\"<>]+|socks5://[^\\s\\\"<>]+"));
    const auto matches = urls.globalMatch(text);
    QList<QRegularExpressionMatch> found;
    for (auto it = matches; it.hasNext();) found.append(it.next());
    for (auto it = found.crbegin(); it != found.crend(); ++it)
        text.replace(it->capturedStart(), it->capturedLength(), safeUrl(QUrl(it->captured())));
    return text.left(16384);
}
inline QJsonValue safeValue(const QJsonValue &value) {
    if (value.isString()) return redact(value.toString());
    if (value.isArray()) { QJsonArray result; for (const auto &item : value.toArray()) result.append(safeValue(item)); return result; }
    if (value.isObject()) { QJsonObject result; const auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) result.insert(it.key(), safeValue(it.value()));
        return result;
    }
    return value;
}
inline QJsonObject proxyInfo(const QNetworkProxy &proxy) {
    return {{"type", int(proxy.type())}, {"host", proxy.hostName()}, {"port", proxy.port()},
        {"has_credentials", !proxy.user().isEmpty() || !proxy.password().isEmpty()}};
}
struct Log {
    QFile file;
    QElapsedTimer clock;
    QMutex mutex;
    int nextRequest = 0;
    qint64 transferred = 0;
    qint64 budget = 128LL * 1024 * 1024;
    bool budgetExceeded = false;
    bool throttled = false;
    QList<qint64> firstBytes;
    QMap<QString, int> protocols;
    explicit Log(const QString &path) : file(path) {
        clock.start();
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return;
    }
    void event(const QString &kind, QJsonObject data = {}) {
        static thread_local bool writing = false;
        if (writing) return;
        const QScopedValueRollback<bool> guard(writing, true);
        QMutexLocker lock(&mutex);
        if (!file.isOpen() || file.size() > 16 * 1024 * 1024) return;
        data.insert("event", kind); data.insert("elapsed_ms", double(clock.elapsed()));
        data.insert("time", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        file.write(QJsonDocument(safeValue(data).toObject()).toJson(QJsonDocument::Compact) + '\n');
        if (kind == QStringLiteral("case_start") || kind == QStringLiteral("request_created")
            || kind == QStringLiteral("system_proxy_query_start") || kind == QStringLiteral("case_finished")) file.flush();
        // No per-event fsync: avoid turning diagnostic logging into the bottleneck.
    }
    void flush() { QMutexLocker lock(&mutex); file.flush(); }
};
class SystemProxy final : public QNetworkProxyFactory {
    std::shared_ptr<Log> log;
public:
    explicit SystemProxy(std::shared_ptr<Log> value) : log(std::move(value)) {}
    QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery &query) override {
        log->event("system_proxy_query_start", {{"url", safeUrl(query.url())}});
        const auto result = QNetworkProxyFactory::systemProxyForQuery(query);
        QJsonArray choices; for (const auto &proxy : result) choices.append(proxyInfo(proxy));
        log->event("system_proxy_query_end", {{"url", safeUrl(query.url())}, {"choices", choices}});
        return result;
    }
};
class Access final : public QNetworkAccessManager {
    std::shared_ptr<Log> log;
    QJsonObject options;
public:
    Access(std::shared_ptr<Log> value, QJsonObject configuration, QObject *parent)
        : QNetworkAccessManager(parent), log(std::move(value)), options(std::move(configuration)) {
        const QString route = options.value("route").toString();
        if (route == QStringLiteral("direct")) setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        else if (route == QStringLiteral("explicit")) {
            const QUrl proxy(options.value("proxy").toString());
            setProxy(QNetworkProxy(proxy.scheme() == QStringLiteral("socks5") ? QNetworkProxy::Socks5Proxy : QNetworkProxy::HttpProxy,
                proxy.host(), quint16(proxy.port())));
        } else setProxyFactory(new SystemProxy(log));
    }
protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &original, QIODevice *outgoing) override {
        QNetworkRequest request(original);
        const bool allowH2 = options.value("h2").toBool()
            && original.attribute(QNetworkRequest::Http2AllowedAttribute, QVariant(true)).toBool();
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, allowH2);
        request.setAttribute(QNetworkRequest::Http2DirectAttribute, false);
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
        request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
        QHttp1Configuration h1; h1.setNumberOfConnectionsPerHost(options.value("h1_connections").toInt(12));
        request.setHttp1Configuration(h1);
        request.setTransferTimeout(30000);
        const int id = ++log->nextRequest;
        const auto started = log->clock.elapsed();
        log->event("request_created", {{"id", id}, {"method", operation == HeadOperation ? "HEAD" : "GET"},
            {"url", safeUrl(request.url())}, {"route", options.value("route")}, {"http2_requested", options.value("h2")}, {"http2_allowed", allowH2},
            {"h1_connections", options.value("h1_connections")}, {"range", QString::fromLatin1(request.rawHeader("Range"))}});
        QNetworkReply *reply = QNetworkAccessManager::createRequest(operation, request, outgoing);
        struct State { qint64 bytes = 0; qint64 first = -1; int redirects = 0; };
        auto state = std::make_shared<State>();
        const auto snapshot = [reply, state, started, value = log, id](const QString &kind) {
            value->event(kind, {{"id", id}, {"request_ms", double(value->clock.elapsed() - started)},
                {"url", safeUrl(reply->url())}, {"reported_bytes", double(state->bytes)},
                {"buffered_bytes", double(reply->bytesAvailable())}, {"first_byte_ms", double(state->first)}});
        };
        connect(reply, &QNetworkReply::socketStartedConnecting, reply, [snapshot] { snapshot(QStringLiteral("socket_connect_start")); });
        connect(reply, &QNetworkReply::requestSent, reply, [snapshot] { snapshot(QStringLiteral("request_sent")); });
        connect(reply, &QNetworkReply::encrypted, reply, [this, reply, id, snapshot] {
            snapshot(QStringLiteral("tls_encrypted"));
            const auto ssl = reply->sslConfiguration();
            QJsonArray chain;
            for (const auto &certificate : ssl.peerCertificateChain()) chain.append(QJsonObject{
                {"subject", certificate.subjectInfo(QSslCertificate::CommonName).join(',')},
                {"issuer", certificate.issuerInfo(QSslCertificate::CommonName).join(',')},
                {"sha256", QString::fromLatin1(certificate.digest(QCryptographicHash::Sha256).toHex())},
                {"expires", certificate.expiryDate().toString(Qt::ISODate)}});
            log->event("tls_details", {{"id", id}, {"cipher", ssl.sessionCipher().name()},
                {"protocol", ssl.sessionCipher().protocolString()}, {"alpn", QString::fromLatin1(ssl.nextNegotiatedProtocol())}, {"certificates", chain}});
        });
        connect(reply, &QNetworkReply::sslErrors, reply, [this, id](const QList<QSslError> &errors) {
            QJsonArray list; for (const auto &error : errors) list.append(error.errorString());
            log->event("tls_errors", {{"id", id}, {"errors", list}}); // Never ignore certificate errors.
        });
        connect(reply, &QNetworkReply::redirected, reply, [this, state, id](const QUrl &target) {
            ++state->redirects; log->event("redirect", {{"id", id}, {"target", safeUrl(target)}, {"number", state->redirects}});
        });
        connect(reply, &QNetworkReply::metaDataChanged, reply, [this, reply, id, snapshot] {
            if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 429)
                log->throttled = true;
            snapshot(QStringLiteral("headers"));
            QJsonObject headers;
            for (const QByteArray &key : {QByteArray("Content-Length"), QByteArray("Content-Type"), QByteArray("Content-Range"),
                QByteArray("Accept-Ranges"), QByteArray("ETag"), QByteArray("Last-Modified"), QByteArray("Server"), QByteArray("Via"),
                QByteArray("CF-Ray"), QByteArray("CF-Cache-Status"), QByteArray("X-Cache"), QByteArray("Age"), QByteArray("Retry-After")})
                if (reply->hasRawHeader(key)) headers.insert(QString::fromLatin1(key), QString::fromLatin1(reply->rawHeader(key)));
            log->event("response_headers", {{"id", id}, {"status", reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()}, {"headers", headers}});
            if (reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > log->budget) {
                log->budgetExceeded = true; log->event("body_budget_exceeded", {{"id", id}}); reply->abort();
            }
        });
        connect(reply, &QIODevice::readyRead, reply, [this, reply, state, started, id, snapshot] {
            if (state->first < 0 && reply->bytesAvailable() > 0) {
                state->first = log->clock.elapsed() - started; log->firstBytes.append(state->first);
                snapshot(QStringLiteral("first_body_byte"));
            }
        });
        connect(reply, &QNetworkReply::downloadProgress, reply, [this, reply, state, id](qint64 bytes, qint64 total) {
            if (bytes > state->bytes) { log->transferred += bytes - state->bytes; state->bytes = bytes; }
            if (log->transferred > log->budget && !log->budgetExceeded) {
                log->budgetExceeded = true; log->event("body_budget_exceeded", {{"id", id}, {"reported_bytes", double(log->transferred)}}); reply->abort();
            }
            Q_UNUSED(total);
        });
        auto *heartbeat = new QTimer(reply); heartbeat->setInterval(1000);
        connect(heartbeat, &QTimer::timeout, reply, [snapshot] { snapshot(QStringLiteral("sample")); }); heartbeat->start();
        connect(reply, &QNetworkReply::finished, reply, [this, reply, state, id, started, heartbeat] {
            heartbeat->stop();
            const auto h2 = reply->attribute(QNetworkRequest::Http2WasUsedAttribute);
            const QString protocol = !h2.isValid() ? QStringLiteral("unknown") : h2.toBool() ? QStringLiteral("h2") : QStringLiteral("h1");
            ++log->protocols[protocol];
            log->event("request_finished", {{"id", id}, {"url", safeUrl(reply->url())}, {"protocol", protocol},
                {"status", reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()}, {"error_code", int(reply->error())},
                {"error", reply->errorString()}, {"reported_bytes", double(state->bytes)}, {"request_ms", double(log->clock.elapsed() - started)},
                {"first_byte_ms", double(state->first)}, {"redirects", state->redirects}});
        });
        return reply;
    }
};
}
