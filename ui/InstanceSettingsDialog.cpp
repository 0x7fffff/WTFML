/**
 * @file InstanceSettingsDialog.cpp
 * @brief 搭出「实例设置…」对话框的控件，并把它们读成一份覆盖值。
 * @author 0x7fffff
 * @date 2026-09-21
 * @note 这里**不落盘、不碰 src/**：值从控件读，落盘在 MainWindow 那边。
 *
 * 这一层唯一的"业务判断"是内存那两行在自动档下的表现（禁用 + 点确定时不写覆盖），
 * 理由写在 .hpp 的 override() 上。
 *
 * 2026-09-26 起这一页是**三栏**（大类 | 小类 | 内容，见 .hpp 里 categoryKeys() 那一段）：
 * 控件全部直接挂在 `this` 上、按 objectName 认，所以分栏对"读值"这一侧一个字都没改
 * —— 只是控件换了个父容器。
 */
#include "InstanceSettingsDialog.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFont>
#include <QGuiApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QSize>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <utility>

#include "InstanceFilesPage.hpp"
#include "MainWindowUI.hpp"
#include "VersionIcons.hpp"
#include "VersionTreeDelegate.hpp"

namespace {

/// 数字框的宽度。和设置页那两个用同一个值 —— 同一个"内存 MB 数"在两处宽度不一样
/// 会显得像是两种东西。
constexpr int kMemorySpinWidth = 120;
/// 追加参数那一行的输入高度（三四行够放几条常见参数）。
constexpr int kAppendJvmArgsHeight = 76;
/// 左栏那两条导航列在算出来的宽度上再加多少余量（左右各一半）。
/// `sizeHintForColumn` 给的是"字有多宽"，条目的字贴着边看着像被切了。
constexpr int kNavListExtraWidth = 28;
/// 三栏之间空多少。和整页外边距那一档（12）同一档。
constexpr int kColumnSpacing = 12;
/// 单独拿出来用时的初始大小。宽是"两列导航 + 一块还放得下两列表格的内容"量出来的
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
        setAutoFillBackground(true);
        QPalette menuPalette = palette();
        menuPalette.setColor(QPalette::Window, menuPalette.color(QPalette::Window).darker(112));
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
                                               bool warnJavaMismatch, QWidget *parent)
    : QDialog(parent), m_global(global), m_instanceId(instanceId) {
    setWindowTitle(tr("实例设置"));
    // 比分组对话框宽得多：从左到右是**三栏**（大类 | 小类 | 内容），600 那个宽度放不下。
    // NOTE: 嵌进覆盖层的时候这个尺寸**不作数** —— 那一层给它的是内容区的整幅宽（见
    //       OverlayHost::execEmbedded 里那句 `area->layout()->addWidget(dialog)`）。
    //       这一句只影响单独把它拿出来用的时候（验证工程那几处）。
    resize(kDialogWidth, kDialogHeight);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 12, 12, 12);
    outer->setSpacing(10);

    auto *title = new QLabel(tr("实例设置：%1").arg(instanceName), this);
    m_titleLabel = title;
    title->setObjectName(titleLabelName());
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    outer->addWidget(title);

    // ---------- 三栏（见 .hpp 里 categoryKeys() 上面那一段）----------
    auto *columns = new QHBoxLayout;
    columns->setSpacing(kColumnSpacing);

    // 两条导航列（大类 / 小类）是**同一种东西**，所以走同一段代码：同一个代理、同一套字体
    // 与行高算法 —— 和设置页、下载页那两列左栏也是同一个样子。
    // NOTE: **不设** rowRefreshRole()。那一格里画的刷新圈是下载页专有的（这一列没有
    //       "重新拉一遍"这回事），代理见到没设就不画。
    const auto makeNavList = [this](const QString &objectName, const QStringList &keys,
                                    const QStringList &texts) {
        auto *list = new QListWidget(this);
        list->setObjectName(objectName);
        list->setUniformItemSizes(true);
        list->setItemDelegate(new VersionTreeDelegate(list));
        QFont listFont = list->font();
        listFont.setPointSize(listFont.pointSize() + MainWindowUI::versionTreeFontStep());
        list->setFont(listFont);
        for (int i = 0; i < keys.size(); ++i) {
            auto *item = new QListWidgetItem(texts.value(i), list);
            // 身份进 userData：条目文字是显示内容、会随文案改，不能拿它当身份。
            item->setData(Qt::UserRole, keys.value(i));
        }
        // 高度贴着内容：几条两三个字的条目被拉成一整条空白看着像坏了。行高**交给样式算**
        // （sizeHintForRow），别写死数字 —— 换字体/换样式就错。和设置页那一列同一条规矩。
        int rowsHeight = 0;
        for (int i = 0; i < list->count(); ++i) {
            rowsHeight += list->sizeHintForRow(i);
        }
        list->setFixedHeight(rowsHeight + 2 * list->frameWidth() + 2);
        list->setFixedWidth(navListWidth(list));
        return list;
    };

    m_categoryList = makeNavList(categoryListName(), categoryKeys(), categoryTexts());
    addNavColumn(columns, m_categoryList);

    m_subCategoryList = makeNavList(subCategoryListName(), filePageKeys(), filePageTexts());
    // 只有选到「文件管理」那一栏时才露出来（见下面 applyCategory 那一发）。
    // NOTE: 收着的时候 `currentRow` **留着** —— 切走再切回来还停在刚才那一页。
    m_subCategoryList->setVisible(false);
    addNavColumn(columns, m_subCategoryList);

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(stackName());
    columns->addWidget(m_stack, 1);
    // 三栏吃掉整页剩下的竖直空间（底下那排按钮按自己的高排）。
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

        // 预览：这张图就是"点确定之后树上会长什么样"。定死边长，换图时整行不跟着跳。
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
        auto *line = new QHBoxLayout(cell);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(8);
        m_changeIconButton = new QPushButton(changeIconText(), cell);
        m_changeIconButton->setObjectName(changeIconButtonName());
        m_changeIconButton->setToolTip(iconHint());
        connect(m_changeIconButton, &QPushButton::clicked, this,
                [this]() { popUpMenuBelow(m_iconMenu, m_changeIconButton); });
        line->addWidget(m_changeIconButton);

        m_groupButton = new QPushButton(groupButtonText(), cell);
        m_groupButton->setObjectName(groupButtonName());
        // tooltip 由 refreshGroupMenuState() 写（它要说清"现在归在哪一组"）。
        connect(m_groupButton, &QPushButton::clicked, this,
                [this]() { popUpMenuBelow(m_groupMenu, m_groupButton); });
        line->addWidget(m_groupButton);
        line->addStretch(1);
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
            button->setCheckable(true);
            // 和底下那排「确定 / 取消」同一条：点一下换个样子，不该抢键盘焦点
            // （抢了之后回车就不再是"确定"了）。
            button->setFocusPolicy(Qt::NoFocus);
            const QString key = keys.at(i);
            connect(button, &QToolButton::clicked, this, [this, key]() {
                m_iconChoice = VersionIcons::iconValueForKey(key);
                // 换了内置候选 = 用户不要刚才挑的那张本地图了。**不删盘上那份**：
                // 它现在还在原处，而实例目录里那份要等点确定才动（见 pickedIconPath()）。
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
            m_iconMenu->close();
        });
        actions->addWidget(pick);
        auto *reset = new QPushButton(iconResetText(), menu);
        reset->setObjectName(iconResetButtonName());
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

    // ---------- 第四行：导出（**占位**）----------
    //
    // 用户原话：「第四行 导出（还没做，可行的话顺便做了，麻烦的话先放着）」，当场问过之后
    // 选的是「现在放占位符」。所以这里是一颗**灰的**按钮 + 一句"还没做" ——
    // 摆一颗按得下去、却什么都不发生的按钮比灰着更糟（用户会以为坏了），而灰着不说话同样
    // 糟（那是这一层的另一条规矩：灰掉的每一项都得配一句为什么）。
    {
        auto *cell = new QWidget(this);
        auto *column = new QVBoxLayout(cell);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(4);
        auto *button = new QPushButton(exportPlaceholderText(), cell);
        button->setObjectName(exportPlaceholderButtonName());
        button->setEnabled(false);
        button->setToolTip(exportPlaceholderHint());
        column->addWidget(button, 0, Qt::AlignLeft);
        auto *hint = new QLabel(exportPlaceholderHint(), cell);
        hint->setWordWrap(true);
        column->addWidget(hint);
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
        // 宽度给 1：这一格里剩下的横向空间全归这个框（右边的「浏览…」按自己的
        // sizeHint 排）。
        choiceRow->addWidget(m_javaPathEdit, 1);

        auto *browse = new QPushButton(tr("浏览…"), cell);
        browse->setObjectName(browseJavaButtonName());
        connect(browse, &QPushButton::clicked, this, [this]() {
            // 模态，验证工程驱动不了它 —— 那一半是"问路径"，可测的那一半是这个框里的
            // 文字（override() 直接读它）。和 GroupDialog 里问组名的拆法一样。
            // NOTE: "驱动不了"的原因和内嵌提示那几条不一样：这里开的是 **Windows 自己的
            //       文件选择框**，不是我们的窗（同选文件夹 / 选整合包 / 选皮肤图，2026-09-25
            //       之后全项目就剩这四处独立窗）。理由和取舍见 MainWindow.cpp 里
            //       onAddFolderClicked() 上面那一段。
            const QString picked = QFileDialog::getOpenFileName(
                this, tr("选择 java.exe"), m_javaPathEdit->text(),
                tr("Java 可执行文件 (java.exe);;所有文件 (*)"));
            if (!picked.isEmpty()) {
                m_javaPathEdit->setText(picked);
                // 选了一个具体文件 = 用户明确要这个，顺手把这一行切成「自定义」。
                // 不切的话他选了路径、这一行还在"跟随全局"，点确定等于什么都没发生。
                m_javaPathCustom->setChecked(true);
            }
        });
        choiceRow->addWidget(browse);
    }

    // ---------- 内存：最大 / 最小 / 分配方式 ----------
    {
        QWidget *cell = addOverrideRow(grid, row, xmxField(), tr("最大内存"),
                                       tr("只对这个实例生效（给 JVM 的 -Xmx）。"));
        row += 2;
        m_xmxFollowCell = cell;
        auto *choiceRow = choiceRowOf(xmxField());
        m_memoryMaxSpin = new QSpinBox(cell);
        m_memoryMaxSpin->setObjectName(memoryMaxSpinName());
        m_memoryMaxSpin->setRange(MainWindowUI::memoryMaxMinMb(), MainWindowUI::memoryLimitMaxMb());
        m_memoryMaxSpin->setSingleStep(MainWindowUI::memoryStepMb());
        m_memoryMaxSpin->setSuffix(QStringLiteral(" MB"));
        // 和设置页那两个内存数字框同一份滚轮防护（见 MainWindowUI.hpp 里的 WheelGuard）：
        // 数字框在没有焦点时也吃滚轮，改完界面上看不出来，下次启动用的就是那个被滚出来的数。
        WheelGuard::install(m_memoryMaxSpin);
        m_memoryMaxSpin->setFixedWidth(kMemorySpinWidth);
        choiceRow->addWidget(m_memoryMaxSpin);
    }
    {
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
            // 那一刻"的高度，不是构造时那个 600x480）。
            if (expanded) {
                m_heightBeforeAdvanced = height();
            }
            m_advancedBox->setHidden(!expanded);
            m_advancedToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
            // 折回之后得让窗口**缩回去**：Qt 只在"窗口比内容还小"时把它撑大（展开那一下
            // 就是它干的），内容变少时它一动不动 —— 不补这一句，展开一次再收起，底下就
            // 永远留着那一截空白。实测（600x480 那个默认大小）：展开 → 600x503、
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

        auto *line = new QHBoxLayout;
        line->setSpacing(8);
        auto *exportScript = new QPushButton(exportScriptText(), cell);
        exportScript->setObjectName(exportScriptButtonName());
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
            // 取消 = 什么都不做，和点了返回箭头是同一件事。
            const QString picked = QFileDialog::getSaveFileName(
                this, exportScriptText(), suggested, tr("批处理文件 (*.bat)"));
            if (picked.isEmpty()) {
                return;
            }
            // 真正"写什么"在 MainWindow 里：这一层够不着 authManager / versionManager
            // （ui/ 不 include src/ 下的头）。见 .hpp 里那个信号的说明。
            emit exportLaunchScriptRequested(picked);
        });
        line->addWidget(exportScript);

        auto *repair = new QPushButton(repairFilesText(), cell);
        repair->setObjectName(repairFilesButtonName());
        repair->setToolTip(repairFilesHint());
        // 只报"用户想干这件事"：能不能开始（有没有下载在跑 / 这个实例装没装）由
        // MainWindow 说了算，它才知道。**不问路径**，所以这一层没什么可问的了。
        connect(repair, &QPushButton::clicked, this, &InstanceSettingsDialog::repairFilesRequested);
        line->addWidget(repair);
        line->addStretch(1);
        column->addLayout(line);

        auto *hint = new QLabel(actionsHint(), cell);
        hint->setWordWrap(true);
        column->addWidget(hint);
        grid->addWidget(cell, row++, 0, 1, 2);
    }

    // ---------- 文件管理（八页）----------
    //
    // 这八页说的是**这个实例自己的目录**：`--gameDir` 就是 `versions/<实例名>/`，所以
    // `mods/ resourcepacks/ shaderpacks/ datapacks/ saves/` 都在里头 —— 实例之间天然隔离，
    // 在这一页里动什么都不会碰到别的实例。
    //
    // 八页分三种形状（见 .hpp 里"文件管理那八页"那一段）：快捷方式是按钮网格，服务器列表
    // 读一份递进来的解析结果，中间六页是同一个 InstanceFilesPage。**目录一律由调用方
    // 算好递进来**（setInstanceDir / setFileDirs）—— 那几个目录名是 MC 的约定，
    // src/download/ResourceCatalog::folderForKind() 是唯一出处，这一层不抄第二份。
    //
    // NOTE: 页面标题（那行粗体）**故意没有** —— 左栏那一列小类一直亮着，写在这里是把
    //       同一句话挨着说两遍。每一页正文底下那行小字才是这一页**特有**的那句。
    {
        // ① 快捷方式：一排按钮，每颗开一个文件夹。
        {
            QWidget *page = addPage(QStringLiteral("shortcut"));
            auto *layout = qobject_cast<QVBoxLayout *>(page->layout());
            if (layout != nullptr) {
                auto *grid = new QGridLayout;
                grid->setContentsMargins(0, 0, 0, 0);
                grid->setHorizontalSpacing(8);
                grid->setVerticalSpacing(8);
                const QStringList texts = shortcutTexts();
                const QStringList keys = shortcutPathKeys();
                for (int i = 0; i < texts.size(); ++i) {
                    auto *button = new QPushButton(texts.value(i), page);
                    button->setObjectName(shortcutButtonName(i));
                    const QString key = keys.value(i);
                    // 路径**现场问 m_instanceDir / m_fileDirs**（不是捕获一份快照）：
                    // setInstanceDir() 是在构造函数之后才被调的，捕获快照的话拿到的
                    // 永远是空串 —— 这颗按钮点了什么都不会发生，而且不报错。
                    connect(button, &QPushButton::clicked, this, [this, key] {
                        emit openInstanceFolderRequested(
                            key == QStringLiteral("dir") ? m_instanceDir : m_fileDirs.value(key));
                    });
                    // 按钮**不给它抻**：默认尺寸（和右下角那两颗一样），三颗一排靠左。
                    grid->addWidget(button, i / shortcutPerRow(), i % shortcutPerRow(),
                                    Qt::AlignLeft);
                    m_shortcutButtons.append(button);
                }
                auto *rowWrap = new QHBoxLayout;
                rowWrap->setContentsMargins(0, 0, 0, 0);
                rowWrap->addLayout(grid);
                // 底下这一条 stretch 是"三颗一排靠左"的另一半：没有它，网格会把三列
                // 摊平分给整幅宽，每颗按钮都被抻成一大条。
                rowWrap->addStretch(1);
                layout->addLayout(rowWrap);
                auto *hint = new QLabel(filesHint(QStringLiteral("shortcut")), page);
                hint->setObjectName(filesHintName(QStringLiteral("shortcut")));
                hint->setWordWrap(true);
                layout->addWidget(hint);
                layout->addStretch(1);
            }
        }

        // ② 中间六页：同一个控件，喂不同的目录与三条开关。
        const QStringList keys = filePageKeys();
        for (const QString &key : keys) {
            if (key == QStringLiteral("shortcut") || key == QStringLiteral("server")) {
                continue;
            }
            QWidget *page = addPage(key);
            auto *layout = qobject_cast<QVBoxLayout *>(page->layout());
            if (layout == nullptr) {
                continue;
            }
            auto *files = new InstanceFilesPage(page);
            files->setObjectName(filesPageName(key));
            // 存档只列文件夹（saves 底下那些 level.dat / session.lock 不是存档）。
            files->setMode(key == QStringLiteral("save") ? InstanceFilesPage::Mode::DirsOnly
                                                          : InstanceFilesPage::Mode::FilesAndDirs);
            // 只有游戏会自己扫的那三个目录给「禁用」；存档**不给删**（理由见
            // InstanceFilesPage::setCanDelete()）。
            files->setCanToggle(fileKeyCanToggle(key));
            files->setCanDelete(key != QStringLiteral("save"));
            files->setEmptyText(filesEmptyText(key));
            files->setDirectory(m_fileDirs.value(key));
            connect(files, &InstanceFilesPage::openFolderRequested, this,
                    &InstanceSettingsDialog::openInstanceFolderRequested);
            connect(files, &InstanceFilesPage::toggleRequested, this,
                    &InstanceSettingsDialog::toggleInstanceFileRequested);
            connect(files, &InstanceFilesPage::deleteRequested, this,
                    &InstanceSettingsDialog::deleteInstanceFileRequested);
            m_filesPages.insert(key, files);
            layout->addWidget(files, 1);
            auto *hint = new QLabel(filesHint(key), page);
            hint->setObjectName(filesHintName(key));
            hint->setWordWrap(true);
            layout->addWidget(hint);
        }

        // ③ 服务器列表：读 servers.dat 的结果由调用方递进来（这一层不 include src/）。
        {
            QWidget *page = addPage(QStringLiteral("server"));
            auto *layout = qobject_cast<QVBoxLayout *>(page->layout());
            if (layout != nullptr) {
                auto *top = new QHBoxLayout;
                top->setContentsMargins(0, 0, 0, 0);
                top->addStretch(1);
                m_serverRefresh = new QPushButton(serverRefreshText(), page);
                m_serverRefresh->setObjectName(serverRefreshButtonName());
                // 重读要碰 NBT 解析（在 src/ 里），所以这一发只能交给 MainWindow。
                connect(m_serverRefresh, &QPushButton::clicked, this,
                        &InstanceSettingsDialog::refreshServerListRequested);
                top->addWidget(m_serverRefresh);
                layout->addLayout(top);

                m_serverNotice = new QLabel(page);
                m_serverNotice->setObjectName(serverNoticeName());
                m_serverNotice->setWordWrap(true);
                layout->addWidget(m_serverNotice);

                m_serverTree = new QTreeWidget(page);
                m_serverTree->setObjectName(serverTreeName());
                m_serverTree->setHeaderLabels(
                    {serverNameColumnText(), serverAddressColumnText()});
                // 「一行一个服务器」是平表不是树：不要每行前面那个展开三角。
                m_serverTree->setRootIsDecorated(false);
                m_serverTree->setUniformRowHeights(true);
                m_serverTree->setSelectionMode(QAbstractItemView::SingleSelection);
                m_serverTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
                layout->addWidget(m_serverTree, 1);

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

    // ---------- 分栏的接线 ----------
    // 左栏一变：换页，并且只在「文件管理」那一栏把小类那一列露出来。
    //
    // NOTE: 越界保护的理由和设置页那条一样 —— 分类表和页数将来要是对不上，宁可停在原来
    //       那页，也不要 setCurrentIndex(-1) 把右栏整片清空（"点一下界面就空了"用户只会
    //       当成崩了）。
    connect(m_categoryList, &QListWidget::currentRowChanged, this,
            [this](int row) { applyCategory(row); });
    connect(m_subCategoryList, &QListWidget::currentRowChanged, this, [this](int row) {
        // 只有当前栏就是「文件管理」时这一发才算数：小类那一列收着的时候它不该换页
        // （切走再切回来时，那一发由 applyCategory() 自己补）。
        if (categoryKeys().value(m_categoryList->currentRow()) == filesCategoryKey()) {
            applyFilePage(row);
        }
    });
    // 打开就是第一条（概览）—— 左栏亮着哪一条、右栏就是哪一页，两边不能各说各的。
    // 小类那一列先摆到第一条上：它这会儿还收着，但那一条得是亮的。
    m_subCategoryList->setCurrentRow(0);
    m_categoryList->setCurrentRow(0);
    // 上面那一发要是因为"行号本来就对"而没发信号（Qt 那条"一样就不发"），这里补一次：
    // 露不露小类那一列、当前是哪一页，都归它管。
    applyCategory(m_categoryList->currentRow());

    // ---------- 灌值 ----------
    // 「有值 = 自定义，没值 = 跟随全局」这一条两边都要对上：磁盘上有一项覆盖时，
    // 重开这个对话框必须停在「自定义」上，否则用户会以为自己的设置丢了。
    m_javaPathFollow->setChecked(!current.javaPath.has_value());
    m_javaPathCustom->setChecked(current.javaPath.has_value());
    m_javaPathEdit->setText(current.javaPath.value_or(global.javaPath));

    m_xmxFollow->setChecked(!current.xmx.has_value());
    m_xmxCustom->setChecked(current.xmx.has_value());
    m_memoryMaxSpin->setValue(current.xmx.value_or(global.xmx));

    m_xmsFollow->setChecked(!current.xms.has_value());
    m_xmsCustom->setChecked(current.xms.has_value());
    m_memoryMinSpin->setValue(current.xms.value_or(global.xms));

    m_autoMemoryFollow->setChecked(!current.autoMemory.has_value());
    m_autoMemoryCustom->setChecked(current.autoMemory.has_value());
    m_memoryAutoCheck->setChecked(current.autoMemory.value_or(global.autoMemory));

    m_extraJvmArgsFollow->setChecked(!current.extraJvmArgs.has_value());
    m_extraJvmArgsCustom->setChecked(current.extraJvmArgs.has_value());
    m_extraJvmArgsEdit->setText(current.extraJvmArgs.value_or(global.extraJvmArgs));

    // 每一行下面那句话要等值灌完才刷（它写的是"当前是什么"）。上面那些 setChecked
    // 已经触发过一轮 refreshHints 了，但那时候另一个单选钮还是空的。
    refreshHints();

    // 图标那一行的初值走 setIcon()（调用方在后头调，见 .hpp 里那段"为什么用 setter"）。
    // 这里先按"默认"画一遍：没被 setIcon() 叫过的时候（验证工程里那些只验结构的构造点）
    // 八颗按钮也得是**八颗都没勾**的样子，而不是停在某个随机状态上。
    refreshIconRow();

    // ---------- 确定 / 取消 ----------
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto *cancel = new QPushButton(tr("取消"), this);
    cancel->setObjectName(cancelButtonName());
    auto *confirm = new QPushButton(tr("确定"), this);
    confirm->setObjectName(confirmButtonName());
    confirm->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(confirm);
    outer->addLayout(buttons);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(confirm, &QPushButton::clicked, this, &QDialog::accept);
}

QString InstanceSettingsDialog::titleText() const {
    return m_titleLabel != nullptr ? m_titleLabel->text() : QString();
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

// ---------- 分栏（见 .hpp 里 categoryKeys() 上面那一段）----------

QWidget *InstanceSettingsDialog::addPage(const QString &key) {
    auto *page = new QWidget(m_stack);
    page->setObjectName(panelName(key));
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    m_stack->addWidget(page);
    return page;
}

QGridLayout *InstanceSettingsDialog::addForm(const QString &key, QWidget *page) {
    auto *form = new QWidget(page);
    form->setObjectName(formName(key));
    auto *grid = new QGridLayout(form);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(10);
    // 和设置页栏内那一层同一套比例（名字在左、控件在右）。
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

void InstanceSettingsDialog::applyCategory(int row) {
    if (row < 0 || row >= categoryKeys().size()) {
        return;
    }
    const bool files = categoryKeys().at(row) == filesCategoryKey();
    // 「文件管理」之外的两栏里，中间那一列**整个收掉**（用户原话就是"叠在上面"——
    // 它是文件管理专用的第二层，不是常驻的一列）。
    m_subCategoryList->setVisible(files);
    if (files) {
        // 小类那一列可能是刚露出来的：按它现在亮着的那一条换页。
        applyFilePage(m_subCategoryList->currentRow());
        return;
    }
    // 概览 0 / 启动 1（顺序见 filePageFirstIndex()）。
    m_stack->setCurrentIndex(row);
}

void InstanceSettingsDialog::applyFilePage(int row) {
    if (row < 0 || row >= filePageKeys().size()) {
        return;
    }
    m_stack->setCurrentIndex(filePageFirstIndex() + row);
    // 切到这一页就**当场重列一遍**：打开这个对话框的这段时间里，盘上那些文件夹完全可能
    // 被别的东西动过（游戏正在写日志、用户自己开了个资源管理器）。列目录只有读，很便宜，
    // 而"我看见的和盘上不一样"是这一页最没道理的一种错。
    if (InstanceFilesPage *page = m_filesPages.value(filePageKeys().at(row))) {
        page->refresh();
    }
}

// ---------- 文件管理那八页（见 .hpp 里 filePageKeys() 上面那一段）----------

QStringList InstanceSettingsDialog::shortcutTexts() {
    return {tr("版本目录"), tr("存档"),     tr("截图"),     tr("模组"), tr("资源包"),
            tr("数据包"),   tr("光影包"),   tr("原理图"),   tr("日志")};
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
    if (key == QStringLiteral("shortcut")) {
        return tr("这几颗只是把文件夹打开 —— 看里面有什么、禁用一个、删掉一个，都在旁边那一列"
                  "里（模组 / 资源包 / 光影包那几页）。");
    }
    if (key == QStringLiteral("mod")) {
        return tr("游戏会自己扫这里面的 .jar。「禁用」只是把文件名改成 .jar.disabled —— "
                  "文件还在，改回来就又生效了。");
    }
    if (key == QStringLiteral("resourcepack")) {
        return tr("游戏会自己扫这里：.zip 和文件夹都算一个资源包。");
    }
    if (key == QStringLiteral("datapack")) {
        return tr("下载到这儿不等于生效：要自己把它（或者它里面的文件夹）搬进对应存档的"
                  "datapacks 文件夹里。");
    }
    if (key == QStringLiteral("shader")) {
        return tr("游戏会自己扫这里，但要有 Iris 或者 OptiFine 才加载得了。");
    }
    if (key == QStringLiteral("schematic")) {
        return tr("Litematica 装好的话它默认就读这个文件夹。");
    }
    if (key == QStringLiteral("save")) {
        return tr("一个文件夹就是一个存档。启动器不删存档 —— 这里只看得见，要删得自己去"
                  "文件夹里删。");
    }
    if (key == QStringLiteral("server")) {
        return tr("servers.dat 是游戏自己写的，启动器只读不改。");
    }
    return QString();
}

QString InstanceSettingsDialog::filesEmptyText(const QString &key) {
    if (key == QStringLiteral("mod")) {
        return tr("这个实例还没装模组。去「下载」页找一个装进来。");
    }
    if (key == QStringLiteral("resourcepack")) {
        return tr("这个实例还没有资源包。");
    }
    if (key == QStringLiteral("datapack")) {
        return tr("这个实例还没有数据包。");
    }
    if (key == QStringLiteral("shader")) {
        return tr("这个实例还没有光影包。");
    }
    if (key == QStringLiteral("schematic")) {
        return tr("这个实例还没有原理图。");
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
    return tr("原理图（投影）是 Litematica 用的，这个实例没装它 —— 这个文件夹现在没人会读。"
              "先去「下载」页装一个。");
}

void InstanceSettingsDialog::setInstanceDir(const QString &dir) {
    // 只是记下来：快捷方式那九颗按钮**点的时候现场读**（见构造函数里那段 NOTE），
    // 所以这里不用重画什么。
    m_instanceDir = dir;
}

void InstanceSettingsDialog::setFileDirs(const QHash<QString, QString> &dirs) {
    m_fileDirs = dirs;
    // 六页在构造函数里就搭好了，那会儿还不知道目录在哪儿（这一份是构造之后才灌的）——
    // 所以灌完必须让它们各列一遍，否则每一页都停在"这个文件夹还是空的"上。
    const QList<QString> keys = m_filesPages.keys();
    for (const QString &key : keys) {
        if (InstanceFilesPage *page = m_filesPages.value(key)) {
            page->setDirectory(m_fileDirs.value(key));
        }
    }
}

void InstanceSettingsDialog::setShaderUsable(bool usable) {
    const QString reason = usable ? QString() : shaderUnavailableText();
    if (InstanceFilesPage *page = m_filesPages.value(QStringLiteral("shader"))) {
        page->setUnavailable(reason);
    }
    markPageUnusable(QStringLiteral("shader"), reason);
}

void InstanceSettingsDialog::setSchematicUsable(bool usable) {
    const QString reason = usable ? QString() : schematicUnavailableText();
    if (InstanceFilesPage *page = m_filesPages.value(QStringLiteral("schematic"))) {
        page->setUnavailable(reason);
    }
    markPageUnusable(QStringLiteral("schematic"), reason);
}

void InstanceSettingsDialog::markPageUnusable(const QString &key, const QString &reason) {
    // 中栏那一条也挂上原因：用户在这一条上停一下就该知道为什么这一页是空的，
    // 不用点进去再退出来。**不把条目禁用掉** —— 禁用了就点不进去，那句话也就永远
    // 看不见了（灰掉一条"用不上"既没说是什么、也没说怎么办）。
    for (int i = 0; i < m_subCategoryList->count(); ++i) {
        QListWidgetItem *item = m_subCategoryList->item(i);
        if (item->data(Qt::UserRole).toString() == key) {
            item->setToolTip(reason);
            return;
        }
    }
}

void InstanceSettingsDialog::setServerList(const QList<QPair<QString, QString>> &servers,
                                           const QString &problem) {
    if (m_serverTree == nullptr) {
        return;
    }
    m_serverTree->clear();
    for (const QPair<QString, QString> &server : servers) {
        auto *item = new QTreeWidgetItem(m_serverTree);
        item->setText(0, server.first.isEmpty() ? unnamedMark() : server.first);
        item->setText(1, server.second);
    }
    // ★「读不动」和「读到了但一条都没有」是**两句不同的话**（和版本树那条"问到了就是
    //   没有" / "列表没取到"同一条规矩）：说成一句的话，用户会照着错的那句去修 ——
    //   一边是"我确实没加过服务器"，另一边是"你的 servers.dat 坏了，加多少条都白搭"。
    const bool readable = problem.isEmpty();
    const bool empty = servers.isEmpty();
    m_serverTree->setVisible(readable && !empty);
    m_serverNotice->setVisible(!readable || empty);
    m_serverNotice->setText(!readable ? problem : (empty ? serverEmptyText() : QString()));
}

void InstanceSettingsDialog::refreshFilePages() {
    const QList<InstanceFilesPage *> pages = m_filesPages.values();
    for (InstanceFilesPage *page : pages) {
        if (page != nullptr) {
            page->refresh();
        }
    }
}

InstanceFilesPage *InstanceSettingsDialog::filesPage(const QString &key) const {
    return m_filesPages.value(key);
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

void InstanceSettingsDialog::showCategory(const QString &key) {
    const int row = categoryKeys().indexOf(key);
    if (row < 0) {
        return;
    }
    m_categoryList->setCurrentRow(row);
    // `setCurrentRow()` 在"本来就亮着这一条"时不发信号，所以这一步**不能**靠信号。
    applyCategory(row);
}

QWidget *InstanceSettingsDialog::addOverrideRow(QGridLayout *grid, int row, const QString &field,
                                                const QString &title, const QString &customHint) {
    // **两行**：上面那一行是这个设置的名字（独占一整行），下面那一行才是它的选项
    // （「跟随全局 / 自定义」那对单选钮 + 控件）——用户 2026-09-26 原话「上一行设置名称，
    // 下一行设置选项」。**调用方因此要 `row += 2`**（见 addTitledRow() 那段）。
    QVBoxLayout *cellLayout = nullptr;
    QWidget *cell = addTitledRow(grid, row, title, this, &cellLayout);

    // 控件和单选钮在同一格，是为了内存那两行"整行禁用"能一次把两者都灰掉 ——
    // 只禁数字框会留下一个能选的「自定义」，选了却什么都不发生。
    auto *choiceRow = new QWidget(cell);
    choiceRow->setObjectName(choiceRowName(field));
    auto *choiceLayout = new QHBoxLayout(choiceRow);
    choiceLayout->setContentsMargins(0, 0, 0, 0);
    choiceLayout->setSpacing(8);

    auto *follow = new QRadioButton(followText(), choiceRow);
    follow->setObjectName(followRadioName(field));
    auto *custom = new QRadioButton(customText(), choiceRow);
    custom->setObjectName(customRadioName(field));
    // 同一行那对必须互斥。它们是同一个父控件的兄弟，Qt 本来就会自动互斥，但显式放进
    // 一个 QButtonGroup 更稳：以后谁把其中一个挪进别的容器也不会变成"两个都亮"。
    auto *group = new QButtonGroup(choiceRow);
    group->addButton(follow);
    group->addButton(custom);

    choiceLayout->addWidget(follow);
    choiceLayout->addWidget(custom);

    cellLayout->addWidget(choiceRow);

    auto *hintLabel = new QLabel(cell);
    hintLabel->setObjectName(followHintName(field));
    hintLabel->setWordWrap(true);
    // 提示文字有**两种形态**：选「自定义」时说"这一项只对这个实例生效"，选「跟随
    // 全局」时说"当前跟随的是什么值"。这里先把自定义那一种记下来，refreshHints()
    // 在两种之间切。
    m_customHints.insert(field, customHint);
    cellLayout->addWidget(hintLabel);

    // 成员留着，refreshHints() 要按档位开关它们、也要读它们算"有效值"。
    if (field == javaPathField()) {
        m_javaPathFollow = follow;
        m_javaPathCustom = custom;
    } else if (field == xmxField()) {
        m_xmxFollow = follow;
        m_xmxCustom = custom;
    } else if (field == xmsField()) {
        m_xmsFollow = follow;
        m_xmsCustom = custom;
    } else if (field == autoMemoryField()) {
        m_autoMemoryFollow = follow;
        m_autoMemoryCustom = custom;
    } else if (field == extraJvmArgsField()) {
        m_extraJvmArgsFollow = follow;
        m_extraJvmArgsCustom = custom;
    }

    // 单选钮一变，那一行下面那句话和内存两行的可用性都可能跟着变。
    connect(follow, &QRadioButton::toggled, this, &InstanceSettingsDialog::refreshHints);
    connect(custom, &QRadioButton::toggled, this, &InstanceSettingsDialog::refreshHints);

    return cell;
}

QHBoxLayout *InstanceSettingsDialog::choiceRowOf(const QString &field) const {
    if (auto *row = findChild<QWidget *>(choiceRowName(field))) {
        return qobject_cast<QHBoxLayout *>(row->layout());
    }
    return nullptr;
}

bool InstanceSettingsDialog::effectiveAutoMemory() const {
    return m_autoMemoryCustom->isChecked() ? m_memoryAutoCheck->isChecked() : m_global.autoMemory;
}

bool InstanceSettingsDialog::advancedExpanded() const { return m_advancedToggle->isChecked(); }

void InstanceSettingsDialog::refreshHints() {
    const bool autoMemory = effectiveAutoMemory();

    // 内存那两行：自动档下**整格**禁用（单选钮 + 数字框），提示换成"为什么它由不得你"。
    m_xmxFollowCell->setEnabled(!autoMemory);
    m_xmsFollowCell->setEnabled(!autoMemory);

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

    followOrCustom(javaPathField(), m_javaPathCustom->isChecked(),
                   m_global.javaPath.isEmpty() ? tr("未指定") : m_global.javaPath);
    followOrCustom(autoMemoryField(), m_autoMemoryCustom->isChecked(),
                   m_global.autoMemory ? tr("自动（按系统内存）") : tr("手动指定"));
    followOrCustom(extraJvmArgsField(), m_extraJvmArgsCustom->isChecked(),
                   m_global.extraJvmArgs.isEmpty() ? tr("未设置") : m_global.extraJvmArgs);

    if (autoMemory) {
        // 自动档：这两行是灰的，提示只说这一件事（"当前 %1 MB"在这里没有意义 ——
        // 那两个数每次启动前都会重算）。
        setHint(xmxField(), memoryAutoDisabledHint());
        setHint(xmsField(), memoryAutoDisabledHint());
    } else {
        followOrCustom(xmxField(), m_xmxCustom->isChecked(),
                       QStringLiteral("%1 MB").arg(m_global.xmx));
        followOrCustom(xmsField(), m_xmsCustom->isChecked(),
                       QStringLiteral("%1 MB").arg(m_global.xms));
    }
}

InstanceSettingsDialog::Override InstanceSettingsDialog::override() const {
    Override result;

    if (m_javaPathCustom->isChecked()) {
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
        if (m_xmxCustom->isChecked()) {
            result.xmx = m_memoryMaxSpin->value();
        }
        if (m_xmsCustom->isChecked()) {
            result.xms = m_memoryMinSpin->value();
        }
    }

    if (m_autoMemoryCustom->isChecked()) {
        result.autoMemory = m_memoryAutoCheck->isChecked();
    }
    if (m_extraJvmArgsCustom->isChecked()) {
        result.extraJvmArgs = m_extraJvmArgsEdit->text().trimmed();
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
