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
/// 这里刻意**不写域名**：默认走哪一台（不要 key 的社区镜像、还是官方 + key）、用户在设置里
/// 填没填 key，全是 `ModpackImport::curseForgeApiBase()` 一处说了算（见那个函数的注释）。
/// 把 `apiBase` 当 override 传进去，三种情形就都落在那一个函数里了：
/// 传进来的非空 → 就用它（验证工程指本机假服务器）；空 → 没填 key 的那一套（社区镜像）。
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

/// 版本号里的一段：**前导数字** + 后面剩下那截。`1.20.5-pre1` 的第二段 → (5, "-pre1")；
/// `Forge` / `pre1` 这种一位数字都不带的 → (没有数字, 原文)。
///
/// NOTE: 拆出"前导数字"而不是"整段是不是数"，正是为了 `5-pre1` 和 `6` 能按 **5 < 6** 比
///       （`1.20.5-pre1` 该排在 `1.20.6` 前面）。整段判类型的写法在这里会退回字符串比，
///       于是 `5-pre1` 大于 `6` —— 那是另一套次序（见下面那段）。
struct VersionSegment {
    bool numbered = false;  ///< 有没有前导数字
    qint64 number = 0;      ///< 那个数（没有前导数字时无意义）
    QString rest;           ///< 前导数字之后剩下的那截（没有前导数字时是原文）
};

/// 前导数字最多读这么多个字符。更长的（一份 20 位的"版本号"）**当它没有前导数字**：
/// 折成 0 会让两个不同的巨数比成相等，而"按剩下的那截比"至少还是个全序。
constexpr int kMaxVersionDigits = 18;

VersionSegment splitVersionSegment(const QString &text) {
    int digits = 0;
    while (digits < text.size()) {
        const QChar ch = text.at(digits);
        if (ch < QLatin1Char('0') || ch > QLatin1Char('9')) {
            break;
        }
        ++digits;
    }
    VersionSegment out;
    out.rest = text;
    if (digits > 0 && digits <= kMaxVersionDigits) {
        out.numbered = true;
        out.number = text.left(digits).toLongLong();
        out.rest = text.mid(digits);
    }
    return out;
}

/// 两段的次序：**带前导数字的在前**，然后比那个数，最后比剩下的那截。
///
/// 三个判据各自都是全序，按这个优先级拼起来就是全序 —— 这一条是硬要求，见下面那段。
int compareVersionSegment(const VersionSegment &a, const VersionSegment &b) {
    if (a.numbered != b.numbered) {
        return a.numbered ? -1 : 1;
    }
    if (a.numbered && a.number != b.number) {
        return a.number < b.number ? -1 : 1;
    }
    const int compared = QString::compare(a.rest, b.rest);
    if (compared != 0) {
        return compared < 0 ? -1 : 1;
    }
    return 0;
}

/// 「1.9 比 1.10 旧」那件事：按 `.` 切成段比。**只给 gameVersionsOldestFirst 用。**
///
/// 前面几段一样时**段数少的在前**（1.21 < 1.21.1）。段与段之间走
/// compareVersionSegment()：带前导数字的按数比（字符串比会把 1.10 判成小于 1.9），
/// 5-pre1 这种"数 + 后缀"先比数、再比后缀。
///
/// ★ 2026-09-26 修：这个函数以前是**一段一段"要么都按数比、要么都按字符串比"**的写法 ——
///   而两段**类型不同的那一次**落到字符串比，字符串比的次序和数比的次序不是同一个
///   （`9` 用数比小于 `10`、用字符串比大于 `10`）。三段之间于是能比出**圈**来：
///     1.9 < 1.10（数比）  ·  1.10 < 1.10-pre1（字符串比，`10` 是 `10-pre1` 的前缀）
///     ·  1.10-pre1 < 1.9（字符串比，`1` < `9`）—— 三条同时成立。
///   而它喂的正是 std::stable_sort（gameVersionsOldestFirst）：**非传递的比较器进任何
///   标准排序都是未定义行为**，introsort 会按"这个圈"去分区，结果是次序随机（同一份入参
///   换个排列能排出不同答案）、真机上还能读到数组外面去。
///   新的写法**不靠字面值决定用哪把尺子**（先看"有没有前导数字"，再看数，再看后缀），
///   圈就没了；真实数据上的次序和改之前一致（`1.20.5-pre1` < `1.20.6`、`1.21` < `1.21-pre1`、
///   `Forge` 排在数字版本后面，都是原来那个答案）。
///
/// NOTE: 已知的、和改之前一致的近似：**预发布被排在它自己的正式版后面**（1.21 < 1.21-pre1，
///       语义上该反过来）。这是原来就有的行为，这次只动"圈"，没动它。
int compareVersionText(const QString &left, const QString &right) {
    const QStringList a = left.split(QLatin1Char('.'));
    const QStringList b = right.split(QLatin1Char('.'));
    const int shared = qMin(a.size(), b.size());
    for (int i = 0; i < shared; ++i) {
        const int compared = compareVersionSegment(splitVersionSegment(a.at(i)),
                                                  splitVersionSegment(b.at(i)));
        if (compared != 0) {
            return compared;
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

QString modrinthApiRoot() { return apiRoot(); }

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
        // ★ 名称那一段叫 **`searchFilter`**，不是 `search`。2026-09-26 实测：发 `search=jei`
        //   这一家**一个字都不筛**，安安静静地把这一类**最热门的那一批**回给你（`totalCount`
        //   是 10000 = 整类的数，而不是 "jei" 那 377 条）—— 界面上看起来"搜出来了"，
        //   其实搜什么都是同一批。**它不报错**，所以这个错只能靠"地址里那一段叫什么"来钉住
        //   （见 build/verify 里 Z7e 那两条）。
        //   （比它更早的那句"实测 Popularity 会把搜索词无视掉"是同一个错的另一副面孔：
        //   词压根没送到，换哪个排序都一样。更正过之后 sortField=6 只是为了"按总下载量"。）
        params.append(QStringLiteral("searchFilter=") + enc(query));
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
        // 排序：按**总下载量**倒序（界面那一页默认搜的就是「最热门」，两处口径要一致）。
        // `sortOrder=desc` 不能省 —— 这一家的默认方向是 asc，那样头一条会变成"这一类里
        // 最没人用的"。
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

    // ★ 根地址那一格 `apiBase` **两个来源都要认**（2026-09-28 修）：从前这里写死 `apiRoot()`，
    //   于是"把接口指到本机假服务器"那个口子（`Launcher/ModrinthApiBase`）在下载页这条路上
    //   **一声不响地失效** —— CurseForge 那条路认它、Modrinth 这条不认，而症状是"验证工程
    //   悄悄打到了真接口上"（T24 那一节一直以为自己全程离线）。
    //   空 = 没人在乎，照旧走官方那一份。
    return (apiBase.isEmpty() ? apiRoot() : apiBase) + QStringLiteral("/search?") +
           params.join(QLatin1Char('&'));
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
        // 最后一次更新（详情页头卡上那句「5 天前」）。两家名字不同，各自那一栏见
        // parseCurseForgeSearch。读不到就是空串（界面那一截不显示）。
        hit.updatedIso = object.value(QStringLiteral("date_modified")).toString();
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
        // 不是一份搜索结果。出错时那一家回的是 `{"error":"…"}`，而**社区镜像**在超时 /
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
        // ★ 2026-09-27：**必须先判空**。`QJsonArray::first()` 在空数组上是越界读（Qt 里带
        //   Q_ASSERT，release 下是未定义行为）—— 而空数组在这儿是**很容易到**的：
        //   `toArray()` 对"没这个键 / 值是 null / 值是字符串"一律回空数组，所以镜像那边只要
        //   漏了 `authors`（或者哪一天真给了一条没有作者的记录），搜到这一条就当场读到一个
        //   垃圾值，轻则那行小字空着，重则整个搜索结果页画不出来。
        //   这一段里别的字段（id/slug/summary/categories/…）全都写了缺省兜底，只有这一处
        //   原来没有 —— 现在补上：取不到就是空作者，那行小字自己会少写一段。
        const QJsonArray authors = object.value(QStringLiteral("authors")).toArray();
        if (!authors.isEmpty()) {
            hit.author = authors.first().toObject().value(QStringLiteral("name")).toString();
        }
        hit.downloads = static_cast<qint64>(object.value(QStringLiteral("downloadCount")).toDouble());
        // 最后一次更新。**名字和 Modrinth 不一样**（这边是 dateModified，那边是
        // date_modified）—— 两处各读各的，读到同一栏里去。
        hit.updatedIso = object.value(QStringLiteral("dateModified")).toString();
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

namespace {

/// 老式周号快照：两位年 + 'w' + 两位周 + 一个小写字母（24w14a / 13w41b）。
/// **只认这一个长度**：`20w14infinite` 那种愚人节版是另一回事（见头文件里那段"不猜"）。
bool isWeeklySnapshotId(const QString &lower) {
    if (lower.size() != 6 || lower.at(2) != QLatin1Char('w')) {
        return false;
    }
    const bool digits = lower.at(0).isDigit() && lower.at(1).isDigit() &&
                        lower.at(3).isDigit() && lower.at(4).isDigit();
    const QChar suffix = lower.at(5);
    return digits && suffix >= QLatin1Char('a') && suffix <= QLatin1Char('z');
}

/// `1.21.4` / `26.2` 这种"纯数字加点"的前缀才认。
///
/// NOTE: **不认的就不折**。愚人节版 `1.RV-Pre1` 里那一段是 `1.RV`，折出来是一个谁也不
///       是的大版本 —— 摆在"游戏版本"那一排里，用户点进去什么都搜不到，而且看不出哪里
///       错了。`20w14infinite` 那一路用的是同一条规矩（见头文件那段"不猜"）。
bool isPlainVersionStem(const QString &text) {
    if (text.isEmpty() || !text.at(0).isDigit()) {
        return false;
    }
    bool lastWasDot = false;
    for (const QChar c : text) {
        if (c.isDigit()) {
            lastWasDot = false;
            continue;
        }
        if (c == QLatin1Char('.') && !lastWasDot) {
            lastWasDot = true;
            continue;
        }
        return false;
    }
    return !lastWasDot;
}

/// 快照号长什么样。两件事一起算，是因为"折出来的那一半"正是这两个公开函数唯一的区别。
struct SnapshotShape {
    bool snapshot = false;  ///< 这是不是一个快照号
    QString stem;           ///< 它属于哪个大版本（空串 = 折不出来，比如 24w14a）
};

/**
 * @brief 认四套写法，都是 Mojang 真用过的。
 *
 * ① 新式快照 `26.2-snapshot-1` / `26.1-snapshot-11` —— 2026-09-27 之前**一条都不认**：
 *    它们既没有 `-pre`/`-rc`、长度也不是 6，于是不勾「显示快照版」时照样摆在那一排
 *    chip 上，下载页那棵树的第二层也一家伙多出十来行（26.3-snapshot-1…10 各占一行）。
 * ② 预览与候选 `1.21.4-pre3` / `26.2-rc1`（新老两套命名都长这样）。
 * ③ 空格式的预览版 `1.14 Pre-Release 3`、`1.14.2 Pre-Release 4` —— 老 Mojang 的写法：
 *    分隔符是**空格**、词也写全了。同样是以前一条都不认。
 * ④ 老式周号 `24w14a`：认得出是快照，但读不出属于哪个大版本（`stem` 留空 = 折不了）。
 *
 * 尾巴必须是**末尾那一段**（`1.21.4-pre3`，不是 `1.21.4-pre3-1`），前缀必须是纯数字加点
 * （见 isPlainVersionStem）。这两条合起来挡掉的就是"把看不懂的号悄悄折成别的号"。
 */
SnapshotShape snapshotShapeOf(const QString &version) {
    SnapshotShape shape;
    const QString text = version.trimmed();
    if (text.isEmpty()) {
        return shape;
    }
    const QString lower = text.toLower();
    if (isWeeklySnapshotId(lower)) {
        shape.snapshot = true;
        return shape;
    }
    // **长关键词排前面**：`pre-release` 要是排在 `pre` 后面，`1.14 pre-release 3` 会先被
    // `pre` 那一支认下来，尾巴变成 `-release 3`（不是纯数字）⇒ 整条认不出来。
    static const QStringList keywords{QStringLiteral("snapshot"), QStringLiteral("pre-release"),
                                      QStringLiteral("pre"), QStringLiteral("rc")};
    for (const QString &keyword : keywords) {
        // 从**最后一次出现**取：`26.2-snapshot-1` 里 `snapshot` 只有一处，这个写法只是
        // 免得将来有个 `snapshot` 出现在大版本号里（不存在，但取最后一次总是对的）。
        const int at = lower.lastIndexOf(keyword);
        if (at <= 0) {
            continue;  // 没有这个词，或者它就在开头（前面没有"大版本"那一段）
        }
        const QString head = lower.left(at);
        const QChar separator = head.at(head.size() - 1);
        if (separator != QLatin1Char('-') && separator != QLatin1Char(' ')) {
            continue;  // `1.21.4pre3` 这种连在一块儿的不认（真没这么写的）
        }
        QString suffix = lower.mid(at + keyword.size());
        if (suffix.startsWith(QLatin1Char('-')) || suffix.startsWith(QLatin1Char(' '))) {
            suffix = suffix.mid(1);  // `-snapshot-1` / ` Pre-Release 3` 的第二个分隔符
        }
        if (suffix.isEmpty()) {
            continue;
        }
        bool allDigits = true;
        for (const QChar c : suffix) {
            allDigits = allDigits && c.isDigit();
        }
        if (!allDigits) {
            continue;
        }
        const QString stem = head.left(head.size() - 1);
        if (!isPlainVersionStem(stem)) {
            continue;  // 折出来不是个版本号（`1.RV-Pre1`）⇒ 当它不是快照
        }
        shape.snapshot = true;
        shape.stem = stem;
        return shape;
    }
    return shape;
}

}  // namespace

bool isSnapshotGameVersion(const QString &version) {
    return snapshotShapeOf(version).snapshot;
}

QString majorGameVersionOf(const QString &version) {
    const SnapshotShape shape = snapshotShapeOf(version);
    if (!shape.snapshot || shape.stem.isEmpty()) {
        // 不是快照：原样还回去（调用方因此不必先判一次"这是不是快照"）。
        // 是快照但折不出来（`24w14a`，前面那个版本号谁都猜不出来）：也原样还回去 ——
        // 调用方看它"还是快照"就知道这一条折不了（见 gameVersionChoices 里那段）。
        return version;
    }
    return shape.stem;
}

QStringList gameVersionChoices(const QStringList &declaredGameVersions, bool showSnapshots) {
    QStringList out;
    out.reserve(declaredGameVersions.size());
    // 倒着抄一遍（接口给的是旧的在前）。**不是排序** —— 见头文件那段，这两种顺序都是
    // 接口自己定的，我们只是把要显示的那个方向挑出来。
    for (int i = declaredGameVersions.size() - 1; i >= 0; --i) {
        const QString declared = declaredGameVersions.at(i);
        if (showSnapshots) {
            out.append(declared);
            continue;
        }
        // 不显示快照版：折到大版本上。
        const QString folded = majorGameVersionOf(declared);
        // 折完**还是快照** = 这一条折不动（`24w14a` 这种读不出属于哪个大版本）⇒ 不摆它。
        // 勾选框叫「显示快照版」，不勾的时候就不该有快照版露在外面（它们照样在「全部」里
        // 看得到）。**判据就是这个**（Z8h 钉着）：写成 `folded != declared && …` 是错的 ——
        // 折不动的那一条恰恰是"折完跟没折一样"，那个写法会把它原样摆出来。
        if (isSnapshotGameVersion(folded)) {
            continue;
        }
        // 同一个大版本下声明过几个快照（26.2-rc1、26.2-rc2）时只留一颗。**去重在这里做**：
        // 上面是倒着走的（新的在前），所以留下的是先遇到的那一颗，而几颗折出来的字是同一个。
        if (!out.contains(folded)) {
            out.append(folded);
        }
    }
    return out;
}

QString gameVersionAfterToggle(const QStringList &declaredGameVersions, bool showSnapshots,
                               const QString &current) {
    // 「全部」永远是「全部」：两颗表里都有它，没有什么可换算的。
    if (current.isEmpty()) {
        return QString();
    }
    const QStringList choices = gameVersionChoices(declaredGameVersions, showSnapshots);
    // ① 这一颗在另一档里还在（正式版两边都有）—— 绝大多数情况到这儿就回去了。
    if (choices.contains(current)) {
        return current;
    }
    if (!showSnapshots) {
        // ② 勾 → 不勾：把现在这一颗折一下再找（26.2-rc1 → 26.2）。
        const QString folded = majorGameVersionOf(current);
        return choices.contains(folded) ? folded : QString();
    }
    // ③ 不勾 → 勾：这条线上**没有正式版**（只声明过 rc / pre）。挑最新的那一条快照，
    //    让用户留在同一条线上。声明表是**旧的在前**，所以从尾巴往前走，遇到的第一条
    //    就是最新的那一条。
    for (int i = declaredGameVersions.size() - 1; i >= 0; --i) {
        const QString declared = declaredGameVersions.at(i);
        if (isSnapshotGameVersion(declared) && majorGameVersionOf(declared) == current) {
            return declared;
        }
    }
    // 都对不上（调用方给了一个表里根本没有的 key）：回「全部」。这儿**不猜** —— 和
    // applyChipChecked 里那条"挑不中就按全部"是同一条规矩。
    return QString();
}

QString iconUrlForProject(const QString &projectId) {
    if (projectId.isEmpty()) {
        return QString();
    }
    // 为什么要有它：接口给的那个 icon_url 常常是一条小 7 倍的 webp，而**webp 不是每台
    // 机器都解得动**（见 ImageDecode::webpAvailable）。解不动、或者接口干脆没给（
    // hit.iconUrl 为空）、或者给了条别的域名的，都退到这一条上来。
    //
    // ★ 2026-09-27 实测更正：这条 `<id>/icon.png` **只是老项目的别名**，不是每个项目
    //   都有的那一条。用户真实 mods/ 里 20 个模组量下来 11 个回 404（FerriteCore /
    //   Fabric Language Kotlin / Carpet / Fabric Permissions API / ArmorQuickSwap …），
    //   只有 9 个回 200（恰好是"图标当年就叫 icon.png"的那几个：Sodium / Fabric API /
    //   Jade / cloth-config …）。修完在另一台盘上又量了一遍（`mw_check icon-lookup-live`
    //   打一个真实例的 mods/，51 个模组）：**33 个回 404、18 个回 200** —— 比例和上一批
    //   差不多，所以这不是"某一个实例特殊"。所以它**只配当最后一档**，谁要是拿它当
    //   "自己拼得出来"的正常地址，那些项目就永远是那张占位拼图（那一发 404 被当成
    //   "没传过图标"，屏幕上一个异常都没有）。真正那条只有问接口：projectsRequestUrl()。
    return cdnRoot() + QStringLiteral("/data/") + enc(projectId) + QStringLiteral("/icon.png");
}

QString projectsRequestUrl(const QStringList &projectIds, const QString &apiBase) {
    if (projectIds.isEmpty()) {
        return QString();
    }
    // ids 那一段照旧走 jsonArrayParam（百分号编码 + 用 QJsonDocument 拼，见那一处）：
    // 项目号都是 base62 / 纯数字，本来就没有要转义的字符，但**形状**必须是接口认的那个。
    return (apiBase.isEmpty() ? apiRoot() : apiBase) + QStringLiteral("/projects?ids=") +
           jsonArrayParam(projectIds);
}

QList<Hit> parseProjects(const QByteArray &json) {
    QList<Hit> hits;
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    // ★ 回复是一个**数组**（`GET /projects?ids=…` 是这个形状），不是搜索那种 `{hits:[…]}`。
    //   按对象读会一条都读不出来 —— 而"没有图标"和"读不懂"在屏幕上是一个样子。
    if (error.error != QJsonParseError::NoError || !document.isArray()) {
        return hits;
    }
    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        const QString id = object.value(QStringLiteral("id")).toString();
        if (id.isEmpty()) {
            // 没有项目号就没有身份 —— 这一条进了界面也找不回任何一行（见 .hpp 那条 NOTE）。
            continue;
        }
        Hit hit;
        hit.source = Source::Modrinth;
        hit.projectId = id;
        hit.slug = object.value(QStringLiteral("slug")).toString();
        hit.title = object.value(QStringLiteral("title")).toString();
        hit.description = object.value(QStringLiteral("description")).toString();
        // 下载量可能大过 int（9 位数的有的是），所以走 double 再截断 —— 和 parseSearch 一样。
        hit.downloads = static_cast<qint64>(object.value(QStringLiteral("downloads")).toDouble());
        // ★ 更新时间这一栏在这一份响应里叫 `updated`（搜索那份叫 `date_modified`，
        //   CurseForge 那边叫 `dateModified`）—— 三个名字，读到**同一栏**里去。
        hit.updatedIso = object.value(QStringLiteral("updated")).toString();
        hit.categories = stringList(object.value(QStringLiteral("categories")));
        hit.gameVersions = stringList(object.value(QStringLiteral("game_versions")));
        // 加载器：这一份自己有一栏（`loaders`），不像搜索那边要按 categories 现推
        // （见 Hit::explicitLoaders 那段 —— 非空就只认它）。
        hit.explicitLoaders = stringList(object.value(QStringLiteral("loaders")));
        // 图标地址是外部输入，过一遍域名闸（见 isUsableIconUrl）；过不去就当这个项目
        // 没给图标地址 —— 和搜索那边读 icon_url 是同一个规矩。
        const QString icon = object.value(QStringLiteral("icon_url")).toString();
        hit.iconUrl = isUsableIconUrl(icon) ? icon : QString();
        hits.append(hit);
    }
    return hits;
}

QHash<QString, QString> parseProjectIconUrls(const QByteArray &json) {
    // 同一份响应（`GET /projects?ids=…`）只读第一格。**空着也插进去**（"这个项目没给
    // 图标地址"和"回复里根本没有这个项目"在调用方那边是两件事，见 .hpp 那条 NOTE）。
    QHash<QString, QString> icons;
    for (const Hit &hit : parseProjects(json)) {
        icons.insert(hit.projectId, hit.iconUrl);
    }
    return icons;
}

QList<Hit> parseCurseForgeMods(const QByteArray &json) {
    // 那一家的"按 id 问"和"搜"回的是**同一个对象形状**（`data[]`），所以不抄第二份解析
    // （见 .hpp 那条 NOTE）：`parseCurseForgeSearch()` 里那些字段名、空数组兜底、图标那两条
    // 地址的域名闸，全都在它那儿。
    return parseCurseForgeSearch(json).hits;
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

// ---------- 资源详情页要用的那几件 ----------

/// 加载器在界面上排列的固定次序。**和 loaderOptions() 那份表同一个次序**
///（fabric / forge / neoforge / quilt）—— 两处不一样的话，同一组版本在不同地方会换个先后。
/// 认不出来的一律排在这些后面。
int loaderOrderIndex(const QString &loader) {
    static const QStringList order = {QStringLiteral("fabric"), QStringLiteral("forge"),
                                      QStringLiteral("neoforge"), QStringLiteral("quilt")};
    const int index = order.indexOf(loader.toLower());
    return index < 0 ? order.size() : index;
}

/// 一条版本声明的**最新**那个游戏版本（组头里要用的那个）。一个都没有回空串。
///
/// 比法走 compareVersionText（`1.9` < `1.10` 那一套，也是"旧的在前"那个方向），
/// 取最大的那个。**不按接口给的顺序取第一个**：CF 那条 `gameVersions` 的顺序是随机的
///（见文件里那段），照抄第一个会把一条 1.21 的版本分到 1.7.10 那一组去。
QString newestGameVersionOf(const VersionEntry &entry) {
    QString best;
    for (const QString &candidate : entry.gameVersions) {
        if (best.isEmpty() || compareVersionText(best, candidate) < 0) {
            best = candidate;
        }
    }
    return best;
}

/// 一条版本的**主加载器**（组头里那一个）：按固定次序取第一个认得的。
/// 一个都不认得（`optifine` 这种、或者空）就取它自己第一个原样小写，没有就回空串
///（= 通用）。
QString primaryLoaderOf(const VersionEntry &entry) {
    for (const QString &known : {QStringLiteral("fabric"), QStringLiteral("forge"),
                                 QStringLiteral("neoforge"), QStringLiteral("quilt")}) {
        for (const QString &declared : entry.loaders) {
            if (declared.toLower() == known) {
                return known;
            }
        }
    }
    return entry.loaders.isEmpty() ? QString() : entry.loaders.first().toLower();
}

QString relativeTimeText(const QString &isoTime) {
    return relativeTimeText(isoTime, QDateTime::currentDateTime());
}

QString relativeTimeText(const QString &isoTime, const QDateTime &now) {
    if (isoTime.isEmpty()) {
        return QString();
    }
    // 两条格式都试：接口给的那一串**带毫秒**（`2024-06-14T12:34:56.789Z`），而 ISO 那一档
    // 读毫秒是分版本的（Qt 6 的 Qt::ISODate 认得，早先的版本要 Qt::ISODateWithMs）。
    // 两条都不认就把这一串当"读不懂"—— 那是空串，界面上不显示这一截。
    QDateTime then = QDateTime::fromString(isoTime, Qt::ISODate);
    if (!then.isValid()) {
        then = QDateTime::fromString(isoTime, Qt::ISODateWithMs);
    }
    if (!then.isValid()) {
        return QString();
    }
    const qint64 seconds = then.secsTo(now);
    if (seconds < 0) {
        // 将来时间：这一台机器的钟或者接口的时间戳有一个是错的。**空串**，不说「-3 天前」。
        return QString();
    }
    if (seconds < 60) {
        return QStringLiteral("刚刚");
    }
    if (seconds < 3600) {
        return QStringLiteral("%1 分钟前").arg(seconds / 60);
    }
    if (seconds < 86400) {
        return QStringLiteral("%1 小时前").arg(seconds / 3600);
    }
    const qint64 days = seconds / 86400;
    if (days < 30) {
        return QStringLiteral("%1 天前").arg(days);
    }
    if (days < 365) {
        return QStringLiteral("%1 个月前").arg(days / 30);
    }
    return QStringLiteral("%1 年前").arg(days / 365);
}

QString releaseChannelNote(const QString &versionType) {
    if (versionType == QStringLiteral("beta")) {
        return QStringLiteral("测试版");
    }
    if (versionType == QStringLiteral("alpha")) {
        return QStringLiteral("早期测试版");
    }
    // release 和"认不出来"都回空串：正式版是绝大多数，那三个字不是信息；认不出来的
    // 更不该编一个（见头文件）。
    return QString();
}

int releaseChannelRank(const QString &versionType) {
    if (versionType == QStringLiteral("release")) {
        return 0;
    }
    if (versionType == QStringLiteral("beta")) {
        return 1;
    }
    if (versionType == QStringLiteral("alpha")) {
        return 2;
    }
    // 认不出来回 -1，**不是** 0：把一条不知道是什么渠道的版本画成绿色的"正式版"，是把
    // "不知道"说成了"很稳"（详情页那个方块认不出这一档就不画字母，见头文件）。
    return -1;
}

QString loaderDisplayName(const QString &loader) {
    const QString lowered = loader.toLower();
    if (lowered == QStringLiteral("fabric")) {
        return QStringLiteral("Fabric");
    }
    if (lowered == QStringLiteral("forge")) {
        return QStringLiteral("Forge");
    }
    if (lowered == QStringLiteral("neoforge")) {
        return QStringLiteral("NeoForge");
    }
    if (lowered == QStringLiteral("quilt")) {
        return QStringLiteral("Quilt");
    }
    return loader;  // 认不出来原样还回去：显示 optifine 比显示空串强
}

QString versionRowSubText(const VersionEntry &version) {
    QStringList bits;
    // 加载器。一条版本可能同时支持好几个（Modrinth 那边常见 fabric + quilt），都列出来 ——
    // 组头只放得下主加载器一个（见 primaryLoaderOf），其余在这儿。
    QStringList loaders;
    for (const QString &loader : version.loaders) {
        loaders.append(loaderDisplayName(loader));
    }
    if (!loaders.isEmpty()) {
        bits.append(loaders.join(QStringLiteral(" / ")));
    }
    // 文件名：落盘的就是它，用户拿着它能在网页上认出同一个文件。
    const FileEntry *file = version.primaryFile();
    if (file != nullptr && !file->filename.isEmpty()) {
        bits.append(file->filename);
    }
    // 下载量：0 = 接口没给这一栏（见 VersionEntry::downloads），那一截省掉。
    if (version.downloads > 0) {
        bits.append(QStringLiteral("下载 %1次").arg(formatDownloads(version.downloads)));
    }
    const QString updated = relativeTimeText(version.datePublished);
    if (!updated.isEmpty()) {
        bits.append(QStringLiteral("更新于 %1").arg(updated));
    }
    const QString channel = releaseChannelNote(version.versionType);
    if (!channel.isEmpty()) {
        bits.append(channel);
    }
    return bits.join(QStringLiteral(", "));
}

QList<VersionEntry> versionsForGameVersion(const QList<VersionEntry> &versions,
                                           const QString &gameVersion, bool showSnapshots) {
    // 空串 = 不筛（「全部」那一颗）。**原样还回去**，不是"筛一个空串"（那会一条都不剩）。
    if (gameVersion.isEmpty()) {
        return versions;
    }
    QList<VersionEntry> out;
    for (const VersionEntry &version : versions) {
        // ★ 看的是 `gameVersions`（这一版**声明过**的每一个游戏版本），**不是**它落在哪个
        //   组里（`groupVersions` 那个归属）。一条同时支持 1.20.1 / 1.20.2 的版本落在
        //   1.20.2 那一组，但选了 1.20.1 那一格时它照样该在 —— 这两件事在详情页里是分开
        //   的两步（先按 chip 筛、再分组），别合并成一步。
        if (version.gameVersions.contains(gameVersion)) {
            out.append(version);
            continue;
        }
        // 不勾「显示快照版」时，这一排上的 `26.2` 代表的是"26.2 那条线"，声明的快照也要算
        // 进来（勾上时 `26.2` 就只是 `26.2`，快照各去点自己那一颗 —— 见头文件那段）。
        // 折出来还是快照的那些（24w14a）在这一支里自然落空：它们没被折过。
        if (!showSnapshots) {
            for (const QString &declared : version.gameVersions) {
                if (majorGameVersionOf(declared) == gameVersion) {
                    out.append(version);
                    break;
                }
            }
        }
    }
    return out;
}

QString hitMetaLine(const Hit &hit) {
    QStringList bits;
    // 加载器：这一条**支持哪几个**（不是某一个版本的）。和版本那一行同一个显示名函数 ——
    // `fabric` 在界面上永远是 `Fabric`。
    QStringList loaders;
    const QStringList raw = hit.loaders();
    for (const QString &loader : raw) {
        loaders.append(loaderDisplayName(loader));
    }
    if (!loaders.isEmpty()) {
        bits.append(loaders.join(QStringLiteral(" / ")));
    }
    // 下载量：0 = 接口没给这一栏（见 Hit::downloads），那一截省掉 —— 不写"下载 0 次"。
    if (hit.downloads > 0) {
        bits.append(QStringLiteral("下载 %1次").arg(formatDownloads(hit.downloads)));
    }
    const QString updated = relativeTimeText(hit.updatedIso);
    if (!updated.isEmpty()) {
        bits.append(QStringLiteral("更新于 %1").arg(updated));
    }
    // 来源总在：这一行是"这一条是从哪一家来的"，而图标和版本列表都跟着它走。
    bits.append(sourceLabel(hit.source));
    return bits.join(QStringLiteral(" · "));
}

QString VersionGroup::title() const {
    if (loader.isEmpty() && gameVersion.isEmpty()) {
        return QStringLiteral("其它版本");
    }
    if (loader.isEmpty()) {
        return gameVersion;
    }
    const QString name = loaderDisplayName(loader);
    return gameVersion.isEmpty() ? name : name + QLatin1Char(' ') + gameVersion;
}

QList<VersionGroup> groupVersions(const QList<VersionEntry> &versions, bool showSnapshots,
                                  const QString &preferredLoader) {
    QList<VersionGroup> groups;
    for (const VersionEntry &version : versions) {
        // 不勾「显示快照版」时组头也归到大版本（一条只声明到 26.2-rc1 的版本落在
        // 「Fabric 26.2」那一组）—— 和那一排 chip 折的是同一个东西，见头文件那段。
        // **先取最新再折**：折完再比大小的话，"最新"就变成按折过的那串排的了。
        const QString declared = newestGameVersionOf(version);
        const QString gameVersion = showSnapshots ? declared : majorGameVersionOf(declared);
        const QString loader = primaryLoaderOf(version);
        int found = -1;
        for (int i = 0; i < groups.size(); ++i) {
            if (groups.at(i).gameVersion == gameVersion && groups.at(i).loader == loader) {
                found = i;
                break;
            }
        }
        if (found < 0) {
            VersionGroup group;
            group.gameVersion = gameVersion;
            group.loader = loader;
            groups.append(group);
            found = groups.size() - 1;
        }
        groups[found].versions.append(version);
    }

    // 组的次序：游戏版本**新的在前**（compareVersionText 是"旧的在前"那把尺子，反着用），
    // 同一个游戏版本里按加载器的固定次序，空游戏版本（信息就是缺的）和最不认识的加载器
    // 都排到最后。stable_sort：两条完全同序的组保持它们进列表的先后。
    //
    // ★ `preferredLoader`（2026-09-28）：同一个游戏版本里**点名的那一个加载器排最前**。
    //   放在这里、而不是在调用方排一遍：组序只有这一处出处（见 .hpp 那段)，劈成两处
    //   之后"两组各说各的"迟早错开。次序的**第一关键字仍然是游戏版本**——见 .hpp 那条
    //   NOTE，不然筛了 1.21.1 之后一个老 Fabric 组会跑到 26.2 的 NeoForge 组前面。
    const QString preferred = preferredLoader.trimmed().toLower();
    const auto rankOf = [&preferred](const QString &loader) {
        // 点名的那个排在最前（-1 比谁都小），其余照旧走那把固定的尺子。
        if (!preferred.isEmpty() && loader == preferred) {
            return -1;
        }
        return loaderOrderIndex(loader);
    };
    std::stable_sort(groups.begin(), groups.end(),
                     [&rankOf](const VersionGroup &a, const VersionGroup &b) {
                         if (a.gameVersion != b.gameVersion) {
                             if (a.gameVersion.isEmpty()) {
                                 return false;
                             }
                             if (b.gameVersion.isEmpty()) {
                                 return true;
                             }
                             return compareVersionText(a.gameVersion, b.gameVersion) > 0;
                         }
                         return rankOf(a.loader) < rankOf(b.loader);
                     });
    return groups;
}

QString projectPageUrl(Source source, Kind kind, const QString &slug) {
    if (slug.isEmpty()) {
        return QString();
    }
    if (source == Source::CurseForge) {
        // 类名那一段是**那一家自己网页里的目录名**，不是接口的类目号、也不是我们那份
        // kindKey —— 三套东西，别互相套用。
        QString section;
        switch (kind) {
        case Kind::Mod:
            section = QStringLiteral("mc-mods");
            break;
        case Kind::Modpack:
            section = QStringLiteral("modpacks");
            break;
        case Kind::ResourcePack:
            section = QStringLiteral("texture-packs");
            break;
        case Kind::Shader:
            section = QStringLiteral("shaders");
            break;
        case Kind::DataPack:
            section = QStringLiteral("data-packs");
            break;
        }
        return QStringLiteral("https://www.curseforge.com/minecraft/") + section +
               QLatin1Char('/') + slug;
    }
    QString section;
    switch (kind) {
    case Kind::Mod:
        section = QStringLiteral("mod");
        break;
    case Kind::Modpack:
        section = QStringLiteral("modpack");
        break;
    case Kind::ResourcePack:
        section = QStringLiteral("resourcepack");
        break;
    case Kind::Shader:
        section = QStringLiteral("shader");
        break;
    case Kind::DataPack:
        section = QStringLiteral("datapack");
        break;
    }
    return QStringLiteral("https://modrinth.com/") + section + QLatin1Char('/') + slug;
}

QString mcmodSearchUrl(const QString &name) {
    if (name.isEmpty()) {
        return QString();
    }
    // 那一家没有"按对面的 id 直接跳"的路：它的项目号是另一套，和对面的 id 对不上。
    // 所以这一条是**拿名字去搜**（跳到结果页，不一定是那一页）—— 按钮上的话也照这个说。
    // 分隔符和参数名抄的是它自己搜索框提交的那一条。
    return QStringLiteral("https://www.mcmod.cn/s?key=") +
           QString::fromLatin1(QUrl::toPercentEncoding(name));
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

    // ★ 根地址和 searchUrl 那一条逐字同一个理由（见那儿的 ★）：`apiBase` 两个来源都认。
    QString url = (apiBase.isEmpty() ? apiRoot() : apiBase) + QStringLiteral("/project/") +
                  enc(projectId) + QStringLiteral("/version");
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

QString projectsRequestKey(const QStringList &projectIds, Source source) {
    // id 先排一遍再拼：同一批号不管调用方是按什么顺序攒起来的，都是**同一把钥匙** ——
    // 不排的话，同一批号换一个顺序就成了"另一发"，前一趟回来的结果会被当成过期的丢掉，
    // 界面上的症状是"这一批名字永远填不上"。
    QStringList sorted = projectIds;
    sorted.sort();
    // `,` 在两种 id（base62 / 纯数字）里都不会出现，当分隔符不用转义。
    return QStringLiteral("projects|") + sourceKey(source) + QLatin1Char('|') +
           sorted.join(QLatin1Char(','));
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
        // 这一版自己被下过多少次（详情页那行小字）。**读不到就是 0** —— 0 在界面上是
        // "这一截不显示"，不是"没人下过"（见 VersionEntry::downloads 那段）。
        entry.downloads = static_cast<qint64>(object.value(QStringLiteral("downloads")).toDouble());
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
        // 这一条文件被下过多少次（Modrinth 那边这一栏叫 downloads，这边叫 downloadCount）。
        entry.downloads =
            static_cast<qint64>(object.value(QStringLiteral("downloadCount")).toDouble());

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
