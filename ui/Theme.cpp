/**
 * @file Theme.cpp
 * @brief Theme 的实现：预设 / 覆盖项怎么算成一个调色板，以及"地方"和"语义色"现在是什么颜色。
 * @author 0x7fffff
 * @date 2026-09-28
 */

#include "Theme.hpp"

#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QHash>
#include <QSettings>
#include <QStyle>
#include <QStyleHints>
#include <QWidget>

namespace Theme {
namespace {

// ---------------------------------------------------------------------------
// 那几张表：每一项都是"画面上哪儿"的唯一出处
// ---------------------------------------------------------------------------

/// 一块骨架今天的样子：`from` 那一档颜色压暗 `shade`，
///
///   - `shade == 0` ⇒ 直接用 `from` 那一档（卡片：今天就是 `Base`）；
///   - `literal` ⇒ 默认值是个写死的色（四周变暗那一层），不从 `from` 推；
///   - `accent` ⇒ 默认值是**主题色那一支**（`accentShade(AccentShade::Pure)`），
///     既不看 `from` 也不看 `shade`（只有标题栏是）。
struct PartInfo {
    const char *key;
    const char *name;
    int shade;
    QPalette::ColorRole from;
    bool literal;
    bool accent;
};

/// 顺序 = `Part` 的枚举顺序。`static_assert` 在下面盯着。
/// NOTE: 最后那一位（出厂字色是不是白墨）在 `partLightInk()` 里另有一张表 —— 它只有标题栏
///       用得到，塞进这一张只会让上面五行都跟着多一个恒假的字段。
const PartInfo kPartInfos[] = {
    // ★ 标题栏出厂就是**主题色那一支**（2026-09-28 用户原话：「使用 #56eb6c 作为默认主题
    //   颜色（顶栏用）」）。它原来和底栏一样是"窗口底色压暗一档"，两处**故意不一样了**：
    //   底下那条留着中性色 —— 它装的是启动/日志那一排控件，那些控件自己有自己的底色，
    //   而顶上那条整条就是一块主题色的底。ui_check W5 那两条照着这件事改过。
    {"titleBar", "标题栏", 105, QPalette::Window, false, true},
    {"bottomBar", "底栏", 105, QPalette::Window, false, false},
    {"panel", "浮层面板", 112, QPalette::Window, false, false},
    {"card", "卡片", 0, QPalette::Base, false, false},
    {"scrim", "四周变暗", 0, QPalette::Window, true, false},
    // 下面两块的 `from` / `shade` 只是"万一有人给它设了个底色"时的兜底算法（今天没有任何
    // 调用点取它们的底）—— 它们真正的底是 `QTabWidget` 自己画的那一层，**不在调色板里**
    // （见 `partHasBackground()` 那段）。加这两项是为了能单独铺一张图。没设图时它们一个
    // 像素都不改。
    {"homeLeft", "主页左栏", 0, QPalette::Window, false, false},
    {"pageBackground", "页面背景", 0, QPalette::Window, false, false},
};
static_assert(sizeof(kPartInfos) / sizeof(kPartInfos[0]) == static_cast<size_t>(partCount()),
              "kPartInfos 和 Part 的项数对不上 —— 加一块骨架时两处都要动");

/// 一张图的摆法表（顺序 = `ImageFit` 的枚举顺序）。
struct ImageFitInfo {
    const char *key;
    const char *name;
};

const ImageFitInfo kImageFitInfos[] = {
    {"cover", "铺满（裁剪）"},
    {"contain", "完整放下"},
    {"stretch", "拉伸铺满"},
    {"tile", "平铺"},
};
static_assert(sizeof(kImageFitInfos) / sizeof(kImageFitInfos[0]) ==
                  static_cast<size_t>(imageFitCount()),
              "kImageFitInfos 和 ImageFit 的项数对不上 —— 加一种摆法时两处都要动");

/// 四周变暗那一层的出厂色。**和 `OverlayHost::scrimColor()` 从前的字面量一字不差**：
/// 半透明黑，盖在什么底色上都成立。
const QColor &scrimLiteral() {
    static const QColor color(0, 0, 0, 96);
    return color;
}

/// 按钮身上那个"身份"属性名（`Theme::markButton()` 挂的、`widgetStyleSheet()` 里选择器
/// 认的就是它）。**只有这一处出处** —— 两边各写一遍字符串的话，改名字就成了"属性挂上了、
/// 样式一条都没匹配上"，而屏幕上看只是"这颗按钮颜色不对"，查起来要命。
constexpr const char kButtonKindProperty[] = "wtfmlButtonKind";

/// 把 @p color 按 @p percent 罩在白上（`percent` = 0 给全白、100 给 @p color 本色）。
QColor mixWithWhite(const QColor &color, int percent) {
    const int p = qBound(0, percent, 100);
    const auto mix = [p](int top) { return (255 * (100 - p) + top * p + 50) / 100; };
    return QColor(mix(color.red()), mix(color.green()), mix(color.blue()));
}

/// 能单独改的调色板角色表（顺序 = `Role` 的枚举顺序）。
struct RoleInfo {
    const char *key;
    const char *name;
    QPalette::ColorRole role;
};

const RoleInfo kRoleInfos[] = {
    {"window", "内容区底色", QPalette::Window},
    {"base", "输入框/列表底色", QPalette::Base},
    {"alternateBase", "隔行底色", QPalette::AlternateBase},
    {"text", "输入框/列表里的字", QPalette::Text},
    {"windowText", "正文", QPalette::WindowText},
    {"button", "按钮底色", QPalette::Button},
    {"buttonText", "按钮上的字", QPalette::ButtonText},
    {"highlight", "选中/悬停", QPalette::Highlight},
    {"highlightedText", "选中那一块上的字", QPalette::HighlightedText},
    {"placeholderText", "次要文字", QPalette::PlaceholderText},
    {"mid", "分隔线", QPalette::Mid},
};
static_assert(sizeof(kRoleInfos) / sizeof(kRoleInfos[0]) == static_cast<size_t>(roleCount()),
              "kRoleInfos 和 Role 的项数对不上 —— 加一个角色时两处都要动");

/// 文字不透明度落在哪几个角色上。**只有带字的角色**：`Highlight` 那种是"一块底"，
/// 跟着变淡的话选中那一块就淡了，那不是"文字透明度"。
///
/// NOTE: `ToolTipText` 也在这儿，它**不在** `kRoleInfos` 里（用户调不到它，但它是字，
///       全局那一位得管它，不然提示框里的字会比别处清楚）。
const QPalette::ColorRole kInkRoles[] = {
    QPalette::WindowText, QPalette::Text,        QPalette::ButtonText,
    QPalette::HighlightedText, QPalette::PlaceholderText, QPalette::ToolTipText,
};

QPalette::ColorGroup colorGroups[] = {QPalette::Active, QPalette::Inactive, QPalette::Disabled};

// ---------------------------------------------------------------------------
// 全局那一份
// ---------------------------------------------------------------------------

Spec &mutableSpec() {
    static Spec spec = defaultSpec();
    return spec;
}

/// 上一次 `apply()` 有没有往应用上调色板铺过东西（不是出厂那一份）。
///
/// 要它是因为"跟着系统"那一档得能**收回**：铺过深色预设之后用户又选回"跟着系统"，
/// 光"什么都不做"是收敛不回去的（深色那一份还挂在应用上）。
bool &installedExplicit() {
    static bool installed = false;
    return installed;
}

/// 我们**顶掉**的那一份调色板 = 平台自己给的那一份。
///
/// ★ 要它是因为"跟着系统"那一档得能**原样**收回来，而 `QStyle::standardPalette()` **不是**
///   平台那一份：真机实测（2026-09-28）平台给的是 `Window = #f0f0f0`，Fusion 的
///   `standardPalette()` 是 `#efefef` —— 于是"深色 → 跟着系统"之后整个窗口都差着一档。
///   `ui_shot` 的 30) 是拿"换主题之前那一张"逐像素比出来的：**还有 141 万个像素停在别的
///   颜色上**（那是 2026-09-28 修之前量的数），而界面上人眼是看不出来的 —— 典型的"改回来
///   了、但没改回原样"。所以这儿把顶掉的那一份存下来，还原时用它，不用算出来的那一份。
///
/// NOTE: Qt 没有"把调色板还回去"这个接口（`setPalette()` 是单向的），所以只能自己记。
/// NOTE: 记的是"还没顶掉过的时候应用身上挂着的那一份" —— 那一刻它就是平台给的
///       （`apply()` 每次都会顺手刷新，见那儿的注释）。
QPalette &platformPalette() {
    static QPalette palette;
    return palette;
}

bool &platformPaletteKnown() {
    static bool known = false;
    return known;
}

int clampOpacity(int value) {
    return qBound(0, value, 100);
}

/// 调色板里"正文/次要文字从哪儿取"那两档的当前值（`partForeground()` 的兜底用）。
QColor appColor(QPalette::ColorRole role) {
    if (QGuiApplication::instance() == nullptr) {
        return QColor();
    }
    return QGuiApplication::palette().color(role);
}

// ---------------------------------------------------------------------------
// 存盘的键
// ---------------------------------------------------------------------------

const char *const kPresetKey = "Theme/Preset";
const char *const kTextOpacityKey = "Theme/TextOpacity";
/// ★ **单数**：主题色（"这一套界面是什么颜色"）。底下那个复数前缀是**语义色**
/// （"红黄绿各是哪一个红黄绿"）。两个前缀只差一个 s，别看串了：往 `Theme/Accents/` 里写
/// 一个 "Accent" 是不生效的（`load()` 读的是这个单数键）。
const char *const kAccentKey = "Theme/Accent";
const char *const kRolesPrefix = "Theme/Roles/";
const char *const kPartsPrefix = "Theme/Parts/";
const char *const kAccentsPrefix = "Theme/Accents/";

/// 色存成 `#aarrggbb`（`QColor::HexArgb`）—— 注册表里能手看、能手改，和别的设置一个待遇。
QString colorToString(const QColor &color) {
    return color.isValid() ? color.name(QColor::HexArgb) : QString();
}

QColor colorFromString(const QString &text) {
    const QColor color = QColor::fromString(text);
    // 认不出的字符串（手改歪了）当"没设"：`fromString()` 对垃圾给一个无效色。
    return color.isValid() ? color : QColor();
}

QString roleSettingKey(Role role) {
    return QLatin1String(kRolesPrefix) + QLatin1String(kRoleInfos[static_cast<int>(role)].key);
}

QString partSettingKey(Part part, const char *field) {
    return QLatin1String(kPartsPrefix) + QLatin1String(kPartInfos[static_cast<int>(part)].key) +
           QLatin1Char('/') + QLatin1String(field);
}

QString accentSettingKey(const char *field) {
    return QLatin1String(kAccentsPrefix) + QLatin1String(field);
}

}  // namespace

// ---------------------------------------------------------------------------
// 名字 / 键
// ---------------------------------------------------------------------------

QString partKey(Part part) {
    const int index = static_cast<int>(part);
    return index >= 0 && index < partCount() ? QLatin1String(kPartInfos[index].key) : QString();
}

QString partName(Part part) {
    const int index = static_cast<int>(part);
    return index >= 0 && index < partCount() ? QString::fromUtf8(kPartInfos[index].name) : QString();
}

bool partFromKey(const QString &key, Part *out) {
    for (int i = 0; i < partCount(); ++i) {
        if (key == QLatin1String(kPartInfos[i].key)) {
            if (out != nullptr) {
                *out = static_cast<Part>(i);
            }
            return true;
        }
    }
    return false;
}

QString imageFitKey(ImageFit fit) {
    const int index = static_cast<int>(fit);
    return index >= 0 && index < imageFitCount() ? QLatin1String(kImageFitInfos[index].key)
                                                 : QString();
}

QString imageFitName(ImageFit fit) {
    const int index = static_cast<int>(fit);
    return index >= 0 && index < imageFitCount()
               ? QString::fromUtf8(kImageFitInfos[index].name)
               : QString();
}

bool imageFitFromKey(const QString &key, ImageFit *out) {
    for (int i = 0; i < imageFitCount(); ++i) {
        if (key == QLatin1String(kImageFitInfos[i].key)) {
            if (out != nullptr) {
                *out = static_cast<ImageFit>(i);
            }
            return true;
        }
    }
    return false;
}

QPalette::ColorRole qtRole(Role role) {
    const int index = static_cast<int>(role);
    return index >= 0 && index < roleCount() ? kRoleInfos[index].role : QPalette::WindowText;
}

QString roleKey(Role role) {
    const int index = static_cast<int>(role);
    return index >= 0 && index < roleCount() ? QLatin1String(kRoleInfos[index].key) : QString();
}

QString roleName(Role role) {
    const int index = static_cast<int>(role);
    return index >= 0 && index < roleCount() ? QString::fromUtf8(kRoleInfos[index].name) : QString();
}

bool roleFromKey(const QString &key, Role *out) {
    for (int i = 0; i < roleCount(); ++i) {
        if (key == QLatin1String(kRoleInfos[i].key)) {
            if (out != nullptr) {
                *out = static_cast<Role>(i);
            }
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// 主题色那一支
// ---------------------------------------------------------------------------

QColor defaultAccentColor() {
    // ★ 用户 2026-09-28 点名的那一支：「使用 #56eb6c 作为默认主题颜色（顶栏用）」。
    //   全仓只有这一处写这六个十六进制位 —— 顶栏、按钮的字与边、当前那一颗导航底下那条
    //   线，量的都是它算出来的某一档（见 `accentShade()`），**不许在别处再写一遍**。
    return QColor(0x56, 0xeb, 0x6c);
}

QColor accentShade(AccentShade shade) {
    // 当前那一份里的主题色；认不出（盘上手改歪了）就退回出厂那一支。
    QColor pure = current().accent;
    if (!pure.isValid()) {
        pure = defaultAccentColor();
    }
    switch (shade) {
    case AccentShade::Pure:
        return pure;
    case AccentShade::Ink:
        // 白底上写字的那个绿：#56eb6c 的 **value 折半**（`darker(200)`，Qt 走的是 HSV）⇒
        // #2b7536，对白底 **5.7:1**。用户点名那一支压在白底上只有 1.55:1 —— 当边框都嫌淡，
        // 当字就是"看得见、读不清"。所以字这一档必须更深：对比度是这一档存在的全部理由。
        return pure.darker(200);
    case AccentShade::Border:
        // 那一圈边：#56eb6c 的 value 压到 1/1.5 ⇒ #39a949，对白底 **3.5:1**
        // （WCAG 对"界面部件"那条线是 3:1 —— 边框要看得见，不必像字那么重）。
        //
        // ★ 为什么不是 1/1.4：那一档实测只有 **3.04:1** —— 恰好在线上。线的两侧只差
        //   Qt 给 `darker()` 取整时进的那一位（`v*100/factor` 是整数除法，144 和 145 会
        //   差出 0.03），等于把"够不够 3:1"交给一个取整细节去掷骰子。压到 1/1.5 之后
        //   实测 3.45~3.49，留得下一档误差。
        return pure.darker(150);
    case AccentShade::Hover:
    case AccentShade::Pressed:
        // 悬停 / 按下那两层底：**主题色罩在白上**（12% / 28%），不是把主题色调亮或调暗 ——
        // 调亮方向会撞到"白底"、调暗方向就成了另一种颜色，而这两层底上写的还是那一档深绿
        // （`Ink`），底色必须一直待在很浅那一头，那行字才一直读得清。
        return mixWithWhite(pure, shade == AccentShade::Hover ? 12 : 28);
    default:
        break;
    }
    return pure;
}

// ---------------------------------------------------------------------------
// 按钮那一套外观
// ---------------------------------------------------------------------------

QColor neutralInkColor() {
    // 「一般性」那一档的黑。**唯一出处**：次要按钮（Neutral）的字和边、未勾选的勾选框
    // 那个框和那行字，量的都是它。不用纯黑（`#000000`）：纯黑压在纯白上是 21:1，
    // 比正文（`#000000`）还重 —— 一颗"取消"不该比它旁边那颗"确定"更抢眼。
    return QColor(0x20, 0x20, 0x20);
}

QString buttonKindKey(ButtonKind kind) {
    switch (kind) {
    case ButtonKind::Neutral:
        return QStringLiteral("neutral");
    case ButtonKind::Danger:
        return QStringLiteral("danger");
    case ButtonKind::Accent:
        break;
    }
    // 普通按钮**不挂属性**（挂一个 "accent" 只是让构造出来的控件都带一位恒真的值）。
    return QString();
}

void markButton(QWidget *button, ButtonKind kind) {
    if (button == nullptr) {
        return;
    }
    const QString key = buttonKindKey(kind);
    if (key.isEmpty()) {
        button->setProperty(kButtonKindProperty, QVariant());
    } else {
        button->setProperty(kButtonKindProperty, key);
    }
}

QString widgetStyleSheet() {
    const QColor ink = accentShade(AccentShade::Ink);
    const QColor border = accentShade(AccentShade::Border);
    const QColor pure = accentShade(AccentShade::Pure);
    const QColor hover = accentShade(AccentShade::Hover);
    const QColor pressed = accentShade(AccentShade::Pressed);
    // 次要 / 危险那两档的色。红**故意不取 `dangerColor()`**：那一支是"按深浅现推"的
    // （深底上用浅红），而按钮的底一直是白的 —— 白底上用 `dangerColor()` 在深色预设下会
    // 拿到那个浅红（对白底 2.3:1，读不清）。所以这一档直接钉死那个对白底 4.8:1 的深红，
    // 它和 `errorTextColor()` 浅底那一档是同一个。
    const QColor red(0xc0, 0x28, 0x28);
    const QColor black = neutralInkColor();
    const QColor disabledInk(0x9a, 0x9a, 0x9a);
    const QColor disabledBorder(0xd2, 0xd2, 0xd2);
    const QColor disabledFill(0xf7, 0xf7, 0xf7);

    // ★ `padding: 6px` 不是随手写的：Fusion 的 `PM_ButtonMargin` 就是 6，边框 1 ⇒
    //   2*(6+1) = 14，和样式自己算的**一模一样**。差一点点的话界面上是看不出来，但一堆
    //   "这一行放得下几颗按钮"的断言会整片地漂。
    QString qss = QStringLiteral(
                      "QPushButton {"
                      " background-color: #ffffff;"
                      " color: %1;"
                      " border: 1px solid %2;"
                      " border-radius: 4px;"
                      " padding: 6px; }"
                      // 默认那一颗（对话框里回车就按下去的那颗）：淡淡一层主题色的底。
                      // **不能跟悬停同色** —— 同色的话"鼠标停在哪儿"就看不出默认是哪一颗了。
                      "QPushButton:default:enabled { background-color: %3; }"
                      "QPushButton:hover:enabled { background-color: %4; }"
                      "QPushButton:pressed:enabled { background-color: %5; }"
                      "QPushButton:disabled { background-color: %6; color: %7;"
                      " border-color: %8; }")
                      .arg(ink.name(), border.name(), mixWithWhite(accentShade(AccentShade::Pure), 6).name(),
                           hover.name(), pressed.name(), disabledFill.name(), disabledInk.name(),
                           disabledBorder.name());
    // 危险 / 次要两档：只换字和边那两处颜色，其余（白底、圆角、内边距）照旧。
    const auto kindBlock = [](const QString &key, const QColor &color, const QColor &hoverFill,
                              const QColor &pressedFill) {
        return QStringLiteral("QPushButton[%1=\"%2\"] { color: %3; border-color: %3; }"
                              "QPushButton[%1=\"%2\"]:hover:enabled { background-color: %4; }"
                              "QPushButton[%1=\"%2\"]:pressed:enabled { background-color: %5; }")
            .arg(QString::fromLatin1(kButtonKindProperty), key, color.name(), hoverFill.name(),
                 pressedFill.name());
    };
    qss += kindBlock(QStringLiteral("danger"), red, QColor(0xfd, 0xea, 0xea), QColor(0xf7, 0xd5, 0xd5));
    qss += kindBlock(QStringLiteral("neutral"), black, QColor(0xf0, 0xf0, 0xf0),
                     QColor(0xe0, 0xe0, 0xe0));

    // -----------------------------------------------------------------------
    // 输入框（2026-09-28）
    // -----------------------------------------------------------------------
    //
    // 用户原话：「输入文本的搜索框的框框跟随主题色，文本保留黑色」。所以**边**走主题色
    // （`Border` 那一档，对白底 3.5:1 —— 边框要看得见，不必像字那么重），**底一律白、
    // 字一律黑**（不去跟深浅：这一套界面上的按钮和输入框都是"白纸"，跟了深浅反而会在
    // 深色系统主题下出现白底黑字旁边一堆深灰控件）。
    //
    // 聚焦那一档换成 `Ink`（5.6:1）：边框从"看得见"变成"这一格是活的" —— 只靠加粗或者
    // 换灰是说不清"哪一格有光标"的，而换一档更深的同色系一眼就是它。
    qss += QStringLiteral(
               "QLineEdit { background-color: #ffffff; color: #000000; border: 1px solid %1;"
               " border-radius: 3px; padding: 1px 3px; }"
               "QLineEdit:focus { border-color: %2; }"
               "QLineEdit:disabled { background-color: %3; color: %4; border-color: %5; }")
               .arg(border.name(), ink.name(), disabledFill.name(), disabledInk.name(),
                    disabledBorder.name());
    // ★ **数字框 / 下拉框里面那一格也是 `QLineEdit`** —— 上面那条会连它一起框上，画出来
    //   就是"框里还套着一个框"（自旋框自己已经有一圈边了）。这一条把它按回去。
    //   用**后代**选择器（Qt 的 `>` 子选择器不可靠，后代选择器一定生效）。
    qss += QStringLiteral("QAbstractSpinBox QLineEdit, QComboBox QLineEdit {"
                          " border: none; background: transparent; padding: 0; }");

    // -----------------------------------------------------------------------
    // 勾选框（2026-09-28）
    // -----------------------------------------------------------------------
    //
    // 用户原话：「勾选框未勾选的时候框和文本做成黑色，勾选了两者都变成主题色」。
    // 所以这里**成对**地换色：未勾选 = 黑框 + 黑字（`neutralInkColor()`）；勾上 = 主题色
    // 的**实心**框 + 主题色的字（用 `Ink` 那一档写字，5.6:1，`Pure` 只有 1.56:1）。
    //
    // ★ 勾上那一格画的是**实心块**、不是"白底 + 一个勾"：样式表画勾要一张图，而图是死的
    //   —— 主题色是能换的（`Theme/Accent`），贴一张绿勾上去等于把"换一个主题色"这件事
    //   在勾选框这一格上弄坏。实心 vs 空心是**不依赖任何图**就能分辨的一对状态，而且
    //   "框变成主题色"正是用户点名要的那个效果。
    qss += QStringLiteral(
               "QCheckBox { color: %1; }"
               "QCheckBox:checked { color: %2; }"
               "QCheckBox:disabled { color: %3; }"
               "QCheckBox::indicator { background-color: #ffffff; border: 1px solid %1;"
               " border-radius: 2px; }"
               "QCheckBox::indicator:checked { background-color: %4; border-color: %2; }"
               "QCheckBox::indicator:disabled { background-color: %5; border-color: %3; }")
               .arg(black.name(), ink.name(), disabledInk.name(), pure.name(),
                    disabledFill.name());
    return qss;
}

// ---------------------------------------------------------------------------
// 预设
// ---------------------------------------------------------------------------

QStringList presetIds() {
    return {QStringLiteral("system"), QStringLiteral("light"), QStringLiteral("dark")};
}

QString presetName(const QString &id) {
    if (id == QLatin1String("light")) {
        return QStringLiteral("浅色");
    }
    if (id == QLatin1String("dark")) {
        return QStringLiteral("深色");
    }
    return QStringLiteral("跟着系统");
}

bool isPresetId(const QString &id) {
    return presetIds().contains(id);
}

Spec presetSpec(const QString &id) {
    Spec spec;
    spec.presetId = isPresetId(id) ? id : defaultSpec().presetId;
    return spec;
}

Spec defaultSpec() {
    // 出厂那一份**故意什么都没有**：跟系统、不透明度 100、一项覆盖都没有 ⇒ `apply()`
    // 一个字节都不碰（见文件头那段"默认那一份 = 今天的样子"）。
    // NOTE: 主题色是唯一一位"出厂就有值"的 —— 它不是覆盖项（不进 `roles` / `parts`），
    //       落在顶栏那一块的出厂色和按钮的样式表上，所以"不碰调色板"这条照旧成立。
    Spec spec;
    spec.presetId = QStringLiteral("system");
    spec.accent = defaultAccentColor();
    return spec;
}

// ---------------------------------------------------------------------------
// 当前这一份
// ---------------------------------------------------------------------------

Spec normalize(const Spec &spec) {
    Spec clean;
    clean.presetId = isPresetId(spec.presetId) ? spec.presetId : defaultSpec().presetId;
    clean.textOpacity = clampOpacity(spec.textOpacity);
    // 主题色：认不得（盘上手改歪了 / 没写过）一律退回出厂那一支 —— 它没有"空"这个状态，
    // 界面上随时都有顶栏那一块要画。
    clean.accent = spec.accent.isValid() ? spec.accent : defaultAccentColor();
    for (auto it = spec.roles.constBegin(); it != spec.roles.constEnd(); ++it) {
        // 越界的下标（盘上手改出来的）直接丢：认不出的角色没有"画面上是哪儿"可言。
        if (it.key() < 0 || it.key() >= roleCount() || !it.value().isValid()) {
            continue;
        }
        clean.roles.insert(it.key(), it.value());
    }
    for (auto it = spec.parts.constBegin(); it != spec.parts.constEnd(); ++it) {
        if (it.key() < 0 || it.key() >= partCount()) {
            continue;
        }
        PartStyle style = it.value();
        style.hasBackground = style.hasBackground && style.background.isValid();
        style.hasForeground = style.hasForeground && style.foreground.isValid();
        style.textOpacity = style.textOpacity < 0 ? -1 : clampOpacity(style.textOpacity);
        // 图这一位：**路径**是唯一说得准"有没有"的东西（`hasImage` 由它推出来，不是另立
        // 一位让两边对不上 —— 空字符串天然就是"没设过"）。
        // 摆法认不出的（盘上手改歪了）退回默认那一种，不整条丢掉 —— 图还在，只是摆法回到
        // 「铺满」；为了一个摆法把用户挑的图整个忽略掉，那是把"读不懂"变成了"没设过"。
        style.imagePath = style.imagePath.trimmed();
        style.hasImage = !style.imagePath.isEmpty();
        if (style.hasImage) {
            if (static_cast<int>(style.fit) < 0 || static_cast<int>(style.fit) >= imageFitCount()) {
                style.fit = ImageFit::Cover;
            }
            style.imageOpacity = clampOpacity(style.imageOpacity);
            style.imageName = style.imageName.trimmed();
        } else {
            style.imagePath.clear();
            // 没图了，"当初挑的那张叫什么"也就没有意义了（留着的话下次读回来这一块会
            // 带着一个孤儿名字）。
            style.imageName.clear();
        }
        // 一项覆盖都没有的那一块**不留**：留着的话"这一块设过"这件事在盘上是假的
        // （读回来之后 `parts.contains()` 说它有，而它什么都没改）。
        if (!style.hasBackground && !style.hasForeground && style.textOpacity < 0 &&
            !style.hasImage) {
            continue;
        }
        clean.parts.insert(it.key(), style);
    }
    clean.danger = spec.danger.isValid() ? spec.danger : QColor();
    clean.warning = spec.warning.isValid() ? spec.warning : QColor();
    clean.ok = spec.ok.isValid() ? spec.ok : QColor();
    clean.errorText = spec.errorText.isValid() ? spec.errorText : QColor();
    return clean;
}

const Spec &current() {
    return mutableSpec();
}

void setSpec(const Spec &spec) {
    mutableSpec() = normalize(spec);
}

// ---------------------------------------------------------------------------
// 铺开
// ---------------------------------------------------------------------------

bool isDarkPalette(const QPalette &palette) {
    // 判据取的是**窗口底色的明度**（不是某个"主题叫什么"的标志位）：深底用浅红、
    // 浅底用深红，两种主题下都是"一眼看出是红字"而不是"一团糊在底色里"。
    return palette.color(QPalette::Window).lightness() < 128;
}

bool isDark() {
    return isDarkPalette(QGuiApplication::palette());
}

QPalette buildPalette(const Spec &spec, const QPalette &base) {
    QPalette palette = base;
    for (auto it = spec.roles.constBegin(); it != spec.roles.constEnd(); ++it) {
        if (it.key() < 0 || it.key() >= roleCount() || !it.value().isValid()) {
            continue;
        }
        palette.setColor(qtRole(static_cast<Role>(it.key())), it.value());
    }
    if (spec.textOpacity != 100) {
        for (QPalette::ColorRole role : kInkRoles) {
            for (QPalette::ColorGroup group : colorGroups) {
                QColor color = palette.color(group, role);
                if (!color.isValid()) {
                    continue;
                }
                // **乘**上去而不是盖掉：`PlaceholderText` 本来就带一点透明度，
                // 直接赋值等于把它从半透明变成"和正文一样清楚"。
                color.setAlpha(color.alpha() * spec.textOpacity / 100);
                palette.setColor(group, role, color);
            }
        }
    }
    return palette;
}

void notifyWidgets() {
    if (QGuiApplication::instance() == nullptr) {
        return;
    }
    const QList<QWidget *> widgets = QApplication::allWidgets();
    for (QWidget *widget : widgets) {
        if (widget == nullptr) {
            continue;
        }
        QEvent event(QEvent::PaletteChange);
        QCoreApplication::sendEvent(widget, &event);
    }
}

/// 把那一套控件外观（按钮 / 输入框 / 勾选框）挂到应用上。`force` = 样式表字符串没变也重挂一遍。
///
/// ★ 为什么换过调色板之后**必须**重挂（2026-09-28 实测，ui_shot 那条"深色换回来"揪出来的）：
///   应用身上挂着样式表的时候，`QApplication::setPalette()` 那套"发给每个控件"的传播会
///   **漏掉一部分控件** —— 最明显的是 `QCheckBox`：从深色换回"跟着系统"之后，它记着的还是
///   深色那一份调色板（白字 + 深色勾选框），在浅底上那行字就**看不见了**。
///   实测（本机，设置页那一刻）：挂着样式表走一遍深浅来回，10 个勾选框 **10 个**残留；
///   把同样那一份样式表**再挂一遍**（= 逼所有控件重新抛光一遍），残留 **0** 个；
///   撤掉样式表再走，也 **0** 个 —— 所以这一位是样式表带来的，不是调色板本身。
///   NOTE: 重挂只在"这一趟真换了调色板"时做（`apply()` 会被"换个背景图"这种小事调起来，
///         每趟都重挂一遍全窗口就是白跑 —— 见上面那条 NOTE）。
void installButtonSheet(QApplication &app, bool force) {
    const QString buttons = widgetStyleSheet();
    static QString installed;
    if (!force && installed == buttons) {
        return;
    }
    installed = buttons;
    app.setStyleSheet(buttons);
}

void apply(QApplication &app) {
    const Spec &spec = current();
    const bool followSystem = spec.presetId == QLatin1String("system");

    // -1) 按钮那一套外观（白底 + 主题色的字和边）：走**应用级样式表**，不走调色板。
    //
    //     ★ 为什么按钮这一层非走样式表不可：用户要的是"按钮中心白、字和边是主题色"，
    //       而 Fusion 画的按钮**边是它自己从按钮底色推出来的**（调色板里没有"这一圈边是
    //       什么色"这个位置），把 `Role::Button` 改成白只会得到"白底 + 一圈灰边"。
    //     ★ 为什么不能顺手把主题色塞进调色板（比如 `Role::Highlight` 也换成这一支绿）：
    //       出厂那一份一旦带上角色覆盖，`touchesPalette` 就为真 ⇒ 每一趟都 `setPalette()`
    //       ⇒ 应用调色板成了"我们设的"，**Qt 从此不再跟着系统深浅走了**（跟着系统那一档的
    //       全部意义，见文件头那段）。样式表不碰调色板，所以这条路是干净的。
    //     NOTE: 只在**真变了**的时候铺：`apply()` 会被"改一张背景图"这种小事调起来，
    //           每次都铺一遍等于让整个界面重新 polish 一趟（样式没变也白跑）。
    installButtonSheet(app, false);

    // 0) 记下"平台自己那一份"。`installedExplicit()` 为假 = 应用身上现在挂的就是平台给的
    //    那一份（我们没顶掉过，或者已经还回去了）—— 这一刻存下来，还原时才有东西可还。
    //    顶掉过的那一档不刷新：那时候应用身上挂着的是**我们**铺的，存它等于把深色当成了
    //    "平台那一份"（见 `platformPalette()` 那段）。
    if (!installedExplicit()) {
        platformPalette() = app.palette();
        platformPaletteKnown() = true;
    }

    // 1) 深浅那一档。**只有"跟着系统"那一档才不去动它**（见头文件那段 NOTE）。
    if (followSystem) {
        app.styleHints()->unsetColorScheme();
    } else {
        app.styleHints()->setColorScheme(spec.presetId == QLatin1String("dark")
                                             ? Qt::ColorScheme::Dark
                                             : Qt::ColorScheme::Light);
    }

    // 2) 基色一律现问样式要。**不能拿"现在的应用调色板"当基色**：铺过一次深色预设之后
    //    它就是深色那一份了，再叠一次"跟系统"的覆盖项会得到深底 + 浅色覆盖的怪东西。
    //    这一刻深浅那一档已经定好，所以 standardPalette() 问到的就是那一档。
    const QPalette base = app.style()->standardPalette();

    const bool touchesPalette = !followSystem || !spec.roles.isEmpty() || spec.textOpacity != 100;
    if (!touchesPalette) {
        // 出厂那一份：今天的样子。**一个字节都不碰** —— 碰了就等于把"跟着系统"钉死
        // （Qt 只在"调色板不是我们设的"时才跟着系统深浅走）。
        if (installedExplicit()) {
            // 例外：上一次铺过别的预设，这一次要选回"跟着系统" ⇒ 得把出厂那一份盖回去，
            // 光"什么都不做"是收不回的（深色那份还挂在应用上）。
            // ★ 还回去的是**记下来的平台那一份**，不是上面算出来的 `base`：`base` 是
            //   `standardPalette()`，和平台那一份差着一档（见 `platformPalette()` 那段）。
            //   没记下来过（理论上到不了）才退而求其次用 `base`。
            app.setPalette(platformPaletteKnown() ? platformPalette() : base);
            installedExplicit() = false;
            // ★ 换过调色板就得把样式表重挂一遍，见 `installButtonSheet()` 那段。
            installButtonSheet(app, true);
        }
        notifyWidgets();
        return;
    }

    const QPalette before = app.palette();
    app.setPalette(buildPalette(spec, base));
    installedExplicit() = true;
    if (app.palette() != before) {
        // ★ 同上：这一趟**真**把调色板换掉了，所以重挂一遍样式表把控件那层缓存冲掉。
        //   没换（比如只是改了背景图）就不挂 —— 那正是上面 NOTE 说的"白跑一趟"。
        installButtonSheet(app, true);
    }
    // 铺完之后 Qt 自己会把"调色板变了"发给每一个控件（`ui_check` W2 量着这件事），
    // 所以这里**不再**敲第二遍 —— 敲了就是每换一次主题白跑一趟全窗口的重算。
}

// ---------------------------------------------------------------------------
// 每一块 / 每一个语义色
// ---------------------------------------------------------------------------

QColor partBackground(Part part) {
    const int index = static_cast<int>(part);
    if (index < 0 || index >= partCount()) {
        return QColor();
    }
    const auto it = current().parts.constFind(index);
    if (it != current().parts.constEnd() && it->hasBackground) {
        return it->background;
    }
    const PartInfo &info = kPartInfos[index];
    if (info.accent) {
        // 顶栏：主题色那一支（用户点名要的）。用户单独给这一块设过底色的话上面那条已经
        // 返回了 —— 覆盖永远赢过出厂值。
        return accentShade(AccentShade::Pure);
    }
    if (info.literal) {
        return scrimLiteral();
    }
    const QColor from = appColor(info.from);
    return info.shade == 0 ? from : from.darker(info.shade);
}

bool partHasBackground(Part part) {
    const int index = static_cast<int>(part);
    if (index < 0 || index >= partCount()) {
        return false;
    }
    const auto it = current().parts.constFind(index);
    return it != current().parts.constEnd() && it->hasBackground;
}

bool partLightInk(Part part) {
    const int index = static_cast<int>(part);
    // ★ 白墨这一位跟着"顶栏底色 = 主题色那一支"走：绿条上的字必须够白才读得出来
    //   （用户 2026-09-28：「顺便那个最小化和 x 都换成白的」）。它是**出厂值**，
    //   用户单独给这一块设过前景色的话那个覆盖照样赢（`partForeground()` 里那条在前）。
    return index >= 0 && index < partCount() && kPartInfos[index].accent;
}

QColor partForeground(Part part, const QColor &fallback) {
    const int index = static_cast<int>(part);
    if (index < 0 || index >= partCount()) {
        return fallback;
    }
    // 没人给这一块设过前景色时，出厂字色就是这一位上写着的那一支（顶栏是**白墨**，
    // 别的块才是调用方递进来的那个兜底色）。
    const QColor factoryInk = kPartInfos[index].accent ? QColor(Qt::white) : fallback;
    const auto it = current().parts.constFind(index);
    if (it == current().parts.constEnd()) {
        return factoryInk;
    }
    QColor color = it->hasForeground ? it->foreground : factoryInk;
    // 这一块**自己**那一位才在这儿生效；全局那一位落在调色板的文字角色上（`buildPalette()`），
    // 而 `fallback` 多半就是从调色板取来的 ⇒ 在这儿再乘一次就是乘了两遍。
    if (it->textOpacity >= 0 && color.isValid()) {
        color.setAlpha(color.alpha() * it->textOpacity / 100);
    }
    return color;
}

PartImage partImage(Part part) {
    PartImage image;
    const int index = static_cast<int>(part);
    if (index < 0 || index >= partCount()) {
        return image;
    }
    const auto it = current().parts.constFind(index);
    if (it == current().parts.constEnd() || !it->hasImage) {
        return image;
    }
    image.path = it->imagePath;
    image.name = it->imageName;
    image.fit = it->fit;
    image.opacity = clampOpacity(it->imageOpacity);
    return image;
}

QColor scrimColor() {
    return partBackground(Part::Scrim);
}

namespace {

/// 语义色的出厂值：一深一浅两档。**参数是"算不算深色"**，不是调色板 —— 三个语义色
/// 都只用得到这一位。
QColor dangerFor(bool dark) {
    return dark ? QColor(0xff, 0x6b, 0x6b) : QColor(0xc0, 0x28, 0x28);
}

}  // namespace

QColor dangerColor(const QPalette &palette) {
    const QColor override = current().danger;
    if (override.isValid()) {
        return override;
    }
    return dangerFor(isDarkPalette(palette));
}

QColor warningColor(const QPalette &palette) {
    const QColor override = current().warning;
    if (override.isValid()) {
        return override;
    }
    // 黄这一档取的是**偏金黄**的琥珀色（不是纯黄 #ffff00）：那块方块的底色是底栏，
    // 纯黄在浅色底上几乎看不见。色相实测 43(浅)/45(深)，和红那一档（0）差得开。
    return isDarkPalette(palette) ? QColor(0xff, 0xd2, 0x4d) : QColor(0xe0, 0xa0, 0x00);
}

QColor okColor(const QPalette &palette) {
    const QColor override = current().ok;
    if (override.isValid()) {
        return override;
    }
    return isDarkPalette(palette) ? QColor(0x5e, 0xd1, 0x7a) : QColor(0x1f, 0x9d, 0x4d);
}

QColor errorTextColor(const QPalette &palette) {
    const QColor override = current().errorText;
    if (override.isValid()) {
        return override;
    }
    // 浅底那一档和原来那个写死的 #c0392b **一字不差**（对浅色主题的窗口底色 #f0f0f0
    // 实测 4.77:1，够 WCAG 正文那条 4.5:1）。深底那一档没法照搬：同一个 #c0392b 落到
    // 深色主题的底上只有 2.26:1 —— 名字写错了却看不出底下那行写了什么，等于没有。
    // 所以借用同一个界面里已经调好的那个浅红（`dangerColor()`）。
    // NOTE: 两处红**故意**不合并成一个语义色：浅底那一档 #c0392b 和 #c02828 各有各的
    //       对比度来历（见 `MainWindowUI::installNameErrorColor()` 从前那段注释）。
    return isDarkPalette(palette) ? dangerFor(true) : QColor(0xc0, 0x39, 0x2b);
}

// ---------------------------------------------------------------------------
// 落盘
// ---------------------------------------------------------------------------

void save(const Spec &spec) {
    const Spec clean = normalize(spec);
    QSettings settings;
    // 先整组清掉再写：上一次设过、这一次没设的那几项要是留着，读回来就会"复活"
    // （用户把标题栏改回默认了，重开一次又变回去）。
    settings.remove(QStringLiteral("Theme"));
    settings.setValue(QLatin1String(kPresetKey), clean.presetId);
    settings.setValue(QLatin1String(kTextOpacityKey), clean.textOpacity);
    settings.setValue(QLatin1String(kAccentKey), colorToString(clean.accent));
    for (auto it = clean.roles.constBegin(); it != clean.roles.constEnd(); ++it) {
        settings.setValue(roleSettingKey(static_cast<Role>(it.key())), colorToString(it.value()));
    }
    for (auto it = clean.parts.constBegin(); it != clean.parts.constEnd(); ++it) {
        const PartStyle &style = it.value();
        const Part part = static_cast<Part>(it.key());
        if (style.hasBackground) {
            settings.setValue(partSettingKey(part, "Background"), colorToString(style.background));
        }
        if (style.hasForeground) {
            settings.setValue(partSettingKey(part, "Foreground"), colorToString(style.foreground));
        }
        if (style.textOpacity >= 0) {
            settings.setValue(partSettingKey(part, "TextOpacity"), style.textOpacity);
        }
        if (style.hasImage) {
            // 路径按 `QDir::toNativeSeparators` 写（注册表里能手看）—— 读回来再统一成
            // `/`，见 `load()`。这一来一回是**故意的**：注册表里那串得是 `\`，用户在
            // regedit 里一眼认得出那是自己盘上的哪一条；而这一层内部一律用 `/`。
            settings.setValue(partSettingKey(part, "Image"),
                              QDir::toNativeSeparators(style.imagePath));
            settings.setValue(partSettingKey(part, "ImageFit"), imageFitKey(style.fit));
            settings.setValue(partSettingKey(part, "ImageOpacity"), style.imageOpacity);
            // 用户挑的那张**原来叫什么**：设置页那一行「现在铺的是：…」写的是它，不是我们
            // 存进来那个槽位名（见 `PartStyle::imageName`）。空的不写 —— 读回来是空的就
            // 退回落盘那个文件的名字。
            if (!style.imageName.isEmpty()) {
                settings.setValue(partSettingKey(part, "ImageName"), style.imageName);
            }
        }
    }
    if (clean.danger.isValid()) {
        settings.setValue(accentSettingKey("Danger"), colorToString(clean.danger));
    }
    if (clean.warning.isValid()) {
        settings.setValue(accentSettingKey("Warning"), colorToString(clean.warning));
    }
    if (clean.ok.isValid()) {
        settings.setValue(accentSettingKey("Ok"), colorToString(clean.ok));
    }
    if (clean.errorText.isValid()) {
        settings.setValue(accentSettingKey("ErrorText"), colorToString(clean.errorText));
    }
    settings.sync();
}

Spec load() {
    QSettings settings;
    Spec spec = defaultSpec();
    spec.presetId = settings.value(QLatin1String(kPresetKey), spec.presetId).toString();
    spec.textOpacity = settings.value(QLatin1String(kTextOpacityKey), spec.textOpacity).toInt();
    // 主题色：没写过（老版本存的那一份）就是出厂那一支 —— 它没有"空"这个状态。
    const QColor accent = colorFromString(settings.value(QLatin1String(kAccentKey)).toString());
    if (accent.isValid()) {
        spec.accent = accent;
    }
    for (int i = 0; i < roleCount(); ++i) {
        const Role role = static_cast<Role>(i);
        const QColor color = colorFromString(
            settings.value(roleSettingKey(role)).toString());
        if (color.isValid()) {
            spec.roles.insert(i, color);
        }
    }
    for (int i = 0; i < partCount(); ++i) {
        const Part part = static_cast<Part>(i);
        PartStyle style;
        style.background = colorFromString(
            settings.value(partSettingKey(part, "Background")).toString());
        style.hasBackground = style.background.isValid();
        style.foreground = colorFromString(
            settings.value(partSettingKey(part, "Foreground")).toString());
        style.hasForeground = style.foreground.isValid();
        const QVariant opacity = settings.value(partSettingKey(part, "TextOpacity"));
        if (opacity.isValid()) {
            style.textOpacity = opacity.toInt();
        }
        // 图：路径统一成 `/`（写盘时是 `\`，见 `save()`）。**不认识就不留下这一条**
        // （下一句由 `normalize()` 收尾：空的路径 = 没设过）。
        style.imagePath = QDir::fromNativeSeparators(
            settings.value(partSettingKey(part, "Image")).toString());
        style.hasImage = !style.imagePath.isEmpty();
        if (style.hasImage) {
            ImageFit fit = ImageFit::Cover;
            imageFitFromKey(settings.value(partSettingKey(part, "ImageFit")).toString(), &fit);
            style.fit = fit;
            const QVariant imageOpacity = settings.value(partSettingKey(part, "ImageOpacity"));
            // 没写过这一项（手改注册表删掉了）⇒ 默认 100，不是 0 —— 0 是一张看不见的图，
            // 那个"空白"会被当成坏了。
            style.imageOpacity = imageOpacity.isValid() ? imageOpacity.toInt() : 100;
            // 「当初挑的那张叫什么」：没有这一项（老版本存的、或者手改注册表删了）就留空，
            // 界面那一侧会退回落盘那个文件的名字 —— 不许因为少一位就写不出话。
            style.imageName = settings.value(partSettingKey(part, "ImageName")).toString();
        }
        if (style.hasBackground || style.hasForeground || style.textOpacity >= 0 ||
            style.hasImage) {
            spec.parts.insert(i, style);
        }
    }
    spec.danger = colorFromString(settings.value(accentSettingKey("Danger")).toString());
    spec.warning = colorFromString(settings.value(accentSettingKey("Warning")).toString());
    spec.ok = colorFromString(settings.value(accentSettingKey("Ok")).toString());
    spec.errorText = colorFromString(settings.value(accentSettingKey("ErrorText")).toString());
    // 盘上那一份是能手改的、也可能是以后降级回退留下的：**认不出的一律退回出厂**，
    // 不猜也不报错（和 LauncherSettings::load() 那几处同一个路数）。
    return normalize(spec);
}

// ---------------------------------------------------------------------------
// follow()
// ---------------------------------------------------------------------------

namespace {

/**
 * @brief "主题一变就重算一遍"这条登记。
 *
 * 它是个事件过滤器而不是"调色板变了"这个信号 —— Qt 没有那样的信号，但**每个控件**都会
 * 收到 `ApplicationPaletteChange`（应用级）和 `PaletteChange`（控件级）。
 */
class PaletteFollower : public QObject {
public:
    PaletteFollower(QWidget *owner, std::function<void()> fn)
        : QObject(owner), m_fn(std::move(fn)) {
        owner->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        switch (event->type()) {
        case QEvent::ApplicationPaletteChange:
        case QEvent::PaletteChange:
        case QEvent::StyleChange:
            // ★ 防自咬：`m_fn()` 里多半就是 `setStyleSheet()` / `setPalette()`，而这两样
            //   各自会**再发一次** `StyleChange` / `PaletteChange` —— 不拦的话就是
            //   "重算 → 又收到 → 再重算"，而且它是无限递归，屏幕上只会看到卡死。
            //   `ui_check` W6 那条断言就是照着这句话量的（计数器不许涨第二下）。
            if (!m_running) {
                m_running = true;
                m_fn();
                m_running = false;
            }
            break;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void()> m_fn;
    bool m_running = false;
};

}  // namespace

void followImpl(QWidget *owner, std::function<void()> fn) {
    // parent 就是 owner ⇒ owner 没了这一条自己掉，不用谁来收。
    new PaletteFollower(owner, std::move(fn));
}

}  // namespace Theme
