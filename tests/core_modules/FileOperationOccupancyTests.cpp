#include "Support.hpp"

#include "controllers/FileOperationOccupancy.hpp"

// 「后台正在搬这个实例的文件吗」那一份引用计数（见 controllers/FileOperationOccupancy.hpp）。
//
// 这几条钉的是**配对语义**，不是某一处调用：
// · 两趟作业压同一个实例时，先结束的那趟不许把另一趟的"忙"摘掉（否则实例在后台还在
//   写盘时就能启动 / 删除）；
// · 没记过就放开要当没发生（配错一对不许把计数搞成负数 —— 那样下一次 mark 从 -1 加到 0，
//   这个实例就永远摘不掉"忙"了）；
// · 空 id 两边都不算（调用方还没定下是谁）。
int runFileOperationOccupancyTests(const QString &) {
    section("FO. 后台搬文件的占用（引用计数）");

    FileOperationOccupancy occupancy;
    check(occupancy.isEmpty() && occupancy.instanceIds().isEmpty(),
          "FO1 空着的时候一个实例都不占");
    check(!occupancy.contains(QStringLiteral("甲")), "FO1 没记过的实例不算忙");

    occupancy.mark(QStringLiteral("甲"));
    check(occupancy.contains(QStringLiteral("甲")) && occupancy.countFor(QStringLiteral("甲")) == 1,
          "FO2 压一层 ⇒ 忙");
    check(occupancy.instanceIds() == QSet<QString>{QStringLiteral("甲")},
          "FO2 `instanceIds()` 收的就是这一档（这份并进 installingVersionIds()）");

    occupancy.mark(QStringLiteral("甲"));
    check(occupancy.countFor(QStringLiteral("甲")) == 2, "FO3 同一个实例压了两趟 ⇒ 计数是 2");
    occupancy.release(QStringLiteral("甲"));
    check(occupancy.contains(QStringLiteral("甲")) && occupancy.countFor(QStringLiteral("甲")) == 1,
          "★★ FO3 先结束的那一趟不摘「忙」（还有一趟正压着）");
    occupancy.release(QStringLiteral("甲"));
    check(!occupancy.contains(QStringLiteral("甲")) && occupancy.isEmpty(),
          "★ FO3 归零才真的摘掉");

    // 没记过就放开：配错一对不许把计数变成负数。
    occupancy.release(QStringLiteral("乙"));
    check(!occupancy.contains(QStringLiteral("乙")) && occupancy.isEmpty(),
          "★★ FO4 没记过就放开 ⇒ 当没发生（不记负数）");
    occupancy.mark(QStringLiteral("乙"));
    check(occupancy.countFor(QStringLiteral("乙")) == 1,
          "★ FO4 之后照常能压（上一发没留下 -1）");

    // 空 id 两边都不算。
    occupancy.mark(QString());
    occupancy.release(QString());
    check(occupancy.instanceIds() == QSet<QString>{QStringLiteral("乙")},
          "FO5 空 id 压 / 放都不记账（调用方还没定下是谁）");

    // 两个实例互不影响。
    occupancy.mark(QStringLiteral("丙"));
    check(occupancy.instanceIds() == (QSet<QString>{QStringLiteral("乙"), QStringLiteral("丙")}),
          "FO6 两个实例各自记各自的");
    occupancy.release(QStringLiteral("乙"));
    check(occupancy.contains(QStringLiteral("丙")) && !occupancy.contains(QStringLiteral("乙")),
          "★ FO6 放掉一个不影响另一个");
    return 0;
}
