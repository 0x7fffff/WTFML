/**
 * @file Theme.hpp
 * @brief 把「界面上各个地方长什么样」从各自那几句 `darker()` 里收出来，做成一份**能整套
 * 换、也能一处一处换**的数据。
 * @note 要求：「各个地方的背景等允许切换，允许各个框架结构进行一个主题的
 * 切换（比如文本透明度等），先不急着做入口，把 ui 框架改成能高度个性化定义的状态」。
 *
 * 【这一层是什么】
 *
 * 一份 `Spec`（预设 + 覆盖项）描述"界面长什么样"，`apply()` 把它铺到整个应用上。用户能
 * 单独动的东西分三类，**每一类都必须说得出"它在画面上是哪儿"**：
 *
 * | 用户眼里的"地方" | 这一层怎么表达 |
 * |-----------------------------|------------------------------------------------------|
 * | 内容区那一块底 | `Role::Window` |
 * | 输入框 / 列表 / 卡片里那层底 | `Role::Base`（`AlternateBase` 是隔行那一档） |
 * | 选中 / 悬停那一块 | `Role::Highlight`（+ `HighlightedText`） |
 * | 正文 / 次要文字 / 提示文字 | `Role::WindowText` `Text` `PlaceholderText` + `textOpacity` |
 * | 顶上那一条 | `Part::TitleBar`（出厂 = **主题色那一支**，白字白记号）|
 * | 底下那一条 | `Part::BottomBar` |
 * | 压上来的浮层（面板 / 菜单） | `Part::Panel` |
 * | 卡片（整页 / 小提示） | `Part::Card` |
 * | 四周变暗那一层 | `Part::Scrim` |
 *
 * 为什么分成 Role 和 Part 两拨，而不是把十来处背景一律做成 Part：**Part 那一拨今天的
 * 底色全是从窗口底色推出来的**（`darker(105)` / `darker(112)` / `Base` / 一层半透明黑），
 * 调色板里没有它们的位置；而 Role 那一拨本来就有位置（改调色板就是改它们，界面里那些
 * 读 `palette()` 的地方**自动**跟着走，不用挨个去改）。硬把 Role 那拨也做成 Part，就等于
 * 同一件事存两份，早晚对不上。
 *
 * 【默认与兼容】
 *
 * `defaultSpec()` 使用翡翠实验室。海洋蓝和炭灰由相同的 Tokens 映射到调色板与 QSS。
 * 已存储的系统主题继续跟随系统；旧 light 标识迁移为 emerald，自定义覆盖仍优先。
 *
 * 【换主题那一刻会发生什么】
 *
 * `apply()` 把调色板铺到 `QApplication` 上，Qt 自己会把"调色板变了"发给每一个控件；那些
 * 把自己算出来的颜色**存了一份**的地方（QSS 里嵌死的色、自绘控件的悬停色、块状标记的
 * 位图）靠 `follow()` 跟着重算一遍 —— 它就是"下一次重算"这一个动作的登记处。没登记过的
 * 地方不是错的：每次现建的那些（浮层 / 菜单 / 列表行）下次打开自然就是新的。
 *
 * NOTE: **跟着系统**那一档（`presetId == "system"`）仍然是我们唯一不去钉死
 * `ColorScheme` 的一档：用户在 Windows 设置里切深浅，界面跟着变（`ColorSchemeWatcher`
 * 接住平台的 `colorSchemeChanged` 之后重铺一遍调色板）。调色板本身现在也由我们铺
 * —— 样式表里的颜色是 `palette(...)` 引用，不铺就没地方读那些档位。
 * 明确选了"浅色 / 深色"之后，我们就把那一档钉住 —— 那时候还跟着系统走，等于用户
 * 选了"深色"而界面是浅的。
 */

#pragma once

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QHash>
#include <QJsonObject>
#include <QPalette>
#include <QRect>
#include <QScrollArea>
#include <QScrollBar>
#include <QString>
#include <QStringList>
#include <QStyle>

#include <functional>
#include <utility>

class QPainter;
class QPushButton;
class QStyle;
class QWidget;

namespace Theme {

// 色号来自《WTFML_三主题配色规范》，组件共用，用户覆盖仍在 Spec 中保存。
struct Tokens {
    QColor primary, deep, primaryHover, primaryPressed;
    QColor page, main, card, subtle, hover, selected, input;
    QColor text, secondary, disabled, stroke, divider, focus, thumb, track;
    QColor logWindow, logToolbar, logBase, logHint, logBorder, logDivider;
};
Tokens tokens(const QString &presetId);
Tokens tokens();

// ---------------------------------------------------------------------------
// 能单独定义的那几块骨架
// ---------------------------------------------------------------------------

/// 界面上能**单独**换底色的那几块骨架（判据见文件头那张表）。
enum class Part {
    TitleBar,   ///< 顶上那一条（52 px）—— 出厂 = 主题色那一支（白字白记号，见文件头那张表）
    BottomBar,  ///< 底下那一条 —— 今天 = 窗口底色 darker(105)，和标题栏**不是**同一档（见上）
    Panel,      ///< 压在内容区上来的浮层：底栏那两块面板、行尾齿轮弹的那两列菜单 —— darker(112)
    Card,       ///< 覆盖层里那张卡片（整页 / 小提示）—— 今天 = `Base`
    Scrim,      ///< 提示框四周变暗那一层 —— 今天 = `rgba(0, 0, 0, 96)`
    /// 主页左栏那一块（「已安装版本」到「启动游戏」那一片）—— 今天 = 窗口底色（不额外压暗）。
    /// 用户点名要的「主页左侧自定义图像」说的就是它。
    HomeLeft,
    /// 四个页面（主页 / 实例 / 下载 / 设置）内容区那一整块底 —— 今天 = 窗口底色。
    /// 回答的是用户那句「其他页面的背景能换图吗」：一块管四页（换一页就得再加一格，
    /// 而那四个页面今天长得一模一样，分成四格只是让用户点四下）。
    PageBackground,
    Count,
};

/// 块数（循环遍历用）。
constexpr int partCount() { return static_cast<int>(Part::Count); }

/// 存盘用的键（"titleBar"）、给人看的名字（"标题栏"）。两者都只有这一处出处。
QString partKey(Part part);
QString partName(Part part);
/// 从存盘的键认回来；认不出返回 false（规格那一侧的兜底）。
bool partFromKey(const QString &key, Part *out);

/// 一张图摆进一块里的摆法。
enum class ImageFit {
    Cover,    ///< 铺满：按比例放大到盖住整块，多出来的裁掉（**默认** —— 不留空白）
    Contain,  ///< 完整放下：按比例缩到整块放得下，四周留出底色
    Stretch,  ///< 拉伸铺满：宽高各自拉满（**不保比例**，照片会变形）
    Tile,     ///< 平铺：原尺寸一张张重复，边缘那几张裁掉
    Count,
};

constexpr int imageFitCount() { return static_cast<int>(ImageFit::Count); }

/// 存盘用的键（"cover"）、给人看的名字（"铺满"）。两者都只有这一处出处。
QString imageFitKey(ImageFit fit);
QString imageFitName(ImageFit fit);
/// 从存盘的键认回来；认不出返回 false（规格那一侧的兜底）。
bool imageFitFromKey(const QString &key, ImageFit *out);

/// 一块上的样子。**没写的那几位 = 不覆盖**：调用方照旧用今天那一句算出来。
struct PartStyle {
    bool hasBackground = false;
    QColor background;
    bool hasForeground = false;
    QColor foreground;
    /// 这一块上文字的不透明度（0~100）；`-1` = 跟全局那一位。
    /// NOTE: `Scrim` 上这一位没有意义（它上面一个字都没有，见 `partForeground()`）。
    int textOpacity = -1;

    bool hasImage = false;
    /// 铺在这一块上的那张图（**绝对路径**）。空的 = 不铺图。
    ///
    /// NOTE: 用户挑的那张**不会**直接用：启动器会把它拷进数据目录
    /// （`<数据目录>/cache/theme/`，见 `src/util/ThemeImage.hpp`）再记这儿 ——
    /// 记用户自己那个路径的话，他自己把图挪走/删掉之后，界面上就是一块突兀的空白，
    /// 而且他多半已经忘了当初挑的是哪一张。
    QString imagePath;
    /// 用户挑的那张**原来叫什么**（挑图那一刻的 `QFileInfo(picked).fileName()`）。
    ///
    /// NOTE: 只有设了图时它才有意义 —— `normalize()` 在"没设图"那一档把它一起清掉。
    /// NOTE: 它可以为空（用户手改注册表、或者以后别的入口直接按路径设图）：那边一律退回
    /// 用 `imagePath` 的文件名，界面上不许因为少一位就写「现在铺的是：」加个空白。
    QString imageName;
    /// 怎么摆（见 `ImageFit`）。
    ImageFit fit = ImageFit::Cover;
    /// 图的不透明度（0~100）。**底下的底色照旧先铺一遍**，所以这一位调低 = 底色透上来
    /// —— 文字要读得清就是靠它（一张照片铺满、字全糊在上面是不能用的）。
    int imageOpacity = 100;
};

// ---------------------------------------------------------------------------
// 能单独定义的调色板角色
// ---------------------------------------------------------------------------

/// 能单独改的调色板角色。**不是把 `QPalette::ColorRole` 整个搬过来**：端出去三十来个
/// 名字（`BrightText`、`LinkVisited`……）只会让"能调什么"变成一份没人看得懂的清单，
/// 而这里每一项都得说得出它在画面上是哪儿（见文件头那张表）。
enum class Role {
    Window,          ///< 内容区那一块底
    Base,            ///< 输入框 / 列表 / 卡片里那层底
    AlternateBase,   ///< 隔行的另一档（表格交替行）
    Text,            ///< `Base` 上的字
    WindowText,      ///< `Window` 上的字（正文）
    Button,          ///< 按钮的底
    ButtonText,      ///< 按钮上的字
    Highlight,       ///< 选中 / 悬停那一块（浮层里的悬停是它加一层透明度）
    HighlightedText, ///< 选中那块上的字
    PlaceholderText, ///< 次要文字（两行式里的第二行、没填的提示）
    Mid,             ///< 分隔线 / 一圈细边
    Count,
};

constexpr int roleCount() { return static_cast<int>(Role::Count); }

QPalette::ColorRole qtRole(Role role);
QString roleKey(Role role);
QString roleName(Role role);
bool roleFromKey(const QString &key, Role *out);

// ---------------------------------------------------------------------------
// 主题色（用户 点名的那一支）
// ---------------------------------------------------------------------------

/**
 * @brief 主题色那一支的几档。`Spec::accent` 里存的是 `Pure` 那一档，**其余全是算出来的**。
 *
 * 为什么要分档，而不是把默认主题色到处写：同一个色在"顶栏 / 主按钮的大色块"和
 * "白底上一行小字"上不是一回事。纯色要兼顾白字，描边按钮的文字和细边则继续走更深
 * 的两档：既保留层级，也不会让小尺寸文字显得发虚。
 *
 * 这些档**不进 `Role`**（用户不能单独挑其中一个），但会作为样式表的"变量"写进调色板
 * 的几个通用角色（`Accent` / `Light` 等，见 `buildPalette()`）—— 换配色时只要换调色板，
 * 不必重挂整份应用样式表。
 */
enum class AccentShade {
    Pure,     ///< 用户点名的那个色 —— 顶栏的底、当前那一颗导航底下那条线
    Ink,      ///< 白底上写字的那个绿（按钮文本）= 纯色的 60% —— 对白底 9.5:1
    Border,   ///< 按钮那一圈边 = 纯色的 80% —— 对白底 6.7:1（WCAG 对"界面部件"那条线是 3:1）
    Hover,    ///< 按钮悬停时那层浅浅的底（主题色 12% 罩在白上）
    Pressed,  ///< 按钮按下时那层底（主题色 28% 罩在白上）
    Count,
};

constexpr int accentShadeCount() { return static_cast<int>(AccentShade::Count); }

/// 某一档现在的色。**永远有效**（`Spec::accent` 认不出时退回出厂那支绿）。
QColor accentShade(AccentShade shade);
/// 出厂那一支（用户 点名的那六个十六进制位）。**全仓唯一出处。**
QColor defaultAccentColor();

/**
 * @brief 往底色里混主题色多少（百分比）—— **行悬停那一层的唯一出处**。
 *
 * 12 不是随手挑的：它和按钮 `:hover` 那一档（`AccentShade::Hover`）**同一个数** ——
 * 界面上"鼠标停在这儿"只该有一种深浅（见 `rowHoverFill()` 那段）。
 * 验证工程读的就是这个常数，别在别处再抄一份。
 */
constexpr int rowHoverAccentPercent() { return 12; }

/**
 * @brief 行/条目**悬停**时铺在它自己那一圈里的底：从**这一行真正的底色**往主题色那头混
 * `rowHoverAccentPercent()`。
 *
 * 要求：「把所有悬停在版本/按钮上都加一个颜色加深的效果」。
 *
 * 为什么入参是底色、而不是直接回 `accentShade(AccentShade::Hover)`：按钮那一档是
 * **主题色罩在写上死的白上面**（按钮和输入框在这一套界面里一直当"白纸"处理，不跟系统
 * 深浅 —— 见 `widgetStyleSheet()` 里那段）。行不能照抄：深色预设下 `QPalette::Base` 是
 * `#323232`，罩一层近白会闪一大片。从**这一行自己的底色**出发，两边就都对：
 * **浅色下它和按钮那一档逐字节相等**（底色就是白，混法与 `mixWithWhite(pure, 12)` 完全
 * 同式），深色下也还是"底色加深一点"。
 *
 * 它**不进调色板**（不是 `Role`），理由和上面那几档一样：往调色板里塞东西等于把"跟着
 * 系统深浅走"钉死。
 */
QColor rowHoverFill(const QColor &base);

/**
 * @brief 「中性那一档」：从 @p base 往 @p ink 混 @p percent —— 界面里那些**既不是主题色、
 * 也不是正文**的灰都从它算。
 *
 * 和 `rowHoverFill()` 是**一对**（同一个式子，只是那一头混的是墨色而不是主题色）：
 * 悬停那一层要"底色带一点主题色"，中性面要"底色带一点墨"—— 两条都不能写死。
 * 混法必须和 `mixWithWhite()` **逐字同式**（`(底*(100-p) + 墨*p + 50) / 100`），
 * `percent` 就是"往墨那头走了几成"。
 *
 * 读者：日志窗那排开关的关态底（29）、它那圈边（34）、开态那个兜底灰（9）、行间那条
 * 分隔线（15，从**正文区**的 `Base` 出发）。
 */
QColor neutralFill(const QColor &base, const QColor &ink, int percent = 29);

// ---------------------------------------------------------------------------
// 列表 / 侧栏那一列「选中」的那一块（ 合成一份）
// ---------------------------------------------------------------------------
//
// 两块来历，现在是**同一档**：
// ① 要求「选中的时候那个蓝色的换成深灰色」，紧接着又补了半句
// 「再浅一点，#cccccc」—— 那一版是**一整块固定浅灰**（版本树、加载器树、资源表、
// 实例列表都铺它）；
// ② 用户 那份界面改版文档点名侧栏那一列导航的选中项是「浅主题色背景 +
// 左侧 3px 主题色指示条 + 主题色深色文字，而不是一整块深灰」。
//
// ** 起两档合并**（用户当天对这一条的答复：「完全照 §9.3（和侧栏导航同款）」，
// 也就是那份 `CODING_PLAN_FULL.md` 的 §9.3）：全仓每一处"选中一行"都是
// **由这一行自己的底色混出的低饱和浅主题色底 + 左边一根 3 px 主题色指示条 +
// 主题色那一支里的深墨**。原先那条"灰是'选中的一行数据'、导航是'你停在哪一栏'，
// 二者不该长一样"的理由**作废了** —— 用户要的就是两处一样。
//
// 底为什么从**这一行自己的底色**出发混，而不是写死一个浅绿：深色预设下
// `QPalette::Base` 是 `#323232`，罩一层近白会闪一大片（理由见 `rowHoverFill()` 那段）。
// 混法和 `rowHoverFill()` **逐字同式**（同一个 `mixToward()`），只差比例 —— 浅色下
// 底就是白，这一档和 `accentShade(AccentShade::Hover)` 只差那两个百分比。
//
// 混的比例比行悬停**大一档**（`rowHoverAccentPercent()` 是 12）：两者同色的话，
// 鼠标一移到当前那一项上"选中"和"指着"就分不出来了。

/// 选中那一行往主题色混多少（百分比，见上面那段）。
constexpr int selectionAccentPercent() { return 20; }

/// 选中那一行的底。入参是**这一行自己的底色**（`option.palette.color(QPalette::Base)`）；
/// 不传 = 白底（默认那身浅色主题上所有表的底色都是白）。
QColor selectionFill(const QColor &base = QColor(Qt::white));
/// 它上面那行字：主题色那一支里"白底上写字"的那一档（`AccentShade::Ink`，约 7.4:1）。
/// 深底（深色预设）上自动换成那一支里"深底上写字"的那一档 —— 见 .cpp 里那段。
QColor selectionInk(const QColor &base = QColor(Qt::white));
/// 它上面那行**次要**小字（版本树第二行那行小字 / 占位文字 / 行里那些小图标）：
/// 比正文浅一档，但压在这块底上仍然读得清。
QColor selectionSubInk(const QColor &base = QColor(Qt::white));
/// 左边那根指示条的色：主题色本色（`AccentShade::Pure`）—— 它和顶栏是同一个色，
/// "这一行被选中了"这件事只靠这一笔就说得清。
QColor selectionBar();
/// 那根指示条多宽（px）。
constexpr int selectionBarThickness() { return 3; }
/// 把那根指示条画在**画家坐标系 x = 0** 那一列上（竖直方向取 @p rowRect 的那一条）。
///
/// 唯一出处：样式那两条原语和代理都走这一句 —— 各自 `fillRect` 一个矩形的话，
/// "那条竖条从哪儿起、多宽"就有了第二个说法。x 恒取 0 是因为行在画家坐标系里的
/// 左边缘就是 0（横向滚动是烘进 `option.rect` 里的，不是平移画家）。
void paintSelectionRail(QPainter *painter, const QRect &rowRect);

/**
 * @brief 把"选中那一行画在浅主题色底上"这件事落到**一份能直接取色的调色板**上。
 *
 * @param palette 这一行原本那一份（`option.palette`）。
 * @param selected 这一行此刻是不是选中（`option.state & QStyle::State_Selected`）。
 *
 * 不选中时**原样返回**（一个字节都没动）。选中时返回一份拷贝，里面这几档换掉：
 * `WindowText` / `Text` / `HighlightedText` → `selectionInk(base)`，`PlaceholderText` →
 * `selectionSubInk(base)`，`Highlight` → `selectionFill(base)`（有几个地方按"这一行真正铺
 * 的那一层"重算文字对比度 —— 版本树上的 MOTD —— 读的就是 `Highlight`）。
 * 那几个 `base` 一律取这一份调色板里的 `QPalette::Base`（= 这一行自己的底色）。
 *
 * 为什么要这么一份：**手画正文/图标的代理有三个**（`VersionTreeDelegate` /
 * `InstanceGroupDelegate` / `GroupListDelegate`），它们各自的正文、小字、行尾那几颗按钮
 * 都是从这一份里取色的。底色不再问调色板要（是我们自己混出来那一块），取色那一处就得
 * 跟着换 —— 抄三遍那段"换哪几档"迟早会漏掉一档（漏的那一档在屏幕上就是一行看不出来的字）。
 * 样式那一半（`CE_ItemViewItem` 里印字的那些，比如实例行的正文）在
 * `Theme::createStyle()` 里读的也是这个函数 —— 两条路同一个出处。
 *
 * 用户把"文字透明度"调低过的话（`kInkRoles` 里那几位），换上去的墨**沿用原来那一档的
 * alpha**：选中那一行的字不该比别处更实。
 */
QPalette selectionPalette(const QPalette &palette, bool selected);

// ---------------------------------------------------------------------------
// 按钮
// ---------------------------------------------------------------------------

/**
 * @brief 一颗按钮的"身份"（决定它那圈边和字用哪一档色）。
 *
 * 挂在控件的动态属性上（`wtfmlButtonKind`）；**没挂的按 `Accent` 算** —— 界面上一百来颗
 * 按钮里绝大多数就是普通按钮，犯不着每建一颗都喊一遍。这个属性只影响颜色，不影响尺寸。
 *
 * **四档怎么分**（除 `Primary` 外三档来自用户 的要求，主动作档用于拉开层级）：
 * 「一些确认性的（如启动，下载，搜索）做成主题色，
 * 一些一般性的（设置，清空搜索）做成黑色，一些不可撤销（如修改存档，删除实例）做成红色」。
 * 落成一条能照着判的规矩：
 * - `Accent` —— **干这件事**：启动 / 下载 / 安装 / 搜索 / 登录 / 保存 / 确定 / 应用 /
 * 创建（新建、复制、添加、导入）/ 备份 / 还原。
 * - `Primary` —— 页面上唯一最重要的动作；今天只给主页的「启动游戏」使用实心主题色。
 * - `Neutral` —— **看一眼或者挪个地方**：取消 / 关闭 / 返回 / 打开文件夹 / 刷新 /
 * 清空搜索 / 切换 / 分组 / 更换图标 / 导出 / 浏览 / 那些"快捷方式" / 菜单项 /
 * 查询（检查更新 / 检查并修复 / 测速）。
 *
 * 「查询」原来列在 `Accent` 那一行（ 改）：那一档的说法是"盘上会不会
 * 变"，而"检查一下"这一步**只读不写** —— 按下去出现的是一个结果，东西一个字节都
 * 没动，和「刷新」是同一件事。实例概览里那三颗（检查更新 / 更新中心 / 检查并修复）
 * 原来没挂档、跟着默认吃了绿的 ⇒ 现在显式挂 `Neutral`（见
 * `InstanceSettingsDialog` 里那三处）。真正会写的那些（补全版本文件、创建快照、
 * 复制实例）仍然留在 `Accent`。
 * - `Ghost` —— **低频的辅助动作**：卡片里的一句"复制到剪贴板""查看报告"、行尾的
 * "换个样子"这类。它和 `Neutral` 是同一种判断的两档，分界线是**频率与分量**：
 * `Neutral` 是一颗"看得出是个按钮"的次要按钮（白底 + 一圈边），`Ghost` 只有字
 * （没有底、没有边），挪到边上不抢眼。**判不准就用 `Neutral`** —— Ghost 用滥了
 * 一整页会变成"一堆没有形状的字"。
 * - `Danger` —— **删了就拿不回来**：删除（实例 / 文件 / 账号 / 分组）/ 恢复默认分组设置 /
 * 还原上一次 / 清空日志 / 写进存档（「世界设置…」那一页的保存）。
 *
 * NOTE: 判不准的时候选 `Neutral`。把一颗普通按钮染成绿的，代价是"绿不再意味着该按"；
 * 少染一颗只是那颗按钮朴素一点。
 */
enum class ButtonKind {
    Accent,   ///< 确认性：淡主题色底 + 主题色文字（**默认**，不挂属性）
    Primary,  ///< 页面唯一主动作：主题色实底 + 白字（例如主页的「启动游戏」）
    Neutral,  ///< 一般性：浅中性色底 + 深色文字，默认不画边
    Ghost,    ///< 低频辅助：没有底、没有边，只有字（边留成透明的 1px，**几何不变**）
    Danger,   ///< 不可撤销：淡红底 + 红字，默认不画边
};

/// 属性名 + 属性值（`markButton()` 挂的就是这一对）；`QSS` 里那几条选择器也照着它写。
QString buttonKindKey(ButtonKind kind);
/// 给一颗按钮钉一个身份（**只改颜色**）。下一趟按钮样式重算时生效。
void markButton(QWidget *button, ButtonKind kind);
/// 「一般性」那一档的黑（次要按钮的字与边、未勾选的勾选框）。**唯一出处。**
QColor neutralInkColor();

/**
 * @brief 「灰掉」那一档的三个色（`QPushButton:disabled` 的底 / 字·边 / 边）。**唯一出处。**
 *
 * coding plan §3.3 点名要"单独设计 Disabled 状态：降低对比度但仍保留轮廓和文字可读性，
 * 不能让正常 Secondary 和 Disabled 看起来是同一档"。所以：
 * - **轮廓还在**（`disabledBorderColor()` 那 1px）—— 完全抹掉边的话，灰掉的一格在
 * 白底上就只剩几个灰字，"这儿本来有一颗按钮"这件事看不出来了；
 * - **字还读得清**（`disabledInkColor()` 对底 ≥ 3:1，见 ui_check 那条现算的判据）——
 * 再浅一档就纯是装饰了；
 * - 和 `Neutral`（白底 + 纯黑字 + 纯黑边）**差着一整档**，一眼分得开。
 */
QColor disabledInkColor();
QColor disabledBorderColor();
QColor disabledFillColor();

/**
 * @brief 键盘焦点环那一圈色（coding plan §8.3「可见焦点环」）。
 *
 * 深底上给浅主题色、浅底上给深主题色（`AccentShade::Hover` / `AccentShade::Ink`），
 * 判据就是 `isDarkPalette()` —— 和别处"深色下用亮的那一档"是同一个判断，别另立一条。
 *
 * 为什么**不算进** `AccentShade` 那几档：那几档是"同一个色在画面上深浅不同"，这一圈色
 * 是**跟着底翻的**（浅底用深的那档、深底用浅的那档），语义不是"第几档深浅"。
 *
 * 画它的地方只有一处：`Theme::createStyle()` 那层 `WtfmlProxyStyle` 里截
 * `PE_FrameFocusRect`（见 .cpp 里那段 —— 那是全应用唯一一个"每个控件都会走"的点）。
 */
QColor focusRingColor(const QPalette &palette);

/**
 * @brief 自绘进度条（`ui/TaskProgressBar`）那一根**轨道**的底色。**唯一出处。**
 *
 * 从中性那一支算（`neutralFill()`）：底色往墨色那头混一档，于是浅底给一支浅灰、深底给一支
 * 深灰 —— 和日志窗那排开关是同一个混法。**不写死一支灰**：底栏、任务面板、详情页三处的底
 * 各不一样，写死一支在另一种底上不是看不见就是一块疙瘩。
 */
QColor progressTrackColor(const QPalette &palette);

/**
 * @brief 自绘进度条**往前走的那一截**的色（`ui/TaskProgressBar` 的**唯一出处**）。
 *
 * 就是主题色本身：浅底下用它本色（`AccentShade::Pure`）、深底下用亮一档
 * （`AccentShade::Hover`）—— 判据和焦点环同一条（`isDarkPalette()`），别另立一条。
 */
QColor progressFillColor(const QPalette &palette);

/// 全应用那一份控件样式表（按钮 / 输入框 / 勾选框；`apply()` 铺的就是它）。
/// 要它是因为"这些东西长什么样"也得能量 —— 验证工程逐条读它算出来的色。
QString widgetStyleSheet();

// ---------------------------------------------------------------------------
// 「面」与「文字角色」那两个属性（ 那次界面改版）
// ---------------------------------------------------------------------------

/**
 * @brief 页面骨架那几个"面"与几档文字，各自由一个**动态属性**认领。
 *
 * 用户 那份改版文档把界面收成"侧栏 + 一个正文表面"（原文：「整个页面只保留
 * 侧栏和一个正文表面，正文内部靠排版、间距和少量小节卡片建立层级」），并直接给了形状：
 * `footer->setProperty("wtfmlSurface", "footer")`。这一组函数就是那两个属性名和那几个值
 * 的**唯一出处**：`widgetStyleSheet()` 里那几条选择器按它们写，产品代码按它们挂，
 * 验证工程也按它们找控件（`findChild<QFrame*>()` 那一族按 objectName 找，而面是**没有**
 * objectName 的 —— 一页里同样的面有好几块，名字只能是属性）。
 *
 * 两件事分开记：
 * · `wtfmlSurface` —— **一块面的底**（侧栏 / 正文 / 小节卡片 / 页面底栏）；
 * · `wtfmlTextRole` —— **一行字是什么身份**（页面标题 / 小节标题 / 次要说明）。
 *
 * 色**不在这儿**：面与字的颜色一律由 `widgetStyleSheet()` 按当前调色板现算
 * （见那几条 QSS 那句 `mixToward()` 的注释）—— 这里给的只是"叫什么"，不是"长什么样"。
 */
QString surfacePropertyName();
QString sidebarSurfaceValue();
QString workspaceSurfaceValue();
QString sectionSurfaceValue();
QString footerSurfaceValue();
QString textRolePropertyName();
QString pageTitleTextRoleValue();
QString sectionTitleTextRoleValue();
QString secondaryTextRoleValue();

// ---------------------------------------------------------------------------
// 尺寸令牌（，coding plan §9.1）
// ---------------------------------------------------------------------------
//
// 用户那份 coding plan 的 §9.1 点名了一条：「把页面间距、卡片圆角、侧栏宽度、页头高度、
// 按钮高度收进 `Theme`/布局规格层」，并且**不要单纯统一成一个数字；要统一成有限的 3–4 档
// 节奏**。于是全仓的"间距"只留三档：
//
// 页面外边距 20 ／ 区块间距 16 ／ 控件间距 8
//
// 圆角只留四档：控件 4 ／ 按钮 6 ／ 主表面 8 ／ 小节 10。
//
// 间距那三档的**取数处仍然是 `MainWindowUI`**（`pageMargin()` / `kColumnSpacing` /
// `kFilterRowSpacing`）—— 页面骨架整个搭在那儿，那是"布局规格层"；这里只收两位
// **样式表自己也要用**的数（动作高度两档 + 圆角），不然同一个数会在 QSS 字符串里再写
// 一遍（写了就会漂，而漂了只在屏幕上差几像素，谁都看不出来）。
//
// 动作高度这一档只管 `QPushButton`。输入框 / 下拉框 / 数字框**仍然是 24**（那是
// 用户点名加高的，另一条线，见 `widgetStyleSheet()` 里那段）—— 动作按钮
// 32、输入控件 24，这是两件事。
//
// 两处**故意不跟令牌**的（写在这儿免得以后被"顺手统一"掉）：
// * 主页那颗「启动游戏」是 76（`kStartButtonMinHeight`）—— 它是这一页唯一的主角；
// * 下载页的「安装」是 40（`MainWindowUI::installButtonMinHeight()`）—— 它的字号比
// 别处大 2pt（`installButtonFontStep()`），32 会显得挤。

/// 普通动作按钮（确定 / 取消 / 应用 / 全部那些）的最小高度。
constexpr int actionHeight() { return 34; }
/// "页面上唯一最重要的动作"那一档（`ButtonKind::Primary`），比普通动作略高一档。
constexpr int primaryActionHeight() { return 38; }

/// 这一位是**图标按钮**（齿轮 / 底栏面板上那几颗 / 详情页顶上的 chip）：它按自己钉死的
/// 边长来，不跟 `actionHeight()` 那一档。
QString iconButtonSizeValue();
/// 给一颗按钮钉成"图标按钮"（**只改尺寸那一档**，不动颜色）。和 `markButton()` 一样是
/// 挂一个动态属性，下一趟按钮样式重算时生效。
void markIconButton(QWidget *button);

// ---------------------------------------------------------------------------
// chip（可勾选的小按钮）
// ---------------------------------------------------------------------------

/**
 * @brief 一排 chip 的高度（px）—— **唯一出处**。
 *
 * 24 而不是 `actionHeight()`（32）：chip 是**筛选条件**，不是"按下去会发生一件事"的主
 * 行动按钮。它和一排里的输入框（`QLineEdit` 24px）同高，两者并排摆着才是"一行控件"；
 * 用它那一档高度的话，筛选行会比搜索框高出一截、看着像两件不搭界的东西。
 *
 * NOTE: 它同时是**资源详情页那排游戏版本 chip** 的高度（原来那个数字写在那儿）。
 */
constexpr int chipHeight() { return 24; }

/**
 * @brief chip 那颗按钮长什么样（QSS 字符串）。
 *
 * 一圈细边 + 透明底；鼠标悬停时淡淡一层 Highlight；**勾上的那颗换成 Highlight 实底 +
 * HighlightedText 的字**。颜色全从 @p palette 里取（不写死灰）—— 换主题、换深色，这一排
 * 跟着走。
 *
 * 为什么要单独一套 QSS：全局那一份按钮样式（`widgetStyleSheet()`）里**没有 `:checked`
 * 这一条** —— 那四档 kind 说的是"这颗按钮是干什么的"，而"哪一颗此刻按着"是筛选这一类
 * 控件自己的事。缺了它的症状很难自己发现：勾选状态**逻辑上是对的**（`isChecked()` 为真），
 * 只有屏幕上那颗按着的和没按的长得一模一样。
 * 调用方必须 `setCheckable(true)`：这套样式只描述"勾上什么样"，不负责让按钮能勾。
 */
QString chipStyleSheet(const QPalette &palette);

/**
 * @brief 把一颗按钮摆成 chip（高度 + 圆角外观 + 换主题重挂），**勾选状态自己摆**。
 *
 * 三件事一起做，因为少任何一件都会留下一个"看着像 chip、其实不是"的按钮：
 * ① `setFixedHeight(chipHeight())`；
 * ② `markIconButton()` —— 报一句"我按自己钉的那一档高度来"，否则全局那条
 * `min-height` 会把它顶到 32（口径见 `markIconButton()` 那段）；
 * ③ 挂 chip 样式，并接一条 `Theme::follow()`：QSS 里嵌的是**色名**，主题一换得重算一遍
 * （常驻的那些 chip 不会有人替它们重建）。
 */
void markChipButton(QPushButton *button);

// ---------------------------------------------------------------------------
// 色块（一颗按钮，面上就是它代表的那块色）
// ---------------------------------------------------------------------------
//
// 为什么是按钮而不是画上去的色块：它得能点、能拿到键盘焦点、能走 Tab。一颗 `QPushButton`
// 三样都是白给的，而"自己画一块"这三样就得各写一遍（焦点环那一圈还得自己接上
// `PE_FrameFocusRect`，见 `focusRingColor()`）。
// 面上那个色是**我们自己挂的样式表**（应用级那份只对"素"按钮生效，控件自己挂的赢），
// 所以它**不跟着调色板走**：换主题时块里那个色照旧是用户选的那个 —— 那正是它要表达的。

/**
 * @brief 色块那一圈边：**跟着这块色自己的明暗翻** —— 深色给白边、浅色给黑边。
 *
 * 判据取的是 `color.lightness() < 128`（和 `isDarkPalette()` 同一个式子）。没有这一圈，
 * 一块近黑的主题色搁在浅色页面上就是"一个没有形状的洞"。
 */
QColor swatchBorder(const QColor &color);

/**
 * @brief 把 @p button 摆成一块**色块**（面上就是 @p color）。
 *
 * 三件事：`markIconButton()`（报一句"我按自己钉死的边长来"，否则全局那条 `min-height`
 * 会把它顶到 32）、挂自己的样式表、把鼠标形状换成手形。
 *
 * @param button **调用方自己 `setFixedSize()`** —— 色块多大是各处的排版说了算
 * （设置页那一行 64×24、取色卡片里那张色板各 48×24），这一层不该再有一份尺寸表。
 * NOTE: 悬停 / 按下**不换底**（换底色 = 换掉了它要表达的那件事），只在边框上给一点反应。
 */
void markColorSwatch(QPushButton *button, const QColor &color);


/// 输入类控件（`QLineEdit` / `QComboBox` / `QAbstractSpinBox` / 勾选框那个框）的圆角。
constexpr int controlRadius() { return 4; }
/// `QPushButton` 的圆角（比输入类大一档：它比输入框矮胖，同一个圆角看着更方）。
constexpr int buttonRadius() { return 4; }
/// 主表面（`wtfmlSurface="workspace"` / `"sidebar"`）的圆角。
constexpr int surfaceRadius() { return 4; }
/// 小节卡片（`wtfmlSurface="section"`）的圆角。比主表面大一档：它是"面里的一层"。
constexpr int sectionRadius() { return 4; }

// ---------------------------------------------------------------------------
// 滚动条
// ---------------------------------------------------------------------------

/**
 * @brief 滚动条有多粗（逻辑像素）—— **唯一出处**。
 *
 * 要求：「把滚动条变成一个嵌在页面边上的条，不要直接一个白色条」。
 *
 * 12 是**比 Fusion 出厂值（`PM_ScrollBarExtent`）细一档**挑的：这一套界面里的滚动条
 * 一直在页面的最右边那条边线上，宽了看着像另开了一栏。细到 12 之后，真正看得见的东西
 * 只有中间那 6 px 的把手（见 `scrollBarHandleMargin()`），剩下的都是"条嵌在页面边上"
 * 那点留白。
 *
 * 这一位有三处读者：`widgetStyleSheet()` 里那两段 QSS（竖的给 `width`、横的给 `height`）、
 * 以及验证工程（量真控件的宽度 / 量把手的左右两边是不是各留了这么宽）。
 */
constexpr int scrollBarThickness() { return 10; }

/// 把手离滚动条自己那条边线的距离（左右各一份，横的那一档是上下各一份）。
/// 于是**看得见的把手宽 = `scrollBarThickness()` - 2 * 这一位**。
constexpr int scrollBarHandleMargin() { return 2; }

/// 把手两头的圆角：**就是可见宽度的一半**，因此始终是两头圆的胶囊。
/// 写成一算就出来的式子，免得两个数将来各自漂走（漂了只会"看着不那么圆"，不会报错）。
constexpr int scrollBarHandleRadius() { return (scrollBarThickness() - 2 * scrollBarHandleMargin()) / 2; }

/// 把手最短画多长。Fusion 自己也有一条（`PM_ScrollBarSliderMin`）；这一条是我们钉的，
/// 内容长到上千行时把手不至于短成一个小点。
constexpr int scrollBarMinHandleLength() { return 28; }

/**
 * @brief 右边常留一条"死区"的滚动区：宽度永远是 `scrollBarThickness()` 那一格。
 *
 * 要求：「上下拖拽窗口调整大小的时候文字会因为边上滚动条的出现和消失
 * 重排列，在那里设置一个死区不要让文本进去」。
 *
 * 普通 `QScrollArea` 的竖条是 `AsNeeded` 的：内容一高，条子就**从视口里**拿走
 * `scrollBarThickness()` 那么宽，里面那些换行的说明文字当场重排一遍；窗口再高一点、
 * 条子收回去，文字又跳回去。这里把条子改成**常驻**（`ScrollBarAlwaysOn`）：视口宽度
 * 从头到尾是同一个值，条子要么在那一格里画把手、要么那一格什么都不画 —— 就是那条
 * "死区"，文字不许进去。
 *
 * 只给"里面是换行文字"的那几层页面用（设置页每一栏、覆盖层整页、任务中心详情…）。
 * 条目视图（列表 / 树 / 表格）不要换：它们自己那一圈边线和选中块的几何都在别处钉着
 * （见 [[wtfml-scrollbar-look]]），多出来的这条留白只会让行里的列宽少一截。
 */
class GutterScrollArea : public QScrollArea {
public:
    explicit GutterScrollArea(QWidget *parent = nullptr) : QScrollArea(parent) {
        // 条子**常驻**（`AlwaysOn`），不是 `AsNeeded`：`QAbstractScrollArea` 只在条子
        // "露着"的时候才从视口里拿走那一格 —— 常驻 ⇒ 视口宽度从头到尾是同一个值，
        // 里面换行的文字永远不重排。装得下的时候那一格什么都不画（见 `syncIdle()`），
        // 就是用户要的那条"死区"。
        // 试过 `setViewportMargins()`：那个接口是给表头/页脚留位的，条子照样再从视口里
        // 拿一格 ⇒ 条子一露，文字反而**一次少两格**（量出来 230 → 220）。
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        QScrollBar *bar = verticalScrollBar();
        connect(bar, &QScrollBar::rangeChanged, this, [this] { syncIdle(); });
        syncIdle();
    }

private:
    /// 装得下（`minimum == maximum`）时把条子标成"空的"：QSS 那条 `[wtfmlIdle="true"]`
    /// 把把手画成透明 ⇒ 常驻的那一格看上去是一条留白，而不是一根永远拖不动的把手。
    void syncIdle() {
        QScrollBar *bar = verticalScrollBar();
        const bool idle = bar->maximum() <= bar->minimum();
        if (bar->property("wtfmlIdle").isValid() && bar->property("wtfmlIdle").toBool() == idle) {
            return;
        }
        bar->setProperty("wtfmlIdle", idle);
        // 动态属性是 QSS 选择器的一部分 ⇒ 改完**必须重新 polish** 一次，Qt 不会自己来看。
        bar->style()->unpolish(bar);
        bar->style()->polish(bar);
        bar->update();
    }
};

/**
 * @brief 把手平时的色。**带 alpha 的一支中性灰**，跟着深浅各取一边。
 *
 * 为什么不像按钮那样直接写死一支（比如 `#c0c0c0`）：滚动条底下那层是**页面自己的底**，
 * 它跟着主题深浅走（浅色 `#fcfcfc` / 深色 `#2b2b2b`）—— 写死一支灰，在另一种底上不是
 * 看不见就是一块黑疙瘩。所以底色深就给一支**浅**灰、底色浅就给一支**深**灰，
 * 两边都是"看得见但不抢眼"。
 *
 * 为什么带 alpha 而不是给一个实色：这一套界面里"把手底下那层"没有统一的一个色
 * （有的是页面的底、有的是白卡片），alpha 让它在任何一层上都还是"这层加深一点"，
 * 这正是 `rowHoverFill()` 那条规矩的同一种做法。
 *
 * 它**不进调色板**（不是 `Role`）—— 和悬停/选中那两档同一个理由：往调色板里塞东西
 * 等于把"跟着系统深浅走"钉死（见 `apply()` 里那段）。
 */
QColor scrollHandleColor();
/// 鼠标停在把手上（比平时实一档）。
QColor scrollHandleHoverColor();
/// 正拖着把手（最实的一档）。
QColor scrollHandlePressedColor();

/**
 * @brief 应用级样式（`QApplication::setStyle()` 用的那一份）：Fusion + 下拉框那个 `∨` 箭头。
 *
 * **产品与三个验证工程都从这儿拿**，谁也别再自己写 `QStyleFactory::create("Fusion")` ——
 * 「样式基线是同一个东西」这件事原来靠四处各写一行相同的代码，现在靠这一个函数。
 * 返回的对象**归调用方**（`QApplication::setStyle()` 会接过去）。
 */
QStyle *createStyle();

// ---------------------------------------------------------------------------
// 一份主题
// ---------------------------------------------------------------------------

/**
 * @brief 界面的样子。
 *
 * 语义色（危险 / 警告 / 正常 / 报错）**默认是空的** —— 空的意思不是"没有颜色"，而是
 * "按当前这一档深浅现推"（见 `dangerColor()`），于是深色主题下那处红自动变成浅红。
 * 只有用户在预设之外点名要一个色时才写进来。
 */
struct Spec {
    /// 预设 id（`presetIds()` 里那三个之一）。**故意没有 "custom"**：覆盖项是叠在预设
    /// 上面的，换了覆盖项预设还是那一个（"深色 + 我改的那几项"读起来比"自定义"准）。
    QString presetId = QStringLiteral("system");
    /// 全体文字的不透明度（0~100）。100 = 一点也不透（今天的样子）。
    int textOpacity = 100;
    /// **主题色**（那一支绿）。出厂 = `defaultAccentColor()`，永远有效（认不出退回出厂）。
    ///
    /// NOTE: 它**不是** `Theme/Accents/*` 底下那四个（那四个是"语义色"：危险 / 警告 /
    /// 正常 / 报错）。单数那个是"这一套界面是什么颜色"，复数那几个是"红黄绿各是
    /// 哪一个红黄绿"—— 注册表里单复数是两个前缀，别看串了。
    QColor accent;
    /// 覆盖的调色板角色：`int(Role)` → 色。
    QHash<int, QColor> roles;
    /// 覆盖的骨架：`int(Part)` → 那几项。
    QHash<int, PartStyle> parts;
    /// 语义色的覆盖项（无效 = 按深浅现推）。
    QColor danger;
    QColor warning;
    QColor ok;
    QColor errorText;
};

/// 整个应用当前那一份（**不是每个窗口一份**：主题是应用程序级的，见 .cpp 里那段 NOTE）。
const Spec &current();
/// 只换内存里这一份（不落盘、也不铺开 —— 要生效得再 `apply()`）。参数越界会被收拾干净。
void setSpec(const Spec &spec);

/// "出厂"那一份：翡翠实验室、一项覆盖都没有（**主题色除外** —— 它出厂就是那一支绿，
/// 用户点名要的）。
Spec defaultSpec();

/// 三个预设：`system`（跟着系统）/ `light` / `dark`。
QStringList presetIds();
QString presetName(const QString &id);
bool isPresetId(const QString &id);
/// 某一个预设的"出厂"那一份（覆盖项是空的）。认不出的 id 给 `defaultSpec()`。
Spec presetSpec(const QString &id);

/// 把 `spec` 收拾成一份合法的（认不出的 Role/Part 丢掉、不透明度钳到 0~100、色不合法
/// 的丢掉）。**盘上读回来的那一份必须过这一道**：注册表是能手改的。
Spec normalize(const Spec &spec);

// ---------------------------------------------------------------------------
// 铺开
// ---------------------------------------------------------------------------

/// 这一份调色板算不算深色。判据取的是**窗口底色的明度**（不是标志位）—— 用户把内容区
/// 调成黑的、预设却写着浅色的时候，跟着标志位走会让"深底上的深红"这种话成真。
/// NOTE: 这个判据原来是 `MainWindowUI::paletteIsDark()`，现在**只有这一处**，那边转调这儿。
bool isDarkPalette(const QPalette &palette);
/// 当前这一份算不算深色（拿应用调色板问上面那个函数）。
bool isDark();

/// 从 `base` 出发，叠上 `spec` 的角色覆盖与文字不透明度。
/// NOTE: 「跟着系统」那一档要先问平台现在是不是深色（`platformColorSchemeDark()`），
/// 所以严格说它会读一下 `qApp` 的 `styleHints()`；其余预设是纯函数。
QPalette buildPalette(const Spec &spec, const QPalette &base);

/**
 * @brief 把当前那一份铺到整个应用上。
 *
 * 三件事，按这个顺序：定深浅那一档 → 算调色板 → 铺开（并让该重算的重新算一遍）。
 *
 * NOTE: 「跟着系统」也会铺一份我们自己算的调色板；平台深浅变了由 `ColorSchemeWatcher`
 * 接住重铺（见 .cpp 里那段）。
 */
void apply(QApplication &app);

/// 把"该重算了"这一下敲给每一个控件（`QEvent::PaletteChange`）。
/// `apply()` 自己在"调色板没动、只有哪一块变了"那条路上会调它。
/// NOTE: 只该由 `apply()` 调 —— 从别处敲这一发等于凭空调一次重算。
void notifyWidgets();

// ---------------------------------------------------------------------------
// 每一块 / 每一个语义色：现在的颜色
// ---------------------------------------------------------------------------

/// 这一块现在的底色。**永远有效**：没覆盖过就返回今天那一份算出来的（见文件头那张表）。
QColor partBackground(Part part);

/// 这一块**自己**设过底色没有（没设过时 `partBackground()` 给的是"今天那一份"算出来的）。
bool partHasBackground(Part part);
/// 这一块现在的字色：没覆盖过返回 `fallback`（调用方自己那句）。
///
/// NOTE: 这一块**自己**那一位不透明度（`PartStyle::textOpacity`）在这儿生效；全局那一位
/// 落在调色板的文字角色上（`buildPalette()` 干的），而 `fallback` 多半就是从调色板
/// 取来的 —— 在这儿再乘一次就是乘了两遍。所以这里**只**用那一块自己的值。
QColor partForeground(Part part, const QColor &fallback);

/// 这一块的**出厂**字色是不是白的那一支（`partForeground()` 在没人覆盖过时给的就是它）。
///
/// 要它是给"次要文字"用的：标题栏的底现在是主题色那一支绿，而它上面"不是当前那一颗"
/// 的导航写的是**白墨压淡**（`strong` 加个 alpha），不是调色板里那个次要灰 —— 白底绿条
/// 上再用一个灰，那两个字就糊在一起了。调用方（`TitleBar::applyColors()`）靠这一位知道
/// "该走压淡，还是该走调色板那个次要色"。**只有标题栏是 true。**
bool partLightInk(Part part);

/// 这一块上铺的那张图：路径 + 怎么摆 + 多透。没设过时 `path` 是空的（`isSet()` 为假）。
///
/// NOTE: 这一层**不碰磁盘**（路径存不存在、解不解得开都不在这儿判）—— `ThemeBackground`
/// 画的时候读不到了就只铺底色，不报错也不弹窗。理由是那一步在**每一次重绘**上：
/// 一张存在 U 盘里、拔了盘的主题图不该让整个界面画不出来。
struct PartImage {
    QString path;
    /// 用户挑的那张**原来叫什么**（见 `PartStyle::imageName`）。可能是空串 —— 界面那一侧
    /// 拿它显示之前先看空不空，空的话退回落盘那个文件的名字。
    QString name;
    ImageFit fit = ImageFit::Cover;
    int opacity = 100;
    bool isSet() const { return !path.isEmpty(); }
};
PartImage partImage(Part part);

/// 提示框四周那一层。**就是 `partBackground(Part::Scrim)`** —— 单独留一个名字是因为
/// `OverlayHost::scrimColor()` 从这一层之前就存在（改的是它的实现，不是它的名字）。
QColor scrimColor();

/// 语义色。**参数的调色板决定没覆盖时是哪一档**（深底上用亮的、浅底上用深的），
/// 覆盖过就一律用覆盖的那一个（那时候用户说了算）。
QColor dangerColor(const QPalette &palette);
QColor warningColor(const QPalette &palette);
QColor okColor(const QPalette &palette);
/// 表单报错那处红（`installNameErrorColor()`）。和 `dangerColor()` **不是同一个色**：
/// 浅底那一档 #c0392b 和 #c02828 差一点点，各有各的对比度来历，别顺手合并。
QColor errorTextColor(const QPalette &palette);

// ---------------------------------------------------------------------------
// 落盘
// ---------------------------------------------------------------------------

/// 存到 `QSettings`（组织名/应用名在 `src/main.cpp` 里设的那一份，和别的设置同一个窝）。
void save(const Spec &spec);
/// 从 `QSettings` 读回来（读不出来 / 认不出的项一律退回出厂那一档，不报错也不猜）。
Spec load();

// ---------------------------------------------------------------------------
// 导入 / 导出（coding plan §8.3「提供…导入/导出主题」）
// ---------------------------------------------------------------------------
//
// 一份主题进得去、出得来。**换成 JSON 只是为了"用户能手看、能手改"**，字段和
// `QSettings` 里那套**一一对应**（`preset` / `textOpacity` / `accent` / `roles` /
// `parts` / `semantic`），所以看盘上那一份和看导出来的那一份是同一件事。
//
// 里头的**图片路径是导出这台机器上的那一条**（主题里存的就是路径）。换台机器导入之后
// 那几张图找不到 —— 这是诚实的：图**不在**这一份文件里（真要带上就得把几兆的位图塞进
// 一个 JSON）。`MainWindow` 导入之后会自己数一遍找不到的，并说一句（见那边）。

/// `Spec` → JSON（颜色一律 `#aarrggbb`，和 `QSettings` 里那套同一个写法）。
QJsonObject specToJson(const Spec &spec);
/// JSON → `Spec`。认不出的项一律按出厂那一档处理（走 `normalize()`）。
/// 失败（不是一份主题文件）时返回 false 并写 `*error`（参数可给 nullptr）。
bool specFromJson(const QJsonObject &json, Spec *out, QString *error);

// ---------------------------------------------------------------------------
// 跟着调色板重算
// ---------------------------------------------------------------------------

/// `follow()` 的实现体（模板里只有这一句转发，好让模板不依赖 `QWidget` 的完整定义）。
void followImpl(QWidget *owner, std::function<void()> fn);

/**
 * @brief 把"主题一变就重算一遍"登记在 @p owner 身上。
 *
 * QSS 里嵌死的颜色（`.arg(color.name())` 那种）和自绘控件存下来的那几份色**不会**自己
 * 跟着调色板走，得有人重算。这一发就是那个登记处：`fn` 在 owner 收到"调色板变了"时跑
 * 一遍，owner 没了这一条自己掉（parent 就是 owner）。
 *
 * NOTE: 【防自咬】`fn` 里多半就是 `setStyleSheet()` / `setPalette()`，而这两样各自会再发
 * 一次 `PaletteChange` —— 不拦的话就是"重算 → 又收到 → 再重算"。所以重算期间来的
 * 事件一律忽略（见 .cpp 里那个 `m_running`）。
 *
 * NOTE: 浮层 / 菜单 / 列表里那些**每次现建**的控件不用登记：它们下一次打开就是新的。
 * 要登记的是**常驻**的那几处（底栏那几颗按钮、头像、日志窗那五颗筛选）。
 */
template <typename F>
void follow(QWidget *owner, F &&fn) {
    if (owner == nullptr) {
        return;
    }
    followImpl(owner, std::function<void()>(std::forward<F>(fn)));
}

}  // namespace Theme
