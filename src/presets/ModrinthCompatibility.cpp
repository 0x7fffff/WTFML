/**
 * @file ModrinthCompatibility.cpp
 * @brief `ModrinthCompatibility.hpp` 的实现。
 */

#include "ModrinthCompatibility.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>

namespace Presets {
namespace {

/// 查询超时那一位打在这个属性上：`finished` 里要分得清"到点收手"和"别的原因 abort"。
const char *const kTimedOutProperty = "wtfmlCompatTimedOut";

/// 429 之后别再去敲门的那段窗口（毫秒）。退避只对"这一发没成"有用；被限流时
/// 立刻再发只会把窗口拖得更长，所以整段时间内同一个平台的查询直接给「暂无法确认」。
constexpr qint64 kThrottleWindowMs = 30 * 1000;

/// 游戏版本那个 JSON 数组参数（Modrinth 的写法：`game_versions=["1.21.1"]`）。
QString versionFilter(const QString &gameVersion) {
    return QStringLiteral("[\"%1\"]").arg(gameVersion.trimmed());
}

/// 加载器 → CurseForge 的 `modLoaderType`（官方那套数：Forge 1 / Fabric 4 / Quilt 5 /
/// NeoForge 6）。认不出来的回 0 = 不加这一格（宁可放宽，也不猜一个错的类型）。
int curseForgeLoaderType(const QString &loader) {
    const QString value = normalizeCompatibilityLoader(loader);
    if (value == QLatin1String("forge")) {
        return 1;
    }
    if (value == QLatin1String("fabric")) {
        return 4;
    }
    if (value == QLatin1String("quilt")) {
        return 5;
    }
    if (value == QLatin1String("neoforge")) {
        return 6;
    }
    return 0;
}

QString projectPath(const QString &projectId) {
    return QString::fromLatin1(QUrl::toPercentEncoding(projectId));
}

QString throttleText(qint64 untilMs) {
    const qint64 leftMs = untilMs - QDateTime::currentMSecsSinceEpoch();
    const qint64 seconds = qMax<qint64>(1, (leftMs + 999) / 1000);
    return QStringLiteral("平台正在限流（大约还要 %1 秒），暂时无法确认。").arg(seconds);
}

}  // namespace

PlatformCompatibilityProvider::PlatformCompatibilityProvider(const QUrl &modrinthRoot,
                                                             QObject *parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this)), m_modrinthRoot(modrinthRoot) {
    // 结尾斜杠去掉一次：下面拼地址时只认"根 + /xxx"这一种形状。
    QString root = m_modrinthRoot.toString();
    while (root.endsWith(QLatin1Char('/'))) {
        root.chop(1);
    }
    m_modrinthRoot = QUrl(root);
}

PlatformCompatibilityProvider::~PlatformCompatibilityProvider() = default;

void PlatformCompatibilityProvider::setCurseForge(const QUrl &base, const QByteArray &apiKey) {
    QString root = base.toString();
    while (root.endsWith(QLatin1Char('/'))) {
        root.chop(1);
    }
    m_curseForgeBase = QUrl(root);
    m_curseForgeKey = apiKey.trimmed();
    // 两样都得有：没有 key 的官方接口是 403，拿它去问只会把"没配置"报成"查询失败"。
    m_curseForgeAvailable = !root.isEmpty() && !m_curseForgeKey.isEmpty();
}

int PlatformCompatibilityProvider::retryDelayMs(int attempt) {
    // 400 ms、800 ms…… 只给瞬态失败用；平台明确答复（200 / 404）从不走这里。
    return 400 * (attempt + 1);
}

void PlatformCompatibilityProvider::query(const ModReference &reference,
                                          const CompatibilityContext &context, Reply reply) {
    if (!reply) {
        return;
    }
    const QString platform = normalizeModPlatform(reference.platform);
    if (platform == QLatin1String("modrinth")) {
        queryModrinth(reference, context, reply);
        return;
    }
    if (platform == QLatin1String("curseforge")) {
        queryCurseForge(reference, context, reply);
        return;
    }
    // 走到这儿说明上游没做本地筛选（检查器会做）。仍然按"不知道"报，不猜。
    reply(CompatibilityState::Invalid, QStringLiteral("平台认不出来。"));
}

void PlatformCompatibilityProvider::queryModrinth(const ModReference &reference,
                                                  const CompatibilityContext &context,
                                                  const Reply &reply, int attempt) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_modrinthThrottledUntilMs > now) {
        reply(CompatibilityState::Unknown, throttleText(m_modrinthThrottledUntilMs));
        return;
    }

    QUrl url(m_modrinthRoot.toString() + QStringLiteral("/project/") +
             projectPath(reference.projectId) + QStringLiteral("/version"));
    QUrlQuery query;
    if (!context.gameVersion.trimmed().isEmpty()) {
        query.addQueryItem(QStringLiteral("game_versions"), versionFilter(context.gameVersion));
    }
    // 只有这四个是 Modrinth 认的模组加载器。`optifine` / 别的名字**不加**这一格：
    // 加一个它不认的加载器名等于什么都查不到，那是假"缺失"。
    const QString loader = normalizeCompatibilityLoader(context.loader);
    if (loader == QLatin1String("fabric") || loader == QLatin1String("forge") ||
        loader == QLatin1String("neoforge") || loader == QLatin1String("quilt")) {
        query.addQueryItem(QStringLiteral("loaders"), versionFilter(loader));
    }
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "WTFML (Minecraft launcher)");
    request.setRawHeader("Accept", "application/json");
    QNetworkReply *networkReply = m_network->get(request);

    QTimer::singleShot(m_transferTimeoutMs, networkReply, [networkReply]() {
        if (networkReply->isFinished()) {
            return;
        }
        networkReply->setProperty(kTimedOutProperty, true);
        networkReply->abort();
    });

    QObject::connect(networkReply, &QNetworkReply::finished, networkReply,
                     [this, networkReply, reply, context, reference, attempt]() {
                         networkReply->deleteLater();
                         // 瞬态失败：还有名额就退避一会儿再来一发；名额用完才报「暂无法确认」。
                         const auto retryOrUnknown = [this, networkReply, reply, context,
                                                      reference, attempt](const QString &why) {
                             if (attempt + 1 < m_maxAttempts) {
                                 QTimer::singleShot(
                                     retryDelayMs(attempt), this,
                                     [this, reference, context, reply, attempt]() {
                                         queryModrinth(reference, context, reply, attempt + 1);
                                     });
                                 return;
                             }
                             reply(CompatibilityState::Unknown, why);
                         };
                         if (networkReply->property(kTimedOutProperty).toBool()) {
                             // 超时不在这一层重试：检查器那一层有总时限，再挂一轮只会拖长。
                             reply(CompatibilityState::Unknown,
                                   QStringLiteral("查询超时，暂时无法确认。"));
                             return;
                         }
                         const int status =
                             networkReply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                                 .toInt();
                         if (networkReply->error() != QNetworkReply::NoError && status == 0) {
                             // 连 HTTP 响应都没拿到：网络层的事，**不是**缺失。
                             retryOrUnknown(
                                 QStringLiteral("查询失败：%1").arg(networkReply->errorString()));
                             return;
                         }
                         if (status == 404) {
                             reply(CompatibilityState::Missing,
                                   QStringLiteral("Modrinth 上没有这个项目。"));
                             return;
                         }
                         if (status == 429) {
                             // 限流：记下一个窗口，窗口里的后续查询直接「暂无法确认」，
                             // 不去把平台的桶敲得更空。429 本身从不算缺失。
                             m_modrinthThrottledUntilMs =
                                 QDateTime::currentMSecsSinceEpoch() + kThrottleWindowMs;
                             reply(CompatibilityState::Unknown,
                                   QStringLiteral("Modrinth 限流了，暂时无法确认，稍后再试。"));
                             return;
                         }
                         if (status == 408 || status >= 500) {
                             retryOrUnknown(
                                 QStringLiteral("接口返回 HTTP %1，暂时无法确认。").arg(status));
                             return;
                         }
                         if (status < 200 || status >= 300) {
                             reply(CompatibilityState::Unknown,
                                   QStringLiteral("接口返回 HTTP %1，暂时无法确认。").arg(status));
                             return;
                         }
                         QJsonParseError error{};
                         const QJsonDocument document =
                             QJsonDocument::fromJson(networkReply->readAll(), &error);
                         if (error.error != QJsonParseError::NoError || !document.isArray()) {
                             reply(CompatibilityState::Unknown,
                                   QStringLiteral("接口返回的内容看不懂，暂时无法确认。"));
                             return;
                         }
                         const QJsonArray versions = document.array();
                         if (versions.isEmpty()) {
                             reply(CompatibilityState::Missing,
                                   QStringLiteral("Modrinth 上没有兼容 %1%2 的发行版。")
                                       .arg(context.gameVersion.isEmpty()
                                                ? QStringLiteral("这个版本")
                                                : context.gameVersion,
                                            context.loader.isEmpty()
                                                ? QString()
                                                : QStringLiteral(" / ") + context.loader));
                             return;
                         }
                         reply(CompatibilityState::Matched, QString());
                     });
}

void PlatformCompatibilityProvider::queryCurseForge(const ModReference &reference,
                                                    const CompatibilityContext &context,
                                                    const Reply &reply, int attempt) {
    if (!m_curseForgeAvailable) {
        reply(CompatibilityState::Unknown,
              QStringLiteral("没有配置 CurseForge 接口，暂时无法确认。"));
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_curseForgeThrottledUntilMs > now) {
        reply(CompatibilityState::Unknown, throttleText(m_curseForgeThrottledUntilMs));
        return;
    }

    QUrl url(m_curseForgeBase.toString() + QStringLiteral("/mods/") +
             projectPath(reference.projectId) + QStringLiteral("/files"));
    QUrlQuery query;
    if (!context.gameVersion.trimmed().isEmpty()) {
        query.addQueryItem(QStringLiteral("gameVersion"), context.gameVersion.trimmed());
    }
    const int loaderType = curseForgeLoaderType(context.loader);
    if (loaderType > 0) {
        query.addQueryItem(QStringLiteral("modLoaderType"), QString::number(loaderType));
    }
    query.addQueryItem(QStringLiteral("pageSize"), QStringLiteral("50"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "WTFML (Minecraft launcher)");
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("x-api-key", m_curseForgeKey);
    QNetworkReply *networkReply = m_network->get(request);

    QTimer::singleShot(m_transferTimeoutMs, networkReply, [networkReply]() {
        if (networkReply->isFinished()) {
            return;
        }
        networkReply->setProperty(kTimedOutProperty, true);
        networkReply->abort();
    });

    QObject::connect(networkReply, &QNetworkReply::finished, networkReply,
                     [this, networkReply, reply, context, reference, attempt]() {
                         networkReply->deleteLater();
                         const auto retryOrUnknown = [this, networkReply, reply, context,
                                                      reference, attempt](const QString &why) {
                             if (attempt + 1 < m_maxAttempts) {
                                 QTimer::singleShot(
                                     retryDelayMs(attempt), this,
                                     [this, reference, context, reply, attempt]() {
                                         queryCurseForge(reference, context, reply, attempt + 1);
                                     });
                                 return;
                             }
                             reply(CompatibilityState::Unknown, why);
                         };
                         if (networkReply->property(kTimedOutProperty).toBool()) {
                             reply(CompatibilityState::Unknown,
                                   QStringLiteral("查询超时，暂时无法确认。"));
                             return;
                         }
                         const int status =
                             networkReply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                                 .toInt();
                         if (networkReply->error() != QNetworkReply::NoError && status == 0) {
                             retryOrUnknown(
                                 QStringLiteral("查询失败：%1").arg(networkReply->errorString()));
                             return;
                         }
                         if (status == 404) {
                             reply(CompatibilityState::Missing,
                                   QStringLiteral("CurseForge 上没有这个项目。"));
                             return;
                         }
                         if (status == 429) {
                             m_curseForgeThrottledUntilMs =
                                 QDateTime::currentMSecsSinceEpoch() + kThrottleWindowMs;
                             reply(CompatibilityState::Unknown,
                                   QStringLiteral("CurseForge 限流了，暂时无法确认，稍后再试。"));
                             return;
                         }
                         if (status == 408 || status >= 500) {
                             retryOrUnknown(
                                 QStringLiteral("接口返回 HTTP %1，暂时无法确认。").arg(status));
                             return;
                         }
                         if (status < 200 || status >= 300) {
                             reply(CompatibilityState::Unknown,
                                   QStringLiteral("接口返回 HTTP %1，暂时无法确认。").arg(status));
                             return;
                         }
                         QJsonParseError error{};
                         const QJsonDocument document =
                             QJsonDocument::fromJson(networkReply->readAll(), &error);
                         if (error.error != QJsonParseError::NoError || !document.isObject()) {
                             reply(CompatibilityState::Unknown,
                                   QStringLiteral("接口返回的内容看不懂，暂时无法确认。"));
                             return;
                         }
                         const QJsonArray files =
                             document.object().value(QStringLiteral("data")).toArray();
                         if (files.isEmpty()) {
                             reply(CompatibilityState::Missing,
                                   QStringLiteral("CurseForge 上没有兼容 %1%2 的发行版。")
                                       .arg(context.gameVersion.isEmpty()
                                                ? QStringLiteral("这个版本")
                                                : context.gameVersion,
                                            context.loader.isEmpty()
                                                ? QString()
                                                : QStringLiteral(" / ") + context.loader));
                             return;
                         }
                         reply(CompatibilityState::Matched, QString());
                     });
}

}  // namespace Presets
