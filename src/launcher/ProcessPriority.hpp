/**
 * @file ProcessPriority.hpp
 * @brief 游戏进程跑在哪个优先级上（Windows 的 priority class）。
 * @author 0x7fffff
 * @date 2026-09-24
 * @note 单独一个头是因为它有两个用户、而两个用户**互不相干**：LauncherManager 拿它决定
 *       给游戏进程设哪个优先级，LauncherSettings 拿它当一项设置存起来。摆在 LauncherManager.hpp
 *       里的话，存设置那一侧就得把整个"启动器"头（QProcess、会话表、GameSession）拖进来；
 *       摆在 LauncherSettings.hpp 里的话，launcher 那一侧又得依赖设置了。（现在这个方向是
 *       settings → launcher，而 settings 本来就只依赖这一个**不含任何 include** 的叶子头，
 *       不会成环。）所以这里只放枚举，没有任何实现 —— 到 Windows 常量那一步的映射在
 *       LauncherManager.cpp 里（那是它一家的事）。
 */
#pragma once

/**
 * @brief 优先级档位。名字就是 Windows 那五个 priority class 的直译。
 *
 * NOTE: 这里**刻意没有**「不变」那一档。子进程默认继承父进程的优先级，而启动器本身
 *       有可能被用户（或者某个系统工具）以"高"启动过 —— 那时候"不设置"会静默变成"高"，
 *       界面上写着"正常"、实际却是别的档。每次都显式设一遍（默认 Normal）才是可预测的。
 */
enum class ProcessPriority {
    Low,         ///< 低（IDLE_PRIORITY_CLASS）
    BelowNormal, ///< 低于正常（BELOW_NORMAL_PRIORITY_CLASS）
    Normal,      ///< 正常（NORMAL_PRIORITY_CLASS）
    AboveNormal, ///< 高于正常（ABOVE_NORMAL_PRIORITY_CLASS）
    High,        ///< 高（HIGH_PRIORITY_CLASS）
};
