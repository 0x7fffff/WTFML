/**
 * @file ResourceCatalog.cpp
 * @brief ResourceCatalog 的实现：拼请求地址 + 读接口回的那段 JSON。**一个网络请求都不发**
 *        （这个文件里没有 QNetworkAccessManager，将来也不会加 —— 发请求是 ResourceStore 的事）。
 * @author 0x7fffff
 * @date 2026-09-23
 */
#include "ResourceCatalog.hpp"

#include "manage/ModpackImport.hpp"  // curseForgeApiBase() —— CurseForge 那张接口根地址的唯一出处

#include <algorithm>

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
/// CurseForge 那一家的接口根（`…/v1`，**带 v1 那一段**）。
///
/// 这里刻意**不写域名**：默认走哪一台（社区反代还是官方 + key）、用户在设置里填没填 key，
/// 全是 `ModpackImport::curseForgeApiBase()` 一处说了算（见那个函数的注释）。
/// 把 `apiBase` 当 override 传进去，三种情形就都落在那一个函数里了：
/// 传进来的非空 → 就用它（验证工程指本机假服务器）；空 → 没填 key 的那一套（社区反代）。
///
/// 症状提醒：这里要是自己拼一个 `https://api.curseforge.com/v1`，看起来"更直接"，
/// 但那一条**不带 key 就是 403**，而界面上只会显示"搜索失败"。
QString curseForgeApiRoot(const QString &apiBase) {
    return ModpackImport::curseForgeApiBase(QString(), apiBase);
}

/// 把 `{键, 中文标签}` 那张表变成界面上那一栏的选项，并在**最前面**补一项「全部」。
/// （两张表都不含「全部」那一项 —— 它是每一栏都有的，写进每一张表里迟早会漏一处。）
QList<FilterOption> filterOptions(const QList<QPair<QString, QString>> &pairs) {
    QList<FilterOption> out;
    out.reserve(pairs.size() + 1);
    out.append(FilterOption{QString(), QStringLiteral("全部")});
    for (const QPair<QString, QString> &pair : pairs) {
        out.append(FilterOption{pair.first, pair.second});
    }
    return out;
}

/// 「1.9 比 1.10 旧」那件事：按 `.` 切成段比。**只给 gameVersionsOldestFirst 用。**
///
/// 段里全是数字就按数比（字符串比会把 1.10 判成小于 1.9），否则按字符串比
/// （`1.21-pre1` 那种）；前面几段一样时**段数少的在前**（1.21 < 1.21.1）。
int compareVersionText(const QString &left, const QString &right) {
    const QStringList a = left.split(QLatin1Char('.'));
    const QStringList b = right.split(QLatin1Char('.'));
    const int shared = qMin(a.size(), b.size());
    for (int i = 0; i < shared; ++i) {
        bool aIsNumber = false;
        bool bIsNumber = false;
        const int aNumber = a.at(i).toInt(&aIsNumber);
        const int bNumber = b.at(i).toInt(&bIsNumber);
        if (aIsNumber && bIsNumber) {
            if (aNumber != bNumber) {
                return aNumber < bNumber ? -1 : 1;
            }
            continue;
        }
        const int compared = QString::compare(a.at(i), b.at(i));
        if (compared != 0) {
            return compared < 0 ? -1 : 1;
        }
    }
    if (a.size() != b.size()) {
        return a.size() < b.size() ? -1 : 1;
    }
    return 0;
}

/// 加载器那几个词在 CurseForge 的**答复正文**里长什么样（`gameVersions` 那一串里混着的
/// `Forge` / `NeoForge` / …）。**不是** filterOption 的键、也不是 `modLoaderType` 的数字 ——
/// 那是"问的时候"叫什么，这是"答的时候"叫什么，两套东西，别合并。
const QStringList &curseForgeLoaderWords() {
    static const QStringList words = {
        QStringLiteral("forge"),    QStringLiteral("neoforge"),
        QStringLiteral("fabric"),   QStringLiteral("quilt"),
    };
    return words;
}

/// CurseForge 的 `modLoader` / `modLoaderType` **数字** → 加载器的名字（小写）。
///
/// 实测那一套编号：1 Forge / 4 Fabric / 5 Quilt / 6 NeoForge。**0 和认不出来的回空串**
/// （0 是"没写"，实测有些包两边都不写）—— 不猜：猜错的话加载器那道闸会拿一个假名字去比。
QString curseForgeModLoaderName(int modLoader) {
    switch (modLoader) {
    case 1:
        return QStringLiteral("forge");
    case 4:
        return QStringLiteral("fabric");
    case 5:
        return QStringLiteral("quilt");
    case 6:
        return QStringLiteral("neoforge");
    default:
        return QString();
    }
}

/// CurseForge 的 `releaseType` 数字 → Modrinth 那三个词（第三层那行小字两个来源得一样）。
/// 认不出来（0 = 没写）回空串：**不猜**，界面上那一截就不显示。
QString curseForgeReleaseType(int releaseType) {
    switch (releaseType) {
    case 1:
        return QStringLiteral("release");
    case 2:
        return QStringLiteral("beta");
    case 3:
        return QStringLiteral("alpha");
    default:
        return QString();
    }
}

/// 一条 CurseForge 的 `hashes[]` 里挑出 sha1（`algo == 1`；`2` 是 md5）。
/// 取 `hashes[0]` 是错的 —— 实测那一条**正好**是 sha1，但顺序没有保证。
QString curseForgeSha1(const QJsonObject &file) {
    const QJsonArray hashes = file.value(QStringLiteral("hashes")).toArray();
    for (const QJsonValue &hash : hashes) {
        const QJsonObject object = hash.toObject();
        if (object.value(QStringLiteral("algo")).toInt() == 1) {
            return object.value(QStringLiteral("value")).toString();
        }
    }
    return QString();
}

/// 那一串 `gameVersions` 里，**哪些是真游戏版本**、哪些不是。
///
/// CurseForge 把三样东西混在同一个数组里（实测 JEI 那一条：`["1.21","Client","Forge",
/// "1.21.1","Server"]`）—— 后三个是"哪一侧"和"哪个加载器"。判据是**以数字开头**：
/// 游戏版本号一定这么开头（`1.20.1` / `26.3`），而 `Client` / `Server` / `Forge` 都不。
bool looksLikeGameVersion(const QString &text) {
    return !text.isEmpty() && text.at(0).isDigit();
}

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

// ---------- 几类资源 ----------

QList<Kind> allKinds() {
    // 顺序照下载页左栏那份表（去掉「游戏」）：整合包 / 模组 / 数据包 / 资源包 / 光影。
    return {Kind::Modpack, Kind::Mod, Kind::DataPack, Kind::ResourcePack, Kind::Shader};
}

QString kindKey(Kind kind) {
    switch (kind) {
    case Kind::Mod:
        return QStringLiteral("mod");
    case Kind::Modpack:
        return QStringLiteral("modpack");
    case Kind::DataPack:
        // Modrinth 的 project_type 里就叫 datapack（界面上那一条写「数据包」）。
        return QStringLiteral("datapack");
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
    case Kind::DataPack:
        // 和三个兄弟类一样下进实例自己的目录。原版只认存档里那份（`<存档>/datapacks/`）——
        // 这件事写在头文件那段里，界面上那句提示也得说清楚（见 downloadSearchHints）。
        return QStringLiteral("datapacks");
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
    case Kind::DataPack:
        return QStringLiteral("数据包");
    case Kind::Shader:
        return QStringLiteral("光影");
    case Kind::ResourcePack:
        return QStringLiteral("资源包");
    }
    return QString();
}

// ---------- 来源 ----------

QList<Source> allSources() {
    // 顺序就是界面上那个下拉框的顺序：Modrinth 在前面（不要 key、原来就能用的那个）。
    return {Source::Modrinth, Source::CurseForge};
}

QString sourceKey(Source source) {
    switch (source) {
    case Source::Modrinth:
        return QStringLiteral("modrinth");
    case Source::CurseForge:
        return QStringLiteral("curseforge");
    }
    return QString();
}

QString sourceLabel(Source source) {
    switch (source) {
    case Source::Modrinth:
        return QStringLiteral("Modrinth");
    case Source::CurseForge:
        return QStringLiteral("CurseForge");
    }
    return QString();
}

bool sourceFromKey(const QString &key, Source *out) {
    const QList<Source> sources = allSources();
    for (Source source : sources) {
        if (sourceKey(source) == key) {
            if (out != nullptr) {
                *out = source;
            }
            return true;
        }
    }
    return false;
}

int curseForgeClassId(Kind kind) {
    // 九条 `isClass=true` 里我们用到的那五个（表在 .hpp 里）。**和 ModpackImport 那边
    // 是同一套号** —— 这边管"去哪个类目里搜"，那边管"清单里的文件落哪个目录"。
    switch (kind) {
    case Kind::Mod:
        return 6;
    case Kind::Modpack:
        return 4471;
    case Kind::DataPack:
        return 6945;
    case Kind::ResourcePack:
        return 12;
    case Kind::Shader:
        return 6552;
    }
    return 0;
}

// ---------- 那一行筛选项 ----------

QList<FilterOption> categoryOptions(Source source, Kind kind) {
    if (source == Source::CurseForge) {
        // 类目号来自 `/v1/categories?gameId=432`（2026-09-25 抄下来的）。**挑的是那一类底下
        // 的常用子类**（parentCategoryId == 那个类目号），不是全抄 —— 模组那一类有 52 条，
        // 里面一半是 `Addons/<某个模组>` 那种"给 Create 做附属"的，摆进下拉框没人找得到。
        switch (kind) {
        case Kind::Mod:
            return filterOptions({
                {QStringLiteral("6814"), QStringLiteral("优化")},
                {QStringLiteral("421"), QStringLiteral("前置库")},
                {QStringLiteral("434"), QStringLiteral("装备武器")},
                {QStringLiteral("424"), QStringLiteral("装饰外观")},
                {QStringLiteral("422"), QStringLiteral("冒险")},
                {QStringLiteral("419"), QStringLiteral("魔法")},
                {QStringLiteral("412"), QStringLiteral("科技")},
                {QStringLiteral("406"), QStringLiteral("世界生成")},
                {QStringLiteral("408"), QStringLiteral("矿物资源")},
                {QStringLiteral("407"), QStringLiteral("生物群系")},
                {QStringLiteral("411"), QStringLiteral("生物")},
                {QStringLiteral("409"), QStringLiteral("结构")},
                {QStringLiteral("436"), QStringLiteral("食物")},
                {QStringLiteral("420"), QStringLiteral("存储")},
                {QStringLiteral("4558"), QStringLiteral("红石")},
                {QStringLiteral("5191"), QStringLiteral("实用与体验")},
                {QStringLiteral("435"), QStringLiteral("服务端")},
                {QStringLiteral("423"), QStringLiteral("地图与信息")},
                {QStringLiteral("6821"), QStringLiteral("修复")},
                {QStringLiteral("425"), QStringLiteral("杂项")},
            });
        case Kind::Modpack:
            return filterOptions({
                {QStringLiteral("4472"), QStringLiteral("科技")},
                {QStringLiteral("4473"), QStringLiteral("魔法")},
                {QStringLiteral("4475"), QStringLiteral("冒险与 RPG")},
                {QStringLiteral("4476"), QStringLiteral("探索")},
                {QStringLiteral("4478"), QStringLiteral("任务")},
                {QStringLiteral("4736"), QStringLiteral("空岛")},
                {QStringLiteral("4483"), QStringLiteral("战斗与 PvP")},
                {QStringLiteral("4477"), QStringLiteral("小游戏")},
                {QStringLiteral("4484"), QStringLiteral("多人")},
                {QStringLiteral("5128"), QStringLiteral("原版增强")},
                {QStringLiteral("4479"), QStringLiteral("硬核")},
                {QStringLiteral("9243"), QStringLiteral("专家")},
                {QStringLiteral("7418"), QStringLiteral("恐怖")},
                {QStringLiteral("4481"), QStringLiteral("轻量")},
                {QStringLiteral("4482"), QStringLiteral("大型")},
                {QStringLiteral("4474"), QStringLiteral("科幻")},
            });
        case Kind::DataPack:
            return filterOptions({
                {QStringLiteral("6946"), QStringLiteral("模组支持")},
                {QStringLiteral("6951"), QStringLiteral("科技")},
                {QStringLiteral("6952"), QStringLiteral("魔法")},
                {QStringLiteral("6948"), QStringLiteral("冒险")},
                {QStringLiteral("6950"), QStringLiteral("前置库")},
                {QStringLiteral("6953"), QStringLiteral("实用")},
                {QStringLiteral("6949"), QStringLiteral("奇幻")},
                {QStringLiteral("6947"), QStringLiteral("杂项")},
            });
        case Kind::ResourcePack:
            return filterOptions({
                {QStringLiteral("393"), QStringLiteral("16x")},
                {QStringLiteral("394"), QStringLiteral("32x")},
                {QStringLiteral("395"), QStringLiteral("64x")},
                {QStringLiteral("396"), QStringLiteral("128x")},
                {QStringLiteral("397"), QStringLiteral("256x")},
                {QStringLiteral("398"), QStringLiteral("512x 及以上")},
                {QStringLiteral("403"), QStringLiteral("传统")},
                {QStringLiteral("401"), QStringLiteral("现代")},
                {QStringLiteral("402"), QStringLiteral("中世纪")},
                {QStringLiteral("400"), QStringLiteral("写实")},
                {QStringLiteral("404"), QStringLiteral("动画")},
                {QStringLiteral("4465"), QStringLiteral("模组支持")},
                {QStringLiteral("5244"), QStringLiteral("字体")},
                {QStringLiteral("405"), QStringLiteral("杂项")},
            });
        case Kind::Shader:
            return filterOptions({
                {QStringLiteral("6553"), QStringLiteral("写实")},
                {QStringLiteral("6554"), QStringLiteral("奇幻")},
                {QStringLiteral("6555"), QStringLiteral("原版风")},
            });
        }
        return QList<FilterOption>();
    }

    // Modrinth：键是 slug（`/v2/tag/category` 里那些）。**母集比 CurseForge 小**，
    // 所以没有被砍掉的"附属模组"那类问题，反而要挑：资源包那 29 条里一半是
    // `resolutions` / `features` 那种"改了哪一块"的细分。
    switch (kind) {
    case Kind::Mod:
        return filterOptions({
            {QStringLiteral("optimization"), QStringLiteral("优化")},
            {QStringLiteral("library"), QStringLiteral("前置库")},
            {QStringLiteral("equipment"), QStringLiteral("装备")},
            {QStringLiteral("decoration"), QStringLiteral("装饰")},
            {QStringLiteral("adventure"), QStringLiteral("冒险")},
            {QStringLiteral("magic"), QStringLiteral("魔法")},
            {QStringLiteral("technology"), QStringLiteral("科技")},
            {QStringLiteral("worldgen"), QStringLiteral("世界生成")},
            {QStringLiteral("mobs"), QStringLiteral("生物")},
            {QStringLiteral("food"), QStringLiteral("食物")},
            {QStringLiteral("storage"), QStringLiteral("存储")},
            {QStringLiteral("transportation"), QStringLiteral("运输")},
            {QStringLiteral("game-mechanics"), QStringLiteral("游戏机制")},
            {QStringLiteral("management"), QStringLiteral("管理")},
            {QStringLiteral("minigame"), QStringLiteral("小游戏")},
            {QStringLiteral("social"), QStringLiteral("社交")},
            {QStringLiteral("utility"), QStringLiteral("实用")},
            {QStringLiteral("cursed"), QStringLiteral("诅咒")},
        });
    case Kind::Modpack:
        return filterOptions({
            {QStringLiteral("optimization"), QStringLiteral("优化")},
            {QStringLiteral("technology"), QStringLiteral("科技")},
            {QStringLiteral("magic"), QStringLiteral("魔法")},
            {QStringLiteral("adventure"), QStringLiteral("冒险")},
            {QStringLiteral("quests"), QStringLiteral("任务")},
            {QStringLiteral("combat"), QStringLiteral("战斗")},
            {QStringLiteral("multiplayer"), QStringLiteral("多人")},
            {QStringLiteral("challenging"), QStringLiteral("挑战")},
            {QStringLiteral("kitchen-sink"), QStringLiteral("大杂烩")},
            {QStringLiteral("lightweight"), QStringLiteral("轻量")},
        });
    case Kind::DataPack:
        // **空表**：Modrinth 的分类表里根本没有 datapack 这一档（实测 `/v2/tag/category`
        // 只给 mod / modpack / resourcepack / shader / 服务器）。界面上看到空表就把整个
        // 「类型」框藏起来 —— 摆一个只有「全部」的框等于骗人。
        return QList<FilterOption>();
    case Kind::ResourcePack:
        return filterOptions({
            {QStringLiteral("16x"), QStringLiteral("16x")},
            {QStringLiteral("32x"), QStringLiteral("32x")},
            {QStringLiteral("64x"), QStringLiteral("64x")},
            {QStringLiteral("128x"), QStringLiteral("128x")},
            {QStringLiteral("256x"), QStringLiteral("256x")},
            {QStringLiteral("512x+"), QStringLiteral("512x 及以上")},
            {QStringLiteral("realistic"), QStringLiteral("写实")},
            {QStringLiteral("simplistic"), QStringLiteral("简约")},
            {QStringLiteral("themed"), QStringLiteral("主题")},
            {QStringLiteral("vanilla-like"), QStringLiteral("原版风")},
            {QStringLiteral("decoration"), QStringLiteral("装饰")},
            {QStringLiteral("modded"), QStringLiteral("模组支持")},
            {QStringLiteral("tweaks"), QStringLiteral("微调")},
            {QStringLiteral("cursed"), QStringLiteral("诅咒")},
        });
    case Kind::Shader:
        return filterOptions({
            {QStringLiteral("realistic"), QStringLiteral("写实")},
            {QStringLiteral("semi-realistic"), QStringLiteral("半写实")},
            {QStringLiteral("fantasy"), QStringLiteral("奇幻")},
            {QStringLiteral("cartoon"), QStringLiteral("卡通")},
            {QStringLiteral("vanilla-like"), QStringLiteral("原版风")},
            {QStringLiteral("potato"), QStringLiteral("极低配")},
            {QStringLiteral("path-tracing"), QStringLiteral("光追")},
            {QStringLiteral("pbr"), QStringLiteral("PBR 材质")},
            {QStringLiteral("colored-lighting"), QStringLiteral("彩色光照")},
            {QStringLiteral("reflections"), QStringLiteral("反射")},
            {QStringLiteral("shadows"), QStringLiteral("阴影")},
            {QStringLiteral("atmosphere"), QStringLiteral("大气")},
            {QStringLiteral("foliage"), QStringLiteral("植被")},
            {QStringLiteral("bloom"), QStringLiteral("泛光")},
        });
    }
    return QList<FilterOption>();
}

QList<FilterOption> loaderOptions(Source source) {
    // 两边**顺序一样**（fabric / forge / neoforge / quilt），键不一样：Modrinth 要那个词，
    // CurseForge 要 `modLoaderType` 的**数字**（实测 1 Forge / 4 Fabric / 5 Quilt /
    // 6 NeoForge —— 这组数字和答复里 `latestFilesIndexes[].modLoader` 是同一套）。
    if (source == Source::CurseForge) {
        return filterOptions({
            {QStringLiteral("4"), QStringLiteral("Fabric")},
            {QStringLiteral("1"), QStringLiteral("Forge")},
            {QStringLiteral("6"), QStringLiteral("NeoForge")},
            {QStringLiteral("5"), QStringLiteral("Quilt")},
        });
    }
    return filterOptions({
        {QStringLiteral("fabric"), QStringLiteral("Fabric")},
        {QStringLiteral("forge"), QStringLiteral("Forge")},
        {QStringLiteral("neoforge"), QStringLiteral("NeoForge")},
        {QStringLiteral("quilt"), QStringLiteral("Quilt")},
    });
}

// ---------- 搜索 ----------

QStringList Hit::loaders() const {
    // 接口自己说了加载器就**只认它**（CurseForge 那条路：categories 是「Technology」那种
    // 分类标签，按老规矩去里面找 `fabric` 会一个都找不到）。
    if (!explicitLoaders.isEmpty()) {
        return explicitLoaders;
    }
    const QStringList &known = loaderFacetNames();
    QStringList out;
    for (const QString &category : categories) {
        if (known.contains(category)) {
            out.append(category);
        }
    }
    return out;
}

QString Hit::iconPngUrl() const {
    // CurseForge 那条路拼不出地址来（那边的图在一个带哈希的路径下，**只有接口知道**），
    // 所以它的"兜底"就是接口给的第二条（`logo.url`，见 iconFallbackUrl 那段）。
    if (source == Source::CurseForge) {
        return iconFallbackUrl;
    }
    return iconUrlForProject(projectId);
}

QString Hit::iconCacheId() const {
    // 两个 id 空间各在自己那套里唯一，而 `238222` 这种纯数字 id 在 Modrinth 的 base62 里
    // 也是合法的 —— 不带来源的话，撞上之后那格的图标会一直是**另一个**资源的
    // （缓存默认不过期 ⇒ 一直错下去，而且看起来一个错都没有）。
    return sourceKey(source) + QLatin1Char('-') + projectId;
}

QString searchUrl(Kind kind, const QString &query, int offset, int limit,
                  const QString &gameVersion, const QString &loader, Source source,
                  const QString &category, const QString &apiBase) {
    if (source == Source::CurseForge) {
        // 这一家的 `/mods/search` **没有 project_type 这个概念**，只有 `classId`（类目号）
        // —— 也就是说"搜模组"和"搜资源包"在这里是靠类目号分开的，少传它就会把九类东西
        // 全混在一起回给你。
        QStringList params;
        params.append(QStringLiteral("gameId=432"));  // Minecraft（写死的，这家还管别的游戏）
        params.append(QStringLiteral("classId=") + QString::number(curseForgeClassId(kind)));
        params.append(QStringLiteral("search=") + enc(query));
        if (!gameVersion.isEmpty()) {
            params.append(QStringLiteral("gameVersion=") + enc(gameVersion));
        }
        if (!loader.isEmpty()) {
            // 数字（见 loaderOptions）。**空串不拼这一段** —— 不筛就是没有这个参数，
            // 而不是发一个空值上去（两个接口对空值的处理都不一样，靠不住）。
            params.append(QStringLiteral("modLoaderType=") + enc(loader));
        }
        if (!category.isEmpty()) {
            params.append(QStringLiteral("categoryId=") + enc(category));
        }
        params.append(QStringLiteral("index=") + QString::number(qMax(0, offset)));
        params.append(QStringLiteral("pageSize=") + QString::number(qBound(1, limit, 50)));
        // 排序：一个值就够（实测，见 .hpp 那段）。`sortOrder=desc` 不能省 —— 这一家的默认
        // 方向是 asc，那样头一条会变成"这一类里最没人用的"。
        params.append(QStringLiteral("sortField=6"));
        params.append(QStringLiteral("sortOrder=desc"));

        return curseForgeApiRoot(apiBase) + QStringLiteral("/mods/search?") +
               params.join(QLatin1Char('&'));
    }

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
    if (!category.isEmpty()) {
        // 类型和加载器在这边**是同一种东西**（都是 categories facet）—— 所以它俩同时选上时
        // 是"且"（两层数组）而不是互相覆盖。facets 的"一层或、两层且"见 facetsParam。
        groups.append(QStringLiteral("categories:") + category);
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
        hit.source = Source::Modrinth;  // 写出来：读的人一眼看得出这半边的来源
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

SearchResult parseCurseForgeSearch(const QByteArray &json) {
    SearchResult result;

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        result.problem = unreadableText(error);
        return result;
    }

    const QJsonObject root = document.object();
    if (!root.contains(QStringLiteral("data"))) {
        // 不是一份搜索结果。出错时那一家回的是 `{"error":"…"}`，而**社区反代**在超时 /
        // 过载时回的可能是别的形状（比如一个 Cloudflare 的页面被当成 json 塞进来）。
        // 两条都带出去：接口自己的那句话优先，没有就如实说形状不对。
        const QString apiError = root.value(QStringLiteral("error")).toString();
        result.problem = !apiError.isEmpty()
                             ? QStringLiteral("接口说：%1").arg(apiError)
                             : QStringLiteral("这不是一份搜索结果（里面没有 data）");
        return result;
    }

    const QJsonArray data = root.value(QStringLiteral("data")).toArray();
    const QJsonObject pagination = root.value(QStringLiteral("pagination")).toObject();
    result.totalHits = pagination.value(QStringLiteral("totalCount")).toInt();
    result.offset = pagination.value(QStringLiteral("index")).toInt();
    result.limit = pagination.value(QStringLiteral("pageSize")).toInt();
    result.hits.reserve(data.size());
    for (const QJsonValue &item : data) {
        const QJsonObject object = item.toObject();
        Hit hit;
        hit.source = Source::CurseForge;
        // 那边的 `id` 是个**数字**（1546263），而 Modrinth 那个是 base62 字符串。这里统一
        // 存成字符串 —— 下游（图标缓存文件名、请求钥匙、问版本列表）只把它当一个不透明的
        // 标识，没有一处需要它是数字。
        hit.projectId = QString::number(object.value(QStringLiteral("id")).toDouble(), 'f', 0);
        hit.slug = object.value(QStringLiteral("slug")).toString();
        hit.title = object.value(QStringLiteral("name")).toString();   // 名字在 `name` 里
        hit.description = object.value(QStringLiteral("summary")).toString();
        // 作者：那边是个数组（实测只有一位）。取第一个 —— 这一行小字放不下第二个。
        hit.author = object.value(QStringLiteral("authors")).toArray().first().toObject()
                         .value(QStringLiteral("name")).toString();
        hit.downloads = static_cast<qint64>(object.value(QStringLiteral("downloadCount")).toDouble());
        // categories 在那边是一个个**对象**（`{"name":"Technology",…}`），取名字那一项。
        // 这一串现在只在出错排查时看（界面那行小字用的是作者和下载量），存英文名最贴近原样。
        const QJsonArray categories = object.value(QStringLiteral("categories")).toArray();
        for (const QJsonValue &category : categories) {
            hit.categories.append(category.toObject().value(QStringLiteral("name")).toString());
        }
        // 游戏版本和加载器都从 `latestFilesIndexes` 来（那边一次不给整张 supported-versions
        // 表）：那是"每个游戏版本各有一条最新文件"的小表，所以**要按版本去重**，然后排成
        // **旧的在前**（和 Modrinth 那份同一个方向，见 gameVersionsOldestFirst）。
        QStringList versions;
        QStringList loaders;
        const QJsonArray indexes = object.value(QStringLiteral("latestFilesIndexes")).toArray();
        for (const QJsonValue &index : indexes) {
            const QJsonObject entry = index.toObject();
            const QString version = entry.value(QStringLiteral("gameVersion")).toString();
            if (!version.isEmpty() && !versions.contains(version)) {
                versions.append(version);
            }
            // `modLoader` 是数字（1 Forge / 4 Fabric / 5 Quilt / 6 NeoForge），0 = 没写。
            // 翻成小写的名字 —— 加载器那道闸和界面上那行小字认的都是小写。
            const QString loader = curseForgeModLoaderName(entry.value(QStringLiteral("modLoader")).toInt());
            if (!loader.isEmpty() && !loaders.contains(loader)) {
                loaders.append(loader);
            }
        }
        hit.gameVersions = gameVersionsOldestFirst(versions);
        hit.explicitLoaders = loaders;

        // 图标：接口给两条（256px 的缩略图 + 原图），一大一小。**主次关系和 Modrinth 那边
        // 一样**（先试小的），只是那边那条小的是 webp、这边两条都是 png。
        // 两条都要过 isUsableIconUrl（域名是 media.forgecdn.net）—— 那是外部输入。
        const QJsonObject logo = object.value(QStringLiteral("logo")).toObject();
        const QString thumbnail = logo.value(QStringLiteral("thumbnailUrl")).toString();
        const QString full = logo.value(QStringLiteral("url")).toString();
        hit.iconUrl = isUsableIconUrl(thumbnail, Source::CurseForge) ? thumbnail : QString();
        hit.iconFallbackUrl = isUsableIconUrl(full, Source::CurseForge) ? full : QString();
        result.hits.append(hit);
    }

    // 一条都没有**不算错**（和 Modrinth 那条一样：搜不到 ≠ 没搜成）。
    result.ok = true;
    return result;
}

QStringList gameVersionsOldestFirst(const QStringList &versions) {
    QStringList out = versions;
    // stable_sort：比不出大小的两条（同一个版本写两种法）保持接口给的次序。
    std::stable_sort(out.begin(), out.end(), [](const QString &left, const QString &right) {
        return compareVersionText(left, right) < 0;
    });
    return out;
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

bool isUsableIconUrl(const QString &url, Source source) {
    if (url.isEmpty()) {
        return false;
    }
    // 图在**各自那一家**的域名上（实测）：Modrinth 是 cdn.modrinth.com，CurseForge 是
    // media.forgecdn.net。**不是"两个域都放行"** —— CurseForge 那条路上收到一条
    // cdn.modrinth.com 的地址是不该发生的事（要么接口换了、要么正文被人改过），
    // 那种时候当"没给"比"照着要一张"稳。
    const QString expected = source == Source::CurseForge ? QStringLiteral("media.forgecdn.net")
                                                          : QStringLiteral("cdn.modrinth.com");
    const QUrl parsed(url);
    // 三件事都过才放行：协议是 https（明文 http 的图不要）、host **全等**（不是 endsWith
    // —— 见头文件里 `cdn.modrinth.com.别的.com` 那段）、以及解析本身没出岔子
    //（QUrl 对读不懂的串会给一个 host 为空的东西，那也过不了）。
    return parsed.isValid() && parsed.scheme() == QStringLiteral("https") && parsed.host() == expected;
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
                    const QString &loader, Source source, const QString &apiBase) {
    if (source == Source::CurseForge) {
        // `GET {base}/mods/{id}/files?gameVersion=&modLoaderType=`。名字和搜索那边**一样**
        // 是谁的就是谁的：那一家同一个概念在两处用同一个参数名（和 Modrinth 正好相反，
        // 那边搜索叫 categories、版本叫 loaders）。
        QStringList params;
        if (!gameVersion.isEmpty()) {
            params.append(QStringLiteral("gameVersion=") + enc(gameVersion));
        }
        if (!loader.isEmpty()) {
            // 这里也要**数字**（见 loaderOptions）—— 传一个 `fabric` 上去不会报错，
            // 只是筛不出东西。
            params.append(QStringLiteral("modLoaderType=") + enc(loader));
        }
        QString url = curseForgeApiRoot(apiBase) + QStringLiteral("/mods/") + enc(projectId) +
                      QStringLiteral("/files");
        if (!params.isEmpty()) {
            url += QStringLiteral("?") + params.join(QLatin1Char('&'));
        }
        return url;
    }

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

QString searchRequestKey(Kind kind, Source source) {
    // 前缀是为了和下面那把一眼分得开（两把钥匙都会进同一张表）。来源必带：用户在一个页面上
    // 从 Modrinth 切成 CurseForge 时，前一趟回来的结果**不是**"我现在等的那一发"。
    return QStringLiteral("search|") + sourceKey(source) + QLatin1Char('|') + kindKey(kind);
}

QString versionsRequestKey(const QString &projectId, const QString &gameVersion,
                           const QString &loader, Source source) {
    // `|` 在 project_id（base62 或纯数字）、游戏版本号和加载器名里都不会出现，拿它当分隔符
    // 不用转义。来源也带上了：两边 id 空间是分开的，"238222" 在两边是两个不同的项目。
    return QStringLiteral("versions|") + sourceKey(source) + QLatin1Char('|') + projectId +
           QLatin1Char('|') + gameVersion + QLatin1Char('|') + loader;
}

QList<VersionEntry> parseVersions(const QByteArray &json) {
    QList<VersionEntry> out;

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError) {
        return out;
    }
    if (document.isObject()) {
        // 根是对象 = CurseForge 那一份（`{"data":[…]}`）。**按根分岔**而不是让调用方先判
        // 来源：调用方判的话，"哪个接口回哪种形状"这件事就抄到了调用点上。
        return parseCurseForgeVersions(json);
    }
    if (!document.isArray()) {
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

QList<VersionEntry> parseCurseForgeVersions(const QByteArray &json) {
    QList<VersionEntry> out;

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return out;
    }
    const QJsonArray array = document.object().value(QStringLiteral("data")).toArray();
    out.reserve(array.size());
    for (const QJsonValue &item : array) {
        const QJsonObject object = item.toObject();
        VersionEntry entry;
        // 那边**一条就是一个文件**（不是"一个版本里带几个文件"）：每条有自己的 id、文件名
        // 和自己的 `gameVersions`。所以一条直接变成一个 VersionEntry、`files` 里只有它自己
        //（Modrinth 那边是"一个版本带文件数组"，形状不同但到界面上的形状是同一个）。
        const qint64 fileId = static_cast<qint64>(object.value(QStringLiteral("id")).toDouble());
        entry.id = QString::number(fileId);
        const QString fileName = object.value(QStringLiteral("fileName")).toString();
        // 版本号那两栏都用**接口给的那两串**：`displayName` 是给用户看的
        //（实测 `19.57.0.449 for Forge 1.21.1`），`versionNumber` 用它比用文件名更像版本号。
        // 界面第三层画的是**文件名**（要落盘的就是它），两者都留着。
        entry.versionNumber = object.value(QStringLiteral("displayName")).toString();
        entry.name = entry.versionNumber;
        entry.versionType =
            curseForgeReleaseType(object.value(QStringLiteral("releaseType")).toInt());
        entry.datePublished = object.value(QStringLiteral("fileDate")).toString();

        // `gameVersions` 那一串混着三样东西（游戏版本 / 哪一侧 / 哪个加载器），要挑开：
        // 游戏版本进 `gameVersions`，加载器进 `loaders`（翻成小写，加载器那道闸和小字都认
        // 小写），`Client` / `Server` 这一类**两边都不进**（界面上没有它们的位置）。
        const QStringList raw = stringList(object.value(QStringLiteral("gameVersions")));
        const QStringList &loaderWords = curseForgeLoaderWords();
        for (const QString &word : raw) {
            if (looksLikeGameVersion(word)) {
                entry.gameVersions.append(word);
                continue;
            }
            const QString lowered = word.toLower();
            if (loaderWords.contains(lowered)) {
                entry.loaders.append(lowered);
            }
        }

        FileEntry file;
        file.filename = fileName;
        file.size = static_cast<qint64>(object.value(QStringLiteral("fileLength")).toDouble());
        file.sha1 = curseForgeSha1(object);
        file.primary = true;  // 一条就是一个文件，它就是这一版的那个主文件
        // `downloadUrl` **可能是 null** = 作者在项目设置里关掉了第三方分发（那家的规矩，
        // 见 ModpackImport 里那条许可闸）。那时候地址是空串，而这**不是**"这一版还没传
        // 文件" —— 得给用户一句说得通的话，否则他会一遍遍重试（见 unavailableNote）。
        // 实测：那条 CDN 地址其实点得开（`edge.forgecdn.net/files/<id/1000>/<id%1000>/<name>`），
        // 但接口给 null 就是在说"不许你代下"，所以这里一个字都不拼。
        file.url = object.value(QStringLiteral("downloadUrl")).toString();
        if (file.url.isEmpty()) {
            file.unavailableNote =
                QStringLiteral("作者关掉了这个文件的第三方分发，启动器不能替你下载。\n\n"
                               "可以在浏览器里打开这个项目，从它的文件页手动下载。");
        }
        entry.files.append(file);

        // 前置模组那一栏这一轮不用（`requiredDependenciesOf` 现在没有调用方），但**顺手读出来**
        // 比留着不读强：那边的 `relationType` 是个数字（3 = required、2 = optional……），
        // 和 Modrinth 的字符串不是一套，将来接的时候这里就是唯一需要翻译的地方。
        const QJsonArray dependencies = object.value(QStringLiteral("dependencies")).toArray();
        for (const QJsonValue &dependency : dependencies) {
            const QJsonObject dependencyObject = dependency.toObject();
            Dependency entryDependency;
            entryDependency.projectId = QString::number(
                static_cast<qint64>(dependencyObject.value(QStringLiteral("modId")).toDouble()));
            if (dependencyObject.value(QStringLiteral("relationType")).toInt() == 3) {
                entryDependency.type = QStringLiteral("required");
            }
            entry.dependencies.append(entryDependency);
        }

        out.append(entry);
    }

    // 顺序**原样**：那一家回的也是新的在前（实测 JEI 那条 19.57.0.449 → .448）。
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
