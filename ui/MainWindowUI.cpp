/**
 * @file MainWindowUI.cpp
 * @brief 实现 UI 构建器类，实际创建主窗口的控件、布局并设置窗口属性。
 * @author 0x7fffff
 * @date 2026-04-11
 * @note 完成主窗口所有 UI 元素的初始化与排列，供 MainWindow 构造函数调用。
 */

#include "MainWindowUI.hpp"

#include "Animations.hpp"  // 换页 / 展开折叠 / 上拉面板那几段动效（2026-09-29）
#include "ElidedLabel.hpp"
#include "InstanceGroupDelegate.hpp"
#include "MemorySlider.hpp"
#include "OverlayHost.hpp"
#include "SkinModel.hpp"
#include "Theme.hpp"
#include "ThemeBackground.hpp"
#include "TitleBar.hpp"
#include "VersionIcons.hpp"
#include "VersionTreeDelegate.hpp"
#include "WindowChrome.hpp"

#include <QColor>
#include <QEvent>
#include <QFont>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QScreen>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

namespace {

/// 给 @p widget 挂一个"面"（`wtfmlSurface`）。名字与值都从 Theme 那一组**唯一出处**来，
/// 这里只是把 `const char *` 那一下收成一行（属性名要 `QByteArray` 活得比这一次调用长）。
void markSurface(QWidget *widget, const QString &value) {
    const QByteArray name = Theme::surfacePropertyName().toUtf8();
    widget->setProperty(name.constData(), value);
}

/// 给 @p label 挂一个"文字角色"（`wtfmlTextRole`）。同上。
void markTextRole(QWidget *label, const QString &value) {
    const QByteArray name = Theme::textRolePropertyName().toUtf8();
    label->setProperty(name.constData(), value);
}

/// 每个选项卡页面共用的边距：贴着选项卡内容区留一圈呼吸空间。
/// **值本身在 MainWindowUI::pageMargin()** —— 内容区那个覆盖层（OverlayHost）也是一整个
/// "页面"，它留白得和别的页面一样，所以那一边读的是同一个出处。
const int kPageMargin = MainWindowUI::pageMargin();

/// 左右两栏之间的间距（= 令牌里的"区块间距"那一档）。
///
/// ★ 2026-10-12（coding plan §9.1）从 12 到 **16**：令牌只留三档（页面 20 / 区块 16 /
///   控件 8），栏间距属于中间那一档。同一天 `InstanceSettingsDialog` 里那一份同名的
///   局部常量也跟着改成 16（两处说的是同一件事：两栏之间留多少）。
constexpr int kColumnSpacing = 16;

/// 宽屏下“选择/导航”侧栏不再按比例无限长大。默认 900 px 窗口里各栏还没碰到这些上限，
/// 所以原来的 7:3 / 3:7 / 2:8 比例完全保留；只有窗口明显拉宽后，多出来的空间才全部交给
/// 真正承载内容的那一栏。
constexpr int kNarrowSideMaxWidth = 260;
constexpr int kRichSideMaxWidth = 420;

// ---------- 各处手调的尺寸 ----------
// 集中放这里是为了"改外观只需看这一处"：这些数值是照着当前样式（Fusion）的控件度量
// 调出来的，换背景色/字体/控件高度时就是这一组要重估。散在各自的构建代码里每次都得
// 全文件搜一遍。
//
// NOTE: 设置页那个数字框的宽度（kMemorySpinWidth）不在这里 —— 它只服务
//       addInlineControl() 一个调用点，留在它旁边更好找。

/// 底栏整条的高度。给固定值：栏里三格的内容高度都是自己定的，跟着窗口高变没有意义。
constexpr int kBottomBarHeight = 62;

/// 进度文字那格（"已下载 1234 / 3580 xxx.jar"）的宽度下限，免得**文件名一换整条就左右跳**。
/// 它管的是底栏**中间那一格自己**的宽度，不是左右两格的 —— 左右那两格用的是
/// MainWindowUI::bottomBarRunStateColumnWidth() / bottomBarAccountColumnWidth()。
constexpr int kStatusLabelMinWidth = 260;

/// 左下角头像的边长（正方形）。**值本身不在这里** —— 它和"主窗口拿什么尺寸去裁头像"
/// 必须是同一个数，所以唯一的出处是 MainWindowUI::avatarSize()（见头文件）。
const int kAvatarSize = MainWindowUI::avatarSize();

/// 「启动游戏」按钮的高度。两行文字仍有足够呼吸空间，但不再占掉启动卡近一半高度。
constexpr int kStartButtonMinHeight = 76;

/// 主页左栏两张卡之间的竖直留白（「开始游戏」和「上次游玩」）。
///
/// ★ 不走 `layout->setSpacing()`：那只管"相邻两项之间"，而这一栏里还有两段 `addStretch`，
///   它们和卡片之间也会按 spacing 拉开 —— 那两段留白是这一栏"居中"的算法本身，动不得。
constexpr int kHomeCardGap = 14;

/// 「额外 JVM 参数」多行输入框的高度，约三四行的样子；再高会把下面那几项挤出可视区。
constexpr int kJvmArgsEditHeight = 72;

/// 几个按钮的宽度下限：文案长短差得多（「安装」/「应用」这种），
/// 不给下限就会各自缩成一小块、排在一起长短不齐。
constexpr int kDownloadButtonMinWidth = 96;
/// 「账号与外观…」那个入口按钮的最小宽度。文案固定，但给一个下限，免得以后换文案时
/// 按钮缩成一小块。**主页那个和设置页那个共用这一个值** —— 它们说的是同一件事。
constexpr int kAccountSkinButtonMinWidth = 140;
constexpr int kApplyJvmArgsButtonMinWidth = 72;
/// 下载页每一页顶上那一行筛子里，控件之间（以及那一行里两行之间）的间距
/// = 令牌里的"控件间距"那一档（见 `ui/Theme.hpp` 的尺寸令牌那一节）。
/// 比 kColumnSpacing（16）小一档：这一行里的东西是一组，挨着摆才像一组。
constexpr int kFilterRowSpacing = 8;

/// 侧栏那一列的宽度。**唯一出处**（`MainWindowUI::sidebarWidth()`）—— 用户 2026-10-12
/// 那份文档里点名的是 208~220 px，取中间那个整数。
///
/// NOTE: 它取代了"按份数分宽"（下载 2:8 / 实例 3:7）：导航那一列本来就不是"和正文抢宽度"
///       的东西，按比例分的话窗口一拉宽它也跟着长，多出来的全成了空白（这正是那份文档
///       里"左侧导航 / 右侧正文缺少主次关系"那条）。主页**不**走这一档 —— 它左边是"选版本
///       + 启动"，那是正文，仍然是 7:3 的份数。
const int kSidebarWidth = MainWindowUI::sidebarWidth();

/// 正文面（`wtfmlSurface="workspace"`）里那一圈内边距 = 令牌里的"区块间距"那一档。
///
/// 与页面外边距保持接近但略小：面内紧凑，面与窗口之间留出更明确的呼吸空间。
constexpr int kSurfacePadding = 18;

/// 页头两行之间的间距。
constexpr int kHeaderSpacing = 3;

/// 一栏：控件本身 + 它自己的竖排布局 + 这一栏那个"面"，右栏还多一个页头。
///
/// ★ 2026-10-12 起 `layout` 不再**必然**是 `widget` 自己的布局：正文那一栏是
///   `widget`（透明外框）→ 页头 → `surface`（"正文面"）→ `layout`。调用方照旧
///   `column.layout->addWidget(...)` 往里塞东西，塞进去的自然落在正文面里，
///   页头那一行永远在它上面 —— **这正是"页面标题 + 操作"该在的位置**（用户
///   2026-10-12 那份文档：`页面标题 / 页面说明 / 主内容区 / 底部操作`）。
///   侧栏那一栏没有页头：`surface == widget`、`layout` 就是它自己的布局。
struct Column {
    QWidget *widget = nullptr;
    QVBoxLayout *layout = nullptr;
    /// 这一栏那个"面"（侧栏是 `wtfmlSurface="sidebar"`，正文是 `"workspace"`）。
    QFrame *surface = nullptr;
    /// 页头（**只有正文那一栏有**，侧栏是 nullptr）。里面是标题 + 说明 + 右上角操作。
    QWidget *header = nullptr;
    /// 页头里那两行字。
    QLabel *title = nullptr;
    QLabel *subtitle = nullptr;
    /// 页头右上角那一条（页面级操作塞这儿）。
    QHBoxLayout *actions = nullptr;
};

/// 一个页面的左右两栏（= 用户 2026-10-12 那份文档里的统一页面骨架 `PageScaffold`）。
struct TwoColumns {
    Column left;
    Column right;
};

/// 主页两张常驻卡片：底色跟 Theme::Part::Card，既能压住用户自定义背景图保证文字可读，
/// 也和资源详情页、覆盖层里的卡片保持同一种视觉语言。
QFrame *makeHomeCard(QWidget *parent) {
    auto *card = new QFrame(parent);
    card->setFrameShape(QFrame::StyledPanel);
    card->setAutoFillBackground(true);
    const auto applyPalette = [card]() {
        QPalette palette = QApplication::palette();
        palette.setColor(QPalette::Window, Theme::partBackground(Theme::Part::Card));
        card->setPalette(palette);
    };
    applyPalette();
    Theme::follow(card, applyPalette);
    return card;
}

QLabel *makeHomeTitle(const QString &text, QWidget *parent) {
    auto *label = new QLabel(text, parent);
    QFont font = label->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() + 2.0);
    label->setFont(font);
    return label;
}

QLabel *makeHomeHint(const QString &text, QWidget *parent) {
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setForegroundRole(QPalette::PlaceholderText);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    return label;
}

/**
 * @brief 把页面搭成那套统一骨架（用户 2026-10-12 那份改版文档里的 `PageScaffold`）。
 * @param page 选项卡页面。
 * @param tab 页面代号（"home" / "instance" / "download" / "settings"），进 objectName，
 *            **也决定份数**。
 * @return 两栏的控件与布局；调用方往 `layout` 里塞东西即可。
 *
 * 【这套骨架长什么样】（用户文档里那张 ASCII）
 *
 *     ┌──────────────────────────────────────────────────┐
 *     │ 侧栏 216px │ 页面标题                    操作 │
 *     │            │ 页面说明 / 路径 / 当前状态        │
 *     │ 导航或列表 ├───────────────────────────────────┤
 *     │            │ 主内容区（一个"正文面"）           │
 *     └──────────────────────────────────────────────────┘
 *
 * 四条规则落在代码里就是：
 *   ① **页面本身不画边框**：整页只有两个面（侧栏 + 正文），别的层一律透明；
 *   ② **侧栏只用一个轻微底色**（`wtfmlSurface="sidebar"`）区分，不画矩形边框；
 *   ③ **正文只有一个面**（`wtfmlSurface="workspace"`）—— 原来那些 `QGroupBox` 全部
 *      让位于它，小节改走"标题 + 小节卡片"（`wtfmlSurface="section"`）；
 *   ④ **页面级操作永远在右上角**：塞进 `columns.right.actions`（页头那一行的最右），
 *      确认操作由调用方摆在自己的"底栏"上（`wtfmlSurface="footer"`）。
 *
 * 【哪些页走哪一档】主页那一栏是"选版本 + 启动"、那是**正文**，所以它不套侧栏面、
 * 也不固定宽度（仍然是 7:3 的份数，见 `leftColumnIsWider()`）；另外三页的左栏是
 * **导航**（一列分类 / 一列文件夹），走固定宽度的侧栏面。
 *
 * 【`layout` 落在哪儿】正文那一栏：`widget`（透明外框）→ 页头 → `surface`（正文面）→
 * `layout`。也就是说调用方 `columns.right.layout->addWidget(...)` 塞进去的东西**都在
 * 页头下面、正文面里面** —— 这正是文档里"主内容区"该在的位置。侧栏那一栏没有页头，
 * `surface == widget`、`layout` 就是它自己的布局。
 *
 * 【只带名字的栏才是验证工程要的】栏做成真控件而不是裸布局，是为了验证工程能按
 * objectName 找回来量几何；页头里那两行字与操作那一条也各有名字（见
 * `pageHeaderName()` / `pageTitleName()` / `pageSubtitleName()` / `pageActionsName()`）。
 */
TwoColumns buildTwoColumnPage(QWidget *page, const QString &tab) {
    auto *row = new QHBoxLayout(page);
    row->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);
    row->setSpacing(kColumnSpacing);

    const bool home = tab == QLatin1String("home");

    // 栏本身是个控件（不是裸布局），而且**主页那一条左栏要能铺图**：用户点名要的
    // 「主页左侧自定义图像」就是这一格（`Theme::Part::HomeLeft`，见 Theme.hpp）。
    //
    // NOTE: 栏本身在"没设图"时**什么都不画**（`ThemeBackground::paintEvent` 直接 return，
    //       见那个文件头），所以这一栏原来那个裸 `QWidget` 换成它**一个像素都不差** ——
    //       两者看到的都是底下那一层（页面 / 选项卡自己画的底）。
    const auto makeColumn = [page](const QString &name, bool themed) {
        Column column;
        column.widget = themed ? static_cast<QWidget *>(
                                     new ThemeBackground(Theme::Part::HomeLeft, page))
                               : new QWidget(page);
        column.widget->setObjectName(name);
        column.layout = new QVBoxLayout(column.widget);
        // 栏本身不留边距：页面那一层已经留过 kPageMargin 了，再留一层就是双层边距
        column.layout->setContentsMargins(0, 0, 0, 0);
        column.layout->setSpacing(10);
        return column;
    };

    TwoColumns columns;
    columns.left = makeColumn(MainWindowUI::leftColumnName(tab), home);
    columns.right = makeColumn(MainWindowUI::rightColumnName(tab), false);
    if (home) {
        columns.right.widget->setMaximumWidth(kRichSideMaxWidth);
    } else {
        // ---- 侧栏那一栏：固定宽度 + 一个轻微底色 ----
        //
        // ★ 固定宽度取代了"按份数分宽"（见 kSidebarWidth 那段）。
        // ★ 面是 QFrame 而不是裸 QWidget：样式表给的底色只对 QFrame 这一族生效
        //   （裸 QWidget 得自己画）。
        auto *sidebar = new QFrame(columns.left.widget);
        markSurface(sidebar, Theme::sidebarSurfaceValue());
        auto *sidebarLayout = new QVBoxLayout(sidebar);
        sidebarLayout->setContentsMargins(kSurfacePadding, kSurfacePadding, kSurfacePadding,
                                          kSurfacePadding);
        sidebarLayout->setSpacing(10);
        columns.left.surface = sidebar;
        // 侧栏里那些东西落在**面**里（侧栏没有页头，面就是这一栏的全部）
        columns.left.layout->addWidget(sidebar, 1);
        columns.left.layout = sidebarLayout;

        // ---- 正文那一栏：页头 + 一个"正文面" ----
        //
        // ★ 页头**在面外面**（面里只有内容）：标题和说明是"这一页是什么"，内容面是
        //   "这一页有什么" —— 把标题装进面里的话，那一圈边就同时框住了标题和内容，
        //   又回到了"框套框"的观感（用户文档第一条要减掉的就是它）。
        auto *header = new QWidget(columns.right.widget);
        header->setObjectName(MainWindowUI::pageHeaderName(tab));
        auto *headerLayout = new QVBoxLayout(header);
        headerLayout->setContentsMargins(kSurfacePadding, 2, kSurfacePadding, 0);
        headerLayout->setSpacing(kHeaderSpacing);

        auto *titleRow = new QHBoxLayout;
        titleRow->setSpacing(kColumnSpacing);
        columns.right.title = new QLabel(header);
        columns.right.title->setObjectName(MainWindowUI::pageTitleName(tab));
        markTextRole(columns.right.title, Theme::pageTitleTextRoleValue());
        {   // 标题的字：比正文大两档、加粗（和主页那几张卡片的标题同一档）。
            QFont titleFont = columns.right.title->font();
            titleFont.setBold(true);
            // 页面标题要比卡片标题再高一档。原来的 +2 在默认 900px 窗口里只比正文
            // 大一点点，右边有「刷新 / 分组设置」时尤其容易被按钮抢走视觉重心。
            titleFont.setPointSizeF(titleFont.pointSizeF() + 3.0);
            columns.right.title->setFont(titleFont);
        }
        titleRow->addWidget(columns.right.title);
        titleRow->addStretch(1);
        columns.right.actions = new QHBoxLayout;
        columns.right.actions->setSpacing(kFilterRowSpacing);
        titleRow->addLayout(columns.right.actions);
        headerLayout->addLayout(titleRow);

        columns.right.subtitle = new QLabel(header);
        columns.right.subtitle->setObjectName(MainWindowUI::pageSubtitleName(tab));
        markTextRole(columns.right.subtitle, Theme::secondaryTextRoleValue());
        columns.right.subtitle->setWordWrap(true);
        // 和主页那行说明同一条规矩：纯说明文字**不许**参与横向度量（折行 QLabel 报出来的
        // sizeHint 是"整句排成一行"的宽度，让它决定这一栏多宽是本末倒置）。
        columns.right.subtitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        columns.right.subtitle->setVisible(false);
        headerLayout->addWidget(columns.right.subtitle);

        auto *workspace = new QFrame(columns.right.widget);
        markSurface(workspace, Theme::workspaceSurfaceValue());
        auto *workspaceLayout = new QVBoxLayout(workspace);
        workspaceLayout->setContentsMargins(kSurfacePadding, kSurfacePadding, kSurfacePadding,
                                            kSurfacePadding);
        workspaceLayout->setSpacing(10);
        columns.right.surface = workspace;

        columns.right.header = header;
        columns.right.layout->addWidget(header);
        columns.right.layout->addWidget(workspace, 1);
        columns.right.layout = workspaceLayout;
    }
    // 主页那一栏左宽右窄，靠份数（见上面那段）；另几页的侧栏是**固定宽度**，
    // 份数给它 0（它自己那一位由 `setFixedWidth()` 定死），多出来的宽度全归正文那一栏。
    // NOTE: 这两句直接读 `leftColumnStretch(tab)`/`rightColumnStretch(tab)` —— 那一对
    //       函数**自己**就编码了"只有主页按份数、另三页是 0:1"（见头文件里那段），
    //       所以这里没有三元表达式，改份数还是只有那一处。
    row->addWidget(columns.left.widget, MainWindowUI::leftColumnStretch(tab));
    row->addWidget(columns.right.widget, MainWindowUI::rightColumnStretch(tab));
    if (!home) {
        columns.left.widget->setFixedWidth(kSidebarWidth);
        // ★ 尺寸策略也要一起钉（2026-10-12）：`setFixedWidth()` 只写死了 min/max，策略留在
        //   Preferred —— 屏幕上看着一样，但"这一栏跟不跟窗口长"这件事的判据是策略
        //   （按份数分宽那一版是 Preferred/Expanding，两者只在策略上分得开）。ui_check 那条
        //   "左栏的横向尺寸策略是 Fixed"量的是这一句；少它的话，将来谁把 setFixedWidth 挪走
        //   换成 setMinimumWidth，屏幕上要到某个窗口宽度才看得出走样。
        columns.left.widget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    }
    return columns;
}

/// 给正文那一栏的页头填标题与说明（`subtitle` 传空串就整行收起来，连位置都不占）。
void fillPageHeader(Column &column, const QString &title, const QString &subtitle) {
    if (column.title != nullptr) {
        column.title->setText(title);
    }
    if (column.subtitle != nullptr) {
        column.subtitle->setText(subtitle);
        column.subtitle->setVisible(!subtitle.isEmpty());
    }
}

/// 设置页里一个小节：**一个标题 + 一张内容卡片**。
///
/// ★ 2026-10-12：用户那份改版文档点名了这一条 —— 「小节使用"标题 + 内容卡片"，避免传统
///   `QGroupBox` 的缺口式标题」。于是设置页里原来那种「一行粗体标题横跨两列、下面接着
///   几行设置项」的排法换成了它：一节 = 一张 `wtfmlSurface="section"` 的卡片
///   （浅一档的底 + 10 px 圆角，**不画边**），标题和这一节所有的项**都在卡片里面**。
///
///   为什么原来那种排法要换掉：标题横跨整幅、项也横跨整幅，两者之间只有"挨着"这一层
///   关系 —— 一项到底是哪一节的，得靠"往上找最近那个粗体行"去猜；换成一节一张卡之后
///   那是**装在里面**的关系，一眼就是。顺带卡片自己还会把内容收进一圈内边距里
///   （18/17/18/18），"说明文字一行跨得太长"那件事也跟着好一档（另有 820 的宽度上限，
///   见 `settingsFormMaxWidth()`）。
///
/// 【为什么卡片进 grid 而不是各节自己竖着排】每一栏那一页仍然是一张两列的 `QGridLayout`
/// （`addSettingsPanel()` 里那对 3:7 的 `setColumnStretch` 还留着，ui_check 仍在核它），
/// 卡片按行跨两列塞进去。这样滚动区那一套（`scroll->widget() == form`）、栏与栏之间"页
/// 数 = 分类数"那条对应关系、每一页的名字一个都不用动。
struct SettingsSection {
    /// 这一节的卡片（`wtfmlSurface="section"`）。**它就是项们的父窗**。
    QFrame *card = nullptr;
    /// 卡片里那条竖排布局。项按顺序往里加（见 `addSettingItem()`）。
    QVBoxLayout *layout = nullptr;
};

/// 起一节（标题 + 一张空的内容卡片），并在 @p row 那一行把它摆进 @p grid。
/// @param row 按引用收：卡片占掉一行，函数自己推进（和原来 `addSectionTitle()` 里
///            `row++` 是一个规矩，只是现在忘不掉了）。
SettingsSection addSettingsSection(QGridLayout *grid, int &row, const QString &title,
                                   QWidget *parent) {
    SettingsSection section;
    section.card = new QFrame(parent);
    // 面挂在**属性**上（不是 objectName）：一页里这样的卡片有好几张，名字只能靠它认
    //（产品、样式表、验证工程三方共用这一对字符串，见 Theme::surfacePropertyName()）。
    markSurface(section.card, Theme::sectionSurfaceValue());
    auto *layout = new QVBoxLayout(section.card);
    // ★ 2026-10-12（coding plan §9.1）：内边距 18,17,18,18 → **16 四面**、行距 13 → **10**。
    //   两个数都落进令牌那三档（区块间距 16 / 控件间距 10）：卡片自己那一圈属于"区块"，
    //   卡里"行名 → 控件"那种成对的间距属于"控件"。改版那份文档给的 18/16/18/18 + 12
    //   被 plan 这份的三档节奏取代（两处都是"松紧"，只有断言能钉住 —— ui_check 的 K2 里
    //   那一条量的一直是这两个数）。
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);
    auto *titleLabel = new QLabel(title, section.card);
    markTextRole(titleLabel, Theme::sectionTitleTextRoleValue());
    {
        QFont titleFont = titleLabel->font();
        titleFont.setBold(true);
        titleFont.setPointSizeF(titleFont.pointSizeF() + 1.0);
        titleLabel->setFont(titleFont);
    }
    layout->addWidget(titleLabel);
    grid->addWidget(section.card, row, 0, 1, 2);
    ++row;
    section.layout = layout;
    return section;
}

/// 往一节里加一项：**行名独占一行、内容在下一行**（2026-09-26 用户原话「设置界面的项目和
/// 设置条目变成和实例设置一样的上下结构」—— 实例设置那边 2026-09-26 傍晚先改成了这样，
/// 见 InstanceSettingsDialog.cpp 里的 addTitledRow()）。
///
/// NOTE: 从前这里是「名字在左（右对齐）、控件在右」的两列行，名字那一列占 3 份宽。换成
///       上下结构之后**没有任何一行再用单独一列**了，所以 addSettingsPanel() 里那对
///       `setColumnStretch`（3:7）从此只剩下"表格的形状"这一层意思 —— 卡片跨两列，它里面
///       的宽度和列怎么分无关，留着它是为了两边的表格长得一样（ui_check 仍在核它）。
void addSettingItem(const SettingsSection &section, const QString &labelText, QWidget *control,
                    QWidget *parent) {
    QLabel *label = new QLabel(labelText, parent);
    label->setAlignment(Qt::AlignLeft);
    section.layout->addWidget(label);
    section.layout->addWidget(control);
}

/// 设置页里数字框的宽度（够写「65536 MB」再加一对上下箭头）。
constexpr int kMemorySpinWidth = 130;

/// 内存那根条的最短宽度。太短就拖不准：32 GB 的机器上那张档位表有五十多格，
/// 260 px 摊下来一格才 5 px 上下（够点得着），再窄就要靠键盘挪档了。
constexpr int kMemorySliderMinWidth = 260;

/// 「游戏窗口」那一行两个数字框的宽度：装得下「7680」（上限）加一对箭头就够了 ——
/// 它没有后缀，比 kMemorySpinWidth 窄一些才不像两个空盒子。
constexpr int kWindowSizeSpinWidth = 90;

/// 「游戏窗口」那一行中间那个「×」。
constexpr int kWindowSizeRowSpacing = 8;

/// 「外观」那一行那个文本框的宽。装得下 `titleTextMaxLength()` 个字（16 个汉字约 224 px）
/// 就够 —— 再宽就是在暗示"这里可以写很长"，而条上那一格是量的。
constexpr int kTitleTextEditWidth = 220;

/// 给设置页里的一行控件套一层"控件 + 说明文字"的容器（说明统一小一号、灰色）。
/// @return 容器；调用方再往它的 layout 里塞真控件 —— 说明文字在**最后**加，
///         所以这里只把空布局还回去。
QWidget *makeHintedControl(QWidget *parent) {
    QWidget *box = new QWidget(parent);
    QVBoxLayout *layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    return box;
}

/// 往 makeHintedControl() 造出来的容器里加一句说明，并把标签还回去。
/// 文字会随情况变的那几句用它（比如「内存分配」下面那句要说系统现在有多少内存）。
QLabel *addDynamicHint(QWidget *box, const QString &text, QWidget *parent) {
    QLabel *hint = new QLabel(text, parent);
    hint->setWordWrap(true);
    QFont hintFont = hint->font();
    hintFont.setPointSize(qMax(1, hintFont.pointSize() - 1));
    hint->setFont(hintFont);
    QPalette hintPalette = hint->palette();
    hintPalette.setColor(QPalette::WindowText, hintPalette.color(QPalette::PlaceholderText));
    hint->setPalette(hintPalette);
    qobject_cast<QVBoxLayout *>(box->layout())->addWidget(hint);
    return hint;
}

/// 和 addDynamicHint() 同一个形状，但字是**红的** —— 名字框底下那行"名字不能用"的原因
/// 用的就是它（大小、换行、摆放都和旁边那行灰字一套）。
QLabel *addDynamicErrorHint(QWidget *box, QWidget *parent) {
    QLabel *hint = addDynamicHint(box, QString(), parent);
    QPalette errorPalette = hint->palette();
    // 拿的正是这个标签当下这块调色板（深色主题下它就是深的那一块），所以下面那行红字
    // 自己会跟着主题换档 —— 不用再去问"现在是什么主题"。
    errorPalette.setColor(QPalette::WindowText,
                          MainWindowUI::installNameErrorColor(errorPalette));
    hint->setPalette(errorPalette);
    return hint;
}

/// 往 makeHintedControl() 造出来的容器里加一句说明。
void addHint(QWidget *box, const QString &text, QWidget *parent) {
    addDynamicHint(box, text, parent);
}

}  // namespace

MainWindowUI::MainWindowUI(QMainWindow *parent) { setupUi(parent); }

const MainWindowUI::ThemeImageRow &MainWindowUI::getThemeImageRow(int index) const {
    // 越界**不崩**：那几行的下标是调用方自己数出来的（今天一共两行），而这一层在
    // 构造之后才被问到 —— 给一个空行（指针全是 nullptr）比断言更好，调用方本来就要判空
    // （见头文件那句"调用方自己判空"）。
    static const ThemeImageRow kEmpty;
    if (index < 0 || index >= m_themeImageRows.size()) {
        return kEmpty;
    }
    return m_themeImageRows.at(index);
}

QString MainWindowUI::runningDurationText(qint64 startedAtMs, qint64 nowMs) {
    // 负数（时钟被往回调过、或者 startedAtMs 是个没写过的 0）当"刚开始"：这一行是个说明，
    // 不该出现"已运行 -3 分钟"这种算术漏出来的话。
    const qint64 seconds = qMax<qint64>(0, (nowMs - startedAtMs) / 1000);
    if (seconds < 60) {
        return QCoreApplication::translate("MainWindowUI", "已运行不到 1 分钟");
    }
    const qint64 minutes = seconds / 60;
    if (minutes < 60) {
        return QCoreApplication::translate("MainWindowUI", "已运行 %1 分钟").arg(minutes);
    }
    const qint64 hours = minutes / 60;
    if (hours < 24) {
        const qint64 rest = minutes % 60;
        // 整点就不缀那半句："已运行 2 小时 0 分钟"读起来是台机器写的。
        return rest == 0 ? QCoreApplication::translate("MainWindowUI", "已运行 %1 小时").arg(hours)
                         : QCoreApplication::translate("MainWindowUI", "已运行 %1 小时 %2 分钟")
                               .arg(hours)
                               .arg(rest);
    }
    const qint64 days = hours / 24;
    // 再往上一档（"N 个月"）没有意义：一个实例连着跑好几天已经是要不要收掉的问题了，
    // 具体几天几小时够用。
    return QCoreApplication::translate("MainWindowUI", "已运行 %1 天 %2 小时")
        .arg(days)
        .arg(hours % 24);
}

bool MainWindowUI::isHomeTabCurrent() const {
    // 主页恒定是第一页（下面 addTab 的顺序就是契约），所以比下标就够，
    // 不用再存一份页面指针。
    return tabWidget != nullptr && tabWidget->currentIndex() == homeTabIndex();
}

void MainWindowUI::setupUi(QMainWindow *parent) {
    parent->setWindowTitle(QStringLiteral("WTF Minecraft Launcher"));
    parent->resize(900, 620);

    parent->setWindowFlags(parent->windowFlags() & ~Qt::WindowMaximizeButtonHint);  // 禁用最大化按钮

    QWidget *centralWidget = new QWidget(parent);
    parent->setCentralWidget(centralWidget);

    // NOTE: 边距和间距都是 0 —— 选项卡栏和底部状态条要**通栏**贴到窗口边缘
    //       （Steam 那条底栏就是通栏的）。页面内部自己留边距，见 kPageMargin。
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ================= 顶上那条自己画的标题栏 =================
    // 系统那条白色标题栏已经由 WindowChrome 去掉了（客户区铺满整窗，见它的文件头），
    // 顶上这一行就是替它的：左边草方块 + 启动器名字（默认是 `TitleBar::defaultBrandText()`
    // 那五个字母，设置页「个性化」栏的「外观」里可以改），
    // 然后是**四个功能的导航按钮**（下面 addTab 完之后 setNavItems() 灌进去），
    // 右边最小化与关闭。第一行就是它、第二行才是标签页 —— 顺序由 addWidget 的先后决定。
    //
    // NOTE: 覆盖层（OverlayHost）挂在 tabWidget 上、条在 tabWidget 上面那一行，所以开设置页
    //       时这条**不会被盖住** —— 和"底下那一条不用覆盖"是同一条规矩；用户原话里那句
    //       「顶上四个选项卡也不覆盖」现在由"四个功能在条上、条不被覆盖"接手。
    bar = new TitleBar(centralWidget);
    mainLayout->addWidget(bar);
    // 条只发信号、不自己动手（见 TitleBar.hpp）：谁接这两下是这一层的事。
    // NOTE: 这个类**不是 QObject**（没有 tr()、没有自己的信号槽），所以得写全 `QObject::connect`。
    QObject::connect(bar, &TitleBar::minimizeRequested, parent, &QMainWindow::showMinimized);
    QObject::connect(bar, &TitleBar::closeRequested, parent, &QMainWindow::close);

    // ================= 选项卡：主页 / 实例 / 下载 / 设置 =================
    // 四页各自只放自己那摊控件，都排成左右两栏：左栏是要操作的那点东西
    // （选版本、按钮），右栏是列表或一整摊设置项。份数按页不同，见 leftColumnIsWider()。
    // 注意"选版本"这件事现在分在两个地方：「已安装版本」那个**下拉框**在主页、只管启动；
    // 下载页上不再有下拉框了，是**一棵版本树**（点分类展开、点一个版本号进安装页装它）。
    // 两边本来就是互不相干的数据源（一个是盘上装好的、一个是清单里能下的），
    // 以前并排放着只是因为都叫"版本"。
    tabWidget = new QTabWidget(centralWidget);
    tabWidget->setObjectName(tabWidgetName());

    // ---------- 主页：左 = 挑一个已装好的版本，启动；右 = 我的角色（皮肤预览） ----------
    //
    // 四个页面（这一处和下面三处）都是 `ThemeBackground`：用户 2026-09-28 要的「其他页面的
    // 背景能换图吗」说的就是这一块（`Theme::Part::PageBackground`）。没设图时它画的就是
    // 这一页原来那个底色，一个像素都不差（见 ui/ThemeBackground.hpp 的文件头）。
    QWidget *homeTab = new ThemeBackground(Theme::Part::PageBackground, tabWidget);
    homeTab->setObjectName(homeTabName());
    const TwoColumns home = buildTwoColumnPage(homeTab, QStringLiteral("home"));
    {
        auto *launchCard = makeHomeCard(home.left.widget);
        launchCard->setObjectName(QStringLiteral("homeLaunchCard"));
        launchCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto *launchLayout = new QVBoxLayout(launchCard);
        launchLayout->setContentsMargins(22, 20, 22, 22);
        launchLayout->setSpacing(10);

        launchLayout->addWidget(makeHomeTitle(QStringLiteral("开始游戏"), launchCard));
        launchLayout->addWidget(
            makeHomeHint(QStringLiteral("选择一个已安装实例，准备好后即可启动。"), launchCard));
        launchLayout->addSpacing(4);
        launchLayout->addWidget(new QLabel(QStringLiteral("游戏实例"), launchCard));

        installedVersionCombo = new QComboBox(launchCard);

        // 空状态用"没选中任何项(currentIndex() == -1) + placeholderText"表示，
        // 不再往列表里塞 "自定义版本" 那种假条目：控件自己会把占位文案画成灰字。
        // NOTE: 只要占位文案非空，往空下拉框里 addItems() 就不会自动选中第 0 项
        // （QComboBoxPrivate::rowsInserted 里那条 setCurrentIndex(0) 带了
        //  !placeholderText.isEmpty() 的条件）。所以"空着"能一直保持到用户真的挑一项，
        // 代价是刷新之后必须自己把选中项捡回来，见 MainWindow.cpp 里的 fillVersionCombo()
        // （它是那个文件匿名命名空间里的自由函数，不是 MainWindow 的成员）。
        // 这里只是初始值；灌完数据后由 MainWindow 在两个串之间切换。
        installedVersionCombo->setPlaceholderText(noInstalledVersionPlaceholder());

        // 版本号长度不定（26.2-snapshot-1 之类），默认的 AdjustToContentsOnFirstShow 会在
        // 第一次显示（那时还是空的）时定一个宽度然后一直沿用，之后灌进上千个长版本号就容易
        // 把窗口顶宽。改成按"最短内容长度"算，宽度确定，灌数据时也不会跳。
        //
        // NOTE: "宽度确定"有个前提，很容易踩：**占位文案一旦超过 minimumContentsLength
        //       就会被算进 sizeHint**。实测（Qt 6.10.1，Fusion）13 字的占位会把框顶宽
        //       12px，而**条目文本和当前选中文本不影响**（空 / 加了一行超长条目 / 选中
        //       那行超长条目，量出来都是同一个宽度）。
        //       所以：给这两个下拉框设的每一条占位文案都必须 ≤12 字。现在共四处 ——
        //       本文件两处 + MainWindow.cpp 两处（那句取数失败的文案原来是 16 字，会让
        //       框在失败时突然变宽，2026-09-21 缩成 10 字）。要放长句就写到旁边的 hint
        //       标签上去，QLabel 没有这个约束。
        installedVersionCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        installedVersionCombo->setMinimumContentsLength(12);
        // 滚轮防护：见 MainWindowUI.hpp 里的 WheelGuard
        WheelGuard::install(installedVersionCombo);
        installedVersionCombo->setObjectName(installedVersionComboName());
        launchLayout->addWidget(installedVersionCombo);

        startButton = new QPushButton(QStringLiteral("启动游戏"), launchCard);
        // 文字是两行（"启动游戏\n<显示名>"，由 MainWindow::updateStartButtonText() 维护；
        // 高度固定给足，免得两行被压成一行半。
        startButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        startButton->setMinimumHeight(kStartButtonMinHeight);
        QFont startFont = startButton->font();
        startFont.setBold(true);
        startFont.setPointSizeF(startFont.pointSizeF() + 1.0);
        startButton->setFont(startFont);
        Theme::markButton(startButton, Theme::ButtonKind::Primary);
        launchLayout->addWidget(startButton);
        launchLayout->addWidget(
            makeHomeHint(QStringLiteral("实例与游戏文件可在「实例」页中管理。"), launchCard));

        // 这里原来有一个「离线模式」勾选框（2026-09-24 去掉的，用户原话：「现在不要启动
        // 界面那个离线登录勾选框了，在添加账户那里选择添加离线账号」）。它管的是"这次启动
        // 用哪种身份"，而现在那件事**只由当前账号是什么**决定 —— 离线就去账号窗里加一个
        // 离线账号、切过去，没有第二个开关。判据的唯一出处是 MainWindow::offlineIdentity()。

        // ---------- 主页左栏第二张卡：「上次游玩」（coding plan §5.1）----------
        //
        // §9.5 说主页只留三件事：启动、**最近活动**、账号与外观。上面那张是启动、右栏那张是
        // 账号与外观，这一张就是中间那件。**它不是仪表盘**（§5.1 第一句）：只有一行"上次是
        // 哪一份、什么时候、玩到哪儿"，加上两颗按钮，没有统计图、没有列表。
        //
        // ★ 一次都没启动过时**整张卡收起来**（`setHomeRecent()` 里 hide）：主页面不留一张
        //   空的卡片等数据，§9.2 那条"不摆空壳"在这儿同样成立。
        auto *recentCard = makeHomeCard(home.left.widget);
        // ★ 这一行**不能漏**：`setHomeRecent()` 头一句就是拿 `homeRecentCard` 判空（那是
        //   "界面还没搭完"那一档），漏了它整张卡就永远停在 `hide()` 那一档 —— 主页上一片
        //   空白，而两颗按钮照旧接得上线（它们各自是成员）。2026-10-13 的 mw_check T36
        //   抓到的就是这个形状（卡片收着、按钮还写着初始那两个字）。
        homeRecentCard = recentCard;
        recentCard->setObjectName(homeRecentCardName());
        // 高度由内容定（和启动卡一样）：这一栏在竖直方向上是"居中一坨"，卡片不该自己去
        // 抢留白。
        recentCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto *recentLayout = new QVBoxLayout(recentCard);
        recentLayout->setContentsMargins(22, 16, 22, 18);
        recentLayout->setSpacing(6);

        // 第一行：标题在左，「昨天 22:14」在右（§5.1 那张草图里时间就是右上角那一句，
        // 不单独占一行）。
        auto *recentHead = new QHBoxLayout();
        recentHead->setSpacing(8);
        recentHead->addWidget(makeHomeTitle(homeRecentTitleText(), recentCard));
        recentHead->addStretch(1);
        homeRecentWhenLabel = new QLabel(recentCard);
        homeRecentWhenLabel->setObjectName(homeRecentWhenLabelName());
        // 低强调度：它是"什么时候"，不是这张卡要说的事。
        homeRecentWhenLabel->setForegroundRole(QPalette::PlaceholderText);
        recentHead->addWidget(homeRecentWhenLabel);
        recentLayout->addLayout(recentHead);

        // 第二行：**启动那一刻**那一句实例名（里面已经带着版本和加载器）。
        homeRecentNameLabel = new QLabel(recentCard);
        homeRecentNameLabel->setObjectName(homeRecentNameLabelName());
        homeRecentNameLabel->setWordWrap(true);
        // 横向摘出度量：这一栏的宽度只由 7:3 的份数决定，不让一句长实例名把它顶宽
        // （和下面 detailLabel / 右栏 skinHintLabel 同一条理由）。
        homeRecentNameLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        {
            QFont nameFont = homeRecentNameLabel->font();
            nameFont.setBold(true);
            homeRecentNameLabel->setFont(nameFont);
        }
        recentLayout->addWidget(homeRecentNameLabel);

        // 第三行（灰字）：目标 + 玩了多久 + 那一趟的结局。**每一段都可能没有**（普通启动
        // 就没有目标），整段空着时这一行**收起来** —— 空 QLabel 也占一行字高，留着就是白留。
        homeRecentDetailLabel = makeHomeHint(QString(), recentCard);
        homeRecentDetailLabel->setObjectName(homeRecentDetailLabelName());
        recentLayout->addWidget(homeRecentDetailLabel);

        // 第四行：两颗按钮。左边那一颗是主动作（接着玩），右边那颗是"去改设置"。
        auto *recentButtons = new QHBoxLayout();
        recentButtons->setSpacing(8);
        homeRecentContinueButton =
            new QPushButton(homeRecentContinueText(), recentCard);
        homeRecentContinueButton->setObjectName(homeRecentContinueButtonName());
        Theme::markButton(homeRecentContinueButton, Theme::ButtonKind::Primary);
        homeRecentSettingsButton =
            new QPushButton(homeRecentSettingsText(), recentCard);
        homeRecentSettingsButton->setObjectName(homeRecentSettingsButtonName());
        Theme::markButton(homeRecentSettingsButton, Theme::ButtonKind::Neutral);
        recentButtons->addWidget(homeRecentContinueButton);
        recentButtons->addWidget(homeRecentSettingsButton);
        recentButtons->addStretch(1);
        recentLayout->addLayout(recentButtons);

        // 一开始就是"没有记录"那一档。
        recentCard->hide();

        // 启动卡落在视觉中心略偏上的位置；窗口变高时留白分到上下两侧，不再只堆在按钮下方。
        home.left.layout->addStretch(1);
        home.left.layout->addWidget(launchCard);
        home.left.layout->addSpacing(kHomeCardGap);
        home.left.layout->addWidget(recentCard);
        home.left.layout->addStretch(2);
    }
    // ---------- 主页右栏：我的角色（3D 皮肤预览） ----------
    // 这里原来是空的：早先放着一张「版本列表」，和左边那个下拉框是同一份数据的两种看法，
    // 而实例页右边现在也有一张同样数据的列表了，留着就是把同一件事说三遍（2026-09 去掉的）。
    // 空着的那阵子这一栏的作用只剩"按份数把左边那套操作顶到 7 成宽"，现在拿它装皮肤预览 ——
    // 三成宽正好够放一个小人（见 SkinModel 里"缩放按控件尺寸自适应"那条，控件多大它多大）。
    {
        auto *profileCard = makeHomeCard(home.right.widget);
        profileCard->setObjectName(QStringLiteral("homeProfileCard"));
        auto *profileLayout = new QVBoxLayout(profileCard);
        profileLayout->setContentsMargins(16, 16, 16, 16);
        profileLayout->setSpacing(10);

        QLabel *skinTitle = makeHomeTitle(QStringLiteral("我的角色"), profileCard);
        profileLayout->addWidget(skinTitle);

        skinViewer = new SkinViewer(profileCard);
        skinViewer->setObjectName(skinViewerName());
        // 立刻喂一套外观。这个控件**任何时候都不该是空白的** —— 没登录、还没取到、
        // 取不到，这三种情况看到的都是这套默认外观，所以兜底放在这里而不是等业务层来灌：
        // 少一处"谁忘了设"的可能。业务层拿到真皮肤时再覆盖它。
        skinViewer->setSkin(SkinModel::defaultSteveSkin(), QImage(), false);
        // 份数 1：这一栏里竖直方向全是它的（标题和说明那两行是固定高度）。
        profileLayout->addWidget(skinViewer, 1);

        skinHintLabel = new QLabel(skinHintNotLoggedInText(), profileCard);
        skinHintLabel->setObjectName(skinHintLabelName());
        // 必须换行：这句最长的时候（「取不到 <名字> 的皮肤，显示的是默认外观」）有 20 多字，
        // 而这一栏只有 260 px 上下，不换行就会把整栏顶宽、把左边的 7 份挤掉。
        skinHintLabel->setWordWrap(true);
        // 横向上把自己**从布局的度量里摘出去**：wordWrap 的 QLabel 报出来的 sizeHint 是
        // "整句排成一行"的宽度（好几百 px），而最小宽度又是按最长的一段算的 —— 这一栏是
        // 3 份（约 260 px），让一个纯说明文字去决定它的宽度是本末倒置。摘掉之后这一栏的
        // 宽度仍然只由份数决定（主页那个精确的 7:3 就是靠这个成立的）。
        skinHintLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        profileLayout->addWidget(skinHintLabel);

        // 「账号与外观…」：多账号 / 换皮肤换披风的入口，按用户要求放在 3D 预览这一块
        // 的下方。**说明行（skinHintLabel）在它上面**：那句是预览的图注，紧贴着图才是
        // 图注该在的位置；把按钮塞进图和图注中间会把「图 + 它的注解」拆成两半。
        // 宽度**不**跟着这一栏长（这一栏只有 3 份，拉满会顶宽），贴左边固定宽度。
        accountSkinButton = new QPushButton(MainWindowUI::accountSkinButtonText(),
                                            profileCard);
        accountSkinButton->setObjectName(MainWindowUI::accountSkinButtonName());
        // 「角色皮肤…」= 通往另一个窗的一扇门（一般性），不是"干这件事"。
        Theme::markButton(accountSkinButton, Theme::ButtonKind::Neutral);
        accountSkinButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        accountSkinButton->setMinimumWidth(kAccountSkinButtonMinWidth);
        profileLayout->addWidget(accountSkinButton, 0, Qt::AlignLeft);
        home.right.layout->addWidget(profileCard, 1);
    }
    // NOTE: 控件一个都不放也得把这栏建出来 —— 验证工程靠 leftColumnName()/rightColumnName()
    //       找这两栏量几何，主页右栏没了就等于主页的比例没地方量。

    // ---------- 实例：左 = 游戏文件夹列表；右 = 那个文件夹里的实例 ----------
    QWidget *instanceTab = new ThemeBackground(Theme::Part::PageBackground, tabWidget);
    instanceTab->setObjectName(instanceTabName());
    // NOTE: 这一份**不是 const**（另几页那三份是）：页头那两行字和右上角那条操作是
    //       `buildTwoColumnPage()` 搭完之后才由这一页填的（`fillPageHeader()` 收的是
    //       `Column&`）。
    TwoColumns instance = buildTwoColumnPage(instanceTab, QStringLiteral("instance"));
    // ---- 页头：标题「实例」+ 副标题（路径 + 实例数量，就是原来那句「当前文件夹：…」）----
    //
    // ★ 那句副标题**就是** `instanceFolderLabel`：它一直说的是"这一页的实例是从哪一份
    //   .minecraft 里扫出来的、一共有几个"，用户 2026-10-12 那份文档里"页面说明 / 路径 /
    //   当前状态"要的正是它。所以这里把页头那个说明标签**接过来用**（objectName 换成它
    //   原来那个名字）—— 文案、灌数据那条路（MainWindow::refreshInstanceList）、
    //   验证工程找它用的名字一处都不用动，只是从"框里的一行"变成了"标题下面那行说明"。
    fillPageHeader(instance.right, instancePageTitle(), QString());
    instanceFolderLabel = instance.right.subtitle;
    instanceFolderLabel->setObjectName(MainWindowUI::instanceFolderLabelName());
    // 它就是这一页的状态栏那一行，**不跟着"文字空不空"收起来**（文字由 MainWindow 灌，
    // 界面这边不知道盘上有几份）。
    instanceFolderLabel->setVisible(true);
    // 右上角那颗页面级操作：「分组设置…」（它管的是**分组清单本身**，不是选中的那一个
    // 实例 —— 见下面那段，那是 2026-09-25 就定下的语义）。
    groupInstanceButton = new QPushButton(groupInstanceText(), instance.right.header);
    groupInstanceButton->setObjectName(groupInstanceButtonName());
    // 「分组设置…」= 打开另一个窗（一般性），不是"干这件事"。
    Theme::markButton(groupInstanceButton, Theme::ButtonKind::Neutral);
    instance.right.actions->addWidget(groupInstanceButton);

    {
        // ---- 侧栏：一个页面小标题「游戏目录」+ 文件夹列表 + 两颗按钮（都在侧栏底部）----
        //
        // ★ 2026-10-12：这一栏原来是一个 `QGroupBox("游戏文件夹")`（缺口式标题 + 一圈边）。
        //   用户那份文档的第一条就是「减少框」：**页面本身不画边框**、侧栏只用一个轻微
        //   底色区分，小节标题改成**一行普通的粗体字**。于是那圈边整块没了，标题变成下面
        //   这一行，列表和按钮直接落在侧栏面上。
        QLabel *folderTitle = new QLabel(folderSectionTitleText(), instance.left.surface);
        folderTitle->setObjectName(folderSectionTitleName());
        markTextRole(folderTitle, Theme::sectionTitleTextRoleValue());
        {
            QFont titleFont = folderTitle->font();
            titleFont.setBold(true);
            folderTitle->setFont(titleFont);
        }
        instance.left.layout->addWidget(folderTitle);

        // 左边这张列表就是"启动器用哪一份 .minecraft"的选择器：选中哪一项，版本、资源、
        // 库、自管 Java 就全从那一份里找（PCL 左边那几张卡片是同一个意思）。
        // 自带的那一份永远排第一（见 FolderManager），所以列表不会是空的。
        //
        // NOTE: 每一行的内容**不是**条目自己画的（条目上没有文字），而是 setItemWidget()
        //       塞进去的一个 FolderRowWidget（见 setFolderRows()）——两行字 + 行尾一颗
        //       齿轮需要一个真控件才摆得下。省略因此完全由那一行里的 ElidedLabel 自己做。
        //
        // NOTE: 2026-09-25 删掉了这里原来的 `setTextElideMode(Qt::ElideMiddle)`。它是个
        //       **死设置**：条目上一个字都没有，列表没有任何东西可省略，这一句从来没起过
        //       作用（真正拦住横向滚动条的是下面那条 ScrollBarAlwaysOff）。留着更糟 ——
        //       它让人以为"路径从中间省略"这件事已经配好了，而实际干活的那个标签当时一律
        //       从**尾巴**省略（路径最后一级目录名正是唯一认得出是哪一份的那截）。
        //       现在这件事放在真正画字的那个标签上：见 FolderRowWidget 里的
        //       `subtitle->setElideMode(...)`。
        folderList = new QListWidget(instance.left.surface);
        folderList->setObjectName(folderListName());
        folderList->setUniformItemSizes(true);
        folderList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        folderList->setSelectionMode(QAbstractItemView::SingleSelection);
        // ★ 列表**不画自己那圈边**：侧栏面已经是一层底了，再套一圈就是"框套框"
        //   （用户文档第一条要减掉的正是它）。这条列表的行本来就是自绘的两行式行
        //   （见 FolderRowWidget），有没有外框在观感上是"一块面板"和"一串行"的区别。
        folderList->setFrameShape(QFrame::NoFrame);
        instance.left.layout->addWidget(folderList, 1);

        // 下面两颗按钮**贴着侧栏底部**（列表那一格吃掉剩下的高度）。
        // 这一行**只有「添加文件夹…」一个按钮**了：2026-09-24 用户说「下面那个移除文件夹
        // 也可以弃掉了」—— 「移除」现在只有行尾齿轮菜单里那一项（同一个
        // MainWindow::removeFolderFromList()，同一个确认框）。
        //
        // ★ 两颗的身份是**有意分开**的（用户文档里那两颗就是两种样子）：「添加文件夹…」
        //   是"加东西进来"那一档（主题色描边，`ButtonKind::Accent` = 默认那一份样式）；
        //   「导入整合包…」是**另一条**路（一般性），走中性那档白底黑字。两颗都顶满侧栏。
        addFolderButton = new QPushButton(addFolderText(), instance.left.surface);
        addFolderButton->setObjectName(addFolderButtonName());
        addFolderButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        instance.left.layout->addWidget(addFolderButton);

        // 「导入整合包…」：从 Modrinth 格式的整合包（.zip / .mrpack）里**造一个新实例**。
        // 2026-09-25 从右栏挪到这儿（用户原话：「导入整合包放在左边添加文件夹下面」）：
        // 它和「添加文件夹…」是同一类动作（往这一列里添东西）—— 而右栏那三个都是
        // "对选中的实例做什么"，混在一排里用户得先猜它作用于选中项还是新建。
        importModpackButton = new QPushButton(importModpackText(), instance.left.surface);
        importModpackButton->setObjectName(importModpackButtonName());
        importModpackButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        // 「导入整合包…」= 一般性（白底黑字那一档），和上面那颗"主题色描边"的加文件夹区分开。
        Theme::markButton(importModpackButton, Theme::ButtonKind::Neutral);
        instance.left.layout->addWidget(importModpackButton);

        // 那颗按钮的说明，**从右栏那段提示里拆出来的头一段**（剩下的几句说的都是重命名 /
        // 分组 / 实例设置，跟这一组无关）。摆在这儿是因为它讲的就是上面那颗按钮 ——
        // 2026-09-25 之前它俩隔着两栏，读的人得自己把句子和按钮对起来。
        //
        // ★ 这一句**只能这么短**（2026-09-25 实测）：折行 QLabel 的 sizeHint 宽度是横向
        //   advance 的**分档**，而左栏从前是按各控件的 sizeHint 分宽度的 —— 第一版写的是
        //   「收的是 Modrinth 格式的整合包（压缩包根目录里有一份 modrinth.index.json）。它
        //   会把加载器、模组、config 和资源包一次装成一个新实例。」（约 70 个汉字），当场
        //   两条红：整窗 sizeHint 818 → **1042**（ui_check E5 的上限是 900）。
        //   NOTE: 2026-10-12 起侧栏是**固定宽度**（`sidebarWidth()`），折行标签再也顶不宽
        //   它了 —— 但这条规矩照旧：真有话要说就走那颗按钮的 tooltip（完全不吃布局）。
        QLabel *importHint = new QLabel(
            QStringLiteral("收 Modrinth 格式的整合包（.zip / .mrpack），"
                           "加载器、模组、config 一次装好。"),
            instance.left.surface);
        importHint->setObjectName(importHintLabelName());
        importHint->setWordWrap(true);
        markTextRole(importHint, Theme::secondaryTextRoleValue());
        // 横向把自己从布局的度量里摘出去：侧栏是固定宽度，让这句话参与横向度量只会让它
        // 把侧栏的内容挤出去（见 `buildTwoColumnPage()` 里说明那一行同一条规矩）。
        importHint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        instance.left.layout->addWidget(importHint);

        // 原来这里还有一段提示（"选中的就是启动器当前使用的 .minecraft…自带的那一份
        // 移不掉…"），2026-09-24 用户说「下面那个文本也删了」，一起拿掉了。
        // 它那两句话现在各有各的出处，一件都没丢：
        //   ① 「这一份就是当前在用的」—— 页头那行说明（`instanceFolderLabel`）就写着
        //      当前是哪一份；
        //   ② 「移除只动列表、删除才动盘」—— 齿轮菜单那两项各自的 tooltip
        //       （folderActionTooltip）以及它们弹的那个确认框里各写了一遍。
    }
    {
        // 「分组设置…」这一颗**现在是页头右上角那颗页面级操作**（见上面 `instance.right.actions`
        // 那一段）：2026-09-25 用户原话「分组设置先放右上角框外不删了」。当天右栏那一排三颗
        // （重命名… / 分组设置… / 实例设置…）一起删了，另外两颗各有各的新家（「重命名…」进了
        // 「实例设置」那一页；「实例设置」= **点这一条实例行**，行尾还常显一颗齿轮），只有它
        // 留下来，从框**里面**挪到框**外面**的右上角。
        //
        // 为什么它本来就不该跟那一排站在一起：那两颗作用于**选中的那一个实例**，它作用于
        // **分组清单本身**（有哪些组、叫什么名字、什么顺序、默认展开还是收着、哪一个是
        // 置顶组 —— 见 ui/GroupDialog）。"选中的实例"对它没有意义，混在一排里用户得先猜
        // 它是"对这条做什么"还是"对这一列做什么"。摆在页头、正文之上，说的是"下面这一整块
        // 怎么组织" —— 2026-10-12 那次改版把"框外右上角"落实成了**页头的操作位**。
        //
        // ---- 正文：就只有这一棵树（原来那个 `QGroupBox("实例列表")` 整块没了）----
        //
        // ★ 2026-10-12：用户那份文档第一条就是「减少框」—— 页面不画边框、正文只有一个面。
        //   于是这一组那个缺口式标题 + 一圈边整块撤掉：树的四周本来就是正文面（`workspace`），
        //   再套一圈就是"框套框"。原来那句「当前文件夹：…」也没丢，它现在是**页头那行说明**
        //   （见上面），而且和标题同级、比框里那行更像"这一页的状态"。
        //
        // 主页那个「已安装版本」下拉框的同一份数据，只是摆在这儿配左边的文件夹看。
        // 点击 = 在主页把它选中（两边按 id 双向同步，见 MainWindow::setupConnections）。
        //
        // 是**树**不是列表：实例可以归到用户建的组里，组是父节点、实例是子节点。
        // 一个组都没有的时候它就是平铺的一层（照样不插组头），观感和列表一样。
        instanceList = new QTreeWidget(instance.right.surface);
        instanceList->setObjectName(instanceListName());
        instanceList->setHeaderHidden(true);
        // ★ 这一棵树**自己那圈边不画**（2026-10-12）：它四周已经是正文那个面（`workspace`），
        //   再套一圈就是用户那份文档第一条要减掉的"框套框"。行本身（组行 / 实例行）都是自绘的，
        //   有没有外框在观感上差的是"一块面板"还是"一串行"。
        instanceList->setFrameShape(QFrame::NoFrame);
        // 左边**不画** Qt 自带那个展开三角：展开/收起这件事画在行的**右**端（一个箭头，
        // 见 InstanceGroupDelegate），和版本树那几条分类长得一样（用户原话：「每一个组和
        // 版本下载几乎一致」）。开着的话一行上会有两个箭头，而且左边那个还会把组名往里推。
        // NOTE: 正路就是 setRootIsDecorated(false) —— 拿 `DontShowIndicator` 那条 item flag
        //       顶替会让整条分类的子条目一起消失（版本树那边踩过）。
        instanceList->setRootIsDecorated(false);
        // 每一行高度一样（没有多行文本），打开这个能让 QTreeWidget 不用去问每一行的
        // sizeHint —— 实例多的时候（真机上有 61 个）滚动明显更顺。
        instanceList->setUniformRowHeights(true);
        // 组下面的实例缩进多少由它给。默认 20 在窄栏里有点深，收到 14：
        // 正文那一栏还要留出滚动条和行尾那几颗按钮的宽度，缩进多一格就少一格放实例名。
        instanceList->setIndentation(14);
        // 分组头那一行不是默认画法：组名粗体、右边一个箭头 + 一颗齿轮（"点一下打开/关闭"
        // 和"这一组的更多操作"都画在那两个格里）。代理只接管挂着 groupIdRole() 的行，
        // 实例行原样走基类（见 InstanceGroupDelegate 文件头）。
        instanceList->setItemDelegate(new InstanceGroupDelegate(instanceList));
        // 实例行前面那个图标（原版草方块 / Forge 铁砧 / Fabric…）：**这一份树以前从来没设过
        // iconSize**，用的是样式给的默认值 —— 而图标是按 instanceTreeIconSize() 画的，
        // 不设的话它会被缩到默认那一档、和实例名那行字挤在一起。
        //
        // ★ 这个边长是**实例列表自己**那一档（2026-09-29 起 26，之前借的是版本树的 18，
        //   用户原话「图标大一点」）。它和 MainWindow::refreshInstanceList() 里现算那些
        //   QPixmap 时递进去的边长**必须同一个数** —— 两处对不上的症状是图标被 QIcon
        //   重新采样一遍（屏幕上只是略糊，验证工程里那条"行首那张图 == 现画的那张"会红）。
        instanceList->setIconSize(QSize(instanceTreeIconSize(), instanceTreeIconSize()));
        // ★ 悬停那两颗按钮（上箭头 / 垃圾桶）**必须**有这一句，而且它是缺一不可的两半里的
        //   一半（另一半是代理里那个 `&& hovered`，见 InstanceGroupDelegate::paint()）：
        //   干净的视图 `WA_Hover=0` / `mouseTracking=0`，**真鼠标移动一个字都不改**
        //   `QStyle::State_MouseOver`（2026-09-25 下载页左栏那六个刷新圈实测过，windows 与
        //   offscreen 两个平台结论一样）。少这一句的表现是"那两颗按钮再也不出现"，而两边
        //   各自的断言还都是绿的。
        instanceList->setMouseTracking(true);
        // 展开/折叠动画（2026-09-29）：组行那一按，子条目是**滑**出来的。交给 Qt 自己
        // （`QTreeView::setAnimated`），规矩见 ui/Animations.hpp。
        Anim::animateExpansion(instanceList);
        instance.right.layout->addWidget(instanceList, 1);

        // NOTE: 这里原来是三颗按钮的一排（重命名… / 分组设置… / 实例设置…）加下面一段
        //       折行提示。**2026-09-25 全部拿掉了**，用户原话：「删掉实例页面右侧三个按钮
        //       （因为都已经分别有入口了），顺便把下面的文本干掉」。
        //       三颗各自的去处（一个都没丢）：
        //         - 「实例设置…」= **点这一条实例行**（空白、左边那个图标、版本名都算），
        //           行尾还会常显一颗齿轮 —— 两颗做的是同一件事（用户原话：「直接点空白区域
        //           （和左边图标版本名）进入实例设置」+ 追加答复「要，一颗齿轮常显」）；
        //         - 「重命名…」搬进了「实例设置」那一页（改实例名就在那一页里改）；
        //         - 「分组设置…」挪到了框**外面**的右上角（现在是页头那颗页面级操作，
        //           见那一段 NOTE）。
        //
        //       那段提示一起删了：它当初讲的是"重命名会改目录名、分组只记在启动器自己的
        //       文件里、实例设置每一项都能跟随全局或自己填"—— 前两句现在各有各的出处
        //       （重命名那一页、分组那一页自己会讲），第三句在「实例设置」页里一眼就能
        //       看出来（每一行左边都摆着那对「跟随全局 / 自定义」）。
        //
        // NOTE: 2026-09-25 之前这里就没有「停止游戏…」了（用户原话「实例里停止游戏按钮
        //       删了」）。停在哪儿这件事归底栏左面板里那一行的电源（见
        //       MainWindow::stopGameSession()）：那一格写着"哪一个会话在跑"，停哪一份比在
        //       "对选中实例做什么"的排里更说得清。代价（如实记一笔）：原来那颗按钮走的是
        //       applyStopGame(versionId)，能一次停掉**同一个实例的多份会话**；底栏那一行的
        //       电源只停**这一份**（会话号）—— 多开的那几份要逐个停。
        //       「导入整合包…」2026-09-25 从这一排挪去了左栏「游戏文件夹」那一组（在
        //       「添加文件夹…」下面），2026-10-12 起那一组不再是一个框，它就是侧栏本身。
        // 正文那一格由上面那棵树吃满 —— 它上面再没有别的东西了（页头在面外面）。
    }

    // ---------- 下载：左 = 分类入口（仿 PCL）；右 = 那个分类的内容 ----------
    //
    // 左栏 2026-09-22 从"挑加载器 + 挑版本"换成了 PCL 那种**一列分类**：游戏 / 整合包 /
    // 模组 / 数据包 / 资源包 / 光影。点哪个，右栏那份 QStackedWidget 就换到哪一页。
    //
    // NOTE: 2026-09-23 起**后面几页全是真的**了。「游戏」那一页就是原来那一摊（挑加载器、
    //       挑版本、下载），一个控件都没少；其余几页是"一个搜索框 + 一棵三层树"（资源 →
    //       游戏版本 → 版本号），在这里一段循环里搭出来（见下面那段）。原来那几句"还没做"
    //       的话连同 downloadPlaceholderText() 一起删掉了 —— 留着就是那几页在说假话。
    //       （「数据包」是 2026-09-25 加的，见头文件那张表那段。）
    QWidget *downloadTab = new ThemeBackground(Theme::Part::PageBackground, tabWidget);
    downloadTab->setObjectName(downloadTabName());
    // NOTE: 这一份**不是 const**（另两页那两份是）：页头那两处是搭完之后才由这一页填的
    //       （`fillPageHeader()` 收的是 `Column&`）。
    TwoColumns download = buildTwoColumnPage(downloadTab, QStringLiteral("download"));
    {
        const QStringList categoryKeys = downloadCategoryKeys();
        const QStringList categoryTexts = downloadCategoryTexts();

        // ---- 页头：标题「下载」+ 说明（当前停在哪一类、那一类能干什么）+ 右上角那颗刷新 ----
        //
        // ★ 2026-10-12：用户那份改版文档给下载页点名的两件事就是这一块 —— 页面要有**稳定的
        //   头部**（返回、标题、表单、安装键原来"看起来是散落的"），以及"刷新按钮属于
        //   **当前分类自己的页面头部**"。于是：
        //     · 标题恒为「下载」（翻哪一类都不变，这才叫稳定）；
        //     · 说明跟着当前那一类走（`setDownloadHeaderRow()`，由 MainWindow 在
        //       `currentRowChanged` 上刷）；
        //     · 刷新那一颗放在**动作位**上（右上角），作用于当前这一类。
        fillPageHeader(download.right, downloadPageTitle(),
                       downloadHeaderSubtitle(downloadCategoryGameIndex()));
        downloadHeaderSubtitleLabel = download.right.subtitle;
        downloadHeaderRefreshButton = new QPushButton(downloadHeaderRefreshText(),
                                                      download.right.header);
        downloadHeaderRefreshButton->setObjectName(downloadHeaderRefreshButtonName());
        // 「刷新」= 一般性（看一眼 / 重新拉一遍，不是"干这件事"）。
        Theme::markButton(downloadHeaderRefreshButton, Theme::ButtonKind::Neutral);
        download.right.actions->addWidget(downloadHeaderRefreshButton);

        downloadCategoryList = new QListWidget(download.left.surface);
        downloadCategoryList->setObjectName(downloadCategoryListName());
        downloadCategoryList->setUniformItemSizes(true);
        // ★ 2026-10-12：这一列**不画自己那圈边** —— 它整列就长在侧栏那个面上（见
        //   `buildTwoColumnPage()`），再套一圈就是用户那份文档第一条要减掉的"框套框"。
        downloadCategoryList->setFrameShape(QFrame::NoFrame);
        // 2026-09-23：这一列换成**和上面两棵树同一个代理**（用户原话："下载界面左边栏
        // 每一项做大点，每一项的右边放一个刷新按钮"）。换代理同时办成两件事：
        //   ① 行高从样式那个"一行文字"（实测 ~24 px）变成 versionTreeRowHeight()（46），
        //      字号也跟着下面那次 +versionTreeFontStep() 放大 —— "做大点"就是这两条；
        //   ② 行尾那一格本来就是这个代理画箭头的活，加个刷新字形（rowRefreshRole()）
        //      不用另起一套控件：QListWidget 的 itemWidget 会盖住选中高亮那层背景，
        //      那样"现在停在哪一类"就看不出来了。
        // NOTE: 条目文字是**粗体**（代理对顶层行一律加粗，见 VersionTreeDelegate::paint）——
        //       和版本树那四条分类一个长相，这一列本来也就是"分类"。
        // ★ 末一个参数 true = **导航那一档**（2026-10-12）：这一列选中时走的是"浅主题色底 +
        //   左边 3 px 主题色指示条 + 主题色深色文字"，不是那块浅灰 —— 用户那份改版文档
        //   原话：「选中项：浅主题色背景 + 左侧 3px 主题色指示条 + 主题色深色文字，而不是
        //   一整块深灰」。灰那一档是给"选中的数据行"用的（版本树、资源表、实例列表），
        //   而这一列是**导航**：它说的是"现在停在哪一类"，和"选中了哪一条数据"是两件事。
        downloadCategoryList->setItemDelegate(
            new VersionTreeDelegate(downloadCategoryList, /*leafArrow=*/true,
                                    /*iconSize=*/0, /*navRow=*/true));
        // ★ 2026-09-25：这一列那五个刷新按钮**只在鼠标悬停到那一行时才显示**（用户原话：
        //    「下载界面只有鼠标悬停在按钮上时才显示对应的刷新按钮」）。代理读的是视图填的
        //    QStyle::State_MouseOver，而**视图默认不填** —— 量过（探针在 tmp/probe_hover）：
        //    干净的 QListWidget 视口 WA_Hover=0 / mouseTracking=0，真鼠标移动一个字都不改；
        //    开了下面这一句之后，鼠标真挪过去才会把"光标底下是哪一行"交给代理（windows 与
        //    offscreen 两个平台各量一遍，同一个结果）。
        // NOTE: 只开给这一份列表：那一格（rowRefreshRole()）全仓只有这里有，版本树、加载器树、
        //       资源表上都没有，开了只是让它们多收几条鼠标移动事件。
        //       这一句和 VersionTreeDelegate::paint() 里那段 showRefresh 是一对 —— 少哪一句
        //       都是"刷新按钮再也不出现"，两边各自的断言都还是绿的。
        downloadCategoryList->setMouseTracking(true);
        QFont categoryFont = downloadCategoryList->font();
        categoryFont.setPointSize(categoryFont.pointSize() + versionTreeFontStep());
        downloadCategoryList->setFont(categoryFont);
        for (int i = 0; i < categoryKeys.size(); ++i) {
            auto *item = new QListWidgetItem(categoryTexts.value(i), downloadCategoryList);
            // 身份进 userData：条目文字是显示内容，会随产品文案走，不能拿它当身份。
            item->setData(Qt::UserRole, categoryKeys.value(i));
            // 每个条目右边都有那个刷新按钮（一条不落）。点它的那一下由
            // MainWindow::eventFilter 接 —— 判据是 VersionTreeDelegate::refreshRect()。
            // NOTE: 这里是**两参数**那个 setData（QListWidgetItem 认的是 role，没有列）——
            //       三参数那一个是 QTreeWidgetItem 的，写在这儿编不过。
            item->setData(rowRefreshRole(), true);
        }
        // 高度贴着内容：五个两三个字的条目，被拉成一整条空白看着像坏了。行高**交给样式
        // 算**（sizeHintForRow），别写死数字 —— 换字体/换样式就错（UI 样式基线那件事）。
        int rowsHeight = 0;
        for (int i = 0; i < downloadCategoryList->count(); ++i) {
            rowsHeight += downloadCategoryList->sizeHintForRow(i);
        }
        downloadCategoryList->setFixedHeight(rowsHeight +
                                             2 * downloadCategoryList->frameWidth() + 2);
        // 「← 返回实例设置」那一颗（2026-09-28 从右栏挪到这儿）。
        //
        // ★ 用户原话：「返回按钮也是放在左上角（就是左边栏的顶上）」。挪过来之后它由
        //   **六栏共用一颗**（原来是五栏各一颗，摆在右栏那行筛子的最前头）—— 两件事
        //   一起变对了：
        //   ① 「回到刚才那个实例」与用户此刻翻到哪一栏**无关**，本来就不该一栏一颗；
        //   ② 它在左栏顶上，翻栏时**不动**，不会跟着右栏一起刷没了。
        // **默认收着**（连位置都不占）：只有这一次是从某个实例的设置页点进来的才露面。
        downloadBackButton = new QPushButton(downloadBackButtonText(), download.left.surface);
        downloadBackButton->setObjectName(downloadBackButtonName());
        downloadBackButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        downloadBackButton->setToolTip(downloadBackTooltip());
        // 「← 返回」= 往回走（一般性）。
        Theme::markButton(downloadBackButton, Theme::ButtonKind::Neutral);
        downloadBackButton->setVisible(false);
        download.left.layout->addWidget(downloadBackButton);

        download.left.layout->addWidget(downloadCategoryList);
        download.left.layout->addStretch(1);

        downloadStack = new QStackedWidget(download.right.widget);
        downloadStack->setObjectName(downloadStackName());
        download.right.layout->addWidget(downloadStack, 1);

        // ---- 第 0 页「游戏」：里面再分两层（版本树 / 安装页） ----
        //
        // 用户原话见头文件里 downloadGameStackName() 上面那段：四个分类各占一条 →
        // 点开是那条底下的版本号 → 点一个版本号进安装页；安装页顶上是名称框（带重名
        // 小字提示）、底下是四个加载器各一条（每条右边选它自己的版本）、最下面「安装」。
        // 左栏那列分类、右栏这份堆叠、底下状态栏都不动。
        QWidget *gamePage = new QWidget(downloadStack);
        gamePage->setObjectName(downloadGamePageName());
        QVBoxLayout *gameLayout = new QVBoxLayout(gamePage);
        gameLayout->setContentsMargins(0, 0, 0, 0);
        gameLayout->setSpacing(0);

        gameStack = new QStackedWidget(gamePage);
        gameStack->setObjectName(downloadGameStackName());
        gameLayout->addWidget(gameStack, 1);

        // ---- [0] 四条分类 + 它们底下的版本号 ----
        //
        // 四条的**内容**（哪四条、各叫什么、底下哪些版本）全部来自
        // VersionManager::availableVersionChannels()，界面这边一条判据都不写 ——
        // "什么算愚人节版"那件事只有 VersionCategory 一处（含那张写死的名单）。
        QWidget *treePage = new QWidget(gameStack);
        auto *treeLayout = new QVBoxLayout(treePage);
        treeLayout->setContentsMargins(0, 0, 0, 0);
        treeLayout->setSpacing(10);
        versionTree = new QTreeWidget(treePage);
        versionTree->setObjectName(versionTreeName());
        // 只有一列、只有一个版本号，表头没有信息量（"1" 那一格纯属噪音）。
        versionTree->setHeaderHidden(true);
        // 742 条快照全是同一个行高，让它按第一行算一次就够（省的是滚动时的重算）。
        // NOTE: 行高现在是**常数**（versionTreeRowHeight()）—— 见代理的 sizeHint()：
        //       分类那一行是一行字、版本号那两行字，按内容算会两边不一样高，两行那种会被裁。
        versionTree->setUniformRowHeights(true);
        // 左边那个"小三角"不画（展开/收起的箭头我们画在**行最右边**，见下面的代理）。
        // NOTE: 走 rootIsDecorated(false) 而**不是**条目上的 setChildIndicatorPolicy ——
        //       QTreeModel::hasChildren() 是按那个 policy 直接 return false 的，那样
        //       整个分类的版本号一条都排不进布局（树上只剩四行空的分类）。
        //       这里关掉的只是装饰格：分类从 x=0 起画，子条目照样缩进 indent 那么多。
        versionTree->setRootIsDecorated(false);
        versionTree->setColumnCount(1);
        // 每一行长什么样（草方块 + 版本号 + 下面那行发布时间 + 最右边的 >/V）全在这个代理里。
        // 条目还是普通 QTreeWidgetItem（身份、展开状态、点击那条路一个都没变）。
        versionTree->setItemDelegate(new VersionTreeDelegate(versionTree));
        // ★ 悬停那一行的加深底色（代理里那段 Theme::rowHoverFill()）**要的就是这一句**：
        //   代理画不画看 `QStyle::State_MouseOver`，而那一位是**视图**填进 option 里的，
        //   干净的视图默认不填（`WA_Hover=0` / `mouseTracking=0`）—— 少了它悬停底色一次
        //   都不会出现，而且**不报错、断言也不一定先红**，屏幕上就是"悬停没反应"。
        //   真平台 windows 与 offscreen 两个平台都量过：这一位不填，代理那段一个字都不生效。
        versionTree->setMouseTracking(true);
        // 字号放大一点（用户要的）。代理里那行小字是从这个字号往下推的，别在这儿单独设它。
        QFont treeFont = versionTree->font();
        treeFont.setPointSize(treeFont.pointSize() + versionTreeFontStep());
        versionTree->setFont(treeFont);
        // 图标尺寸：画是代理画的（它自己按 versionTreeIconSize 缩放），这里设的是**视图
        // 认下的那个尺寸** —— 装上安装页那棵加载器树之后，这两条竖的设置必须成对地
        // 一样（验证工程拿"两棵树这三条相等"钉"两边是同一个模式"）。
        versionTree->setIconSize(QSize(versionTreeIconSize(), versionTreeIconSize()));
        // 单击分类就展开/折叠（用户点的是最右边那个 >，那个箭头就画在那一行上）。
        // 双击**不再**自己展开一次 —— 那会和上面那次单击叠起来，双击等于"展开又收起"。
        versionTree->setExpandsOnDoubleClick(false);
        // 展开/折叠动画（2026-09-29）：分类那一层展开的时候，底下的版本号是**滑**出来的。
        // 五张资源表（下面那个循环里）走同一句，见 ui/Animations.hpp。
        Anim::animateExpansion(versionTree);
        treeLayout->addWidget(versionTree, 1);
        gameStack->addWidget(treePage);

        // ---- [1] 安装页 ----
        QWidget *installPage = new QWidget(gameStack);
        installPage->setObjectName(installPageName());
        QVBoxLayout *installLayout = new QVBoxLayout(installPage);
        installLayout->setContentsMargins(0, 0, 0, 0);
        installLayout->setSpacing(10);

        // 顶上那一行：回得去 + 这一份是"基于哪个原版"装的。点的是树里哪个版本，到这里
        // 必须还看得见 —— 否则"我在给哪个版本装加载器"就全靠记忆了。
        auto *installTop = new QHBoxLayout();
        installTop->setSpacing(8);
        installBackButton = new QPushButton(installBackButtonText(), installPage);
        installBackButton->setObjectName(installBackButtonName());
        // 「← 返回」= 往回走（一般性）。
        Theme::markButton(installBackButton, Theme::ButtonKind::Neutral);
        installBackButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        installTop->addWidget(installBackButton);
        installTargetLabel = new QLabel(installPage);
        installTargetLabel->setObjectName(installTargetLabelName());
        installTop->addWidget(installTargetLabel);
        installTop->addStretch(1);
        installLayout->addLayout(installTop);

        // 版本名称：实例的目录名（`versions/<这个名字>`）。默认名、重名提示、名字不合法
        // 那句原因，都在 MainWindow::updateInstallNameHint() 一处刷（界面这边不持有
        // 业务状态：它不知道盘上有哪些目录）。
        QWidget *nameBox = makeHintedControl(installPage);
        auto *nameLabel = new QLabel(installNameLabelText(), nameBox);
        nameBox->layout()->addWidget(nameLabel);
        installNameEdit = new QLineEdit(nameBox);
        installNameEdit->setObjectName(installNameEditName());
        nameBox->layout()->addWidget(installNameEdit);
        installNameHint = addDynamicHint(nameBox, QString(), nameBox);
        installNameHint->setObjectName(installNameHintName());
        // 名字不能用（有非法字符、空着）时的**原因**另起一行红字，不去顶掉上面那句
        // 说明（用户 2026-09-23 原话："如果版本名字存在非法字符，不要替换掉那个「装好后
        // 在实例...」，而是在下面添加一行红色字"）。平时它空着、也藏起来 —— 藏起来而不是
        // "留一个空行"，是为了正常情况下名字框底下那几行的位置和以前一模一样。
        // 显隐和文字都由 MainWindow::updateInstallNameHint() 一处刷（界面这边不知道盘上
        // 有哪些目录、也不知道什么名字算不能用）。
        installNameError = addDynamicErrorHint(nameBox, nameBox);
        installNameError->setObjectName(installNameErrorName());
        installNameError->setVisible(false);
        installLayout->addWidget(nameBox);

        // 加载器：**一棵树**，和上一页的版本树同一个模式（用户原话见头文件里那段）：
        // 顶层五条 = 四个加载器 + 「不装（纯原版）」，点一条展开它底下的版本号，
        // 点一个版本号就收起来、把选中的版本号写在那一行的第二行小字上。
        //
        // NOTE: 这一组就是原来左栏那五个钮 —— 表、下标、文案一个字没改（见
        //       modLoaderOptionNames()），换掉的是"单选钮 + 右边一个组合框"那个形状。
        //
        // ★ 2026-10-12：原来这里是一个 `QGroupBox("模组加载器")`，现在换成设置页那种
        //   「标题 + 小节卡片」（见 `addSettingsSection()`）。理由就是用户那份改版文档
        //   第三条 —— 「右侧只有一个主要内容表面（不再用 `QGroupBox` 套起来）」：这一页
        //   整块已经长在正文那个面上（`wtfmlSurface="workspace"`），再套一个带边的
        //   `QGroupBox` 就是"框套框"，而那圈边和正文面那圈边挨得只有 10 px，屏幕上就是
        //   两条平行的框。换成卡片之后这一节和设置页那些小节长得一模一样，缺口式标题也
        //   没有了。**这是产品界面上最后一个 QGroupBox**。
        auto *loaderCard = new QFrame(installPage);
        markSurface(loaderCard, Theme::sectionSurfaceValue());
        auto *loaderLayout = new QVBoxLayout(loaderCard);
        // 和设置页那些小节同一圈内边距/行距（文档里给的 18,16,18,18 / 12）。
        loaderLayout->setContentsMargins(18, 16, 18, 18);
        loaderLayout->setSpacing(12);
        {
            auto *loaderTitle = new QLabel(modLoaderLabelText(), loaderCard);
            markTextRole(loaderTitle, Theme::sectionTitleTextRoleValue());
            QFont titleFont = loaderTitle->font();
            titleFont.setBold(true);
            loaderTitle->setFont(titleFont);
            loaderLayout->addWidget(loaderTitle);
        }
        installLayout->addWidget(loaderCard);

        loaderTree = new QTreeWidget(loaderCard);
        loaderTree->setObjectName(loaderTreeName());
        // 竖的那几条设置和版本树那边**一模一样**（表头藏起来、行高常数、左边那个小三角
        // 不画、同一个代理、同一个字号偏移）—— 用户要的就是"和上一页相同的模式"，
        // 两边长得不一样的话那句话就只兑现了一半。
        loaderTree->setHeaderHidden(true);
        // ★ 树**自己不画那圈边**（2026-10-12，和实例列表、文件夹列表、下载页左栏那三张
        //   同一条规矩）：它四周已经是上面那张小节卡片，再套一圈又是"框套框"。
        loaderTree->setFrameShape(QFrame::NoFrame);
        loaderTree->setUniformRowHeights(true);
        loaderTree->setRootIsDecorated(false);
        loaderTree->setColumnCount(1);
        // 末一个参数 false：这棵树的**叶子**（两档 + 具体版本号）右边不画那个 `>` ——
        // 那一层是"选中它"，底下没有下一页（见 VersionTreeDelegate 的构造函数）。
        // 顶层那五条照画（它们是真的能展开）。
        loaderTree->setItemDelegate(new VersionTreeDelegate(loaderTree, /*leafArrow=*/false));
        // ★ 悬停底色要它，理由见上面版本树那第一处：视图不填 State_MouseOver 就不出底色，也不报错。
        loaderTree->setMouseTracking(true);
        loaderTree->setExpandsOnDoubleClick(false);
        // 展开/折叠动画（2026-09-29）。NOTE: 这棵树的高度是**跟着展开了几条**现算的
        // （下面那个 fitLoaderTreeHeight，挂在 expanded/collapsed 上），所以高度是"啪"地
        // 让出地方、版本号再滑进来 —— 两段不是同一拍，但是看着是对的（先空出来、再填上）。
        Anim::animateExpansion(loaderTree);
        // 展开/收起是"点那一条"完成的，而点中一条会把它整行画成选中色（和版本树那边
        // 一样）。这里**不要**再自己画高亮：选中色就是"你现在停在哪一条"。
        QFont loaderFont = loaderTree->font();
        loaderFont.setPointSize(loaderFont.pointSize() + versionTreeFontStep());
        loaderTree->setFont(loaderFont);
        // 五条加载器（+ 展开出来的版本号）都用同一个标志尺寸，和版本树上那 18 px 一致。
        loaderTree->setIconSize(QSize(versionTreeIconSize(), versionTreeIconSize()));
        loaderLayout->addWidget(loaderTree);

        // 顶层五条：四个加载器 + 最后那条「不装（纯原版）」。顺序、下标、文案全走
        // modLoaderOptionNames()/modLoaderOptionTexts() 那两张表（见头文件里那段 NOTE）。
        {
            const QStringList texts = modLoaderOptionTexts();
            for (int i = 0; i < texts.size(); ++i) {
                auto *item = new QTreeWidgetItem(loaderTree);
                item->setData(loaderTreeKeyColumn(), Qt::UserRole, i);
                item->setText(loaderTreeKeyColumn(), texts.at(i));
                // 每条前面那个标志（Fabric 的织纹 / Forge 的铁砧 / NeoForge 的狐狸 /
                // OptiFine 的放大镜；「不装」那条是草方块 —— 那条路装出来的就是原版）。
                // 画法是 ui/VersionIcons.cpp，那几个图标的画法表和这张表**同序**；
                // 给的是整数下标，越界时它自己会退回草方块。
                item->setIcon(loaderTreeKeyColumn(), QIcon(VersionIcons::loaderMark(i, versionTreeIconSize())));
                // 「不装（纯原版）」那条**没有**版本可选（原版没有加载器版本），所以它
                // 底下一直是空的、也不长展开箭头（代理按"有没有孩子"决定画不画）。
                // NOTE: 这一条**不是凑数**：它是"回到装纯原版"那条路，初始选中的就是它。
            }
        }
        // 这棵树的高度**跟着"现在展开了几条"变**：五条全收着就是 5 行高，展开一条之后
        // 按 5 + 那条的版本数往上长，长到 loaderTreeMaxRows() 行就停（再多出滚动条）。
        // 不这么做的话：写死 5 行高，展开之后版本号全挤在一条一行高的缝里看不见；
        // 写死"铺满整页"，那五条收着的时候底下空一大片、安装按钮还被推到窗口最底下。
        // 挂在 expanded/collapsed 上（而不是让 MainWindow 每次灌完数据记得调一下）：
        // 展开这件事本来就发生在这棵树自己身上。
        // NOTE: 这个 lambda 只捕获树的指针、**不捕获 this** —— 它挂在树上（context 也是树），
        //       而 MainWindowUI 本身不是 QObject（它只是搭界面的那个类，连着两句都用
        //       树自己当 context）。
        const auto fitLoaderTreeHeight = [tree = loaderTree]() {
            if (tree == nullptr) {
                return;
            }
            int rows = 0;
            for (int i = 0; i < tree->topLevelItemCount(); ++i) {
                QTreeWidgetItem *top = tree->topLevelItem(i);
                rows += 1 + (top->isExpanded() ? top->childCount() : 0);
            }
            rows = qBound(tree->topLevelItemCount(), rows, loaderTreeMaxRows());
            const int frame = 2 * tree->frameWidth();
            tree->setFixedHeight(rows * versionTreeRowHeight() + frame);
        };
        // NOTE: 写 QObject::connect 而不是 connect —— MainWindowUI **不是** QObject
        //       （见类声明），成员那版 connect 在这儿不存在；直接写 connect 编译器会
        //       去找一个叫 connect 的普通函数。这两句用树自己当 context。
        QObject::connect(loaderTree, &QTreeWidget::itemExpanded, loaderTree,
                         [fitLoaderTreeHeight]() { fitLoaderTreeHeight(); });
        QObject::connect(loaderTree, &QTreeWidget::itemCollapsed, loaderTree,
                         [fitLoaderTreeHeight]() { fitLoaderTreeHeight(); });
        // NOTE: 「这一棵树现在该显示成什么样」（哪条写着"与 X 不兼容"、哪条写着选中的
        //       版本号）一件都不在这里做 —— 那是 MainWindow::updateLoaderRows() 的活，
        //       它按"选中了哪个加载器"一处重算。这里只把五条搭出来。
        fitLoaderTreeHeight();

        installButton = new QPushButton(installButtonText(), installPage);
        installButton->setObjectName(installButtonName());
        installButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        installButton->setMinimumWidth(kDownloadButtonMinWidth);
        // 字放大两档、给一个最小高度（字大了还顶着老高度会显得挤）。
        //
        // ★ 2026-10-12：摆法从"这一栏里横向居中"改成了**右下角的固定底栏**。用户那份
        //   改版文档里的原话是「确认操作放右下角或固定底栏，不悬浮在页面中间」—— 居中
        //   那一版正是"悬浮在页面中间"：上面是表单、下面是深浅不定的一截空白，这颗键
        //   落在哪儿全看那一栏当时多高。现在它和表单之间有一条分隔线（`wtfmlSurface="footer"`
        //   那条 `border-top`），摆位在**右下角**，和"填完表、按右下角那一颗"这条路上的
        //   直觉一致。
        //
        // NOTE: 表格仍然是"内容在上、底栏在下"，只是中间不再垫 addStretch() —— 底栏跟着
        //       内容走（`installLayout` 里最后那一条 stretch 留着，见下面），所以加载器
        //       展开之后它不会被顶出可视区。
        QFont installFont = installButton->font();
        installFont.setPointSize(installFont.pointSize() + installButtonFontStep());
        installButton->setFont(installFont);
        installButton->setMinimumHeight(installButtonMinHeight());
        // 底栏那一块：一条分隔线 + 里面右对齐的操作（那颗键靠右，前面垫一条 stretch）。
        auto *installFooter = new QFrame(installPage);
        installFooter->setObjectName(installFooterName());
        markSurface(installFooter, Theme::footerSurfaceValue());
        auto *footerLayout = new QHBoxLayout(installFooter);
        footerLayout->setContentsMargins(0, 12, 0, 0);
        footerLayout->setSpacing(kFilterRowSpacing);
        footerLayout->addStretch(1);
        footerLayout->addWidget(installButton);
        installLayout->addWidget(installFooter);
        // 底下那一大块空着是**刻意**的：加载器那条展开之后的版本号有多长说不准，
        // 给按钮留出"余量"，免得它贴着窗口底边。
        installLayout->addStretch(1);
        gameStack->addWidget(installPage);

        // 第 0 页加在**最前面** —— 下面那个循环按同一个表、同一个顺序往后加，
        // 于是"第 i 个分类 = 第 i 页"这条对应关系成立（验证工程会数一遍两边的条数）。
        downloadStack->addWidget(gamePage);

        // ---- 其余几页：一行筛子 + 一棵三层树（资源 → 游戏版本 → 版本号）----
        //
        // 这几页的控件**一模一样**，只有搜索框里那句提示文字不同，所以这里是循环几遍，
        // 不是抄几份。三层的那条路就是用户描述的那条（见头文件里 downloadResourceTreeName()
        // 上面那段）：
        //   第一层 资源 → 点开是游戏版本 → 点开某个游戏版本**这时才去问接口** → 第三层版本号
        // 关键是"什么在哪儿灌"：这一层只**搭**（空树、空框），几条路的数据都由 MainWindow 灌
        //（和版本树/加载器树那两棵一样，界面这边一条业务判据都不写）。
        for (int i = 0; i < categoryKeys.size(); ++i) {
            if (i == downloadCategoryGameIndex()) {
                continue;
            }
            const QString categoryKey = categoryKeys.value(i);
            QWidget *page = new QWidget(downloadStack);
            page->setObjectName(categoryKey + QStringLiteral("Page"));
            auto *pageLayout = new QVBoxLayout(page);
            pageLayout->setContentsMargins(0, 0, 0, 0);
            pageLayout->setSpacing(10);

            // ---- 顶上那一行筛子（2026-09-25）----
            //
            // 用户原话见头文件里 downloadFilterVersionComboName() 上面那段。摆法是**两行**：
            //   第一行：名称（就是原来那个搜索框，占满剩下的宽度）+「搜索」+「清空条件」
            //           （**整合包那一页**还多一颗「导入整合包…」）
            //   第二行：版本 / 模组加载器 / 来源 / 类型 四个下拉框，每个前面带一个小标签
            // 为什么不摆成一行：七样东西摆不下 —— 900 px 宽的窗口里右栏只剩 600 px 上下，
            // 七个控件挤进去每个 80 px，下拉框会被压到只剩一个箭头，那还不如不给。
            // 为什么名称单独占第一行的左边：它是**自由输入**的（用户原话「名字/版本可自行
            // 输入」），要占满剩下的宽度才好打字。
            QWidget *filterRow = new QWidget(page);
            auto *filterLayout = new QVBoxLayout(filterRow);
            filterLayout->setContentsMargins(0, 0, 0, 0);
            filterLayout->setSpacing(kFilterRowSpacing);
            auto *filterTop = new QHBoxLayout;
            filterTop->setSpacing(kFilterRowSpacing);
            auto *filterBottom = new QHBoxLayout;
            filterBottom->setSpacing(kFilterRowSpacing);

            // 名称那一格。**回车就搜** —— 用户原话是"上方预留搜索功能（如果简单就直接做了）"
            // 加 2026-09-25 那句"这两个框选中时按回车等同于按搜索键"，那就做成能用的。
            // NOTE: 不加"清除"那个小叉（setClearButtonEnabled）：它在输入框里塞一个真的
            //       子按钮，而这个页面上的控件形状是被验证工程一条一条数着的；
            //       要清空有旁边那颗「清空条件」。
            //       提示文字是那页"能干什么"的一句话（downloadSearchHint，唯一出处）——
            //       空框里那句话正好在最该看见它的地方。
            // ★ 2026-09-28：那颗「← 返回实例设置」**不在这一行里了** —— 用户原话
            //   「返回按钮也是放在左上角（就是左边栏的顶上）」。它现在是整个下载页
            //   **共用的一颗**、摆在左栏顶上，见 setupUi 里那一句（那一颗有两件事
            //   只有那一层知道：左栏是共用的，而"从实例进来的"这件事本来就与哪一栏无关）。

            auto *search = new QLineEdit(filterRow);
            search->setObjectName(downloadSearchEditName(categoryKey));
            search->setPlaceholderText(downloadSearchHint(i));
            filterTop->addWidget(search, 1);

            // 四个下拉框共用一套竖设置：宽度按"最短内容长度"算（不按内容算，理由见
            // installedVersionCombo 那一段 —— 这一层搭出来的这一刻框还是空的，按内容算就会
            // 定在"空框"那个宽度上，之后灌进长版本号也再也跟不上）、宽度再来一个上限
            //（四个框加起来顶不破整窗那个 900 —— 那个 934 就是这么来的，见头文件里
            // downloadFilterComboMaxWidth() 那段）、装滚轮防护。
            const auto makeFilterCombo = [&](const QString &objectName) {
                auto *combo = new QComboBox(filterRow);
                combo->setObjectName(objectName);
                combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
                combo->setMinimumContentsLength(downloadFilterMinimumContentsLength());
                combo->setMaximumWidth(downloadFilterComboMaxWidth());
                WheelGuard::install(combo);
                return combo;
            };
            // 一个"标签 + 下拉框"的小组。标签只为说明右边那个框是筛什么用的 —— 这四样
            // 没有一样能从框里的内容猜出来（尤其「全部」那一项在四个框里长得一模一样）。
            const auto addFilterLabel = [&](QLayout *row, const QString &text) {
                auto *label = new QLabel(text, filterRow);
                label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
                row->addWidget(label);
            };

            // 版本：**可编辑的下拉框** —— 上面那张清单里挑一个，也能自己打一个（用户原话
            // 「版本可自行输入」）。清单由 MainWindow 灌（它才知道有哪些游戏版本），
            // 灌之前它就是空的一个框。回车这一条也由 MainWindow 接（可编辑下拉框内部那个
            // QLineEdit 是 `combo->lineEdit()`，这一层不接信号）。
            addFilterLabel(filterBottom, downloadFilterVersionLabel());
            QComboBox *versionCombo = makeFilterCombo(downloadFilterVersionComboName(categoryKey));
            versionCombo->setEditable(true);
            // NOTE: 可输入的框上**必须**把"回车就把框里的字追进清单"这条关掉（Qt 的默认是
            //       `InsertAtBottom`，而且 duplicatesEnabled 默认开着 ⇒ 连重复项都照追）。
            //       这一格存在的理由正是"手打一个清单里没有的版本号"（用户原话「版本可自行
            //       输入」），于是每按一次回车就多一项：清单越用越脏，而追进去的那一项永远
            //       也搜不出东西来（它不在任何一家接口的版本表里）。
            versionCombo->setInsertPolicy(QComboBox::NoInsert);
            versionCombo->setPlaceholderText(downloadFilterVersionPlaceholder());
            filterBottom->addWidget(versionCombo);

            // 模组加载器：**只有模组那一页有**（用户原话「模组加载器只在模组那栏存在」）。
            // 别的几页连控件都不建 —— 建了再藏起来不叫"只在模组那栏存在"（验证工程按名字
            // 能把它找出来）。判据是**那一页的下标**（downloadCategoryModIndex()，和
            // 「游戏」那条一个规矩）：这张表要再插一条，「模组」那个号跟着挪，别在调用点
            // 再写一遍 "mod" 那个字面量。
            QComboBox *loaderCombo = nullptr;
            if (i == downloadCategoryModIndex()) {
                addFilterLabel(filterBottom, downloadFilterLoaderLabel());
                loaderCombo = makeFilterCombo(downloadFilterLoaderComboName(categoryKey));
                filterBottom->addWidget(loaderCombo);
            }

            // 来源 / 类型：两页都有。类型那一栏**整个会藏起来**（连标签一起）——
            // `categoryOptions()` 回空表的时候（实测只有 Modrinth 的数据包那一档），
            // 摆一个只有「全部」的框等于骗人。显隐由 MainWindow 按那张表决定，这一层
            // 只是把标签也一起放在能藏的地方（标签进 layout、留个指针不管不行，所以这里
            // 用一个容器把"标签 + 框"包成一件东西）。
            addFilterLabel(filterBottom, downloadFilterSourceLabel());
            QComboBox *sourceCombo = makeFilterCombo(downloadFilterSourceComboName(categoryKey));
            filterBottom->addWidget(sourceCombo);

            QWidget *typeBox = new QWidget(filterRow);
            auto *typeLayout = new QHBoxLayout(typeBox);
            typeLayout->setContentsMargins(0, 0, 0, 0);
            typeLayout->setSpacing(kFilterRowSpacing);
            addFilterLabel(typeLayout, downloadFilterTypeLabel());
            QComboBox *typeCombo = makeFilterCombo(downloadFilterTypeComboName(categoryKey));
            typeLayout->addWidget(typeCombo);
            filterBottom->addWidget(typeBox);

            filterBottom->addStretch(1);

            // 「搜索」和「清空条件」两颗键（用户原话「一个搜索键和一个清空条件键」）。
            // 这一层只把它们摆上；点下去干什么、以及"名字/版本那两个框里按回车"这条，
            // 都接在 MainWindow 那一侧（同一个槽）。
            auto *searchButton = new QPushButton(downloadFilterSearchText(), filterRow);
            searchButton->setObjectName(downloadFilterSearchButtonName(categoryKey));
            // NOTE: 这里**不设** setAutoDefault —— 那个开关只在 QDialog 里有意义（它管的是
            //       "对话框里按回车走哪颗键"），而这一页住在 QMainWindow 里，设了也是张白纸。
            //       用户要的那条"名字/版本框里按回车 = 按搜索键"是 MainWindow 显式接的
            //      （两个框各自的 returnPressed → 同一个槽），不靠默认按钮。
            filterTop->addWidget(searchButton);
            auto *resetButton = new QPushButton(downloadFilterResetText(), filterRow);
            resetButton->setObjectName(downloadFilterResetButtonName(categoryKey));
            // 用户点名的例子：「清空搜索」= 一般性（那颗「搜索」是确认性，所以它是绿的）。
            Theme::markButton(resetButton, Theme::ButtonKind::Neutral);
            filterTop->addWidget(resetButton);

            // 整合包那一页那颗「导入整合包…」（用户原话「整合包那一页带一个导入功能」）。
            // 字和实例页那颗共用（importModpackText()），走的路也是同一条。
            if (i == downloadCategoryModpackIndex()) {
                downloadImportModpackButton = new QPushButton(importModpackText(), filterRow);
                downloadImportModpackButton->setObjectName(downloadFilterImportModpackButtonName());
                filterTop->addWidget(downloadImportModpackButton);
            }

            filterLayout->addLayout(filterTop);
            filterLayout->addLayout(filterBottom);
            pageLayout->addWidget(filterRow);

            // 下面那棵树：三层，和上一页那两棵用**同一个代理、同一套竖设置**（表头藏起来、
            // 行高常数、左边那个小三角不画、同一个字号偏移），所以长得一样。
            // 只有两处不一样：
            //   ① 图标大一点（resourceTreeIconSize()）—— 要看清缩略图，18 px 那个不行；
            //   ② 叶子那一层**画**箭头（leafArrow=true）—— 第三层"点一个版本号"是**下它**，
            //      是个真动作（"点进去"那层意思），和加载器树上"选中它"不是一回事。
            QTreeWidget *tree = new QTreeWidget(page);
            tree->setObjectName(downloadResourceTreeName(categoryKey));
            tree->setHeaderHidden(true);
            tree->setUniformRowHeights(true);
            tree->setRootIsDecorated(false);
            tree->setColumnCount(1);
            tree->setItemDelegate(
                new VersionTreeDelegate(tree, /*leafArrow=*/true, resourceTreeIconSize()));
            // ★ 悬停底色要它，理由见版本树那第一处：视图不填 State_MouseOver 就不出底色，也不报错。
            tree->setMouseTracking(true);
            tree->setExpandsOnDoubleClick(false);
            QFont resourceFont = tree->font();
            resourceFont.setPointSize(resourceFont.pointSize() + versionTreeFontStep());
            tree->setFont(resourceFont);
            tree->setIconSize(QSize(resourceTreeIconSize(), resourceTreeIconSize()));
            // 展开/折叠动画（2026-09-29）：和版本树那两棵同一句（三个层次都共用同一个代理，
            // 动画也共用同一个开关），见 ui/Animations.hpp。
            Anim::animateExpansion(tree);
            pageLayout->addWidget(tree, 1);

            downloadResourceSearches.insert(categoryKey, search);
            downloadResourceTrees.insert(categoryKey, tree);
            downloadFilterVersionCombos.insert(categoryKey, versionCombo);
            downloadFilterSourceCombos.insert(categoryKey, sourceCombo);
            downloadFilterTypeCombos.insert(categoryKey, typeCombo);
            downloadFilterTypeBoxes.insert(categoryKey, typeBox);
            downloadFilterSearchButtons.insert(categoryKey, searchButton);
            downloadFilterResetButtons.insert(categoryKey, resetButton);
            // 加载器那一格**只有模组页才登记** —— 别的页没有这一项，按它们的键取回来是
            // nullptr（那正是"只在模组那栏存在"要的形状，见头文件里那段）。
            if (loaderCombo != nullptr) {
                downloadFilterLoaderCombos.insert(categoryKey, loaderCombo);
            }
            downloadStack->addWidget(page);
        }
        // 打开下载页时停在「游戏」（= 原来那个行为）。
        downloadCategoryList->setCurrentRow(downloadCategoryGameIndex());
    }

    // ---------- 设置：左 = 选项名称，右 = 选项内容 ----------
    QWidget *settingsTab = new ThemeBackground(Theme::Part::PageBackground, tabWidget);
    settingsTab->setObjectName(settingsTabName());
    setupSettingsPage(settingsTab);

    // NOTE: 顺序就是契约：homeTabIndex() == 0 靠的就是"主页第一个 addTab"。
    tabWidget->addTab(homeTab, QStringLiteral("主页"));
    tabWidget->addTab(instanceTab, QStringLiteral("实例"));
    tabWidget->addTab(downloadTab, QStringLiteral("下载"));
    tabWidget->addTab(settingsTab, QStringLiteral("设置"));
    mainLayout->addWidget(tabWidget, 1);

    // ================= 那四个选项卡搬到顶上那一条上 =================
    // 用户原话：「把四个选项卡放到最顶上去，通过顶端按钮打开四个功能」。
    //
    // 做法是"条上一排按钮 + 把 QTabWidget 自己那条标签栏藏起来"，而不是把 QTabBar 搬出来：
    // 标签栏的几何归 QTabWidget 管（和内容区那条接缝、圆角都是它画的），搬出来只会得到一个
    // 位置还会被拽回去的条。藏起来的后果是好的那一半 —— Qt 的 `initStyleOption()` 里那句
    // `if (d->tabs->isVisibleTo(this))` 一判，`tabBarSize` 归零，于是 `contentsRect` 变成
    // 整个 QTabWidget：**内容页铺满整块**（所以覆盖层也跟着铺满，见 OverlayHost）。
    tabWidget->tabBar()->hide();
    // 标签**就是** tabWidget 自己的那一份：名字、顺序、条数都只有一处出处。
    // NOTE: 这一句必须在上面四个 addTab **之后** —— tabText() 这会儿才有东西。
    QStringList navLabels;
    for (int i = 0; i < tabWidget->count(); ++i) {
        navLabels << tabWidget->tabText(i);
    }
    bar->setNavItems(navLabels);
    bar->setCurrentNav(tabWidget->currentIndex());
    // 条只发信号、不自己动手（见 TitleBar.hpp）：翻页是这一层的事。
    QObject::connect(bar, &TitleBar::navRequested, tabWidget, [this](int index) {
        // ★ **先**记下这一按是从哪一颗上按下去的（= 此刻这一颗，翻页之前）。下面那一句
        //   `setCurrentIndex()` 一执行，这个事实就没了 —— 而 `MainWindow` 那条槽醒过来的时候
        //   它已经执行完了（见 `lastNavSourceTab()` 的注释）。
        m_lastNavSourceTab = tabWidget->currentIndex();
        // 用户原话：「顶端四个按钮点击以后自动到达对应选项卡的首页（比如说我现在正点开
        // sodium 的下载页面，现在我点击启动选项卡，直接切到那个页面，下载这个"弹窗"自动
        // 收起）」+「在下载页点击 下载（顶上那个，不是下载确认键），那就回到下载页的首页
        // （四个都适用）」。于是这一颗有两种意思：
        //
        // ① 点的**不是**现在这一颗 ⇒ 翻页。切走之后那页不会丢，只是"收着"（见
        //    OverlayHost.hpp 那段），切回来还是原来那个界面、连滚到哪儿都在；
        // ② 点的**就是**现在这一颗 ⇒ "回到这个功能的首页"：把上面那一层收掉 —— 页面那一档
        //    走"返回箭头"那一发（`execPage()` 的那一次才醒得过来，理由见 dismiss()）。
        if (index == tabWidget->currentIndex()) {
            if (overlayHost != nullptr) {
                overlayHost->dismiss();
            }
            return;
        }
        tabWidget->setCurrentIndex(index);
    });
    // 反方向也要通：别处（底栏、主页那几个入口）翻页之后，条上标着的也得跟着换。
    QObject::connect(tabWidget, &QTabWidget::currentChanged, bar, &TitleBar::setCurrentNav);

    // ================= 换页那一下 =================
    // 2026-09-29 用户点名要的第二个动效（「切换页面」）：新那一块**先等一拍、再淡进来**，
    // 顺带从下面抬 `Anim::pageRiseDistance()` 那么一小段。规矩（尤其是"一个像素的几何都
    // 不动"）见 ui/Animations.hpp。
    //
    // ★ 这里动的**是内容区整个控件**（`tabWidget` 自己），**不是** `currentWidget()` 那一页。
    //   用户 2026-09-29 原话：「这个框架啥的都是瞬间出现，能不能给它设定一定延迟」（他指名
    //   的是「换页时整页的卡片」）。原因在这一层底下：那张卡片和它的外框**是 `QTabWidget`
    //   自己画的** —— 四个页面都是 `ThemeBackground(Part::PageBackground)`，而它在"没设图"
    //   时 `paintEvent()` 直接 return（见那个文件头上那一大段）。所以只给页面装效果的话，
    //   卡片和外框不在这张图里，淡的是"卡片上浮着的一层内容"，虚的那几帧能看见卡片那层底
    //   从中间透出来。
    //
    // ★★ 2026-10-11 改的是**抬**那一半：淡的仍然是整块（卡片一起淡），但**往下挪着画的只有
    //   它里面的页面**（`ui/Animations.cpp` 里那个文件内的 `liftTarget()` 把它认出来）。用户原话：「这个动画，在有框的
    //   时候框会瞬移，能不能考虑一下用其它组件之类的解决」—— 卡片那圈外框换页前后都在同一
    //   个地方，整块一起挪就是**框自己跳了一下再滑回来**；内容是新上来的，没有参照物，反倒
    //   像正常的滑入。整段理由与做法见 `Animations.hpp` 里 `pageAppear()` 那段。
    //
    // NOTE: 垫的底色是**卡片那一层**（`QPalette::Base`）：内容抬起来让出来的那一条露的就是
    //       它。垫成窗口底色（`#f0f0f0`）的话，卡片上会横着一条偏灰的带子（卡片自己是
    //       `#fcfcfc`，Fusion 画这一层用的就是 `Base`）。
    // NOTE: 挂的是 `currentChanged`，不是某一次点击 —— 四颗导航、底栏、主页那几个入口
    //       都会翻页，只有挂在这儿才一处管住全部（"唯一出处"那条老规矩）。
    // NOTE: 只在**已经露着头**的时候动。搭界面这一路会挨个 `addTab()`，`currentChanged`
    //       在那时候就会响，而那些页还没上屏、几何也没定下来，给它们装效果纯属白干
    //       （`tabWidget` 是 `centralWidget` 里生出来的，这条判据在它身上同样说得通）。
    QObject::connect(tabWidget, &QTabWidget::currentChanged, tabWidget, [this](int) {
        if (tabWidget->isVisible()) {
            Anim::pageAppear(tabWidget, tabWidget->palette().color(QPalette::Base));
        }
    });

    // 内层那三层"换页"走同一条路（用户说的"切换页面"不只顶上那四颗）：
    //   * 下载页右栏：游戏 / 模组 / 资源包 / 数据包 / 光影 / 整合包 之间横着换；
    //   * 下载页里"游戏"那一栏自己还有一层（版本树 ↔ 安装页）；
    //   * 设置页右栏：启动 / 游戏 / 提示 / 个性化 / 下载。
    // 都是 `QStackedWidget`，所以一条 `currentChanged` 就够了（谁换的都算）。
    //
    // ★ 垫的底色是 `QPalette::Base`（**不是**顶上那一档的窗口底色）：这三层右栏每一页都
    //   活在一层滚动区里，视口铺的是 `Base` —— 垫窗口那份的话，起手那一帧会看见一整块
    //   偏灰的方块贴在页面正中（见 `Anim::pageAppear(widget, backdrop)` 那段）。
    // ★ 这几页是滚动区：抬的会是它们的**视口**（`liftTarget()`，`ui/Animations.cpp` 里那个），滚动区自己那圈框
    //   留在原地 —— 和顶上那一档同一条规矩（见 `Animations.hpp` 里 `pageAppear()` 那段）。
    const auto appearOnSwitch = [](QStackedWidget *stack) {
        if (stack == nullptr) {
            return;
        }
        QObject::connect(stack, &QStackedWidget::currentChanged, stack, [stack](int) {
            QWidget *page = stack->currentWidget();
            if (page != nullptr && page->isVisible()) {
                Anim::pageAppear(page, page->palette().color(QPalette::Base));
            }
        });
    };
    appearOnSwitch(downloadStack);
    appearOnSwitch(gameStack);
    appearOnSwitch(settingsCategoryStack);

    setupBottomBar(centralWidget, mainLayout);

    // ---------- 内容区上面那一层 ----------
    // 设置界面和各种提示都开在这儿，不再另开窗口（见 OverlayHost.hpp 开头那段）。
    // NOTE: 建在**最后**：它不吃布局，几何靠盯住 tabWidget 的 Resize/Move 现算，所以
    //       搭它的时候上面那四个 addTab 必须已经跑完了。
    overlayHost = new OverlayHost(tabWidget, centralWidget);
    // ★ 2026-09-27：**提示开着时把底栏一起按住**。这一句必须在底栏建好之后（上面的
    //   setupBottomBar 已经跑完），理由见 OverlayHost::setPromptBlockedWidget()：
    //   底栏不在覆盖层底下（这一层挂在 tabWidget 上），"被盖住 = 点不着"那条判据管不到它，
    //   于是提示开着时底栏照样能点开上拉面板 → 面板里点「改设置」被挡住 → 而那句解释
    //   为什么的话又要被"提示叠提示不做"吞掉 ⇒ 用户点了一下，屏幕上什么都没有。
    overlayHost->setPromptBlockedWidget(bottomBar);

    // ---------- 外框 ----------
    // 客户区铺满整窗 + 条上哪一片能拖（见 WindowChrome.hpp 开头那两段）。**建在最后**：
    // 它的构造会把窗口标志加上 FramelessWindowHint（外框归谁管、标志就谁设），
    // 等上面那些控件都挂完了再动标志，少一层"改到一半"的心。
    windowChrome = new WindowChrome(parent, bar, parent);

    // 空的菜单栏：窗口上那条菜单现在什么都没挂。
    // TODO: 菜单里放"打开 .minecraft 目录 / 关于"这类动作；设置项已经全部搬进
    //       「设置」选项卡了（以前这上面挂着"做设置界面"的待办）。
    parent->setMenuBar(new QMenuBar(parent));
}

namespace {

/// 设置页里一栏那一页的两样东西：表单控件（说明文字的父窗）与它那张 grid。
struct SettingsPanel {
    QWidget *form = nullptr;
    QGridLayout *grid = nullptr;
};

/**
 * @brief 往设置页右栏那份堆叠页里加一栏。
 * @param stack 右栏那份堆叠页。**加进去的顺序就是左栏分类列表的下标顺序**。
 * @param key 分栏的键（settingsCategoryKeys() 里那一个），进两个 objectName。
 * @return 那一页的表单与表格。调用方从第 0 行开始铺，**收尾自己做**
 *         （`grid->setRowStretch(row, 1)`：剩下的竖直空间留底部，设置项不跟着窗高被拉长）。
 *
 * 每一栏**各自**套一层滚动区，不是整页套一层 —— 整页套一层的话，左栏那五条分类会跟着
 * 右栏的内容一起滚走，而那五条是导航。这条和下载页左栏那一列是同一条理由。
 */
SettingsPanel addSettingsPanel(QStackedWidget *stack, const QString &key) {
    QScrollArea *scroll = new QScrollArea(stack);
    scroll->setObjectName(MainWindowUI::settingsPanelName(key));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    // ★ 2026-10-12：设置页那条"内容横向无限伸展、说明文字一行跨得太长"（用户原文）的
    //   正解就是这两句 —— 表单**限量宽**（`settingsFormMaxWidth()`），窗口再宽它也不再
    //   拉长；多出来的地方让滚动区把表单**居中**摆（上对齐：这一页是从上往下读的表）。
    //
    // NOTE: 宽度是给**表单**限的，不是给滚动区限的：滚动条仍然贴着正文面最右边那条线
    //       （和别的页一个规矩），只有表单本身收窄。
    // NOTE: 窗口默认 900 逻辑像素时，正文那一栏只有 620 上下 ⇒ 这个上限还够不着，
    //       拉宽窗口才生效。所以量它的判据（ui_check）量的是 `maximumWidth()` 本身，
    //       不是"这一趟实际多宽"。
    scroll->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

    SettingsPanel panel;
    panel.form = new QWidget(scroll);
    panel.form->setObjectName(MainWindowUI::settingsFormName(key));
    panel.form->setMaximumWidth(MainWindowUI::settingsFormMaxWidth());

    panel.grid = new QGridLayout(panel.form);
    panel.grid->setContentsMargins(0, 0, 0, 0);
    panel.grid->setHorizontalSpacing(12);
    panel.grid->setVerticalSpacing(10);
    // 这一对 3:7 从前是「选项名称放左边、设置内容放右边」用的 —— 现在每一节都是一张跨两列
    // 的卡片（见 addSettingsSection()），所以它只摆表格的形状、不分隔任何一行了。
    panel.grid->setColumnStretch(0, MainWindowUI::leftColumnStretch());
    panel.grid->setColumnStretch(1, MainWindowUI::rightColumnStretch());
    scroll->setWidget(panel.form);

    stack->addWidget(scroll);
    return panel;
}

} // namespace

void MainWindowUI::setupSettingsPage(QWidget *page) {
    // 2026-09-26：设置页**拆成了几条分类**（用户原话：「将设置分栏，分为启动/提示/个性化/…
    // 然后分门别类放进去」）。形状照下载页来：左栏一列分类、右栏一份堆叠页，点哪条换哪页。
    //
    // 从前它是**一张通到底的两列表格**（外观 / 账户 / Java 版本 / 内存分配 / 启动后 /
    // 启动选项 / 启动提醒 / JVM 参数 / 下载源 / 版本名格式，十节一路往下排）。十节连成
    // 一竖条之后"找一个开关"只能靠滚，而且"哪几项是一回事"只能靠相邻关系去猜 —— 分栏把
    // 这件事说明白了：每一栏各自成一页、各滚各的。
    //
    // NOTE: 从前整页靠一层 QScrollArea 才放得下（窗口初始高度只有 620，只 resize 过、
    //       没有 setFixedSize），不给滚动就等于把最后那几项一直藏在可视区外。现在滚动
    //       归**栏**管（addSettingsPanel 里那段），理由见它上面。
    // NOTE: 这一份**不是 const**（另几页那三份是）：页头那行标题是搭完之后才由这一页填的
    //       （`fillPageHeader()` 收的是 `Column&`）。
    TwoColumns columns = buildTwoColumnPage(page, QStringLiteral("settings"));
    // 页头：标题「设置」。**没有说明那一行**（`fillPageHeader()` 收到空串就把它整行收起来
    // 连位置都不占）—— 这一页"是什么"不需要解释，左栏那五条分类已经说完了。
    fillPageHeader(columns.right, settingsPageTitle(), QString());

    const QStringList categoryKeys = settingsCategoryKeys();
    const QStringList categoryTexts = settingsCategoryTexts();

    // ---- 左栏：那五条分类（和下载页左栏那一列同一个代理、同一套写法）----
    settingsCategoryList = new QListWidget(columns.left.surface);
    settingsCategoryList->setObjectName(settingsCategoryListName());
    settingsCategoryList->setUniformItemSizes(true);
    // 同一个代理 = 一样高的行、大一号的字、"分类"那种粗体 —— 两栏里是同一种东西。
    // NOTE: **不设** rowRefreshRole()。那一格里画的刷新圈是下载页专有的（这一列没有
    //       "重新拉一遍"这回事），代理见到没设就不画。
    // 末一个参数 true = 和下载页左栏那一列**同一档**（侧栏导航）。两处的形状一样，
    // 判据也一样（见 VersionTreeDelegate 构造函数那段）。
    settingsCategoryList->setItemDelegate(
        new VersionTreeDelegate(settingsCategoryList, /*leafArrow=*/true, /*iconSize=*/0,
                                /*navRow=*/true));
    // ★ 悬停底色要它，理由见版本树那第一处：视图不填 State_MouseOver 就不出底色，也不报错。
    settingsCategoryList->setMouseTracking(true);
    QFont categoryFont = settingsCategoryList->font();
    categoryFont.setPointSize(categoryFont.pointSize() + versionTreeFontStep());
    settingsCategoryList->setFont(categoryFont);
    for (int i = 0; i < categoryKeys.size(); ++i) {
        auto *item = new QListWidgetItem(categoryTexts.value(i), settingsCategoryList);
        // 身份进 userData：条目文字是显示内容、会随文案改，不能拿它当身份。
        item->setData(Qt::UserRole, categoryKeys.value(i));
    }
    // 高度贴着内容：五条两三个字的条目被拉成一整条空白看着像坏了。行高**交给样式算**
    // （sizeHintForRow），别写死数字 —— 换字体/换样式就错。和下载页那一列同一条规矩。
    int rowsHeight = 0;
    for (int i = 0; i < settingsCategoryList->count(); ++i) {
        rowsHeight += settingsCategoryList->sizeHintForRow(i);
    }
    settingsCategoryList->setFixedHeight(rowsHeight + 2 * settingsCategoryList->frameWidth() + 2);
    columns.left.layout->addWidget(settingsCategoryList);
    columns.left.layout->addStretch(1);

    // ---- 右栏：那一堆页 ----
    settingsCategoryStack = new QStackedWidget(columns.right.widget);
    settingsCategoryStack->setObjectName(settingsCategoryStackName());
    columns.right.layout->addWidget(settingsCategoryStack, 1);

    // 加页的顺序**必须**就是 categoryKeys 的顺序（"点第 N 条 = 第 N 页"是条不变量；
    // ui_check 把两边的条数、以及每一页的 objectName 都核一遍）。
    setupSettingsLaunchPanel(settingsCategoryStack);
    setupSettingsGamePanel(settingsCategoryStack);
    setupSettingsWarnPanel(settingsCategoryStack);
    setupSettingsLookPanel(settingsCategoryStack);
    setupSettingsDownloadPanel(settingsCategoryStack);

    // 越界保护的理由和下载页那条一模一样（见 MainWindow 里那段）：分类表和页数将来要是
    // 对不上，宁可停在原来那页，也不要 setCurrentIndex(-1) 把右栏整片清空 —— 那种
    // "点一下界面就空了"的毛病，用户只会当成崩了。
    QObject::connect(settingsCategoryList, &QListWidget::currentRowChanged, settingsCategoryList,
                     [this](int row) {
                         if (row >= 0 && row < settingsCategoryStack->count()) {
                             settingsCategoryStack->setCurrentIndex(row);
                         }
                     });
    // 打开就是第一条：左栏亮着哪一条、右栏就是哪一页，两边不能各说各的。
    settingsCategoryList->setCurrentRow(0);
    settingsCategoryStack->setCurrentIndex(0);
}

void MainWindowUI::setDownloadHeaderRow(int row) {
    if (downloadHeaderSubtitleLabel == nullptr) {
        return;  // 界面没搭出来（还没调 setupUi）——和别处那些 getter 一个规矩。
    }
    if (row < 0 || row >= downloadCategoryKeys().size()) {
        // 越界什么都不改：页头宁可停在上一句，也不要空成一行（空字符串会把它整行收起来，
        // 页头就"矮"了一截，看着像换了页）。
        return;
    }
    downloadHeaderSubtitleLabel->setText(downloadHeaderSubtitle(row));
    // 说明是**这一页的状态**，永远露着（`fillPageHeader()` 那套"空串就收起来"在这儿
    // 用不上：它管的是"这一页有没有说明"，而这里每一类都有一句）。
    downloadHeaderSubtitleLabel->setVisible(true);
}

void MainWindowUI::setupSettingsLaunchPanel(QStackedWidget *stack) {
    // 这一栏的键。**必须**和 settingsCategoryKeys() 里那一格一字不差（面板的 objectName
    // 由它拼出来），ui_check 拿那张表逐页核。
    const SettingsPanel panel = addSettingsPanel(stack, QStringLiteral("launch"));
    QWidget *form = panel.form;
    QGridLayout *grid = panel.grid;
    int row = 0;

    // ---------- Java 版本 ----------
    // 2026-09-26 分栏时这一节拆成两半：**这一行**（运行时）留在这儿，那行"实例的 Java
    // 对不上时要不要问一句"搬去了「提示」栏 —— 它挡的是启动那一下，和那两条提醒是一回事。
    // ---- 第 1 节：标题 + 一张小节卡片（见 addSettingsSection()）----
    const SettingsSection section1 = addSettingsSection(grid, row, QStringLiteral("Java 版本"), form);
    {
        QWidget *box = makeHintedControl(form);
        auto *boxLayout = qobject_cast<QVBoxLayout *>(box->layout());
        // 2026-09-28：这一格原来是"自动 / 手动 + 本机扫到的那些运行时"的下拉框。用户
        // 点名的改法是把它**换成一个按钮**：挑哪个 Java、哪个缺了，全都归「Java 管理」
        // 那一页管（见 ui/JavaManageDialog）。按钮靠左摆，不跟着列宽抻成一条横杠 ——
        // 它不是这一页的主角，是通往另一页的一扇门。
        javaManageButton = new QPushButton(QStringLiteral("Java 管理…"), box);
        javaManageButton->setObjectName(javaManageButtonName());
        // 「Java 管理…」= 通往另一页的一扇门（一般性）—— 和上面那句注释说的"它不是这一页的
        // 主角"是同一个判断，颜色也跟着这个走。
        Theme::markButton(javaManageButton, Theme::ButtonKind::Neutral);
        boxLayout->addWidget(javaManageButton, 0, Qt::AlignLeft);
        javaPathLabel = new QLabel(box);
        javaPathLabel->setObjectName(javaPathLabelName());
        javaPathLabel->setWordWrap(true);
        javaPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        boxLayout->addWidget(javaPathLabel);
        // NOTE: 这段说的是**全局**这一层，它和实例覆盖层的行为**不一样**，别把两边写成
        //       一回事：全局档挑中的 Java 对不上就是直接报错（不替你换、也不问）；
        //       而「实例设置…」里给某个实例单独钉的 Java 对不上时，会弹一个框问要不要
        //       改用匹配的那个（那个开关在「提示」栏里）。全局档不问，是因为它不匹配时
        //       自动档本来就会挑一个替换掉，钉死的那档是用户对**整台机器**的选择 ——
        //       那种一眼就该是自己配错的，不值得每次启动都问一遍。
        addHint(box, QStringLiteral("按版本要求自动挑：1.16.5 及以下要 Java 8、"
                                    "1.18~1.20.4 要 Java 17、1.20.5~1.21.11 要 Java 21、"
                                    "26.x 要 Java 25（同一个启动器里得能共存）。"
                                    "缺哪一个、或者哪一个坏了打不开，点「Java 管理…」去装。"
                                    "要给某一个实例单独钉一个 Java，用它的「实例设置…」；"
                                    "那边对不上时会弹框问一句（那个开关在「提示」栏里）。"),
                form);
        addSettingItem(section1, QStringLiteral("运行时"), box, form);
    }

    // ---------- 内存分配 ----------
    // ---- 第 2 节：标题 + 一张小节卡片（见 addSettingsSection()）----
    const SettingsSection section2 = addSettingsSection(grid, row, QStringLiteral("内存分配"), form);
    {
        // 第一行是「自动 / 手动」的总开关。自动档下那两个数字框是**禁用**的 ——
        // 它们显示的是刚算出来的建议值，让用户以为能改会变成"改了不生效"。
        QWidget *box = makeHintedControl(form);
        auto *boxLayout = qobject_cast<QVBoxLayout *>(box->layout());
        memoryAutoCheck = new QCheckBox(QStringLiteral("自动（按系统内存）"), box);
        memoryAutoCheck->setObjectName(memoryAutoCheckName());
        boxLayout->addWidget(memoryAutoCheck, 0, Qt::AlignLeft);
        // 文字由 MainWindow 填（要说清系统总内存/当前可用/建议值），这里先给一句占位，
        // 免得空标签把行高压成 0、布局跳一下。
        memoryAutoHint = addDynamicHint(box, QStringLiteral("正在读取系统内存…"), form);
        memoryAutoHint->setObjectName(memoryAutoHintName());
        addSettingItem(section2, QStringLiteral("分配方式"), box, form);
    }
    {
        // 「最大内存」这一行是**一根条 + 一个数字框**（两件东西说的是同一件事）。
        // 条是主要入口（用户原话：「像 pcl 那样子的灵活变通的拖动条」），数字框留着是为了
        // "我就要 6144 这个数"的时候能直接打进去 —— 少了它，拖是拖不到精确值的。
        QWidget *box = makeHintedControl(form);
        auto *boxLayout = qobject_cast<QVBoxLayout *>(box->layout());

        QWidget *line = new QWidget(box);
        auto *lineLayout = new QHBoxLayout(line);
        lineLayout->setContentsMargins(0, 0, 0, 0);
        lineLayout->setSpacing(8);

        memoryMaxSlider = new MemorySlider(line);
        memoryMaxSlider->setObjectName(memoryMaxSliderName());
        // 上限这里是 built-in 那个常数（65536）。**真跑起来时 MainWindow 会按本机物理内存
        // 再收一次口**（见 MainWindow::applyMemoryLimits）：界面层不许读系统内存（它不依赖
        // src/），而"这台机器有多少内存"只有业务层知道。界面单独搭出来时（验证工程里那样）
        // 就是这条常数，够宽、也不假。
        memoryMaxSlider->setRangeMb(memoryMaxMinMb(), memoryLimitMaxMb());
        memoryMaxSlider->setMinimumWidth(kMemorySliderMinWidth);
        // 条也吃滚轮（挪一档）：滚过去扫一眼就把内存挪了，而且没有焦点时**看不出来**
        // —— 和那两个数字框是同一条理由（见 WheelGuard）。
        WheelGuard::install(memoryMaxSlider);
        // 条占满剩下的宽度（stretch 1），数字框按 sizeHint 摆 —— 顺序也是这个：条在左边。
        lineLayout->addWidget(memoryMaxSlider, 1);

        memoryMaxSpin = new QSpinBox(line);
        memoryMaxSpin->setObjectName(memoryMaxSpinName());
        memoryMaxSpin->setRange(memoryMaxMinMb(), memoryLimitMaxMb());
        memoryMaxSpin->setSingleStep(memoryStepMb());
        memoryMaxSpin->setSuffix(QStringLiteral(" MB"));
        // 数字框也吃滚轮（加一步/减一步），而且没有焦点时改完**看不出来** —— 见 WheelGuard。
        WheelGuard::install(memoryMaxSpin);
        memoryMaxSpin->setMinimumWidth(kMemorySpinWidth);
        lineLayout->addWidget(memoryMaxSpin, 0);

        // 这一行要**撑满整格**（不是 addInlineControl 那种"按 sizeHint 靠左"）：条的长度
        // 就是它的精度，短了拖不准。
        boxLayout->addWidget(line);
        addHint(box, QStringLiteral("给 JVM 的 -Xmx。左边拖着改，也可以直接填数字。"
                                    "模组多、整合包大就往上调；机器内存不够时先降这一项。"),
                form);
        addSettingItem(section2, QStringLiteral("最大内存"), box, form);
    }
    // NOTE: 「最小内存」（-Xms）**这一行没有了**（2026-09-26）。用户原话：「不要 xms 了
    //       （或者隐藏起来），按照默认值」—— 界面上不再让他管这一项，值照旧存在设置里
    //       （默认 1024 MB，命令行照发 -Xms），所以"这一项现在是多少"改由「分配方式」
    //       那一行的说明文字说全（见 MainWindow::refreshMemoryUi 里那三支 setText）。

    // ---------- 启动后 ----------
    // ---- 第 3 节：标题 + 一张小节卡片（见 addSettingsSection()）----
    const SettingsSection section3 = addSettingsSection(grid, row, QStringLiteral("启动后"), form);
    {
        QWidget *box = makeHintedControl(form);
        auto *boxLayout = qobject_cast<QVBoxLayout *>(box->layout());
        launcherBehaviourCombo = new QComboBox(box);
        launcherBehaviourCombo->setObjectName(launcherBehaviourComboName());
        WheelGuard::install(launcherBehaviourCombo);
        // ★ 2026-10-11：**每颗下拉框都按 `sizeHint` 靠左摆**（`AlignLeft`）。同一页上原来
        //   只有「窗口模式」那一颗带了它，另几颗吃到拉伸 ⇒ 「进程优先级」那一条（项就
        //   「正常（默认）」几个字）被拉成整行那么宽，右边一大片空白。这几颗是同一件事的
        //   同一个控件（选一项），落到的宽度就该是同一条规矩。见 08a 那张截图。
        boxLayout->addWidget(launcherBehaviourCombo, 0, Qt::AlignLeft);
        addHint(box, QStringLiteral("游戏起来之后启动器自己怎么办。「收起」是把窗口藏起来、"
                                    "进程还在（关掉进程会连带杀掉游戏）。"),
                form);
        addSettingItem(section3, QStringLiteral("启动器行为"), box, form);
    }

    // ---------- JVM 参数 ----------
    // ---- 第 4 节：标题 + 一张小节卡片（见 addSettingsSection()）----
    const SettingsSection section4 = addSettingsSection(grid, row, QStringLiteral("JVM 参数"), form);
    {
        QWidget *box = makeHintedControl(form);
        auto *boxLayout = qobject_cast<QVBoxLayout *>(box->layout());
        extraJvmArgsEdit = new QPlainTextEdit(box);
        extraJvmArgsEdit->setObjectName(extraJvmArgsEditName());
        extraJvmArgsEdit->setPlaceholderText(QStringLiteral("-XX:+UseG1GC -XX:MaxGCPauseMillis=50"));
        // 三四行就够：这一格放的是"顺手加的几条参数"，不是一整份命令行
        extraJvmArgsEdit->setFixedHeight(kJvmArgsEditHeight);
        boxLayout->addWidget(extraJvmArgsEdit);

        QWidget *buttonRow = new QWidget(box);
        QHBoxLayout *buttonRowLayout = new QHBoxLayout(buttonRow);
        buttonRowLayout->setContentsMargins(0, 0, 0, 0);
        applyExtraJvmArgsButton = new QPushButton(QStringLiteral("应用"), buttonRow);
        applyExtraJvmArgsButton->setObjectName(applyExtraJvmArgsButtonName());
        applyExtraJvmArgsButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        applyExtraJvmArgsButton->setMinimumWidth(kApplyJvmArgsButtonMinWidth);
        buttonRowLayout->addWidget(applyExtraJvmArgsButton);
        buttonRowLayout->addStretch(1);
        boxLayout->addWidget(buttonRow);

        addHint(box, QStringLiteral("多条参数用空格分隔（一行一条也行）。改完点「应用」生效 —— "
                                    "不边打字边存，免得打到一半的半条参数被写进配置。"
                                    "按版本的那份写在 .minecraft/versions/<版本>/wtfml.meta "
                                    "的 jvmArgs 里，排在全局之后（同一条以后写的为准）。"),
                form);
        addSettingItem(section4, QStringLiteral("全局额外参数"), box, form);
    }

    // 剩下的竖直空间留在底部：设置项不跟着窗口高度一起被拉长
    grid->setRowStretch(row, 1);
}

void MainWindowUI::setupSettingsGamePanel(QStackedWidget *stack) {
    const SettingsPanel panel = addSettingsPanel(stack, QStringLiteral("game"));
    QWidget *form = panel.form;
    QGridLayout *grid = panel.grid;
    int row = 0;

    // ---------- 启动选项 ----------
    // 这三件事都是"这一份游戏进程怎么起来"：看不看它的输出、它占多少 CPU、窗口开多大。
    // NOTE: 2026-09-26 分栏时这三项单独成了一栏（这一栏的**全部**内容就是它们）—— 从前的
    //       相邻关系里，它们一边挨着「启动后」（那是启动器自己）、一边挨着「启动提醒」
    //       （那是要不要问一句），三件事各自的性质都还没这么清楚。
    // ---- 第 1 节：标题 + 一张小节卡片（见 addSettingsSection()）----
    const SettingsSection section1 = addSettingsSection(grid, row, QStringLiteral("启动选项"), form);
    {
        QWidget *box = makeHintedControl(form);
        auto *boxLayout = qobject_cast<QVBoxLayout *>(box->layout());
        showGameLogCheck = new QCheckBox(showGameLogCheckText(), box);
        showGameLogCheck->setObjectName(showGameLogCheckName());
        // 勾选状态由 MainWindow 灌（它才知道这个开关存在哪），这里只搭控件。
        boxLayout->addWidget(showGameLogCheck, 0, Qt::AlignLeft);
        addHint(box, QStringLiteral("打开之后，每启动一份游戏都会另外开一个小窗，"
                                    "把那一份游戏自己的输出实时写进去 —— 崩溃前那几行报错就在那里。"
                                    "同时开好几份时一份游戏一个窗，不会混在一起。"
                                    "关掉时游戏输出只进日志文件。"),
                form);
        // 行标题用 RowText() 那一句，**不是**勾选框上的那句话（原来两处共用同一个串，
        // 同一句话就并排出现两遍；见 showGameLogRowText() 上面那段）。
        addSettingItem(section1, showGameLogRowText(), box, form);
    }
    {
        QWidget *box = makeHintedControl(form);
        auto *boxLayout = qobject_cast<QVBoxLayout *>(box->layout());
        processPriorityCombo = new QComboBox(box);
        processPriorityCombo->setObjectName(processPriorityComboName());
        WheelGuard::install(processPriorityCombo);
        // ★ 2026-10-11：靠左按 `sizeHint` 摆 —— 理由见「启动器行为」那一颗上面那段。
        boxLayout->addWidget(processPriorityCombo, 0, Qt::AlignLeft);
        addHint(box, QStringLiteral("游戏进程占 CPU 的优先级。这和任务管理器里「设置优先级」"
                                    "是同一件事，只是在这里设一次就一直生效。"
                                    "往低调可以在后台跑着游戏的时候让系统先顾别的；"
                                    "往高调**不解决卡顿** —— 卡顿多半是内存或显卡的事，"
                                    "调高只会让别的程序更卡。"),
                form);
        addSettingItem(section1, QStringLiteral("进程优先级"), box, form);
    }
    {
        QWidget *box = makeHintedControl(form);
        auto *boxLayout = qobject_cast<QVBoxLayout *>(box->layout());
        gameWindowModeCombo = new QComboBox(box);
        gameWindowModeCombo->setObjectName(gameWindowModeComboName());
        WheelGuard::install(gameWindowModeCombo);
        boxLayout->addWidget(gameWindowModeCombo, 0, Qt::AlignLeft);
        addHint(box, QStringLiteral("小窗 = 按下面那个大小开一个普通窗口。"
                                    "最大化 = 开一个铺满屏幕（任务栏那块地方留出来）的窗口，"
                                    "下面那个大小这时不生效。"
                                    "全屏 = 直接进全屏（原版自己的 --fullscreen），"
                                    "下面那个大小这时是「按 F11 退出全屏之后回到多大」。"
                                    // 这几句是**屏幕上给用户看的话**：没有加粗这回事，写
                                    // `**…**` 会原样画成两个星号（「」才是这一套文案里
                                    // 强调的写法）。
                                    "这三档都只影响「启动那一刻」；游戏里自己改过的窗口大小、"
                                    "以及游戏自己记住的那一份，启动器不覆盖。"),
                form);
        addSettingItem(section1, QStringLiteral("窗口模式"), box, form);
    }
    {
        QWidget *box = makeHintedControl(form);
        auto *boxLayout = qobject_cast<QVBoxLayout *>(box->layout());
        QWidget *sizeRow = new QWidget(box);
        QHBoxLayout *sizeLayout = new QHBoxLayout(sizeRow);
        sizeLayout->setContentsMargins(0, 0, 0, 0);
        sizeLayout->setSpacing(kWindowSizeRowSpacing);
        gameWindowWidthSpin = new QSpinBox(sizeRow);
        gameWindowWidthSpin->setObjectName(gameWindowWidthSpinName());
        gameWindowWidthSpin->setRange(windowMinWidth(), windowMaxWidth());
        gameWindowWidthSpin->setSingleStep(windowSizeStep());
        gameWindowWidthSpin->setMinimumWidth(kWindowSizeSpinWidth);
        // 数字框也吃滚轮（加一步/减一步），而且没有焦点时改完看不出来 —— 见 WheelGuard。
        WheelGuard::install(gameWindowWidthSpin);
        sizeLayout->addWidget(gameWindowWidthSpin);
        // 存成成员：MainWindow 要按「窗口模式」把这一格灰/亮（见 updateGameWindowSizeEnabled），
        // 而它够不着这个局部变量 —— MainWindowUI 不是 QObject，`findChild` 那一套用不上。
        gameWindowSizeTimesLabel = new QLabel(QStringLiteral("×"), sizeRow);
        gameWindowSizeTimesLabel->setObjectName(gameWindowSizeTimesLabelName());
        sizeLayout->addWidget(gameWindowSizeTimesLabel);
        gameWindowHeightSpin = new QSpinBox(sizeRow);
        gameWindowHeightSpin->setObjectName(gameWindowHeightSpinName());
        gameWindowHeightSpin->setRange(windowMinHeight(), windowMaxHeight());
        gameWindowHeightSpin->setSingleStep(windowSizeStep());
        gameWindowHeightSpin->setMinimumWidth(kWindowSizeSpinWidth);
        WheelGuard::install(gameWindowHeightSpin);
        sizeLayout->addWidget(gameWindowHeightSpin);
        // 整行按 sizeHint 靠左摆，不撑满整格（内容那一格是整幅宽，两个数字框被拉开就是坏的）——
        // 和 addInlineControl() 是同一条规矩，只是这里那一格是整个小容器而不是单个控件。
        boxLayout->addWidget(sizeRow, 0, Qt::AlignLeft);
        addHint(box, QStringLiteral("启动时按这个大小开窗（单位是像素，走的是原版自己的 "
                                    "--width/--height 那两个参数）。默认的 854×480 就是"
                                    "原版的默认值 —— 没动过这一项时和以前一模一样。"
                                    "游戏内自己把窗口拖大过的话，以游戏记住的为准。"),
                form);
        addSettingItem(section1, QStringLiteral("窗口大小"), box, form);
    }

    // 剩下的竖直空间留在底部：设置项不跟着窗口高度一起被拉长
    grid->setRowStretch(row, 1);
}

void MainWindowUI::setupSettingsWarnPanel(QStackedWidget *stack) {
    const SettingsPanel panel = addSettingsPanel(stack, QStringLiteral("warn"));
    QWidget *form = panel.form;
    QGridLayout *grid = panel.grid;
    int row = 0;

    // ---------- 启动前提醒 ----------
    // 这三条都是**点启动之后、游戏起来之前**挡一下的那种确认框：挡的是"同一份存档被两个
    // 进程同时打开"这件事，而这件事**事后没有任何办法补救**（后写的那一份覆盖先写的）。
    // 所以三条默认都开着；嫌烦的可以在这里各关各的（提示框上那句小字就是告诉他来这儿关）。
    //
    // NOTE: 2026-09-26 分栏时三条并到一栏。前两条（重复启动 / 进度提醒）原来和「启动选项」
    //       挤在同一张表里，第三条（Java 版本）原来挂在「启动」栏的 Java 那一节底下 ——
    //       它们管的是同一件事："现在这一下要不要再问一句"。**勾选框上那句话一个字都没动**，
    //       动的是"它摆在哪儿"和第三条那一行的行标题（原来叫「版本提醒」，说不清是提醒什么）。
    // ---- 第 1 节：标题 + 一张小节卡片（见 addSettingsSection()）----
    const SettingsSection section1 = addSettingsSection(grid, row, QStringLiteral("启动前提醒"), form);
    {
        QWidget *box = makeHintedControl(form);
        auto *boxLayout = qobject_cast<QVBoxLayout *>(box->layout());
        // 勾选状态由 MainWindow 灌（它才知道这个开关存在哪），这里只搭控件 ——
        // 和 memoryAutoCheck 同一条约定。
        javaWarnMismatchCheck =
            new QCheckBox(QStringLiteral("实例指定的 Java 版本不符时提醒"), box);
        javaWarnMismatchCheck->setObjectName(javaWarnMismatchCheckName());
        boxLayout->addWidget(javaWarnMismatchCheck, 0, Qt::AlignLeft);
        addHint(box, QStringLiteral("只对「实例设置…」里单独指定过 Java 的实例起作用。"
                                    "关掉之后不再问，直接按你给它指定的那个 Java 启动 ——"
                                    "版本真的不对时游戏会以 UnsupportedClassVersionError "
                                    "之类的方式失败，得去「实例设置…」里换回来。"),
                form);
        addSettingItem(section1, javaWarnMismatchRowText(), box, form);
    }
    {
        QWidget *box = makeHintedControl(form);
        auto *boxLayout = qobject_cast<QVBoxLayout *>(box->layout());
        warnDuplicateLaunchCheck = new QCheckBox(warnDuplicateLaunchCheckText(), box);
        warnDuplicateLaunchCheck->setObjectName(warnDuplicateLaunchCheckName());
        // 勾选状态由 MainWindow 灌（它才知道这个开关存在哪），这里只搭控件。
        boxLayout->addWidget(warnDuplicateLaunchCheck, 0, Qt::AlignLeft);
        addHint(box, QStringLiteral("用同一个账号、在十秒之内再点一次启动同一个实例时，"
                                    "先问一句再启动 —— 手快连点两下会开出两份一模一样的游戏，"
                                    "而它们开着同一份存档。"),
                form);
        addSettingItem(section1, warnDuplicateLaunchRowText(), box, form);
    }
    {
        QWidget *box = makeHintedControl(form);
        auto *boxLayout = qobject_cast<QVBoxLayout *>(box->layout());
        warnWorldLossCheck = new QCheckBox(warnWorldLossCheckText(), box);
        warnWorldLossCheck->setObjectName(warnWorldLossCheckName());
        boxLayout->addWidget(warnWorldLossCheck, 0, Qt::AlignLeft);
        addHint(box, QStringLiteral("这个实例已经有一份在跑时提醒一句：单人存档被两份游戏同时"
                                    "打开可能会丢进度。换账号启动、或者上一问选了「仍要启动」，"
                                    "走到的都是这一条。"),
                form);
        addSettingItem(section1, warnWorldLossRowText(), box, form);
    }

    // 剩下的竖直空间留在底部：设置项不跟着窗口高度一起被拉长
    grid->setRowStretch(row, 1);
}

void MainWindowUI::setupSettingsLookPanel(QStackedWidget *stack) {
    const SettingsPanel panel = addSettingsPanel(stack, QStringLiteral("look"));
    QWidget *form = panel.form;
    QGridLayout *grid = panel.grid;
    int row = 0;

    // ---------- 外观 ----------
    // 2026-09-25 加的。用户原话：「上面的四个居中，左上角五个字母默认是这五个，在设置里
    // 允许自定义」—— 后半句就是这一行：条上那行字（`TitleBar::defaultBrandText()` 那五个
    // 字母）能在这儿改成别的，**留空就是默认那五个**。
    // 2026-09-26 分栏时它连同「账户」「版本名格式」一起并进「个性化」：这三节管的都不是
    // "游戏怎么起来"，而是"这东西看起来是什么样 / 我叫什么名字"。
    // ---- 第 1 节：标题 + 一张小节卡片（见 addSettingsSection()）----
    const SettingsSection section1 = addSettingsSection(grid, row, QStringLiteral("外观"), form);
    {
        QWidget *box = makeHintedControl(form);
        auto *boxLayout = qobject_cast<QVBoxLayout *>(box->layout());
        titleTextEdit = new QLineEdit(box);
        titleTextEdit->setObjectName(titleTextEditName());
        // 长度上限在**两个地方**各写一遍（ui/ 不许 include src/，见 titleTextMaxLength()）。
        titleTextEdit->setMaxLength(titleTextMaxLength());
        // 占位文字就是默认那五个字母：一个不知所以的空框会被当成"这里还没做完"。
        titleTextEdit->setPlaceholderText(TitleBar::defaultBrandText());
        titleTextEdit->setMinimumWidth(kTitleTextEditWidth);
        // 文本框和下拉框不往整格里撑（内容那一格是整幅宽，一个名字框被拉到 600 px 看着就是坏的）——
        // 和 addInlineControl() 是同一条规矩。
        boxLayout->addWidget(titleTextEdit, 0, Qt::AlignLeft);
        addHint(box,
                QStringLiteral("窗口左上角那行字。留空就是默认的 %1。")
                    .arg(TitleBar::defaultBrandText()),
                form);
        addSettingItem(section1, QStringLiteral("标题文字"), box, form);
    }

    // ---------- 背景图 ----------
    // 2026-09-28 加的：用户原话「能给主页左侧自定义图像吗？其他页面的背景能换图吗」。
    // 两块底各一行 —— 主页左栏、四个页面的内容区（见 ui/Theme.hpp 里 Part 那两项）。
    //
    // NOTE: 挑图那一下**不是**把用户那个路径记下来：文件会被拷进数据目录
    //       （`src/util/ThemeImage.hpp`）。这一节只摆控件，收图和落盘都在 MainWindow。
    // ---- 第 2 节：标题 + 一张小节卡片（见 addSettingsSection()）----
    const SettingsSection section2 = addSettingsSection(grid, row, QStringLiteral("背景图"), form);
    {
        const auto addImageRow = [&](Theme::Part part, const QString &what) {
            const QString key = Theme::partKey(part);
            QWidget *box = makeHintedControl(form);
            auto *boxLayout = qobject_cast<QVBoxLayout *>(box->layout());

            QWidget *line = new QWidget(box);
            auto *lineLayout = new QHBoxLayout(line);
            lineLayout->setContentsMargins(0, 0, 0, 0);
            lineLayout->setSpacing(MainWindowUI::themeImageRowSpacing());

            ThemeImageRow widgets;
            widgets.partKey = key;

            widgets.choose = new QPushButton(MainWindowUI::themeImageChooseText(), line);
            widgets.choose->setObjectName(MainWindowUI::themeImageChooseName(key));
            widgets.choose->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

            widgets.clear = new QPushButton(MainWindowUI::themeImageClearText(), line);
            widgets.clear->setObjectName(MainWindowUI::themeImageClearName(key));
            // 「清除图片」= 一般性（和「清空搜索」同一档：拿掉一层，不是丢掉一份东西 ——
            // 那张图还在用户自己盘上）。旁边那颗「选择图片…」是确认性。
            Theme::markButton(widgets.clear, Theme::ButtonKind::Neutral);
            widgets.clear->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

            widgets.fit = new QComboBox(line);
            widgets.fit->setObjectName(MainWindowUI::themeImageFitName(key));
            // 项**由这里填**（`Theme::ImageFit` 这个名字本身就是 ui/ 那一层的东西）：
            // 和「窗口模式」那个下拉框不一样 —— 那个的档位枚举在 src/ 里，这边看不见。
            for (int i = 0; i < Theme::imageFitCount(); ++i) {
                const auto fit = static_cast<Theme::ImageFit>(i);
                widgets.fit->addItem(Theme::imageFitName(fit), Theme::imageFitKey(fit));
            }
            widgets.fit->setMinimumWidth(MainWindowUI::themeImageFitComboWidth());

            widgets.opacity = new QSlider(Qt::Horizontal, line);
            widgets.opacity->setObjectName(MainWindowUI::themeImageOpacityName(key));
            widgets.opacity->setRange(0, 100);
            widgets.opacity->setValue(100);
            widgets.opacity->setFixedWidth(MainWindowUI::themeImageOpacityWidth());

            widgets.percent = new QLabel(MainWindowUI::themeImagePercentText(100), line);
            widgets.percent->setObjectName(MainWindowUI::themeImagePercentName(key));
            widgets.percent->setMinimumWidth(MainWindowUI::themeImagePercentWidth());

            lineLayout->addWidget(widgets.choose);
            lineLayout->addWidget(widgets.clear);
            lineLayout->addSpacing(MainWindowUI::themeImageGroupSpacing());
            lineLayout->addWidget(new QLabel(QStringLiteral("摆法"), line));
            lineLayout->addWidget(widgets.fit);
            lineLayout->addSpacing(MainWindowUI::themeImageGroupSpacing());
            lineLayout->addWidget(new QLabel(QStringLiteral("不透明度"), line));
            lineLayout->addWidget(widgets.opacity);
            lineLayout->addWidget(widgets.percent);
            lineLayout->addStretch(1);
            boxLayout->addWidget(line);

            // "现在铺的是哪一张"那一行：**字号正常、不用 hint 那档灰** —— 它是这一格当前
            // 的状态（和上面那行说明不是一个意思），用户扫一眼要知道自己挑了哪张。
            widgets.state = new QLabel(MainWindowUI::themeImageNoneText(), box);
            widgets.state->setObjectName(MainWindowUI::themeImageStateName(key));
            widgets.state->setWordWrap(true);
            boxLayout->addWidget(widgets.state);

            addHint(box,
                    QStringLiteral("%1「选择图片…」挑的那张会拷一份进启动器的数据目录，"
                                   "之后把它挪走、删掉都不影响这一块。不透明度调低 = "
                                   "底下的底色透上来 —— 字要读得清就是靠它。")
                        .arg(what),
                    form);
            addSettingItem(section2, Theme::partName(part), box, form);

            m_themeImageRows.append(widgets);
        };

        addImageRow(Theme::Part::HomeLeft,
                    QStringLiteral("主页左栏那一块（「已安装版本」到「启动游戏」那一片）。"));
        addImageRow(Theme::Part::PageBackground,
                    QStringLiteral("四个页面（主页 / 实例 / 下载 / 设置）内容区那一整块底。"));
    }

    // ---------- 账户 ----------
    // 这一格原来摆的是个「微软登录」按钮（登录过之后叫「重新登录」）。它**搬进**
    // 「账号与外观」那个窗里了：那个按钮只会登录一件事 —— 加离线账号、切换账号、删账号、
    // 换皮肤披风它一件都做不到，而这一格却叫「当前账户」，用户来设置页找"管账号"的地方
    // 会撞上一个只能登录的按钮。账号那一整块只在一个窗里做（ui/AccountSkinDialog），
    // 这里留一个入口。
    // ---- 第 3 节：标题 + 一张小节卡片（见 addSettingsSection()）----
    const SettingsSection section3 = addSettingsSection(grid, row, QStringLiteral("账户"), form);
    {
        QWidget *box = makeHintedControl(form);
        auto *boxLayout = qobject_cast<QVBoxLayout *>(box->layout());
        accountSkinSettingsButton = new QPushButton(accountSkinButtonText(), box);
        // 文案和主页那个入口**共用一份**（accountSkinButtonText()，见上面那段说明）。
        accountSkinSettingsButton->setObjectName(accountSkinSettingsButtonName());
        // 和主页那一颗同一个判断（一般性）—— 两颗字都一样，颜色不该两样。
        Theme::markButton(accountSkinSettingsButton, Theme::ButtonKind::Neutral);
        accountSkinSettingsButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        accountSkinSettingsButton->setMinimumWidth(kAccountSkinButtonMinWidth);
        boxLayout->addWidget(accountSkinSettingsButton, 0, Qt::AlignLeft);
        addHint(box, QStringLiteral("正版登录、添加离线账号、切换账号、换皮肤和披风都在这个窗里。"
                                    "登录后用户名显示在窗口左下角，启动游戏时用的也是它。"),
                form);
        addSettingItem(section3, QStringLiteral("账号"), box, form);
    }

    // ---------- 版本名格式 ----------
    // 实例名长什么样，也是"看起来是什么样"那一类 —— 和「外观」同一栏。
    // ---- 第 4 节：标题 + 一张小节卡片（见 addSettingsSection()）----
    const SettingsSection section4 = addSettingsSection(grid, row, QStringLiteral("版本名格式"), form);
    {
        QWidget *box = makeHintedControl(form);
        auto *boxLayout = qobject_cast<QVBoxLayout *>(box->layout());

        QWidget *checks = new QWidget(box);
        QGridLayout *checkLayout = new QGridLayout(checks);
        checkLayout->setContentsMargins(0, 0, 0, 0);
        checkLayout->setHorizontalSpacing(12);
        checkLayout->setVerticalSpacing(4);
        versionNameCustomCheck = new QCheckBox(QStringLiteral("自定义名"), checks);
        versionNameCustomCheck->setObjectName(versionNameCustomCheckName());
        versionNameRealCheck = new QCheckBox(QStringLiteral("版本名"), checks);
        versionNameRealCheck->setObjectName(versionNameRealCheckName());
        versionNameLoaderCheck = new QCheckBox(QStringLiteral("加载器名"), checks);
        versionNameLoaderCheck->setObjectName(versionNameLoaderCheckName());
        versionNameLoaderVersionCheck = new QCheckBox(QStringLiteral("加载器版本"), checks);
        versionNameLoaderVersionCheck->setObjectName(versionNameLoaderVersionCheckName());
        checkLayout->addWidget(versionNameCustomCheck, 0, 0);
        checkLayout->addWidget(versionNameRealCheck, 0, 1);
        checkLayout->addWidget(versionNameLoaderCheck, 1, 0);
        checkLayout->addWidget(versionNameLoaderVersionCheck, 1, 1);
        checkLayout->setColumnStretch(2, 1);
        boxLayout->addWidget(checks);

        addHint(box, QStringLiteral("上面这些名字是按「自定义名-实际版本-加载器名-加载器版本」"
                                    "拼出来的，勾掉哪一项哪一段就不显示。"
                                    "数据来自 .minecraft/versions/<版本>/wtfml.meta。"),
                form);
        addSettingItem(section4, QStringLiteral("显示哪些字段"), box, form);
    }

    // 剩下的竖直空间留在底部：设置项不跟着窗口高度一起被拉长
    grid->setRowStretch(row, 1);
}

void MainWindowUI::setupSettingsDownloadPanel(QStackedWidget *stack) {
    const SettingsPanel panel = addSettingsPanel(stack, QStringLiteral("download"));
    QWidget *form = panel.form;
    QGridLayout *grid = panel.grid;
    int row = 0;

    // ---------- 下载源 ----------
    // ---- 第 1 节：标题 + 一张小节卡片（见 addSettingsSection()）----
    const SettingsSection section1 = addSettingsSection(grid, row, QStringLiteral("下载源"), form);
    {
        QWidget *box = makeHintedControl(form);
        auto *boxLayout = qobject_cast<QVBoxLayout *>(box->layout());
        downloadSourceCombo = new QComboBox(box);
        downloadSourceCombo->setObjectName(downloadSourceComboName());
        WheelGuard::install(downloadSourceCombo);
        // ★ 2026-10-11：靠左按 `sizeHint` 摆 —— 理由见「启动器行为」那一颗上面那段。
        boxLayout->addWidget(downloadSourceCombo, 0, Qt::AlignLeft);
        addHint(box, QStringLiteral("镜像源是第三方服务（BMCLAPI），国内通常快很多。"
                                    "下回来的文件不对时可以换回官方源重下。"),
                form);
        addSettingItem(section1, QStringLiteral("来源"), box, form);
    }
    {
        // CurseForge 的 API key（2026-09-25 加）。放在「下载源」这一组里是因为它和上面那格
        // 是同一件事：**下载走哪条路**。那一格管 Minecraft 自己的文件，这一格管整合包
        // 清单里那批文件的"换地址"那一发。
        QWidget *box = makeHintedControl(form);
        auto *boxLayout = qobject_cast<QVBoxLayout *>(box->layout());
        curseForgeApiKeyEdit = new QLineEdit(box);
        curseForgeApiKeyEdit->setObjectName(curseForgeApiKeyEditName());
        curseForgeApiKeyEdit->setPlaceholderText(curseForgeApiKeyPlaceholder());
        // NOTE: 明文显示（不是 Password 那种圆点）。理由：① 这一格是"用户自己去申请来的一串"，
        //       贴错了得能看一眼；② 它本来就明文躺在配置里，圆点给的是**假的**安全感。
        //       它是这一项而不是"账号密码"那一类：泄露的代价是那个用户自己的额度被用掉。
        boxLayout->addWidget(curseForgeApiKeyEdit);
        addHint(box, QStringLiteral("CurseForge 那一条路要用它：下载页把来源选成 CurseForge 时"
                                    "搜项目靠接口，CurseForge 格式的整合包导入也要靠接口把"
                                    "清单里那些编号换成下载地址。官方接口要 key（在 "
                                    "console.curseforge.com 免费申请），留空则走一台社区镜像 —— "
                                    "它不用 key，但可能限流、也可能哪天就停了。"
                                    "改完按回车（或者点到别处）就记住。"),
                form);
        addSettingItem(section1, curseForgeApiKeyRowText(), box, form);
    }

    // ---------- 下下来的东西放在哪儿（2026-09-28） ----------
    // 用户原话见 LauncherSettings 里那一节（那是**唯一**的语义出处）。这里只搭控件：
    // 勾选状态由 MainWindow 灌（它才知道这项设置存在哪）。
    // ---- 第 2 节：标题 + 一张小节卡片（见 addSettingsSection()）----
    const SettingsSection section2 = addSettingsSection(grid, row, QStringLiteral("下载的文件"), form);
    {
        QWidget *box = makeHintedControl(form);
        auto *boxLayout = qobject_cast<QVBoxLayout *>(box->layout());
        downloadAutoIntoInstanceCheck =
            new QCheckBox(downloadAutoIntoInstanceCheckText(), box);
        downloadAutoIntoInstanceCheck->setObjectName(downloadAutoIntoInstanceCheckName());
        boxLayout->addWidget(downloadAutoIntoInstanceCheck, 0, Qt::AlignLeft);
        addHint(box, QStringLiteral("勾上：从实例设置里点「下载新模组」（或资源包等）时，"
                                    "下回来的文件直接放进那个实例，不再问存哪儿；"
                                    "在下载页里直接下的仍然会弹框让你挑位置。\n"
                                    "不勾：不管从哪儿点进来的，每次都弹框让你自己挑。"),
                form);
        addSettingItem(section2, downloadAutoIntoInstanceRowText(), box, form);
    }

    // ---------- 「下载缺前置时提醒」（2026-10-09） ----------
    // 和上面那一项同一个栏、同一种形状：一个勾选框 + 一句话说清勾上/不勾分别是什么。
    // 用户原话见 LauncherSettings 里那一节（那是**唯一**的语义出处）。
    {
        QWidget *box = makeHintedControl(form);
        auto *boxLayout = qobject_cast<QVBoxLayout *>(box->layout());
        warnMissingDependenciesCheck = new QCheckBox(warnMissingDependenciesCheckText(), box);
        warnMissingDependenciesCheck->setObjectName(warnMissingDependenciesCheckName());
        boxLayout->addWidget(warnMissingDependenciesCheck, 0, Qt::AlignLeft);
        addHint(box, QStringLiteral("勾上：点下载一个模组（或光影包）时，先看它要装进的那个实例"
                                    "缺不缺前置；缺了就弹窗列出来，可以一键一起下。\n"
                                    "不勾：不查也不提示，只下你点的那一版 —— 前置缺了游戏会在"
                                    "启动时崩，而崩溃画面里一个字都不会提到少了哪个模组。"),
                form);
        addSettingItem(section2, warnMissingDependenciesRowText(), box, form);
    }

    // 剩下的竖直空间留在底部：设置项不跟着窗口高度一起被拉长
    grid->setRowStretch(row, 1);
}

namespace {

/// 底栏那颗按钮点开之后向上弹出的面板有多大（边距 / 行距 / 和按钮之间的空隙）。
constexpr int kPanelPadding = 10;
constexpr int kPanelRowSpacing = 6;
constexpr int kPanelGap = 6;

/// 面板里一行内部的两处间距：行首图标和那列字之间、那列字和行尾第一颗按钮之间。
/// 后者取小一点（4）：行尾那几颗按钮是一组，得看着"贴着"那列字，别像两个各管各的。
constexpr int kPanelRowIconSpacing = 8;
constexpr int kPanelRowActionSpacing = 4;

/// 行尾那个**文字**按钮的最小宽度（「选择」「登录」「登出」都是两个字）。
/// 钉死是为了让同一个面板里几行的按钮**竖着对齐** —— 跟着字宽变的话，三行的「登录」
/// 和「选择」就会错开几个像素，看着像没对齐的表格。
constexpr int kPanelActionTextWidth = 56;

/// 「一行都没有」时面板里写的那个字。正常情况下左面板会自己给一行"现在没有实例在跑"
/// （那是 MainWindow 的事，它知道该怎么说话）；这一句兜的是**没装回调**那种情况
/// （setBottomBarPanelContent() 没被调过），空白一块比一句话更难查。
constexpr const char kPanelEmptyText[] = "（没有可显示的内容）";

/**
 * @brief 面板里行尾那几颗动作按钮的外观。
 *
 * 和底栏那两颗按钮同一个路数：不画自己的底（面板本来就有底色）、只在鼠标划过/按下时
 * 淡淡蒙一层，颜色一律从调色板取（**不写死**），浅色深色主题下都成立。
 *
 * NOTE: hover / pressed 那两条都带 `:enabled` —— 万一有哪一颗是灰掉的，它划过时**不许**
 *       亮：亮一下就说明"能点"，点下去却什么都不发生。（右面板里当前账号那一行 2026-09-24
 *       之前就是灰的 —— 用户说完「把那个账户管理里的当前改成登出」之后那一颗能点了，
 *       所以现在没有哪一颗是灰的；`:enabled` 留着是护这条规矩本身。）
 *
 * NOTE: 文字按钮要留左右各 6 px 内边距（两三个字贴着边看着像被裁过）；图标按钮不留
 *       （它的大小已经由 kPanelActionSize 定死了，留了反而把图形挤小）。
 */
void applyPanelActionStyle(QPushButton *button, const QPalette &panelPalette, bool hasText) {
    QColor hover = panelPalette.color(QPalette::Highlight);
    hover.setAlpha(48);
    QColor pressed = panelPalette.color(QPalette::Highlight);
    pressed.setAlpha(96);
    button->setStyleSheet(
        QStringLiteral("QPushButton { border: none; background: transparent; padding: %1; }"
                       "QPushButton:hover:enabled { background-color: %2; }"
                       "QPushButton:pressed:enabled { background-color: %3; }")
            .arg(hasText ? QStringLiteral("0 6px") : QStringLiteral("0"),
                 hover.name(QColor::HexArgb))
            .arg(pressed.name(QColor::HexArgb)));
}

/**
 * @brief 面板里那一行：行首图标 + 一列字（主行/次行）+ 行尾几个动作按钮。
 *
 * NOTE: 行尾那几颗用的是**真按钮**（QPushButton），不是自绘 + 事件过滤器 —— 和实例树里
 *       那个"取消小叉"正好相反（那里只能自绘，因为那是 item view 的一行，`editorEvent`
 *       压不住 `itemClicked`）。面板不是列表控件，这里放真控件白得 hover / 按下 /
 *       disabled 三态，验证工程也能按 objectName 把每一颗找回来点它。
 */
class BottomBarPanelRowWidget : public QWidget {
public:
    /// @param row 这一行要画什么（数据由 MainWindow 翻好递进来）。
    /// @param index 第几行（**只用来生成 objectName**）。
    /// @param onAction 行尾第 a 颗按钮被点了（参数是"哪一行是谁、第几颗"）。
    BottomBarPanelRowWidget(const MainWindowUI::PanelRow &row, int index,
                            const std::function<void(const QString &, int)> &onAction,
                            QWidget *parent)
        : QWidget(parent) {
        setObjectName(MainWindowUI::bottomBarPanelRowName(index));

        QHBoxLayout *line = new QHBoxLayout(this);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(kPanelRowIconSpacing);

        const int textWidth = MainWindowUI::bottomBarPanelTextWidth();
        QVBoxLayout *texts = new QVBoxLayout;
        texts->setContentsMargins(0, 0, 0, 0);
        texts->setSpacing(0);

        if (!row.icon.isNull()) {
            QLabel *icon = new QLabel(this);
            icon->setObjectName(MainWindowUI::bottomBarPanelRowIconName(index));
            icon->setPixmap(row.icon);
            icon->setFixedSize(MainWindowUI::bottomBarPanelIconSize(),
                               MainWindowUI::bottomBarPanelIconSize());
            // 图标**对着两行字那一整块居中**（对第一行会显得偏高）。
            line->addWidget(icon, 0, Qt::AlignVCenter);
        }

        QLabel *title = new ElidedLabel(this);
        title->setObjectName(MainWindowUI::bottomBarPanelRowTitleName(index));
        title->setText(row.title);
        // 宽度**钉死**（不是只封顶）：面板的宽度是"内容多宽就多宽"，钉死之后同一个面板
        // 在任何数据下都是同一个宽度，几行的按钮也竖着对齐。超出去的截成省略号。
        title->setFixedWidth(textWidth);
        if (row.placeholder) {
            // "什么都没有"那句话用**次要颜色**：它是一句说明，不是一行内容
            // （和原来那两行占位文字同一个路数）。
            QPalette titlePalette = title->palette();
            titlePalette.setColor(QPalette::WindowText,
                                  titlePalette.color(QPalette::PlaceholderText));
            title->setPalette(titlePalette);
        }
        texts->addWidget(title);

        if (!row.subtitle.isEmpty()) {
            QLabel *subtitle = new ElidedLabel(this);
            subtitle->setObjectName(MainWindowUI::bottomBarPanelRowSubtitleName(index));
            subtitle->setText(row.subtitle);
            subtitle->setFixedWidth(textWidth);
            // 次行一律次要颜色（已运行时长 / 账号类型都是"说明"那一档）。
            QPalette subtitlePalette = subtitle->palette();
            subtitlePalette.setColor(QPalette::WindowText,
                                     subtitlePalette.color(QPalette::PlaceholderText));
            subtitle->setPalette(subtitlePalette);
            texts->addWidget(subtitle);
        }

        // 份数 0：这一列字是钉死的，富余宽度都给后面那个空档 —— 行尾那几颗按钮因此
        // 贴在**最右边**（不这么做的话它们会紧跟着短名字，几行的按钮就对不齐了）。
        line->addLayout(texts, 0);
        line->addStretch(1);

        for (int a = 0; a < row.actions.size(); ++a) {
            const MainWindowUI::PanelAction &action = row.actions.at(a);
            QPushButton *button = new QPushButton(this);
            button->setObjectName(MainWindowUI::bottomBarPanelRowActionName(index, a));
            const bool hasText = action.icon.isNull();
            if (hasText) {
                button->setText(action.text);
                button->setMinimumWidth(kPanelActionTextWidth);
            } else {
                button->setIcon(QIcon(action.icon));
                button->setIconSize(action.icon.size());
                button->setFixedSize(MainWindowUI::bottomBarPanelActionSize(),
                                     MainWindowUI::bottomBarPanelActionSize());
                // 只有图标那几颗是**自己的边长**（22）—— 得报一句，不然会被
                // `actionHeight()` 那条 `min-height` 顶到 32（见 `markIconButton()`）。
                // NOTE: 上面那个 `hasText` 那一支**不报**：那几颗本来就是一行里的动作按钮。
                Theme::markIconButton(button);
            }
            button->setEnabled(action.enabled);
            button->setToolTip(action.tooltip);
            applyPanelActionStyle(button, palette(), hasText);
            // 点的是**哪一颗**靠下标认（不认按钮上的字：那两个字会随语言/状态变，
            // 拿显示内容当身份标识的接线在产品改文案时会以看不懂的方式挂掉）。
            // 行号**不进这个回调**：见 PanelRow::key 那段 NOTE（行号是"哪一行"，
            // 外面要的是"哪一个人/哪一份会话"）。
            const QString key = row.key;
            QObject::connect(button, &QPushButton::clicked, button,
                             [onAction, key, a]() { onAction(key, a); });
            if (a > 0) {
                line->addSpacing(kPanelRowActionSpacing);
            }
            line->addWidget(button, 0, Qt::AlignVCenter);
        }
    }
};

/**
 * @brief 底栏两头那两颗按钮点开之后**向上**弹出来的那块面板（像官启/PCL 那样）。
 *
 * 用 Qt::Popup 而不是 QMenu：这里要的是"面板"这么一块地方（以后左边放实例列表、
 * 右边放账号列表），菜单那种"一行一个 action"的形状套不上。而 Qt::Popup 该白给的一样
 * 白给：点面板外面自动关、Esc 关、不抢主窗口的焦点。
 *
 * NOTE: 它是个**窗口**（Qt::Popup 一定是顶层窗口），但 parent 照旧挂着 —— 这样
 *       findChild 找得到它（验证工程靠这个量几何），生命周期也不用单独管。
 */
class BottomBarPanel : public QWidget {
public:
    /// 行尾某个按钮被点了：参数是"那一行是谁（PanelRow::key）、第几颗"。
    using ActionCallback = std::function<void(const QString &, int)>;
    /// "点在面板外面"那一下要问外面两件事（两个都按**全局坐标**说话 —— 面板自己认不出
    /// "外面是哪儿"，它只知道用户在框外点了一下）。两问的顺序不能换，理由见
    /// handleOutsidePress() 的第二段 NOTE：
    ///   · claimed —— 收回去**之前**问：这一下落在底栏那两颗按钮上吗？（是 ⇒ 把 Qt 那记
    ///     重放压掉）
    ///   · dismissed —— 收回去**之后**叫：去开该开的那一块（点的要是同一颗就什么都不开）。
    struct OutsidePressHandler {
        std::function<bool(const QPoint &globalPos)> claimed;
        std::function<void(const QPoint &globalPos)> dismissed;
    };

    /// @param anchor 贴着哪颗按钮弹（也决定"至少和按钮一样宽"）。
    /// @param alignRight true = 右边缘和按钮右边缘对齐（右端那颗按钮用），
    ///                   false = 左边缘对齐（左端那颗用）。
    /// @param rows 面板里的几行（数据由 MainWindow 翻好，见 setBottomBarPanelContent()）。
    /// @param onAction 行尾的按钮被点了该告诉谁（可以给空的 = 这些按钮点了没反应）。
    /// @param onOutsidePress 点在外面那一下该告诉谁（两个回调都可以给空的 = 只知道"要收回去"）。
    BottomBarPanel(QWidget *anchor, bool alignRight,
                   const QList<MainWindowUI::PanelRow> &rows, const ActionCallback &onAction,
                   const OutsidePressHandler &onOutsidePress, QWidget *parent)
        : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint), m_anchor(anchor),
          m_alignRight(alignRight), m_onOutsidePress(onOutsidePress) {
        setObjectName(MainWindowUI::bottomBarPanelName());
        // 底色比底栏再深一档：它是压在内容区上面的一块，得从底下那堆东西里分出来。
        // **不写死颜色**：从当前调色板推出来，浅色/深色主题下都成立（和底栏同一个路数）。
        // 那一档从 2026-09-28 起由 `Theme::partBackground(Part::Panel)` 说了算（用户可以把
        // 这一族的浮层单独换一个色），算出来和从前那个 `darker(112)` 一模一样。
        // NOTE: 这一块**每次现建**（Qt::Popup + WA_DeleteOnClose）⇒ 不用挂 follow()：
        //       主题换了之后下一次打开自然就是新的（`Theme::follow()` 的 NOTE 那一段）。
        setAutoFillBackground(true);
        QPalette panelPalette = QApplication::palette();
        panelPalette.setColor(QPalette::Window, Theme::partBackground(Theme::Part::Panel));
        setPalette(panelPalette);

        // 点行尾那几颗按钮要**先把自己收回去**，再把动作交给外面 —— 见下面那段 lambda。
        // context 用 parent（底栏）而**不是** this：这个面板是 WA_DeleteOnClose，
        // close() 之后它随时会被删掉，捕它等于捕一个随时没了的对象；底栏和主窗口同寿。
        QWidget *const deferContext = parent;
        const auto dispatch = [this, deferContext, onAction](const QString &key, int action) {
            // **先把面板收回去**：这些动作里有两个要弹模态窗（进实例设置、关实例那个
            // "游戏不会存档"的确认框），而这块面板是个 Qt::Popup —— 它按着鼠标键盘的
            // grab。模态窗开在一块抓着手柄的东西底下，Windows 上就是"窗出来了但点不动"。
            close();
            if (!onAction) {
                return;
            }
            // 再排到**下一轮事件循环**去做事：close() 只是发了个关闭事件，面板要等这一轮
            // 走完才真的没了 —— 当轮就弹模态窗等于没关（上面那条毛病一样在）。
            QTimer::singleShot(0, deferContext,
                               [onAction, key, action]() { onAction(key, action); });
        };

        QVBoxLayout *box = new QVBoxLayout(this);
        box->setContentsMargins(kPanelPadding, kPanelPadding, kPanelPadding, kPanelPadding);
        box->setSpacing(kPanelRowSpacing);
        if (rows.isEmpty()) {
            // 兜底：没装回调（或回调给了空列表）时面板里不留一块空白。
            MainWindowUI::PanelRow empty;
            empty.title = QString::fromUtf8(kPanelEmptyText);
            empty.placeholder = true;
            box->addWidget(new BottomBarPanelRowWidget(empty, 0, dispatch, this));
            return;
        }
        for (int i = 0; i < rows.size(); ++i) {
            box->addWidget(new BottomBarPanelRowWidget(rows.at(i), i, dispatch, this));
        }
    }

    /// 贴的是哪颗按钮（"再点一次收回"要按它认人：点的是同一颗才算收回）。
    QWidget *anchor() const { return m_anchor; }

    /// 摆到 anchor 的正上方并显示出来。
    void popUp() {
        adjustSize();
        // 至少和那颗按钮一样宽：以后装实例列表/账号列表，比按钮还窄就没法看了。
        if (m_anchor != nullptr && width() < m_anchor->width()) {
            resize(m_anchor->width(), height());
        }
        if (m_anchor != nullptr) {
            const QPoint anchorTopLeft = m_anchor->mapToGlobal(QPoint(0, 0));
            const int x = m_alignRight ? anchorTopLeft.x() + m_anchor->width() - width()
                                       : anchorTopLeft.x();
            // 2026-09-29：上拉面板**从下面滑上来**。它是顶层窗口（Qt::Popup），所以"滑"
            // 就是它自己的 pos() 在动 —— 落到哪儿还是一样算，只是先把起手位摆在落点下面
            // `panelSlideDistance()` 那一格上。关掉动效时 `slideUp()` 直接 move 到落点，
            // 和从前那一句 move(x, …) 一模一样（见 ui/Animations.hpp）。
            Anim::slideUp(this, QPoint(x, anchorTopLeft.y() - height() - kPanelGap));
        }
        show();
        raise();
        // 拿焦点只为一件事：Esc 能关（外面点一下 Qt 自己会关，那条不用管）。
        setFocus(Qt::PopupFocusReason);
    }

protected:
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Escape) {
            close();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    /// 点在面板**外面**的那一下（面板开着的时侯，外面那一下是**投递给面板自己**的 ——
    /// 局部坐标在框外，底下那颗按钮一个事件都收不到；实测见文件头那段）。
    /// 返回 true = 这一下已经办完了（不要再去走基类那套）。
    ///
    /// NOTE: 【为什么要在这里把坐标交出去】外面那一下是"面板该收了"，而"换到另一块面板"
    ///       只有在这里才办得到（那颗按钮收不到任何事件）。所以认"外面是哪儿"这件事由
    ///       回调办（它才知道那两颗按钮在哪），面板只管收自己。
    /// NOTE: 【为什么要压 Qt 的重放】Qt 在关一块 Qt::Popup 的时候，如果光标不在它几何里，
    ///       会把这同一个 press **重放**给光标底下那个控件（`QApplicationPrivate::closePopup`
    ///       → `replayMousePress` → `QWidgetWindow::handleMouseEvent` 尾巴上那段；
    ///       Windows 上那个 style hint 是 true）。那一下落在底栏那颗按钮上就是**又一次点击**：
    ///         · 点的是**另一颗** → 我们刚替用户把那一块开出来，重放这一下又把它收回去
    ///           （看上去就是"点了没反应，得点两下"）；
    ///         · 点的是**同一颗** → 我们刚收回去，重放这一下又开出来（看上去就是"收不回去"）。
    ///       `Qt::WA_NoMouseReplay` 就是干这个的（QMenu 对它自己那颗按钮也是这么办的，
    ///       见 `QMenuPrivate::noReplayFor`）。**必须在 close() 之前钉上** —— Qt 是关窗
    ///       （`hide_helper`）那一刻读这个属性。属性会被 Qt 在"这个控件又收到鼠标事件"时
    ///       清掉，所以挂在构造里没用，只能在这儿按需要钉。
    bool handleOutsidePress(QMouseEvent *event) {
        const QPoint local = event->position().toPoint();
        if (rect().contains(local)) {
            return false;  // 落在面板自己身上：照旧往下走
        }
        const QPoint global = event->globalPosition().toPoint();
        const bool claimed = m_onOutsidePress.claimed && m_onOutsidePress.claimed(global);
        if (claimed) {
            // **必须赶在这里**（close() 之前）：Qt 是关窗那一刻读这个属性的。
            setAttribute(Qt::WA_NoMouseReplay, true);
        }
        close();
        // 收回去之后才轮到"要不要接着开另一块"：那一下要开的那一块，是**这一块已经收掉
        // 之后**才该出现的（顺序反了的话，新面板会被底下那段"收旧"再关一次）。
        if (claimed && m_onOutsidePress.dismissed) {
            m_onOutsidePress.dismissed(global);
        }
        event->accept();
        return true;
    }

    /**
     * @brief 点在面板外面的那一下 = "面板该收回了"。落点是不是底栏那两颗按钮、要不要换一块，
     *        由回调说了算（见 handleOutsidePress()）。
     */
    void mousePressEvent(QMouseEvent *event) override {
        if (handleOutsidePress(event)) {
            return;
        }
        QWidget::mousePressEvent(event);
    }

    /// 点得**快**的时候，这"外面的一下"到的是双击事件（Windows 上第二发就是它，见
    /// ui/RowActivation.hpp 那段）—— 同一个处理，不接的话那一下还是白点。
    void mouseDoubleClickEvent(QMouseEvent *event) override {
        if (handleOutsidePress(event)) {
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    /// 一圈细边：同一档深浅在浅色主题下几乎看不出边界，分不出"面板到哪为止"。
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setPen(palette().color(QPalette::Mid));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }

private:
    QWidget *m_anchor = nullptr;
    bool m_alignRight = false;
    OutsidePressHandler m_onOutsidePress;
};

/// 底栏整条那一块布的调色板：底比窗口底色深一档（用户单独改过就是那个）。
///
/// ★ 基色调色板一律取 `QApplication::palette()`，**不能**取底栏自己的 —— 底栏身上就写着
///   算好的那一份，第二次进来取到的 Window 已经是压暗过一档的，再压一次就深两档
///   （`TitleBar::applyColors()` 那边同一个坑，2026-09-25 在那儿踩过一次）。
QPalette bottomBarChromePalette() {
    QPalette palette = QApplication::palette();
    palette.setColor(QPalette::Window, Theme::partBackground(Theme::Part::BottomBar));
    return palette;
}

/// 底栏按钮身上那个"跟随登记只做过一次"的印记（见 `applyBottomBarButtonStyle()` 里的闸门）。
constexpr const char kBottomBarButtonFollowedProperty[] = "wtfmlThemeFollowed";

/// 底栏那两颗按钮的**同一套外观**：不画按钮自己的底（它占的是底栏那一格），
/// 只在鼠标划过/按下时淡淡蒙一层 —— 不然用户根本看不出那一格能点。
/// 蒙的那层取自调色板的 Highlight（**不写死颜色**），浅色/深色主题下都成立。
void applyBottomBarButtonStyle(QPushButton *button, const QPalette &barPalette) {
    QColor hover = barPalette.color(QPalette::Highlight);
    hover.setAlpha(48);
    QColor pressed = barPalette.color(QPalette::Highlight);
    pressed.setAlpha(96);
    button->setStyleSheet(
        QStringLiteral("QPushButton { border: none; background: transparent; text-align: left; "
                       "padding: 0; }"
                       "QPushButton:hover { background-color: %1; }"
                       "QPushButton:pressed { background-color: %2; }")
            .arg(hover.name(QColor::HexArgb), pressed.name(QColor::HexArgb)));
    // ★ 登记**只能登记一次**，所以这里用属性当闸门 —— 这个函数本身就在"主题变了"的回调
    //   那一支上（下面那条 lambda），里面那句 `setStyleSheet()` 又发一次 `StyleChange`。
    //   原来把 `Theme::follow()` 直接摆在这个函数里 ⇒ 每一趟都**再生一个**跟随者，新跟随者
    //   又同步回调进这个函数、又生一个…… 挂上主题之后进程起来直接栈溢出（`0xC00000FD`，
    //   一行输出都没有）。`PaletteFollower` 里那个 `m_running` 只拦得住**同一个**跟随者自咬，
    //   拦不住"生新的"。闸门放在这里，是因为两个调用点之外谁也够不到这一支。
    if (button->property(kBottomBarButtonFollowedProperty).toBool()) {
        return;
    }
    button->setProperty(kBottomBarButtonFollowedProperty, true);
    // 源调色板取**应用那一份**：这一条只用到 `Highlight`，而底栏身上那份只改了 `Window`
    // （所以取谁都一样）—— 取应用那份就不必操心"底栏和按钮谁先收到事件"。
    Theme::follow(button, [button]() { applyBottomBarButtonStyle(button, QApplication::palette()); });
}

/// 按钮右边那个**朝上**的小箭头（点开是向上弹的，所以箭头朝上）。画的，不是图片文件。
QLabel *makeChevronLabel(QWidget *parent) {
    const int size = MainWindowUI::bottomBarChevronSize();
    QLabel *label = new QLabel(parent);
    label->setObjectName(MainWindowUI::bottomBarChevronName());
    label->setPixmap(VersionIcons::chevronUp(size, parent->palette().color(QPalette::WindowText)));
    // 这是**位图**：颜色已经落在像素里了 ⇒ 主题一换得重画一张，不然那颗箭头会停在旧色上。
    // （`VersionIcons` 里按"边长 + 颜色"缓存着，所以重画这一步不贵。）
    Theme::follow(label, [label, size]() {
        label->setPixmap(
            VersionIcons::chevronUp(size, label->palette().color(QPalette::WindowText)));
    });
    label->setFixedSize(size, size);
    return label;
}

// ---------------------------------------------------------------------------
// 实例页左栏「游戏文件夹」列表：一行 = 上面一个名字、下面那条路径、行尾一颗齿轮。
// ---------------------------------------------------------------------------

/// 行里两圈边距：外面那圈（行自己的内边距）比组框那圈小一点，里面两行字之间不留缝
/// （它们是一段话的两行，不是两个控件）。
constexpr int kFolderRowSideMargin = 8;
constexpr int kFolderRowTopMargin = 6;

/// 行尾那颗齿轮到自己那两行字的间距。
constexpr int kFolderRowGearSpacing = 6;

/// 齿轮菜单那块浮层自己的内边距与行距。**不共用 `kPanelPadding`**：那几个数是底栏那两块
/// 上拉面板的，菜单比它们紧一档（菜单是压在内容上的一小块，四周留白跟面板一样多就显得
/// 糊在底下那行上了）。
///
/// 2026-09-24 用户原话：「点开的那个框在文本左右都有点留白，上下也是，能不能缩小一点点
/// （减少到 80% 的样子）」+ 紧接着一句「上下不用减那么多，匹配即可」。取值就是照着这两句
/// 算的 —— 文字左边原来 10 + 8 = 18 px（框内边距 + 按钮内边距），现在 8 + 6 = 14 px
/// （≈78%）；上边原来 10 + 6 = 16 px，现在 8 + **6** = 14 px（≈88%，就是"不用减那么多"
/// 那半句：按钮的上下内边距一个像素都没动）—— 于是**上下与左右都是 14 px**，这就是"匹配"
/// 那两个字要的。行距 6 → 4：行与行之间 6 + 4 + 6 = 16 px（原来 18）。
constexpr int kFolderMenuPadding = 8;
constexpr int kFolderMenuRowSpacing = 4;

/**
 * @brief 「游戏文件夹」列表里的一行。
 *
 * NOTE: 这两行字和行尾那颗齿轮全是**真控件**，没有一行是自绘的 —— 和底栏面板那几行
 *       同一个取法（理由也一样：真按钮白得 hover / 按下 / disabled 三态，验证工程还
 *       能按 objectName 把每一颗找回来点它）。
 *
 * NOTE: 这一行里**只有齿轮吃鼠标那一下**。那两行字是普通 QLabel（不吃鼠标事件），
 *       事件会一路冒泡到列表的 viewport，于是"点行 = 选中这一份文件夹"照旧生效 ——
 *       这和"取消小叉"那个自绘的格子正好相反（那里非拦不可，因为拦晚了会变成"点进了
 *       安装页"）。这里不拦是有意的：选中那一份文件夹是这个列表最主要的用途。
 */
class FolderRowWidget : public QWidget {
public:
    /// @param row 这一行要画什么（数据由 MainWindow 翻好递进来）。
    /// @param index 第几行（**只用来生成 objectName**）。
    /// @param onGear 行尾那颗齿轮被点了，参数是"哪一行、那颗按钮"。
    FolderRowWidget(const MainWindowUI::FolderRow &row, int index,
                    const std::function<void(const MainWindowUI::FolderRow &, QWidget *)> &onGear,
                    QWidget *parent)
        : QWidget(parent) {
        setObjectName(MainWindowUI::folderRowName(index));

        QHBoxLayout *line = new QHBoxLayout(this);
        line->setContentsMargins(kFolderRowSideMargin, kFolderRowTopMargin, kFolderRowSideMargin,
                                 kFolderRowTopMargin);
        line->setSpacing(kFolderRowGearSpacing);

        QVBoxLayout *texts = new QVBoxLayout;
        texts->setContentsMargins(0, 0, 0, 0);
        texts->setSpacing(0);

        // 上面那行：名字。正常字色、正常字号 —— 它是这一行的正文。
        QLabel *title = new ElidedLabel(this);
        title->setObjectName(MainWindowUI::folderRowTitleName(index));
        title->setText(row.title);
        texts->addWidget(title);

        // 下面那行：路径。小一号、次要颜色（和版本树那几行的第二行一个路数）。
        ElidedLabel *subtitle = new ElidedLabel(this);
        subtitle->setObjectName(MainWindowUI::folderRowSubtitleName(index));
        // **从中间省略**：路径的两头都有用（盘符 + 最后一级目录名），从尾巴截掉的恰好是
        // "这一份是哪个文件夹"唯一认得出的那截 —— 几个文件夹前缀一样时（D:/MC/a、D:/MC/b）
        // 尾巴省略会让两行看起来一模一样。见 ElidedLabel::setElideMode()。
        subtitle->setElideMode(MainWindowUI::folderRowPathElideMode());
        subtitle->setText(row.subtitle);
        QFont subtitleFont = subtitle->font();
        subtitleFont.setPointSize(qMax(1, subtitleFont.pointSize() - 1));
        subtitle->setFont(subtitleFont);
        QPalette subtitlePalette = subtitle->palette();
        subtitlePalette.setColor(QPalette::WindowText,
                                 subtitlePalette.color(QPalette::PlaceholderText));
        subtitle->setPalette(subtitlePalette);
        texts->addWidget(subtitle);

        // 份数 0：这两行字多宽就占多宽，富余的宽度都给齿轮前面那个空档 —— 齿轮因此
        // 贴在**最右边**（几行的齿轮竖着对齐）。不这么做的话它紧跟着路径，而路径一行
        // 长一行短，几颗齿轮就会错开。
        line->addLayout(texts, 0);
        line->addStretch(1);

        QPushButton *gear = new QPushButton(this);
        gear->setObjectName(MainWindowUI::folderRowGearName(index));
        const int iconSize = MainWindowUI::folderMenuIconSize();
        gear->setIcon(QIcon(VersionIcons::gear(iconSize, palette().color(QPalette::WindowText))));
        gear->setIconSize(QSize(iconSize, iconSize));
        gear->setFixedSize(MainWindowUI::folderRowGearSize(), MainWindowUI::folderRowGearSize());
        // 行尾那颗齿轮按自己的 24 来（报一句见 `Theme::markIconButton()`：`min-height`
        // 连 `setFixedSize()` 都拦不住）。
        Theme::markIconButton(gear);
        gear->setCursor(Qt::PointingHandCursor);
        gear->setToolTip(row.assistant
                             ? QCoreApplication::translate(
                                   "MainWindowUI", "这一份的更多操作（自带的那一份移不掉、"
                                                   "也删不掉）")
                             : QCoreApplication::translate("MainWindowUI",
                                                           "这一份的更多操作"));
        applyPanelActionStyle(gear, palette(), false);
        // 点的是**哪一行**靠这里捕下来的值认（不认"第几行"）。菜单弹出来到用户点某一项
        // 之间隔着看两眼的时间，这中间列表是会变的（见 FolderRow::path 那段 NOTE）。
        QObject::connect(gear, &QPushButton::clicked, gear,
                         [onGear, row, gear]() { onGear(row, gear); });
        line->addWidget(gear, 0, Qt::AlignVCenter);

        // 整行的悬停提示：两行字都可能被省略号截掉（左栏只有窗口宽度的 3/13），
        // 把没截的那份原文挂在这儿。
        setToolTip(row.title + QLatin1Char('\n') + row.subtitle);
    }
};

/**
 * @brief 行尾那颗齿轮点出来的那个框（刷新 / 重命名 / 打开 / 移除 / 删除）。
 *
 * 用 Qt::Popup 而不是 QMenu：**不是**为了好看，是为了验证工程驱动得了 —— QMenu::exec()
 * 会阻塞在事件循环里（offscreen 下尤其没法驱动），而 Qt::Popup + show() 该白给的一样
 * 白给：点框外面自动关、Esc 关、不抢主窗口的焦点，而且**不阻塞**。
 *
 * NOTE: 它是个窗口（Qt::Popup 一定是顶层窗口），但 parent 照旧挂着 —— 这样 findChild
 *       找得到它（验证工程靠这个把它捞回来逐项点），生命周期也不用单独管。
 */
class FolderMenu : public QWidget {
public:
    /// 某一项被点了：参数是"哪一份文件夹、第几项"。
    using ActionCallback = std::function<void(const QString &, int)>;

    /// @param row 给谁弹的（决定哪几项是灰的）。
    /// @param anchor 贴着哪颗齿轮弹（也用来认"再点一次是收回"）。
    /// @param onAction 某一项被点了该告诉谁（可以给空的 = 点了没反应）。
    FolderMenu(const MainWindowUI::FolderRow &row, QWidget *anchor,
               const ActionCallback &onAction, QWidget *parent)
        : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint), m_anchor(anchor), m_path(row.path) {
        setObjectName(MainWindowUI::folderMenuName());
        // 底色比内容区深一档：它是压在列表上面的一块，得从底下那堆东西里分出来。
        // **不写死颜色**：从当前调色板推出来，浅色/深色主题下都成立（和底栏、面板同一个路数）。
        // 这一族（浮层）那一档从 2026-09-28 起由 `Theme::partBackground(Part::Panel)` 说了算。
        // NOTE: 每次现建 ⇒ 不用挂 follow()（和底栏面板同一条）。
        setAutoFillBackground(true);
        QPalette menuPalette = QApplication::palette();
        menuPalette.setColor(QPalette::Window, Theme::partBackground(Theme::Part::Panel));
        setPalette(menuPalette);

        // 点某一项要**先把自己收回去**，再把动作交给外面 —— 那五项里有两项要弹模态窗
        // （重命名那个输入框、删除那个确认框），而这块框是个 Qt::Popup，按着鼠标键盘的
        // grab。模态窗开在一块抓着手柄的东西底下，Windows 上就是"窗出来了但点不动"。
        //
        // NOTE: context 用 parent（列表那一头）而**不是** this：这个框是 WA_DeleteOnClose，
        //       close() 之后它随时会被删掉，捕它等于捕一个随时没了的对象。
        QWidget *const deferContext = parent;
        const auto dispatch = [this, deferContext, onAction](int action) {
            const QString path = m_path;
            close();
            if (!onAction) {
                return;
            }
            // 再排到**下一轮事件循环**：close() 只是发了个关闭事件，要等这一轮走完才真的
            // 没了 —— 当轮就弹模态窗等于没关（上面那条毛病一样在）。
            QTimer::singleShot(0, deferContext, [onAction, path, action]() {
                onAction(path, action);
            });
        };

        const int iconSize = MainWindowUI::folderMenuIconSize();
        QVBoxLayout *box = new QVBoxLayout(this);
        box->setContentsMargins(kFolderMenuPadding, kFolderMenuPadding, kFolderMenuPadding,
                                kFolderMenuPadding);
        box->setSpacing(kFolderMenuRowSpacing);
        for (int a = 0; a < MainWindowUI::folderActionCount(); ++a) {
            // 「移除」和「删除」对自带那一份是灰的：它移不掉（FolderManager::removeFolder）
            // 也删不掉（deleteFolder）。灰掉必须**配一句为什么** —— 灰着不说话，用户只会
            // 以为界面坏了。
            //
            // NOTE: 这个判断要在**画图标之前**算出来（下面那条彩色规则要用它）。
            const bool protectedFolder = row.assistant &&
                                         (a == MainWindowUI::FolderActionRemove ||
                                          a == MainWindowUI::FolderActionDelete);
            QPushButton *button = new QPushButton(this);
            button->setObjectName(MainWindowUI::folderMenuActionName(a));
            button->setText(MainWindowUI::folderActionText(a));
            // 字形颜色：「删除」那一项的垃圾桶跟着它的字一起红（2026-09-24 用户要的）。
            // 灰掉的那一档仍走正文色 —— 和字那边是同一条规矩（见 folderActionStyle 里
            // 的 `:enabled`）：红图标落在灰按钮上会被读成"这一项现在能点"。
            button->setIcon(QIcon(MainWindowUI::folderActionIcon(
                a, iconSize,
                (a == MainWindowUI::FolderActionDelete && !protectedFolder)
                    ? MainWindowUI::folderDangerColor(menuPalette)
                    : menuPalette.color(QPalette::WindowText))));
            button->setIconSize(QSize(iconSize, iconSize));
            // 图标放在文字**前面**：QPushButton 本来就是这个摆法（图标在左、文字在右），
            // 前提是别把 text-align 设成居中 —— 下面那行 QSS 设的是 left，于是这一整块
            // （图标 + 文字）从左边起排，看着才是"菜单"而不是"一排居中按钮"。
            button->setStyleSheet(folderActionStyle(a, menuPalette));
            button->setEnabled(!protectedFolder);
            button->setToolTip(protectedFolder
                                   ? QCoreApplication::translate(
                                         "MainWindowUI", "启动器自带的游戏文件夹，这一项做不了")
                                   : MainWindowUI::folderActionTooltip(a));
            button->setCursor(Qt::PointingHandCursor);
            QObject::connect(button, &QPushButton::clicked, button,
                             [dispatch, a]() { dispatch(a); });
            box->addWidget(button);
        }
    }

    /// 贴的是哪颗齿轮（"再点一次收回"要按它认人：点的是同一颗才算收回）。
    QWidget *anchor() const { return m_anchor; }

    /// 摆到 @p globalTopLeft 这个位置并显示出来（超出屏幕时往里收）。
    void popUpAt(const QPoint &globalTopLeft) {
        adjustSize();
        QPoint target = globalTopLeft;
        // 贴着行尾那颗齿轮弹，左栏又在窗口靠左的位置，所以整块框是往**右**展开的 ——
        // 窗口窄的时候它会伸到屏幕外，得往回拉一下（拉的是整块框，不是它里面某一项）。
        if (const QScreen *screen = QGuiApplication::screenAt(globalTopLeft)) {
            const QRect available = screen->availableGeometry();
            target.setX(qBound(available.left(), target.x(),
                               qMax(available.left(), available.right() - width() + 1)));
            target.setY(qBound(available.top(), target.y(),
                               qMax(available.top(), available.bottom() - height() + 1)));
        }
        move(target);
        show();
        raise();
        // 拿焦点只为一件事：Esc 能关（外面点一下 Qt 自己会关，那条不用管）。
        setFocus(Qt::PopupFocusReason);
    }

protected:
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Escape) {
            close();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    /// 点在**自己那颗齿轮**上 = "再点一次收回"，这里自己把框关掉（和底栏那块面板同一个
    /// 理由：Qt::Popup 有把这一下重放给下面控件那条路数，两条路都只关不开，所以怎么点
    /// 都不会再冒出一块新的）。
    void mousePressEvent(QMouseEvent *event) override {
        if (m_anchor != nullptr &&
            m_anchor->rect().contains(m_anchor->mapFromGlobal(event->globalPosition().toPoint()))) {
            close();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    /// 一圈细边：同一档深浅在浅色主题下几乎看不出边界，分不出"框到哪为止"。
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setPen(palette().color(QPalette::Mid));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }

private:
    /// 菜单里一项的外观：不画自己的底、只在鼠标划过时淡淡蒙一层（颜色从调色板取，
    /// **不写死**），文字一律左对齐，**字一律加粗**（2026-09-24 用户要的：一块压在上面的
    /// 浮层，字不沉一点读起来像被底下的内容冲淡了）。「删除」那一项的字色是全仓唯一
    /// 一处红字。
    static QString folderActionStyle(int action, const QPalette &menuPalette) {
        QColor hover = menuPalette.color(QPalette::Highlight);
        hover.setAlpha(48);
        QColor pressed = menuPalette.color(QPalette::Highlight);
        pressed.setAlpha(96);
        // 左右各留 6 px：图标贴着框边看着像被裁过，但再多就显得这块框糊在底下的行上了
        // （原来是 8，2026-09-24 跟着 kFolderMenuPadding 一起收紧 —— 两个数怎么来的、
        // 用户那两句话怎么读，都写在 kFolderMenuPadding 上面那段）。
        // NOTE: 加粗走 QSS 的 `font-weight`（别去 setFont() 改控件的字体）—— 这条样式
        //       表的**每一档状态**都是这里出的，字重跟别的属性待在同一个地方才不会漏。
        QString sheet = QStringLiteral("QPushButton { border: none; background: transparent;"
                                       " font-weight: bold;"
                                       " text-align: left; padding: 6px 6px; }"
                                       "QPushButton:hover:enabled { background-color: %1; }"
                                       "QPushButton:pressed:enabled { background-color: %2; }")
                            .arg(hover.name(QColor::HexArgb), pressed.name(QColor::HexArgb));
        if (action == MainWindowUI::FolderActionDelete) {
            // 带 `:enabled` 是必须的：「删除」对自带那一份是**灰的**，红字落在灰按钮上
            // 会读成"这一项现在能点"（而它点不动）。灰的时候一律跟着样式走（和别的项一样）。
            sheet += QStringLiteral("QPushButton:enabled { color: %1; }")
                         .arg(MainWindowUI::folderDangerColor(menuPalette).name());
        }
        return sheet;
    }

    QWidget *m_anchor = nullptr;
    QString m_path;
};

/**
 * @brief 实例页右栏那张树里**组行行尾那颗齿轮**点出来的框。
 *
 * 两套构造，同一个类：
 *   * **动作菜单**（默认）：这一组能做什么（移入分组… / 都移出分组 / 重命名 / 默认展开 /
 *     删除）；
 *   * **挑目标**：把**选中的那个实例**移进哪一组（列出全部组，含一个实例都没有的
 *     那些 —— 空组在树里是藏着的，这是唯一能把第一个实例放进去的入口）。
 *
 * NOTE: 骨架是从 FolderMenu 抄的（Qt::Popup + 点框外/Esc 自己关 + "再点同一颗齿轮
 *       收回" + 按屏幕收边 + 点某项先关窗、排到下一轮事件循环再交出去；那边每一条的
 *       **理由**都写在它的注释里，这里不重复）。两块真正的差别只有两点：
 *         · 认"点的是不是同一颗齿轮"用的是一个**锚点矩形**（行是代理画的，压根没有
 *           控件可指）；于是"收回"那一下还得压 Qt 的重放 —— 见 handleAnchorPress()；
 *         · 动作菜单里有一项（移入分组…）要**再弹第二个框**，所以这一块的父窗口
 *           底下可能同时存在两"代"。
 *       **要改这套骨架时两块一起改**（文件夹那一块叫 FolderMenu，就在上面）。
 */
class GroupMenu : public QWidget {
public:
    /// 动作菜单里某一项被点了：参数是"哪一组、第几项"。
    using ActionCallback = std::function<void(const QString &, int)>;
    /// 挑目标那个框里某一组被点了：参数是目标组 id（空串 = 未分组）。
    using PickCallback = std::function<void(const QString &)>;

    /// @param row 给哪一组弹的（决定哪几项显示、哪几项是灰的）。
    /// @param anchorGlobal 那颗齿轮占的**全局**矩形（贴它弹，也用来认"再点一次是收回"）。
    /// @param replayHost 接得住 Qt 那记重放的控件（树的 viewport，见 handleAnchorPress()）。
    GroupMenu(const MainWindowUI::GroupRow &row, const QRect &anchorGlobal,
              const ActionCallback &onAction, QWidget *replayHost, QWidget *parent)
        : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint),
          m_anchorGlobal(anchorGlobal),
          m_groupId(row.groupId),
          m_replayHost(replayHost) {
        setObjectName(MainWindowUI::groupMenuName());
        applyMenuPalette();

        QWidget *const deferContext = parent;
        const auto dispatch = [this, deferContext, onAction](int action) {
            const QString groupId = m_groupId;
            close();
            if (!onAction) {
                return;
            }
            // 再排到**下一轮事件循环**：close() 只是发了个关闭事件，要等这一轮走完才真的
            // 没了 —— 当轮就弹模态窗等于没关（和文件夹那块一模一样，那里有完整一段）。
            QTimer::singleShot(0, deferContext, [onAction, groupId, action]() {
                onAction(groupId, action);
            });
        };

        QVBoxLayout *box = new QVBoxLayout(this);
        box->setContentsMargins(kFolderMenuPadding, kFolderMenuPadding, kFolderMenuPadding,
                                kFolderMenuPadding);
        box->setSpacing(kFolderMenuRowSpacing);
        const QPalette menuPalette = palette();
        for (int a = 0; a < MainWindowUI::groupActionCount(); ++a) {
            // 虚拟的「未分组」只剩第一项（见 groupActionVisible 那段）。
            if (!MainWindowUI::groupActionVisible(a, row)) {
                continue;
            }
            QPushButton *button = new QPushButton(this);
            button->setObjectName(MainWindowUI::groupMenuActionName(a));
            button->setText(MainWindowUI::groupActionText(a, row));
            button->setStyleSheet(entryStyle(a == MainWindowUI::GroupActionDelete, menuPalette));
            // 灰掉的那几项必须**配一句为什么**（灰着不说话用户只会以为界面坏了）——
            // 提示那一句由 MainWindowUI::groupActionTooltip() 给，它那边已经分好了
            // "能点时说什么 / 不能点时说什么"。
            button->setEnabled(MainWindowUI::groupActionEnabled(a, row));
            button->setToolTip(MainWindowUI::groupActionTooltip(a, row));
            button->setCursor(Qt::PointingHandCursor);
            QObject::connect(button, &QPushButton::clicked, button, [dispatch, a]() { dispatch(a); });
            box->addWidget(button);
        }
    }

    /// 挑目标：@p rows 是**全部**分组（含一个实例都没有的、含虚拟的「未分组」），
    /// 顺序就是显示顺序。
    GroupMenu(const QList<MainWindowUI::GroupRow> &rows, const QRect &anchorGlobal,
              const PickCallback &onPick, QWidget *replayHost, QWidget *parent)
        : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint),
          m_anchorGlobal(anchorGlobal),
          m_pick(true),
          m_replayHost(replayHost) {
        setObjectName(MainWindowUI::groupMenuName());
        applyMenuPalette();

        QWidget *const deferContext = parent;
        const auto dispatch = [this, deferContext, onPick](const QString &groupId) {
            close();
            if (!onPick) {
                return;
            }
            QTimer::singleShot(0, deferContext, [onPick, groupId]() { onPick(groupId); });
        };

        QVBoxLayout *box = new QVBoxLayout(this);
        box->setContentsMargins(kFolderMenuPadding, kFolderMenuPadding, kFolderMenuPadding,
                                kFolderMenuPadding);
        box->setSpacing(kFolderMenuRowSpacing);
        const QPalette menuPalette = palette();
        for (int i = 0; i < rows.size(); ++i) {
            const MainWindowUI::GroupRow &row = rows.at(i);
            QPushButton *button = new QPushButton(this);
            // 名字按下标给（0 起，就是递进来的顺序）：验证工程按它取控件，而组名是用户
            // 起的、随时会变，拿文字当身份标识必错（和文件夹那几行同一条）。
            button->setObjectName(MainWindowUI::groupPickRowName(i));
            // 文字带上实例数（和树上那个组头一模一样）：挑的时候"这一组里已经有几个了"
            // 是个有用的信息，而空组在这张表里是**看得见的**（树上它藏着）。
            button->setText(MainWindowUI::groupItemText(row.name, row.memberCount));
            button->setStyleSheet(entryStyle(false, menuPalette));
            // 选中的那个实例**已经在这一组里**的那一条灰掉：点它等于什么都没做，
            // 而"什么都没做"和"点了没反应"在用户眼里是一回事。
            button->setEnabled(!row.containsSelected);
            button->setToolTip(row.containsSelected
                                   ? QCoreApplication::translate("MainWindowUI",
                                                                 "选中的实例已经在这一组里了")
                                   : QCoreApplication::translate("MainWindowUI",
                                                                 "把这个实例放到这一组里"));
            button->setCursor(Qt::PointingHandCursor);
            const QString groupId = row.groupId;
            QObject::connect(button, &QPushButton::clicked, button,
                             [dispatch, groupId]() { dispatch(groupId); });
            box->addWidget(button);
        }
    }

    /// 这块框是"挑目标"那一套吗（真假两块共用一个 objectName，验证工程靠它分开）。
    bool pickMode() const { return m_pick; }

    /// "再点一次同一颗齿轮 = 收回"认的人：动作菜单是它伺候的那一组，挑目标那个框是空串
    /// （它不认人 —— 同一颗齿轮下第二次点进来时，上面那块动作菜单已经不在了）。
    QString groupId() const { return m_groupId; }

    /// 摆到 @p globalTopLeft 这个位置并显示出来（超出屏幕时往里收）。理由和 FolderMenu
    /// 那个 ::popUpAt() 一模一样：贴着行尾那颗齿轮弹，而它在窗口靠右的位置。
    void popUpAt(const QPoint &globalTopLeft) {
        adjustSize();
        QPoint target = globalTopLeft;
        if (const QScreen *screen = QGuiApplication::screenAt(globalTopLeft)) {
            const QRect available = screen->availableGeometry();
            target.setX(qBound(available.left(), target.x(),
                               qMax(available.left(), available.right() - width() + 1)));
            target.setY(qBound(available.top(), target.y(),
                               qMax(available.top(), available.bottom() - height() + 1)));
        }
        move(target);
        show();
        raise();
        // 拿焦点只为一件事：Esc 能关（外面点一下 Qt 自己会关，那条不用管）。
        setFocus(Qt::PopupFocusReason);
    }

protected:
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Escape) {
            close();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override {
        if (handleAnchorPress(event)) {
            return;
        }
        QWidget::mousePressEvent(event);
    }

    /// 点得**快**的时候，第二发及以后到的是双击事件（Windows 上就是它，见
    /// ui/RowActivation.hpp 那段）—— 同一个处理，不接的话"再点一下收回"就成了白点。
    void mouseDoubleClickEvent(QMouseEvent *event) override {
        if (handleAnchorPress(event)) {
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    /// 一圈细边（和 FolderMenu 那条同一个理由）：同一档深浅在浅色主题下几乎看不出边界，
    /// 分不出"框到哪为止"。
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setPen(palette().color(QPalette::Mid));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }

private:
    /// 底色比内容区深一档（从调色板推，**不写死颜色**）—— 和 FolderMenu 那条一样。
    /// 那一档由 `Theme::partBackground(Part::Panel)` 说了算（这一族浮层共用一个色）。
    void applyMenuPalette() {
        setAutoFillBackground(true);
        QPalette menuPalette = QApplication::palette();
        menuPalette.setColor(QPalette::Window, Theme::partBackground(Theme::Part::Panel));
        setPalette(menuPalette);
    }

    /// 菜单里一项的外观。**和 FolderMenu::folderActionStyle() 逐字同形** —— 两块框是
    /// 同一族浮层（都是行尾齿轮弹出来的一列按钮），字重/对齐/留白/红字那几条理由写在
    /// 那边，这里不重复。差别只有"哪一项是红的"由参数给（那边写死是「删除」）。
    static QString entryStyle(bool danger, const QPalette &menuPalette) {
        QColor hover = menuPalette.color(QPalette::Highlight);
        hover.setAlpha(48);
        QColor pressed = menuPalette.color(QPalette::Highlight);
        pressed.setAlpha(96);
        QString sheet = QStringLiteral("QPushButton { border: none; background: transparent;"
                                       " font-weight: bold;"
                                       " text-align: left; padding: 6px 6px; }"
                                       "QPushButton:hover:enabled { background-color: %1; }"
                                       "QPushButton:pressed:enabled { background-color: %2; }")
                            .arg(hover.name(QColor::HexArgb), pressed.name(QColor::HexArgb));
        if (danger) {
            // 带 `:enabled` 是必须的：红字落在灰按钮上会被读成"这一项现在能点"（和
            // folderActionStyle() 里那条注释同一个理由）。
            sheet += QStringLiteral("QPushButton:enabled { color: %1; }")
                         .arg(MainWindowUI::groupDangerColor(menuPalette).name());
        }
        return sheet;
    }

    /// 落在**自己那颗齿轮**上 = "再点一次收回"。
    ///
    /// NOTE: 【为什么要压 Qt 的重放】和底栏那两块面板同一条：Qt 关一块 Qt::Popup 的时候，
    ///       如果光标不在它几何里，会把这同一个 press **重放**给光标底下那个控件。那颗
    ///       齿轮在**树的行里**，重放那一下落到 viewport 上 —— 而那一格正是"点齿轮"的
    ///       命中格（见 MainWindow::eventFilter），于是菜单刚收回去又会被自己弹开，看着
    ///       就是"收不回去"。`WA_NoMouseReplay` 干的就是这个，**必须在 close() 之前钉**
    ///       （Qt 是关窗那一刻读它；属性会被 Qt 在"这个控件又收到鼠标事件"时清掉，所以
    ///       挂在构造里没用）。popup 这一侧和**接重放那一侧**都钉：文档说这个属性是给
    ///       popup 用的，但重放是"投递给下面那个控件"的，两处都钉才两种路径都盖住。
    /// @return true = 这一下已经办完了（不要再去走基类那套）。
    bool handleAnchorPress(QMouseEvent *event) {
        if (!m_anchorGlobal.contains(event->globalPosition().toPoint())) {
            return false;  // 落在框自己身上（或者别处）：照旧往下走
        }
        setAttribute(Qt::WA_NoMouseReplay, true);
        if (m_replayHost != nullptr) {
            m_replayHost->setAttribute(Qt::WA_NoMouseReplay, true);
        }
        close();
        event->accept();
        return true;
    }

    /// 那颗齿轮占的全局矩形（贴它弹、也按它认"再点一次"）。
    QRect m_anchorGlobal;
    /// 动作菜单伺候的是哪一组（挑目标那个框不用它，留空串）。
    QString m_groupId;
    bool m_pick = false;
    QWidget *m_replayHost = nullptr;
};

}  // namespace

void MainWindowUI::setupBottomBar(QWidget *parent, QVBoxLayout *mainLayout) {
    // ---------- 底部通栏状态条（Steam 风格） ----------
    // 三个格子：左 = 实例运行状态，中 = 启动/下载进度，右 = 头像与用户名。
    // 这里只**搭**它：显示什么由 MainWindow::updateBottomBar() 决定，
    // 界面这边不持有任何业务状态。
    //
    // NOTE: 头像那格和实例状态那格在 2026-09 **对调过**（原来头像在左、实例状态在右）。
    //       底下那几个 stretch 是围绕中间那格配的，对调两头时中间那格的份数原样不动，
    //       进度条仍然居中。
    bottomBar = new QFrame(parent);
    bottomBar->setObjectName(bottomBarName());
    bottomBar->setFrameShape(QFrame::NoFrame);
    bottomBar->setFixedHeight(kBottomBarHeight);
    // 底色比窗口底色深一档把它从内容区里分出来。**不写死颜色**：从当前调色板推出来，
    // 浅色主题和深色主题下都成立（darker() 在两种主题上的效果都是"更沉一点"）。
    // 那一档从 2026-09-28 起由 `Theme::partBackground(Part::BottomBar)` 说了算（用户可以把
    // 这一条单独换成别的色），算出来和从前那个 `darker(105)` 一模一样。
    bottomBar->setAutoFillBackground(true);
    bottomBar->setPalette(bottomBarChromePalette());
    // 底栏是**常驻**的（不像浮层那样每次现建）⇒ 主题一换它得自己重算一遍。
    Theme::follow(bottomBar, [this]() { bottomBar->setPalette(bottomBarChromePalette()); });

    QHBoxLayout *barLayout = new QHBoxLayout(bottomBar);
    barLayout->setContentsMargins(12, 6, 12, 6);
    barLayout->setSpacing(12);

    // ---------- 左：当前实例运行状态 ----------
    // 第一行是状态正文，第二行是实例名。**在主页且选中的就是正在跑的那个实例**时
    // 只显示第一行 —— 那种情况下用户眼前就是那个实例，再说一遍名字是废话。
    // 最左边那颗**状态方块**（红/黄/绿，2026-09-24 加的）。它是**第一行的一部分**，
    // 所以下面把第一行做成"方块 + 文字"的一个横排，而不是把方块摆在整格的最左边 ——
    // 摆整格最左边的话，显示第二行时方块会停在两行的中间（VB 居中那一档），
    // 看着不属于任何一行。
    //
    // NOTE: 这里**只是把图摆出来**，颜色由 MainWindow 按状态来要（runStateMarkPixmap()）。
    //       方块自己不知道现在是哪个状态 —— 那件事的判据在 MainWindow::updateBottomBar()
    //       里（"有没有正在启动的会话、有没有正在跑的进程"），这儿是 ui/ 层，看不到那些。
    runStateMarkLabel = new QLabel(bottomBar);
    runStateMarkLabel->setObjectName(runStateMarkName());
    runStateMarkLabel->setFixedSize(MainWindowUI::runStateMarkSize(),
                                    MainWindowUI::runStateMarkSize());

    runStateLabel = new ElidedLabel(bottomBar);
    runStateLabel->setObjectName(runStateLabelName());
    // 贴在底栏最左边，所以文字左对齐（这两行原来在右端，靠的是右对齐）
    runStateLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // 宽度封顶（理由见头文件那段）：多开之后这几行字会随"跑着几个、都叫什么名字"变长，
    // 宽度跟着变就会把中间那根进度条挤得长短不一。
    runStateLabel->setMaximumWidth(MainWindowUI::bottomBarRunStateTextWidth());

    runStateDetailLabel = new ElidedLabel(bottomBar);
    runStateDetailLabel->setObjectName(runStateDetailLabelName());
    runStateDetailLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // ★ 这一格的宽度上限 = **第一行那个数 + 方块那一段**（2026-09-26 修）。
    //   原来两行封的是同一个数（bottomBarRunStateTextWidth() = 136），可两行的"少掉 16 px"
    //   不是一回事：第一行那个方块是布局里**单独的控件**（它占的是上限**之外**的位置，
    //   所以那 136 全是给字的），第二行的缩进走的是 contentsMargins —— 那 16 px 是**从上限
    //   里面**扣的。于是第二行真正能写的只有 120 px，右边比第一行短一截，而下面那句注释
    //   一直写着"剩下的可用宽度正好也一样"（承诺和代码相反，见 2026-09-25 那条教训）。
    //   加回一段之后：两行留给字的宽度都是 136，整格的总宽也都还是 152。
    runStateDetailLabel->setMaximumWidth(MainWindowUI::bottomBarRunStateTextWidth() +
                                         MainWindowUI::bottomBarRunStateMarkWidth());
    // 第二行**缩进方块那一段**，两行文字的左边才对得齐。不缩进的话第一行比第二行多出
    // 16 px，两行会看着像各说各的。
    runStateDetailLabel->setContentsMargins(MainWindowUI::bottomBarRunStateMarkWidth(), 0, 0, 0);
    // 第二行小一号、灰一点，跟第一行区分开。字号按当前字号减 1（不用点值，
    // 免得不支持点值的平台出现负字号）；颜色用调色板里的 PlaceholderText。
    QFont detailFont = runStateDetailLabel->font();
    detailFont.setPointSize(qMax(1, detailFont.pointSize() - 1));
    runStateDetailLabel->setFont(detailFont);
    QPalette detailPalette = runStateDetailLabel->palette();
    detailPalette.setColor(QPalette::WindowText, detailPalette.color(QPalette::PlaceholderText));
    runStateDetailLabel->setPalette(detailPalette);

    // 第一行 = 方块 + 文字。方块那一格**不带 stretch**（它是固定的 10 px），文字那格带 1。
    // 竖直方向上文字是 VCenter、方块也是 VCenter，两者因此居中对齐 —— 方块比文字矮一号，
    // 居中之后看着就跟这一行是一体的。
    QHBoxLayout *runStateTitleRow = new QHBoxLayout();
    runStateTitleRow->setContentsMargins(0, 0, 0, 0);
    runStateTitleRow->setSpacing(MainWindowUI::runStateMarkSpacing());
    runStateTitleRow->addWidget(runStateMarkLabel, 0, Qt::AlignVCenter);
    runStateTitleRow->addWidget(runStateLabel, 1);

    QVBoxLayout *runStateLayout = new QVBoxLayout();
    runStateLayout->setSpacing(0);
    runStateLayout->addLayout(runStateTitleRow);
    runStateLayout->addWidget(runStateDetailLabel);
    runStateLayout->setContentsMargins(0, 0, 0, 0);

    // 这一整格是一颗**按钮**：点它向上弹出实例那块面板（用户 2026-09-23 要的）。
    // 按钮的宽度是**钉死**的，所以这一格的宽度跟"里面有没有字、字有多长"彻底没关系。
    //
    // NOTE: 宽度钉在**按钮**上、不钉在里面那两行字上，是量出来的结论：那两行在没有实例
    //       在跑时是藏起来的，而 **Qt 的布局把藏起来的格子当成宽度 0** —— 钉在字上的话，
    //       「下载中」到「启动起来」这一步就是 0 → 200，中间那根条当场从 582 缩到 440
    //       （**142 px**），用户报的「实例运行中一出来条就变长变短」量到的就是这个数。
    //       钉在按钮上，这一格就一直在。
    //       按钮里那个 layout 的边距要显式清成 0：**套在控件里的 layout 默认会拿样式给的
    //       边距**（套在 layout 里的不会），不清的话这格是 200 加左右两条边距。
    runStateButton = new QPushButton(bottomBar);
    runStateButton->setObjectName(runStateButtonName());
    runStateButton->setFixedWidth(MainWindowUI::bottomBarRunStateColumnWidth());
    // NOTE: **竖直方向必须自己说要铺满**。QPushButton 的竖直 sizePolicy 默认是 Fixed，
    //       而它这会儿的高度提示只有一行字那么点（QSS 把 padding 和 border 都清零了，
    //       实测 sizeHint 高 12 px）—— 结果这一格只有 12 px 高，里面那两行字被压成各 6 px
    //       （"文字和箭头挤在一起、显示不全"，用户 2026-09-23 报的），鼠标划过时那块
    //       高亮框也成了一长条。宽度钉死 + 高度铺满，这一格才真的是"占住底栏那一块"。
    runStateButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    runStateButton->setCursor(Qt::PointingHandCursor);
    runStateButton->setToolTip(MainWindowUI::bottomBarRunPanelTip());
    applyBottomBarButtonStyle(runStateButton, bottomBar->palette());
    QHBoxLayout *runStateRow = new QHBoxLayout(runStateButton);
    runStateRow->setContentsMargins(0, 0, 0, 0);
    runStateRow->setSpacing(MainWindowUI::bottomBarChevronSpacing());
    runStateRow->addLayout(runStateLayout, 1);
    // 这个箭头**另存一份指针**（右边那颗按钮上那个不用）：没有实例在跑时它要收起来，
    // 见 setRunPanelAvailable()。
    runStateChevron = makeChevronLabel(runStateButton);
    runStateRow->addWidget(runStateChevron, 0, Qt::AlignVCenter);

    // 没有实例在跑时这几样本来会藏起来（那时 MainWindow 写「当前没有运行中的实例」）。
    // **藏的是字和那颗方块，不是格子** —— 格子（上面那颗按钮）永远占着那 170 px（宽度是
    // bottomBarRunStateColumnWidth()，从 200 缩到 170 那次改的，别照旧数字找）。
    // NOTE: 方块和那两行一起 hide() 是初值：产品里 MainWindow 每次重画底栏都会摆一个状态
    //       （三个状态各有一句话，见 updateBottomBar()），所以这之后它一直是亮的。
    runStateMarkLabel->hide();
    runStateLabel->hide();
    runStateDetailLabel->hide();

    // ---------- 中：任务摘要（点一下展开任务中心） ----------
    // 这一格**也是一整颗按钮**（coding plan §3.1「整块可点击」），做法跟两头那两颗一样：
    // 悬停高亮走 applyBottomBarButtonStyle()、右边一个朝上的箭头、整格都是一个点击区。
    // 宽度**不钉**（两头那两颗才钉）—— 中间这格本来就是"吃掉富余宽度"的那一份。
    taskSummaryButton = new QPushButton(bottomBar);
    taskSummaryButton->setObjectName(taskSummaryButtonName());
    // 竖直方向铺满，理由同另外两颗（QPushButton 默认 Fixed，会让这一格只有一行字那么高）。
    taskSummaryButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    taskSummaryButton->setCursor(Qt::PointingHandCursor);
    taskSummaryButton->setToolTip(MainWindowUI::taskSummaryTip());
    applyBottomBarButtonStyle(taskSummaryButton, bottomBar->palette());

    // 里面一列两样：上面"标题 + 右边那句小字 + 箭头"，下面那根进度条。
    QVBoxLayout *taskLayout = new QVBoxLayout(taskSummaryButton);
    taskLayout->setContentsMargins(0, 0, 0, 0);
    taskLayout->setSpacing(2);
    QHBoxLayout *taskTextRow = new QHBoxLayout();
    taskTextRow->setContentsMargins(0, 0, 0, 0);
    taskTextRow->setSpacing(MainWindowUI::bottomBarChevronSpacing());

    // 「还没看过的一次失败」那颗红点：**收起来的**（布局里宽度是 0，所以不显示的时候
    // 右边那几样不会跟着挪）。它是个真控件，不是拼进文字里的字符 —— 图的颜色得能跟着
    // 主题走（画的，见 setBottomBarTaskSummary()）。
    taskFailureDot = new QLabel(taskSummaryButton);
    taskFailureDot->setObjectName(MainWindowUI::taskSummaryDotName());
    taskFailureDot->setFixedSize(MainWindowUI::taskCenterPanelFailureDotSize(),
                                 MainWindowUI::taskCenterPanelFailureDotSize());
    taskFailureDot->hide();
    taskTextRow->addWidget(taskFailureDot, 0, Qt::AlignVCenter);

    statusLabel = new ElidedLabel(taskSummaryButton);
    statusLabel->setObjectName(statusLabelName());
    // 固定一个最小宽度：这一格的字长长短短时，右边那几样不会再跳来跳去
    statusLabel->setMinimumWidth(kStatusLabelMinWidth);
    // **上限也得有**（2026-09-24 补的）。ElidedLabel 的 sizeHint() 是把宽度封在上限上，
    // 而上限的来源就是 maximumWidth() —— 不设上限时，一句长文件名会把这一格的 sizeHint
    // 顶到 3912、把底栏顶到 4338、把**主窗口**顶到 4338（默认窗口只有 900 宽，实测见
    // ui_check 的 E5 段）。理由全文（含"最小宽度那一半其实不受影响"）见头文件那段。
    statusLabel->setMaximumWidth(MainWindowUI::bottomBarStatusTextWidth());
    // NOTE: 它是 ElidedLabel（**不是**普通 QLabel）：中间那格比这句长的时候（任务标题长、
    //       窗口窄）普通 QLabel 是**硬裁**的 —— 最后一个字被切掉一半就杵在那儿。
    //       超出去的部分由它自己截成省略号。上下两个宽度一夹，布局就钉死了。
    taskTextRow->addWidget(statusLabel, 0, Qt::AlignVCenter);

    // 弹簧摆在**标题和小字之间**（不是小字右边）：§3.1 要的是「在**右侧**写'另有 2 项'」，
    // 而标题那一格有自己的上限（320），弹簧在中间才把那句小字推到右边去。
    taskTextRow->addStretch(1);

    // 「另有 N 项」那句小字。宽度封顶（`taskExtraTextWidth()`）—— 它要是能被一句长话顶宽，
    // 中间那格就会把旁边那颗按钮挤走（E5 那一段量的就是这件事）。
    taskExtraLabel = new ElidedLabel(taskSummaryButton);
    taskExtraLabel->setObjectName(taskExtraLabelName());
    taskExtraLabel->setMaximumWidth(MainWindowUI::taskExtraTextWidth());
    taskExtraLabel->setMinimumWidth(0);
    {
        QFont extraFont = taskExtraLabel->font();
        extraFont.setPointSize(qMax(1, extraFont.pointSize() - 1));
        taskExtraLabel->setFont(extraFont);
        QPalette extraPalette = taskExtraLabel->palette();
        extraPalette.setColor(QPalette::WindowText,
                              extraPalette.color(QPalette::PlaceholderText));
        taskExtraLabel->setPalette(extraPalette);
    }
    taskTextRow->addWidget(taskExtraLabel, 0, Qt::AlignVCenter);
    taskTextRow->addWidget(makeChevronLabel(taskSummaryButton), 0, Qt::AlignVCenter);
    taskLayout->addLayout(taskTextRow);

    progressBar = new QProgressBar(taskSummaryButton);
    progressBar->setObjectName(progressBarName());
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setTextVisible(true);
    taskLayout->addWidget(progressBar);

    // 一开始一项任务都没有：那一句低强调度的占位话 + **不显示空进度条**（§3.1）。
    // 产品里 MainWindow 每次重画底栏都会摆一份真的进来（updateBottomBar()），
    // 这里这个初值管的是**还没被谁摆过**的时候（独立构造 MainWindowUI 的地方吃它）。
    taskExtraLabel->hide();
    progressBar->hide();
    setBottomBarTaskSummary(BottomBarTaskSummary());

    // ---------- 右：头像 + 用户名 ----------
    // 真正的手绘皮肤头像要等 Minecraft profile API（登录流程现在只存了令牌，没取
    // 玩家档案），这里先画一块"首字母方块"。颜色同样取自调色板，不写死。
    // NOTE: 刻意**不加 border-radius** —— 这块是方的。真皮肤头像是 8x8 的方块贴图，
    //       方形底比圆形底更接近它以后的样子。
    avatarLabel = new QLabel(QStringLiteral("?"), bottomBar);
    avatarLabel->setObjectName(avatarLabelName());
    avatarLabel->setFixedSize(kAvatarSize, kAvatarSize);
    avatarLabel->setAlignment(Qt::AlignCenter);
    // 底和字都取自调色板的两个角色（选中那一块 / 它上面的字）——**不写死颜色**。
    // NOTE: QSS 里嵌的是**色名** ⇒ 主题一换它不会自己变，所以这两句包成一个动作、
    //       除了现在跑一遍，还挂在 `Theme::follow()` 上（下面那一条）。
    const auto paintAvatar = [this]() {
        const QPalette appPalette = QApplication::palette();
        avatarLabel->setStyleSheet(
            QStringLiteral("QLabel { background-color: %1; color: %2; }")
                .arg(appPalette.color(QPalette::Highlight).name(),
                     appPalette.color(QPalette::HighlightedText).name()));
    };
    paintAvatar();
    Theme::follow(avatarLabel, [paintAvatar]() { paintAvatar(); });

    accountLabel = new ElidedLabel(bottomBar);
    accountLabel->setObjectName(accountLabelName());
    accountLabel->setText(QStringLiteral("?"));
    // 用户名给一个宽度上限：正版名最长 16 个字符，长度一变右边那格就带着进度条一起动。
    // 这里**只封顶、不钉死**（和左边那格不一样，理由见头文件那段）：这格右对齐贴着窗边，
    // 钉死会让短名字在窗口最右边留出一块空白。
    // 上限之外的名字截成省略号，不硬裁（见 ElidedLabel）。
    accountLabel->setMaximumWidth(MainWindowUI::bottomBarAccountColumnWidth());

    // 第二行：这次启动会用哪种身份（正版 / 离线 / 还没登录）。做法**照抄左上角那格**
    // （小一号 + PlaceholderText 颜色）—— 底栏左右两格在视觉上是对称的两条两行文字。
    // 文字由 MainWindow::updateBottomBar() 刷，界面这边不持有业务状态。
    accountTypeLabel = new QLabel(accountTypeNotLoggedInText(), bottomBar);
    accountTypeLabel->setObjectName(accountTypeLabelName());
    accountTypeLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    QFont accountTypeFont = accountTypeLabel->font();
    accountTypeFont.setPointSize(qMax(1, accountTypeFont.pointSize() - 1));
    accountTypeLabel->setFont(accountTypeFont);
    QPalette accountTypePalette = accountTypeLabel->palette();
    accountTypePalette.setColor(QPalette::WindowText,
                                accountTypePalette.color(QPalette::PlaceholderText));
    accountTypeLabel->setPalette(accountTypePalette);

    QVBoxLayout *accountTextLayout = new QVBoxLayout();
    accountTextLayout->setSpacing(0);
    accountTextLayout->addWidget(accountLabel);
    accountTextLayout->addWidget(accountTypeLabel);
    accountTextLayout->setContentsMargins(0, 0, 0, 0);

    // 这一格也是**一整颗按钮**：点它向上弹出账号那块面板（和左边那颗对称）。
    // 宽度同样**钉死**，理由见头文件 bottomBarAccountButtonWidth()：里面那一组靠右摆，
    // 所以钉死之后短名字看着照样贴着窗边，而中间那根条从此不再受"用户名多长"影响。
    accountButton = new QPushButton(bottomBar);
    accountButton->setObjectName(accountButtonName());
    accountButton->setFixedWidth(MainWindowUI::bottomBarAccountButtonWidth());
    // 竖直方向铺满，理由同左边那颗（默认 Fixed 会让这一格只有一行字那么高）。
    accountButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    accountButton->setCursor(Qt::PointingHandCursor);
    accountButton->setToolTip(QCoreApplication::translate("MainWindowUI", "点一下向上展开账号信息"));
    applyBottomBarButtonStyle(accountButton, bottomBar->palette());
    QHBoxLayout *accountRow = new QHBoxLayout(accountButton);
    accountRow->setContentsMargins(0, 0, 0, 0);
    accountRow->setSpacing(MainWindowUI::bottomBarAccountRowSpacing());
    // 头像**贴着按钮左边缘**摆（不是靠右摆）：这一格宽 208，而头像 + 名字常态只有
    // 九十来 px，靠右摆就会在头像左边留出一大段空白 —— 用户 2026-09-23 报的
    // 「头像那里的左侧有一段空白」就是它（实测空 115 px：头像 x=115、按钮宽 208）。
    // 弹簧因此摆在**名字和箭头之间**：剩下的那一截落在名字右边的姓名格里
    // （姓名格自己就是这么宽的），箭头照旧钉在窗口右边，中间那根条的位置一点都不动。
    accountRow->addWidget(avatarLabel);
    // 头像和那两行文字按竖直居中对齐（QHBoxLayout 默认就是这个），所以名字多一行之后
    // 那块方头像不会往上看。
    accountRow->addLayout(accountTextLayout);
    accountRow->addStretch(1);
    accountRow->addWidget(makeChevronLabel(accountButton), 0, Qt::AlignVCenter);

    // 那两颗按钮点开都是**向上**弹一块面板（像官启那样）。面板里画什么由 MainWindow 灌
    // （每次点开现问一遍，见 setBottomBarPanelContent()），这里只留"怎么弹、弹在哪、
    // 点了哪一行该告诉谁"。左边那块（account = false）列**正在跑的实例**，右边那块
    // （account = true，也就是右对齐的那块）列**最近用过的账号**。
    // 开关的三种情形都归 toggleBottomBarPanel() 一处：点同一颗 = 收回、点另一颗 = 收旧开新、
    // 都没开着 = 开。**每一次点击都算数**（用户 2026-09-25 原话：「下面两个上拉槽改成和下载
    // 版本选择一样的，点击一下展开，再点击就关闭」，以及"点另一颗要一条路走到位"，见
    // BottomBarPanel::handleOutsidePress() 那两段 NOTE）。
    //
    // NOTE: 行里的按钮点下去不是"立刻做事"，而是"面板先收、动作排到下一轮"（见
    //       BottomBarPanel 里 dispatch 那段）—— 那两个动作都要弹模态窗，压在 Qt::Popup
    //       底下会点不动。所以**别**在这里顺手补一句"点完也 close 一下"：那是同一件事
    //       做两遍，而且顺序反了就白做。
    QObject::connect(runStateButton, &QPushButton::clicked, runStateButton, [this]() {
        toggleBottomBarPanel({runStateButton, BottomBarAnchorKind::Instances});
    });
    QObject::connect(accountButton, &QPushButton::clicked, accountButton, [this]() {
        toggleBottomBarPanel({accountButton, BottomBarAnchorKind::Accounts});
    });
    // 中间那格也是**一颗按钮**（2026-10-12）：点它向上展开任务中心。三格挂了同一个
    // toggle，所以"点另一颗 = 收旧开新"在三个方向上都成立（那一件事全靠 toggle）。
    QObject::connect(taskSummaryButton, &QPushButton::clicked, taskSummaryButton, [this]() {
        toggleBottomBarPanel({taskSummaryButton, BottomBarAnchorKind::Tasks});
    });

    // 三格从左到右摆：实例状态（0）· 空档（1）· 任务摘要（6）· 空档（1）· 账户（0）。
    // 两头那两颗按钮的宽度都是钉死的，份数写 0；富余宽度全在中间三份里分。
    // NOTE: 中间那份**从 4 提到 6**（2026-09-23）：两头那两格原来吃掉 458 px，900 px 的
    //       窗口里富余只有 370 px，中间拿 4/6 就只有 247 px（实测条 262 px）—— 用户报
    //       「中间进度条奇小无比」。缩了两头（200+258 → 170+208）之后再让中间拿 6/8，
    //       实测条 356 px（900 的窗口，从 262 涨上来的），两边各留一份空档所以还居中。
    // NOTE: 中间那一份 2026-10-12 从"一个裸布局"变成"一颗按钮"（任务摘要），份数照旧是 6
    //       —— 按钮里面那个 layout 的边距显式清成了 0，所以宽度还是原来那 356 px。
    barLayout->addWidget(runStateButton, 0);
    barLayout->addStretch(1);
    barLayout->addWidget(taskSummaryButton, 6);
    barLayout->addStretch(1);
    barLayout->addWidget(accountButton, 0);

    mainLayout->addWidget(bottomBar);
}

QString MainWindowUI::folderActionText(int action) {
    // 上下文写死 "MainWindowUI"：这个类不是 QObject（没有 tr()），和 launchingStatusText()
    // 那几句同一个写法。
    switch (action) {
    case FolderActionRefresh:
        return QCoreApplication::translate("MainWindowUI", "刷新");
    case FolderActionRename:
        return QCoreApplication::translate("MainWindowUI", "重命名");
    case FolderActionOpen:
        return QCoreApplication::translate("MainWindowUI", "打开");
    case FolderActionRemove:
        return QCoreApplication::translate("MainWindowUI", "移除");
    case FolderActionDelete:
        return QCoreApplication::translate("MainWindowUI", "删除");
    }
    // 越界（理论上到不了：菜单是照 folderActionCount() 循环摆的）。给空串而不是随便
    // 返回一项 —— 空串在画面上是个空白按钮，一眼看得出不对。
    return QString();
}

QString MainWindowUI::folderActionTooltip(int action) {
    switch (action) {
    case FolderActionRefresh:
        return QCoreApplication::translate("MainWindowUI",
                                           "重新扫一遍这个文件夹（里面多了或少了一个版本，"
                                           "按一下就能看到）");
    case FolderActionRename:
        // 这句是用户最容易误会的一步：「重命名」听起来像要把磁盘上的目录改个名，
        // 而它改的只是一个**显示用的名字**。所以正文里把"目录名不动"写死。
        return QCoreApplication::translate("MainWindowUI",
                                           "只改列表上显示的名字，磁盘上的目录名不动");
    case FolderActionOpen:
        return QCoreApplication::translate("MainWindowUI",
                                           "用文件资源管理器打开这个文件夹");
    case FolderActionRemove:
        return QCoreApplication::translate("MainWindowUI",
                                           "只从列表里移除：文件夹本身和里面的文件都不会被删掉");
    case FolderActionDelete:
        return QCoreApplication::translate("MainWindowUI",
                                           "把这个文件夹和里面的文件从磁盘上删掉（不可恢复）");
    }
    return QString();
}

QPixmap MainWindowUI::folderActionIcon(int action, int size, const QColor &color) {
    // 刷新和 X 用的是树上早就有的那两个字形（同一个"转一圈回来"、同一个"×"），
    // 另外三个是本轮新画的（见 VersionIcons 里那三个函数的说明）。
    switch (action) {
    case FolderActionRefresh:
        return VersionIcons::refresh(size, color);
    case FolderActionRename:
        return VersionIcons::pen(size, color);
    case FolderActionOpen:
        return VersionIcons::folderMark(size, color);
    case FolderActionRemove:
        return VersionIcons::cross(size, color);
    case FolderActionDelete:
        return VersionIcons::trash(size, color);
    }
    return QPixmap();
}

bool MainWindowUI::paletteIsDark(const QPalette &palette) {
    // ★ 2026-09-28 起判据只有一处（`Theme::isDarkPalette()`）—— 主题那一层要知道同一件事
    //   （语义色按深浅挑），而"两处各写一遍 lightness() < 128"正是这个改动要收掉的东西。
    return Theme::isDarkPalette(palette);
}

QColor MainWindowUI::folderDangerColor(const QPalette &palette) {
    // 出厂的深浅两档（#ff6b6b / #c02828）、以及"用户点名要一个红"这一档，都在 Theme 里。
    return Theme::dangerColor(palette);
}

QColor MainWindowUI::installNameErrorColor(const QPalette &palette) {
    // NOTE: 这一处红和上面那个**故意不是同一个色**：浅底 #c0392b 对窗口底色 #f0f0f0
    //       实测 4.77:1（够 WCAG 正文那条 4.5:1），#c02828 是另一条来历。深底那一档没法
    //       照搬同一个 #c0392b（只有 2.26:1），所以借了同一个浅红。理由整段挪进了
    //       `Theme::errorTextColor()`。
    return Theme::errorTextColor(palette);
}

QString MainWindowUI::groupActionText(int action, const GroupRow &row) {
    // 上下文写死 "MainWindowUI"：这个类不是 QObject（没有 tr()），和上面那几张
    // 文件夹的表同一个写法。
    switch (action) {
    case GroupActionMoveInto:
        // 带省略号：点了之后还要弹一个"移进哪一组"的小框问点什么（和「重命名…」同一条
        // 规矩）。那一项**必须**有 —— 一个实例都没有的组在树里是藏着的，把它放进第一个
        // 实例这件事只有这一个入口（见 openGroupPicker）。
        return QCoreApplication::translate("MainWindowUI", "把选中的实例移入分组…");
    case GroupActionClearMembers:
        return QCoreApplication::translate("MainWindowUI", "把这一组里的实例都移出分组");
    case GroupActionRename:
        return QCoreApplication::translate("MainWindowUI", "重命名这一组…");
    case GroupActionToggleExpand:
        // 状态**写在字里**（而不是摆一个勾字形）：菜单里只有这一项有状态，摆勾会变成
        // "有的项前面有东西、有的没有"，右边那列文字全对不齐。
        return row.expanded ? QCoreApplication::translate("MainWindowUI", "默认展开：开")
                            : QCoreApplication::translate("MainWindowUI", "默认展开：关");
    case GroupActionDelete:
        return QCoreApplication::translate("MainWindowUI", "删除这一组…");
    }
    // 越界（理论上到不了：菜单是照 groupActionCount() 循环摆的）。给空串而不是随便
    // 返回一项 —— 空串在画面上是个空白按钮，一眼看得出不对。
    return QString();
}

bool MainWindowUI::groupActionEnabled(int action, const GroupRow &row) {
    switch (action) {
    case GroupActionMoveInto:
        // 没有选中的实例就没什么可移的。
        return row.hasSelection;
    case GroupActionClearMembers:
        // 空组**在树里是看不见的**，所以这一条真正拦的是"菜单开着的时候实例被别处
        // 搬走了"那种过期快照。
        return row.memberCount > 0;
    case GroupActionRename:
    case GroupActionToggleExpand:
    case GroupActionDelete:
        // 这三项什么时候都办得到（删一个有实例的组也只是把那些实例放回未分组，
        // 不删任何东西 —— 见下面那句提示）。
        return true;
    }
    return false;
}

QString MainWindowUI::groupActionTooltip(int action, const GroupRow &row) {
    // 点不动的那几项要在**这里**说清为什么（菜单那边照着这个把话挂上去）。
    switch (action) {
    case GroupActionMoveInto:
        if (!row.hasSelection) {
            return QCoreApplication::translate("MainWindowUI",
                                               "先在列表里选中一个实例，再回这儿挑一组");
        }
        return QCoreApplication::translate(
            "MainWindowUI", "挑一个组把选中的实例放进去（它只能属于一个组，原来在别的组的话就换过来）");
    case GroupActionClearMembers:
        if (row.memberCount <= 0) {
            return QCoreApplication::translate("MainWindowUI", "这一组里一个实例都没有");
        }
        return QCoreApplication::translate("MainWindowUI",
                                           "这一组里的 %1 个实例会回到未分组"
                                           "（组留着，实例一个都不删）")
            .arg(row.memberCount);
    case GroupActionRename:
        return QCoreApplication::translate("MainWindowUI",
                                           "给这一组换个名字（只改名字，组里的实例不动）");
    case GroupActionToggleExpand:
        // 这一步同时是"默认"和"当前"：树每次重灌都按它画（所以点完那一下，收着的组
        // 不会在下次刷新时自己弹开）。
        return row.expanded ? QCoreApplication::translate("MainWindowUI",
                                                          "这一组现在画成展开的。"
                                                          "点一下改成默认收起")
                            : QCoreApplication::translate("MainWindowUI",
                                                          "这一组现在画成收着的。"
                                                          "点一下改成默认展开");
    case GroupActionDelete:
        return QCoreApplication::translate("MainWindowUI",
                                           "删掉这一组：里面的 %1 个实例会回到未分组，"
                                           "一个实例都不会被删掉")
            .arg(row.memberCount);
    }
    return QString();
}

QColor MainWindowUI::runStateMarkColor(RunStateMark state, const QPalette &palette) {
    // 三个色现在都是**语义色**（Theme 那一层）：红 = 危险/没在跑、黄 = 警告/启动中、
    // 绿 = 正常/在跑。深浅两档的判据、以及"用户点名要某个色"这一档，都在 Theme 里 ——
    // 这里只剩下"哪一档状态是哪一个语义"（那是界面的说法，不是主题的说法）。
    switch (state) {
    case RunStateMark::NotRunning:
        // 红这一档**直接复用**「删除」那一项的红（同一个界面上的两种红各调各的没有道理，
        // 调歪了就是"一处红看着干净、另一处发脏"这种没人查得出的毛病）。
        return folderDangerColor(palette);
    case RunStateMark::Launching:
        return Theme::warningColor(palette);
    case RunStateMark::Running:
        return Theme::okColor(palette);
    }
    return palette.color(QPalette::WindowText);
}

QPixmap MainWindowUI::runStateMarkPixmap(RunStateMark state, const QPalette &palette) {
    // 图按 (边长, 颜色) 缓存（VersionIcons 里那套），所以这个函数每次重画底栏都能随便调。
    return VersionIcons::runStateMark(runStateMarkSize(), runStateMarkColor(state, palette));
}

void MainWindowUI::setFolderRows(const QList<FolderRow> &rows) {
    QListWidget *list = folderList;
    if (list == nullptr) {
        return;  // setupUi() 还没跑完（和那几个访问器拿空指针当判据是同一个意思）
    }
    // 重灌不是"用户改选"，别让最后那句 setCurrentItem() 把切换逻辑又跑一遍（换了当前
    // 文件夹等于重扫磁盘、重灌界面）。这一条以前在 MainWindow::refreshFolderList() 里，
    // 现在填表搬到这里了，挡住信号的这一句就跟着搬过来。
    const QSignalBlocker blocker(list);
    list->clear();
    for (int i = 0; i < rows.size(); ++i) {
        const FolderRow row = rows.at(i);
        auto *item = new QListWidgetItem(list);
        // 身份照旧挂在 UserRole 上（updateFolderButtons() 和点击那条路都按它认人）。
        item->setData(Qt::UserRole, row.path);
        // 行高**必须显式给**：setItemWidget() 摆进去的那个控件多高由这一格决定，
        // 不给的话条目按"没有文字"算高度，那一行会被压成一条缝。
        item->setSizeHint(QSize(0, folderRowHeight()));
        // 回调捕的是 row 本身（不是行号）：菜单弹出来到用户点某一项之间列表是会变的。
        auto *widget = new FolderRowWidget(
            row, i, [this](const FolderRow &clicked, QWidget *gear) { openFolderMenu(clicked, gear); },
            list->viewport());
        list->setItemWidget(item, widget);
        if (row.current) {
            list->setCurrentItem(item);
        }
    }
}

void MainWindowUI::openFolderMenu(const FolderRow &row, QWidget *gear) {
    // 弹出来那一刻现问一遍"这一行长什么样"是做不到的（数据在 MainWindow 手里，
    // 而这里只是个画界面的）—— 所以用的就是点这颗齿轮时捕下来的那一份快照。
    // 它可能已经过时了（比如这一份刚被别处删掉）：那几种情况由 MainWindow 那边
    // 按 path 重新查一遍状态来处理，界面这一层不猜。
    QListWidget *const list = folderList;
    if (list == nullptr) {
        return;  // setupUi() 还没跑完（齿轮是列表里的东西，正常到不了这儿）
    }
    // "现在有没有开着一块"靠 findChild 认（和底栏那两块面板同一个路数）：
    // NOTE: **挂在这个列表底下**，不是挂 MainWindowUI 上面 —— 这个类根本不是 QObject
    //       （它没有 tr()），没有 findChild()。列表是这一行那个齿轮的宿主，也是全窗口
    //       寿命的东西，拿它当 popup 的 parent 既找得回来又不会被谁提前删掉。
    if (auto *open = dynamic_cast<FolderMenu *>(list->findChild<QWidget *>(folderMenuName()))) {
        const bool sameAnchor = open->anchor() == gear;
        open->close();
        if (sameAnchor) {
            return;
        }
    }
    const FolderMenu::ActionCallback onAction = [this](const QString &path, int action) {
        if (m_folderActionHandler) {
            m_folderActionHandler(path, action);
        }
    };
    auto *menu = new FolderMenu(row, gear, onAction, list);
    // 这次弹出来的框归它自己管：关掉就删（不然点一次攒一个）。
    menu->setAttribute(Qt::WA_DeleteOnClose);
    // 开口朝右下：贴着齿轮的**右下角**往下展开一点点。菜单弹在行尾那颗按钮的下方
    // 而不是"鼠标点在哪就弹在哪"—— 那样的话同一个齿轮点两次会弹在差几个像素的
    // 地方，而且行高一行只有一次，对齐按钮比对齐鼠标更稳。
    menu->popUpAt(gear->mapToGlobal(QPoint(0, gear->height() + kPanelGap)));
}

void MainWindowUI::openGroupMenu(const GroupRow &row, const QRect &anchorGlobal) {
    // 和文件夹那一块同一个路数：弹出来那一刻**不重新查数据**（数据在 MainWindow 手里），
    // 用的就是点这颗齿轮时捕下来的那一份快照；过期的那几种情况由 MainWindow 按组 id
    // 重新查一遍来处理，界面这一层不猜。
    QTreeWidget *const tree = instanceList;
    if (tree == nullptr) {
        return;  // setupUi() 还没跑完（组行是树里的东西，正常到不了这儿）
    }
    // "现在有没有开着一块"靠 findChild 认，**挂在这棵树底下**（这个类不是 QObject，
    // 没有 findChild；树是那颗齿轮的宿主，也是全窗口寿命的东西）。
    //
    // NOTE: 两块框（动作菜单 / 挑目标）**共用一个 objectName** —— 同一颗齿轮下只可能
    //       开着其中一个，按名字捞回来的就该是当下那一个。捞回来的是"挑目标"那块时
    //       groupId() 是空串，于是它永远不认"再点同一颗齿轮"，直接收掉重开动作菜单
    //       （那正是"想换个操作"时该发生的事）。
    if (auto *open = dynamic_cast<GroupMenu *>(tree->findChild<QWidget *>(groupMenuName()))) {
        const bool sameAnchor = !open->pickMode() && open->groupId() == row.groupId;
        open->close();
        if (sameAnchor) {
            return;
        }
    }
    const GroupMenu::ActionCallback onAction = [this](const QString &groupId, int action) {
        if (m_groupActionHandler) {
            m_groupActionHandler(groupId, action);
        }
    };
    // 重放那一侧就是这棵树的 viewport（见 GroupMenu::handleAnchorPress()）。
    auto *menu = new GroupMenu(row, anchorGlobal, onAction, tree->viewport(), tree);
    // 这次弹出来的框归它自己管：关掉就删（不然点一次攒一个）。
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popUpAt(QPoint(anchorGlobal.left(), anchorGlobal.bottom() + kPanelGap));
}

void MainWindowUI::openGroupPicker(const QList<GroupRow> &rows, const QRect &anchorGlobal) {
    QTreeWidget *const tree = instanceList;
    if (tree == nullptr) {
        return;
    }
    // 挨个换菜单时（动作菜单 → 移入分组… → 这个框）前一块**已经在关的路上了**
    // （GroupMenu 里是"先 close() 再排到下一轮事件循环交给回调"），正常捞不到它；
    // 捞到了就是"同一颗齿轮下还开着一块"，收掉再开新的。
    if (auto *open = dynamic_cast<GroupMenu *>(tree->findChild<QWidget *>(groupMenuName()))) {
        open->close();
    }
    const GroupMenu::PickCallback onPick = [this](const QString &groupId) {
        if (m_groupPickHandler) {
            m_groupPickHandler(groupId);
        }
    };
    auto *menu = new GroupMenu(rows, anchorGlobal, onPick, tree->viewport(), tree);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    // 和动作菜单弹在**同一个位置**：那颗齿轮下面。用户看到的是"刚才那块框换了个内容"，
    // 而不是"又冒出来一块"（这也是"挑目标"不做成二级子菜单的代价换来的好处：
    // 二级子菜单在 Qt::Popup 上要另立一套抓取/收回，验证工程也驱动不动）。
    menu->popUpAt(QPoint(anchorGlobal.left(), anchorGlobal.bottom() + kPanelGap));
}

MainWindowUI::BottomBarAnchor MainWindowUI::bottomBarAnchorAt(const QPoint &globalPos) const {
    // 三颗按钮摆在哪由底栏的布局说了算，所以判据只能是"这个全局坐标落在它自己的矩形里吗"
    //（mapFromGlobal 之后按 rect() 判 —— 和所有"点在不在这个控件上"的写法一致）。
    const auto hits = [&globalPos](QPushButton *button) {
        return button != nullptr
               && button->rect().contains(button->mapFromGlobal(globalPos));
    };
    if (hits(runStateButton)) {
        return {runStateButton, BottomBarAnchorKind::Instances};
    }
    if (hits(accountButton)) {
        return {accountButton, BottomBarAnchorKind::Accounts};
    }
    if (hits(taskSummaryButton)) {
        return {taskSummaryButton, BottomBarAnchorKind::Tasks};
    }
    return {};
}

TaskCenterPanel *MainWindowUI::taskCenterPanel() const {
    // 认它靠 objectName + 动态转型（和那两块 BottomBarPanel 一个路数）：面板挂在底栏底下，
    // close() 之后要等 deleteLater 才真删，所以"现在开着哪一块"不用自己记指针 —— 记了
    // 反而要操心它在哪一行被删掉。
    return dynamic_cast<TaskCenterPanel *>(
        bottomBar != nullptr ? bottomBar->findChild<QWidget *>(taskCenterPanelName()) : nullptr);
}

void MainWindowUI::openTaskCenterPanel() {
    if (bottomBar == nullptr || taskSummaryButton == nullptr) {
        return;
    }
    // 点在面板外面的那一下：和那两块面板同一套（理由全文见 BottomBarPanel 里
    // handleOutsidePress() 那两段 NOTE —— 包括"为什么必须在 close() 之前钉
    // WA_NoMouseReplay"）。
    TaskCenterPanel::OutsidePressHandler onOutsidePress;
    onOutsidePress.claimed = [this](const QPoint &globalPos) {
        return bottomBarAnchorAt(globalPos).button != nullptr;
    };
    onOutsidePress.dismissed = [this](const QPoint &globalPos) {
        const BottomBarAnchor hit = bottomBarAnchorAt(globalPos);
        if (hit.button != nullptr) {
            toggleBottomBarPanel(hit);
        }
    };
    auto *panel = new TaskCenterPanel(taskSummaryButton, m_taskCenterProvider,
                                      m_taskCenterClearHandler, onOutsidePress, bottomBar);
    // 点一行进详情页。★ **要在 popUp() 之前挂**：`rebuild()`（popUp() 里第一件事）就是按
    //   这个处理器决定那几行"可不可点"的 —— 挂晚了的话这一面板里的行没手型光标、点了也没
    //   反应，得关掉重开一次才行。
    panel->setRowActivatedHandler(m_taskCenterRowActivated);
    panel->setRowActionHandler(m_taskCenterRowAction);
    // 这次弹出来的面板归它自己管：关掉就删（不然点一次攒一个）。
    panel->setAttribute(Qt::WA_DeleteOnClose);
    panel->popUp();
}

void MainWindowUI::openBottomBarPanel(const BottomBarAnchor &anchor) {
    if (bottomBar == nullptr || anchor.button == nullptr) {
        return;
    }
    // 现问一遍要弹什么（不缓存：实例随时会结束、账号随时会多一个，"上次那几行"
    // 一旦留着就会弹出早就不是那样的一面板）。
    const QList<PanelRow> rows =
        m_panelRowsProvider ? m_panelRowsProvider(anchor.account()) : QList<PanelRow>();
    // 面板只知道"第几行第几颗"，"是哪一块面板"由这里补上再转给 MainWindow ——
    // 一块面板只可能属于一边，所以这个得在**建面板的时候**就绑死。
    const BottomBarPanel::ActionCallback onAction = [this, account = anchor.account()](
                                                        const QString &key, int action) {
        if (m_panelActionHandler) {
            m_panelActionHandler(account, key, action);
        }
    };
    // 点在面板外面的那一下：面板自己认不出"外面是哪儿"，所以这里按全局坐标认那一颗按钮。
    // 认出来了就压掉 Qt 的重放（claimed），收掉之后走**同一个** toggle 换过去（dismissed）——
    // 点另一颗那条路全靠它：那一下面板底下的按钮**一个事件都收不到**，只有面板收到了
    //（见 BottomBarPanel::handleOutsidePress() 的第一段 NOTE）。
    BottomBarPanel::OutsidePressHandler onOutsidePress;
    onOutsidePress.claimed = [this](const QPoint &globalPos) {
        return bottomBarAnchorAt(globalPos).button != nullptr;
    };
    onOutsidePress.dismissed = [this](const QPoint &globalPos) {
        const BottomBarAnchor hit = bottomBarAnchorAt(globalPos);
        if (hit.button != nullptr) {
            // NOTE: 这里让 toggle 自己去 findChild 认"现在开着哪一块"：刚刚被收掉的那一块
            //       还在（close 之后要等 deleteLater 才真删），所以"点的是同一颗"那一路它
            //       认得出来（认出来就什么都不开）；点另一颗则收旧开新。
            toggleBottomBarPanel(hit);
        }
    };
    auto *panel = new BottomBarPanel(anchor.button, anchor.account(), rows, onAction, onOutsidePress,
                                     bottomBar);
    // 这次弹出来的面板归它自己管：关掉就删（不然点一次攒一个）。
    panel->setAttribute(Qt::WA_DeleteOnClose);
    panel->popUp();
}

void MainWindowUI::toggleBottomBarPanel(const BottomBarAnchor &anchor) {
    if (bottomBar == nullptr || anchor.button == nullptr) {
        return;
    }
    // 左下那一格**没有实例可列时点不开**（箭头也收起来了，见 setRunPanelAvailable()）：
    // 这一下只当"收"用。
    //
    // NOTE: 之所以不只靠那颗按钮收不到鼠标（WA_TransparentForMouseEvents）：面板开着的
    //       时候，外面那一下是**投递给面板自己的**，它按坐标认出点在哪一颗按钮上再转给
    //       这里（见 BottomBarPanel::handleOutsidePress()）—— 那条路根本不经过按钮。
    //       而下面 open->close() 那一步照走，所以点在这颗点不动的格子上时，**已经开着的
    //       那一块会被收掉**（点外面本来就该收），只是不再开新的 —— 不然那一下看着像
    //       "点了没反应"，而那一块还杵在那儿。
    const bool blocked = anchor.kind == BottomBarAnchorKind::Instances && !m_runPanelAvailable;
    // "现在有没有开着一块"靠 findChild 认（和行尾齿轮菜单一个路数）：面板挂在 bottomBar
    // 底下，close 之后要等 deleteLater 才真删（同一轮事件处理里还找得到），不用自己记指针。
    //
    // NOTE: 中间那格那块是**另一个类、另一个名字**（`TaskCenterPanel`），所以这里要问两次
    //       —— 一次问"那两块短行面板开着吗"、一次问"任务中心开着吗"。只问一次的话，
    //       "任务中心开着时点左下那格"就会变成两块面板同时在屏幕上。
    bool sameAnchor = false;
    bool anyOpen = false;
    if (auto *open = dynamic_cast<BottomBarPanel *>(
            bottomBar->findChild<QWidget *>(MainWindowUI::bottomBarPanelName()))) {
        sameAnchor = open->anchor() == anchor.button;
        anyOpen = true;
        open->close();
    }
    if (auto *panel = taskCenterPanel()) {
        sameAnchor = sameAnchor || panel->anchor() == anchor.button;
        anyOpen = true;
        panel->close();
    }
    if (anyOpen && (sameAnchor || blocked)) {
        return;  // 再点一次**同一颗** = 收回去，就这一下
    }
    if (blocked) {
        return;
    }
    if (anchor.kind == BottomBarAnchorKind::Tasks) {
        openTaskCenterPanel();
        return;
    }
    openBottomBarPanel(anchor);
}

void MainWindowUI::setBottomBarTaskSummary(const BottomBarTaskSummary &summary) {
    if (statusLabel == nullptr || progressBar == nullptr) {
        return;  // setupUi() 还没跑完（和那几个访问器拿空指针当判据是同一个意思）
    }
    // ---------- 那一行字 ----------
    // 占位那一档（一项任务都没有）用调色板里的 PlaceholderText：§3.1 要的是**低强调度**
    // 的一句"暂无后台任务"，和"真有一件事在跑"的标题不能长成一个样子。
    //
    // NOTE: 色的底取 `QApplication::palette()`，不取这个标签自己的 —— 它装在一颗挂了
    //       QSS 的按钮里（applyBottomBarButtonStyle()），带样式表那句 `border: none;
    //       background: transparent` 不影响文字色，但按钮上那份 palette 是从调色板推出来的
    //       一份**副本**，主题换了之后哪个先更新不好说（和 runStateMarkPixmap 那条取
    //       "底栏的调色板"是同一个顾虑）。取应用那一份就不必操心谁先收到事件。
    QPalette textPalette = QApplication::palette();
    textPalette.setColor(QPalette::WindowText,
                         textPalette.color(summary.hasTask ? QPalette::WindowText
                                                           : QPalette::PlaceholderText));
    statusLabel->setPalette(textPalette);
    statusLabel->setText(summary.hasTask ? MainWindowUI::taskSummaryText(summary.title, summary.state)
                                         : MainWindowUI::taskSummaryNoTaskText());

    // 「失败」那一档整句染红：这一格只有一行字，"那次没办成"是它最要紧的信息。
    // （面板里那一行才有地方挂红点，见 ui/TaskCenterPanel.cpp。）
    if (summary.state == TaskCenter::State::Failed
        || summary.state == TaskCenter::State::Interrupted) {
        QPalette failedPalette = statusLabel->palette();
        failedPalette.setColor(QPalette::WindowText, folderDangerColor(textPalette));
        statusLabel->setPalette(failedPalette);
    }

    // 那一颗红点（"用户还没看过的一次失败"）：**藏着的时候它也在**，只是宽度 0。
    if (taskFailureDot != nullptr) {
        if (summary.unreadFailure) {
            taskFailureDot->setPixmap(VersionIcons::runStateMark(
                MainWindowUI::taskCenterPanelFailureDotSize(),
                folderDangerColor(textPalette)));
            taskFailureDot->show();
        } else {
            taskFailureDot->hide();
        }
    }

    // ---------- 右边那句小字（「另有 N 项」） ----------
    if (taskExtraLabel != nullptr) {
        if (summary.extraCount > 0) {
            taskExtraLabel->setText(MainWindowUI::taskSummaryMoreText(summary.extraCount));
            taskExtraLabel->show();
        } else {
            taskExtraLabel->clear();
            taskExtraLabel->hide();
        }
    }

    // ---------- 下面那根条 ----------
    // §3.1：「无任务时不显示空进度条」。所以没任务时它是**藏着的**（不是复位之后留着）。
    if (!summary.hasTask) {
        progressBar->setRange(0, 100);
        Anim::smoothProgress(progressBar, 0);
        progressBar->hide();
        return;
    }
    progressBar->show();
    if (summary.percent < 0) {
        // 测不出总量（同步等待那几段、启动那一段、以及"刚点下载、清单还在路上"）：
        // 不确定态的滚动条 —— 绝不编一个百分比出来（§3.4 那条）。
        progressBar->setRange(0, 0);
        return;
    }
    progressBar->setRange(0, 100);
    // 走同一条动画路数（§9.4 / ui/Animations.hpp 里那四条"原地落"的规则）：它自己认得出
    // "总量变了 / 退回 0 了"那几档，不用这里再判一遍。
    Anim::smoothProgress(progressBar, summary.percent);
}

void MainWindowUI::refreshTaskCenterPanel() {
    if (TaskCenterPanel *panel = taskCenterPanel()) {
        panel->refresh();
    }
}

void MainWindowUI::setRunPanelAvailable(bool available) {
    if (runStateButton == nullptr || m_runPanelAvailable == available) {
        return;
    }
    m_runPanelAvailable = available;
    // 箭头：没有东西可展开就收起来（用户 2026-09-25：「箭头隐藏」）。**只是藏起来、不删**
    // —— "底栏那两颗按钮各带一个箭头"有断言钉着（见 ui_check I4 那节），删了那条当场红。
    if (runStateChevron != nullptr) {
        runStateChevron->setVisible(available);
    }
    // 点不动的那一格**收的是鼠标**（理由见头文件里那段 NOTE：不用 setEnabled 是为了不让
    // 里面那几行字跟着变灰）。顺带把 hover 高亮、手型指针、tooltip 一起收掉了 —— 它们
    // 都挂在"这个控件收不收得到鼠标"上。
    runStateButton->setAttribute(Qt::WA_TransparentForMouseEvents, !available);
    // 点不动的格子不该再挂着"点一下向上展开实例列表"（鼠标都到不了它身上，也弹不出来）。
    runStateButton->setToolTip(available ? MainWindowUI::bottomBarRunPanelTip() : QString());
}

void MainWindowUI::setHomeRecent(const HomeRecent &recent) {
    if (homeRecentCard == nullptr) {
        return;  // setupUi() 还没跑完（和上面那些访问器拿空指针当判据是同一个意思）
    }
    // 一次都没启动过（或者账本读不出来、账上一条可玩的都没有）⇒ **整张卡收起来**。
    // NOTE: 这里只 `hide()` 不删：`homeRecentCard` 造在 `setupUi()` 里，验证工程找它用的是
    //       objectName，删掉就等于"这一档没地方量"。
    if (!recent.visible) {
        homeRecentCard->hide();
        return;
    }
    homeRecentCard->show();
    homeRecentNameLabel->setText(recent.instanceLine);

    // 第二行那半句（目标 / 时长 / 结局）：**可能一句都没有**（普通启动 + 正常退出 + 时长还
    // 没算出来），那时这一行整行收起来 —— 空的 QLabel 也占着一行字高，留着就是白留一块。
    homeRecentDetailLabel->setText(recent.detailLine);
    homeRecentDetailLabel->setVisible(!recent.detailLine.isEmpty());

    homeRecentWhenLabel->setText(recent.whenLine);
    homeRecentWhenLabel->setVisible(!recent.whenLine.isEmpty());

    // 那一颗主动作：目标还在就是「继续游玩」，目标（或者那个实例本身）没了就**降级**成
    // 「启动实例」（§5.1）。按钮**不消失** —— 目标被删了不该让这张卡变成死路一条。
    homeRecentContinueButton->setText(recent.canContinue ? MainWindowUI::homeRecentContinueText()
                                                        : MainWindowUI::homeRecentLaunchText());
}
