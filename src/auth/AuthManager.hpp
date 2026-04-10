/**
 * @file AuthManager.hpp
 * @brief 微软账户登录和回调处理的声明。
 * @author LoserKugua
 * @date 2026-04-10
 * @note 使用一个本机 HTTP 回调处理验证器重定向。
 *
 * 通过启动一个本机 HTTP 回调服务器并交换验证令牌来处理登入流。
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
     * @brief 构建验证管理器。
     * @param parent Parent QObject.
     */
    explicit AuthManager(QObject *parent = nullptr);

    /**
     * @brief 初始化微软验证器登入流。
     * @return 若登入流成功开启，则返回 true.
     */
    bool loginMicrosoft();

    /**
     * @brief 加载一个已保存的令牌，若其失效则将其刷新。
     * @return 返回一个有效的令牌，当令牌不可用时返回空值。
     */
    QString loadSavedToken();

    /**
     * @brief 尝试刷新令牌。
     * @return 刷新成功返回 true.
     */
    bool refreshAccessToken();

    /**
     * @brief 记录玩家用户名。
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
