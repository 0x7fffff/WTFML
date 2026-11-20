/**
 * NOTE: 两条令牌链是两回事，这里存两份：
 * - 微软那份（`MicrosoftTokenData`）：只有 refresh_token 是长期有用的，用来在
 * Minecraft 令牌过期之后重新换一遍。它换不出能启动游戏的令牌。
 * - Minecraft 那份（`MinecraftTokenData`）：启动游戏时 `--accessToken` 要的就是它，
 * 有效期约 24 小时，**换不出来就得从微软那头整条链重走**。
 */
#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>

class QTcpServer;
class QTcpSocket;
class QTimer;
class QNetworkAccessManager;

class AuthManager : public QObject {
    Q_OBJECT

public:
    /// 令牌刷新失败分类：区分「网络/服务器暂不可用（可重试或离线游玩）」与「刷新令牌失效（必须重新登录）」。
    enum class RefreshFailureKind {
        None,             ///< 未失败（或尚未触发刷新）
        Network,          ///< 网络不通、超时或服务端暂不可用 / 限流
        ReloginRequired,  ///< 刷新令牌失效或缺失，必须重新登录
        Other,            ///< 其它配置或账号受限错误（如未配置客户端 ID、XSTS 拦截、无档案等）
    };

    /// 测试用端点覆盖（与 SkinApi::setBaseUrlOverride 同形）：空串表示使用官方地址。
    struct EndpointOverrides {
        QString oauthTokenUrl;
        QString xboxLiveAuthUrl;
        QString xstsAuthUrl;
        QString minecraftLoginUrl;
        QString minecraftProfileUrl;
        QString clientId;
    };

    /**
     * @brief 构建验证管理器。
     * @param parent Parent QObject.
     */
    explicit AuthManager(QObject *parent = nullptr);

    /**
     * @brief 启动微软登录流：起回调服务器，再把浏览器打开到登录页。
     * @return 回调服务器起得来、浏览器也打开成功时才返回 true；已经有一个流程在走时返回
     * false（见 loginInFlight()），**这种 false 不发信号**。
     *
     * NOTE: 返回 true 只代表**流程开了头**：登录真正成功还是失败是异步的，靠下面那三个
     * 信号。配置、监听或浏览器失败时都会先发一次 loginFailed() —— 也就是说调用方
     * 不需要自己处理失败，**更不要**在那边再写一句日志当提示（那就成了两份说法）。
     */
    bool loginMicrosoft();

    /// 空串表示已配置；只检查构建配置，不连接网络或打开浏览器。
    static QString loginConfigurationError();

    /**
     * @brief 登录流程是不是正在走（点了按钮、浏览器那头还没回来）。
     * @return 是则 true.
     *
     * NOTE: 界面拿它决定登录按钮的样子（灰掉 +「正在登录…」），**不自己再存一个副本** ——
     * 两边各存一个就会出现"按钮说在登录、实际早就不等了"这种状态。
     * 它由超时兜底（kLoginTimeoutMinutes）：用户点了登录就去干别的、把浏览器关了，
     * 按钮不能永远灰着 —— 到点会发一次 loginFailed() 说明超时了。
     */
    bool loginInFlight() const;

    /**
     * @brief 拿一个能启动游戏的 Minecraft access_token；剩余不足 6 小时（或已过期）时重新刷新。
     * @param errorMessage 可选。失败时写进一句**能直接给用户看**的失败原因（"没登录过"
     * 和"登录过期了换不出来"是两回事，对应的话也不一样 —— 光返回空串的话，
     * 调用方只能笼统说一句"请先登录"，用户照做也还是登不上）。
     * @return Minecraft 令牌；没登录过、解不出来、或者刷新失败时返回空串。
     *
     * NOTE: 盘上 Minecraft 令牌剩余有效期 >= tokenRefreshWindowSecs（6 小时）时**不碰网络**，
     * 直接用它；不足 6 小时才去用盘上微软那份的 refresh_token 重新换（见 refreshMinecraftToken()）。
     */
    QString loadSavedToken(QString *errorMessage = nullptr);

    /// 返回当前已加载到内存中的 Minecraft access_token（不触发网络刷新；给断网离线游玩兜底用）。
    QString savedAccessToken() const { return m_accessToken; }

    /// 最近一次刷新失败的分类（成功或跳过刷新时为 None）。
    RefreshFailureKind lastRefreshFailureKind() const { return m_lastRefreshFailureKind; }

    /// 当前账号是否因刷新令牌失效而被标记为「需要重新登录」。
    bool needsRelogin() const { return m_needsRelogin; }

    /// 根据 HTTP 状态码、网络错误与 OAuth error 字段对刷新失败进行分类（纯函数）。
    static RefreshFailureKind classifyRefreshFailure(int status, const QString &networkError,
                                                     const QString &oauthError = QString());

    /// 测试专用：把令牌刷新链的地址与客户端 ID 指到本机模拟服务端（默认全部为空 = 官方配置）。
    static void setEndpointOverridesForTest(const EndpointOverrides &overrides);

    /// 盘上那张 Minecraft 令牌**离到期还有多少秒**。
    ///
    /// NOTE: `-1` = 没有令牌、或者认不出到期时刻（凭据缺字段、从没登录过）—— 调用方把它
    /// 当成"该换一张了"处理就行：真换不出来的原因由 loadSavedToken() /
    /// refreshMinecraftToken() 说得更具体（从没登录过 / 凭据不完整 / 被 XSTS 挡了）。
    ///
    /// NOTE: 比的是**去掉那 30 秒余量之后**的时刻（见 applyMinecraftTokenData()）——
    /// 这里给出的"还剩多少"因此已经把那 30 秒算进去了，调用方不用再减。
    int tokenSecondsLeft() const;

    /// 剩余有效期**不到这个数**就该换一张了（6 小时）。
    ///
    /// 为什么不是"等它真的过期"：一张 Minecraft 令牌能用 24 小时，而"能不能启动游戏"问的是
    /// **点启动那一刻**。启动器很可能开着不关（用户挂着它过夜、第二天点启动），那时候按
    /// "还没过期"放过去的就是一张只剩几分钟的令牌 —— 用户要的是"这趟走得顺"，不是
    /// "这一毫秒还没过期"。6 小时是刷新窗口：过期时间小于 6 小时就刷新。
    static constexpr int tokenRefreshWindowSecs = 6 * 60 * 60;

    /**
     * @brief 用盘上的微软 refresh_token 把整条链重走一遍，拿一份新的 Minecraft 令牌。
     * @param errorMessage 可选。失败时写进一句能直接给用户看的原因。
     * @return 成功返回 true（新令牌已经落盘、也在成员里）。
     *
     * NOTE: 刷新用的 refresh_token 是**从盘上重新读的**，没有任何成员缓存它 ——
     * 多一份内存副本就是多一份会和盘上失配的东西。
     */
    bool refreshMinecraftToken(QString *errorMessage = nullptr);

    /**
     * @brief 盘上的凭据被换掉了（多账号切换），重新读一遍。
     *
     * NOTE: 只读盘，**不碰网络**（走 applyMinecraftTokenData()）—— 换账号那一刻能立刻
     * 显示出新账号的名字就够了，令牌过没过期是启动那一刻刷新才知道的事
     * （那时失败会有弹窗说清原因）。切完之后盘上那份的 uuid/名字是**新账号**的，
     * 所以这里读得进来；读不进来（副本坏了）就退回未登录，不保留上一个人的状态。
     */
    void reloadStoredAccount();

    /**
     * @brief 把当前玩家设成一个**离线名字**（切到离线账号时用）。
     * @param name 离线账号的名字。空串退回占位值 "Player"。
     *
     * NOTE: 顺手把 uuid 和令牌一起清掉 —— 离线身份没有在线那一半。不清的话底栏会说
     * "离线登录"、而 --uuid 里还发着上一个人的 uuid，游戏里那套身份就是两个人的
     * 拼盘。清完 loggedIn() 变成 false，这是对的：盘上**确实**没有可用的账户。
     */
    void setOfflinePlayer(const QString &name);

    /**
     * @brief 启动游戏时写进 --username 的那个名字。
     * @return 登录过就是 Minecraft 档案里的真名（登录那一刻和每次开窗时从盘上读回来的
     * 那份都算）；没登录过就是占位值 "Player"。
     * 同一个值也是底栏左下的用户名和头像首字母（MainWindow::updateBottomBar）。
     */
    QString playerName() const;

    /**
     * @brief 现在是不是有一个登录好的账户（盘上有 Minecraft 令牌，或本次会话刚登过）。
     * @return 是则 true.
     *
     * NOTE: 判据是"有没有档案"而不是"令牌还有没有过期" —— 令牌过期后重走一遍链就能续上，
     * 账户还在。界面拿它决定按钮写「微软登录」还是「重新登录」。
     */
    bool loggedIn() const;

    /**
     * @brief 启动游戏时写进 --uuid 的那个账户 UUID（带横线的标准写法）。
     * @return 登录过就是档案里的 UUID；没登录过是空串（调用方自己兜底）。
     *
     * NOTE: 档案里给的是**不带横线**的 32 位十六进制（profile.id），游戏命令行要的是
     * 带横线的那种，转换在 dashedUuid() 里。
     */
    QString playerUuid() const;

    /**
     * @brief 把不带横线的 UUID 补成标准写法（8-4-4-4-12）。
     * @param raw 32 位十六进制，带不带横线都收。
     * @return 标准写法；长度不对时返回空串 —— 宁可不给，也不拼一个"看起来像"的假 UUID
     * （一个错的 UUID 到了正版服上比没有更难查）。
     *
     * NOTE: 做成静态纯函数是为了能在验证工程里钉住（真机上要登录一次才看得到档案里的值）。
     */
    static QString dashedUuid(const QString &raw);

    /**
     * @brief XSTS 拒绝授权时的 XErr 码翻成一句能直接给用户看的中文。
     * @param xerr 响应里的 XErr；0 或认不出来的码走最后那条兜底说法。
     * @return 中文说明（**含**该怎么办）。
     *
     * NOTE: 参数是 qint64 不是 int —— 这些码比 INT_MAX 还大（2148916233），收成 int 就是
     * 负数、认不出来，一律走兜底。
     * 这几个码是这条链上最可能真的挡住人的地方（国内用微软账户的玩家大多折在这里，
     * 因为它的判据是"有没有注册过 Xbox 账户"，而这跟有没有买 Minecraft 无关）。
     * 做成静态纯函数是为了能在验证工程里逐条钉住，不用真去连一次微软。
     */
    static QString xstsErrorMessage(qint64 xerr);

    /**
     * @brief login_with_xbox 回 403 时，判断正文里那句 errorMessage 是不是"这个 Azure
     * 应用没被 Mojang 批准"。
     * @param bodyMessage 响应正文里的 errorMessage 字段（没有就传空串）。
     * @return 一句能照着做的中文说明；**不是**这种情况返回空串，调用方走通用的那套说法。
     *
     * NOTE: 这是 真机上真正卡住登录的那一步 —— 前面整条链（微软 OAuth →
     * Xbox Live → XSTS）全过，只有 login_with_xbox 回 403 + "Invalid app
     * registration, see https://aka.ms/AppRegInfo"。它不是令牌坏了、也不是账号有问题，
     * 而是这个 Azure 应用**不在 Mojang 的名单上**：要拿 App ID 去
     * https://aka.ms/mce-reviewappid 申请批准（要等）。通用那套只报得出"HTTP 403"，
     * 用户对着一个状态码猜不出该做什么。
     * 做成静态纯函数是为了能在验证工程里钉住 —— 真机上要先有一个没被批准的应用。
     */
    static QString appRegistrationHint(const QString &bodyMessage);

    /**
     * @brief OAuth 令牌端点（/oauth20_token.shtml）失败时，把响应里那两个字段翻成一句
     * 能直接给用户看的中文。
     * @param code 响应里的 error 字段（如 invalid_grant）；没有就传空串。
     * @param description 响应里的 error_description 字段。
     * @param what 这一步在做什么（"刷新微软令牌失败"这种），拼在这句话最前头。
     * @return 一句中文说明；**两个字段都是空的**时给空串，调用方走通用那套说法。
     */
    static QString oauthErrorText(const QString &code, const QString &description,
                                  const QString &what);

signals:
    // NOTE: 三个信号在 MainWindow 里都接上了：登录失败不能只在浏览器那个页面里
    // 看得见，启动器界面上也得有反应。
    // loginFailed 的正文是**给用户看的**，接收方要拿它弹窗（产品约定：挡住操作的
    // 错误必须弹窗，不能只写日志）。
    void loginStarted();
    void loginSucceeded(const QString &playerName);
    void loginFailed(const QString &errorMessage);

private slots:
    void handleNewConnection();

private:
    /// 回调套接字上**已经把 HTTP 请求头收全**之后才进来。
    ///
    /// NOTE: 拆出来是因为 `handleNewConnection()` 不再阻塞等字节（见那里的说明）：
    /// 它只负责收，收全了才交给这里走令牌链。解析失败的分支自己断开连接。
    void processCallbackRequest(QTcpSocket *socket, const QByteArray &requestData);

    QUrl buildAuthUrl() const;
    void resetLoginAttempt();

    /// 授权码 → 微软令牌。成功时把微软那份落盘，并把新的 access_token 交给 *accessToken。
    bool exchangeAuthorizationCode(const QString &code, QString *accessToken,
                                   QString *errorMessage);
    /// 微软令牌过期时用 refresh_token 换一份新的。写回盘上时保住旧的 refresh_token
    /// （响应里没带新的那种情况）。**不会**顺手把 Minecraft 那条链也走一遍。
    bool requestMicrosoftTokenRefresh(const QString &refreshToken, QString *accessToken,
                                      QString *errorMessage,
                                      const QString &expectedActiveUuid = QString(),
                                      RefreshFailureKind *failureKind = nullptr);

    /// 微软 access_token → Xbox Live → XSTS → Minecraft 令牌 → 玩家档案。整条链走通才
    /// 返回 true：中途任何一步失败，**Minecraft 那份令牌和玩家名一个都不写**
    /// （半截的登录状态比没登录更难查）。
    ///
    /// NOTE: 这条链**不清**微软那份令牌：refresh_token / access_token 在链之前就已经落盘
    /// （见 exchangeAuthorizationCode()，同文件实现里那段 NOTE 也记着）。它单独用不了
    /// （是"下次续期用的凭据"、不是"已登录"），但盘上确实有它。
    bool runMinecraftChain(const QString &microsoftAccessToken, QString *errorMessage,
                           const QString &expectedActiveUuid = QString(),
                           RefreshFailureKind *failureKind = nullptr);
    bool requestXboxLiveToken(const QString &microsoftAccessToken, QString *xblToken,
                              QString *userHash, QString *errorMessage,
                              RefreshFailureKind *failureKind = nullptr);
    bool requestXstsToken(const QString &xblToken, QString *xstsToken, QString *userHash,
                          QString *errorMessage, RefreshFailureKind *failureKind = nullptr);
    bool requestMinecraftToken(const QString &userHash, const QString &xstsToken,
                               QString *mcToken, int *expiresIn, QString *errorMessage,
                               RefreshFailureKind *failureKind = nullptr);
    bool fetchPlayerProfile(const QString &mcToken, QString *name, QString *uuid,
                            QString *errorMessage, RefreshFailureKind *failureKind = nullptr);

    bool storeMicrosoftTokenData(const QJsonObject &tokenData);
    bool storeMinecraftTokenData(const QString &mcToken, int expiresIn, const QString &name,
                                 const QString &uuid);
    QJsonObject loadMicrosoftTokenData() const;
    QJsonObject loadMinecraftTokenData() const;
    /// 把盘上那份 Minecraft 令牌应用到成员上（含档案名）。**不碰网络** ——
    /// 所以开窗时调它是安全的（构造函数里就调了一次，用来把上次登录的名字显示出来）。
    bool applyMinecraftTokenData(const QJsonObject &tokenData);

    QString m_playerName;   // 登录过是真名，没登录是 "Player"，见 playerName()
    QString m_uuid;         // 档案里的账户 UUID（不带横线）；空 = 没登录。见 loggedIn()
    QString m_accessToken;  // 最近一次拿到/应用到成员上的 **Minecraft** access_token
    // NOTE: refresh_token **不在这里存一份** —— 刷新时直接用 loadMicrosoftTokenData() 从盘上
    // 读（见 refreshMinecraftToken()），多一个成员就是多一份会和盘上失配的副本。
    QDateTime m_tokenExpiry;  // 比 expires_in 提前 30 秒到期，见 applyMinecraftTokenData()
    QString m_codeVerifier;
    /// 当前 OAuth 请求的一次性关联值；回调必须逐字匹配，完成后立即清空。
    QString m_oauthState;
    /// 绑定回环端口后确定的完整 URI，授权与换取令牌共用。
    QUrl m_oauthRedirectUri;
    QTcpServer *m_callbackServer;
    QNetworkAccessManager *m_networkManager;
    // 登录流程在走：从 loginMicrosoft() 起，到回调处理完（或超时）为止。见 loginInFlight()。
    bool m_loginInFlight = false;
    // 只有 exchangeAuthorizationCode() 会把它点起来（不是 requestMicrosoftTokenRefresh() ——
    // 那条路刷新的是**同一个**账号的令牌，覆盖了也是它自己的，清掉等于把好好的凭据扔了）。
    // 用它把两种失败分开：**换了别人**的那种失败收尾时得把微软槽一起清掉，否则下一次启动
    // 会拿这份"没头没尾"的凭据把登录悄悄补完，人不知鬼不觉地换成新账号（见 handleNewConnection()
    // 里失败那一支的 NOTE）。登录开始时复位。
    bool m_microsoftTokenOverwritten = false;
    // 超时兜底计时器（单次，懒创建）。它保证"浏览器那边再也不回来了"不会把界面卡在
    // 「正在登录…」上 —— 到点关掉回调服务器、发一次 loginFailed()。
    QTimer *m_loginTimeout = nullptr;
    // 正在处理一次回调。浏览器会顺手来要 /favicon.ico，而处理真回调那条路上有网络请求
    // （内嵌事件循环），favicon 那一下会**重入**到这里来 —— 它没有授权码，会当成"登录失败"
    // 弹个窗，把好好的登录搅黄。这一位就是用来把重入的那次挡在外面的。
    bool m_handlingCallback = false;
    RefreshFailureKind m_lastRefreshFailureKind = RefreshFailureKind::None;
    bool m_needsRelogin = false;
};
