#include "Support.hpp"
#include "auth/AuthManager.hpp"

void runOAuthEndpointTests() {
    section("ZA. 回调地址 → 监听端口（构建注入那两格）");

    // 测试运行时未配置项目 App ID：应在监听/浏览器/网络之前同步拒绝。
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


    // ---------- ZA1. 出厂那份兜底地址读出来还是 38226 ----------
    {
        const QString shipped = QStringLiteral("http://localhost:38226/");
        check(OAuthEndpoints::callbackPort(shipped, 1234) == 38226,
              "ZA1 出厂那份回调地址读出来就是 38226（换实现不许悄悄挪端口）",
              QString::number(OAuthEndpoints::callbackPort(shipped, 1234)));
        check(OAuthEndpoints::isUsableRedirectUri(shipped),
              "ZA1 出厂那份地址本身是一条能用的 http 地址");
    }

    // ---------- ZA2. 地址里那个端口就是我们要听的端口 ----------
    {
        check(OAuthEndpoints::callbackPort(QStringLiteral("http://localhost:38300/"), 1234) == 38300,
              "ZA2 换一条带端口的地址 ⇒ 听那个端口（这正是 `-DWTFML_REDIRECT_URI` 生效的样子）");
        check(OAuthEndpoints::callbackPort(QStringLiteral("http://127.0.0.1:8080/callback"), 1234)
                  == 8080,
              "ZA2 主机名写成 IP、路径带一截都读得出端口");
        check(OAuthEndpoints::callbackPort(QStringLiteral("https://localhost:65535/"), 1234)
                  == 65535,
              "ZA2 边界值 65535 原样读出来（不是「太大就当没有」）");
        check(OAuthEndpoints::callbackPort(QStringLiteral("http://localhost:1/"), 1234) == 1,
              "ZA2 边界值 1 也原样读出来");
    }

    // ---------- ZA3. 没写端口 ⇒ 协议默认端口，**不是**兜底值 ----------
    {
        check(OAuthEndpoints::callbackPort(QStringLiteral("http://localhost/"), 1234) == 80,
              "★★ ZA3 `http://localhost/` 没写端口 ⇒ 80（浏览器就连 80，我们得在 80 上听；"
              "回兜底值 1234 等于听着一个没人会连的端口）",
              QString::number(OAuthEndpoints::callbackPort(QStringLiteral("http://localhost/"), 1234)));
        check(OAuthEndpoints::callbackPort(QStringLiteral("https://localhost/"), 1234) == 443,
              "★★ ZA3 https 同理 ⇒ 443",
              QString::number(OAuthEndpoints::callbackPort(QStringLiteral("https://localhost/"), 1234)));
    }

    // ---------- ZA4. 读不出来 ⇒ 回兜底值（不许回 0） ----------
    {
        const QStringList junk = {
            QString(),
            QStringLiteral("localhost:38226/"),          // 没有协议
            QStringLiteral("ftp://localhost:38226/"),    // 协议不是 http/https
            QStringLiteral("http://"),                   // 有协议没主机
            QStringLiteral("不是一条地址"),
        };
        bool allFallback = true;
        QString firstBad;
        for (const QString &one : junk) {
            const int port = OAuthEndpoints::callbackPort(one, 1234);
            if (port != 1234) {
                allFallback = false;
                firstBad = QStringLiteral("%1 ⇒ %2").arg(one, QString::number(port));
                break;
            }
        }
        check(allFallback, "★★ ZA4 读不出端口的五条输入一律回**调用方给的兜底值**（不是 0："
                           "0 会让 listen() 去要一个随机端口，更没人连得上）",
              firstBad);
        bool noneUsable = true;
        for (const QString &one : junk) {
            if (OAuthEndpoints::isUsableRedirectUri(one)) {
                noneUsable = false;
            }
        }
        check(noneUsable, "ZA4 那五条也一律不是「能用的回调地址」");
    }

    // ---------- ZA5. 「能用吗」那一问只管协议和主机名 ----------
    {
        check(OAuthEndpoints::isUsableRedirectUri(QStringLiteral("http://localhost")),
              "ZA5 末尾不带斜杠也算能用（路径怎么写不影响我们听哪个端口）");
        check(OAuthEndpoints::isUsableRedirectUri(QStringLiteral("http://localhost/?a=b")),
              "ZA5 带查询串也算能用");
        check(!OAuthEndpoints::isUsableRedirectUri(QStringLiteral("localhost")),
              "★ ZA5 光一个主机名不算（没有协议，浏览器也不知道该怎么发这一发）");
    }
}

