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

/// 进度文字那格（"已下载 1234 / 3580 xxx.jar"）的宽度下限，免得**文件名一换整条就左右跳**。
/// 它管的是底栏**中间那一格自己**的宽度，不是左右两格的 —— 左右那两格用的是
/// MainWindowUI::bottomBarRunStateColumnWidth() / bottomBarAccountColumnWidth()。
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

/// 「游戏窗口」那一行两个数字框的宽度：装得下「7680」（上限）加一对箭头就够了 ——
/// 它没有后缀，比 kMemorySpinWidth 窄一些才不像两个空盒子。
constexpr int kWindowSizeSpinWidth = 90;

/// 「游戏窗口」那一行中间那个「×」。
constexpr int kWindowSizeRowSpacing = 8;

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

/// 和 addDynamicHint() 同一个形状，但字是**红的** —— 名字框底下那行"名字不能用"的原因
/// 用的就是它（大小、换行、摆放都和旁边那行灰字一套）。
QLabel *addDynamicErrorHint(QWidget *box, QWidget *parent) {
    QLabel *hint = addDynamicHint(box, QString(), parent);
    QPalette errorPalette = hint->palette();
    errorPalette.setColor(QPalette::WindowText, MainWindowUI::installNameErrorColor());
    hint->setPalette(errorPalette);
    return hint;
}

/// 往 makeHintedControl() 造出来的容器里加一句说明。
void addHint(QWidget *box, const QString &text, QWidget *parent) {
    addDynamicHint(box, text, parent);
}

/**
 * @brief 放不下时把尾巴截成省略号的 QLabel。
 *
 * 底栏那三行会变的文字（运行状态正文 / 实例名那行 / 用户名）用它。**为什么非它不可**：
 * 左边那格被钉死了固定宽度、右边那格封了上限（MainWindowUI 的
 * bottomBarRunStateColumnWidth() / bottomBarAccountColumnWidth()），
 * 而 QLabel 在控件比文字窄的时候是**硬裁**的 —— 最后一个字被切掉一半、右边什么都没有，
 * 看着像画坏了。也没法靠 QLabel 自己解决：翻遍 qlabel.h 都没有 elide 相关的 API。
 *
 * sizeHint() **不受那三行文字的影响**（宽度被 maxWidth 钉住），这样旁边那根进度条
 * 就不会跟着文字长短伸缩；超出去的只影响"画出来什么"，不影响布局。
 */
class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(QWidget *parent = nullptr) : QLabel(parent) {}

    QSize sizeHint() const override {
        const QSize full = QLabel::sizeHint();
        // 宽度上限由 setMaximumWidth() 给。**没设过时它是 QWIDGETSIZE_MAX（16777215），
        // 不是 -1** —— 拿"有没有设小上限"当判据，别拿"是不是负数"。
        const int cap = maximumWidth();
        if (cap >= QWIDGETSIZE_MAX || full.width() <= cap) {
            return full;
        }
        return QSize(cap, full.height());
    }

protected:
    void paintEvent(QPaintEvent *) override {
        // NOTE: 这里**每次都重算**，不缓存。缓存（"宽度没变就接着用上次那串"）看着是省事，
        //       但这几个格子的宽度是**钉死的**：文字改了宽度也不改，resizeEvent 压根不触发
        //       —— 缓存就永远不失效，第一句话会一直挂在底栏上（「实例运行中」在三个实例
        //       都跑着的时候还是「实例运行中」）。
        //       elidedText() 只在重画时算一次，底栏这几行重画并不频繁，不值当为它冒这个险。
        const QRect box = contentsRect();
        const QString shown = fontMetrics().elidedText(text(), Qt::ElideRight, box.width());
        QPainter painter(this);
        painter.setPen(palette().color(foregroundRole()));
        painter.drawText(box, alignment() | Qt::TextSingleLine, shown);
    }
};

}  // namespace

MainWindowUI::MainWindowUI(QMainWindow *parent) { setupUi(parent); }

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
        WheelGuard::install(installedVersionCombo);
        installedVersionCombo->setObjectName(installedVersionComboName());
        home.left.layout->addWidget(installedVersionCombo);

        startButton = new QPushButton(QStringLiteral("启动游戏"), home.left.widget);
        // 文字是两行（"启动游戏\n<显示名>"，由 MainWindow::updateStartButtonText() 维护；
        // 高度固定给足，免得两行被压成一行半。
        startButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        startButton->setMinimumHeight(kStartButtonMinHeight);
        home.left.layout->addWidget(startButton);

        // 这里原来有一个「离线模式」勾选框（2026-09-24 去掉的，用户原话：「现在不要启动
        // 界面那个离线登录勾选框了，在添加账户那里选择添加离线账号」）。它管的是"这次启动
        // 用哪种身份"，而现在那件事**只由当前账号是什么**决定 —— 离线就去账号窗里加一个
        // 离线账号、切过去，没有第二个开关。判据的唯一出处是 MainWindow::offlineIdentity()。

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
        //
        // NOTE: 每一行的内容**不是**条目自己画的（条目上没有文字），而是 setItemWidget()
        //       塞进去的一个 FolderRowWidget（见 setFolderRows()）——两行字 + 行尾一颗
        //       齿轮需要一个真控件才摆得下。所以下面那两条"文字怎么省略"的老设置现在只管
        //       "别冒出横向滚动条"这一件事：省略由那一行里的 ElidedLabel 自己做（从中间
        //       省略：路径两头 —— 盘符和最后一级目录名 —— 都比中间那截有用）。
        folderList = new QListWidget(folderGroupBox);
        folderList->setObjectName(folderListName());
        folderList->setUniformItemSizes(true);
        folderList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        folderList->setTextElideMode(Qt::ElideMiddle);
        folderList->setSelectionMode(QAbstractItemView::SingleSelection);
        folderLayout->addWidget(folderList, 1);

        // 这一行**只有「添加文件夹…」一个按钮**了：2026-09-24 用户说「下面那个移除文件夹
        // 也可以弃掉了」—— 「移除」现在只有行尾齿轮菜单里那一项（同一个
        // MainWindow::removeFolderFromList()，同一个确认框）。
        QHBoxLayout *folderButtons = new QHBoxLayout;
        addFolderButton = new QPushButton(addFolderText(), folderGroupBox);
        addFolderButton->setObjectName(addFolderButtonName());
        folderButtons->addWidget(addFolderButton);
        folderLayout->addLayout(folderButtons);

        // 原来这里还有一段提示（"选中的就是启动器当前使用的 .minecraft…自带的那一份
        // 移不掉…"），2026-09-24 用户说「下面那个文本也删了」，一起拿掉了。
        // 它那两句话现在各有各的出处，一件都没丢：
        //   ① 「这一份就是当前在用的」—— 右边「实例列表」那一组顶上那句
        //      instanceFolderLabel 就写着当前是哪一份；
        //   ② 「移除只动列表、删除才动盘」—— 齿轮菜单那两项各自的 tooltip
        //      （folderActionTooltip）以及它们弹的那个确认框里各写了一遍。
        // NOTE: 顺带解掉了一个耦合。那段话是折行的，而**折行 QLabel 的 sizeHint 宽度是
        //       横向 advance 的分档**（不是线性）：2026-09-24 实测把它写到 1260 px 宽时，
        //       它自己的 sizeHint 从 240 跳到 480、**整窗**的 sizeHint 从 818 顶到 1042，
        //       ui_check 的 E5（整窗 sizeHint ≤ 900）当场红。现在左栏里没有折行标签了，
        //       那条断言重新有了一大截余量 —— 但仍然别在这里加长句子（E5 量的还是整窗）。
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

        // 四个按钮都作用于列表里当前选中的那一个实例。横着排：右栏宽度本来就紧
        // （3:7 里的 7 还要和左栏分），竖着堆会把列表挤没。
        QHBoxLayout *instanceButtons = new QHBoxLayout;
        renameInstanceButton = new QPushButton(renameInstanceText(), instanceGroupBox);
        renameInstanceButton->setObjectName(renameInstanceButtonName());
        groupInstanceButton = new QPushButton(groupInstanceText(), instanceGroupBox);
        groupInstanceButton->setObjectName(groupInstanceButtonName());
        instanceSettingsButton = new QPushButton(instanceSettingsText(), instanceGroupBox);
        instanceSettingsButton->setObjectName(instanceSettingsButtonName());
        stopGameButton = new QPushButton(stopGameText(), instanceGroupBox);
        stopGameButton->setObjectName(stopGameButtonName());
        // 四个按钮一律可点，没选中实例（或者那个实例没在跑）时由 MainWindow 的槽弹一句
        // 说清原因（和「重命名…」同一个口径：**几时能点**由状态决定的话，用户会先花时间
        // 猜"为什么它是灰的"，而那句话直接把原因说了）。

        instanceButtons->addWidget(renameInstanceButton);
        instanceButtons->addWidget(groupInstanceButton);
        instanceButtons->addWidget(instanceSettingsButton);
        instanceButtons->addWidget(stopGameButton);
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
    // NOTE: 2026-09-23 起**五页全是真的**了。「游戏」那一页就是原来那一摊（挑加载器、挑版本、
    //       下载），一个控件都没少；其余四页是"一个搜索框 + 一棵三层树"（资源 → 游戏版本 →
    //       版本号），在这里一段循环里搭出来（见下面那段）。原来那四句"还没做"的话连同
    //       downloadPlaceholderText() 一起删掉了 —— 留着就是四页在说假话。
    QWidget *downloadTab = new QWidget(tabWidget);
    downloadTab->setObjectName(downloadTabName());
    const TwoColumns download = buildTwoColumnPage(downloadTab, QStringLiteral("download"));
    {
        const QStringList categoryKeys = downloadCategoryKeys();
        const QStringList categoryTexts = downloadCategoryTexts();

        downloadCategoryList = new QListWidget(download.left.widget);
        downloadCategoryList->setObjectName(downloadCategoryListName());
        downloadCategoryList->setUniformItemSizes(true);
        // 2026-09-23：这一列换成**和上面两棵树同一个代理**（用户原话："下载界面左边栏
        // 每一项做大点，每一项的右边放一个刷新按钮"）。换代理同时办成两件事：
        //   ① 行高从样式那个"一行文字"（实测 ~24 px）变成 versionTreeRowHeight()（46），
        //      字号也跟着下面那次 +versionTreeFontStep() 放大 —— "做大点"就是这两条；
        //   ② 行尾那一格本来就是这个代理画箭头的活，加个刷新字形（rowRefreshRole()）
        //      不用另起一套控件：QListWidget 的 itemWidget 会盖住选中高亮那层背景，
        //      那样"现在停在哪一类"就看不出来了。
        // NOTE: 条目文字是**粗体**（代理对顶层行一律加粗，见 VersionTreeDelegate::paint）——
        //       和版本树那四条分类一个长相，这一列本来也就是"分类"。
        downloadCategoryList->setItemDelegate(new VersionTreeDelegate(downloadCategoryList));
        QFont categoryFont = downloadCategoryList->font();
        categoryFont.setPointSize(categoryFont.pointSize() + versionTreeFontStep());
        downloadCategoryList->setFont(categoryFont);
        for (int i = 0; i < categoryKeys.size(); ++i) {
            auto *item = new QListWidgetItem(categoryTexts.value(i), downloadCategoryList);
            // 身份进 userData：条目文字是显示内容，会随产品文案走，不能拿它当身份。
            item->setData(Qt::UserRole, categoryKeys.value(i));
            // 每个条目右边都有那个刷新按钮（五条一条不落）。点它的那一下由
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
        // 图标尺寸：画是代理画的（它自己按 versionTreeIconSize 缩放），这里设的是**视图
        // 认下的那个尺寸** —— 装上安装页那棵加载器树之后，这两条竖的设置必须成对地
        // 一样（验证工程拿"两棵树这三条相等"钉"两边是同一个模式"）。
        versionTree->setIconSize(QSize(versionTreeIconSize(), versionTreeIconSize()));
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
        QGroupBox *loaderBox = new QGroupBox(modLoaderLabelText(), installPage);
        auto *loaderLayout = new QVBoxLayout(loaderBox);
        loaderLayout->setContentsMargins(9, 9, 9, 9);
        installLayout->addWidget(loaderBox);

        loaderTree = new QTreeWidget(loaderBox);
        loaderTree->setObjectName(loaderTreeName());
        // 竖的那几条设置和版本树那边**一模一样**（表头藏起来、行高常数、左边那个小三角
        // 不画、同一个代理、同一个字号偏移）—— 用户要的就是"和上一页相同的模式"，
        // 两边长得不一样的话那句话就只兑现了一半。
        loaderTree->setHeaderHidden(true);
        loaderTree->setUniformRowHeights(true);
        loaderTree->setRootIsDecorated(false);
        loaderTree->setColumnCount(1);
        // 末一个参数 false：这棵树的**叶子**（两档 + 具体版本号）右边不画那个 `>` ——
        // 那一层是"选中它"，底下没有下一页（见 VersionTreeDelegate 的构造函数）。
        // 顶层那五条照画（它们是真的能展开）。
        loaderTree->setItemDelegate(new VersionTreeDelegate(loaderTree, /*leafArrow=*/false));
        loaderTree->setExpandsOnDoubleClick(false);
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
        // 用户原话"安装键做大点在右侧居中"：字放大两档、给一个最小高度（字大了还顶着
        // 老高度会显得挤），再在**这一页上横向居中**摆（安装页就是右栏，所以居中就是
        // "在这一栏里居中"）。垂直方向仍然靠底下那个 stretch 顶上去。
        QFont installFont = installButton->font();
        installFont.setPointSize(installFont.pointSize() + installButtonFontStep());
        installButton->setFont(installFont);
        installButton->setMinimumHeight(installButtonMinHeight());
        installLayout->addWidget(installButton, 0, Qt::AlignHCenter);
        // 底下那一大块空着是**刻意**的：加载器那条展开之后的版本号有多长说不准，
        // 给按钮留出"往上顶"的余量，免得它贴着窗口底边。
        installLayout->addStretch(1);
        gameStack->addWidget(installPage);

        // 第 0 页加在**最前面** —— 下面那个循环按同一个表、同一个顺序往后加，
        // 于是"第 i 个分类 = 第 i 页"这条对应关系成立（验证工程会数一遍两边的条数）。
        downloadStack->addWidget(gamePage);

        // ---- 其余四页：一个搜索框 + 一棵三层树（资源 → 游戏版本 → 版本号）----
        //
        // 四页的控件**一模一样**，只有搜索框里那句提示文字不同，所以这里是循环四遍，
        // 不是抄四份。三层的那条路就是用户描述的那条（见头文件里 downloadResourceTreeName()
        // 上面那段）：
        //   第一层 资源 → 点开是游戏版本 → 点开某个游戏版本**这时才去问接口** → 第三层版本号
        // 关键是"什么在哪儿灌"：这一层只**搭**（空树），三条路的数据都由 MainWindow 灌
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

            // 上面那一条：搜索框。**回车就搜** —— 用户原话是"上方预留搜索功能（如果简单就
            // 直接做了）"，那就做成能用的：一个输入框 + 回车，不另加按钮。
            // NOTE: 不加"清除"那个小叉（setClearButtonEnabled）：它在输入框里塞一个真的
            //       子按钮，而这个页面上的控件形状是被验证工程一条一条数着的。
            //       提示文字是那页"能干什么"的一句话（downloadSearchHint，唯一出处）——
            //       空框里那句话正好在最该看见它的地方。
            auto *search = new QLineEdit(page);
            search->setObjectName(downloadSearchEditName(categoryKey));
            search->setPlaceholderText(downloadSearchHint(i));
            pageLayout->addWidget(search);

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
            tree->setExpandsOnDoubleClick(false);
            QFont resourceFont = tree->font();
            resourceFont.setPointSize(resourceFont.pointSize() + versionTreeFontStep());
            tree->setFont(resourceFont);
            tree->setIconSize(QSize(resourceTreeIconSize(), resourceTreeIconSize()));
            pageLayout->addWidget(tree, 1);

            downloadResourceSearches.insert(categoryKey, search);
            downloadResourceTrees.insert(categoryKey, tree);
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
        WheelGuard::install(javaVersionCombo);
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
        WheelGuard::install(memoryMaxSpin);
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
        WheelGuard::install(memoryMinSpin);
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
        WheelGuard::install(launcherBehaviourCombo);
        boxLayout->addWidget(launcherBehaviourCombo);
        addHint(box, QStringLiteral("游戏起来之后启动器自己怎么办。「收起」是把窗口藏起来、"
                                    "进程还在（关掉进程会连带杀掉游戏）。"),
                form);
        addSettingRow(grid, row++, QStringLiteral("启动器行为"), box, form);
    }

    // ---------- 启动选项 ----------
    // 这一组三件事都是"这一份游戏进程怎么起来"：看不看它的输出、它占多少 CPU、
    // 窗口开多大。摆在「启动后」和「JVM 参数」中间是因为它们的**作用对象**是启动那一下
    // （上面那几组分别是账户、Java、内存、启动器自己）。
    addSectionTitle(grid, row++, QStringLiteral("启动选项"), form);
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
        addSettingRow(grid, row++, showGameLogCheckText(), box, form);
    }
    {
        QWidget *box = makeHintedControl(form);
        auto *boxLayout = qobject_cast<QVBoxLayout *>(box->layout());
        processPriorityCombo = new QComboBox(box);
        processPriorityCombo->setObjectName(processPriorityComboName());
        WheelGuard::install(processPriorityCombo);
        boxLayout->addWidget(processPriorityCombo);
        addHint(box, QStringLiteral("游戏进程占 CPU 的优先级。这和任务管理器里「设置优先级」"
                                    "是同一件事，只是在这里设一次就一直生效。"
                                    "往低调可以在后台跑着游戏的时候让系统先顾别的；"
                                    "往高调**不解决卡顿** —— 卡顿多半是内存或显卡的事，"
                                    "调高只会让别的程序更卡。"),
                form);
        addSettingRow(grid, row++, QStringLiteral("进程优先级"), box, form);
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
        QLabel *timesLabel = new QLabel(QStringLiteral("×"), sizeRow);
        timesLabel->setObjectName(gameWindowSizeTimesLabelName());
        sizeLayout->addWidget(timesLabel);
        gameWindowHeightSpin = new QSpinBox(sizeRow);
        gameWindowHeightSpin->setObjectName(gameWindowHeightSpinName());
        gameWindowHeightSpin->setRange(windowMinHeight(), windowMaxHeight());
        gameWindowHeightSpin->setSingleStep(windowSizeStep());
        gameWindowHeightSpin->setMinimumWidth(kWindowSizeSpinWidth);
        WheelGuard::install(gameWindowHeightSpin);
        sizeLayout->addWidget(gameWindowHeightSpin);
        // 整行按 sizeHint 靠左摆，不撑满右列（右列有 7 份宽，两个数字框被拉开就是坏的）——
        // 和 addInlineControl() 是同一条规矩，只是这里那一格是整个小容器而不是单个控件。
        boxLayout->addWidget(sizeRow, 0, Qt::AlignLeft);
        addHint(box, QStringLiteral("启动时按这个大小开窗（单位是像素，走的是原版自己的 "
                                    "--width/--height 那两个参数）。默认的 854×480 就是"
                                    "原版的默认值 —— 没动过这一项时和以前一模一样。"
                                    "游戏内自己把窗口拖大过的话，以游戏记住的为准。"),
                form);
        addSettingRow(grid, row++, QStringLiteral("窗口大小"), box, form);
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
        WheelGuard::install(downloadSourceCombo);
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

    /// @param anchor 贴着哪颗按钮弹（也决定"至少和按钮一样宽"）。
    /// @param alignRight true = 右边缘和按钮右边缘对齐（右端那颗按钮用），
    ///                   false = 左边缘对齐（左端那颗用）。
    /// @param rows 面板里的几行（数据由 MainWindow 翻好，见 setBottomBarPanelContent()）。
    /// @param onAction 行尾的按钮被点了该告诉谁（可以给空的 = 这些按钮点了没反应）。
    BottomBarPanel(QWidget *anchor, bool alignRight,
                   const QList<MainWindowUI::PanelRow> &rows, const ActionCallback &onAction,
                   QWidget *parent)
        : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint), m_anchor(anchor),
          m_alignRight(alignRight) {
        setObjectName(MainWindowUI::bottomBarPanelName());
        // 底色比底栏再深一档：它是压在内容区上面的一块，得从底下那堆东西里分出来。
        // **不写死颜色**：从当前调色板推出来，浅色/深色主题下都成立（和底栏同一个路数）。
        setAutoFillBackground(true);
        QPalette panelPalette = palette();
        panelPalette.setColor(QPalette::Window, panelPalette.color(QPalette::Window).darker(112));
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
            move(x, anchorTopLeft.y() - height() - kPanelGap);
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

    /**
     * @brief 点在**自己那颗按钮**上 = "再点一次收回"，这里自己把面板关掉。
     *
     * NOTE: 这一下也可能是被 Qt 重放给按钮的（见 setupBottomBar 里 togglePanel 那段），
     *       走哪条路都通向"收回去"：两条路都只关不开，所以怎么点都不会多冒出一块面板。
     */
    void mousePressEvent(QMouseEvent *event) override {
        if (m_anchor != nullptr &&
            m_anchor->rect().contains(m_anchor->mapFromGlobal(event->globalPosition().toPoint()))) {
            close();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
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
};

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
}

/// 按钮右边那个**朝上**的小箭头（点开是向上弹的，所以箭头朝上）。画的，不是图片文件。
QLabel *makeChevronLabel(QWidget *parent) {
    const int size = MainWindowUI::bottomBarChevronSize();
    QLabel *label = new QLabel(parent);
    label->setObjectName(MainWindowUI::bottomBarChevronName());
    label->setPixmap(VersionIcons::chevronUp(size, parent->palette().color(QPalette::WindowText)));
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
        QLabel *subtitle = new ElidedLabel(this);
        subtitle->setObjectName(MainWindowUI::folderRowSubtitleName(index));
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
        setAutoFillBackground(true);
        QPalette menuPalette = palette();
        menuPalette.setColor(QPalette::Window, menuPalette.color(QPalette::Window).darker(112));
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
        box->setContentsMargins(kPanelPadding, kPanelPadding, kPanelPadding, kPanelPadding);
        box->setSpacing(kPanelRowSpacing);
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
        // 左右各留 8 px：图标贴着框边看着像被裁过。
        // NOTE: 加粗走 QSS 的 `font-weight`（别去 setFont() 改控件的字体）—— 这条样式
        //       表的**每一档状态**都是这里出的，字重跟别的属性待在同一个地方才不会漏。
        QString sheet = QStringLiteral("QPushButton { border: none; background: transparent;"
                                       " font-weight: bold;"
                                       " text-align: left; padding: 6px 8px; }"
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
    runStateDetailLabel->setMaximumWidth(MainWindowUI::bottomBarRunStateTextWidth());
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
    runStateButton->setToolTip(
        QCoreApplication::translate("MainWindowUI", "点一下向上展开实例列表"));
    applyBottomBarButtonStyle(runStateButton, bottomBar->palette());
    QHBoxLayout *runStateRow = new QHBoxLayout(runStateButton);
    runStateRow->setContentsMargins(0, 0, 0, 0);
    runStateRow->setSpacing(MainWindowUI::bottomBarChevronSpacing());
    runStateRow->addLayout(runStateLayout, 1);
    runStateRow->addWidget(makeChevronLabel(runStateButton), 0, Qt::AlignVCenter);

    // 没有实例在跑时这两行本来会藏起来（那时 MainWindow 写「实例未运行」）。**藏的是字，
    // 不是格子** —— 格子（上面那颗按钮）永远占着那 170 px（宽度是
    // bottomBarRunStateColumnWidth()，从 200 缩到 170 那次改的，别照旧数字找）。
    runStateLabel->hide();
    runStateDetailLabel->hide();

    // ---------- 中：进度（启动优先于下载） ----------
    statusLabel = new ElidedLabel(bottomBar);
    statusLabel->setObjectName(statusLabelName());
    // 固定一个最小宽度：文件名长短变化时这一格不会再跳来跳去
    statusLabel->setMinimumWidth(kStatusLabelMinWidth);
    // **上限也得有**（2026-09-24 补的）。ElidedLabel 的 sizeHint() 是把宽度封在上限上，
    // 而上限的来源就是 maximumWidth() —— 不设上限时，一句长文件名会把这一格的 sizeHint
    // 顶到 3912、把底栏顶到 4338、把**主窗口**顶到 4338（默认窗口只有 900 宽，实测见
    // ui_check 的 E5 段）。理由全文（含"最小宽度那一半其实不受影响"）见头文件那段。
    statusLabel->setMaximumWidth(MainWindowUI::bottomBarStatusTextWidth());
    // NOTE: 它是 ElidedLabel（**不是**普通 QLabel）：中间那格比这句长的时候（文件名长、
    //       窗口窄）普通 QLabel 是**硬裁**的 —— 最后一个字被切掉一半就杵在那儿。
    //       超出去的部分由它自己截成省略号。上下两个宽度一夹，布局就钉死了。
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
    // 现在拉伸因子写成 6 : 1 : 1（2026-09-23 从 4:1:1 提上来，理由见底下那段）——
    // 左右各一份空档各自留白，所以中间那格仍然居中，窗口拉宽时按同样比例跟着长。
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
    // 点开是**开关**：已经开着的时候再点**同一颗**按钮要收回去（用户 2026-09-23 原话：
    // 「那个按钮，点开以后处于展开状态时再次点击按钮收回」）。点另一颗时，这一下要是真落到
    // 按钮上，就是"这块收、那块开"；被旧那块吃掉（见下面 NOTE）就只收不开，再点一下才开 ——
    // 两条路都不会把两块一起挂着。
    //
    // NOTE: 为什么"收回"得在这里挡一道 —— Qt::Popup 点外面会自己关（点面板外那一下多半不往
    //       下传：探针 popup_probe 用**真窗口事件**实测，关掉之后按钮收不到 clicked），但它
    //       也有**把这一下重放给下面那个控件**的路数（QMenu::setNoReplayFor() 就是专门为
    //       "别重放给开菜单那颗按钮"加的 API，可见默认是重放的）。重放那条路上按钮照样收到
    //       clicked：不挡就是"关了又立刻开一块新的"，看着像怎么点都收不回去 —— 那才是用户报的
    //       毛病。这里靠 findChild 认"现在有没有开着"：面板挂在 bottomBar 底下，close 之后要等
    //       deleteLater 才真删（同一轮事件处理里还找得到，所以挡得住），不用自己记指针。
    //
    // NOTE: 行里的按钮点下去不是"立刻做事"，而是"面板先收、动作排到下一轮"（见
    //       BottomBarPanel 里 dispatch 那段）—— 那两个动作都要弹模态窗，压在 Qt::Popup
    //       底下会点不动。所以**别**在这里顺手补一句"点完也 close 一下"：那是同一件事
    //       做两遍，而且顺序反了就白做。
    const auto togglePanel = [this](QWidget *anchor, bool account) {
        if (auto *open = dynamic_cast<BottomBarPanel *>(
                bottomBar->findChild<QWidget *>(MainWindowUI::bottomBarPanelName()))) {
            const bool sameAnchor = open->anchor() == anchor;
            open->close();
            if (sameAnchor) {
                return;
            }
        }
        // 现问一遍要弹什么（不缓存：实例随时会结束、账号随时会多一个，"上次那几行"
        // 一旦留着就会弹出早就不是那样的一面板）。
        const QList<PanelRow> rows =
            m_panelRowsProvider ? m_panelRowsProvider(account) : QList<PanelRow>();
        // 面板只知道"第几行第几颗"，"是哪一块面板"由这里补上再转给 MainWindow ——
        // 一块面板只可能属于一边，所以这个得在**建面板的时候**就绑死。
        const BottomBarPanel::ActionCallback onAction = [this, account](const QString &key,
                                                                       int action) {
            if (m_panelActionHandler) {
                m_panelActionHandler(account, key, action);
            }
        };
        auto *panel = new BottomBarPanel(anchor, account, rows, onAction, bottomBar);
        // 这次弹出来的面板归它自己管：关掉就删（不然点一次攒一个）。
        panel->setAttribute(Qt::WA_DeleteOnClose);
        panel->popUp();
    };
    QObject::connect(runStateButton, &QPushButton::clicked, runStateButton,
                     [this, togglePanel]() { togglePanel(runStateButton, false); });
    QObject::connect(accountButton, &QPushButton::clicked, accountButton,
                     [this, togglePanel]() { togglePanel(accountButton, true); });

    // 三格从左到右摆：实例状态（0）· 空档（1）· 进度（6）· 空档（1）· 账户（0）。
    // 两头那两颗按钮的宽度都是钉死的，份数写 0；富余宽度全在中间三份里分。
    // NOTE: 中间那份**从 4 提到 6**（2026-09-23）：两头那两格原来吃掉 458 px，900 px 的
    //       窗口里富余只有 370 px，中间拿 4/6 就只有 247 px（实测条 262 px）—— 用户报
    //       「中间进度条奇小无比」。缩了两头（200+258 → 170+208）之后再让中间拿 6/8，
    //       实测条 356 px（900 的窗口，从 262 涨上来的），两边各留一份空档所以还居中。
    barLayout->addWidget(runStateButton, 0);
    barLayout->addStretch(1);
    barLayout->addLayout(progressLayout, 6);
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

QColor MainWindowUI::folderDangerColor(const QPalette &palette) {
    // 判据取的是**窗口底色的明度**（不是某个"主题叫什么"的标志位）：深底用浅红、
    // 浅底用深红，两种主题下都是"一眼看出是红字"而不是"一团糊在底色里"。
    if (palette.color(QPalette::Window).lightness() < 128) {
        return QColor(0xff, 0x6b, 0x6b);
    }
    return QColor(0xc0, 0x28, 0x28);
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
