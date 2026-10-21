/**
 * @file VersionInheritance.cpp
 * @brief 实现 version.json 继承链合并。
 * @note 合并规则见 .hpp；这里只保留实现。
 */

#include "VersionInheritance.hpp"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QList>

namespace VersionInheritance {
namespace {

/// 合并 arguments 下的一个数组：**父的在前、子的在后**。
///
/// NOTE: 顺序不能反。JVM 对同一条可重复选项取**后写**的那个（`-cp`、`-D` 都是），
/// 而"加载器盖掉原版声明的参数"正是加载器留这些 `arguments.jvm` 的用意
/// （Fabric 就靠 `-DFabricMcEmu=` 那一行把原版主类告诉自己的 Knot）。
/// 反过来拼，加载器的参数会被原版盖掉，症状是加载器像没装一样。
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

/// / 一条库坐标里**不看版本号**的那几段：`group:artifact`（有 classifier 时再加上它）。
///
/// 名字的形状是 `group:artifact:version[:classifier][@ext]`，例如
/// `org.ow2.asm:asm:9.6`、`org.lwjgl:lwjgl:3.3.3:natives-windows`、
/// `net.minecraftforge:forge:1.20.1-47.4.10:universal@zip`。
/// 版本号是第 3 段；classifier 是第 4 段（如果有），它**要留着** —— 见 .hpp 的 NOTE。
QString coordinateKey(const QString &name) {
    QString body = name;
    const int at = body.indexOf(QLatin1Char('@'));
    if (at >= 0) {
        body = body.left(at);  // `@zip` / `@jar` 这种扩展名标记不参与坐标
    }
    const QStringList parts = body.split(QLatin1Char(':'));
    if (parts.size() < 3) {
        // 段数不够（`group:artifact` 这种没写全的）：整串拿来当键，只保证"一模一样的两条"
        // 会被合成一条，认不出的东西不做额外判断。
        return body;
    }
    const QString classifier = parts.size() > 3 ? QStringLiteral(":") + parts.at(3) : QString();
    return parts.at(0) + QLatin1Char(':') + parts.at(1) + classifier;
}

/// 一条库的 `rules` 数组原样序列化出来的那串字符（没有 rules 时是空串）。
///
/// 去重的键里**必须带上它**，不然会把"同一坐标、不同平台"的那一对合成一条 —— 那正是
/// 原版 json 表达"Windows 用 2.9.1、macOS 用 2.9.2-nightly"的手法：
/// 1.8.json 里 `org.lwjgl.lwjgl:lwjgl` 就有两条，
/// `2.9.1` rules = [allow, disallow/osx] ← Windows 要的那个
/// `2.9.2-…` rules = [allow/osx] ← macOS 要的那个
/// 只按坐标去重（后到的赢）会把 Windows 那份**换掉**，而它留下的那条在当前平台上
/// "没有一条规则命中 ⇒ 不允许"（`LibraryRules::libraryAllowed` 的语义），于是 lwjgl 从
/// classpath 上整个消失 —— 真机上 `1.8` 就是这个下场：
/// java.lang.NoClassDefFoundError: org/lwjgl/LWJGLException
/// （`20w14∞` 里那 9 对 LWJGL 3.2.1/3.2.2 是同一个形状。）
///
/// NOTE: 判据是"逐字相同"（只认字面，不去理解 `os`/`features` 里写了什么）。宁可**少合并**
/// 几条（退化成改动前的行为：两条都在 classpath 上），也不要按自己的理解去挑一条 ——
/// 平台的语义只有 LibraryRules 那一处说了算，这一层不重复实现一遍。
QString rulesFingerprint(const QJsonObject &library) {
    const QJsonValue rules = library.value(QStringLiteral("rules"));
    if (!rules.isArray()) {
        return QString();
    }
    return QString::fromUtf8(QJsonDocument(rules.toArray()).toJson(QJsonDocument::Compact));
}

// 合并 `arguments`：jvm / game 两个数组各自"父在前子在后"，其余键按"子优先"。
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

    // 父那份的 inheritsFrom **也要去掉**：下面那个循环只跳了**子**的，
    // 而 `out = parent` 已经把父那份的抄进来了 —— 于是三层链
    // （OptiFine → Forge → 原版这种真的存在：OptiFine 的 json 继承的是它打过补丁那一层）
    // 合完之后，结果里还留着一个 `inheritsFrom`（指着中间那一层），而 .hpp 里那句
    // "结果里刻意丢掉 inheritsFrom"就成了一句**看着像真话的假话**。
    //
    // 今天没有谁拿"有没有这个字段"当判据（VersionArguments 那条 NOTE 明确写了判据不是它，
    // VersionManager 那个循环也是从**父版本自己那份**里读下一层 id），所以它现在不现症状 ——
    // 但那正是这条不变式存在的意义：它是**写给以后读代码的人**的（"合并过的就没有
    // inheritsFrom，有就是没合过"）。让代码和说明一致，比让说明去迁就代码便宜。
    out.remove(QStringLiteral("inheritsFrom"));

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

QJsonArray dedupeLibraries(const QJsonArray &libraries) {
    QList<QJsonObject> merged;
    QHash<QString, int> positions;  // 坐标 + rules 的指纹 -> 在 merged 里的下标

    for (const QJsonValue &value : libraries) {
        const QJsonObject library = value.toObject();
        const QString name = library.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) {
            // 没有 name 的条目照收、也占一个位置（但谁都不会再去改它）。
            merged.append(library);
            continue;
        }
        // 键里带着 rules 的指纹（见 rulesFingerprint()）：只有"同坐标**且** rules 逐字相同"
        // 的两条才算同一个库。不同 rules 的同坐标条目是**给不同平台的**，一条都不能少。
        const QString key = coordinateKey(name) + QLatin1Char('\n') + rulesFingerprint(library);
        const auto existing = positions.constFind(key);
        if (existing != positions.constEnd()) {
            // 同一个坐标又来一条：**后到的赢**，但留在先到的那条的位置上
            // （位置那条的理由和 mergeLibraries() 里一样：classpath 上的先后决定
            // 谁盖住谁，覆盖项不该因此往后挪）。
            merged[existing.value()] = library;
            continue;
        }
        positions.insert(key, merged.size());
        merged.append(library);
    }

    QJsonArray out;
    for (const QJsonObject &library : merged) {
        out.append(library);
    }
    return out;
}

}  // namespace VersionInheritance
