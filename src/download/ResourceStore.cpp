/**
 * @file ResourceStore.cpp
 * @brief ResourceStore 的实现。发请求、认钥匙、把正文交给 ResourceCatalog 那一层去读。
 * @author 0x7fffff
 * @date 2026-09-23
 */
#include "ResourceStore.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include "net/HttpClient.hpp"
#include "util/AppPaths.hpp"
#include "util/ImageDecode.hpp"

namespace {

/// 一个 GET 请求该带的头。
///
/// User-Agent：Modrinth 的接口规矩里写着要能认出是谁在问（它按这个封滥用），别的接口
/// 那边都不发这个头，所以只在这里加。
/// 超时走 HttpClient::kDefaultTimeoutMs（15 秒，**唯一出处**）—— 这里用的是 Qt 自己的
/// transferTimeout，不用再搭一个 QTimer：HttpClient 那边手写超时是因为它在跑内嵌事件循环。
QNetworkRequest resourceRequest(const QString &url) {
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("WTFML"));
    request.setTransferTimeout(HttpClient::kDefaultTimeoutMs);
    return request;
}

/// 图标那一发的超时：**比** kDefaultTimeoutMs 短得多，理由和 PlayerSkinStore 那边同一个
///（见它 kTransferTimeoutMs 那段）：图标是**装饰**。
///
/// 2026-09-23 实测：这张图正常是 0.2～3 秒的事情（35 KB 上下），而 Modrinth 的 CDN 会把
/// 每一张 307 到 `cdn-alt.modrinth.com` 上 —— 那个域名从国内连不通的时候，每一次都要把
/// 整个超时耗完才轮到下一张。一页二十来个图标、六条连接轮着来，用户看到的就是"图标半天
/// 不出来、界面还被拖着"。8 秒：正常路况留了三倍余量，坏路况下也别把人按在椅子上。
constexpr int kIconTimeoutMs = 8000;

/// 图标那一发用的请求（只有超时和上面那条不一样）。
QNetworkRequest iconRequest(const QString &url) {
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("WTFML"));
    request.setTransferTimeout(kIconTimeoutMs);
    return request;
}

/// 一串图标字节 → 一张图。解不出来回一张 null 的 QPixmap。
///
/// 一律过 ImageDecode（**不是** `QPixmap::loadFromData`）：接口给的那条图是 webp，这份 Qt
/// 自己解不了，得让 ImageDecode 去问系统的 WIC。盘上那份缓存同理 —— 里面两种字节都会躺着。
QPixmap decodeIcon(const QByteArray &bytes) {
    return QPixmap::fromImage(ImageDecode::decode(bytes));
}

/// project_id 能不能直接当文件名用。
///
/// 这个 id 是从**接口正文**里来的，所以按外部输入对待：只放行 `[A-Za-z0-9._-]`
///（Modrinth 的 id 实测就是这一套字符），并且显式挡掉 `.` 和 `..` —— 它们每一个字符都
/// 在放行集里，却指向目录本身和上一层，正好是"拼出一个能写到别处去的路径"最短的那条路。
bool isSafeFileStem(const QString &id) {
    if (id.isEmpty() || id == QStringLiteral(".") || id == QStringLiteral("..")) {
        return false;
    }
    for (const QChar ch : id) {
        const bool ok = (ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) ||
                        (ch >= QLatin1Char('A') && ch <= QLatin1Char('Z')) ||
                        (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) ||
                        ch == QLatin1Char('.') || ch == QLatin1Char('_') || ch == QLatin1Char('-');
        if (!ok) {
            return false;
        }
    }
    return true;
}

} // namespace

/// 这个 reply 上的 HTTP 状态码（没连上时是 0）。
int statusOf(QNetworkReply *reply) {
    return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
}

/// 状态码在 2xx（真的拿到了东西，而不是"拿到了一个错误页"）。
bool okStatus(int status) { return status >= 200 && status < 300; }

ResourceStore::ResourceStore(QObject *parent)
    : QObject(parent), m_net(new QNetworkAccessManager(this)) {}

ResourceStore::~ResourceStore() { abortAll(); }

void ResourceStore::abortAll() {
    const QList<QNetworkReply *> replies = m_replies.values();
    m_replies.clear();
    for (QNetworkReply *reply : replies) {
        // abort 之后**不能再去读它**（HttpClient 那边的老规矩，漏了就是偶发崩溃）。
        // 掐掉会让它发 finished，那个回调第一件事就是"这一发还是不是手上这个"——
        // 上面已经把 key 摘掉了，所以它会自己安静地退出去，不会往界面上发过期的东西。
        if (reply != nullptr) {
            reply->abort();
        }
    }
    m_iconPending.clear();
}

QString ResourceStore::failureText(QNetworkReply *reply, const QByteArray &body,
                                   const QString &what) {
    const int status = statusOf(reply);
    const QString description =
        QJsonDocument::fromJson(body).object().value(QStringLiteral("description")).toString();
    if (status > 0) {
        if (!description.isEmpty()) {
            return QStringLiteral("%1失败（HTTP %2）：%3").arg(what).arg(status).arg(description);
        }
        return QStringLiteral("%1失败（HTTP %2）").arg(what).arg(status);
    }
    // 压根没连上（断网、DNS、超时）。errorString() 是 Qt 给的那句英文，带上比不带强。
    return QStringLiteral("%1失败：%2").arg(what, reply->errorString());
}

void ResourceStore::search(const QString &requestKey, ResourceCatalog::Kind kind,
                           const QString &query, const QString &gameVersion,
                           const QString &loader) {
    if (QNetworkReply *previous = m_replies.take(requestKey)) {
        previous->abort();
    }

    // 用户又动了一下（换分类 / 又敲了一次回车）= 那一批图标重新开始算。这道闸只管
    // "这一屏别一张张干等"，不该管一整个会话：他刚才多半就是去把代理打开了
    //（见 m_iconGaveUp 那段）。
    m_iconHostFailures = 0;
    m_iconGaveUp = false;

    const QString url = ResourceCatalog::searchUrl(kind, query, 0, kSearchPageSize, gameVersion,
                                                  loader);
    QNetworkReply *reply = m_net->get(resourceRequest(url));
    m_replies.insert(requestKey, reply);

    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, requestKey]() {
        reply->deleteLater();
        if (m_replies.value(requestKey) != reply) {
            return; // 又搜了一次（或者被 abortAll 掐了）：这一发的结果已经没人要了
        }
        m_replies.remove(requestKey);

        const int status = statusOf(reply);
        const QByteArray body = reply->readAll();

        ResourceCatalog::SearchResult result;
        if (status == HttpClient::kStatusTooManyRequests) {
            // 429 说的不是"请求哪儿写错了"，是"你问得太勤了"，得跟用户这么说（唯一出处）。
            result.problem = HttpClient::throttledText(QStringLiteral("搜索"));
        } else if (okStatus(status)) {
            // 2xx 才交给解析器。它读不懂时会自己报「接口返回的数据读不懂」——
            // 那是对的：拿到 200 却读不出 hits，问题就在正文上。
            result = ResourceCatalog::parseSearch(body);
        } else {
            result.problem = failureText(reply, body, QStringLiteral("搜索"));
        }
        emit searchFinished(requestKey, result);
    });
}

void ResourceStore::fetchVersions(const QString &requestKey, const QString &projectId,
                                  const QString &gameVersion, const QString &loader) {
    if (QNetworkReply *previous = m_replies.take(requestKey)) {
        previous->abort();
    }

    const QString url = ResourceCatalog::versionsUrl(projectId, gameVersion, loader);
    QNetworkReply *reply = m_net->get(resourceRequest(url));
    m_replies.insert(requestKey, reply);

    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, requestKey]() {
        reply->deleteLater();
        if (m_replies.value(requestKey) != reply) {
            return;
        }
        m_replies.remove(requestKey);

        const int status = statusOf(reply);
        const QByteArray body = reply->readAll();
        const QString what = QStringLiteral("读取版本列表");

        if (status == HttpClient::kStatusTooManyRequests) {
            emit versionsFinished(requestKey, QList<ResourceCatalog::VersionEntry>(),
                                  HttpClient::throttledText(what));
            return;
        }
        if (!okStatus(status)) {
            emit versionsFinished(requestKey, QList<ResourceCatalog::VersionEntry>(),
                                  failureText(reply, body, what));
            return;
        }
        // 200 也分两种情况：真给了一份版本清单（哪怕是空数组 = 这个版本下确实没有），
        // 还是给了个别的东西（网关的拦截页之类）。
        // **这两种在界面上必须是两句话**：前者是"这个资源在 1.21.1 上没有"，后者是
        // "没问到"。混成一句的话，用户会以为自己选错了版本，而问题在网络上
        //（同一个坑在加载器树那边踩过：见 MainWindow 里「无可用版本」那句 NOTE）。
        if (!QJsonDocument::fromJson(body).isArray()) {
            emit versionsFinished(requestKey, QList<ResourceCatalog::VersionEntry>(),
                                  QStringLiteral("版本列表读不懂（接口回的不是一份版本清单）"));
            return;
        }
        emit versionsFinished(requestKey, ResourceCatalog::parseVersions(body), QString());
    });
}

bool ResourceStore::iconCacheIsFresh(const QDateTime &writtenAt, const QDateTime &now) {
    if (!writtenAt.isValid()) {
        return false; // 不知道什么时候存的 = 不敢信（见声明处那条 NOTE）
    }
    return writtenAt.msecsTo(now) < kIconCacheFreshMs;
}

QString ResourceStore::iconCachePath(const QString &projectId) {
    if (!isSafeFileStem(projectId)) {
        return QString(); // 不是文件名安全的那种 id：宁可不缓存（见声明处）
    }
    // 后缀 `.icon` 不是 `.png` —— 里面可能是 webp（见声明处那条 NOTE）。
    return AppPaths::iconCacheDir() + QLatin1Char('/') + projectId + QStringLiteral(".icon");
}

bool ResourceStore::loadIconFromCache(const QString &projectId, QByteArray *bytesOut,
                                      bool *freshOut) {
    const QString path = iconCachePath(projectId);
    if (path.isEmpty()) {
        return false;
    }
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray bytes = file.readAll();
    const QDateTime writtenAt = QFileInfo(path).lastModified();
    file.close(); // 见 build/verify 里 writeFileAt 那段 NOTE：句柄不放手，后面删不掉
    if (bytesOut != nullptr) {
        *bytesOut = bytes;
    }
    if (freshOut != nullptr) {
        *freshOut = iconCacheIsFresh(writtenAt, QDateTime::currentDateTime());
    }
    return true;
}

bool ResourceStore::saveIconToCache(const QString &projectId, const QByteArray &bytes) {
    const QString path = iconCachePath(projectId);
    if (path.isEmpty()) {
        return false;
    }
    QDir().mkpath(AppPaths::iconCacheDir());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false; // best-effort：写不进去不是错误（这一次的图照样发出去）
    }
    const bool ok = file.write(bytes) == bytes.size();
    file.close();
    return ok;
}

void ResourceStore::dropIconFromCache(const QString &projectId) {
    const QString path = iconCachePath(projectId);
    if (!path.isEmpty()) {
        QFile::remove(path);
    }
}

bool ResourceStore::isHostLevelIconFailure(int httpStatus) {
    // 0 = 压根没连上（断网、DNS、超时）。404/429 不算：那两条说明主机是活的。
    if (httpStatus == 0) {
        return true;
    }
    if (httpStatus == 404 || httpStatus == HttpClient::kStatusTooManyRequests) {
        return false;
    }
    return httpStatus >= 500;
}

void ResourceStore::noteIconFailure(int httpStatus) {
    if (!isHostLevelIconFailure(httpStatus)) {
        return;
    }
    ++m_iconHostFailures;
    if (m_iconHostFailures >= kIconGiveUpAfter) {
        m_iconGaveUp = true;
    }
}

void ResourceStore::answerIcon(const QString &projectId) {
    // 见声明处那条 NOTE：手上那张（旧的但没坏）重发一遍，别把已经画上去的那一格擦掉。
    if (m_icons.contains(projectId)) {
        emit iconReady(projectId, m_icons.value(projectId));
        return;
    }
    emit iconReady(projectId, QPixmap());
}

void ResourceStore::requestIcon(const ResourceCatalog::Hit &hit) {
    const QString &projectId = hit.projectId;
    if (projectId.isEmpty()) {
        return;
    }
    if (m_icons.contains(projectId)) {
        emit iconReady(projectId, m_icons.value(projectId));
        return;
    }
    if (m_iconMissing.contains(projectId)) {
        emit iconReady(projectId, QPixmap());
        return;
    }
    if (m_iconPending.contains(projectId)) {
        return; // 已经在问了，等那一发
    }

    // 盘上那份：先把它顶上（这一发回来之前列表上就有图），新鲜的连请求都不用发。
    // NOTE: 这一段**排在"放弃了"那道闸前面** —— 盘上那些一个请求都不用发，没理由跟着
    //       一起放弃（网络不通的时候它们正是唯一还能画出来的图）。
    QByteArray cached;
    bool fresh = false;
    if (loadIconFromCache(projectId, &cached, &fresh)) {
        const QPixmap icon = decodeIcon(cached);
        if (icon.isNull()) {
            // 盘上那份读不出图（写到一半、或者被别的东西动过）。删掉当没有 —— 留着的话
            // 每进一次这一页都要白读一遍，而且永远画不出图来。
            dropIconFromCache(projectId);
        } else {
            m_icons.insert(projectId, icon);
            emit iconReady(projectId, icon);
            if (fresh) {
                return; // 一个请求都不发（这个缓存存在的全部意义）
            }
            // 不新鲜：图已经画上去了，下面再**后台**问一遍。问到了会再发一次
            // iconReady（界面把那一格换成新的），没问到也不动它（见 answerIcon）。
        }
    }

    if (m_iconGaveUp) {
        // 见成员那段：这一批已经不问了。盘上那张（上面那一段）该发的已经发过。
        if (!m_icons.contains(projectId)) {
            emit iconReady(projectId, QPixmap());
        }
        return;
    }

    // 挑这一发要哪条地址。**两条地址的差别只在大小**：接口那条是 96px 缩略图（样张实测
    // 4800 B，而同一张的 icon.png 是 35218 B），但它是 webp，得这台机器解得了才有意义。
    //
    // NOTE: 小多少是**按张**变的（2026-09-23 icon-live 真跑 20 张：18 份 webp 一共
    //       189 KB，平均十来 KB；同一批走 PNG 要大好几倍）。别把"7 倍"当成常数写进
    //       别的地方 —— 那是 AANobbMI 那一张的数。
    //
    // NOTE: 解不了的时候**不试**那条 webp（不是"试了失败再退"）—— 那会白等一个超时，而
    //       "解不了"是个一次探测就定下来的事实（ImageDecode::webpAvailable 记着结论）。
    //       挑错了的代价只有"少省那 1.1 秒"，挑不动才是问题。
    const bool wantWebp = ImageDecode::webpAvailable() && ResourceCatalog::isWebpUrl(hit.iconUrl);
    const QString url = wantWebp ? hit.iconUrl : ResourceCatalog::iconUrlForProject(projectId);
    if (url.isEmpty()) {
        emit iconReady(projectId, QPixmap());
        return;
    }

    // 兜底只在"这一发要的是 webp"时存在 —— 要 PNG 的时候已经是最坏的那条了，没有下一发。
    startIconRequest(projectId, url, wantWebp ? ResourceCatalog::iconUrlForProject(projectId)
                                              : QString());
}

void ResourceStore::startIconRequest(const QString &projectId, const QString &url,
                                     const QString &fallbackUrl) {
    if (url.isEmpty()) {
        emit iconReady(projectId, QPixmap());
        return;
    }

    m_iconPending.insert(projectId);
    QNetworkReply *reply = m_net->get(iconRequest(url));
    QObject::connect(reply, &QNetworkReply::finished, this,
                     [this, reply, projectId, fallbackUrl]() {
        reply->deleteLater();
        m_iconPending.remove(projectId);

        const int status = statusOf(reply);
        const QByteArray body = reply->readAll();

        if (status == HttpClient::kStatusTooManyRequests) {
            // 图标是**次要**的东西：为它弹一个"你问得太勤了"的窗、把用户正在看的列表打断，
            // 代价比"这个方块空着"大得多。所以这里静静退回去画占位，不问用户。
            //（限流那句话的唯一出处仍然是 HttpClient::throttledText，只是这里不显示它。）
            //
            // NOTE: 退回**也要发**一个图标，不能直接 return。这个信号现在的契约是
            //       "每一发请求都会回一次"（界面按它把那一格摆正，见 onResourceIconReady）——
            //       唯独这一条不发的话，被限流的那批行会永远比别的行少一格。
            //       429 **不算**主机级失败（主机明明答话了），也不换地址重试（换一条也是
            //       同一个主机，这一分钟它照样不给你）。
            answerIcon(projectId);
            return;
        }

        QPixmap icon;
        if (okStatus(status)) {
            icon = decodeIcon(body);
        }
        if (icon.isNull()) {
            // "没解出一张图"。**先看要不要换一条地址** —— 见声明处那段：主机答话了、
            // 而且手上还有另一条地址，就再要一次（这一次不算失败）。pending 位在重发那一
            // 发里重新立起来，所以这中间"同一个 project_id 别问第二遍"的规矩不破。
            if (!fallbackUrl.isEmpty() && !isHostLevelIconFailure(status)) {
                startIconRequest(projectId, fallbackUrl, QString());
                return;
            }
            if (status == 404) {
                // 作者没传过图标（或者删了）。这是"没有"，别再问第二遍 —— 顺手把盘上那张
                // 旧的也丢掉：真删了的话，留着它只会每次进这一页重演一遍"画出来又被撤掉"。
                m_iconMissing.insert(projectId);
                m_icons.remove(projectId);
                dropIconFromCache(projectId);
                emit iconReady(projectId, QPixmap());
                return;
            }
            // 别的失败（断网、超时、5xx）**不进** m_iconMissing：那是一次没成，下次还该再试。
            noteIconFailure(status);
            answerIcon(projectId);
            return;
        }
        m_iconHostFailures = 0; // 这一路是通的（成功一次就把那道闸放回去）
        m_icons.insert(projectId, icon);
        saveIconToCache(projectId, body);
        emit iconReady(projectId, icon);
    });
}
