#include "Support.hpp"
#include "HomeQuickLimit.hpp"
// 主页实例带的收起 / 展开：点箭头 → 设置里的 Home/QuickCollapsed 跟着变；两个数量的上限按新张数夹紧。
int MwHomeQuickCollapseChecks(MainWindow &window) {
    auto *toggle = byObjectName<QPushButton>(&window, QStringLiteral("homeQuickToggle"));
    check(toggle != nullptr, "主页实例带的收起 / 展开箭头按 objectName 找得到");
    if (toggle == nullptr) return 0;

    check(!LauncherSettings().homeQuickCollapsed(), "收起状态默认是展开");
    toggle->click();
    QApplication::processEvents();
    check(LauncherSettings().homeQuickCollapsed(), "点箭头收起后，设置里记着「已收起」（另起一个读盘者读到的）");
    toggle->click();
    QApplication::processEvents();
    check(!LauncherSettings().homeQuickCollapsed(), "再点一次展开，设置里的状态跟着回去");

    {
        LauncherSettings seed;
        seed.setHomeFavoriteCount(99);
        seed.setHomeRecentCount(99);
    }
    check(LauncherSettings().homeFavoriteCount() == kHomeQuickMaxCount &&
              LauncherSettings().homeRecentCount() == kHomeQuickMaxCount,
          "收藏 / 上次游玩的数量按上限夹紧",
          QString::number(LauncherSettings().homeFavoriteCount()));
    {
        QSettings raw;
        raw.setValue(QStringLiteral("Home/FavoriteCount"), 5);
        raw.setValue(QStringLiteral("Home/RecentCount"), 5);
    }
    check(LauncherSettings().homeFavoriteCount() == kHomeQuickMaxCount &&
              LauncherSettings().homeRecentCount() == kHomeQuickMaxCount,
          "旧版存过的 5 读回来变成新的上限");
    return 0;
}
