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

        // 诅咒那两条的 id 是 `binding_curse` / `vanishing_curse`（1.21 数据文件里的名字），
        // 不是早期 wiki 上那种 `curse_of_...` —— 界面里的下拉框一直用表里的 id，写错了这里
        // 不会出声，但任何按游戏 id 送进来的行都会变成"表里没有"。
        const Anvil::EnchantInfo *binding = Anvil::findEnchantment(QStringLiteral("binding_curse"));
        const Anvil::EnchantInfo *vanishing = Anvil::findEnchantment(QStringLiteral("vanishing_curse"));
        check(binding != nullptr && binding->name == QStringLiteral("绑定诅咒")
                  && vanishing != nullptr && vanishing->name == QStringLiteral("消失诅咒")
                  && Anvil::findEnchantment(QStringLiteral("curse_of_binding")) == nullptr
                  && Anvil::findEnchantment(QStringLiteral("curse_of_vanishing")) == nullptr,
              "★★ ZAD1 诅咒 id 用 1.21 数据里的 `binding_curse` / `vanishing_curse`（旧的 `curse_of_*` 不再认）");

        // 1.21 数据里 `loyalty` 的 `anvil_cost` 是 2（书那一档减半后是 1）—— 不是 1/1。
        const Anvil::EnchantInfo *loyalty = Anvil::findEnchantment(QStringLiteral("loyalty"));
        check(loyalty != nullptr && loyalty->itemMultiplier == 2 && loyalty->bookMultiplier == 1,
              "★★ ZAD1 忠诚：倍率 物品 2 / 书 1（照 `anvil_cost: 2`，书那一档减半取整）",
              loyalty == nullptr ? QStringLiteral("没找到")
                                 : QStringLiteral("物品 %1 / 书 %2")
                                       .arg(loyalty->itemMultiplier)
                                       .arg(loyalty->bookMultiplier));

        // 伤害那一组在 1.21 是**六条**：`exclusive_set/damage` 的标签里就是这六个，标签里
        // 的附魔两两互斥（`Enchantment.areCompatible` 那条判据）。表里也得两两都写上。
        const QStringList damageClique = {
            QStringLiteral("sharpness"), QStringLiteral("smite"),
            QStringLiteral("bane_of_arthropods"), QStringLiteral("impaling"),
            QStringLiteral("density"), QStringLiteral("breach")};
        int cliqueMissing = 0;
        for (const QString &left : damageClique) {
            const Anvil::EnchantInfo *info = Anvil::findEnchantment(left);
            if (info == nullptr) {
                ++cliqueMissing;
                continue;
            }
            for (const QString &right : damageClique) {
                if (left != right && !info->incompatible.contains(right)) {
                    ++cliqueMissing;
                }
            }
        }
        check(cliqueMissing == 0,
              "★★ ZAD1 伤害那一组六条两两互斥（锋利 / 亡灵杀手 / 节肢杀手 / 穿刺 / 密度 / 破甲）",
              QStringLiteral("%1 条没写上").arg(cliqueMissing));
        // 对照：`exclusive_set/riptide` 只被**激流**那条引用，忠诚与引雷自己没声明互斥，
        // 所以这两条按游戏判据是能共存的（表里也不能替它们加一条）。
        Anvil::Unit loyaltyBook;
        loyaltyBook.enchantments.insert(QStringLiteral("loyalty"), 1);
        Anvil::Unit channelingBook;
        channelingBook.enchantments.insert(QStringLiteral("channeling"), 1);
        check(Anvil::mergeCost(loyaltyBook, channelingBook).incompatible == 0,
              "★ ZAD1 忠诚 + 引雷不互斥（互斥标签只有激流引用了它）");
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

        // 牺牲品**自己身上**带着两条互斥的：铁砧是一条一条塞进去的，先塞进去的那条会让
        // 后一条变成互斥（+1、丢掉），不会两条都合上来。这里按表里的 id 次序先轮到锋利。
        Anvil::Unit plainBook;
        plainBook.enchantments.insert(QStringLiteral("unbreaking"), 1);
        Anvil::Unit conflictedBook;
        conflictedBook.enchantments.insert(QStringLiteral("sharpness"), 2);
        conflictedBook.enchantments.insert(QStringLiteral("smite"), 2);
        const Anvil::MergeCost internal = Anvil::mergeCost(plainBook, conflictedBook);
        check(internal.incompatible == 1 && internal.dropped.value(QStringLiteral("smite")) == 2
                  && internal.result.value(QStringLiteral("sharpness")) == 2
                  && !internal.result.contains(QStringLiteral("smite"))
                  && internal.total == 3,
              "★★ ZAD4 牺牲品上两条互斥：只有先轮到的那条合上来，后一条 +1 被顶掉（不是两条都进来）",
              QStringLiteral("互斥 %1 / 合计 %2 / 结果 %3 条")
                  .arg(internal.incompatible)
                  .arg(internal.total)
                  .arg(internal.result.size()));
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
        check(renamed.totalCost == pair.totalCost + 1 && renamed.renameStep == 0
                  && renamed.renameCost == 1 && renamed.steps.first().renamedHere
                  && renamed.steps.first().cost.total == pair.totalCost,
              "★ ZAD6 `includeRename` 把「改个名」那 1 级算进去（正好多 1，落在那一步上）",
              QStringLiteral("%1 → %2 / 第 %3 步")
                  .arg(pair.totalCost)
                  .arg(renamed.totalCost)
                  .arg(renamed.renameStep + 1));

        // 三步的计划：改名那 1 级**只加一次**，不是每一步都 +1（旧实现就是每步都加）。
        Anvil::Unit extra;
        extra.enchantments.insert(QStringLiteral("unbreaking"), 2);
        const Anvil::Plan triple = Anvil::plan({a, b, extra});
        const Anvil::Plan tripleRenamed = Anvil::plan({a, b, extra}, true);
        check(triple.steps.size() == 2 && tripleRenamed.ok
                  && tripleRenamed.totalCost == triple.totalCost + 1
                  && tripleRenamed.renameStep >= 0
                  && tripleRenamed.steps.at(tripleRenamed.renameStep).renamedHere,
              "★★ ZAD6 两步的计划里改名只加 1 级（不是每一步都 +1）",
              QStringLiteral("不改名 %1 → 改名 %2（%3 步）")
                  .arg(triple.totalCost)
                  .arg(tripleRenamed.totalCost)
                  .arg(triple.steps.size()));

        // 十件 + 改名：上限那一档也得算得动（这条顺便当性能哨兵，整套验证本来就要跑几十秒）。
        // 十条**互不冲突**的附魔各一本（都吃得上，所以九步合并一步也不省）。
        const QStringList tenIds = {QStringLiteral("sharpness"), QStringLiteral("protection"),
                                    QStringLiteral("unbreaking"), QStringLiteral("mending"),
                                    QStringLiteral("looting"), QStringLiteral("efficiency"),
                                    QStringLiteral("power"), QStringLiteral("punch"),
                                    QStringLiteral("flame"), QStringLiteral("feather_falling")};
        QList<Anvil::Unit> ten;
        for (const QString &id : tenIds) {
            Anvil::Unit unit;
            unit.enchantments.insert(id, 1);
            ten.append(unit);
        }
        const Anvil::Plan tenPlain = Anvil::plan(ten);
        const Anvil::Plan tenRenamed = Anvil::plan(ten, true);
        check(tenPlain.ok && tenPlain.steps.size() == Anvil::kMaxUnits - 1 && tenRenamed.ok
                  && tenRenamed.steps.size() == tenPlain.steps.size()
                  && tenRenamed.totalCost == tenPlain.totalCost + 1,
              "★ ZAD6 十件（上限那一档）+ 改名也照常算：只多那 1 级",
              QStringLiteral("%1 → %2（%3 步）")
                  .arg(tenPlain.totalCost)
                  .arg(tenRenamed.totalCost)
                  .arg(tenRenamed.steps.size()));

        // 单个东西也能改名：不用合，代价 = 自己的前科 + 1（单改名不涨前科）。
        const Anvil::Plan aloneRenamed = Anvil::plan({single}, true);
        check(aloneRenamed.ok && aloneRenamed.steps.isEmpty() && aloneRenamed.renameStep == -1
                  && aloneRenamed.renameCost == 1 && aloneRenamed.totalCost == 1,
              "★ ZAD6 单件改名：不用合，1 级（前科 0 + 1；`renameStep == -1` = 单独那一次）",
              QStringLiteral("renameCost=%1 / total=%2")
                  .arg(aloneRenamed.renameCost)
                  .arg(aloneRenamed.totalCost));
        Anvil::Unit worn = single;
        worn.penalty = 63;  // 63 + 1 = 64 ⇒ 单改名封顶 39
        const Anvil::Plan wornRenamed = Anvil::plan({worn}, true);
        check(wornRenamed.ok && wornRenamed.renameCost == 39 && wornRenamed.totalCost == 39,
              "★★ ZAD6 单改名封顶 39（前科再高也只收 39 级，而且前科不变）",
              QStringLiteral("renameCost=%1 / penalty=%2")
                  .arg(wornRenamed.renameCost)
                  .arg(wornRenamed.finalPenalty));

        // 那一步已经压线 39：改名塞进去就是 40（铁砧拒绝），所以改到合完之后单独做一次。
        Anvil::Unit target;
        target.enchantments.insert(QStringLiteral("protection"), 4);
        target.penalty = 20;
        Anvil::Unit sacrificeBook;
        sacrificeBook.enchantments.insert(QStringLiteral("silk_touch"), 1);
        sacrificeBook.penalty = 15;  // 不带改名 39、带改名 40
        const Anvil::Plan renamedOver = Anvil::plan({target, sacrificeBook}, true);
        check(renamedOver.ok && !renamedOver.anyTooExpensive && renamedOver.withinSurvival
                  && renamedOver.renameStep == -1 && renamedOver.renameCost == 39
                  && renamedOver.totalCost == 78 && renamedOver.finalPenalty == 41,
              "★★ ZAD6 那一步已经 39 级 ⇒ 改名不能塞进去（变 40）；改到合完单独做：39 + 39",
              QStringLiteral("代价 %1 / renameCost %2 / step %3 / penalty %4")
                  .arg(renamedOver.totalCost)
                  .arg(renamedOver.renameCost)
                  .arg(renamedOver.renameStep)
                  .arg(renamedOver.finalPenalty));

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

    // ---------- ZAD8. 随机小算例：子集 DP 的剪枝没把最省的那条剪掉 ----------
    // ZAD7 只钉了三组手挑的；这一节每次用固定种子生成一批小算例（4–5 件、倍率偏小的
    // 那几条附魔、前科 0–2），拿同一套穷举当独立对照。等级与前科都压在低档，是为了让每一步
    // 都稳稳低于 39 级 —— 这一节问的是"搜索会不会丢最优解"，不是 39 级那条线（那条在 ZAD5/6）。
    {
        quint32 seed = 0x5EED1234u;
        const auto next = [&seed]() {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            return seed;
        };
        struct Pick {
            const char *id;
            int maxLevel;
        };
        const Pick pool[] = {
            {"sharpness", 3}, {"smite", 3},     {"unbreaking", 3}, {"looting", 3},
            {"thorns", 3},    {"fortune", 3},   {"efficiency", 3}, {"mending", 1},
            {"silk_touch", 1}, {"protection", 3},
        };
        constexpr int kPickCount = int(sizeof(pool) / sizeof(pool[0]));
        int cases = 0;
        int mismatches = 0;
        QString firstMismatch;
        for (int trial = 0; trial < 560; ++trial) {
            // 4 件那批把前科放到 0–4、每条 1–3 个附魔（结果映射更杂，最考剪枝）；5 件那批
            // 收在前科 0–2，免得某一步自己被 39 级挡住 —— 那条线不归这一节管。
            const int count = trial < 500 ? 4 : 5;
            const int penaltySpan = count == 4 ? 5 : 3;
            QList<ToolboxBruteUnit> bruteUnits;
            QList<Anvil::Unit> units;
            for (int index = 0; index < count; ++index) {
                ToolboxBruteUnit item;
                item.penalty = int(next() % quint32(penaltySpan));
                // 最后一件偶尔摆成装备：合出来的那一件就必须是它（`wantBook` 那一档）。
                item.isBook = index + 1 != count || next() % 4u != 0;
                const int picks = 1 + int(next() % 3u);
                for (int pick = 0; pick < picks; ++pick) {
                    const Pick &choice = pool[size_t(next() % kPickCount)];
                    const int level = 1 + int(next() % quint32(choice.maxLevel));
                    const QString id = QString::fromLatin1(choice.id);
                    item.enchantments[id] = qMax(item.enchantments.value(id), level);
                }
                bruteUnits.append(item);
                Anvil::Unit unit;
                unit.enchantments = item.enchantments;
                unit.penalty = item.penalty;
                unit.isBook = item.isBook;
                units.append(unit);
            }
            const Anvil::Plan planned = Anvil::plan(units);
            const int brute = planned.ok
                                  ? toolboxBruteForceMinCostFor(bruteUnits, planned.finalEnchantments)
                                  : -1;
            ++cases;
            if (!planned.ok || brute < 0 || planned.totalCost != brute) {
                ++mismatches;
                if (firstMismatch.isEmpty()) {
                    QStringList described;
                    for (const ToolboxBruteUnit &item : bruteUnits) {
                        QStringList parts;
                        for (auto it = item.enchantments.constBegin(); it != item.enchantments.constEnd(); ++it) {
                            parts.append(QStringLiteral("%1=%2").arg(it.key()).arg(it.value()));
                        }
                        described.append(QStringLiteral("[%1|前科%2|%3]")
                                             .arg(parts.join(QLatin1Char('+')),
                                                  QString::number(item.penalty),
                                                  item.isBook ? QStringLiteral("书")
                                                              : QStringLiteral("装备")));
                    }
                    firstMismatch = QStringLiteral("%1 ⇒ 计划 %2 / 穷举 %3")
                                        .arg(described.join(QStringLiteral(" ")),
                                             planned.ok ? QString::number(planned.totalCost)
                                                        : planned.error,
                                             QString::number(brute));
                }
            }
        }
        check(mismatches == 0,
              QStringLiteral("★★ ZAD8 %1 组随机算例（4–5 件）：计划代价 == 穷举所有次序的最小值")
                  .arg(cases),
              QStringLiteral("%1 组对不上；第一组：%2").arg(mismatches).arg(firstMismatch));
    }
}

