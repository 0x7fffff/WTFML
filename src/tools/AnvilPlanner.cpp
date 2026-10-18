/**
 * @file AnvilPlanner.cpp
 * @brief AnvilPlanner 的实现（这套规矩的出处与"不做什么"在 .hpp 开头那一段）。
 * @author 0x7fffff
 * @date 2026-10-16
 */
#include "AnvilPlanner.hpp"

#include <QSet>

#include <algorithm>
#include <cstring>
#include <map>

namespace {

using namespace Anvil;

/// 那一对倍率的来源：wiki 的 Anvil mechanics 页面（2026-10-16 核对）。
///
/// NOTE: 1.21 那几条锤子上的新附魔（`density` / `breach` / `wind_burst`）的**互斥关系**没
///       登记在这张表里 —— 那几条我只核到了倍率与上限。表里 `incompatible` 那一列收的是
///       从早期版本起就定下来的那几对（锋利/亡灵杀手/节肢杀手、保护四兄弟、精准采集/时运、
///       深海探索者/冰霜行者、经验修补/无限、三叉戟那三条、多重箭/穿透）。
struct TableEntry {
    const char *id;
    const char *name;
    const char *english;
    int maxLevel;
    int itemMultiplier;
    int bookMultiplier;
    const char *incompatible;  ///< 逗号分开的 id 表；空串 = 没有
};

const TableEntry kTable[] = {
    {"protection", "保护", "Protection", 4, 1, 1,
     "fire_protection,blast_protection,projectile_protection"},
    {"fire_protection", "火焰保护", "Fire Protection", 4, 2, 1,
     "protection,blast_protection,projectile_protection"},
    {"feather_falling", "摔落保护", "Feather Falling", 4, 2, 1, ""},
    {"blast_protection", "爆炸保护", "Blast Protection", 4, 4, 2,
     "protection,fire_protection,projectile_protection"},
    {"projectile_protection", "弹射物保护", "Projectile Protection", 4, 2, 1,
     "protection,fire_protection,blast_protection"},
    {"thorns", "荆棘", "Thorns", 3, 8, 4, ""},
    {"respiration", "水下呼吸", "Respiration", 3, 4, 2, ""},
    {"depth_strider", "深海探索者", "Depth Strider", 3, 4, 2, "frost_walker"},
    {"aqua_affinity", "水下速掘", "Aqua Affinity", 1, 4, 2, ""},
    {"sharpness", "锋利", "Sharpness", 5, 1, 1, "smite,bane_of_arthropods"},
    {"smite", "亡灵杀手", "Smite", 5, 2, 1, "sharpness,bane_of_arthropods"},
    {"bane_of_arthropods", "节肢杀手", "Bane of Arthropods", 5, 2, 1, "sharpness,smite"},
    {"knockback", "击退", "Knockback", 2, 2, 1, ""},
    {"fire_aspect", "火焰附加", "Fire Aspect", 2, 4, 2, ""},
    {"looting", "抢夺", "Looting", 3, 4, 2, ""},
    {"sweeping_edge", "横扫之刃", "Sweeping Edge", 3, 4, 2, ""},
    {"efficiency", "效率", "Efficiency", 5, 1, 1, ""},
    {"silk_touch", "精准采集", "Silk Touch", 1, 8, 4, "fortune"},
    {"unbreaking", "耐久", "Unbreaking", 3, 2, 1, ""},
    {"fortune", "时运", "Fortune", 3, 4, 2, "silk_touch"},
    {"power", "力量", "Power", 5, 1, 1, ""},
    {"punch", "冲击", "Punch", 2, 4, 2, ""},
    {"flame", "火矢", "Flame", 1, 4, 2, ""},
    {"infinity", "无限", "Infinity", 1, 8, 4, "mending"},
    {"luck_of_the_sea", "海之眷顾", "Luck of the Sea", 3, 4, 2, ""},
    {"lure", "饵钓", "Lure", 3, 4, 2, ""},
    {"frost_walker", "冰霜行者", "Frost Walker", 2, 4, 2, "depth_strider"},
    {"mending", "经验修补", "Mending", 1, 4, 2, "infinity"},
    {"curse_of_binding", "绑定诅咒", "Curse of Binding", 1, 8, 4, ""},
    {"curse_of_vanishing", "消失诅咒", "Curse of Vanishing", 1, 8, 4, ""},
    {"impaling", "穿刺", "Impaling", 5, 4, 2, ""},
    {"riptide", "激流", "Riptide", 3, 4, 2, "loyalty,channeling"},
    {"loyalty", "忠诚", "Loyalty", 3, 1, 1, "riptide"},
    {"channeling", "引雷", "Channeling", 1, 8, 4, "riptide"},
    {"multishot", "多重射击", "Multishot", 1, 4, 2, "piercing"},
    {"piercing", "穿透", "Piercing", 4, 1, 1, "multishot"},
    {"quick_charge", "快速装填", "Quick Charge", 3, 2, 1, ""},
    {"soul_speed", "灵魂疾行", "Soul Speed", 3, 8, 4, ""},
    {"swift_sneak", "迅捷潜行", "Swift Sneak", 3, 8, 4, ""},
    {"wind_burst", "风爆", "Wind Burst", 3, 4, 2, ""},
    {"density", "密度", "Density", 5, 2, 1, ""},
    {"breach", "破甲", "Breach", 4, 4, 2, ""},
};

QList<EnchantInfo> buildTable() {
    QList<EnchantInfo> table;
    for (const TableEntry &entry : kTable) {
        EnchantInfo info;
        info.id = QString::fromLatin1(entry.id);
        info.name = QString::fromUtf8(entry.name);
        info.english = QString::fromLatin1(entry.english);
        info.maxLevel = entry.maxLevel;
        info.itemMultiplier = entry.itemMultiplier;
        info.bookMultiplier = entry.bookMultiplier;
        const QString incompatible = QString::fromLatin1(entry.incompatible);
        if (!incompatible.isEmpty()) {
            info.incompatible = incompatible.split(QChar(','), Qt::SkipEmptyParts);
        }
        table.append(info);
    }
    return table;
}

/// 这一条附魔的倍率（附魔书按书的那一列）。
int multiplierOf(const EnchantInfo *info, bool isBook) {
    if (info == nullptr) {
        // 表里没有的那一条：**别把整件事停下**（用户可能报了一个更新版本才有的附魔）。
        // 按最便宜那一档算，并在计划里由 plan() 事先拦掉——见那边的前置校验。
        return 1;
    }
    return isBook ? info->bookMultiplier : info->itemMultiplier;
}

/// 这一对附魔互斥吗（查表那一列）。
bool incompatiblePair(const QString &left, const QString &right) {
    const EnchantInfo *info = findEnchantment(left);
    return info != nullptr && info->incompatible.contains(right);
}

/// 两张附魔表合并（左边留下、右边被吃）：代价、结果、不兼容的条数。
struct MergeMaps {
    int value = 0;
    int incompatible = 0;
    QMap<QString, int> result;
    QMap<QString, int> dropped;
};

MergeMaps mergeMaps(const QMap<QString, int> &target, const QMap<QString, int> &sacrifice, bool sacrificeIsBook) {
    MergeMaps merged;
    merged.result = target;
    for (auto it = sacrifice.constBegin(); it != sacrifice.constEnd(); ++it) {
        const QString id = it.key();
        const int level = it.value();
        // 目标上有一条和它互斥的 ⇒ 目标保住自己那条，代价 +1（Java 那一档）。
        bool conflict = false;
        for (auto existing = target.constBegin(); existing != target.constEnd(); ++existing) {
            if (incompatiblePair(existing.key(), id)) {
                conflict = true;
                break;
            }
        }
        if (conflict) {
            ++merged.incompatible;
            merged.dropped.insert(id, level);
            continue;
        }
        const EnchantInfo *info = findEnchantment(id);
        const int existingLevel = target.value(id, 0);
        int level_ = level;
        if (existingLevel == level) {
            // ★ 同级 ⇒ +1（这就是"两本锋利 III 合出锋利 IV"那条规矩）。
            ++level_;
        } else {
            level_ = qMax(existingLevel, level);
        }
        if (info != nullptr) {
            level_ = qMin(level_, info->maxLevel);
        }
        merged.result.insert(id, level_);
        merged.value += multiplierOf(info, sacrificeIsBook) * level_;
    }
    return merged;
}

/// 计划搜索里的一个状态：手上那一件（前科 + 总代价 + 带着什么）+ 它是从哪两步来的。
struct State {
    int cost = 0;
    int penalty = 0;
    /// 走到这一件为止，**最大那一步**花了多少级（判"生存里做不做得出来"用它，
    /// 不看累计 —— 铁砧限制的是每一次，不是总和）。
    int maxStep = 0;
    /// 这一件是不是附魔书（决定它当牺牲品时按哪一列倍率算；合并出来的那件跟着左边那件）。
    bool isBook = true;
    QMap<QString, int> enchantments;
    /// 来源：子集 A、子集 B、以及"谁左谁右"（true = A 当牺牲品）。
    int leftMask = 0;
    int rightMask = 0;
    int leftState = -1;
    int rightState = -1;
    bool leftIsSacrifice = false;
    /// 这一次合并的账（重建步骤时要显示）。
    MergeCost merge;
    int node = -1;  ///< 这一件在第几步被合出来（叶子的 -1 表示"输入里那一件"）
};

}  // namespace

namespace Anvil {

const QList<EnchantInfo> &enchantmentTable() {
    static const QList<EnchantInfo> table = buildTable();
    return table;
}

QString normalizeId(const QString &id) {
    QString name = id.trimmed().toLower();
    if (name.startsWith(QLatin1String("minecraft:"))) {
        name = name.mid(int(std::strlen("minecraft:")));
    }
    return name;
}

const EnchantInfo *findEnchantment(const QString &id) {
    const QString name = normalizeId(id);
    for (const EnchantInfo &info : enchantmentTable()) {
        if (info.id == name) {
            return &info;
        }
    }
    return nullptr;
}

QString enchantmentName(const QString &id) {
    const EnchantInfo *info = findEnchantment(id);
    return info != nullptr ? info->name : normalizeId(id);
}

int penaltyForUses(int uses) {
    if (uses <= 0) {
        return 0;
    }
    if (uses >= 31) {
        // 2^31 会溢出 int；31 次以上早就超过铁砧那条 39 级的上限了，给一个够大的数就行。
        return 0x40000000;
    }
    return (1 << uses) - 1;
}

int usesForPenalty(int penalty) {
    if (penalty < 0) {
        return -1;
    }
    for (int uses = 0; uses < 31; ++uses) {
        if (penaltyForUses(uses) == penalty) {
            return uses;
        }
    }
    return -1;
}

MergeCost mergeCost(const Unit &target, const Unit &sacrifice) {
    MergeCost cost;
    const MergeMaps merged = mergeMaps(target.enchantments, sacrifice.enchantments, sacrifice.isBook);
    cost.enchantValue = merged.value;
    cost.incompatible = merged.incompatible;
    cost.dropped = merged.dropped;
    cost.penalties = target.penalty + sacrifice.penalty;
    cost.total = cost.enchantValue + cost.penalties + cost.incompatible;
    // 39 级是铁砧那条上限（wiki："beyond that, repairs are refused"）—— 代价**超过 39**
    // 就拒绝，所以判据是 `> 39` 而不是 `>= 39`。
    cost.tooExpensive = cost.total > 39;
    cost.result = merged.result;
    cost.resultPenalty = 2 * qMax(target.penalty, sacrifice.penalty) + 1;
    return cost;
}

Plan plan(const QList<Unit> &units, bool includeRename) {
    Plan plan;
    if (units.isEmpty()) {
        plan.error = QStringLiteral("手上什么都没有：先加几件。");
        return plan;
    }
    if (units.size() > kMaxUnits) {
        plan.error = QStringLiteral("一次最多算 %1 件（再多要穷举的次序就太多了）。").arg(kMaxUnits);
        return plan;
    }
    // ---------- 先把表里没有的那几条拦下来（宁可不算，也不给一条基于猜测的计划）----------
    QStringList unknown;
    for (const Unit &unit : units) {
        for (auto it = unit.enchantments.constBegin(); it != unit.enchantments.constEnd(); ++it) {
            if (findEnchantment(it.key()) == nullptr && !unknown.contains(normalizeId(it.key()))) {
                unknown.append(normalizeId(it.key()));
            }
        }
    }
    if (!unknown.isEmpty()) {
        plan.error = QStringLiteral("这几条附魔不在表里，算不了：%1。").arg(unknown.join(QStringLiteral("、")));
        return plan;
    }

    const int total = 1 << units.size();
    // 每个子集的候选状态（**帕累托**：代价和前科都不比它差的那个留着，其余丢掉）。
    // NOTE: 这里必须写成 `static_cast<size_t>(…)` 而不是 `size_t(total)` —— 后者会被当成
    //       一个名叫 `total` 的形参声明（最刁钻的解析），`best` 就变成函数声明了。
    std::vector<QList<State>> best(static_cast<size_t>(total));

    for (int index = 0; index < units.size(); ++index) {
        State state;
        state.cost = 0;
        state.penalty = units.at(index).penalty;
        state.isBook = units.at(index).isBook;
        state.enchantments = units.at(index).enchantments;
        best[size_t(1 << index)].append(state);
    }

    /// 剪枝：状态太多的话只留代价最低的那些（前科小的也留几个，免得把"以后更省"的那条剪掉）。
    constexpr int kMaxStatesPerMask = 12;

    for (int mask = 1; mask < total; ++mask) {
        if (__builtin_popcount(unsigned(mask)) < 2) {
            continue;
        }
        QList<State> &bucket = best[size_t(mask)];
        for (int left = (mask - 1) & mask; left > 0; left = (left - 1) & mask) {
            const int right = mask ^ left;
            if (right == 0) {
                continue;
            }
            // 每一对 (left, right) 只算一次（`left < right` 那一半）。
            if (left < right) {
                continue;
            }
            for (int li = 0; li < best[size_t(left)].size(); ++li) {
                for (int ri = 0; ri < best[size_t(right)].size(); ++ri) {
                    const State &leftState = best[size_t(left)].at(li);
                    const State &rightState = best[size_t(right)].at(ri);
                    // 左右不能对调着看（附魔价值只算牺牲品那一边），所以两种摆法都试。
                    for (int swapped = 0; swapped < 2; ++swapped) {
                        const State &targetState = swapped == 0 ? leftState : rightState;
                        const State &sacrificeState = swapped == 0 ? rightState : leftState;
                        Unit target;
                        target.enchantments = targetState.enchantments;
                        target.penalty = targetState.penalty;
                        Unit sacrifice;
                        sacrifice.enchantments = sacrificeState.enchantments;
                        sacrifice.penalty = sacrificeState.penalty;
                        // 附魔书的倍率那一列：牺牲品是**书**才按书算，是装备就按物品那一列。
                        // 合并出来的那一件跟着**左边**（留下）那一件 —— 铁砧的结果就是把
                        // 左边那件改一改，所以拿一件装备当底子合到最后还是那件装备。
                        sacrifice.isBook = sacrificeState.isBook;

                        MergeCost merge = mergeCost(target, sacrifice);
                        if (includeRename) {
                            merge.total += 1;  // 改个名 +1 级（wiki 那句 "adding a single
                                               // experience level"）
                            merge.tooExpensive = merge.total > 39;
                        }

                        State state;
                        state.cost = targetState.cost + sacrificeState.cost + merge.total;
                        state.penalty = merge.resultPenalty;
                        state.isBook = targetState.isBook;
                        state.maxStep = qMax(targetState.maxStep,
                                             qMax(sacrificeState.maxStep, merge.total));
                        state.enchantments = merge.result;
                        state.leftMask = left;
                        state.rightMask = right;
                        state.leftState = li;
                        state.rightState = ri;
                        state.leftIsSacrifice = swapped != 0;
                        state.merge = merge;
                        bucket.append(state);
                    }
                }
            }
        }

        // ---------- 剪枝 ----------
        std::sort(bucket.begin(), bucket.end(), [](const State &a, const State &b) {
            if (a.cost != b.cost) {
                return a.cost < b.cost;
            }
            if (a.maxStep != b.maxStep) {
                return a.maxStep < b.maxStep;
            }
            return a.penalty < b.penalty;
        });
        QList<State> kept;
        for (const State &candidate : bucket) {
            bool dominated = false;
            for (const State &keptState : kept) {
                if (keptState.cost <= candidate.cost && keptState.penalty <= candidate.penalty
                    && keptState.maxStep <= candidate.maxStep
                    && keptState.isBook == candidate.isBook
                    && keptState.enchantments == candidate.enchantments) {
                    dominated = true;
                    break;
                }
            }
            if (!dominated) {
                kept.append(candidate);
            }
            if (kept.size() >= kMaxStatesPerMask) {
                break;
            }
        }
        bucket = kept;
    }

    const QList<State> &finalStates = best[size_t(total - 1)];
    if (finalStates.isEmpty()) {
        plan.error = QStringLiteral("算不出计划来（这一组东西可能会有互斥的附魔）。");
        return plan;
    }

    // ---------- 选哪一条 ----------
    // ★ "合完是哪一件"要跟着用户抱来的那堆东西走：全是书 ⇒ 合出来还是书；里面有一件
    //   已经附过魔的装备 ⇒ 合出来必须是**那件装备**。不钉这一条的话，两件东西里"把装备
    //   当牺牲品喂给书"往往更便宜，算出来的计划就变成"把装备烧了换一本书"，不是用户要的。
    bool wantBook = true;
    for (const Unit &unit : units) {
        if (!unit.isBook) {
            wantBook = false;
            break;
        }
    }
    /// 同样地看"能不能做出来"：累计小但中间某一步超了 39 的那条在生存里做不出来。
    auto better = [](const State &candidate, const State &chosen) {
        const bool candidateFeasible = candidate.maxStep <= 39;
        const bool chosenFeasible = chosen.maxStep <= 39;
        if (candidateFeasible != chosenFeasible) {
            return candidateFeasible;
        }
        if (candidate.cost != chosen.cost) {
            return candidate.cost < chosen.cost;
        }
        return candidate.penalty < chosen.penalty;
    };

    // 目标附魔 = 全部用上时、按"合完还是那一件"这条规矩的最优结果（用户列了哪些书，
    // 就奔着那套结果去）。
    int targetMask = total - 1;
    int targetIndex = -1;
    for (int index = 0; index < finalStates.size(); ++index) {
        if (finalStates.at(index).isBook != wantBook) {
            continue;
        }
        if (targetIndex < 0 || better(finalStates.at(index), finalStates.at(targetIndex))) {
            targetIndex = index;
        }
    }
    if (targetIndex < 0) {  // 兜底：一套符合"合完还是那一件"的都没有，就照累计最小那条
        targetIndex = 0;
    }
    const QMap<QString, int> target = finalStates.at(targetIndex).enchantments;
    // 材料可以不全用：所以要在**所有子集**合出来的状态里找，不只是"全用上"那一档 ——
    // 合出来同一套附魔、少花几本的那条更好（剩下的那几本在 `unused` 里点名）。
    int chosenMask = targetMask;
    int chosenIndex = targetIndex;
    for (int mask = 1; mask < total; ++mask) {
        const QList<State> &bucket = best[size_t(mask)];
        for (int index = 0; index < bucket.size(); ++index) {
            const State &state = bucket.at(index);
            if (state.enchantments != target || state.isBook != wantBook) {
                continue;
            }
            const State &chosen = best[size_t(chosenMask)].at(chosenIndex);
            if (better(state, chosen)) {
                chosenMask = mask;
                chosenIndex = index;
            }
        }
    }
    const State *finalState = &best[size_t(chosenMask)].at(chosenIndex);

    // ---------- 把步骤重建出来 ----------
    struct Walk {
        const std::vector<QList<State>> *best = nullptr;
        const QList<Unit> *units = nullptr;
        QList<Step> steps;
        QList<int> consumed;
        int totalCost = 0;
        bool anyTooExpensive = false;

        /// 回一个"节点号"：叶子的节点号 = 输入下标，中间件的节点号 = 产出它的那一步在
        /// steps 里的序号（从输入个数开始数）。
        ///
        // ★ `identity`：这一件东西**是哪一件输入**（叶子下标）。中间件的那一件其实还是它左边
        //   那一件（铁砧的结果就是把左边那件改一改，不是凭空多出来一件），所以它的身份要
        //   一路传上来。`consumed` 收的是"被吃掉的那一件是**哪件输入**"—— 直接把
        //   `step.sacrificeIndex` 收进去的话，牺牲品是中间件的那一步就什么都不记（四件东西
        //   按两两配对的次序合，最后一步被吃的是中间件 ⇒ 只会记下两件，而实际上你做掉了三件）。
        std::map<int, int> identity;

        int walk(int mask, int index) {
            const State &state = (*best)[size_t(mask)].at(index);
            if (state.leftState < 0) {
                // 叶子的"节点号"就是输入下标，而它的身份也就是它自己 —— 表里不用记。
                return __builtin_ctz(unsigned(mask));
            }
            const int leftNode = walk(state.leftMask, state.leftState);
            const int rightNode = walk(state.rightMask, state.rightState);
            Step step;
            const int targetNode = state.leftIsSacrifice ? rightNode : leftNode;
            const int sacrificeNode = state.leftIsSacrifice ? leftNode : rightNode;
            step.targetIndex = targetNode < units->size() ? targetNode : -1;
            step.sacrificeIndex = sacrificeNode < units->size() ? sacrificeNode : -1;
            step.targetNode = step.targetIndex < 0 ? targetNode : -1;
            step.sacrificeNode = step.sacrificeIndex < 0 ? sacrificeNode : -1;
            step.cost = state.merge;
            step.result = state.enchantments;
            step.resultIsBook = state.isBook;
            steps.append(step);
            const int node = units->size() + steps.size() - 1;
            // 中间件在表里、叶子不在表里（叶子的身份就是它的下标）。
            const int targetIdentity = identity.count(targetNode) > 0 ? identity.at(targetNode) : targetNode;
            const int sacrificeIdentity =
                identity.count(sacrificeNode) > 0 ? identity.at(sacrificeNode) : sacrificeNode;
            identity[node] = targetIdentity;  // 这一件还是左边那一件
            totalCost += state.merge.total;
            if (state.merge.tooExpensive) {
                anyTooExpensive = true;
            }
            consumed.append(sacrificeIdentity);
            return node;
        }
    };

    Walk walk;
    walk.best = &best;
    walk.units = &units;
    walk.walk(chosenMask, chosenIndex);

    plan.ok = true;
    plan.steps = walk.steps;
    plan.totalCost = walk.totalCost;
    plan.finalPenalty = finalState->penalty;
    plan.finalEnchantments = finalState->enchantments;
    plan.finalIsBook = finalState->isBook;
    plan.anyTooExpensive = walk.anyTooExpensive;
    plan.withinSurvival = !walk.anyTooExpensive;
    for (int index = 0; index < units.size(); ++index) {
        if ((chosenMask & (1 << index)) == 0) {
            plan.unused.append(index);
        }
    }
    plan.consumed = walk.consumed;
    return plan;
}

}  // namespace Anvil
