/**
 * @file Beacon.cpp
 * @brief Beacon.hpp 的实现（数值与那张效果表，见头文件）。
 * @author 0x7fffff
 * @date 2026-10-18
 */

#include "tools/Beacon.hpp"

namespace Beacon {

namespace {

/// 效果的解锁表：第 i 项是"到第 i+1 级才出现的那一批"。
const char *const kUnlockedByLevel[][2] = {
    {"速度", "急迫"},
    {"抗性提升", "跳跃提升"},
    {"力量", nullptr},
    {"再生", nullptr},
};

}  // namespace

int minLevel() { return 1; }
int maxLevel() { return 4; }

bool isValidLevel(int level) { return level >= minLevel() && level <= maxLevel(); }

bool isValidFromLevel(int fromLevel) { return fromLevel >= 0 && fromLevel < maxLevel(); }

int layerBlocks(int level) {
    if (!isValidLevel(level)) return 0;
    const int side = 2 * level + 1;
    return side * side;
}

int totalBlocks(int level) {
    if (!isValidLevel(level)) return 0;
    int total = 0;
    for (int k = minLevel(); k <= level; ++k) total += layerBlocks(k);
    return total;
}

int blocksToAdd(int fromLevel, int toLevel) {
    if (fromLevel < 0 || fromLevel > maxLevel() || !isValidLevel(toLevel) || toLevel <= fromLevel) {
        return 0;
    }
    return totalBlocks(toLevel) - totalBlocks(fromLevel);
}

int effectRange(int level) {
    if (!isValidLevel(level)) return 0;
    return 10 * level + 10;
}

QStringList effectNames() {
    QStringList names;
    for (const auto &tier : kUnlockedByLevel) {
        for (const char *name : tier) {
            if (name != nullptr) names.append(QString::fromUtf8(name));
        }
    }
    return names;
}

// ---------- 材质与矿物折算 ----------
//
// 信标底座只认这五种方块（铁 / 金 / 钻石 / 绿宝石 / 下界合金）。方块数、层数、效果与
// 材质无关 —— 这一节只回答"这些方块折成多少个锭 / 宝石"，不做"哪种更划算"的价值判断。

namespace {

struct MaterialInfo {
    const char *block;  ///< 「铁块」
    const char *unit;   ///< 「铁锭」
};

// 次序必须与 `Material` 的整数值一一对应。
const MaterialInfo kMaterials[] = {
    {"铁块", "铁锭"},
    {"金块", "金锭"},
    {"钻石块", "钻石"},
    {"绿宝石块", "绿宝石"},
    {"下界合金块", "下界合金锭"},
};

constexpr int kMaterialCount = int(sizeof(kMaterials) / sizeof(kMaterials[0]));

}  // namespace

QStringList materialNames() {
    QStringList names;
    for (const MaterialInfo &info : kMaterials) names.append(QString::fromUtf8(info.block));
    return names;
}

bool isValidMaterial(int index) { return index >= 0 && index < kMaterialCount; }

QString blockName(int index) {
    return isValidMaterial(index) ? QString::fromUtf8(kMaterials[index].block) : QString();
}

QString unitName(int index) {
    return isValidMaterial(index) ? QString::fromUtf8(kMaterials[index].unit) : QString();
}

QString materialName(int index) { return blockName(index); }

int unitsPerBlock() { return 9; }

int unitsForBlocks(int index, int blocks) {
    if (!isValidMaterial(index) || blocks <= 0) return 0;
    return blocks * unitsPerBlock();
}

QString materialText(int index, int blocks) {
    if (!isValidMaterial(index)) {
        return QStringLiteral("认不出这种底座方块（%1）。").arg(index);
    }
    if (blocks <= 0) return QString();
    return QStringLiteral("%1 块%2 = %3 个%4（每块 %5 个）")
        .arg(blocks)
        .arg(blockName(index))
        .arg(unitsForBlocks(index, blocks))
        .arg(unitName(index))
        .arg(unitsPerBlock());
}

// ---------- 多颗信标共用底座 ----------

int maxBeaconsPerBase() { return 9; }

int sharedBaseBlocks(int level, int beaconCount) {
    if (!isValidLevel(level) || beaconCount < 1 || beaconCount > maxBeaconsPerBase()) return 0;
    // 共用同一座金字塔时方块数**不随颗数增加** —— 顶面是一整块 3 × 3，几颗信标都站在上面。
    return totalBlocks(level);
}

QString sharedBaseText(int level, int beaconCount) {
    if (!isValidLevel(level)) {
        return QStringLiteral("信标等级只有 1 – 4 级（%1 不是一个真的等级）。").arg(level);
    }
    if (beaconCount < 1 || beaconCount > maxBeaconsPerBase()) {
        return QStringLiteral("同一座金字塔上能放 1 – %1 颗信标（%2 颗放不下）。")
            .arg(maxBeaconsPerBase())
            .arg(beaconCount);
    }
    if (beaconCount == 1) {
        return QStringLiteral("一颗信标：底座 %1 块。信标本体另外要 3 块黑曜石 + 1 颗下界之星 + 3 块玻璃。")
            .arg(totalBlocks(level));
    }
    // 每颗信标自己那一套材料按颗数算；底座只有一座。
    return QStringLiteral("%1 颗信标共用一座 %2 级金字塔：底座仍然只要 %3 块（顶面 3 × 3，最多放 %4 颗）；"
                          "每颗信标自己另外要 3 块黑曜石 + 1 颗下界之星 + 3 块玻璃，%1 颗一共 %5 块黑曜石、"
                          "%1 颗下界之星、%5 块玻璃。")
        .arg(beaconCount)
        .arg(level)
        .arg(totalBlocks(level))
        .arg(maxBeaconsPerBase())
        .arg(beaconCount * 3);
}

QStringList effectsForLevel(int level) {
    QStringList names;
    if (!isValidLevel(level)) return names;
    for (int tier = 0; tier < level; ++tier) {
        for (const char *name : kUnlockedByLevel[tier]) {
            if (name != nullptr) names.append(QString::fromUtf8(name));
        }
    }
    return names;
}

QString summaryText(int level) {
    if (!isValidLevel(level)) {
        return QStringLiteral("信标等级只有 1 – 4 级（%1 不是一个真的等级）。").arg(level);
    }
    return QStringLiteral("%1 级信标：范围 %2 格，累计 %3 块。")
        .arg(level)
        .arg(effectRange(level))
        .arg(totalBlocks(level));
}

QString detailText(int fromLevel, int level) {
    if (!isValidLevel(level)) {
        return QStringLiteral("信标等级只有 1 – 4 级（%1 不是一个真的等级）。").arg(level);
    }
    if (fromLevel < 0 || fromLevel > maxLevel()) {
        return QStringLiteral("「现在已有几级」只能是 0 – 4（%1 越界了）。").arg(fromLevel);
    }

    QStringList lines;
    lines << summaryText(level);
    // ★ 说的是**从下面数第 level 层**（搭到这一级时新铺的那一层，也是最大的那一层）——
    //   不是"最上面那一层"：最上面永远是 3 × 3。
    lines << QStringLiteral("· 从下面数第 %1 层：%2 × %2 = %3 块（最上面那层永远是 3 × 3）")
                 .arg(level)
                 .arg(2 * level + 1)
                 .arg(layerBlocks(level));
    lines << QStringLiteral("· 从零搭起一共要 %1 块（每一层比下面那层窄两格；这个数**不含**信标本体）")
                 .arg(totalBlocks(level));
    if (fromLevel > 0 && fromLevel < level) {
        lines << QStringLiteral("· 你现在是 %1 级：再补 %2 块就够")
                     .arg(fromLevel)
                     .arg(blocksToAdd(fromLevel, level));
    } else if (fromLevel == level) {
        lines << QStringLiteral("· 你现在已经是 %1 级了，不用再加方块").arg(level);
    } else if (fromLevel > level) {
        lines << QStringLiteral("· 你现在是 %1 级，已经比这个目标高了（要降低就得自己拆）")
                     .arg(fromLevel);
    }
    lines << QStringLiteral("· 效果范围：%1 格").arg(effectRange(level));
    lines << QStringLiteral("· 可以给：%1").arg(effectsForLevel(level).join(QStringLiteral("、")));
    if (level < maxLevel()) {
        lines << QStringLiteral("· 再升一级（%1 级）要再加 %2 块，范围变成 %3 格")
                     .arg(level + 1)
                     .arg(layerBlocks(level + 1))
                     .arg(effectRange(level + 1));
    } else {
        lines << QStringLiteral("· 已经到顶了：4 级就是最高一级");
    }
    lines << QStringLiteral("· 信标要放在金字塔正中间、上面留出天空（玻璃可以，别的方块不行）");
    return lines.join(QLatin1Char('\n'));
}

}  // namespace Beacon
