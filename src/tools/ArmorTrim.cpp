/**
 * @file ArmorTrim.cpp
 * @brief ArmorTrim 的实现：纹饰材料表 / 图案表 / 部位与盔甲材质表 / 一条计划。
 */

#include "tools/ArmorTrim.hpp"

namespace {

struct TrimMaterialRow {
    const char *id;
    const char *name;
    const char *item;
    const char *since;  ///< 哪个 Java 版本开始有
};

// 名字照游戏 `zh_cn.json` 的 `trim_material.minecraft.*` 写（「紫水晶质」那一列），
// 不是自己另起一套简称；`item` 是锻造台里真正摆进去的那件东西。
const TrimMaterialRow kTrimMaterials[] = {
    {"amethyst", "紫水晶质", "紫水晶碎片", "1.20"},
    {"copper", "铜质", "铜锭", "1.20"},
    {"diamond", "钻石质", "钻石", "1.20"},
    {"emerald", "绿宝石质", "绿宝石", "1.20"},
    {"gold", "金质", "金锭", "1.20"},
    {"iron", "铁质", "铁锭", "1.20"},
    {"lapis", "青金石质", "青金石", "1.20"},
    {"netherite", "下界合金质", "下界合金锭", "1.20"},
    {"quartz", "石英质", "石英", "1.20"},
    {"redstone", "红石质", "红石粉", "1.20"},
    {"resin", "树脂质", "树脂砖", "1.21.4"},
};

struct TrimPatternRow {
    const char *id;
    const char *name;
    const char *source;    ///< 模板从哪儿来
    const char *duplicate; ///< 复制这枚模板要的那种方块
    const char *since;     ///< 哪个 Java 版本开始有
};

// 名字照游戏 `zh_cn.json` 的 `trim_pattern.minecraft.*` 写（「镶铆盔甲纹饰」那一列），
// 来源按 Java 1.20 / 1.21 的实际箱子写；不写"城堡里某处"这种含糊说法。
// `duplicate` 抄的是游戏自己的复制配方（`data/minecraft/recipe/*_armor_trim_smithing_template.json`
// 里那个 `C` 位），方块名照 `zh_cn.json`；四块陶瓦、四块圆石那几组是游戏原样，不是取整。
const TrimPatternRow kPatterns[] = {
    {"coast", "海岸", "沉船（藏宝箱）", "圆石", "1.20"},
    {"sentry", "哨兵", "掠夺者前哨站", "圆石", "1.20"},
    {"dune", "沙丘", "沙漠神殿", "砂岩", "1.20"},
    {"wild", "荒野", "丛林神庙", "苔石", "1.20"},
    {"ward", "监守", "远古城市", "深板岩圆石", "1.20"},
    {"eye", "眼眸", "要塞", "末地石", "1.20"},
    {"vex", "恼鬼", "林地府邸", "圆石", "1.20"},
    {"tide", "潮汐", "远古守卫者（击杀掉落）", "海晶石", "1.20"},
    {"snout", "猪鼻", "堡垒遗迹", "黑石", "1.20"},
    {"rib", "肋骨", "下界要塞", "下界岩", "1.20"},
    {"spire", "尖塔", "末地城", "紫珀块", "1.20"},
    {"wayfinder", "向导", "古迹废墟（可疑的沙砾）", "陶瓦", "1.20"},
    {"shaper", "塑造", "古迹废墟（可疑的沙砾）", "陶瓦", "1.20"},
    {"silence", "幽静", "远古城市", "深板岩圆石", "1.20"},
    {"raiser", "牧民", "古迹废墟（可疑的沙砾）", "陶瓦", "1.20"},
    {"host", "雇主", "古迹废墟（可疑的沙砾）", "陶瓦", "1.20"},
    {"flow", "涡流", "试炼密室（不祥宝库）", "旋风棒", "1.21"},
    {"bolt", "镶铆", "试炼密室（宝库）", "铜块", "1.21"},
};

const char *kParts[] = {"头盔", "胸甲", "护腿", "靴子"};

/// 盔甲材质：id / 中文名 / 全名里那个"前缀"。
struct ArmorMaterialRow {
    const char *id;
    const char *name;
};

const ArmorMaterialRow kArmorMaterials[] = {
    {"leather", "皮革"},   {"chainmail", "锁链"}, {"iron", "铁"},
    {"gold", "金"},        {"diamond", "钻石"},   {"netherite", "下界合金"},
    {"turtle", "海龟壳"},
};

constexpr int kTrimMaterialCount = int(sizeof(kTrimMaterials) / sizeof(kTrimMaterials[0]));
constexpr int kPatternCount = int(sizeof(kPatterns) / sizeof(kPatterns[0]));
constexpr int kPartCount = int(sizeof(kParts) / sizeof(kParts[0]));
constexpr int kArmorMaterialCount =
    int(sizeof(kArmorMaterials) / sizeof(kArmorMaterials[0]));

/// 海龟壳那个材质的下标（**只认头盔**那一条判据就靠它）。
constexpr int kTurtleShell = kArmorMaterialCount - 1;

bool validTrimMaterial(int index) { return index >= 0 && index < kTrimMaterialCount; }
bool validPattern(int index) { return index >= 0 && index < kPatternCount; }
bool validPart(int index) { return index >= 0 && index < kPartCount; }
bool validArmorMaterial(int index) { return index >= 0 && index < kArmorMaterialCount; }

}  // namespace

namespace ArmorTrim {

int trimMaterialCount() { return kTrimMaterialCount; }

TrimMaterial trimMaterialAt(int index) {
    if (!validTrimMaterial(index)) {
        return {};
    }
    return {QString::fromLatin1(kTrimMaterials[index].id),
            QString::fromUtf8(kTrimMaterials[index].name),
            QString::fromUtf8(kTrimMaterials[index].item)};
}

QString trimMaterialSince(int index) {
    return validTrimMaterial(index) ? QString::fromLatin1(kTrimMaterials[index].since) : QString();
}

QStringList trimMaterialNames() {
    QStringList list;
    for (const TrimMaterialRow &row : kTrimMaterials) {
        list.append(QString::fromUtf8(row.name));
    }
    return list;
}

int patternCount() { return kPatternCount; }

TrimPattern patternAt(int index) {
    if (!validPattern(index)) {
        return {};
    }
    return {QString::fromLatin1(kPatterns[index].id), QString::fromUtf8(kPatterns[index].name),
            QString::fromUtf8(kPatterns[index].source),
            QString::fromUtf8(kPatterns[index].duplicate),
            QString::fromLatin1(kPatterns[index].since)};
}

QString patternSource(int index) {
    return validPattern(index) ? QString::fromUtf8(kPatterns[index].source) : QString();
}

QString patternDuplicate(int index) {
    return validPattern(index) ? QString::fromUtf8(kPatterns[index].duplicate) : QString();
}

QStringList patternNames() {
    QStringList list;
    for (const TrimPatternRow &row : kPatterns) {
        list.append(QString::fromUtf8(row.name));
    }
    return list;
}

int partCount() { return kPartCount; }

QString partName(int index) {
    if (!validPart(index)) {
        return {};
    }
    return QString::fromUtf8(kParts[index]);
}

int armorMaterialCount() { return kArmorMaterialCount; }

QString armorMaterialId(int index) {
    return validArmorMaterial(index) ? QString::fromLatin1(kArmorMaterials[index].id) : QString();
}

QString armorMaterialName(int index) {
    if (!validArmorMaterial(index)) {
        return {};
    }
    return QString::fromUtf8(kArmorMaterials[index].name);
}

QString armorPieceName(int armorMaterial, int part) {
    if (!validArmorMaterial(armorMaterial) || !validPart(part)) {
        return {};
    }
    if (armorMaterial == kTurtleShell) {
        return QStringLiteral("海龟壳");
    }
    return QStringLiteral("%1%2").arg(armorMaterialName(armorMaterial), partName(part));
}

bool armorMaterialFitsPart(int armorMaterial, int part) {
    if (!validArmorMaterial(armorMaterial) || !validPart(part)) {
        return false;
    }
    if (armorMaterial == kTurtleShell) {
        return part == 0;  // 只有头盔
    }
    return true;
}

Plan plan(int armorMaterial, int part, int patternIndex, int trimMaterial, int pieces,
          bool wholeSet) {
    Plan result;
    if (!validArmorMaterial(armorMaterial)) {
        result.error = QStringLiteral("盔甲材质认不出来（下标 %1）。").arg(armorMaterial);
        return result;
    }
    if (!validPattern(patternIndex)) {
        result.error = QStringLiteral("纹饰图案认不出来（下标 %1）。").arg(patternIndex);
        return result;
    }
    if (!validTrimMaterial(trimMaterial)) {
        result.error = QStringLiteral("纹饰材料认不出来（下标 %1）。").arg(trimMaterial);
        return result;
    }
    if (wholeSet) {
        // 整套 = 四个部位各一件；海龟壳只有头盔，做不了整套。
        if (armorMaterial == kTurtleShell) {
            result.error = QStringLiteral("海龟壳只有头盔那一件 —— 没有整套。");
            return result;
        }
    } else {
        if (!validPart(part)) {
            result.error = QStringLiteral("部位认不出来（下标 %1）。").arg(part);
            return result;
        }
        if (pieces < 1 || pieces > 4) {
            result.error = QStringLiteral("做几件只能是 1–4 件（你给了 %1）。").arg(pieces);
            return result;
        }
        if (!armorMaterialFitsPart(armorMaterial, part)) {
            result.error = QStringLiteral("海龟壳只有头盔那一件 —— 没有%1。").arg(partName(part));
            return result;
        }
    }

    const TrimPattern pattern = patternAt(patternIndex);
    const TrimMaterial material = trimMaterialAt(trimMaterial);
    const QString templateName = QStringLiteral("%1纹饰模板").arg(pattern.name);

    // 一句话把"从哪来"和"再要一枚拿什么换"说完 —— 这两样是用户看这页真正要查的。
    // 模板本身和那七颗钻石不必写：前者就是这一枚，后者谁都备得住。
    result.notes.append(QStringLiteral("这枚%1来自：%2；复制用%3。")
                            .arg(templateName, pattern.source, pattern.duplicate));
    if (pattern.since != QStringLiteral("1.20")) {
        result.notes.append(QStringLiteral("%1是 Java %2 才加入的 —— 更早的版本里没有这枚模板。")
                                .arg(templateName, pattern.since));
    }
    const QString materialSince = trimMaterialSince(trimMaterial);
    if (!materialSince.isEmpty() && materialSince != QStringLiteral("1.20")) {
        result.notes.append(QStringLiteral("「%1」是 Java %2 才加入的纹饰材料 —— 更早的版本里没有。")
                                .arg(material.name, materialSince));
    }

    if (wholeSet) {
        // ---------- 整套四件 ----------
        QStringList pieceNames;
        for (int index = 0; index < kPartCount; ++index) {
            pieceNames.append(armorPieceName(armorMaterial, index));
        }
        const QString setLabel = QStringLiteral("%1四件套").arg(armorMaterialName(armorMaterial));

        result.steps.append(QStringLiteral("去锻造台，把%1 + %2 + 每件盔甲各摆一次（一共四次）")
                                .arg(templateName, material.item));
        for (const QString &piece : pieceNames) {
            result.steps.append(
                QStringLiteral("%1 + %2 + %3 → 「%2 / %4纹饰的%3」")
                    .arg(templateName, material.item, piece, pattern.name));
        }
        result.materials.append(QStringLiteral("%1 × %2").arg(templateName).arg(kPartCount));
        result.materials.append(QStringLiteral("%1 × %2").arg(material.item).arg(kPartCount));
        for (const QString &piece : pieceNames) {
            result.materials.append(QStringLiteral("%1 × 1").arg(piece));
        }
        result.notes.append(QStringLiteral("整套需要 %1 次锻造，模板和材料各 %1 份。")
                                .arg(kPartCount));
        result.summary = QStringLiteral("%1 / %2纹饰的%3")
                             .arg(material.name, pattern.name, setLabel);
        result.ok = true;
        return result;
    }

    // ---------- 单件 / 同一件做几份 ----------
    const QString armor = armorPieceName(armorMaterial, part);

    result.steps.append(QStringLiteral("去锻造台：把%1 + %2 + %3 摆进去")
                            .arg(templateName, material.item, armor));
    result.steps.append(QStringLiteral("一次锻造消耗 1 个模板、1 份材料、1 件%1 → 「%2 / %3纹饰的%1」")
                            .arg(armor, material.name, pattern.name));

    result.materials.append(QStringLiteral("%1 × %2").arg(templateName).arg(pieces));
    result.materials.append(QStringLiteral("%1 × %2").arg(material.item).arg(pieces));
    result.materials.append(QStringLiteral("%1 × %2").arg(armor).arg(pieces));

    if (pieces > 1) {
        result.notes.append(QStringLiteral("模板每打一件消耗一个：%1 件共需 %1 个。")
                                .arg(pieces));
    }
    result.summary = QStringLiteral("%1 / %2纹饰的%3 × %4")
                         .arg(material.name, pattern.name, armor)
                         .arg(pieces);
    result.ok = true;
    return result;
}

QString Plan::detailText() const {
    if (!ok) {
        return error.isEmpty() ? QStringLiteral("排不出来。") : error;
    }
    QStringList lines;
    lines.append(summary);
    lines.append(QString());
    lines.append(QStringLiteral("一步一步："));
    for (const QString &step : steps) {
        lines.append(QStringLiteral("• %1").arg(step));
    }
    if (!materials.isEmpty()) {
        lines.append(QString());
        lines.append(QStringLiteral("材料清单："));
        for (const QString &row : materials) {
            lines.append(QStringLiteral("• %1").arg(row));
        }
    }
    if (!notes.isEmpty()) {
        lines.append(QString());
        for (const QString &note : notes) {
            lines.append(QStringLiteral("（%1）").arg(note));
        }
    }
    return lines.join(QChar('\n'));
}

}  // namespace ArmorTrim
