/**
 * @file ModpackImport.hpp
 * @brief 整合包（Modrinth 的 modrinth.index.json）长什么样、哪条路径不许拼、该怎么起名。
 * @author 0x7fffff
 * @date 2026-09-22
 * @note 这个文件里**没有一个函数碰磁盘、也没有一个函数碰网络**：给一段字节、还一个结论。
 *       所以这些判据能在验证工程里逐条钉住 —— 而整合包这条路真正难的就是它们（下载、
 *       解压、落盘都是仓库里已经验过的老路，见 VersionManager::importModpack）。
 *
 * 【为什么要做整合包导入】真机上的整合包（2026-09-22 拿用户桌面上那个 779 MB 的
 *   `ATT2-The Journey Of Revelation.1.8.4.zip` 核过）是这么个东西：
 *     - 压缩包根目录一份 `modrinth.index.json`：它基于哪个 Minecraft 版本、哪个加载器
 *       的哪个版本，以及**一长串待下载文件**（那个包 150 条，每条都带 sha1 + sha512 +
 *       字节数，地址指向 cdn.modrinth.com / mediafilez.forgecdn.net）；
 *     - 根目录再一份 `overrides/`（那个包 922 条）：整合包作者自己塞的 config、资源包、
 *       光影，以及没托管在 Modrinth 上的 mod。**这一份不在 index 里**，只能从压缩包里解。
 *   所以"导入"= 装一个带加载器的版本（老路）+ 按清单下一批文件 + 把 overrides 解开。
 */

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace ModpackImport {

/// 压缩包根目录下**有哪个文件**就说明它是哪种整合包。判据就是这两个名字
/// （见 ZipUtil::readEntry：它能把"没这个条目"和"读失败"分开）。
enum class Format {
    Unknown,
    Modrinth,   ///< 有 modrinth.index.json
    CurseForge, ///< 有 manifest.json（**还没支持**，见 unsupportedFormatText()）
};

/// 待下载的一条。`path` 已经过 isSafeRelativePath()，可以直接拼到实例目录底下。
struct PackFile {
    QString path;      ///< 相对实例目录的路径，如 `mods/sodium.jar`
    QStringList urls;  ///< 按顺序试的地址（Modrinth 一般只给一个）
    QString sha1;      ///< 清单里的 sha1，落盘后按它校验
    qint64 size = -1;  ///< 清单里的字节数；-1 = 没写（那就只校验 sha1）
};

/// 一份解析好的整合包清单。
struct Pack {
    Format format = Format::Unknown;
    QString name;          ///< 整合包的显示名（起实例名时的候选，见 suggestInstanceName）
    QString version;       ///< 整合包自己的版本号（Modrinth 的 versionId）
    QString gameVersion;   ///< `dependencies.minecraft` —— 它基于哪个原版
    QString loaderKey;     ///< 加载器那一行的键（空 = 这个包是纯原版的，见 loaderKey*()）
    QString loaderVersion; ///< 加载器那一行的值
    QList<PackFile> files; ///< 要下载的那些（已剔掉"只给服务端用"和"没给地址"的）
    /// 清单里**有、但我们不会下**的文件个数，两个原因：
    ///   - `env.client` 写着 `unsupported`（只给服务端用的 mod，装到客户端上会崩）；
    ///   - `downloads` 是空的（Modrinth 规定这种文件该出现在 overrides 里，让压缩包自己带）。
    /// 单独计数是为了在确认框里**如实说出来**：不说的话，用户会以为整合包少装了东西。
    int filesSkipped = 0;
};

/// 解析结果。失败时 `problem` 是**直接弹给用户看**的一句话（不夹错误码，见产品约定）。
struct Result {
    bool ok = false;
    QString problem;
    Pack pack;
};

/// 压缩包根目录下那份清单的名字。放这儿而不是让各个调用点各写一遍字面量：
/// "这是不是整合包"、"读哪一条"、"去重"三处用的必须是同一个串。
QString modrinthIndexName();
QString curseForgeManifestName();

/**
 * @brief 解析一份 `modrinth.index.json`。
 * @param json 那一条条目的**原始字节**。
 * @return 成功时 ok=true；否则 problem 里是一句给用户看的原因（**不是**"解析失败"）。
 *
 * 每一条拒绝的理由都是"用户能理解、也多半能自己处理"的：这份 json 读不懂（不是整合包）、
 * 它不是 Minecraft 的包、没写基于哪个版本、文件清单一团糟、里面有往外跳的路径。
 *
 * NOTE: **路径不安全的整包直接拒绝**（而不是跳过那一条）。整合包是从网上下来的压缩包，
 *       里面出现 `mods/../../x.jar` 这种路径只有两种可能：文件坏了，或者有人想往
 *       实例目录外面写东西。这两种情况都不该"照常装、只少一个文件"。
 */
Result parseModrinthIndex(const QByteArray &json);

// ---- `dependencies` 里那几个键 ----
//
// 键名只有这一份：判断"这是个加载器包还是纯原版包"、映射到 VersionManager::LoaderKind、
// 以及给用户报"这个包要 Quilt"时用的都是它们。各写各的字面量的话，写错一个字符的症状是
// "明明是 Fabric 包，却按纯原版装了"——那种错不报警，装完才发现。

/// 整合包依赖表里加载器那几个键。**顺序就是认的顺序**（一份包里只会有其中一个）。
QStringList loaderDependencyKeys();
QString fabricLoaderKey();
QString forgeLoaderKey();
QString neoforgeLoaderKey();
QString quiltLoaderKey();

/**
 * @brief 这个加载器本启动器装不装得了。装得了（含"纯原版整合包"）返回空串。
 * @param loaderKey `Pack::loaderKey`。
 * @param loaderVersion `Pack::loaderVersion`（只说给用户听，判断不看它）。
 *
 * NOTE: 认不出来的键走的是和 Quilt **同一条**路（"这个加载器本启动器还不认识"），
 *       不返回空串 —— 返回空串等于"那就当纯原版装吧"，装出来一个进不去的整合包。
 */
QString unsupportedLoaderText(const QString &loaderKey, const QString &loaderVersion);

/// CurseForge 那个 `manifest.json` 现在还不支持（它的清单里**没有文件地址**，得调
/// CurseForge 的 API 换，那是另一件事）。这一句是给用户看的：说清是什么包、为什么不行、
/// 以及他现在能做什么。
QString unsupportedFormatText(Format format);

/**
 * @brief 一条相对路径能不能安全地拼到实例目录底下（**纯判据**）。
 *
 * 逐段按 `InstanceFiles::validatePathSegment()` 判（那一套和"实例名"共用，只有一处）：
 * 非法字符、控制字符、`.`/`..`、结尾的点、Windows 保留设备名。另外这里还管两件
 * 路径独有的事：
 *   - **反斜杠一律拒绝**：Modrinth 的清单用 `/`，出现 `\` 只可能是有人想借 Windows 的
 *     分隔符绕过逐段判据（`mods\..\..\x.jar` 按 `/` 切是一段，逐段看起来完全正常）；
 *   - **不许以 `/` 开头**（绝对路径），也不许空段（`a//b`，逐段判据会把它切出一个空段）。
 * 长度上限按整条路径算（240，Windows 的 MAX_PATH 减去实例目录本身那段），不按单段算 ——
 * 真实的 mod 文件名可以很长（`mods/[1.20.2]xxx-fabric-quilt-forge-1.20.x.jar` 就有 60+），
 * 单段限长会把正常的整合包挡在外面。
 */
bool isSafeRelativePath(const QString &path);

/**
 * @brief 从整合包的名字推一个**合法**的实例名候选（给"给这个实例起个名"那个输入框当地板）。
 *
 * 两步，只做必要的事：把名字里 Windows 建不出目录的字符换成 `-`，再把长度收到 64
 * （和 `InstanceFiles::validateDirName()` 那条上限**是同一个数**，超了它就不认）。
 * 结果一定过得了 validateDirName()，所以用户直接点确定就能用。
 *
 * NOTE: **不做**"去空格、去中文、只留 ASCII"那种清理 —— 真机上的实例名就长这样
 *       （`原版ATT2地图（无模组）`），那是用户的实例，不是文件名。
 */
QString suggestInstanceName(const QString &packName);

/**
 * @brief 清单里给的那几条地址，按"先试哪条"重排一遍：`cdn.modrinth.com` 排最前。
 * @param urls 清单里那一条文件的 `downloads`（1~3 条）。
 * @return 同样那几条，只换次序；认不出 modrinth 的整份原样返回。
 *
 * 【为什么要有这个】2026-09-22 真机实测（同一份文件、同一时刻、单条连接）：
 *     mediafilez.forgecdn.net   22.9 KB/s      edge.forgecdn.net   14.5 KB/s
 *     cdn.modrinth.com         119.4 KB/s
 *   另外两份文件是同一个形状（26 vs 262 KB/s、34 vs 88 KB/s）—— **差 3~10 倍**，而且是
 *   每条连接都差（6 条并发：forgecdn 合计 ~160 KB/s，modrinth 合计 ~600 KB/s）。那个包
 *   422 个文件里 **332 个、89% 的字节**都有 modrinth 这条地址，可清单把它排在最后，而
 *   原来只用 `urls.first()` 与 `urls.value(1)` —— 两台 forgecdn，一台都没轮到它。
 *
 * NOTE: 只是**换次序**，一条都不丢：换完之后 `urls.value(1)` 仍然是"取不回来时再试一次"
 *       那一条，只是它成了 forgecdn。谁快谁慢是实测出来的，不是猜的；也别把它当成
 *       "modrinth 一定对" —— 真取不回来时那条老路（退下一条 + 失速看门狗）照旧。
 */
QStringList preferredUrlOrder(const QStringList &urls);

}  // namespace ModpackImport
