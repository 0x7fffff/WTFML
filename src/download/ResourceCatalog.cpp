/**
 * @file ResourceCatalog.cpp
 * @brief ResourceCatalog 的实现：拼请求地址 + 读接口回的那段 JSON。**一个网络请求都不发**
 *        （这个文件里没有 QNetworkAccessManager，将来也不会加 —— 发请求是 ResourceStore 的事）。
 * @author 0x7fffff
 * @date 2026-09-23
 */
#include "ResourceCatalog.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QUrl>

namespace ResourceCatalog {

namespace {

/// 接口的根地址。Modrinth 官方那一个 —— mcimirror 那个镜像也认 `/modrinth/v2/...`，
/// 但这一轮**先不接镜像**：搜索是一次一条的小请求，不是几千个对象那种非镜像不可的量，
/// 而官方那个实测也通（2026-09-23）。将来要接镜像，改的只有这一处。
QString apiRoot() { return QStringLiteral("https://api.modrinth.com/v2"); }

/// 图标的根地址。和 apiRoot 是**两个不同的域名**（cdn.），别合并。
QString cdnRoot() { return QStringLiteral("https://cdn.modrinth.com"); }

/// 百分号编码。查询参数的值一律过它 —— 搜索词是用户手打的，`&`、`#`、空格、中文什么都有，
/// 不编码的话拼出来的地址会在第一个 `&` 那里断掉，变成"搜别的东西"。
QString enc(const QString &text) {
    return QString::fromLatin1(QUrl::toPercentEncoding(text));
}

/// 把一串字符串拼成一维 JSON 数组再编码：`["1.21.1"]`。
///
/// NOTE: 用 QJsonDocument 拼，**不是手写方括号加引号**。手写的话，值里出现一个引号
///       （搜索词里就可能有）就拼出一段读不懂的 JSON，接口回 400 —— 而 400 到界面上
///       只是一句"搜索失败"，没人会想到是引号。
QString jsonArrayParam(const QStringList &values) {
    QJsonArray array;
    for (const QString &value : values) {
        array.append(value);
    }
    return enc(QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
}

/// 同上，但拼的是二维数组：`[["project_type:mod"],["versions:1.21.1"]]`。
/// facets 要的就是这个形状 —— **一层数组是"或"、两层是"且"**，少一层方括号整个筛选就失效
/// （接口不报错，只是把筛选当没写，症状是"明明按 1.21.1 筛的，出来一堆别的版本"）。
QString facetsParam(const QStringList &groups) {
    QJsonArray outer;
    for (const QString &group : groups) {
        QJsonArray inner;
        inner.append(group);
        outer.append(inner);
    }
    return enc(QString::fromUtf8(QJsonDocument(outer).toJson(QJsonDocument::Compact)));
}

/// JSON 里那种"可能没有"的字符串列表。**不是数组就当空**（接口偶尔给 null）。
QStringList stringList(const QJsonValue &value) {
    QStringList out;
    const QJsonArray array = value.toArray();
    out.reserve(array.size());
    for (const QJsonValue &item : array) {
        out.append(item.toString());
    }
    return out;
}

/// 读不懂 JSON 时统一的那句话。带上 QJsonParseError 的位置 —— 报"解析失败"等于没报，
/// 而这句是要显示给用户的，得让他能判断"是网络中间被劫持了"还是"接口变了"。
QString unreadableText(const QJsonParseError &error) {
    return QStringLiteral("接口返回的数据读不懂（第 %1 个字节：%2）")
        .arg(error.offset)
        .arg(error.errorString());
}

/// Modrinth 搜索里当"加载器"用的那几个词。
///
/// NOTE: 这张表和 ModpackImport::loaderDependencyKeys() **不是一回事**，别合并：
///       那几个是**整合包清单**里 `dependencies` 的键（一份包里只会有一个，用来判
///       "按哪个加载器装"）；这几个是**搜索接口**的 categories 词表（一个模组的 categories
///       里可能一个都没有 —— 比如数据包）。两边的词也不完全重合：这里有 liteloader /
///       modloader / rift，那边有 quilt。
const QStringList &loaderFacetNames() {
    static const QStringList names = {
        QStringLiteral("fabric"),     QStringLiteral("forge"),
        QStringLiteral("neoforge"),   QStringLiteral("quilt"),
        QStringLiteral("liteloader"), QStringLiteral("modloader"),
        QStringLiteral("rift"),
    };
    return names;
}

} // namespace

// ---------- 四类资源 ----------

QList<Kind> allKinds() {
    return {Kind::Mod, Kind::Modpack, Kind::Shader, Kind::ResourcePack};
}

QString kindKey(Kind kind) {
    switch (kind) {
    case Kind::Mod:
        return QStringLiteral("mod");
    case Kind::Modpack:
        return QStringLiteral("modpack");
    case Kind::Shader:
        return QStringLiteral("shader");
    case Kind::ResourcePack:
        return QStringLiteral("resourcepack");
    }
    return QString();
}

bool kindFromCategoryKey(const QString &key, Kind *out) {
    const QList<Kind> kinds = allKinds();
    for (Kind kind : kinds) {
        if (kindKey(kind) == key) {
            if (out != nullptr) {
                *out = kind;
            }
            return true;
        }
    }
    return false;
}

QString folderForKind(Kind kind) {
    switch (kind) {
    case Kind::Mod:
        return QStringLiteral("mods");
    case Kind::ResourcePack:
        return QStringLiteral("resourcepacks");
    case Kind::Shader:
        return QStringLiteral("shaderpacks");
    case Kind::Modpack:
        // 整合包下回来是个 .mrpack，接着走既有的导入流程 —— 往哪个目录放是那份清单说了算，
        // 不是这里说了算。回空串，调用方看到空串就该知道"这一类不走这条安装路"。
        return QString();
    }
    return QString();
}

QString kindLabel(Kind kind) {
    switch (kind) {
    case Kind::Mod:
        return QStringLiteral("模组");
    case Kind::Modpack:
        return QStringLiteral("整合包");
    case Kind::Shader:
        return QStringLiteral("光影");
    case Kind::ResourcePack:
        return QStringLiteral("资源包");
    }
    return QString();
}

// ---------- 搜索 ----------

QStringList Hit::loaders() const {
    const QStringList &known = loaderFacetNames();
    QStringList out;
    for (const QString &category : categories) {
        if (known.contains(category)) {
            out.append(category);
        }
    }
    return out;
}

QString Hit::iconPngUrl() const { return iconUrlForProject(projectId); }

QString searchUrl(Kind kind, const QString &query, int offset, int limit,
                  const QString &gameVersion, const QString &loader) {
    QStringList groups;
    groups.append(QStringLiteral("project_type:") + kindKey(kind));
    if (!gameVersion.isEmpty()) {
        groups.append(QStringLiteral("versions:") + gameVersion);
    }
    if (!loader.isEmpty()) {
        // 注意这里是 `categories:` 不是 `loaders:` —— Modrinth 的搜索把加载器当普通分类标签，
        // 写 `loaders:fabric` 实测筛不出东西（那个词只在版本接口里用）。
        groups.append(QStringLiteral("categories:") + loader);
    }

    QStringList params;
    params.append(QStringLiteral("query=") + enc(query));
    params.append(QStringLiteral("facets=") + facetsParam(groups));
    // 空搜索词按下载量排（那是"最热门的模组"），有搜索词按相关度排。不这样的话，
    // 搜「sodium」的头一条会是名字里带 sodium 但没人用的东西。
    params.append(QStringLiteral("index=") +
                  (query.isEmpty() ? QStringLiteral("downloads") : QStringLiteral("relevance")));
    params.append(QStringLiteral("offset=") + QString::number(qMax(0, offset)));
    params.append(QStringLiteral("limit=") + QString::number(qBound(1, limit, 100)));

    return apiRoot() + QStringLiteral("/search?") + params.join(QLatin1Char('&'));
}

SearchResult parseSearch(const QByteArray &json) {
    SearchResult result;

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        result.problem = unreadableText(error);
        return result;
    }

    const QJsonObject root = document.object();
    if (!root.contains(QStringLiteral("hits"))) {
        // 少了这一项就不是搜索响应（实测 400 时回的是 `{"error":...,"description":...}`）。
        // 把接口自己的那句话带出去 —— 它比"搜索失败"有用得多。
        const QString description = root.value(QStringLiteral("description")).toString();
        result.problem = description.isEmpty()
                             ? QStringLiteral("这不是一份搜索结果（里面没有 hits）")
                             : QStringLiteral("接口说：%1").arg(description);
        return result;
    }

    const QJsonArray hits = root.value(QStringLiteral("hits")).toArray();
    result.totalHits = root.value(QStringLiteral("total_hits")).toInt();
    result.offset = root.value(QStringLiteral("offset")).toInt();
    result.limit = root.value(QStringLiteral("limit")).toInt();
    result.hits.reserve(hits.size());
    for (const QJsonValue &item : hits) {
        const QJsonObject object = item.toObject();
        Hit hit;
        hit.projectId = object.value(QStringLiteral("project_id")).toString();
        hit.slug = object.value(QStringLiteral("slug")).toString();
        hit.title = object.value(QStringLiteral("title")).toString();
        hit.description = object.value(QStringLiteral("description")).toString();
        hit.author = object.value(QStringLiteral("author")).toString();
        // 下载量在 JSON 里可能大过 int（实测有 9 位数的），所以走 double 再截断。
        hit.downloads = static_cast<qint64>(object.value(QStringLiteral("downloads")).toDouble());
        hit.categories = stringList(object.value(QStringLiteral("categories")));
        hit.gameVersions = stringList(object.value(QStringLiteral("versions")));
        // 图标地址：只认我们那个域名（见 isUsableIconUrl），别的当"没给"——那时界面走
        // 自己拼的 icon.png。**注意这一条不等于"能用"**：它可能是一条 webp，而这一台
        // 机器解不解得动是 ResourceStore 的事。
        const QString givenIconUrl = object.value(QStringLiteral("icon_url")).toString();
        hit.iconUrl = isUsableIconUrl(givenIconUrl) ? givenIconUrl : QString();
        result.hits.append(hit);
    }

    // 一条都没有**不算错** —— "搜不到"和"没搜成"是两句话，界面上得分开说
    //（同一个坑在加载器树那边踩过：详见 MainWindow 里「无可用版本」与「列表没取到」）。
    result.ok = true;
    return result;
}

QStringList gameVersionChoices(const Hit &hit) {
    QStringList out;
    out.reserve(hit.gameVersions.size());
    // 倒着抄一遍（接口给的是旧的在前）。**不是排序** —— 见头文件那段，这两种顺序都是
    // 接口自己定的，我们只是把要显示的那个方向挑出来。
    for (int i = hit.gameVersions.size() - 1; i >= 0; --i) {
        out.append(hit.gameVersions.at(i));
    }
    return out;
}

QString iconUrlForProject(const QString &projectId) {
    if (projectId.isEmpty()) {
        return QString();
    }
    // 为什么要有它：接口给的那个 icon_url 常常是一条小 7 倍的 webp，而**webp 不是每台
    // 机器都解得动**（见 ImageDecode::webpAvailable）。解不动、或者接口干脆没给（
    // hit.iconUrl 为空）、或者给了条别的域名的，都退到这一条上来。
    // 这个地址实测回 200 image/png。项目没传过图标时回 404 —— 那不算错，画占位方块。
    return cdnRoot() + QStringLiteral("/data/") + enc(projectId) + QStringLiteral("/icon.png");
}

bool isWebpUrl(const QString &url) {
    if (url.isEmpty()) {
        return false;
    }
    // 走 QUrl::path() 而不是"整串 endsWith"：地址上可能挂着查询串 / 片段，那样
    // `…_96.webp?x=1` 会被判成不是 webp。取到 path 之后再比才算"看后缀"。
    return QUrl(url).path().endsWith(QStringLiteral(".webp"), Qt::CaseInsensitive);
}

bool isUsableIconUrl(const QString &url) {
    if (url.isEmpty()) {
        return false;
    }
    const QUrl parsed(url);
    // 三件事都过才放行：协议是 https（明文 http 的图不要）、host **全等**
    // cdn.modrinth.com（不是 endsWith —— 见头文件里 `cdn.modrinth.com.别的.com` 那段）、
    // 以及解析本身没出岔子（QUrl 对读不懂的串会给一个 host 为空的东西，那也过不了）。
    return parsed.isValid() && parsed.scheme() == QStringLiteral("https") &&
           parsed.host() == QStringLiteral("cdn.modrinth.com");
}

QString formatDownloads(qint64 downloads) {
    if (downloads < 0) {
        return QString();
    }
    // 单位只在这里换一次。界面上列表那一行、详情、以及将来别处显示的下载量都读它，
    // 否则"1234567 次"和"123 万次"会在同一个界面上各说各的。
    if (downloads >= 100000000) {
        return QStringLiteral("%1 亿").arg(downloads / 100000000.0, 0, 'f', 1);
    }
    if (downloads >= 10000) {
        // NOTE: 9999.95 万 ~ 1 亿 那一小段（不到一千万分之一的值）四舍五入后会显示成
        //       「10000.0 万」。数字是对的，只是单位没跟着进位 —— 换个单位要再切一刀，
        //       为一个这么窄的窗口不值当，先留着。
        return QStringLiteral("%1 万").arg(downloads / 10000.0, 0, 'f', 1);
    }
    return QString::number(downloads);
}

// ---------- 版本 ----------

const FileEntry *VersionEntry::primaryFile() const {
    for (const FileEntry &file : files) {
        if (file.primary) {
            return &file;
        }
    }
    // 一个版本实测**总是**有一个 primary，但读不到时退回第一条比回 nullptr 有用：
    // 前者下的是"作者写的那个文件"，后者在界面上是"点不动"。
    return files.isEmpty() ? nullptr : &files.first();
}

QString versionsUrl(const QString &projectId, const QString &gameVersion,
                    const QString &loader) {
    QStringList params;
    if (!gameVersion.isEmpty()) {
        params.append(QStringLiteral("game_versions=") + jsonArrayParam({gameVersion}));
    }
    if (!loader.isEmpty()) {
        // 这一条**确实**是 `loaders`（和搜索那边不一样，见 searchUrl 里的注）。
        params.append(QStringLiteral("loaders=") + jsonArrayParam({loader}));
    }

    QString url = apiRoot() + QStringLiteral("/project/") + enc(projectId) +
                  QStringLiteral("/version");
    if (!params.isEmpty()) {
        url += QStringLiteral("?") + params.join(QLatin1Char('&'));
    }
    return url;
}

QString searchRequestKey(Kind kind) {
    // 前缀是为了和下面那把一眼分得开（两把钥匙都会进同一张表）。
    return QStringLiteral("search|") + kindKey(kind);
}

QString versionsRequestKey(const QString &projectId, const QString &gameVersion,
                           const QString &loader) {
    // `|` 在 project_id（base62）、游戏版本号和加载器名里都不会出现，拿它当分隔符不用转义。
    return QStringLiteral("versions|") + projectId + QLatin1Char('|') + gameVersion +
           QLatin1Char('|') + loader;
}

QList<VersionEntry> parseVersions(const QByteArray &json) {
    QList<VersionEntry> out;

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) {
        return out;
    }

    const QJsonArray array = document.array();
    out.reserve(array.size());
    for (const QJsonValue &item : array) {
        const QJsonObject object = item.toObject();
        VersionEntry entry;
        entry.id = object.value(QStringLiteral("id")).toString();
        entry.versionNumber = object.value(QStringLiteral("version_number")).toString();
        entry.name = object.value(QStringLiteral("name")).toString();
        entry.versionType = object.value(QStringLiteral("version_type")).toString();
        entry.datePublished = object.value(QStringLiteral("date_published")).toString();
        entry.gameVersions = stringList(object.value(QStringLiteral("game_versions")));
        entry.loaders = stringList(object.value(QStringLiteral("loaders")));

        const QJsonArray files = object.value(QStringLiteral("files")).toArray();
        entry.files.reserve(files.size());
        for (const QJsonValue &fileValue : files) {
            const QJsonObject fileObject = fileValue.toObject();
            FileEntry file;
            file.url = fileObject.value(QStringLiteral("url")).toString();
            file.filename = fileObject.value(QStringLiteral("filename")).toString();
            file.size = static_cast<qint64>(fileObject.value(QStringLiteral("size")).toDouble());
            file.sha1 = fileObject.value(QStringLiteral("hashes"))
                            .toObject()
                            .value(QStringLiteral("sha1"))
                            .toString();
            file.primary = fileObject.value(QStringLiteral("primary")).toBool();
            entry.files.append(file);
        }

        const QJsonArray dependencies = object.value(QStringLiteral("dependencies")).toArray();
        entry.dependencies.reserve(dependencies.size());
        for (const QJsonValue &dependencyValue : dependencies) {
            const QJsonObject dependencyObject = dependencyValue.toObject();
            Dependency dependency;
            dependency.projectId =
                dependencyObject.value(QStringLiteral("project_id")).toString();
            // 实测这一项**可能是 null**（没钉具体版本，"随便哪个版本都行"）——
            // toString() 对 null 回空串，正好就是"没钉"。
            dependency.versionId =
                dependencyObject.value(QStringLiteral("version_id")).toString();
            dependency.type = dependencyObject.value(QStringLiteral("dependency_type")).toString();
            entry.dependencies.append(dependency);
        }

        out.append(entry);
    }

    // 顺序**原样**返回：接口给的就是新的在前（两份真响应的 date_published 都严格递减，
    // 见头文件那段），界面直接从上往下画。这里刻意不排：写一个"1.9 比 1.10 旧"的
    // 比较器，错了不会有任何东西报警。
    return out;
}

// ---------- 前置模组（这一轮只留接口）----------

QList<Dependency> requiredDependenciesOf(const QList<VersionEntry> &versions,
                                         const QString &versionId) {
    QList<Dependency> out;
    for (const VersionEntry &version : versions) {
        if (version.id != versionId) {
            continue;
        }
        for (const Dependency &dependency : version.dependencies) {
            if (dependency.required()) {
                out.append(dependency);
            }
        }
        break;
    }
    // NOTE: 光有 project_id 还下不了 —— 前置模组也得挑一个版本（`versionId` 常常是 null，
    //       意思是"哪个版本都行"）。那一步要问接口，得看目标实例是什么游戏版本/加载器，
    //       所以它不属于这个纯函数。用户要求这一轮**先不做**（原话「前置模组记得预留一键
    //       下载的接口，先不急」），所以这里就停在"要下谁"。
    return out;
}

} // namespace ResourceCatalog
