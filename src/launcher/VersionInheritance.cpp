/**
 * @file VersionInheritance.cpp
 * @brief Implements the inheritsFrom merge.
 * @author 0x7fffff
 * @date 2026-09-21
 * @note 规则的来龙去脉写在 .hpp 的块注释里；这里只有实现，几处 NOTE 说的是"为什么不能
 *       照直觉改"。
 */

#include "VersionInheritance.hpp"

#include <QHash>
#include <QJsonArray>
#include <QJsonValue>
#include <QList>

namespace VersionInheritance {
namespace {

/// 合并 arguments 下的一个数组：**父的在前、子的在后**。
///
/// NOTE: 顺序不能反。JVM 对同一条可重复选项取**后写**的那个（`-cp`、`-D` 都是），
///       而"加载器盖掉原版声明的参数"正是加载器留这些 `arguments.jvm` 的用意
///       （Fabric 就靠 `-DFabricMcEmu=` 那一行把原版主类告诉自己的 Knot）。
///       反过来拼，加载器的参数会被原版盖掉，症状是加载器像没装一样。
QJsonArray concatArrays(const QJsonArray &parentArray, const QJsonArray &childArray) {
    QJsonArray out = parentArray;
    for (const QJsonValue &value : childArray) {
        out.append(value);
    }
    return out;
}

/// 合并 libraries：父的全部按原顺序铺开，再叠上子的（按 `name` 去重，**子赢**）。
///
/// 位置那条是刻意的，不是实现细节：子覆盖了父的某个库时，赢的那条留在**父原来的位置**上
/// —— 这正是原版启动器那个 LinkedHashMap 的行为（`put` 一个已有的键不改变顺序）。
/// Java 在 classpath 上取**先找到**的那个同名类，把覆盖项挪到队尾会改变谁盖住谁。
QJsonArray mergeLibraries(const QJsonArray &parentLibraries, const QJsonArray &childLibraries) {
    QList<QJsonObject> merged;
    QHash<QString, int> positions;  // name -> 在 merged 里的下标

    const auto absorb = [&merged, &positions](const QJsonArray &libraries) {
        for (const QJsonValue &value : libraries) {
            const QJsonObject library = value.toObject();
            const QString name = library.value(QStringLiteral("name")).toString();
            // 没有 name 的条目照收：它是我们看不懂的东西，丢掉比留着更危险
            // （说不定里面有个 artifact 是必需的）。去重只对认得出 name 的做。
            if (!name.isEmpty()) {
                const auto existing = positions.constFind(name);
                if (existing != positions.constEnd()) {
                    merged[existing.value()] = library;
                    continue;
                }
                positions.insert(name, merged.size());
            }
            merged.append(library);
        }
    };

    absorb(parentLibraries);
    absorb(childLibraries);

    QJsonArray out;
    for (const QJsonObject &library : merged) {
        out.append(library);
    }
    return out;
}

/// 合并 `arguments`：jvm / game 两个数组各自"父在前子在后"，其余键按"子优先"。
QJsonObject mergeArguments(const QJsonObject &parentArguments, const QJsonObject &childArguments) {
    QJsonObject out;
    for (auto it = parentArguments.constBegin(); it != parentArguments.constEnd(); ++it) {
        if (it.key() != QLatin1String("jvm") && it.key() != QLatin1String("game")) {
            out.insert(it.key(), it.value());
        }
    }
    for (auto it = childArguments.constBegin(); it != childArguments.constEnd(); ++it) {
        if (it.key() == QLatin1String("jvm") || it.key() == QLatin1String("game")) {
            continue;
        }
        if (it.value().isNull() || it.value().isUndefined()) {
            continue;
        }
        out.insert(it.key(), it.value());
    }
    for (const char *key : {"jvm", "game"}) {
        const QJsonArray array = concatArrays(parentArguments.value(QLatin1String(key)).toArray(),
                                              childArguments.value(QLatin1String(key)).toArray());
        if (!array.isEmpty()) {
            out.insert(QLatin1String(key), array);
        }
    }
    return out;
}

/// 两个数里取大的那个；两边都不是数时返回未定义的值（不往结果里塞东西）。
QJsonValue largerNumber(const QJsonObject &parent, const QJsonObject &child, const QString &key) {
    const QJsonValue parentValue = parent.value(key);
    const QJsonValue childValue = child.value(key);
    const bool parentIsNumber = parentValue.isDouble();
    const bool childIsNumber = childValue.isDouble();
    if (parentIsNumber && childIsNumber) {
        return qMax(parentValue.toInt(), childValue.toInt());
    }
    if (childIsNumber) {
        return childValue;
    }
    if (parentIsNumber) {
        return parentValue;
    }
    return QJsonValue(QJsonValue::Undefined);
}

}  // namespace

QString parentIdOf(const QJsonObject &json) {
    const QJsonValue value = json.value(QStringLiteral("inheritsFrom"));
    return value.isString() ? value.toString() : QString();
}

int maxDepth() {
    // 真实世界里的链最多两层（加载器 → 原版），16 是"离谱但还没到病态"的位置。
    return 16;
}

QJsonObject merge(const QJsonObject &child, const QJsonObject &parent) {
    QJsonObject out = parent;

    // 先把子的字段盖上去。这一步的默认语义就是"子优先"，逐字段的特例在下面单独处理 ——
    // 正是因为默认是子优先，以后 Mojang 加一个新字段时不需要动这里，而"新字段被父的
    // 旧值顶掉"这种错也不会出现。
    for (auto it = child.constBegin(); it != child.constEnd(); ++it) {
        const QString key = it.key();
        // inheritsFrom 刻意不带过去，理由见 .hpp。
        if (key == QLatin1String("inheritsFrom")) {
            continue;
        }
        // 下面几个各有各的合并语义，交给它们自己处理。
        if (key == QLatin1String("libraries") || key == QLatin1String("arguments") ||
            key == QLatin1String("complianceLevel") ||
            key == QLatin1String("minimumLauncherVersion")) {
            continue;
        }
        // 子显式写了 null / 什么都没有：当作"没写"，父的值留着。
        if (it.value().isNull() || it.value().isUndefined()) {
            continue;
        }
        out.insert(key, it.value());
    }

    out.insert(QStringLiteral("libraries"),
               mergeLibraries(parent.value(QStringLiteral("libraries")).toArray(),
                              child.value(QStringLiteral("libraries")).toArray()));

    const QJsonObject arguments = mergeArguments(
        parent.value(QStringLiteral("arguments")).toObject(),
        child.value(QStringLiteral("arguments")).toObject());
    if (!arguments.isEmpty() || parent.contains(QStringLiteral("arguments")) ||
        child.contains(QStringLiteral("arguments"))) {
        out.insert(QStringLiteral("arguments"), arguments);
    }

    // 取大的：这两个字段的意思是"至少要满足谁的要求"，取小的等于放行一个我们知道
    // 不满足的启动器。加载器版本的 complianceLevel 通常比原版高（它要求的东西更多）。
    for (const char *key : {"complianceLevel", "minimumLauncherVersion"}) {
        const QJsonValue value = largerNumber(parent, child, QLatin1String(key));
        if (!value.isUndefined()) {
            out.insert(QLatin1String(key), value);
        }
    }

    return out;
}

}  // namespace VersionInheritance
