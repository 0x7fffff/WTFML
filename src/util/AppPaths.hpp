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

/// 实例图标：versions/<id>/icon.png（可选文件，用户自己挑的那张图存在这儿）。
///
/// NOTE: 挑中的那张图要**拷进实例目录**、meta 里只记一句"用本地那张"（见
///       VersionManager::iconField()），而不是记原图的绝对路径 —— 后者在改名
///       （整个目录连带搬家）之后就失效了，而那正是这个项目里天天发生的事。
/// NOTE: 固定叫 icon.png、不跟着原图的扩展名走：读的一侧只认这一个名字，用户挑了 jpg
///       也照样按 PNG 存（QImage 读进来再写出去，格式由我们定，写不出来就报错不落盘）。
inline QString versionIconPath(const QString &versionId) {
    return versionDir(versionId) + QStringLiteral("/icon.png");
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

/// 下载页那些资源的**图标**在本地留一份的目录：<数据目录>/cache/icons。
///
/// 为什么图标要落盘：实测（2026-09-23）它这一路很贵 —— `cdn.modrinth.com` 对每一张图
/// 只回一个 307，真正的字节在 `cdn-alt.modrinth.com` 上，从国内连它 2 秒起步（不通的时候
/// 就是一个都不出）。而下载页一进去就是二十来个图标、而且**每次都是同一批热门资源**。
/// 留一份在盘上，第二次进这一页就是一个请求都不发（见 ResourceStore::loadIconFromCache）。
///
/// NOTE: 挂在**数据根目录**下（和 cache/installers、cache/modpacks 一样），不挂 %APPDATA%：
///       "整个启动器连带数据可以整体拷走"这条在这里同样适用。代价是换一份 .minecraft
///       就重新攒一份图标 —— 和 skinsDir() 那段是同一个取舍，理由也一样。
/// NOTE: 没人会去清它，清掉也不影响正确性（下一次进那一页重新攒）。过期/读坏的那些
///       由 ResourceStore 自己换掉或删掉。
inline QString iconCacheDir() {
    return minecraftDir() + QStringLiteral("/cache/icons");
}

/// 从下载页下回来的整合包（.mrpack）的落脚处：<数据目录>/cache/modpacks。
///
/// 它只是个中转：下回来紧接着就走「导入整合包…」那条**同一条路**（解包、问实例名、下清单
/// 里那几百个文件）。留在这儿是为了"导入到一半用户反悔了"时不用重下那几十 MB —— 和安装器
/// jar 是同一种打算（见上面那段）。
///
/// NOTE: 挂在 cache/ 下面，理由和安装器一样：它不能进 versions/（那底下每一个子目录都会被
///       当成一个实例），也不该跟着实例走。清不清由用户自己决定（重下一次而已）。
inline QString modpackCacheDir() {
    return minecraftDir() + QStringLiteral("/cache/modpacks");
}

/// 「服务器列表」那一页上一次测出来的样子（图标 / MOTD / 人数 / 延迟）：
/// <数据目录>/cache/servers，一台一个 json（见 ServerStatusCache）。
///
/// 为什么要落盘：一次测速要**真的连上去**（每台最多 4 秒超时），而这一页是"每次打开实例
/// 设置都会看见"的一页 —— 断网那一次、或者手快没等完的那一次，那一页不该是一片空白
/// （上一次明明问到过）。它和 cache/icons 是同一条路上的东西：都是"问一次很贵、
/// 答案不会一秒一变"。
///
/// NOTE: 同样挂在**数据根目录**下（和 cache/ 里那几位一样），不挂 %APPDATA% ——
///       "整个启动器连带数据可以整体拷走"这条在这里同样适用。没人会去清它，清掉也不影响
///       正确性（下一次测速重新攒）。
inline QString serverCacheDir() {
    return minecraftDir() + QStringLiteral("/cache/servers");
}

/// 「配置文件」那一页**每存一次就留一份原来那份**的落脚处：
/// <数据目录>/cache/config-backups/<实例id>/<时间戳>/<相对 config/ 的路径>。
///
/// 为什么要有：这是一个能改坏用户配置的编辑器（改错了原来没有撤销），而配置是用户调了
/// 几个月的。写之前把盘上那份原样留一份，用户就能回去。
///
/// ★ 为什么**不能**放在 `<实例>/config/.wtfml-backups/`（这是最自然、也最容易做错的那一
///   个选择）：模组配置是**按目录树认的，不是只认 config/ 那一层** —— 实测盘上 1343 个
///   `.toml` 里有 **392 个住在 config/ 的子目录里**（`config/ars_nouveau/xxx.toml` 166 个、
///   `config/Mekanism/` 15 个…），也就是说有一整套按子目录布局的配置正在被正常加载。我们
///   塞进去的那些 `.toml` 备份会顺着同一条发现逻辑被当成**活的配置** —— 一份配置读两遍、
///   模组读到自己上一次的样子，而且屏幕上什么都看不出来。放 cache/ 底下没有这个问题：那是
///   数据根目录底下、和 config/ 平级、游戏不认的一个目录。
///
/// NOTE: 和 cache/ 里那几位同一条约定（挂数据根目录、可整体拷走）。**但有一处不一样**：
///       清理只认我们自己生成的文件夹名（见 ConfigFileText::backupFoldersToRemove()），
///       而且只删第 20 份往后的那些 —— 它是全仓唯一一处会主动删东西的缓存目录。
inline QString configBackupDir() {
    return minecraftDir() + QStringLiteral("/cache/config-backups");
}

}  // namespace AppPaths
