/**
 * @file AsciiPathAlias.hpp
 * @brief 给 Java 8 换一套 ASCII 路径 —— 它打不开任何带非 ASCII 字符的路径。
 *
 * 【这个文件存在的理由】
 *
 * 真机实测（，全量实例启动那一趟）里有两个实例**只能看到一句**
 * `找不到或无法加载主类 net.minecraft.client.main.Main`：
 * - `20w14∞`（快照，javaVersion.majorVersion = 8，目录名里有 `∞`）；
 * - `魔改版ATT2-BC&A&IS(多周目)`（1.8 整合包，目录名是中文）。
 * 同一个 jar、同一条命令行，把路径换成纯 ASCII 就正常（build/tmp-run/cp8c.ps1 的单变量
 * 对照：相对/绝对都一样，**只有非 ASCII 那一条会失败**）。根因不在我们这一层，也不在
 * version.json —— 是**这台机器开着 Windows 的"Beta: 使用 Unicode UTF-8 提供全球语言支持"**
 * （系统 ANSI 代码页 65001），而 Java 8 把命令行参数按它自己认定的编码转换：
 * 实测 `-XshowSettings:properties` 给出 `sun.jnu.encoding = GBK`、
 * `file.encoding = GBK`，与实际的 65001 对不上，于是**凡是路径里出现一个非 ASCII 字符，
 * Java 8 那一侧拿到的就是一个不存在的路径**。官方启动器、PCL 在同一台机器上是同一个下场
 * （换 Java 9+ 就好，但 1.8 的整合包和 20w14∞ 都跑不了 Java 9+）。
 *
 * 所以这里做的事情是：**在数据目录里另建一套纯 ASCII 的路径**，把要递给 Java 的那几条路
 * 换过去 —— 游戏本身照旧读写原来那个目录，用户看不出任何区别：
 * - `game`：目录联接（junction）→ `versions/<id>`，用来当 `--gameDir`；
 * - `jars/`：那一批顶层 jar 的**硬链接**（客户端 jar 那一个，外加实例目录里别的 jar），
 * 用来进 classpath；文件名自己带非 ASCII 的（真机上 `20w14∞.jar`）会同时换成
 * `jar-<哈希>.jar` 这样一个纯 ASCII 的名字，见 jarFileNameInAlias()；
 * - `natives`：目录联接 → `versions/<id>/natives`，用来当 `-Djava.library.path`
 * （以及 version.json 里 `${natives_directory}` 那个占位符）。
 * 这三样要么全换、要么全不换：混着用的症状（比如 gameDir 是别名、classpath 还是原路径）
 * 比全都不用更难查。
 *
 * 【为什么是一个 jars/ 目录，而不是"客户端 jar 一个硬链接"】
 * classpath 上不止客户端 jar 一个来自 `versions/` —— 有的实例主类住在自己目录里**另一个**
 * jar 里（真机上 `GTNH 2.8.4` 就是：`me.eigenraven.lwjgl3ify.relauncherstub.RelauncherStubMain`
 * 在 `lwjgl3ify-forgePatches.jar` 里，而它在 `libraries` 里一条都没有，见 LauncherManager
 * 里那段兄弟 jar 搜索）。只链客户端 jar 的话，那种实例换了别名照样起不来，而且症状是
 * `找不到或无法加载主类` —— 和"根本没建别名"一模一样。所以：**实例目录（以及 jar 的主人那个
 * 目录）顶层有几个 jar，就在这里链几个**，classpath 一律走 `jars/<名字>`。
 *
 * 【为什么是联接/硬链接，不是复制或符号链接】
 * - **复制**会把"用户在游戏里新建的存档"落到那个副本里，原目录反而看不见 —— 那是换了一
 * 个游戏目录，不是别名；
 * - 联接（`IO_REPARSE_TAG_MOUNT_POINT`）是**不需要管理员权限**的（符号链接要，除非系统
 * 开了开发者模式），而且对文件系统是透明的：游戏往别名里的 `saves/` 写，落的就是
 * `versions/<id>/saves/`；
 * - 硬链接共享同一份数据（16 MB 的客户端 jar 不占第二份空间），而且"盘的这一份被换了"
 * 由文件索引判得出来（见 .cpp 里的 sameFile()）。
 *
 * 【为什么落在 cache/ascii 底下】
 * 和 cache/ 里那几位同一条约定（挂数据根目录、可以整体拷走）。**不能**放进 `versions/<id>/`：
 * 那底下多出来的任何目录都会被游戏自己扫（见 AppPaths::worldBackupDir() 那段 —— 同一类坑），
 * 而且 `versions/` 底下每一个子目录都会被当成一个实例。
 *
 * 【什么时候**不**做】
 * - Java 9 及以上：它们读 UTF-8 命令行，本来就没有这个问题（改了反而多一层）；
 * - javaMajorVersion 未知（0）：宁可维持原样 —— 判错的代价是"本该能起的实例起不来了"；
 * - 路径里**没有**非 ASCII 字符：绝大多数实例（真机 70 个里 68 个）走这条，行为逐字节
 * 不变，这也是这个文件最大的那一条不变量；
 * - 数据目录**自己**的路径里就有非 ASCII 字符：别名根目录也在它底下，救不了。这时
 * ensure() 会返回一句说清楚的话（宁可不启动，也不要让用户对着 `找不到主类` 猜）。
 */
#pragma once

#include <QString>
#include <QStringList>

namespace AsciiPathAlias {

/// 这一段文本里有没有非 ASCII 字符（> U+007F）。
///
/// NOTE: 判据是**字符**级，不是"编码得出来吗"：Java 8 那一步坏掉的是"命令行上的字节和它
/// 认定的编码对不上"这件事本身，`∞`（GBK 里有它）和中文一样会坏。
bool hasNonAscii(const QString &text);

/**
 * @brief 某个 jar 在别名里叫什么名字（**纯函数**，只看名字、不碰盘）。
 * @param fileName 真实文件名（例如 `20w14∞.jar`）。
 * @return 名字本身已经全是 ASCII 时**原样返回**；否则返回 `jar-<哈希前 8 位>.jar`。
 *
 * 为什么文件名也要换：Java 8 打不开的是**整条路径字符串**，目录名换成 ASCII 了、
 * 文件名里还剩一个 `∞` 一样打不开 —— 真机上 `20w14∞` 的客户端 jar 就叫
 * `20w14∞.jar`，第一版只换了目录那一层，于是它照旧停在"找不到或无法加载主类"。
 * （判据和目录那一层完全一样：只要有一个 > U+007F 的字符就换。）
 *
 * NOTE: 哈希取 SHA-1 前 8 位十六进制，**不能**用 qHash()（Qt 6 每次进程启动换种子，
 * 那样每次算出的名字都不一样、硬链接永远建不完）。
 * NOTE: 换名字只影响**递给我们自己的 classpath 字符串**：文件内容、硬链接指向的真 jar、
 * 游戏看到的一切都没变（用户在自己的实例目录里看到的还是 `20w14∞.jar`）。
 */
QString jarFileNameInAlias(const QString &fileName);

/// 一个实例那套 ASCII 别名路径。纯数据，pathsFor() 只算不碰盘。
struct Paths {
    /// 这套别名的根：`<数据目录>/cache/ascii/<实例 id 的哈希>`。
    QString root;
    /// 当 `--gameDir` 用的那一份（联接 → `versions/<id>`）。
    QString gameDir;
    /// 那一批 jar（硬链接）住的地方，classpath 上凡是从 `versions/` 来的 jar 都走这里。
    QString jarsDir;
    /// 当 `-Djava.library.path` 用的那一份（联接 → `versions/<id>/natives`）。
    QString nativesDir;

    /// 某个 jar（**真实文件名**）在别名里的路径。文件名可能被换过，见 jarFileNameInAlias()。
    QString jarFor(const QString &fileName) const {
        return jarsDir + QLatin1Char('/') + jarFileNameInAlias(fileName);
    }
};

/**
 * @brief 算出某个实例那套别名路径。**纯函数**，不碰磁盘。
 * @param versionId 实例 id（= versions/ 下的目录名）。
 *
 * 根目录名取 id 的 SHA-1 前 12 位十六进制：id 里可以是中文、`∞`、空格、括号（真机上都
 * 有），拼进路径就不是 ASCII 了；而哈希是纯十六进制。**不能**用 `qHash()` —— Qt 6 的
 * `qHash(QString)` 每次进程启动都带一个随机种子，那样每次启动都会新算出一个根目录，
 * 别名也就永远建不完、永远清不掉。
 */
Paths pathsFor(const QString &versionId);

/**
 * @brief 这几种情况下要不要给 Java 换别名。
 * @param javaMajorVersion 这次启动用的 Java 大版本（0 = 调用方没探到）。
 * @param realPaths 这次**本来会递给 Java** 的那几条路径（gameDir / jar / natives）。
 * @return 只有"Java ≤ 8 且至少有一条路径带非 ASCII 字符"才是 true。
 *
 * NOTE: 传进来的必须是**真正会出现在命令行上的**那几条路径。libraries/ 和 assets/ 不在此列
 * —— 它们在数据根目录底下，和数据根目录自己是 ASCII 还是不是**同一条判据**（那一种
 * 由 ensure() 自己拦，见文件头最后一条）。
 */
bool needed(int javaMajorVersion, const QStringList &realPaths);

/**
 * @brief 这套别名是不是已经建好了（只看盘，不建也不删）。
 *
 * 调用方（LauncherManager）用它决定"这次命令行到底发哪一套路径"：ready 才换。这样命令行
 * 的构造仍然是**只读盘**的，建/删那半边只在 ensure() 一处发生。
 *
 * NOTE: 判据里**没有** natives 那一条：它由调用方按 `needsNativesDir` 自己看（真的需要却
 * 缺了，那边有 hasNativeLibraries() 的 qWarning 现原形）。少判一条换来的是这个函数
 * 不用知道"这个版本要不要 natives"，而那个答案在 LauncherManager 里是拼 classpath
 * 的过程中才算出来的。
 */
bool isReady(const Paths &paths);

/**
 * @brief 把这一套别名建出来（已经建好的直接复用，幂等；顺便清掉过期的那些）。
 * @param paths pathsFor() 算出来的那套路径。
 * @param versionId 实例 id（= versions/ 下的目录名）。
 * @param realNativesPath 真实 natives 目录；空 = 不需要（1.19+ 由 LWJGL 自己从 classpath
 * 的 jar 里提取，prepareNatives() 返回空串）。
 * @return 成功返回空串；失败返回**给用户看的一句话**（要弹窗，见 MainWindow 里那一步）。
 *
 * 已建好的判据不是"这个文件在不在"，而是"它还是不是**同一份东西**"：
 * - `game` / `natives` 那两个联接：目标还不存在（实例被改名/删掉）就重做；
 * - `jars/` 里那些硬链接：和真实 jar 不是同一个文件索引（jar 被重下过、实例被删了又
 * 建过）就重做。拿"文件在不在"当判据的话，症状是**游戏跑起来的是上一份 jar**。
 *
 * NOTE: 顺手做清理的那一步只认**自己算出来的**那几个名字（`game` / `jars` / `natives`），
 * 而且只删指向已消失实例的那些根目录 —— 别名根底下的别的东西一律不碰。
 */
QString ensure(const Paths &paths, const QString &versionId, const QString &realNativesPath);

}  // namespace AsciiPathAlias
