/**
 * @file InstanceFiles.cpp
 * @brief Implements the instance-directory criterion and jar/json name resolution.
 * @author 0x7fffff
 * @date 2026-09-21
 * @note 判据的来龙去脉写在 .hpp 的块注释里；这里只有实现，几处 NOTE 说的是"为什么不能
 *       照直觉改"。
 */

#include "InstanceFiles.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLatin1Char>
#include <QSet>

#include "../launcher/VersionInheritance.hpp"
#include "../util/AppPaths.hpp"

namespace InstanceFiles {
namespace {

/// 在清单里找一个名字，**不区分大小写**，返回它在磁盘上的**真实**拼写（找不到返回空串）。
///
/// 为什么不区分大小写：Windows 的文件系统本来就不区分。用户把目录从 `MyPack` 改成 `mypack`
/// 之后，里面的 `MyPack.jar` 是**同一个文件**，用区分大小写的比较会认不出来。返回值必须是
/// 磁盘上那个真实拼写 —— 它要拿去拼路径。
QString findNamed(const QStringList &names, const QString &wanted) {
    for (const QString &name : names) {
        if (name.compare(wanted, Qt::CaseInsensitive) == 0) {
            return name;
        }
    }
    return QString();
}

/// 名字的长度上限。见 .hpp 里 validateDirName() 的说明。
constexpr int kMaxDirNameLength = 64;

/// 读一个实例目录里的版本 JSON，取它声明的父版本（`inheritsFrom`）。
///
/// 读不到、读不懂、字段不是字符串，一律返回空串 —— 也就是"这不是一个继承型实例"。
/// **刻意不告警**：这条路径只在"一个 jar 都认不出来"时才会走到，而那种目录里
/// 绝大多数本来就是下载残骸（真机上还有 0 字节的空目录），一句告警换不来任何有用信息，
/// 只会把"认不出来的目录"这件事从安静变成刷屏。真有版本的目录在别处有它自己的说法
/// （installedVersions 的告警、启动时的"版本信息缺失"）。
QString readInheritsFrom(const QString &versionId, const QString &jsonName) {
    const QString name = jsonName.isEmpty() ? versionId + QStringLiteral(".json") : jsonName;
    QFile file(AppPaths::versionDir(versionId) + QLatin1Char('/') + name);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return QString();
    }
    return VersionInheritance::parentIdOf(doc.object());
}

/// Windows 保留设备名（CON / PRN / AUX / NUL / COM1-9 / LPT1-9）。
/// 判的是**主名**：`CON` 建不出来，`CON.txt` 一样建不出来。大小写不敏感。
bool isReservedDeviceName(const QString &stem) {
    const QString upper = stem.toUpper();
    if (upper == QLatin1String("CON") || upper == QLatin1String("PRN") ||
        upper == QLatin1String("AUX") || upper == QLatin1String("NUL")) {
        return true;
    }
    return upper.size() == 4 &&
           (upper.startsWith(QLatin1String("COM")) || upper.startsWith(QLatin1String("LPT"))) &&
           upper.at(3) >= QLatin1Char('1') && upper.at(3) <= QLatin1Char('9');
}

}  // namespace

QString validateDirName(const QString &rawName) {
    const QString name = rawName.trimmed();

    if (name.isEmpty()) {
        return QStringLiteral("名字不能是空的。");
    }

    // `/` `\` 排在最前面判，理由写在 .hpp 里：它们同时是路径分隔符，放过去就是
    // 路径穿越 —— `../别的目录` 这种名字会让后面的 QDir::rename 跑到 versions/ 外面。
    // 其余几个是 Windows 文件名的既定禁区，不挡的话会在 rename 那一刻才失败。
    // 0x00-0x1F（控制字符）单列一条：名字里夹一个换行或制表符，界面上看起来是空的，
    // 但这种名字在盘上确实建得出来，之后就再也说不清那是哪个实例了。
    for (const QChar ch : name) {
        if (ch.unicode() < 0x20 || QStringLiteral("<>:\"/\\|?*").contains(ch)) {
            return QStringLiteral(
                "名字里不能有 < > : \" / \\ | ? * 这些字符，也不能有制表符、换行这类控制字符。");
        }
    }

    if (name == QLatin1String(".") || name == QLatin1String("..")) {
        return QStringLiteral("“.” 和 “..” 是目录自己的名字，不能拿来用。");
    }

    // Windows 会**静默**删掉结尾的空格和点：输入 `abc ` 建出来就是 `abc`。静默是关键 ——
    // 用户看到的目录名和他刚输的不一样，然后就在列表里找不到自己的实例了。
    //
    // NOTE: 这里只判**点**。尾随空格已经被上面的 trim() 吃掉了（用户就是多敲了个空格，
    //       那不是错误，替他规整掉正是他想要的），所以"以空格结尾"这一条不可能被触发 ——
    //       写了也是死代码。点不一样：trim 不管它，真交给 Windows 就会被悄悄吃掉，
    //       所以我们自己拦下来，把决定权交回用户。
    if (name.endsWith(QLatin1Char('.'))) {
        return QStringLiteral("名字不能以点结尾（Windows 会把它悄悄去掉）。");
    }

    if (name.length() > kMaxDirNameLength) {
        return QStringLiteral("名字太长了，最多 %1 个字符（现在 %2 个）。")
            .arg(kMaxDirNameLength)
            .arg(name.length());
    }

    // 主名 = 第一个点之前的那一段。用 section 而不是 QFileInfo::baseName()：
    // 后者对 `CON.txt` 给 `CON`、对没有点的 `CON` 也给 `CON`，两者都对这里合适，
    // 但它还会做别的规整（比如把 `..x` 当成扩展名），没必要。
    if (isReservedDeviceName(name.section(QLatin1Char('.'), 0, 0))) {
        return QStringLiteral(
            "“%1”是 Windows 的保留设备名（CON/PRN/AUX/NUL/COM1-9/LPT1-9），建不出目录来。")
            .arg(name);
    }

    return QString();
}

QString validateRenameTarget(const QString &rawName, const QString &currentId) {
    const QString shapeProblem = validateDirName(rawName);
    if (!shapeProblem.isEmpty()) {
        return shapeProblem;
    }

    const QString name = rawName.trimmed();
    if (name == currentId) {
        return QStringLiteral("新名字和现在一样，没有改动的必要。");
    }

    // 重名比较不区分大小写，理由见 .hpp。`entry` 用磁盘上的真实拼写，
    // 那句提示要指得出到底撞上了哪一个。
    const QStringList entries =
        QDir(AppPaths::versionsDir()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        if (entry.compare(name, Qt::CaseInsensitive) == 0 &&
            entry.compare(currentId, Qt::CaseInsensitive) != 0) {
            return QStringLiteral("已经有一个叫“%1”的实例了，换一个名字。").arg(entry);
        }
    }

    return QString();
}

Resolved resolve(const DirListing &listing) {
    Resolved out;
    out.jarCandidates = listing.jarNames;

    // ① 精确匹配（<目录名>.jar）优先。绝大多数实例走这条，而且它不依赖扫描顺序。
    // ② 否则顶层恰好一个 jar 就是它 —— 这一条正是本轮放松判据要救的那类实例：
    //    用户自己在资源管理器里改了目录名，没改里面的 jar 名。
    // ③ 其余（一个都没有 / 两个以上且没有精确匹配）都不算实例。
    out.jarName = findNamed(listing.jarNames, listing.dirName + QStringLiteral(".jar"));
    if (out.jarName.isEmpty() && listing.jarNames.size() == 1) {
        out.jarName = listing.jarNames.first();
    }

    // json 按同样的 ①/② 解析，但**解析不到不算认不出来**：
    //   1. installedVersions() 的语义始终是"客户端 jar 在不在"（半个版本、只有 json 的下载
    //      残骸都不是实例），把 json 纳入判据会把"下到一半"的目录从"看得见、点了会告诉你
    //      为什么"变成"看不见" —— 正是本轮要消灭的那种静默。
    //   2. json 缺席有它自己现成的错误路径：启动时会弹"版本信息缺失，请先重新下载"。
    out.jsonName = findNamed(listing.jsonNames, listing.dirName + QStringLiteral(".json"));
    if (out.jsonName.isEmpty() && listing.jsonNames.size() == 1) {
        out.jsonName = listing.jsonNames.first();
    }

    // ④ 一个 jar 都没有、但 json 声明了父版本 → 认得出。带模组加载器的版本就长这样
    //    （Fabric 的目录里只有一份 json，主 jar 在原版那边）。
    //
    // NOTE: 这条**不吃上面那两条的失败**：顶层有两个 jar、又没同名的那一个时，
    //       jarNames 非空、认不出 jar，但 json 声明了父版本的话也算数 —— 那两个 jar
    //       是垃圾（用户丢进来的、装别的加载器剩下的），客户端 jar 反正在父版本那边，
    //       按 ①/② 挑只会挑错。判据是"声明了父版本"，不是"有没有多余的 jar"。
    out.inheritsFrom = listing.inheritsFrom;

    return out;
}

DirListing scanDir(const QString &versionId) {
    DirListing out;
    out.dirName = versionId;

    const QDir dir(AppPaths::versionDir(versionId));
    if (!dir.exists()) {
        return out;
    }

    // NOTE: 必须**非递归** —— QDir::Files 不能配上 QDir::Subdirectories。
    //       实例目录底下还有 libraries/：下载时把整个版本目录当目的地，库 jar 全写在
    //       versions/<id>/libraries/ 里，真机上一个整合包随手就是几百个。递归扫会把它们
    //       统统当成客户端 jar 的候选，于是"顶层恰好一个 jar"这条判据立刻失效、
    //       所有实例反而认不出来了。别顺手加上 Subdirectories。
    out.jarNames = dir.entryList(QStringList() << QStringLiteral("*.jar"), QDir::Files);
    out.jsonNames = dir.entryList(QStringList() << QStringLiteral("*.json"), QDir::Files);
    return out;
}

Resolved resolveInstance(const QString &versionId) {
    const DirListing listing = scanDir(versionId);
    Resolved out = resolve(listing);
    if (!out.recognized()) {
        // 只有"一个 jar 都认不出来"时才去读 json —— 这一条是热路径（每个已安装版本、
        // 每次刷新界面都要过一遍，真机上 61 个实例），绝大多数实例在自己目录里就认出了
        // jar，于是这里一次磁盘读取都不会发生，行为与改动前逐字节相同。
        out.inheritsFrom = readInheritsFrom(versionId, out.jsonName);
    }
    return out;
}

QString jarOwnerId(const QString &versionId) {
    Resolved resolved = resolveInstance(versionId);
    if (!resolved.jarName.isEmpty()) {
        return versionId;
    }

    // 自己没有 jar：顺着 inheritsFrom 链往上找第一个"自己有 jar"的那一层。
    //
    // 两道闸都是必须的：手写/写错的 json 可以首尾相接成环（A 继承 B、B 继承 A），
    // 也可以串成一条很长的链。这里不报错，只退化 —— 报错的地方在别处（启动前检查会说
    // "缺少客户端 JAR"，VersionManager 会说"父版本没装"），这一层的职责只是给一个
    // 说得通的路径，让那些报错能指着正确的地方。
    QSet<QString> seen;
    seen.insert(versionId);
    QString last = versionId;
    for (int depth = 0; depth < VersionInheritance::maxDepth(); ++depth) {
        const QString parent = resolved.inheritsFrom;
        if (parent.isEmpty() || seen.contains(parent)) {
            break;
        }
        seen.insert(parent);
        last = parent;
        resolved = resolveInstance(parent);
        if (!resolved.jarName.isEmpty()) {
            return parent;
        }
    }
    // 整条链上都没有 jar：指向链上最后一层，报错时说的就是"1.20.1 的 jar 不在"，
    // 而不是把责任推给加载器那个目录（它的 json 本来就是不长 jar 的）。
    return last;
}

QString jarName(const QString &versionId) {
    const QString owner = jarOwnerId(versionId);
    const Resolved resolved = resolveInstance(owner);
    // 退回老名字。见 .hpp 的 NOTE —— 这不是"错误处理"，是让只想拿个名字去看看在不在的
    // 调用方在正常目录上逐字节不变。
    return resolved.recognized() && !resolved.jarName.isEmpty()
               ? resolved.jarName
               : owner + QStringLiteral(".jar");
}

QString jsonName(const QString &versionId) {
    const Resolved resolved = resolveInstance(versionId);
    return resolved.jsonName.isEmpty() ? versionId + QStringLiteral(".json") : resolved.jsonName;
}

QString jarPath(const QString &versionId) {
    return AppPaths::versionDir(jarOwnerId(versionId)) + QLatin1Char('/') + jarName(versionId);
}

QString jarRelativePath(const QString &versionId) {
    return QStringLiteral("versions/%1/%2").arg(jarOwnerId(versionId), jarName(versionId));
}

QString jsonPath(const QString &versionId) {
    return AppPaths::versionDir(versionId) + QLatin1Char('/') + jsonName(versionId);
}

}  // namespace InstanceFiles
