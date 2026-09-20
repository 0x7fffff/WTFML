/**
 * @file LibraryRules.hpp
 * @brief version.json 中 library 的平台规则判定与 natives classifier 解析。
 * @author 0x7fffff
 * @date 2026-09
 * @note DownloadManager 选 natives classifier、LauncherManager 拼 classpath
 *       都要用同一套平台规则，所以提取到这里，避免两处语义各自漂移。
 */
#pragma once

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
 * @return "x86_64" / "x86" / "arm64"，无法识别时返回归一化前的原始值。
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
        if (!version.isEmpty() && !QRegularExpression(version).match(osVersion()).hasMatch()) {
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
 * 语义与 Mojang 一致：没有 rules 就一律允许；有 rules 时按顺序处理命中的条目，
 * allow 置为允许、disallow/deny 置为禁止，未命中任何 allow 则视为不允许。
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
