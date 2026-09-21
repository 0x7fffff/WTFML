/**
 * @file AppPaths.hpp
 * @brief 集中定义启动器数据目录的路径，作为"我们用的是哪个 .minecraft"的唯一出处。
 * @author 0x7fffff
 * @date 2026-09-20
 * @note header-only：只有 inline 函数，不需要加 .cpp，也不用碰链接。
 *
 * 改造前 `QCoreApplication::applicationDirPath() + "/.minecraft"` 这句拼接散在 5 个文件
 * 15 处，各自再往后缀 "/versions"、"/assets"、"/java"…… 后果不是炸，而是：
 *
 *   1. 【**已用**】"切换 .minecraft 文件夹"这个功能没有落点。要让启动器改用另一个
 *      游戏目录，得改 15 处，漏一处就是"版本列表来自 A、下载写进 B"这种只在一半功能上
 *      生效的错位，而且两条路径长得几乎一样，看日志也未必看得出。这个功能做出来了：
 *      落点就是本文件（setMinecraftDir() 一处切换），见 FolderManager。
 *   2. 【**已修**】当时 startGame() 自己拼了一份 versions/<id> 的绝对路径给
 *      ensureInitialOptions 用，而 --gameDir 传的却是相对路径、靠 QProcess 的工作目录
 *      去解析 —— 两份值"恰好相等"，靠的是调用顺序而不是代码。现在这两处取的是
 *      **同一个表达式** AppPaths::versionDir(versionId)，不再有第二份值。
 *   3. 【**已收**】"同一个目录算两遍"这一类也清了：原来这里还有一个 gameDir()（和
 *      versionDir() 同义、没有任何调用点），另外 DownloadManager::downloadAssets() 收了
 *      一个 basePath 参数、而它调用的 parseAndDownloadAssets() 无视该参数直接读
 *      AppPaths::assetsDir() —— 同一个"资源在哪儿"存了两份。两处都删了：gameDir() 没有
 *      调用方可以直接删，downloadAssets() 的那个参数则是**并到了 assetsDir() 这一份**上
 *      （索引和 objects 从此必然同源，游戏按 --assetsDir 找 indexes/<id>.json 不会扑空）。
 *
 * NOTE: 这里**只存一样东西** —— 用户选中的那个数据根目录（见 setMinecraftDir()），
 *       而且它是"被显式设定、要求所有调用方都看见"的状态，不是缓存。其余的一律现算：
 *       applicationDirPath() 本身是 O(1)（Qt 缓存了结果），这些函数比起随后要做的
 *       扫盘/读文件便宜到可以忽略；反过来，一旦把算出来的路径也缓存下来，这个头文件
 *       就从"路径的唯一出处"变成"路径的唯一出处 + 一份可能过期的副本"。
 */

#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QLatin1Char>
#include <QString>

namespace AppPaths {

/// 启动器**自带**的数据根目录：<exe 所在目录>/.minecraft
///
/// 放在 exe 旁边（而不是 %APPDATA%）是刻意的：整个启动器连带游戏数据可以整体拷走。
/// 代价是必须用绝对路径，因为工作目录可能被改动。
///
/// 它是「游戏文件夹」列表里永远排第一、也删不掉的那一项（见 FolderManager）：别的
/// 目录被挪走、U 盘被拔掉的时候，启动器总得有一个能落的根目录。
inline QString defaultMinecraftDir() {
    return QCoreApplication::applicationDirPath() + QStringLiteral("/.minecraft");
}

/// 数据根目录的可写覆盖。空串 = 用默认那份（defaultMinecraftDir()）。
///
/// NOTE: 写成函数局部 static 而不是文件级 static，免得引入静态初始化顺序的问题。
inline QString &minecraftDirOverride() {
    static QString dir;
    return dir;
}

/// 切换启动器使用的数据根目录（「游戏文件夹」列表里点一下就走这里）。
/// @param dir 目录路径；传空串 = 恢复成自带的那一份（<exe 所在目录>/.minecraft）。
///
/// NOTE: 只做 cleanPath（去掉多余的斜杠、`.`、`..`），**不**检查目录存不存在 ——
///       "它是不是个真目录"是 FolderManager 的事，这一层只管"路径长什么样"。
inline void setMinecraftDir(const QString &dir) {
    minecraftDirOverride() = dir.isEmpty() ? QString() : QDir::cleanPath(dir);
}

/// 启动器当前的数据根目录：用户选过就是那一份，否则是自带的那一份。
///
/// 版本、资源、库、自管 Java 全部挂在它下面，所以切换它等于切换"整套 .minecraft"。
inline QString minecraftDir() {
    const QString &custom = minecraftDirOverride();
    return custom.isEmpty() ? defaultMinecraftDir() : custom;
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

/// 实例分组：<数据目录>/wtfml.folders（可选文件，没分过组就不存在）。
///
/// NOTE: 它挂在**数据根目录**下、不挂在 versions 下，因为分组是"这一份 .minecraft 里
///       的实例怎么摆"，换了一份 .minecraft 就是另一套分组。挂在 versions 里会让
///       "删掉 versions 重下一遍"顺手把用户的分组也删了。
/// NOTE: 和 wtfml.meta 一样**不叫 .json**：它是给启动器读写的配置文件，扩展名跟别的
///       json 混在一起，早晚有人拿它当"某个版本的元数据"去读。
inline QString instanceGroupFilePath() {
    return minecraftDir() + QStringLiteral("/wtfml.folders");
}

/// 旧格式（1.18.2 及以下）natives classifier jar 的解压目标：versions/<id>/natives
inline QString nativesDir(const QString &versionId) {
    return versionDir(versionId) + QStringLiteral("/natives");
}

/// 共享库根目录：<数据目录>/libraries。所有版本共用一份。
inline QString libraryDir() { return minecraftDir() + QStringLiteral("/libraries"); }

/// 资源根目录：<数据目录>/assets（objects 索引与哈希文件都在其下）。
inline QString assetsDir() { return minecraftDir() + QStringLiteral("/assets"); }

/// 玩家皮肤/披风贴图的缓存根目录：<数据目录>/skins（见 PlayerSkinStore）。
///
/// NOTE: 跟着当前游戏文件夹走 —— 换一份 .minecraft 就是各存一份缓存，和 java/、assets/
///       一样。这是接受的：那两样本来就是"每份游戏目录各自一份"，皮肤缓存没理由特殊，
///       而且真要共用一份反而要多一个跟游戏目录无关的落点（比如 %APPDATA%），
///       那又和"整个启动器连带数据可以整体拷走"那条打架。
inline QString skinsDir() { return minecraftDir() + QStringLiteral("/skins"); }

/// 自管 Java 运行时的根目录：<数据目录>/java/<主版本>/…
inline QString javaDir() { return minecraftDir() + QStringLiteral("/java"); }

/// 某个主版本的 Java 运行时目录：<数据目录>/java/<majorVersion>
inline QString javaVersionDir(int majorVersion) {
    return javaDir() + QLatin1Char('/') + QString::number(majorVersion);
}

/// 下载下来的第三方安装器 jar 的落脚处：<数据目录>/cache/installers。
///
/// Forge / NeoForge 的安装器是 6 MB 上下的 jar，而"装一次"和"这个版本上换个加载器版本
/// 再装一次"往往挨得很近。留在盘上，第二次装就直接命中"文件在就算好"那条判据，不用重下。
///
/// NOTE: 挂在**数据根目录**下（不挂在 versions 里），理由和 wtfml.folders 一样：它是"给
///       这一份 .minecraft 装东西用的工具"，换一份游戏目录就该是另一份工具。而它又不能
///       丢进 versions/ —— 那底下每一个子目录都会被当成一个实例。
/// NOTE: 没人会去清它（装坏了也不影响：下一次装会重新下）。用户想清就自己删 cache/。
inline QString installerCacheDir() {
    return minecraftDir() + QStringLiteral("/cache/installers");
}

}  // namespace AppPaths
