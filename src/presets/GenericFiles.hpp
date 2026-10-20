/**
 * @file GenericFiles.hpp
 * @brief 通用文件组那一段（`config/` / `defaultconfigs/` / 资源包 / 光影包）的**判据层**：
 *        这一组里有什么、每一项这一趟怎么落、以及每一条路径能不能碰（coding plan §6 P3）。
 * @author 0x7fffff
 * @date 2026-10-15
 *
 * 【这一段和「原版设置」「服务器列表」那两段的根本不同：我们**看不懂**里面的东西】
 *
 * `options.txt` 有键值、`servers.dat` 有 `ip` / `name`，所以那两段能"只改该改的那几格"。
 * 一个 `config/sodium-options.json` 对我们是**一坨字节**：它是什么结构、哪些是用户调过的、
 * 模组自己会不会重写它 —— 一概不知道。硬去合并一坨字节，最可能的结果是**把用户调好的那份
 * 弄坏**（JSON 少一个逗号、键序变了模组认不出来、`.toml` 里那段注释没了）。
 *
 * 所以这一段的规矩是**只做整份的动作**，一件也不"部分修改"：
 *
 *   · `CopyIfMissing` —— 目标上**没有**那一份才拷（"我这份是干净的、用户还没调过"那一档）。
 *     ★ 逐文件判：一项是一个目录时，缺哪个文件补哪个（用户已经调过的那几份一个都不动）。
 *   · `Replace` —— 目标上**同名的那一份换成预设里的**（用户改过也换）。
 *     ★★ **不删目标上多出来的文件**。整棵子树替换（先把目标那一项删掉再拷）听起来更"干净"，
 *        但那会顺手删掉用户自己的东西 —— 而那**不可恢复**（§8.1 的备份是逐文件的，删掉的
 *        东西它也不认）。"覆盖"这两个字在同一段里的含义必须只有一条：**同名的那份换成我的**。
 *   · `Skip` —— 这一项不动。
 *   · `Merge` / `Append` —— **对一坨字节没有意义**（那要一份懂它结构的适配器，P4 的 MaLiLib
 *     才是第一份）。所以这两档下每一项都落 `NeedsChoice`，**一个字节都不写**、条数报出来，
 *     由界面去问用户"这些只能复制或覆盖，你要哪个"（§6 P3 最后那条「未知配置明确标记为
 *     '只能复制/覆盖'」）。**不猜**：默认档（`Merge`）下悄悄按复制做，用户以为合并过的
 *     那份配置里其实有几格是旧的 —— 那种账没人对得出来。
 *
 * 【"只能复制/覆盖"是一个**按项**记的记号，不是一句注释】
 *
 * `GenericItem::copyOnly` 和段载荷里那一格 `"copyOnly"` 就是它。今天这一组里**绝大多数**
 * 东西都是 `true`；P4 接上 MaLiLib 那一族之后，`config/` 底下**按结构**认得出是那一族的
 * 那几份回 `false`（判据在 `genericItemIsCopyOnly()` **那一处**，界面和报告都不用动）。
 *
 * 【路径：三处闸，缺一不可】
 *
 * §6 P3 点名「路径穿越、绝对路径和符号链接逃逸检查」。这个文件里那三道是：
 *   1. `genericPathIsSafe()` —— 名字本身：相对路径、不许 `..` 或 `.` 那一段、不许空段、
 *      不许绝对的（`C:` / `/` / `\\`），每一段还要过 `InstanceFiles::validatePathSegment()`
 *      （Windows 上那些非法字符在实例里本来就落不下去）；
 *   2. `genericPathBelongsToGroup()` —— 这一条路径**必须落在它自称的那一组底下**（段载荷
 *      里写着 `group=config` 而路径是 `shaderpacks/x.zip` 的，整段不认）；
 *   3. `InstanceFiles::pathIsSameOrInside()` —— 落盘那一刻，源和目标**都**要再判一次
 *      "还是不是在这一组目录底下"。★ 前两道判的都是**字面的**路径，而 junction 的
 *      `canonicalFilePath()` 解不开、`isSymLink()` 也认不出（见 `InstanceFiles.hpp` 那段）；
 *      真正能挡住"预设里一个 junction 指到 `C:\\Windows`"的就是这第三道，而它**只有一份
 *      实现**（复制实例那条路用的是同一个）。
 *
 * 链接（symbolic link / junction）本身一律**不碰**：源那一侧碰到就拒绝这一项（不顺着一根
 * 链接把外面那棵树拷进来），目标那一侧碰到就拒绝往它里面写（不然"覆盖一份配置"会变成
 * "改掉链接那头某个别的目录里的东西"）。★ 目标那一侧**每一层**都判（不只最后那个文件
 * 名）：目标上某一层目录是链接时，接着往下递归就写到链接那头去了。★ 判链接在判逃逸
 * **之前**：两件事都会拒，但"往一根链接里写"是一句能让人直接去 `mklink` 那儿看的话，
 * 而"在这一组目录外面"只说了现象。判法就是 `isJunction() || isSymLink()` —— 全仓
 * 四处都这么写（`BackupFiles` / `FolderManager` / `ModpackExport` / 这里），因为它俩
 * **必须一起判**：junction 的 `isSymLink()` 是 false。
 *
 * 【这一层也不开 `QFile` 写自己那套落盘】
 *
 * 每一份文件都是**先写临时文件再原子换**（`QSaveFile`）—— 一份写到一半的配置比没有还糟。
 * 但**一项里那几份文件之间不是事务**：第 3 份失败就当场收手，前两份留着（要整趟回滚得靠
 * §8.1 那一层的备份，那是应用引擎的事，不是这一层）。这一点写在报告的用法里：失败时
 * `ok = false`，已经写下去的那些**留在盘上**（和 `copyTreeWithoutFollowingLinks()` 一样）。
 */

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "PresetTypes.hpp"

namespace Presets {

// ---------------------------------------------------------------------------
// 四个落点
// ---------------------------------------------------------------------------

/**
 * @brief 这一组是哪一组（§6 P3 点名的四个）。
 *
 * NOTE: 次序**就是界面上那个次序**（`allFileGroups()`）—— 换它等于换用户看见的次序。
 */
enum class FileGroup {
    Config,          ///< `config/`（模组的配置文件）
    DefaultConfigs,  ///< `defaultconfigs/`（Forge 那些"新实例的默认配置"）
    ResourcePacks,   ///< `resourcepacks/`
    ShaderPacks,     ///< `shaderpacks/`
};

/**
 * @brief 这一组在实例里的**目录名**（`config` / `defaultconfigs` / `resourcepacks` /
 *        `shaderpacks`）。
 *
 * ★ **一个词一个意思**：段载荷里也用它、路径的第一段也是它 —— 不另立一套"键"和"目录名"
 *   的说法，那样迟早对不上。
 * ★ 资源包和光影那两个的名字**不是在这儿定的**：它们还得和"下载页把资源包下到哪儿"一致，
 *   而那件事的唯一出处是 `ResourceCatalog::folderForKind()`（见 `MainWindow.hpp` 那段
 *   「目录名的唯一出处是 ResourceCatalog 那张表」）。所以这一口去问它，不自己写一遍。
 */
QString fileGroupDirName(FileGroup group);

/// 认目录名（认不出返回 false）。**只有这一处**认这四个词。
bool fileGroupFromDirName(const QString &dirName, FileGroup *out);

/// 四组的次序（= 界面上的次序，也是"哪一组先落"的次序）。
QList<FileGroup> allFileGroups();

/// 这一条**相对实例根**的路径属于哪一组（第一段就是那一组的目录名）。
/// 不属于这四组里任何一个 ⇒ false（`config/a.json` ✓、`mods/x.jar` ✗、`a.json` ✗）。
bool fileGroupOfPath(const QString &relativePath, FileGroup *out);

// ---------------------------------------------------------------------------
// 名字与路径
// ---------------------------------------------------------------------------

/**
 * @brief 这一条相对路径能不能碰（见文件头第 1 道闸）。
 *
 * 拒的：空串 / 绝对的（`C:`、`/`、`\\`）/ 含 `..`、`.` 或空段（`a//b`）/ 某一段过不了
 * `InstanceFiles::validatePathSegment()`。
 *
 * @param problemOut 拒的时候写一句给人看的话（成功时清空）。
 */
bool genericPathIsSafe(const QString &relativePath, QString *problemOut = nullptr);

/// 把根目录和一条相对路径拼起来（`/` 分隔；两边都不带多余的斜杠）。
QString genericJoined(const QString &root, const QString &relativePath);

/// 这一条路径**在盘上**是不是一根链接（symbolic link / junction，见文件头）。
/// 不存在的返回 false（"没有"不是"链接"）。
bool genericPathIsLink(const QString &absolutePath);

// ---------------------------------------------------------------------------
// 扫这一组
// ---------------------------------------------------------------------------

/**
 * @brief 这一组里的一项。
 *
 * ★ **一项 = 这一组目录底下第一层的某一个东西**（一个文件，或者一棵子树）。不拆到文件级：
 *   用户嘴里的"这一份配置"/"这个资源包"就是这一层，而"覆盖"这件事按文件级问也更没意义
 *   （一个光影包两百个文件，问两百遍吗）。子树里的文件是按它整棵走的，见 .cpp。
 */
struct GenericItem {
    /// 相对**实例根**的路径，用 `/`（例：`config/sodium-options.json`、`shaderpacks/BSL.zip`）。
    QString relativePath;
    /// 这一项是一棵子树（`config/某个模组/`）而不是一个文件。
    bool directory = false;
    /**
     * ★ §6 P3 最后那条记号：这一项**只有复制/覆盖两种做法**（它的结构我们认不出来，合并
     *   它等于拿用户的配置去赌）。
     */
    bool copyOnly = true;
    /// 多大（字节）。目录是整棵的和（不算链接那一头的东西）—— 只用于显示。
    qint64 sizeBytes = 0;
};

/**
 * @brief 这一项是不是**只有复制/覆盖两种做法**（§6 P3 最后那条记号，见文件头）。
 *        ★ 这个记号的**唯一出处**。
 *
 * 判法分两截：
 *   · **名字那一截**（只看 @p group + @p relativePath）：不是 `config/` 底下的 `.json`
 *     就一律 `true` —— 资源包、光影包、`defaultconfigs/` 那些我们一个字都看不懂。
 *   · **结构那一截**（给了 @p absolutePath 才算）：`config/<xxx>.json` 里凡是 `MaLiLib`
 *     那一族**按结构**认得出（`malilibLooksLikeConfig()`）的都回 `false` —— 那一族有 P4 的
 *     适配器，它就不是"只能整份复制"的东西了。
 *
 * ★ **按结构认、不按名字认**（§6 P4 那条）：`config/tweakermore.json` 这种名字我们一个都
 *   不认识，可它的结构一眼就是那一族的 —— 所以判据在**内容**上，模组名的表一张都不立
 *   （立了也永远不全）。
 * ★ 不给 @p absolutePath 就只能按名字那一截算（段载荷里只有相对路径，那一处没有盘上的
 *   位置）。**要紧的那一处是带位置的那些**：扫盘（`scanFileGroup()`）和算计划那一档
 *   （`planFileGroup()`，那儿拿源那一侧的文件看）。
 * ★ 读不动 / 太大 / 不是文件 ⇒ 一律回 `true`（"看不出来"只能按"不认识"办，反过来会让用户
 *   以为我们能合并一份**根本没读进来**的配置）。
 */
bool genericItemIsCopyOnly(FileGroup group, const QString &relativePath,
                           const QString &absolutePath = QString());

/// `config/<xxx>.json` 这一份文件里**按结构**认得出是 MaLiLib 那一族吗（见上面那一条）。
/// 读不动就 false。**读文件的那一处**在这儿（别处再开一遍 `QFile` 就是第二份实现）。
bool genericConfigLooksStructured(const QString &absolutePath);

/**
 * @brief 扫一组（**两侧都能扫**：实例那侧，或者预设 `files/` 那侧）。
 *
 * 这一组的目录不在 ⇒ 成功 + 空清单（"这一组是空的"不是失败）。
 * 名字不安全 / 是链接的那些**不进清单**，逐条报在 @p refusedOut 里（带一句原因）——
 * 悄悄略过是不行的：用户看到的清单会和盘上对不上。
 */
bool scanFileGroup(const QString &root, FileGroup group, QList<GenericItem> *out,
                   QStringList *refusedOut = nullptr, QString *problemOut = nullptr);

// ---------------------------------------------------------------------------
// 计划
// ---------------------------------------------------------------------------

/// 一项这一趟会怎么样。
enum class GenericAction {
    Replace,       ///< 同名的那一份换成预设里的
    CopyIfMissing, ///< 目标上没有那一份才拷（逐文件判）
    Skip,          ///< 这一项不动
    NeedsChoice,   ///< 这一档下我们做不了主（`Merge` / `Append` 落在这一坨字节上）
};

/// 那一档的中文名（§7 那张计划表里写的词）。**唯一出处**。
QString genericActionName(GenericAction action);

struct GenericPlanItem {
    QString relativePath;
    GenericAction action = GenericAction::NeedsChoice;
    /// 为什么是这一档（`NeedsChoice` 那一档就是给用户看的那句话）。
    QString reason;
};

struct GenericPlan {
    bool ok = false;
    QString problem;

    QList<GenericPlanItem> items;
    /// 四项计数（都按**项**算，不按文件）。
    int replaced = 0;
    int copied = 0;
    int skipped = 0;
    int needsChoice = 0;

    int total() const { return static_cast<int>(items.size()); }
    /// 这一趟要不要动盘（`NeedsChoice` 和 `Skip` 都不算）。
    bool writesAnything() const { return replaced + copied > 0; }
};

/**
 * @brief 算这一趟每一项的档（纯值）。
 *
 * `Merge` / `Append` 下每一项都是 `NeedsChoice`（理由见文件头）；`Skip` 下全是 `Skip`。
 * ★ `ok = false` 只有一条来路：哪一项的路径不安全（**整组不认**，不是"跳过那一项"——
 *   一份自己带着非法路径的预设，我们没资格接着往下走）。★ 这时 @p out 的清单和那四个
 *   计数**都是空的**（那一条是逐项判的，坏项在尾巴上时前面几项已经攒进计数了 —— 留着的话
 *   读者看 `writesAnything()` 会以为"这一趟要写 4 项"，而其实一个字节都不会写）。
 *
 * @param sourceRoot 源那一侧的文件根。★ 要它是因为 `CopyIfMissing` 那一档得**干跑一趟**
 *        （去数目标上缺哪几份）才能报对："一项底下每一份目标上都有"是 `Skip` 而不是
 *        "复制了 0 个文件"—— 那两个数在报告和界面上差得很远。
 */
GenericPlan planFileGroup(const QString &sourceRoot, const QList<GenericItem> &items,
                          const QString &targetRoot, ApplyMode mode);

// ---------------------------------------------------------------------------
// 落盘
// ---------------------------------------------------------------------------

/// 这一趟的报告（界面按它摆"配置文件 复制 12 项"那种行，见 §7）。
struct GenericReport {
    bool ok = false;
    QString problem;

    /// 按**项**算。
    int replaced = 0;
    int copied = 0;
    int skipped = 0;
    int needsChoice = 0;
    /// 真的写下去的那几份**文件**（一项是一棵子树时不止一份）。
    int filesWritten = 0;
    /// 名字不安全 / 是链接、所以连列都没列进来的那些（带原因的一句话）。
    QStringList refused;
    /// 逐文件判"缺了才拷"时，目标上已经有、所以没动的那些。
    QStringList skippedPaths;

    bool writesAnything() const { return replaced + copied > 0; }
};

/**
 * @brief 把预设那一侧的一组落到 @p targetRoot 那一份实例上。
 *
 * @param sourceRoot 预设那侧的文件根（`PresetStore::filesDir(root, id)`）。
 * @param targetRoot 实例目录（`<版本目录>`）。
 *
 * ★ 用的是 `planFileGroup()` 算出来的那个计划（报告里那些数和"真的做了什么"同源）。
 * ★ `ok = false` 时报告里说清楚；**已经写下去的那些留在盘上**（见文件头末尾那段）。
 *
 * ★ 清单**从 `sourceRoot` 盘上扫**（见下面那个重载：要按清单落的那一路上）。扫的时候
 *   组根自身是链接 ⇒ 整组失败（`scanFileGroup()` 那一档）。
 */
GenericReport applyFileGroup(const QString &sourceRoot, const QString &targetRoot, FileGroup group,
                             ApplyMode mode);

/**
 * @brief 同上，但**落的就是 @p items 这一份清单**（不再重扫 @p sourceRoot）。
 *
 * 预设管理页 "应用" 走的是这一档：清单来自预设段（计划、备份范围都用它算）。
 * 盘上那份 `files/<组>` 目录里可能有**清单之外**的旧文件（提取是"覆盖同名、多出来的不删"），
 * 重扫会让它们一起落进实例 —— 计划里没提、备份里也没有。
 *
 * ★ 清单里列了、盘上却不在的那一项：报成 `ok = false`（`walkItem()` 的那句话），
 *   和"缺了一份"这件事一致，**不静默少拷**。
 */
GenericReport applyFileGroup(const QString &sourceRoot, const QString &targetRoot, FileGroup group,
                             ApplyMode mode, const QList<GenericItem> &items);

/**
 * @brief 反过来：把实例上的这一组**收进**预设的 `files/` 里（提取那一趟）。
 *
 * `Replace` 那一档的语义（同名的一律换成现在的这一份，**多出来的不删**）—— 提取是"把现在
 * 这一份收下来"，收的时候删东西没有任何道理。
 */
GenericReport extractFileGroup(const QString &sourceRoot, const QString &presetFilesRoot,
                               FileGroup group);

// ---------------------------------------------------------------------------
// 预设段 ⇄ 这一组
// ---------------------------------------------------------------------------

/// 载荷形状：`items` = `[{"path": "config/a.json", "copyOnly": true}, …]`（**保序**）。
///
/// ★ **不另写一格 `group`**：这一组是哪一组**就是路径的第一段**（`config/…` 落在 `config`），
///   而那是同一个事实的第二种写法 —— 两份说法迟早对不上（"写着 `config` 而每一条路径都在
///   `shaderpacks/` 底下"这种段我们该拿它怎么办？）。段的身份（哪一组）在 `id` 里
///   （`sectionIdFor(SectionKind::GenericFiles, 目录名)`），载荷只放"有哪些项"。
bool sectionsFromFileGroup(FileGroup group, const QList<GenericItem> &items, ApplyMode mode,
                           QList<PresetSection> *out, QString *problemOut = nullptr);

/// 从段的 `id` 反推这是哪一组（界面要按它摆标题、决定往哪个目录落）。
/// 不是这一层的四段之一 ⇒ false。
bool fileGroupFromSectionId(const QString &sectionId, FileGroup *out);

/**
 * @brief 反过来：从一段预设里把这一组读回来。
 *
 * `ok = false` 的来路：`kind` 不归这一层管 / 项不是对象 / `path` 缺了、不是字符串、或者
 * **不安全** / `path` **不属于这四组里任何一个** / **同一段里的项落在了不同的组上**。
 * 读不回来的段**不许拿去改用户的实例**。
 *
 * ★ 一组是哪一组**由各项的路径自己说了算**（第一段），所以调用方拿到的清单里那些项必然是
 *   同一组的；空段也算成功（"这一组是空的"，一个字节都不写）。
 */
bool fileGroupFromSection(const PresetSection &section, QList<GenericItem> *out,
                          QString *problemOut = nullptr);

/// 这一段的种类归不归这一层管（`GenericFiles`）。应用那一层按它分派。
bool genericFilesHandlesSection(SectionKind kind);

}  // namespace Presets
