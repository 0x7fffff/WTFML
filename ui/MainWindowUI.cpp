/**
 * @file MainWindowUI.cpp
 * @brief 实现 UI 构建器类，实际创建主窗口的控件、布局并设置窗口属性。
 * @author 0x7fffff
 * @date 2026-04-11
 * @note 完成主窗口所有 UI 元素的初始化与排列，供 MainWindow 构造函数调用。
 */

#include "MainWindowUI.hpp"

#include "SkinModel.hpp"
#include "VersionIcons.hpp"
#include "VersionTreeDelegate.hpp"

#include <QColor>
#include <QEvent>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QPalette>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

/// 每个选项卡页面共用的边距：贴着选项卡内容区留一圈呼吸空间。
constexpr int kPageMargin = 9;

/// 左右两栏之间的间距。
constexpr int kColumnSpacing = 12;

// ---------- 各处手调的尺寸 ----------
// 集中放这里是为了"改外观只需看这一处"：这些数值是照着当前样式（Fusion）的控件度量
// 调出来的，换背景色/字体/控件高度时就是这一组要重估。散在各自的构建代码里每次都得
// 全文件搜一遍。
//
// NOTE: 设置页那个数字框的宽度（kMemorySpinWidth）不在这里 —— 它只服务
//       addInlineControl() 一个调用点，留在它旁边更好找。

/// 底栏整条的高度。给固定值：栏里三格的内容高度都是自己定的，跟着窗口高变没有意义。
constexpr int kBottomBarHeight = 56;

/// 底栏左格「实例运行状态」那两行文字的宽度下限，免得内容一变整条就左右跳。
constexpr int kStatusLabelMinWidth = 260;

/// 左下角头像的边长（正方形）。**值本身不在这里** —— 它和"主窗口拿什么尺寸去裁头像"
/// 必须是同一个数，所以唯一的出处是 MainWindowUI::avatarSize()（见头文件）。
const int kAvatarSize = MainWindowUI::avatarSize();

/// 「启动游戏」按钮的高度。文字是两行（"启动游戏\n<显示名>"），给足高度免得被压成一行半。
constexpr int kStartButtonMinHeight = 88;

/// 「额外 JVM 参数」多行输入框的高度，约三四行的样子；再高会把下面那几项挤出可视区。
constexpr int kJvmArgsEditHeight = 72;

/// 几个按钮的宽度下限：文案长短差得多（「安装」/「应用」这种），
/// 不给下限就会各自缩成一小块、排在一起长短不齐。
constexpr int kDownloadButtonMinWidth = 96;
/// 「账号与外观…」那个入口按钮的最小宽度。文案固定，但给一个下限，免得以后换文案时
/// 按钮缩成一小块。**主页那个和设置页那个共用这一个值** —— 它们说的是同一件事。
constexpr int kAccountSkinButtonMinWidth = 140;
constexpr int kApplyJvmArgsButtonMinWidth = 72;

/// 一栏：控件本身 + 它自己的竖排布局。
struct Column {
    QWidget *widget = nullptr;
    QVBoxLayout *layout = nullptr;
};

/// 一个页面的左右两栏。
struct TwoColumns {
    Column left;
    Column right;
};

/**
 * @brief 把页面摆成左右两栏（仿 PCL）。
 * @param page 选项卡页面。
 * @param tab 页面代号（"home" / "instance" / "download"），进 objectName，**也决定份数**。
 * @return 两栏的控件与布局；调用方往 layout 里塞东西即可。
 *
 * 份数按页不同（主页左宽右窄、另两页左窄右宽，见 MainWindowUI::leftColumnIsWider()），
 * 所以这里把 tab 一路传下去，不自己写死。
 *
 * **只有主页/实例/下载这三个**走这条路；「设置」页走 setupSettingsPage()，它的 3:7 是在
 * 那张两列 grid 上用 setColumnStretch() 表达的（份数取自默认那一对 leftColumnStretch/
 * rightColumnStretch，所以比例仍是单点决定，但栏控件只有这三页有）。
 * 栏做成真控件而不是裸布局，是为了验证工程能按 objectName 找回来量几何。
 */
TwoColumns buildTwoColumnPage(QWidget *page, const QString &tab) {
    auto *row = new QHBoxLayout(page);
    row->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);
    row->setSpacing(kColumnSpacing);

    const auto makeColumn = [page](const QString &name) {
        Column column;
        column.widget = new QWidget(page);
        column.widget->setObjectName(name);
        column.layout = new QVBoxLayout(column.widget);
        // 栏本身不留边距：页面那一层已经留过 kPageMargin 了，再留一层就是双层边距
        column.layout->setContentsMargins(0, 0, 0, 0);
        column.layout->setSpacing(10);
        return column;
    };

    TwoColumns columns;
    columns.left = makeColumn(MainWindowUI::leftColumnName(tab));
    columns.right = makeColumn(MainWindowUI::rightColumnName(tab));
    row->addWidget(columns.left.widget, MainWindowUI::leftColumnStretch(tab));
    row->addWidget(columns.right.widget, MainWindowUI::rightColumnStretch(tab));
    return columns;
}

/// 设置页里一行：名字在左、控件在右。**没有返回值** —— "下一个空行号"由调用方自己
/// `row++` 推进（每次调用照样把它摆在 grid 的第 row 行）。
/// @note 3:7 是靠 grid 的列拉伸因子实现的（见 setupSettingsPage），这里只负责摆。
void addSettingRow(QGridLayout *grid, int row, const QString &labelText, QWidget *control,
                   QWidget *parent) {
    QLabel *label = new QLabel(labelText, parent);
    label->setAlignment(Qt::AlignRight | Qt::AlignTop);
    grid->addWidget(label, row, 0);
    grid->addWidget(control, row, 1);
}

/// 设置页里的分组标题，横跨两列。
void addSectionTitle(QGridLayout *grid, int row, const QString &text, QWidget *parent) {
    QLabel *title = new QLabel(text, parent);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    grid->addWidget(title, row, 0, 1, 2);
}

/// 设置页里数字框的宽度（够写「65536 MB」再加一对上下箭头）。
constexpr int kMemorySpinWidth = 130;

/// 把一个控件加到设置页那一行的容器里，**只占它需要的那点宽度、靠左**放。
///
/// 为什么不是默认的"填满整格"：右列有 7 份宽（900 px 的窗口下就有 600 多 px），
/// 一个「4096 MB」的数字框被拉到那个宽度看着就是坏的 —— PCL 那种设置页里的数字框
/// 也只有巴掌宽。下拉框和文本框不这么处理：它们的宽度是有信息量的（Java 路径、
/// 参数列表都长）。
///
/// @param minWidth 最小宽度；比 sizeHint 小的话实际按 sizeHint 走。
void addInlineControl(QVBoxLayout *box, QWidget *control, int minWidth) {
    control->setMinimumWidth(minWidth);
    // 对齐标志传 0 以外的值 = "按 sizeHint 摆、不要撑满整格"（QBoxLayout::addWidget
    // 的文档里那句 "the widget fills the entire cell" 只在 alignment 为 0 时成立）。
    box->addWidget(control, 0, Qt::AlignLeft);
}

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

/// 往 makeHintedControl() 造出来的容器里加一句说明。
void addHint(QWidget *box, const QString &text, QWidget *parent) {
    addDynamicHint(box, text, parent);
}

}  // namespace

MainWindowUI::MainWindowUI(QMainWindow *parent) { setupUi(parent); }

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
    QWidget *homeTab = new QWidget(tabWidget);
    homeTab->setObjectName(homeTabName());
    const TwoColumns home = buildTwoColumnPage(homeTab, QStringLiteral("home"));
    {
        home.left.layout->addWidget(new QLabel(QStringLiteral("已安装版本"), home.left.widget));

        installedVersionCombo = new QComboBox(home.left.widget);

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
        installedVersionCombo->installEventFilter(new WheelGuard(installedVersionCombo));
        installedVersionCombo->setObjectName(installedVersionComboName());
        home.left.layout->addWidget(installedVersionCombo);

        startButton = new QPushButton(QStringLiteral("启动游戏"), home.left.widget);
        // 文字是两行（"启动游戏\n<显示名>"，由 MainWindow::updateStartButtonText() 维护；
        // 高度固定给足，免得两行被压成一行半。
        startButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        startButton->setMinimumHeight(kStartButtonMinHeight);
        home.left.layout->addWidget(startButton);

        offlineModeCheckBox = new QCheckBox(QStringLiteral("离线模式"), home.left.widget);
        offlineModeCheckBox->setObjectName(offlineModeCheckBoxName());
        offlineModeCheckBox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        home.left.layout->addWidget(offlineModeCheckBox, 0, Qt::AlignLeft);

        // 剩下的竖直空间留在底部：按钮不跟着窗口高度一起被拉长
        home.left.layout->addStretch(1);
    }
    // ---------- 主页右栏：我的角色（3D 皮肤预览） ----------
    // 这里原来是空的：早先放着一张「版本列表」，和左边那个下拉框是同一份数据的两种看法，
    // 而实例页右边现在也有一张同样数据的列表了，留着就是把同一件事说三遍（2026-09 去掉的）。
    // 空着的那阵子这一栏的作用只剩"按份数把左边那套操作顶到 7 成宽"，现在拿它装皮肤预览 ——
    // 三成宽正好够放一个小人（见 SkinModel 里"缩放按控件尺寸自适应"那条，控件多大它多大）。
    {
        QLabel *skinTitle = new QLabel(QStringLiteral("我的角色"), home.right.widget);
        QFont skinTitleFont = skinTitle->font();
        skinTitleFont.setBold(true);
        skinTitle->setFont(skinTitleFont);
        home.right.layout->addWidget(skinTitle);

        skinViewer = new SkinViewer(home.right.widget);
        skinViewer->setObjectName(skinViewerName());
        // 立刻喂一套外观。这个控件**任何时候都不该是空白的** —— 没登录、还没取到、
        // 取不到，这三种情况看到的都是这套默认外观，所以兜底放在这里而不是等业务层来灌：
        // 少一处"谁忘了设"的可能。业务层拿到真皮肤时再覆盖它。
        skinViewer->setSkin(SkinModel::defaultSteveSkin(), QImage(), false);
        // 份数 1：这一栏里竖直方向全是它的（标题和说明那两行是固定高度）。
        home.right.layout->addWidget(skinViewer, 1);

        skinHintLabel = new QLabel(skinHintNotLoggedInText(), home.right.widget);
        skinHintLabel->setObjectName(skinHintLabelName());
        // 必须换行：这句最长的时候（「取不到 <名字> 的皮肤，显示的是默认外观」）有 20 多字，
        // 而这一栏只有 260 px 上下，不换行就会把整栏顶宽、把左边的 7 份挤掉。
        skinHintLabel->setWordWrap(true);
        // 横向上把自己**从布局的度量里摘出去**：wordWrap 的 QLabel 报出来的 sizeHint 是
        // "整句排成一行"的宽度（好几百 px），而最小宽度又是按最长的一段算的 —— 这一栏是
        // 3 份（约 260 px），让一个纯说明文字去决定它的宽度是本末倒置。摘掉之后这一栏的
        // 宽度仍然只由份数决定（主页那个精确的 7:3 就是靠这个成立的）。
        skinHintLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        home.right.layout->addWidget(skinHintLabel);

        // 「账号与外观…」：多账号 / 换皮肤换披风的入口，按用户要求放在 3D 预览这一块
        // 的下方。**说明行（skinHintLabel）在它上面**：那句是预览的图注，紧贴着图才是
        // 图注该在的位置；把按钮塞进图和图注中间会把「图 + 它的注解」拆成两半。
        // 宽度**不**跟着这一栏长（这一栏只有 3 份，拉满会顶宽），贴左边固定宽度。
        accountSkinButton = new QPushButton(MainWindowUI::accountSkinButtonText(),
                                            home.right.widget);
        accountSkinButton->setObjectName(MainWindowUI::accountSkinButtonName());
        accountSkinButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        accountSkinButton->setMinimumWidth(kAccountSkinButtonMinWidth);
        home.right.layout->addWidget(accountSkinButton, 0, Qt::AlignLeft);
    }
    // NOTE: 控件一个都不放也得把这栏建出来 —— 验证工程靠 leftColumnName()/rightColumnName()
    //       找这两栏量几何，主页右栏没了就等于主页的比例没地方量。

    // ---------- 实例：左 = 游戏文件夹列表；右 = 那个文件夹里的实例 ----------
    QWidget *instanceTab = new QWidget(tabWidget);
    instanceTab->setObjectName(instanceTabName());
    const TwoColumns instance = buildTwoColumnPage(instanceTab, QStringLiteral("instance"));
    {
        QGroupBox *folderGroupBox = new QGroupBox(QStringLiteral("游戏文件夹"),
                                                  instance.left.widget);
        QVBoxLayout *folderLayout = new QVBoxLayout(folderGroupBox);
        folderLayout->setContentsMargins(9, 9, 9, 9);
        folderLayout->setSpacing(10);

        // 左边这张列表就是"启动器用哪一份 .minecraft"的选择器：选中哪一项，版本、资源、
        // 库、自管 Java 就全从那一份里找（PCL 左边那几张卡片是同一个意思）。
        // 自带的那一份永远排第一（见 FolderManager），所以列表不会是空的。
        folderList = new QListWidget(folderGroupBox);
        folderList->setObjectName(folderListName());
        folderList->setUniformItemSizes(true);
        // 路径比左栏宽是常态（`D:\MC\.minecraft` 就快 20 个字符了），而左栏只有 3:10。
        // 不关横向滚动条的话，条目会按文字宽度撑开、滚轮之外还多一条横条，而文字**不会**
        // 省略 —— 关掉它，条目宽度就跟视口走，省略号才生效。从中间省略：路径两头
        // （盘符和最后一级目录名）都比中间那截有用，完整路径在 tooltip 里。
        folderList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        folderList->setTextElideMode(Qt::ElideMiddle);
        folderLayout->addWidget(folderList, 1);

        QHBoxLayout *folderButtons = new QHBoxLayout;
        addFolderButton = new QPushButton(addFolderText(), folderGroupBox);
        addFolderButton->setObjectName(addFolderButtonName());
        removeFolderButton = new QPushButton(removeFolderText(), folderGroupBox);
        removeFolderButton->setObjectName(removeFolderButtonName());
        // 「移除」**只是从列表里拿掉**，盘上的东西一个字节都不动。这句是给"不敢按"的人
        // 看的：按钮上写不下，就在悬停时说出来（同一个意思在确认框和下面那句提示里各有一遍）。
        removeFolderButton->setToolTip(
            QStringLiteral("只从列表里移除：文件夹本身和里面的文件都不会被删掉"));
        folderButtons->addWidget(addFolderButton);
        folderButtons->addWidget(removeFolderButton);
        folderLayout->addLayout(folderButtons);

        QLabel *folderHint = new QLabel(
            QStringLiteral("选中的就是启动器当前使用的 .minecraft（版本、存档、资源都从这里读）。"
                           "自带的那一份移不掉；「移除文件夹」只把它从列表里拿掉，"
                           "盘上的文件夹和文件都留着。"),
            folderGroupBox);
        folderHint->setObjectName(folderHintLabelName());
        folderHint->setWordWrap(true);
        folderHintLabel = folderHint;
        folderLayout->addWidget(folderHint);
        instance.left.layout->addWidget(folderGroupBox, 1);
    }
    {
        QGroupBox *instanceGroupBox = new QGroupBox(QStringLiteral("实例列表"),
                                                    instance.right.widget);
        QVBoxLayout *instanceLayout = new QVBoxLayout(instanceGroupBox);
        instanceLayout->setContentsMargins(9, 9, 9, 9);
        instanceLayout->setSpacing(10);

        instanceFolderLabel = new QLabel(instanceGroupBox);
        instanceFolderLabel->setObjectName(instanceFolderLabelName());
        instanceFolderLabel->setWordWrap(true);
        instanceLayout->addWidget(instanceFolderLabel);

        // 主页那个「已安装版本」下拉框的同一份数据，只是摆在这儿配左边的文件夹看。
        // 点击 = 在主页把它选中（两边按 id 双向同步，见 MainWindow::setupConnections）。
        //
        // 是**树**不是列表：实例可以归到用户建的组里，组是父节点、实例是子节点。
        // 一个组都没有的时候它就是平铺的一层（照样不插组头），观感和列表一样。
        instanceList = new QTreeWidget(instanceGroupBox);
        instanceList->setObjectName(instanceListName());
        instanceList->setHeaderHidden(true);
        instanceList->setRootIsDecorated(true);
        // 每一行高度一样（没有多行文本），打开这个能让 QTreeWidget 不用去问每一行的
        // sizeHint —— 实例多的时候（真机上有 61 个）滚动明显更顺。
        instanceList->setUniformRowHeights(true);
        // 组下面的实例缩进多少由它给。默认 20 在窄栏里有点深，收到 14：
        // 右栏还得和左栏分 7:3 的宽度，缩进多一格就少一格放实例名。
        instanceList->setIndentation(14);
        instanceLayout->addWidget(instanceList, 1);

        // 「导入整合包…」：从 Modrinth 格式的整合包（.zip / .mrpack）装出一个实例。
        // 单独一行摆在下面那三个**上面**。为什么不挤进那一排：下面那三个都是"对选中的
        // 实例做什么"（没选中会弹一句"先选中一个实例"），这个是**造**一个新实例 ——
        // 混在一排里，用户得先猜它作用于选中项还是新建。右栏又本来就窄，四个按钮挤一行
        // 会把字挤成省略号。
        importModpackButton = new QPushButton(importModpackText(), instanceGroupBox);
        importModpackButton->setObjectName(importModpackButtonName());
        // 宽度贴自己的字（不跟着这一栏长）：右栏那 7 份是分给列表的，让一个按钮去顶宽
        // 会把列表挤窄 —— 和主页那个「账号与外观…」同一个口径。
        importModpackButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        instanceLayout->addWidget(importModpackButton, 0, Qt::AlignLeft);

        // 三个按钮都作用于列表里当前选中的那一个实例。横着排：右栏宽度本来就紧
        // （3:7 里的 7 还要和左栏分），竖着堆会把列表挤没。
        QHBoxLayout *instanceButtons = new QHBoxLayout;
        renameInstanceButton = new QPushButton(renameInstanceText(), instanceGroupBox);
        renameInstanceButton->setObjectName(renameInstanceButtonName());
        groupInstanceButton = new QPushButton(groupInstanceText(), instanceGroupBox);
        groupInstanceButton->setObjectName(groupInstanceButtonName());
        instanceSettingsButton = new QPushButton(instanceSettingsText(), instanceGroupBox);
        instanceSettingsButton->setObjectName(instanceSettingsButtonName());
        // 三个按钮一律可点，没选中实例时由 MainWindow 的槽弹一句"先选中一个实例"
        // （和「重命名…」同一个口径：**几时能点**由状态决定的话，用户会先花时间
        // 猜"为什么它是灰的"，而那句话直接把原因说了）。

        instanceButtons->addWidget(renameInstanceButton);
        instanceButtons->addWidget(groupInstanceButton);
        instanceButtons->addWidget(instanceSettingsButton);
        instanceLayout->addLayout(instanceButtons);

        QLabel *instanceHint = new QLabel(
            QStringLiteral("「导入整合包…」支持 Modrinth 格式的整合包"
                           "（压缩包根目录里有一份 modrinth.index.json）。"
                           "它会把整合包整个装成一个新实例：该装的加载器、该下的模组、"
                           "该解开的 config 和资源包，都在这一次里做完。"
                           "一个实例就是 versions 下的一个目录。判据是「认得出哪个是客户端 jar」"
                           "—— 你自己在资源管理器里改过目录名的实例也认得出来。"
                           "重命名会真的改磁盘上的目录名；分组只记在启动器自己的文件里，"
                           "不动磁盘上的目录。「实例设置…」只影响这一个实例：里面每一项"
                           "都能选「跟随全局」或者自己填一个值。"),
            instanceGroupBox);
        instanceHint->setWordWrap(true);
        instanceLayout->addWidget(instanceHint);
        instance.right.layout->addWidget(instanceGroupBox, 1);
    }

    // ---------- 下载：左 = 分类入口（仿 PCL）；右 = 那个分类的内容 ----------
    //
    // 左栏 2026-09-22 从"挑加载器 + 挑版本"换成了 PCL 那种**一列分类**：游戏 / 整合包 /
    // 模组 / 资源包 / 光影。点哪个，右栏那份 QStackedWidget 就换到哪一页。
    //
    // NOTE: 现在只有「游戏」那一页是真的 —— 它就是原来那一摊（挑加载器、挑版本、下载），
    //       一个控件都没少。其余四页只有一句"还没做"（那句话的唯一出处是
    //       downloadPlaceholderText()）：入口先占住，免得下次往里加东西时又要重排整页。
    QWidget *downloadTab = new QWidget(tabWidget);
    downloadTab->setObjectName(downloadTabName());
    const TwoColumns download = buildTwoColumnPage(downloadTab, QStringLiteral("download"));
    {
        const QStringList categoryKeys = downloadCategoryKeys();
        const QStringList categoryTexts = downloadCategoryTexts();

        downloadCategoryList = new QListWidget(download.left.widget);
        downloadCategoryList->setObjectName(downloadCategoryListName());
        downloadCategoryList->setUniformItemSizes(true);
        for (int i = 0; i < categoryKeys.size(); ++i) {
            auto *item = new QListWidgetItem(categoryTexts.value(i), downloadCategoryList);
            // 身份进 userData：条目文字是显示内容，会随产品文案走，不能拿它当身份。
            item->setData(Qt::UserRole, categoryKeys.value(i));
        }
        // 高度贴着内容：五个两三个字的条目，被拉成一整条空白看着像坏了。行高**交给样式
        // 算**（sizeHintForRow），别写死数字 —— 换字体/换样式就错（UI 样式基线那件事）。
        int rowsHeight = 0;
        for (int i = 0; i < downloadCategoryList->count(); ++i) {
            rowsHeight += downloadCategoryList->sizeHintForRow(i);
        }
        downloadCategoryList->setFixedHeight(rowsHeight +
                                             2 * downloadCategoryList->frameWidth() + 2);
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
        // 字号放大一点（用户要的）。代理里那行小字是从这个字号往下推的，别在这儿单独设它。
        QFont treeFont = versionTree->font();
        treeFont.setPointSize(treeFont.pointSize() + versionTreeFontStep());
        versionTree->setFont(treeFont);
        // 单击分类就展开/折叠（用户点的是最右边那个 >，那个箭头就画在那一行上）。
        // 双击**不再**自己展开一次 —— 那会和上面那次单击叠起来，双击等于"展开又收起"。
        versionTree->setExpandsOnDoubleClick(false);
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
        installLayout->addWidget(nameBox);

        // 加载器：四个各占**一条**（用户明确要的形状 —— 每个加载器都要有地方显示
        // 自己那个版本号，横排的选项卡排不下），最后加一条「不装（纯原版）」。
        //
        // NOTE: 这一组就是原来左栏那五个钮 —— 表、下标、文案一个字没改（见
        //       modLoaderOptionNames()），只是从"孤零零五个钮"变成了"一条一个加载器，
        //       右边跟着它自己的版本"。判据仍然是 modLoaderGroup->checkedId()，
        //       所以"下标 → 加载器"那层翻译（loaderKindFromIndex）原样可用。
        QGroupBox *loaderBox = new QGroupBox(modLoaderLabelText(), installPage);
        auto *loaderLayout = new QVBoxLayout(loaderBox);
        loaderLayout->setContentsMargins(9, 9, 9, 9);
        // 每条之间挨紧一点（页面的间距是 10，那是给"标签/控件一段一段"用的；这几条是
        // 同一组里的几行，拉开反而看不出它们是一组）。
        loaderLayout->setSpacing(4);
        installLayout->addWidget(loaderBox);

        modLoaderGroup = new QButtonGroup(installPage);
        modLoaderGroup->setObjectName(modLoaderGroupName());

        const auto addLoaderRow = [&](int index) {
            auto *row = new QWidget(loaderBox);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            rowLayout->setSpacing(8);

            QRadioButton *radio = new QRadioButton(modLoaderOptionTexts().at(index), row);
            radio->setObjectName(modLoaderOptionNames().at(index));
            // 每条前面那个标志（Fabric 的织纹 / Forge 的铁砧 / NeoForge 的狐狸 /
            // OptiFine 的放大镜；「不装」那条是草方块 —— 那条路装出来的就是原版）。
            // 画法是 ui/VersionIcons.cpp，那几个图标的画法表和这张表**同序**。
            radio->setIcon(QIcon(VersionIcons::loaderMark(index, versionTreeIconSize())));
            radio->setIconSize(QSize(versionTreeIconSize(), versionTreeIconSize()));
            // id 就是两张表里的下标，接线时靠它把"勾了哪个"翻成加载器枚举。
            modLoaderGroup->addButton(radio, index);
            rowLayout->addWidget(radio);

            if (index == modLoaderNoneIndex()) {
                // 「不装（纯原版）」那条没有版本可选 —— 原版没有加载器版本。
                // NOTE: 这一条**不是凑数**：单选钮一旦勾上就没法用鼠标取消（Qt 的互斥组
                //       把"点已勾中的那个"当成什么都没发生），没有它的话用户勾过一次
                //       Fabric 就再也回不到"装纯原版"了。它**初始勾中**（见下面）。
                rowLayout->addStretch(1);
            } else {
                QComboBox *combo = new QComboBox(row);
                combo->setObjectName(loaderVersionComboName(index));
                // 占位文案（"正在获取…"）必须 ≤12 字 —— 完整原因和实测数据见上面
                // installedVersionCombo 那处：AdjustToMinimumContentsLengthWithIcon 会把
                // 超过 minimumContentsLength 的占位文案算进 sizeHint 宽度，于是这个框
                // 会在"正在获取"和"拿到版本列表"之间抽一下宽度。
                combo->setPlaceholderText(loaderVersionFetchingText());
                combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
                combo->setMinimumContentsLength(12);
                // 手一滑把一次 GB 级的下载指到别的版本上：和别的下拉框同一道闸。
                combo->installEventFilter(new WheelGuard(combo));
                // 开局四条都灰着：默认勾的是「不装（纯原版）」，谁都没被选中，
                // 那四个组合框现在管不着任何事。勾哪条、亮哪条、哪条去拉版本列表，
                // 全在 MainWindow::updateLoaderRows() 一处决定。
                combo->setEnabled(false);
                rowLayout->addWidget(combo, 1);
                // 下标就是加载器下标（0..3），和"勾了哪个"用的是同一个数 ——
                // 不另写一张"哪个组合框是哪个加载器"的表，那种表迟早会有一处对不上。
                loaderVersionCombos.append(combo);
            }
            loaderLayout->addWidget(row);
            return radio;
        };
        // NOTE: 变量名这里照抄上面那张表（第 0 个是 Fabric、第 1 个是 Forge…），
        //       顺序一乱就是"名字和内容对不上"，而那种错不会报错、只会装错加载器。
        modLoaderFabric = addLoaderRow(0);
        modLoaderForge = addLoaderRow(1);
        modLoaderNeoForge = addLoaderRow(2);
        modLoaderOptiFine = addLoaderRow(3);
        modLoaderNone = addLoaderRow(modLoaderNoneIndex());
        // 默认勾「不装」= 打开安装页时就是"装纯原版"，也是原来那个默认行为。
        modLoaderNone->setChecked(true);
        // 单选钮不吃滚轮，所以那几个钮不需要上面组合框装的 WheelGuard（见它上面那段）。

        installButton = new QPushButton(installButtonText(), installPage);
        installButton->setObjectName(installButtonName());
        installButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        installButton->setMinimumWidth(kDownloadButtonMinWidth);
        installLayout->addWidget(installButton, 0, Qt::AlignLeft);
        // 底下那一大块空着是**刻意**的：几个加载器各自展开之后的版本号有多长说不准，
        // 给按钮留出"往上顶"的余量，免得它贴着窗口底边。
        installLayout->addStretch(1);
        gameStack->addWidget(installPage);

        // 第 0 页加在**最前面** —— 下面那个循环按同一个表、同一个顺序往后加，
        // 于是"第 i 个分类 = 第 i 页"这条对应关系成立（验证工程会数一遍两边的条数）。
        downloadStack->addWidget(gamePage);

        // ---- 其余四页：入口先留着，点进去只说一句"还没做" ----
        for (int i = 0; i < categoryKeys.size(); ++i) {
            if (i == downloadCategoryGameIndex()) {
                continue;
            }
            QWidget *page = new QWidget(downloadStack);
            page->setObjectName(categoryKeys.value(i) + QStringLiteral("Page"));
            auto *pageLayout = new QVBoxLayout(page);
            pageLayout->setContentsMargins(0, 0, 0, 0);
            pageLayout->setSpacing(10);
            auto *note = new QLabel(downloadPlaceholderText(i), page);
            note->setWordWrap(true);
            note->setAlignment(Qt::AlignLeft | Qt::AlignTop);
            pageLayout->addWidget(note);
            pageLayout->addStretch(1);
            downloadStack->addWidget(page);
        }
        // 打开下载页时停在「游戏」（= 原来那个行为）。
        downloadCategoryList->setCurrentRow(downloadCategoryGameIndex());
    }

    // ---------- 设置：左 = 选项名称，右 = 选项内容 ----------
    QWidget *settingsTab = new QWidget(tabWidget);
    settingsTab->setObjectName(settingsTabName());
    setupSettingsPage(settingsTab);

    // NOTE: 顺序就是契约：homeTabIndex() == 0 靠的就是"主页第一个 addTab"。
    tabWidget->addTab(homeTab, QStringLiteral("主页"));
    tabWidget->addTab(instanceTab, QStringLiteral("实例"));
    tabWidget->addTab(downloadTab, QStringLiteral("下载"));
    tabWidget->addTab(settingsTab, QStringLiteral("设置"));
    mainLayout->addWidget(tabWidget, 1);

    setupBottomBar(centralWidget, mainLayout);

    // 空的菜单栏：窗口上那条菜单现在什么都没挂。
    // TODO: 菜单里放"打开 .minecraft 目录 / 关于"这类动作；设置项已经全部搬进
    //       「设置」选项卡了（以前这上面挂着"做设置界面"的待办）。
    parent->setMenuBar(new QMenuBar(parent));
}

void MainWindowUI::setupSettingsPage(QWidget *page) {
    // 设置项多起来之后一屏放不下（账户 / Java / 内存 / 启动后 / JVM 参数 / 下载源 /
    // 版本名格式），所以套一层滚动区：窗口初始高度只有 620（只 resize 过、没有
    // setFixedSize），不给滚动就等于把最后那几项一直藏在可视区外，用户不拖大窗口
    // 根本看不见。
    QScrollArea *scroll = new QScrollArea(page);
    scroll->setObjectName(settingsScrollName());
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    QWidget *form = new QWidget(scroll);
    form->setObjectName(settingsFormName());

    QGridLayout *grid = new QGridLayout(form);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(10);
    // 「选项名称放左边、设置内容放右边」的比例就是这一对 3:7。
    grid->setColumnStretch(0, leftColumnStretch());
    grid->setColumnStretch(1, rightColumnStretch());
    scroll->setWidget(form);

    int row = 0;

    // ---------- 账户 ----------
    // 这一格原来摆的是个「微软登录」按钮（登录过之后叫「重新登录」）。它**搬进**
    // 「账号与外观」那个窗里了：那个按钮只会登录一件事 —— 加离线账号、切换账号、删账号、
    // 换皮肤披风它一件都做不到，而这一格却叫「当前账户」，用户来设置页找"管账号"的地方
    // 会撞上一个只能登录的按钮。账号那一整块只在一个窗里做（ui/AccountSkinDialog），
    // 这里留一个入口。
    addSectionTitle(grid, row++, QStringLiteral("账户"), form);
    {
        QWidget *box = makeHintedControl(form);
        auto *boxLayout = qobject_cast<QVBoxLayout *>(box->layout());
        accountSkinSettingsButton = new QPushButton(accountSkinButtonText(), box);
        // 文案和主页那个入口**共用一份**（accountSkinButtonText()，见上面那段说明）。
        accountSkinSettingsButton->setObjectName(accountSkinSettingsButtonName());
        accountSkinSettingsButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        accountSkinSettingsButton->setMinimumWidth(kAccountSkinButtonMinWidth);
        boxLayout->addWidget(accountSkinSettingsButton, 0, Qt::AlignLeft);
        addHint(box, QStringLiteral("正版登录、添加离线账号、切换账号、换皮肤和披风都在这个窗里。"
                                    "登录后用户名显示在窗口左下角，启动游戏时用的也是它。"),
                form);
        addSettingRow(grid, row++, QStringLiteral("账号"), box, form);
    }

    // ---------- Java 版本 ----------
    addSectionTitle(grid, row++, QStringLiteral("Java 版本"), form);
    {
        QWidget *box = makeHintedControl(form);
        auto *boxLayout = qobject_cast<QVBoxLayout *>(box->layout());
        javaVersionCombo = new QComboBox(box);
        javaVersionCombo->setObjectName(javaVersionComboName());
        javaVersionCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        javaVersionCombo->setMinimumContentsLength(12);
        javaVersionCombo->installEventFilter(new WheelGuard(javaVersionCombo));
        boxLayout->addWidget(javaVersionCombo);
        javaPathLabel = new QLabel(box);
        javaPathLabel->setObjectName(javaPathLabelName());
        javaPathLabel->setWordWrap(true);
        javaPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        boxLayout->addWidget(javaPathLabel);
        // NOTE: 这一句说的是**全局**这一层的选择，它和实例覆盖层的行为**不一样**，别把
        //       两边写成一回事：这里钉的 Java 对不上就是直接报错（不替你换、也不问）；
        //       而「实例设置…」里给某个实例单独指定的 Java 对不上时，会弹一个框问要不要
        //       改用匹配的那个（由下面那行提醒开关管）。全局档不问，是因为它不匹配时
        //       自动档本来就会挑一个替换掉，手动档是用户对**整台机器**的选择 ——
        //       那种一眼就该是自己配错的，不值得每次启动都问一遍。
        addHint(box, QStringLiteral("「自动」是按版本要求挑（1.16.5 及以下要 Java 8、"
                                    "26.x 要 Java 25，同一个启动器里得能共存）。"
                                    "这一行选的是「全局」那一层：选了具体的 Java 就别指望"
                                    "它会替你换一个 —— 版本对不上会直接报错，不会静默换一个。"
                                    "要给某一个实例单独指定 Java，用它的「实例设置…」；"
                                    "那边对不上时会弹框问一句（由下面「版本提醒」管）。"),
                form);
        addSettingRow(grid, row++, QStringLiteral("运行时"), box, form);
    }
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
        addSettingRow(grid, row++, QStringLiteral("版本提醒"), box, form);
    }

    // ---------- 内存分配 ----------
    addSectionTitle(grid, row++, QStringLiteral("内存分配"), form);
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
        addSettingRow(grid, row++, QStringLiteral("分配方式"), box, form);
    }
    {
        QWidget *box = makeHintedControl(form);
        auto *boxLayout = qobject_cast<QVBoxLayout *>(box->layout());
        memoryMaxSpin = new QSpinBox(box);
        memoryMaxSpin->setObjectName(memoryMaxSpinName());
        memoryMaxSpin->setRange(memoryMaxMinMb(), memoryLimitMaxMb());
        memoryMaxSpin->setSingleStep(memoryStepMb());
        memoryMaxSpin->setSuffix(QStringLiteral(" MB"));
        // 数字框也吃滚轮（加一步/减一步），而且没有焦点时改完**看不出来** —— 见 WheelGuard。
        memoryMaxSpin->installEventFilter(new WheelGuard(memoryMaxSpin));
        addInlineControl(boxLayout, memoryMaxSpin, kMemorySpinWidth);
        addHint(box, QStringLiteral("给 JVM 的 -Xmx。模组多、整合包大就往上调；"
                                    "机器内存不够时先降这一项。"),
                form);
        addSettingRow(grid, row++, QStringLiteral("最大内存"), box, form);
    }
    {
        QWidget *box = makeHintedControl(form);
        auto *boxLayout = qobject_cast<QVBoxLayout *>(box->layout());
        memoryMinSpin = new QSpinBox(box);
        memoryMinSpin->setObjectName(memoryMinSpinName());
        memoryMinSpin->setRange(memoryMinMinMb(), memoryLimitMaxMb());
        memoryMinSpin->setSingleStep(memoryStepMb());
        memoryMinSpin->setSuffix(QStringLiteral(" MB"));
        // 同上：滚轮防护（见 WheelGuard）。
        memoryMinSpin->installEventFilter(new WheelGuard(memoryMinSpin));
        addInlineControl(boxLayout, memoryMinSpin, kMemorySpinWidth);
        addHint(box, QStringLiteral("给 JVM 的 -Xms。设成和最大内存一样可以少几次扩容，"
                                    "但会一直占着那些内存。"),
                form);
        addSettingRow(grid, row++, QStringLiteral("最小内存"), box, form);
    }

    // ---------- 启动后 ----------
    addSectionTitle(grid, row++, QStringLiteral("启动后"), form);
    {
        QWidget *box = makeHintedControl(form);
        auto *boxLayout = qobject_cast<QVBoxLayout *>(box->layout());
        launcherBehaviourCombo = new QComboBox(box);
        launcherBehaviourCombo->setObjectName(launcherBehaviourComboName());
        launcherBehaviourCombo->installEventFilter(new WheelGuard(launcherBehaviourCombo));
        boxLayout->addWidget(launcherBehaviourCombo);
        addHint(box, QStringLiteral("游戏起来之后启动器自己怎么办。「收起」是把窗口藏起来、"
                                    "进程还在（关掉进程会连带杀掉游戏）。"),
                form);
        addSettingRow(grid, row++, QStringLiteral("启动器行为"), box, form);
    }

    // ---------- JVM 参数 ----------
    addSectionTitle(grid, row++, QStringLiteral("JVM 参数"), form);
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

        addHint(box, QStringLiteral("多条参数用空格分隔。改完点「应用」生效 —— "
                                    "不边打字边存，免得打到一半的半条参数被写进配置。"
                                    "按版本的那份写在 .minecraft/versions/<版本>/wtfml.meta "
                                    "的 jvmArgs 里，排在全局之后（同一条以后写的为准）。"),
                form);
        addSettingRow(grid, row++, QStringLiteral("全局额外参数"), box, form);
    }

    // ---------- 下载源 ----------
    addSectionTitle(grid, row++, QStringLiteral("下载源"), form);
    {
        QWidget *box = makeHintedControl(form);
        auto *boxLayout = qobject_cast<QVBoxLayout *>(box->layout());
        downloadSourceCombo = new QComboBox(box);
        downloadSourceCombo->setObjectName(downloadSourceComboName());
        downloadSourceCombo->installEventFilter(new WheelGuard(downloadSourceCombo));
        boxLayout->addWidget(downloadSourceCombo);
        addHint(box, QStringLiteral("镜像源是第三方服务（BMCLAPI），国内通常快很多。"
                                    "下回来的文件不对时可以换回官方源重下。"),
                form);
        addSettingRow(grid, row++, QStringLiteral("来源"), box, form);
    }

    // ---------- 版本名格式 ----------
    addSectionTitle(grid, row++, QStringLiteral("版本名格式"), form);
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
        addSettingRow(grid, row++, QStringLiteral("显示哪些字段"), box, form);
    }

    // 剩下的竖直空间留在底部：设置项不跟着窗口高度一起被拉长
    grid->setRowStretch(row, 1);

    QVBoxLayout *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);
    pageLayout->addWidget(scroll);
}

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
    bottomBar->setAutoFillBackground(true);
    QPalette barPalette = bottomBar->palette();
    barPalette.setColor(QPalette::Window, barPalette.color(QPalette::Window).darker(105));
    bottomBar->setPalette(barPalette);

    QHBoxLayout *barLayout = new QHBoxLayout(bottomBar);
    barLayout->setContentsMargins(12, 6, 12, 6);
    barLayout->setSpacing(12);

    // ---------- 左：当前实例运行状态 ----------
    // 第一行是状态正文，第二行是实例名。**在主页且选中的就是正在跑的那个实例**时
    // 只显示第一行 —— 那种情况下用户眼前就是那个实例，再说一遍名字是废话。
    runStateLabel = new QLabel(bottomBar);
    runStateLabel->setObjectName(runStateLabelName());
    // 贴在底栏最左边，所以文字左对齐（这两行原来在右端，靠的是右对齐）
    runStateLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    runStateDetailLabel = new QLabel(bottomBar);
    runStateDetailLabel->setObjectName(runStateDetailLabelName());
    runStateDetailLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // 第二行小一号、灰一点，跟第一行区分开。字号按当前字号减 1（不用点值，
    // 免得不支持点值的平台出现负字号）；颜色用调色板里的 PlaceholderText。
    QFont detailFont = runStateDetailLabel->font();
    detailFont.setPointSize(qMax(1, detailFont.pointSize() - 1));
    runStateDetailLabel->setFont(detailFont);
    QPalette detailPalette = runStateDetailLabel->palette();
    detailPalette.setColor(QPalette::WindowText, detailPalette.color(QPalette::PlaceholderText));
    runStateDetailLabel->setPalette(detailPalette);

    QVBoxLayout *runStateLayout = new QVBoxLayout();
    runStateLayout->setSpacing(0);
    runStateLayout->addWidget(runStateLabel);
    runStateLayout->addWidget(runStateDetailLabel);
    // 没有实例在跑时左下角什么都不显示（不占着一句"未运行"）
    runStateLabel->hide();
    runStateDetailLabel->hide();

    // ---------- 中：进度（启动优先于下载） ----------
    statusLabel = new QLabel(bottomBar);
    statusLabel->setObjectName(statusLabelName());
    // 固定一个最小宽度：文件名长短变化时这一格不会再跳来跳去
    statusLabel->setMinimumWidth(kStatusLabelMinWidth);
    progressBar = new QProgressBar(bottomBar);
    progressBar->setObjectName(progressBarName());
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setTextVisible(true);

    // 文字在上、进度条在下：底栏只有 56 px 高，横着并排会把进度条挤得太短
    QVBoxLayout *progressLayout = new QVBoxLayout();
    progressLayout->setSpacing(2);
    progressLayout->addWidget(statusLabel);
    progressLayout->addWidget(progressBar);
    // 富余宽度**不完全**给中间那格：以前左右两格按自身宽度、中间那格吃掉全部富余，
    // 900 px 的窗口下进度条横跨大半个底栏（实测 789 px，占底栏 88%），太长了。
    // 现在拉伸因子写成 4 : 1 : 1 —— 中间那格拿 4/(1+4+1) = 2/3 的富余宽度
    // （实测 526 px = 789 的 2/3），左右各一份空档各自留白，所以它仍然居中，
    // 窗口拉宽时按同样比例跟着长。
    // NOTE: 别把中间那格写成 2 —— Qt 分配富余宽度是按拉伸因子直接切那个池子的，
    //       不是"先满足最小宽度再分剩下的"，2:1:1 拿到的是 1/2 而不是 2/3。
    // 一开始什么都没在下：整格藏起来（"没在下载就看不见进度条"）
    statusLabel->hide();
    progressBar->hide();

    // ---------- 右：头像 + 用户名 ----------
    // 真正的手绘皮肤头像要等 Minecraft profile API（登录流程现在只存了令牌，没取
    // 玩家档案），这里先画一块"首字母方块"。颜色同样取自调色板，不写死。
    // NOTE: 刻意**不加 border-radius** —— 这块是方的。真皮肤头像是 8x8 的方块贴图，
    //       方形底比圆形底更接近它以后的样子。
    avatarLabel = new QLabel(QStringLiteral("?"), bottomBar);
    avatarLabel->setObjectName(avatarLabelName());
    avatarLabel->setFixedSize(kAvatarSize, kAvatarSize);
    avatarLabel->setAlignment(Qt::AlignCenter);
    const QColor avatarBackground = bottomBar->palette().color(QPalette::Highlight);
    const QColor avatarForeground = bottomBar->palette().color(QPalette::HighlightedText);
    avatarLabel->setStyleSheet(QStringLiteral("QLabel { background-color: %1; color: %2; }")
                                   .arg(avatarBackground.name(), avatarForeground.name()));

    accountLabel = new QLabel(QStringLiteral("?"), bottomBar);
    accountLabel->setObjectName(accountLabelName());

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

    QHBoxLayout *accountRow = new QHBoxLayout();
    accountRow->setSpacing(8);
    accountRow->addWidget(avatarLabel);
    // 头像和那两行文字按竖直居中对齐（QHBoxLayout 默认就是这个），所以名字多一行之后
    // 那块方头像不会往上看。
    accountRow->addLayout(accountTextLayout);

    // 三格从左到右摆：实例状态（0）· 空档（1）· 进度（4）· 空档（1）· 账户（0）。
    // 两头那两格都按自身宽度靠边站，份数写 0；富余宽度全在中间三份里分。
    barLayout->addLayout(runStateLayout, 0);
    barLayout->addStretch(1);
    barLayout->addLayout(progressLayout, 4);
    barLayout->addStretch(1);
    barLayout->addLayout(accountRow, 0);

    mainLayout->addWidget(bottomBar);
}
