#include "AuthManager.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDesktopServices>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonDocument>
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
static constexpr const char kTokenStoreKey[] = "MicrosoftTokenData";
static constexpr const char kEncryptionKey[] = "WTFMLSecretKey2026";
static constexpr const char kOAuthAuthorizeUrl[] =
    "https://login.microsoftonline.com/consumers/oauth2/v2.0/authorize";
static constexpr const char kOAuthTokenUrl[] =
    "https://login.microsoftonline.com/consumers/oauth2/v2.0/token";
static constexpr int kCallbackPort = 38226;
static constexpr const char kRedirectUri[] = "http://localhost:38226/";
static constexpr const char kOAuthScope[] = "XboxLive.signin offline_access";
}  // namespace

AuthManager::AuthManager(QObject *parent)
    : QObject(parent),
      m_playerName(tr("玩家")),
      m_callbackServer(new QTcpServer(this)),
      m_networkManager(new QNetworkAccessManager(this)),
      m_callbackPort(kCallbackPort) {
    connect(m_callbackServer, &QTcpServer::newConnection, this, &AuthManager::handleNewConnection);
    loadStoredTokenData();
}

bool AuthManager::loginMicrosoft() {
    if (!startCallbackServer()) {
        emit loginFailed(tr("无法启动回调服务器。"));
        return false;
    }

    m_codeVerifier = createCodeVerifier();
    const QUrl authUrl(buildAuthUrl());
    if (!QDesktopServices::openUrl(authUrl)) {
        emit loginFailed(tr("无法打开默认浏览器。"));
        return false;
    }

    emit loginStarted();
    return true;
}

QString AuthManager::loadSavedToken() {
    const QJsonObject tokenData = loadStoredTokenData();
    if (tokenData.isEmpty()) {
        return QString();
    }

    if (!applyTokenData(tokenData)) {
        return QString();
    }

    if (m_tokenExpiry.isValid() && QDateTime::currentDateTimeUtc() >= m_tokenExpiry) {
        if (refreshAccessToken()) {
            const QJsonObject refreshed = loadStoredTokenData();
            if (!refreshed.isEmpty()) {
                applyTokenData(refreshed);
                return m_accessToken;
            }
        }
        return QString();
    }

    return m_accessToken;
}

bool AuthManager::refreshAccessToken() {
    const QJsonObject tokenData = loadStoredTokenData();
    const QString refreshToken = tokenData.value("refresh_token").toString();
    if (refreshToken.isEmpty()) {
        return false;
    }
    return requestTokenRefresh(refreshToken);
}

QString AuthManager::playerName() const { return m_playerName; }

void AuthManager::handleNewConnection() {
    QTcpSocket *socket = m_callbackServer->nextPendingConnection();
    if (!socket) {
        return;
    }

    connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);

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
    const QUrlQuery query(requestedUrl.query());
    const QString code = query.queryItemValue("code");
    const QString error = query.queryItemValue("error");

    qDebug() << "OAuth callback received from browser";
    qDebug() << "Code length:" << code.length();
    qDebug() << "Has error:" << !error.isEmpty();

    if (!code.isEmpty()) {
        qDebug() << "Code (first 20 chars):" << code.left(20) << "...";
    }
    if (!error.isEmpty()) {
        qDebug() << "Error from OAuth:" << error;
    }

    QString responseHtml;
    bool success = false;

    if (!code.isEmpty()) {
        success = exchangeAuthorizationCode(code);
        if (success) {
            responseHtml =
                tr("<html><body><h1>登录成功</h1><p>你可以关闭此窗口。</p></body></html>");
            emit loginSucceeded();
        } else {
            responseHtml = tr("<html><body><h1>登录失败</h1><p>令牌交换失败。</p></body></html>");
            emit loginFailed(tr("令牌交换失败。"));
        }
    } else if (!error.isEmpty()) {
        responseHtml = tr("<html><body><h1>登录失败</h1><p>%1</p></body></html>").arg(error);
        emit loginFailed(error);
    } else {
        responseHtml = tr("<html><body><h1>登录回调无效</h1></body></html>");
        emit loginFailed(tr("未收到授权码。"));
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

    // Only disconnect if socket is still connected
    if (socket->state() == QAbstractSocket::ConnectedState) {
        socket->disconnectFromHost();
        if (socket->state() != QAbstractSocket::UnconnectedState) {
            socket->waitForDisconnected(3000);
        }
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

bool AuthManager::exchangeAuthorizationCode(const QString &code) {
    QNetworkRequest request{QUrl(kOAuthTokenUrl)};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    request.setRawHeader("Accept", "application/json");

    QUrlQuery body;
    body.addQueryItem(QStringLiteral("client_id"), kClientId);
    body.addQueryItem(QStringLiteral("code"), code);
    body.addQueryItem(QStringLiteral("redirect_uri"), kRedirectUri);
    body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    body.addQueryItem(QStringLiteral("code_verifier"), m_codeVerifier);

    QNetworkReply *reply = m_networkManager->post(request, body.query(QUrl::FullyEncoded).toUtf8());
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeoutTimer.start(15000);
    loop.exec();

    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        return false;
    }

    const QByteArray response = reply->readAll();
    const bool hasError = reply->error() != QNetworkReply::NoError;
    const QString responseText = QString::fromUtf8(response);
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (hasError) {
        qWarning().noquote() << "Token exchange failed - Error:" << reply->errorString();
        qWarning().noquote() << "HTTP Status Code:" << statusCode;
        qWarning().noquote() << "Response:" << responseText;
        reply->deleteLater();
        return false;
    }

    if (statusCode != 200) {
        qWarning().noquote() << "Token exchange returned HTTP" << statusCode;
        qWarning().noquote() << "Response:" << responseText;
        reply->deleteLater();
        return false;
    }

    reply->deleteLater();
    const QJsonDocument document = QJsonDocument::fromJson(response);
    if (!document.isObject()) {
        qWarning().noquote() << "Token exchange returned invalid JSON:" << responseText;
        return false;
    }

    const QJsonObject jsonObject = document.object();
    if (jsonObject.contains("error")) {
        const QString errorCode = jsonObject.value("error").toString();
        const QString errorDesc = jsonObject.value("error_description").toString();
        qWarning().noquote() << "OAuth error:" << errorCode << "-" << errorDesc;
        return false;
    }

    qDebug() << "Token response received, JSON keys:" << jsonObject.keys();
    qDebug() << "Storing token data...";
    const bool storeResult = storeTokenData(jsonObject);
    if (!storeResult) {
        qWarning() << "Failed to store token data!";
        return false;
    }
    qDebug() << "Token exchange completed successfully";
    return true;
}

bool AuthManager::requestTokenRefresh(const QString &refreshToken) {
    if (refreshToken.isEmpty()) {
        return false;
    }

    QNetworkRequest request{QUrl(kOAuthTokenUrl)};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    request.setRawHeader("Accept", "application/json");

    QUrlQuery body;
    body.addQueryItem(QStringLiteral("client_id"), kClientId);
    body.addQueryItem(QStringLiteral("refresh_token"), refreshToken);
    body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));

    QNetworkReply *reply = m_networkManager->post(request, body.query(QUrl::FullyEncoded).toUtf8());
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeoutTimer.start(15000);
    loop.exec();

    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        return false;
    }

    const QByteArray response = reply->readAll();
    const bool hasError = reply->error() != QNetworkReply::NoError;
    const QString responseText = QString::fromUtf8(response);
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (hasError) {
        qWarning().noquote() << "Refresh token failed - Error:" << reply->errorString();
        qWarning().noquote() << "HTTP Status Code:" << statusCode;
        qWarning().noquote() << "Response:" << responseText;
        reply->deleteLater();
        return false;
    }

    if (statusCode != 200) {
        qWarning().noquote() << "Refresh token returned HTTP" << statusCode;
        qWarning().noquote() << "Response:" << responseText;
        reply->deleteLater();
        return false;
    }

    reply->deleteLater();
    const QJsonDocument document = QJsonDocument::fromJson(response);
    if (!document.isObject()) {
        qWarning().noquote() << "Refresh token returned invalid JSON:" << responseText;
        return false;
    }

    const QJsonObject jsonObject = document.object();
    if (jsonObject.contains("error")) {
        const QString errorCode = jsonObject.value("error").toString();
        const QString errorDesc = jsonObject.value("error_description").toString();
        qWarning().noquote() << "OAuth error during refresh:" << errorCode << "-" << errorDesc;
        return false;
    }

    return storeTokenData(jsonObject);
}

bool AuthManager::storeTokenData(const QJsonObject &tokenData) {
    if (tokenData.isEmpty()) {
        qWarning() << "storeTokenData: tokenData is empty";
        return false;
    }

    if (!applyTokenData(tokenData)) {
        qWarning() << "storeTokenData: applyTokenData failed";
        return false;
    }

    QSettings settings;
    const QByteArray rawData = QJsonDocument(tokenData).toJson(QJsonDocument::Compact);
    const QByteArray encrypted = obfuscate(rawData.toBase64(), false);
    settings.setValue(kTokenStoreKey, encrypted);

    qDebug() << "App Organization:" << QCoreApplication::organizationName();
    qDebug() << "App Name:" << QCoreApplication::applicationName();
    qDebug() << "App Version:" << QCoreApplication::applicationVersion();
    qDebug() << "Settings file:" << settings.fileName();
    qDebug() << "Settings format:"
             << (settings.format() == QSettings::NativeFormat ? "Native" : "IniFormat");
    qDebug() << "Settings value set, encrypted data size:" << encrypted.size();

    // Ensure settings are written to disk immediately
    settings.sync();

    const QSettings::Status status = settings.status();
    if (status != QSettings::NoError) {
        qWarning() << "storeTokenData: QSettings error status:" << status;
        qWarning() << "Settings file path was:" << settings.fileName();
        return false;
    }

    qDebug() << "Token data stored successfully";
    return true;
}

QJsonObject AuthManager::loadStoredTokenData() const {
    QSettings settings;
    const QByteArray encrypted = settings.value(kTokenStoreKey).toByteArray();
    if (encrypted.isEmpty()) {
        return QJsonObject();
    }

    const QByteArray decrypted = obfuscate(encrypted, true);
    const QByteArray rawData = QByteArray::fromBase64(decrypted);
    const QJsonDocument document = QJsonDocument::fromJson(rawData);
    if (!document.isObject()) {
        return QJsonObject();
    }

    return document.object();
}

bool AuthManager::applyTokenData(const QJsonObject &tokenData) {
    const QString accessToken = tokenData.value("access_token").toString();
    const QString refreshToken = tokenData.value("refresh_token").toString();
    const int expiresIn = tokenData.value("expires_in").toInt(0);

    if (accessToken.isEmpty()) {
        qWarning() << "Token data missing access_token";
        return false;
    }

    if (refreshToken.isEmpty()) {
        qWarning() << "Token data missing refresh_token";
        return false;
    }

    if (expiresIn <= 0) {
        qWarning() << "Token data has invalid expires_in:" << expiresIn;
    }

    m_accessToken = accessToken;
    m_refreshToken = refreshToken;
    m_tokenExpiry = QDateTime::currentDateTimeUtc().addSecs(expiresIn - 30);
    qDebug() << "Token data applied successfully. Expires at:" << m_tokenExpiry;
    return true;
}

QByteArray AuthManager::obfuscate(const QByteArray &plain, bool decrypt) const {
    QByteArray key =
        QCryptographicHash::hash(QByteArray(kEncryptionKey), QCryptographicHash::Sha256);
    QByteArray data = plain;
    if (decrypt) {
        // direct XOR is symmetric
    }

    QByteArray result;
    result.reserve(data.size());
    for (int i = 0; i < data.size(); ++i) {
        result.append(data.at(i) ^ key.at(i % key.size()));
    }
    return result;
}
