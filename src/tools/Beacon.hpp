/**
 * @file Beacon.hpp
 * @brief 信标计算器的判据层（「信标计算」）。
 *
 * 只做算术和一张表：信标是几级、底面积多大、累计要多少块、效果范围多少格、这一级能
 * 给哪些效果。**不碰盘、不联网**，所以验证工程拿几个数字就能钉住它。
 *
 * 【那几组数从哪儿来】
 *
 * Minecraft Java 版信标：
 *
 * * 第 n 级那一层是 `(2n+1) × (2n+1)` 的方块（第 1 级 3×3 = 9 块，第 2 级 5×5 = 25 块，
 * 第 3 级 7×7 = 49 块，第 4 级 9×9 = 81 块）；金字塔是一层层叠上去的，所以到第 n 级的
 * **累计**块数 = Σ(4k²+4k+1)（k=1..n）= 9 / 34 / 83 / 164。
 * * 效果范围 = `10 × 级数 + 10` 格（20 / 30 / 40 / 50 格）。
 * * 可选效果按级数解锁：1 级 速度 / 急迫；2 级再加 抗性提升 / 跳跃提升；3 级再加 力量；
 * 4 级再加 再生。**每一档给的都是一级效果**（4 级的「再生」是那个额外的第二效果那一格，
 * 这里照样列出来 —— 用户想知道"4 级能拿到什么"）。
 *
 * 【为什么 `fromLevel` 可以取 0】
 *
 * 「我现在 2 级，想升到 4 级还要多少块」是这个工具最常见的问题；0 级 = 塔还没搭，
 * 那一路就是"从零开始要多少块"。所以 `fromLevel` 的下界是 0，而 `level` 是 1–4。
 */

#pragma once

#include <QString>
#include <QStringList>

namespace Beacon {

/// 信标最低几级（效果那一张表从这里开始）。
int minLevel();
/// 信标最高几级（再高没有效果，金字塔也就到 4 层）。
int maxLevel();
/// @p level 是不是一个真的信标等级（1–4）。`fromLevel` 另外允许 0，见文件头。
bool isValidLevel(int level);
/// 「已有几级」这一格的取值范围（0–3；到 4 级就没得升了）。
bool isValidFromLevel(int fromLevel);

/// 第 @p level 级那一层要多少块（`(2*level+1)^2`；越界回 0）。
int layerBlocks(int level);
/// 从零搭到 @p level 级的**累计**块数（9 / 34 / 83 / 164；越界回 0）。
int totalBlocks(int level);
/// 从 @p fromLevel 级升到 @p toLevel 级还要再加多少块（顺序不对回 0）。
int blocksToAdd(int fromLevel, int toLevel);

/// 效果范围（格）：`10 * level + 10`；越界回 0。
int effectRange(int level);

/// 信标金字塔能用的方块种类（矿物的换算按这一张表走）。
///
/// 只影响**换算成锭/宝石**那一步：方块数、层数、效果全都与材质无关（游戏里也是）。
enum class Material {
    Iron = 0,
    Gold,
    Diamond,
    Emerald,
    Netherite,
};

/// 全部材质（**次序 = 界面下拉框的次序**，下标就是 `Material` 的整数值）。
QStringList materialNames();
/// @p index 是不是一个真的材质下标（0 – 4）。
bool isValidMaterial(int index);
/// 界面上那一格的名字（越界时回空串 —— 不编一个假材质出来）。
QString materialName(int index);
/// 一个方块要几个锭/宝石（五种都是 9）。
int unitsPerBlock();
/// @p blocks 块 @p index 材质一共要多少个锭/宝石（材料不够时返回 0）。
int unitsForBlocks(int index, int blocks);
/// 「铁块」/「金块」/「钻石块」/「绿宝石块」/「下界合金块」（越界回空串）。
QString blockName(int index);
/// 「铁锭」/「金锭」/「钻石」/「绿宝石」/「下界合金锭」（越界回空串）。
QString unitName(int index);
/// 「164 块铁块 = 1476 个铁锭」那一句（@p blocks 为 0 时回空串）。
QString materialText(int index, int blocks);

/// 一座金字塔的顶面是 3 × 3 ⇒ 同一座金字塔上最多能放几颗信标。
int maxBeaconsPerBase();
/// 多颗信标**共用同一座金字塔**时底座一共要多少块（颗数越界回 0）。
///
/// 共用底座时方块数不随颗数增加：几颗信标只是站在同一块 3 × 3 顶面上。每颗信标自己
/// 另外还要 3 块黑曜石 + 1 颗下界之星 + 3 块玻璃 —— 那部分也按颗数算。
int sharedBaseBlocks(int level, int beaconCount);
/// 共用底座那一句解释（包含每颗信标自己的材料）；颗数越界时说清楚。
QString sharedBaseText(int level, int beaconCount);

/// 全部效果的名字（**次序 = 解锁的先后**，1 级那一档排最前面）。
QStringList effectNames();
/// @p level 级能用的效果（越界回空表）。
QStringList effectsForLevel(int level);

/// 一句总述（`4 级信标：范围 50 格，累计 164 块`）。越界时把话说清楚。
QString summaryText(int level);
/// 整份明细：底面积、累计、范围、效果、还要再加多少块（@p fromLevel 可以是 0）。
QString detailText(int fromLevel, int level);

}  // namespace Beacon
