#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QString>

class QTcpServer;
class QNetworkAccessManager;

class AuthManager : public QObject {
    Q_OBJECT

public:
    explicit AuthManager(QObject *parent = nullptr);

    bool loginMicrosoft();
    QString loadSavedToken();
    bool refreshAccessToken();
    QString playerName() const;

signals:
    void loginStarted();
    void loginSucceeded();
    void loginFailed(const QString &errorMessage);

private slots:
    void handleNewConnection();

private:
    bool startCallbackServer();
    QString buildAuthUrl();
    QString createCodeVerifier() const;
    QString createCodeChallenge(const QString &verifier) const;
    QByteArray base64UrlEncode(const QByteArray &input) const;
    bool exchangeAuthorizationCode(const QString &code);
    bool requestTokenRefresh(const QString &refreshToken);
    bool storeTokenData(const QJsonObject &tokenData);
    QJsonObject loadStoredTokenData() const;
    bool applyTokenData(const QJsonObject &tokenData);
    QByteArray obfuscate(const QByteArray &plain, bool decrypt = false) const;

    QString m_playerName;
    QString m_accessToken;
    QString m_refreshToken;
    QDateTime m_tokenExpiry;
    QString m_codeVerifier;
    QTcpServer *m_callbackServer;
    QNetworkAccessManager *m_networkManager;
    int m_callbackPort;
};
