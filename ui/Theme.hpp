/**
 * @file Theme.hpp
 * @brief 把「界面上各个地方长什么样」从各自那几句 `darker()` 里收出来，做成一份**能整套
 *        换、也能一处一处换**的数据。
 * @author 0x7fffff
 * @date 2026-09-28
 * @note 用户 2026-09-28 原话：「各个地方的背景等允许切换，允许各个框架结构进行一个主题的
 *       切换（比如文本透明度等），先不急着做入口，把 ui 框架改成能高度个性化定义的状态」。
 *
 * 【这一层是什么】
 *
 * 一份 `Spec`（预设 + 覆盖项）描述"界面长什么样"，`apply()` 把它铺到整个应用上。用户能
 * 单独动的东西分三类，**每一类都必须说得出"它在画面上是哪儿"**：
 *
 * | 用户眼里的"地方"            | 这一层怎么表达                                       |
 * |-----------------------------|------------------------------------------------------|
 * | 内容区那一块底              | `Role::Window`                                       |
 * | 输入框 / 列表 / 卡片里那层底 | `Role::Base`（`AlternateBase` 是隔行那一档）        |
 * | 选中 / 悬停那一块           | `Role::Highlight`（+ `HighlightedText`）            |
 * | 正文 / 次要文字 / 提示文字   | `Role::WindowText` `Text` `PlaceholderText` + `textOpacity` |
 * | 顶上那一条                  | `Part::TitleBar`                                     |
 * | 底下那一条                  | `Part::BottomBar`                                    |
 * | 压上来的浮层（面板 / 菜单）  | `Part::Panel`                                        |
 * | 卡片（整页 / 小提示）        | `Part::Card`                                         |
 * | 四周变暗那一层               | `Part::Scrim`                                        |
 *
 * ★ 为什么分成 Role 和 Part 两拨，而不是把十来处背景一律做成 Part：**Part 那一拨今天的
 *   底色全是从窗口底色推出来的**（`darker(105)` / `darker(112)` / `Base` / 一层半透明黑），
 *   调色板里没有它们的位置；而 Role 那一拨本来就有位置（改调色板就是改它们，界面里那些
 *   读 `palette()` 的地方**自动**跟着走，不用挨个去改）。硬把 Role 那拨也做成 Part，就等于
 *   同一件事存两份，早晚对不上。
 *
 * 【默认那一份 = 今天的样子，一个像素都不差】
 *
 * `defaultSpec()` 是"跟着系统 + 一项覆盖都没有"。`apply()` 在那种情况下**连调色板都不碰**
 * （见 .cpp 里那段 NOTE，以及 ui_check W1 那条断言）—— 这一层加进来是为了"以后能改"，
 * 不是"改了点什么"。所以它落在盘上、落在界面上都是零差异。
 *
 * 【换主题那一刻会发生什么】
 *
 * `apply()` 把调色板铺到 `QApplication` 上，Qt 自己会把"调色板变了"发给每一个控件；那些
 * 把自己算出来的颜色**存了一份**的地方（QSS 里嵌死的色、自绘控件的悬停色、块状标记的
 * 位图）靠 `follow()` 跟着重算一遍 —— 它就是"下一次重算"这一个动作的登记处。没登记过的
 * 地方不是错的：每次现建的那些（浮层 / 菜单 / 列表行）下次打开自然就是新的。
 *
 * NOTE: **跟着系统**那一档（`presetId == "system"`）是唯一"我们不去动 ColorScheme"的
 *       一档：用户在 Windows 设置里切深浅，界面跟着变（这是加这一层之前就有的行为）。
 *       明确选了"浅色 / 深色"之后，我们就把那一档钉住 —— 那时候还跟着系统走，等于用户
 *       选了"深色"而界面是浅的。
 */

#pragma once

#include <QApplication>
#include <QColor>
#include <QHash>
#include <QPalette>
#include <QString>
#include <QStringList>

#include <functional>
#include <utility>

class QWidget;

namespace Theme {

// ---------------------------------------------------------------------------
// 能单独定义的那几块骨架
// ---------------------------------------------------------------------------

/// 界面上能**单独**换底色的那几块骨架（判据见文件头那张表）。
enum class Part {
    TitleBar,   ///< 顶上那一条（44 px）—— 今天 = 窗口底色 darker(105)
    BottomBar,  ///< 底下那一条 —— 今天 = 窗口底色 darker(105)，和标题栏**同一档**
    Panel,      ///< 压在内容区上来的浮层：底栏那两块面板、行尾齿轮弹的那两列菜单 —— darker(112)
    Card,       ///< 覆盖层里那张卡片（整页 / 小提示）—— 今天 = `Base`
    Scrim,      ///< 提示框四周变暗那一层 —— 今天 = `rgba(0, 0, 0, 96)`
    /// 主页左栏那一块（「已安装版本」到「启动游戏」那一片）—— 今天 = 窗口底色（不额外压暗）。
    /// ★ 2026-09-28 用户点名要的「主页左侧自定义图像」说的就是它。
    HomeLeft,
    /// 四个页面（主页 / 实例 / 下载 / 设置）内容区那一整块底 —— 今天 = 窗口底色。
    /// ★ 回答的是用户那句「其他页面的背景能换图吗」：一块管四页（换一页就得再加一格，
    ///   而那四个页面今天长得一模一样，分成四格只是让用户点四下）。
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
    ///       （`<数据目录>/cache/theme/`，见 `src/util/ThemeImage.hpp`）再记这儿 ——
    ///       记用户自己那个路径的话，他自己把图挪走/删掉之后，界面上就是一块突兀的空白，
    ///       而且他多半已经忘了当初挑的是哪一张。
    QString imagePath;
    /// 用户挑的那张**原来叫什么**（挑图那一刻的 `QFileInfo(picked).fileName()`）。
    ///
    /// ★ 为什么非留这一位不可：拷进数据目录那一份的名字是**槽位名**（`homeLeft.jpg`），
    ///   用户看见那三个字等于什么都没看见 —— 设置页那一行「现在铺的是：…」要写的是
    ///   **他挑的那一张**（2026-09-28 首版写的是 `imagePath` 的文件名，ui_shot 的 31c
    ///   照出来就是「现在铺的是：homeLeft.jpg」，一眼就看得出不对）。源文件挪走/删掉都
    ///   不影响它（就是一个名字）。
    ///
    /// NOTE: 只有设了图时它才有意义 —— `normalize()` 在"没设图"那一档把它一起清掉。
    /// NOTE: 它可以为空（用户手改注册表、或者以后别的入口直接按路径设图）：那边一律退回
    ///       用 `imagePath` 的文件名，界面上不许因为少一位就写「现在铺的是：」加个空白。
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

/// "出厂"那一份：跟着系统、一项覆盖都没有 = **今天的样子**。
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

/// 从 `base` 出发，叠上 `spec` 的角色覆盖与文字不透明度。纯函数（不碰 `qApp`）。
QPalette buildPalette(const Spec &spec, const QPalette &base);

/**
 * @brief 把当前那一份铺到整个应用上。
 *
 * 三件事，按这个顺序：定深浅那一档 → 算调色板 → 铺开（并让该重算的重新算一遍）。
 *
 * NOTE: 默认那一份（跟系统 + 没有任何覆盖）下**一个字节都不碰** —— 不调 `setPalette`、
 *       也不去敲那些跟着调色板走的控件。见文件头那段。
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
///
/// ★ 为什么单留这一位：`Part::HomeLeft` / `Part::PageBackground` 今天的底**不是调色板里的
///   任何一个色** —— 主页左栏和四个页面都长在选项卡内容区上，那一层底是 `QTabWidget`
///   自己画的（实测 2026-09-28：Fusion 下是 `#fcfcfc`，而 `QPalette::Window` 是 `#f0f0f0`，
///   中间差着一档）。于是这两块在"什么都没设"时**一笔都不该画**：拿 `Window` 盖上去就是把
///   选项卡画的那一层顶掉，屏幕上是整块颜色悄悄变了一点 —— 判据就是这一位
///   （见 `ui/ThemeBackground.cpp` 里那段 return）。
bool partHasBackground(Part part);
/// 这一块现在的字色：没覆盖过返回 `fallback`（调用方自己那句）。
///
/// NOTE: 这一块**自己**那一位不透明度（`PartStyle::textOpacity`）在这儿生效；全局那一位
///       落在调色板的文字角色上（`buildPalette()` 干的），而 `fallback` 多半就是从调色板
///       取来的 —— 在这儿再乘一次就是乘了两遍。所以这里**只**用那一块自己的值。
QColor partForeground(Part part, const QColor &fallback);

/// 这一块上铺的那张图：路径 + 怎么摆 + 多透。没设过时 `path` 是空的（`isSet()` 为假）。
///
/// NOTE: 这一层**不碰磁盘**（路径存不存在、解不解得开都不在这儿判）—— `ThemeBackground`
///       画的时候读不到了就只铺底色，不报错也不弹窗。理由是那一步在**每一次重绘**上：
///       一张存在 U 盘里、拔了盘的主题图不该让整个界面画不出来。
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
 *       一次 `PaletteChange` —— 不拦的话就是"重算 → 又收到 → 再重算"。所以重算期间来的
 *       事件一律忽略（见 .cpp 里那个 `m_running`）。
 *
 * NOTE: 浮层 / 菜单 / 列表里那些**每次现建**的控件不用登记：它们下一次打开就是新的。
 *       要登记的是**常驻**的那几处（底栏那几颗按钮、头像、日志窗那五颗筛选）。
 */
template <typename F>
void follow(QWidget *owner, F &&fn) {
    if (owner == nullptr) {
        return;
    }
    followImpl(owner, std::function<void()>(std::forward<F>(fn)));
}

}  // namespace Theme
