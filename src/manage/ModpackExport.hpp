/**
 * @file ModpackExport.hpp
 * @brief 「导出…」那一半：把一个已装好的实例打成整合包。**只有拼清单和算指纹**，
 *        不枚举目录、不联网、不碰 zip —— 那三件事在调用点（见 exportInstance）。
 * @author 0x7fffff
 * @date 2026-09-26
 * @note 导出和导入是**同一件事的两个方向**，所以这份代码的判据来源就是这个仓库自己的
 *       解析器（ModpackImport）：写出来的清单必须能被 parseModrinthIndex /
 *       parseCurseForgeManifest 原样读回来（加载器那一行、依赖表、路径规则都算）。
 *       这一条是**完全离线**能验的（build/verify 里就有往返断言），而它正好覆盖了
 *       导出最容易写错的那一层 —— 格式。
 *
 * 【导出长什么样】两种规范都是"一个 zip，根目录一份清单 + 一层 overrides"：
 *   - Modrinth（`.mrpack`）：`modrinth.index.json`。清单里每条**自己带地址、sha1、
 *     sha512、字节数**，所以导入方不需要问接口就能下；
 *   - CurseForge（`.zip`）：`manifest.json`。清单里每条**只有两个号**
 *     （`projectID` + `fileID`），导入方得自己拿号换地址（我们这边是
 *     VersionManager::resolveCurseForgeFiles）。
 * 两边都**不打包**清单里已经写了的文件本体（那样包会巨大、而且导入时会重复），
 * 只打包"认不出来"的那些，放进 overrides/ —— 所以"能认出来多少"直接决定包的质量。
 *
 * 【本地文件怎么换回项目号】两条路都实测过（2026-09-26）：
 *   - Modrinth：`POST /v2/version_files`，把 sha1 批量发过去，回一个 map；
 *   - CurseForge：`POST /v1/fingerprints`，把指纹批量发过去，回 exactMatches[]。
 *   指纹的算法见 curseForgeFingerprint() —— 那不是文档里的东西（CurseForge 官方
 *   从没公开过），是照着社区实现算 + 拿真文件跟官方记录对上的。
 */

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace ModpackExport {

/// 往哪个规范里导。两者的清单结构差别见 .cpp 里的两个 build* 函数。
enum class Format {
    Modrinth,
    CurseForge,
};

/// 一条**已经反查成功**的资源：它在清单里会变成一条"下载这个文件"。
///
/// NOTE: `path` 是**它现在躺在实例里的那个相对路径**，不是接口给的原始文件名 ——
///       两份规范里 `path` 说的是"放到哪儿"，而"是不是同一份文件"是由 hashes / 编号
///       决定的。所以用户自己改过名字（或者被别的启动器加上 `.disabled` 后缀）的文件
///       照样能写进清单，导回去还是原来的样子。
struct MatchedFile {
    QString path;         ///< 相对实例目录，如 `mods/sodium.jar`
    QString sha1;         ///< Modrinth 清单必须要（见 .cpp 里的 must-have）
    QString sha512;       ///< 同上
    QStringList urls;     ///< 下载地址，按顺序试
    qint64 size = -1;     ///< 字节数；-1 = 不知道（那就只写哈希）
    /// `env` 那一对（Modrinth 才有）：空串 = 不写这一段。只在"这个文件只给客户端用"
    /// 这类明确知道的情况下才填（见 .cpp 里 envClientOf 那一套说法）。
    QString envClient;
    QString envServer;
};

/// CurseForge 清单里的一条：**只有两个号**。它没有文件名、没有地址、没有哈希。
struct MatchedRef {
    int projectId = 0;
    int fileId = 0;
};

/// 一次导出要写进清单的全部东西。`files` 与 `refs` 分别给两份规范用
/// （Modrinth 读 files、CurseForge 读 refs；另一份恒为空）。
struct Spec {
    Format format = Format::Modrinth;
    QString name;     ///< 整合包名字（用户在导出框里填）
    QString version;  ///< 整合包自己的版本号（Modrinth 叫 versionId）
    QString summary;  ///< 一句话简介（两份规范里都不是必填，空就不写）
    QString author;   ///< CurseForge 那份有这一格（各家都写，官方 schema 里没见过）
    QString gameVersion;   ///< 基于哪个原版，如 `1.21.1`（= wtfml.meta 的 realVersion）
    QString loaderKey;     ///< 加载器那一行的键，取 ModpackImport 的 loaderKey*()
    QString loaderVersion; ///< 加载器那一行的值（纯版本号，如 `21.1.210`）
    QList<MatchedFile> files;
    QList<MatchedRef> refs;
    /// overrides 那一层目录名。两份规范都认 `overrides`（`client-overrides` 也合规，
    /// 但主流那几家都写 overrides）。**这个字段必须明写出来**：规范没定缺省值。
    QString overridesDir = QStringLiteral("overrides");
};

/// 拼出 `modrinth.index.json`（UTF-8，紧凑）。`files` 那一格**总是写出来**（空的也写）。
///
/// NOTE: 空 `files` 在规范里是合法的（一个纯 overrides 的包），但**我们自己的解析器认
///       不下来**：`parseModrinthIndex` 在"一条文件都没有"时判整包无效
///       （`src/manage/ModpackImport.cpp:379`）。所以调用方**不能**导出一个
///       `spec.files` 为空的包 —— 那样出来的包我们自己都装不回去。一个都认不出来的
///       实例要照实说（"没认出任何资源，整包都会是 overrides"），由用户决定要不要继续。
QByteArray buildModrinthIndex(const Spec &spec);

/// 拼出 `manifest.json`（UTF-8，紧凑）。`spec.refs` 为空也合法。
QByteArray buildCurseForgeManifest(const Spec &spec);

/// 这条能不能作为一条 `files[]` 写进 Modrinth 清单。**这是调用方的分岔口**，
/// 不是给序列化函数用的：假的时候那个文件要**进 overrides**，不是丢掉。
///
/// 判据三条：路径非空、至少一个地址、`sha1` 与 `sha512` **两个都有**（规范原文是
/// MUST）。为什么要卡哈希：清单里的 sha1 是安装时**唯一**的完整性判据 ——
/// `DownloadManager::fileAlreadyValid()` 在 sha1 是空串时直接点头（见
/// `src/download/DownloadManager.cpp:456`），于是半截文件也算"下好了"。
bool writableAsIndexFile(const MatchedFile &file);

/// 加载器的键 → Modrinth 依赖表里那一行叫什么。纯原版（空键）返回空串 = 不写这一行。
QString modrinthDependencyKey(const QString &loaderKey);

/// 加载器的键 + 版本 → CurseForge 的 `modLoaders[].id`（如 `forge-47.2.0`）。
/// 认不出来的加载器返回空串 —— 调用方要么照实说"这个包的加载器没法表达"，要么别导。
///
/// NOTE: NeoForge 在 **1.20.1** 上要插一段游戏版本（`neoforge-1.20.1-47.1.106`），
///       因为那一代的 NeoForge 版本号本身就长这样。这一段是照 Prism 的做法来的
///       （官方只明文确认过 `fabric-x.y.z` 这一种写法），1.20.1 以外的版本不加。
QString curseForgeLoaderId(const QString &loaderKey, const QString &loaderVersion,
                           const QString &gameVersion);

/**
 * @brief 算一个文件的 CurseForge 指纹（`fileFingerprint`）。
 *
 * 算法：把文件**整个**读一遍，剔掉空白字节（`0x09 \t`、`0x0A \n`、`0x0D \r`、
 * `0x20 空格`）之后，对剩下的字节做 murmur2（MurmurHash2 32 位），**种子恒为 1**。
 *
 * 就这么多。**没有**按 zip 条目拼、**没有**"class 文件只留到常量池结束"那一套 ——
 * 后面那条说法流传很广，但既没有出处、实测也不对（四种组合全不匹配）。
 *
 * NOTE: 这个函数**不缓存**。同一个文件会被算两遍的是调用方的事（PCL 那种
 *       "路径+大小+改动时间 做 key" 的缓存该做在调用点，因为那儿才知道文件没变）。
 * NOTE: 内存是 O(1) 的：先数一遍"有几个非空白字节"，再喂第二遍。murmur2 的初值是
 *       `种子 ^ 长度`，长度得先知道，所以**必须两趟**（或者把整个文件读进内存）。
 */
bool curseForgeFingerprint(const QString &path, quint32 *out, QString *errorOut);

/// 对一段内存字节算指纹（文件那条路走的就是同一套状态机）。
/// 单独开这个口子是给验证工程用的：钉住算法本身，不依赖任何外部文件。
quint32 curseForgeFingerprintOfBytes(const QByteArray &bytes);

/// 「这个文件值不值得拿去反查」—— 一个**代价**上的取舍，不是正确性上的：
/// 只对像资源的文件（几种扩展名 + 路径里带 mods/resource 之类）算哈希，否则一个包里
/// config/ 底下成千上万个小文件都要算一遍、发一遍，导出会慢到没法用。
/// 认不出来的照样进 overrides，只是"不问接口"而已（见 .cpp 里那张表）。
bool looksLikeResolvableResource(const QString &relativePath);

}  // namespace ModpackExport
