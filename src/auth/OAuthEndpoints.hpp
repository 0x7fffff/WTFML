/**
 * @file OAuthEndpoints.hpp
 * @brief 微软 OAuth 的动态回调、PKCE 与 state 校验。
 */

#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QHostAddress>
#include <QRandomGenerator>
#include <QString>
#include <QStringList>
#include <QTcpServer>
#include <QUrl>
#include <QUrlQuery>

#include <optional>

namespace OAuthEndpoints {

/// Entra 只对 localhost 忽略端口。保留旧构建配置的路径，端口统一交给系统分配。
inline QUrl loopbackRedirectBase(const QString &configuredUri) {
    QUrl url(configuredUri, QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != QStringLiteral("http") ||
        url.host() != QStringLiteral("localhost") || url.authority().contains(u'@') ||
        url.hasQuery() || url.hasFragment()) {
        return {};
    }
    url.setPort(-1);
    if (url.path().isEmpty()) {
        url.setPath(QStringLiteral("/"));
    }
    return url;
}

/// 监听成功后立刻使用已占有的端口组装 URI，避免查空闲端口和实际绑定之间的竞态。
inline QUrl listenForCallback(QTcpServer &server, QUrl redirectBase) {
    if (redirectBase.isEmpty() || !server.listen(QHostAddress::LocalHost, 0)) {
        return {};
    }
    redirectBase.setPort(server.serverPort());
    return redirectBase;
}

inline QString randomToken() {
    quint32 words[8];
    QRandomGenerator::system()->fillRange(words);
    const auto bytes = QByteArray::fromRawData(reinterpret_cast<const char *>(words), sizeof(words));
    return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

inline QString codeChallenge(const QString &verifier) {
    const auto hash = QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

inline void addRedirectUri(QUrlQuery &parameters, const QUrl &redirectUri) {
    // redirect_uri 是嵌套在参数里的完整 URI，路径已有的百分号也要编码一层。
    parameters.addQueryItem(QStringLiteral("redirect_uri"),
                            QString::fromLatin1(QUrl::toPercentEncoding(redirectUri.toString(QUrl::FullyEncoded))));
}

/// 回调的目标必须匹配本次路径，并恰好带有一个与当前请求相符的 state。
inline std::optional<QUrlQuery> callbackParameters(const QByteArray &requestLine,
                                                   const QUrl &redirectUri,
                                                   const QString &expectedState) {
    const auto parts = requestLine.split(' ');
    if (expectedState.isEmpty() || parts.size() != 3 || parts.first() != "GET" ||
        !parts.at(1).startsWith('/') || parts.at(1).startsWith("//")) {
        return std::nullopt;
    }
    const QUrl target = QUrl::fromEncoded(parts.at(1), QUrl::StrictMode);
    if (!target.isValid() || target.hasFragment() ||
        target.path(QUrl::FullyEncoded) != redirectUri.path(QUrl::FullyEncoded)) {
        return std::nullopt;
    }
    const QUrlQuery query(target);
    if (query.allQueryItemValues(QStringLiteral("state")) != QStringList{expectedState}) {
        return std::nullopt;
    }
    return query;
}

}  // namespace OAuthEndpoints
