/**
 * @file Brewing.cpp
 * @brief Brewing.hpp 的实现（那张表与路线拼装，见头文件）。
 */

#include "tools/Brewing.hpp"
#include <QCoreApplication>
#include "tools/GameVersion.hpp"

#include <QHash>

namespace Brewing {

namespace {

/// 药的配方表。**次序就是界面上下拉框的次序**（把常做的几种摆前面）。
///
/// NOTE: `parent` 只指向**表里更靠前**的那一种 —— 这样"从上往下补全路线"永远不会绕
/// 出一只环，也就不需要额外做环检测。
struct Recipe {
    const char *name;
    const char *ingredient;
    int parent;
    bool fromWater;
    bool extendable;
    bool upgradable;
    int upgradedLevel = 2;  ///< 荧石粉之后是几级（只有迟缓是 IV，见下面那一条）
    const char *since = nullptr;  ///< 从哪一版起才有（空 = 各版本都有）
};

const Recipe kRecipes[] = {
    {QT_TRANSLATE_NOOP("Brewing", "迅捷"), QT_TRANSLATE_NOOP("Brewing", "糖"), -1, false, true, true},
    {QT_TRANSLATE_NOOP("Brewing", "跳跃"), QT_TRANSLATE_NOOP("Brewing", "兔子脚"), -1, false, true, true},
    {QT_TRANSLATE_NOOP("Brewing", "治疗"), QT_TRANSLATE_NOOP("Brewing", "闪烁的西瓜片"), -1, false, false, true},
    {QT_TRANSLATE_NOOP("Brewing", "伤害"), QT_TRANSLATE_NOOP("Brewing", "发酵蜘蛛眼"), 2, false, false, true},
    {QT_TRANSLATE_NOOP("Brewing", "中毒"), QT_TRANSLATE_NOOP("Brewing", "蜘蛛眼"), -1, false, true, true},
    {QT_TRANSLATE_NOOP("Brewing", "力量"), QT_TRANSLATE_NOOP("Brewing", "烈焰粉"), -1, false, true, true},
    // 再生能加红石延长（1:30 → 3:00），荧石粉升 II —— 之前那张表把它标成了「不能延长」。
    {QT_TRANSLATE_NOOP("Brewing", "再生"), QT_TRANSLATE_NOOP("Brewing", "恶魂之泪"), -1, false, true, true},
    {QT_TRANSLATE_NOOP("Brewing", "抗火"), QT_TRANSLATE_NOOP("Brewing", "岩浆膏"), -1, false, true, false},
    {QT_TRANSLATE_NOOP("Brewing", "水肺"), QT_TRANSLATE_NOOP("Brewing", "河豚"), -1, false, true, false},
    {QT_TRANSLATE_NOOP("Brewing", "夜视"), QT_TRANSLATE_NOOP("Brewing", "金胡萝卜"), -1, false, true, false},
    {QT_TRANSLATE_NOOP("Brewing", "隐身"), QT_TRANSLATE_NOOP("Brewing", "发酵蜘蛛眼"), 9, false, true, false},
    // 荧石粉给的是**迟缓 IV**（不是 II）—— 这是唯一一种升级跳两级的药水。
    {QT_TRANSLATE_NOOP("Brewing", "迟缓"), QT_TRANSLATE_NOOP("Brewing", "发酵蜘蛛眼"), 0, false, true, true, 4},
    {QT_TRANSLATE_NOOP("Brewing", "缓降"), QT_TRANSLATE_NOOP("Brewing", "幻翼膜"), -1, true, true, false},
    {QT_TRANSLATE_NOOP("Brewing", "虚弱"), QT_TRANSLATE_NOOP("Brewing", "发酵蜘蛛眼"), -1, true, true, false},
    {QT_TRANSLATE_NOOP("Brewing", "海龟大师"), QT_TRANSLATE_NOOP("Brewing", "海龟壳"), -1, false, true, true},
    // 1.21 加的四瓶：都从**粗制的药水**来（不是水瓶直做），也没有延长 / II 级版本。
    // 材料名照 `zh_cn.json`：旋风棒 / 蜘蛛网 / 黏液块 / 石头。
    {QT_TRANSLATE_NOOP("Brewing", "盘丝"), QT_TRANSLATE_NOOP("Brewing", "蜘蛛网"), -1, false, false, false, 2, "1.21"},
    {QT_TRANSLATE_NOOP("Brewing", "蓄风"), QT_TRANSLATE_NOOP("Brewing", "旋风棒"), -1, false, false, false, 2, "1.21"},
    {QT_TRANSLATE_NOOP("Brewing", "渗浆"), QT_TRANSLATE_NOOP("Brewing", "黏液块"), -1, false, false, false, 2, "1.21"},
    {QT_TRANSLATE_NOOP("Brewing", "虫蚀"), QT_TRANSLATE_NOOP("Brewing", "石头"), -1, false, false, false, 2, "1.21"},
};

int recipeCount() { return int(sizeof(kRecipes) / sizeof(kRecipes[0])); }

QString potionText(const QString &name) { return name + QCoreApplication::translate("Brewing", "药水"); }

QString nameWithModifiers(const QString &name, bool extended, bool level2, Form form,
                          int upgradedLevel) {
    QString prefix;
    switch (form) {
    case Form::Splash: prefix = QCoreApplication::translate("Brewing", "喷溅型"); break;
    case Form::Lingering: prefix = QCoreApplication::translate("Brewing", "滞留型"); break;
    case Form::Normal: break;
    }
    QString text = prefix + potionText(name);
    if (extended) text += QCoreApplication::translate("Brewing", "（延长）");
    if (level2) text += QLatin1Char(' ') + levelText(upgradedLevel > 0 ? upgradedLevel : 2);
    return text;
}

}  // namespace

int formCount() { return 3; }

QString formName(Form form) {
    switch (form) {
    case Form::Normal: return QCoreApplication::translate("Brewing", "普通");
    case Form::Splash: return QCoreApplication::translate("Brewing", "喷溅型");
    case Form::Lingering: return QCoreApplication::translate("Brewing", "滞留型");
    }
    return QString();
}

QString levelText(int level) {
    if (level <= 0) return QString();
    switch (level) {
    case 1: return QStringLiteral("I");
    case 2: return QStringLiteral("II");
    case 3: return QStringLiteral("III");
    case 4: return QStringLiteral("IV");
    default: return QString::number(level);
    }
}

int maxBottlesPerOperation() { return 3; }
int maxBottles() { return 64; }

int operationsFor(int bottles) {
    if (bottles <= 0) return 0;
    return (bottles + maxBottlesPerOperation() - 1) / maxBottlesPerOperation();
}

int blazePowderFor(int operations) {
    if (operations <= 0) return 0;
    return (operations + 19) / 20;  // 1 个烈焰粉够 20 次酿造
}

int potionCount() { return recipeCount(); }

PotionInfo potionAt(int index) {
    PotionInfo info;
    if (index < 0 || index >= recipeCount()) return info;
    const Recipe &recipe = kRecipes[index];
    info.name = QCoreApplication::translate("Brewing", recipe.name);
    info.ingredient = QCoreApplication::translate("Brewing", recipe.ingredient);
    info.parent = recipe.parent;
    info.fromWater = recipe.fromWater;
    info.extendable = recipe.extendable;
    info.upgradable = recipe.upgradable;
    info.upgradedLevel = recipe.upgradable ? recipe.upgradedLevel : 0;
    info.since = recipe.since != nullptr ? QString::fromLatin1(recipe.since) : QString();
    return info;
}

QStringList potionNames() {
    QStringList names;
    for (const Recipe &recipe : kRecipes) names.append(QCoreApplication::translate("Brewing", recipe.name));
    return names;
}

QString potionSince(int index) {
    if (index < 0 || index >= recipeCount() || kRecipes[index].since == nullptr) {
        return QString();
    }
    return QString::fromLatin1(kRecipes[index].since);
}

bool available(int index, const QString &version) {
    if (index < 0 || index >= recipeCount()) {
        return false;
    }
    return GameVersion::availableIn(potionSince(index), version);
}

QString Plan::detailText() const {
    if (!ok) return error;
    QStringList lines;
    lines << summary;
    lines << QString();
    for (int i = 0; i < steps.size(); ++i) {
        lines << QStringLiteral("%1. %2").arg(i + 1).arg(steps.at(i));
    }
    lines << QString();
    lines << QCoreApplication::translate("Brewing", "• 要 %1 瓶：一共酿 %2 次（一次最多 %3 瓶）")
                 .arg(bottles)
                 .arg(operations)
                 .arg(maxBottlesPerOperation());
    if (!materials.isEmpty()) {
        lines << QCoreApplication::translate("Brewing", "• 材料合计（每一步按 %1 次备料）：%2")
                     .arg(operations)
                     .arg(materials.join(QCoreApplication::translate("Brewing", "、")));
    }
    lines << QCoreApplication::translate("Brewing", "• 玻璃瓶 %1 个（装水用）。").arg(bottles);
    lines << QCoreApplication::translate("Brewing", "• 烈焰粉燃料：酿 %1 次 ⇒ %2 个（1 个烈焰粉够 20 次酿造）。")
                 .arg(operations)
                 .arg(blazePowderFor(operations));
    return lines.join(QLatin1Char('\n'));
}

Plan plan(int potionIndex, bool extended, bool level2, Form form, int bottles) {
    Plan result;
    if (potionIndex < 0 || potionIndex >= recipeCount()) {
        result.error = QCoreApplication::translate("Brewing", "认不出这种药水（下标越界）。");
        return result;
    }
    if (extended && level2) {
        result.error = QCoreApplication::translate("Brewing", "红石粉（延长）和荧石粉（升一级）在游戏里是二选一：" "后加的那一样会把先加的冲掉。");
        return result;
    }
    const PotionInfo target = potionAt(potionIndex);
    if (extended && !target.extendable) {
        result.error = QCoreApplication::translate("Brewing", "%1没有延长版本（游戏里不能给它加红石粉）。")
                           .arg(potionText(target.name));
        return result;
    }
    if (level2 && !target.upgradable) {
        result.error = QCoreApplication::translate("Brewing", "%1没有强化版本（游戏里不能给它加荧石粉）。")
                           .arg(potionText(target.name));
        return result;
    }
    if (int(form) < 0 || int(form) >= formCount()) {
        result.error = QCoreApplication::translate("Brewing", "认不出这种形态（下标越界）。");
        return result;
    }
    if (bottles < 1 || bottles > maxBottles()) {
        result.error = QCoreApplication::translate("Brewing", "瓶数只能是 1 – %1（%2 不在这个范围里）。")
                           .arg(maxBottles())
                           .arg(bottles);
        return result;
    }

    result.bottles = bottles;
    result.operations = operationsFor(bottles);

    // 材料记账：每一种材料出现一次就算一份，最后统一乘"酿几次"。
    QStringList ingredientOrder;
    QHash<QString, int> ingredientCounts;
    const auto addIngredient = [&ingredientOrder, &ingredientCounts](const QString &name) {
        if (name.isEmpty()) return;
        if (!ingredientCounts.contains(name)) ingredientOrder << name;
        ingredientCounts[name] += 1;
    };

    // ---------- 把"从水瓶到目标"那一条链摆出来 ----------
    QList<int> chain;
    int cursor = potionIndex;
    int guard = 0;
    while (cursor >= 0 && guard++ <= recipeCount()) {
        chain.prepend(cursor);
        cursor = potionAt(cursor).parent;
    }

    result.steps << QCoreApplication::translate("Brewing", "玻璃瓶装水 → 水瓶（要几瓶就摆几瓶，一次最多 %1 瓶）")
                        .arg(maxBottlesPerOperation());
    bool first = true;
    for (int index : chain) {
        const PotionInfo info = potionAt(index);
        const QString produced = potionText(info.name);
        if (first && info.fromWater) {
            result.steps << QCoreApplication::translate("Brewing", "水瓶 + %1 → %2").arg(info.ingredient, produced);
            addIngredient(info.ingredient);
        } else {
            if (first) {
                result.steps << QCoreApplication::translate("Brewing", "水瓶 + 下界疣 → 粗制的药水");
                addIngredient(QCoreApplication::translate("Brewing", "下界疣"));
            }
            const QString source = first ? QCoreApplication::translate("Brewing", "粗制的药水")
                                         : potionText(potionAt(info.parent).name);
            result.steps << QStringLiteral("%1 + %2 → %3").arg(source, info.ingredient, produced);
            addIngredient(info.ingredient);
        }
        first = false;
    }

    // ---------- 后处理 ----------
    if (extended) {
        result.steps << QCoreApplication::translate("Brewing", "红石粉 → 时长延长");
        addIngredient(QCoreApplication::translate("Brewing", "红石粉"));
    }
    if (level2) {
        result.steps << QCoreApplication::translate("Brewing", "荧石粉 → 等级提升到 %1")
                            .arg(levelText(target.upgradedLevel));
        addIngredient(QCoreApplication::translate("Brewing", "荧石粉"));
    }
    switch (form) {
    case Form::Normal: break;
    case Form::Splash:
        result.steps << QCoreApplication::translate("Brewing", "火药 → 喷溅型");
        addIngredient(QCoreApplication::translate("Brewing", "火药"));
        break;
    case Form::Lingering:
        // 龙息只能加在喷溅药水上：顺序里先把火药补上，再上龙息。
        result.steps << QCoreApplication::translate("Brewing", "火药 → 喷溅型");
        result.steps << QCoreApplication::translate("Brewing", "龙息 → 滞留型");
        addIngredient(QCoreApplication::translate("Brewing", "火药"));
        addIngredient(QCoreApplication::translate("Brewing", "龙息"));
        break;
    }

    // 每一次酿造消耗一份材料（和瓶数无关：一次最多 3 瓶，材料只放 1 个）。
    for (const QString &name : ingredientOrder) {
        result.materials << QStringLiteral("%1 × %2")
                                .arg(name)
                                .arg(ingredientCounts.value(name) * result.operations);
    }

    result.ok = true;
    result.summary = QCoreApplication::translate("Brewing", "一共 %1 步 / 产出：%2")
                         .arg(result.steps.size())
                         .arg(nameWithModifiers(target.name, extended, level2, form,
                                                target.upgradedLevel));
    if (bottles != 1) {
        result.summary += QCoreApplication::translate("Brewing", " / %1 瓶（酿 %2 次）")
                              .arg(bottles)
                              .arg(result.operations);
    }
    return result;
}

}  // namespace Brewing
