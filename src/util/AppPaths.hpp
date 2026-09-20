/**
 * @file AppPaths.hpp
 * @brief 集中定义启动器数据目录的路径，作为"我们用的是哪个 .minecraft"的唯一出处。
 * @author 0x7fffff
 * @date 2026-09-20
 * @note header-only：只有 inline 函数，不需要加 .cpp，也不用碰链接。
 *
 * 以前 `QCoreApplication::applicationDirPath() + "/.minecraft"` 这句拼接散在 5 个文件
 * 15 处，各自再往后缀 "/versions"、"/assets"、"/java"…… 后果不是今天就会炸，而是：
 *
 *   1. "切换 .minecraft 文件夹"这个功能没有落点。要让启动器改用另一个游戏目录，
 *      得改 15 处，漏一处就是"版本列表来自 A、下载写进 B"这种只在一半功能上生效的
 *      错位，而且两条路径长得几乎一样，看日志也未必看得出。
 *   2. 同一个目录被算了两遍。例如 startGame() 自己拼了一份 versions/<id> 的绝对路径
 *      给 ensureInitialOptions 用，而 --gameDir 传的却是相对路径、靠 QProcess 的工作
 *      目录去解析。两份值今天恰好相等，但"恰好"是靠调用顺序维持的，不是靠代码。
 *
 * NOTE: 这里刻意不缓存。applicationDirPath() 本身是 O(1)（Qt 缓存了结果），而这些
 *       函数比起随后要做的扫盘/读文件便宜到可以忽略；反过来，一旦缓存，这个头文件
 *       就从"路径的唯一出处"变成"路径的唯一出处 + 一份可能过期的副本"。
 */

#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QLatin1Char>
#include <QString>

namespace AppPaths {

/// 启动器的数据根目录：<exe 所在目录>/.minecraft
///
/// 放在 exe 旁边（而不是 %APPDATA%）是刻意的：整个启动器连带游戏数据可以整体拷走。
/// 代价是必须用绝对路径，因为工作目录可能被改动。
inline QString minecraftDir() {
    return QCoreApplication::applicationDirPath() + QStringLiteral("/.minecraft");
}

/// 已安装版本的根目录：<数据目录>/versions
inline QString versionsDir() { return minecraftDir() + QStringLiteral("/versions"); }

/// 单个版本自身的目录：versions/<id>
/// NOTE: versionId 是目录名、也是进 --version 的值，可以含中文/空格/全角括号（实测用户的
///       实例名就有 `26.2-Fabric 0.19.5`、`原版ATT2地图（无模组）` 这种）。这里不做任何
///       转义或清洗——它就是文件系统上的那个名字。
inline QString versionDir(const QString &versionId) {
    return versionsDir() + QLatin1Char('/') + versionId;
}

/// 客户端 jar：versions/<id>/<id>.jar。是"这个版本装好了没有"的判据。
inline QString versionJarPath(const QString &versionId) {
    return versionDir(versionId) + QLatin1Char('/') + versionId + QStringLiteral(".jar");
}

/// 版本 JSON：versions/<id>/<id>.json。javaVersion / assetIndex / libraries 都在里面。
inline QString versionJsonPath(const QString &versionId) {
    return versionDir(versionId) + QLatin1Char('/') + versionId + QStringLiteral(".json");
}

/// 启动器自己的版本元数据：versions/<id>/wtfml.meta（可选文件）。
/// NOTE: 扩展名不是 .json —— <id>.json 是版本 JSON 的既定名字，撞名会让"读元数据"
///       和"读版本 JSON"落到同一个文件上。详见 VersionManager::displayParts()。
inline QString versionMetaPath(const QString &versionId) {
    return versionDir(versionId) + QStringLiteral("/wtfml.meta");
}

/// 旧格式（1.18.2 及以下）natives classifier jar 的解压目标：versions/<id>/natives
inline QString nativesDir(const QString &versionId) {
    return versionDir(versionId) + QStringLiteral("/natives");
}

/// 独立游戏目录：--gameDir 指向的地方。当前就是版本目录本身。
inline QString gameDir(const QString &versionId) { return versionDir(versionId); }

/// 共享库根目录：<数据目录>/libraries。所有版本共用一份。
inline QString libraryDir() { return minecraftDir() + QStringLiteral("/libraries"); }

/// 资源根目录：<数据目录>/assets（objects 索引与哈希文件都在其下）。
inline QString assetsDir() { return minecraftDir() + QStringLiteral("/assets"); }

/// 自管 Java 运行时的根目录：<数据目录>/java/<主版本>/…
inline QString javaDir() { return minecraftDir() + QStringLiteral("/java"); }

/// 某个主版本的 Java 运行时目录：<数据目录>/java/<majorVersion>
inline QString javaVersionDir(int majorVersion) {
    return javaDir() + QLatin1Char('/') + QString::number(majorVersion);
}

}  // namespace AppPaths
