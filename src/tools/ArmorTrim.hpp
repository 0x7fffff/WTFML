/**
 * @file ArmorTrim.hpp
 * @brief 纹饰搭配的判据层（工具箱里的「纹饰搭配」）。
 *
 * 只做一张表 + 一条计划：用户选"哪件盔甲、什么材质、哪个纹饰、用什么纹饰材料、做几件"，
 * 这里回一段"锻造台里怎么摆、每种东西要多少"。**不碰盘、不联网、不画图**。
 *
 * 【为什么这里不出预览图】
 *
 * 要求素材来源不明确时不伪造游戏内精确预览，也不直接复制官方贴图。
 * 这一层仍只给成品名与材料账。可选三维预览由独立 controller 在运行时读取用户本地
 * 或用户确认后从官方获取的客户端纹理；源码与发布包不携带官方纹理。
 *
 * 【一张模板打一件】
 *
 * 锻造台里**模板每用一次消耗一个**（不是永久解锁）。所以做整套（四件）要学会算成
 * "模板 × 4"。模板攒不够时可以复制：**1 个模板 + 7 颗钻石 + 1 块对应方块 → 2 个模板**。
 * 那块方块**每种图案不一样**，所以这里把 18 种各抄一份（游戏 `data/minecraft/recipe/`
 * 里那张复制配方的 `C` 位），结果里直接写"复制用圆石"这种话，不留"以游戏内为准"。
 *
 * 【海龟壳】
 *
 * 海龟壳只有头盔那一件，所以选了海龟壳又想做几件时直接报错（不悄悄改成皮革）。
 */

#pragma once

#include <QString>
#include <QStringList>

namespace ArmorTrim {

/// 十一种纹饰材料（名字照游戏 `zh_cn.json` 的 `trim_material.minecraft.*`）。
struct TrimMaterial {
    QString id;    ///< `amethyst`
    QString name;  ///< `紫水晶质`
    QString item;  ///< 锻造台里摆的那件（`紫水晶碎片`）
};

int trimMaterialCount();
/// 第 @p index 种（越界回空的那个）。
TrimMaterial trimMaterialAt(int index);
/// 这种材料哪个 Java 版本才有（`1.20` / `1.21.4`；越界回空串）。
QString trimMaterialSince(int index);
QStringList trimMaterialNames();

/// 纹饰图案（模板）。
struct TrimPattern {
    QString id;        ///< `coast`
    QString name;      ///< `海岸`
    QString source;    ///< 这枚模板从哪儿来（`沉船（藏宝箱）`）
    QString duplicate; ///< 复制这枚模板要的方块（`圆石`）
    QString since;     ///< 哪个 Java 版本开始有（`1.20` / `1.21`）
};

int patternCount();
/// 第 @p index 种（越界回空的那个）。
TrimPattern patternAt(int index);
QStringList patternNames();
/// 模板来源那一句（`沉船（藏宝箱）`；越界回空串）。界面别自己再写一份。
QString patternSource(int index);
/// 复制这枚模板要的那种方块（`圆石`；越界回空串）。抄的是游戏自己的复制配方。
QString patternDuplicate(int index);

// ================= 盔甲 =================

/// 四个部位（`头盔` / `胸甲` / `护腿` / `靴子`）。**下标就是界面上那个下拉框的下标**。
int partCount();
QString partName(int index);

/// 盔甲材质（`皮革` / `锁链` / `铁` / `金` / `钻石` / `下界合金` / `海龟壳`）。
int armorMaterialCount();
QString armorMaterialName(int index);
QString armorMaterialId(int index);
/// 一件盔甲的全名（`钻石胸甲`；海龟壳固定回 `海龟壳`）。
QString armorPieceName(int armorMaterial, int part);
/// 这个材质能不能做这个部位（海龟壳只认头盔）。
bool armorMaterialFitsPart(int armorMaterial, int part);

/// 一条计划。
struct Plan {
    bool ok = false;
    QString error;        ///< 非空 = 排不出来
    QStringList steps;    ///< 一步一步
    QString summary;      ///< 一句总述（`紫水晶质 · 海岸纹饰的钻石胸甲 × 1`）
    QStringList materials;///< 材料清单（`海岸纹饰模板 × 1`…）
    QStringList notes;    ///< 要提醒的（模板复制那一条、整套那一条）
    /// 整份明细（总述 + 步骤 + 材料 + 提醒）。界面上那个结果框直接摆它。
    QString detailText() const;
};

/**
 * @brief 算一条纹饰计划。
 * @param armorMaterial 盔甲材质（`armorMaterialName()` 的下标）。
 * @param part 部位（`partName()` 的下标；@p wholeSet 时忽略）。
 * @param patternIndex 纹饰图案（`patternAt()` 的下标）。
 * @param trimMaterial 纹饰材料（`trimMaterialAt()` 的下标）。
 * @param pieces 做几件（1–4；做整套就填 4）。
 * @param wholeSet true = 整套四件（头盔 / 胸甲 / 护腿 / 靴子各一件；@p pieces 忽略）。
 * 海龟壳没有整套，选它会报错（不悄悄只做头盔）。
 */
Plan plan(int armorMaterial, int part, int patternIndex, int trimMaterial, int pieces,
          bool wholeSet = false);

}  // namespace ArmorTrim
