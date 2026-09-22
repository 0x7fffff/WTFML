/**
 * @file VersionArguments.hpp
 * @brief 展开 version.json 的 arguments 数组（平台规则过滤 + ${...} 占位符替换）。
 * @author 0x7fffff
 * @date 2026-09
 * @note 做成 header-only：LauncherManager 和验证工程都要用，而它只依赖
 *       LibraryRules 的规则判定，不需要 Widgets。
 */
#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QString>
#include <QStringList>

#include "LibraryRules.hpp"

namespace VersionArguments {

/// 替换一条参数里的 ${...} 占位符。
///
/// NOTE: 不认识的占位符**保留原文并告警**。宁可日志里显眼，也不要静默丢掉一个 Mojang
///       认为必要的参数 —— 那种"参数凭空消失"正是 26.3 崩溃的成因。
inline QString substitutePlaceholders(const QString &raw,
                                      const QHash<QString, QString> &placeholders) {
    QString arg = raw;
    for (auto it = placeholders.constBegin(); it != placeholders.constEnd(); ++it) {
        arg.replace(QStringLiteral("${") + it.key() + QLatin1Char('}'), it.value());
    }
    if (arg.contains(QLatin1String("${"))) {
        qWarning() << "Unresolved placeholder in version arguments:" << arg;
    }
    return arg;
}

/**
 * @brief 这个长选项是不是"原版那套游戏参数"里的一条（原版 arguments.game 的名字表）。
 *
 * 用来避免同一件事发两遍：加载器版本的 arguments.game 是**整份继承下来的**，里面绝大多数
 * 条目原版就有，而启动器那边已经按自己的规则发过一遍（--username / --gameDir / ...）。
 */
inline bool isVanillaGameArgument(const QString &flag) {
    static const QSet<QString> names = {
        QStringLiteral("--username"),       QStringLiteral("--version"),
        QStringLiteral("--gameDir"),        QStringLiteral("--assetsDir"),
        QStringLiteral("--assetIndex"),     QStringLiteral("--uuid"),
        QStringLiteral("--accessToken"),    QStringLiteral("--clientId"),
        QStringLiteral("--xuid"),           QStringLiteral("--userType"),
        QStringLiteral("--versionType"),    QStringLiteral("--userProperties"),
        QStringLiteral("--auth_session"),   QStringLiteral("--demo"),
        QStringLiteral("--width"),          QStringLiteral("--height"),
        QStringLiteral("--quickPlayPath"),  QStringLiteral("--quickPlaySingleplayer"),
        QStringLiteral("--quickPlayMultiplayer"), QStringLiteral("--quickPlayRealms"),
    };
    return names.contains(flag);
}

/**
 * @brief 把一个 arguments 数组展开成参数列表。
 *
 * NOTE: 两种数组它都能吃，但启动器给它的主要是 arguments.jvm（调用点在 LauncherManager）。
 *       version.json 的 arguments.game 走的是下面 extraGameArguments 那一条 —— 那是
 *       2026-09-22 补的（NeoForge 26.2 启动即崩，缺的正是 arguments.game 里的 --fml.*）。
 * @param entries version.json 里 arguments 下的数组，可同时含字符串和规则对象。
 * @param placeholders ${...} 占位符到实际值的映射。
 * @param features 已启用的 feature 集合，启动器不启用任何 feature，传空即可。
 * @param dropClasspath 是否丢弃 ${classpath}（连同紧邻它前面的 -cp）。
 * @return 返回可以直接交给 QProcess 的参数列表。
 *
 * 数组里每个元素有两种形态：
 *   - 字符串：无条件使用，例如 '-XX:StackShadowPages=32'；
 *   - 对象：{"rules": [...], "value": <字符串或字符串数组>}，只有规则命中当前平台才使用。
 *
 * NOTE: 规则对象的形状和 library 的 rules 完全一致（rules 数组 + allow/disallow
 * 折叠），所以这里直接复用 LibraryRules::libraryAllowed —— 绝不写第二份规则逻辑，
 * 否则两处的语义迟早各自漂移。顺带一提，--add-exports 和它的值在 JSON 里是两个
 * 独立元素，展开时保持原顺序、原样成对传递即可，不要去"配对"它们。
 */
inline QStringList expand(const QJsonArray &entries, const QHash<QString, QString> &placeholders,
                          const QSet<QString> &features = QSet<QString>(),
                          bool dropClasspath = true) {
    // 先按顺序挑出适用的原始条目，再做替换 —— 分开两步，规则判定不用关心占位符。
    QStringList declared;
    for (const QJsonValue &entry : entries) {
        if (entry.isString()) {
            declared << entry.toString();
            continue;
        }
        if (!entry.isObject()) {
            continue;
        }
        const QJsonObject entryObj = entry.toObject();
        if (!LibraryRules::libraryAllowed(entryObj, features)) {
            continue;
        }
        const QJsonValue value = entryObj.value(QStringLiteral("value"));
        if (value.isArray()) {
            for (const QJsonValue &item : value.toArray()) {
                if (item.isString()) {
                    declared << item.toString();
                }
            }
        } else if (value.isString()) {
            declared << value.toString();
        }
    }

    QStringList result;
    for (const QString &raw : declared) {
        if (dropClasspath && raw == QLatin1String("${classpath}")) {
            // classpath 由调用方拼（只有它知道绝对路径），这里连它前面的 -cp 一起
            // 丢掉，否则命令行上会出现两个 -cp。
            if (!result.isEmpty() && result.last() == QLatin1String("-cp")) {
                result.removeLast();
            }
            continue;
        }

        const QString arg = substitutePlaceholders(raw, placeholders);
        if (!arg.isEmpty()) {
            result << arg;
        }
    }
    return result;
}

/**
 * @brief 判断展开后的参数里是否已经有某个前缀的参数。
 * @param args expand() 的结果。
 * @param prefix 要查找的前缀，例如 "-Djava.library.path="。
 * @return 若已存在该前缀的参数则返回 true.
 *
 * 用来避免同一属性被我们和 version.json 各加一次。
 */
inline bool declaresPrefix(const QStringList &args, const QString &prefix) {
    for (const QString &arg : args) {
        if (arg.startsWith(prefix)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief 把 version.json 自己声明的**额外游戏参数**挑出来。
 *
 * NOTE: 这个问题是 2026-09-22 由真机日志顶出来的（用户报"启动游戏崩了"）：
 *       `versions/neoforge-26.2.0.88/` 那份 json 的 arguments.game 里写着
 *       `--fml.neoForgeVersion 26.2.0.88 --fml.mcVersion 26.2 --fml.neoFormVersion 2`，
 *       而启动器**只展开 arguments.jvm**，于是 FML 起来第一句就是
 *       "When launching in production, --fml.mcVersion must be present as a
 *       command-line argument"，紧接着 `VersionSupportMatrix.<init>` 拿着 null 版本号 NPE，
 *       游戏在加载窗口那一步就没了（日志见 build/.minecraft/logs/latest.log）。Forge 走的
 *       是同一条路（`--launchTarget forgeclient --fml.*`）；Fabric 不受影响（它的东西都在
 *       arguments.jvm 里），所以这个 bug 只在 Forge / NeoForge 上现形。
 *
 * 三条规矩：
 *   1. 已经在 `alreadyPassed` 里发过的选项、以及原版那套参数名（isVanillaGameArgument），
 *      一律跳过（**连它的值一起**），剩下的按原顺序原样追加。纯原版那条路因此是空的：它的
 *      arguments.game / minecraftArguments 里全是那套名字（实测 26.2 / 26.3 / 1.18.2 /
 *      1.19 / 1.12.2 五份原版 json，名单外的 flag 一个都没有），而原版启动器该怎么发这些
 *      参数、上面那段硬编码已经发过了 —— 已经验过的那条路逐字节不变。
 *   2. 老格式（`minecraftArguments` 那个空格分隔的字符串）也吃 —— 1.12.2 那一代的 Forge
 *      就是靠它声明 `--tweakClass` 的，同样从来没被展开过。
 *
 * NOTE: 2026-09-22 第二版改动：**判据不是 `inheritsFrom`**。第一版写的是"json 里有
 *       inheritsFrom 才展开"，单元测试（拿 meta/ 里那份原样的加载器 profile 喂进去）是过的，
 *       可真实启动那条路上进来的 json 是**继承链合并过**的那份 —— 而 `VersionInheritance::
 *       merge` 刻意把 `inheritsFrom` 丢掉了（见那边的 NOTE），于是判据恒为假、这三条
 *       `--fml.*` 一条都发不出去，NeoForge 26.2 照旧崩在原来那一句上。
 *       这个坑是靠 `verify.exe dumpmerged <版本> <主版本> <文件>`（拿沙箱里真装好的版本目录
 *       走一遍合并，把 generateJvmArguments 的结果打出来）看出来的：光看单元测试是绿的。
 *       所以判据只剩上面第 1 条 —— 它对"合并过的"和"没合并的"两种输入给同一个答案。
 *
 * @param versionJson 版本 json（**最终那份**：加载器版本是继承链合并后的，merge 会把
 *        arguments.game 按"父在前、子在后"拼起来）。
 * @param alreadyPassed 启动器已经发过的参数（把最终 args 列表原样传进来即可）。
 * @param placeholders ${...} 占位符到实际值的映射（游戏参数那一套，见调用点）。
 * @param features 已启用的 feature 集合，启动器不启用任何 feature，传空即可。
 * @return 需要追加到命令行末尾的那些参数。
 */
inline QStringList extraGameArguments(const QJsonObject &versionJson,
                                     const QStringList &alreadyPassed,
                                     const QHash<QString, QString> &placeholders,
                                     const QSet<QString> &features = QSet<QString>()) {
    const QJsonObject arguments = versionJson.value(QStringLiteral("arguments")).toObject();
    const QJsonArray game = arguments.value(QStringLiteral("game")).toArray();
    QStringList declared;
    if (!game.isEmpty()) {
        // dropClasspath = false：游戏参数里不该有 ${classpath}（那是 JVM 那边的事），
        // 就算有也不该把前面的 -cp 一起吃掉。
        declared = expand(game, placeholders, features, false);
    } else {
        const QString legacy = versionJson.value(QStringLiteral("minecraftArguments")).toString();
        for (const QString &raw : legacy.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            const QString arg = substitutePlaceholders(raw, placeholders);
            if (!arg.isEmpty()) {
                declared << arg;
            }
        }
    }

    QStringList out;
    for (int i = 0; i < declared.size(); ++i) {
        const QString &token = declared.at(i);
        if (!token.startsWith(QLatin1String("--"))) {
            out << token;
            continue;
        }
        if (alreadyPassed.contains(token) || isVanillaGameArgument(token)) {
            // 这条已经有一份了：跳过它和它的值（值是紧跟其后、不以 - 开头的那个 ——
            // 原版那套参数名后面跟的都是值，没有"光杆开关"）。
            if (i + 1 < declared.size() && !declared.at(i + 1).startsWith(QLatin1Char('-'))) {
                ++i;
            }
            continue;
        }
        out << token;
    }
    return out;
}

}  // namespace VersionArguments
