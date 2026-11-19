/**
 * @file AgentWebTransport.hpp
 * @brief 联网工具用的解析与 HTTP 取出接口（可替换，测试用假实现）。
 *
 * 生产实现是 `QtAgentWebTransport`：强制直连（不走系统代理）、手动跟随重定向、连**已校验
 * 的 IP**、用原 host 做 Host 头与 TLS 主机名校验。
 *
 * 两个回调都只在主线程触发；解析用异步 `QHostInfo`，不在 UI 线程上阻塞等 DNS。
 */

#pragma once

#include "AgentWebPolicy.hpp"

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QString>
#include <QUrl>

#include <functional>

struct AgentHttpRequest {
    QUrl pinnedUrl;          ///< host 已经是校验过的 IP
    QString hostHeader;      ///< 原 host（带非默认端口时含 :port）
    QString peerVerifyName;  ///< 原 host，用于 TLS SNI / 证书主机名
    int timeoutMs = AgentWebPolicy::kTimeoutMs;
    QString accept;
};

struct AgentHttpResponse {
    bool ok = false;
    int status = 0;
    QByteArray body;
    QString contentType;
    QString location;    ///< 3xx 的 Location（原始值）
    QString error;
    bool truncated = false;
};

class AgentWebTransport {
public:
    using ResolveCallback =
        std::function<void(bool ok, const QList<QHostAddress> &addresses, const QString &error)>;
    using ResponseCallback = std::function<void(const AgentHttpResponse &response)>;

    virtual ~AgentWebTransport() = default;

    /// 解析目标 host 的**全部** A/AAAA。
    virtual void resolve(const QString &host, ResolveCallback done) = 0;
    /// 发一次 GET（不跟重定向）。
    virtual void get(const AgentHttpRequest &request, ResponseCallback done) = 0;
};

/// Host 头的值：host + 非默认端口。
QString hostHeaderFor(const QUrl &url);
