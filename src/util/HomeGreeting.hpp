#pragma once
/**
 * @file HomeGreeting.hpp
 * @brief 主页大标题那句问候的规则：按本地时段、上次打开日期、有没有实例、玩家名出一句话。
 *
 * 纯函数，不读设置、不碰界面，便于脱网单测。「上次打开日期」怎么记在
 * `LauncherSettings::lastOpenedDate()`，什么时候重算在 `MainWindow::updateHomeGreeting()`，
 * 名字太长怎么截在 `MainWindowUI::setHomeHeroTitle()`。产品约定见 docs/FEATURES.md 主页那一条。
 */

#include <QDate>
#include <QDateTime>
#include <QString>

namespace HomeGreeting {

/// 上次打开距今满这么多天，就说「好久不见」。
inline constexpr int kLongAbsenceDays = 14;

/// 取第一条命中的：
/// 1. 没有上次打开记录（@p lastOpened 无效）且一个实例都没有 →「欢迎来到 WTFML。」；
/// 2. 上次打开距今 ≥ 14 天 →「好久不见，{名字}。」；
/// 3. 按 @p now 的钟点：05–10 早上好 / 11–12 中午好 / 13–17 下午好 / 18–22 晚上好 / 23–04 夜深了。
/// @p name 为空（没登录）时不带名字：「晚上好。」。
QString text(const QDateTime &now, const QDate &lastOpened, bool hasInstances, const QString &name);

}  // namespace HomeGreeting
