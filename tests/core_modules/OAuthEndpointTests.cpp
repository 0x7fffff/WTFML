#include "Support.hpp"
#include "auth/AuthManager.hpp"
#include "auth/OAuthEndpoints.hpp"

void runOAuthEndpointTests() {
    using namespace OAuthEndpoints;
    section("ZA. 微软登录动态回调、S256 PKCE 与 state");

    if (!AuthManager::loginConfigurationError().isEmpty()) {
        AuthManager auth;
        QString failure;
        int failures = 0;
        QObject::connect(&auth, &AuthManager::loginFailed, &auth, [&](const QString &message) {
            failure = message;
            ++failures;
        });
        check(!auth.loginMicrosoft() && !auth.loginInFlight(),
              "ZA0 缺少登录构建配置时拒绝开启授权，不进入等待状态");
        check(failures == 1 && failure == AuthManager::loginConfigurationError(),
              "ZA0 同步给出一次可读的配置错误，界面可以恢复按钮");
    }

    const QUrl defaultBase = loopbackRedirectBase(QStringLiteral("http://localhost"));
    const QUrl customBase = loopbackRedirectBase(QStringLiteral("http://localhost:8080/OAuth%20Callback"));
    check(defaultBase == QUrl(QStringLiteral("http://localhost/")),
          "ZA1 Entra 的 localhost 配置采用根路径");
    check(customBase == QUrl(QStringLiteral("http://localhost/OAuth%20Callback")),
          "ZA1 旧构建配置保留注册路径，由系统分配端口");
    const QStringList invalidBases{
        QString(),
        QStringLiteral("localhost"),
        QStringLiteral("http://user@localhost/"),
        QStringLiteral("http://localhost/?q=1"),
        QStringLiteral("http://localhost/#"),
        QStringLiteral("https://localhost/"),
        QStringLiteral("http://127.0.0.1/"),
        QStringLiteral("http://localhost.example.invalid/"),
        QStringLiteral("https://login.microsoftonline.com/common/oauth2/nativeclient"),
    };
    check(std::all_of(invalidBases.cbegin(), invalidBases.cend(),
                      [](const QString &uri) { return loopbackRedirectBase(uri).isEmpty(); }),
          "ZA1 拒绝含凭据、查询、片段及不受支持的协议或主机的回调配置");

    QTcpServer firstListener;
    QTcpServer secondListener;
    const QUrl firstUri = listenForCallback(firstListener, defaultBase);
    const QUrl secondUri = listenForCallback(secondListener, customBase);
    check(firstListener.isListening() && secondListener.isListening() &&
              firstListener.serverAddress().isLoopback() && secondListener.serverAddress().isLoopback() &&
              firstListener.serverPort() != 0 && secondListener.serverPort() != 0 &&
              firstListener.serverPort() != secondListener.serverPort(),
          "ZA2 两个同时开启的监听各占用一个系统分配的回环端口");
    check(firstUri.port() == firstListener.serverPort() && firstUri.path() == QStringLiteral("/") &&
              secondUri.port() == secondListener.serverPort() && secondUri.path() == customBase.path(),
          "ZA2 回调 URI 包含实际端口与配置路径");

    QUrlQuery authorization;
    QUrlQuery tokenExchange;
    addRedirectUri(authorization, secondUri);
    addRedirectUri(tokenExchange, secondUri);
    QUrl authorizationUrl(QStringLiteral("https://example.invalid/authorize"));
    authorizationUrl.setQuery(authorization);
    const auto wireAuthorization = QUrlQuery(QUrl::fromEncoded(authorizationUrl.toEncoded()));
    const auto wireTokenExchange = QUrlQuery(tokenExchange.query(QUrl::FullyEncoded));
    const QString expectedUri = secondUri.toString(QUrl::FullyEncoded);
    check(wireAuthorization.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded) == expectedUri &&
              wireTokenExchange.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded) == expectedUri,
          "ZA2 授权与令牌交换保留同一个完整 URI，包括路径中的百分号编码");

    const QString verifier = randomToken();
    const QString state = randomToken();
    const QRegularExpression tokenPattern(QStringLiteral("\\A[A-Za-z0-9_-]{43}\\z"));
    check(tokenPattern.match(verifier).hasMatch() && tokenPattern.match(state).hasMatch() && verifier != state,
          "ZA3 为 PKCE verifier 和 state 分别生成 32 字节的随机值");
    check(codeChallenge(QStringLiteral("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk")) ==
              QStringLiteral("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"),
          "ZA3 S256 结果匹配 RFC 7636 附录 B");

    const QUrl expectedCallback(QStringLiteral("http://localhost:54321/OAuth%20Callback"));
    const auto valid = callbackParameters(
        "GET /OAuth%20Callback?state=active&code=sample%2Bcode HTTP/1.1", expectedCallback,
        QStringLiteral("active"));
    check(valid && valid->queryItemValue(QStringLiteral("code"), QUrl::FullyDecoded) ==
                       QStringLiteral("sample+code"),
          "ZA4 仅本次 state 与已注册路径匹配时接收授权码");

    const QList<QByteArray> invalidRequests{
        "GET /OAuth%20Callback?code=sample HTTP/1.1",
        "GET /OAuth%20Callback?state=old&code=sample HTTP/1.1",
        "GET /OAuth%20Callback?state=active&state=old&code=sample HTTP/1.1",
        "GET /oauth%20Callback?state=active&code=sample HTTP/1.1",
        "GET /favicon.ico?state=active HTTP/1.1",
        "POST /OAuth%20Callback?state=active&code=sample HTTP/1.1",
        "GET //other.invalid/OAuth%20Callback?state=active HTTP/1.1",
    };
    check(std::all_of(invalidRequests.cbegin(), invalidRequests.cend(), [&](const QByteArray &line) {
              return !callbackParameters(line, expectedCallback, QStringLiteral("active"));
          }) && !callbackParameters("GET /OAuth%20Callback?state=active HTTP/1.1", expectedCallback, {}),
          "ZA4 忽略缺失、旧的或重复的 state，以及路径或方法不匹配的请求");
}

