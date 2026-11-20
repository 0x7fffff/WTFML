#include "Support.hpp"
#include "auth/AccountStore.hpp"
#include "auth/AuthManager.hpp"
#include "auth/OAuthEndpoints.hpp"
#include "auth/TokenStore.hpp"

#include <QTcpServer>
#include <QTcpSocket>

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

    // ---- ZB. 按需刷新策略（6 小时窗口）、失败分类与「需要重新登录」状态 ----
    section("ZB. 凭据按需刷新策略与失败分类");
    {
        using Kind = AuthManager::RefreshFailureKind;
        check(AuthManager::classifyRefreshFailure(-1, QStringLiteral("Connection refused")) == Kind::Network &&
                  AuthManager::classifyRefreshFailure(0, QString()) == Kind::Network &&
                  AuthManager::classifyRefreshFailure(429, QString()) == Kind::Network &&
                  AuthManager::classifyRefreshFailure(503, QString()) == Kind::Network &&
                  AuthManager::classifyRefreshFailure(400, QString(), QStringLiteral("temporarily_unavailable")) ==
                      Kind::Network,
              "ZB1 网络超时/连接失败/429/5xx 分类为 Network");
        check(AuthManager::classifyRefreshFailure(400, QString(), QStringLiteral("invalid_grant")) ==
                      Kind::ReloginRequired &&
                  AuthManager::classifyRefreshFailure(400, QString(), QStringLiteral("interaction_required")) ==
                      Kind::ReloginRequired &&
                  AuthManager::classifyRefreshFailure(401, QString()) == Kind::ReloginRequired,
              "ZB1 invalid_grant / interaction_required / 401 分类为 ReloginRequired");
        check(AuthManager::classifyRefreshFailure(403, QString()) == Kind::Other &&
                  AuthManager::classifyRefreshFailure(404, QString()) == Kind::Other,
              "ZB1 403/404 等非网络/非刷新失效错误分类为 Other");
    }

    // 搭建本机模拟认证服务，验证真实 HTTP 刷新链
    {
        struct MockAuthServer {
            QTcpServer server;
            int requestCount = 0;
            int oauthStatus = 200;
            QByteArray oauthErrorBody;
            QString issuedMcToken = QStringLiteral("mc-refreshed-token");

            bool start() {
                if (!server.listen(QHostAddress::LocalHost, 0)) return false;
                QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
                    while (auto *sock = server.nextPendingConnection()) {
                        QObject::connect(sock, &QTcpSocket::disconnected, sock, &QTcpSocket::deleteLater);
                        QObject::connect(sock, &QTcpSocket::readyRead, sock, [this, sock] {
                            QByteArray buf = sock->property("buf").toByteArray() + sock->readAll();
                            sock->setProperty("buf", buf);
                            const qsizetype hdrEnd = buf.indexOf("\r\n\r\n");
                            if (hdrEnd < 0) return;
                            int contentLen = 0;
                            for (const QByteArray &line : buf.left(hdrEnd).split('\n')) {
                                if (line.trimmed().toLower().startsWith("content-length:")) {
                                    contentLen = line.trimmed().mid(15).trimmed().toInt();
                                }
                            }
                            if (buf.size() < hdrEnd + 4 + contentLen) return;
                            const QList<QByteArray> firstLine = buf.left(buf.indexOf("\r\n")).split(' ');
                            const QByteArray path = firstLine.value(1);
                            ++requestCount;
                            int code = 200;
                            QByteArray body = QByteArrayLiteral("{}");
                            if (path == "/token") {
                                code = oauthStatus;
                                body = oauthStatus == 200
                                           ? QByteArrayLiteral("{\"access_token\":\"ms-new\",\"refresh_token\":\"rt-new\"}")
                                           : oauthErrorBody;
                            } else if (path == "/xbl") {
                                body = QByteArrayLiteral("{\"Token\":\"xbl-tok\",\"DisplayClaims\":{\"xui\":[{\"uhs\":\"u123\"}]}}");
                            } else if (path == "/xsts") {
                                body = QByteArrayLiteral("{\"Token\":\"xsts-tok\",\"DisplayClaims\":{\"xui\":[{\"uhs\":\"u123\"}]}}");
                            } else if (path == "/mc") {
                                body = QStringLiteral("{\"access_token\":\"%1\",\"expires_in\":86400}")
                                           .arg(issuedMcToken)
                                           .toUtf8();
                            } else if (path == "/profile") {
                                body = QByteArrayLiteral("{\"id\":\"11111111111111111111111111111111\",\"name\":\"Alice\"}");
                            } else {
                                code = 404;
                            }
                            sock->write("HTTP/1.1 " + QByteArray::number(code) +
                                        (code == 200 ? " OK" : " Bad Request") +
                                        "\r\nContent-Type: application/json\r\nContent-Length: " +
                                        QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n");
                            sock->write(body);
                            sock->disconnectFromHost();
                        });
                    }
                });
                return true;
            }
            QString baseUrl() const {
                return QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
            }
        } mock;

        check(mock.start(), "ZB2 本机模拟认证服务启动成功");
        AuthManager::EndpointOverrides overrides;
        overrides.oauthTokenUrl = mock.baseUrl() + QStringLiteral("/token");
        overrides.xboxLiveAuthUrl = mock.baseUrl() + QStringLiteral("/xbl");
        overrides.xstsAuthUrl = mock.baseUrl() + QStringLiteral("/xsts");
        overrides.minecraftLoginUrl = mock.baseUrl() + QStringLiteral("/mc");
        overrides.minecraftProfileUrl = mock.baseUrl() + QStringLiteral("/profile");
        overrides.clientId = QStringLiteral("test-client-id");
        AuthManager::setEndpointOverridesForTest(overrides);

        const QString uuidA = QStringLiteral("11111111111111111111111111111111");
        const QString uuidB = QStringLiteral("22222222222222222222222222222222");
        TokenStore::clear(TokenStore::microsoftKey());
        TokenStore::clear(TokenStore::minecraftKey());
        TokenStore::clear(TokenStore::accountsKey());
        TokenStore::clear(TokenStore::currentUuidKey());

        const auto seedAccount = [](const QString &uuid, const QString &name,
                                    const QString &mcToken, int secsLeft) {
            QJsonObject ms{{QStringLiteral("access_token"), QStringLiteral("ms-") + name},
                           {QStringLiteral("refresh_token"), QStringLiteral("rt-") + name}};
            QJsonObject mc{{QStringLiteral("access_token"), mcToken},
                           {QStringLiteral("expires_at"),
                            QDateTime::currentDateTimeUtc().addSecs(secsLeft).toString(Qt::ISODate)},
                           {QStringLiteral("name"), name},
                           {QStringLiteral("uuid"), uuid}};
            return TokenStore::writeJson(TokenStore::microsoftKey(), ms) &&
                   TokenStore::writeJson(TokenStore::minecraftKey(), mc) &&
                   AccountStore::captureActive();
        };

        // 1) 剩余 > 6 小时：不刷新、0 个请求
        check(seedAccount(uuidA, QStringLiteral("Alice"), QStringLiteral("mc-fresh"),
                          AuthManager::tokenRefreshWindowSecs + 3600),
              "ZB2 准备剩余 > 6h 的账号 Alice");
        AuthManager auth;
        QString err;
        check(auth.loadSavedToken(&err) == QStringLiteral("mc-fresh") && mock.requestCount == 0 &&
                  auth.lastRefreshFailureKind() == AuthManager::RefreshFailureKind::None,
              "ZB2 剩余 > 6h 时直接返回旧令牌且不发网络请求", err);

        // 2) 剩余 < 6 小时（但未过期，例如剩 5 小时）：触发刷新
        check(seedAccount(uuidA, QStringLiteral("Alice"), QStringLiteral("mc-expiring"),
                          AuthManager::tokenRefreshWindowSecs - 3600),
              "ZB3 准备剩余 5h（< 6h 但未过期）的账号 Alice");
        mock.requestCount = 0;
        mock.issuedMcToken = QStringLiteral("mc-renewed-5h");
        err.clear();
        check(auth.loadSavedToken(&err) == QStringLiteral("mc-renewed-5h") &&
                  mock.requestCount == 5 && !auth.needsRelogin() &&
                  !AccountStore::needsRelogin(uuidA),
              "ZB3 剩余 < 6h 时按统一策略触发完整链刷新", err);

        // 3) 网络不通（关闭端口）：分类为 Network，不标「需要重新登录」，保留现有凭据
        check(seedAccount(uuidA, QStringLiteral("Alice"), QStringLiteral("mc-keep-local"), 1800),
              "ZB4 准备剩余 30 分钟的账号 Alice");
        QTcpServer deadPort;
        check(deadPort.listen(QHostAddress::LocalHost, 0), "ZB4 分配一个临时关闭端口");
        const quint16 closedPort = deadPort.serverPort();
        deadPort.close();
        AuthManager::EndpointOverrides deadOverrides = overrides;
        deadOverrides.oauthTokenUrl = QStringLiteral("http://127.0.0.1:%1/token").arg(closedPort);
        AuthManager::setEndpointOverridesForTest(deadOverrides);
        err.clear();
        check(auth.loadSavedToken(&err).isEmpty() && !err.isEmpty() &&
                  auth.lastRefreshFailureKind() == AuthManager::RefreshFailureKind::Network &&
                  !auth.needsRelogin() && !AccountStore::needsRelogin(uuidA) &&
                  auth.loggedIn() && auth.savedAccessToken() == QStringLiteral("mc-keep-local"),
              "ZB4 断网/连接失败分类为 Network，不置需要重新登录且保留本地档案与令牌", err);

        // 4) 微软返回 invalid_grant：分类为 ReloginRequired，标记「需要重新登录」，切走再切回仍保留
        AuthManager::setEndpointOverridesForTest(overrides);
        int reloginChanges = 0;  // 标记真变化才发 needsReloginChanged()，账号页靠它刷新列表
        QObject::connect(&auth, &AuthManager::needsReloginChanged, &auth, [&reloginChanges] { ++reloginChanges; });
        mock.oauthStatus = 400;
        mock.oauthErrorBody = QByteArrayLiteral(
            "{\"error\":\"invalid_grant\",\"error_description\":\"AADSTS70000: expired refresh token\"}");
        err.clear();
        SavedAccount foundA;
        check(auth.loadSavedToken(&err).isEmpty() &&
                  auth.lastRefreshFailureKind() == AuthManager::RefreshFailureKind::ReloginRequired &&
                  auth.needsRelogin() && AccountStore::needsRelogin(uuidA) &&
                  AccountStore::find(uuidA, &foundA) && foundA.needsRelogin,
              "ZB5 invalid_grant 分类为 ReloginRequired 并持久化「需要重新登录」状态", err);
        check(reloginChanges == 1, "ZB5 打上标记发一次 needsReloginChanged()", QString::number(reloginChanges));

        // 添加另一个健康账号 B 并切过去；再切回 A 时应恢复「需要重新登录」查询结果
        check(seedAccount(uuidB, QStringLiteral("Bob"), QStringLiteral("mc-bob"), 86400),
              "ZB5 准备健康账号 Bob");
        auth.reloadStoredAccount();
        check(!auth.needsRelogin() && !AccountStore::needsRelogin(uuidB) &&
                  AccountStore::needsRelogin(uuidA),
              "ZB5 切到 Bob 后 AuthManager::needsRelogin() 为 false，Alice 仍保持标记");
        check(AccountStore::activate(uuidA), "ZB5 切回 Alice");
        auth.reloadStoredAccount();
        check(auth.needsRelogin() && AccountStore::needsRelogin(uuidA),
              "ZB5 切回 Alice 并 reloadStoredAccount() 后仍能查出需要重新登录");

        // 5) 再次刷新成功（模拟重新登录 / 续期恢复）：清除「需要重新登录」标记
        mock.oauthStatus = 200;
        mock.issuedMcToken = QStringLiteral("mc-recovered");
        err.clear();
        check(auth.loadSavedToken(&err) == QStringLiteral("mc-recovered") &&
                  !auth.needsRelogin() && !AccountStore::needsRelogin(uuidA) &&
                  AccountStore::find(uuidA, &foundA) && !foundA.needsRelogin,
              "ZB6 重新刷新成功后清除「需要重新登录」状态", err);
        check(reloginChanges == 2, "ZB6 清掉标记再发一次 needsReloginChanged()", QString::number(reloginChanges));

        AuthManager::setEndpointOverridesForTest({});
        TokenStore::clear(TokenStore::microsoftKey());
        TokenStore::clear(TokenStore::minecraftKey());
        TokenStore::clear(TokenStore::accountsKey());
        TokenStore::clear(TokenStore::currentUuidKey());
    }
}

