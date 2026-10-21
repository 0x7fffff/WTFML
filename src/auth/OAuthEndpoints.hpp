/**
 * @file OAuthEndpoints.hpp
 * @brief 正版登录那两条"随构建配置走"的常数里，唯一需要**算**的那一格：回调地址里的端口
 * （coding plan §9.4「客户端 ID 和回调地址通过构建配置注入」）。
 *
 * 【为什么端口要从地址里读，而不是再写一个常数】
 *
 * 回调地址和本地监听端口**是同一件事的两半**：浏览器照 `redirect_uri` 去连，而我们在
 * `kCallbackPort` 上听。两个常数各写一份、谁改了一个忘了另一个，症状是"点登录、浏览器
 * 打开了、授权也点了、然后启动器一直转圈到超时" —— 一点错都不报，因为那一头根本不认识
 * 我们。所以从 起 `kCallbackPort` **不再是独立常数**：它由这里从
 * `kRedirectUri` 里读出来（`AuthManager` 构造里那一次）。
 *
 * 【为什么没写端口时回 http=80 / https=443 而不是报错】
 *
 * 地址里不写端口时，浏览器会连**协议默认的那个端口**（80 或 443）—— 那是用户真正告诉
 * 微软的东西。我们照它听才叫"跟着用户说的做"；回一个别的数（比如"回兜底 38226"）等于
 * 假装地址写的是那个数，最后还是一样连不上、还是同一句转圈到超时。
 * 代价要写在明处：这么改之后，"我不写端口"就等于"我要占用 80（或 443）"。那是个特权
 * 端口，绑不上时 `QTcpServer::listen()` 会失败 —— 那时该说的是"这个端口绑不上、换一个
 * 带端口的回调地址"，而不是悄悄退回 38226（那只会让人对着一个永远等不到的回调发呆）。
 *
 * 这个头文件**没有网络、没有 QObject、不看时钟**：纯字符串进、int 出。所以 verify 能
 * 直接喂一串地址进来断言它读出的端口（见 tests/main.cpp 的 ZW 节）。
 */

#pragma once

#include <QString>
#include <QUrl>

namespace OAuthEndpoints {

/// 回调地址里那个端口：浏览器会连哪个端口，我们就在哪个端口上听。
///
/// @param redirectUri 构建时注进去的那个回调地址（`WTFML_REDIRECT_URI`，没注就是源码里
/// 那份兜底值）。
/// @param fallback 这个地址**根本读不出端口**时回什么（调用方传自己那份默认值）。
/// 注意："地址里没写端口"**不算**读不出来 —— 那种情况按协议默认端口回
/// （见文件头那段）；这里说的是地址本身不是一条能用的 http/https 地址。
inline int callbackPort(const QString &redirectUri, int fallback) {
    const QUrl url(redirectUri, QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty()) {
        return fallback;
    }
    const QString scheme = url.scheme().toLower();
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) {
        return fallback;
    }
    const int port = url.port();
    if (port > 0 && port <= 65535) {
        return port;
    }
    return scheme == QStringLiteral("https") ? 443 : 80;
}

/// 这个地址是不是一条**能拿来做本地回调**的地址：http/https、有主机名、其余随它去
/// （路径带不带 `/`、末尾带不带查询串都不影响"我们听哪个端口"）。
///
/// NOTE: 只有"设置页要不要提醒一句"和验证工程会问这个 —— 构建出来一条错地址时的表现是
/// 登录卡到超时，所以宁可在构建/自检的时候问一声。
inline bool isUsableRedirectUri(const QString &redirectUri) {
    const QUrl url(redirectUri, QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty()) {
        return false;
    }
    const QString scheme = url.scheme().toLower();
    return scheme == QStringLiteral("http") || scheme == QStringLiteral("https");
}

}  // namespace OAuthEndpoints
