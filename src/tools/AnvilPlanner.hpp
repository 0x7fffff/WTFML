/**
 * @file AnvilPlanner.hpp
 * @brief 附魔顺序助手：照 Java 版铁砧那套惩罚机制，算一条最省等级的顺序。
 * @author 0x7fffff
 * @date 2026-10-16
 * @note 用户 2026-10-16 加的那一件：「基于 mc 的铁砧惩罚机制做一个附魔顺序助手」。
 *
 * 【为什么要算这个】
 *
 * 铁砧每用一次都会给**结果**记一笔"前科"（先前加工惩罚），而每一次合并的代价里都要把
 * **两边的前科**一起算进去。于是把七本书按不同次序合并，总代价能差出一大截；合到最后
 * 一次超过 39 级，铁砧直接一句「Too Expensive!」谁也不许动手（创造模式除外）。这个工具
 * 就是把"按什么次序合"算出来给你看。
 *
 * 【这一层照的规矩（全部来自 wiki，2026-10-16 核对过）】
 *
 * - **前科**：一件东西在铁砧上用 n 次（不算纯改名），前科 = `2^n − 1`：0 → 0、1 → 1、
 *   2 → 3、3 → 7、4 → 15、5 → 31（wiki 原话："increasing by 2n+1 where n is the current
 *   largest penalty between the two items"，表格 0/1/3/7/15/31）；
 * - **合并后**的前科 = `2 × max(左, 右) + 1`（wiki 原话："only the higher of the penalties
 *   of the two items is considered" —— 两边各 1 那一档合出来就是 3，也就是"从"最高的那一边"
 *   再数一次）；
 * - **一次合并的代价**（wiki 那条等式）：
 *   `牺牲品那一边的附魔价值 + 左边的前科 + 右边的前科 + 改名 1 级 + 补耐久 2 级 + 不兼容附魔`
 *   —— 这一层只管前两项与不兼容那一项：**这一层算的是附魔书**（书没有耐久、也不改名），
 *   补耐久与改名那两项在 .cpp 里明写着为什么不参与；
 * - **附魔价值**：`倍率 × 合并之后的等级`。倍率照下那张表（同一条附魔：物品一档、附魔书一档，
 *   书是物品的一半）；★ 用的是**合并之后**的等级 —— 所以"两本 III 合出一本 IV"那一步，
 *   算的是 IV 那 4 级，不是 III 那 3 级；
 * - **同级同条**：牺牲品上那条附魔和目标**同级**时，结果等级 **+1**（封顶到这条附魔的上限）。
 *   这就是"两本锋利 III 合出锋利 IV"那条规矩；
 * - **不兼容**：牺牲品上那条附魔和目标上已有的某条互斥时，**目标保住自己那条**，代价 **+1**
 *   （wiki 原话："Since Smite is incompatible with Sharpness, add 1 level in Java"）；
 * - **上限**：一次合并的代价**超过 39 级**就被铁砧拒绝（wiki 原话："The anvil has a limit of
 *   39 levels; beyond that, repairs are refused."）。玩家的等级不影响这条判据 —— 它是代价的
 *   上限，不是"你现在有多少级"。
 *
 * 【这一层不做什么（说清楚，免得界面那边替它猜）】
 *
 * - **不算改名与补耐久**（纯附魔书合并没有这两项）；
 * - **不判"这件装备吃不吃这条附魔"**（wiki 里 `canEnchant` 那一档）：界面上列的单位都是
 *   附魔书，书没有这一档限制；
 * - **不联网、不读游戏文件**：这一层是纯函数，进来一串单位、出去一条计划。
 */

#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

namespace Anvil {

/// 一条附魔在那张表里长什么样。
struct EnchantInfo {
    QString id;            ///< 内部 id（`sharpness`，不带 `minecraft:`）
    QString name;          ///< 中文名
    QString english;       ///< 英文名（wiki 那一列）
    int maxLevel = 0;      ///< 上限（同级那条 +1 就是封顶到这里）
    int itemMultiplier = 0;  ///< 倍率（物品）
    int bookMultiplier = 0;  ///< 倍率（附魔书）
    /// 和它互斥的那些（两边都要写：`sharpness` 那条里有 `smite`，`smite` 那条里也有
    /// `sharpness`）。不兼容判据只查这张表。
    QStringList incompatible;
};

/// 那张表（**唯一出处**：界面上的下拉框、倍率、上限、互斥全问它）。
const QList<EnchantInfo> &enchantmentTable();
/// 按 id 找（`minecraft:sharpness` 和 `sharpness` 都认；`Sharpness` 也认大小写）。
/// 找不到回 `nullptr`。
const EnchantInfo *findEnchantment(const QString &id);
/// 中文名（找不到就回传进来的那串 id 本身 —— 界面上宁可显示个生 id，也不显示空白）。
QString enchantmentName(const QString &id);
/// 归一化 id（去 `minecraft:` 前缀、转小写、去空白）。找不到对应的表项时回归一化之后的值。
QString normalizeId(const QString &id);

/// 在铁砧上用过 @p uses 次的前科（`2^uses − 1`）。
int penaltyForUses(int uses);
/// 反着来：这一笔前科是"用过几次"（1 → 1、3 → 2、7 → 3；不是 2^n−1 的形态回 -1）。
int usesForPenalty(int penalty);

/// 一个单位：一顶物品（或一本附魔书）+ 它带着的附魔 + 它身上的前科。
struct Unit {
    /// 显示名（"锋利 IV 的书" / "目标装备"）。空着也能算，界面上补。
    QString label;
    /// 附魔 id（归一化过的）→ 等级。
    QMap<QString, int> enchantments;
    /// 前科（用 `penaltyForUses()` 从"用过几次"算）。拿不准就 0（崭新的东西）。
    int penalty = 0;
    /// 这一件是不是附魔书。**默认 true**（这一层是给附魔书算的：倍率那一列按书的那一档算，
    /// 也就是物品那一档的一半）。要算一件已经附过魔的装备当底子，把它设成 false。
    bool isBook = true;
};

/// 一次合并算出来的账。
struct MergeCost {
    /// 牺牲品那一边的附魔价值（Σ 倍率 × 合并后等级）。
    int enchantValue = 0;
    /// 两边前科之和。
    int penalties = 0;
    /// 不兼容那一条 +1 的条数。
    int incompatible = 0;
    /// 牺牲品上因为和目标互斥、这一步**没合上来**的那些附魔（id → 等级）。
    /// 界面拿它解释"谁被谁顶掉了"；不算进结果，但每一条照旧让代价 +1（上面那个数）。
    QMap<QString, int> dropped;
    /// 总代价（三项之和）。
    int total = 0;
    /// 超过 39 级 ⇒ 铁砧拒绝这一次（"Too Expensive!"）。
    bool tooExpensive = false;
    /// 合并之后的附魔（同级那条已经 +1、封顶也做了）。
    QMap<QString, int> result;
    /// 合并之后那一件的前科（`2 × max + 1`）。
    int resultPenalty = 0;
};

/// 一次合并：@p target 是放在左边那一件，@p sacrifice 是被吃掉的右边那一件。
///
/// NOTE: 左右**不能对调着看**：附魔价值只算牺牲品那一边的，所以同一对东西换个左右手，
///       代价往往不一样。计划里的每一步都记着谁左谁右。
MergeCost mergeCost(const Unit &target, const Unit &sacrifice);

/// 计划里的一步。
struct Step {
    /// 左边（留下、被加强的那一件）在**输入那一串里的下标**；合并出来的中间件用 -1。
    int targetIndex = -1;
    /// 右边（被吃掉的那一件）同理。
    int sacrificeIndex = -1;
    /// 合并出来的那一件是哪一步的产物（第一次合并的下标 = 输入个数；用它认中间件）。
    int targetNode = -1;
    int sacrificeNode = -1;
    MergeCost cost;
    /// 这一步之后手上那一件带着什么（给界面显示"合完是锋利 IV 了"）。
    QMap<QString, int> result;
    /// 合出来的那一件还是不是附魔书（跟着左边那件；拿装备当底子合出来的就是装备）。
    bool resultIsBook = true;
};

/// 一条计划。
struct Plan {
    /// 算出来了。输入只有一件时也算（代价 0、没有步骤）。
    bool ok = false;
    QString error;
    QList<Step> steps;
    /// 全部步骤的代价之和（这才是"要多少级经验"）。
    int totalCost = 0;
    /// 合完之后那一件的前科。
    int finalPenalty = 0;
    /// 最后那一件带着的附魔。
    QMap<QString, int> finalEnchantments;
    /// 最后那一件是不是附魔书（有装备当底子时是 false）。
    bool finalIsBook = true;
    /// 有没有哪一步超过 39 级（有的话这一条计划在生存里做不出来）。
    bool anyTooExpensive = false;
    /// 这一条计划**每一步**都在 39 级以内（生存里真做得出来）。
    bool withinSurvival = false;
    /// 有哪几件输入**没用上**（材料可不全用：那一本和别的重复、或者加进去也升不了级）。
    /// 里面是输入下标。NOTE: 和 `consumed` 不重叠 —— 没用的那几件既没被吃、也没被合。
    QList<int> unused;
    /// 哪几件**输入**被吃掉了（下标，恰好 `件数 - 1` 个、互不相同）——界面可以把它们标出来。
    /// NOTE: 记的是"被吃掉的那一件是哪件输入"，不是"那一步的右手边是第几个节点"：铁砧的
    ///       结果就是把左边那件改一改，所以中间件的身份一路往上还是某一件输入。少记这一层
    ///       的话，四件东西按两两配对合出来的计划只会记下两件（而用户实际上做掉了三件）。
    QList<int> consumed;
};

/// 一次搜索最多认几个单位（再多就算不动了；界面上限也是它）。
constexpr int kMaxUnits = 10;

/**
 * @brief 算一条最省等级的顺序。
 *
 * 做法是**穷举所有合并次序**（每一层状态只留"代价更低"的）：单位少（≤ kMaxUnits）时
 * 它比"看着像最优的贪心"更可靠 —— 每合并一次都会把两边的前科算进去，所以"先合便宜的"
 * 这种直觉在附魔等级不一样的时候会翻车。
 *
 * 目标是 **"生存里真做得到"的那一条**：所有步骤都不超过 39 级的前提下累计等级最小；
 * 一套可行次序都没有时，退而给出累计最小的那条，并把 `anyTooExpensive` 摆成 true。
 * 材料**可以不全用**（加进去也升不了级的那一本不必白花代价）—— 没用的那几件在 `unused`
 * 里点名，剩下的照样是全部输入减掉它们。
 *
 * @param units 手上的东西（附魔书若干，可以再带一件已经附过魔的装备当底子）。
 * @param includeRename 要不要把"顺带改个名"那 1 级算进去（默认不算）。
 */
Plan plan(const QList<Unit> &units, bool includeRename = false);

}  // namespace Anvil
