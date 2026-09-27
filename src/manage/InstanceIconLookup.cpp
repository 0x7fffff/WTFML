/**
 * @file InstanceIconLookup.cpp
 * @brief 实现见 .hpp。
 * @author 0x7fffff
 * @date 2026-09-27
 */

#include "InstanceIconLookup.hpp"

#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QPair>

#include "../net/HttpClient.hpp"
#include "ModpackExport.hpp"
#include "ModpackImport.hpp"

namespace InstanceIconLookup {

namespace {

/// 一发最多带多少个哈希 / 指纹。**和 `VersionManager::kLookupChunk` 是同一个数**（100）——
/// 两处都是"批量反查"的同一台接口，那边那个数的来龙去脉写在 VersionManager.cpp 里。
constexpr int kLookupChunk = 100;

/// Modrinth 的接口规矩里写着要能认出是谁在问（它按这个封滥用）。和别处那几个串逐字相同，
/// 但**不能共用一份**（一个在 manage/、一个在 download/、一个在别处，为一行字建一个头
/// 文件不划算）—— 改的时候记得一起看。
const QByteArray kUserAgent = QByteArrayLiteral("WTFML");

/// 从 `POST /v1/mods` 的回复里取每个 modId 的那两条图标地址。
///
/// NOTE: 和 `ResourceCatalog.cpp` 解析搜索回复时那四行（`logo.thumbnailUrl` / `logo.url`）
///       是同一个规矩 —— 那边的回复里带的是**整个项目对象**，这边 `data[]` 里只有
///       `id` + `logo`，所以没能共用同一个函数。两边一起改。
QHash<int, QPair<QString, QString>> parseCurseForgeModLogos(const QByteArray &json) {
    QHash<int, QPair<QString, QString>> out;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) {
        return out;
    }
    const QJsonValue data = doc.object().value(QStringLiteral("data"));
    if (!data.isArray()) {
        return out;
    }
    for (const QJsonValue &value : data.toArray()) {
        const QJsonObject entry = value.toObject();
        const int id = entry.value(QStringLiteral("id")).toInt(0);
        if (id <= 0) {
            continue;
        }
        const QJsonObject logo = entry.value(QStringLiteral("logo")).toObject();
        // 两条都过域名闸（那是外部输入）：不是 media.forgecdn.net 上的一律当没有 ——
        // 和 ResourceCatalog 那边一模一样，理由见 isUsableIconUrl 的注释。
        const QString thumbnail = logo.value(QStringLiteral("thumbnailUrl")).toString();
        const QString full = logo.value(QStringLiteral("url")).toString();
        out.insert(id, qMakePair(ResourceCatalog::isUsableIconUrl(thumbnail, ResourceCatalog::Source::CurseForge)
                                     ? thumbnail
                                     : QString(),
                                 ResourceCatalog::isUsableIconUrl(full, ResourceCatalog::Source::CurseForge)
                                     ? full
                                     : QString()));
    }
    return out;
}

}  // namespace

bool isLookupCandidate(const QString &path) {
    QString name = QFileInfo(path).fileName();
    // 禁用着的模组叫 `X.jar.disabled` —— 尾巴上是那四个字，内容还是那个 jar。
    // 这个后缀的**唯一出处**是 InstanceFilesPage::disabledSuffix()，但那个在 ui/ 里
    // （这一层不许 include ui/，反过来的方向才是对的），所以这儿只认后缀的样子。
    if (name.endsWith(QStringLiteral(".disabled"))) {
        name.chop(QStringLiteral(".disabled").size());
    }
    return name.endsWith(QStringLiteral(".jar"), Qt::CaseInsensitive) ||
           name.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive);
}

Result lookup(const QStringList &paths, const Options &options) {
    Result result;

    // ---------- ① 算哈希 ----------
    //
    // 一个 sha1 可能对应好几个路径（同一个 jar 摆了两份、或者 `X.jar` 和 `X.jar.disabled`
    // 同时存在），所以两张表都是"哈希 → 路径表"。
    QHash<QString, QStringList> pathsBySha1;
    QHash<quint32, QStringList> pathsByFingerprint;
    for (const QString &path : paths) {
        if (!isLookupCandidate(path)) {
            continue;
        }
        ModpackExport::FileHashes hashes;
        QString error;
        // needFingerprint = true：两个来源都要问，而算哈希要**读两趟文件**
        // （见 hashFile 的 NOTE）。一趟把 sha1 和指纹都算出来，比两条路各读一遍便宜。
        if (!ModpackExport::hashFile(path, true, &hashes, &error)) {
            ++result.unreadable;
            continue;
        }
        ++result.hashed;
        pathsBySha1[hashes.sha1].append(path);
        if (hashes.fingerprintOk) {
            pathsByFingerprint[hashes.fingerprint].append(path);
        }
    }
    if (pathsBySha1.isEmpty()) {
        return result;
    }

    // ★ 这一层自己 new 一个 QNAM：**它归调用它的那个线程**（见 .hpp 里那段"为什么不在
    //   工作线程里用 VersionManager 那两个 lookup"）。三发都走它。
    QNetworkAccessManager network;

    // ---------- ② Modrinth：sha1 → 版本 → 项目 ----------
    if (!pathsBySha1.isEmpty()) {
        // 根地址：验证工程的口子优先，否则官方那一份（唯一出处仍是 ResourceCatalog 里
        // 那个 apiRoot()）—— 和 VersionManager 那边逐字同一条规矩。
        const QString root = options.modrinthApiBaseOverride.isEmpty()
                                 ? ResourceCatalog::modrinthApiRoot()
                                 : options.modrinthApiBaseOverride;
        const QList<QPair<QByteArray, QByteArray>> headers{{QByteArrayLiteral("User-Agent"), kUserAgent}};
        const QStringList sha1s = pathsBySha1.keys();
        for (int offset = 0; offset < sha1s.size(); offset += kLookupChunk) {
            const QByteArray body =
                ModpackExport::modrinthVersionFilesRequestBody(sha1s.mid(offset, kLookupChunk));
            if (body.isEmpty()) {
                continue;
            }
            const HttpClient::Reply reply = HttpClient::send(
                &network, QUrl(root + QStringLiteral("/version_files")), HttpClient::Verb::Post, body,
                QByteArrayLiteral("application/json"), QByteArray(), HttpClient::kDefaultTimeoutMs,
                headers);
            if (!reply.ok()) {
                // 这一发没成：**到此为止**（不再往下问 CurseForge），但**已经认出来的那些
                // 留着** —— 它们是真的，没理由因为后面这一发失败就把它们扔掉。
                //
                // ★ 真正要紧的是 `problem` 立起来了：接的人按它决定"没认出来的那些**别**
                //   记成'问过了、没有'"。半真半假的一趟要是把负缓存写了，用户这一整个会话
                //   都再也看不到图标（见 Result::problem 那段）。
                result.problem = reply.networkError.isEmpty()
                                     ? QObject::tr("Modrinth 反查回了 HTTP %1。").arg(reply.status)
                                     : QObject::tr("Modrinth 反查没连上：%1").arg(reply.networkError);
                return result;
            }
            QString problem;
            const QHash<QString, ModpackExport::ModrinthHit> part =
                ModpackExport::parseModrinthVersionFiles(reply.raw, &problem);
            if (!problem.isEmpty()) {
                result.problem = problem;
                return result;
            }
            for (auto it = part.constBegin(); it != part.constEnd(); ++it) {
                // 这个哈希对应哪几个文件（见上面那段）。同名的两个都要有图标。
                const QString projectId = it.value().projectId;
                if (projectId.isEmpty()) {
                    continue;
                }
                for (const QString &path : pathsBySha1.value(it.key())) {
                    Found found;
                    found.path = path;
                    found.projectId = projectId;
                    found.source = ResourceCatalog::Source::Modrinth;
                    // 图标地址**这一发给不出来**（`/v2/version_files` 是"版本"的回复，里面
                    // 没有项目的图标）—— 下一段专门为它再问一发，这里先空着。
                    result.found.append(found);
                }
            }
        }

        // ---------- ②' 这些项目号各自的图标地址 ----------
        //
        // ★ 这一段是 2026-09-27 补的，缘由是一次实测：原来这里不收图标地址，指望
        //   `Hit::iconPngUrl()` 自己拼的那条 `cdn.modrinth.com/data/<id>/icon.png` 兜住。
        //   那条地址**只是老项目的别名** —— 拿用户真实 `mods/` 里 20 个模组一个一个要过：
        //   **11 个回 404**（FerriteCore / Fabric Language Kotlin / Carpet / Fabric
        //   Permissions API / ArmorQuickSwap / Fox Friend …），只有 9 个回 200（恰好是
        //   "图标当年就叫 icon.png"的那几个：Sodium / Fabric API / Jade / cloth-config …）。
        //   修完再用本工程的 `icon-lookup-live` 对着一个真实例的 mods/ 复量（51 个模组）：
        //   **51 个全部拿到接口给的图标地址**，而那条老别名 33 个回 404、18 个回 200。
        //   于是这一页上**绝大多数模组只有占位拼图**，而屏幕上一点异常都没有（那一发 404
        //   被当成"这个项目没传过图标"—— 那本来是完全正常的一件事）。
        //   （用户原话：「模组的图标没有正确加载」。）
        //
        //   真正那条地址在**带哈希的文件名**底下（`…/<哈希>.png` 或 `…/<哈希>_96.webp`），
        //   除了接口没人说得出来。一发问一批（同一个上限），50 个号实测 894 字符、1.0 秒。
        QStringList wantedProjects;
        for (const Found &found : result.found) {
            if (!wantedProjects.contains(found.projectId)) {
                wantedProjects.append(found.projectId);
            }
        }
        QHash<QString, QString> iconByProject;
        for (int offset = 0; offset < wantedProjects.size(); offset += kLookupChunk) {
            const HttpClient::Reply reply = HttpClient::send(
                &network,
                QUrl(ResourceCatalog::projectsRequestUrl(wantedProjects.mid(offset, kLookupChunk),
                                                         root)),
                HttpClient::Verb::Get, QByteArray(), QByteArray(), QByteArray(),
                HttpClient::kDefaultTimeoutMs, headers);
            if (!reply.ok()) {
                // ★ **不 return**（和上面那两处不一样）：身份已经定下来了 —— 上面那一发认
                //   出来的那些是真的，缺的只是图，那几格退回占位拼图就是正确的界面。而且
                //   也不能往 `problem` 里写：它是"这一趟完整不完整"的判据，这一发失败不影响
                //   那个判断，写进去的代价是下一趟把几十兆 jar 从头重读一遍（见 Result::iconProblem）。
                result.iconProblem = reply.networkError.isEmpty()
                                         ? QObject::tr("Modrinth 取图标地址时回了 HTTP %1。")
                                               .arg(reply.status)
                                         : QObject::tr("Modrinth 取图标地址时没连上：%1")
                                               .arg(reply.networkError);
                break;
            }
            const QHash<QString, QString> part = ResourceCatalog::parseProjectIconUrls(reply.raw);
            for (auto it = part.constBegin(); it != part.constEnd(); ++it) {
                iconByProject.insert(it.key(), it.value());
            }
        }
        for (Found &found : result.found) {
            found.iconUrl = iconByProject.value(found.projectId);
        }
    }

    // ---------- ③ CurseForge：指纹 → 文件 → 项目 → logo ----------
    if (!pathsByFingerprint.isEmpty()) {
        const QString base = ModpackImport::curseForgeApiBase(options.curseForgeApiKey,
                                                              options.curseForgeApiBaseOverride);
        const bool hasKey = !options.curseForgeApiKey.isEmpty();
        QList<QPair<QByteArray, QByteArray>> headers;
        if (hasKey) {
            headers.append({QByteArrayLiteral("x-api-key"), options.curseForgeApiKey.toUtf8()});
        }

        // 指纹 → 项目号。和 Modrinth 那一支一样分批发（同一台接口的同一个上限）。
        QHash<quint32, int> projectByFingerprint;
        const QList<quint32> fingerprints = pathsByFingerprint.keys();
        for (int offset = 0; offset < fingerprints.size(); offset += kLookupChunk) {
            const QByteArray body = ModpackExport::curseForgeFingerprintsRequestBody(
                fingerprints.mid(offset, kLookupChunk));
            if (body.isEmpty()) {
                continue;
            }
            const HttpClient::Reply reply = HttpClient::send(
                &network, QUrl(base + QStringLiteral("/fingerprints")), HttpClient::Verb::Post, body,
                QByteArrayLiteral("application/json"), QByteArray(), HttpClient::kDefaultTimeoutMs,
                headers);
            if (!reply.ok()) {
                // 同上：已经认出来的留着，problem 立起来（那一句是给日志的）。
                result.problem = reply.networkError.isEmpty()
                                     ? QObject::tr("CurseForge 反查回了 HTTP %1。").arg(reply.status)
                                     : QObject::tr("CurseForge 反查没连上：%1").arg(reply.networkError);
                return result;
            }
            QString problem;
            const QHash<quint32, ModpackExport::CurseForgeHit> part =
                ModpackExport::parseCurseForgeFingerprints(reply.raw, &problem);
            if (!problem.isEmpty()) {
                result.problem = problem;
                return result;
            }
            for (auto it = part.constBegin(); it != part.constEnd(); ++it) {
                if (it.value().projectId > 0) {
                    projectByFingerprint.insert(it.key(), it.value().projectId);
                }
            }
        }

        // 项目号 → logo 那两条地址。**只有认出来的项目才问**（十几个号一发就够）。
        QList<int> projectIds;
        for (auto it = projectByFingerprint.constBegin(); it != projectByFingerprint.constEnd();
             ++it) {
            if (!projectIds.contains(it.value())) {
                projectIds.append(it.value());
            }
        }
        QHash<int, QPair<QString, QString>> logos;
        for (int offset = 0; offset < projectIds.size(); offset += kLookupChunk) {
            const QByteArray body =
                ModpackImport::curseForgeModsRequestBody(projectIds.mid(offset, kLookupChunk));
            if (body.isEmpty()) {
                continue;
            }
            const HttpClient::Reply reply = HttpClient::send(
                &network, QUrl(base + QStringLiteral("/mods")), HttpClient::Verb::Post, body,
                QByteArrayLiteral("application/json"), QByteArray(), HttpClient::kDefaultTimeoutMs,
                headers);
            if (!reply.ok()) {
                // 这一发只影响"图标地址拿不拿得到"，不改变"这是哪个资源"（那一步已经定了）。
                // 所以**认出来的那些照样往下走**（图标是空的，那一格退回占位拼图），
                // 只把 problem 立起来 —— 理由见上面那一处，两处逐字相同。
                result.problem = reply.networkError.isEmpty()
                                     ? QObject::tr("CurseForge 取图标时回了 HTTP %1。").arg(reply.status)
                                     : QObject::tr("CurseForge 取图标时没连上：%1").arg(reply.networkError);
                break;
            }
            const QHash<int, QPair<QString, QString>> part = parseCurseForgeModLogos(reply.raw);
            for (auto it = part.constBegin(); it != part.constEnd(); ++it) {
                logos.insert(it.key(), it.value());
            }
        }

        for (auto it = projectByFingerprint.constBegin(); it != projectByFingerprint.constEnd();
             ++it) {
            const int projectId = it.value();
            const QPair<QString, QString> logo = logos.value(projectId);
            for (const QString &path : pathsByFingerprint.value(it.key())) {
                Found found;
                found.path = path;
                found.projectId = QString::number(projectId);
                found.source = ResourceCatalog::Source::CurseForge;
                // 这一发没拿到 logo（接口没给 / 那个号在回复里缺席）也**照样认这一条**：
                // 图标是空的 ⇒ ResourceStore 发一张空 pixmap ⇒ 那一格退回占位拼图。
                // 比"整个来源都不认"好 —— 名字和身份还是对的。
                found.iconUrl = logo.first;
                found.iconFallbackUrl = logo.second;
                result.found.append(found);
            }
        }
    }

    return result;
}

}  // namespace InstanceIconLookup
