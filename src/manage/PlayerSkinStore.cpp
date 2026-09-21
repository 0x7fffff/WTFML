/**
 * @file PlayerSkinStore.cpp
 * @brief 实现免鉴权的档案请求与内容寻址的贴图缓存。
 * @author 0x7fffff
 * @date 2026-09-21
 * @note 协议形状、缓存策略与"为什么不弹窗"都写在 .hpp 里，这里只实现。
 */

#include "PlayerSkinStore.hpp"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUrl>

#include "../util/AppPaths.hpp"

namespace {

/// 免鉴权的档案端点。uuid 直接跟在后面（去掉横线的 32 位十六进制，见 uuidKey()）。
constexpr char kProfileEndpoint[] =
    "https://sessionserver.mojang.com/session/minecraft/profile/";

/// 这一个请求的超时。比 DownloadManager 的 30 s 短是有意的：那 30 s 是给几十兆的
/// jar/资源包的，而这是一张几 KB 的贴图、而且是**可选**的外观请求 —— 真连不上就早点
/// 放弃、退回默认外观，不该拖着什么。
constexpr int kTransferTimeoutMs = 10000;

/// 档案缓存文件的格式版本。将来格式变了就 +1，而**不认识的版本一律当读不懂** ——
/// 读侧返回 false 的效果是"退回默认外观 + 后台重新取一份"，比按错的格式解出一堆
/// 空字段要安全得多（和 wtfml.folders / wtfml.meta 同一条规矩）。
constexpr int kCacheVersion = 1;

/// 缓存文件里的键名。写成常量是为了和读侧共用同一个出处，不会一边改了另一边没跟。
const QString kVersionField = QStringLiteral("version");
const QString kSkinField = QStringLiteral("skin");
const QString kCapeField = QStringLiteral("cape");
const QString kVariantField = QStringLiteral("variant");

/// sessionserver 信封里的字段。
const QString kPropertiesField = QStringLiteral("properties");
const QString kNameField = QStringLiteral("name");
const QString kValueField = QStringLiteral("value");
/// 解出来那份档案里的字段。
const QString kTexturesField = QStringLiteral("textures");
const QString kSkinObjectField = QStringLiteral("SKIN");
const QString kCapeObjectField = QStringLiteral("CAPE");
const QString kUrlField = QStringLiteral("url");
const QString kMetadataField = QStringLiteral("metadata");
const QString kModelField = QStringLiteral("model");

}  // namespace

PlayerSkinStore::PlayerSkinStore(QObject *parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this)) {}

PlayerSkinProfile PlayerSkinStore::parseProfileJson(const QByteArray &body, QString *errorOut) {
    // 每条失败路径都要写 errorOut，所以包一层（别指望调用方去区分返回的空档案是
    // "没有披风"还是"整份档案没解出来"）。
    const auto fail = [errorOut](const QString &reason) {
        if (errorOut != nullptr) {
            *errorOut = reason;
        }
        return PlayerSkinProfile();
    };

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return fail(QStringLiteral("档案不是合法的 JSON：%1").arg(parseError.errorString()));
    }

    // properties 是个数组，里面按 name 找 "textures" 那一条。**不假定它在第 0 位**：
    // 这个数组本来就是"属性"的通用容器（官方现在只放这一条，但不是协议保证）。
    QString encoded;
    const QJsonArray properties = document.object().value(kPropertiesField).toArray();
    for (const QJsonValue &value : properties) {
        const QJsonObject entry = value.toObject();
        if (entry.value(kNameField).toString() == QLatin1String("textures")) {
            encoded = entry.value(kValueField).toString();
            break;
        }
    }
    if (encoded.isEmpty()) {
        return fail(QStringLiteral("档案里没有 textures 那一条（没设过皮肤的账号？官方换协议了？）"));
    }

    // 里面那层 base64 解出来才是真档案。
    // NOTE: 带上 AbortOnBase64DecodingErrors —— 默认那个选项**会跳过非法字符**，
    //       于是一段被截断的 base64 也能"成功"解出半截 JSON，报出来的错会指向
    //       "JSON 解不动"而不是"base64 就是坏的"，白查一轮。
    const auto decoded = QByteArray::fromBase64Encoding(
        encoded.toLatin1(), QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded) {
        return fail(QStringLiteral("textures 那一段不是合法的 base64"));
    }

    const QJsonDocument inner = QJsonDocument::fromJson(decoded.decoded, &parseError);
    if (parseError.error != QJsonParseError::NoError || !inner.isObject()) {
        return fail(QStringLiteral("textures 解出来不是合法的 JSON：%1").arg(parseError.errorString()));
    }

    const QJsonObject textures = inner.object().value(kTexturesField).toObject();
    const QJsonObject skin = textures.value(kSkinObjectField).toObject();

    PlayerSkinProfile profile;
    profile.skinUrl = skin.value(kUrlField).toString();
    profile.capeUrl = textures.value(kCapeObjectField).toObject().value(kUrlField).toString();
    // metadata.model 只有纤细模型才有这个字段 —— **认不出来的一律当经典**，和
    // SkinModel::isSlimVariant() 同一条规矩（宁可画粗一点，也不要因为一个没见过的
    // 字符串把手臂画瘦）。
    profile.variant =
        skin.value(kMetadataField).toObject().value(kModelField).toString() == QLatin1String("slim")
            ? QStringLiteral("slim")
            : QStringLiteral("classic");

    if (profile.skinUrl.isEmpty()) {
        // 有档案但没皮肤地址：每个账号都有皮肤（没设过就是默认那套），所以这一定是
        // 协议变了。当成"解不出来"，别把一份没有皮肤的档案发出去。
        return fail(QStringLiteral("档案里没有 SKIN.url"));
    }
    return profile;
}

PlayerSkinStore::ProfileFetchOutcome PlayerSkinStore::classifyProfileResponse(int httpStatus,
                                                                            const QByteArray &body) {
    // 204 在这套协议里有两层意思：这个人查不到（已注销/打错的 uuid），以及**限流**。
    // 两者都不该被当成"错误"弹给用户看 —— 调用方那边的表现是"沿用上次的外观"。
    if (httpStatus == 204) {
        return ProfileFetchOutcome::NoSuchPlayer;
    }
    if (httpStatus != 200) {
        return ProfileFetchOutcome::Failed;
    }
    // 200 也可能是软的（代理返回一张网页、body 被截断），所以**必须**能解出皮肤才算 Ok。
    QString ignored;
    return parseProfileJson(body, &ignored).isEmpty() ? ProfileFetchOutcome::Failed
                                                      : ProfileFetchOutcome::Ok;
}

QString PlayerSkinStore::cachePathForUrl(const QString &url) {
    // 文件名就是 URL 的 sha1：贴图 URL 里那一段本来就是内容哈希，所以"同一个 URL"
    // 和"同一张图"是等价的 —— 这个缓存因此**永不失效**，不必存时间戳、也不必校验。
    const QByteArray digest = QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1);
    return AppPaths::skinsDir() + QLatin1Char('/') + QString::fromLatin1(digest.toHex()) +
           QStringLiteral(".png");
}

QString PlayerSkinStore::uuidKey(const QString &uuid) {
    QString key;
    key.reserve(32);
    for (const QChar ch : uuid) {
        if (ch == QLatin1Char('-')) {
            continue;
        }
        const bool isHex = (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) ||
                           (ch >= QLatin1Char('a') && ch <= QLatin1Char('f')) ||
                           (ch >= QLatin1Char('A') && ch <= QLatin1Char('F'));
        if (!isHex) {
            return QString();
        }
        key.append(ch.toLower());
    }
    return key.size() == 32 ? key : QString();
}

QString PlayerSkinStore::cachedProfilePath(const QString &uuid) {
    const QString key = uuidKey(uuid);
    if (key.isEmpty()) {
        // 认不出来就返回空串，**不**拼一个野路径出来：拼出来的那个名字会被当成
        // "某某人的档案"，而它其实谁都不是。
        return QString();
    }
    return AppPaths::skinsDir() + QLatin1Char('/') + QStringLiteral("profile-") + key +
           QStringLiteral(".json");
}

bool PlayerSkinStore::loadCachedProfile(const QString &uuid, PlayerSkinProfile *out) {
    const QString path = cachedProfilePath(uuid);
    if (path.isEmpty()) {
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        // 常态：第一次用这个账号。不是异常，也别打日志。
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return false;
    }
    const QJsonObject root = document.object();
    if (root.value(kVersionField).toInt() != kCacheVersion) {
        return false;
    }

    PlayerSkinProfile profile;
    profile.skinUrl = root.value(kSkinField).toString();
    profile.capeUrl = root.value(kCapeField).toString();
    profile.variant = root.value(kVariantField).toString();
    if (profile.skinUrl.isEmpty()) {
        return false;
    }
    // 手改文件会干的事：把 variant 写成一个没见过的词。当经典处理（同 parseProfileJson）。
    if (profile.variant != QLatin1String("slim")) {
        profile.variant = QStringLiteral("classic");
    }

    if (out != nullptr) {
        // 只在成功这一条路上写 out：失败时不动调用方手上那份（别用一个空值把它盖掉）。
        *out = profile;
    }
    return true;
}

bool PlayerSkinStore::saveProfile(const QString &uuid, const PlayerSkinProfile &profile,
                                  QString *errorOut) {
    const auto fail = [errorOut](const QString &reason) {
        if (errorOut != nullptr) {
            *errorOut = reason;
        }
        return false;
    };

    const QString path = cachedProfilePath(uuid);
    if (path.isEmpty()) {
        return fail(QStringLiteral("uuid 认不出来，不存档案"));
    }
    if (profile.isEmpty()) {
        return fail(QStringLiteral("档案里没有皮肤，不存"));
    }

    if (!QDir().mkpath(AppPaths::skinsDir())) {
        return fail(QStringLiteral("无法创建目录 %1。")
                        .arg(QDir::toNativeSeparators(AppPaths::skinsDir())));
    }

    QJsonObject root;
    root.insert(kVersionField, kCacheVersion);
    root.insert(kSkinField, profile.skinUrl);
    root.insert(kCapeField, profile.capeUrl);
    root.insert(kVariantField, profile.variant);

    // QSaveFile：先写同目录的临时文件、commit() 时才改名覆盖。这份档案坏掉的后果是
    // "以后每次开窗都先画一遍默认外观再跳成真的"（能自愈，但很难看），而且它是每次
    // 刷新都要重写一遍的常写文件 —— 中断留下的半截 JSON 正是最容易撞上的那种坏。
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return fail(QStringLiteral("无法写入 %1。").arg(QDir::toNativeSeparators(path)));
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        return fail(QStringLiteral("写入 %1 失败。").arg(QDir::toNativeSeparators(path)));
    }
    return true;
}

bool PlayerSkinStore::looksLikeSkinPng(const QByteArray &bytes) {
    // PNG 的定长开头：8 字节签名，接着 4 字节块长度 + "IHDR"，再往后 4+4 字节就是宽和高
    // （都是大端）。所以只读前 24 个字节就够判断"是不是一张 64 宽的皮肤图"。
    static const char kPngSignature[] = "\x89PNG\r\n\x1a\n";
    if (bytes.size() < 24 || bytes.left(8) != QByteArray(kPngSignature, 8)) {
        return false;
    }
    if (bytes.mid(12, 4) != QByteArrayLiteral("IHDR")) {
        return false;
    }
    const auto bigEndianInt = [&bytes](int offset) {
        return (static_cast<quint32>(static_cast<unsigned char>(bytes[offset])) << 24) |
               (static_cast<quint32>(static_cast<unsigned char>(bytes[offset + 1])) << 16) |
               (static_cast<quint32>(static_cast<unsigned char>(bytes[offset + 2])) << 8) |
               static_cast<quint32>(static_cast<unsigned char>(bytes[offset + 3]));
    };
    // 宽必须 64（皮肤贴图就是这么定的）；高 64 = 新格式、32 = 老格式
    // （见 SkinModel::completeLegacySkin()）。
    return bigEndianInt(16) == 64 && (bigEndianInt(20) == 64 || bigEndianInt(20) == 32);
}

void PlayerSkinStore::refreshForUuid(const QString &uuid) {
    const QString key = uuidKey(uuid);
    if (key.isEmpty()) {
        // 没有 uuid 就是"没登录"。那不是错误，连日志都不该写（没登录的人是多数）。
        return;
    }

    const QUrl url(QLatin1String(kProfileEndpoint) + key);
    QNetworkRequest request(url);
    request.setTransferTimeout(kTransferTimeoutMs);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        // 先取完要用的东西再放它走（reply->readAll() 之后才 deleteLater）。
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const QString networkError = reply->errorString();
        reply->deleteLater();

        const ProfileFetchOutcome outcome = classifyProfileResponse(status, body);
        if (outcome == ProfileFetchOutcome::NoSuchPlayer) {
            // 204：查不到这个人，或者被限流了。**不发信号**、也不清掉手上那份缓存 ——
            // 多半是限流，此时"继续用上次那套外观"比"退回默认外观"更接近用户想看到的。
            qWarning().noquote() << "官方查不到这个玩家的档案（204），沿用上次的外观";
            return;
        }
        if (outcome == ProfileFetchOutcome::Failed) {
            emit failed(QStringLiteral("取档案失败（HTTP %1）：%2").arg(status).arg(networkError));
            return;
        }

        QString error;
        const PlayerSkinProfile profile = parseProfileJson(body, &error);
        if (profile.isEmpty()) {
            // 分类说 Ok 就没道理走到这里（Ok 的定义就是"解得出来"），但真到了这一步也
            // 不能发一份空档案出去 —— 那会让调用方把好不容易画出来的外观清掉。
            emit failed(error);
            return;
        }
        emit profileReady(profile);
    });
}

void PlayerSkinStore::fetchTexture(const QString &url) {
    if (url.isEmpty()) {
        // "这个人没有披风"是常态（多数账号都没有），不是错误，什么都不做。
        return;
    }

    const QString cached = cachePathForUrl(url);
    // 命中缓存**同步**发信号，一个请求都不发（贴图 URL 就是内容哈希，本地有就等于
    // 已经拿到了）。调用方因此可以指望"这条路是即时的"：开窗那一瞬间把上次的外观
    // 零延迟地画出来，靠的就是它。
    if (QFile::exists(cached)) {
        emit textureReady(url, cached);
        return;
    }

    // 地址来自**外面**（我们解出来的档案）。正常就是 https://textures.minecraft.net/...，
    // 但被改过的档案可以让这里变成任意协议 —— 所以只认 https，别的当失败。
    // （贴图不走 BMCLAPI 镜像：那个镜像只镜像 launchermeta/libraries/resources 那三个
    //   host，贴图不在其中，拼出来的地址是假的。）
    const QUrl target(url);
    if (!target.isValid() || target.scheme() != QLatin1String("https")) {
        emit failed(QStringLiteral("贴图地址不可用：%1").arg(url));
        return;
    }

    QNetworkRequest request(target);
    request.setTransferTimeout(kTransferTimeoutMs);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, cached]() {
        const QByteArray bytes = reply->readAll();
        const QString networkError = reply->errorString();
        const bool ok = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();

        if (!ok) {
            emit failed(QStringLiteral("下贴图失败：%1").arg(networkError));
            return;
        }
        if (!looksLikeSkinPng(bytes)) {
            // **不落盘**。往里写一张垃圾图，下次开窗会把它当"命中缓存"用，而缓存是
            // 永不失效的 —— 那张图会一直错下去，而且再也不会有人去重下它。
            emit failed(QStringLiteral("贴图不是一张 64 宽的皮肤 PNG（%1 字节）").arg(bytes.size()));
            return;
        }

        if (!QDir().mkpath(AppPaths::skinsDir())) {
            emit failed(QStringLiteral("无法创建目录 %1。")
                            .arg(QDir::toNativeSeparators(AppPaths::skinsDir())));
            return;
        }
        QSaveFile file(cached);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
            // 落盘失败（磁盘满、权限）时**也当失败**：这张图只在内存里，而调用方要的是
            // 一个能读的路径。说清楚比"发一个读不到的路径"强。
            emit failed(QStringLiteral("贴图存不下来：%1").arg(QDir::toNativeSeparators(cached)));
            return;
        }
        emit textureReady(url, cached);
    });
}
