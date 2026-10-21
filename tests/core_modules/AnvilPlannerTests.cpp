#include "Support.hpp"

void runAnvilPlannerTests() {
    section("ZAD. 附魔顺序助手：铁砧那套账（前科 / 价值 / 39 级上限）（工具箱第 5 件）");

    // ---------- ZAD1. 那张表 ----------
    {
        check(Anvil::normalizeId(QStringLiteral("  MineCraft:Sharpness  "))
                  == QStringLiteral("sharpness")
                  && Anvil::normalizeId(QStringLiteral("sharpness")) == QStringLiteral("sharpness"),
              "ZAD1 归一化：去 `minecraft:` 前缀、转小写、去空白");

        const Anvil::EnchantInfo *sharpness = Anvil::findEnchantment(QStringLiteral("minecraft:sharpness"));
        check(sharpness != nullptr && sharpness->id == QStringLiteral("sharpness")
                  && sharpness->maxLevel == 5 && sharpness->itemMultiplier == 1
                  && sharpness->bookMultiplier == 1,
              "★ ZAD1 锋利：上限 V、倍率 物品 1 / 书 1（wiki 那两列照抄）",
              sharpness == nullptr ? QStringLiteral("没找到") : QString::number(sharpness->maxLevel));
        check(Anvil::enchantmentName(QStringLiteral("sharpness")) == QStringLiteral("锋利"),
              "ZAD1 中文名就是界面上那一个");
        check(Anvil::enchantmentName(QStringLiteral("some_new_enchant")) == QStringLiteral("some_new_enchant"),
              "★ ZAD1 表里没有的那条**原样回自己**（宁可显示个生 id，也不显示空白）");

        struct Pinned {
            const char *id;
            int maxLevel;
            int item;
            int book;
        };
        const Pinned pinned[] = {
            {"protection", 4, 1, 1}, {"thorns", 3, 8, 4},   {"efficiency", 5, 1, 1},
            {"silk_touch", 1, 8, 4}, {"unbreaking", 3, 2, 1}, {"mending", 1, 4, 2},
            {"soul_speed", 3, 8, 4}, {"fortune", 3, 4, 2},  {"infinity", 1, 8, 4},
        };
        int tallied = 0;
        for (const Pinned &row : pinned) {
            const Anvil::EnchantInfo *info = Anvil::findEnchantment(QString::fromLatin1(row.id));
            if (info != nullptr && info->maxLevel == row.maxLevel && info->itemMultiplier == row.item
                && info->bookMultiplier == row.book) {
                ++tallied;
            }
        }
        check(tallied == int(sizeof(pinned) / sizeof(pinned[0])),
              "★ ZAD1 九条量化过的：上限与那两列倍率和 wiki 逐条一样（9/9）",
              QStringLiteral("%1/9").arg(tallied));

        // 互斥那一列**两边都要写**（表自己声明的规矩），而且写进去的 id 必须在表里。
        int asymmetric = 0;
        int dangling = 0;
        const QList<Anvil::EnchantInfo> &table = Anvil::enchantmentTable();
        for (const Anvil::EnchantInfo &info : table) {
            for (const QString &other : info.incompatible) {
                const Anvil::EnchantInfo *peer = Anvil::findEnchantment(other);
                if (peer == nullptr) {
                    ++dangling;
                } else if (!peer->incompatible.contains(info.id)) {
                    ++asymmetric;
                }
            }
        }
        check(dangling == 0, "★ ZAD1 互斥那一列里写进去的 id 都真的在表里（没有写错名字的）",
              QStringLiteral("%1 条悬空").arg(dangling));
        check(asymmetric == 0,
              "★ ZAD1 互斥关系是**对称**的（两边都写了：锋利↔亡灵杀手、保护四兄弟、精准采集↔时运…）",
              QStringLiteral("%1 条只写了一边").arg(asymmetric));
        check(table.size() >= 40,
              "ZAD1 表里至少有四十条（1.13 之后的一整套 + 1.21 那三条锤子的）",
              QStringLiteral("%1 条").arg(table.size()));
    }

    // ---------- ZAD2. 前科（2^n − 1）----------
    {
        check(Anvil::penaltyForUses(0) == 0 && Anvil::penaltyForUses(1) == 1
                  && Anvil::penaltyForUses(2) == 3 && Anvil::penaltyForUses(3) == 7
                  && Anvil::penaltyForUses(4) == 15 && Anvil::penaltyForUses(5) == 31,
              "★ ZAD2 前科表就是 wiki 那张 0/1/3/7/15/31（2^n − 1）",
              QStringLiteral("%1 %2 %3 %4 %5 %6")
                  .arg(Anvil::penaltyForUses(0))
                  .arg(Anvil::penaltyForUses(1))
                  .arg(Anvil::penaltyForUses(2))
                  .arg(Anvil::penaltyForUses(3))
                  .arg(Anvil::penaltyForUses(4))
                  .arg(Anvil::penaltyForUses(5)));
        check(Anvil::penaltyForUses(-3) == 0, "★ ZAD2 负数当 0（「崭新的东西」）");
        check(Anvil::usesForPenalty(0) == 0 && Anvil::usesForPenalty(1) == 1
                  && Anvil::usesForPenalty(3) == 2 && Anvil::usesForPenalty(7) == 3,
              "ZAD2 反着来：这笔前科是「用过几次」");
        check(Anvil::usesForPenalty(2) == -1 && Anvil::usesForPenalty(-1) == -1,
              "★ ZAD2 不是 2^n − 1 的形态回 **-1**（不是 2 次、不是 0 次）");
    }

    // ---------- ZAD3. 一次合并的账 ----------
    {
        // wiki 那个例子：钻石靴（灵魂疾行 III）+ 一本「荆棘 III + 摔落保护 IV」的书。
        // 书那一边的价值 wiki 明写是 16（荆棘 III 4×3 + 摔落保护 IV 1×4）。
        Anvil::Unit boots;
        boots.enchantments.insert(QStringLiteral("soul_speed"), 3);
        boots.isBook = false;
        Anvil::Unit book;
        book.enchantments.insert(QStringLiteral("thorns"), 3);
        book.enchantments.insert(QStringLiteral("feather_falling"), 4);

        const Anvil::MergeCost merged = Anvil::mergeCost(boots, book);
        check(merged.enchantValue == 16,
              "★★ ZAD3 那本书的价值 = 16（荆棘 III 4×3 + 摔落保护 IV 1×4）—— wiki 那个例子逐字对上",
              QString::number(merged.enchantValue));
        check(merged.result.value(QStringLiteral("soul_speed")) == 3
                  && merged.result.value(QStringLiteral("thorns")) == 3
                  && merged.result.value(QStringLiteral("feather_falling")) == 4
                  && merged.result.size() == 3,
              "ZAD3 靴子上那条灵魂疾行 III **没被动**（书是牺牲品，靴子留下）",
              QStringLiteral("%1 条附魔").arg(merged.result.size()));
        check(merged.total == 16 && merged.penalties == 0 && !merged.tooExpensive,
              "ZAD3 崭新的一对：总代价 = 牺牲品那一侧的价值 + 两边前科(0+0) = 16",
              QString::number(merged.total));

        // 左右不能对调着看：附魔价值只算牺牲品那一边。
        const Anvil::MergeCost swapped = Anvil::mergeCost(book, boots);
        check(swapped.enchantValue == 24,
              "★★ ZAD3 换个左右手：这回被吃的是靴子（不是书）⇒ 价值按**物品**那一列算 = 8×3 = 24",
              QString::number(swapped.enchantValue));

        // wiki 那条"用**合并之后**的等级"的例子：两本深海探索者 II ⇒ 6（III × 书 2）。
        Anvil::Unit depthA;
        depthA.enchantments.insert(QStringLiteral("depth_strider"), 2);
        Anvil::Unit depthB;
        depthB.enchantments.insert(QStringLiteral("depth_strider"), 2);
        const Anvil::MergeCost depth = Anvil::mergeCost(depthA, depthB);
        check(depth.enchantValue == 6 && depth.result.value(QStringLiteral("depth_strider")) == 3,
              "★★ ZAD3 两本深海探索者 II ⇒ 价值 **6**（wiki 原话：按合并后那 III 算，不是 II 的 4）",
              QStringLiteral("价值 %1 / 结果等级 %2")
                  .arg(depth.enchantValue)
                  .arg(depth.result.value(QStringLiteral("depth_strider"))));

        // 同级 +1，封顶到上限。
        Anvil::Unit sharpBookA;
        sharpBookA.enchantments.insert(QStringLiteral("sharpness"), 5);
        Anvil::Unit sharpBookB;
        sharpBookB.enchantments.insert(QStringLiteral("sharpness"), 5);
        const Anvil::MergeCost capped = Anvil::mergeCost(sharpBookA, sharpBookB);
        check(capped.result.value(QStringLiteral("sharpness")) == 5 && capped.enchantValue == 5,
              "★ ZAD3 两本锋利 V：同级那条 +1 被**上限 5 封住**（不是 VI），价值按结果那 5 级算");

        // 等级不一样：目标保住高的那一边。
        Anvil::Unit low;
        low.enchantments.insert(QStringLiteral("sharpness"), 2);
        const Anvil::MergeCost higher = Anvil::mergeCost(low, sharpBookB);
        check(higher.result.value(QStringLiteral("sharpness")) == 5 && higher.enchantValue == 5,
              "★ ZAD3 锋利 II + 锋利 V ⇒ V（牺牲品更高就是抬到它那一档）",
              QString::number(higher.result.value(QStringLiteral("sharpness"))));

        // 两本荆棘 III：上限 III，同级那一格 +1 也顶不上去。
        Anvil::Unit thornA;
        thornA.enchantments.insert(QStringLiteral("thorns"), 3);
        Anvil::Unit thornB;
        thornB.enchantments.insert(QStringLiteral("thorns"), 3);
        const Anvil::MergeCost thorns = Anvil::mergeCost(thornA, thornB);
        check(thorns.result.value(QStringLiteral("thorns")) == 3 && thorns.enchantValue == 12,
              "ZAD3 两本荆棘 III ⇒ 还是 III（上限就是 III），价值 4×3 = 12",
              QString::number(thorns.enchantValue));
    }

    // ---------- ZAD4. 互斥那一条 ----------
    {
        Anvil::Unit sharp;
        sharp.enchantments.insert(QStringLiteral("sharpness"), 3);
        Anvil::Unit smite;
        smite.enchantments.insert(QStringLiteral("smite"), 2);
        const Anvil::MergeCost conflict = Anvil::mergeCost(sharp, smite);
        check(conflict.incompatible == 1 && !conflict.result.contains(QStringLiteral("smite"))
                  && conflict.result.value(QStringLiteral("sharpness")) == 3,
              "★★ ZAD4 锋利 + 亡灵杀手互斥：**目标保住自己那条**，牺牲品那条进不来",
              QStringLiteral("结果 %1 条").arg(conflict.result.size()));
        check(conflict.enchantValue == 0 && conflict.total == 1,
              "★ ZAD4 互斥那一条**不计价值、只 +1 级**（wiki：add 1 level in Java）",
              QString::number(conflict.total));
        check(conflict.dropped.size() == 1
                  && conflict.dropped.value(QStringLiteral("smite")) == 2,
              "★★ ZAD4 顶掉的那一条**点名记下来**（界面拿它解释「谁没合上来」）",
              QStringLiteral("%1 条").arg(conflict.dropped.size()));

        // 两边都互斥时，每一条各 +1。
        Anvil::Unit mixed;
        mixed.enchantments.insert(QStringLiteral("smite"), 2);
        mixed.enchantments.insert(QStringLiteral("bane_of_arthropods"), 1);
        const Anvil::MergeCost two = Anvil::mergeCost(sharp, mixed);
        check(two.incompatible == 2 && two.total == 2,
              "★ ZAD4 两条互斥 ⇒ +2（每一条互斥的附魔各 +1）", QString::number(two.total));
    }

    // ---------- ZAD5. 前科怎么传下去 + 39 级那条线 ----------
    {
        Anvil::Unit left;
        left.enchantments.insert(QStringLiteral("protection"), 4);
        left.penalty = 1;
        Anvil::Unit right;
        right.enchantments.insert(QStringLiteral("feather_falling"), 4);
        right.penalty = 1;
        const Anvil::MergeCost merge = Anvil::mergeCost(left, right);
        check(merge.penalties == 2 && merge.resultPenalty == 3,
              "★★ ZAD5 合并后的前科 = 2 × max(左, 右) + 1（两边各 1 ⇒ 3，不是 1+1=2）",
              QStringLiteral("penalties=%1 resultPenalty=%2").arg(merge.penalties).arg(merge.resultPenalty));

        // 39 级那条线：**超过** 39 才拒绝（39 本身能做）。
        Anvil::Unit target;
        target.enchantments.insert(QStringLiteral("protection"), 4);
        target.penalty = 20;
        Anvil::Unit sacrificeBook;
        sacrificeBook.enchantments.insert(QStringLiteral("silk_touch"), 1);  // 书 4×1 = 4
        sacrificeBook.penalty = 15;                                          // 20 + 15 + 4 = 39
        const Anvil::MergeCost atLimit = Anvil::mergeCost(target, sacrificeBook);
        check(atLimit.total == 39 && !atLimit.tooExpensive,
              "★★ ZAD5 正好 39 级**能做**（wiki 那句是「39 级以上才拒绝」）",
              QString::number(atLimit.total));
        Anvil::Unit sacrificeBook2 = sacrificeBook;
        sacrificeBook2.penalty = 16;  // 20 + 16 + 4 = 40
        const Anvil::MergeCost overLimit = Anvil::mergeCost(target, sacrificeBook2);
        check(overLimit.total == 40 && overLimit.tooExpensive,
              "★★ ZAD5 40 级 ⇒ 「Too Expensive!」（这个数只跟代价有关，跟玩家有多少级无关）",
              QString::number(overLimit.total));
    }

    // ---------- ZAD6. 计划：一件 / 两件 / 拦下来的那些 ----------
    {
        const Anvil::Plan nothing = Anvil::plan({});
        check(!nothing.ok && !nothing.error.isEmpty(), "ZAD6 手上什么都没有 ⇒ 不算，给一句人话",
              nothing.error);

        QList<Anvil::Unit> eleven;
        for (int index = 0; index < Anvil::kMaxUnits + 1; ++index) {
            Anvil::Unit unit;
            unit.enchantments.insert(QStringLiteral("sharpness"), 1);
            eleven.append(unit);
        }
        const Anvil::Plan tooMany = Anvil::plan(eleven);
        check(!tooMany.ok && tooMany.error.contains(QString::number(Anvil::kMaxUnits)),
              "ZAD6 超过 kMaxUnits 件 ⇒ 明说上限（再多要穷举的次序就太多了）", tooMany.error);

        Anvil::Unit unknown;
        unknown.enchantments.insert(QStringLiteral("some_new_enchant"), 1);
        const Anvil::Plan unknownPlan = Anvil::plan({unknown});
        check(!unknownPlan.ok && unknownPlan.error.contains(QStringLiteral("some_new_enchant")),
              "★★ ZAD6 表里没有的附魔 ⇒ **宁可不算**，并把是哪一条说出来（不许给一条基于猜的计划）",
              unknownPlan.error);

        Anvil::Unit single;
        single.enchantments.insert(QStringLiteral("sharpness"), 3);
        const Anvil::Plan alone = Anvil::plan({single});
        check(alone.ok && alone.steps.isEmpty() && alone.totalCost == 0 && alone.consumed.isEmpty()
                  && alone.finalEnchantments.value(QStringLiteral("sharpness")) == 3,
              "ZAD6 只有一件 ⇒ 计划是「一件都不用合」（代价 0、没有步骤）");

        Anvil::Unit a;
        a.enchantments.insert(QStringLiteral("sharpness"), 3);
        Anvil::Unit b;
        b.enchantments.insert(QStringLiteral("sharpness"), 3);
        const Anvil::Plan pair = Anvil::plan({a, b});
        check(pair.ok && pair.steps.size() == 1 && pair.consumed.size() == 1
                  && pair.totalCost == Anvil::mergeCost(a, b).total
                  && pair.finalEnchantments.value(QStringLiteral("sharpness")) == 4,
              "ZAD6 两本锋利 III ⇒ 一步：合出锋利 IV、那一件被吃掉",
              QStringLiteral("代价 %1").arg(pair.totalCost));

        const Anvil::Plan renamed = Anvil::plan({a, b}, true);
        check(renamed.totalCost == pair.totalCost + 1,
              "★ ZAD6 `includeRename` 把「顺带改个名」那 1 级算进去（正好多 1）",
              QStringLiteral("%1 → %2").arg(pair.totalCost).arg(renamed.totalCost));

        // 改名那 1 级也要走 39 级那条线（不能因为"原本 39 能做"就放过）。
        Anvil::Unit target;
        target.enchantments.insert(QStringLiteral("protection"), 4);
        target.penalty = 20;
        Anvil::Unit sacrificeBook;
        sacrificeBook.enchantments.insert(QStringLiteral("silk_touch"), 1);
        sacrificeBook.penalty = 15;  // 不带改名 39、带改名 40
        const Anvil::Plan renamedOver = Anvil::plan({target, sacrificeBook}, true);
        check(renamedOver.ok && renamedOver.anyTooExpensive && renamedOver.totalCost == 40,
              "★★ ZAD6 带上改名这一级就压线过去了 ⇒ 计划里明说「这一条在生存里做不出来」",
              QStringLiteral("代价 %1 / tooExpensive=%2")
                  .arg(renamedOver.totalCost)
                  .arg(renamedOver.anyTooExpensive ? QStringLiteral("1") : QStringLiteral("0")));
        check(renamedOver.ok && !renamedOver.withinSurvival,
              "★★ ZAD6 `withinSurvival` 和那一步超 39 是**同一件事**的两个说法");

        // 材料可以不全用：两本锋利 V 合出来还是锋利 V（封顶了），那一本白花代价 ⇒ 别用。
        Anvil::Unit sharpFiveA;
        sharpFiveA.enchantments.insert(QStringLiteral("sharpness"), 5);
        Anvil::Unit sharpFiveB = sharpFiveA;
        const Anvil::Plan leftover = Anvil::plan({sharpFiveA, sharpFiveB});
        check(leftover.ok && leftover.steps.isEmpty() && leftover.totalCost == 0
                  && leftover.unused == QList<int>{1}
                  && leftover.consumed.isEmpty(),
              "★★ ZAD6 材料可以不全用：第二本锋利 V 加进去也升不了级 ⇒ 不用它、代价 0",
              QStringLiteral("步骤 %1 / 没用 %2 / 代价 %3")
                  .arg(leftover.steps.size())
                  .arg(leftover.unused.size())
                  .arg(leftover.totalCost));

        // 装备当底子：合出来的还是那件装备（倍率按"牺牲品是书"那一列算，但结果不是书）。
        Anvil::Unit armor;
        armor.isBook = false;
        armor.enchantments.insert(QStringLiteral("protection"), 4);
        Anvil::Unit thornsBook;
        thornsBook.enchantments.insert(QStringLiteral("thorns"), 3);
        const Anvil::Plan ontoArmor = Anvil::plan({armor, thornsBook});
        check(ontoArmor.ok && !ontoArmor.finalIsBook && ontoArmor.steps.size() == 1
                  && !ontoArmor.steps.first().resultIsBook
                  && ontoArmor.totalCost == Anvil::mergeCost(armor, thornsBook).total,
              "★★ ZAD6 拿装备当底子 ⇒ 合出来的还是装备（`finalIsBook` / `resultIsBook` 都是 false）",
              QStringLiteral("cost=%1 book=%2")
                  .arg(ontoArmor.ok ? ontoArmor.totalCost : -1)
                  .arg(ontoArmor.finalIsBook ? QStringLiteral("1") : QStringLiteral("0")));
    }

    // ---------- ZAD7. 计划确实最省（拿穷举当独立对照）----------
    {
        struct Group {
            const char *name;
            QList<ToolboxBruteUnit> units;
        };
        QList<Group> groups;
        groups.append(Group{
            "锋利III + 锋利III + 耐久II + 经验修补I",
            {{QMap<QString, int>{{QStringLiteral("sharpness"), 3}}, 0, true},
             {QMap<QString, int>{{QStringLiteral("sharpness"), 3}}, 0, true},
             {QMap<QString, int>{{QStringLiteral("unbreaking"), 2}}, 1, true},
             {QMap<QString, int>{{QStringLiteral("mending"), 1}}, 0, true}}});
        groups.append(Group{
            "锋利III + 亡灵杀手II（互斥）+ 抢夺III + 耐久II",
            {{QMap<QString, int>{{QStringLiteral("sharpness"), 3}}, 0, true},
             {QMap<QString, int>{{QStringLiteral("smite"), 2}}, 0, true},
             {QMap<QString, int>{{QStringLiteral("looting"), 3}}, 1, true},
             {QMap<QString, int>{{QStringLiteral("unbreaking"), 2}}, 3, true}}});
        groups.append(Group{
            "一件装备当底子（不是书）+ 三本书",
            {{QMap<QString, int>{{QStringLiteral("protection"), 4}}, 0, false},
             {QMap<QString, int>{{QStringLiteral("thorns"), 3}}, 0, true},
             {QMap<QString, int>{{QStringLiteral("mending"), 1}}, 0, true},
             {QMap<QString, int>{{QStringLiteral("unbreaking"), 3}}, 0, true}}});

        for (const Group &group : groups) {
            QList<Anvil::Unit> units;
            for (const ToolboxBruteUnit &item : group.units) {
                Anvil::Unit unit;
                unit.enchantments = item.enchantments;
                unit.penalty = item.penalty;
                unit.isBook = item.isBook;
                units.append(unit);
            }
            const Anvil::Plan planned = Anvil::plan(units);
            const int brute = toolboxBruteForceMinCostFor(group.units, planned.finalEnchantments);
            check(planned.ok && brute >= 0 && planned.totalCost == brute,
                  QStringLiteral("★★ ZAD7 %1：计划那套 DP 算出来的代价 == 穷举所有次序（材料可不用）的最小值")
                      .arg(QString::fromUtf8(group.name)),
                  QStringLiteral("计划 %1 / 穷举 %2")
                      .arg(planned.ok ? QString::number(planned.totalCost) : planned.error)
                      .arg(brute));
            if (!planned.ok) {
                continue;
            }
            // 步骤与账本自洽：步数 = 用上的件数 - 1、总代价 = 各步之和。
            int sum = 0;
            for (const Anvil::Step &step : planned.steps) {
                sum += step.cost.total;
            }
            const int used = units.size() - planned.unused.size();
            check(planned.steps.size() == used - 1 && sum == planned.totalCost,
                  QStringLiteral("ZAD7 %1：步数 = 用上的件数 - 1、总代价 = 各步之和（账本自洽）")
                      .arg(QString::fromUtf8(group.name)),
                  QStringLiteral("%1 步 / 和 %2 vs 总 %3")
                      .arg(planned.steps.size())
                      .arg(sum)
                      .arg(planned.totalCost));
            // 被吃掉的那些：恰好是"用上的件数 - 1"个互不相同的下标，且和 unused 不重叠。
            QSet<int> consumed;
            for (int index : planned.consumed) {
                consumed.insert(index);
            }
            QSet<int> unused;
            for (int index : planned.unused) {
                unused.insert(index);
            }
            check(consumed.size() == planned.consumed.size()
                      && consumed.size() == used - 1
                      && !consumed.contains(-1)
                      && consumed.intersects(unused) == false,
                  QStringLiteral("★ ZAD7 %1：被吃掉的恰好是「用上的件数 - 1」个**互不相同**的下标（没有一步吃掉空气）")
                      .arg(QString::fromUtf8(group.name)),
                  QStringLiteral("%1 个 / 没用 %2 个").arg(consumed.size()).arg(unused.size()));
            // 最后一步的结果 = 计划说的"合完是什么"。
            if (!planned.steps.isEmpty()) {
                check(planned.steps.last().result == planned.finalEnchantments,
                      QStringLiteral("ZAD7 %1：最后一步的产出 == 计划最后那一件的附魔")
                          .arg(QString::fromUtf8(group.name)));
            }
        }
    }
}

