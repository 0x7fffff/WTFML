/**
 * @file ServerListPreset.hpp
 * @brief 服务器列表那一段（`servers.dat`）的**判据层**：地址怎么算同一台、Append / Replace /
 * Skip 各是什么意思、这一趟会动哪几条。
 *
 * 【这一段和「原版设置」那一段最大的不同：这是一个**列表**，不是一张表】
 *
 * `options.txt` 是一张"键 → 值"的表，所以原版设置那边每一格天然只有一个正确值，"合并"就是逐条
 * 改。`servers.dat` 是一串**顺序有意义**的条目（用户在游戏里排过的顺序），所以：
 *
 * · 追加 **不许重排已有的**（新条目一律接在表尾 —— 原版在游戏里点"添加服务器"也是接在
 * 尾巴上，见 `ServerListFile::addEntry()`）；
 * · 「同一台服务器」这件事**得先判**（不然追加第二遍就得到两张一模一样的行）；
 * · 而"覆盖"对这一份来说是**把整张表换掉**，比 `options.txt` 那一档狠得多 —— 所以它
 * 同样必须用户明确确认过（那条规矩在这一段照旧）。
 *
 * 【怎么算"同一台服务器"】
 *
 * `normalizeServerAddress()`：拆成主机 + 端口（走 `ServerPing::splitAddress()` —— 地址怎么
 * 拆**只有那一处实现**），主机折成小写、端口补上默认的 25565，再拼回来。于是
 * `MC.Example.com`、`mc.example.com:25565`、`mc.example.com` 都归一成 `mc.example.com:25565`。
 *
 * 这是一条**判据**，不是事实。它的用途只有一个：**决定"要不要再添一条"**。所以：
 * · 判成"同一台"的那些**不是被悄悄丢掉**的 —— 条数和地址都写在报告里（`skippedAddresses`），
 * 界面上能说出「这 3 台已经在列表里了」；
 * · 有一条真例外要记住：**发过 SRV 记录的服务器**上，`mc.example.com` 和
 * `mc.example.com:25565` 可能真的不是同一台。我们照"是同一台"处理（那是绝大多数情况，
 * 也是用户写下这两个地址时心里想的那件事），代价是这一档漏添一条，用户看得见。
 * · 认不出来的地址（`splitAddress()` 说 valid=false）**只去两头空白、一个字都不改写** ——
 * 把看不懂的地址悄悄"修"成默认端口，换来的是一句假的"这两条是同一台"
 * （`ServerPing::splitAddress()` 那段 ★ 是同一条取舍）。
 *
 * 【认不出来的一律不删】
 *
 * 【这一层动盘，但只在"真要改一条"的时候动】
 *
 * 判据（`planServerList()` / `planServerListApply()`）是纯值，一条盘都不碰。
 * `applyServerListToInstance()` 才走文件：读 → 照计划改那棵树 → 原子写回。**写回去的形状
 * 跟着读进来那一份**（原来是 gzip 就还是 gzip），文件不在而又有东西要加时按
 * `ServerListFile::kFreshFileGzipped` 建一份新的 —— 两条规矩都不在**这里**定，见那边。
 * "这一趟一条都不用动"（全部重复、或者 Skip）时**连写都不写**：把一个字节没变的文件
 * 重写一遍，只会在用户的实例里留下一个变了 mtime 的文件。
 */

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "PresetTypes.hpp"
#include "util/NbtTree.hpp"
#include "util/NbtUtil.hpp"

namespace Presets {

// ---------------------------------------------------------------------------
// 判"是不是同一台"
// ---------------------------------------------------------------------------

/**
 * @brief 归一化地址（见文件头那一节）。**认不出来的原样回去**（只去两头空白）。
 *
 * 空串归一成空串 —— 调用方那边"没有地址的条目"在读的时候就进不来（`serverEntriesOf()`
 * 跳的就是它们，和 `NbtUtil::readServerList()` 同一条规矩），所以这个角落到不了
 * "两条空地址被当成同一台"那种事上。
 */
QString normalizeServerAddress(const QString &address);

/// 两条地址是不是同一台（= 归一化之后逐字相同）。
bool sameServerAddress(const QString &left, const QString &right);

// ---------------------------------------------------------------------------
// 一棵 `servers.dat` 树 → 条目
// ---------------------------------------------------------------------------

/**
 * @brief 从一棵已经解开的 `servers.dat` 里取出条目（**保序**）。
 *
 * 判"这个元素算不算一条服务器"用的是**和读侧同一套**：复合标签、且 `ip` 那一格是非空
 * 字符串（`NbtUtil::readServerList()` 就是这么跳的）。不这么对齐的话，界面上第 N 行和
 * 树里第 N 个元素对不上 —— `ServerListFile` 文件头第 3 条纪律防的正是这件事。
 *
 * @param untouchableOut 认不出来的元素条数（不是复合标签的、没有 `ip` 的）。
 * @return `servers` 那一格不在 / 不是一张表 ⇒ false（同 `ServerListFile::serversListOf()`）。
 *
 * 收的是**可变**的那棵树（不是 `const`）：认 `servers` 那一格和校验它只有一处实现
 * （`ServerListFile::serversListOf()`），而它是给"要改树"的人写的。这一趟本身一格都不动。
 */
bool serverEntriesOf(NbtTree::Document *document, QList<NbtUtil::ServerEntry> *out,
                     int *untouchableOut = nullptr, QString *problemOut = nullptr);

// ---------------------------------------------------------------------------
// 计划
// ---------------------------------------------------------------------------

/**
 * @brief 这一趟会写哪几条（纯值）。计划和执行**走同一段代码**（`applyServerListToInstance()`
 * 用的就是它），不然迟早出现"计划说添 3 条、实际添了 2 条"。
 */
struct ServerListPlan {
    bool ok = false;
    /// 人话（`ok = false` 时才有）。
    QString problem;

    /// 真要写进去的那几条（**保序**、已经按归一化去过重）。★ 目标上已经有同一个地址的
    /// 那几条**不在这里** —— 它们进 `skippedAddresses`。
    QList<NbtUtil::ServerEntry> accepted;
    /// 因为目标上已经有一台同一个地址、所以这一趟没写的那些。
    QStringList skippedAddresses;
    /// `accepted` 里目标上原来没有同一个地址的条数（"添了一条新的"）。
    int addedCount = 0;
    /// 目标上"认得出是服务器"的条数（「覆盖」档要拿掉的就是它们）。
    int targetEntryCount = 0;
    /// 目标上认不出来的元素条数（**任何一档都不动它们**，见文件头）。★ 从参数抄进来的。
    int targetUntouchable = 0;

    int acceptedCount() const { return static_cast<int>(accepted.size()); }
    int skippedCount() const { return static_cast<int>(skippedAddresses.size()); }
};

/**
 * @brief 算这一趟的判据。
 *
 * 各档的含义（`ApplyMode` 见 `PresetTypes`）：
 * · `Append` —— 把预设里**目标上没有的**接在表尾（保序、按归一化去重）。
 * · `Merge` / `CopyIfMissing` —— 对一张列表来说和 `Append` 是同一件事（"合并"就是"添进去"，
 * 而"缺了才拷"逐条看正是"目标上没有才添"）。写在这儿是为了让"为什么这三个一样"有个
 * 出处 —— 不是漏了两档。
 * · `Replace` —— 目标上那几条里，**地址在预设里还找得到的就地改名**、找不到了的拿掉，
 * 预设里目标上原来没有的接在表尾（认不出的元素一个都不动）。
 * 不是"清空整张表再照预设重建"：重建会把每一条都换成只有 `ip` + `name` 两格的新节点，
 * 顺手抹掉盘上原来那些 `icon` / `hidden`（见 `ServerListFile.hpp` 那条"只动 name 和 ip
 * 两格"的纪律）。代价是留下的那几条**位置照目标原来那份**，接在表尾的才是预设的顺序。
 * · `Skip` —— 一条都不动（`accepted` 空、源里那几条全算"没写"）。
 *
 * 校验也在这儿：源里有一条**地址是空的**⇒ 整份不认（`ok = false`）—— 地址是这一条唯一的
 * 实质内容，写进去的那一行在游戏里是一条点不动的空气（同 `ServerListFile::addEntry()`）。
 */
ServerListPlan planServerList(const QList<NbtUtil::ServerEntry> &target, int targetUntouchable,
                              const QList<NbtUtil::ServerEntry> &source, ApplyMode mode);

/**
 * 几个计数的名字和 `VanillaOptions::OptionsApplyReport` 是**故意对齐的**（`written` /
 * `added` / `removed` / `skipped` / `untouchable`）—— 计划表要拿同一段代码把各段念
 * 出来。类型上还没有合成一个（两段的"冲突"那一格形状不同），等应用引擎那一层落地时再说。
 */
struct ServerListReport {
    bool ok = false;
    /// 走到「覆盖」而用户还没明确确认过（那条规矩在这一段照旧）。
    bool needsConfirm = false;
    /// 人话（`ok = false` 时才有）。**原样能显示**，界面不再加工。
    QString problem;

    int written = 0;      ///< 真要写进去的条数（「覆盖」档里含"就地改名"的那些）
    int added = 0;        ///< 其中目标上原来没有同一台地址的（= 接在表尾的）
    int removed = 0;      ///< 「覆盖」档真的拿掉的目标条目（对不上预设的那些）
    int skipped = 0;      ///< 预设里有、因为目标上已经有同一个地址而没写的
    int untouchable = 0;  ///< 目标上认不出来、所以一个都不动的元素
    /// `skipped` 那些是哪些（给界面显示「这 3 台已经在列表里了」）。
    QStringList skippedAddresses;
};

/// 只要计划（= 上面那个判据的念法）。`replaceConfirmed` 见 `applyServerListToInstance()`。
ServerListReport planServerListApply(ApplyMode mode, const QList<NbtUtil::ServerEntry> &target,
                                     int targetUntouchable,
                                     const QList<NbtUtil::ServerEntry> &source,
                                     bool replaceConfirmed = false);

// ---------------------------------------------------------------------------
// 落到那个实例上
// ---------------------------------------------------------------------------

/**
 * @brief 把这一段落到 @p datPath 那一份 `servers.dat` 上（读 → 判 → 改树 → 原子写回）。
 *
 * @param replaceConfirmed 「覆盖」必须由用户明确确认过 —— 没确认就是 `ok = false` +
 * `needsConfirm`，**一个字节都不写**（不是"先写了再回滚"）。
 *
 * `ok = false` 的来路（**每一条都不写盘**）：没给路径 / 源里那一条没有地址 / 文件读不动
 * （是目录 / 太大 / gzip 解不开 / 不是 NBT）/ `servers` 那一格不是一张表 / 「覆盖」没确认。
 *
 * **目标上还没有 `servers.dat` 不是失败**（"第一次给这个实例配服务器列表"正是最常见的那
 * 一趟）：那一档从零建一棵游戏认得出来的树（`ServerListFile::emptyDocument()`），
 * 并且按 `kFreshFileGzipped` 落盘。一台上都没有（`accepted` 空）时**连文件都不建**。
 */
ServerListReport applyServerListToInstance(const QString &datPath, ApplyMode mode,
                                           const QList<NbtUtil::ServerEntry> &source,
                                           bool replaceConfirmed = false);

/// 这一段的种类归不归这一层管（`ServerList`）。应用那一层按它分派。
bool serverListHandlesSection(SectionKind kind);

// ---------------------------------------------------------------------------
// 预设段 ⇄ 这一份列表
// ---------------------------------------------------------------------------

/// 载荷形状：`items` = `[{"name": "…", "address": "host:port"}, …]`（**保序**）。
///
/// 名字**允许是空串**（盘上的 `name` 真能是空串 —— 1.12.2 那种老 fixture 里就只有 `ip`），
/// 所以那一条写成 `"name": ""`，不是省掉这一格。
QList<PresetSection> sectionsFromServerList(const QList<NbtUtil::ServerEntry> &entries,
                                           ApplyMode mode = ApplyMode::Append);

/**
 * @brief 反过来：从一段预设里把这张表读出来。
 *
 * `ok = false` 的来路：项不是对象 / `name` 或 `address` **缺了或者不是字符串** /
 * `address` 是空串。读不回来的段**不许拿去改用户的实例**（同 `VanillaOptions` 那条）。
 */
bool serverListFromSection(const PresetSection &section, QList<NbtUtil::ServerEntry> *out,
                           QString *problemOut = nullptr);

}  // namespace Presets
