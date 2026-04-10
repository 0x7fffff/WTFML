/**
 * @file AuthManager.hpp
 * @brief Declares Microsoft account authentication and OAuth callback handling.
 * @author LoserKugua
 * @date 2026-04-10
 * @note Uses a local HTTP callback server for OAuth redirect handling.
 *
 * Manages login flow by starting a local HTTP callback server and exchanging
 * OAuth authorization codes for access tokens.
 */
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
    /**
     * @brief Constructs the authentication manager.
     * @param parent Parent QObject.
     */
    explicit AuthManager(QObject *parent = nullptr);

    /**
     * @brief Initiates Microsoft OAuth login flow.
     * @return True if the login flow successfully started.
     */
    bool loginMicrosoft();

    /**
     * @brief Loads a saved access token if available and refreshes it if expired.
     * @return Valid access token string or empty if unavailable.
     */
    QString loadSavedToken();

    /**
     * @brief Attempts to refresh the saved access token.
     * @return True if refresh succeeded.
     */
    bool refreshAccessToken();

    /**
     * @brief Returns the current player name.
     */
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
