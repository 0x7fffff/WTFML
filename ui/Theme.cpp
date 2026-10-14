/**
 * @file Theme.cpp
 * @brief Theme 的实现：预设 / 覆盖项怎么算成一个调色板，以及"地方"和"语义色"现在是什么颜色。
 * @author 0x7fffff
 * @date 2026-09-28
 */

#include "Theme.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QGuiApplication>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <QPushButton>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QStyleOptionComboBox>
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
    // ★ 标题栏出厂就是**主题色那一支**（默认值由 defaultAccentColor() 统一提供）。
    //   它原来和底栏一样是"窗口底色压暗一档"，两处**故意不一样了**：
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

/// 按钮身上那个"尺寸档"属性（`Theme::markIconButton()` 挂的）。今天只有一个值：图标按钮。
/// **只有这一处出处**，理由同上（改名字 = 属性挂上了、选择器一条都没匹配到）。
constexpr const char kButtonSizeProperty[] = "wtfmlButtonSize";
constexpr const char kIconButtonValue[] = "icon";

/// 页面骨架那几层"面"挂在控件上的属性名 / 属性值（2026-10-12）。
///
/// 用户 2026-10-12 那份改版文档里的第一条规则是「减少框」：整页只留**两个**面
/// （侧栏 + 正文），其余靠排版分层。这一套属性就是那两个面（外加"小节卡片"和"页脚"
/// 两档）的**唯一出处** —— 产品那边 `setProperty()` 写的是这两个常量，样式表里那几条
/// 选择器读的也是它们。两边各写一遍字符串的话，改名字就成了"属性挂上了、样式一条都没
/// 匹配上"，而屏幕上看只是"这一块没有底色"。
constexpr const char kSurfaceProperty[] = "wtfmlSurface";
/// 文字角色（页面标题 / 小节标题 / 次要说明）挂在 QLabel 上的属性名。
constexpr const char kTextRoleProperty[] = "wtfmlTextRole";

/// 它们那几个值。**只有这一处**（产品、样式表、验证工程三方共用，见头文件那段）。
constexpr const char kSurfaceSidebar[] = "sidebar";
constexpr const char kSurfaceWorkspace[] = "workspace";
constexpr const char kSurfaceSection[] = "section";
constexpr const char kSurfaceFooter[] = "footer";
constexpr const char kTextRolePageTitle[] = "pageTitle";
constexpr const char kTextRoleSectionTitle[] = "sectionTitle";
constexpr const char kTextRoleSecondary[] = "secondary";

/// 把 @p color 按 @p percent 罩在白上（`percent` = 0 给全白、100 给 @p color 本色）。
QColor mixWithWhite(const QColor &color, int percent) {
    const int p = qBound(0, percent, 100);
    const auto mix = [p](int top) { return (255 * (100 - p) + top * p + 50) / 100; };
    return QColor(mix(color.red()), mix(color.green()), mix(color.blue()));
}

/// 把 @p color 按 @p percent 混进**黑**里（`percent` = 0 给全黑、100 给 @p color 本色）。
///
/// 和 `mixWithWhite()` 是同一族（同一个式子，只是那一头是 `0` 而不是 `255`）——
/// **主题色那一支里"深一点"那两档走的就是它**，不走 `QColor::darker()`。
///
/// ★ 为什么不用 `darker()`（2026-10-13 修 W13 时定的）：两者算出来的数**基本一样**
///   —— 实测出厂色下 `pure.darker(167)` 和 `mixWithBlack(pure, 60)` **逐字节相等**，
///   都是 `#254d3e`；换别的主题色最多差 1（八种主题色逐个量过）。
///   差别在**读得懂读不懂**：`darker(n)` 那一头是"明度乘 `100/n`"，想表达"六成"得写
///   `167`、"八成"得写 `125` —— 这两个数在调用点上读不出意图，改一个数还得先在脑子里
///   换算一遍。这里从**通道**出发直接混，算式和 `mixWithWhite()` 逐字同式（含那一位
///   `+50`），调用点写的就是比例本身（六成 / 八成），和 `mixWithWhite(pure, 12)` 是一族。
QColor mixWithBlack(const QColor &color, int percent) {
    const int p = qBound(0, percent, 100);
    const auto mix = [p](int c) { return (c * p + 50) / 100; };
    return QColor(mix(color.red()), mix(color.green()), mix(color.blue()));
}

/// 把 @p color 按 @p percent 混进 @p base（`percent` = 0 给 @p base 本色、100 给 @p color
/// 本色）。和 `rowHoverFill()` / `selectionFill()` 用的是**同一个式子**（含那一位 `+50`）
/// —— 三处都是"从这一块自己的底色出发往主题色那头混"，差 1 就没法逐字节比了。
QColor mixToward(const QColor &base, const QColor &color, int percent) {
    const int p = qBound(0, percent, 100);
    const auto mix = [p](int b, int a) { return (b * (100 - p) + a * p + 50) / 100; };
    return QColor(mix(base.red(), color.red()), mix(base.green(), color.green()),
                  mix(base.blue(), color.blue()));
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

// ---------------------------------------------------------------------------
// 我们那一份代理样式（`wtfml-fusion`）—— 今天它有两件事
// ---------------------------------------------------------------------------

/**
 * @brief 产品+三个验证工程共用的那一份代理样式。**它接手两件事**：
 *
 *   1. 下拉框右边那个 `∨`（下面这一大段）；
 *   2. **选中那一块**（`PE_PanelItemViewItem` / `PE_PanelItemViewRow` 两条原语 +
 *      `CE_ItemViewItem` 里换墨）—— 用户 2026-09-29 原话「选中的时候那个蓝色的换成深灰色」，
 *      2026-10-12 起那一块是"这一行自己的底色混一档主题色 + 左边一根 3 px 主题色指示条"
 *      （用户答复：「完全照 §9.3（和侧栏导航同款）」）。
 *      为什么落在这一层：调色板的 `Highlight` 是平台给的蓝，而"出厂那一份不碰调色板"这条
 *      不能破（破=把"跟着系统深浅"钉死，见 `apply()` 那段）。样式这一层一份代码就管住
 *      全仓每一张表，连不用我们那两个代理的表、以及下拉框弹出来那张表也一起管住了。
 *
 * （类名原来叫 `ComboChevronStyle`，只管得着第 1 件。加第 2 件的时候一并改成现在这个名字
 *   —— 名字叫"下拉框箭头样式"会让人以为选中色是别处画的。）
 *
 * 【第 1 件：下拉框右边那个 `∨` 为什么非自己画不可】2026-09-29 读 `qstylesheetstyle.cpp`
 * + 实测：样式表里只要出现
 * `QComboBox::drop-down { border: none; … }`，这一格在 `QRenderRule::hasDrawable()` 眼里就
 * 成了"有东西要画" —— `border: none` 解析出来是 `BorderStyle_None`，**不等于**
 * `BorderStyle_Native`（`qcssparser.cpp` 那个 `parseBorderStyle`）—— 于是
 * `drawComplexControl` 走 `subRule.drawRule() + subRule2.drawRule()` 那一条分支，
 * **一个箭头都不画**。也就是说旧样式表下"下拉框没有箭头"不是错觉，"有个箭头"这件事得由
 * 我们自己接回来：把 `border: none` 去掉（只留 `width:`），Qt 就落回基样式那条路，
 * 由基样式调 `proxy()->drawPrimitive(PE_IndicatorArrowDown, …)` —— 而 `proxy()` 指回来的
 * 正是这里。**所以这一条和 `widgetStyleSheet()` 里那个 `::drop-down` 是一对，改一边必须
 * 看另一边。**
 *
 * 【两件量出来的事】(1) 进来时 `opt->rect` 是**箭头那一小格**而不是整个下拉框（基样式已经
 * 收窄过），拿它算格子会算歪 ⇒ 按控件自己的 `rect` 重新问一次；(2) 绘制期间
 * `subControlRect()` 探不到样式表里那个 `::drop-down` 的宽（它头上那句 `RECURSION_GUARD`
 * 让它直接转给基样式），量出来比真正画立体边的那一格**窄 1px** ⇒ 右边要补 1，不然会留一条
 * `#555555` 的竖线。
 */
class WtfmlProxyStyle : public QProxyStyle
{
public:
    using QProxyStyle::QProxyStyle;

    /// "这一条原语该不该由我们画成那块浅灰"—— 是的话给出那一份 option，不是给 nullptr。
    ///
    /// 判据和基样式里那两条逐字一样（`if (showDecorationSelected && State_Selected)`），
    /// 不自己发明第二条：`showDecorationSelected` 是"选中那一块铺整行还是只铺文字那一格"
    /// 这个开关，样式说 false 的地方基样式本来就不铺，我们补上去等于凭空多画一块。
    static const QStyleOptionViewItem *selectedItemOption(const QStyleOption *opt)
    {
        const auto *vopt = qstyleoption_cast<const QStyleOptionViewItem *>(opt);
        if (vopt == nullptr || !vopt->showDecorationSelected ||
            (vopt->state & QStyle::State_Selected) == 0) {
            return nullptr;
        }
        // ★ 只接管"铺**平台那一档**选中色"的地方。谁**自己**把选中色换掉了，就交回给基样式
        //   —— 下拉框弹出来那张表正是（`widgetStyleSheet()` 里那条 `selection-background-color`
        //   是用户 2026-09-28 点名要的"主题色打底 + 黑墨"）。判据是"这一份 option 里的
        //   `Highlight` 和**应用调色板**里那一档是不是同一个色"：一样 ⇒ 它没自己换过。
        //
        //   ★ 比的是 **Active 那一组**，不是 `color(role)` 那个 Current：窗口一失焦，控件身上
        //     的 `Highlight` 就落到 Inactive 那一组（Fusion 给的是另一种淡蓝），拿 Current 比
        //     会得到"窗口一失焦我们就撒手"—— 那个 bug 只在失焦时出现，屏幕上看着像"有时灰
        //     有时蓝"。比 8 位的那三个数（`.rgb()`），不用 `QColor::operator==`：Qt 6 那个比
        //     的是内部 16 位分量，两处各构造一次就可能不相等（这条坑见 `ui_check` 里那段）。
        if (vopt->palette.color(QPalette::Active, QPalette::Highlight).rgb() !=
            platformSelectionColor().rgb()) {
            return nullptr;
        }
        return vopt;
    }

    /// 平台那一档选中色（= 应用调色板里那一档；我们从不去改它，见 `apply()` 那段说明）。
    static QColor platformSelectionColor()
    {
        return QGuiApplication::palette().color(QPalette::Active, QPalette::Highlight);
    }

    void drawPrimitive(PrimitiveElement pe, const QStyleOption *opt, QPainter *p,
                       const QWidget *w) const override
    {
        // ★ 选中那一块：调色板里那一档（`Highlight`）是**平台给的蓝**，而"出厂那一份不碰
        //   调色板"这条不能破（破了就把"跟着系统深浅"钉死了，见 `apply()` 那段）⇒ 由这里
        //   接过来自己画成"这一行自己的底色混一档主题色"。用户 2026-09-29 原话：「选中的
        //   时候那个蓝色的换成深灰色」；2026-10-12 换成现在这一档（用户答复：「完全照 §9.3
        //   （和侧栏导航同款）」）—— 底和左边那根指示条的理由都在 `ui/Theme.hpp` 那段里。
        //
        //   两条原语各管一半，缺一条屏幕上就留一块蓝：
        //   - `PE_PanelItemViewItem` = 条目那一格（每张表都有）；
        //   - `PE_PanelItemViewRow`  = **左边那一截分支区**（`QTreeView::drawRow()` 在把画笔
        //     交给代理之前自己铺的那一条，矩形是"整条行高 × 缩进宽度"）。
        //   代理侧还有一份同样的补涂（`VersionTreeDelegate::paint()` 里那段），因为这两条
        //   原语是不是真的被走到由**视图**决定 —— 补涂那一份是保底，两边同一个色。
        //
        //   ★ 指示条也在这里画：这张表是不是真的走这两条原语由**视图**决定，等它铺完再
        //     补一条竖条的话，视图那条路上就又留一个"补没补"的分叉 ⇒ 一次画齐（代理那边
        //     只在"它自己把这一格又盖了一遍"时才需要重画一次，见 `VersionTreeDelegate`）。
        //   ★ 两处例子**故意不要这根条**（各自有自己那一圈形状），由它们自己拿底色擦掉：
        //     实例行（带子只有卡片那一圈 —— 用户 2026-09-29 点名要删的正是左边那根竖条）
        //     和分组对话框那份表（一行一块内缩的卡片）。见那两个代理里的 NOTE。
        if (pe == PE_PanelItemViewItem || pe == PE_PanelItemViewRow) {
            if (const QStyleOptionViewItem *vopt = selectedItemOption(opt)) {
                p->fillRect(vopt->rect, selectionFill(vopt->palette.color(QPalette::Base)));
                paintSelectionRail(p, vopt->rect);
                return;
            }
        }

        // ★ 键盘焦点环（coding plan §8.3「全部关键操作具备键盘焦点、可见焦点环」）。
        //
        //   基样式（Fusion）画的是**一圈虚线**，而我们的按钮挂了应用级样式表之后那圈虚线
        //   常常被自己的边盖掉/看不见 —— "按 Tab 键跳到哪儿了"就成了一个说不出口的状态。
        //   这里把它接过来画成**两像素的主题色圆圈**：浅底上用 `AccentShade::Ink`（深绿）、
        //   深底上用 `AccentShade::Hover`（浅绿），判据只有一处（`focusRingColor()`）。
        //
        //   ★ **条目视图（列表 / 树 / 表格）不接管**：那儿的"当前项"框和选中那一块是两回事，
        //     基样式画的 `PE_FrameFocusRect` 在 view 上是"焦点在哪个格子上"的提示，换掉它
        //     等于顺手改了列表的样子（我们的选中色是另画的，见上面那两条原语）。
        //   ★ `w == nullptr` 时交回去：没有控件就没有"谁拿着焦点"这回事（探针调用的场景）。
        if (pe == PE_FrameFocusRect) {
            if (w == nullptr || qobject_cast<const QAbstractItemView *>(w) != nullptr ||
                (opt->state & State_HasFocus) == 0) {
                QProxyStyle::drawPrimitive(pe, opt, p, w);
                return;
            }
            QColor ring = focusRingColor(opt->palette);
            // 文字透明度调低过的话（`textOpacity`）那一整圈也跟着淡一档 —— 它挂在
            // 调色板的墨色上就是那个意思，跟着走别自己算。
            const int inkAlpha = opt->palette.color(QPalette::WindowText).alpha();
            if (inkAlpha < 255) {
                ring.setAlpha(inkAlpha);
            }
            p->save();
            p->setRenderHint(QPainter::Antialiasing, true);
            p->setBrush(Qt::NoBrush);
            QPen pen(ring);
            pen.setWidthF(2.0);
            p->setPen(pen);
            // 内缩 1 px：两像素的笔有一半落在外面，贴着边缘画会被裁掉一半（看上去就成一像素了）。
            // 圆角 3 和输入框那一圈（样式表里 `border-radius`）对齐。
            p->drawRoundedRect(QRectF(opt->rect).adjusted(1, 1, -1, -1), 3, 3);
            p->restore();
            return;
        }

        // 别的向下箭头（带菜单的按钮、表头排序…）原样交回去。
        if (pe != PE_IndicatorArrowDown || qobject_cast<const QComboBox *>(w) == nullptr) {
            QProxyStyle::drawPrimitive(pe, opt, p, w);
            return;
        }

        // 1) 先把那一格擦成下拉框自己的底：基样式在画箭头之前压了一层 95 风格的立体边
        //    （`qDrawWinButton`），不擦掉的话 V 会坐在一个凸起的小方块里。
        QStyleOptionComboBox comboOpt;
        comboOpt.initFrom(w);
        comboOpt.rect = w->rect();
        comboOpt.subControls = SC_All;
        QRect cell = subControlRect(CC_ComboBox, &comboOpt, SC_ComboBoxArrow, w);
        if (cell.isValid()) {
            // 只擦边框以内那一片（外圈那一圈是样式表画的那 1px），右边补足上面说的那 1px。
            cell = cell.intersected(w->rect().adjusted(1, 1, -1, -1));
            cell.setRight(w->width() - 2);
            p->fillRect(cell, opt->palette.brush(QPalette::Button));
        }

        // 2) 画 V。墨色跟着 `ButtonText` 走 —— 应用级样式表那句 `color:` 已经被灌进调色板
        //    了（可用 = 纯黑、禁用 = 灰），**不用自己判 `isEnabled()`**：判了就等于在别的底上
        //    写死了两档灰。
        const QRect box = cell.isValid() ? cell : opt->rect;
        const QPoint center = box.center();
        const int half = 4;  // 半宽
        const int rise = 2;  // 两端比中间高多少
        QPainterPath path;
        path.moveTo(center.x() - half, center.y() - rise);
        path.lineTo(center.x(), center.y() + rise);
        path.lineTo(center.x() + half, center.y() - rise);
        QPen pen(opt->palette.color(QPalette::ButtonText));
        pen.setWidthF(1.6);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->setPen(pen);
        p->setBrush(Qt::NoBrush);
        p->drawPath(path);
        p->restore();
    }

    void drawControl(ControlElement ce, const QStyleOption *opt, QPainter *p,
                     const QWidget *w) const override
    {
        // ★ 选中那一行的**字**：底色那一块已经换成"这一行自己的底色混一档主题色"了（见上面
        //   那两条原语），而基样式紧接着印字用的是 `HighlightedText` —— 那一档还是"照蓝底
        //   配的"（深色系统主题下是白、浅色下是近黑）。这一块底上必须换一支**配套**的墨：
        //   浅色主题下平台给的近黑勉强能用，深色主题下那支白字压上去就没了（见
        //   `selectionInk()` 里那两档怎么翻）。用户原话和整套理由见 `ui/Theme.hpp` 那段。
        //
        //   改的是**这一份拷贝**、不是控件身上那份：换色只对这一次绘制生效，别的地方读
        //   `HighlightedText`（它们是照平台那一套算的）一点都不受影响。
        //
        //   ★ 只有 `CE_ItemViewItem` 走这一支。两个代理自己手画正文的那几棵树不经过这儿
        //     （它们在 `paint()` 里读的是自己那份局部调色板，`VersionTreeDelegate` /
        //     `InstanceGroupDelegate` 各有一处同样的换墨），实例行那种"把文字交给样式印"
        //     的走这儿 —— 三条路上屏幕看到的必须是同一支墨。
        if (ce == CE_ItemViewItem) {
            if (const QStyleOptionViewItem *vopt = selectedItemOption(opt)) {
                QStyleOptionViewItem patched(*vopt);
                // 换墨那一套只有一处出处（`selectionPalette()`），和三个手画正文的代理
                // 读的是同一个函数 —— 两条路上屏幕看到的必须是同一支墨。
                patched.palette = selectionPalette(patched.palette, /*selected=*/true);
                QProxyStyle::drawControl(ce, &patched, p, w);
                return;
            }
        }
        QProxyStyle::drawControl(ce, opt, p, w);
    }
};

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
    // 2026-10-12 把原先偏荧光的 #56eb6c 收成更沉稳的翡翠绿：顶栏长时间停在视野里，
    // 降低明度后不再抢内容；同时白字对它仍有 4.5:1 以上的对比度，实心主按钮也能复用。
    //   全仓只有这一处写这六个十六进制位 —— 顶栏、按钮的字与边、当前那一颗导航底下那条
    //   线，量的都是它算出来的某一档（见 `accentShade()`），**不许在别处再写一遍**。
    return QColor(0x3d, 0x80, 0x68);
}

QColor rowHoverFill(const QColor &base) {
    // 认不出（盘上手改歪了）就退回出厂那一支 —— 和 accentShade() 同一个兜底。
    QColor accent = current().accent;
    if (!accent.isValid()) {
        accent = defaultAccentColor();
    }
    // ★ 混法**必须和 `mixWithWhite()` 逐字同式**（`(底*(100-p) + 主题*p + 50) / 100`）：
    //   浅色下 `base` 就是白，这一份和 `accentShade(AccentShade::Hover)` 因此**逐字节相等**
    //   —— ui_check 钉着这条（"行悬停和按钮悬停是同一个色"就是靠它）。四舍五入那一位
    //   （`+50`）不改的话两边会对不上，而那种差 1 的错在屏幕上完全看不出来。
    const int p = qBound(0, rowHoverAccentPercent(), 100);
    const auto mix = [p](int b, int a) { return (b * (100 - p) + a * p + 50) / 100; };
    return QColor(mix(base.red(), accent.red()), mix(base.green(), accent.green()),
                  mix(base.blue(), accent.blue()));
}

QColor neutralFill(const QColor &base, const QColor &ink, int percent) {
    // 「中性那一档」：底色往墨色那头混。**唯二的读者是日志窗那一排开关和它行间那条线**
    // （见 .hpp 里那段"开放深浅色之后写死的那几个灰站不住"）—— 浅色下和原来那几个字面量
    // 只差 ≤5，深色下自动变深灰。
    // 混法和 `mixWithWhite()` / `rowHoverFill()` **逐字同式**（那一位 `+50` 也照抄）：
    // 三个地方算出来的是同一族数，屏幕上才不会出现"两处差 1"的斑。
    // @param percent = 往墨那头走了几成：`0` 给 @p base 原样、`100` 给全墨。
    const int p = qBound(0, percent, 100);
    const auto mix = [p](int b, int i) { return (b * (100 - p) + i * p + 50) / 100; };
    return QColor(mix(base.red(), ink.red()), mix(base.green(), ink.green()),
                  mix(base.blue(), ink.blue()));
}

QColor selectionFill(const QColor &base) {
    // 混法**和 `rowHoverFill()` 逐字同式**（同一个 `mixToward()`），只差比例 —— 两边都是
    // "从这一行自己的底色出发往主题色那头混"。浅色下底就是白，所以这一档和
    // `accentShade(AccentShade::Hover)` 也只差那两个百分比。
    QColor accent = current().accent;
    if (!accent.isValid()) {
        accent = defaultAccentColor();
    }
    return mixToward(base, accent, selectionAccentPercent());
}

QColor selectionInk(const QColor &base) {
    // ★ 墨**跟着那一块底的明暗翻**（2026-09-29 立下的规矩：底改浅了墨必须跟着翻过去 ——
    //   反方向上"亮墨压浅底 = 等于没字"照样成立）。浮上来的一直是**主题色那一支**里
    //   对应的那一档，不是黑/白两档写死的灰。
    //
    //   浅底（默认那身浅色主题：20% 主题色罩在白上）：`AccentShade::Ink` 压上去约 7.4:1。
    //   **不能**用 `AccentShade::Pure` —— 本色压在自己 20% 的浅底上只有 2.4:1，那行字就糊了。
    //   深底（深色预设：约 `#31433a`）：`Ink` 那一档压上去只剩 1.2:1（等于没字）⇒ 换成
    //   那一支里"深底上写字"的那一档（主题色罩 25% 白 —— 罩白比例**越小越亮**），约 7.5:1。
    if (selectionFill(base).lightness() > 128) {
        return accentShade(AccentShade::Ink);
    }
    return mixWithWhite(accentShade(AccentShade::Pure), 25);
}

QColor selectionSubInk(const QColor &base) {
    // 次要那一档（版本树第二行那行小字、占位文字、行里那些小图标）：浅底上约 5:1、
    // 深底上约 5.7:1 —— 比正文那一档低一档（罩白的比例大一档，于是更靠主题色、
    // 也就更暗），但一样读得清。
    if (selectionFill(base).lightness() > 128) {
        return QColor(0x55, 0x55, 0x55);
    }
    return mixWithWhite(accentShade(AccentShade::Pure), 45);
}

QColor selectionBar() {
    return accentShade(AccentShade::Pure);
}

void paintSelectionRail(QPainter *painter, const QRect &rowRect) {
    if (painter == nullptr || rowRect.height() <= 0 || selectionBarThickness() <= 0) {
        return;
    }
    // x 恒取 0：行在画家坐标系里的左边缘就是 0（横向滚动是烘进 `option.rect` 里的，
    // 不是平移画家）—— ui_check §⑫ 量到的也正是 x = 0..2 这一列。
    painter->fillRect(QRect(0, rowRect.top(), selectionBarThickness(), rowRect.height()),
                      selectionBar());
}

QPalette selectionPalette(const QPalette &palette, bool selected) {
    QPalette out = palette;
    if (!selected) {
        return out;
    }
    // 这一行**自己的底色**（`QAbstractItemView` 构造里给视口设的 backgroundRole 就是它）：
    // 底和墨都从它推 —— 深色预设下底是 `#323232`，不从这个出发就会闪成一片近白。
    const QColor base = palette.color(QPalette::Base);
    // 换上去的墨沿用**原来那一档的 alpha**（用户把"文字透明度"调低过的话，选中那一行的字
    // 也该一样淡 —— `kInkRoles` 里就有这几位，那个 alpha 正是这个意思）。
    const auto inkOn = [](const QColor &want, const QColor &was) {
        QColor ink = want;
        if (was.alpha() < 255) {
            ink.setAlpha(was.alpha());
        }
        return ink;
    };
    out.setColor(QPalette::WindowText,
                 inkOn(selectionInk(base), palette.color(QPalette::WindowText)));
    out.setColor(QPalette::Text, inkOn(selectionInk(base), palette.color(QPalette::Text)));
    out.setColor(QPalette::HighlightedText,
                 inkOn(selectionInk(base), palette.color(QPalette::HighlightedText)));
    out.setColor(QPalette::PlaceholderText,
                 inkOn(selectionSubInk(base), palette.color(QPalette::PlaceholderText)));
    // 这一行真正铺的那一层是那块浅主题色底（不是调色板里那一档蓝）—— 有几处按"底色"重算
    // 文字对比度（版本树上那两行 MOTD），读的就是 `Highlight`。
    out.setColor(QPalette::Highlight, selectionFill(base));
    return out;
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
        // 白底上写字的那一档：**主题色的 60%**（= 明度压到六成，色相 / 饱和度原样）。
        //   出厂色 #3d8068 下是 `#254d3e` —— 对白底 9.5:1，比正文（纯黑 21:1）轻，
        //   但一眼看得出是"主题色那一支的深绿"。
        // ★ 2026-10-13 修 W13 改的就是这一档：原先取的是纯色的**一半**（`darker(200)`
        //   = `#1f4034`），对白底 11:1 —— 太黑了，压到和「一般性」那一档黑（`#202020`）
        //   只差 53（ui_check 那条判据要 60），屏幕上就是"分了三档"这句话说不响：
        //   一颗主题色按钮和一颗黑按钮长得几乎一样。六成之后两处同时成立：离那一档黑
        //   80、对白底仍有 9.5:1（WCAG 正文那条要 4.5:1）。
        return mixWithBlack(pure, 60);
    case AccentShade::Border:
        // 那一圈边：**主题色的 80%**（出厂色下 `#316653`，对白底 6.7:1）。
        //   比字浅一档（"边只要看得见、字要读得清"），但对白底仍远超界面部件那条 3:1。
        // ★ 2026-10-13 跟着 `Ink` 一起抬：它原来取纯色的 2/3（`#295545`），夹在
        //   "抬上来的字"和纯色之间只剩 12 的明度差，三档就挤成一坨了。现在纯色 / 边 /
        //   字是 128 / 102 / 77，两两差着 20 上下，屏幕上真能分出三层。
        return mixWithBlack(pure, 80);
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

// 「灰掉」那一档的三个色（详见头文件里那段）。★ 三处**必须一起看**：底浅一档、字浅两档、
// 边浅浅地留一道 —— 单看哪一个都只是"一个灰色"，合起来才是"这一格现在按不动，但形状还在"。
QColor disabledInkColor() {
    // ★ 2026-10-14（coding plan §3.3「单独设计 Disabled 状态」）：从 `#9a9a9a` 提到
    //   **`#8a8a8a`**。原来那个对 `disabledFillColor()` 只有 2.6:1 —— 一页里字号最小的
    //   那几颗（12~13px）灰掉之后基本是"看不清"而不是"读得清"。这一档是 3.2:1，
    //   既还是明显灰的（对白底 3.4:1、对次要用那支纯黑 15:1），又读得出来写的是什么。
    //   判据不是"我觉得好看"：ui_check 里那条**现算对比度**，下限就是 3:1。
    return QColor(0x8a, 0x8a, 0x8a);
}

QColor disabledBorderColor() {
    // 轮廓那一圈：比字再浅一档。抹掉它的话，灰掉的一格在白底上只剩几个灰字 ——
    // "这儿本来有一颗按钮"这件事就看不出来了（而那正是禁用态要传达的一半信息）。
    return QColor(0xd2, 0xd2, 0xd2);
}

QColor disabledFillColor() {
    // 底：贴白但明显不是白（对着 `#ffffff` 的页面底一眼能看出这一格是"塌"下去的）。
    return QColor(0xf7, 0xf7, 0xf7);
}

QColor focusRingColor(const QPalette &palette) {
    // 深浅两档就是主题色那一支里的两头（浅底用"白底上写字"的那一档、深底用"往白里混 12%"
    // 的那一档）。**和别处"深色下用亮的那一档"是同一个判断**（`isDarkPalette()`），
    // 别在这儿另立一条"看主题名"的规矩。
    return isDarkPalette(palette) ? accentShade(AccentShade::Hover)
                                  : accentShade(AccentShade::Ink);
}

QColor progressTrackColor(const QPalette &palette) {
    // 底色往墨色那头混一档 —— 混法和 `neutralFill()` 里那三处逐字同式（含那一位 `+50`）。
    // 14 是照着"浅底上要淡到像一条槽、又不能淡到看不见"挑的（浅色下大概 `#e3e3e3`）。
    return neutralFill(palette.color(QPalette::Base), palette.color(QPalette::WindowText), 14);
}

QColor progressFillColor(const QPalette &palette) {
    // 深浅两档和焦点环、以及"深色下用亮的那一档"是**同一个判断**（`isDarkPalette()`）。
    return isDarkPalette(palette) ? accentShade(AccentShade::Hover)
                                  : accentShade(AccentShade::Pure);
}

QString buttonKindKey(ButtonKind kind) {
    switch (kind) {
    case ButtonKind::Primary:
        return QStringLiteral("primary");
    case ButtonKind::Neutral:
        return QStringLiteral("neutral");
    case ButtonKind::Ghost:
        return QStringLiteral("ghost");
    case ButtonKind::Danger:
        return QStringLiteral("danger");
    case ButtonKind::Accent:
        break;
    }
    // 普通按钮**不挂属性**（挂一个 "accent" 只是让构造出来的控件都带一位恒真的值）。
    return QString();
}

// ---------------------------------------------------------------------------
// 「面」与「文字角色」那两个属性（2026-10-12）
// ---------------------------------------------------------------------------

// 这一组就是上面那几个 `constexpr char` 的出口（**唯一出处**：产品、样式表、验证工程
// 三方都读它，理由见头文件里那段）。写成函数而不是把常量放进头文件，是为了它们能和
// 别的 `QString` 访问器一样用在 `setProperty()` / `findChild()` 那一族调用里。
QString surfacePropertyName() { return QString::fromLatin1(kSurfaceProperty); }
QString sidebarSurfaceValue() { return QString::fromLatin1(kSurfaceSidebar); }
QString workspaceSurfaceValue() { return QString::fromLatin1(kSurfaceWorkspace); }
QString sectionSurfaceValue() { return QString::fromLatin1(kSurfaceSection); }
QString footerSurfaceValue() { return QString::fromLatin1(kSurfaceFooter); }
QString textRolePropertyName() { return QString::fromLatin1(kTextRoleProperty); }
QString pageTitleTextRoleValue() { return QString::fromLatin1(kTextRolePageTitle); }
QString sectionTitleTextRoleValue() { return QString::fromLatin1(kTextRoleSectionTitle); }
QString secondaryTextRoleValue() { return QString::fromLatin1(kTextRoleSecondary); }

// ---------------------------------------------------------------------------
// 滚动条
// ---------------------------------------------------------------------------

namespace {

/// 把手那一支中性灰的**实色**那一半（alpha 由下面三个函数各自加）。
///
/// 底色浅 ⇒ 一支深灰；底色深 ⇒ 一支浅灰。**同一支灰**在两种底上都不是"看不见"就是
/// "一块黑疙瘩"，所以这一位必须跟着深浅走。
QColor scrollHandleBase() {
    return isDark() ? QColor(0xff, 0xff, 0xff) : QColor(0x0f, 0x0f, 0x0f);
}

/// 同一支灰的三档浓淡（0~255 的 alpha）：平时 / 悬停 / 拖。
QColor scrollHandleAt(int alpha) {
    QColor color = scrollHandleBase();
    color.setAlpha(alpha);
    return color;
}

/// QSS 里那个 `rgba(...)`。**不写 `#AARRGGBB`**：Qt 样式表认的是 `#RRGGBB` 和 `rgba()`
/// 两种，八位的十六进制进到那边是"认不出就忽略"，静默退成没有这一条。
QString rgbaCss(const QColor &color) {
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alpha());
}

}  // namespace

QColor scrollHandleColor() {
    // 110/255 ≈ 43%：压在 `#fcfcfc` 上量出来是一支 `#919191` 上下的灰 —— 一眼看得见，
    // 又不会把旁边那行字比下去。深底上那支浅灰同理。
    return scrollHandleAt(110);
}

QColor scrollHandleHoverColor() {
    return scrollHandleAt(165);
}

QColor scrollHandlePressedColor() {
    return scrollHandleAt(205);
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

QString iconButtonSizeValue() {
    return QString::fromLatin1(kIconButtonValue);
}

void markIconButton(QWidget *button) {
    if (button == nullptr) {
        return;
    }
    button->setProperty(kButtonSizeProperty, QString::fromLatin1(kIconButtonValue));
}

QString chipStyleSheet(const QPalette &palette) {
    // 那一圈边取 `Mid`（调色板里"看得见的细线"那一档）。
    const QColor border = palette.color(QPalette::Mid);
    QColor hover = palette.color(QPalette::Highlight);
    hover.setAlpha(48);
    return QStringLiteral("QPushButton { border: 1px solid %1; border-radius: %2px;"
                          " padding: 2px 10px; background: transparent; }"
                          // ★ 悬停这一条要 `:!checked`：不加的话，鼠标划过**按着的**那颗
                          //   会把实底盖成半透明的一层 —— "哪一颗是按着的"当场看不出来。
                          "QPushButton:hover:!checked { background-color: %3; }"
                          "QPushButton:checked { background-color: %4; color: %5;"
                          " border-color: %4; }")
        .arg(border.name(QColor::HexArgb))
        .arg(chipHeight() / 2)
        .arg(hover.name(QColor::HexArgb), palette.color(QPalette::Highlight).name(QColor::HexArgb),
             palette.color(QPalette::HighlightedText).name(QColor::HexArgb));
}

void markChipButton(QPushButton *button) {
    if (button == nullptr) {
        return;
    }
    button->setFixedHeight(chipHeight());
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    // 报一句"我按自己钉的那一档高度来"：全局那条 `min-height` 会把它顶到 `actionHeight()`
    // （32），光 `setFixedHeight()` 拦不住（见 `markIconButton()` 那段实测）。
    markIconButton(button);
    button->setStyleSheet(chipStyleSheet(button->palette()));
    // QSS 里嵌的是色名 ⇒ 主题一换得重算。chip 是**常驻**控件（不像资源详情页那一排是整排
    // 重建的），没人会替它重挂，所以这一条必须自己接。
    follow(button, [button]() { button->setStyleSheet(chipStyleSheet(button->palette())); });
}

QColor swatchBorder(const QColor &color) {
    // 深色块给白边、浅色块给黑边。两边都带一点 alpha：这一圈是"给它一个形状"，不是内容，
    // 全实的话浅色页面上会是一圈扎眼的黑。
    //
    // NOTE: 判据取的是**这块色自己的明度**（不是当前主题的深浅）：一块近黑的主题色搁在浅色
    //       页面上，跟着主题走照样是"一个没有形状的洞"。
    return color.lightness() < 128 ? QColor(0xff, 0xff, 0xff, 0xa0)
                                   : QColor(0x00, 0x00, 0x00, 0xa0);
}

/// 现在这一块是哪个色（存在按钮自己的动态属性上）。
///
/// ★ 为什么要存一份：`follow()` 那条重算**不能捕获调用方当时给的那个 `color`** —— 用户换过
///   一个色之后，同一个按钮身上那条老登记还在，主题一变它会把**旧色**贴回去。存一份、
///   重算时读回来，改色这条路上就只有一个出处。
const char *const kSwatchColorProperty = "wtfmlSwatchColor";
/// 这一块**登记过 follow 没有**。`follow()` 每调一次就新挂一个事件过滤器，重算里再调一次
/// 就是每换一次主题多挂一个（越挂越多，`ui_check` W6 那种"只许跑一遍"的判据也会跟着炸）。
const char *const kSwatchFollowProperty = "wtfmlSwatchFollow";

QString swatchStyleSheet(const QColor &color) {
    const QColor edge = swatchBorder(color);
    // 悬停 / 按下**只动边框**：底色就是它要表达的那件事，跟着鼠标换掉等于这块色块什么都不是。
    //   - 悬停 → 边框换成主题色本色（"这一块能点"）；
    //   - 按下 → 边框换成主题色更深的那一档（这一下真的按下去了）。
    // ★ 这几条 `:hover` / `:pressed` 是**必须写的**：应用级那份样式表里有同名的伪类规则，
    //   不写的话鼠标一划过去，块里的色会被换成"按钮悬停"那一层淡主题色。
    return QStringLiteral("QPushButton { background-color: %1; border: 1px solid %2;"
                          " border-radius: %3px; padding: 0px; }"
                          "QPushButton:hover { background-color: %1; border-color: %4; }"
                          "QPushButton:pressed { background-color: %1; border-color: %5; }")
        .arg(color.name(QColor::HexArgb), edge.name(QColor::HexArgb))
        .arg(controlRadius())
        .arg(accentShade(AccentShade::Pure).name(QColor::HexArgb),
             accentShade(AccentShade::Ink).name(QColor::HexArgb));
}

void markColorSwatch(QPushButton *button, const QColor &color) {
    if (button == nullptr) {
        return;
    }
    // 先报一句"我按自己钉死的边长来"：全局那条 `min-height` 会把它顶到 `actionHeight()`
    // （32），`setFixedSize()` 拦不住（见 markIconButton() 那段实测）。
    markIconButton(button);
    // ★ 2026-10-13：**把这一刻钉死的尺寸先记下来**，挂完样式表之后再钉一次（下一段有实测）。
    //   "钉死"= 调用方 `setFixedSize()` 过 ⇒ 最小 == 最大；没钉过的是 0×0 对 QWIDGETSIZE_MAX，
    //   不相等 ⇒ 这里就什么都不做。
    const QSize pinned = button->minimumSize() == button->maximumSize() ? button->minimumSize()
                                                                       : QSize();
    button->setCursor(Qt::PointingHandCursor);
    button->setProperty(kSwatchColorProperty, color);
    button->setStyleSheet(swatchStyleSheet(color));
    // ★★ 2026-10-13（ui_shot 第 41 节在**真机**上量出来的）：`setStyleSheet()` 那一下会走一遍
    //    `QStyleSheetStyle::polish`，而它**会按样式表里的 `min-height` 把控件的最小高度改掉** ——
    //    我们那条图标复位规则写的是 `min-height: 0px`，于是最小高度被打回 **2**（= 上下各 1px
    //    边框），`setFixedSize()` 设的"最小 = 最大"就只剩下最大那一半。
    //
    //    后果不是"差几个像素"：最小高度不再是钉死那个数，布局就能把它压下去。真机实测
    //    （ui_shot 第 41 节）：四块语义色量出来是 64×**18**（它们和 18 高的名字标签同一行），
    //    而主题色那一块是 64×**24**（同一行挨着一颗 32 高的按钮，被撑到了最大那儿）——
    //    屏幕上就是"上面那块比下面四块高一头"。钉回去之后五块都是 24。
    //
    //    NOTE: offscreen（ui_check 的跑法）量不出这一条：那边量到的是 64×24，看着好好的。
    //          布局类断言在 offscreen 上会假绿，这是全仓那条老规矩。
    if (pinned.isValid() && pinned.width() < QWIDGETSIZE_MAX && pinned.height() < QWIDGETSIZE_MAX) {
        button->setFixedSize(pinned);
    }
    if (!button->property(kSwatchFollowProperty).toBool()) {
        button->setProperty(kSwatchFollowProperty, true);
        // QSS 里嵌的是色名，而**悬停那一圈跟着主题色走** ⇒ 主题一换得重算一遍（块里那个色
        // 不跟着动 —— 它正是用户选的那个）。
        follow(button, [button]() {
            button->setStyleSheet(
                swatchStyleSheet(button->property(kSwatchColorProperty).value<QColor>()));
            // ★ 样式表重挂那一趟同样会把最小高度打回去 —— 这里也得跟着再钉一次（不然
            //   "换了主题之后色块矮了一截"会在下一次布局时冒出来）。
            const QSize again = button->maximumSize();
            if (again.width() < QWIDGETSIZE_MAX && again.height() < QWIDGETSIZE_MAX) {
                button->setFixedSize(again);
            }
        });
    }
}

QString widgetStyleSheet() {
    const QColor ink = accentShade(AccentShade::Ink);
    const QColor border = accentShade(AccentShade::Border);
    const QColor pure = accentShade(AccentShade::Pure);
    const QColor hover = accentShade(AccentShade::Hover);
    const QColor pressed = accentShade(AccentShade::Pressed);
    // 次要 / 危险那两档的色。红**故意不取 `dangerColor()`**：那一支是"按深浅现推"的；
    // 而按钮使用很浅的固定底色，深色预设给出的浅红在这里对比度不够。所以这一档直接钉死
    // 那个对浅底仍有 4.8:1 的深红，
    // 它和 `errorTextColor()` 浅底那一档是同一个。
    const QColor red(0xc0, 0x28, 0x28);
    const QColor black = neutralInkColor();
    const QColor disabledInk = disabledInkColor();
    const QColor disabledBorder = disabledBorderColor();
    const QColor disabledFill = disabledFillColor();
    const QColor accentFill = mixWithWhite(pure, 6);
    const QColor neutralFill(0xf3, 0xf4, 0xf3);
    const QColor dangerFill(0xfd, 0xf1, 0xf1);

    // ★ `padding: 6px` 不是随手写的：Fusion 的 `PM_ButtonMargin` 就是 6，边框 1 ⇒
    //   2*(6+1) = 14，和样式自己算的**一模一样**。差一点点的话界面上是看不出来，但一堆
    //   "这一行放得下几颗按钮"的断言会整片地漂。
    // ★ 2026-10-12（coding plan §9.1）：圆角走 `buttonRadius()`、**高度走 `actionHeight()`**。
    //   `min-height` 这一位以前根本不存在 —— 一颗按钮多高全看系统字号 + `padding: 6px`
    //   算出来多少（字号一换就在 30~33 之间飘，"一行里两颗一样高"是碰巧的）。钉成 32 之后
    //   它是个**确定的数**，`Theme::actionHeight()` 那个常量也就是从这儿生效的。
    //
    //   NOTE: 两处**故意不跟**（都是 `setMinimumHeight()` 钉过的，QSS 这是"最小"高度，
    //         只会被它们顶上去）：主页那颗「启动游戏」（76）、下载页那颗「安装」（40）。
    //   NOTE: `min-height` 对**自己钉过固定高度**的小控件不生效（行尾那几颗 22/24 px 的
    //         图标按钮都是 `setFixedSize()` 的）—— 那不是漏了，是"它自己说了算"。
    // 按钮那一圈"镀层"：上下各 6px 内边距 + 各 1px 边框 = 14。
    // ★★ QSS 的 `min-height` 量的是**内容**那一格，不是整颗控件（2026-10-12 探针实测：
    //    这一套样式配 `min-height: 32px` 量出来是 **46**，配 18px 才是 32）。所以"整颗
    //    `actionHeight()`"在样式表里得写成它**减掉这一圈**。写错不报错 —— 症状只是"每颗
    //    按钮都胖了一圈"，而那条只能靠数值判据（ui_check 里那一条量的就是整颗高度）。
    constexpr int kButtonChrome = 2 * 6 + 2 * 1;
    QString qss = QStringLiteral(
                      "QPushButton {"
                      " background-color: %3;"
                      " color: %1;"
                      " border: 1px solid transparent;"
                      " border-radius: %6px;"
                      " min-height: %7px;"
                      " padding: 6px; }"
                      // 默认那一颗（对话框里回车就按下去的那颗）：淡淡一层主题色的底。
                      // **不能跟悬停同色** —— 同色的话"鼠标停在哪儿"就看不出默认是哪一颗了。
                      "QPushButton:default:enabled { background-color: %4; }"
                      "QPushButton:hover:enabled { background-color: %4; }"
                      "QPushButton:pressed:enabled { background-color: %5; }"
                      "QPushButton:focus { border-color: %2; }")
                      .arg(ink.name(), border.name(), accentFill.name(), hover.name(), pressed.name())
                      .arg(buttonRadius())
                      .arg(actionHeight() - kButtonChrome);
    // 主动作是页面上唯一一颗实心按钮。悬停和按下都向更深处走，白字对比度不会因为交互
    // 状态而降低；普通确认 / 危险 / 次要按钮仍是白底描边，不会整页争夺注意力。
    const auto primaryBlock = [](const QString &key, const QColor &fill,
                                 const QColor &hoverFill, const QColor &pressedFill) {
        // ★ 2026-10-12（coding plan §9.1）：主动作单独高一档（36）。它是页面上唯一一颗实心
        //   按钮，"这一页最重要的那一下"值得多 4px；其余那颗留在 32 上，一页里两档就够。
        return QStringLiteral("QPushButton[%1=\"%2\"] { background-color: %3; color: #ffffff;"
                              " border-color: %3; border-radius: %6px; min-height: %7px; }"
                              "QPushButton[%1=\"%2\"]:hover:enabled { background-color: %4;"
                              " border-color: %4; }"
                              "QPushButton[%1=\"%2\"]:pressed:enabled { background-color: %5;"
                              " border-color: %5; }")
            .arg(QString::fromLatin1(kButtonKindProperty), key, fill.name(), hoverFill.name(),
                 pressedFill.name())
            .arg(buttonRadius())
            .arg(primaryActionHeight() - kButtonChrome);
    };
    qss += primaryBlock(QStringLiteral("primary"), pure, pure.darker(108), pure.darker(118));

    // 危险 / 次要两档用轻填充建立形状，不再常驻一圈深色框；透明边仍占着 1px，
    // 键盘焦点出现时才换成有意义的语义色，因此不会改变按钮几何。
    const auto kindBlock = [](const QString &key, const QColor &color, const QColor &fill,
                              const QColor &hoverFill, const QColor &pressedFill) {
        return QStringLiteral("QPushButton[%1=\"%2\"] { color: %3; background-color: %4;"
                              " border-color: transparent; }"
                              "QPushButton[%1=\"%2\"]:focus { border-color: %3; }"
                              "QPushButton[%1=\"%2\"]:hover:enabled { background-color: %5; }"
                              "QPushButton[%1=\"%2\"]:pressed:enabled { background-color: %6; }")
            .arg(QString::fromLatin1(kButtonKindProperty), key, color.name(), fill.name(),
                 hoverFill.name(), pressedFill.name());
    };
    qss += kindBlock(QStringLiteral("danger"), red, dangerFill, QColor(0xfd, 0xe5, 0xe5),
                     QColor(0xf7, 0xd5, 0xd5));
    qss += kindBlock(QStringLiteral("neutral"), black, neutralFill, QColor(0xe9, 0xec, 0xe9),
                     QColor(0xdc, 0xe1, 0xdc));

    // ---------- Ghost（2026-10-14，coding plan §3.3「统一按钮视觉层级」）----------
    //
    // 「低频辅助动作」那一档：**只有字**。和 `Neutral` 的分别不是颜色而是**形状** ——
    // 它没有底、没有边，所以它挪到卡片边上不抢眼（这是这一档存在的全部意义）。
    //
    // ★★ 边**不是删掉，是留成透明的 1px**。这一条是这一段最容易写错的地方：写
    //    `border: none` 看着"更干净"，可它会把控件矮 2px、窄 2px（基样式那条
    //    `QPushButton` 规则给的就是 1px 边）—— 于是同一个布局里换一档 kind，行里所有
    //    东西都跟着挪 1px。留着透明的边，三档的几何**一模一样**。
    //   NOTE: 悬停/按下那两层浅灰和 `Neutral` 用的是同一对（`#f0f0f0` / `#e0e0e0`）：
    //         两档的区别在"平时有没有形"，不在"按下去什么色"。
    //   NOTE: 勾上那一档给的是主题色那层淡底（和基样式里 `:default` 同一支色）—— 一颗
    //         checkable 的 Ghost（比如"只看选中的"那种小开关）得让人看出它现在是"亮着"的。
    const QString ghostTint = mixWithWhite(pure, 6).name();
    qss += QStringLiteral("QPushButton[%1=\"ghost\"] { background-color: transparent;"
                          " border-color: transparent; color: %2; }"
                          "QPushButton[%1=\"ghost\"]:hover:enabled { background-color: %3; }"
                          "QPushButton[%1=\"ghost\"]:pressed:enabled { background-color: %4; }"
                          "QPushButton[%1=\"ghost\"]:checked { background-color: %5; }")
               .arg(QString::fromLatin1(kButtonKindProperty), black.name(),
                    QColor(0xf0, 0xf0, 0xf0).name(), QColor(0xe0, 0xe0, 0xe0).name(), ghostTint);

    // ★ 「灰掉」这一条**必须摆在上面那两块之后**（2026-10-09 真机踩到）。
    //   Qt 的样式表按**优先级**排：`QPushButton[wtfmlButtonKind="neutral"]` 和
    //   `QPushButton:disabled` 都是"元素 + 一个属性/伪类"，**优先级一模一样** ——
    //   平手的规矩就是**谁写在后面谁赢**。原来那条 `:disabled` 摆在最前面，于是
    //   中性/危险那两种按钮一灰掉，底变成 `disabledFill` 了（说明确实 disabled 生效了），
    //   **字和边却还是 `#202020`** —— 屏幕上看着就是一颗能按的按钮，只有主题色那颗
    //   （Accent，没有 kind 属性、不参与这场平手）灰得对。真机像素实测：禁用一颗中性
    //   按钮，底 `#F7F7F7` / 字 `#202020`。
    //   所以这一条挪到**最后**追加，把两种 kind 的字和边一并按下去。
    qss += QStringLiteral(
               "QPushButton:disabled { background-color: %1; color: %2;"
               " border-color: transparent; }")
               .arg(disabledFill.name(), disabledInk.name());

    // ★ 图标按钮那一档**必须摆在最后**（2026-10-12）：它和 `[wtfmlButtonKind="primary"]`
    //   优先级一样（都是"元素 + 一个属性"），平手时后面写的赢。它只有一句话 ——
    //   "我按自己钉死的边长来"，把上面那条 `min-height` 按回去。
    //   实测（探针）：写死 22 的 `QPushButton` 在 `min-height: 18px` 下量出来是 **32**
    //   —— `setFixedSize()` 拦不住它，只能靠这一条。`min-height: 0` 是有效的复位。
    qss += QStringLiteral("QPushButton[%1=\"%2\"] { min-height: 0px; }")
               .arg(QString::fromLatin1(kButtonSizeProperty), QString::fromLatin1(kIconButtonValue));

    // 主页快速启动的空槽：它是布局占位和“去实例页添加”的入口，不应长得像已经能启动的
    // 实例。半透明感在 QWidget 上用一层极浅主题色表达；虚线边只用于空槽，不把普通卡片
    // 又带回“框框套框框”的旧观感。
    qss += QStringLiteral(
               "QPushButton[wtfmlQuickEmpty=\"true\"] { background-color: %1; color: %2;"
               " border: 1px dashed %3; }"
               "QPushButton[wtfmlQuickEmpty=\"true\"]:hover:enabled { background-color: %4; }"
               "QPushButton[wtfmlQuickEmpty=\"true\"]:pressed:enabled { background-color: %5; }")
               .arg(mixWithWhite(pure, 3).name(), ink.name(), border.name(),
                    mixWithWhite(pure, 8).name(), mixWithWhite(pure, 13).name());

    // 主页次动作使用浅主题色表面，避免和 Disabled 共用一脸系统灰。它仍比实心 Primary
    // 弱很多，因此“启动”保持唯一主动作，账号、设置和实例卡则明确表现为可以点击。
    qss += QStringLiteral(
               "QPushButton[wtfmlHomeAction=\"true\"] { background-color: %1; color: %2;"
               " border-color: transparent; }"
               "QPushButton[wtfmlHomeAction=\"true\"]:hover:enabled { background-color: %3; }"
               "QPushButton[wtfmlHomeAction=\"true\"]:pressed:enabled { background-color: %4; }"
               "QPushButton[wtfmlHomeAction=\"true\"]:focus { border-color: %5; }")
               .arg(mixWithWhite(pure, 8).name(), ink.name(), mixWithWhite(pure, 14).name(),
                    mixWithWhite(pure, 22).name(), border.name());

    // -----------------------------------------------------------------------
    // 输入框（2026-09-28）
    // -----------------------------------------------------------------------
    //
    // 用户原话：「输入文本的搜索框的框框跟随主题色，文本保留黑色」。所以**边**走主题色
    // （`Border` 那一档，对白底 6.7:1 —— 边框要看得见，不必像字那么重），**底一律白、
    // 字一律黑**（不去跟深浅：这一套界面上的按钮和输入框都是"白纸"，跟了深浅反而会在
    // 深色系统主题下出现白底黑字旁边一堆深灰控件）。
    //
    // 聚焦那一档换成 `Ink`（9.5:1）：边框从"看得见"变成"这一格是活的" —— 只靠加粗或者
    // 换灰是说不清"哪一格有光标"的，而换一档更深的同色系一眼就是它。
    // ★ 高度（2026-09-29 用户原话：「下拉框和所有输入框的高度略微改高点」）。
    //   下面两条**要一起看**，它们必须落成一样高（并排摆着差 1px 一眼就看出来）：
    //
    //     `QLineEdit` / `QComboBox`：`padding: 1px 3px` + `min-height: 20px`
    //     `QAbstractSpinBox`：       `min-height: 18px`
    //
    //   ★★ 2026-10-14（coding plan §3.3 的高度收口）：这一套原来是 **`padding: 3px`**、
    //     靠"字体的自然内容高 + 上下各 3px"凑出 24。那笔账是拿**离屏那套桩字体**算的：
    //     真 windows 的字体下自然内容高是 19（输入框）/ 20（下拉框）/ 18（数字框），
    //     于是三样落成 **25 / 26 / 24** —— 数字框那条 `min-height` 把它钉住了，另外两样
    //     直接吃字体的自然高。并排摆着差 1~2 px，正是当初要消掉的那个毛病。
    //     ⇒ 现在改成**给内容盒统一下限** + 竖向 padding 收成 1px：
    //        输入框 / 下拉框：20 + 2×1 + 2×1（边）= 24
    //        数字框：          18 + 6（它自己那一圈框，基样式画的）= 24
    //       正常字号下两边都是 24px；大字号下允许控件随字体一起长高，不能为对齐裁字。
    //
    //   水平那一位**故意还是 3px**：用户只说了高度，横着那 1px 没有理由动 —— 动了就是
    //   "这一页上所有输入框的字整体右移 1px"。
    //
    //   ★ 为什么数字框不是同样的算法：它那一圈边是**基样式自己画的**（下面那条把里面的
    //     `QLineEdit` 按了回去），给它加 `padding` 就是"框里再套一个框"。它走 `min-height`
    //     —— Qt 里那一位是**设控件的最小高度**（`setMinimumHeight()`，值 = `boxSize(内容高)`），
    //     数字框那一圈框占 6px ⇒ 18 + 6 = 24。两种控件用两种写法不是没统一，是只能这么写。
    //   ★ 因此数字框的 `sizeHint()`（21）和它最后占的高度（24）**不是同一个数**：竖向 `Fixed`
    //     的控件过布局时要取"最小高度"那一关。**量高度别只量 `sizeHint()`** —— ui_check 的
    //     W13 就是那么栽过一次的（三个数都报 24、摆上屏幕数字框却是 27）。
    //   ★ 2026-10-12（coding plan §9.1）：圆角 3 → `controlRadius()`（4）。输入类四个控件
    //     （`QLineEdit` / `QComboBox` / 数字框里面的 `QLineEdit` / 勾选框那个方框）**共用
    //     同一个数**，而按钮是 `buttonRadius()`（6，高一档）—— 按钮比输入框矮胖，同一个
    //     圆角摆在它身上看着更方。
    qss += QStringLiteral(
               "QLineEdit { background-color: #ffffff; color: #000000; border: 1px solid %1;"
               " border-radius: %6px; padding: 1px 3px;"
               " min-height: 20px; }"
               "QLineEdit:focus { border-color: %2; }"
               "QLineEdit:disabled { background-color: %3; color: %4; border-color: %5; }")
               .arg(border.name(), ink.name(), disabledFill.name(), disabledInk.name(),
                    disabledBorder.name())
               .arg(controlRadius());
    // 只钉最小高度，不钉最大高度：正常字号下三者仍是 24px；系统使用大字号时允许控件
    // 跟着长高，不能为了像素对齐把字裁掉。
    qss += QStringLiteral("QAbstractSpinBox { min-height: 18px; }");
    // ★ **数字框 / 下拉框里面那一格也是 `QLineEdit`** —— 上面那条会连它一起框上，画出来
    //   就是"框里还套着一个框"（自旋框自己已经有一圈边了）。这一条把它按回去。
    //   用**后代**选择器（Qt 的 `>` 子选择器不可靠，后代选择器一定生效）。
    //   ★ 2026-10-14：**高度那两位也要按回去**（`min-height: 0px` 是有记录的复位写法，见
    //     下面 `markIconButton()` 那一条）。不按回去的话：里面那一格被钉成"内容高 20"，
    //     而它比数字框自己要的 18 还大 ⇒ **反过来把数字框撑到 26** —— 三样等高的意图从
    //     另一头破掉。
    qss += QStringLiteral("QAbstractSpinBox QLineEdit, QComboBox QLineEdit {"
                          " border: none; background: transparent; padding: 0;"
                          " min-height: 0px; max-height: 16777215px; }");

    // -----------------------------------------------------------------------
    // 下拉框（2026-09-28）
    // -----------------------------------------------------------------------
    //
    // 用户原话：「下拉框和输入框一样都主题色」。所以**和上面那条一模一样的四个色** ——
    // 白底 / 纯黑字 / `Border` 那一档的边 / 聚焦换 `Ink`。两处各写一份颜色是**有意**的：
    // 它们在这一页上是同一族控件（一个框住一段字、一个框住一个选择），差一档并排摆着
    // 一眼就看得出来。改这一条的时候顺手看一眼上面那条。
    //
    // ★ 下拉框比输入框多一层：**弹出来那张表是它的子控件**。Qt 样式表里的 `color` 会
    //   **往下传**，而那张表有自己的底色 —— 不点名的话，深色系统主题下弹出来的就是
    //   "黑的字压在深色的底上"。所以最底下那条 `QComboBox QAbstractItemView` 把表和
    //   选中那一格也一起写成白底黑字（选中那一格用主题色打底、**黑墨**：黑压 `Pure`
    //   是 13.5:1，白压 `Pure` 只有 1.56:1）。
    //
    // NOTE: 箭头那一格是 **2026-09-29 用户原话「下拉框最右边放一个 V 箭头」** 改过的：
    //
    //   - 宽度 18 → 20；
    //   - **`border: none` 拿掉了**。这是这一条里唯一一处看着像"退了一步"、其实是必须的
    //     动作：留着它这一格就算"有东西要画"，Qt 于是走另一条分支、把箭头整个吃掉（实测
    //     旧样式表下箭头压根不存在，见 `WtfmlProxyStyle` 那段）。拿掉之后由
    //     `WtfmlProxyStyle` 接手画那个 `∨` —— 墨色跟着 `color:` 走，换深浅主题照样跟着变。
    //   ⇒ **改这一条之前先把 `WtfmlProxyStyle` 那段读了**，两处是一对。
    qss += QStringLiteral(
               "QComboBox { background-color: #ffffff; color: #000000; border: 1px solid %1;"
               " border-radius: %7px; padding: 1px 3px;"
               " min-height: 20px; }"
               "QComboBox:focus { border-color: %2; }"
               "QComboBox:disabled { background-color: %3; color: %4; border-color: %5; }"
               "QComboBox::drop-down { width: 20px; }"
               "QComboBox QAbstractItemView { background-color: #ffffff; color: #000000;"
               " border: 1px solid %1;"
               " selection-background-color: %6; selection-color: #000000; outline: none; }")
               .arg(border.name(), ink.name(), disabledFill.name(), disabledInk.name(),
                    disabledBorder.name(), pure.name())
               .arg(controlRadius());

    // -----------------------------------------------------------------------
    // 勾选框（2026-09-28）
    // -----------------------------------------------------------------------
    //
    // 用户原话：「勾选框未勾选的时候框和文本做成黑色，勾选了两者都变成主题色」。
    // 所以这里**成对**地换色：未勾选 = 黑框 + 黑字（`neutralInkColor()`）；勾上 = 主题色
    // 的**实心**框 + 主题色的字（用 `Ink` 那一档写字，9.5:1，`Pure` 只有 1.56:1）。
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
               " border-radius: %6px; }"
               "QCheckBox::indicator:checked { background-color: %4; border-color: %2; }"
               "QCheckBox::indicator:disabled { background-color: %5; border-color: %3; }")
               .arg(black.name(), ink.name(), disabledInk.name(), pure.name(),
                    disabledFill.name())
               .arg(controlRadius());

    // -----------------------------------------------------------------------
    // 滚动条（2026-09-29）
    // -----------------------------------------------------------------------
    //
    // 用户原话：「每一次切页面的时候滚动条都是瞬间出现的，把滚动条变成一个嵌在页面边上的
    // 条，不要直接一个白色条」。
    //
    // 这一条一次说清三件事：
    //
    //   1. **没有底**（`background: transparent`）：Fusion 出厂那一条是"一格浅灰的槽",
    //      看着就是一根白条 —— 那是用户点名不要的。底色不画之后，滚动条那一条露出来的是
    //      **它自己底下那一层**（页面 / 卡片的底），于是把手看着是"嵌在页面边上"，
    //      而不是"旁边另开了一栏"。
    //   2. **两头的小箭头不要**（`add-line` / `sub-line` 给 0）：这一套界面里没有人点那个
    //      三角翻一格。★ 但 `add-page` / `sub-page`（在槽上点一下翻一屏）**必须留着** ——
    //      给它 0 就是把"点槽翻页"这件事一起删掉了，只是"不画"，不是"没有"。
    //   3. **把手是一个胶囊**：宽 = `scrollBarThickness() - 2 * scrollBarHandleMargin()`，
    //      圆角 = 半宽（`scrollBarHandleRadius()`），墨色跟着深浅各取一支带 alpha 的灰
    //      （`scrollHandleColor()` 那段讲了为什么必须跟着深浅走）。
    //
    // ★ `margin` 是这儿**唯一一处"留白"**：它把把手从滚动条那一条的两边各推进来
    //   `scrollBarHandleMargin()`，看着才是"嵌在边上的一条"而不是"顶满一整条"。
    // ★ 横的那一条**必须跟着一起写**：这一套界面里横向滚动条是少，但少不等于没有（表里
    //   某一列放不下就出来了）。只写竖的，横的那一条会**整条退回 Fusion 出厂样子** ——
    //   那时候屏幕上"竖的变了、横的没变"，比两处都不改还显眼。两段**同一个 `.arg()`**，
    //   就是不许它们漂开。
    const QString handle = rgbaCss(scrollHandleColor());
    const QString handleHover = rgbaCss(scrollHandleHoverColor());
    const QString handlePressed = rgbaCss(scrollHandlePressedColor());
    qss += QStringLiteral(
               "QScrollBar:vertical { background: transparent; width: %1px; margin: 0px; }"
               "QScrollBar::handle:vertical { background: %2; min-height: %3px;"
               " border-radius: %4px; margin: %5px; }"
               "QScrollBar::handle:vertical:hover { background: %6; }"
               "QScrollBar::handle:vertical:pressed { background: %7; }"
               "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
               "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
               " background: transparent; }"
               "QScrollBar:horizontal { background: transparent; height: %1px; margin: 0px; }"
               "QScrollBar::handle:horizontal { background: %2; min-width: %3px;"
               " border-radius: %4px; margin: %5px; }"
               "QScrollBar::handle:horizontal:hover { background: %6; }"
               "QScrollBar::handle:horizontal:pressed { background: %7; }"
               "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }"
               "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {"
               " background: transparent; }")
               .arg(QString::number(scrollBarThickness()), handle,
                    QString::number(scrollBarMinHandleLength()),
                    QString::number(scrollBarHandleRadius()),
                    QString::number(scrollBarHandleMargin()), handleHover, handlePressed);

    // -----------------------------------------------------------------------
    // 页面骨架那几层"面"（2026-10-12）
    // -----------------------------------------------------------------------
    //
    // 用户 2026-10-12 那份改版文档：整页只留**侧栏 + 正文**两个面，其余靠排版分层（不再
    // 用一堆 QGroupBox 套起来）。这两块的色**从调色板现问**（`QPalette::Base` / `Mid`）——
    // 换深浅主题时 `apply()` 会重挂一遍样式表（见 `installButtonSheet()`），这一份跟着重算。
    //
    // ★ 侧栏是"轻微底色"（正文色混主题色 6%）：一眼分得出"这一列是导航"，又不至于像一块
    //   独立的卡片。正文面就是 `Base`（今天那份卡片色）加一圈极细的边 —— 边**只有一档**
    //   （`Mid`），不是主题色：整页上一个面而已，边上再点一笔主题色就变成第三个强调点了。
    // ★ 小节卡片（`section`）比正文面**再淡一档**（6%），它装在正文面里面，"一块白压在白上"
    //   是看不见的（用户要的是"标题 + 内容卡片"，卡片得看得出来是一块）。
    // ★ 页脚（`footer`）**不铺底**、只画上面那条分隔线：它是正文面的一部分，铺底会把它切
    //   成"又一个面"。用户要的"固定底栏"是**位置**上的固定（钉在正文面底部），不是又一个盒子。
    // ★ 这几条**只管挂了这个属性的控件**（属性选择器），别处一个像素都不动。控件本身得是
    //   QFrame（`QLabel`/裸 `QWidget` 不铺样式表给的底色）。
    //
    // ★★ **这里故意没有**那份文档里给的这几条（如实记一笔，不是漏了）：
    //     `QListView[wtfmlNav="sidebar"]::item { min-height: 42px; padding: 0 12px;
    //      border-radius: 7px; }` / `::item:selected { ... border-left: 3px solid ...; }`
    //     —— 文档点名的两个"侧栏导航"（下载页左栏那六条分类、设置页左栏那五条）用的都是
    //     `VersionTreeDelegate`，**整行是代理自己画的**（底色、文字、行高全在 paint() 里），
    //     样式表里那几条 `::item` 规则一条都匹配不到那层底色：写上去就是死代码 ——
    //     挂在那里让人以为"侧栏选中样式配好了"，而实际画出来的仍然只有代理那一支。
    //     所以那一档落成了**代理里的 `navRow` 模式**（`new VersionTreeDelegate(..., true)`），
    //     色一个字节都没另写：`selectionFill()`（这一列自己的底色混 20% 主题色）、
    //     `selectionInk()`（主题色那一支里配套的那一档墨）、`selectionBar()` +
    //     `selectionBarThickness()`（左边那根 3 px 的指示条），三处都在 ui/Theme.hpp 里 ——
    //     从 2026-10-12 起**全仓的列表选中态都是这一套**（用户答复：「完全照 §9.3
    //     （和侧栏导航同款）」）。
    //     行高和圆角同理：那份列表的行高一直是 `versionTreeRowHeight()`（46 ≥ 文档要的 42），
    //     而代理画的是一整行**不带圆角**的方块 —— 多出来的圆角只能由代理画（今天没画）。
    {
        const QColor sheetBase =
            appColor(QPalette::Base).isValid() ? appColor(QPalette::Base) : QColor(Qt::white);
        const QColor sidebarFill = mixToward(sheetBase, accentShade(AccentShade::Pure), 6);
        // 小节卡片要能和正文面分得开，但仍然只是正文内部的一层。6% 在默认绿色下
        // 足以建立层级，又不会像旧式 GroupBox 那样每节都变成一个沉重的框。
        const QColor sectionFill = mixToward(sheetBase, accentShade(AccentShade::Pure), 6);
        const QColor sheetSecondary = appColor(QPalette::PlaceholderText);
        // ★ 2026-10-12（coding plan §9.1）：两个圆角各走 `surfaceRadius()`（8）/ `sectionRadius()`
        //   （10）—— **数没变**，变的是它从"样式表里写着 8"变成"令牌层说了算"。层级是
        //   小节卡片（10）> 主表面（8）> 普通动作（6）> 输入类（4），四档一路收小。
        qss += QStringLiteral(
                   "QFrame[%1=\"sidebar\"] { background-color: %2; border: none;"
                   " border-radius: %7px; }"
                   "QFrame[%1=\"workspace\"] { background-color: %3; border: none;"
                   " border-radius: %7px; }"
                   "QFrame[%1=\"section\"] { background-color: %4; border: none;"
                   " border-radius: %8px; }"
                   "QFrame[%1=\"footer\"] { background-color: transparent; border: none; }"
                   "QLabel[%5=\"secondary\"] { color: %6; }")
                   .arg(QString::fromLatin1(kSurfaceProperty), sidebarFill.name(),
                        sheetBase.name(), sectionFill.name(),
                        QString::fromLatin1(kTextRoleProperty), sheetSecondary.name())
                   .arg(surfaceRadius())
                   .arg(sectionRadius());
    }
    return qss;
}

QStyle *createStyle() {
    QStyle *style = new WtfmlProxyStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    // 名字是给 `ui_shot` 那行 `style: …` 看的 —— 那一行存在的意义就是"图是在哪个样式下画的
    // 必须能从产物本身看出来"。`QProxyStyle` 自己不设 `objectName`，不设的话那一行会印成
    // 空的，等于把这道哨兵悄悄撤了（`QStyleFactory::create()` 会给它造的那一份设成 "fusion"，
    // 所以我们这里接过来改个名，见 `Theme.hpp` 那段）。
    style->setObjectName(QStringLiteral("wtfml-fusion"));
    return style;
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
// 导入 / 导出
// ---------------------------------------------------------------------------

namespace {

/// JSON 里那几位字段名。**和 `QSettings` 里那套一一对应**（`preset` / `textOpacity` /
/// `accent` / `roles` / `parts` / `semantic`）—— 两个格式说的是同一件事，看哪一份都行。
constexpr const char *kJsonVersionKey = "wtfmlTheme";
constexpr int kJsonVersion = 1;

QColor colorFromJson(const QJsonValue &value) {
    // 两种写法都收：一个裸字符串（手写时最省事），或者 `{"color": "#…"}`（导出给的形状）。
    if (value.isString()) {
        return colorFromString(value.toString());
    }
    if (value.isObject()) {
        return colorFromString(value.toObject().value(QStringLiteral("color")).toString());
    }
    return QColor();
}

QJsonObject specFields(const Spec &spec) {
    QJsonObject out;
    out.insert(QStringLiteral("preset"), spec.presetId);
    out.insert(QStringLiteral("textOpacity"), spec.textOpacity);
    out.insert(QStringLiteral("accent"), colorToString(spec.accent));
    if (!spec.roles.isEmpty()) {
        QJsonObject roles;
        for (auto it = spec.roles.constBegin(); it != spec.roles.constEnd(); ++it) {
            roles.insert(roleKey(static_cast<Role>(it.key())), colorToString(it.value()));
        }
        out.insert(QStringLiteral("roles"), roles);
    }
    if (!spec.parts.isEmpty()) {
        QJsonObject parts;
        for (auto it = spec.parts.constBegin(); it != spec.parts.constEnd(); ++it) {
            const Part part = static_cast<Part>(it.key());
            const PartStyle &style = it.value();
            QJsonObject one;
            if (style.hasBackground) {
                one.insert(QStringLiteral("background"), colorToString(style.background));
            }
            if (style.hasForeground) {
                one.insert(QStringLiteral("foreground"), colorToString(style.foreground));
            }
            if (style.textOpacity >= 0) {
                one.insert(QStringLiteral("textOpacity"), style.textOpacity);
            }
            if (style.hasImage) {
                // ★ 写的是**导出这台机器上的那一条路径**（理由见 .hpp 里那段：图不在这份
                //   文件里）。换台机器导入之后那几张图找不到，由 `MainWindow` 数出来说一句。
                one.insert(QStringLiteral("image"), style.imagePath);
                one.insert(QStringLiteral("imageName"), style.imageName);
                one.insert(QStringLiteral("imageFit"), imageFitKey(style.fit));
                one.insert(QStringLiteral("imageOpacity"), style.imageOpacity);
            }
            parts.insert(partKey(part), one);
        }
        out.insert(QStringLiteral("parts"), parts);
    }
    QJsonObject semantic;
    if (spec.danger.isValid()) {
        semantic.insert(QStringLiteral("danger"), colorToString(spec.danger));
    }
    if (spec.warning.isValid()) {
        semantic.insert(QStringLiteral("warning"), colorToString(spec.warning));
    }
    if (spec.ok.isValid()) {
        semantic.insert(QStringLiteral("ok"), colorToString(spec.ok));
    }
    if (spec.errorText.isValid()) {
        semantic.insert(QStringLiteral("errorText"), colorToString(spec.errorText));
    }
    if (!semantic.isEmpty()) {
        out.insert(QStringLiteral("semantic"), semantic);
    }
    return out;
}

}  // namespace

QJsonObject specToJson(const Spec &spec) {
    const Spec clean = normalize(spec);
    QJsonObject out = specFields(clean);
    out.insert(QLatin1String(kJsonVersionKey), kJsonVersion);
    return out;
}

bool specFromJson(const QJsonObject &json, Spec *out, QString *error) {
    const auto fail = [error](const QString &why) {
        if (error != nullptr) {
            *error = why;
        }
        return false;
    };

    // 认得出来是一份主题文件吗 —— 判据是"里面至少有一样我们认识的东西"（有任意一项就收）。
    // 不要求那个版本号标记：手写一份只写 `{"accent": "#ff3d8068"}` 也该能用，而
    // "导入了别的文件"这件事仍然挡得住（那些人一个认识的字段都没有）。
    static const char *const fields[] = {"preset", "textOpacity", "accent", "roles", "parts",
                                         "semantic"};
    bool recognised = false;
    for (const char *field : fields) {
        if (json.contains(QLatin1String(field))) {
            recognised = true;
            break;
        }
    }
    if (!recognised) {
        return fail(QStringLiteral("里面认不出任何一项主题设置（没有 preset / accent / "
                                   "roles / parts / semantic 中的任何一条）"));
    }

    Spec spec = defaultSpec();  // 从出厂那一份出发：**缺的那几项就是出厂**，不是"没有"
    const QString preset = json.value(QStringLiteral("preset")).toString();
    if (!preset.isEmpty()) {
        spec.presetId = preset;  // 认不出的 id 由 normalize() 退回"跟着系统"
    }
    if (json.contains(QStringLiteral("textOpacity"))) {
        spec.textOpacity = json.value(QStringLiteral("textOpacity")).toInt(spec.textOpacity);
    }
    if (json.contains(QStringLiteral("accent"))) {
        const QColor accent = colorFromJson(json.value(QStringLiteral("accent")));
        if (accent.isValid()) {
            spec.accent = accent;
        }
    }

    const QJsonObject roles = json.value(QStringLiteral("roles")).toObject();
    for (auto it = roles.constBegin(); it != roles.constEnd(); ++it) {
        Role role;
        const QColor color = colorFromJson(it.value());
        // 认不出的名字（不是我们这十来个角色之一）静默丢掉：`normalize()` 也是这个路数，
        // 而且主题文件是能手改的，为一个错字把整份导入拒掉对用户没有好处。
        if (roleFromKey(it.key(), &role) && color.isValid()) {
            spec.roles.insert(static_cast<int>(role), color);
        }
    }

    const QJsonObject parts = json.value(QStringLiteral("parts")).toObject();
    for (auto it = parts.constBegin(); it != parts.constEnd(); ++it) {
        Part part;
        if (!partFromKey(it.key(), &part) || !it.value().isObject()) {
            continue;
        }
        const QJsonObject one = it.value().toObject();
        PartStyle style;
        style.background = colorFromJson(one.value(QStringLiteral("background")));
        style.hasBackground = style.background.isValid();
        style.foreground = colorFromJson(one.value(QStringLiteral("foreground")));
        style.hasForeground = style.foreground.isValid();
        if (one.contains(QStringLiteral("textOpacity"))) {
            style.textOpacity = one.value(QStringLiteral("textOpacity")).toInt(-1);
        }
        style.imagePath = one.value(QStringLiteral("image")).toString();
        style.hasImage = !style.imagePath.isEmpty();
        if (style.hasImage) {
            ImageFit fit = ImageFit::Cover;
            if (!imageFitFromKey(one.value(QStringLiteral("imageFit")).toString(), &fit)) {
                fit = ImageFit::Cover;  // 认不出的摆法退回默认那一种（和 normalize() 一样）
            }
            style.fit = fit;
            style.imageName = one.value(QStringLiteral("imageName")).toString();
            style.imageOpacity = one.contains(QStringLiteral("imageOpacity"))
                                     ? one.value(QStringLiteral("imageOpacity")).toInt(100)
                                     : 100;
        }
        spec.parts.insert(static_cast<int>(part), style);
    }

    const QJsonObject semantic = json.value(QStringLiteral("semantic")).toObject();
    spec.danger = colorFromJson(semantic.value(QStringLiteral("danger")));
    spec.warning = colorFromJson(semantic.value(QStringLiteral("warning")));
    spec.ok = colorFromJson(semantic.value(QStringLiteral("ok")));
    spec.errorText = colorFromJson(semantic.value(QStringLiteral("errorText")));

    if (out != nullptr) {
        *out = normalize(spec);  // 收拾干净 —— 和盘上读回来那份走同一道闸
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
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
