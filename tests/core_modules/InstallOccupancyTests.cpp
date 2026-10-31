#include "Support.hpp"

#include "controllers/InstallOccupancy.hpp"

// 「这一批下载是谁的」那四档占用的判据（见 controllers/InstallOccupancy.hpp）。
//
// 这几条钉的是**问法**，不是某一处调用：谁算"正在改这个实例"（预取不算）、任务中心按
// 哪一档说话（优先级）、收尾时该按哪一档报（`clearFor()` 的返回值）、以及"两档撞了同一个
// id"这种不该发生的情形也**两档都松开**（只松一档的话那个实例就再也启动不了了）。
int runInstallOccupancyTests(const QString &) {
    section("OC. 安装占用（导入 / 安装 / 补全 / 预取四档的唯一出处）");

    using Batch = InstallOccupancy::Batch;

    InstallOccupancy occupancy;
    check(!occupancy.active().valid() && occupancy.instanceIds().isEmpty()
              && !occupancy.anyModifying(),
          "OC1 空着的时候哪一档都不是、一个实例都不占");

    // ---- 「安装」一档 ----
    occupancy.setBatch(Batch::Install, QStringLiteral("甲"));
    check(occupancy.active().batch == Batch::Install
              && occupancy.active().instanceId == QStringLiteral("甲"),
          "OC2 记上「安装」之后说话的就是它", occupancy.active().instanceId);
    check(occupancy.instanceIds() == QSet<QString>{QStringLiteral("甲")},
          "OC2 `instanceIds()` 收的是这一档（这份并进 installingVersionIds()）");
    check(occupancy.modifying(QStringLiteral("甲")) && occupancy.anyModifying(),
          "OC2 安装算\"正在改它\"（文件马上要被重写）");

    // ---- 优先级：导入 > 安装 > 补全 > 预取 ----
    // 四档同时非空是不该发生的，但判据不能靠"不该发生"：真撞了的时候，说哪一档必须
    // 是**确定**的 —— 就是这一张表的顺序（和收尾那一段 if 链同序）。
    occupancy.setBatch(Batch::Import, QStringLiteral("乙"));
    occupancy.setBatch(Batch::Repair, QStringLiteral("丙"));
    occupancy.setBatch(Batch::Prefetch, QStringLiteral("丁"));
    check(occupancy.active().batch == Batch::Import
              && occupancy.active().instanceId == QStringLiteral("乙"),
          "OC3 四档都在时说话的是导入（优先级最高）",
          QStringLiteral("%1 / %2")
              .arg(int(occupancy.active().batch))
              .arg(occupancy.active().instanceId));
    check(occupancy.instanceIds().size() == 4,
          "OC3 四档的实例名都进\"现在不能启动\"那份", QString::number(occupancy.instanceIds().size()));

    // ---- 预取不算"正在改它" ----
    // 它写的是共享的 assets/asset_director，实例目录一个字都不动 —— 所以补全 / 预设落盘
    // 那些"这个实例现在能不能动"的闸不该被预取挡住；但它仍然算"不能启动"（上面的
    // instanceIds() 收它）。
    InstallOccupancy prefetchOnly;
    prefetchOnly.setBatch(Batch::Prefetch, QStringLiteral("戊"));
    check(!prefetchOnly.modifying(QStringLiteral("戊")) && !prefetchOnly.anyModifying(),
          "OC4 预取**不**算\"正在改它\"（它不写实例目录）");
    check(prefetchOnly.instanceIds().contains(QStringLiteral("戊")),
          "OC4 但预取期间这个实例照样不能启动（`instanceIds()` 里有它）");

    // ---- 收尾：按对得上的那一档说话，并把对得上的全松开 ----
    check(occupancy.clearFor(QStringLiteral("对不上")) == Batch::None,
          "OC5 对不上的 id 什么都不清、返回 None");
    check(occupancy.instanceIds().size() == 4, "OC5 对不上的 id 不会误伤别的档");
    check(occupancy.clearFor(QString()) == Batch::None, "OC5 空 id 什么都不做");
    check(occupancy.clearFor(QStringLiteral("丙")) == Batch::Repair,
          "OC6 对得上的那一档被清掉，返回值告诉收尾该按哪一档说话");
    check(occupancy.instanceIds().size() == 3 && !occupancy.modifying(QStringLiteral("丙")),
          "OC6 清完之后这个实例不再\"正在改\"（可以启动 / 可以补全）");

    // 两档撞同一个 id（不该发生，但真撞了必须都松开）：只松优先级最高的那一档的话，
    // 另一档会永远占着，这个实例从此再也启动不了。
    InstallOccupancy collided;
    collided.setBatch(Batch::Install, QStringLiteral("己"));
    collided.setBatch(Batch::Repair, QStringLiteral("己"));
    check(collided.clearFor(QStringLiteral("己")) == Batch::Install,
          "OC7 两档撞同一个 id 时按优先级报（安装 > 补全）");
    check(collided.instanceIds().isEmpty(), "OC7 但两档都松开了（不是只松最高的那一档）");

    // ---- `None` 不是一档：写它不算数 ----
    InstallOccupancy none;
    none.setBatch(Batch::None, QStringLiteral("庚"));
    check(!none.active().valid() && none.instanceIds().isEmpty(),
          "OC8 往 None 上写不算记了哪一档");
    return 0;
}
