/**
 * @file ModpackExport.hpp
 * @brief 「导出…」那一半：把一个已装好的实例打成整合包。**从枚举到清单都在这一层**，
 * 但**不联网、不碰 zip**：反查那两发由调用点传进来的 query 走（buildExportPlan
 * 的入参），zip 由调用点在它上面再套一层（见 .cpp 末尾那段）。
 * @note 导出和导入是**同一件事的两个方向**，所以这份代码的判据来源就是这个仓库自己的
 * 解析器（ModpackImport）：写出来的清单必须能被 parseModrinthIndex /
 * parseCurseForgeManifest 原样读回来（加载器那一行、依赖表、路径规则都算）。
 * 这一条是**完全离线**能验的（build/verify 里就有往返断言），而它正好覆盖了
 * 导出最容易写错的那一层 —— 格式。
 *
 * 【导出长什么样】两种规范都是"一个 zip，根目录一份清单 + 一层 overrides"：
 * - Modrinth（`.mrpack`）：`modrinth.index.json`。清单里每条**自己带地址、sha1、
 * sha512、字节数**，所以导入方不需要问接口就能下；
 * - CurseForge（`.zip`）：`manifest.json`。清单里每条**只有两个号**
 * （`projectID` + `fileID`），导入方得自己拿号换地址（我们这边是
 * VersionManager::resolveCurseForgeFiles）。
 * 两边都**不打包**清单里已经写了的文件本体（那样包会巨大、而且导入时会重复），
 * 只打包"认不出来"的那些，放进 overrides/ —— 所以"能认出来多少"直接决定包的质量。
 */

#pragma once

#include "util/ZipUtil.hpp"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace ModpackExport {

/// 往哪个规范里导。两者的清单结构差别见 .cpp 里的两个 build* 函数。
enum class Format {
    Modrinth,
    CurseForge,
};

/// 一条**已经反查成功**的资源：它在清单里会变成一条"下载这个文件"。
///
/// NOTE: `path` 是**它现在躺在实例里的那个相对路径**，不是接口给的原始文件名 ——
/// 两份规范里 `path` 说的是"放到哪儿"，而"是不是同一份文件"是由 hashes / 编号
/// 决定的。所以用户自己改过名字（或者被别的启动器加上 `.disabled` 后缀）的文件
/// 照样能写进清单，导回去还是原来的样子。
struct MatchedFile {
    QString path;         ///< 相对实例目录，如 `mods/sodium.jar`
    QString sha1;         ///< Modrinth 清单必须要（见 .cpp 里的 must-have）
    QString sha512;       ///< 同上
    QStringList urls;     ///< 下载地址，按顺序试
    qint64 size = -1;     ///< 字节数；-1 = 不知道（那就只写哈希）
    /// `env` 那一对（Modrinth 才有）：空串 = 不写这一段。填的规则只有一种 ——
    /// 接口说这个文件 `client_only` 时（见 .cpp 里 applyEnvironment()，那里也写了
    /// 为什么 `server_only` 反而**故意不写**）。
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
/// 不下来**：`parseModrinthIndex` 在"一条文件都没有"时判整包无效
/// （`src/manage/ModpackImport.cpp:379`）。所以调用方**不能**导出一个
/// `spec.files` 为空的包 —— 那样出来的包我们自己都装不回去。一个都认不出来的
/// 实例要照实说（"没认出任何资源，整包都会是 overrides"），由用户决定要不要继续。
QByteArray buildModrinthIndex(const Spec &spec);

/// 拼出 `manifest.json`（UTF-8，紧凑）。`spec.refs` 为空也合法。
QByteArray buildCurseForgeManifest(const Spec &spec);

/// 清单的**正文** —— 按 `spec.format` 挑上面那两位里的一位。
///
/// NOTE: 它和 indexFileName() **是一对**：一个给名字、一个给内容，两边认的是同一个枚举。
/// 调用点（写 zip 那一步）因此**不必自己写一个 `format ==` 的分岔** —— 分岔多一处，
/// 就多一处"文件名叫 manifest.json、正文却是 Modrinth 那份"的机会，而那种包**打得
/// 出来、也读得回来**（.NET 读 zip 不看内容），只有别人的启动器会当场说不认识。
QByteArray buildIndexFile(const Spec &spec);

/// 这条能不能作为一条 `files[]` 写进 Modrinth 清单。**这是调用方的分岔口**，
/// 不是给序列化函数用的：假的时候那个文件要**进 overrides**，不是丢掉。
///
/// 判据三条：路径非空、至少一个地址、`sha1` 与 `sha512` **两个都有**（规范原文是
/// MUST）。为什么要卡哈希：清单里的 sha1 是安装时**唯一**的完整性判据 ——
/// `DownloadManager::fileAlreadyValid()` 在 sha1 是空串时直接点头（见
/// `src/download/DownloadManager.cpp:475`），于是半截文件也算"下好了"。
bool writableAsIndexFile(const MatchedFile &file);

/// 加载器的键 → Modrinth 依赖表里那一行叫什么。纯原版（空键）返回空串 = 不写这一行。
QString modrinthDependencyKey(const QString &loaderKey);

/// 加载器的键 + 版本 → CurseForge 的 `modLoaders[].id`（如 `forge-47.2.0`）。
/// 认不出来的加载器返回空串 —— 调用方要么照实说"这个包的加载器没法表达"，要么别导。
///
/// NOTE: NeoForge 在 **1.20.1** 上要插一段游戏版本（`neoforge-1.20.1-47.1.106`），
/// 因为那一代的 NeoForge 版本号本身就长这样。这一段是照 Prism 的做法来的
/// （官方只明文确认过 `fabric-x.y.z` 这一种写法），1.20.1 以外的版本不加。
QString curseForgeLoaderId(const QString &loaderKey, const QString &loaderVersion,
                           const QString &gameVersion);

/**
 * @brief 算一个文件的 CurseForge 指纹（`fileFingerprint`）。
 *
 * 算法：把文件**整个**读一遍，剔掉空白字节（`0x09 \t`、`0x0A \n`、`0x0D \r`、
 * `0x20 空格`）之后，对剩下的字节做 murmur2（MurmurHash2 32 位），**种子恒为 1**。
 *
 * NOTE: 这个函数**不缓存**。同一个文件会被算两遍的是调用方的事（PCL 那种
 * "路径+大小+改动时间 做 key" 的缓存该做在调用点，因为那儿才知道文件没变）。
 * NOTE: 内存是 O(1) 的：先数一遍"有几个非空白字节"，再喂第二遍。murmur2 的初值是
 * `种子 ^ 长度`，长度得先知道，所以**必须两趟**（或者把整个文件读进内存）。
 */
bool curseForgeFingerprint(const QString &path, quint32 *out, QString *errorOut);

/// 对一段内存字节算指纹（文件那条路走的就是同一套状态机）。
/// 单独开这个口子是给验证工程用的：钉住算法本身，不依赖任何外部文件。
quint32 curseForgeFingerprintOfBytes(const QByteArray &bytes);

// ---------------------------------------------------------------------------
// 枚举：实例目录里哪些文件要进包
// ---------------------------------------------------------------------------

/// 打包时的两个可选项。默认值**照 PCL**（存档与截图不进包）：这两个目录是"用户的
/// 个人数据 + 体积大头"，跟"这个实例用了哪些资源"没关系。关掉的那种要**照实数出来**
/// （见 CollectResult），不能悄悄少一堆文件。
struct IncludeOptions {
    bool saves = false;        ///< `saves/`（存档）
    bool screenshots = false;  ///< `screenshots/`（截图）
};

/// 要打进包的一个文件。`path` 用 `/` 分隔、相对实例目录 —— 正好就是两份清单里
/// `path` / `overrides/` 后面那一段，也是 ZipUtil 那套要写进压缩包的条目名。
struct InstanceFile {
    QString path;
    qint64 size = -1;
};

/// 一次枚举的结果。
struct CollectResult {
    /// 要进包的文件，**按路径排序**（打包是可复现的：同一个实例两次导出条目顺序一样）。
    QList<InstanceFile> files;
    /// 被排除掉的目录名（去重、保持顺序），以及**没进包的文件个数**。
    /// 这两个数只为了在结果里**如实说**："`logs/`、`natives/`、存档没进包（共 N 个文件）"。
    /// NOTE: `skippedFileCount` 的**口径是"没进包的文件"**，不只是"被排除表拦下来的文件"：
    /// 被排除掉的目录**里面的**文件也数进去（一个 `saves/` 是三百个文件，不是 0 个）。
    /// 那个数是给用户看"少了多少东西"的，只数直接命中的那些等于把大头漏掉。
    QStringList skippedDirs;
    int skippedFileCount = 0;
    /// 非空 = 这个目录根本没读成（枚举本身失败）。**不是**"包里一个文件都没有"。
    QString problem;
};

/**
 * @brief 把实例目录里该进包的文件列出来（排除表见 instanceExclusionReason）。
 * @param instanceDir 我们的实例目录（`versions/<实例名>/`）—— 它本身就是规范里那个
 * "Minecraft instance directory"，所以路径**不需要**任何换算。
 */
CollectResult collectInstanceFiles(const QString &instanceDir, const IncludeOptions &options);

/**
 * @brief 这条相对路径要不要排除。纯函数，为的是能直接断言（枚举那层只是照着它走）。
 * @param instanceId 实例目录名：`<实例名>.jar` 与 `<实例名>.json` 是这个实例自己的
 * 版本文件（真正该装什么由清单里的依赖管），**不**进包。
 * @return 空串 = 不排除；非空 = 排除的理由（一句给用户看的话）。**只按顶层那一段判** ——
 * 规则说的都是实例根底下的东西，`mods/logs/` 这种是另一回事，不跟着一起排。
 */
QString instanceExclusionReason(const QString &relativePath, const QString &instanceId,
                                const IncludeOptions &options);

// ---------------------------------------------------------------------------
// 哈希
// ---------------------------------------------------------------------------

/// 一个文件的哈希。导出要的三样：Modrinth 清单要 sha1 + sha512，CurseForge 要指纹。
struct FileHashes {
    QString sha1;
    QString sha512;
    /// `needFingerprint` 为假、或者算不出来时是 0（`fingerprintOk` 说了算）。
    quint32 fingerprint = 0;
    bool fingerprintOk = false;
};

/**
 * @brief 一个文件读**两趟**，把要的哈希全算出来。
 *
 * 为什么是两趟：指纹（murmur2）的初值是 `1 ^ 非空白字节数`，长度得先知道；
 * 而 sha1/sha512 是流式的。所以第一趟只数"剔掉空白之后有几个字节"，第二趟同时喂
 * 两个 QCryptographicHash 和那台指纹状态机。**不**把文件读进内存：一个 200 MB 的
 * mod 乘以并行线程数就是几个 GB。
 *
 * NOTE: `needFingerprint` 为假时只读一趟（Modrinth 那条路要的 sha1/sha512 一趟就够）。
 * NOTE: 同一个文件的两条路（这一趟 / `curseForgeFingerprint`）算出来的指纹必须一样 ——
 * build/verify 里钉着这一条，因为它俩是两份代码。
 */
bool hashFile(const QString &path, bool needFingerprint, FileHashes *out, QString *errorOut);

// ---------------------------------------------------------------------------
// 反查：本地文件换回"它是哪个项目/哪一版"
// ---------------------------------------------------------------------------
//
// 两份规范各有一条路，都是批量一发（不是一文件一发）：
// - Modrinth：`POST /v2/version_files`，体是 sha1 表，回一个"哈希 → 版本"的 map；
// 认不出来的哈希**不在 map 里**（不是报错）。
// - CurseForge：`POST /v1/fingerprints`，体是指纹表，回 `exactMatches[]` +
// `unmatchedFingerprints[]`。
// 两条路的请求体和回复解析都在这儿（纯函数），发请求那一步在调用点（要 HttpClient）。

/// Modrinth 的批量反查请求体：`{"hashes":[...],"algorithm":"sha1"}`。
/// 空表返回空 QByteArray —— 那一发**不该发出去**（空表的回复必然是空 map）。
QByteArray modrinthVersionFilesRequestBody(const QStringList &sha1s);

/**
 * @brief CurseForge 的批量反查请求体：`{"fingerprints":[1234567890, …]}`。
 *
 * NOTE(）：数组里是 **JSON 数字**（int64），**不是**字符串。
 * 这条 NOTE 从前写的是反的（"必须是字符串"），而那一面正是洞 —— 实测：
 * `["3816865428"]` 回 400（`invalid type: string … expected i64`），`[3816865428]` 回 200。
 * 发成字符串的代价不是"报个错"，是 **CurseForge 那一支整支不出声**：屏幕上只是那几格
 * 没有图标，而"这个资源本来就没传图标"长得一模一样（详见 .cpp 里那段）。
 */
QByteArray curseForgeFingerprintsRequestBody(const QList<quint32> &fingerprints);

/// 反查命中的一个 Modrinth 版本文件 —— 清单里那一条要的字段全在这儿。
struct ModrinthHit {
    QString projectId;      ///< 只为了在结果里报数/说名字（mrpack 的 `files[]` 用不上它）
    QString versionNumber;  ///< 同上
    QString filename;
    QString sha1;           ///< 是**接口给的那一份**的哈希，不是我们本地算的
    QString sha512;
    QString url;            ///< 下载地址（就写进 `downloads[]`）
    qint64 size = -1;
    /// 接口给的 `environment`。映射成清单里 `env` 那一对的规则见 .cpp：**只有在
    /// "只给客户端用"时才写那一段**，别的都不写（不写 = 两边都要，导入方不会少装）。
    QString environment;
};

/**
 * @brief 解析 Modrinth 的反查回复。
 * @return **键是那份文件自己的 sha1**（小写）—— 调用方拿本地那个文件的 sha1 去查。
 * 解析不了时 `problemOut` 非空（此时返回空表）。
 *
 * NOTE: 用的是回复里"哈希 → 版本"的**那一层键**（规范就是这么定义的），但取哪一份
 * 文件是按"文件自己的 sha1 与那个键相同"挑的。挑不到（理论上不该发生）**不认
 * 这一条**，绝不退回"版本里的第一个文件" —— 认错了的后果是往清单里写一个错的
 * sha1，导入方会**永远**校不过、每次都重下同一个文件。
 */
QHash<QString, ModrinthHit> parseModrinthVersionFiles(const QByteArray &json, QString *problemOut);

/// 反查命中的一个 CurseForge 文件。
struct CurseForgeHit {
    int projectId = 0;
    int fileId = 0;
    /// `isAvailable` 为假 = 不能分发：**不写进清单**（写了等于给出一个装不上的包），
    /// 退回 overrides 并单独报数。
    /// 假的**原因有两种**，报告里那句话只能认其中一种 —— 所以还带
    /// `availableKnown`：接口这一项**缺席**时 `toBool(false)` 也是假，而"接口没给这一项"
    /// 与"作者关掉了第三方分发"是两回事。不分开的话，报告会替接口认下一个它没做的决定
    /// （"作者关掉了"），而真实原因可能是接口换了字段名 —— 用户照着这句话去查作者的设置，
    /// 永远查不出所以然。
    bool available = false;
    /// 回复里**真的**有 `isAvailable` 这一项（见上面那段）。假 = 不知道能不能分发。
    bool availableKnown = false;
    QString displayName;
    QString fileName;
    QString downloadUrl;
    QString sha1;  ///< 接口 `hashes` 里 `algo == 1` 的那一条
    qint64 size = -1;
};

/// 解析 CurseForge 的反查回复。**键是那个文件自己的 `fileFingerprint`**（接口会原样
/// 回给我们）—— 不按位置对：批量回来的次序没有承诺，按位置取会静默错位。
/// 认不出来的指纹**不在表里**（接口把它们列在 `unmatchedFingerprints` 里）。
/// 毒指纹那一条也**不在**（见 isPoisonFingerprint）。
QHash<quint32, CurseForgeHit> parseCurseForgeFingerprints(const QByteArray &json,
                                                          QString *problemOut);

/// 那个"毒指纹"。它本来应该是某个具体文件的值，但接口偶尔会把它当"匹配上了"回来
/// （HMCL 专门为 issue #4597 打过这个补丁）—— 见到就当没找到，别往清单里写。
/// 拦的位置是 parseCurseForgeFingerprints：信息在**回复里那条记录自己**的
/// `fileFingerprint` 上，再往下走就已经被当成"我们发过去的某个指纹"了。
bool isPoisonFingerprint(quint32 fingerprint);

// ---------------------------------------------------------------------------
// 把上面这些串成一次导出
// ---------------------------------------------------------------------------

/// 一次导出要的输入（界面上那几个框 + 实例自己的元数据）。
struct ExportRequest {
    Format format = Format::Modrinth;
    QString instanceDir;  ///< 我们的实例目录（`versions/<实例名>/`）
    QString name;         ///< 整合包名字（配置框里填的）
    QString version;      ///< 整合包自己的版本号
    QString summary;      ///< Modrinth 那份有；CF 那份没有这一格
    QString author;       ///< CF 那份有
    QString gameVersion;  ///< `wtfml.meta` 的 realVersion
    QString loaderKey;    ///< 用 ModpackImport 那套键（`fabric-loader` / `forge` / …）
    QString loaderVersion;
    IncludeOptions include;
};

/// 进度：`done` / `total` 按**要算哈希的文件**计，`current` 是正在处理的相对路径。
/// NOTE: 从**调用 buildExportPlan 的那个线程**同步回调（不会蹦到别的线程上去）——
/// 界面上要刷新就自己把它挪到主线程（或者干脆整个 buildExportPlan 都放后台线程跑，
/// 那是最省事的用法：它本来就是一段"慢但不需要界面"的活）。
using ExportProgress = std::function<void(int done, int total, const QString &current)>;

/// 取消：返回真 = 用户叫停（在文件之间检查）。空函数 = 不取消。
using ExportCancel = std::function<bool()>;

/// 两条反查路的"接口"。返回假 + `problemOut` 里一句人话 = **这一发没成**
/// （网络断了、限流、回复读不懂）。产品里由 HttpClient 那一侧实现，验证工程里给假的。
using ModrinthQuery = std::function<bool(const QStringList &sha1s,
                                        QHash<QString, ModrinthHit> *out, QString *problemOut)>;
using CurseForgeQuery = std::function<bool(const QList<quint32> &fingerprints,
                                          QHash<quint32, CurseForgeHit> *out, QString *problemOut)>;

/**
 * @brief 这份清单「我们自己装得回来吗」= 清单里**有没有条目**。
 *
 * 空清单的包在规范里合法（一个纯 overrides 的包），但**我们自己的解析器一律拒收**
 * （Modrinth 那一侧见 `ModpackImport.cpp:379`，CF 那一侧见 `:597`）—— 所以调用点
 * 导出前必须拿它问一句：假的时候要给用户一句人话，而不是给出一个连自己都装不回来的包。
 * 用户手上那个包也可能是这么来的（别人的工具导的），那时候走的是导入方的话术，不归这里管。
 */
bool reimportable(const Spec &spec);

/// 一次导出的产物：清单要的东西 + 哪些文件要原样打进 overrides/。
struct ExportPlan {
    Spec spec;
    /// 实例目录**原样带上**：`overrides` 里的条目只有相对路径，打包那一步要拿它拼出盘上的
    /// 位置（见 archiveEntries）—— 于是 `writePackArchive()` 只吃一份 plan 就够，不必
    /// 让调用点把 request 再传一遍。
    QString instanceDir;
    /// 要原样打进 `overrides/` 的（相对路径口径与 `spec.files` 的 `path` 一致）。
    /// **认出来的文件绝不在这里**（重复内容），认不出来的一个都不会丢。
    QList<InstanceFile> overrides;
    CollectResult collect;      ///< 枚举那一层原样带出来（被排掉的目录要照实报）
    QStringList hashFailures;   ///< 连哈希都算不出来的文件（它们也只能进 overrides）
    int matched = 0;            ///< 认出来几条
    int notDistributable = 0;   ///< CF：认出来了，**而且接口明说**作者关了第三方分发
    /// CF：认出来了，但接口**没说**能不能分发（回复里没有 `isAvailable` 这一项）——
    /// 和上面那个同样退回 overrides，但**不能**报成"作者关掉了"（见 CurseForgeHit 那段）。
    int notDistributableUnknown = 0;
    /// CF：认出来了，但**这个名字带不住状态**（`.disabled` / `.old` 后缀，或者用户自己
    /// 改过名）—— 写进清单等于让收包的人那边落回作者给的原名（禁用着的会变成启用的）。
    /// 退回 overrides 原样带上（见 .cpp 里判那一条那段）。
    int keptLocalState = 0;
    int queried = 0;
    bool lookupFailed = false;  ///< 反查那一发没成 —— 要不要继续**由用户决定**（不是错误）
    QString lookupProblem;      ///< 没成的那一句（直接给用户看）
    /// 用户叫停了。**这时候整份 plan 都作废**（`overrides` 里可能有半截的东西，
    /// 别拿来用、也别拿来报数）—— 调用点该做的是把临时产物删掉、当没这回事。
    bool cancelled = false;
    QString problem;            ///< 非空 = 整件事停下来（枚举那一层就出错了）
};

/**
 * @brief 跑完一次导出：枚举 → 算哈希 → 批量反查 → 拼出清单与 overrides。
 *
 * 全程**不碰网络**（反查那两发由传进来的 query 走）、**不写文件** —— 所以这一整层
 * 可以在验证工程里用假接口跑，把"认出来的进清单、认不出来的进 overrides"这条
 * 一条一条钉住。
 *
 * NOTE: 反查失败（`lookupFailed`）**不是**错误：照 PCL 那套，界面要问一句
 * 「联网获取部分文件信息失败，是否继续导出？（继续的话，认不出信息的文件会被
 * 直接打包进去）」—— 用户说继续，那就是一个挂在 overrides 上、断网也能做出来的包。
 * NOTE: 哈希这一步现在是**串行**的。并行（QThreadPool）是留给以后的优化，不是漏了：
 * 进度回调是从调用线程同步发的，多线程之后"进度从哪个线程来"就成了契约的一部分，
 * 而这一步的 90% 时间在等网络（反查那一发）而不是在算哈希。
 */
ExportPlan buildExportPlan(const ExportRequest &request, const ModrinthQuery &queryModrinth,
                           const CurseForgeQuery &queryCurseForge,
                           const ExportProgress &progress = ExportProgress(),
                           const ExportCancel &cancel = ExportCancel());

// ---------------------------------------------------------------------------
// 落成 zip 的条目表（条目名这一层是纯函数，真去写 zip 的是 ZipUtil::createZip）
// ---------------------------------------------------------------------------

/// 清单在 zip 根叫什么。Modrinth 那份的名字是规范钉死的，CF 那份也是。
QString indexFileName(Format format);

/// overrides 那一层的目录名（zip 里那个前缀）。**吃的是整份 spec**：两边口径不同，
/// 见 .cpp 里那段（Modrinth 钉死 `overrides`，CF 读 `overridesDir`）。
QString overridesPrefix(const Spec &spec);

/**
 * @brief 把一份 plan 摊成"归档条目表"：清单一条 + 每个 overrides 文件一条。
 * @param plan 跑完的那一趟（实例目录从 `plan.instanceDir` 拿 —— `InstanceFile` 里只有
 * 相对路径）。
 * @param manifestPath 清单**已经写到盘上**的那份（调用点先按 indexFileName() 写进
 * 暂存目录 —— 这一层不写盘，见 .hpp 顶上那句）。
 * @return 条目名一律 `/` 分隔、**清单在第一条**、其余按路径排序（同一个实例两次导出的
 * 归档顺序一样）。`overrides` 里那些文件本身就带相对路径，前面补一层前缀。
 *
 * NOTE: 名字拼错是**静默**的：包照样打得出来、我们自己（.NET）也读得回来，只有别人的
 * 启动器认不出 —— 所以条目名这几条规则在这儿是纯函数，验证工程里一条一条钉着。
 */
QList<ZipUtil::ZipSource> archiveEntries(const ExportPlan &plan, const QString &manifestPath);

/// 「这个文件值不值得拿去反查」—— 一个**代价**上的取舍，不是正确性上的：
/// 只对像资源的文件（几种扩展名 + 路径里带 mods/resource 之类）算哈希，否则一个包里
/// config/ 底下成千上万个小文件都要算一遍、发一遍，导出会慢到没法用。
/// 认不出来的照样进 overrides，只是"不问接口"而已（见 .cpp 里那张表）。
bool looksLikeResolvableResource(const QString &relativePath);

}  // namespace ModpackExport
