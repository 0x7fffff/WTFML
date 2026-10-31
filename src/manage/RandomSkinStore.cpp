/**
 * @file RandomSkinStore.cpp
 * @brief 实现"挑一张随机皮肤"：要一页库、随机取一个哈希、拼成官方贴图地址。
 * @note 为什么不是 NameMC、为什么只给离线账号、为什么挑一次就记盘，都写在 .hpp 里，
 * 这里只实现。
 */

#include "RandomSkinStore.hpp"

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
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSet>

#include "../util/AppPaths.hpp"
#include "../net/ControlGet.hpp"
#include "PlayerSkinStore.hpp"

namespace {

/// 那个库的列表端点（免 key）。理由见 .hpp。
constexpr char kListEndpoint[] = "https://api.mineskin.org/v2/skins";

/// 一个请求的超时。和 PlayerSkinStore 取同一个数：这是**可选**的外观请求，
/// 真连不上就早点放弃、退回默认外观，不该拖着什么。
/// NOTE: 这一发走 ControlGet，所以这个数是**排队 + 传输**一起的预算。
constexpr int kTransferTimeoutMs = 10000;

/// 请求里的字段名。
const QString kSkinsField = QStringLiteral("skins");
const QString kTextureField = QStringLiteral("texture");

/// 验证工程的地址覆盖（见 setEndpointOverride）。空 = 官方地址。
QString g_listBaseOverride;

}  // namespace

RandomSkinStore::RandomSkinStore(QObject *parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this)) {}

void RandomSkinStore::setEndpointOverride(const QString &base) {
    g_listBaseOverride = base.trimmed();
}

QUrl RandomSkinStore::listRequestUrl(int size) {
    // 自己先钳住（理由见 .hpp：官方对超限回的是 400，不是一个截短了的列表）。
    const int clamped = qBound(1, size, maxListSize());
    const QString base =
        g_listBaseOverride.isEmpty() ? QString::fromLatin1(kListEndpoint) : g_listBaseOverride;
    // 覆盖地址本身可能带着查询串（夹具常这么写），那就接着往后拼。
    const QChar sep = base.contains(QLatin1Char('?')) ? QLatin1Char('&') : QLatin1Char('?');
    return QUrl(base + sep + QStringLiteral("size=") + QString::number(clamped));
}

QStringList RandomSkinStore::parseTextureHashes(const QByteArray &body, QString *errorOut) {
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (doc.isNull() || !doc.isObject()) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("解不出皮肤列表（不是一份 JSON 对象：%1）")
                            .arg(parseError.errorString());
        }
        return {};
    }
    const QJsonValue skinsValue = doc.object().value(kSkinsField);
    if (!skinsValue.isArray()) {
        // 200 但不是这个形状：多半是接口变了。说清楚比"拿到 0 张"强 —— 后者会被当成
        // "这个库里现在一张皮肤都没有"，而那是另一回事。
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("皮肤列表里没有 %1 这一项（接口形状变了？）")
                            .arg(kSkinsField);
        }
        return {};
    }

    QStringList hashes;
    QSet<QString> seen;
    const QJsonArray skins = skinsValue.toArray();
    for (const QJsonValue &value : skins) {
        const QString hash = value.toObject().value(kTextureField).toString();
        if (hash.isEmpty()) {
            continue;  // 这一项没有贴图，跳过（另一项还是能挑）
        }
        // **用 textureUrlForHash() 当判据**，不在这儿另写一套"64 位十六进制"：那个函数
        // 是"这个哈希能不能拼出一个合法地址"的唯一出处，而这里要的是同一件事。
        // 拼不出地址的哈希留在列表里，只会在挑中它之后才发现是坏的。
        if (PlayerSkinStore::textureUrlForHash(hash).isEmpty()) {
            continue;
        }
        if (seen.contains(hash)) {
            // 去重：刚生成的那一张会在列表里连着出现好几次（同一批提交），留着它就是给
            // 那张图加权，而"随机"的意思是每一张一样大。
            continue;
        }
        seen.insert(hash);
        hashes.append(hash);
    }
    if (hashes.isEmpty() && errorOut != nullptr) {
        *errorOut = QStringLiteral("皮肤列表里一条能用的贴图都没有（共 %1 项）").arg(skins.size());
    }
    return hashes;
}

QString RandomSkinStore::pickIndex(const QStringList &hashes, int roll) {
    if (roll < 0 || roll >= hashes.size()) {
        return QString();  // 越界当"没挑到"，让调用方走失败那条路，别去读一个不存在的下标
    }
    return hashes.at(roll);
}

QString RandomSkinStore::chosenPathFor(const QString &uuid) {
    const QString key = PlayerSkinStore::uuidKey(uuid);
    if (key.isEmpty()) {
        return QString();
    }
    return AppPaths::skinsDir() + QLatin1Char('/') + QStringLiteral("random-") + key +
           QStringLiteral(".txt");
}

QString RandomSkinStore::loadChosenSkinUrl(const QString &uuid) {
    const QString path = chosenPathFor(uuid);
    if (path.isEmpty()) {
        return QString();
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();  // 还没挑过 —— 这是**常态**，不是错误
    }
    const QString line = QString::fromUtf8(file.readAll()).trimmed();
    file.close();
    // 每一行都过 textureRequestUrl()：那是"这个地址算不算数"的唯一判据（认官方那个 host、
    // 把 http 升级成 https）。文件是可以被手改的，改坏了就当没挑过、重新挑一张。
    //
    // NOTE: 返回的是**盘上原样那一行**，不是升级之后的 QUrl —— 缓存路径和 textureReady()
    // 发出去的 url 都按原样那个地址认（见 PlayerSkinStore::textureRequestUrl 的 NOTE）。
    // 我们写下去的就是 https，所以两者本来就一致。
    if (!PlayerSkinStore::textureRequestUrl(line).isValid()) {
        return QString();
    }
    return line;
}

bool RandomSkinStore::saveChosenSkinUrl(const QString &uuid, const QString &skinUrl,
                                        QString *errorOut) {
    const QString path = chosenPathFor(uuid);
    if (path.isEmpty()) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("认不出这个账号的 uuid，随机皮肤没法记下来。");
        }
        return false;
    }
    if (!QDir().mkpath(AppPaths::skinsDir())) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("无法创建目录 %1。")
                            .arg(QDir::toNativeSeparators(AppPaths::skinsDir()));
        }
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(skinUrl.toUtf8() + QByteArrayLiteral("\n")) < 0 || !file.commit()) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("随机皮肤存不下来：%1")
                            .arg(QDir::toNativeSeparators(path));
        }
        return false;
    }
    return true;
}

bool RandomSkinStore::fetchRandomSkin() {
    if (m_inFlight) {
        // 已经有一次在飞了。这条路会被反复走到（每次刷新外观、每次切账号都重新
        // startPlayerSkinLoad()），不挡一下就会攒出一串一样的请求。
        return false;
    }
    m_inFlight = true;

    QNetworkRequest request(listRequestUrl(defaultListSize()));
    // 不在这条请求上写超时：它和别的短请求共用 Control 名额，排队 + 传输一起算进
    // kTransferTimeoutMs（ControlGet 按剩余时间自己设 transferTimeout）。
    RequestBudget::ControlGet::start(
        this, m_network, request, kTransferTimeoutMs,
        [this](QNetworkReply *reply, const QString &jobError) {
            // 不管是发出去的还是没发出去的，这一趟到此为止 —— 先放开闸，后面的刷新才能再问。
            m_inFlight = false;
            if (reply == nullptr) {
                emit failed(QStringLiteral("取随机皮肤列表失败：%1").arg(jobError));
                return;
            }
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray body = reply->readAll();
            const QString networkError = reply->errorString();
            const bool ok = reply->error() == QNetworkReply::NoError;

            if (!ok) {
                emit failed(QStringLiteral("取随机皮肤列表失败（HTTP %1）：%2")
                                .arg(status)
                                .arg(networkError));
                return;
            }

            QString error;
            const QStringList hashes = parseTextureHashes(body, &error);
            if (hashes.isEmpty()) {
                emit failed(error);
                return;
            }
            const QString hash =
                pickIndex(hashes, QRandomGenerator::global()->bounded(hashes.size()));
            const QString url = PlayerSkinStore::textureUrlForHash(hash);
            if (url.isEmpty()) {
                // parseTextureHashes() 已经按同一把尺子筛过一遍，理论上到不了这儿。
                emit failed(QStringLiteral("挑中的贴图哈希拼不出地址：%1").arg(hash));
                return;
            }
            emit skinReady(url);
        });
    return true;  // 请求真的发出去了（被 m_inFlight 挡掉那一趟是上面那个 return false）
}
