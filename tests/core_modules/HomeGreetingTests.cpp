#include "Support.hpp"

#include "util/HomeGreeting.hpp"

#include <QDateTime>

/// 主页大标题问候的规则：时段边界、首次 / 升级老用户、久别 13 / 14 天、有名 / 无名。纯函数，不碰设置。
void runHomeGreetingTests() {
    section(QStringLiteral("HG 主页问候：时段、首次、久别、名字"));
    const QDate today(2026, 11, 13);
    const QDate yesterday = today.addDays(-1);
    const auto at = [&](int h, int m) { return QDateTime(today, QTime(h, m)); };
    const auto say = [&](int h, int m, const QString &name = QStringLiteral("Steve")) {
        return HomeGreeting::text(at(h, m), yesterday, true, name);
    };

    // ---- 时段边界 ----
    const struct {
        int h, m;
        const char *expect;
    } edges[] = {
        {4, 59, "夜深了，Steve。"},  {5, 0, "早上好，Steve。"},   {10, 59, "早上好，Steve。"},
        {11, 0, "中午好，Steve。"},  {12, 59, "中午好，Steve。"}, {13, 0, "下午好，Steve。"},
        {17, 59, "下午好，Steve。"}, {18, 0, "晚上好，Steve。"},  {22, 59, "晚上好，Steve。"},
        {23, 0, "夜深了，Steve。"},  {0, 0, "夜深了，Steve。"},
    };
    for (const auto &e : edges) {
        const QString got = say(e.h, e.m);
        check(got == QString::fromUtf8(e.expect),
              QStringLiteral("HG1 %1:%2 → %3")
                  .arg(e.h, 2, 10, QLatin1Char('0'))
                  .arg(e.m, 2, 10, QLatin1Char('0'))
                  .arg(QString::fromUtf8(e.expect)),
              got);
    }

    // ---- 首次 / 升级老用户 ----
    check(HomeGreeting::text(at(20, 0), QDate(), false, QStringLiteral("Steve")) ==
              QStringLiteral("欢迎来到 WTFML。"),
          "HG2 没有上次打开记录、也没有实例 → 欢迎来到 WTFML。（有名字也不带）");
    check(HomeGreeting::text(at(20, 0), QDate(), false, QString()) == QStringLiteral("欢迎来到 WTFML。"),
          "HG3 首次且没登录 → 同一句");
    check(HomeGreeting::text(at(20, 0), QDate(), true, QStringLiteral("Steve")) ==
              QStringLiteral("晚上好，Steve。"),
          "HG4 没记录但有实例 = 升级上来的老用户，按时段问候");
    check(HomeGreeting::text(at(20, 0), yesterday, false, QStringLiteral("Steve")) ==
              QStringLiteral("晚上好，Steve。"),
          "HG5 有记录但实例删光了 → 不再说欢迎");

    // ---- 久别 ----
    check(HomeGreeting::text(at(20, 0), today.addDays(-13), true, QStringLiteral("Steve")) ==
              QStringLiteral("晚上好，Steve。"),
          "HG6 隔 13 天 → 还按时段");
    check(HomeGreeting::text(at(20, 0), today.addDays(-14), true, QStringLiteral("Steve")) ==
              QStringLiteral("好久不见，Steve。"),
          "HG7 隔 14 天 → 好久不见");
    check(HomeGreeting::text(at(3, 0), today.addDays(-400), false, QString()) == QStringLiteral("好久不见。"),
          "HG8 久别优先于时段；没登录不带名字");
    check(HomeGreeting::text(at(20, 0), today.addDays(3), true, QStringLiteral("Steve")) ==
              QStringLiteral("晚上好，Steve。"),
          "HG9 上次打开日期在未来（改过系统时钟）→ 不算久别");

    // ---- 名字 ----
    check(say(20, 0, QString()) == QStringLiteral("晚上好。"), "HG10 没登录 → 晚上好。");
    check(say(20, 0, QStringLiteral("  ")) == QStringLiteral("晚上好。"), "HG11 全空白名字按没登录算");
    check(say(20, 0, QStringLiteral("ThisIsAVeryLongName_16")) == QStringLiteral("晚上好，ThisIsAVeryLongName_16。"),
          "HG12 长名字原样拼进去（截短是界面层的事）");
    check(say(20, 0, QStringLiteral("小明")) == QStringLiteral("晚上好，小明。"), "HG13 中文名字");
}
