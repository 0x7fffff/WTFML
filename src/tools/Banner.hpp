/**
 * @file Banner.hpp
 * @brief 旗帜设计的判据层（coding plan §9.11 那一批里的「旗帜设计」）。
 *
 * 只做两件事：**一张图案表**（原版织布机能叠的那几十种图案、各自要什么材料）和
 * **一条叠层计划**（底色 + 若干层，回一段"先做什么、每层在织布机里放什么、一共要多少染料"）。
 * **不碰盘、不联网、不画图**。
 *
 * 【为什么这里不出一张"旗子长什么样"的预览】
 *
 * 预览要图案的遮罩贴图，而那些贴图是游戏自带的资源；计划 §9.11 明说**素材来源和分发许可
 * 没明确之前不伪造游戏内精确预览、也不复制官方贴图**。所以这一版只给**文字层序**：第几层
 * 是什么图案、什么颜色。用户拿去照着在织布机里点，一样能叠出来；以后要真预览，得先定
 * 贴图从哪儿来（自己按遮罩规则画，或者让用户指定游戏目录读本地的）。
 *
 * 【层的数与合法性】
 *
 * 原版织布机**最多叠 6 层**（第 6 层之后界面不再给图案）。这里按 6 卡死并报一句人话。
 * 同一图案可以叠两次（游戏允许），但**连续两层图案和颜色都一样**在画面上等于只叠了一层，
 * 所以那种情况给一条提醒（不是错误 —— 用户可能就是照别人的图叠的）。
 *
 * 【图案材料那两栏】
 *
 * 大多数图案的材料是「旗帜图案」这件物品本身（分图案种类）；少数几种在原版里用别的物品：
 * 苦力怕头颅、凋灵骷髅头颅、滨菊、附魔金苹果、橡树树苗、金胡萝卜、藤蔓、砖块。表里
 * `item` 那一栏就是这一步要摆进织布机的那件东西 —— **界面不该自己再猜一份**。
 */

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace Banner {

/// 原版织布机最多叠几层。
constexpr int kMaxLayers = 6;

/// 表里的一种图案。
struct PatternInfo {
    QString id;    ///< 图案 id（`stripe_bottom`；也用于 NBT 与指令）
    QString name;  ///< 中文名（`底横条`）
    QString item;  ///< 织布机里要摆的那件材料（`旗帜图案` / `苦力怕头颅` …）
    /// 这一种图案从哪一版起才有（`1.21`；表里绝大多数是 `1.14` 织布机那批，回 `1.14`）。
    QString since;
};

int patternCount();
/// 第 @p index 种（越界回空的那个 —— `id` 为空）。
PatternInfo patternAt(int index);
/// 全部图案名（界面下拉框用；次序就是表的次序）。
QStringList patternNames();
/// 按 id 找下标；认不出来回 -1。
int patternIndexById(const QString &id);
/// 这一种图案从哪一版起才有（`1.14` / `1.21`）。越界回空串。
QString patternSince(int index);

// ================= 十六色 =================
//
// 次序和游戏里染料/羊毛的次序一致（0 = 白 … 15 = 黑），**下标就是界面下拉框的下标**。
int colorCount();
/// 中文颜色名（`红色`）；越界回空串。
QString colorName(int index);
/// 颜色的 id（`red`；拼 `red_banner` / `red_dye` 用）。
QString colorId(int index);
/// 染料物品名（`红色染料`）。
QString dyeName(int index);

/// 一层：什么图案、什么颜色。
struct Layer {
    int patternIndex = 0;
    int colorIndex = 0;
};

/// 一条叠层计划。
struct Plan {
    bool ok = false;
    QString error;        ///< 非空 = 排不出来（层数超了 / 下标不对）
    QStringList steps;    ///< 一步一步（先做底旗，再一层一层叠）
    QString summary;      ///< 一句总述（`底色 红色 · 3 层 / 6`）
    QStringList materials;///< 材料清单（`红色羊毛 × 6`、`白色染料 × 2`…）
    QStringList notes;    ///< 要提醒的（连续两层一模一样、材料有特殊物品…）
    QString command;      ///< 1.13–1.20.4 的 `/give` NBT 写法（1.20.5+ 改组件，写法不同）
    /// 整份明细（总述 + 步骤 + 材料 + 指令）。界面上那个结果框直接摆它。
    QString detailText() const;
};

/**
 * @brief 算一条叠层计划。
 * @param baseColorIndex 底色（`colorName()` 的下标）。
 * @param layers 从下往上叠的层（第 0 项是先用织布机叠的那一层）。
 */
Plan plan(int baseColorIndex, const QList<Layer> &layers);

}  // namespace Banner
