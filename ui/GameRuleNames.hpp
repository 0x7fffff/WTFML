/**
 * @file GameRuleNames.hpp
 * @brief 游戏规则的**官方中文名** —— 界面上那一列盖在英文名上面的那一层。
 * @note 这张表是从哪儿抄的、为什么摆在这一层，见 .cpp 顶上那一段。
 */
#pragma once

#include <QString>

namespace GameRuleNames {

/// 这个规则名在游戏里的官方中文名（游戏规则的英文名上盖一层对应的
/// 官方中文）。
///
/// **认不出来就原样回它自己** —— 模组写的规则、或者比这张表还新的规则都走这一档，
/// 调用方不用再判一次"空串怎么办"（屏幕上最坏也只是原样显示它自己）。
QString displayName(const QString &ruleName);

/// 这张表里有多少条（验证工程钉"这一层真被读过"用；也是"表被谁清空了"的哨兵）。
int count();

}  // namespace GameRuleNames
