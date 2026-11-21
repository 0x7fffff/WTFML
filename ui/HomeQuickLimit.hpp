/**
 * @file HomeQuickLimit.hpp
 * @brief 主页实例带「收藏」「上次游玩」两个数量的上限：设置页数量框的范围和 `LauncherSettings`
 *        读写时的夹紧用同一个数。
 *
 * 取值是默认窗口宽度下一排放得下的卡片数（量法与数据见 docs/features 里的主页实例带一节）。
 */
#pragma once

inline constexpr int kHomeQuickMaxCount = 4;
