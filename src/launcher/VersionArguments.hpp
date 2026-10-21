/**
 * @file VersionArguments.hpp
 * @brief 展开 version.json 的 arguments 数组（平台规则过滤 + ${...} 占位符替换）。
 * @note 做成 header-only：LauncherManager 和验证工程都要用，而它只依赖
 * LibraryRules 的规则判定，不需要 Widgets。
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
/// 认为必要的参数 —— 那种"参数凭空消失"正是 26.3 崩溃的成因。
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
 *
 * NOTE: `--width`/`--height` 在这张表里是**有活干**的，不是凑数。
 * 原版 json 里那条带 `has_custom_resolution` 规则的 width/height 条目就是靠这里被
 * 丢掉的 —— 去掉它们的话，用户设了窗口大小、而启动器又自己发一遍 `--width`，同一个
 * 开关就会发两次，joptsimple 遇到重复选项直接报错退出。启动器那一侧发这一对参数的
 * 地方见 LauncherManager::generateJvmArguments 里「游戏窗口大小」那一段。
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
 * @brief 只做"规则过滤"这一步：把 arguments 数组里**适用**的条目按原顺序摊平成原始 token
 * 列表，**一个占位符都不替换**。
 *
 * NOTE: `dropClasspath` 那一格**不在这里**（它挪不掉）—— 它要在替换之前看着"上一条是不是
 * 已经被丢了"，所以留在 `expand()` 里。调用它的只有 extraGameArguments，而那边
 * 本来就是 `dropClasspath = false`。
 */
inline QStringList declaredArguments(const QJsonArray &entries,
                                     const QSet<QString> &features = QSet<QString>()) {
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
    return declared;
}

/**
 * @brief 把一个 arguments 数组展开成参数列表。
 *
 * NOTE: 两种数组它都能吃，但启动器给它的主要是 arguments.jvm（调用点在 LauncherManager）。
 * version.json 的 arguments.game 走的是下面 extraGameArguments 那一条 —— 那是
 * 补的（NeoForge 26.2 启动即崩，缺的正是 arguments.game 里的 --fml.*）。
 * @param entries version.json 里 arguments 下的数组，可同时含字符串和规则对象。
 * @param placeholders ${...} 占位符到实际值的映射。
 * @param features 已启用的 feature 集合，启动器不启用任何 feature，传空即可。
 * @param dropClasspath 是否丢弃 ${classpath}（连同紧邻它前面的 -cp）。
 * @return 返回可以直接交给 QProcess 的参数列表。
 *
 * 数组里每个元素有两种形态：
 * - 字符串：无条件使用，例如 '-XX:StackShadowPages=32'；
 * - 对象：{"rules": [...], "value": <字符串或字符串数组>}，只有规则命中当前平台才使用。
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
    const QStringList declared = declaredArguments(entries, features);

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
 * NOTE: `versions/neoforge-26.2.0.88/` 那份 json 的 arguments.game 里写着
 * `--fml.neoForgeVersion 26.2.0.88 --fml.mcVersion 26.2 --fml.neoFormVersion 2`，
 * 而只展开 arguments.jvm 的话，FML 起来第一句就是
 * "When launching in production, --fml.mcVersion must be present as a
 * command-line argument"，紧接着 `VersionSupportMatrix.<init>` 拿着 null 版本号 NPE，
 * 游戏在加载窗口那一步就没了。Forge 走的
 * 是同一条路（`--launchTarget forgeclient --fml.*`）；Fabric 不受影响（它的东西都在
 * arguments.jvm 里），所以这个问题只在 Forge / NeoForge 上现形。
 *
 * NOTE: **判据不是 `inheritsFrom`**：真实启动那条路上进来的 json 是**继承链合并过**的那份 ——
 * 而 `VersionInheritance::merge` 刻意把 `inheritsFrom` 丢掉了（见那边的 NOTE），于是判据
 * 恒为假、这三条 `--fml.*` 一条都发不出去，NeoForge 26.2 崩在那一句上。
 * 判据只剩上面第 1 条 —— 它对"合并过的"和"没合并的"两种输入给同一个答案。
 *
 * @param versionJson 版本 json（**最终那份**：加载器版本是继承链合并后的，merge 会把
 * arguments.game 按"父在前、子在后"拼起来）。
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
        // 这一份**只过滤、不替换**（`declaredArguments()` 而不是 `expand()`）—— 替换挪到
        // 最后那一步去做。理由：这一份里绝大多数条目是原版那套名字（`--clientId
        // ${clientid}`、`--xuid ${auth_xuid}`、`--userProperties ${user_properties}`、
        // `--quickPlayPath ${quickPlayPath}` …），它们**下一句就会被整条跳掉**（启动器
        // 自己发的是另一份），可那几个占位符我们本来就不提供 —— 先替换的话，每一次启动
        // 都会为这几条打一句「Unresolved placeholder」，而那几行
        // **一个字节都不会进命令行**。真出问题的那种（我们会发出去、却没人填值的）照样
        // 会报，因为它活到了最后那一步。
        //
        // NOTE: 不能传 `expand(game, 空映射, features, false)`：`expand()` 里那句
        // `substitutePlaceholders()` 是**无条件调的**，空映射只是"一条都换不掉"，警告
        // 照发 ⇒ 注释逐字承诺的事一件没做到，反而每次启动多刷 10~11 条。
        // **空映射不等于"不替换"**。
        declared = declaredArguments(game, features);
    } else {
        declared = versionJson.value(QStringLiteral("minecraftArguments"))
                       .toString()
                       .split(QLatin1Char(' '), Qt::SkipEmptyParts);
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

    // 活下来的这些才替换占位符（连同它的值 —— 值和开关一样要走这一遍，`--foo ${x}` 里
    // 那条是值）。替换成空串的丢掉，和 expand() 里的口径一致。
    QStringList result;
    for (const QString &token : out) {
        const QString arg = substitutePlaceholders(token, placeholders);
        if (!arg.isEmpty()) {
            result << arg;
        }
    }
    return result;
}

}  // namespace VersionArguments
