/**
 * @file LibraryRules.hpp
 * @brief version.json 中 library 的平台规则判定与 natives classifier 解析。
 * @note DownloadManager 选 natives classifier、LauncherManager 拼 classpath
 * 都要用同一套平台规则，所以提取到这里，避免两处语义各自漂移。
 */
#pragma once

#include <QDebug>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1String>
#include <QOperatingSystemVersion>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QSysInfo>

namespace LibraryRules {

/**
 * @brief 当前平台在 version.json 里使用的名字。
 * @return "windows" / "osx" / "linux"，无法判定时按 "windows" 处理。
 */
inline QString platformName() {
    const QString product = QSysInfo::productType().toLower();
    if (product.contains("windows")) {
        return QStringLiteral("windows");
    }
    if (product.contains("mac") || product.contains("osx") || product.contains("darwin")) {
        return QStringLiteral("osx");
    }
    if (product.contains("linux")) {
        return QStringLiteral("linux");
    }
    return QStringLiteral("windows");
}

/**
 * @brief 当前 CPU 架构在 version.json 里使用的名字。
 * @return "x86_64" / "x86" / "arm64"，无法识别时返回**已经转成小写**的那个原始值
 * （QSysInfo 在各平台本来就返回小写，所以这一支今天看不出差别）。
 */
inline QString platformArch() {
    const QString arch = QSysInfo::currentCpuArchitecture().toLower();
    if (arch == QLatin1String("amd64") || arch == QLatin1String("x86_64")) {
        return QStringLiteral("x86_64");
    }
    if (arch == QLatin1String("i386") || arch == QLatin1String("i686") ||
        arch == QLatin1String("x86")) {
        return QStringLiteral("x86");
    }
    if (arch == QLatin1String("aarch64") || arch == QLatin1String("arm64")) {
        return QStringLiteral("arm64");
    }
    return arch;
}

/**
 * @brief 当前系统的版本号，语义对齐 Java 的 os.version 属性。
 * @return 形如 "10.0" 的主次版本号。
 *
 * NOTE: 必须用 major.minor，不能用 QSysInfo::productVersion()。后者在 Windows 11 上
 * 返回 "11"，而 Java 的 os.version 在 Win10 和 Win11 上都是 "10.0" —— version.json
 * 里 Mojang 的规则也正是按 "^10\." 匹配（1.18.2/1.19 的 -Dos.name=Windows 10 就挂在
 * 这条规则上）。用 productVersion() 会让这条规则在 Win11 上静默失效。
 */
inline QString osVersion() {
    const QOperatingSystemVersion current = QOperatingSystemVersion::current();
    return QStringLiteral("%1.%2").arg(current.majorVersion()).arg(current.minorVersion());
}

/**
 * @brief 按 pattern 串缓存的 `QRegularExpression`（给 `ruleMatches()` 里那条 `os.version`
 * 规则用）。
 */
inline QRegularExpression cachedOsVersionPattern(const QString &pattern) {
    static thread_local QHash<QString, QRegularExpression> cache;
    const auto it = cache.constFind(pattern);
    if (it != cache.constEnd()) {
        return it.value();
    }
    return *cache.insert(pattern, QRegularExpression(pattern));
}

/**
 * @brief 判断单条 rule 是否命中当前环境。
 * @param rule version.json 里的一条 rule 对象。
 * @param features 已启用的 feature 集合，启动器目前不启用任何 feature，传空即可。
 * @return 若该 rule 适用于当前环境，则返回 true.
 */
inline bool ruleMatches(const QJsonObject &rule, const QSet<QString> &features = QSet<QString>()) {
    const QJsonObject osObj = rule.value(QStringLiteral("os")).toObject();
    if (!osObj.isEmpty()) {
        const QString name = osObj.value(QStringLiteral("name")).toString().toLower();
        if (!name.isEmpty() && name != platformName()) {
            return false;
        }
        const QString arch = osObj.value(QStringLiteral("arch")).toString().toLower();
        if (!arch.isEmpty() && arch != platformArch()) {
            return false;
        }
        const QString version = osObj.value(QStringLiteral("version")).toString();
        if (!version.isEmpty() && !cachedOsVersionPattern(version).match(osVersion()).hasMatch()) {
            return false;
        }
    }

    const QJsonObject featureObj = rule.value(QStringLiteral("features")).toObject();
    for (auto it = featureObj.constBegin(); it != featureObj.constEnd(); ++it) {
        if (it.value().toBool() != features.contains(it.key())) {
            return false;
        }
    }
    return true;
}

/**
 * @brief 按 rules 数组判断一个 library 是否适用于当前平台。
 * @param libObj version.json 里的一个 library 对象。
 * @param features 已启用的 feature 集合。
 * @return 若该 library 可用则返回 true.
 *
 * 判据：**没有 rules 这个键**（或者它压根不是数组）就一律允许；有 rules 时按顺序处理命中
 * 的条目，allow 置为允许、disallow/deny 置为禁止，未命中任何 allow 则视为不允许 ——
 * 这一段和 Mojang 那套（起手 lastAction = DISALLOW，逐条命中的覆盖它）逐字一致。
 *
 * NOTE: **唯一一处和 Mojang 不一样**：`"rules": []`（键在、但是个空数组）这里算**允许**，
 * 官方那套算**不允许**（它没有"空数组就放行"这条早退）。这条注释以前写的是笼统的
 * "语义与 Mojang 一致"，把这个差别盖过去了。
 * 为什么朝着"允许"这边偏：这两种解释在现实中几乎不会碰上（真实版本 json 里没见过
 * 空数组），而万一碰上，两种错的代价差得很远 —— 多带一个库上 classpath 顶多是重复，
 * 少一个库是游戏起来时一句 NoClassDefFoundError（这个项目已经为这一类症状查过好几次）。
 * 哪天要改成和官方严格一致，判据就是"键在不在"（contains），而不是数组空不空。
 */
inline bool libraryAllowed(const QJsonObject &libObj,
                           const QSet<QString> &features = QSet<QString>()) {
    const QJsonArray rules = libObj.value(QStringLiteral("rules")).toArray();
    if (rules.isEmpty()) {
        return true;
    }

    bool allowed = false;
    for (const QJsonValue &ruleValue : rules) {
        const QJsonObject ruleObj = ruleValue.toObject();
        const QString action = ruleObj.value(QStringLiteral("action")).toString();
        if (action.isEmpty() || !ruleMatches(ruleObj, features)) {
            continue;
        }
        if (action == QLatin1String("allow")) {
            allowed = true;
        } else if (action == QLatin1String("disallow") || action == QLatin1String("deny")) {
            allowed = false;
        }
    }
    return allowed;
}

/**
 * @brief 把 maven 坐标（library 的 `name`）翻译成它在 maven 仓库里的相对路径。
 * @param name 形如 `group:artifact:version`，可带 `:classifier`，末段可带 `@扩展名`。
 * @return 形如 `group/with/slashes/artifact/version/artifact-version[-classifier].jar`；
 * 解析不出来（字段不够、有任何一段不干净）时返回**空串**。
 *
 * 什么时候用得上：**库对象里只有 `name` 和 `url`、没有 `downloads` 那一块**的时候。
 * 带模组加载器的版本 json 一律长这样 —— Fabric 的 profile json 里 8 个库全都没有
 * `downloads.artifact`，只有 maven 坐标和一个仓库地址。这是 maven 那套约定，
 * 加载器生态（Forge/NeoForge 的 maven、Fabric 的 maven）都按它走。
 *
 * NOTE: 每一段都按 `[A-Za-z0-9._+-]+` 白名单过滤，**任何一段不符合就整体返回空**。
 * 这不是"防御性编程"的姿态：这个字符串来自**下载来的 JSON**，而它的用途是拼
 * 文件路径，`../../` 这种 group 会让库被写到 libraries/ 外面去。宁可这个库下不下来
 * （调用方会跳过它、日志里看得见），也不要往任意路径写文件。
 * NOTE: 白名单里必须有 `+`。maven 的版本号本来就能带它，而 Fabric 真的在用：
 * `net.fabricmc:sponge-mixin:0.17.4+mixin.0.8.7`（jar 的名字里也带 `+`，仓库那边
 * 就是这么存的）。漏了它这一个库就静默消失，症状还是那句 NoClassDefFoundError。
 * 反过来说，Windows 上不能进文件名的字符（`<>:"/\|?*`）一个都不在白名单里。
 * NOTE: **classifier 是可选的**（`group:artifact:version` 三段最常见，Fabric 给的 8 个
 * 坐标里有 7 个就是三段）—— 所以它不能进上面那个"任何一段为空就拒绝"的循环，
 * 否则每一个不带 classifier 的坐标都会解析失败。这个坑很容易再踩一次。
 * NOTE: `@zip` 这类扩展名照收（maven 的坐标本来就允许），classifier 段最多认一个，
 * 第 5 段往后直接**当作不存在**：maven 没有那种形式，拿它拼只会得到错路径，
 * 而"错路径"在下载时会 404、界面报错，比"静默少一个库、游戏起来时
 * NoClassDefFoundError"更容易查。
 */
inline QString mavenPathForName(const QString &name) {
    QString coordinate = name;
    QString extension = QStringLiteral("jar");
    const int at = coordinate.lastIndexOf(QLatin1Char('@'));
    if (at > 0) {
        extension = coordinate.mid(at + 1);
        coordinate = coordinate.left(at);
    }

    const QStringList parts = coordinate.split(QLatin1Char(':'), Qt::KeepEmptyParts);
    if (parts.size() < 3) {
        return QString();
    }
    const QString group = parts.at(0);
    const QString artifact = parts.at(1);
    const QString version = parts.at(2);
    const QString classifier = parts.size() > 3 ? parts.at(3) : QString();

    // 字符白名单：允许 `.` 是因为它要做**段内**的分隔（组名的层级、版本号里的点）。
    static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9._+-]+$"));
    // 不写捕获列表：`safe` 是 static（静态存储期），lambda 直接就能看见它 —— 写 `[&safe]`
    // 反而会让 gcc 报一条「capture of variable with non-automatic storage duration」。
    const auto isSafeSegment = [](const QString &segment) {
        if (segment.isEmpty() || !safe.match(segment).hasMatch()) {
            return false;
        }
        return segment != QLatin1String(".") && segment != QLatin1String("..");
    };
    for (const QString &segment : {group, artifact, version, extension}) {
        if (!isSafeSegment(segment)) {
            return QString();
        }
    }
    // classifier 允许为空（等于没有），但给了就必须干净
    if (!classifier.isEmpty() && !isSafeSegment(classifier)) {
        return QString();
    }

    QString groupPath = group;
    groupPath.replace(QLatin1Char('.'), QLatin1Char('/'));

    const QString fileName = artifact + QLatin1Char('-') + version +
                             (classifier.isEmpty() ? QString()
                                                   : QLatin1Char('-') + classifier) +
                             QLatin1Char('.') + extension;
    return QStringLiteral("%1/%2/%3/%4").arg(groupPath, artifact, version, fileName);
}

/**
 * @brief 一条"从下载来的 JSON 里读出来、要拼到 `libraries/` 底下的相对路径"安不安全。
 *
 * 这就是 `mavenPathForName()` 那一套白名单，用在**另一个入口**上：`downloads.artifact.path`
 * 和 `downloads.classifiers.<k>.path` 这两个字段是**原样收下、原样拼**的（见 artifactFor()
 * 和 nativeClassifier()），而它们和 maven 那个名字是同一个来路 —— 下载来的 JSON。
 * 那个白名单的理由（:161 那段）对它一字不差成立：恶意/写坏的 version.json 写一句
 * `"path": "../../../x.jar"`，库就落到 `libraries/` 外面去了（Windows 上那就是一个
 * DLL 劫持面）。所以同一把尺子量到底。
 *
 * NOTE: **实测过真实数据**：本机 `E:\MC\.minecraft\versions` 下 66 份
 * version.json、6596 条 path（872 条去重，含 Forge/NeoForge/OptiFine/Fabric 装出来的
 * 那些）逐段喂进来，**一条都过不了白名单的都没有**；出现过的字符只有
 * `+ - . / 0-9 F J M S _ a-z`。所以这条判据不会把任何一个正经库挡在外面。
 *
 * NOTE: 判据比 maven 那条多两样（那边是逐段拼、这里的字符串是整条路径）：
 * - **反斜杠一律拒** —— 它按 `/` 切是一整段，逐段看起来完全正常（`..\..\x.jar`），
 * 而 Windows 那边照样当分隔符用；
 * - **冒号一律拒** —— 盘符（`C:...`）和 NTFS 的备用数据流（`a.jar:x`）都是靠它。
 */
inline bool libraryRelativePathIsSafe(const QString &path) {
    if (path.isEmpty()) {
        return false;
    }
    if (path.contains(QLatin1Char('\\')) || path.contains(QLatin1Char(':')) ||
        path.startsWith(QLatin1Char('/'))) {
        return false;
    }
    // 同上（mavenPathForName() 那一处 ★）：常量尺子只构造一次。
    static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9._+-]+$"));
    const QStringList segments = path.split(QLatin1Char('/'));
    for (const QString &segment : segments) {
        // 空段 = `a//b`；`.` / `..` 单独判 —— 它俩**能过**上面那个字符集（和
        // mavenPathForName() 里那一条同一个理由）。
        if (segment.isEmpty() || segment == QLatin1String(".") ||
            segment == QLatin1String("..")) {
            return false;
        }
        if (!safe.match(segment).hasMatch()) {
            return false;
        }
    }
    return true;
}

/**
 * @brief 一个 library 的主 artifact（就是要进 classpath 的那个 jar）。
 */
struct Artifact {
    QString path;   ///< libraries/ 下的相对路径
    QString url;    ///< 下载地址
    QString sha1;   ///< 期望的 SHA1，提供方没给时为空
    qint64 size = 0;
    /// 只有路径、**一个下载地址都没有**的库。OptiFine 的安装器写出来的两个库就是这样：它
    /// 把 jar 直接放进 libraries/，json 里只留一个 maven 坐标（见 LoaderMeta.hpp 里 OptiFine
    /// 那一节）。这种库不该去下（没有地址），但**必须进 classpath**。
    bool localOnly = false;

    /// 下载列表用：有地址才下得动。
    bool valid() const { return !path.isEmpty() && !url.isEmpty(); }

    /// classpath 用：只要解析出了路径就得算它一份 —— localOnly 的那几个正是靠这一条才上得去。
    bool hasPath() const { return !path.isEmpty(); }
};

/**
 * @brief 解析一个 library 的主 artifact，**兼容两种 json 写法**。
 * @param libObj version.json 里的一个 library 对象。
 * @return 路径/地址至少有一个解析不出来时返回一个 invalid 结构。
 *
 * 三种写法：
 * 1. `downloads.artifact` 里给全了 path/url/sha1/size（Mojang 的原版 json 都是这样）；
 * 2. **只有 `name`（maven 坐标）和 `url`（仓库地址）**。带模组加载器的版本 json 一律
 * 长这样 —— Fabric 的 profile json 里 8 个库全都没有 `downloads` 那一块；
 * 3. **只有 `name`，连 `url` 都没有**（见下面那条）：jar 是别人已经放好的，不下只挂。
 *
 * 第 2 种照"只认 downloads.artifact"的判据会被**静默跳过**，症状是加载器装好了、
 * 启动时 NoClassDefFoundError 找不到 net.fabricmc.loader，而下载界面一路绿灯。
 *
 * NOTE: 路径按 maven 的规矩从名字拼，而那个名字来自**下载来的 JSON**、用途是拼文件路径，
 * `mavenPathForName()` 的逐段白名单正是为此（`../../` 这种 group 会让库被写到
 * libraries/ 外面去）。拼不出来时如果仓库地址是有的，**必须吭声** —— 不然就是
 * "库静默少了一个"，只有游戏起来时才炸。
 */
inline Artifact artifactFor(const QJsonObject &libObj) {
    Artifact result;

    const QJsonObject artifact = libObj.value(QStringLiteral("downloads"))
                                     .toObject()
                                     .value(QStringLiteral("artifact"))
                                     .toObject();
    result.path = artifact.value(QStringLiteral("path")).toString();
    result.url = artifact.value(QStringLiteral("url")).toString();
    result.sha1 = artifact.value(QStringLiteral("sha1")).toString();
    result.size = artifact.value(QStringLiteral("size")).toVariant().toLongLong();

    // 这个 path 是**原样收下、原样拼**的（:233 那句就是它），所以先过一遍白名单。
    // NOTE()：这里原来**没有**这道闸 —— 白名单只做在下面那条"没有 downloads
    // 块、按 maven 名字拼"的回退路上，而两条路的字符串是同一个来路、同一个用途。
    // 给一句 `"path":"../../../x.jar"` 就够了，库会落到 libraries/ 外面。
    // 拒掉之后**不算完**：把 path 清掉，让下面那条 maven 回退路照样有机会把这个库
    // 装对（名字干净的话它拼出来的路径是安全的）；名字也拼不出来才真的少一个库 ——
    // 那种情况上面已经 qWarning 了，不会静默。
    if (!result.path.isEmpty() && !libraryRelativePathIsSafe(result.path)) {
        qWarning() << "Refusing an unsafe library path from the version json:" << result.path
                   << "for" << libObj.value(QStringLiteral("name")).toString();
        result.path.clear();
    }

    if (!result.path.isEmpty() && !result.url.isEmpty()) {
        return result;
    }

    const QString mavenBase = libObj.value(QStringLiteral("url")).toString();
    const QString mavenName = libObj.value(QStringLiteral("name")).toString();
    const QString mavenPath = mavenPathForName(mavenName);
    if (!mavenPath.isEmpty() && !mavenBase.isEmpty()) {
        result.path = mavenPath;
        result.url = mavenBase.endsWith(QLatin1Char('/'))
                         ? mavenBase + mavenPath
                         : mavenBase + QLatin1Char('/') + mavenPath;
        // 哈希和大小取库里**顶层**的 sha1/size：Fabric 给了前 6 个库，两个都给了。
        // 后两个（intermediary / fabric-loader）本来就没有，空 sha1 就是不校验。
        result.sha1 = libObj.value(QStringLiteral("sha1")).toString();
        result.size = libObj.value(QStringLiteral("size")).toVariant().toLongLong();
    } else if (!mavenPath.isEmpty()) {
        // 第 3 种写法：**只有 name，连 url 都没有**。实测 OptiFine 的安装器写出来的就是
        // 这样（两个库：它自己 + launchwrapper-of），因为那两个 jar 是它**自己**放进
        // libraries/ 的，根本没有仓库地址可写。
        //
        // 这种库**必须进 classpath**：少了它，启动时主类 net.minecraft.launchwrapper.Launch
        // 就找不到（NoClassDefFoundError），而"装"的那一步一切正常 —— 这种错最难查。
        // 路径照 maven 的规矩拼，拼不出来（名字脏）的仍然是 invalid，不猜。
        result.path = mavenPath;
        result.localOnly = true;
        result.sha1 = libObj.value(QStringLiteral("sha1")).toString();
        result.size = libObj.value(QStringLiteral("size")).toVariant().toLongLong();
    } else if (!mavenBase.isEmpty()) {
        qWarning() << "Cannot derive a maven path from library name:" << mavenName;
    }
    return result;
}

/**
 * @brief 某个 library 在当前平台需要解压的 natives classifier。
 */
struct NativeClassifier {
    QString path;          ///< libraries/ 下的相对路径
    QString url;           ///< 下载地址
    QString sha1;          ///< 期望的 SHA1
    QStringList excludes;  ///< 来自 library 的 extract.exclude，例如 META-INF/

    bool valid() const { return !path.isEmpty() && !url.isEmpty(); }
};

/**
 * @brief 解析某个 library 在当前平台需要解压的 natives classifier。
 * @param libObj version.json 里的一个 library 对象。
 * @return 若该库不适用于当前平台、或没有 natives 字段，则返回一个 invalid 结构。
 *
 * NOTE: 1.19 起 Mojang 把 classifier 直接写进 name
 * （org.lwjgl:lwjgl:3.3.1:natives-windows）并走普通的 downloads.artifact，
 * 由 LWJGL 从 classpath 上的 jar 自己提取，因此那些库没有 natives 字段 ——
 * 走到这里会直接返回空，既不会重复下载也不会重复解压。
 */
inline NativeClassifier nativeClassifier(const QJsonObject &libObj) {
    NativeClassifier result;

    const QJsonObject natives = libObj.value(QStringLiteral("natives")).toObject();
    if (natives.isEmpty()) {
        return result;
    }

    // 必须先过平台规则：1.12.2 的 lwjgl-platform:2.9.2-nightly-20140822 在 Windows 上
    // 被 rules 禁止，但它的 natives 里照样有 windows key。不过滤就会把同名 DLL
    // （lwjgl.dll / OpenAL64.dll）和 2.9.4 解压到同一目录互相覆盖，得到不确定的版本。
    if (!libraryAllowed(libObj)) {
        return result;
    }

    QString classifierKey = natives.value(platformName()).toString();
    if (classifierKey.isEmpty()) {
        return result;
    }
    // 少数更老的版本会用 natives-windows-${arch} 这种占位符写法。
    // 实测 1.18.2 / 1.19 / 1.12.2 都不需要，这里留着以防更早的版本。
    if (classifierKey.contains(QLatin1String("${arch}"))) {
        classifierKey.replace(QLatin1String("${arch}"),
                              platformArch() == QLatin1String("x86") ? QStringLiteral("32")
                                                                     : QStringLiteral("64"));
    }

    const QJsonObject downloads = libObj.value(QStringLiteral("downloads")).toObject();
    const QJsonObject classifier = downloads.value(QStringLiteral("classifiers"))
                                       .toObject()
                                       .value(classifierKey)
                                       .toObject();
    result.path = classifier.value(QStringLiteral("path")).toString();
    result.url = classifier.value(QStringLiteral("url")).toString();
    result.sha1 = classifier.value(QStringLiteral("sha1")).toString();

    // 和 artifactFor() 里那一处同一个理由、同一把尺子：这条 path 也是原样收下、原样拼的
    // （落盘目标 + 解压来源）。不安全就清掉 —— 调用方按"这个库没有 natives"跳过它，
    // 而不是把它写到 libraries/ 外面去。
    if (!result.path.isEmpty() && !libraryRelativePathIsSafe(result.path)) {
        qWarning() << "Refusing an unsafe natives path from the version json:" << result.path
                   << "for" << libObj.value(QStringLiteral("name")).toString();
        result.path.clear();
    }

    const QJsonArray excludes = libObj.value(QStringLiteral("extract"))
                                    .toObject()
                                    .value(QStringLiteral("exclude"))
                                    .toArray();
    for (const QJsonValue &exclude : excludes) {
        result.excludes << exclude.toString();
    }
    return result;
}

}  // namespace LibraryRules
