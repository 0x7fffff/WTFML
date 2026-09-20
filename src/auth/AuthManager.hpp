/**
 * @file AuthManager.hpp
 * @brief 微软账户登录（OAuth 授权码 + PKCE）与令牌存取。
 * @author LoserKugua
 * @date 2026-04-10
 * @note 登录流程：起一个本机 HTTP 回调服务器 → 打开浏览器到微软登录页 → 浏览器重定向
 *       回本机、带回授权码 → 用授权码换令牌 → 令牌落盘（QSettings）。
 *       落盘那一步用的是 XOR 混淆（见 obfuscate()），**不是加密** —— 密钥就写在二进制
 *       里，谁拿到那个 ini 都能还原出令牌明文。
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
     * @brief 启动微软登录流：起回调服务器，再把浏览器打开到登录页。
     * @return 回调服务器起得来、浏览器也打开成功时才返回 true.
     *
     * NOTE: 返回 true 只代表**流程开了头**：登录真正成功还是失败是异步的，靠下面那三个
     *       信号。两条返回 false 的路径都会先发一次 loginFailed()。
     */
    bool loginMicrosoft();

    /**
     * @brief 把盘上存的令牌读出来应用上；已经过期的话先刷新一次。
     * @return 可用的 access_token（就是启动游戏要用的那个）；盘上没有、解不出来、
     *         或者刷新失败时返回空串。
     *
     * NOTE: 每次调用都重新读一遍 QSettings，不依赖构造时缓存下来的成员 —— 所以多调几次
     *       是安全的，代价是一次 ini 读 + 一次 XOR 解密。
     */
    QString loadSavedToken();

    /**
     * @brief 尝试刷新令牌。
     * @return 刷新成功返回 true.
     *
     * NOTE: 刷新用的 refresh_token 是**从盘上重新读的**，没有任何成员缓存它 ——
     *       多一份内存副本就是多一份会和盘上失配的东西。
     */
    bool refreshAccessToken();

    /**
     * @brief 启动游戏时写进 --username 的那个名字。
     * @return 目前**恒为构造时那个占位值 "Player"** —— 登录流程只把令牌落盘，还没有去拉
     *         玩家档案，所以没有真名可用。要改成真名得先补一次 profile 请求。
     *         同一个值也是底栏左边显示的用户名和头像首字母（MainWindow::updateBottomBar）。
     */
    QString playerName() const;

signals:
    // NOTE: 三个里只有 loginSucceeded 有接收方（MainWindow 拿它刷底栏的用户名/头像）。
    //       loginStarted 和 loginFailed **没有任何 connect** —— 所以登录失败时
    //       errorMessage 那句不会出现在启动器界面上，只有浏览器里那个回调页面看得见。
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

    QString m_playerName;   // 恒为 "Player"，见 playerName() 的说明
    QString m_accessToken;  // 最近一次应用成功的 access_token
    // NOTE: refresh_token **不在这里存一份** —— 刷新时直接用 loadStoredTokenData() 从盘上
    //       读（见 refreshAccessToken()），多一个成员就是多一份会和盘上失配的副本。
    QDateTime m_tokenExpiry;  // 比 expires_in 提前 30 秒到期，见 applyTokenData()
    QString m_codeVerifier;
    QTcpServer *m_callbackServer;
    QNetworkAccessManager *m_networkManager;
    int m_callbackPort;
};
