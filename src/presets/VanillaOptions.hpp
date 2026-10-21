/**
 * @file VanillaOptions.hpp
 * @brief 原版设置那一段（`options.txt`）的**保序模型**，以及"这一段怎么落到用户的实例上"
 * 的判据。
 *
 * 【为什么不能拿一张 map 把整份读回来再写出去】
 *
 * 要求「不要简单用 `QMap` 重写整个文件，也不要把缺失字段当空值写回。」三条理由：
 *
 * ① `options.txt` 是**用户的文件**：手改过、别的启动器改过、里面有我们从来没听说过的
 * 键（模组加过之后又卸了、别的版本写的、我们这一版还没跟上的新设置）。
 * 读进一个 `QMap<QString,QString>` 再写出来 = **顺手删掉**所有没进过我们那张表的键。
 * ② 行序**没有语义**：游戏自己保存时的顺序来自它内部那张表的遍历序，和我们读到的常不
 * 一样 —— 所以我们没有任何正当理由重排它。别人的行原样留着，我们只动被要求动的那些。
 * ③ 值里可以有冒号（`fullscreenResolution:1920x1080@60:24`），按**第一个**冒号切。
 *
 * 因此这一层的门槛是**原地往返**：读进来再写出去逐字节相同（没被改过的那些行一个字都
 * 不许动）。测试里那几条 `check(同一条字节)` 就是这条门槛本身 —— 和实例设置里那一页
 * 「能不能改」判的是原地往返是同一条道理。
 *
 * 【同一个键出现两次】
 *
 * 游戏读这一份的时候是往一张表里 `put` ⇒ **最后一条说了算**。所以这一层读按最后一条、
 * 写也改最后一条（改前面那条等于白改：游戏看不到）。重复的行里除了被改的那一条，其余
 * 一个都不动 —— 我们不知道它们为什么在那儿，就没有资格替用户抹掉。
 *
 * 【认哪些键、不认哪些键】
 *
 * `OptionsField` 四档：这是**分组选择**的依据（「基础设置与 `key_` 按键分组选择」）。
 * 认不出来的键落 `Unknown`，而 `Unknown` **默认不选**：不认识的键的正确值只有用户知道，
 * 替他搬过去不是"帮他省事"，是拿猜出来的值改他的实例。
 *
 * `Hardware` 那一档对应「显示器、全屏分辨率等硬件相关项**默认不选**」——
 * 判据是「这个值对不对要对着**这台机器**才说得清」（分辨率、窗口模式、GUI 缩放、有没有
 * 触摸屏）。看着也像"跟机器有关"的那几个性能项（`renderDistance`、`maxFps`…）**没进
 * 这一档**：它们是用户的偏好，换台机器多半照样想用。真要收紧以后只改那张表。
 *
 * `resourcePacks` / `incompatibleResourcePacks` **故意认不出来**：那一串归「资源包、
 * 光影包、模组及启用顺序」那块，要对着盘上那些资源文件判"这一份还在不在"，是内容层
 * 的事。摆进这一层只会得到一个"能过去但不对"的覆盖。
 *
 * 【不碰盘】
 *
 * 这一层全是纯函数（字节进、字节出），和 `PresetTypes` 的分工一样：读哪个实例的
 * `options.txt`、备份、加写锁、原子替换，全是**应用那一层**的事（「所有写入先完成
 * ApplyPlan，再备份和原子替换」）。所以这里收的是 `OptionsFile`、吐的是 `QByteArray`。
 */

#pragma once

#include <QByteArray>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include "PresetTypes.hpp"

namespace Presets {

// ---------------------------------------------------------------------------
// 文件 → 行
// ---------------------------------------------------------------------------

/**
 * @brief `options.txt` 里的一行。
 *
 * 两种行分得很清楚，因为它们**留法不一样**：认得出键的行按 `key` / `value` 走（我们要改
 * 的就是它们），认不出键的行只有一个 `text`（原样，一个字节都不动）。
 */
struct OptionsLine {
    /// 冒号前那一段。★ 空 = 这一行没有键（空行 / 我们认不出的东西）—— 这时 `text` 说了算。
    QString key;
    /// 冒号**之后**的原文（可以再带冒号）。
    QString value;
    /// 没有键的那些行：整行原样（不含换行）。
    QString text;
    /// 这一行后面那一截换行（`"\n"` / `"\r\n"` / 最后一行没有换行时是空串）。
    /// 逐行留着它，是为了让"手改过的文件用 CRLF"这种小事也能原样往返 —— 不收成
    /// "整份一个换行符"的话，一趟下来整个文件会被我们换一遍行尾。
    QString eol;

    /// 这一行**原来的字节**（不含换行），以及"值有没有被我们动过"。
    ///
    /// 这两个是给往返用的，界面别碰：`options.txt` 里的字节不一定都是 UTF-8（老版本
    /// Java 在中文系统上写过平台编码），所以**没被改过的行一律按原字节写回去**，
    /// 只有 `setValue()` 动过的那一行才由 `key` / `value` 重新拼。少了这两位，
    /// 一趟"读进来再写出去"就会把文件里非 UTF-8 的字节换成 U+FFFD。
    QByteArray raw;
    bool edited = false;

    bool hasKey() const { return !key.isEmpty(); }
};

/**
 * @brief 一整份 `options.txt`：**保序**的若干行。
 *
 * 行序 = 文件里的行序，从头到尾不被排序、不被去重、不被分组。
 */
struct OptionsFile {
    QList<OptionsLine> lines;

    /// 写回时用的字节。★ 没被改过的那一份**一定**逐字节等于读进来的那一份。
    QByteArray toBytes() const;

    /// 取一个键的值。★ **最后一条说了算**（游戏是 `put`，见文件头）。
    QString value(const QString &key) const;
    /// 那个键最后一条的下标（没有回 -1）。
    int lastIndexOfKey(const QString &key) const;
    bool contains(const QString &key) const { return lastIndexOfKey(key) >= 0; }
    /// 文件里的键（第一次出现的位置定序，重复的只算一次）。
    QStringList keys() const;

    /// 改一个键：有就改**最后一条**，没有就**追加到文件末尾**。
    ///
    /// 追加的位置就是末尾：我们不知道游戏会把它写在第几行（它自己那张表的遍历序和我们
    /// 读到的行序没关系），硬插到"看着该在的位置"等于凭空造出一种排序规则，而那种规则
    /// 下一次就会被游戏自己打乱。落末尾是唯一一个"没有加戏"的选择。
    void setValue(const QString &key, const QString &value);
};

/**
 * @brief 一份字节 → 一行一行。
 *
 * 认不出来的一律**照原样留着**（没有冒号的行、空行、`:` 开头那种空键的行）。这个函数
 * 不返回"有没有坏"：`options.txt` 没有"坏"这回事，只有"我们认得的行"和"我们认不出的行"。
 */
OptionsFile parseOptions(const QByteArray &bytes);

// ---------------------------------------------------------------------------
// 一个键属于哪一档
// ---------------------------------------------------------------------------

/// 认得出键的那几档（分组选择的依据）。认不出来的键落 `Unknown`。
enum class OptionsField {
    Game,      ///< 基础设置（`fov`、`renderDistance`、`lang`……）
    Hotkey,    ///< 标准按键（`key_*`）
    Hardware,  ///< 跟这台机器的显示器/输入设备绑着的那些（**默认不选**）
    Unknown,   ///< 我们不认识（别的版本/模组写的）—— **默认不选**，也不当作"多余"删掉
};

/// 落盘 / 界面翻译用的稳定键（中文名归 `ui/` 那一层，见 `ui/` 与 `src/` 的分工）。
QString optionsFieldKey(OptionsField field);

OptionsField optionsFieldOf(const QString &key);

/// 这一档默认勾不勾：`Game` / `Hotkey` 勾，`Hardware` / `Unknown` **不勾**（文件头那两条）。
bool optionsDefaultSelected(const QString &key);

/// 一份预设里摆到界面上的几组（顺序 = `Game` / `Hotkey` / `Hardware` / `Unknown`）。
/// 空组照样出现在表里（界面自己决定要不要画一个空壳）。
struct OptionsEntryGroup {
    OptionsField field = OptionsField::Unknown;
    QList<OptionsLine> lines;
};
QList<OptionsEntryGroup> groupOptions(const OptionsFile &source);

/// 一份源文件里"默认该勾上"的那些键（= `Game` + `Hotkey` 里出现过的）。
QSet<QString> defaultSelection(const OptionsFile &source);

// ---------------------------------------------------------------------------
// 冲突：两个动作抢同一个键
// ---------------------------------------------------------------------------

/**
 * @brief 一个绑定值被两个以上的动作抢着用（「检测标准按键的**精确重复**」）。
 *
 * "精确"是字面意义上的：绑定串**逐字节相同**才算。`key.keyboard.w` 和
 * `key.keyboard.left.shift` 里都带着 `shift`，但那是两个不同的键，不算冲突；真要做
 * "物理上是不是同一个键"那种判断得先会有修饰键的键位表，而那件事（以及 `key.jump` 和
 * `key_sneak` 谁该让位）没有唯一答案，得问用户。这一层只把**说得死的**那一种报出来。
 *
 * 空绑定（`key_xxx:`，用户自己清掉的）**永远不算冲突**：未绑定可以有一堆。
 */
struct OptionsConflict {
    /// 那个一模一样的绑定串。
    QString binding;
    /// 预设里抢这个键的动作（≥1 条）。
    QStringList sourceKeys;
    /// 目标实例上现在也绑着它的动作（可能空 —— 全在预设这一侧也一样是冲突）。
    QStringList targetKeys;

    /// 合起来有几条动作在抢。
    int claimedBy() const { return sourceKeys.size() + targetKeys.size(); }
};

/// 合并**之后**会出现的冲突（源里选中的 + 目标上已有的，见上面那两条）。
/// `selected` 只影响源那一侧：没勾的键这一趟不动，也就不参与这场抢。
QList<OptionsConflict> optionsConflicts(const OptionsFile &source, const OptionsFile &target,
                                        const QSet<QString> &selected);

// ---------------------------------------------------------------------------
// 预设段 ⇄ 这一份文件
// ---------------------------------------------------------------------------

/// 这一层的载荷形状：`items` = `[{"key": "fov", "value": "0.0"}, …]`（**保序**）。
///
/// 段的种类照 `PresetTypes` 的分法拆成两段：非 `key_` 的进 `VanillaOptions`、`key_*` 的
/// 进 `VanillaKeys`（「基础设置与 `key_` 按键分组选择」）。空的那些**照样出一段**
/// （"我这一趟就是要覆盖成空"和"我没有这一段"是两件事，同 `PresetSection` 那段）。
QList<PresetSection> sectionsFromOptions(const OptionsFile &source, const QSet<QString> &selected);

/**
 * @brief 反过来：从一份预设的那两段里把文件**值**读出来（应用那一层的第一步）。
 *
 * `ok = false` 的来路：某一段的项不是对象、或者 `key` 不是非空字符串 —— 读不回来的段
 * **不许拿去改用户的实例**（和 `PresetTypes` 里那条整份不认同一个道理）。
 */
OptionsFile optionsFromSections(const QList<PresetSection> &sections, bool *ok = nullptr);

/// 段自己的稳定 id：由**段种类**推出来的（UUID v5，固定的命名空间）。
///
/// 定死的而不是每写一次现搓一个：同一段的 id 在两份预设里一样、在两次读回来之后也一样，
/// 冲突提示和"这一段是同一个东西"这种判断才有落脚点（`PresetSection::id` 就是干这个的）。
QString optionsSectionId(SectionKind kind);

// ---------------------------------------------------------------------------
// 落法：计划（先说清楚要干什么）与执行（真算出新内容）
// ---------------------------------------------------------------------------

/**
 * @brief 这一趟会做什么（界面按它摆"游戏设置 合并 18 项，冲突 1 项"那种行）。
 *
 * `written` 数的是**要写进去的项数**（改的 + 加的），和"值是不是正好一样"无关：用户在
 * 计划表上看到的应该是我这一趟动了哪几项，而不是"其中几项恰好本来就是那个值"。
 */
struct OptionsApplyReport {
    bool ok = false;
    /// 走到「覆盖」而用户还没明确确认过（「默认 Merge，Replace 必须明确确认」）。
    bool needsConfirm = false;
    /// 人话（`ok = false` 时才有）。**原样能显示**，界面不再加工。
    QString problem;

    int written = 0;      ///< 会写进去的项数
    int added = 0;        ///< 其中目标上原来没有、要追加的
    int removed = 0;      ///< 「覆盖」档要删掉的目标上的键
    int skipped = 0;      ///< 预设里有、这一趟没勾的
    int untouchable = 0;  ///< 「覆盖」档里我们**认不出来**、所以留着不动的行

    QList<OptionsConflict> conflicts;
    bool hasConflicts() const { return !conflicts.isEmpty(); }
};

/// 计划 + 新内容一起给（`report` 为假时 `bytes` = 目标**原样**，一个字节都没动）。
struct OptionsApplyResult {
    OptionsApplyReport report;
    QByteArray bytes;
};

/**
 * @brief 算出"把预设这一段落到目标上"之后的目标内容（不碰盘）。
 *
 * @param kind 这一段的种类 —— 「覆盖」档要删的正是**这一段管得着的那一域**
 * （`VanillaOptions` ⇒ 基础设置 + 硬件那两档；`VanillaKeys` ⇒ 标准按键那档）。
 * `Unknown` 的行**任何一档都不删**：那是别人写的东西，不是"多余的东西"。
 * @param selected 这一趟勾了哪些键（`defaultSelection()` 是默认那一套）。
 * @param replaceConfirmed 「覆盖」必须由用户明确确认过 —— 没确认就是 `ok = false` + 目标原样。
 *
 * 各档的落法（`ApplyMode` 见 `PresetTypes`）：
 * · `Merge` —— 勾中的逐条写：目标有就改、没有就追加；目标上别的行（含 `Unknown`）一个不动。
 * · `Replace` —— 先把目标上**这一域认得出的**键删掉，再把勾中的写进去（`Unknown` 留着）。
 * · `CopyIfMissing` —— 只有目标上**没有**这个键时才追加（"别动用户已经调好的"那一档）。
 * · `Append` —— 对 `options.txt` 没有意义（键是唯一的），按 `Merge` 走；写在这里免得
 * 以后有人以为漏了一档。
 * · `Skip` —— 什么都不做。
 */
OptionsApplyResult applyOptions(SectionKind kind, const OptionsFile &target,
                                const OptionsFile &source, const QSet<QString> &selected,
                                ApplyMode mode, bool replaceConfirmed = false);

/// 只要计划（= `applyOptions()` 的 `report`）。存在的意义是调用顺序只有一处。
OptionsApplyReport planOptionsApply(SectionKind kind, const OptionsFile &source,
                                    const OptionsFile &target, const QSet<QString> &selected,
                                    ApplyMode mode, bool replaceConfirmed = false);

/// 这一段的种类归不归这一层管（`VanillaOptions` / `VanillaKeys`）。应用那一层按它分派。
bool optionsHandlesSection(SectionKind kind);

}  // namespace Presets
