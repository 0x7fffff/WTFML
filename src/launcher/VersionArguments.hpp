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

/**
 * @brief 把一个 arguments 数组（arguments.jvm 或 arguments.game）展开成参数列表。
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

        QString arg = raw;
        for (auto it = placeholders.constBegin(); it != placeholders.constEnd(); ++it) {
            arg.replace(QStringLiteral("${") + it.key() + QLatin1Char('}'), it.value());
        }
        if (arg.contains(QLatin1String("${"))) {
            // 不认识的占位符：保留原文并告警。宁可日志里显眼，也不要静默丢掉一个
            // Mojang 认为必要的参数 —— 那种"参数凭空消失"正是 26.3 崩溃的成因。
            qWarning() << "Unresolved placeholder in version arguments:" << arg;
        }
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

}  // namespace VersionArguments
