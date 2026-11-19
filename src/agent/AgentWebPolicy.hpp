/**
 * @file AgentWebPolicy.hpp
 * @brief 联网工具的地址策略与内容处理（纯函数，docs/features/AGENT.md §9.5）。
 *
 * 判据只有这一处：scheme 白名单、userinfo/片段拒绝、目标 host 的**全部** A/AAAA 必须是
 * 公网地址、单跳重定向重新走一遍全流程。`web.fetch` 必须连**已校验的 IP**，并用原 host
 * 做 Host 头与 TLS 主机名校验（pinned-IP）。
 */

#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QString>
#include <QUrl>

namespace AgentWebPolicy {

constexpr int kMaxRedirects = 5;
constexpr qint64 kMaxResponseBytes = 2 * 1024 * 1024;   ///< 2 MB
constexpr int kMaxTextChars = 200000;
constexpr int kTimeoutMs = 20000;

enum class IpClass {
    Public,
    Loopback,
    Private,
    LinkLocal,
    Multicast,
    Reserved,
    Unspecified,
    Invalid,
};

QString ipClassName(IpClass value);

/// 这个地址能不能连（只有 Public 能）。
bool addressAllowed(const QHostAddress &address);
IpClass classify(const QHostAddress &address);

struct UrlCheck {
    bool ok = false;
    QString problem;
    QString host;
    QString scheme;
};

/// 只允许 http/https；拒绝 userinfo、片段、空 host、非默认端口以外的问题交给 DNS/IP 校验。
UrlCheck validateUrl(const QUrl &url);

/// 解析结果里**任何一个**地址不允许就整条拒绝（§9.5）。
bool allAddressesAllowed(const QList<QHostAddress> &addresses, QString *problem);

/// 把 URL 的 host 换成已验证的 IP；端口、路径、查询原样保留。
QUrl pinnedUrl(const QUrl &original, const QHostAddress &address);

/// 响应 Content-Type 是否可读（text/*、application/json、xml 家族）。
bool contentTypeAllowed(const QString &contentType);

/// HTML → 纯文本（去 script/style/标签，解常见实体，压缩空行）。
QString htmlToText(const QString &html);

/// 把正文按上限截断；返回是否截断。
bool truncateText(QString *text, int maxChars = kMaxTextChars);

}  // namespace AgentWebPolicy
