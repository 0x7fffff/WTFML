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
#include <QHash>
#include <QList>
#include <QMetaType>
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

/// CurseForge 清单里的一条：**它只给两个号**（`projectID` / `fileID`），文件名和地址
/// 都没有 —— 那不是漏了，是这个格式本来就长这样（见 parseCurseForgeManifest 上面那段）。
/// 所以它得先经过一次"拿号换文件"（`VersionManager::resolveCurseForgeFiles`）才能变成
/// 上面那个 `PackFile`。
struct PackRef {
    int projectId = 0;
    int fileId = 0;
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
    /// CurseForge 专用：还没换成地址的那些（见 PackRef）。解析完 `manifest.json` 之后
    /// **`files` 是空的、它们全在这儿** —— 换成了才挪过去。其余格式恒为空。
    QList<PackRef> pendingRefs;
    /// 清单里**有、但我们不会下**的文件个数，三个原因：
    ///   - `env.client` 写着 `unsupported`（只给服务端用的 mod，装到客户端上会崩）；
    ///   - `downloads` 是空的（Modrinth 规定这种文件该出现在 overrides 里，让压缩包自己带）；
    ///   - CurseForge 那边它是个**嵌套的整合包**（`fileID` 指着一个 .zip，我们不代装）。
    /// 单独计数是为了在确认框里**如实说出来**：不说的话，用户会以为整合包少装了东西。
    int filesSkipped = 0;
    /// CurseForge 专用：接口**明确说"不许第三方代下"**的个数（`downloadUrl` 是 null，
    /// 那是作者在项目设置里关掉了第三方分发）。这一档和上面那种"按规矩跳过"**不是一回事**：
    /// 用户自己动手是能补齐的，所以那句话得说到"你怎么办"（见 finishModpackImport）。
    ///
    /// NOTE: 这些文件在 CDN 上其实是躺着的（`mediafilez.forgecdn.net/files/<id/1000>/<id%1000>/<file>`
    ///       实测 200），**所以"技术上能下"不是"该下"** —— 接口给你一条 null 就是在说不许，
    ///       绕过它等于替用户做了他没同意的那个决定，而且做在别人的 CDN 上。
    int filesNotDistributable = 0;
    /// CurseForge 专用：接口里**查不到**（或者文件已被作者删除 / 没过审 / 名字拼不出安全
    /// 路径）的个数。这一档是"这个包本身有问题"，用户除了换包没法处理。
    int filesMissing = 0;
    /// 压缩包里覆盖文件那一层目录名（Modrinth 固定 `overrides` / `client-overrides`；
    /// CurseForge 由清单里的 `overrides` 字段给，**后一个对 CF 恒为空** —— 它没有这个规矩）。
    QString overridesDir = QStringLiteral("overrides");
    QString clientOverridesDir = QStringLiteral("client-overrides");
    /// 清单里点了名的文件总数（**不管下不下得成**）。确认框上那句"要下载的文件：N 个"
    /// 用它：CurseForge 的 N 条在这一步还只是号，但用户要知道的正是"这个包有多大"。
    int fileCount() const { return files.size() + pendingRefs.size(); }
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

// ---------------------------------------------------------------------------
// CurseForge：`manifest.json`
// ---------------------------------------------------------------------------
//
// 【这个格式难在哪儿】它的 `files[]` 里**一条地址都没有**，只有两个号：
//
//     {"projectID": 238222, "fileID": 8971390, "required": true}
//
// 文件名、字节数、sha1、下载地址**全都要去 CurseForge 的接口换**（2026-09-25 实测，
// 探针与数字见下）。所以这条路上"解析"是两段：先把号和基础信息读出来
// （parseCurseForgeManifest），再拿号去换文件（parseCurseForgeFileInfo）。
//
//     GET  /v1/mods/{modId}/files/{fileId}         一条一条问（200）
//     POST /v1/mods/files  {"fileIds":[...]}       **批量**，一个包一问就够（200）
//     POST /v1/mods        {"modIds":[...]}        批量问"这是模组还是资源包"（200）
//
// 官方那份 `api.curseforge.com` **要 `x-api-key`**（不带 = 403，实测）。所以默认走的是
// 一条社区反代（`api.curse.tools`，同一个接口形状、不要 key，实测 200）—— 代价写在
// curseForgeApiBase() 上面。用户自己填了 key 就改走官方。

/**
 * @brief 解析一份 CurseForge 的 `manifest.json`。
 * @param json 那个条目的原始字节。
 * @return 成功时 ok=true，`pack.files` **是空的** —— 那些号在 `pack.pendingRefs` 里等着换。
 *
 * 和 Modrinth 那一条一样，"读不懂"永远是一句用户看得懂的话（少了哪个字段、值是什么）。
 * 加载器那一行的写法两边**完全不同**：CurseForge 是 `{"id": "forge-47.2.0"}`，一个字符串
 * 把名字和版本拼在一起，见 curseForgeLoaderKey()。
 */
Result parseCurseForgeManifest(const QByteArray &json);

/**
 * @brief CurseForge 那个 `modLoaders[].id` 拆成 Modrinth 那套键。
 * @param rawId `forge-47.2.0` / `fabric-0.15.11` / `neoforge-20.4.1` / `quilt-0.20.0` / 光秃秃的 `forge`。
 * @param keyOut `Pack::loaderKey` 该填什么（fabric → `fabric-loader`、quilt → `quilt-loader`，
 *        翻译过来之后**下游一个字都不用改**：unsupportedLoaderText / loaderKindForDependencyKey
 *        认的就是这四个键）。
 * @param versionOut `Pack::loaderVersion`；`rawId` 里没带版本（老包的 `"forge"`）时是空串，
 *        空串在安装那一步是"挑最新的"。
 * @return 认不认识这个加载器。**认不出来也给 keyOut**（原样那个前缀，如 `liteloader`）——
 *        空串在 unsupportedLoaderText() 眼里是"纯原版包，放行"，装出来一个进不去的整合包。
 *
 * NOTE: 拆在**第一个** `-` 上，不是最后一个：`neoForge-20.4.1-beta` 这种带后缀的也得
 *       把版本整段留住。
 */
bool curseForgeLoaderKey(const QString &rawId, QString *keyOut, QString *versionOut);

/// 批量问文件那一发的请求体（`{"fileIds":[…]}`）。纯函数是为了能离线钉住 ——
/// 这一发的形状写错（键名、号是数字还是字符串）时接口回 400，而 400 的正文里
/// 只有一句 `{"error":"…"}`，看着和"这个包坏了"一模一样。
QByteArray curseForgeFilesRequestBody(const QList<PackRef> &refs);

/// 批量问"这是哪一类"那一发的请求体（`{"modIds":[…]}`）。
QByteArray curseForgeModsRequestBody(const QList<int> &projectIds);

/// 换回来的一个文件。**它和 `PackFile` 差一步**：`path` 里此刻只有一个文件名、还没有目录 ——
/// 目录得拿 `modId` 去问 classId（文件对象自己不带类目号），见 placeCurseForgeFiles()。
struct CurseForgeFile {
    PackFile file;
    int modId = 0;
};

/// 一次批量问号的结果。**四条出路必须分得开**（这和"无可用版本 / 列表没取到"是同一课）：
///   - `problem` 非空 ⇒ 这一发根本没成（网络、限流、正文读不懂）—— 整件事停下来报错；
///   - 换到的进 `resolved`；
///   - 换不到的**分两类计数**：作者关了第三方分发（`Pack::filesNotDistributable`）、
///     接口里查不到（`Pack::filesMissing`）。
struct CurseForgeFiles {
    QList<CurseForgeFile> resolved;
    QList<int> projectIds;   ///< 这批文件属于哪几个项目（去重后，拿它去问 classId）
    int notDistributable = 0;
    int missing = 0;
    QString problem;         ///< 非空 = 这一发没成
};

/**
 * @brief 接口正文 → 文件。@param asked 是**这一问问的那些号**（正文里缺哪几条要能算出来）。
 *
 * 正文里那条 `hashes` 数组里 `algo` 是 1 的那条就是 sha1（2 是 md5）—— 有它，
 * 下载那一段的 `VerifyMode::SizeAndSha1` 一个字都不用改。
 *
 * NOTE: `fileName` 是**外部输入**，它要拼进路径里（`mods/<fileName>`），所以这里按
 *       `isSafeRelativePath()` 逐条判过：带 `/`、`\`、`..` 的当场算"查不到"（记进 missing），
 *       绝不放行到写盘那一步去。名字不安全的文件**整个包不拒绝**（和 Modrinth 那份清单里
 *       写死路径的情形不同：那边路径是清单作者写的，这边是一个我们控制不了的文件名）。
 */
CurseForgeFiles parseCurseForgeFileInfo(const QByteArray &json, const QList<PackRef> &asked);

/**
 * @brief 这个项目属于哪一类 → 文件该落到实例目录的哪个子目录。
 * @param classId CurseForge 的类目号（6 模组 / 12 资源包 / 6552 光影 / 6945 数据包 /
 *        17 存档 / 4471 整合包）。
 * @return 带结尾斜杠的目录名，如 `mods/`；**空串 = 这一条不下**（嵌套的整合包）。
 *
 * 6 那一档和"认不出来"合在一起：认不出来的一律落 `mods/`（主流启动器都这么做）——
 * 落错的代价是"这个文件没用"，而拒绝的代价是"整合包装不全"，后者重得多。
 *
 * NOTE: 判据是**类目号，不是文件名的后缀**：`.zip` 既可能是资源包也可能是存档，
 *       而 `fileName` 是接口给的、可以随便起。
 */
QString curseForgeFilePath(int classId);

/// 把换回来的文件摆进目录。**这是"号 → 能下的文件"的最后一步**。
struct CurseForgePlacement {
    QList<PackFile> files;   ///< 这一步之后 `path` 才是完整的、能直接下的相对路径
    int skipped = 0;         ///< 类目是嵌套整合包（见 curseForgeFilePath 的空串那一档）
    int missing = 0;         ///< 拼出来的路径过不了 isSafeRelativePath()（多半是太长）
};

/**
 * @brief 给每个文件补上目录。
 * @param resolved 上一步换回来的（`file.path` 里此刻只有一个文件名）。
 * @param classIdByProject `modId → classId`，来自 POST /v1/mods 那一发。
 *        **里面没有的号一律当"认不出来"**（落 `mods/`）—— 拿不到类目号不该让文件消失。
 *
 * NOTE: 同一个路径出现两次时**以最后一条为准**（和 Modrinth 那一份的 `pathDedupKey()`
 *       是同一课：文件名是外部输入，撞车是可能的，而两条同时写一个文件是数据竞争）。
 */
CurseForgePlacement placeCurseForgeFiles(const QList<CurseForgeFile> &resolved,
                                         const QHash<int, int> &classIdByProject);

/**
 * @brief `POST /v1/mods` 的答复 → `modId → classId`。
 * @param problem 非空则这一发没成（正文读不懂 / 没有 data）。
 *
 * NOTE: 这一发没成**不该让整个导入停下** —— 缺的只是"落到哪个目录"（退成 `mods/`，
 *       见 placeCurseForgeFiles），文件本身还是下得下来的。所以 problem 是给调用方
 *       "说一句"用的，不是"报错"用的。
 */
QHash<int, int> parseCurseForgeClassIds(const QByteArray &json, QString *problem = nullptr);

/**
 * @brief 问哪一台接口。
 * @param apiKey 用户在设置里填的 key；空 = 走那条社区反代。
 * @param override 非空则**优先**用它（`Launcher/CurseForgeApiBase`，验证工程拿它把接口
 *        指到本机的假服务器上）。产品自己**从不写这个键**。
 *
 * 【为什么默认是反代】官方那份要 `x-api-key`（不带 = 403，实测 2026-09-25）。而 key 一旦
 * 编进 exe 就等于公开（谁都能从二进制里抠出来），泄露的 key 会被吊销 —— 所以默认走
 * `https://api.curse.tools/v1`（同一个接口形状，不要 key）。**代价要说清楚**：那是**别人
 * 的服务**，可能限流、可能哪天就没了。所以：① 用户填了自己的 key 就改走官方；② 接口那一发
 * 失败时那句话里要提到"可以在设置里填自己的 CurseForge API key"（见 VersionManager）。
 */
QString curseForgeApiBase(const QString &apiKey, const QString &override = QString());

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

/// 认不出格式时给用户看的那一句：说清两种包各自长什么样、他手上这个多半是什么。
///
/// NOTE: 2026-09-25 起 CurseForge 那一支**删了**（那个格式已经能装，见上面那一段）。
///       留着一个"还没支持"的分支，将来只会以"明明支持却说不行"的形式出现。
QString unsupportedFormatText(Format format);

/**
 * @brief 一条相对路径能不能安全地拼到实例目录底下（**纯判据**）。
 *
 * 逐段按 `InstanceFiles::validatePathSegment()` 判（那一套和"实例名"共用，只有一处）：
 * 非法字符、控制字符、`.`/`..`、结尾的点或空格、Windows 保留设备名。另外这里还管两件
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
 *
 * NOTE: 上面那组"6 条并发"是 2026-09-22 量的，当天 `kMaxConcurrentDownloads` 还是 6。
 *       2026-09-23 那个上限提到了 24（依据是官方 CDN 上 3598 个对象的实测，见那个常量），
 *       **mod CDN 这一侧没有跟着重测**：它的单条连接本来就慢（22~119 KB/s），并发高一点
 *       大概率更快，但那条 CDN 另有"连下两百多个文件之后开始回 `HTTP/2 protocol error`、
 *       甚至直接掐连接"的毛病（那次 422 个文件的真导入实测），并发调高会不会让它更早翻脸
 *       还不知道。别把这里的数当成"现在的行为"，要用就得重新量一遍。
 * NOTE: 上限调到 24 的同一批里补了**按主机**那条守卫（`kMaxConcurrentPerHost`）：一个还没
 *       证明自己走 h2 的主机只放 6 条。所以这里真正的变化是"**这一条 CDN 要是 h2，它就能用
 *       到 24 条；要是不是，它还按 6 条走**"，不会因为全局上限调大而把请求塞进 Qt 那个会
 *       超时的内部队列（那个形状在本地 h1.1 假服务器上复现过：40 条作业 28 条超时）。
 */
QStringList preferredUrlOrder(const QStringList &urls);

}  // namespace ModpackImport

// 这个枚举要**过一个 Q_INVOKABLE 的边界**：MainWindow::applyModpackImport() 的第四个参数是
// 它，而验证工程是拿 QMetaObject::invokeMethod 驱动那一半的（那一半是私有的）。
// 声明放在**命名空间外面**：放里面声明出来的名字是裸的 `Format`，而 Q_ARG 那侧写的名字是
// `ModpackImport::Format` —— 对不上就等于没注册（Qt 只在运行时报一句 no such type，
// 而 invokeMethod 只是**静默返回 false**，看着像"那条路没被拦住"）。
// 同款先例见 LoaderMeta.hpp 末尾那两行。
Q_DECLARE_METATYPE(ModpackImport::Format)
