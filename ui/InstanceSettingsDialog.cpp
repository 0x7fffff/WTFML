/**
 * @file InstanceSettingsDialog.cpp
 * @brief 搭出「实例设置…」对话框的控件，并把它们读成一份覆盖值。
 * @author 0x7fffff
 * @date 2026-09-21
 * @note 这里**不落盘、不碰 src/**：值从控件读，落盘在 MainWindow 那边。
 *
 * 这一层唯一的"业务判断"是内存那两行在自动档下的表现（禁用 + 不写覆盖），
 * 理由写在 .hpp 的 override() 上。
 *
 * 2026-09-26 起这一页**没有「确定 / 取消」**：值一改就发 `settingsEdited()`，写盘那边
 * 接住就写（见 .hpp 里那个信号）。所以这一层的出口是**两条信号**加那一堆读值的函数。
 *
 * 2026-09-27 起这一页是**两栏**（左栏一列按实例现算的页表 | 右栏一块堆叠页，见 .hpp 里
 * pinnedPageKeys() 那一段）：控件全部直接挂在 `this` 上、按 objectName 认，所以分栏对
 * "读值"这一侧一个字都没改 —— 只是控件换了个父容器。
 */
#include "InstanceSettingsDialog.hpp"

#include "Animations.hpp"
#include "Theme.hpp"   // 浮层的底色（Theme::Part::Panel）

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QMimeData>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSize>
#include <QUrl>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <utility>

#include "ConfigFileEditorPage.hpp"
#include "InstanceFilesPage.hpp"
#include "MainWindowUI.hpp"
#include "MemorySlider.hpp"
#include "VersionIcons.hpp"
// 第 1 格那个世界设置（2026-09-28）。头文件那边已经 include 了它（进参是个嵌套类型），
// 这一句是给"这一份 .cpp 自己用得上它的完整定义"留的。
#include "WorldSettingsPage.hpp"
#include "VersionTreeDelegate.hpp"

namespace {

/// 数字框的宽度。和设置页那两个用同一个值 —— 同一个"内存 MB 数"在两处宽度不一样
/// 会显得像是两种东西。
constexpr int kMemorySpinWidth = 120;
/// 「最大内存」那一行那根条的最短宽度。比设置页那份（260）窄一点：这一页那一格左边还占着
/// [跟随全局][自定义] 两个单选钮，条再要 260 就得把数字框挤出门外了。
constexpr int kMemorySliderMinWidth = 200;
/// 「窗口大小」那一行**两个**数字框各自的宽度。比上面那个窄：这一行右边那两个框是并排的，
/// 每个都 120 就得 240 + 那个 × + 那对单选钮，整行会被挤出门外；而这里填的是四位像素数，
/// 80 已经能把「7680」连框一起摆下。
constexpr int kWindowSizeSpinWidth = 80;
/// 追加参数那一行的输入高度（三四行够放几条常见参数）。
constexpr int kAppendJvmArgsHeight = 76;
/// 左栏那一列导航在算出来的宽度上再加多少余量（左右各一半）。
/// `sizeHintForColumn` 给的是"字有多宽"，条目的字贴着边看着像被切了。
constexpr int kNavListExtraWidth = 28;
/// 两栏之间空多少。和整页外边距那一档（12）同一档。
constexpr int kColumnSpacing = 12;
/// 单独拿出来用时的初始大小。宽是"一列导航 + 一块还放得下两列表格的内容"量出来的
/// （见 .cpp 构造函数里那句 NOTE：嵌进覆盖层时这个数不作数）。
constexpr int kDialogWidth = 820;
constexpr int kDialogHeight = 560;
/// 图标预览那一格的边长（唯一出口是 InstanceSettingsDialog::iconPreviewSize()，外面按它
/// 解图递进来）。
constexpr int kIconPreviewSize = 64;
/// 八颗候选按钮上那张图多大。比预览小一档 —— 它们是"挑哪一类"，预览才是"结果"。
constexpr int kIconCandidateSize = 32;
/// 候选按钮一排摆几颗。八颗正好两排。
constexpr int kIconCandidatesPerRow = 4;

/// 这一页顶上那行标题长什么样。**唯一出处**：构造时那一行和 `setInstanceDisplayName()`
/// 那一行都走它（两边各拼一次的话，改完名之后那行字的格式迟早会飘）。
QString titleTextFor(const QString &displayName) {
    return InstanceSettingsDialog::tr("实例设置：%1").arg(displayName);
}

/// 把一列导航（左栏大类 / 中栏小类）摆进三栏那一行。
///
/// NOTE: 外面套一层**竖排 + 底部 stretch**，不是直接把列表 addWidget 进去 —— 列表自己是
///       定高的（"贴着内容"那条），而横排布局会把这一格的**高**整个给它，定高控件在那
///       一格里的落点是 Qt 说了算。套一层之后贴顶是写出来的，不靠猜。设置页左栏那一列
///       也是这个写法。
void addNavColumn(QHBoxLayout *columns, QListWidget *list) {
    auto *column = new QWidget(list->parentWidget());
    auto *layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(list);
    layout->addStretch(1);
    columns->addWidget(column);
}

/// 堆叠页第 @p index 格是**哪一页**（认不出来回空串）。`stackPageIndexOf()` 的反面。
///
/// NOTE: 摆在这一列左栏旁边，是因为"左栏现在该亮哪一条"要从"右栏现在摆着哪一页"反查
///       （见 rebuildEntryList()）—— 反过来再存一份"现在在哪一页"的影子状态，迟早会和
///       堆叠页对不上。
QString pageKeyAtStackIndex(int index) {
    if (index < 0) {
        return QString();
    }
    const QStringList pinned = InstanceSettingsDialog::pinnedPageKeys();
    if (index < pinned.size()) {
        return pinned.at(index);
    }
    const QStringList files = InstanceSettingsDialog::filePageKeys();
    const int fileIndex = index - InstanceSettingsDialog::filePageFirstIndex();
    return fileIndex >= 0 && fileIndex < files.size() ? files.at(fileIndex) : QString();
}

/// 往表格里加一行：**行名独占一行、内容在下一行**（2026-09-26 用户原话「上一行设置名称，
/// 下一行设置选项」）。跨度都是两列 —— 「启动」这一栏从此不再有"名字在左、控件在右"那一套。
///
/// @param row 行名那一行的下标。**内容占的是 `row + 1`** —— 调用方要 `row += 2`，不是
///        `row++`（少加的那一行会被下一项盖掉，界面上少一整行而没有任何报错）。
/// @param layoutOut 内容那一格的竖排布局（外面那层格子已经建好了，直接拿去加东西）。
/// @return 内容那一格（控件的父控件）。
QWidget *addTitledRow(QGridLayout *grid, int row, const QString &title, QWidget *parent,
                      QVBoxLayout **layoutOut = nullptr) {
    auto *label = new QLabel(title, parent);
    grid->addWidget(label, row, 0, 1, 2, Qt::AlignLeft);

    auto *cell = new QWidget(parent);
    auto *column = new QVBoxLayout(cell);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(4);
    grid->addWidget(cell, row + 1, 0, 1, 2);
    if (layoutOut != nullptr) {
        *layoutOut = column;
    }
    return cell;
}

/// 浮层和它上面那颗按钮之间空多少（px）。和实例页那几块齿轮菜单同一档。
constexpr int kMenuGap = 2;
/// 浮层自己的内边距与行距。**和实例页那几块齿轮菜单同两个数**（`kFolderMenuPadding` /
/// `kFolderMenuRowSpacing`，见 MainWindowUI.cpp 那段说明）—— 同一族浮层留白不一样，用户
/// 一眼就看得出是两个东西。
///
/// NOTE: 这两个数在 MainWindowUI.cpp 里是**文件内**的常量（匿名 namespace），这一层拿不到；
///       抄一份在这儿是这一版最省的做法，但**它们是一份值** —— 那边改了这边要一起改。
constexpr int kMenuPadding = 8;
constexpr int kMenuRowSpacing = 4;

// ---------------------------------------------------------------------------
// 页面按钮的大小与排布（2026-09-28）
// ---------------------------------------------------------------------------
//
// 用户原话：「实例设置里面的所有按钮做成恒定宽度和高度的，每一行三个，间距适中且左右能
// 充分利用」。下面这一小段就是那三件事的唯一出处。
//
/// 那四个数的**壳**：出处是类里那四个 `constexpr` 函数（验证工程也读它们）。
/// 这里转一手只是为了在下面那几个 short 函数里少写一截限定名。
constexpr int kActionButtonsPerRow = InstanceSettingsDialog::actionButtonsPerRow();
constexpr int kActionButtonHeight = InstanceSettingsDialog::actionButtonHeight();
constexpr int kActionButtonSpacing = InstanceSettingsDialog::actionButtonSpacing();
constexpr int kActionButtonMinWidth = InstanceSettingsDialog::actionButtonMinWidth();

/// 一颗按钮按这一套的规格摆好（定高 / 最窄 / 横向可抻）。
///
/// NOTE: **凡是要进下面那张网格的按钮都得先过这一手** —— 少过一手的那颗就会比旁边那两颗
///       矮一头或者窄一块，而"恒定"正是这一套唯一的卖点。
void prepareActionButton(QWidget *button) {
    button->setMinimumWidth(kActionButtonMinWidth);
    button->setFixedHeight(kActionButtonHeight);
    // 横向 Expanding：列有多宽它就有多宽 —— 这才是"恒定宽度"的来源（见下一个函数）。
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

/// 新建一张"每行三颗"的网格（列距 / 行距走 `kActionButtonSpacing`）。
QGridLayout *newActionGrid() {
    auto *layout = new QGridLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setHorizontalSpacing(kActionButtonSpacing);
    layout->setVerticalSpacing(kActionButtonSpacing);
    return layout;
}

/// 把若干**格子**摆进"每行三颗"的网格，三列等宽、整幅铺满。格子自己长什么样不管 ——
/// 可以只是一颗按钮，也可以是「一颗按钮 + 它下面那句解释」（概览上那四颗就是后者）。
///
/// ★ 「恒定宽度」和「左右充分利用」这两件事在这里是**同一条**：宽不钉像素，而是"三列各
///   一份 + 网格铺满它所在那一行的整幅"。于是一组里每颗一样宽，**而且这一页上任何另一组
///   三颗也一样宽**（每一组都跨着同样那幅宽）。钉死一个像素做不到这一点 —— 那样在别的
///   字号 / 别的 DPI 下就不再"充分利用"了，字长一点的那颗还会开始打省略号。
///
/// NOTE: 少一颗的那一行（四颗 = 三颗 + 一颗）**不摊开**：那颗照旧三分之一宽。用户要的是
///       "恒定"，四颗里有一颗比别人宽就不是恒定了。
void layoutActionCells(QGridLayout *grid, const QList<QWidget *> &cells) {
    for (int i = 0; i < cells.size(); ++i) {
        grid->addWidget(cells.at(i), i / kActionButtonsPerRow, i % kActionButtonsPerRow);
    }
    for (int column = 0; column < kActionButtonsPerRow; ++column) {
        grid->setColumnStretch(column, 1);
    }
}

/// 一组裸按钮（格子就是按钮本身）摆成"每行三颗"。
void layoutActionButtons(QGridLayout *grid, const QList<QWidget *> &buttons) {
    for (QWidget *button : buttons) {
        prepareActionButton(button);
    }
    layoutActionCells(grid, buttons);
}

/**
 * @brief 「实例设置」里那两个浮层（更改图标 / 分组…）共用的外壳。
 *
 * 形状照着实例页那几块齿轮菜单（MainWindowUI.cpp 里的 FolderMenu / GroupMenu）：Qt::Popup
 * + 一圈细边 + 比内容区深一档的底色 + Esc 能关。**没抄过来的只有一样**：那两块的"再点同一
 * 颗齿轮收回"靠的是锚点矩形 + 重放压制，而这里的两颗按钮是**真的控件**，不需要那套几何。
 *
 * NOTE: 【为什么要有这个类，而不是直接 new QWidget(…, Qt::Popup)】只有一件事需要拦：
 *       **外面那一下不许重放**。Qt 把一块 popup 关掉时，如果光标不在它的几何里，就会把
 *       同一发 press **重放**给光标底下那个控件 —— 在这一页上那意味着"点一下空白处想收起
 *       浮层，结果顺手按到了底下的东西"，最坏的一发是落在覆盖层左上角那颗**返回箭头**上
 *       （浮层收起来的同时，整个对话框跟着被关掉）。
 *       `WA_NoMouseReplay` 干的正是这件事，但**钉的时机**很刁：Qt 是"关的那一刻"读它，而
 *       这个属性会被 Qt 在"这个控件又收到鼠标事件"时清掉（那条实测经验写在
 *       MainWindowUI.cpp 里 GroupMenu::handleAnchorPress() 上面）—— 所以挂在构造里没用，
 *       得拦在 mousePressEvent 里、落在框外面的那一发上。
 */
class MenuPopup : public QWidget {
public:
    explicit MenuPopup(QWidget *parent) : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint) {
        // 底色比内容区深一档（从调色板推，**不写死颜色**）—— 和那两块菜单逐字同款。
        // 那一档从 2026-09-28 起由 `Theme::partBackground(Part::Panel)` 说了算（这一族浮层
        // 共用一个色），算出来和从前那个 `darker(112)` 一模一样。
        // NOTE: 这块框**每次现建** ⇒ 不用挂 follow()：主题换了之后下一次打开自然就是新的。
        setAutoFillBackground(true);
        QPalette menuPalette = QGuiApplication::palette();
        menuPalette.setColor(QPalette::Window, Theme::partBackground(Theme::Part::Panel));
        setPalette(menuPalette);
    }

    /// 贴着 @p anchor 那一格的**下边缘**弹出来（超出屏幕时往里收）。
    void popUpBelow(const QWidget *anchor) {
        adjustSize();
        QPoint target = anchor->mapToGlobal(QPoint(0, anchor->height() + kMenuGap));
        if (const QScreen *screen = QGuiApplication::screenAt(target)) {
            const QRect available = screen->availableGeometry();
            target.setX(qBound(available.left(), target.x(),
                               qMax(available.left(), available.right() - width() + 1)));
            target.setY(qBound(available.top(), target.y(),
                               qMax(available.top(), available.bottom() - height() + 1)));
        }
        move(target);
        show();
        raise();
        // 拿焦点只为一件事：Esc 能关（点外面那一下 Qt 自己会关）。
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
        if (!rect().contains(event->position().toPoint())) {
            // 落在框外面的那一发：Qt 接下来就会把这块 popup 关掉，而它读 WA_NoMouseReplay
            // 正是在那一刻（理由见类头那段）。**钉在基类之前**。
            setAttribute(Qt::WA_NoMouseReplay, true);
        }
        QWidget::mousePressEvent(event);
    }

    /// 一圈细边（和齿轮菜单那条同一个理由）：同一档深浅在浅色主题下几乎看不出边界，
    /// 分不出"框到哪为止"。
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setPen(palette().color(QPalette::Mid));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }
};

/// 浮层里那一列按钮的外观。**和 GroupMenu::entryStyle() 逐字同形**（字重 / 对齐 / 悬停与
/// 按下那两档底色 / `:enabled` 那一条的理由都写在那边）—— 这里只有"不画红字"这一处差别
/// （这两个浮层里没有能删东西的项）。
QString menuEntryStyle(const QPalette &menuPalette) {
    QColor hover = menuPalette.color(QPalette::Highlight);
    hover.setAlpha(48);
    QColor pressed = menuPalette.color(QPalette::Highlight);
    pressed.setAlpha(96);
    return QStringLiteral("QPushButton { border: none; background: transparent;"
                          " font-weight: bold;"
                          " text-align: left; padding: 6px 6px; }"
                          "QPushButton:hover:enabled { background-color: %1; }"
                          "QPushButton:pressed:enabled { background-color: %2; }")
        .arg(hover.name(QColor::HexArgb), pressed.name(QColor::HexArgb));
}

/// 「文件管理」里哪几页有「启用 / 禁用」。
///
/// **只有游戏会自己扫的那三个目录**：模组（`mods/*.jar`）、资源包、光影包。另外三页给
/// 了也没用 —— 数据包要自己搬进存档，原理图要 Litematica 主动去读，存档干脆不是一个文件；
/// 给它们一颗"禁用"只会让用户以为改名就等于不生效了。
bool fileKeyCanToggle(const QString &key) {
    return key == QStringLiteral("mod") || key == QStringLiteral("resourcepack") ||
           key == QStringLiteral("shader");
}

}  // namespace

InstanceSettingsDialog::InstanceSettingsDialog(const QString &instanceName,
                                               const QString &instanceId,
                                               const Override &current,
                                               const GlobalValues &global,
                                               const QStringList &appendJvmArgs,
                                               bool warnJavaMismatch,
                                               const QList<WindowModeChoice> &windowModes,
                                               QWidget *parent)
    : QDialog(parent), m_global(global), m_instanceId(instanceId) {
    setWindowTitle(tr("实例设置"));
    // ★ 2026-09-29（用户原话见 .hpp 里 dragEnterEvent() 那段）：这一页接拖放。
    //   `setAcceptDrops(true)` 只影响**这一层自己**；拖到子控件上的事件由 Qt 沿父链往上
    //   找第一个接了拖放的控件（见那段 NOTE），所以不接拖放的子控件照样会把事件送到这儿。
    setAcceptDrops(true);
    // 比分组对话框宽得多：从左到右是**两栏**（左栏一列页 + 右栏一块内容），600 那个老宽度
    // 放不下。
    // NOTE: 嵌进覆盖层的时候这个尺寸**不作数** —— 那一层给它的是内容区的整幅宽（见
    //       OverlayHost::execEmbedded 里那句 `area->layout()->addWidget(dialog)`）。
    //       这一句只影响单独把它拿出来用的时候（验证工程那几处）。
    resize(kDialogWidth, kDialogHeight);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 12, 12, 12);
    outer->setSpacing(10);

    auto *title = new QLabel(titleTextFor(instanceName), this);
    m_titleLabel = title;
    title->setObjectName(titleLabelName());
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    outer->addWidget(title);

    // ---------- 两栏（见 .hpp 里 pinnedPageKeys() 上面那一段）----------
    auto *columns = new QHBoxLayout;
    columns->setSpacing(kColumnSpacing);

    // 左栏那一列：**内容不在这儿填** —— 它是按这个实例算出来的（哪几页用得上 + 用户勾掉了
    // 哪几页），构造末尾的 rebuildEntryList() 才灌第一遍。这儿只把"长什么样"摆好。
    //
    // NOTE: **不设** rowRefreshRole()。那一格里画的刷新圈是下载页专有的（这一列没有
    //       "重新拉一遍"这回事），代理见到没设就不画。
    m_pageList = new QListWidget(this);
    m_pageList->setObjectName(pageListName());
    m_pageList->setUniformItemSizes(true);
    m_pageList->setItemDelegate(new VersionTreeDelegate(m_pageList));
    // 悬停底色由代理画（`Theme::rowHoverFill()`），但那一位 `State_MouseOver` 是**视图**填的。
    // ★ 少了这一句，代理里那段一个字都不会生效 —— 不报错、断言也不一定先红，屏幕上就是"悬停没反应"。
    m_pageList->setMouseTracking(true);
    QFont listFont = m_pageList->font();
    listFont.setPointSize(listFont.pointSize() + MainWindowUI::versionTreeFontStep());
    m_pageList->setFont(listFont);
    // 只为一件事：**别让"拿到焦点"变成一次翻页**（见 eventFilter() 上面那一整段）。
    m_pageList->installEventFilter(this);
    addNavColumn(columns, m_pageList);

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(stackName());
    columns->addWidget(m_stack, 1);
    // ★ 换页那一下（用户 2026-09-29：「除了切换页面，点击左边栏导致右边栏发生变化的时候也要
    //   加动画」）：这一页的左栏是**每个实例各一张的页表**（不是顶上那四颗选项卡），而它
    //   换页只走 `applyPageKey()` 这一条路 —— 挂在这儿等于一处管住左栏点击（`applyEntry`）
    //   与 `showPage()` 两边。别处那三层右栏是 `MainWindowUI` 接的（那儿够不着这一个对话框：
    //   它是每次开页时现建的，活在栈上）。
    // NOTE: 顶上的四颗选项卡、下载页 / 设置页那几层走的是 `MainWindowUI` 里同一条形状的
    //       接线 —— 形状一样是故意的，但**不是同一个 `connect`**（那几个 `QStackedWidget`
    //       在那边、这个在这一层）。
    // ★ 垫的底色是 `QPalette::Base`：右栏每一页都活在一层滚动区里（见 `addPage()`），
    //   视口铺的就是它。垫窗口那份的话，干等那 70 ms 会看见一整块偏灰的方块。
    connect(m_stack, &QStackedWidget::currentChanged, this, [this](int) {
        QWidget *page = m_stack->currentWidget();
        if (page != nullptr && page->isVisible()) {
            Anim::pageAppear(page, page->palette().color(QPalette::Base));
        }
    });
    // 两栏吃掉整页剩下的竖直空间（底下那排按钮按自己的高排）。
    outer->addLayout(columns, 1);

    // ---------- 概览（2026-09-26 重排成四行）----------
    //
    // 用户原话（逐字）：「实例设置 概览里 / 第一行 图标 名称（可编辑文本）/ 第二行 更改图标
    // （按钮）分组...（按钮）/ 第三行 显示在置顶组中（勾选框，就是在被设为置顶组的那个组里
    // 再放一个同名的，下面给到注释提醒）/ 第四行 导出（还没做，可行的话顺便做了，麻烦的话
    // 先放着）」。
    //
    // 于是这一栏**没有"标题在左、内容在右"的行名了**：四行分别是名称输入框、两颗按钮、
    // 勾选框上的字、第四行那颗占位按钮 —— 名字都在控件自己身上。这也是这一栏和别的那几栏
    // 长得不一样的原因，是刻意的。
    //
    // NOTE: `grid` 与 `row` 在这几栏之间**是复用的**：换一栏就是"换一份表格、行号从 0 数"。
    //       每一栏各自一张表格（各自的 objectName），互不干扰 —— 只是在同一个函数里搭，
    //       没必要给每一栏各起一个名字。
    QGridLayout *grid = addForm(overviewCategoryKey(), addPage(overviewCategoryKey()));
    int row = 0;

    // ---------- 第一行：图标预览 + 实例名 ----------
    //
    // 名字（= 改名）2026-09-25 从实例页右栏那颗「重命名…」按钮搬进来，2026-09-26 和图标
    // 并排摆成一行。它不是一项"覆盖"：别的几行都有「跟随全局 / 自定义」那对单选钮（那些是
    // 启动参数，全局有一份、实例可以盖过它），而这个是这个实例自己的身份，没有"全局"可比
    // —— 所以它不走 addOverrideRow()。
    //
    // 真的动手改名的是 MainWindow（先改名、再写设置），这一层只负责让用户改到那个格子
    // （见 instanceName()）。
    {
        auto *cell = new QWidget(this);
        auto *line = new QHBoxLayout(cell);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(12);

        // 预览：这张图就是"换完图标之后树上会长什么样"。定死边长，换图时整行不跟着跳。
        m_iconPreview = new QLabel(cell);
        m_iconPreview->setObjectName(iconPreviewName());
        m_iconPreview->setFixedSize(kIconPreviewSize, kIconPreviewSize);
        m_iconPreview->setAlignment(Qt::AlignCenter);
        // 顶部对齐：右边那一列（输入框 + 一句解释）多半比 64 px 高，预览图不该被拉到中间。
        line->addWidget(m_iconPreview, 0, Qt::AlignTop);

        auto *nameColumn = new QVBoxLayout;
        nameColumn->setContentsMargins(0, 0, 0, 0);
        nameColumn->setSpacing(4);
        m_instanceNameEdit = new QLineEdit(instanceId, cell);
        m_instanceNameEdit->setObjectName(instanceNameEditName());
        // 初值是**目录名**（不是页眉上那个显示名）—— 见构造函数的 @param instanceId。
        // 这一格现在**没有行名**了，所以那句占位文字就是它唯一的名字。
        m_instanceNameEdit->setPlaceholderText(instanceNameText());
        nameColumn->addWidget(m_instanceNameEdit);
        auto *nameHint = new QLabel(instanceNameHint(), cell);
        nameHint->setWordWrap(true);
        nameColumn->addWidget(nameHint);
        line->addLayout(nameColumn, 1);

        grid->addWidget(cell, row++, 0, 1, 2);
    }

    // ---------- 第二行：两颗按钮（更改图标 / 分组…），各开一个浮层 ----------
    //
    // 用户原话：「第二行 更改图标（按钮） 分组...（按钮）」，当场问过之后两个都选了
    // **"点了弹浮层"**（而不是把候选项铺在这一页上）。
    {
        auto *cell = new QWidget(this);
        auto *line = new QVBoxLayout(cell);
        line->setContentsMargins(0, 0, 0, 0);
        m_changeIconButton = new QPushButton(changeIconText(), cell);
        m_changeIconButton->setObjectName(changeIconButtonName());
        // 「更换图标…」「移入分组…」「导出…」都是**打开一个菜单或另一个框**（一般性）。
        Theme::markButton(m_changeIconButton, Theme::ButtonKind::Neutral);
        m_changeIconButton->setToolTip(iconHint());
        connect(m_changeIconButton, &QPushButton::clicked, this,
                [this]() { popUpMenuBelow(m_iconMenu, m_changeIconButton); });

        m_groupButton = new QPushButton(groupButtonText(), cell);
        m_groupButton->setObjectName(groupButtonName());
        Theme::markButton(m_groupButton, Theme::ButtonKind::Neutral);
        // tooltip 由 refreshGroupMenuState() 写（它要说清"现在归在哪一组"）。
        connect(m_groupButton, &QPushButton::clicked, this,
                [this]() { popUpMenuBelow(m_groupMenu, m_groupButton); });

        // ★ 2026-09-28：两颗并排 → **铺成"每行三颗"那一套里的头两颗**（用户原话「所有按钮
        //   做成恒定宽度和高度的，每一行三个」）。第三格空着，这两颗照旧三分之一宽
        //   —— "恒定"的意思是这一页上所有按钮一样大，不是"这一行要把三格填满"。
        auto *iconGrid = newActionGrid();
        layoutActionButtons(iconGrid, {m_changeIconButton, m_groupButton});
        line->addLayout(iconGrid);
        grid->addWidget(cell, row++, 0, 1, 2);
    }

    // ---------- 「更改图标」那个浮层 ----------
    //
    // 用户原话：「图标（选一张本地图片文件）」「使用实际的那几个 mc 草块 / fabric 布料等的
    // 图片文件，不用担心侵权问题，同时允许用户自主上传」。所以十档：默认 + 八个内置候选
    // + 本地那张。**每一档都有对应的按钮**（八个候选各一颗、「恢复默认」、「选一张图片…」），
    // 没有"换过去就回不来"的死角。
    //
    // NOTE: 那一排按钮**不是**当前值的出处（它们只是 m_iconChoice 的一幅画）—— 理由见 .hpp
    //       里 setIcon() 上面那一段：本地那一档没有控件能表示它。
    //
    // NOTE: 【浮层是**常驻**的 —— 一次建好、以后每次点按钮只是再弹一遍】
    //       三件事都指望这个：①验证工程按 objectName 找得到里面那几颗按钮（**没弹出来的
    //       时候也要能找**，那些断言关心的是"按了会变成什么"，不是"它当时开没开"）；
    //       ②每点一次就新建一个的话，连点两次就会同时存在两个同名浮层，按名字找控件的
    //       那一套当场失去意义；③refreshIconRow() 在关着的时候也得把勾选状态摆对（否则
    //       弹出来那一瞬间画的是上一次的样子）。
    //       **判"此刻开着没开着"的判据是 isVisible()**，不是它存不存在。
    //       同一段理由对整个这一页的两个浮层都成立。
    {
        auto *menu = new MenuPopup(this);
        m_iconMenu = menu;
        m_iconMenu->setObjectName(iconMenuName());
        auto *box = new QVBoxLayout(menu);
        box->setContentsMargins(kMenuPadding, kMenuPadding, kMenuPadding, kMenuPadding);
        box->setSpacing(kMenuRowSpacing);

        auto *candidates = new QGridLayout;
        candidates->setSpacing(4);
        const QStringList keys = VersionIcons::iconCandidateKeys();
        const QStringList names = iconCandidateTexts();
        for (int i = 0; i < keys.size(); ++i) {
            auto *button = new QToolButton(menu);
            button->setObjectName(iconCandidateButtonName(i));
            button->setText(names.value(i));
            // 图在上、字在下：这一排是"挑哪一张"，光看图认不出 Fabric 和 NeoForge。
            button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            button->setIconSize(QSize(kIconCandidateSize, kIconCandidateSize));
            // ★ 2026-09-27：**这张图从前没人画**（`setIconSize` 摆了尺度、`TextUnderIcon`
            //   摆了位置，偏偏没 `setIcon`）—— 八颗按钮全是光秃秃一个词，用户看着「草方块 /
            //   覆雪草块 / 土块 / 幸运方块」选图，而上面那一格预览的是**另一张**（默认图标）。
            //   这一格的全部意义就是"挑哪一张"，看不见图等于没得挑。
            //   图由 VersionIcons 按同一份候选表解出来（键和画法都只有那一处），这里只管贴。
            button->setIcon(QIcon(VersionIcons::iconCandidateMark(i, kIconCandidateSize)));
            button->setCheckable(true);
            // 和设置页那些"点一下换个样子"的按钮同一条：不该抢键盘焦点（抢了之后
            // 键盘上那几下就不再是"在这张浮层上操作"了）。
            button->setFocusPolicy(Qt::NoFocus);
            const QString key = keys.at(i);
            connect(button, &QToolButton::clicked, this, [this, key]() {
                m_iconChoice = VersionIcons::iconValueForKey(key);
                // 换了内置候选 = 用户不要刚才挑的那张本地图了。**不删盘上那份**：
                // 它现在还在原处，而实例目录里那份要等这一次改动落到盘上才被换掉
                // （见 pickedIconPath()）。
                m_pickedIconPath.clear();
                refreshIconRow();
                // 挑完就收：这颗按钮是"选一个"，不是"切换一个开关"。留着框开着挡住的
                // 正是用户接下来要看的地方（那一行里的预览）。
                m_iconMenu->close();
            });
            m_iconButtons.append(button);
            candidates->addWidget(button, i / kIconCandidatesPerRow, i % kIconCandidatesPerRow);
        }
        box->addLayout(candidates);

        auto *actions = new QHBoxLayout;
        actions->setSpacing(8);
        auto *pick = new QPushButton(iconPickText(), menu);
        pick->setObjectName(iconPickButtonName());
        Theme::markButton(pick, Theme::ButtonKind::Neutral);
        connect(pick, &QPushButton::clicked, this, [this]() {
            // 模态，验证工程驱动不了它 —— 和「浏览…」选 java.exe 同一个道理（开的是
            // Windows 自己的文件选择框，不是我们的窗）。可测的那一半是"挑完之后
            // iconChoice() / pickedIconPath() 变成什么"，那两问都是现场读成员、不碰盘。
            const QString picked = QFileDialog::getOpenFileName(
                this, iconPickText(), pickedIconPath(),
                tr("图片 (*.png *.jpg *.jpeg *.bmp *.gif *.webp);;所有文件 (*)"));
            if (picked.isEmpty()) {
                return;  // 取消：一个字都不改（当前那一档原样留着）
            }
            m_iconChoice = localIconValue();
            m_pickedIconPath = picked;
            refreshIconRow();
            // 这一发 `clicked` **在框弹出来之前**就发完了（用户挑完图是那之后的事）
            // ⇒ 改完自己补一发（见 .hpp 里 watchEditors() 那段 NOTE 点的第二处）。
            notifyEdited();
            m_iconMenu->close();
        });
        actions->addWidget(pick);
        auto *reset = new QPushButton(iconResetText(), menu);
        reset->setObjectName(iconResetButtonName());
        Theme::markButton(reset, Theme::ButtonKind::Neutral);
        connect(reset, &QPushButton::clicked, this, [this]() {
            // 「默认」= **这个键不存在**（而不是"写一个等于默认的值"）—— 全项目同一个约定，
            // 见 VersionManager::updateVersionMeta() 里那段关于 QJsonValue::Null 的说明。
            // 存成"等于默认"的话，用户之后换了版本分类（比如从原版换成一个 Fabric 实例），
            // 这个实例就不会跟着换标志了，而他要的明明是"默认"。
            m_iconChoice.clear();
            m_pickedIconPath.clear();
            refreshIconRow();
            m_iconMenu->close();
        });
        actions->addWidget(reset);
        actions->addStretch(1);
        box->addLayout(actions);
    }

    // ---------- 「分组…」那个浮层 ----------
    //
    // 候选项由调用方灌（setGroupChoices）—— 组名是用户可以改的、id 是稳定身份，这一页
    // 只负责显示。第一项「未分组」由这里自己加（它是保留值，不是某一组）。
    // 浮层里那几行**是每次 setGroupChoices() 的时候重建的**（行数会变），理由写在那个
    // 函数里。
    {
        auto *menu = new MenuPopup(this);
        m_groupMenu = menu;
        m_groupMenu->setObjectName(groupMenuName());
        auto *box = new QVBoxLayout(menu);
        box->setContentsMargins(kMenuPadding, kMenuPadding, kMenuPadding, kMenuPadding);
        box->setSpacing(kMenuRowSpacing);
    }

    // ---------- 第三行：显示在置顶组中（勾选框 + 注释）----------
    //
    // 用户原话：「第三行 显示在置顶组中（勾选框，**就是在被设为置顶组的那个组里再放一个
    // 同名的**，下面给到注释提醒）」。那句解释必须留着 —— 他特意点名的就是"下面给到注释"。
    {
        auto *cell = new QWidget(this);
        auto *column = new QVBoxLayout(cell);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(4);
        m_pinCheck = new QCheckBox(pinCheckText(), cell);
        m_pinCheck->setObjectName(pinCheckName());
        // **没有 toggled 那条接线了**（原来接 applyPinState()：勾上就把「分组」那一格挪到
        // 置顶组上并禁用）。旧的语义是"置顶 = 搬进置顶组"，两边都显示之后勾选框和「分组」
        // 那一格说的**是两件互不相干的事**（一个管"要不要在置顶组里多摆一条"，一个管
        // "老家在哪"）—— 它们之间没有任何要同步的东西。那一整段搬走的代码见 .hpp 里
        // setPinned() 那段。
        column->addWidget(m_pinCheck);
        auto *hint = new QLabel(pinHint(), cell);
        hint->setWordWrap(true);
        column->addWidget(hint);
        grid->addWidget(cell, row++, 0, 1, 2);
    }

    // ---------- 四颗「对整个实例动手」的按钮（2026-09-28 改成一张三列网格）----------
    //
    // 导出整合包 / 检查更新 / 复制实例 / 删除实例 —— 原来这四颗**各占一整行**（一颗按钮 +
    // 它自己那句解释，一条一条摞下来）。用户 2026-09-28 原话「实例设置里面的所有按钮做成
    // 恒定宽度和高度的，每一行三个，间距适中且左右能充分利用」，于是它们收成一张网格：
    // 前三颗一行、第四颗单独一行，每格还是"按钮 + 它那句解释"（**解释一个字都没丢**，
    // 只是从整幅宽缩成三分之一宽，靠 `setWordWrap(true)` 自己折行）。
    //
    // NOTE: 四颗一行摆三颗 ⇒ 最后那一行只填得满第一格。**空着的那两格不摊开**是写意的
    //       （见 layoutActionCells() 里那段）：四颗里有一颗比别人宽就不是"恒定"了。
    auto *actionGrid = newActionGrid();
    QList<QWidget *> actionCells;
    {
        auto *cell = new QWidget(this);
        auto *column = new QVBoxLayout(cell);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(4);
        auto *button = new QPushButton(exportText(), cell);
        button->setObjectName(exportButtonName());
        Theme::markButton(button, Theme::ButtonKind::Neutral);
        button->setToolTip(exportButtonTooltip());
        connect(button, &QPushButton::clicked, this, [this]() {
            // 默认文件名用**这个实例的目录名**（同「导出启动脚本」那一段的理由：它一定
            // 是合法文件名）。扩展名给 `.mrpack`：两个筛选项里 Modrinth 那一份是默认，
            // 而 Windows 的保存框会按**当前选中的筛选项**补扩展名。
            QString dir = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
            if (dir.isEmpty()) {
                dir = QDir::homePath();
            }
            const QString suggested =
                dir + QLatin1Char('/') + m_instanceId + QStringLiteral(".mrpack");
            // 开的是 **Windows 自己的保存框**（和「导出启动脚本」「浏览…选 java.exe」
            // 同一档：全项目就剩这几处独立窗）。那个框上的「取消」= 什么都不做。
            //
            // NOTE: 两个筛选项的顺序**就是默认格式的顺序**（保存框默认选第一个），所以
            //       Modrinth 那一份排在前面 —— 它不需要 API key、也不受"作者关了第三方
            //       分发"影响，是能完整还原的那一份。
            const QString modrinthFilter = tr("Modrinth 整合包 (*.mrpack)");
            const QString curseForgeFilter = tr("CurseForge 整合包 (*.zip)");
            QString selectedFilter = modrinthFilter;
            QString picked = QFileDialog::getSaveFileName(
                this, exportText(), suggested,
                modrinthFilter + QStringLiteral(";;") + curseForgeFilter, &selectedFilter);
            if (picked.isEmpty()) {
                return;
            }
            // 格式这一件事**只认扩展名**（见 .hpp 里那个信号的 NOTE）。用户自己敲一个
            // `我的包` 时保存框不一定补扩展名（补不补是那个框的事，我们管不着）——
            // 那就按他选中的筛选项补一个，别让盘上出现一个叫 `我的包`、内容是 Modrinth
            // 清单的文件：名字和正文对不上，正是这一整套里最容易悄悄发生的一种错。
            const QString lower = picked.toLower();
            if (!lower.endsWith(QStringLiteral(".mrpack")) &&
                !lower.endsWith(QStringLiteral(".zip"))) {
                picked += selectedFilter == curseForgeFilter ? QStringLiteral(".zip")
                                                             : QStringLiteral(".mrpack");
            }
            emit exportModpackRequested(picked);
        });
        prepareActionButton(button);
        column->addWidget(button);
        auto *hint = new QLabel(exportHint(), cell);
        hint->setWordWrap(true);
        column->addWidget(hint);
        actionCells.append(cell);
    }

    // ---------- 「检查更新」（2026-09-28）----------
    //
    // 用户原话：「实例设置那里做一个更新检测」。摆在「导出…」下面、快捷方式那排上面：
    // 它和「导出…」是同一档东西（一颗按钮 + 底下那句解释，点了之后**会出现一个结果**，
    // 而不是改一格设置就写盘）。
    //
    // NOTE: 这一颗**只发信号**（和导出那颗同一个形状）—— "盘上那几十个 jar 是谁、能不能
    //       更新"这件事要联好几次网（反查 + 每个项目一发版本列表），全在 MainWindow 那边
    //       （见 `checkInstanceUpdates()`）。这一层连一个请求都不发，所以验证工程能单独
    //       构造它、单独点它。
    {
        auto *cell = new QWidget(this);
        auto *column = new QVBoxLayout(cell);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(4);
        auto *button = new QPushButton(checkUpdatesText(), cell);
        button->setObjectName(checkUpdatesButtonName());
        button->setToolTip(checkUpdatesButtonTooltip());
        connect(button, &QPushButton::clicked, this,
                [this]() { emit checkUpdatesRequested(); });
        prepareActionButton(button);
        column->addWidget(button);
        auto *hint = new QLabel(checkUpdatesHint(), cell);
        hint->setWordWrap(true);
        column->addWidget(hint);
        actionCells.append(cell);
    }

    // ---------- 「复制实例」（2026-09-28）----------
    //
    // 用户原话：「实例设置的概览里面加一条复制实例，点击以后让用户选择复制到的文件夹，
    // 然后给定复制后的实例名称」。摆在「检查更新」下面 —— 它和上面两颗是同一档（一颗
    // 按钮 + 底下那句解释），而这一颗的落点比上面那两颗重（会多出一整个实例目录）。
    //
    // NOTE: 这一颗**只发信号**（和「导出…」「检查更新」同一个形状）：挑哪个游戏文件夹、
    //       问新名字、跑真那份拷贝，全在 MainWindow —— 那几步要 AppPaths（目标在**另一个**
    //       游戏文件夹里，见 VersionManager::copyInstance）和 InlineMessage 的"问一句名字"
    //       （都要覆盖层），这一层两样都没有。
    {
        auto *cell = new QWidget(this);
        auto *column = new QVBoxLayout(cell);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(4);
        auto *button = new QPushButton(copyText(), cell);
        button->setObjectName(copyButtonName());
        button->setToolTip(copyButtonTooltip());
        connect(button, &QPushButton::clicked, this,
                [this]() { emit copyInstanceRequested(); });
        prepareActionButton(button);
        column->addWidget(button);
        auto *hint = new QLabel(copyHint(), cell);
        hint->setWordWrap(true);
        column->addWidget(hint);
        actionCells.append(cell);
    }

    // ---------- 「删除实例」（2026-09-28）----------
    //
    // 用户原话：「顺便把删除实例功能做了，按钮放在概览里」。摆在「复制实例」下面 ——
    // 那两颗是这一页上仅有的两个"对整个实例动手"的动作，一个多出一份、一个少掉一份。
    //
    // 见 .hpp 那段：删除这件事**本来就有**（实例行行尾那颗垃圾桶），这一颗是它的第二个
    // 入口 —— 真去删的那条路（两道闸 + 问一句 + 删目录）一行都没重写。
    //
    // ★ 红的（`ButtonKind::Danger`）：用户点名这一档叫「不可撤销（如修改存档，删除实例）」，
    //   而这一页上它是唯一一颗真会丢东西的按钮。紧挨着它的「复制实例」是绿的 —— 同一个
    //   位置上一红一绿，正好是"这两个不一样"最省字的说法。
    {
        auto *cell = new QWidget(this);
        auto *column = new QVBoxLayout(cell);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(4);
        auto *button = new QPushButton(deleteText(), cell);
        button->setObjectName(deleteButtonName());
        button->setToolTip(deleteButtonTooltip());
        Theme::markButton(button, Theme::ButtonKind::Danger);
        connect(button, &QPushButton::clicked, this,
                [this]() { emit deleteInstanceRequested(); });
        prepareActionButton(button);
        column->addWidget(button);
        auto *hint = new QLabel(deleteHint(), cell);
        hint->setWordWrap(true);
        column->addWidget(hint);
        actionCells.append(cell);
    }

    // 四格攒齐了，一次性摆成"每行三颗"。**必须摆在这一段之后**：那四格是在上面四个
    // 作用域里各自建出来的，layoutActionCells() 要的是完整的一份。
    layoutActionCells(actionGrid, actionCells);
    grid->addLayout(actionGrid, row++, 0, 1, 2);

    // ---------- 快捷方式那九颗按钮（2026-09-27 从「快捷方式」那一页整块搬来）----------
    //
    // 用户原话：「"快捷方式"放到概览的下方」—— 从这儿起「快捷方式」**不再是一页**，
    // 这九颗按钮就是概览页的一部分（键、objectName、底下那句话一个字都没改，只是换了个父）。
    {
        auto *cell = new QWidget(this);
        auto *column = new QVBoxLayout(cell);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(6);

        // ★ 2026-09-28：这九颗**从"默认尺寸靠左堆"改成这一页统一的那一套**（用户原话
        //   「所有按钮做成恒定宽度和高度的，每一行三个，间距适中且左右能充分利用」）——
        //   每颗和「导出…」那几颗一样大，三列铺满整幅。
        //
        // NOTE: `shortcutPerRow()`（9 颗排几列）和这一套的 `kActionButtonsPerRow` **必须
        //       是同一个数**（眼下都是 3）。排布改由 `layoutActionButtons()` 按后者算，
        //       前者只剩"这颗按钮是哪一路目录"那一层意思；两边哪天分叉了，九颗会排成
        //       六列或者三条 —— 界面上一眼看得出来，但没有任何断言拦得住。
        auto *buttonGrid = newActionGrid();
        const QStringList texts = shortcutTexts();
        const QStringList keys = shortcutPathKeys();
        QList<QWidget *> shortcutWidgets;
        for (int i = 0; i < texts.size(); ++i) {
            auto *button = new QPushButton(texts.value(i), cell);
            // 快捷方式那九颗 = "开一个目录"（一般性）。
            Theme::markButton(button, Theme::ButtonKind::Neutral);
            button->setObjectName(shortcutButtonName(i));
            const QString key = keys.value(i);
            // 路径**现场问 m_instanceDir / m_fileDirs**（不是捕获一份快照）：setInstanceDir()
            // 是在构造函数之后才被调的，捕获快照的话拿到的永远是空串 —— 这颗按钮点了什么都
            // 不会发生，而且不报错。
            connect(button, &QPushButton::clicked, this, [this, key] {
                emit openInstanceFolderRequested(
                    key == QStringLiteral("dir") ? m_instanceDir : m_fileDirs.value(key));
            });
            shortcutWidgets.append(button);
            m_shortcutButtons.append(button);
        }
        layoutActionButtons(buttonGrid, shortcutWidgets);
        column->addLayout(buttonGrid);
        auto *hint = new QLabel(filesHint(QStringLiteral("shortcut")), cell);
        hint->setObjectName(filesHintName(QStringLiteral("shortcut")));
        hint->setWordWrap(true);
        column->addWidget(hint);
        grid->addWidget(cell, row++, 0, 1, 2);
    }

    // ---------- 「显示哪些页」（2026-09-27）----------
    //
    // 用户原话：「概览里面调整是否显示」「是否显示的调整放到"快捷方式"的下方」「如果没有
    // 对应的加载器，保持对应选项不可勾选的状态，悬停文本解释」。
    //
    // 七条一条一个勾选框（顺序见 optionalPageKeys()），勾/不勾直接决定左栏那一列里有没有
    // 这一条；勾掉的记在实例自己的 wtfml.meta 里（见 pageChoices()）。**不可用的那一条
    // 不勾、灰着、tooltip 说为什么** —— 灰着而不是藏起来：用户得看得见"有这么一页、
    // 但这个实例上用不上、为什么"。
    //
    // NOTE: 这些勾选框会被 watchEditors() 按类型自动接上（它接 `QAbstractButton::clicked`）
    //       ⇒ 勾一下就走"改了就写"那条路。程序化 `setChecked()`（refreshPageChecks()）
    //       **不发 clicked**，所以重建/灌值不会白白写一次盘。
    {
        auto *cell = new QWidget(this);
        auto *column = new QVBoxLayout(cell);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(6);

        auto *label = new QLabel(showPagesLabelText(), cell);
        QFont labelFont = label->font();
        labelFont.setBold(true);
        label->setFont(labelFont);
        column->addWidget(label);

        auto *checkGrid = new QGridLayout;
        checkGrid->setContentsMargins(0, 0, 0, 0);
        checkGrid->setHorizontalSpacing(8);
        checkGrid->setVerticalSpacing(6);
        const QStringList keys = optionalPageKeys();
        const QStringList texts = optionalPageTexts();
        for (int i = 0; i < keys.size(); ++i) {
            const QString key = keys.at(i);
            auto *check = new QCheckBox(texts.value(i), cell);
            check->setObjectName(pageCheckName(key));
            // 一开始全是勾上的：**可用就显示**是默认（见 .hpp 里 pageChoices() 那条）。
            // 真正的样子由 refreshPageChecks() 摆（构造末尾、以及每一次可用性变化之后）。
            check->setChecked(true);
            m_pageChecks.insert(key, check);
            // 两条一排。
            // 勾一下：先把这一格的样子**记进那一份选择**，再让左栏跟着变。
            //
            // ★ 少了这一发，用户勾掉「存档」之后**左栏里那一条还在** —— `shownEntryKeys()`
            //   读的是那一份选择（`m_pageOverrides`，勾选框只是它的画），而用户点的这一下
            //   原来只走"写盘那条线"（watchEditors() → settingsEdited），要等 MainWindow
            //   把那一次改动写进 wtfml.meta、再灌回来，左栏才掉。用户眼前就是一颗已经空了
            //   的勾选框配着一条还在的左栏，中间隔着一次写盘。「记到盘上」和「当场看见」
            //   是两件事，两条都得接。
            //
            // NOTE: 顺序 —— 这一发接在 watchEditors() 那一发**之前**（构造函数先跑），
            //       所以 MainWindow 收到 settingsEdited 去读 pageChoices() 时，左栏与那一份
            //       选择都已经是最新的。
            connect(check, &QAbstractButton::clicked, this, [this, key](bool checked) {
                m_pageOverrides.insert(key, checked);
                rebuildEntryList();
            });
            checkGrid->addWidget(check, i / 2, i % 2, Qt::AlignLeft);
        }
        column->addLayout(checkGrid);
        grid->addWidget(cell, row++, 0, 1, 2);
    }

    // ---------- 启动（Java 选项 / 内存分配 / 高级选项）----------
    grid = addForm(launchCategoryKey(), addPage(launchCategoryKey()));
    row = 0;

    // 这一句原来挂在整页顶上，2026-09-26 分栏时跟着它解释的那五项一起搬进「启动」栏 ——
    // 「跟随全局 / 自定义」是**这几项**的属性，摆在整页顶上会让「文件管理」那几栏也像有
    // 这么一对档位。
    auto *explain = new QLabel(
        tr("每一项都可以「跟随全局」或「自定义」。「跟随全局」的项目在你改全局设置时跟着变，"
           "「自定义」的项目只对这个实例生效。这些都写在这个实例目录下的 wtfml.meta 里。"),
        this);
    explain->setWordWrap(true);
    grid->addWidget(explain, row++, 0, 1, 2);

    // ---------- Java 路径 ----------
    {
        QWidget *cell = addOverrideRow(grid, row, javaPathField(), tr("Java 路径"),
                                       tr("只对这个实例生效。留空 = 这个实例不指定路径"
                                          "（跟着全局那份走）。"));
        row += 2;
        auto *choiceRow = choiceRowOf(javaPathField());
        m_javaPathEdit = new QLineEdit(cell);
        m_javaPathEdit->setObjectName(javaPathEditName());
        m_javaPathEdit->setPlaceholderText(tr("java.exe 的完整路径"));

        auto *browse = new QPushButton(tr("浏览…"), cell);
        Theme::markButton(browse, Theme::ButtonKind::Neutral);
        browse->setObjectName(browseJavaButtonName());
        connect(browse, &QPushButton::clicked, this, [this]() {
            // 模态，验证工程驱动不了它 —— 那一半是"问路径"，可测的那一半是这个框里的
            // 文字（override() 直接读它）。和 GroupDialog 里问组名的拆法一样。
            // NOTE: "驱动不了"的原因和内嵌提示那几条不一样：这里开的是 **Windows 自己的
            //       文件选择框**，不是我们的窗。**全项目一共 16 处**（2026-10-11 数的）：
            //       MainWindow.cpp 十一处（选文件夹 / 选整合包 / 挑各处的目录与文件）、
            //       AccountSkinDialog.cpp 一处（选皮肤图）、这个文件四处（选 java.exe / 选实例图标 /
            //       导出整合包的保存框 / 导出 .bat 的保存框）。理由和取舍见 MainWindow.cpp 里
            //       onAddFolderClicked() 上面那一段。
            const QString picked = QFileDialog::getOpenFileName(
                this, tr("选择 java.exe"), m_javaPathEdit->text(),
                tr("Java 可执行文件 (java.exe);;所有文件 (*)"));
            if (!picked.isEmpty()) {
                m_javaPathEdit->setText(picked);
                // 选了一个具体文件 = 用户明确要这个，顺手把这一行切成「自定义」。
                // 不切的话他选了路径、这一行还在"跟随全局"，这一次选择等于什么都没发生。
                setChoiceCustom(javaPathField(), true);
                // ★ 这两句都是**程序化**改值（`setText` 不发 editingFinished，
                //   `setChoiceCustom` 自己挡着信号）⇒ watchEditors() 那几条按类型的线一条
                //   都不响。改完自己补一发（见 .hpp 里 watchEditors() 那段 NOTE 里点名的
                //   两处，这是第一处）。
                notifyEdited();
            }
        });
        // ★ 2026-09-28：框 + 「浏览…」也是那一套三列 —— 输入框跨前两列、按钮占第三列。
        //   于是这颗按钮和这一页上别的按钮**一样高**，宽度也按同一份比例分（见
        //   layoutActionCells() 那段：宽是"列数"算出来的，不是钉死的像素）。
        //
        // NOTE: 这颗按钮**贴着它那个框**摆、不挪到别处 —— 它改的就是这个框里的字。
        //       全项目唯一一颗这样的按钮，所以它是这一套里唯一不能独自成行的。
        auto *pathGrid = newActionGrid();
        pathGrid->addWidget(m_javaPathEdit, 0, 0, 1, 2);
        prepareActionButton(browse);
        pathGrid->addWidget(browse, 0, 2);
        for (int column = 0; column < kActionButtonsPerRow; ++column) {
            pathGrid->setColumnStretch(column, 1);
        }
        choiceRow->addLayout(pathGrid, 1);
    }

    // ---------- 内存：最大 / 最小 / 分配方式 ----------
    {
        QWidget *cell = addOverrideRow(grid, row, xmxField(), tr("最大内存"),
                                       tr("只对这个实例生效（给 JVM 的 -Xmx）。"));
        row += 2;
        m_xmxFollowCell = cell;
        auto *choiceRow = choiceRowOf(xmxField());
        // 这一格和设置页那一行一样：**一根条 + 一个数字框**（用户要 PCL 那种拖着改的手感）。
        // 条占满剩下的宽度，数字框窄窄一条 —— 顺序也是这个，条在左边。
        // ★ 2026-09-27：上限是**这台机器的物理内存**（调用方算好放在 global 里），不是写死的
        //   65536。从前这一页的上限一直是内置那个数，于是"设置页最多拖到 16256、实例页能拖到
        //   65536"——用户给一个实例填出比整机内存还大的数，启动时只会拿到一句 JVM 的内存分配
        //   错误，而界面上从头到尾没有一个字拦过他（见 2026-09-27 审计）。
        //   `<= 0` 是"调用方没给"（只验结构的构造点），退回内置上限。
        const int memoryCapMb = m_global.memoryCapMb > 0 ? m_global.memoryCapMb
                                                         : MainWindowUI::memoryLimitMaxMb();
        m_memoryMaxSlider = new MemorySlider(cell);
        m_memoryMaxSlider->setObjectName(memoryMaxSliderName());
        m_memoryMaxSlider->setRangeMb(MainWindowUI::memoryMaxMinMb(), memoryCapMb);
        m_memoryMaxSlider->setMinimumWidth(kMemorySliderMinWidth);
        // 没有焦点时滚轮也会改值（见 MainWindowUI.hpp 里的 WheelGuard）。
        WheelGuard::install(m_memoryMaxSlider);
        choiceRow->addWidget(m_memoryMaxSlider, 1);
        m_memoryMaxSpin = new QSpinBox(cell);
        m_memoryMaxSpin->setObjectName(memoryMaxSpinName());
        m_memoryMaxSpin->setRange(MainWindowUI::memoryMaxMinMb(), memoryCapMb);
        m_memoryMaxSpin->setSingleStep(MainWindowUI::memoryStepMb());
        m_memoryMaxSpin->setSuffix(QStringLiteral(" MB"));
        // 和设置页那两个内存数字框同一份滚轮防护（见 MainWindowUI.hpp 里的 WheelGuard）：
        // 数字框在没有焦点时也吃滚轮，改完界面上看不出来，下次启动用的就是那个被滚出来的数。
        WheelGuard::install(m_memoryMaxSpin);
        m_memoryMaxSpin->setFixedWidth(kMemorySpinWidth);
        choiceRow->addWidget(m_memoryMaxSpin);

        // 条和数字框是同一个值的两半 ⇒ 双向同步，各自挡掉回头那一发（不挡就会来回弹）。
        connect(m_memoryMaxSlider, &QSlider::valueChanged, this, [this]() {
            const QSignalBlocker block(m_memoryMaxSpin);
            m_memoryMaxSpin->setValue(m_memoryMaxSlider->valueMb());
        });
        connect(m_memoryMaxSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() {
            const QSignalBlocker block(m_memoryMaxSlider);
            m_memoryMaxSlider->setValueMb(m_memoryMaxSpin->value());
        });

        // 「改了就写」这一页是**每一发都写盘**的（见 .hpp 里 valueSignature() 那段），
        // 所以拖动中那几十发不能每一发都写一次 —— 和设置页同一个拆法：
        //   - 拖动中：只跟着走数字（上面那条同步线），落盘等松手；
        //   - 不是拖动中（键盘、滚轮、点滑槽跳一格）：一次就是一格，立刻报"值变了"。
        // 上面那条同步线用的是**挡了信号**的 setValue，所以 watchEditors() 按类型接的那条
        // 线在拖动时一发都不响 —— 报"值变了"这件事得由条自己说。
        connect(m_memoryMaxSlider, &QSlider::valueChanged, this, [this]() {
            if (!m_memoryMaxSlider->isSliderDown()) {
                notifyEdited();
            }
        });
        connect(m_memoryMaxSlider, &QSlider::sliderReleased, this,
                [this]() { notifyEdited(); });
    }
    {
        // ★ 2026-09-26：这一整行**不显示了**（用户原话：「不要 xms 了（或者隐藏起来），
        //   按照默认值」）。对象照旧建、照旧跟着盘上的值走 —— 撤掉的是"给人改"这一件事，
        //   不是替用户删掉某个实例上已经存过的那份覆盖：那条覆盖照样由 override() 透传回
        //   去（见 override() 里那段）。整行 hide() 而不是不建，也是为了让验证工程还能按
        //   objectName 把它找回来 —— "这一项查得到、但看不见"正是现在的意思。
        const int titleRow = row;   // 标题在 addTitledRow() 里就摆在 (row, 0) 这一格上
        QWidget *cell = addOverrideRow(grid, row, xmsField(), tr("最小内存"),
                                       tr("只对这个实例生效（给 JVM 的 -Xms）。"));
        row += 2;
        m_xmsFollowCell = cell;
        auto *choiceRow = choiceRowOf(xmsField());
        m_memoryMinSpin = new QSpinBox(cell);
        m_memoryMinSpin->setObjectName(memoryMinSpinName());
        m_memoryMinSpin->setRange(MainWindowUI::memoryMinMinMb(), MainWindowUI::memoryLimitMaxMb());
        m_memoryMinSpin->setSingleStep(MainWindowUI::memoryStepMb());
        m_memoryMinSpin->setSuffix(QStringLiteral(" MB"));
        // 同上：滚轮防护（见 MainWindowUI.hpp 里的 WheelGuard）。
        WheelGuard::install(m_memoryMinSpin);
        m_memoryMinSpin->setFixedWidth(kMemorySpinWidth);
        choiceRow->addWidget(m_memoryMinSpin);
        // 连**标题**一起藏。只藏下面那一格的话，表上会留下孤零零一个「最小内存」（底下
        // 空一块），看着像界面坏了 —— 藏的是"这一行"，不是"这一行里的控件"。
        // 标题是 addTitledRow() 摆在 (titleRow, 0) 上那个 QLabel；QGridLayout 对藏起来的
        // 控件不算高度，所以这两下一做，那一行就整个塌掉、不留空隙。
        if (QLayoutItem *titleItem = grid->itemAtPosition(titleRow, 0)) {
            if (QWidget *title = titleItem->widget()) {
                title->hide();
            }
        }
        cell->hide();
    }
    {
        QWidget *cell = addOverrideRow(grid, row, autoMemoryField(), tr("内存分配"),
                                       tr("只对这个实例生效（勾上 = 按系统内存现算）。"));
        row += 2;
        auto *choiceRow = choiceRowOf(autoMemoryField());
        m_memoryAutoCheck = new QCheckBox(tr("自动（按系统内存）"), cell);
        m_memoryAutoCheck->setObjectName(memoryAutoCheckName());
        choiceRow->addWidget(m_memoryAutoCheck);
        // 勾选框也要接：在「自定义」下面切换自动/手动时，上面那两行的开关跟着变。
        // 接在这里而不是 addOverrideRow 里 —— 那个函数返回时这个勾选框还不存在。
        connect(m_memoryAutoCheck, &QCheckBox::toggled, this,
                &InstanceSettingsDialog::refreshHints);
    }

    // ---------- 窗口模式 / 窗口大小（2026-09-28 加的）----------
    //
    // 这两行是用户原话「游戏的启动参数 小窗/最大化/全屏 以及游戏分辨率 放到设置和实例设置
    // 里面」里**实例设置那一半**（全局那一半在设置页「启动」栏，见 MainWindowUI）。
    // 和上面几项同一个形状：能「跟随全局」，也能给这一个实例钉一份 —— 绝大多数实例是跟随。
    //
    // NOTE: 档位那几项**由调用方灌**（windowModes 参数），这一页只认「标签 + 代号」那一对。
    //       理由见 .hpp 里 WindowModeChoice 那一段（ui/ 不许 include src/，档位那个枚举
    //       和那三个代号串的唯一出处都在 src/）。
    // NOTE: 三个串（两个键名 + 那三个代号）在这一页里**一个字都不重写**：键名是上面那三个
    //       Field() 函数，代号是调用方喂进来的。这正是这条分层规矩要保住的性质。
    if (!windowModes.isEmpty()) {
        QWidget *cell = addOverrideRow(grid, row, windowModeField(), tr("窗口模式"),
                                       tr("只对这个实例生效：这一个实例照这一档开窗。"));
        row += 2;
        auto *choiceRow = choiceRowOf(windowModeField());
        m_windowModeCombo = new QComboBox(cell);
        m_windowModeCombo->setObjectName(windowModeComboName());
        // 下拉框也吃滚轮（在选项之间来回跳），而且没有焦点时改完看不出来 —— 见 WheelGuard。
        WheelGuard::install(m_windowModeCombo);
        for (const WindowModeChoice &choice : windowModes) {
            m_windowModeCombo->addItem(choice.label, choice.token);
        }
        // ★ 换档位**也要发 `settingsEdited()`** —— 这一页是「改了就写」，而按类型接的那几条
        //   线（QAbstractButton / QLineEdit / QSpinBox / QPlainTextEdit）**一条都接不到
        //   QComboBox**，漏了它的症状很具体 —— 用户把这行切到「自定义」（那一发是档位那格
        //   发的，写盘了），再从下拉框里换一档 ⇒ **盘上还是老那一档**，而他看着屏幕上写着
        //   「全屏」。下次动别的开关才会顺带写进去，也就是"改了有时候生效"。
        //   下面灌值那一段是 setCurrentIndex + QSignalBlocker，不会把"打开这一页"当成一次编辑。
        //   NOTE: 「跟随全局」那句提示不用跟着重刷 —— 它写的是**全局**那一档，和这一格选了
        //         什么无关（见 refreshHints()）。
        //   NOTE: 2026-09-28 起这一条**不再单独接在这儿**：`watchEditors()` 新加的那一循环
        //         把这一页上**所有** `QComboBox` 一次接上（含这个和那七格档位），所以这里
        //         只剩这段来龙去脉。那条"容易在测试里被盖住"的教训原样有效 —— mw_check 的
        //         T20.11b 仍然把换档位摆成那一页上最后一个动作。
        choiceRow->addWidget(m_windowModeCombo, 0, Qt::AlignLeft);
    }
    {
        // 分辨率：**一行两个数字框**（宽 × 高）。它和「窗口模式」是两行两对单选钮，但那两个
        // 数本身是一对 —— override() 那边要么两个一起收、要么两个都不收（见 .hpp 里那段）。
        QWidget *cell = addOverrideRow(grid, row, windowWidthField(), tr("窗口大小"),
                                       tr("只对这个实例生效（单位是像素）。"));
        row += 2;
        auto *choiceRow = choiceRowOf(windowWidthField());
        m_windowWidthSpin = new QSpinBox(cell);
        m_windowWidthSpin->setObjectName(windowWidthSpinName());
        m_windowWidthSpin->setRange(MainWindowUI::windowMinWidth(), MainWindowUI::windowMaxWidth());
        m_windowWidthSpin->setSingleStep(MainWindowUI::windowSizeStep());
        m_windowWidthSpin->setMinimumWidth(kWindowSizeSpinWidth);
        WheelGuard::install(m_windowWidthSpin);
        choiceRow->addWidget(m_windowWidthSpin);

        auto *times = new QLabel(QStringLiteral("×"), cell);
        times->setObjectName(windowSizeTimesName());
        choiceRow->addWidget(times);

        m_windowHeightSpin = new QSpinBox(cell);
        m_windowHeightSpin->setObjectName(windowHeightSpinName());
        m_windowHeightSpin->setRange(MainWindowUI::windowMinHeight(),
                                     MainWindowUI::windowMaxHeight());
        m_windowHeightSpin->setSingleStep(MainWindowUI::windowSizeStep());
        m_windowHeightSpin->setMinimumWidth(kWindowSizeSpinWidth);
        WheelGuard::install(m_windowHeightSpin);
        // 整行按 sizeHint 靠左摆，不撑满（右边剩下的地方留给那对单选钮）—— 和设置页那一行
        // 同一条规矩。
        choiceRow->addWidget(m_windowHeightSpin, 0, Qt::AlignLeft);
        // 顶上那句「跟随全局（当前 854×480）」要写全局那一份，从构造函数给的那份值里取。
        // **只在调用方给了正数时才记**：`<= 0` 是"调用方没给"的哨兵（见 .hpp 里
        // GlobalValues 那一段），那时那句话里不写尺寸。
        if (global.windowWidth > 0 && global.windowHeight > 0) {
            m_globalWindowWidth = global.windowWidth;
            m_globalWindowHeight = global.windowHeight;
        }
    }

    // ---------- 高级选项（默认折叠） ----------
    {
        m_advancedToggle = new QToolButton(this);
        m_advancedToggle->setObjectName(advancedToggleName());
        m_advancedToggle->setText(advancedToggleText());
        m_advancedToggle->setCheckable(true);
        m_advancedToggle->setChecked(false);
        m_advancedToggle->setArrowType(Qt::RightArrow);
        m_advancedToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        // 默认折叠。里面几项都是"改错了游戏直接起不来"的东西，一打开对话框就摆在
        // 眼前等于诱导用户去动它。
        grid->addWidget(m_advancedToggle, row++, 0, 1, 2, Qt::AlignLeft);

        m_advancedBox = new QWidget(this);
        m_advancedBox->setObjectName(advancedBoxName());
        auto *advancedGrid = new QGridLayout(m_advancedBox);
        advancedGrid->setContentsMargins(16, 0, 0, 0);
        advancedGrid->setHorizontalSpacing(12);
        advancedGrid->setVerticalSpacing(10);
        advancedGrid->setColumnStretch(0, MainWindowUI::leftColumnStretch());
        advancedGrid->setColumnStretch(1, MainWindowUI::rightColumnStretch());
        m_advancedBox->setHidden(true);
        grid->addWidget(m_advancedBox, row++, 0, 1, 2);

        connect(m_advancedToggle, &QToolButton::toggled, this, [this](bool expanded) {
            // 展开前这个窗有多高：折回时按它还原（用户自己拉过的也算数 —— 记的是"展开
            // 那一刻"的高度，不是构造时那个 820x560）。
            if (expanded) {
                m_heightBeforeAdvanced = height();
            }
            m_advancedBox->setHidden(!expanded);
            m_advancedToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
            // 折回之后得让窗口**缩回去**：Qt 只在"窗口比内容还小"时把它撑大（展开那一下
            // 就是它干的），内容变少时它一动不动 —— 不补这一句，展开一次再收起，底下就
            // 永远留着那一截空白。实测（那时默认还是 600x480）：展开 → 600x503、
            // 折回 → **还是** 600x503（那时布局只要 310，多出来的全是空白）。
            //
            // NOTE: 必须**晚一拍**再缩，而且缩之前得把布局那一串**推一遍**。这一下
            //       `setHidden()` 只是把"重算布局"排进事件队列，此刻这个窗的最小高度还是
            //       展开着的那个数（503），当场 `resize()` 会被它钳回去 —— 看着像"修了但
            //       没生效"。排一发 0 毫秒的还不够：这一页套了**好几级**布局（表格 → 表单
            //       → 那一页 → 堆叠页 → 三栏 → 整页），一级算完才把下一级的 LayoutRequest
            //       排进去，实测要**两拍**最小高度才降回来（2026-09-26 分栏之后多套了两级：
            //       分栏前是一拍就够的）。所以这儿不数拍子，改成**由内到外把每一级的布局
            //       `activate()` 一遍**再缩 —— 收敛是当场问出来的，跟套了几级无关。
            //       收尾那一发挂在 this 上，窗先没了就不会再响。
            if (!expanded) {
                QTimer::singleShot(0, this, [this] {
                    if (m_heightBeforeAdvanced <= 0) {
                        return;
                    }
                    // 由内到外（m_advancedBox 一路往上到 this）：每一级自己重算一次。
                    for (QWidget *widget = m_advancedBox; widget != nullptr;
                         widget = widget->parentWidget()) {
                        if (widget->layout() != nullptr) {
                            widget->layout()->activate();
                        }
                        if (widget == this) {
                            break;
                        }
                    }
                    // 只动高、不动宽：宽度是用户自己拉的（那几个"比分组对话框宽一点"的
                    // 起点），那一块藏在下面，跟宽度没有关系。
                    resize(width(), m_heightBeforeAdvanced);
                });
            }
        });

        // 高级选项里这三行也照「上一行设置名称，下一行设置选项」排（用户说的是**这一栏
        // 整个**，不只是上面那四项）——所以 `advancedRow` 一律 `+= 2`。
        int advancedRow = 0;
        {
            QWidget *cell =
                addOverrideRow(advancedGrid, advancedRow, extraJvmArgsField(), tr("额外 JVM 参数"),
                               tr("只对这个实例生效：取代全局那一份，不是叠加。多条用空格分隔。"));
            advancedRow += 2;
            auto *choiceRow = choiceRowOf(extraJvmArgsField());
            m_extraJvmArgsEdit = new QLineEdit(cell);
            m_extraJvmArgsEdit->setObjectName(extraJvmArgsEditName());
            m_extraJvmArgsEdit->setPlaceholderText(QStringLiteral("-XX:+UseG1GC"));
            choiceRow->addWidget(m_extraJvmArgsEdit, 1);
        }
        {
            // 追加参数**没有**「跟随全局 / 自定义」那一对按钮：它不是覆盖层的一项，
            // meta 里也没有对应的全局值可以跟随 —— 全局那一份是 extraJvmArgs，两个是
            // 不同的东西（一个取代、一个追加）。
            QVBoxLayout *cellLayout = nullptr;
            QWidget *cell =
                addTitledRow(advancedGrid, advancedRow, tr("追加参数"), m_advancedBox, &cellLayout);
            advancedRow += 2;
            m_appendJvmArgsEdit = new QPlainTextEdit(cell);
            m_appendJvmArgsEdit->setObjectName(appendJvmArgsEditName());
            m_appendJvmArgsEdit->setFixedHeight(kAppendJvmArgsHeight);
            m_appendJvmArgsEdit->setPlainText(appendJvmArgs.join(QLatin1Char('\n')));
            cellLayout->addWidget(m_appendJvmArgsEdit);
            auto *hint = new QLabel(appendJvmArgsHint(), cell);
            hint->setWordWrap(true);
            cellLayout->addWidget(hint);
        }
        {
            // 提醒开关。和上面那行一样：**不是覆盖层的一项**（没有「跟随全局 / 自定义」
            // 那对单选钮），它写的是 meta 里的另一个键，由 MainWindow 合并写进去。
            //
            // 放在「高级选项」里而不是摆在五项旁边：它不是一个"值"，摆在那五行之间会让人
            // 以为它也能跟随全局。藏在这里的代价是"关了之后不好找回来"，所以启动时那个
            // 对话框的正文里点名说了它在哪。
            QVBoxLayout *cellLayout = nullptr;
            QWidget *cell = addTitledRow(advancedGrid, advancedRow, tr("版本提醒"), m_advancedBox,
                                         &cellLayout);
            advancedRow += 2;
            m_warnJavaMismatchCheck = new QCheckBox(tr("Java 版本与版本要求不符时提醒"), cell);
            m_warnJavaMismatchCheck->setObjectName(warnJavaMismatchCheckName());
            m_warnJavaMismatchCheck->setChecked(warnJavaMismatch);
            cellLayout->addWidget(m_warnJavaMismatchCheck);
            auto *hint = new QLabel(
                tr("只对这个实例生效。取消勾选之后启动时不再问「这个 Java 版本对不上」，"
                   "直接按你指定的 Java 启动。"),
                cell);
            hint->setWordWrap(true);
            cellLayout->addWidget(hint);
        }
    }

    // ---------- 导出启动脚本 / 补全版本文件（2026-09-26，两颗按钮并排一行）----------
    //
    // 这两件是**动作**，不是设置项：它们不改 wtfml.meta 里的任何一格，所以没有
    // 「跟随全局 / 自定义」那一对单选钮 —— 和上面「追加参数」「版本提醒」同一个形状
    // （那两个也不是覆盖层的一项）。
    //
    // 用户原话「按钮文本直接做成设置名称」，给的例子就是「导出启动脚本（按钮）
    // 补全版本文件（按钮）」并排一行 ⇒ 这一对**没有行名**（按钮上那几个字就是名字），
    // 两句解释挪去 tooltip + 底下那一行合起来的说明。
    //
    // NOTE: 说明**不能只放 tooltip**：「导出的那份里带着登录令牌，别发给别人」是一句安全
    //       警告，而 tooltip 得悬停才看得见（理由见 .hpp 里 actionsHint() 那段）。
    // NOTE: 摆在「高级选项」**之后**：这一页从上到下是"这个实例拿什么启动"（Java、内存、
    //       额外参数），两件动作是收尾。而且高级选项默认折叠着，所以打开这一页看到的
    //       顺序是 Java 路径 → 最大内存 → 最小内存 → 内存分配 → 两件动作。
    {
        auto *cell = new QWidget(this);
        auto *column = new QVBoxLayout(cell);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(4);

        // ★ 2026-09-28：两颗并排 → **这一页统一的那一套**（每行三颗、两颗各占三分之一）。
        //   两颗那一行**不摊开**，和概览上那四颗同一个道理（见 layoutActionCells() 那段）。
        auto *pairGrid = newActionGrid();
        auto *exportScript = new QPushButton(exportScriptText(), cell);
        exportScript->setObjectName(exportScriptButtonName());
        Theme::markButton(exportScript, Theme::ButtonKind::Neutral);
        exportScript->setToolTip(exportScriptHint());
        connect(exportScript, &QPushButton::clicked, this, [this]() {
            // 默认文件名用**这个实例的目录名**（= 构造时那个 instanceId）：它一定是合法
            // 文件名（建/改名那一关挡过非法字符），而显示名不一定。目录也顺带兜一层底。
            QString dir = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
            if (dir.isEmpty()) {
                dir = QDir::homePath();
            }
            const QString suggested =
                dir + QLatin1Char('/') + m_instanceId + QStringLiteral(".bat");
            // 开的是 **Windows 自己的保存框**（和上面「浏览…」选 java.exe 同一档：全项目
            // 就剩这几处独立窗，理由见 MainWindow.cpp 里 onAddFolderClicked() 上面那段）。
            // 那个框上的「取消」= 什么都不做（**不是"取消这一页的设置"** —— 这一页没有
            // 那种东西了，改过的值早写进盘里了，见文件头那一段）。
            const QString picked = QFileDialog::getSaveFileName(
                this, exportScriptText(), suggested, tr("批处理文件 (*.bat)"));
            if (picked.isEmpty()) {
                return;
            }
            // 真正"写什么"在 MainWindow 里：这一层够不着 authManager / versionManager
            // （ui/ 不 include src/ 下的头）。见 .hpp 里那个信号的说明。
            emit exportLaunchScriptRequested(picked);
        });
        auto *repair = new QPushButton(repairFilesText(), cell);
        repair->setObjectName(repairFilesButtonName());
        repair->setToolTip(repairFilesHint());
        // 只报"用户想干这件事"：能不能开始（有没有下载在跑 / 这个实例装没装）由
        // MainWindow 说了算，它才知道。**不问路径**，所以这一层没什么可问的了。
        connect(repair, &QPushButton::clicked, this, &InstanceSettingsDialog::repairFilesRequested);
        layoutActionButtons(pairGrid, {exportScript, repair});
        column->addLayout(pairGrid);

        auto *hint = new QLabel(actionsHint(), cell);
        hint->setWordWrap(true);
        column->addWidget(hint);
        grid->addWidget(cell, row++, 0, 1, 2);
    }

    // ---------- 文件那八页（见 .hpp 里 filePageKeys() 那一段）----------
    //
    // 这八页说的是**这个实例自己的目录**：`--gameDir` 就是 `versions/<实例名>/`，所以
    // `mods/ resourcepacks/ shaderpacks/ datapacks/ saves/` 都在里头 —— 实例之间天然隔离，
    // 在这一页里动什么都不会碰到别的实例。
    //
    // 八页分两种形状：服务器列表读一份递进来的解析结果（③），其余七页是同一个
    // InstanceFilesPage（②）。**目录一律由调用方算好递进来**（setInstanceDir / setFileDirs）
    // —— 那几个目录名是 MC 的约定，src/download/ResourceCatalog::folderForKind() 是唯一
    // 出处，这一层不抄第二份。
    //
    // NOTE: 页面标题（那行粗体）**故意没有** —— 左栏那一列一直亮着，写在这里是把同一句话
    //       挨着说两遍。每一页正文底下那行小字才是这一页**特有**的那句。
    {
        // ② 中间那七页：同一个控件，喂不同的目录与三条开关。
        const QStringList keys = filePageKeys();
        for (const QString &key : keys) {
            if (key == serverPageKey()) {
                continue;
            }
            QWidget *page = addPage(key);
            auto *layout = qobject_cast<QVBoxLayout *>(page->layout());
            if (layout == nullptr) {
                continue;
            }

            // ★ 「配置文件」那一页是**两格子**（见 .hpp 里 configFilePage() 上面那一段：
            //   开不出第二个整页覆盖层）。第 0 格是这一张列表（连「上一级」和底下那行小字
            //   一起），第 1 格是编辑器。别的几页只有一格 —— 所以下面这些"摆进这一页"的
            //   动作全部改摆进 `target`，而不是一律摆进 `layout`。
            const bool isConfig = key == QStringLiteral("config");
            // ★ 2026-09-28：存档那一页（见下面 setCanEdit / setImportText / setBatchText
            //   那三处 —— 支笔、导入世界、批量编辑都只在这一页上）。
            const bool isSave = key == QStringLiteral("save");
            QWidget *listHost = page;
            QVBoxLayout *target = layout;
            if (isConfig || isSave) {
                auto *stack = new QStackedWidget(page);
                if (isConfig) {
                    m_configStack = stack;
                } else {
                    m_worldStack = stack;
                }
                listHost = new QWidget(stack);
                auto *listLayout = new QVBoxLayout(listHost);
                // 第 0 格自己不留白：它四周的留白本来就是外面那一页给的，这一格和那一页
                // 是同一块地方（切过来切过去不该看见内容整体缩一下）。
                listLayout->setContentsMargins(0, 0, 0, 0);
                listLayout->setSpacing(layout->spacing());
                stack->addWidget(listHost);
                target = listLayout;
            }

            auto *files = new InstanceFilesPage(listHost);
            files->setObjectName(filesPageName(key));
            // 存档只列文件夹（saves 底下那些 level.dat / session.lock 不是存档）。
            // ★ 2026-10-11：「模组」那一页**只列文件** —— `mods/` 底下那些目录不是模组
            //   （`mcef-libraries`、Sinytra 的 `.connector` 这种是模组自己用的房子），
            //   而它们在屏幕上占着一行、行尾还挂着开关，点一下是把**整个文件夹**改了名
            //   （真机上已经发生过）。另外三页照旧 —— 一个资源包 / 数据包 / 光影包
            //   **本身就可以是一个目录**（游戏认没解压的那种），那是另一回事。
            files->setMode(key == QStringLiteral("save")   ? InstanceFilesPage::Mode::DirsOnly
                           : key == QStringLiteral("mod") ? InstanceFilesPage::Mode::FilesOnly
                                                          : InstanceFilesPage::Mode::FilesAndDirs);
            // 只有游戏会自己扫的那三个目录给「禁用」；存档**不给删**（理由见
            // InstanceFilesPage::setCanDelete()）。
            //
            // ★ 「配置文件」也不给删（2026-09-27）：这一页的存在意义就是**改**（点一行开
            //   编辑器），行尾再挂一颗垃圾桶等于在"只想改一个数"的动线上摆一个删了就没了
            //   的键 —— 而这一页没有"恢复默认"（模组下次启动会重写一份默认的，但那要等它
            //   启动，而且不保证每个模组都这么干）。要删，那颗「打开文件夹」就在上面。
            files->setCanToggle(fileKeyCanToggle(key));
            files->setCanDelete(key != QStringLiteral("save") && key != QStringLiteral("config"));
            // ★ 2026-09-28：**只有存档那一页**行尾有那支笔，而且只有那一页的目录行会去
            //   算真的大小/时间（见 InstanceFilesPage::setCanEdit()——两件事是一套的）。
            files->setCanEdit(isSave);
            files->setEmptyText(filesEmptyText(key));
            files->setDirectory(m_fileDirs.value(key));
            // ★ 顶上那颗「下载新 X」（2026-09-27）：只有下载页里真有对应一栏的那四页有
            //   （见 filesDownloadText()）。按下去**只报一个分类键**，切选项卡、挑那
            //   一行都在 MainWindow 那一层 —— 这一层够不着选项卡，也不该知道下载页左栏
            //   有几栏（见 browseDownloadsRequested() 那段）。
            files->setDownloadText(filesDownloadText(key), filesDownloadTip(key));
            // ★ 2026-09-28：状态那一栏里要不要有「可更新」——**判据和上面那颗「下载新 X」
            //   是同一个**（下载页里真有对应一栏的那四页）：只有那四页上的文件查得动更新，
            //   别的几页（原理图 / 存档 / 配置文件）里的东西根本不在任何平台的清单里。
            //   多一格永远筛不出东西的选项，用户只会以为"筛坏了"。
            files->setCanFilterUpdates(!filesDownloadText(key).isEmpty());
            // ★ 2026-10-09：**那四页**再开一套「批量管理」（行尾的详情按钮 + 多选 + 底层
            //   那条悬浮操作条 + 顶上四颗横向筛选按钮）。判据**就是上面那个** ——
            //   `filesNameOpensResource()` 说的正是"这一条认得出是哪个资源"，而详情按钮
            //   要的恰好是这一件事（见 MainWindow 那边 detailRequested 那一发 —— 这一发
            //   2026-10-09 从 `resourceRequested` 改的名）。
            //   用户原话：「在模组/资源包等的设置里面…」—— 不新开一张"哪几页算数"的名单。
            // ★ 2026-10-10：这一发同时是**"这一页底下那段说明要不要挂"**的判据（见下面
            //   建 hint 那一处）—— 用户原话：「把这个页面下方那段叙述删了来让框拉到底充分
            //   利用空间」，范围是**这四页**（模组 / 资源包 / 数据包 / 光影包，也就是这套
            //   批量管理的那四页）。其余几页（存档 / 原理图 / 配置文件 / 服务器列表 /
            //   快捷方式）底下那行小字**照旧挂着** —— 它们讲的是别的规矩（数据包那句
            //   "下了不等于生效、要自己搬进存档"连它一起没了，是用户点头的）。
            const bool batch = filesNameOpensResource(key);
            files->setBatchMode(batch);
            // ★ 2026-09-28：存档那一页顶上多两颗（「导入世界」/「批量编辑」）。用户原话：
            //   「顶端加个导入世界按钮和批量编辑按钮（这个先做占位）」。
            //   别的几页**不接文字**（= 那两颗藏起来，见 setImportText）。「批量编辑」今天
            //   是占位 —— 这一层照旧只发信号，那句"还没做"由 MainWindow 说（这一层连
            //   InlineMessage 都用不了）。
            if (isSave) {
                files->setImportText(importWorldText(), importWorldTip());
                files->setBatchText(batchEditWorldsText(), batchEditWorldsTip());
            }
            connect(files, &InstanceFilesPage::downloadRequested, this, [this, key] {
                const QString category = filesDownloadCategoryKey(key);
                if (!category.isEmpty()) {
                    emit browseDownloadsRequested(category);
                }
            });
            // ★ 行尾那颗「详情」按下去（2026-09-27 起是"名字点得动"，2026-10-09 换成那颗
            //   按钮）：**只有"点得动"的那几页接**。判据就是 filesNameOpensResource()
            //   —— 不在表里的那几页既没有那一格按钮，这一条线也不接。
            //   接线这一层是多余的：真的按下去是 InstanceFilesPage 那边认出路径才发的
            //   （认不出来的那些那一颗是灰的，按不动）。
            //   ★ 2026-10-10：**右键点一行**走的也是这一发（用户原话：「右键点击时也可以
            //   进入详情页，此时保留上一页的选择」）—— 判据、落点、收页那一套全都一样，
            //   这一层不必知道用户是按了那颗 ⓘ 还是右键了那一行。
            if (filesNameOpensResource(key)) {
                connect(files, &InstanceFilesPage::detailRequested, this,
                        [this, key](const QString &path) { emit fileResourceRequested(key, path); });
                // ★★ 2026-10-11：这里转过一发 `updateCheckRequested`（那一页顶上「可更新」
                //   在还没查过时兼作第二个入口）—— 已拆，用户原话「**没有检查的时候先隐藏
                //   可更新按钮**」：那一颗在没查过的时候根本摆不出来。检查更新这一栏照旧
                //   只有「概览」那一颗（`checkUpdatesRequested()` 那条线一个字都没动）。

                // 批量那三发：**判据同上**（这三件事只在那四页上谈得上）。
                // NOTE: 转发上去的三个动作全**不改盘** —— 问不问那句、怎么落盘、落完要不要
                //       重列，都是 MainWindow 的事（这一层连 AppPaths 都够不着）。
                connect(files, &InstanceFilesPage::batchUpdateRequested, this,
                        [this, key](const QStringList &paths) {
                            emit fileBatchUpdateRequested(key, paths);
                        });
                connect(files, &InstanceFilesPage::batchToggleRequested, this,
                        [this, key](const QStringList &paths, bool disable) {
                            emit fileBatchToggleRequested(key, paths, disable);
                        });
                connect(files, &InstanceFilesPage::batchDeleteRequested, this,
                        [this, key](const QStringList &paths) {
                            emit fileBatchDeleteRequested(key, paths);
                        });
            }

            // ★ 「配置文件」那一页多两样（2026-09-27）：上面一颗「上级」，以及"双击/回车
            //   有反应"。别的几页看的是一个平铺的目录，config/ 底下却还有层级
            //   （`config/ftbquests/`、`config/paxi/`…），得能进去也得出得来。
            if (isConfig) {
                // 一颗也走这一页统一的那一套（三分之一宽、靠左）—— 见 layoutActionCells()
                // 那段：**少一颗的那一行不摊开**，一颗独苗撑满整幅会让它比别处的按钮大一号。
                auto *navRow = newActionGrid();
                m_configUp = new QPushButton(configUpText(), listHost);
                m_configUp->setObjectName(configUpButtonName());
                Theme::markButton(m_configUp, Theme::ButtonKind::Neutral);
                connect(m_configUp, &QPushButton::clicked, this,
                        &InstanceSettingsDialog::leaveConfigDir);
                layoutActionButtons(navRow, {m_configUp});
                target->addLayout(navRow);
                // 摆到"还没接过目录"那一档（m_configRoot 要等 setFileDirs 才有）。
                updateConfigUpButton();

                connect(files, &InstanceFilesPage::activateRequested, this,
                        &InstanceSettingsDialog::onConfigEntryActivated);
            }
            connect(files, &InstanceFilesPage::openFolderRequested, this,
                    &InstanceSettingsDialog::openInstanceFolderRequested);
            connect(files, &InstanceFilesPage::toggleRequested, this,
                    &InstanceSettingsDialog::toggleInstanceFileRequested);
            connect(files, &InstanceFilesPage::deleteRequested, this,
                    &InstanceSettingsDialog::deleteInstanceFileRequested);
            // ★ 2026-09-28：存档那一页那三样（那支笔 / 导入世界 / 批量编辑）**故意不在这儿
            //   接线**。上面那几发之所以要绕这一层，是因为它们需要翻译（`downloadRequested`
            //   要把页键翻成下载页的分类键、`toggleRequested` 是整个文件管理共用的）；
            //   而这三发只属于存档那一页、也不用翻任何东西 —— 由 MainWindow 直接连到
            //   `filesPage("save")` 上（那一页就是它）。多一层纯转发的信号，只是多一份
            //   要跟着改的声明。
            m_filesPages.insert(key, files);
            target->addWidget(files, 1);
            // ★ 2026-10-10：**开了批量管理的那四页不再挂底下那段说明**（用户原话：
            //   「把这个页面下方那段叙述删了来让框拉到底充分利用空间」）—— 列表那一格是
            //   `stretch = 1`、小字那一格没有 stretch，所以少一句小字 = 列表**直接多长
            //   那么高**（原来那几页里最长的一句在默认窗口下是两三行；那句所在的
            //   `filesDetailHint()` 这一趟已删，见 .hpp 里那段）。
            //   判据就是上面那一发 `batch` —— 和"这一页有没有那一套按钮"是同一句话，
            //   不新开第二张名单。
            if (!batch) {
                auto *hint = new QLabel(filesHint(key), listHost);
                hint->setObjectName(filesHintName(key));
                hint->setWordWrap(true);
                target->addWidget(hint);
            }

            // 第 1 格：编辑器。**建一次留着**（见 .hpp 里 configFilePage() 那两条 NOTE）
            // —— 每一趟开文件只是往里灌内容（openConfigEditor()），接线只有一次。
            if (isConfig) {
                m_configEditor = new ConfigFileEditorPage(m_configStack);
                m_configStack->addWidget(m_configEditor);
                layout->addWidget(m_configStack, 1);
            }
            // 存档那一页的第 1 格：世界设置（2026-09-28），做法和理由同上（见 .hpp 里
            // openWorldSettings() 上面那一段）。
            if (isSave) {
                m_worldSettings = new WorldSettingsPage(m_worldStack);
                m_worldStack->addWidget(m_worldSettings);
                layout->addWidget(m_worldStack, 1);
            }
        }

        // ③ 服务器列表：读 servers.dat 的结果由调用方递进来（这一层不 include src/）。
        {
            QWidget *page = addPage(QStringLiteral("server"));
            auto *layout = qobject_cast<QVBoxLayout *>(page->layout());
            if (layout != nullptr) {
                // ★ 2026-09-28：这两颗从前是"靠右一头"（前面一条 stretch），现在归进这一页
                //   统一的那一套（每行三颗、各占三分之一、靠左）—— 用户原话「每一行三个，
                //   间距适中且左右能充分利用」。
                auto *top = newActionGrid();
                // ★★ 2026-10-10：「测速」**没有了**（用户原话「服务器那一栏测速和刷新
                //   作用重复了」）—— 它和「刷新」都会连上去问一遍每台现在什么样，屏幕上
                //   并排摆着，用户分不出该按哪一颗。留下的这一颗把两件事都做了：
                //   **重读 servers.dat + 重测每一台**（那一页从 2026-09-27 起就不只是那份
                //   文件的一面镜子：图标 / MOTD / 延迟都是刚问来的）。
                m_serverRefresh = new QPushButton(serverRefreshText(), page);
                m_serverRefresh->setObjectName(serverRefreshButtonName());
                Theme::markButton(m_serverRefresh, Theme::ButtonKind::Neutral);
                // 重读要碰 NBT 解析（在 src/ 里），所以这一发只能交给 MainWindow。
                connect(m_serverRefresh, &QPushButton::clicked, this,
                        &InstanceSettingsDialog::refreshServerListRequested);
                // ★ 2026-10-10：「编辑」（改这一台的名字和地址）。用户原话「在服务器列表
                //   那里做一个编辑地址和名称的功能」。
                //   它**不改盘、也不问话** —— 那两件事分别是 src/manage/ServerListFile 和
                //   覆盖层的事，这一层一样都够不着（问话的入口每一个都要 OverlayHost）。
                //   所以它只发一条"要改的是哪一台"，剩下的由 MainWindow 接。
                m_serverEdit = new QPushButton(serverEditText(), page);
                m_serverEdit->setObjectName(serverEditButtonName());
                m_serverEdit->setToolTip(serverEditTip());
                Theme::markButton(m_serverEdit, Theme::ButtonKind::Neutral);
                connect(m_serverEdit, &QPushButton::clicked, this, [this] {
                    QTreeWidgetItem *item = selectedServerItem();
                    if (item == nullptr) {
                        return;  // 没选中（这颗本来就是灰的；这是第二道闸）。
                    }
                    emit editServerRequested(item->data(0, MainWindowUI::serverNameRole()).toString(),
                                             item->data(1, MainWindowUI::serverAddressRole())
                                                 .toString());
                });
                // ★ 2026-10-10：「添加」和「删除」（用户原话「编辑要能改名字改地址以及
                //   删除，服务器那个页面里放一个添加服务器的按钮，允许添加」）。和「编辑」
                //   同一档：这一层不碰盘、也不问话，只发一条，落盘和问话都在 MainWindow。
                //   ★ 两颗的**可点性不一样**，这是它们和「编辑」唯一的区别：
                //     - 「添加」不依赖选中行（要加的那一台还没出现在表里），所以**常亮**；
                //     - 「删除」动的是选中那一行，跟着选中走（见 updateServerSelectionActions）。
                //   档位：「添加」是普通动作（Neutral）；「删除」是这一页上唯一一颗真会
                //   从盘上拿掉东西的键 —— 照「删除实例」那一颗（`ButtonKind::Danger`，
                //   见 ui/Theme.hpp 那句"不可撤销"）。
                m_serverAdd = new QPushButton(serverAddText(), page);
                m_serverAdd->setObjectName(serverAddButtonName());
                m_serverAdd->setToolTip(serverAddTip());
                Theme::markButton(m_serverAdd, Theme::ButtonKind::Neutral);
                connect(m_serverAdd, &QPushButton::clicked, this,
                        &InstanceSettingsDialog::addServerRequested);

                m_serverDelete = new QPushButton(serverDeleteText(), page);
                m_serverDelete->setObjectName(serverDeleteButtonName());
                m_serverDelete->setToolTip(serverDeleteTip());
                Theme::markButton(m_serverDelete, Theme::ButtonKind::Danger);
                connect(m_serverDelete, &QPushButton::clicked, this, [this] {
                    QTreeWidgetItem *item = selectedServerItem();
                    if (item == nullptr) {
                        return;  // 同上：这一颗本来就是灰的。
                    }
                    emit deleteServerRequested(
                        item->data(0, MainWindowUI::serverNameRole()).toString(),
                        item->data(1, MainWindowUI::serverAddressRole()).toString());
                });
                // ★ 「加入服务器」（**临时占位**，2026-10-10）：用游戏自己的
                //   `--quickPlayMultiplayer` 直接连进去。用户原话「世界和服务器都做一个
                //   临时占位（加入选中的世界（仅限有这个参数的版本）/服务器）先试试看能
                //   不能生效」。
                //   ★ 默认**收着**（文字一开始是空的，见 setServerJoinText()）：一个没有
                //     那个启动参数的版本上露着它就是"按下去游戏直接报错退出"（jopt-simple
                //     认不出那个选项）。露不露由调用方读了版本 json 之后说了算。
                m_serverJoin = new QPushButton(QString(), page);
                m_serverJoin->setObjectName(serverJoinButtonName());
                m_serverJoin->setVisible(false);
                // 「加入」是这一页上唯一一颗"真的要开游戏"的键（和概览页那颗启动同一档）。
                Theme::markButton(m_serverJoin, Theme::ButtonKind::Accent);
                connect(m_serverJoin, &QPushButton::clicked, this, [this] {
                    QTreeWidgetItem *item = selectedServerItem();
                    if (item == nullptr) {
                        return;  // 同上。
                    }
                    emit joinServerRequested(item->data(0, MainWindowUI::serverNameRole()).toString(),
                                             item->data(1, MainWindowUI::serverAddressRole())
                                                 .toString());
                });
                // 顺序 = 用户给的那一串（刷新 / 编辑 / 添加 / 删除），「加入服务器」压尾。
                // 每行三颗（`kActionButtonsPerRow`）⇒ 自动折成「刷新 编辑 添加 / 删除 加入」。
                layoutActionButtons(top, {m_serverRefresh, m_serverEdit, m_serverAdd,
                                          m_serverDelete, m_serverJoin});
                layout->addLayout(top);

                m_serverNotice = new QLabel(page);
                m_serverNotice->setObjectName(serverNoticeName());
                m_serverNotice->setWordWrap(true);
                layout->addWidget(m_serverNotice);

                m_serverTree = new QTreeWidget(page);
                m_serverTree->setObjectName(serverTreeName());
                m_serverTree->setHeaderLabels({serverNameColumnText(), serverAddressColumnText(),
                                               serverStatusColumnText()});
                // 「一行一个服务器」是平表不是树：不要每行前面那个展开三角。
                m_serverTree->setRootIsDecorated(false);
                m_serverTree->setUniformRowHeights(true);
                m_serverTree->setSelectionMode(QAbstractItemView::SingleSelection);
                m_serverTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
                // ★ 状态那一格是两行（`在线 · 42 ms` / `7/20 人`，见 setServerStatuses）：
                //   这一句是那个换行**真的会换行**的前提 —— 条目代理只在开了 wordWrap 时
                //   才把正文按 QTextLayout 摆，否则那一格里会是"在线 · 42 ms 7/20 人"一行
                //   （`\n` 被当成一个看不见的字符）。第 0 列不受影响：那两行是代理自己画的。
                m_serverTree->setWordWrap(true);

                // ---------- 三栏的宽度怎么分 ----------
                //
                // 默认是"最后一列吃掉多出来的宽度"，那样一来左边两行 MOTD 会被状态那一栏
                // 挤掉一大截，动不动就出省略号（用户原话「保证左侧的 motd 能显示全」）。
                // 改成：**第 0 列把剩下的全吃掉**，另外两列各占自己需要的那点地方 ——
                //   · 地址那一栏是遮过的（`m-.h-.c-`），本来就窄；
                //   · 状态那一栏按内容定宽（两行里更宽的那一行），文字**靠右**，所以整栏
                //     贴着右边停住、不跟着窗口一起长。
                QHeaderView *header = m_serverTree->header();
                if (header != nullptr) {
                    header->setStretchLastSection(false);
                    header->setSectionResizeMode(0, QHeaderView::Stretch);
                    header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
                    header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
                }
                // 表头那一格也跟着靠右，不然「状态」两个字会和它底下的数字错开一个身位。
                m_serverTree->headerItem()->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
                // 名字那一格画成**两行式的行**：前面一枚服务器图标、名字一行、底下小字一行
                // （那就是 MOTD）。和版本树/下载页那几张表是同一个代理 —— 一份实现多处用。
                // 只有第 0 列挂它（另外两列是普通的地址和状态）。
                m_serverTree->setItemDelegateForColumn(
                    0, new VersionTreeDelegate(m_serverTree, /*leafArrow=*/false,
                                               MainWindowUI::serverIconSize()));
                // 悬停底色由代理画（`Theme::rowHoverFill()`），但那一位 `State_MouseOver` 是**视图**填的。
                // ★ 少了这一句，代理里那段一个字都不会生效 —— 不报错、断言也不一定先红，屏幕上就是"悬停没反应"。
                m_serverTree->setMouseTracking(true);
                // 行高**不是**那个常数：这一列逐条 `setSizeHint` 成 `serverRowHeight()`
                // （= versionTreeRowHeight() + 18 = 64，见下面「46 px 装不下」那一句），
                // 这一句是按同一档告诉控件"图标那一格有多大" —— 下载页那几张表也是这么配的
                // （见 MainWindowUI 里 resourceTreeIconSize() 那两行），不配的话条目自带的
                // 图标会被另算一遍尺寸。
                m_serverTree->setIconSize(
                    QSize(MainWindowUI::serverIconSize(), MainWindowUI::serverIconSize()));
                // ★ 地址那一格**右键 = 复制那一串完整的地址**（用户 2026-09-27：「ip地址
                //   右键可以复制」）。走 `Qt::CustomContextMenu` 只是为了拿到"在哪儿按的"
                //   这一个坐标 —— **不弹菜单**：这一页上右键只有这一件事可做，而悬停提示
                //   （serverAddressCopyHint()）已经把这句话告诉用户了。
                //
                // NOTE: 不用 QMenu（更别说 QMenu::exec()）：仓库里那几块浮层一律是
                //       Qt::Popup 手搭的，理由写在 MainWindowUI.hpp 里 folderMenuName()
                //       那段（offscreen 上的验证工程驱动不了 exec()）。
                m_serverTree->setContextMenuPolicy(Qt::CustomContextMenu);
                connect(m_serverTree, &QTreeWidget::customContextMenuRequested, this,
                        [this](const QPoint &pos) {
                            if (m_serverTree == nullptr) {
                                return;
                            }
                            // pos 是**viewport 坐标**（条目视图自己就是这么发的），
                            // itemAt()/columnAt() 要的也正是它。
                            QTreeWidgetItem *item = m_serverTree->itemAt(pos);
                            if (item == nullptr || m_serverTree->columnAt(pos.x()) != 1) {
                                return;
                            }
                            const QString address =
                                item->data(1, MainWindowUI::serverAddressRole()).toString();
                            if (address.isEmpty()) {
                                return;
                            }
                            emit copyServerAddressRequested(address);
                        });
                // ★ 「编辑」和「加入」靠"选中了哪一行"活着（2026-10-10）：选一下重算一次
                //   那两颗的灰/亮。别的什么都不做 —— 这一发只跟那两颗按钮有关。
                connect(m_serverTree, &QTreeWidget::itemSelectionChanged, this,
                        &InstanceSettingsDialog::updateServerSelectionActions);
                // 初值：现在一条都没有 ⇒ 那两颗是灰的。setServerList() 每次换表还会再喊一遍。
                updateServerSelectionActions();
                layout->addWidget(m_serverTree, 1);
                // 展开/折叠动画（2026-09-29，规则见 ui/Animations.hpp）。
                Anim::animateExpansion(m_serverTree);

                auto *hint = new QLabel(filesHint(QStringLiteral("server")), page);
                hint->setObjectName(filesHintName(QStringLiteral("server")));
                hint->setWordWrap(true);
                layout->addWidget(hint);
            }
            // 没被 setServerList() 叫过的时候（验证工程里那些只验结构的构造点）先摆成
            // "还没读到" —— 露出一张空表格比说一句话更像坏了。
            setServerList({}, QString());
        }
    }

    // ---------- 左栏那一列的接线 ----------
    //
    // 左栏一变：换页。就这一件事 —— 从前那条"只有「文件管理」才露出中间那一列"的分支
    // 跟着中栏一起没了（见 .hpp 里 pinnedPageKeys() 那一段）。
    //
    // NOTE: 越界保护的理由和设置页那条一样 —— 左栏的条目和堆叠页要是对不上，宁可停在原来
    //       那页，也不要 setCurrentIndex(-1) 把右栏整片清空（"点一下界面就空了"用户只会
    //       当成崩了）。
    connect(m_pageList, &QListWidget::currentRowChanged, this,
            [this](int row) { applyEntry(row); });
    // 打开就是第一条（概览）—— 左栏亮着哪一条、右栏就是哪一页，两边不能各说各的。
    // 那一列的内容和这一发都由构造末尾的 rebuildEntryList() 办（它是**算出来的**，
    // 见 .hpp 里 pinnedPageKeys() 那一段）。

    // ---------- 灌值 ----------
    // 「有值 = 自定义，没值 = 跟随全局」这一条两边都要对上：磁盘上有一项覆盖时，
    // 重开这个对话框必须停在「自定义」上，否则用户会以为自己的设置丢了。
    setChoiceCustom(javaPathField(), current.javaPath.has_value());
    m_javaPathEdit->setText(current.javaPath.value_or(global.javaPath));

    setChoiceCustom(xmxField(), current.xmx.has_value());
    m_memoryMaxSpin->setValue(current.xmx.value_or(global.xmx));
    // 条那半边也要摆过来：数字框本来就已经是这个值时上面那一句不发信号，光靠同步那条线
    // 等不到（条会停在上一次那一格上）—— 一进这一页看见的条和数字框就不是一个数。
    // 挡信号：这一发是**灌值**，不是用户改的 —— 不挡的话它会顺手报一次"值变了"
    // （条自己接的那条线，见上面那段），而那时候 `m_lastSignature` 还是空的，
    // 于是打开这一页本身就等于替用户写了一次盘。
    {
        const QSignalBlocker blockSlider(m_memoryMaxSlider);
        m_memoryMaxSlider->setValueMb(m_memoryMaxSpin->value());
    }

    setChoiceCustom(xmsField(), current.xms.has_value());
    m_memoryMinSpin->setValue(current.xms.value_or(global.xms));

    setChoiceCustom(autoMemoryField(), current.autoMemory.has_value());
    m_memoryAutoCheck->setChecked(current.autoMemory.value_or(global.autoMemory));

    setChoiceCustom(extraJvmArgsField(), current.extraJvmArgs.has_value());
    m_extraJvmArgsEdit->setText(current.extraJvmArgs.value_or(global.extraJvmArgs));

    // ---------- 窗口模式 / 窗口大小（2026-09-28 加的）----------
    if (m_windowModeCombo != nullptr) {
        // 档位：有覆盖就用覆盖那一档、没有就摆**全局那一档**（那一档这时是禁用的，摆出来的
        // 意义是"你跟随的是这一档"）。认不出来的代号（手改过 meta、或者老版本写的名字）
        // 就退回第 0 项 —— 和 `GlobalValues::windowModeLabel` 那边同一个态度：看不懂的值
        // 不猜它是什么档，摆最保守的那一档（调用方喂进来的第 0 项就是「小窗」，见
        // MainWindow 构造这张表的地方）。
        const QString token = current.windowMode.value_or(global.windowMode);
        const int index = m_windowModeCombo->findData(token);
        {
            // 挡信号：这一发是**灌值**不是用户改的（和上面那条注释同一个理由 —— 不挡的话
            // 打开这一页本身就等于替用户写一次盘，见 notifyEdited() 那条路）。
            const QSignalBlocker block(m_windowModeCombo);
            m_windowModeCombo->setCurrentIndex(index >= 0 ? index : 0);
        }
        setChoiceCustom(windowModeField(), current.windowMode.has_value());
    }
    if (m_windowWidthSpin != nullptr) {
        // 分辨率那两格：**以窗口宽那一格有没有值为准**（它们是同一个决定的两半，见 .hpp
        // 里 override() 那段），值本身各自取覆盖或全局那一份。
        setChoiceCustom(windowWidthField(), current.windowWidth.has_value());
        m_windowWidthSpin->setValue(current.windowWidth.value_or(global.windowWidth));
        m_windowHeightSpin->setValue(current.windowHeight.value_or(global.windowHeight));
    }

    // 每一行下面那句话要等值灌完才刷（它写的是"当前是什么"）。
    //
    // NOTE: 2026-09-28 起上面那一串是 `setChoiceCustom()`，它**自己挡着信号**（档位下拉框的
    //       `currentIndexChanged` 既接 refreshHints 也接写盘那条线，灌值的时候一条都不该
    //       响）—— 所以这一发是这个函数里**唯一**一次算提示的时机，不是"再刷一遍"。
    refreshHints();

    // 图标那一行的初值走 setIcon()（调用方在后头调，见 .hpp 里那段"为什么用 setter"）。
    // 这里先按"默认"画一遍：没被 setIcon() 叫过的时候（验证工程里那些只验结构的构造点）
    // 八颗按钮也得是**八颗都没勾**的样子，而不是停在某个随机状态上。
    refreshIconRow();

    // ---------- 「改了就写」那套 ----------
    //
    // ★ 2026-09-26：**底下那排「取消 / 确定」删了**（用户原话「实例设置里面的那个取消和
    //   确定删了，修改了的设置实时写入」）。于是这一页只剩两条出口，都由覆盖层那一层管：
    //   左上角那颗返回箭头 = 关页（`execPage` 的 reject），Esc 同它。没有"提交"这一下，
    //   也不再有"改了一堆又后悔"—— 改一格就落一格，这是用户点名要的。
    //
    // NOTE: 2026-09-27 起有一个例外：**「配置文件」那一格里改过还没保存的时候**，
    //       `reject()` 会把这一关挡下来先问一句（见那边）。那一格是这一页上唯一一处
    //       "改动没有实时落盘"的地方（它的「保存」是一颗按钮），所以也只有它需要问。
    //
    // NOTE: 删掉那排按钮之后，**这个对话框自己不再 accept**（从前那颗「确定」是
    //       `confirm->setDefault(true)` 的默认按钮）。`execPage` 那边判的是 Accepted /
    //       Rejected，现在永远是后者 —— 而那正是"这一页关掉了"的意思，它不看返回值
    //       做什么（见 MainWindow::openInstanceSettings() 收尾那一段）。
    watchEditors();
    // 左栏那一列灌第一遍（内容和第一页都归它管）。**排在 watchEditors() 之后**：它会在
    // 那一列上挑一条（程序化 setCurrentRow()），接上接线之后才走"选中 → 换页"那条正常路。
    rebuildEntryList();
    // 起点 = 现在这一份（灌值那一串 setChecked/setText 都已经跑完）。不记这一笔的话，
    // 打开页面之后的第一次交互会把"整份值"当成一次改动写一遍盘。
    m_lastSignature = valueSignature();
}

bool InstanceSettingsDialog::leaveBlockedByUnsavedEdits() const {
    // 这两条**就是** reject() 里那两道闸，一个字的第二种写法都不许有（见 .hpp）。
    if (m_configEditor != nullptr && m_configEditor->isDirty()) {
        return true;
    }
    if (m_worldSettings != nullptr && m_worldSettings->isDirty()) {
        return true;
    }
    return false;
}

QStringList InstanceSettingsDialog::droppedFilePaths(const QMimeData *mime) {
    QStringList paths;
    if (mime == nullptr || !mime->hasUrls()) {
        return paths;
    }
    const QList<QUrl> urls = mime->urls();
    for (const QUrl &url : urls) {
        // 只要**本地文件**：`toLocalFile()` 对 http(s) 链接、`data:` 之类一律回空串。
        if (!url.isLocalFile()) {
            continue;
        }
        const QString path = url.toLocalFile();
        // 目录不收（拖文件夹进来要另说一件事：是"把里面所有 jar 都装进去"还是别的），
        // 不存在的东西也不收 —— 半路被人移走了的话，后面那条复制路会报一句莫名其妙的话。
        if (path.isEmpty() || !QFileInfo(path).isFile()) {
            continue;
        }
        paths.append(path);
    }
    return paths;
}

void InstanceSettingsDialog::dragEnterEvent(QDragEnterEvent *event) {
    // 准入判据就一条：拖的是不是**本地文件**（见 droppedFilePaths()）。
    // 不认的话**什么都不做**（不 accept）—— 于是光标是"禁止"那个形状，用户一眼知道
    // 这一页不收它，而不是拖上去松手之后什么都没有发生。
    if (droppedFilePaths(event->mimeData()).isEmpty()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

void InstanceSettingsDialog::dropEvent(QDropEvent *event) {
    const QStringList paths = droppedFilePaths(event->mimeData());
    if (paths.isEmpty()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    // 判断与落盘全在 MainWindow（见 .hpp）。NOTE: 这里**不能**顺手把事件也交给基类 ——
    //       交出去之后父链上再有人接一发，同一个文件会被装两遍。
    emit filesDropped(paths);
}

void InstanceSettingsDialog::reject() {
    // ★ 走这条路的**不是**编辑器自己那颗「返回」（那颗发 backRequested()、由 MainWindow
    //   接到 leaveConfigEditor() 上），而是**覆盖层页眉那颗返回箭头** —— 它接的是
    //   `dialog->reject()`（见 OverlayHost::execPage 里那段），点一下关掉的是**整个
    //   「实例设置」**。而那一格里可能正压着改了一半的东西。
    //
    //   这一层没有覆盖层、弹不出"要丢掉吗"那一句，所以只能**先挡住这一关**、把这一发交
    //   出去（见 .hpp 里 reject() 那一段）。接的人问完，用户答"丢掉"之后拿
    //   `done(QDialog::Rejected)` 关 —— 绕开这儿，不然又回到"问一句、再问一句"。
    if (m_configEditor != nullptr && m_configEditor->isDirty()) {
        emit closeBlockedByUnsavedConfigFile();
        return;
    }
    // ★ 2026-09-28 起有了第二个例外：「世界设置」那一页。那一条的判据在页自己身上
    //   （`WorldSettingsPage::isDirty()`）—— 这一层不去读盘，"改过没有"拿着页自己的
    //   台账对一下就有答案（见那个函数的 NOTE）。
    //
    // NOTE: 两条**分开问、不合并**：两格里同时压着东西的情况不存在（这两格各占一个
    //       QStackedWidget，同一时刻只可能露出其中一格），而合并成一句只会让"丢掉的是
    //       哪一份"变得说不清。
    //
    // ★ 2026-09-29：这两条判据挪进了 `leaveBlockedByUnsavedEdits()`，只因为**还有第二个
    //   人要在做同一件事之前先问一句**（拖进来一个整合包 ⇒ 程序自己发起的那次关页，
    //   见 MainWindow）。两处各写一遍"哪两条算脏"迟早会分岔。这里只把"是哪一格脏"翻成
    //   两条不同的信号 —— 那件事仍然只有这一处知道。
    if (m_configEditor != nullptr && m_configEditor->isDirty()) {
        emit closeBlockedByUnsavedConfigFile();
        return;
    }
    if (m_worldSettings != nullptr && m_worldSettings->isDirty()) {
        emit closeBlockedByUnsavedWorldSettings();
        return;
    }
    QDialog::reject();
}

QString InstanceSettingsDialog::titleText() const {
    return m_titleLabel != nullptr ? m_titleLabel->text() : QString();
}

void InstanceSettingsDialog::setInstanceName(const QString &name) {
    m_instanceId = name;
    if (m_instanceNameEdit == nullptr) {
        return;
    }
    // **不当成"用户改了这一格"**：程序化 `setText()` 本来也不发 editingFinished，这里
    // 也不需要挡什么 —— 这一句只是让控件和"磁盘上现在叫什么"重新对上（改名成功之后是
    // 新名字，失败之后退回老名字）。
    m_instanceNameEdit->setText(name);
}

void InstanceSettingsDialog::setInstanceDisplayName(const QString &displayName) {
    if (m_titleLabel != nullptr) {
        m_titleLabel->setText(titleTextFor(displayName));
    }
}

QString InstanceSettingsDialog::instanceName() const {
    // 现场读控件（和 override() 一样，没有影子状态）：验证工程摆好那一格直接读它。
    // **不 trim**：去掉空格是多一层判断，而"只有空格差别"那一档由调用方（MainWindow）
    // 按 trimmed() 之后的结果比 —— 那是它的事，这里原样给。
    return m_instanceNameEdit != nullptr ? m_instanceNameEdit->text() : QString();
}

void InstanceSettingsDialog::setTitleVisible(bool visible) {
    if (m_titleLabel != nullptr) {
        m_titleLabel->setVisible(visible);
    }
}

// ---------- 分栏（见 .hpp 里 pinnedPageKeys() 上面那一段）----------

QWidget *InstanceSettingsDialog::addPage(const QString &key) {
    // **一页一层滚动区**（不是整页套一层）—— 和设置页的 addSettingsPanel() 同一套写法、
    // 同一条理由（见 MainWindowUI.cpp 里那段）。
    //
    // ★ 2026-09-26 用户报的 bug 就是"反过来"那一版：整页（含左栏那两条导航列）被
    //   OverlayHost 那一层 pageScroll 包着，右栏内容一高，滚轮滚的是**整页** —— 左栏
    //   那两条跟着滚走了，而它们是这个页面的导航。滚轮落在哪一栏就滚哪一栏才是对的。
    //
    // NOTE: 页面自己是 `scroll->widget()`（objectName 还是 panelName(key)）—— 堆叠页里
    //       加进去的仍然是**一页一个控件**，所以那些按 index 认页的代码（filePageFirstIndex()
    //       那套）一位都不动。验证工程按 panelName() 找回来的也还是那一页。
    auto *scroll = new QScrollArea(m_stack);
    scroll->setObjectName(scrollName(key));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *page = new QWidget(scroll);
    page->setObjectName(panelName(key));
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    scroll->setWidget(page);
    m_stack->addWidget(scroll);
    return page;
}

QGridLayout *InstanceSettingsDialog::addForm(const QString &key, QWidget *page) {
    auto *form = new QWidget(page);
    form->setObjectName(formName(key));
    auto *grid = new QGridLayout(form);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(10);
    // 和设置页栏内那一层同一套比例（两边每一行都是上下结构、每格跨两列，所以这一对
    // 份数在两边都只摆表格的形状，见各自那张 grid 上面那段说明）。
    grid->setColumnStretch(0, MainWindowUI::leftColumnStretch());
    grid->setColumnStretch(1, MainWindowUI::rightColumnStretch());
    if (auto *layout = qobject_cast<QVBoxLayout *>(page->layout())) {
        layout->addWidget(form);
        // 表格底下留一段空白：内容少的时候别把它抻开（几行字被拉开行距看着像坏了）。
        layout->addStretch(1);
    }
    return grid;
}

int InstanceSettingsDialog::navListWidth(const QListWidget *list) {
    int width = 0;
    for (int i = 0; i < list->count(); ++i) {
        width = qMax(width, list->sizeHintForColumn(i));
    }
    return width + kNavListExtraWidth;
}

QString InstanceSettingsDialog::pageText(const QString &key) {
    // 两张按下标对齐的表各查一遍（键表是**身份**，别拿文字去反查）。
    const QStringList pinned = pinnedPageKeys();
    const int pinnedIndex = pinned.indexOf(key);
    if (pinnedIndex >= 0) {
        return pinnedPageTexts().value(pinnedIndex);
    }
    const int fileIndex = filePageKeys().indexOf(key);
    if (fileIndex >= 0) {
        return filePageTexts().value(fileIndex);
    }
    return QString();
}

QStringList InstanceSettingsDialog::shownEntryKeys() const {
    // 左栏那一列的**唯一出处**：钉死的那两条 + 七条里"用得上且没被用户勾掉"的。
    // 顺序 = pinnedPageKeys() 在前、optionalPageKeys() 的顺序在后（用户点名要的那个顺序）。
    QStringList out = pinnedPageKeys();
    for (const QString &key : optionalPageKeys()) {
        if (m_pageAvailable.value(key, true) && m_pageOverrides.value(key).toBool(true)) {
            out.append(key);
        }
    }
    return out;
}

QJsonObject InstanceSettingsDialog::pageChoices() const {
    // ★ 存下来的**只可能是 false**（见 .hpp 里那一段）：勾选框只在那一页可用时能勾，而
    //   不可用就没得选 —— 所以这一格是"藏起来的那几页"的名单，不是一份对称的选择表。
    QJsonObject out = m_pageOverrides;
    for (const QString &key : optionalPageKeys()) {
        // 每条先按"这一刻的样子"重算一遍：可用但没勾 ⇒ 记 false；其余（可用且勾着、
        // 或者这条根本用不上）⇒ 一个键都不写。这么写，"勾上"就是把键去掉。
        out.remove(key);
        const QCheckBox *check = m_pageChecks.value(key);
        if (check != nullptr && m_pageAvailable.value(key, true) && !check->isChecked()) {
            out.insert(key, false);
        }
    }
    return out;
}

void InstanceSettingsDialog::setPageChoices(const QJsonObject &stored) {
    // 只收认得出来的键（前向兼容：以后删掉一页之后，盘上那条不该跟着在这儿复活）。
    m_pageOverrides = QJsonObject();
    for (const QString &key : optionalPageKeys()) {
        if (stored.value(key).isBool()) {
            m_pageOverrides.insert(key, stored.value(key).toBool());
        }
    }
    rebuildEntryList();
}

void InstanceSettingsDialog::applyEntry(int row) {
    const QStringList keys = shownEntryKeys();
    if (row < 0 || row >= keys.size()) {
        return;
    }
    applyPageKey(keys.at(row));
}

void InstanceSettingsDialog::applyPageKey(const QString &key) {
    const int index = stackPageIndexOf(key);
    if (index < 0) {
        return;
    }
    // **"进这一页"和"同一页再摆一次"是两件事**：showPage() 那条路里"选中那一条"这一发
    // 底下不该再做一遍。refresh() 重复一次只是白列一遍目录（还好，而且"再摆一次就重新读
    // 一遍盘"本来就是这一页要的），但 serverPageEntered 多发一次是**白连一轮**（用户看到
    // 的是"每次翻页都多等一次超时"）—— 所以挡的是后者。
    // 用户手里碰不到这一位：点同一条时 QListWidget 本来就不发信号（见 .hpp 那条 NOTE）。
    const bool alreadyHere = m_stack->currentIndex() == index;
    m_stack->setCurrentIndex(index);
    // 切到这一页就**当场重列一遍**：打开这个对话框的这段时间里，盘上那些文件夹完全可能
    // 被别的东西动过（游戏正在写日志、用户自己开了个资源管理器）。列目录只有读，很便宜，
    // 而"我看见的和盘上不一样"是这一页最没道理的一种错。
    if (InstanceFilesPage *page = m_filesPages.value(key)) {
        page->refresh();
    }
    // 「点进去就自动测速」（用户 2026-09-27 原话）。**排在 refresh() 之后**：这一页刚露出来
    // 时先有那张表（表里那几行是这一页本来就有的东西），再谈"连上去问一遍"。
    //
    // NOTE: 这一发走的是**键**不是行号 ⇒ 左栏那一列怎么变（勾掉一条、可用性变了）都不影响
    //       它：只要有人摆到「服务器列表」那一页上，它就该发。
    if (key == serverPageKey() && !alreadyHere) {
        emit serverPageEntered();
    }
}

void InstanceSettingsDialog::rebuildEntryList() {
    // 现在摆着的是哪一页（重建之后要把选中还原回去）。堆叠页是唯一那份"现在是哪一页"，
    // 左栏这一列只是它的画 —— 所以从它反查，别自己存一份。
    const QString currentKey = pageKeyAtStackIndex(m_stack->currentIndex());
    const QStringList keys = shownEntryKeys();
    {
        // 重灌这一列时的每一句都是"程序化改控件"，压着信号：选中的还原由下面那一发
        // applyPageKey() 自己办（`setCurrentRow()` 在"本来就亮着这一条"时不发信号，
        // 光靠信号会漏）。
        const QSignalBlocker blocker(m_pageList);
        m_pageList->clear();
        for (const QString &key : keys) {
            auto *item = new QListWidgetItem(pageText(key), m_pageList);
            // 身份进 userData：条目文字是显示内容、会随文案改，不能拿它当身份。
            item->setData(Qt::UserRole, key);
        }
        // ★ 尺寸**每次都重算** —— 这一列的内容是会变的（勾掉一条就少一格），照抄构造时
        //   那份宽高会留下"标签被裁掉"或者"右边空一大截"。行高**交给样式算**
        //   （sizeHintForRow），别写死数字：换字体/换样式就错。和设置页那一列同一条规矩。
        int rowsHeight = 0;
        for (int i = 0; i < m_pageList->count(); ++i) {
            rowsHeight += m_pageList->sizeHintForRow(i);
        }
        m_pageList->setFixedHeight(rowsHeight + 2 * m_pageList->frameWidth() + 2);
        m_pageList->setFixedWidth(navListWidth(m_pageList));
        // 当前那一页还在表里就选回去；不在（被勾掉 / 变不可用 / 本来就没有条目）就是 -1。
        m_pageList->setCurrentRow(keys.indexOf(currentKey));
    }
    refreshPageChecks();

    // 选中那一发要么没发（行号本来就对）、要么压根没有那一条，所以这儿补一次：
    //   · 当前那一页还在表里 ⇒ 按它换一次页（和自己点了那一条是同一条路）；
    //   · 当前是「配置文件」⇒ **什么都不做**：它本来就是一个没有条目的页（用户 2026-09-27
    //     说收起来），重建（比如可用性变了一下）不该把用户从那一页上赶走；
    //   · 其余（那一页被勾掉 / 变不可用了）⇒ 回概览：右栏摆着一页、左栏里却没有它，
    //     是最容易让人以为"点坏了"的一种状态。
    if (keys.contains(currentKey)) {
        applyPageKey(currentKey);
    } else if (currentKey == configPageKey()) {
        return;
    } else {
        m_pageList->setCurrentRow(0);
        if (!keys.isEmpty()) {
            applyPageKey(keys.first());
        }
    }
}

bool InstanceSettingsDialog::eventFilter(QObject *watched, QEvent *event) {
    // ---- 左栏那一列不许因为"拿到焦点"就自己挑一条 ----
    //
    // ★ 这是 Qt 的一个**兜底行为**，不是我们写的：`QAbstractItemView::focusInEvent()` 里
    //   有这么一段（6.10.1 的 qabstractitemview.cpp）——
    //
    //       if (model && !d->currentIndexSet && !currentIndexValid) {
    //           QModelIndex index = moveCursor(MoveNext, ...);   // 第一条
    //           if (index.isValid() && ... && event->reason() != Qt::MouseFocusReason)
    //               selectionModel()->setCurrentIndex(index, NoUpdate);
    //       }
    //
    //   也就是"**视图拿到焦点、而当前条目是无效的** ⇒ 替你选中第一条"。落到这一页上：
    //   「配置文件」是一个**没有左栏条目**的页（见 configPageKey()），摆到它上面时左栏
    //   选中的就是 -1；接下来用户按一下 Tab 进来、或者窗口重新激活（`ActiveWindowFocusReason`
    //   —— 开关一次别的窗口回来就会走到），Qt 就把选中挑到第 0 条上，而这一列那一下
    //   **是翻页**（`currentRowChanged` → applyEntry() → applyPageKey()）⇒ 右栏摆着配置文件，
    //   左栏自己跳回了概览。**没有用户意图的一次翻页**，正是这一整页最没道理的一种错。
    //
    //   鼠标点出来的焦点不在其列（上面那句 `reason() != Qt::MouseFocusReason` 挡着），
    //   所以只有键盘 / 窗口激活这条路会撞上 —— 而它照样是用户会走的路。
    //
    // NOTE: 吃掉这一发只是"不让它自己挑"，**焦点照旧落在它身上**（焦点是 Qt 在发这个事件
    //       之前就设好的）。按一下方向键时 `QListView::moveCursor()` 对"当前条目无效"本来
    //       就有兜底（回第一条），所以键盘一样能走，只是"没碰过它之前什么都不高亮"。
    //       判据带上 `!currentIndex().isValid()`：当前条目有效时 focusInEvent() 什么都不会做，
    //       那种时候这一发没有吃掉的道理。
    if (watched == m_pageList && event->type() == QEvent::FocusIn &&
        !m_pageList->currentIndex().isValid()) {
        return true;
    }
    return QDialog::eventFilter(watched, event);
}

void InstanceSettingsDialog::refreshPageChecks() {
    // 勾选框是**画**，不是用户改的 ⇒ 逐格压着信号摆（`setChecked()` 本来也不发 clicked ——
    // 写盘那条线接的是 clicked，见 watchEditors() —— 这里压一下是"以后万一有人给它接一条
    // 值变化就写盘的线"时的保险；左栏那一列在这儿**不碰**，别顺手把它的信号也压了）。
    for (const QString &key : optionalPageKeys()) {
        QCheckBox *check = m_pageChecks.value(key);
        if (check == nullptr) {
            continue;
        }
        const bool available = m_pageAvailable.value(key, true);
        // ★ 不可用的那一条**不勾且勾不上**（用户原话：「保持对应选项不可勾选的状态」），
        //   原因走 tooltip（「悬停文本解释」）。灰着而不是藏起来：用户得看得见"有这么
        //   一页、这个实例上用不上、为什么"。
        const QSignalBlocker checkBlocker(check);
        check->setChecked(available && m_pageOverrides.value(key).toBool(true));
        check->setEnabled(available);
        check->setToolTip(available ? pageShownHint() : pageUnavailableText(key));
    }
}

// ---------- 文件管理那八页（见 .hpp 里 filePageKeys() 上面那一段）----------

QStringList InstanceSettingsDialog::shortcutTexts() {
    return {tr("版本目录"), tr("存档"),     tr("截图"),     tr("模组"), tr("资源包"),
            tr("数据包"),   tr("光影包"),   tr("结构/原理图"), tr("日志")};
}

QStringList InstanceSettingsDialog::shortcutPathKeys() {
    // 和 shortcutTexts() **逐位对齐**。这些键由 MainWindow 从 setInstanceDir() /
    // setFileDirs() 灌进来（它才知道那些目录在哪儿）。
    return {QStringLiteral("dir"),        QStringLiteral("save"),
            QStringLiteral("screenshots"), QStringLiteral("mod"),
            QStringLiteral("resourcepack"), QStringLiteral("datapack"),
            QStringLiteral("shader"),     QStringLiteral("schematic"),
            QStringLiteral("logs")};
}

QString InstanceSettingsDialog::filesHint(const QString &key) {
    // ★★ 2026-10-10：**模组 / 资源包 / 数据包 / 光影包四页那几条分支已经删掉了**。
    //   用户原话：「把这个页面下方那段叙述删了来让框拉到底充分利用空间」—— 那四页
    //   （＝开着批量管理的那四页，判据 `filesNameOpensResource()`）**压根不建那个 QLabel**
    //   （见构造里建 hint 那一处），于是这几条分支再没有人调得到，留着就是**第二份真相**：
    //   哪天有人照着它们改字，改的是一个屏幕上不存在的东西。
    //   连 `filesDetailHint()`（那句"行尾那颗 ⓘ …按住 Ctrl 能多选"）也一起没了 ——
    //   ★ 顺带说一句：那句话在 2026-10-10 这一天**本来就过期了**（多选换成了
    //   `MultiSelection`，"按住 Ctrl"已经不是加选的办法），删得正是时候。
    //   剩下这几页的小字照旧，一条没动。
    if (key == QStringLiteral("shortcut")) {
        return tr("这几颗只是把文件夹打开 —— 看里面有什么、禁用一个、删掉一个，都在左边那一"
                  "列里对应的那几页（模组 / 资源包 / 光影包…）。下面那几格决定它们露不露出来。");
    }
    if (key == QStringLiteral("schematic")) {
        // ★ 2026-10-10：这一页改名叫「结构/原理图」了（用户原话「那个原理图那个改成
        //   结构/原理图（我记得这两个应该是类似的保存格式？）」），而**"结构"那半边当天
        //   也做上了**：顶上那两排下拉框（上面「存档」、下面「投影」）就是在说这一页
        //   同时列哪两个来源 —— 结构方块存的那些 `.nbt` **跟着存档走**，投影的那些
        //   `.litematic` 在这个实例自己的文件夹里（见 InstanceFilesPage::
        //   setStructureSources()）。这句话得把这两样都说到：只提投影的话，用户看见
        //   上面多出来一排「存档」会以为是自己哪儿点错了。
        return tr("这里两样东西都在：结构方块存下来的（跟着存档走，上面那排挑是哪一个存档）"
                  "和投影模组读的（在这个实例自己的文件夹里）。两样都是压缩过的 NBT，"
                  "改坏了游戏读不出来 —— 这一页只列给你看，不动它们。");
    }
    if (key == QStringLiteral("config")) {
        // 这一句得把"能干什么"和"为什么是这么个界面"都说清楚 —— 用户问的就是后半句
        // （见 ConfigFileEditorPage.hpp 那段：模组把类型和范围声明在代码里，只有游戏进程
        // 拿得到，所以启动器这边做不出勾选框那一套）。
        return tr("点一个文件就能直接改（双击，或者选中之后按回车）。里面按模组原样排版，"
                  "启动器不改它的格式、不动它的注释 —— 看清楚再改，改错了没有撤销。"
                  "文件夹可以点进去。");
    }
    if (key == QStringLiteral("save")) {
        return tr("一个文件夹就是一个存档。启动器不删存档 —— 这里只看得见，要删得自己去"
                  "文件夹里删。");
    }
    if (key == QStringLiteral("server")) {
        // ★★ 2026-10-10：这段话重写过两次。
        //   2026-09-27 起这一页不只是那份文件的一面镜子（图标 / MOTD / 延迟都是**刚问来的**）；
        //   2026-10-10 起它也不只是"只看"了 —— 加 / 改名 / 改地址 / 删都会**写回
        //   servers.dat**（用户原话「编辑要能改名字改地址以及删除，服务器那个页面里放一个
        //   添加服务器的按钮，允许添加」）。
        //   所以原来那句「启动器只读不改」现在是**错的**，必须说清"哪几颗会动盘"：
        //   「刷新」只重读 + 重问，一个字都不写；四颗真正写盘的都在下面点名。
        return tr("这里就是游戏自己的服务器列表（servers.dat），启动器现在能改它了："
                  "「编辑」改这一台的名字和地址，「添加」多一台，「删除」拿掉一台。"
                  "改的只有名字和地址这两样，图标、隐藏之类的设置原样留着。"
                  "「刷新」会重读一遍文件、再连上去问每台现在什么样（图标、那两行字、"
                  "多少人、多少毫秒），只问不写、不进游戏。地址那一格显示的是遮过的，"
                  "在上面按右键能把完整的那个复制下来。");
    }
    return QString();
}

QString InstanceSettingsDialog::filesEmptyText(const QString &key) {
    if (key == QStringLiteral("mod")) {
        return tr("这个实例还没装模组。按上面的「%1」去找一个。").arg(filesDownloadText(key));
    }
    if (key == QStringLiteral("resourcepack")) {
        return tr("这个实例还没有资源包。按上面的「%1」去找一个。").arg(filesDownloadText(key));
    }
    if (key == QStringLiteral("datapack")) {
        return tr("这个实例还没有数据包。按上面的「%1」去找一个。").arg(filesDownloadText(key));
    }
    if (key == QStringLiteral("shader")) {
        return tr("这个实例还没有光影包。按上面的「%1」去找一个。").arg(filesDownloadText(key));
    }
    if (key == QStringLiteral("schematic")) {
        return tr("这个实例还没有结构 / 原理图。");
    }
    if (key == QStringLiteral("config")) {
        // 「还没有」在这里有**两种**原因，都说得出口：这个实例从来没进过游戏（模组还没
        // 跑过一次，也就还没写配置），或者它确实一个能配的模组都没有。第二种更常见 ——
        // 原版客户端本来就没有 config 这个目录。
        return tr("这个实例里还没有配置文件。进一次游戏，模组自己会把默认配置写进来。");
    }
    if (key == QStringLiteral("save")) {
        return tr("这个实例还没有存档。进游戏存一个就有了。");
    }
    if (key == QStringLiteral("server")) {
        return serverEmptyText();
    }
    return QString();
}

QString InstanceSettingsDialog::shaderUnavailableText() {
    return tr("光影包要由 Iris 或者 OptiFine 来加载，这个实例两个都没有 —— 装进去了也不会"
              "生效。先去「下载」页装一个 Iris，或者给这个实例装 OptiFine。");
}

QString InstanceSettingsDialog::schematicUnavailableText() {
    return tr("原理图是 Litematica / 投影模组用的，这个实例没装它 —— 这个文件夹现在没人会读。"
              "先去「下载」页装一个。");
}

QString InstanceSettingsDialog::modUnavailableText() {
    return tr("这个实例是原版的，没有模组加载器（Forge / NeoForge / Fabric / Quilt / "
              "OptiFine），mods 文件夹里的东西游戏根本不会读。");
}

QString InstanceSettingsDialog::datapackUnavailableText() {
    return tr("数据包是 1.13 才有的东西，这个实例的版本比它老 —— 这个文件夹没有人会读。");
}

QString InstanceSettingsDialog::filesDownloadText(const QString &key) {
    // ★ 「光影包」那一页上写的是「下载新光影」（用户原话就是这么说的）—— 这四个字要和
    //   **落地那一栏**对得上：下载页左栏那一栏叫「光影」（MainWindowUI::downloadCategoryTexts()），
    //   而实例设置左边那一列里这一页仍旧叫「光影包」。
    if (key == QStringLiteral("mod")) {
        return tr("下载新模组");
    }
    if (key == QStringLiteral("resourcepack")) {
        return tr("下载新资源包");
    }
    if (key == QStringLiteral("datapack")) {
        return tr("下载新数据包");
    }
    if (key == QStringLiteral("shader")) {
        return tr("下载新光影");
    }
    return QString();
}

QString InstanceSettingsDialog::filesDownloadTip(const QString &key) {
    const QString category = filesDownloadCategoryKey(key);
    if (category.isEmpty()) {
        return QString();
    }
    const int index = MainWindowUI::downloadCategoryKeys().indexOf(category);
    const QStringList texts = MainWindowUI::downloadCategoryTexts();
    if (index < 0 || index >= texts.size()) {
        return QString();
    }
    // 提示里得说清"这一页会关掉"：用户按它是为了**换个地方**，不是就地多出一个东西。
    return tr("切到「下载」页的「%1」那一栏去找新的（这一页会关掉）。").arg(texts.at(index));
}

QString InstanceSettingsDialog::filesDownloadCategoryKey(const QString &key) {
    // 认的就是这四个页键，回的**就是**下载页那一栏的键（见 .hpp 里那张说明：两套键现在
    // 恰好同名，可这张表是"这两处是一回事"的唯一出处）。
    if (key == QStringLiteral("mod") || key == QStringLiteral("resourcepack") ||
        key == QStringLiteral("datapack") || key == QStringLiteral("shader")) {
        return key;
    }
    return QString();
}

bool InstanceSettingsDialog::filesNameOpensResource(const QString &key) {
    // ★ 用户原话（2026-09-27）：「除了模组以外的几个都是点击模组名称自动进入下载界面
    //   （前提是匹配上了，**模组的待会再说**）」—— "待会"就是这一天到了：模组**接上**
    //   （用户：「对于模组，点进去和资源包差不多，但是右上角显示快照版那里替换为模组设置
    //   （先占位）」）。名字点开和资源包三页走的是**同一条路**，唯一的分别落在那一页右上角
    //   那一格上（`MainWindow::DetailSource::InstanceFiles` + 种类是模组 ⇒ 摆「模组设置」，
    //   见 ResourceDetailPage::setModSettingsSlot）。
    //
    //   要接别的页时改的只有这一处（setFileResource() 那个循环和构造里那条 connect 都读它）。
    //
    // NOTE: 原理图 / 存档 / 配置文件不在：前两个在下载页里没有"它自己的那一页"可去
    //       （原理图压根没有那一栏、存档不是一个能下载的资源），配置文件不是资源。
    return key == QStringLiteral("mod") || key == QStringLiteral("resourcepack") ||
           key == QStringLiteral("datapack") || key == QStringLiteral("shader");
}

QString InstanceSettingsDialog::filesResourceTip(const QString &title) {
    if (title.isEmpty()) {
        // 没认出来 ⇒ 没有这一颗（见 setFileResource()）。
        return QString();
    }
    // 和 filesDownloadTip() 同一句话的规矩：**说清这一页会关掉** —— 用户点它是为了换个
    // 地方，不是就地多出一个东西。
    return tr("打开「%1」的下载页（这一页会关掉）。").arg(title);
}

// ★★ `filesDetailHint()` 2026-10-10 **已删**（原来在这儿，是模组 / 资源包 / 数据包三页小字
//    末尾那一句"行尾那颗 ⓘ 能进它自己的那一页…按住 Ctrl 能多选，Ctrl+A 全选"）。
//    删它的直接原因：那几页底下那段叙述整个不挂了（用户："把这个页面下方那段叙述删了来让
//    框拉到底充分利用空间"），没有第二个调用者。
//    ★ **它在删之前就已经是一句错话了**，这才值得记一笔：`Ctrl` 那一天还是加选的办法，
//    而 2026-10-10 同一天多选换成了 `MultiSelection`（点一下切换，一个修饰键都不用）——
//    留着它的话，用户照着「按住 Ctrl」去按，得到的是"这一条翻了个面"（`Toggle` 不看修饰键），
//    也就是 `wtfml-comment-vs-code` 那个老毛病再来一次：**字面声称的那一侧就是洞**。
//    要再挂说明的文字，写的时候**照着 `setBatchMode()` 里那段**写，别照抄记忆里的版本。

QString InstanceSettingsDialog::pageUnavailableText(const QString &key) {
    // **唯一分发处**：页里那句「用不上」和勾选表的悬停提示都走它。
    if (key == QStringLiteral("mod")) {
        return modUnavailableText();
    }
    if (key == QStringLiteral("shader")) {
        return shaderUnavailableText();
    }
    if (key == QStringLiteral("schematic")) {
        return schematicUnavailableText();
    }
    if (key == QStringLiteral("datapack")) {
        return datapackUnavailableText();
    }
    return QString();
}

QString InstanceSettingsDialog::pageShownHint() {
    return tr("勾上：左边那一列里就有这一页；不勾：这一页收起来（里面的东西一个都不会动）。");
}

void InstanceSettingsDialog::setInstanceDir(const QString &dir) {
    // 只是记下来：快捷方式那九颗按钮**点的时候现场读**（见构造函数里那段 NOTE），
    // 所以这里不用重画什么。
    m_instanceDir = dir;
}

void InstanceSettingsDialog::setFileDirs(const QHash<QString, QString> &dirs) {
    m_fileDirs = dirs;
    // ★ `config/` 的根**在这儿**认下来（不是在构造函数里）—— 那会儿这一份还没灌进来，
    //   抓到的永远是空串，"不许越过 config/"那道墙就形同虚设了（见 .hpp 里 m_configRoot
    //   那一 NOTE）。
    m_configRoot = m_fileDirs.value(QStringLiteral("config"));
    // 六页在构造函数里就搭好了，那会儿还不知道目录在哪儿（这一份是构造之后才灌的）——
    // 所以灌完必须让它们各列一遍，否则每一页都停在"这个文件夹还是空的"上。
    const QList<QString> keys = m_filesPages.keys();
    for (const QString &key : keys) {
        if (InstanceFilesPage *page = m_filesPages.value(key)) {
            // setDirectory() 一律回到**根** —— 换了一个实例再回来，不该还停在上一个实例
            // 翻到的那个子目录里（那个路径在新实例上多半根本不存在）。
            page->setDirectory(m_fileDirs.value(key));
        }
    }
    updateConfigUpButton();
}

void InstanceSettingsDialog::openConfigEditor(const QString &path,
                                              const ConfigFileText::Loaded &loaded,
                                              const ConfigFileText::Stamp &stamp) {
    if (m_configStack == nullptr || m_configEditor == nullptr) {
        return;
    }
    m_configEditor->setFile(path, loaded, stamp);
    m_configStack->setCurrentWidget(m_configEditor);
}

void InstanceSettingsDialog::setConfigBackupAvailable(bool available) {
    if (m_configEditor == nullptr) {
        return;
    }
    // NOTE: 顺序要紧 —— `setFile()` 会把这一位清成 false（换了一份文件，上一条答案不作数），
    //       所以这一句必须**排在 openConfigEditor() 之后**。反过来的话，用户看到的是一颗
    //       永远画不出来的「还原上一次」。
    m_configEditor->setBackupAvailable(available);
}

void InstanceSettingsDialog::openWorldSettings(const WorldSettingsPage::WorldView &view) {
    if (m_worldStack == nullptr || m_worldSettings == nullptr) {
        return;
    }
    m_worldSettings->setWorld(view);
    m_worldStack->setCurrentWidget(m_worldSettings);
}

void InstanceSettingsDialog::openWorldSettingsUnreadable(const QString &dirName,
                                                         const QString &reason) {
    if (m_worldStack == nullptr || m_worldSettings == nullptr) {
        return;
    }
    m_worldSettings->setUnreadable(dirName, reason);
    m_worldStack->setCurrentWidget(m_worldSettings);
}

void InstanceSettingsDialog::closeWorldSettings() {
    if (m_worldStack == nullptr) {
        return;
    }
    // 回第 0 格，并让那一页重新列一遍目录 —— 和 closeConfigEditor() 同一条规矩，但
    // **键不一样**（那一个是 `config`，照抄会列错一页、而且屏幕上看不出来：
    // `m_filesPages.value()` 给的是那一页的指针，刷新它一样会"动一下屏幕"）。
    if (InstanceFilesPage *files = m_filesPages.value(QStringLiteral("save"))) {
        files->refresh();
    }
    m_worldStack->setCurrentIndex(0);
}

bool InstanceSettingsDialog::worldSettingsUnreadable() const {
    return m_worldSettings == nullptr || m_worldSettings->isUnreadable();
}

void InstanceSettingsDialog::closeConfigEditor() {
    if (m_configStack == nullptr) {
        return;
    }
    // 回第 0 格，并让那一页**重新列一遍当前目录**：用户待在编辑器那一格的时候，这个目录
    // 可能在外面（「打开文件夹」开出去的那个窗口）被动过，而这一页只在切进来那一下列一次。
    // 重列一次最省事，也一定不会显示一份旧的清单。
    //
    // NOTE: 走的是 `refresh()` 而**不是** `setDirectory()` —— 后者会回到根，而用户可能是从
    //       `config/ftbquests/` 底下点开一个文件的，返回时该还停在那儿。
    if (InstanceFilesPage *files = m_filesPages.value(QStringLiteral("config"))) {
        files->refresh();
    }
    m_configStack->setCurrentIndex(0);
    updateConfigUpButton();
}

void InstanceSettingsDialog::onConfigEntryActivated(const QString &path, bool isDir) {
    if (!isDir) {
        // 文件：**这一层读不了盘**（也没有 OverlayHost，读失败要弹的那一句得有人弹），
        // 所以绕出去让 MainWindow 读了再拿 openConfigEditor() 递回来。
        emit openConfigFileRequested(path);
        return;
    }
    // 目录：**这一层自己就办得了** —— 换一个目录再列一遍，纯显示层的事（不碰盘、不联网，
    // 也不问 MainWindow）。所以"点进去看"这一下是同步的、没有往返。
    if (InstanceFilesPage *files = m_filesPages.value(QStringLiteral("config"))) {
        files->setDirectory(path);
        updateConfigUpButton();
    }
}

void InstanceSettingsDialog::leaveConfigDir() {
    InstanceFilesPage *files = m_filesPages.value(QStringLiteral("config"));
    if (files == nullptr) {
        return;
    }
    const QString current = QDir::cleanPath(files->directory());
    const QString root = QDir::cleanPath(m_configRoot);
    if (root.isEmpty() || current == root) {
        return;
    }
    QDir up(current);
    if (!up.cdUp()) {
        files->setDirectory(root);
        updateConfigUpButton();
        return;
    }
    const QString parent = QDir::cleanPath(up.absolutePath());
    // 到了根的下一层再点「上级」就**停在根上**，不往上走（`config/` 上面是这个实例自己，
    // 再上面是整个 .minecraft —— 走到那儿"这是配置文件"这件事就走丢了）。
    files->setDirectory(parent.startsWith(root + QLatin1Char('/')) ? parent : root);
    updateConfigUpButton();
}

void InstanceSettingsDialog::updateConfigUpButton() {
    if (m_configUp == nullptr) {
        return;
    }
    InstanceFilesPage *files = m_filesPages.value(QStringLiteral("config"));
    if (files == nullptr) {
        m_configUp->setVisible(false);
        return;
    }
    const QString root = QDir::cleanPath(m_configRoot);
    const QString current = QDir::cleanPath(files->directory());
    // 两个条件缺一不可：根认出来了（setFileDirs 来过），而且现在**在根以下**。
    // 目录还没接上（构造那一会儿）、或者 config/ 根本不存在（原版实例）时，两边都是
    // 空串 —— 那时候也不该画这颗按钮。
    m_configUp->setVisible(!root.isEmpty() && current != root &&
                           current.startsWith(root + QLatin1Char('/')));
}

void InstanceSettingsDialog::setShaderUsable(bool usable) {
    setPageUsable(QStringLiteral("shader"), usable);
}

void InstanceSettingsDialog::setSchematicUsable(bool usable) {
    setPageUsable(QStringLiteral("schematic"), usable);
}

void InstanceSettingsDialog::setModUsable(bool usable) {
    // 「模组」那一页没有"用不上"这个换页状态可言 —— 它背后照样是一个普通的
    // InstanceFilesPage（mods/ 目录），只是原版实例装了也没人读。所以这一条只影响
    // "左栏里有没有它、勾选框能不能勾"，页本身照旧（用户把它勾出来的话看的是真目录）。
    setPageUsable(QStringLiteral("mod"), usable, /*markPage=*/false);
}

void InstanceSettingsDialog::setDatapackUsable(bool usable) {
    // 和「模组」同理：老版本上 datapacks/ 这个目录没人读，但页本身还是那个文件列表。
    setPageUsable(QStringLiteral("datapack"), usable, /*markPage=*/false);
}

void InstanceSettingsDialog::setPageUsable(const QString &key, bool usable, bool markPage) {
    if (markPage) {
        // 页里那句「用不上」是**整页替换**：这一页在这个实例上用不上时，列表和那排按钮
        // 一起收起来，换成一句为什么（判据在 MainWindow，见 .hpp 里 setModUsable() 那段）。
        if (InstanceFilesPage *page = m_filesPages.value(key)) {
            page->setUnavailable(usable ? QString() : pageUnavailableText(key));
        }
    }
    m_pageAvailable.insert(key, usable);
    // 可用性一变，左栏那一列（有没有这一条）和勾选框（能不能勾、勾没勾、tooltip）都要
    // 跟着变 —— 两件事都在 rebuildEntryList() 里。
    rebuildEntryList();
}

QTreeWidgetItem *InstanceSettingsDialog::selectedServerItem() const {
    if (m_serverTree == nullptr) {
        return nullptr;
    }
    // 单选（`setSelectionMode(SingleSelection)`，见构造里那一句）⇒ currentItem() 就是那一条。
    // 走 currentItem() 而不是 selectedItems().first()：前者在"选中了又取消"之后**会自己
    // 回空**，后者拿到的是一个空表 —— 用一个空表的 first() 是越界。
    return m_serverTree->currentItem();
}

void InstanceSettingsDialog::updateServerSelectionActions() {
    if (m_serverEdit == nullptr || m_serverJoin == nullptr || m_serverDelete == nullptr) {
        return;
    }
    const bool hasRow = selectedServerItem() != nullptr;
    // 「添加」**不在这儿** —— 它不依赖选中行（要加的那一台还没出现在表里），常亮。
    m_serverEdit->setEnabled(hasRow);
    m_serverDelete->setEnabled(hasRow);
    m_serverJoin->setEnabled(hasRow);
    // 灰着的时候悬停提示换成"先说清为什么灰"—— 一颗点不动的按钮上写着"改选中的那一台
    // 的名字和地址"，用户会以为自己点错了地方。
    m_serverEdit->setToolTip(hasRow ? serverEditTip() : serverSelectFirstHint());
    m_serverDelete->setToolTip(hasRow ? serverDeleteTip() : serverSelectFirstHint());
    if (!m_serverJoinTip.isEmpty()) {
        m_serverJoin->setToolTip(hasRow ? m_serverJoinTip : serverSelectFirstHint());
    }
}

void InstanceSettingsDialog::setServerJoinText(const QString &text, const QString &tip) {
    if (m_serverJoin == nullptr) {
        return;
    }
    // 空文字 = 收起来（见 .hpp 那条 NOTE）：这一个版本没有 `--quickPlayMultiplayer`，
    // 露着它等于给用户摆一个"按下去游戏立刻报错退出"的键。
    m_serverJoin->setText(text);
    m_serverJoin->setVisible(!text.isEmpty());
    m_serverJoinTip = text.isEmpty() ? QString() : tip;
    // 提示和灰/亮都跟着"有没有选中"走 —— 复用它，别在这儿再写一遍那套判断。
    updateServerSelectionActions();
}

void InstanceSettingsDialog::setServerList(const QList<QPair<QString, QString>> &servers,
                                           const QString &problem) {
    if (m_serverTree == nullptr) {
        return;
    }
    // 这一份是**换掉整张表**：那些"现在什么样"配的是旧的行，一起清掉（见 .hpp 那条 NOTE）。
    m_serverStatuses.clear();
    m_serverStatuses.resize(servers.size());

    m_serverTree->clear();
    for (const QPair<QString, QString> &server : servers) {
        auto *item = new QTreeWidgetItem(m_serverTree);
        item->setText(0, server.first.isEmpty() ? unnamedMark() : server.first);
        // 那一格里**真正**的名字（可以是空串）。屏幕上空名字写的是「（无名）」——那是我们
        // 编的一句话，「编辑」那颗要预填的是文件里原本那一串（见 MainWindowUI::serverNameRole()）。
        item->setData(0, MainWindowUI::serverNameRole(), server.first);
        // 地址那一栏写**遮过的**（`m-.h-.c-`），完整的那一串只在悬停时给（见
        // maskedServerAddress 那段）。左边那两行 MOTD 因此能多占一大截宽度 —— 用户原话
        // 「保证左侧的 motd 能显示全」。
        item->setText(1, maskedServerAddress(server.second));
        // 那一串**完整的**地址挂在这一格上：屏幕上写的是遮过的，而右键要复制的是真的那一串
        // （见 MainWindowUI::serverAddressRole()）。悬停提示给的是同一串 + 一句"右键能复制"
        // ——用户要的"这一点在悬停文本上也告知"就是后半句（serverAddressCopyHint()）。
        item->setData(1, MainWindowUI::serverAddressRole(), server.second);
        item->setToolTip(1, server.second.isEmpty()
                                ? QString()
                                : server.second + QLatin1Char('\n') + serverAddressCopyHint());
        // 状态那一格**一开始就有字**（「还没测」）。空着和"测了没通"在屏幕上是同一个样子，
        // 而这两件事对用户完全不是一回事（一个是"点一下测速"、一个是"这台死了"）。
        item->setText(2, serverUntestedText());
        // 靠右（和 setServerStatuses 里那几行同一条规矩）。每一行都要写一次 —— 这事
        // 记在**条目**上，不是记在列上。
        item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        // 行高**比别处那张表高一档**：这一行底下那一格画的是 MOTD，而 MOTD 是**两行**
        // （游戏里就是两行，见 MotdText::parse 的 maxLines）。46 px 装不下"标题 + 两行小字"。
        // 走 `Qt::SizeHintRole`（也就是 setSizeHint 这一句）交给代理 —— 它学会了读这一格
        // （见 VersionTreeDelegate::sizeHint 那段），在这之前代理一律返回常数 46。
        // NOTE: 这一页开着 uniformRowHeights(true)，所以**每一条都得是同一个高度**
        //       （少设一条，那一行就是 46，屏幕上看着像"这一行矮了一截"）。
        item->setSizeHint(0, QSize(0, MainWindowUI::serverRowHeight()));
    }
    // ★「读不动」和「读到了但一条都没有」是**两句不同的话**（和版本树那条"问到了就是
    //   没有" / "列表没取到"同一条规矩）：说成一句的话，用户会照着错的那句去修 ——
    //   一边是"我确实没加过服务器"，另一边是"你的 servers.dat 坏了，加多少条都白搭"。
    const bool readable = problem.isEmpty();
    const bool empty = servers.isEmpty();
    m_serverTree->setVisible(readable && !empty);
    m_serverNotice->setVisible(!readable || empty);
    m_serverNotice->setText(!readable ? problem : (empty ? serverEmptyText() : QString()));
    // ★ 换掉整张表 ⇒ 选中没了 ⇒ 「编辑 / 加入」那两颗回到灰的（见 updateServerSelectionActions()）。
    //   `clear()` 会发一次 itemSelectionChanged，但那一发**不能指望**：表本来就是空的时候
    //   （第一次灌）clear() 什么都不发。所以在这儿明喊一次。
    updateServerSelectionActions();
}

QStringList InstanceSettingsDialog::serverStatusLines(const ServerRowStatus &status) {
    if (!status.tested) {
        return {serverUntestedText()};
    }
    // ★★ 2026-09-27 用户原话：「‘上次’两个字也不要」。那一句过去时的前缀**整个删掉**
    //    （连 `serverCachedPrefix()` 一起没了）—— 这一格从现在起**只有一种写法**，缓存里
    //    那一份和当场测出来那一份说同一句话。（前一批加的那一位 `Entry::timedOut` 得留着：
    //    正因为没了前缀，"超时"和"没连上"必须是**同一个判据**得出的同一句字，不然同一台
    //    服务器前后两次进这一页看见的还是两个说法。）
    if (!status.online) {
        // 缓存里"没连上"那一档**不重说理由**：那句话是**当时**的（"连接被拒绝"指的是上一次
        // 那一刻），搬到现在来说等于替这一次下结论。就写结论，理由等真的测完那一趟。
        //
        // NOTE: 这一支和下面"当场测出来"那一支**看着几乎一样**，可别合并：这一支只认
        //       `timedOut` 这一位（盘上只存了这一位，见 ServerStatusCache::Entry），而下面
        //       那一支拿得到 `error` 那句全话。
        if (status.fromCache) {
            return {status.timedOut ? serverTimeoutText() : QStringLiteral("没连上")};
        }
        // ★ 超时那一档**只写两个字**（用户 2026-09-27：「超时的只写超时」）。判据是
        //   `Status::timedOut` 那一位，不是这句 error 的前缀 —— 「网络不通」和「它连上了却
        //   不答话」在 ServerPing 里是两处不同的判定（见那边 isTimeoutError()），只是对这个
        //   格子来说结论是同一个：现在连不上。
        if (status.timedOut) {
            return {serverTimeoutText()};
        }
        // 其余没通的**理由来自 ServerPing**（中文，见那边的 connectionErrorText）。真要是
        // 空的也别留一格空白 —— 那就是"看着像没测过"。没通这一档**只有一行**：那句话本来
        // 就长（「没有回应（对面没开，或者端口不对）」），拆成两行反而更难读。
        return {status.error.isEmpty() ? QStringLiteral("连不上") : status.error};
    }
    // 通了就是两行：第一行延迟、第二行几个人。人**不并进第一行**（那是另一件事，而且这一格
    // 窄），这正是用户说的"在线延迟和人数换到两行来"。
    //
    // ★ 「在线」那两个字 2026-09-27 傍晚删了（用户原话：「把服务器那个 在线 文本删了，
    //   只保留延迟和人数」）：`42 ms` 摆在那儿本来就说明它通着，那两个字占的是这一栏的宽度。
    QStringList lines;
    if (status.latencyMs >= 0) {
        lines.append(QStringLiteral("%1 ms").arg(status.latencyMs));
    }
    if (status.playersOnline >= 0 && status.playersMax >= 0) {
        lines.append(QStringLiteral("%1/%2 人").arg(status.playersOnline).arg(status.playersMax));
    } else if (status.playersOnline >= 0) {
        lines.append(QStringLiteral("%1 人").arg(status.playersOnline));
    }
    if (lines.isEmpty()) {
        // 通了、但一个数都没量到。**这一档生产里到不了**（ok 为真时 latencyMs 必然是量过的，
        // 见 ServerPing.cpp），可这一格里空着一行和"还没测"在屏幕上是同一个样子 —— 留一句
        // 兜底，也正好是「在线」那两个字唯一还说得通的地方。
        lines.append(QStringLiteral("在线"));
    }
    // 通了这一档**两处（缓存 / 当场）写的是同一份字**，没有任何前缀可加（见上面那段）。
    return lines;
}

QString InstanceSettingsDialog::serverStatusText(const ServerRowStatus &status) {
    return serverStatusLines(status).join(QStringLiteral(" · "));
}

QString InstanceSettingsDialog::maskedServerAddress(const QString &address) {
    const QString trimmed = address.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    // ---------- ① 把"要遮的那个主机"和"照抄的尾巴"分开 ----------
    //
    // 方括号那一档（`[2001:db8::1]:25566`）**方括号要留着**，这一格和连接那一步是两回事：
    // 不带方括号的 IPv6 后面又跟端口时，哪几个冒号是地址、哪一个是端口分不出来
    // （ServerPing::splitAddress 里那条"看不懂就不猜"的规矩就是为这个）。其余情况按
    // **最后一个**冒号分 —— 而那个冒号左边要是还带着冒号，那这串本来就是裸的 IPv6，
    // 没有端口可拆。
    bool bracketed = false;
    QString host = trimmed;
    QString tail;  // 端口那一截（**带**冒号），没有就是空
    if (trimmed.startsWith(QLatin1Char('['))) {
        const int close = trimmed.indexOf(QLatin1Char(']'));
        if (close > 0) {
            bracketed = true;
            host = trimmed.mid(1, close - 1);
            tail = trimmed.mid(close + 1);
        }
    } else {
        const int colon = trimmed.lastIndexOf(QLatin1Char(':'));
        if (colon > 0 && !trimmed.left(colon).contains(QLatin1Char(':'))) {
            host = trimmed.left(colon);
            tail = trimmed.mid(colon);
        }
    }

    // ---------- ② 这一截是"名字"还是"数字地址" ----------
    //
    // 两种要遮的东西不是一回事，所以判据也不是一条。带冒号的（`[::1]` 是这一支）和"正好
    // 四段、段段是数字"的按数字地址处理 —— 数字地址留头两段就够认出网段了，剩下的打掉。
    // 别的一律当域名：域名要遮的是**那个名字**。
    const QChar separator = host.contains(QLatin1Char(':')) ? QLatin1Char(':') : QLatin1Char('.');
    const QStringList groups = host.split(separator);
    bool numeric = separator == QLatin1Char(':');
    if (!numeric && groups.size() == 4) {
        numeric = true;
        for (const QString &group : groups) {
            if (group.isEmpty() || group.size() > 3) {
                numeric = false;
                break;
            }
            for (const QChar character : group) {
                if (!character.isDigit()) {
                    numeric = false;
                    break;
                }
            }
            if (!numeric) {
                break;
            }
        }
    }

    QStringList masked;
    if (numeric) {
        for (int i = 0; i < groups.size(); ++i) {
            if (i < 2) {
                masked.append(groups.at(i));
            } else {
                // 空的那一段保持空 —— 数字地址里那个空段就是 `::`（压缩写法）本身，
                // 填成 `*` 的话 `[2001:db8::1]` 会遮成 `[2001:db8:*:*]`：四个段、一个
                // 双冒号都没有，看着像另一个地址。留着空段，遮出来是 `[2001:db8::*]`。
                masked.append(groups.at(i).isEmpty() ? QString() : QStringLiteral("*"));
            }
        }
    } else {
        for (const QString &group : groups) {
            // 空的那一段保持空（`a..b` 这种坏地址不该被我们连成 `a-.b-` 那样的假名字）
            masked.append(group.isEmpty() ? QString() : group.left(1) + QStringLiteral("-"));
        }
    }

    QString out = masked.join(separator);
    if (bracketed) {
        out = QStringLiteral("[") + out + QStringLiteral("]");
    }
    return out + tail;
}

void InstanceSettingsDialog::setServerStatuses(const QList<ServerRowStatus> &statuses) {
    if (m_serverTree == nullptr) {
        return;
    }
    // 行数是"盘上那份 servers.dat"说了算，状态是"刚问回来的那一趟"说了算 —— 两边对不上
    // 时**按行来写**：多出来的状态丢掉，行多出来的那些回到「还没测」（不是留着上一次那几句
    // ——那些话配的是上一份列表的行）。两条都不越界。
    m_serverStatuses = statuses;
    const int rows = m_serverTree->topLevelItemCount();
    for (int i = 0; i < rows; ++i) {
        QTreeWidgetItem *item = m_serverTree->topLevelItem(i);
        if (item == nullptr) {
            continue;
        }
        const ServerRowStatus status = i < statuses.size() ? statuses.at(i) : ServerRowStatus();
        // 图标走 `Qt::DecorationRole`（就是 setIcon 那一格）—— 代理读的正是它；
        // 第二行小字走 rowSubTextRole()（版本树上放的是发布时间，这里放 MOTD）。
        item->setIcon(0, status.icon);
        item->setData(0, MainWindowUI::rowSubTextRole(), status.motd);
        // ★ MOTD 的**原样**（带 `§` 码和换行）走另一个角色：代理按它分色、分两行画
        //   （见 VersionTreeDelegate 里 rowMotdRawRole() 那一段）。两个都写 —— 代理那边
        //   是"有原串就画原串、没有才退回洗过那一行"，而 tooltip 用的永远是洗过那一份。
        item->setData(0, MainWindowUI::rowMotdRawRole(), status.motdRaw);
        // ★ 状态那一格是**两行**（`在线 · 42 ms` / `7/20 人`）：`\n` 在这里不是排版上的
        //   小把戏 —— 控件是开了 wordWrap 的，条目代理照 QTextLayout 摆，一个换行就是
        //   真的一行（见建表那段）。悬停提示仍旧给**单行**那份：提示框里没有"两行"这回事，
        //   它要的是一句话。
        item->setText(2, serverStatusLines(status).join(QLatin1Char('\n')));
        item->setToolTip(2, serverStatusText(status));
        item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        // 名字单独给一句 tooltip：MOTD 那一行装不下时是被代理省略掉的，而这一行**没有**
        // 别的地方能看全（那一格只画得下一行）。**无条件写**（MOTD 是空串时这一句就是把
        // 上一次那行字抹掉）—— 不然"上一次那台有 MOTD、这一台没有"会留下一句别人的话。
        item->setToolTip(0, status.motd);
    }
}

void InstanceSettingsDialog::setServerPinging(bool pinging) {
    // ★ 2026-10-10：文字落在**「刷新」**那颗上（「测速」已经并进它了，见构造里那段）。
    if (m_serverRefresh == nullptr) {
        return;
    }
    m_serverRefresh->setText(pinging ? serverPingingText() : serverRefreshText());
    // 连点三下发三遍请求是用户没要的，而且每一遍都要等满超时。
    m_serverRefresh->setEnabled(!pinging);
}

void InstanceSettingsDialog::refreshFilePages() {
    const QList<InstanceFilesPage *> pages = m_filesPages.values();
    for (InstanceFilesPage *page : pages) {
        if (page != nullptr) {
            page->refresh();
        }
    }
}

void InstanceSettingsDialog::setFileUpdates(const QStringList &outdatedPaths, bool checked) {
    // 只有**开得了「可更新」那一格**的那几页接（判据逐字同构造里那一条：下载页里真有
    // 对应一栏的那四页）。别的页上那一格根本不在，喂给它只是让它白白记着一份用不上的
    // 名单（而有名单就会有"下一次刷新之后它还在"这类问题）。
    for (auto it = m_filesPages.constBegin(); it != m_filesPages.constEnd(); ++it) {
        if (it.value() == nullptr || filesDownloadText(it.key()).isEmpty()) {
            continue;
        }
        it.value()->setUpdateResult(outdatedPaths, checked);
    }
}

void InstanceSettingsDialog::setFileIcon(const QString &path, const QPixmap &icon) {
    const QList<InstanceFilesPage *> pages = m_filesPages.values();
    for (InstanceFilesPage *page : pages) {
        if (page != nullptr) {
            // 返回值**不看**：不在这一页的那一条本来就会回 false，而它在那六页里必然
            // 只有一页有（六个目录互不重叠）。找不着才是要说话的那一档，而那一档由
            // "图标是异步回来的、这一条可能已经被删了"解释掉了 —— 什么都没有要做的。
            page->setIconForPath(path, icon);
        }
    }
}

InstanceFilesPage *InstanceSettingsDialog::filesPage(const QString &key) const {
    return m_filesPages.value(key);
}

void InstanceSettingsDialog::setFileResource(const QString &path, const QString &title) {
    // ★ 走**带键**的遍历（不像 setFileIcon() 那样 `values()` 一把梭）：哪几页接这件事是
    //   有讲究的（见 filesNameOpensResource()）—— 同一份路径当然只会在一个目录里，
    //   所以"过滤掉哪几页"这件事靠的不是路径，而是**规矩**：不在 filesNameOpensResource()
    //   那张表里的页连 tip 都不会喂给它（于是那一行连链接的样子都不会有）。
    const QString tip = filesResourceTip(title);
    for (auto it = m_filesPages.constBegin(); it != m_filesPages.constEnd(); ++it) {
        if (it.value() == nullptr || !filesNameOpensResource(it.key())) {
            continue;
        }
        // 返回值**不看**，理由同 setFileIcon()：不在这页的那一条本来就会回 false，而
        // "盘上已经没有这个文件了"那一档由"下一趟刷新会把它抹掉"解释掉了。
        // 空 tip 也要往下走 —— 那是"把上一趟戴上的链接摘掉"（同一个路径换了内容、
        // 认不出来了；这一趟的负结果得覆盖上一趟的正结果）。
        it.value()->setResourceForPath(path, tip);
    }
}

// ---------- 概览栏那三行（见 .hpp 里 setIcon() 上面那一段）----------

QStringList InstanceSettingsDialog::iconCandidateTexts() {
    // 顺序与 VersionIcons::iconCandidateKeys() 一一对应（前四个是版本分类那四张，后四个
    // 是四个加载器）。名字和版本树 / 安装页上用的是同一批词。
    return {tr("草方块"), tr("覆雪草块"), tr("土块"), tr("幸运方块"),
            tr("Fabric"), tr("Forge"), tr("NeoForge"), tr("OptiFine")};
}

QString InstanceSettingsDialog::ungroupedGroupText() {
    // 转手给 MainWindowUI —— 那一句话的唯一出处在那儿（实例树那边也是同一个词）。
    return MainWindowUI::ungroupedGroupText();
}

int InstanceSettingsDialog::iconPreviewSize() { return kIconPreviewSize; }

void InstanceSettingsDialog::setIcon(const QString &icon, const QPixmap &defaultMark,
                                     const QPixmap &localMark) {
    m_iconChoice = icon;
    m_defaultIconMark = defaultMark;
    m_localIconMark = localMark;
    // 初值不是"本地那张"就把那一份丢掉：留着它只会让人以为挑过了。
    if (m_iconChoice != localIconValue()) {
        m_pickedIconPath.clear();
    }
    refreshIconRow();
}

void InstanceSettingsDialog::setGroupChoices(const QList<QPair<QString, QString>> &groups,
                                             const QString &currentGroupId) {
    if (m_groupMenu == nullptr) {
        return;
    }
    auto *box = qobject_cast<QVBoxLayout *>(m_groupMenu->layout());
    if (box == nullptr) {
        return;
    }
    // 浮层里那几行**整个重建** —— 行数是照这份清单来的（用户在「分组设置」里加组 / 删组
    // 之后再调一次这里，旧的几行就没意义了）。
    //
    // NOTE: **当场 delete，不是 deleteLater**。悬着的那几行和新的几行**同名**（objectName
    //       按下标给），而按名字找控件的验证工程会先撞上旧那个 —— 那个窗口只在"事件循环
    //       转起来之前"存在，短到没法调试。代价是这一招**不能在一行自己的槽里调**
    //       （那一行还站在栈上）：产品里只有 MainWindow 在 execPage() 之前调一次。
    for (QPushButton *row : m_groupRows) {
        delete row;
    }
    m_groupRows.clear();
    m_groupIds.clear();

    const auto addRow = [this, box](const QString &text, const QString &groupId) {
        auto *button = new QPushButton(text, m_groupMenu);
        // 名字按下标给（0 起 = 递进来的顺序）：验证工程按它取控件，而组名是用户起的、
        // 随时会变，拿文字当身份必错（和实例页那块「挑目标」浮层同一条）。
        button->setObjectName(groupMenuRowName(m_groupRows.size()));
        button->setStyleSheet(menuEntryStyle(m_groupMenu->palette()));
        button->setCursor(Qt::PointingHandCursor);
        const int index = m_groupRows.size();
        connect(button, &QPushButton::clicked, this, [this, index]() { chooseGroup(index); });
        box->addWidget(button);
        m_groupRows.append(button);
        m_groupIds.append(groupId);
    };
    // 「未分组」排在最前：它是这一列的"没有归属"，和别的组不是一类东西，摆在末尾会让人
    // 以为它也是一个能排序的组。
    addRow(ungroupedGroupText(), QString());
    for (const QPair<QString, QString> &group : groups) {
        addRow(group.first, group.second);
    }
    // 当前值按 **组 id** 找（组名会变），找不到就停在第一项「未分组」—— 那一组刚被删掉的
    // 实例自然落到未分组，和实例树那边同一条规矩，不为它写清理代码。
    const int index = m_groupIds.indexOf(currentGroupId);
    m_chosenGroupIndex = index >= 0 ? index : 0;
    refreshGroupMenuState();
}

void InstanceSettingsDialog::setPinned(bool pinned, const QString &pinnedGroupId) {
    m_pinnedGroupId = pinnedGroupId;
    if (m_pinCheck == nullptr) {
        return;
    }
    // 挡掉 toggled。**这一版没有谁接那一发了**（旧版接的是 applyPinState()），留着这层
    // 挡板是为了"以后谁接上"的时候两条路不会各摆一份状态 —— 和 refreshHints() 那两处
    // 同一个套路。
    const QSignalBlocker blocker(m_pinCheck);
    m_pinCheck->setChecked(pinned);
    // 这一份 .minecraft 里没有置顶组（用户把它删了）：勾上这一下真的没有去处
    // （MainWindow::pinInstance() 直接返回）⇒ **灰掉它并说清为什么**。灰着不说话是这一层
    // 另一条明令禁止的（用户只会以为界面坏了），所以 tooltip 必须给上。
    m_pinCheck->setEnabled(!pinnedGroupId.isEmpty());
    m_pinCheck->setToolTip(
        pinnedGroupId.isEmpty()
            ? tr("这一份 .minecraft 里没有置顶组。先去实例页左栏的「分组设置…」里勾一个。")
            : QString());
}

void InstanceSettingsDialog::chooseGroup(int index) {
    if (index < 0 || index >= m_groupIds.size()) {
        return;
    }
    m_chosenGroupIndex = index;
    refreshGroupMenuState();
    // 挑完就收（和图标那个浮层同一条：它是"选一个"，不是"切换一个开关"）。
    if (m_groupMenu != nullptr) {
        m_groupMenu->close();
    }
    // ★ 这一发是**自己补的**：浮层里那几行是 setGroupChoices() 每次重建出来的，构造
    //   函数跑 watchEditors() 的时候它们还不存在（按类型的线接不到以后才建的控件）。
    //   反正这一个动作只有一个入口，接在这儿比"每次重建再补一遍接线"直白。
    notifyEdited();
}

void InstanceSettingsDialog::refreshGroupMenuState() {
    for (int i = 0; i < m_groupRows.size(); ++i) {
        const bool current = i == m_chosenGroupIndex;
        // 当前那一行**灰掉**：点它等于什么都没做，而"什么都没做"和"点了没反应"在用户
        // 眼里是一回事。这是实例页那块「挑目标」浮层早就定下的规矩（那边叫
        // `containsSelected`），两处的措辞也照抄：灰着必须配一句为什么。
        m_groupRows.at(i)->setEnabled(!current);
        m_groupRows.at(i)->setToolTip(current ? tr("已经在这一组里了")
                                              : tr("把这个实例放到这一组里"));
    }
    if (m_groupButton == nullptr) {
        return;
    }
    // 按钮上那几个字是**固定的**（用户原话就是「分组...（按钮）」），所以"现在归在哪一组"
    // 只能走 tooltip —— 不写这一句的话，用户不点开就完全看不出当前值（那个下拉框被换掉
    // 之前的唯一用处就是这个）。
    const QString name = m_chosenGroupIndex >= 0 && m_chosenGroupIndex < m_groupRows.size()
                             ? m_groupRows.at(m_chosenGroupIndex)->text()
                             : ungroupedGroupText();
    m_groupButton->setToolTip(tr("现在归在「%1」里。点开可以换一组。").arg(name));
}

void InstanceSettingsDialog::popUpMenuBelow(QWidget *menu, const QWidget *anchor) {
    if (menu == nullptr || anchor == nullptr) {
        return;
    }
    // 只有 MenuPopup 知道怎么摆（贴着按钮下边缘、超出屏幕往里收）。这一句是"两个成员在
    // .hpp 里是 QWidget*"和"那个类是 .cpp 里的实现细节"之间的桥。
    //
    // NOTE: 用 dynamic_cast 而**不是** qobject_cast：MenuPopup 是这个 .cpp 里的一个
    //       anonymous-namespace 类，**没有 Q_OBJECT**（它不需要信号槽），而 qobject_cast
    //       要求那个宏（编不过：static_assert HasQ_OBJECT_Macro）。给它补上 Q_OBJECT 是
    //       另一条路，代价是这个 .cpp 结尾要多一行 `#include "...moc"` —— 全仓库没有第二处
    //       这么干的，为一次向下转型不值得。dynamic_cast 认的是 C++ 的类型系统，够用。
    auto *popup = dynamic_cast<MenuPopup *>(menu);
    // 走不到：那两个成员在构造函数里各被赋值一次、都是 MenuPopup。写成断言而不是"没有就
    // 什么都不做"—— 后者是"点了没反应"，而这一页上每一处"点了没反应"都得配一句话。
    Q_ASSERT(popup != nullptr);
    if (popup != nullptr) {
        popup->popUpBelow(anchor);
    }
}

QString InstanceSettingsDialog::iconChoice() const { return m_iconChoice; }

QString InstanceSettingsDialog::pickedIconPath() const { return m_pickedIconPath; }

QString InstanceSettingsDialog::chosenGroupId() const {
    if (m_chosenGroupIndex < 0 || m_chosenGroupIndex >= m_groupIds.size()) {
        // 还没灌过候选（setGroupChoices() 没调过）⇒ 空串 = 未分组。调用方（MainWindow）
        // 拿它和"打开对话框时那一格"比，两边都是空串时结论是"用户没改过"，正确。
        return QString();
    }
    return m_groupIds.at(m_chosenGroupIndex);
}

bool InstanceSettingsDialog::pinned() const {
    return m_pinCheck != nullptr && m_pinCheck->isChecked();
}

QString InstanceSettingsDialog::actionsHint() {
    // 两句各自还有一份，落在那儿（tooltip）和这儿的是**同一句话的两个出口** —— 不是
    // 抄了两份。要改就改上面那两个。
    return exportScriptHint() + QLatin1Char(' ') + repairFilesHint();
}

void InstanceSettingsDialog::refreshIconRow() {
    if (m_iconPreview == nullptr) {
        return;
    }

    // 当前该画哪一张。三档的先后是有讲究的：
    //   ① 手上有一张**刚挑的**图 → 画它（用户要立刻看见自己选了什么，而它这会儿还在原处）；
    //      读不动（不是图 / 半截文件）就往下走，退回这一档原本的样子。
    //   ② 内置候选 → 那一张。
    //   ③ 本地那一档 → 调用方解好的那张；它要是空的（文件没了）→ 退回默认那张。
    //   ④ 默认 → 调用方算好的 defaultMark。
    QPixmap mark;
    if (!m_pickedIconPath.isEmpty()) {
        const QPixmap picked(m_pickedIconPath);
        if (!picked.isNull()) {
            mark = picked;
        }
    }
    if (mark.isNull()) {
        const QString key = VersionIcons::iconKeyFromValue(m_iconChoice);
        if (!key.isEmpty()) {
            mark = VersionIcons::iconMarkForKey(key, kIconPreviewSize);
        } else if (m_iconChoice == localIconValue()) {
            mark = m_localIconMark;
        }
    }
    if (mark.isNull()) {
        mark = m_defaultIconMark;
    }
    // 按**保持长宽比**缩到那一格：用户挑的图多半不是正方形，拉成方的会把人像 / 图标压扁。
    m_iconPreview->setPixmap(mark.isNull()
                                 ? QPixmap()
                                 : mark.scaled(kIconPreviewSize, kIconPreviewSize,
                                               Qt::KeepAspectRatio, Qt::SmoothTransformation));

    // 八颗按钮的勾选状态跟着走（"按下去了哪一颗"必须和预览说的是同一件事）。
    const QStringList keys = VersionIcons::iconCandidateKeys();
    const QString checkedKey = VersionIcons::iconKeyFromValue(m_iconChoice);
    for (int i = 0; i < m_iconButtons.size(); ++i) {
        m_iconButtons.at(i)->setChecked(keys.value(i) == checkedKey);
    }
}

void InstanceSettingsDialog::showPage(const QString &key) {
    const int index = stackPageIndexOf(key);
    if (index < 0) {
        return;
    }
    // 左栏那一条**先**摆好：有条目的是"选中那一条"（和用户点那一条是同一条路），没有条目
    // 的（「配置文件」这种没有入口的页、被用户勾掉了的页、这个实例上用不上的页）那一列里
    // 根本没有它 ⇒ row 是 -1，等于把选中清掉（留着上一次那一行的话，左栏亮着 A、右栏
    // 摆着 B —— 那正是"两栏各说各的"）。
    //
    // NOTE: 这一下**压着信号** —— 下面那一发 applyPageKey() 本来就会换页，让这一下也发
    //       一次的话，同一页会被摆两次（见 applyPageKey() 里那条 alreadyHere：
    //       多发的那一次在「服务器列表」上就是白连一轮）。
    {
        const QSignalBlocker blocker(m_pageList);
        m_pageList->setCurrentRow(shownEntryKeys().indexOf(key));
    }
    // 无论上面那一发有没有真的换过去（`setCurrentRow()` 在"本来就亮着这一条"时不发信号），
    // 这里都摆一次。同一页重复摆的那一多半由 alreadyHere 挡掉。
    applyPageKey(key);
}

QString InstanceSettingsDialog::currentPageKey() const {
    // 判据只有右栏那一位（见 .hpp 那段）：反查函数本来就在、`rebuildEntryList()` 也在用。
    return m_stack == nullptr ? QString() : pageKeyAtStackIndex(m_stack->currentIndex());
}

QWidget *InstanceSettingsDialog::addOverrideRow(QGridLayout *grid, int row, const QString &field,
                                                const QString &title, const QString &customHint) {
    // **两行**：上面那一行是这个设置的名字（独占一整行），下面那一行才是它的选项
    // （「跟随全局 / 自定义」那个档位下拉框 + 控件）——用户 2026-09-26 原话「上一行设置
    // 名称，下一行设置选项」。**调用方因此要 `row += 2`**（见 addTitledRow() 那段）。
    QVBoxLayout *cellLayout = nullptr;
    QWidget *cell = addTitledRow(grid, row, title, this, &cellLayout);

    // 控件和档位下拉框在同一格，是为了内存那两行"整行禁用"能一次把两者都灰掉 ——
    // 只禁数字框会留下一个能选的「自定义」，选了却什么都不发生。
    auto *choiceRow = new QWidget(cell);
    choiceRow->setObjectName(choiceRowName(field));
    auto *choiceLayout = new QHBoxLayout(choiceRow);
    choiceLayout->setContentsMargins(0, 0, 0, 0);
    choiceLayout->setSpacing(8);

    // ★ 2026-09-28：**一对单选钮 → 一个下拉框**（用户原话「所有单选框换成下拉的」）。
    //
    //   两项的**顺序就是下标**：`choiceFollowIndex()`（跟随全局）在前、
    //   `choiceCustomIndex()`（自定义）在后。那两个函数是档位的唯一出处，读侧
    //   （isCustomChoice）、写侧（setChoiceCustom）和验证工程都走它们 —— 这里只是把
    //   它们摆成 `addItem()` 的先后，**不许在别处再写一遍 0 / 1**。
    //
    // NOTE: 下拉框**吃不到** `watchEditors()` 原来那四条按类型的线（它们接的是
    //       `QAbstractButton::clicked` / `QLineEdit::editingFinished` …，QComboBox 一条
    //       都不沾），所以那边新加了一循环接它。漏了的症状很具体：把档位切到「自定义」
    //       之后盘上还是老样子 —— 和「窗口模式」那个下拉框当初漏掉时一模一样
    //       （那一条的来龙去脉见下面构造它那一段）。
    auto *combo = new QComboBox(choiceRow);
    combo->setObjectName(choiceComboName(field));
    combo->addItem(followText());
    combo->addItem(customText());
    // 下拉框也吃滚轮（在两项之间来回跳），而这一格上的滚轮多半是"想滚这一页"。
    // 和「窗口模式」那一个同一个理由。
    WheelGuard::install(combo);
    choiceLayout->addWidget(combo);
    m_choiceCombos.insert(field, combo);

    cellLayout->addWidget(choiceRow);

    auto *hintLabel = new QLabel(cell);
    hintLabel->setObjectName(followHintName(field));
    hintLabel->setWordWrap(true);
    // 提示文字有**两种形态**：选「自定义」时说"这一项只对这个实例生效"，选「跟随
    // 全局」时说"当前跟随的是什么值"。这里先把自定义那一种记下来，refreshHints()
    // 在两种之间切。
    m_customHints.insert(field, customHint);
    cellLayout->addWidget(hintLabel);

    // 档位一变，那一行下面那句话和内存两行的可用性都可能跟着变。
    connect(combo, &QComboBox::currentIndexChanged, this,
            &InstanceSettingsDialog::refreshHints);

    return cell;
}

bool InstanceSettingsDialog::isCustomChoice(const QString &field) const {
    const QComboBox *combo = m_choiceCombos.value(field);
    // 认不出来的 field（这一格没建出来）回 false = 跟随全局 —— 见 .hpp 里那一段。
    return combo != nullptr && combo->currentIndex() == choiceCustomIndex();
}

void InstanceSettingsDialog::setChoiceCustom(const QString &field, bool custom) {
    QComboBox *combo = m_choiceCombos.value(field);
    if (combo == nullptr) {
        return;
    }
    // 挡信号：这一发是**灌值**，不是用户改的。不挡的话它一次会响两条线 ——
    // refreshHints（无害，下面 refreshHints() 反正要再算一遍）和写盘那条
    // （`watchEditors()` 的 QComboBox 循环 ⇒ notifyEdited），后者在构造里
    // `m_lastSignature` 还是空的时候就是"打开这一页顺手替用户写一次盘"。
    const QSignalBlocker block(combo);
    combo->setCurrentIndex(custom ? choiceCustomIndex() : choiceFollowIndex());
}

QHBoxLayout *InstanceSettingsDialog::choiceRowOf(const QString &field) const {
    if (auto *row = findChild<QWidget *>(choiceRowName(field))) {
        return qobject_cast<QHBoxLayout *>(row->layout());
    }
    return nullptr;
}

bool InstanceSettingsDialog::effectiveAutoMemory() const {
    return isCustomChoice(autoMemoryField()) ? m_memoryAutoCheck->isChecked()
                                             : m_global.autoMemory;
}

bool InstanceSettingsDialog::advancedExpanded() const { return m_advancedToggle->isChecked(); }

void InstanceSettingsDialog::refreshHints() {
    const bool autoMemory = effectiveAutoMemory();

    // 内存那两行：自动档下**整格**禁用（单选钮 + 数字框），提示换成"为什么它由不得你"。
    m_xmxFollowCell->setEnabled(!autoMemory);
    m_xmsFollowCell->setEnabled(!autoMemory);

    // ★ 2026-09-27：手动档 + **「跟随全局」** 时，那一格里的条和数字框要**灰掉**。
    //   从前它们只在自动档下禁用，于是在"跟随全局"这一档上照样能拖、能填 —— 而拖完那一发
    //   `override()` 根本不会读（它只在选了「自定义」时才把 `m_memoryMaxSpin->value()` 装进
    //   结果，见下面那段），用户看着数字变了、松手、关页，什么都没发生。这正是设置页那条
    //   注释明确反对的"让人以为能改"。
    //
    //   **整格**不能跟着禁（那对单选钮得留着），所以只点这两个控件；单选钮本身照旧能点，
    //   想要填数就点「自定义」，一按下去这两个就活了（`setEnabled` 的实效是"父与自身都为真"，
    //   所以自动档下这里的 true 也翻不过上面那句整格禁用）。
    //
    // NOTE: 「实例名 / Java 路径」那几行是"跟随全局时编辑框里就摆着全局值、改了等于自定义"
    //       的另一种拆法（那边靠「浏览…」那一按顺手切档），内存这一行**不用**那个拆法：
    //       条目上滚一下滚轮就在盘上凭空多出一份覆盖，这种事不能有。
    const bool memoryEditable = !autoMemory && isCustomChoice(xmxField());
    m_memoryMaxSlider->setEnabled(memoryEditable);
    m_memoryMaxSpin->setEnabled(memoryEditable);
    // 最小内存那一格整个是隐藏的（2026-09-26 撤掉的那一行），但控件照旧跟着同一个规矩 ——
    // 它现在唯一的作用就是把盘上那份值透传回去，多一个能改它的入口就是多一条静默改写的路。
    m_memoryMinSpin->setEnabled(memoryEditable);

    // 每一行下面那句话：选「跟随全局」时写清"当前跟随的是什么值"，选「自定义」时
    // 说明"这一项只对这个实例生效"。两种都要有 —— 只有"跟随全局"四个字的话，
    // 用户不知道跟随的到底是什么，还得退出去翻设置页。
    const auto setHint = [this](const QString &field, const QString &text) {
        if (auto *label = findChild<QLabel *>(followHintName(field))) {
            label->setText(text);
        }
    };
    const auto followOrCustom = [this, &setHint](const QString &field, bool isCustom,
                                                 const QString &currentText) {
        setHint(field, isCustom ? m_customHints.value(field) : followHint(currentText));
    };

    followOrCustom(javaPathField(), isCustomChoice(javaPathField()),
                   m_global.javaPath.isEmpty() ? tr("未指定") : m_global.javaPath);
    followOrCustom(autoMemoryField(), isCustomChoice(autoMemoryField()),
                   m_global.autoMemory ? tr("自动（按系统内存）") : tr("手动指定"));
    followOrCustom(extraJvmArgsField(), isCustomChoice(extraJvmArgsField()),
                   m_global.extraJvmArgs.isEmpty() ? tr("未设置") : m_global.extraJvmArgs);

    // ---------- 窗口模式 / 窗口大小（2026-09-28 加的）----------
    //
    // ★ 「跟随全局」时**这一格里的控件灰掉**（和内存那两行同一个做法，理由也在那边：
    //   能改、改完 override() 又不读，就是"让人以为能改"）。**不能整格禁**：那个档位
    //   下拉框得留着才能切回「自定义」。所以只点那几个控件，下拉框照旧能点。
    //
    // NOTE: 和内存那一行不同的是，这里**没有**"这一格整格禁用"的第二种情况（内存那两种是
    //       "自动档"和"跟随全局"）—— 窗口这几项没有自动档。
    if (m_windowModeCombo != nullptr) {
        const bool custom = isCustomChoice(windowModeField());
        m_windowModeCombo->setEnabled(custom);
        // 那一句话：选「自定义」时说"只对这个实例生效"，选「跟随全局」时说清**跟随的是什么**
        // —— 全局那一档的显示名由调用方喂进来。没喂（空串）时就只说"跟随全局"四个字：
        // 硬套 followHint("") 会写出「跟随全局（当前 ）」那种带个空括号的话。
        setHint(windowModeField(),
                custom ? m_customHints.value(windowModeField())
                       : (m_global.windowModeLabel.isEmpty()
                              ? followText()
                              : followHint(m_global.windowModeLabel)));
    }
    if (m_windowWidthSpin != nullptr) {
        // 分辨率那一行占两个键、只有**一个**档位下拉框，它存在 `windowWidthField()` 那一格
        // 里（见 .hpp 里那一段）。
        const bool editable = isCustomChoice(windowWidthField());
        m_windowWidthSpin->setEnabled(editable);
        m_windowHeightSpin->setEnabled(editable);
        // 中间那个 × 跟着一起灰：只剩两个灰框中间夹一个亮着的 ×，看着像那一格坏了
        // （和设置页那一行同一个做法）。
        if (auto *times = findChild<QLabel *>(windowSizeTimesName())) {
            times->setEnabled(editable);
        }
        // 「跟随全局」那句话里要写出**当前跟随的是多大** —— 光有"跟随全局"四个字，用户
        // 还得退出去翻设置页。调用方没给尺寸（<= 0 那个哨兵）时只说"跟随全局"。
        const QString globalSize = (m_globalWindowWidth > 0 && m_globalWindowHeight > 0)
                                       ? QStringLiteral("%1×%2").arg(m_globalWindowWidth)
                                             .arg(m_globalWindowHeight)
                                       : QString();
        setHint(windowWidthField(),
                editable ? m_customHints.value(windowWidthField())
                         : (globalSize.isEmpty() ? followText() : followHint(globalSize)));
    }

    if (autoMemory) {
        // 自动档：这两行是灰的，提示只说这一件事（"当前 %1 MB"在这里没有意义 ——
        // 那两个数每次启动前都会重算）。
        setHint(xmxField(), memoryAutoDisabledHint());
        setHint(xmsField(), memoryAutoDisabledHint());
    } else {
        followOrCustom(xmxField(), isCustomChoice(xmxField()),
                       QStringLiteral("%1 MB").arg(m_global.xmx));
        followOrCustom(xmsField(), isCustomChoice(xmsField()),
                       QStringLiteral("%1 MB").arg(m_global.xms));
    }
}

InstanceSettingsDialog::Override InstanceSettingsDialog::override() const {
    Override result;

    if (isCustomChoice(javaPathField())) {
        // 去首尾空白：路径末尾多一个空格在 Windows 上就是个不存在的文件，
        // 而那会变成"启动时找不到 Java"。
        const QString path = m_javaPathEdit->text().trimmed();
        // **空的「自定义」= 跟随全局**，不是一个空路径覆盖。这一条不是"顺手清理"：
        // 实例覆盖了 javaPath 就等于手动档（见 GlobalJavaSettings::ensureJavaForVersion
        // 的 NOTE），而手动档 + 空路径是**必然启动失败**的（"还没指定路径"）。
        // 用户清空这个框的意思是"这个实例不特殊指定"，那正是「跟随全局」。
        // NOTE: 这一条**只对 javaPath 成立**。extraJvmArgs 自定义成空串是有意义的
        //       （全局带着 -XX:+UseG1GC，这个实例就是不要额外参数），那里空串照存。
        if (!path.isEmpty()) {
            result.javaPath = path;
        }
    }

    // 自动档下这两项**不写**。界面这时是禁用的，所以正常操作走不到这儿；能走到的是
    // 磁盘上本来就存着这种组合的旧数据（自动档 + Xmx 覆盖）。存一份永远不会被读的
    // 覆盖就是骗人 —— 见 .hpp 里 override() 的 NOTE。
    if (!effectiveAutoMemory()) {
        if (isCustomChoice(xmxField())) {
            result.xmx = m_memoryMaxSpin->value();
        }
        if (isCustomChoice(xmsField())) {
            // ★ 2026-09-26：这一行从界面上撤掉了（整行隐藏），但盘上存过的值**照样透传** ——
            //   撤掉界面不等于替用户删掉他的设置。唯一不能原样透传的是"比最大内存还大"的
            //   那些：Xms > Xmx 是 JVM 直接拒绝启动的组合（Initial heap size set to a
            //   larger value than the maximum heap size），而用户现在在界面上根本看不到
            //   自己存过这么一对 —— 那一行是隐藏的，没有人在旁边替他看着。
            const int effectiveXmx = isCustomChoice(xmxField()) ? m_memoryMaxSpin->value()
                                                                : m_global.xmx;
            result.xms = qMin(m_memoryMinSpin->value(), effectiveXmx);
        }
    }

    if (isCustomChoice(autoMemoryField())) {
        result.autoMemory = m_memoryAutoCheck->isChecked();
    }
    if (isCustomChoice(extraJvmArgsField())) {
        result.extraJvmArgs = m_extraJvmArgsEdit->text().trimmed();
    }

    // 窗口模式：选「自定义」就把下拉框里当前那一项的**代号**收进去（itemData 存的就是它，
    // 见构造那一段）。取不到代号（`windowModes` 空表时这一行根本不存在 ⇒ 指针是空的、
    // 或者下拉框里一项都没有）就当作没覆盖 —— 和 javaPath 那条"空的「自定义」= 跟随全局"
    // 同一个方向：宁可什么都不写，也不要往 meta 里塞一个空代号（那会被读侧当成认不出来的
    // 值、静默退回默认档，比"没写"更难查）。
    if (m_windowModeCombo != nullptr && isCustomChoice(windowModeField())) {
        const QString token = m_windowModeCombo->currentData().toString();
        if (!token.isEmpty()) {
            result.windowMode = token;
        }
    }

    // 分辨率：**两个一起收、或者两个都不收**。它们共用一个档位下拉框（见 .hpp 里 override()
    // 那段），所以判据只看那一个 —— 不去看两个框各自有没有被动过（那既判不出来，也不是
    // 用户的意思：他选的是「自定义」，意思是这一对标就要按他填的走）。
    if (m_windowWidthSpin != nullptr && isCustomChoice(windowWidthField())) {
        result.windowWidth = m_windowWidthSpin->value();
        result.windowHeight = m_windowHeightSpin->value();
    }

    return result;
}

QStringList InstanceSettingsDialog::appendJvmArgs() const {
    QStringList result;
    // 一条一行（不是空格分隔）：见 .hpp 里那个成员的说明 —— 参数本身可能含空格，
    // 空格分隔会把 `-Dfoo=bar baz` 切成两条。
    const QStringList lines = m_appendJvmArgsEdit->toPlainText().split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            result.append(trimmed);
        }
    }
    return result;
}

bool InstanceSettingsDialog::warnJavaMismatch() const {
    // 控件没建出来时返回 true（= 提醒）：读侧默认"要提醒"这个方向是**安全**的那个
    // ——宁可多问一次，也不要静默放行一个版本不对的 Java。
    return m_warnJavaMismatchCheck == nullptr || m_warnJavaMismatchCheck->isChecked();
}

// ---------- 「改了就写」那套（见 .hpp 里 settingsEdited() 那一段）----------

void InstanceSettingsDialog::watchEditors() {
    // 四类各一条线。**按类型接、不按对象接**：往这一页加控件时不用记得回来补一行。
    // 代价是这里面有两处接不到的（程序化改值），它们各自补一发 —— 见下面那段 NOTE。
    for (QAbstractButton *button : findChildren<QAbstractButton *>()) {
        // 勾选框 / 单选钮 / 各种按钮一并算上。「值变了没有」由 notifyEdited() 判，
        // 所以那些**不改值**的按钮（打开文件夹 / 导出脚本 / 高级选项那个折叠开关）
        // 多接一条线是白接，不会多写一次盘。
        connect(button, &QAbstractButton::clicked, this, &InstanceSettingsDialog::notifyEdited);
    }
    for (QLineEdit *edit : findChildren<QLineEdit *>()) {
        // 「实例名」那一格走改名那条路（renameRequested），**不接在这儿**：它改的是目录名，
        // 不能和别的值挤在同一次写盘里。
        if (edit == m_instanceNameEdit) {
            continue;
        }
        // `editingFinished`（回车 / 离开这一格）而不是 `textChanged`：路径和额外参数都是
        // "一段字"，打字打到一半就写盘等于每次按键改一次磁盘上的设置。改名那一格是同一条
        // 理由（见 .hpp 里 renameRequested()）。
        connect(edit, &QLineEdit::editingFinished, this, &InstanceSettingsDialog::notifyEdited);
    }
    // QSpinBox 的两条 valueChanged 重载里挑 int 那条（另一个是 QString，Qt 6 已经删了，
    // 但 QOverload 写成 int 这一条在两边都编得过）。
    for (QSpinBox *spin : findChildren<QSpinBox *>()) {
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this,
                &InstanceSettingsDialog::notifyEdited);
    }
    // ★ 下拉框（2026-09-28 加的这一条）。原来这一页上只有「窗口模式」一个下拉框，
    //   它独自接了一条 `currentIndexChanged`；用户点名「所有单选框换成下拉的」之后
    //   **档位那一格也成了下拉框**（七行），再一个个手接就成了"每加一行都要记得回来补
    //   一条线"。收进这个循环之后和上面四条是同一个形状。
    //   症状（漏掉时）：切到「自定义」之后盘上还是老样子 —— 界面上看着改了、下次开这一页
    //   又是原来的档。**容易在测试里被盖住**：后面只要再动一个别的控件，那一发写盘会顺带
    //   把新档位写进去，于是"没接线"这个洞照样全绿（mw_check 的 T20.11b 就是为这个写的：
    //   它把换档位摆成那一页上**最后一个**动作）。
    //
    // NOTE: 这一循环**连这一页上的子页面一起接**（文件管理那几页的「筛选」下拉框、世界设置
    //       那一页的难度下拉框 —— 它们都是这一页的后代）。改了它们会白喊一声 notifyEdited()
    //       —— 无害：那个函数按 `valueSignature()` 去重，而那几个值不在签名里，所以一次盘都
    //       不会写。按"这一页上的哪些下拉框"去筛是**多一条会过期的名单**，不值当。
    for (QComboBox *combo : findChildren<QComboBox *>()) {
        connect(combo, &QComboBox::currentIndexChanged, this,
                &InstanceSettingsDialog::notifyEdited);
    }
    for (QPlainTextEdit *text : findChildren<QPlainTextEdit *>()) {
        // 这个没有 editingFinished，只能用 textChanged —— 于是"追加参数"那一格是这一页上
        // **唯一一处按每个按键写盘**的（一条一行，本来就短）。值没变还是不发（空格、回车这
        // 种改动会被签名里那句 `appendJvmArgs()` 归一掉）。
        connect(text, &QPlainTextEdit::textChanged, this, &InstanceSettingsDialog::notifyEdited);
    }

    // 「实例名」那一格：回车 / 离开这一格 ⇒ 报"用户要改名"（不是"值变了"）。
    if (m_instanceNameEdit != nullptr) {
        connect(m_instanceNameEdit, &QLineEdit::editingFinished, this, [this]() {
            emit renameRequested(m_instanceNameEdit->text());
        });
    }

    // NOTE: 【上面两条按类型的线接不到两种改动，各自在动手的地方补一发 notifyEdited()】
    //       ①「浏览…」选完 java.exe：那是 `setText()`（程序化，不发 editingFinished）；
    //       ②「选一张图片…」挑完图：`clicked` 在**框弹出来之前**就发完了，等用户挑完，
    //         那一发早过去了（值是在那之后才变的）。
    //       往这一页再加"点一下、然后弹个系统框问点什么"的按钮时，记住同一件事。
}

QStringList InstanceSettingsDialog::valueSignature() const {
    // 拼的就是**写盘那边会读的那几问**（`override()` / `appendJvmArgs()` /
    // `warnJavaMismatch()` / `iconChoice()` / `pickedIconPath()` / `chosenGroupId()` /
    // `pinned()` / `pageChoices()`）。改一处要看得见另一处 —— 所以这个函数摆在它们旁边。
    const Override values = override();
    // std::optional 的写法：先摆"有没有值"，再摆那个值。摆成一个空串是不够的 ——
    // javaPath 的"自定义成空串"和"跟随全局"在 override() 里已经是两回事（前者被那一层
    // 归一成了后者），但 xmx 那一档 4 和"跟随全局"用同一个写法就得靠这一位分开。
    const auto optionalText = [](const std::optional<QString> &value) {
        return value.has_value() ? QStringLiteral("v:") + *value : QStringLiteral("-");
    };
    const auto optionalNumber = [](const std::optional<int> &value) {
        return value.has_value() ? QStringLiteral("v:%1").arg(*value) : QStringLiteral("-");
    };
    const auto optionalFlag = [](const std::optional<bool> &value) {
        return value.has_value() ? QStringLiteral("v:%1").arg(*value ? 1 : 0) : QStringLiteral("-");
    };

    QStringList result;
    result << optionalText(values.javaPath) << optionalNumber(values.xmx)
           << optionalNumber(values.xms) << optionalFlag(values.autoMemory)
           << optionalText(values.extraJvmArgs)
           // 窗口那三项（2026-09-28）：模式是**代号字符串**，宽高各一个数。和上面同一个
           // 写法 —— 有没有值 + 值本身，两者都要摆（"自定义成和全局一样的档"仍然是覆盖，
           // 不能和"跟随全局"混成同一个签名）。
           << optionalText(values.windowMode) << optionalNumber(values.windowWidth)
           << optionalNumber(values.windowHeight)
           // 追加参数按条摆：`appendJvmArgs()` 已经把空行和首尾空白归一掉了，两条顺序不同
           // 的清单在这里就是两份签名（那正是两件不同的设置）。
           << appendJvmArgs()
           << QStringLiteral("warn:%1").arg(warnJavaMismatch() ? 1 : 0);
    // 图标：挑本地那张时**连路径一起算**（换一张图 = 换一次设置），否则只算那一档的键。
    result << (m_pickedIconPath.isEmpty() ? iconChoice()
                                          : localIconValue() + QLatin1Char('|') + m_pickedIconPath);
    // 分组与置顶：分组按 id（组名会变，id 才是身份），置顶只算那一位。
    result << QStringLiteral("group:") + chosenGroupId()
           << QStringLiteral("pin:%1").arg(pinned() ? 1 : 0);
    // 「显示哪些页」那几格：按 optionalPageKeys() 的**顺序**拼（不能按 QHash 的顺序 ——
    // 每次遍历 QHash 的次序都未必一样，那样拼出来的签名会自己变，等于每读一次就说"改了"）。
    // 一条一格 `key=0/1`，这样"张三关掉、李四打开"和"两个人的选择都动了但拼起来一样"
    // 分得开。
    QStringList pageFlags;
    for (const QString &key : optionalPageKeys()) {
        const QCheckBox *check = m_pageChecks.value(key);
        pageFlags << QStringLiteral("%1=%2").arg(key, check != nullptr && check->isChecked()
                                                           ? QStringLiteral("1")
                                                           : QStringLiteral("0"));
    }
    result << pageFlags.join(QLatin1Char(','));
    return result;
}

void InstanceSettingsDialog::notifyEdited() {
    const QStringList now = valueSignature();
    if (now == m_lastSignature) {
        return;
    }
    m_lastSignature = now;
    emit settingsEdited();
}
