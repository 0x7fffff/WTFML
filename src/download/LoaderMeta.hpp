/**
 * @file LoaderMeta.hpp
 * @brief 模组加载器元数据接口的**纯函数**部分：地址怎么拼、列表回复怎么挑版本。
 * @author 0x7fffff
 * @date 2026-09-21
 * @note 这里不联网、不发信号，只做"字符串进、字符串出"。真正发请求的是
 *       VersionManager::installLoaderVersion()（它才是 QObject），这样这几条规则能在
 *       验证工程里离线钉住 —— 联网的那半边没法在沙箱里跑（要真下几个 GB 的资源）。
 *
 * 四类加载器各有一套接口形状（全部实测）：
 *
 * 【Forge / NeoForge】它们不走"拉一份 json 再下库"那条路，而是**跑它们自己的安装器 jar**
 *   （`java -jar xxx-installer.jar --installClient <游戏目录>`，见下面的注释）。这里只放
 *   "版本从哪来、挑哪一个、安装器 jar 在哪个地址、装出来的目录叫什么"这四件事。
 *
 * 【OptiFine】同样是"跑别人的安装器"，但它的安装器**不认命令行给的那个目录**（参数整个
 *   丢掉，只从 APPDATA 算），而且本身是个 GUI 程序、出错时弹框等人点 —— 所以它那条路是
 *   "换一个无界面入口 + 给子进程换 APPDATA + 对着一个临时目录装、装完把产物搬出来"
 *   （实测记录见下面《OptiFine》那一节）。
 *
 * Fabric 的接口形状（2026-09-21 实测）：
 *   GET /v2/versions/loader/<游戏版本>
 *       -> 数组，每个元素 {loader:{version,stable,build,maven}, intermediary:{...},
 *                          launcherMeta:{version,libraries,mainClass,min_java_version}}
 *          **按新到旧排序**，而且只有**最新那个**的 stable 是 true（0.19.5 是 true，
 *          0.19.4 及更早全是 false）—— 实测 253 条里 stable 恰好一条。
 *          回复约 589 KB（253 条 × 每条自带一整份 launcherMeta.libraries，占 99%）。
 *   GET /v2/versions/loader/<游戏版本>/<加载器版本>/profile/json
 *       -> 就是那份"薄 json"，原样写进 versions/<id>/<id>.json 即可（约 2.8 KB）。
 */
#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QLatin1Char>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace LoaderMeta {

/// Fabric 元数据的根地址（没有镜像源：BMCLAPI 不代理 Fabric 的 meta）。
inline QString fabricMetaBase() {
    return QStringLiteral("https://meta.fabricmc.net");
}

/// 某个游戏版本可用的加载器版本列表。
inline QString fabricLoaderListUrl(const QString &gameVersion) {
    return fabricMetaBase() + QStringLiteral("/v2/versions/loader/") + gameVersion;
}

/// 某个加载器版本的 profile json —— 内容就是落盘的那份版本 json。
inline QString fabricProfileUrl(const QString &gameVersion, const QString &loaderVersion) {
    return fabricLoaderListUrl(gameVersion) + QLatin1Char('/') + loaderVersion +
           QStringLiteral("/profile/json");
}

/**
 * @brief Fabric 装出来的版本 id。
 *
 * 协议是 `fabric-loader-<加载器版本>-<游戏版本>`，**不是我们发明**的：profile json 里的
 * `id` 字段就是这个，而它同时也是版本目录名 —— 别的启动器、加载器自己的安装器都按这个名字
 * 找目录，改一个字就等于"装好了但谁都不认识它"。
 */
inline QString fabricLoaderId(const QString &gameVersion, const QString &loaderVersion) {
    return QStringLiteral("fabric-loader-%1-%2").arg(loaderVersion, gameVersion);
}

/**
 * @brief 一个版本 id 能不能直接当目录名用（`versions/<id>/`）。
 *
 * 白名单和 LibraryRules::mavenPathForName() 那条是同一个（`[A-Za-z0-9._+-]`），理由也一样：
 * **内容来自网络、用途是拼路径**。加载器 profile 里的 `id` 是我们唯一一处"把网上的字符串
 * 直接当目录名"的地方（原版的版本 id 来自我们自己的 manifest，从来没从外面进来过），
 * 所以这里必须自己挡一道 —— `../` 那种 id 会让整个版本装到数据目录外面去。
 * 另外单独点名 `.` 和 `..`：它们能过白名单，但显然不是版本名。
 */
inline bool isSafeVersionId(const QString &id) {
    if (id.isEmpty() || id == QLatin1String(".") || id == QLatin1String("..")) {
        return false;
    }
    static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9._+-]+$"));
    return safe.match(id).hasMatch();
}

/// profile 检查没过的**哪一种**。
///
/// NOTE: 这里刻意只回一个枚举、不回已经拼好的话 —— 给用户看的那句话在
///       VersionManager 里（那里才有 tr()），两边分开才不会出现"文案在纯函数里、
///       翻译表却扫不到"这种事。验证工程也断言这个枚举（比断言一句话稳）。
enum class ProfileProblem {
    None,
    BadId,        ///< profile 里的 id 空着或者不能当目录名（见 isSafeVersionId）
    WrongParent,  ///< profile 的 inheritsFrom 不是我们要装在它上面的那个版本
};

/// `checkFabricProfile()` 的结果。
struct ProfileCheck {
    ProfileProblem problem = ProfileProblem::None;
    QString id;      ///< profile 里的版本 id（版本目录名，也是最终 versionDownloadFinished 的 id）
    QString parent;  ///< profile 里的 inheritsFrom（出问题时报给用户看）

    bool ok() const { return problem == ProfileProblem::None; }
};

/**
 * @brief 检查一份 Fabric profile 能不能照这样装下去。
 * @param profile 接口返回的 profile（原样）
 * @param gameVersion 我们打算装在这个原版版本之上
 *
 * 两条规则，都是"宁可不装"：这个 json 会**原样**落盘成 `versions/<id>/<id>.json`，
 * 而它的 id 就是目录名、它的 inheritsFrom 决定启动时去哪找主类/客户端 jar。
 *
 * 1. `id` 必须是能当目录名的（见 isSafeVersionId）—— 不然是往数据目录外面写。
 * 2. `inheritsFrom` 必须就是 `gameVersion` —— 对不上就会装出一个"目录名叫 A、
 *    内容基于 B"的版本：启动时它去找 B，而用户以为自己装的是基于 A 的。
 */
inline ProfileCheck checkFabricProfile(const QJsonObject &profile, const QString &gameVersion) {
    ProfileCheck result;
    result.id = profile.value(QStringLiteral("id")).toString();
    result.parent = profile.value(QStringLiteral("inheritsFrom")).toString();
    if (!isSafeVersionId(result.id)) {
        result.problem = ProfileProblem::BadId;
        return result;
    }
    if (result.parent != gameVersion) {
        result.problem = ProfileProblem::WrongParent;
        return result;
    }
    return result;
}

/**
 * @brief 核对外部安装器装完之后留下的那份版本 json。
 * @param versionJson `versions/<id>/<id>.json` 的内容。
 * @param gameVersion 我们打算装在这个原版版本之上。
 * @param expectedId 我们按约定算出来的版本 id（= 目录名）。
 * @return 复用 ProfileCheck 那两个字段；problem 的含义和 checkFabricProfile() 一样。
 *
 * 为什么退出码 0 还不够：安装器的退出码是它自己说了算的，而**真正被写进磁盘的那份 json
 * 才是别的启动器和我们自己的启动路径要读的东西**。这里查两条：
 *
 * 1. json 里的 `id` 必须**等于我们算出来的那个目录名**。目录名是我们拼的（`forgeLoaderId`),
 *    而 json 是安装器写的；两者对不上意味着"Forge 换了命名约定"，我们接下来要么把
 *    wtfml.meta 写到一个不存在的 id 上、要么把一个名字对不上的版本记成"装好了"。
 * 2. `inheritsFrom` 必须就是 `gameVersion`。对不上就是"装是装上了，但基于的是另一个原版"
 *    —— 启动时它去找那个原版，而用户以为自己装的是基于这个的。
 *
 * NOTE: 和 checkFabricProfile() 用同一个结构体（ProfileCheck）和同一套 problem 枚举是
 *       刻意的：两处的失败模式一模一样（ID 不可用 / 父版本不对），文案由调用方按
 *       problem 分派，多一套枚举只会多一处要同步的东西。
 */
inline ProfileCheck checkInstallerResult(const QJsonObject &versionJson, const QString &gameVersion,
                                         const QString &expectedId) {
    ProfileCheck result;
    result.id = versionJson.value(QStringLiteral("id")).toString();
    result.parent = versionJson.value(QStringLiteral("inheritsFrom")).toString();
    if (result.id != expectedId || !isSafeVersionId(result.id)) {
        result.problem = ProfileProblem::BadId;
        return result;
    }
    if (result.parent != gameVersion) {
        result.problem = ProfileProblem::WrongParent;
        return result;
    }
    return result;
}

/// 从加载器列表里挑出来的那一个。
struct LoaderPick {
    QString version;    ///< 加载器版本号；空 = 列表里没有能用的
    bool stable = false;  ///< 挑中的那个是不是被标了 stable

    bool ok() const { return !version.isEmpty(); }
};

/**
 * @brief 从 `/v2/versions/loader/<mc>` 的回复里挑一个加载器版本。
 * @param list 接口返回的数组（原样）。
 * @return 挑中的版本；列表为空或没有 usable 条目时 `ok()` 为 false。
 *
 * 规则：**优先第一个 stable 的**，没有 stable 就退回第一个带版本号的。
 *
 * 为什么是"第一个"而不是"比大小"：接口保证按新到旧排序（实测 253 条严格递减），
 * 而 stable 只有最新那个是 true。自己写一个版本号比较器（Fabric 的版本号里有
 * `0.1.0.48` 这种四段、也可能带 `+build.N`）反而更容易挑错 —— 而且"最新"本来就是
 * 这个接口的语义，不是我们要判断的事情。
 */
inline LoaderPick pickFabricLoader(const QJsonArray &list) {
    LoaderPick fallback;
    for (const QJsonValue &value : list) {
        const QJsonObject loader = value.toObject().value(QStringLiteral("loader")).toObject();
        const QString version = loader.value(QStringLiteral("version")).toString();
        if (version.isEmpty()) {
            continue;
        }
        if (loader.value(QStringLiteral("stable")).toBool()) {
            return LoaderPick{version, true};
        }
        if (fallback.version.isEmpty()) {
            fallback = LoaderPick{version, false};
        }
    }
    return fallback;
}

// ---------------------------------------------------------------------------
// Forge / NeoForge：跑它们自己的安装器 jar
// ---------------------------------------------------------------------------
//
// 【为什么这一段和上面 Fabric 那段完全不是一回事】
//
// Fabric 那边我们能自己拼出结果：一份 2.8 KB 的 profile json 落盘就是版本 json，剩下的
// 就是下 8 个 maven 库。Forge / NeoForge 不行 —— 它们装出来的东西里有**二进制补丁**
// （拿 vanilla 的 client jar 打出一份 srg 名 jar，再打出一份 client jar），那不是靠下载
// 能凑出来的。所以只能把它们的安装器请下来、**另起一个 Java 进程**跑。
//
// 【实测到的安装器行为】（2026-09-22，沙箱里真的跑过 Forge 47.4.10 和 NeoForge 21.1.215）
//
//   java -jar <安装器>.jar --installClient <游戏目录>
//
//   * 目录里**必须已经有 launcher_profiles.json**。没有的话它直接拒绝：
//     `There is no minecraft launcher profile in "...", you need to run the launcher first!`
//     + `There was an error during installation`，退出码 1 —— 一个字节的库都不会下。
//     这是它唯一的前置条件（我们拿一个只有 launcher_profiles.json 的空目录试过，它照样装成）。
//   * 成功时最后两行是 `Successfully installed client into launcher.` /
//     `You can delete this installer file now if you wish`，退出码 0。
//   * **自动的安装器是拿不到的**：真实下载页在广告后面。见下面 URL 那段的说明。
//
//   Forge 47.4.10 → `versions/1.20.1-forge-47.4.10/1.20.1-forge-47.4.10.json`
//                   （inheritsFrom = 1.20.1，mainClass = cpw.mods.bootstraplauncher
//                   .BootstrapLauncher，29 个库，**没有 downloads 字段**）
//                 外加 `versions/1.20.1/1.20.1.jar`（vanilla 的客户端 jar，sha1 与官方
//                   逐字节相同）—— **但没有 1.20.1.json**。
//   NeoForge 21.1.215 → `versions/neoforge-21.1.215/neoforge-21.1.215.json`
//                 外加 `versions/1.21.1/` 的 json **和** jar。
//   * 两个都**不下 assets**。所以"装完加载器"之后必须自己把原版那一套补全（resources
//     索引 + 5000 个对象），否则用户拿到的是一个启动就崩的版本 —— 补全复用现有的
//     downloadVersion()，不是另写一份。

/// Forge 的"推广版本"表（`<游戏版本>-recommended` / `<游戏版本>-latest` 两两成对）。
///
/// 只有 4 KB，一次把**所有**游戏版本的 Forge 推荐版本都拿回来了。
inline QString forgePromotionsUrl() {
    return QStringLiteral(
        "https://files.minecraftforge.net/net/minecraftforge/forge/promotions_slim.json");
}

/// 从 promotions_slim.json 里挑一个 Forge 版本。
/// @param promotions 接口返回的对象（原样，顶层有 `promos`）。
/// @param gameVersion 原版版本号（就是 `promos` 里键的前半段）。
/// @return 挑中的版本；这个游戏版本一个都没有时 `ok()` 为 false。
///
/// 规则：**优先 recommended，没有才用 latest**。
///
/// 为什么不是 latest：这两个键的语义是 Forge 自己给的 —— recommended 是"我们觉得这个
/// 版本可以日常用"，latest 是"这个游戏版本上最新发出来的"。实测 1.20.1 那一对是
/// 47.4.10（recommended）和 47.4.23（latest），而真机上装着的正是 47.4.10。
/// 还**必须**兜底到 latest：真表里有一大批游戏版本只有 latest 一个键（实测 1.20 只有
/// `46.0.14`、1.19.1 只有 `42.0.9`、1.21.6 只有 `56.0.9`），一个不兜底它们就都装不了。
/// NOTE: 别拿 1.7.10 当"只有 latest"的例子 —— 我一开始就是这么写的，而真表里它**两个键
///       都有**（`10.13.4.1614` 各写一遍）。看着像常识的版本号也得回真数据里核一遍。
/// NOTE: 这里不自己比版本号大小 —— 键的语义是"Forge 推荐的"，不是"数字最大的"，
///       latest 也可能比 recommended 小（真发生过，见上）。
inline LoaderPick pickForgeVersion(const QJsonObject &promotions, const QString &gameVersion) {
    const QJsonObject promos = promotions.value(QStringLiteral("promos")).toObject();
    const QString recommended =
        promos.value(gameVersion + QStringLiteral("-recommended")).toString();
    if (!recommended.isEmpty()) {
        return LoaderPick{recommended, true};
    }
    const QString latest = promos.value(gameVersion + QStringLiteral("-latest")).toString();
    if (!latest.isEmpty()) {
        return LoaderPick{latest, false};
    }
    return LoaderPick{};
}

/// Forge 安装器 jar 的地址（maven 上的坐标就是 `<游戏版本>-<Forge 版本>`）。
///
/// NOTE: 这是 maven 直链，不是下载页那个地址。安装器 jar 自己开头写着一段
///       `_comment_`：`Please do not automate the download and installation of Forge.
///       Our efforts are supported by ads from the download page.` —— 也就是说
///       **Forge 明确不希望别人自动装**。我们走 maven 直链是刻意的选择：下载页那条路
///       要先过广告页拿 token，做成"替用户点广告"才是真的不礼貌；maven 上的 jar 是同一个
///       文件、由 Forge 自己发布，走它至少没绕过任何东西。
///
/// NOTE: 这个 host（maven.minecraftforge.net）不在 DownloadManager::mirrorUrl() 认得的那
///       三个里面，所以它会**原样**过去 —— 镜像源不代理 Forge 的 maven，这是对的。
inline QString forgeInstallerUrl(const QString &gameVersion, const QString &loaderVersion) {
    const QString full = gameVersion + QLatin1Char('-') + loaderVersion;
    return QStringLiteral("https://maven.minecraftforge.net/net/minecraftforge/forge/%1/forge-%1-installer.jar")
        .arg(full);
}

/// Forge 装出来的版本目录名。
///
/// 和 Fabric 那边一个道理：这个名字**不是我们发明的**，是安装器自己写进 launcher_profiles
/// 的 profile、也是版本 json 里 `id` 字段的值（实测 1.20.1 + 47.4.10 → `1.20.1-forge-47.4.10`）。
/// 改一个字就等于"装好了但谁都不认识它"。装完会**核对这个目录真的存在**（见 VersionManager），
/// 所以万一哪天 Forge 改了约定，报的是"装是装了、但没在预期的地方找到"，不是静默写错地方。
inline QString forgeLoaderId(const QString &gameVersion, const QString &loaderVersion) {
    return QStringLiteral("%1-forge-%2").arg(gameVersion, loaderVersion);
}

/// NeoForge 的 maven 元数据（那 63 KB 的 XML 里就是**全部**版本号）。
inline QString neoforgeMavenMetadataUrl() {
    return QStringLiteral(
        "https://maven.neoforged.net/releases/net/neoforged/neoforge/maven-metadata.xml");
}

/// 把一个点分版本号解析成数字列表。空列表 = 不是版本号。
///
/// NOTE: 逐段走正则，不用 QString::toInt() —— 后者接受前导空白、正负号（`"+5"`、`" 5"`
///       都能过），而这里进来的是 maven 上别人写的东西、出去的用途是拼 URL 和比大小。
inline QList<int> parseDottedVersion(const QString &text) {
    QList<int> parts;
    if (text.isEmpty()) {
        return parts;
    }
    static const QRegularExpression digits(QStringLiteral("^[0-9]+$"));
    const QStringList segments = text.split(QLatin1Char('.'));
    for (const QString &segment : segments) {
        if (!digits.match(segment).hasMatch()) {
            return QList<int>();
        }
        parts.append(segment.toInt());
    }
    return parts;
}

/// 比两个点分版本号。**短的按补零算**（所以 `26.1.2` < `26.1.2.71`）。
/// @return 负数 / 0 / 正数和 strcmp 一个意思。
inline int compareDottedVersions(const QList<int> &left, const QList<int> &right) {
    const int length = qMax(left.size(), right.size());
    for (int i = 0; i < length; ++i) {
        const int a = i < left.size() ? left.at(i) : 0;
        const int b = i < right.size() ? right.at(i) : 0;
        if (a != b) {
            return a < b ? -1 : 1;
        }
    }
    return 0;
}

/// `prefix` 是不是 `full` 的**逐段**前缀（`[21,1]` 是 `[21,1,251]` 的前缀，
/// 但**不是** `[21,10,64]` 的前缀）。
///
/// NOTE: 这里必须是逐段比，不能拿字符串前缀比 —— NeoForge 真有 `21.1.x` 和 `21.10.x`
///       两族（分别对应 MC 1.21.1 和 1.21.10），按字符串 `"21.1"` 前缀匹配会把
///       `21.10.64` 也算进来，然后在比大小时把 `21.10.64` 挑成 1.21.1 的加载器。
inline bool isDottedPrefix(const QList<int> &prefix, const QList<int> &full) {
    if (prefix.isEmpty() || prefix.size() > full.size()) {
        return false;
    }
    for (int i = 0; i < prefix.size(); ++i) {
        if (prefix.at(i) != full.at(i)) {
            return false;
        }
    }
    return true;
}

/// NeoForge 版本号的"游戏版本前缀"。
///
/// NeoForge 的版本号刻意跟着游戏版本走，但**不是**同一串数字：
///   游戏 1.21.1  → NeoForge 21.1.x      （去掉开头的 `1.`）
///   游戏 1.20.2  → NeoForge 20.2.x
///   游戏 26.2    → NeoForge 26.2.x      （游戏版本自己就是 26.2，没有 `1.` 可去）
///   游戏 26.1.2  → NeoForge 26.1.2.x
/// 所以规则是：去掉开头的 `1.`（如果有），再取前两段；只有一段时补一个 `.0`。
/// 补 `.0` 那一手是给 `1.21` 这种两段式游戏版本用的 —— 它对应 NeoForge 的 `21.0.x`，
/// 不补的话前缀会退化成 `[21]`，于是把 `21.1.x`、`21.11.x` 全都算进来。
inline QList<int> neoForgeVersionPrefix(const QString &gameVersion) {
    QString text = gameVersion;
    if (text.startsWith(QLatin1String("1."))) {
        text.remove(0, 2);
    }
    QList<int> parts = parseDottedVersion(text);
    if (parts.isEmpty()) {
        return parts;
    }
    while (parts.size() < 2) {
        parts.append(0);
    }
    parts = parts.mid(0, 2);
    return parts;
}

/// 从 neoforge 的 maven-metadata.xml 里挑一个 NeoForge 版本。
/// @param mavenMetadata XML 原文（原样，这里只扫 `<version>...</version>`）。
/// @param gameVersion 原版版本号。
/// @return 挑中的版本；这个游戏版本一个正式版都没有时 `ok()` 为 false。
///
/// 规则：**只认不带后缀的版本号**（`-beta` 那一大堆全部跳过），在剩下的里面挑**数字最大**
/// 的那个。
///
/// 为什么是"比大小"而不是像 Fabric 那样"取第一个"：maven-metadata 的 `<versions>` 是
/// **按发布时间排的，不是按版本号排的**。实测那一份的末尾是
/// `26.3.0.6-beta, 21.1.251, 26.3.0.7-beta, 26.3.0.8-beta` —— `21.1.251` 是刚为一个旧
/// 版本补发的，夹在一堆 26.3 的快照中间。取"最后一个匹配的"会挑出 `21.1.251` 是不小心的
/// 巧合，换个游戏版本就错了。
/// NOTE: `-beta` 之外的后缀也一并跳过（maven 上还有别的），理由是"这个后缀能不能日常用"
///       不是我们能判断的事情，宁可不装也不给用户一个半成品。
inline LoaderPick pickNeoForgeVersion(const QByteArray &mavenMetadata, const QString &gameVersion) {
    const QList<int> prefix = neoForgeVersionPrefix(gameVersion);
    if (prefix.isEmpty()) {
        return LoaderPick{};
    }

    LoaderPick best;
    QList<int> bestParsed;
    static const QRegularExpression tag(QStringLiteral("<version>([^<]+)</version>"));
    QRegularExpressionMatchIterator it = tag.globalMatch(QString::fromUtf8(mavenMetadata));
    while (it.hasNext()) {
        const QString version = it.next().captured(1).trimmed();
        if (version.contains(QLatin1Char('-'))) {
            continue;
        }
        const QList<int> parsed = parseDottedVersion(version);
        if (parsed.isEmpty() || !isDottedPrefix(prefix, parsed)) {
            continue;
        }
        if (best.version.isEmpty() || compareDottedVersions(parsed, bestParsed) > 0) {
            best = LoaderPick{version, true};
            bestParsed = parsed;
        }
    }
    return best;
}

/// NeoForge 安装器 jar 的地址。
/// NOTE: 这条地址是实测通的（21.1.251 和 26.2.0.57 都 HTTP 200）。和 Forge 那条不一样，
///       NeoForge 的 maven 就是它官方的发布处，没有广告页那一层。
inline QString neoforgeInstallerUrl(const QString &loaderVersion) {
    return QStringLiteral("https://maven.neoforged.net/releases/net/neoforged/neoforge/%1/neoforge-%1-installer.jar")
        .arg(loaderVersion);
}

/// NeoForge 装出来的版本目录名。
///
/// 和 Forge **不一样**：它不带游戏版本前缀，直接就是 `neoforge-<版本>`
/// （实测 21.1.215 → `versions/neoforge-21.1.215/`）。这不是笔误，也不能"统一一下"——
/// 别的启动器按这个名字找目录。
inline QString neoforgeLoaderId(const QString &loaderVersion) {
    return QStringLiteral("neoforge-%1").arg(loaderVersion);
}

/// Forge / NeoForge 的安装器认的那个"启动器档案"文件名。
///
/// 它不是一个可选的东西：**没有它安装器直接拒绝干活**（见上面那段实测）。我们的启动器
/// 自己不需要它，但它旁边的原版启动器、以及这些第三方安装器都按它判断"这是一个
/// Minecraft 数据目录"。
inline QString launcherProfilesFileName() { return QStringLiteral("launcher_profiles.json"); }

/// 一份**最小可用**的 launcher_profiles.json。
///
/// 只在目标目录里没有这个文件时才写（见 VersionManager::ensureLauncherProfiles）——
/// 装过任何一个原版启动器/第三方启动器的目录里都有一份内容更全的，覆盖它是把用户的
/// 东西弄丢。结构照抄安装器认的那三个键：原版启动器写出来就是这个形状。
inline QByteArray minimalLauncherProfiles() {
    return QByteArrayLiteral("{\n  \"profiles\": {},\n  \"settings\": {},\n  \"version\": 3\n}\n");
}

// ---------------------------------------------------------------------------
// OptiFine：跑它自己的安装器（无界面入口 + 换 APPDATA）
// ---------------------------------------------------------------------------
//
// 【为什么它和上面 Forge / NeoForge 那段又不一样】（2026-09-22 实测 1.18.2 + HD U H9）
//
// 同样是"请下来别人的安装器、另起一个 Java 进程跑它"，但 OptiFine 的安装器有三处要命的
// 不同：
//
//  1. **命令行参数它整个丢掉**：没有 `--installClient` 那种东西可给，游戏目录只从
//     `%APPDATA%/.minecraft` 算（`Utils.getWorkingDirectory()`）。所以它往哪个目录装
//     完全由子进程的 APPDATA 决定 —— 我们给它换一个，让它对着一个临时目录干活。
//  2. **它是 GUI 程序**：jar 的 `Main-Class` 是 `optifine.InstallerFrame`（Swing 窗口，
//     headless 下当场 HeadlessException）。真正的无界面入口是 **`optifine.Installer`**：
//     `java -cp <安装器>.jar optifine.Installer`，**不需要任何参数** —— 版本号是它从自己
//     那份 jar 里读的（把它改名成「随便什么名字.jar」，它照样报 OptiFine_1.18.2_HD_U_H9）。
//  3. **出错时它弹一个 Swing 对话框等着人点**：不点就永不退出（实测跑满 120 秒被杀）。
//     所以子进程必须带 `-Djava.awt.headless=true` —— 那些框当场变成异常，进程以退出码 1
//     结束。**成功那条路一个框都不弹、退出码 0**（实测 headless 下 2 秒装完）。
//
// 【实测到的产物】（APPDATA=<临时目录>，那个目录里事先放好原版 1.18.2 的 json + jar）
//
//   versions/1.18.2-OptiFine_HD_U_H9/1.18.2-OptiFine_HD_U_H9.json   463 B，见下
//   versions/1.18.2-OptiFine_HD_U_H9/1.18.2-OptiFine_HD_U_H9.jar    **和原版客户端 jar
//       逐字节相同**（sha1 一样）。这一条反直觉但很关键：OptiFine **不往这份 jar 里写
//       补丁**，它只是把原版那份原样放进新目录当"这个版本的客户端 jar"。它自己的代码在
//       下面那个库里，靠 launchwrapper + `--tweakClass optifine.OptiFineTweaker` 挂进去。
//   libraries/optifine/OptiFine/1.18.2_HD_U_H9/OptiFine-1.18.2_HD_U_H9.jar   6.2 MB
//   libraries/optifine/launchwrapper-of/2.3/launchwrapper-of-2.3.jar
//   外加它往 launcher_profiles.json 里加的一条档案（还顺手把 selectedProfile 改成了它）
//
// 那份 json 的 `libraries` 只有两个 `name`，**既没有 `url` 也没有 `downloads`** —— 只能
// 从本机那份 `libraries/` 里满足，没有"靠下载补齐"这条路。所以搬产物时**必须连带
// `libraries/optifine/` 一起搬**，只搬版本目录会得到一个启动就 NoClassDefFound 的版本。
//
// 【和 Forge 正好相反的两处顺序】
//
//  * Forge 是"下安装器 → 跑 → 装完再补原版"。OptiFine 的安装器**要拿原版的客户端 jar
//    当输入**（上面第二样产物就是它），所以原版必须**先**在盘上。
//  * Forge 缺 `launcher_profiles.json` 是第一件事就拒绝（一个字节的库都不下）。OptiFine
//    把它排在**最后一步**：前面整套东西都装完了，它才去更新那个文件，缺了当场弹框报错
//    —— headless 下就是退出码 1，而**产物其实已经躺在盘上了**。我们的临时目录里自己补
//    一份最小的，所以走不到那一步。
//
// 【版本目录名不是我们算得出来的】
//
// Forge/NeoForge 的名字（`1.20.1-forge-47.4.10` / `neoforge-21.1.215`）我们能按约定拼，
// OptiFine 的不行 —— 它是安装器按 jar 里的版本号自己拼的（`<原版>-OptiFine_<版本>`）。
// 所以这一条**装完去盘上认**：我们给它那个临时目录是全新的，`versions/` 底下除了自己放
// 进去的原版之外多出来的那一份就是它装的。见 VersionManager::adoptOptiFineProducts()。

/// OptiFine 的官方下载页：每个游戏版本一节（`<h2>Minecraft <版本></h2>`），整页约 203 KB。
inline QString optifineDownloadsPageUrl() {
    return QStringLiteral("https://optifine.net/downloads");
}

/// 镜像源（BMCLAPI）上某个游戏版本的 OptiFine 列表（JSON 数组，一个游戏版本一页）。
///
/// NOTE: 它和官方下载页**不是同一份数据**：这里每条只有文件名和 type/patch，
///       页面上那套"哪一条是主推的"（`downloadLineMain`）在这里没有对应物。
inline QString optifineMirrorListUrl(const QString &gameVersion) {
    return QStringLiteral("https://bmclapi2.bangbang93.com/optifine/") + gameVersion;
}

/// 镜像源上安装器文件的地址（302 到 `/maven/com/optifine/...`，实测通的）。
///
/// NOTE: 最后两段是 `type` / `patch` 两个**字段**，不是文件名 —— 官方页面上那条
///       `OptiFine_1.18.2_HD_U_H9.jar` 到这里拆成了 `HD_U` 和 `H9`。
inline QString optifineMirrorFileUrl(const QString &gameVersion, const QString &type,
                                     const QString &patch) {
    return optifineMirrorListUrl(gameVersion) + QLatin1Char('/') + type + QLatin1Char('/') + patch;
}

/// 官方那条路上的广告页：真文件在它后面（页面里那条 `downloadx?...&x=<32 位十六进制>`）。
inline QString optifineAdPageUrl(const QString &fileName) {
    return QStringLiteral("https://optifine.net/adloadx?f=") + fileName;
}

/// 安装器 jar 的文件名能不能直接拿来拼 URL 和拼路径。
///
/// 白名单和 isSafeVersionId() 那条是同一个（`[A-Za-z0-9._+-]`），理由也一样：**内容来自
/// 网络、用途是拼路径**。这里多要求 `.jar` 结尾，因为这个名字会落成
/// `cache/installers/` 下面的一个文件。
inline bool isSafeJarFileName(const QString &name) {
    static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9._+-]+\\.jar$"));
    return safe.match(name).hasMatch();
}

/// 预览版的文件名长这样（`preview_OptiFine_1.18.2_HD_U_H9_pre2.jar`）。
inline bool isOptifinePreviewFile(const QString &fileName) {
    return fileName.startsWith(QLatin1String("preview_"));
}

/// 挑出来的那一个 OptiFine 安装器。
struct OptiFinePick {
    QString fileName;  ///< 安装器 jar 的名字（`OptiFine_1.18.2_HD_U_H9.jar`）
    QString type;      ///< 镜像那条路才有：`HD_U`（预览版是 `HD_U_H9`）
    QString patch;     ///< 镜像那条路才有：`H9`（预览版是 `pre2`）

    bool ok() const { return !fileName.isEmpty(); }
};

/// 安装器文件名 → OptiFine 的"版本号"（就是写进 wtfml.meta 的 loaderVersion）。
///
/// `OptiFine_1.18.2_HD_U_H9.jar` → `HD_U_H9`；
/// 预览版 `preview_OptiFine_1.18.2_HD_U_H9_pre2.jar` → `HD_U_H9_pre2`。
///
/// NOTE: **刻意不拿镜像那份 `type`/`patch` 拼**。那两个字段的切法和文件名不一样
///       （预览版那几条的 type 是 `HD_U_H9`、patch 是 `pre2`），而"这个版本叫什么"在
///       官方/镜像两条路上必须是**同一个值** —— 只有这一处切法才不会两边对不上。
inline QString optifineEditionFromFile(const QString &gameVersion, const QString &fileName) {
    QString text = fileName;
    const QString kPreview = QStringLiteral("preview_");
    const QString kPrefix = QStringLiteral("OptiFine_");
    if (text.startsWith(kPreview)) {
        text.remove(0, kPreview.size());
    }
    if (text.startsWith(kPrefix)) {
        text.remove(0, kPrefix.size());
    }
    if (text.endsWith(QLatin1String(".jar"))) {
        text.chop(4);
    }
    const QString versionPrefix = gameVersion + QLatin1Char('_');
    if (text.startsWith(versionPrefix)) {
        text.remove(0, versionPrefix.size());
    }
    return text;
}

/// 从官方下载页的原文里挑一个 OptiFine 版本。
/// @param html `https://optifine.net/downloads` 的原文。
/// @param gameVersion 原版版本号（页面上那一节 `<h2>Minecraft <版本></h2>`）。
///
/// 规则：**只看那一节里标着 `downloadLineMain` 的那一行**，取它的 `adloadx?f=...` 文件名。
/// 这一行是 OptiFine 自己标出来的"这个游戏版本就用这个"。同一节里另外两类行都要避开：
///
///   * `downloadLinePreview` —— 预览版（1.15 之后每节开头都可能有这么一段）
///   * `downloadLineMore` —— 折叠在「+ More」里的旧版本
///
/// 实测 1.18.2 那一节三类同时存在：预览段是空的、main 是 H9、More 里是 H7 / H6。
/// 挑错哪一类都不至于装不上，但"给用户装个预览版"和"装个旧版本"都不是我们该替他做的决定。
/// （26.1.2 那种只有预览、还没发正式版的，这一节里就没有 main 行 —— 那时 `ok()` 为 false。）
///
/// NOTE: 那一节的边界靠**下一个 `<h2>`** 划。页面上 `<h2>` 只用来分游戏版本，所以这是
///       稳的；但也正因为如此，找不到小节时要如实返回空，而不是"从整页里随便挑一个"。
inline OptiFinePick pickOptifineFromDownloadsPage(const QString &html,
                                                  const QString &gameVersion) {
    static const QRegularExpression heading(
        QStringLiteral("<h2>\\s*Minecraft\\s+([^<]+?)\\s*</h2>"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression row(QStringLiteral("<tr\\b([^>]*)>(.*?)</tr>"),
                                        QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression link(QStringLiteral("adloadx\\?f=([^\"'&<>\\s]+)"));

    int sectionStart = -1;
    int sectionEnd = html.size();
    QRegularExpressionMatchIterator headings = heading.globalMatch(html);
    while (headings.hasNext()) {
        const QRegularExpressionMatch match = headings.next();
        if (match.captured(1).trimmed() != gameVersion) {
            continue;
        }
        sectionStart = match.capturedEnd();
        if (headings.hasNext()) {
            sectionEnd = headings.peekNext().capturedStart();
        }
        break;
    }
    if (sectionStart < 0) {
        return OptiFinePick{};
    }

    const QString section = html.mid(sectionStart, sectionEnd - sectionStart);
    QRegularExpressionMatchIterator rows = row.globalMatch(section);
    while (rows.hasNext()) {
        const QRegularExpressionMatch match = rows.next();
        if (!match.captured(1).contains(QLatin1String("downloadLineMain"))) {
            continue;
        }
        const QRegularExpressionMatch found = link.match(match.captured(2));
        if (!found.hasMatch()) {
            continue;
        }
        const QString fileName = found.captured(1);
        if (!isSafeJarFileName(fileName)) {
            continue;
        }
        return OptiFinePick{fileName, QString(), QString()};
    }
    return OptiFinePick{};
}

/// 从镜像源 `/optifine/<原版版本>` 的回复里挑一个 OptiFine 版本。
///
/// 规则：**跳过预览版，取剩下的最后一条**。
///
/// 为什么是"最后一条"而不是比大小：OptiFine 的版本号是 `HD_U_H9` 这种"字母代 + 序号"，
/// 而且换个游戏版本就换成别的字母（I1 / J1 / K1…），自己写个比较器只会更容易挑错。
/// 实测那份数组是**按发布顺序**排的（1.18.2 那三条是 H6 → H7 → H9，连它们 `_id` 前四个
/// 字节里的时间戳都是递增的：2022-03-23 → 04-10 → 06-27），所以"最后一条正式版"就是最新
/// 那条。NOTE: 官方页面上那个"官方主推"的概念（`downloadLineMain`）镜像这边没有，
/// 所以这条路的挑法**只能**是"最新"，两条路挑出不同版本是可能的（官方 lag 一次就会）。
///
/// @param list 那个接口返回的数组（原样）。
inline OptiFinePick pickOptifineFromMirror(const QJsonArray &list) {
    OptiFinePick pick;
    for (const QJsonValue &value : list) {
        const QJsonObject entry = value.toObject();
        const QString fileName = entry.value(QStringLiteral("filename")).toString();
        const QString type = entry.value(QStringLiteral("type")).toString();
        const QString patch = entry.value(QStringLiteral("patch")).toString();
        // 少一样就用不了：文件名是给用户看的和落盘用的，type/patch 是拼下载地址用的。
        if (!isSafeJarFileName(fileName) || type.isEmpty() || patch.isEmpty()) {
            continue;
        }
        if (isOptifinePreviewFile(fileName)) {
            continue;
        }
        pick = OptiFinePick{fileName, type, patch};
    }
    return pick;
}

/// 从广告页的原文里抠出真正的下载地址（`https://optifine.net/downloadx?f=…&x=…`）。
/// @param html `optifineAdPageUrl()` 那个地址的回复原文。
/// @param fileName 我们请求的那个文件名（用来在页面里有多条链接时挑对）。
/// @return 可以直接 GET 的绝对地址；抠不出来时空串。
///
/// 页面上那条链接是**相对**的，所以这里补上 host。挑法：优先挑 `f=` 和我们请求的一致的那
/// 条；页面里只有一条时就用那一条 —— 实测那个 token **不跟文件名绑定**（拿 H7 的 token 去
/// 请求 H9，下下来的照样是 H9 的 jar，6.7 MB、`application/java-archive`）。有好几条又
/// 没有一条对得上时**不猜**：宁可报"拿不到下载地址"，也不给用户下一个不知道是什么的文件。
inline QString optifineJarUrlFromAdPage(const QString &html, const QString &fileName) {
    static const QRegularExpression link(
        QStringLiteral("(downloadx\\?f=([^\"'&<>\\s]+)&x=[0-9a-fA-F]{8,})"));
    QStringList links;
    QStringList names;
    QRegularExpressionMatchIterator it = link.globalMatch(html);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        links.append(match.captured(1));
        names.append(match.captured(2));
    }
    if (links.isEmpty()) {
        return QString();
    }
    int index = names.indexOf(fileName);
    if (index < 0) {
        if (links.size() != 1) {
            return QString();
        }
        index = 0;
    }
    return QStringLiteral("https://optifine.net/") + links.at(index);
}

}  // namespace LoaderMeta
