/**
 * @file LoaderMeta.hpp
 * @brief 模组加载器元数据接口的**纯函数**部分：地址怎么拼、列表回复怎么挑版本。
 * @author 0x7fffff
 * @date 2026-09-21
 * @note 这里不联网、不发信号，只做"字符串进、字符串出"。真正发请求的是
 *       VersionManager::installLoaderVersion()（它才是 QObject），这样这几条规则能在
 *       验证工程里离线钉住 —— 联网的那半边没法在沙箱里跑（要真下几个 GB 的资源）。
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

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1Char>
#include <QRegularExpression>
#include <QString>

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

}  // namespace LoaderMeta
