/**
 * @file Brewing.hpp
 * @brief 酿造助手的判据层（coding plan §9.11 那一批里的「酿造助手」）。
 *
 * 只做一张表和一条路径：用户选"要做什么药水、要不要延长 / II 级 / 喷溅 / 滞留"，
 * 这里回"从水瓶开始，按什么顺序放什么材料"。**不碰盘、不联网**。
 *
 * 【表怎么读】
 *
 * 每一种药水记四件事：它叫什么、要往**上一瓶**里加什么材料、上一瓶是哪一种（`parent`），
 * 以及它是不是从**水瓶**直接做（`fromWater`，只有虚弱和缓降这两种 —— 它们不需要下界疣）。
 * "伤害 / 隐身 / 迟缓"这几种是**发酵蜘蛛眼改出来的**：它们的前一步是另一种药水，
 * 所以 `plan()` 会先把那条链一步步补全（水瓶 → 粗制的药水 → 夜视 → 隐身）。
 *
 * 【红石、荧石粉、火药、龙息】
 *
 * * 红石粉延长**时长**、荧石粉提高**等级** —— 这两样在游戏里**二选一**（后加的会把先加的
 * 冲掉），所以两个勾同时打上时这里直接报错、不悄悄挑一个。
 * * 火药把**普通**变**喷溅**；龙息把**喷溅**变**滞留**（龙息只能加在喷溅药水上，所以选
 * 滞留时前面会先自动补一步火药）。
 * * 不是每一种药水都有延长 / II 级版本（抗火、夜视、隐身这些没有 II 级）—— 表里那两位
 * 标记说了算，界面不该自己再猜一份。
 */

#pragma once

#include <QString>
#include <QStringList>

namespace Brewing {

/// 药水的三种形态。**下标就是界面上那个下拉框的下标**。
enum class Form {
    Normal = 0,   ///< 普通（药水）
    Splash = 1,   ///< 喷溅型（加火药）
    Lingering = 2,///< 滞留型（喷溅型再加龙息）
};

int formCount();
/// 形态的中文名（`普通` / `喷溅型` / `滞留型`）。越界回空串。
QString formName(Form form);

/// 表里的一种药水。
struct PotionInfo {
    QString name;       ///< 药水名（不带"药水"两个字，也不带形态前缀）
    QString ingredient; ///< 这一步要加的材料
    int parent = -1;    ///< -1 = 从粗制的药水（或水瓶）来；>= 0 = 先做出第 parent 种
    bool fromWater = false; ///< 直接从水瓶做（不需要下界疣）
    bool extendable = false;///< 能加红石粉延长
    bool upgradable = false;///< 能加荧石粉升一级
    /// 荧石粉之后是几级（2 = II；**迟缓是 IV**，游戏里荧石粉给的是迟缓 IV）。
    /// `upgradable == false` 时是 0。
    int upgradedLevel = 0;
};

/// @p level 的中文写法：2 → 「II」、4 → 「IV」，其它照数字写（`levelText(0)` 空串）。
QString levelText(int level);

/// 表里有几种药水。
int potionCount();
/// 第 @p index 种（越界回空的那个 —— `name` 为空）。
PotionInfo potionAt(int index);
/// 全部药水名（界面下拉框用；次序就是表的次序）。
QStringList potionNames();

/// 一条酿造计划。
struct Plan {
    bool ok = false;
    QString error;       ///< 非空 = 做不了（这种药水没有延长版本…）
    QStringList steps;   ///< 每一步一行
    QString summary;     ///< 一句总述（`一共 4 步 · 产出：滞留型迅捷药水（延长）`）
    int bottles = 1;     ///< 要几瓶（一次酿造最多 3 瓶）
    int operations = 1;  ///< 一共要酿几次 = `ceil(bottles / 3)`
    /// 材料合计（每种材料一行：`糖 × 2`）。同一步里的材料按**次数**累加。
    QStringList materials;
    /// 整份明细（总述 + 每一步 + 材料与燃料）。界面上那个结果框直接摆它。
    QString detailText() const;
};

/// 一次酿造最多放几瓶。
int maxBottlesPerOperation();
/// 一次「要几瓶」的取值范围上限（界面那个转盘的上限）。
int maxBottles();
/// @p bottles 瓶一共要酿几次（`ceil(bottles / 3)`；<= 0 回 0）。
int operationsFor(int bottles);
/// 酿 @p operations 次要几个烈焰粉（1 个烈焰粉够 20 次；<= 0 回 0）。
int blazePowderFor(int operations);

/**
 * @brief 算一条路线。
 * @param potionIndex 第几种药水（`potionAt()` 的下标）。
 * @param extended 要不要红石延长。
 * @param level2 要不要荧石粉升一级（和 @p extended 二选一）。
 * @param form 最终形态。
 * @param bottles 要几瓶（1 – `maxBottles()`；越界报错）。
 */
Plan plan(int potionIndex, bool extended, bool level2, Form form, int bottles = 1);

}  // namespace Brewing
