/**
 * @file AuthManager.cpp
 * @brief 执行微软 OAuth 登录、换出 Minecraft 令牌并存储登录信息。
 * @author LoserKugua
 * @date 2026-04-10
 * @note 处理基于浏览器的登录重定向、回调处理、Xbox/XSTS/Minecraft 令牌链、存取与刷新。
 *
 * 这个文件里的网络请求都是**同步**的（QEventLoop + 超时），因为调用方是界面上的一次
 * 点击或者一次启动：那两条路本来就等着一个结果，异步化只会让调用点多出一堆状态机。
 * 内嵌事件循环期间界面仍然会重画（Qt 在处理事件），所以"正在登录…"是看得见的。
 */

#include "AuthManager.hpp"

#include "TokenStore.hpp"      // 凭据落盘的字节格式（唯一出处）
#include "../net/HttpClient.hpp"  // 同步 HTTP（唯一出处）

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDesktopServices>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {
static constexpr const char kClientId[] = "1f3dc837-04f1-4389-947e-d698b69571b7";
// NOTE: 两份凭据落在哪些键上、以及那串字节怎么保护，都**不在这里** ——
//       见 TokenStore.hpp（多账号列表里的副本必须和活动槽是同一套格式，所以它只有一个
//       出处）。这一层要知道的只有"键名从 TokenStore 取"。
static constexpr const char kOAuthAuthorizeUrl[] =
    "https://login.microsoftonline.com/consumers/oauth2/v2.0/authorize";
static constexpr const char kOAuthTokenUrl[] =
    "https://login.microsoftonline.com/consumers/oauth2/v2.0/token";
static constexpr const char kXboxLiveAuthUrl[] = "https://user.auth.xboxlive.com/user/authenticate";
static constexpr const char kXstsAuthUrl[] = "https://xsts.auth.xboxlive.com/xsts/authorize";
static constexpr const char kMinecraftLoginUrl[] =
    "https://api.minecraftservices.com/authentication/login_with_xbox";
static constexpr const char kMinecraftProfileUrl[] =
    "https://api.minecraftservices.com/minecraft/profile";
static constexpr int kCallbackPort = 38226;
static constexpr const char kRedirectUri[] = "http://localhost:38226/";
static constexpr const char kOAuthScope[] = "XboxLive.signin offline_access";
static constexpr int kRequestTimeoutMs = 15000;
// 等浏览器回来的上限。见 AuthManager::loginInFlight() 的说明。
static constexpr int kLoginTimeoutMinutes = 5;
// NOTE: 值和 LauncherManager 的 kDefaultPlayerName 一致。那边用它兜底（调用方没给名字时），
//       这里是没有账户时的"名字"。两处都写死同一个字面量是因为它们各有各的来路，
//       谁都不该 include 对方 —— 改的时候一起改。
static constexpr const char kDefaultPlayerName[] = "Player";

/// NOTE: 这里只剩个**名字**：真正的结构在 HttpClient.hpp（那条同步请求的规矩、含"超时了
///       要 abort、abort 之后别读正文"那一串）只有一份，登录链和皮肤接口共用。留这行别名
///       是为了让下面那几十处调用点一个都不用动 —— 搬家的改动越小越不容易搬出 bug。
using HttpResult = HttpClient::Reply;

/// 发一个 JSON 请求并同步等结果（登录链用的都是 GET/POST，所以这里就这一个开关）。
/// @param post true 发 POST（body 就是请求体），false 发 GET。
/// @param contentType 空则不加 Content-Type 头（GET 不需要）。
/// @param bearer 非空则加 Authorization: Bearer 头。
HttpResult sendJson(QNetworkAccessManager *manager, const QUrl &url, bool post,
                    const QByteArray &body, const QByteArray &contentType,
                    const QByteArray &bearer) {
    return HttpClient::send(manager, url, post ? HttpClient::Verb::Post : HttpClient::Verb::Get,
                            body, contentType, bearer, kRequestTimeoutMs);
}

/// 失败时那句"…失败：<为什么>"。有网络层的错就说网络层的，否则说 HTTP 状态码。
QString httpFailureText(const HttpResult &result, const QString &what) {
    if (!result.networkError.isEmpty()) {
        return QObject::tr("%1：%2").arg(what, result.networkError);
    }

    // 429：限流。**必须**在下面那条通用分支之前拦下 —— 通用分支认的正是正文里那个
    // `error` 字段（Minecraft 服务 429 时它就是 "TOO_MANY_REQUESTS"），端给用户等于
    // 说了句他照做不了的话。文案只有一个出处，皮肤披风那条路说的是同一句。
    if (result.status == HttpClient::kStatusTooManyRequests) {
        return HttpClient::throttledText(what);
    }

    // 有些端点把原因写在正文里（Minecraft 服务是 errorMessage，Xbox 是 Message）。
    // 只认这几个字段、不把正文原样丢出去：正文里还有 traceId / path / 令牌之类的东西。
    // NOTE: 这一句是"HTTP 403"和"HTTP 403：Invalid app registration"的区别 —— 后者才是
    //       用户能拿去查、能照着做的那句话（真机上折在 login_with_xbox 时就是它）。
    for (const char *key : {"errorMessage", "Message", "error"}) {
        const QString detail = result.json.value(QString::fromLatin1(key)).toString();
        if (!detail.isEmpty()) {
            return QObject::tr("%1（HTTP %2）：%3").arg(what).arg(result.status).arg(detail);
        }
    }
    return QObject::tr("%1（HTTP %2）。").arg(what).arg(result.status);
}

/// OAuth 令牌端点的失败正文长这样：{"error": "...", "error_description": "..."}。
/// 优先报 description —— invalid_grant 这种错误码对用户没有意义，描述里那句
/// "the user or administrator has not consented" 才有。**不**把正文原样丢出去：
/// 里面还有 trace id 之类的东西。
/// 正文怎么翻在 AuthManager::oauthErrorText() 里（纯函数，验证工程钉得住）；这里只管
/// 从响应正文里把两个字段取出来，以及"这压根不是 OAuth 的失败正文"那条分支。
QString oauthFailureText(const HttpResult &result, const QString &what) {
    const QString code = result.json.value("error").toString();
    if (code.isEmpty()) {
        return httpFailureText(result, what);
    }
    return AuthManager::oauthErrorText(code,
                                      result.json.value("error_description").toString(), what);
}

/// XBL/XSTS 的 DisplayClaims.xui[0].uhs —— 账户在 Xbox 那边的标识（user hash）。
/// XSTS 的响应里也会带一遍，所以 XSTS 那步拿它覆盖 XBL 那步的值。
QString firstUserHash(const QJsonObject &json) {
    const QJsonArray xui = json.value("DisplayClaims").toObject().value("xui").toArray();
    return xui.isEmpty() ? QString() : xui.first().toObject().value("uhs").toString();
}
}  // namespace

AuthManager::AuthManager(QObject *parent)
    : QObject(parent),
      // NOTE: 默认玩家名**不是界面文案，别翻译** —— 它就是启动游戏时 --username 的
      //       值（离线与正版两条分支都取它），也是"还没登录"时的占位名字。
      //       登录成功之后会被档案里的真名替掉，见 applyMinecraftTokenData()。
      m_playerName(QString::fromLatin1(kDefaultPlayerName)),
      m_callbackServer(new QTcpServer(this)),
      m_networkManager(new QNetworkAccessManager(this)),
      m_callbackPort(kCallbackPort) {
    connect(m_callbackServer, &QTcpServer::newConnection, this, &AuthManager::handleNewConnection);
    // 开窗时把上次登录的账户读回来：底栏一上来就显示上次那个名字，而不是占位的 "Player"。
    // 这一句**不碰网络**（loadMinecraftTokenData() 只读一遍 QSettings、再解一层
    // DPAPI，见 TokenStore），
    // 所以放构造函数里是安全的 —— 要换令牌得走 loadSavedToken()，那会发请求。
    // NOTE: 以前这一句是 loadStoredTokenData()，const 方法、返回值直接丢掉，等于什么都没做。
    applyMinecraftTokenData(loadMinecraftTokenData());
}

bool AuthManager::loginMicrosoft() {
    if (m_loginInFlight) {
        // 已经在等了。这里**不能**重开：换掉 code verifier 会让已经打开的那个浏览器
        // 标签页的回调作废（微软那头回 invalid_grant），用户看着登录页莫名其妙失败。
        return false;
    }

    if (!startCallbackServer()) {
        emit loginFailed(tr("无法启动回调服务器（端口 %1 被占用？）。").arg(m_callbackPort));
        return false;
    }

    m_codeVerifier = createCodeVerifier();
    const QUrl authUrl(buildAuthUrl());
    if (!QDesktopServices::openUrl(authUrl)) {
        m_callbackServer->close();
        emit loginFailed(tr("无法打开默认浏览器。"));
        return false;
    }

    m_loginInFlight = true;
    if (m_loginTimeout == nullptr) {
        m_loginTimeout = new QTimer(this);
        m_loginTimeout->setSingleShot(true);
        connect(m_loginTimeout, &QTimer::timeout, this, [this] {
            m_callbackServer->close();
            m_loginInFlight = false;
            // 走 loginFailed 而不是自己憋着：界面就是靠它把几个按钮放回可点状态的。
            // NOTE: 这句**不许点名某个按钮** —— 原来写的是「再点一次"微软登录"重来」，
            //       而那个按钮已经并进「账号与外观」那个窗里了（设置页那格现在只是一个
            //       入口）。用户手上看到的文案会变，这里只说下一步做什么。
            emit loginFailed(tr("登录没有完成（等了 %1 分钟，浏览器那头一直没回来）。\n"
                                "再试一次，或者检查一下网络和代理设置。")
                                 .arg(kLoginTimeoutMinutes));
        });
    }
    m_loginTimeout->start(kLoginTimeoutMinutes * 60 * 1000);

    emit loginStarted();
    return true;
}

bool AuthManager::loginInFlight() const { return m_loginInFlight; }

QString AuthManager::loadSavedToken(QString *errorMessage) {
    const auto fail = [errorMessage](const QString &message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return QString();
    };

    // 盘上那份还在有效期内就直接用：启动游戏不该每次都先绕一圈微软（要好几秒，还得联网）。
    if (applyMinecraftTokenData(loadMinecraftTokenData()) && m_tokenExpiry.isValid() &&
        QDateTime::currentDateTimeUtc() < m_tokenExpiry) {
        return m_accessToken;
    }

    QString failure;
    if (!refreshMinecraftToken(&failure)) {
        return fail(failure);
    }
    return m_accessToken;
}

bool AuthManager::refreshMinecraftToken(QString *errorMessage) {
    const auto fail = [errorMessage](const QString &message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };

    const QString refreshToken = loadMicrosoftTokenData().value("refresh_token").toString();
    if (refreshToken.isEmpty()) {
        return fail(tr("还没有登录过，或者登录信息已经不完整了。"));
    }

    QString microsoftToken;
    QString failure;
    if (!requestMicrosoftTokenRefresh(refreshToken, &microsoftToken, &failure) ||
        !runMinecraftChain(microsoftToken, &failure)) {
        // 走到这儿的原因多数是网络或者账户本身的问题，写一行给命令行看
        // （用户那边由调用方弹窗，见 loadSavedToken() 的 errorMessage）。
        qWarning().noquote() << "刷新登录失败：" << failure;
        return fail(failure);
    }
    return true;
}

void AuthManager::reloadStoredAccount() {
    // applyMinecraftTokenData() 认不下来（凭据缺字段）时**什么都不改** —— 那是"没读到"
    // 而不是"读到一份空的"，所以上一个人的名字得自己清掉，否则底栏会继续显示一个
    // 已经没有凭据的账号。
    if (!applyMinecraftTokenData(loadMinecraftTokenData())) {
        m_accessToken.clear();
        m_tokenExpiry = QDateTime();
        m_uuid.clear();
        m_playerName = QString::fromLatin1(kDefaultPlayerName);
    }
}

QString AuthManager::playerName() const { return m_playerName; }

void AuthManager::setOfflinePlayer(const QString &name) {
    m_accessToken.clear();
    m_tokenExpiry = QDateTime();
    m_uuid.clear();
    m_playerName = name.isEmpty() ? QString::fromLatin1(kDefaultPlayerName) : name;
}

bool AuthManager::loggedIn() const { return !m_uuid.isEmpty(); }

QString AuthManager::playerUuid() const { return dashedUuid(m_uuid); }

QString AuthManager::dashedUuid(const QString &raw) {
    QString hex = raw;
    hex.remove(QLatin1Char('-'));
    if (hex.size() != 32) {
        return QString();
    }

    QString out;
    out.reserve(36);
    for (int i = 0; i < hex.size(); ++i) {
        if (i == 8 || i == 12 || i == 16 || i == 20) {
            out.append(QLatin1Char('-'));
        }
        out.append(hex.at(i));
    }
    return out;
}

QString AuthManager::oauthErrorText(const QString &code, const QString &description,
                                    const QString &what) {
    // 微软的描述尾巴是 "Trace ID: … Correlation ID: … Timestamp: …"：对用户没有意义，
    // 也和这个文件里别处的规矩一致（httpFailureText 只认几个字段、不把正文原样丢出去）。
    QString detail = description;
    const int traceAt = detail.indexOf(QStringLiteral("Trace ID:"));
    if (traceAt > 0) {
        detail = detail.left(traceAt).trimmed();
    }

    // 见头文件那条 NOTE：这张 refresh_token 属于**另一个应用**，换不出来是必然的，
    // 重试没有意义。说清"只能重新登录"，别让用户去查网络或者账号。
    if (description.contains(QStringLiteral("AADSTS70000"), Qt::CaseInsensitive)) {
        return tr("%1：这份登录凭据是另一个应用签发的，当前这个应用换不了它（AADSTS70000）。\n"
                  "重新登录一次就会作废旧的、存下新的，重试没有用。")
            .arg(what);
    }

    if (!code.isEmpty()) {
        return tr("%1：%2").arg(what, detail.isEmpty() ? code : detail);
    }
    return QString();
}

QString AuthManager::xstsErrorMessage(qint64 xerr) {
    // 这些码来自 XSTS 的 401 响应正文（{"Identity":"0","XErr":2148916233,...}）。
    // 2148916233 是最常见的那个，而且它跟"买没买 Minecraft"完全无关 —— 很多人会以为
    // 自己游戏白买了，所以那句话要说清楚"去 xbox.com 登一次"这条路。
    // NOTE: 参数是 qint64：这些码超过 int 的范围（2148916233 > 2^31-1）。
    switch (xerr) {
        case 2148916227LL:
            return tr("这个微软账户被 Xbox Live 封禁了，换一个账户登录。");
        case 2148916233LL:
            return tr("这个微软账户还没有注册过 Xbox 账户，所以换不出 Minecraft 令牌。\n"
                      "用同一个账户到 xbox.com 登录一次（会提示你创建档案），再回来重试。");
        case 2148916235LL:
            return tr("这个微软账户所在的国家/地区不支持 Xbox Live。\n"
                      "换一个账户，或者改一下账户的国家/地区设置。");
        case 2148916236LL:
        case 2148916237LL:
            return tr("这个微软账户需要先在 Xbox 官网完成成人验证才能登录。");
        case 2148916238LL:
            return tr("这是未成年账户，得先由家长把它加进 Microsoft 家庭组，之后才能登录。");
        default:
            return tr("XSTS 授权失败（错误码 %1）。").arg(xerr);
    }
}

QString AuthManager::appRegistrationHint(const QString &bodyMessage) {
    // login_with_xbox 的这条 403 很容易被当成"令牌不对、再登一次"，而它其实是**应用本身**
    // 的问题：同一个账户、同一张 XSTS 令牌，换一个被批准的应用 ID 立刻就能过。所以这句
    // 要写清三件事：账号没问题、卡在哪、下一步去哪做什么。
    if (!bodyMessage.contains(QStringLiteral("Invalid app registration"), Qt::CaseInsensitive)) {
        return QString();
    }

    return tr("这个 Azure 应用还没有被 Mojang 批准调用 Minecraft 接口，所以换不出 Minecraft 令牌。\n"
              "账号本身没问题（Xbox Live 认证和 XSTS 授权都过了），不用换账号、也不用重登。\n"
              "拿这个应用的 App ID（客户端 ID）到 https://aka.ms/mce-reviewappid 提交申请，"
              "批准之后同一个账户就能登录。");
}

void AuthManager::handleNewConnection() {
    QTcpSocket *socket = m_callbackServer->nextPendingConnection();
    if (!socket) {
        return;
    }

    connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);

    // 处理真回调那条路上有网络请求（内嵌事件循环），这段时间里浏览器顺手来要的
    // /favicon.ico 会**重入**到这里。它没有授权码 —— 放它进来只会变成一次假的
    // "登录失败"（用户正等着登录，结果先挨一个失败弹窗）。
    if (m_handlingCallback) {
        socket->disconnectFromHost();
        return;
    }

    if (!socket->waitForReadyRead(10000)) {
        socket->disconnectFromHost();
        return;
    }

    const QByteArray requestData = socket->readAll();
    const QString requestText = QString::fromUtf8(requestData);
    const QStringList requestLines = requestText.split(QLatin1String("\r\n"), Qt::SkipEmptyParts);
    if (requestLines.isEmpty()) {
        socket->disconnectFromHost();
        return;
    }

    const QString requestLine = requestLines.first();
    const QStringList parts = requestLine.split(' ');
    if (parts.size() < 2) {
        socket->disconnectFromHost();
        return;
    }

    const QUrl requestedUrl(QStringLiteral("http://localhost") + parts.at(1));
    // 只认根路径。别的路径（favicon 之类）不是回调，不产生任何信号 —— 关掉连接走人，
    // 真回调随后会另开一条连接进来。
    if (requestedUrl.path() != QStringLiteral("/")) {
        socket->disconnectFromHost();
        return;
    }

    const QUrlQuery query(requestedUrl.query());
    const QString code = query.queryItemValue("code");
    const QString error = query.queryItemValue("error");

    // NOTE: 这里**不要**把 code / error 的内容写进日志。授权码是密钥材料（PKCE 挡的是
    //       "被别人截走后换成令牌"，不是"被别人抄走"），而 Qt 的 qDebug 默认打到 stderr，
    //       谁把启动器的输出重定向了谁就拿到一份。要确认回调有没有成功，看下面的分支和
    //       loginSucceeded/loginFailed 信号就够了。

    m_handlingCallback = true;

    QString responseHtml;
    bool success = false;
    QString failureMessage;

    if (!code.isEmpty()) {
        QString microsoftToken;
        // 两段合起来才算登录成功：先拿授权码换微软令牌，再用它换 Minecraft 令牌和档案。
        // 前半段成功、后半段失败时盘上会留着微软那份令牌（换到它就落盘了，见
        // exchangeAuthorizationCode()），**但 Minecraft 令牌和玩家名一个都不写** ——
        // 那份微软令牌单独用不了，它是"下次续期用的凭据"，不是"已登录"。
        // NOTE: 这里的原注释写的是"一份令牌都不落盘"，与代码不符：微软令牌在
        //       exchangeAuthorizationCode() 里就存了。2026-09-20 真机上正是靠盘上这份
        //       微软令牌把失败的链重放了一遍，才查出 login_with_xbox 的 403。
        if (exchangeAuthorizationCode(code, &microsoftToken, &failureMessage) &&
            runMinecraftChain(microsoftToken, &failureMessage)) {
            success = true;
            responseHtml =
                tr("<html><body><h1>登录成功</h1><p>你可以关闭此窗口。</p></body></html>");
        } else {
            responseHtml = tr("<html><body><h1>登录失败</h1><p>%1</p></body></html>")
                               .arg(failureMessage.toHtmlEscaped());
        }
    } else if (!error.isEmpty()) {
        failureMessage = error;
        responseHtml = tr("<html><body><h1>登录失败</h1><p>%1</p></body></html>")
                           .arg(error.toHtmlEscaped());
    } else {
        failureMessage = tr("未收到授权码。");
        responseHtml = tr("<html><body><h1>登录回调无效</h1><p>%1</p></body></html>")
                           .arg(failureMessage.toHtmlEscaped());
    }

    const QByteArray responseBody = responseHtml.toUtf8();
    QByteArray response;
    response.reserve(responseBody.size() + 256);
    response.append("HTTP/1.1 200 OK\r\n");
    response.append("Content-Type: text/html; charset=UTF-8\r\n");
    response.append("Content-Length: ");
    response.append(QByteArray::number(responseBody.size()));
    response.append("\r\nConnection: close\r\n\r\n");
    response.append(responseBody);

    socket->write(response);
    socket->flush();
    socket->waitForBytesWritten(3000);

    // 当套接字仍然连接时断开连接
    if (socket->state() == QAbstractSocket::ConnectedState) {
        socket->disconnectFromHost();
        if (socket->state() != QAbstractSocket::UnconnectedState) {
            socket->waitForDisconnected(3000);
        }
    }

    // 回调服务器用完就关：它只在"等这一次回调"这段时间里该开着。留着就是一个一直张着的
    // 本地端口，本机别的程序随手连一下就能触发一次假的登录失败。
    m_handlingCallback = false;
    m_callbackServer->close();
    m_loginInFlight = false;
    if (m_loginTimeout != nullptr) {
        m_loginTimeout->stop();
    }

    // 信号**放在最后**发：接收方要弹模态窗，而模态窗会卡住它后面那句 —— 先发信号的话，
    // 浏览器那个标签页得等用户把启动器的弹窗点掉才能显示结果。
    if (success) {
        qInfo().noquote() << "登录成功：" << m_playerName;
        emit loginSucceeded(m_playerName);
    } else {
        qWarning().noquote() << "登录失败：" << failureMessage;
        emit loginFailed(failureMessage);
    }
}

bool AuthManager::startCallbackServer() {
    if (m_callbackServer->isListening()) {
        return true;
    }

    return m_callbackServer->listen(QHostAddress::Any, m_callbackPort);
}

QString AuthManager::buildAuthUrl() {
    const QString codeChallenge = createCodeChallenge(m_codeVerifier);
    QUrl url(kOAuthAuthorizeUrl);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), kClientId);
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("response_mode"), QStringLiteral("query"));
    query.addQueryItem(QStringLiteral("redirect_uri"), kRedirectUri);
    query.addQueryItem(QStringLiteral("scope"), kOAuthScope);
    query.addQueryItem(QStringLiteral("code_challenge"), codeChallenge);
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    url.setQuery(query);
    return url.toString();
}

QString AuthManager::createCodeVerifier() const {
    QByteArray randomBytes;
    randomBytes.reserve(32);
    for (int i = 0; i < 32; ++i) {
        randomBytes.append(static_cast<char>(QRandomGenerator::global()->bounded(0, 256)));
    }
    return base64UrlEncode(randomBytes);
}

QString AuthManager::createCodeChallenge(const QString &verifier) const {
    const QByteArray sha256 =
        QCryptographicHash::hash(verifier.toUtf8(), QCryptographicHash::Sha256);
    return base64UrlEncode(sha256);
}

QByteArray AuthManager::base64UrlEncode(const QByteArray &input) const {
    QByteArray encoded =
        input.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return encoded;
}

bool AuthManager::exchangeAuthorizationCode(const QString &code, QString *accessToken,
                                            QString *errorMessage) {
    QUrlQuery body;
    body.addQueryItem(QStringLiteral("client_id"), kClientId);
    body.addQueryItem(QStringLiteral("code"), code);
    body.addQueryItem(QStringLiteral("redirect_uri"), kRedirectUri);
    body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    body.addQueryItem(QStringLiteral("code_verifier"), m_codeVerifier);

    const HttpResult result =
        sendJson(m_networkManager, QUrl(kOAuthTokenUrl), true,
                 body.query(QUrl::FullyEncoded).toUtf8(), "application/x-www-form-urlencoded",
                 QByteArray());

    if (!result.ok()) {
        *errorMessage = oauthFailureText(result, tr("换取微软令牌失败"));
        return false;
    }

    if (!storeMicrosoftTokenData(result.json)) {
        *errorMessage = tr("登录信息保存失败（写不进配置文件）。");
        return false;
    }

    *accessToken = result.json.value("access_token").toString();
    if (accessToken->isEmpty()) {
        *errorMessage = tr("微软的响应里没有令牌。");
        return false;
    }
    return true;
}

bool AuthManager::requestMicrosoftTokenRefresh(const QString &refreshToken, QString *accessToken,
                                               QString *errorMessage) {
    if (refreshToken.isEmpty()) {
        *errorMessage = tr("没有可用的刷新凭据，请重新登录。");
        return false;
    }

    QUrlQuery body;
    body.addQueryItem(QStringLiteral("client_id"), kClientId);
    body.addQueryItem(QStringLiteral("refresh_token"), refreshToken);
    body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    body.addQueryItem(QStringLiteral("scope"), kOAuthScope);

    const HttpResult result =
        sendJson(m_networkManager, QUrl(kOAuthTokenUrl), true,
                 body.query(QUrl::FullyEncoded).toUtf8(), "application/x-www-form-urlencoded",
                 QByteArray());
    if (!result.ok()) {
        *errorMessage = oauthFailureText(result, tr("刷新微软令牌失败"));
        return false;
    }

    QJsonObject tokenData = result.json;
    // 刷新时微软**通常会**轮换 refresh_token，但没带新的那种情况也得撑住：不带就把原来
    // 那张留着。丢了它，下次过期就只能让用户重新登一遍（还得再开一次浏览器）。
    if (tokenData.value("refresh_token").toString().isEmpty()) {
        tokenData.insert(QStringLiteral("refresh_token"), refreshToken);
    }
    if (!storeMicrosoftTokenData(tokenData)) {
        *errorMessage = tr("登录信息保存失败（写不进配置文件）。");
        return false;
    }

    *accessToken = tokenData.value("access_token").toString();
    if (accessToken->isEmpty()) {
        *errorMessage = tr("微软的响应里没有令牌。");
        return false;
    }
    return true;
}

bool AuthManager::runMinecraftChain(const QString &microsoftAccessToken, QString *errorMessage) {
    QString xblToken;
    QString userHash;
    if (!requestXboxLiveToken(microsoftAccessToken, &xblToken, &userHash, errorMessage)) {
        return false;
    }

    QString xstsToken;
    // XSTS 的响应里也会带一遍 uhs，以它为准（XBL 那步的 uhs 是同一个账户，但 XSTS 是
    // 真正授权给 Minecraft 的那一步，用它给的更稳妥）
    if (!requestXstsToken(xblToken, &xstsToken, &userHash, errorMessage)) {
        return false;
    }

    QString mcToken;
    int expiresIn = 0;
    if (!requestMinecraftToken(userHash, xstsToken, &mcToken, &expiresIn, errorMessage)) {
        return false;
    }

    QString name;
    QString uuid;
    if (!fetchPlayerProfile(mcToken, &name, &uuid, errorMessage)) {
        return false;
    }

    if (!storeMinecraftTokenData(mcToken, expiresIn, name, uuid)) {
        *errorMessage = tr("登录信息保存失败（写不进配置文件）。");
        return false;
    }
    return true;
}

bool AuthManager::requestXboxLiveToken(const QString &microsoftAccessToken, QString *xblToken,
                                       QString *userHash, QString *errorMessage) {
    QJsonObject properties;
    properties.insert(QStringLiteral("AuthMethod"), QStringLiteral("RPS"));
    properties.insert(QStringLiteral("SiteName"), QStringLiteral("user.auth.xboxlive.com"));
    // "d=" 前缀是 XBL 的写法：RpsTicket 可以是微软令牌，也可以是别处拿来的一张票据，
    // d= 指明这是前者（微软令牌）。
    properties.insert(QStringLiteral("RpsTicket"),
                      QStringLiteral("d=") + microsoftAccessToken);

    QJsonObject body;
    body.insert(QStringLiteral("Properties"), properties);
    body.insert(QStringLiteral("RelyingParty"), QStringLiteral("http://auth.xboxlive.com"));
    body.insert(QStringLiteral("TokenType"), QStringLiteral("JWT"));

    const HttpResult result =
        sendJson(m_networkManager, QUrl(kXboxLiveAuthUrl), true,
                 QJsonDocument(body).toJson(QJsonDocument::Compact), "application/json",
                 QByteArray());

    if (!result.ok()) {
        *errorMessage = httpFailureText(result, tr("Xbox Live 认证失败"));
        return false;
    }

    *xblToken = result.json.value(QStringLiteral("Token")).toString();
    *userHash = firstUserHash(result.json);
    if (xblToken->isEmpty() || userHash->isEmpty()) {
        *errorMessage = tr("Xbox Live 认证的响应里没有令牌。");
        return false;
    }
    return true;
}

bool AuthManager::requestXstsToken(const QString &xblToken, QString *xstsToken, QString *userHash,
                                   QString *errorMessage) {
    QJsonObject properties;
    properties.insert(QStringLiteral("SandboxId"), QStringLiteral("RETAIL"));
    properties.insert(QStringLiteral("UserTokens"), QJsonArray{xblToken});

    QJsonObject body;
    body.insert(QStringLiteral("Properties"), properties);
    body.insert(QStringLiteral("RelyingParty"), QStringLiteral("rp://api.minecraftservices.com/"));
    body.insert(QStringLiteral("TokenType"), QStringLiteral("JWT"));

    const HttpResult result =
        sendJson(m_networkManager, QUrl(kXstsAuthUrl), true,
                 QJsonDocument(body).toJson(QJsonDocument::Compact), "application/json",
                 QByteArray());

    if (!result.ok()) {
        // 这一步的失败**几乎全是账户本身的问题**（没注册过 Xbox 账户、地区不支持、
        // 未成年……），所以正文里那个 XErr 才是要给用户看的东西
        const qint64 xerr =
            result.json.value(QStringLiteral("XErr")).toVariant().toLongLong();
        *errorMessage = xerr != 0 ? xstsErrorMessage(xerr)
                                  : httpFailureText(result, tr("XSTS 授权失败"));
        return false;
    }

    *xstsToken = result.json.value(QStringLiteral("Token")).toString();
    const QString hash = firstUserHash(result.json);
    if (!hash.isEmpty()) {
        *userHash = hash;
    }
    if (xstsToken->isEmpty() || userHash->isEmpty()) {
        *errorMessage = tr("XSTS 授权的响应里没有令牌。");
        return false;
    }
    return true;
}

bool AuthManager::requestMinecraftToken(const QString &userHash, const QString &xstsToken,
                                        QString *mcToken, int *expiresIn, QString *errorMessage) {
    QJsonObject body;
    // 固定写法：XBL3.0 x=<user hash>;<XSTS 令牌>
    body.insert(QStringLiteral("identityToken"),
                QStringLiteral("XBL3.0 x=%1;%2").arg(userHash, xstsToken));

    const HttpResult result =
        sendJson(m_networkManager, QUrl(kMinecraftLoginUrl), true,
                 QJsonDocument(body).toJson(QJsonDocument::Compact), "application/json",
                 QByteArray());

    if (!result.ok()) {
        // 这一步的 403 有一种很具体的原因（应用没被 Mojang 批准），单独认出来说明白，
        // 别只回一句"HTTP 403" —— 那正是 2026-09-20 真机上卡住登录却查不出原因的那次。
        const QString hint =
            appRegistrationHint(result.json.value(QStringLiteral("errorMessage")).toString());
        *errorMessage =
            hint.isEmpty() ? httpFailureText(result, tr("登录 Minecraft 服务失败")) : hint;
        return false;
    }

    *mcToken = result.json.value(QStringLiteral("access_token")).toString();
    *expiresIn = result.json.value(QStringLiteral("expires_in")).toInt(0);
    if (mcToken->isEmpty()) {
        *errorMessage = tr("Minecraft 服务的响应里没有令牌。");
        return false;
    }
    return true;
}

bool AuthManager::fetchPlayerProfile(const QString &mcToken, QString *name, QString *uuid,
                                     QString *errorMessage) {
    const HttpResult result = sendJson(m_networkManager, QUrl(kMinecraftProfileUrl), false,
                                       QByteArray(), QByteArray(), mcToken.toUtf8());

    // 404 在这一步的意思很具体：令牌是好的，但这个账户没买过 Minecraft（或者买游戏的是
    // 另一个账户）。这条要单独说 —— 否则用户看到的是"登录失败"，会以为是密码或者网络问题。
    if (result.status == 404) {
        *errorMessage = tr("这个微软账户没有购买 Minecraft，拿不到游戏档案。\n"
                           "换成买了游戏的那个账户登录。");
        return false;
    }
    if (!result.ok()) {
        *errorMessage = httpFailureText(result, tr("读取游戏档案失败"));
        return false;
    }

    *name = result.json.value(QStringLiteral("name")).toString();
    *uuid = result.json.value(QStringLiteral("id")).toString();
    if (name->isEmpty() || uuid->isEmpty()) {
        *errorMessage = tr("游戏档案里没有玩家名。");
        return false;
    }
    return true;
}

bool AuthManager::storeMicrosoftTokenData(const QJsonObject &tokenData) {
    // 微软那份要留的是 refresh_token（换新令牌全靠它）和 access_token（换 XBL 令牌用）。
    // 少任何一个，这份数据下次都用不了 —— 不如不写，免得盘上躺着一份坏凭据。
    if (tokenData.value(QStringLiteral("access_token")).toString().isEmpty() ||
        tokenData.value(QStringLiteral("refresh_token")).toString().isEmpty()) {
        qWarning() << "微软令牌响应里缺少 access_token 或 refresh_token，不落盘";
        return false;
    }

    QString failure;
    if (!TokenStore::writeJson(TokenStore::microsoftKey(), tokenData, &failure)) {
        qWarning().noquote() << "微软令牌写盘失败：" << failure;
        return false;
    }
    return true;
}

bool AuthManager::storeMinecraftTokenData(const QString &mcToken, int expiresIn,
                                          const QString &name, const QString &uuid) {
    QJsonObject tokenData;
    tokenData.insert(QStringLiteral("access_token"), mcToken);
    // 盘上存**到期时刻**而不是 expires_in：下次开窗要判"还能不能用"，存秒数还得自己
    // 记住是什么时候存的。取 ISO 字符串而不是毫秒时间戳，是为了在 ini 里能看懂。
    tokenData.insert(QStringLiteral("expires_at"),
                     QDateTime::currentDateTimeUtc().addSecs(expiresIn).toString(Qt::ISODate));
    tokenData.insert(QStringLiteral("name"), name);
    tokenData.insert(QStringLiteral("uuid"), uuid);

    if (!applyMinecraftTokenData(tokenData)) {
        return false;
    }

    QString failure;
    if (!TokenStore::writeJson(TokenStore::minecraftKey(), tokenData, &failure)) {
        qWarning().noquote() << "Minecraft 令牌写盘失败：" << failure;
        return false;
    }
    return true;
}

QJsonObject AuthManager::loadMicrosoftTokenData() const {
    return TokenStore::readJson(TokenStore::microsoftKey());
}

QJsonObject AuthManager::loadMinecraftTokenData() const {
    return TokenStore::readJson(TokenStore::minecraftKey());
}

bool AuthManager::applyMinecraftTokenData(const QJsonObject &tokenData) {
    const QString accessToken = tokenData.value(QStringLiteral("access_token")).toString();
    const QString name = tokenData.value(QStringLiteral("name")).toString();
    const QString uuid = tokenData.value(QStringLiteral("uuid")).toString();
    if (accessToken.isEmpty() || name.isEmpty() || uuid.isEmpty()) {
        return false;
    }

    const QDateTime expiry =
        QDateTime::fromString(tokenData.value(QStringLiteral("expires_at")).toString(),
                              Qt::ISODate);
    // 提前 30 秒算过期：正好在过期那一刻发出去启动，游戏那边会拿到一张已经失效的令牌
    m_tokenExpiry = expiry.isValid() ? expiry.addSecs(-30) : QDateTime();
    m_accessToken = accessToken;
    m_playerName = name;
    m_uuid = uuid;
    return true;
}

