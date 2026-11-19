/**
 * @file SkinApi.cpp
 * @brief 换皮肤/换披风那几条接口的实现（见 .hpp 里那几段说明）。
 */

#include "SkinApi.hpp"

#include "AuthManager.hpp"  // 只为了 403 那套话术共用一处（appRegistrationHint）

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QUuid>

namespace {

QString g_baseUrlOverride;
// NOTE: 档案那条路径不在这里 —— 它是公开的 SkinApi::profilePath()（见 .hpp 那段说明：
// 披风列表就在档案那份响应里；单独一条披风列表路由不存在）。
constexpr const char kActiveCapePath[] = "/minecraft/profile/capes/active";
constexpr const char kSkinsPath[] = "/minecraft/profile/skins";

/// 披风/皮肤项的 state 值。"ACTIVE" = 现在显示着的这一件。
bool isActiveState(const QString &state) {
    return state.compare(QStringLiteral("ACTIVE"), Qt::CaseInsensitive) == 0;
}

/// 从响应正文里取官方那句原因。只认这两个字段，**不把正文原样丢出去**（和
/// AuthManager 的 httpFailureText 同一条规矩：正文里还有 path / traceId 之类的东西）。
QString bodyDetail(const QByteArray &body) {
    const QJsonObject json = QJsonDocument::fromJson(body).object();
    for (const char *key : {"errorMessage", "error", "Message"}) {
        const QString detail = json.value(QString::fromLatin1(key)).toString();
        if (!detail.isEmpty()) {
            return detail;
        }
    }
    return QString();
}
}  // namespace

QString SkinApi::baseUrl() {
    return g_baseUrlOverride.isEmpty() ? QStringLiteral("https://api.minecraftservices.com")
                                       : g_baseUrlOverride;
}

void SkinApi::setBaseUrlOverride(const QString &base) {
    g_baseUrlOverride = base.trimmed();
}

SkinApi::SkinApi(QObject *parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this)) {}

SkinSummary SkinApi::parseCurrentSkin(const QByteArray &body) {
    const QJsonArray skins = QJsonDocument::fromJson(body).object().value("skins").toArray();

    QJsonObject chosen;
    for (const QJsonValue &value : skins) {
        const QJsonObject item = value.toObject();
        if (item.value("url").toString().isEmpty()) {
            continue;
        }
        // 有 ACTIVE 就用 ACTIVE，没有就退回第一件能用的 —— 旧档案里 state 可能缺这一项。
        if (isActiveState(item.value("state").toString())) {
            chosen = item;
            break;
        }
        if (chosen.isEmpty()) {
            chosen = item;
        }
    }

    SkinSummary summary;
    if (chosen.isEmpty()) {
        return summary;
    }
    summary.url = chosen.value("url").toString();
    summary.variant = normalizeVariant(chosen.value("variant").toString());
    return summary;
}

QList<OwnedCape> SkinApi::parseCapes(const QByteArray &body) {
    // 两种形状都认：
    // `GET /minecraft/profile` → {"id":…,"name":…,"skins":[…],"capes":[…]}（外层是对象）
    // `GET /minecraft/profile/capes` → 裸数组 […]（第三方启动器与 OpenAPI 那边看到的就是它）
    //
    // NOTE: 只认**一种**的话，另一种解出来是**空列表**，而且不报错、不进日志 —— 界面上
    // "一件披风都没有"和"这个账号确实没披风"长得一模一样，属于最难查的那一类。
    // 既然两种形状的项长得一样（id/state/url/alias），判形状这件事放在这里一次做掉。
    const QJsonDocument document = QJsonDocument::fromJson(body);
    const QJsonArray capes = document.isArray()
                                 ? document.array()
                                 : document.object().value("capes").toArray();

    QList<OwnedCape> result;
    result.reserve(capes.size());
    for (const QJsonValue &value : capes) {
        const QJsonObject item = value.toObject();
        OwnedCape cape;
        cape.id = item.value("id").toString();
        if (cape.id.isEmpty()) {
            continue;  // 见 .hpp：没有 id 就设不成当前，摆了也点不动
        }
        cape.alias = item.value("alias").toString();
        if (cape.alias.isEmpty()) {
            cape.alias = cape.id;
        }
        cape.url = item.value("url").toString();
        cape.active = isActiveState(item.value("state").toString());
        result.append(cape);
    }
    return result;
}

QString SkinApi::normalizeVariant(const QString &raw) {
    return raw.compare(QStringLiteral("slim"), Qt::CaseInsensitive) == 0 ? QStringLiteral("slim")
                                                                        : QStringLiteral("classic");
}

bool SkinApi::isAcceptableSkinSize(int width, int height) {
    return width == 64 && (height == 64 || height == 32);
}

QByteArray SkinApi::skinUploadBody(const QByteArray &png, const QString &variant,
                                   const QByteArray &boundary) {
    const QByteArray dash = QByteArrayLiteral("--") + boundary + QByteArrayLiteral("\r\n");

    QByteArray body;
    // 第一段：variant（普通文本字段）
    body += dash;
    body += QByteArrayLiteral("Content-Disposition: form-data; name=\"variant\"\r\n\r\n");
    body += normalizeVariant(variant).toUtf8();
    body += QByteArrayLiteral("\r\n");

    // 第二段：file（二进制，必须写明 Content-Type，否则官方按文本收）
    body += dash;
    body += QByteArrayLiteral(
        "Content-Disposition: form-data; name=\"file\"; filename=\"skin.png\"\r\n");
    body += QByteArrayLiteral("Content-Type: image/png\r\n\r\n");
    body += png;
    body += QByteArrayLiteral("\r\n");

    body += QByteArrayLiteral("--") + boundary + QByteArrayLiteral("--\r\n");
    return body;
}

QByteArray SkinApi::skinUploadContentType(const QByteArray &boundary) {
    return QByteArrayLiteral("multipart/form-data; boundary=") + boundary;
}

QString SkinApi::failureText(const HttpClient::Reply &reply, const QString &what) {
    // 第一条分支：**根本没连上**（断网、DNS 解析不了、超时被 abort）。
    // NOTE: 不看 networkError 的话，这几种情况下 status 是
    // HttpClient 给的初值 -1，报出来的是「读取披风失败（HTTP -1）。」：既没说清
    // 是没连上（用户该去查网络/代理），还端出一个不存在的状态码。登录那条路
    // 一直有这一条（AuthManager 里 httpFailureText 的第一句），皮肤披风这条路也要有。
    if (!reply.networkError.isEmpty()) {
        return tr("%1：%2").arg(what, reply.networkError);
    }

    const int status = reply.status;
    const QString detail = bodyDetail(reply.raw);

    if (status == 401) {
        // 令牌不认了。**这句不说"重新登录"就够了吗**：不够 —— 用户多半刚换过账号或者
        // 令牌过期了，所以要说清是登录态那件事，而不是"权限不够"。
        return tr("%1：登录已失效，请重新登录后重试。").arg(what);
    }
    if (status == 403) {
        // 和登录那条路同一个口径：如果是"这个应用没被 Mojang 批准"，说那句能照着做的。
        const QString hint = AuthManager::appRegistrationHint(detail);
        if (!hint.isEmpty()) {
            return tr("%1：%2").arg(what, hint);
        }
        // NOTE: 授权码换来的令牌权限不够也会走到这儿（令牌是对的、但这个应用没被批准改皮肤）。
        return tr("%1：服务器拒绝应用的请求（HTTP 403）。").arg(what);
    }
    if (status == 404) {
        // 官方对"这个账号没有档案"（比如没买游戏）回 404。
        return tr("%1：未找到可修改的 Minecraft 档案（HTTP 404）。").arg(what);
    }
    if (status == HttpClient::kStatusTooManyRequests) {
        // 429：限流。**不许走下面那条通用分支** —— 通用分支会把正文里那句
        // `TOO_MANY_REQUESTS` 端给用户（真机上看到的就是这种），而那是官方
        // 的内部错误码，用户照它做不了任何事。这一句只有一个出处（登录链说的是同一句）。
        return HttpClient::throttledText(what);
    }
    if (!detail.isEmpty()) {
        return tr("%1（HTTP %2）：%3").arg(what).arg(status).arg(detail);
    }
    return tr("%1（HTTP %2）。").arg(what).arg(status);
}

bool SkinApi::fetchCurrentSkin(const QString &token, SkinSummary *out, QString *errorOut) {
    const HttpClient::Reply reply =
        HttpClient::send(m_network, QUrl(baseUrl() + profilePath()),
                         HttpClient::Verb::Get, {}, {}, token.toUtf8());
    if (!reply.ok()) {
        if (errorOut != nullptr) {
            *errorOut = failureText(reply, tr("读取皮肤失败"));
        }
        return false;
    }
    if (out != nullptr) {
        *out = parseCurrentSkin(reply.raw);
    }
    return true;
}

bool SkinApi::fetchOwnedCapes(const QString &token, QList<OwnedCape> *out, QString *errorOut) {
    // 打的是**档案**那个地址：披风列表在它里面（`capes[]`），没有一条独立的"列披风"路由。
    // 不能打 `/minecraft/profile/capes`：那条路由不存在，404 会被界面
    // 说成"这个账号没有可改的 Minecraft 档案"—— 看着像账号没买游戏，实际是路径不存在。
    // 详见 .hpp 文件头那一段（也记着"别按对称性猜路由"这条）。
    const HttpClient::Reply reply =
        HttpClient::send(m_network, QUrl(baseUrl() + profilePath()),
                         HttpClient::Verb::Get, {}, {}, token.toUtf8());
    if (!reply.ok()) {
        if (errorOut != nullptr) {
            *errorOut = failureText(reply, tr("读取披风失败"));
        }
        return false;
    }
    if (out != nullptr) {
        *out = parseCapes(reply.raw);
    }
    return true;
}

bool SkinApi::setActiveCape(const QString &token, const QString &capeId, QString *errorOut) {
    QJsonObject body;
    body.insert(QStringLiteral("capeId"), capeId);
    const HttpClient::Reply reply = HttpClient::send(
        m_network, QUrl(baseUrl() + QLatin1String(kActiveCapePath)), HttpClient::Verb::Put,
        QJsonDocument(body).toJson(QJsonDocument::Compact),
        QByteArrayLiteral("application/json"), token.toUtf8());
    if (!reply.ok()) {
        if (errorOut != nullptr) {
            *errorOut = failureText(reply, tr("更换披风失败"));
        }
        return false;
    }
    return true;
}

bool SkinApi::hideCape(const QString &token, QString *errorOut) {
    const HttpClient::Reply reply =
        HttpClient::send(m_network, QUrl(baseUrl() + QLatin1String(kActiveCapePath)),
                         HttpClient::Verb::Delete, {}, {}, token.toUtf8());
    if (!reply.ok()) {
        if (errorOut != nullptr) {
            *errorOut = failureText(reply, tr("取消披风失败"));
        }
        return false;
    }
    return true;
}

bool SkinApi::uploadSkin(const QString &token, const QByteArray &png, const QString &variant,
                         QString *errorOut) {
    // boundary 每次都不一样：它只要不和正文里的字节撞上就行，拼一个 uuid 足够。
    const QByteArray boundary =
        QByteArrayLiteral("----WTFML") + QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8();
    const HttpClient::Reply reply = HttpClient::send(
        m_network, QUrl(baseUrl() + QLatin1String(kSkinsPath)), HttpClient::Verb::Post,
        skinUploadBody(png, variant, boundary), skinUploadContentType(boundary), token.toUtf8());
    if (!reply.ok()) {
        if (errorOut != nullptr) {
            *errorOut = failureText(reply, tr("上传皮肤失败"));
        }
        return false;
    }
    return true;
}
