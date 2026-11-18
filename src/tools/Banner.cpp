/**
 * @file Banner.cpp
 * @brief Banner 的实现：图案表、十六色表、一条叠层计划（见头文件那一段）。
 */

#include "tools/Banner.hpp"

#include "tools/GameVersion.hpp"

#include <QHash>

namespace {

/// 图案表（id / 中文名 / 织布机里要摆的材料 / 从哪一版起）。**次序就是界面下拉框的次序**。
///
/// 材料一栏：原版里大多数图案用「旗帜图案」这件物品（按图案分种类），只有下面这几种
/// 用别的物品 —— 表里写死，界面不再猜。
///
/// 最后一栏（`since`）：空 = 1.14 织布机那批就有的。**只给 1.21 才加的那四种写死**
/// （涡流 / 狂风是试炼密室那批；砖墙纹 / 波纹边框是跟基岩版对齐时进 Java 的）——
/// 版本对不上时游戏里根本找不到这几样，计划里得提醒一句。
struct PatternRow {
    const char *id;
    const char *name;
    const char *item;
    const char *since;
};

const PatternRow kPatterns[] = {
    {"bordure", "边框", "砖块", nullptr},
    // 1.21.2 added the pattern ITEMS, not these already-existing loom designs:
    // https://www.minecraft.net/en-us/article/minecraft-java-edition-1-21-2
    {"bordure_indented", "波纹边框", "藤蔓", nullptr},
    {"circle", "圆形", "旗帜图案", nullptr},
    {"creeper_charge", "苦力怕", "苦力怕头颅", nullptr},
    {"cross", "十字", "旗帜图案", nullptr},
    {"curly_border", "卷曲边框", "旗帜图案", nullptr},
    {"field_masoned", "砖墙纹", "砖块", nullptr},
    {"flower_charge", "花朵", "滨菊", nullptr},
    {"flow", "涡流", "旗帜图案", "1.21"},
    {"globe", "地球", "橡树树苗", nullptr},
    {"gradient", "渐变", "旗帜图案", nullptr},
    {"gradient_up", "上渐变", "旗帜图案", nullptr},
    {"guster", "狂风", "旗帜图案", "1.21"},
    {"half_horizontal", "上半", "旗帜图案", nullptr},
    {"half_horizontal_bottom", "下半", "旗帜图案", nullptr},
    {"half_vertical", "左半", "旗帜图案", nullptr},
    {"half_vertical_right", "右半", "旗帜图案", nullptr},
    {"mojang", "Mojang 标志", "附魔金苹果", nullptr},
    {"piglin", "猪灵", "旗帜图案", "1.16"},
    {"rhombus", "菱形", "旗帜图案", nullptr},
    {"skull_charge", "骷髅", "凋灵骷髅头颅", nullptr},
    {"snout", "猪鼻", "金胡萝卜", "1.16"},
    {"straight_cross", "正十字", "旗帜图案", nullptr},
    {"stripe_bottom", "底横条", "旗帜图案", nullptr},
    {"stripe_center", "中竖条", "旗帜图案", nullptr},
    {"stripe_downleft", "左下斜条", "旗帜图案", nullptr},
    {"stripe_downright", "右下斜条", "旗帜图案", nullptr},
    {"stripe_left", "左侧竖条", "旗帜图案", nullptr},
    {"stripe_middle", "中横条", "旗帜图案", nullptr},
    {"stripe_right", "右侧竖条", "旗帜图案", nullptr},
    {"stripe_small", "细横条", "旗帜图案", nullptr},
    {"stripe_top", "顶横条", "旗帜图案", nullptr},
    {"square_bottom_left", "左下角", "旗帜图案", nullptr},
    {"square_bottom_right", "右下角", "旗帜图案", nullptr},
    {"square_top_left", "左上角", "旗帜图案", nullptr},
    {"square_top_right", "右上角", "旗帜图案", nullptr},
    {"thing", "神秘图案", "附魔金苹果", nullptr},
    {"triangle_bottom", "底三角", "旗帜图案", nullptr},
    {"triangle_top", "顶三角", "旗帜图案", nullptr},
    {"triangles_bottom", "底双三角", "旗帜图案", nullptr},
    {"triangles_top", "顶双三角", "旗帜图案", nullptr},
    {"diagonal_left", "左斜条", "旗帜图案", nullptr},
    {"diagonal_right", "右斜条", "旗帜图案", nullptr},
    {"diagonal_up_left", "左上斜条", "旗帜图案", nullptr},
    {"diagonal_up_right", "右上斜条", "旗帜图案", nullptr},
};

/// 十六色（id / 中文名）。**次序和游戏里染料、羊毛的次序一致**（0 = 白 … 15 = 黑）。
struct ColorRow {
    const char *id;
    const char *name;
};

const ColorRow kColors[] = {
    {"white", "白色"},       {"orange", "橙色"},     {"magenta", "品红色"},
    {"light_blue", "淡蓝色"}, {"yellow", "黄色"},     {"lime", "黄绿色"},
    {"pink", "粉红色"},      {"gray", "灰色"},       {"light_gray", "淡灰色"},
    {"cyan", "青色"},        {"purple", "紫色"},     {"blue", "蓝色"},
    {"brown", "棕色"},       {"green", "绿色"},      {"red", "红色"},
    {"black", "黑色"},
};

constexpr int kPatternCount = int(sizeof(kPatterns) / sizeof(kPatterns[0]));
constexpr int kColorCount = int(sizeof(kColors) / sizeof(kColors[0]));

bool validColor(int index) { return index >= 0 && index < kColorCount; }
bool validPattern(int index) { return index >= 0 && index < kPatternCount; }

/// 材料清单按"第一次出现"的次序累计（同一件东西合并成一行 `× N`）。
class MaterialList {
public:
    void add(const QString &name, int count = 1) {
        if (count <= 0) {
            return;
        }
        const auto it = m_counts.find(name);
        if (it == m_counts.end()) {
            m_order.append(name);
            m_counts.insert(name, count);
        } else {
            it.value() += count;
        }
    }
    QStringList rows() const {
        QStringList list;
        for (const QString &name : m_order) {
            list.append(QStringLiteral("%1 × %2").arg(name).arg(m_counts.value(name)));
        }
        return list;
    }

private:
    QStringList m_order;
    QHash<QString, int> m_counts;
};

}  // namespace

namespace Banner {

int patternCount() { return kPatternCount; }

PatternInfo patternAt(int index) {
    if (!validPattern(index)) {
        return {};
    }
    return {QString::fromLatin1(kPatterns[index].id), QString::fromUtf8(kPatterns[index].name),
            QString::fromUtf8(kPatterns[index].item),
            kPatterns[index].since != nullptr ? QString::fromLatin1(kPatterns[index].since)
                                              : QStringLiteral("1.14")};
}

QStringList patternNames() {
    QStringList list;
    for (const PatternRow &row : kPatterns) {
        list.append(QString::fromUtf8(row.name));
    }
    return list;
}

int patternIndexById(const QString &id) {
    for (int index = 0; index < kPatternCount; ++index) {
        if (id == QLatin1String(kPatterns[index].id)) {
            return index;
        }
    }
    return -1;
}

int colorCount() { return kColorCount; }

QString patternSince(int index) {
    return validPattern(index) ? patternAt(index).since : QString();
}

QString colorName(int index) {
    if (!validColor(index)) {
        return {};
    }
    return QString::fromUtf8(kColors[index].name);
}

QString colorId(int index) {
    if (!validColor(index)) {
        return {};
    }
    return QString::fromLatin1(kColors[index].id);
}

QString dyeName(int index) {
    if (!validColor(index)) {
        return {};
    }
    return QStringLiteral("%1染料").arg(colorName(index));
}

Plan plan(int baseColorIndex, const QList<Layer> &layers, const QString &version) {
    Plan result;
    if (!validColor(baseColorIndex)) {
        result.error = QStringLiteral("底色认不出来（下标 %1）—— 十六色里挑一个。")
                           .arg(baseColorIndex);
        return result;
    }
    if (layers.size() > kMaxLayers) {
        result.error = QStringLiteral("原版织布机最多叠 %1 层（你现在排了 %2 层）—— 去掉几层再来。")
                           .arg(kMaxLayers)
                           .arg(layers.size());
        return result;
    }
    for (int index = 0; index < layers.size(); ++index) {
        if (!validPattern(layers[index].patternIndex)) {
            result.error = QStringLiteral("第 %1 层的图案认不出来（下标 %2）。")
                               .arg(index + 1)
                               .arg(layers[index].patternIndex);
            return result;
        }
        if (!validColor(layers[index].colorIndex)) {
            result.error = QStringLiteral("第 %1 层的颜色认不出来（下标 %2）。")
                               .arg(index + 1)
                               .arg(layers[index].colorIndex);
            return result;
        }
    }

    const QString baseName = colorName(baseColorIndex);
    MaterialList materials;

    QString previousKey;
    QStringList newerPatterns;  // 1.21 才加的那几种（同一图案只提醒一次）
    for (int index = 0; index < layers.size(); ++index) {
        const PatternInfo pattern = patternAt(layers[index].patternIndex);
        const QString dye = dyeName(layers[index].colorIndex);
        const QString banner = index == 0 ? QStringLiteral("%1空白旗帜").arg(baseName)
                                          : QStringLiteral("上一层的旗帜");
        result.steps.append(QStringLiteral("第 %1 层：织布机里放%2 + %3 + %4%5 → 叠出「%6」")
                                .arg(index + 1)
                                .arg(banner, dye, pattern.name, pattern.item)
                                .arg(pattern.name));
        materials.add(dye, 1);
        materials.add(pattern.item, 1);
        if (pattern.since != QStringLiteral("1.14") && !newerPatterns.contains(pattern.name)) {
            newerPatterns.append(pattern.name);
            result.notes.append(QStringLiteral("「%1」是 %2 起才加的图案 —— 更早的版本里织布机"
                                               "里找不到这一样。")
                                    .arg(pattern.name, pattern.since));
        }

        const QString key = QStringLiteral("%1|%2")
                                .arg(layers[index].patternIndex)
                                .arg(layers[index].colorIndex);
        if (index > 0 && key == previousKey) {
            result.notes.append(QStringLiteral("第 %1 层和第 %2 层完全一样 —— 画面上等于只叠了一层。")
                                    .arg(index + 1)
                                    .arg(index));
        }
        previousKey = key;
    }

    // 指令那一行只在 6 层以内（这儿已经过闸）。
    //
    // 写法按目标版本分两套：1.20.5 起 NBT 那串不再被 `/give` 接受，图案改走物品组件
    // （`[minecraft:banner_patterns=[{pattern:"minecraft:cross",color:"white"}]]`）。
    // 「不限版本」（空串）照老写法给 —— 与这个参数加进来之前逐字相同。
    const bool componentSyntax =
        !version.isEmpty()
        && GameVersion::availableIn(QStringLiteral("1.20.5"), version);
    if (componentSyntax) {
        QStringList patternEntries;
        for (const Layer &layer : layers) {
            patternEntries.append(QStringLiteral("{pattern:\"minecraft:%1\",color:\"%2\"}")
                                      .arg(patternAt(layer.patternIndex).id,
                                           colorId(layer.colorIndex)));
        }
        result.command = QStringLiteral("/give @p minecraft:%1_banner%2")
                             .arg(colorId(baseColorIndex),
                                  patternEntries.isEmpty()
                                      ? QString()
                                      : QStringLiteral("[minecraft:banner_patterns=[%1]]")
                                            .arg(patternEntries.join(QChar(','))));
    } else {
        QStringList patternTags;
        for (const Layer &layer : layers) {
            patternTags.append(QStringLiteral("{Pattern:\"%1\",Color:%2}")
                                   .arg(patternAt(layer.patternIndex).id)
                                   .arg(layer.colorIndex));
        }
        result.command = QStringLiteral("/give @p minecraft:%1_banner%2")
                             .arg(colorId(baseColorIndex),
                                  patternTags.isEmpty()
                                      ? QString()
                                      : QStringLiteral("{BlockEntityTag:{Patterns:[%1]}}")
                                            .arg(patternTags.join(QChar(','))));
    }

    result.summary = QStringLiteral("底色 %1 · %2/%3 层").arg(baseName).arg(layers.size()).arg(kMaxLayers);
    result.materials = materials.rows();
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
    if (!command.isEmpty()) {
        lines.append(QString());
        lines.append(QStringLiteral("指令（1.13–1.20.4 的 NBT 写法；1.20.5 起换成了组件写法，"
                                    "这一版不出）："));
        lines.append(command);
    }
    return lines.join(QChar('\n'));
}

}  // namespace Banner
