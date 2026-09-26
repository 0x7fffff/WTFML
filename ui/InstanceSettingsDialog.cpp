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
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSize>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

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

/// 把一行左边那一格（项目名称）加进网格：列 0 是名字、列 1 是控件，和设置页同一套排法。
void addRowTitle(QGridLayout *grid, int row, const QString &title, QWidget *parent) {
    auto *label = new QLabel(title, parent);
    // 顶部对齐：右边那一格有两行（单选钮那一行 + 提示），名字应该跟第一行齐。
    grid->addWidget(label, row, 0, Qt::AlignLeft | Qt::AlignTop);
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

    // ---------- 概览（版本名 / 图标 / 分组 / 置顶·取消置顶）----------
    //
    // NOTE: `grid` 与 `row` 在这三栏之间**是复用的**：换一栏就是"换一份表格、行号从 0 数"。
    //       每一栏各自一张表格（各自的 objectName），互不干扰 —— 只是在同一个函数里搭，
    //       没必要给每一栏各起一个名字。
    QGridLayout *grid = addForm(overviewCategoryKey(), addPage(overviewCategoryKey()));
    int row = 0;

    // ---------- 实例名（= 改名，2026-09-25 从实例页右栏那颗「重命名…」按钮搬进来的）----------
    //
    // 它不是一项"覆盖"：别的几行都有「跟随全局 / 自定义」那对单选钮（那些是启动参数，
    // 全局有一份、实例可以盖过它），而这个是这个实例自己的身份，没有"全局"可比 ——
    // 所以它不走 addOverrideRow()，直接一行名字 + 一个输入框 + 一句解释。
    //
    // 真的动手改名的是 MainWindow（先改名、再写设置），这一层只负责让用户改到那个格子
    // （见 instanceName()）。
    {
        addRowTitle(grid, row, instanceNameText(), this);
        auto *cell = new QWidget(this);
        auto *column = new QVBoxLayout(cell);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(4);
        m_instanceNameEdit = new QLineEdit(instanceId, cell);
        m_instanceNameEdit->setObjectName(instanceNameEditName());
        // 初值是**目录名**（不是页眉上那个显示名）—— 见构造函数的 @param instanceId。
        m_instanceNameEdit->setPlaceholderText(instanceNameText());
        column->addWidget(m_instanceNameEdit);
        auto *nameHint = new QLabel(instanceNameHint(), cell);
        nameHint->setWordWrap(true);
        column->addWidget(nameHint);
        grid->addWidget(cell, row, 1);
        ++row;
    }

    // ---------- 图标（2026-09-26）----------
    //
    // 用户原话：「图标（选一张本地图片文件）」「使用实际的那几个 mc 草块 / fabric 布料等的
    // 图片文件，不用担心侵权问题，同时允许用户自主上传」。所以十档：默认 + 八个内置候选
    // + 本地那张。**每一档都有对应的按钮**（八个候选各一颗、「恢复默认」、「选一张图片…」），
    // 没有"换过去就回不来"的死角。
    //
    // NOTE: 那一排按钮**不是**当前值的出处（它们只是 m_iconChoice 的一幅画）—— 理由见 .hpp
    //       里 setInstanceDir() 上面那一段：本地那一档没有控件能表示它。
    {
        addRowTitle(grid, row, iconRowText(), this);
        auto *cell = new QWidget(this);
        auto *column = new QVBoxLayout(cell);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(8);

        auto *top = new QHBoxLayout;
        top->setSpacing(10);
        // 预览：这张图就是"点确定之后树上会长什么样"。定死边长，换图时整行不跟着跳。
        m_iconPreview = new QLabel(cell);
        m_iconPreview->setObjectName(iconPreviewName());
        m_iconPreview->setFixedSize(kIconPreviewSize, kIconPreviewSize);
        m_iconPreview->setAlignment(Qt::AlignCenter);
        top->addWidget(m_iconPreview, 0, Qt::AlignTop);

        auto *candidates = new QGridLayout;
        candidates->setSpacing(4);
        const QStringList keys = VersionIcons::iconCandidateKeys();
        const QStringList names = iconCandidateTexts();
        for (int i = 0; i < keys.size(); ++i) {
            auto *button = new QToolButton(cell);
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
            });
            m_iconButtons.append(button);
            candidates->addWidget(button, i / kIconCandidatesPerRow, i % kIconCandidatesPerRow);
        }
        // 给 1：这八颗吃掉右边剩下的横向空间，别把它们拉成八个大块。
        top->addLayout(candidates, 1);
        column->addLayout(top);

        auto *actions = new QHBoxLayout;
        actions->setSpacing(8);
        auto *pick = new QPushButton(iconPickText(), cell);
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
        });
        actions->addWidget(pick);
        auto *reset = new QPushButton(iconResetText(), cell);
        reset->setObjectName(iconResetButtonName());
        connect(reset, &QPushButton::clicked, this, [this]() {
            // 「默认」= **这个键不存在**（而不是"写一个等于默认的值"）—— 全项目同一个约定，
            // 见 VersionManager::updateVersionMeta() 里那段关于 QJsonValue::Null 的说明。
            // 存成"等于默认"的话，用户之后换了版本分类（比如从原版换成一个 Fabric 实例），
            // 这个实例就不会跟着换标志了，而他要的明明是"默认"。
            m_iconChoice.clear();
            m_pickedIconPath.clear();
            refreshIconRow();
        });
        actions->addWidget(reset);
        actions->addStretch(1);
        column->addLayout(actions);

        auto *hint = new QLabel(iconHint(), cell);
        hint->setWordWrap(true);
        column->addWidget(hint);
        grid->addWidget(cell, row, 1);
        ++row;
    }

    // ---------- 分组（2026-09-26）----------
    //
    // 候选项由调用方灌（setGroupChoices）—— 组名是用户可以改的、id 是稳定身份，这一页
    // 只负责显示。第一项「未分组」由这里自己加（它是保留值，不是某一组）。
    {
        addRowTitle(grid, row, groupRowText(), this);
        auto *cell = new QWidget(this);
        auto *column = new QVBoxLayout(cell);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(4);
        m_groupCombo = new QComboBox(cell);
        m_groupCombo->setObjectName(groupComboName());
        // 和实例页那一列的宽度同一档：这是"选一个名字"，不是输入框。
        m_groupCombo->setFixedWidth(kMemorySpinWidth + 80);
        column->addWidget(m_groupCombo);
        auto *hint = new QLabel(
            tr("这个实例归在实例页的哪一组里。组的排序和增删在实例页左栏的「分组设置…」里。"),
            cell);
        hint->setWordWrap(true);
        column->addWidget(hint);
        grid->addWidget(cell, row, 1);
        ++row;
    }

    // ---------- 置顶（2026-09-26）----------
    {
        addRowTitle(grid, row, pinRowText(), this);
        auto *cell = new QWidget(this);
        auto *column = new QVBoxLayout(cell);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(4);
        m_pinCheck = new QCheckBox(pinCheckText(), cell);
        m_pinCheck->setObjectName(pinCheckName());
        // 勾上/取消都要把分组那一格摆好（勾上 = 挪到置顶组上并禁用）。
        connect(m_pinCheck, &QCheckBox::toggled, this, &InstanceSettingsDialog::applyPinState);
        column->addWidget(m_pinCheck);
        auto *hint = new QLabel(pinHint(), cell);
        hint->setWordWrap(true);
        column->addWidget(hint);
        grid->addWidget(cell, row, 1);
        ++row;
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
        QWidget *cell = addOverrideRow(grid, row++, javaPathField(), tr("Java 路径"),
                                       tr("只对这个实例生效。留空 = 这个实例不指定路径"
                                          "（跟着全局那份走）。"));
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
        QWidget *cell = addOverrideRow(grid, row++, xmxField(), tr("最大内存"),
                                       tr("只对这个实例生效（给 JVM 的 -Xmx）。"));
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
        QWidget *cell = addOverrideRow(grid, row++, xmsField(), tr("最小内存"),
                                       tr("只对这个实例生效（给 JVM 的 -Xms）。"));
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
        QWidget *cell = addOverrideRow(grid, row++, autoMemoryField(), tr("内存分配"),
                                       tr("只对这个实例生效（勾上 = 按系统内存现算）。"));
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

        int advancedRow = 0;
        {
            QWidget *cell = addOverrideRow(
                advancedGrid, advancedRow++, extraJvmArgsField(), tr("额外 JVM 参数"),
                tr("只对这个实例生效：**取代**全局那一份，不是叠加。多条用空格分隔。"));
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
            addRowTitle(advancedGrid, advancedRow, tr("追加参数"), m_advancedBox);
            auto *cell = new QWidget(m_advancedBox);
            auto *cellLayout = new QVBoxLayout(cell);
            cellLayout->setContentsMargins(0, 0, 0, 0);
            cellLayout->setSpacing(4);
            m_appendJvmArgsEdit = new QPlainTextEdit(cell);
            m_appendJvmArgsEdit->setObjectName(appendJvmArgsEditName());
            m_appendJvmArgsEdit->setFixedHeight(kAppendJvmArgsHeight);
            m_appendJvmArgsEdit->setPlainText(appendJvmArgs.join(QLatin1Char('\n')));
            cellLayout->addWidget(m_appendJvmArgsEdit);
            auto *hint = new QLabel(appendJvmArgsHint(), cell);
            hint->setWordWrap(true);
            cellLayout->addWidget(hint);
            advancedGrid->addWidget(cell, advancedRow++, 1);
        }
        {
            // 提醒开关。和上面那行一样：**不是覆盖层的一项**（没有「跟随全局 / 自定义」
            // 那对单选钮），它写的是 meta 里的另一个键，由 MainWindow 合并写进去。
            //
            // 放在「高级选项」里而不是摆在五项旁边：它不是一个"值"，摆在那五行之间会让人
            // 以为它也能跟随全局。藏在这里的代价是"关了之后不好找回来"，所以启动时那个
            // 对话框的正文里点名说了它在哪。
            addRowTitle(advancedGrid, advancedRow, tr("版本提醒"), m_advancedBox);
            auto *cell = new QWidget(m_advancedBox);
            auto *cellLayout = new QVBoxLayout(cell);
            cellLayout->setContentsMargins(0, 0, 0, 0);
            cellLayout->setSpacing(4);
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
            advancedGrid->addWidget(cell, advancedRow++, 1);
        }
    }

    // ---------- 导出启动脚本 / 补全文件（2026-09-26）----------
    //
    // 这两行是**动作**，不是设置项：它们不改 wtfml.meta 里的任何一格，所以没有
    // 「跟随全局 / 自定义」那一对单选钮 —— 和上面「追加参数」「版本提醒」同一个形状
    // （那两个也不是覆盖层的一项）。
    //
    // NOTE: 摆在「高级选项」**之后**：这一页从上到下是"这个实例拿什么启动"（Java、内存、
    //       额外参数），两件动作是收尾。而且高级选项默认折叠着，所以打开这一页看到的
    //       顺序是 Java 路径 → 最大内存 → 最小内存 → 内存分配 → 两件动作。
    {
        addRowTitle(grid, row, exportScriptRowText(), this);
        auto *cell = new QWidget(this);
        auto *column = new QVBoxLayout(cell);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(4);
        auto *button = new QPushButton(exportScriptText(), cell);
        button->setObjectName(exportScriptButtonName());
        connect(button, &QPushButton::clicked, this, [this]() {
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
                this, tr("导出启动脚本"), suggested, tr("批处理文件 (*.bat)"));
            if (picked.isEmpty()) {
                return;
            }
            // 真正"写什么"在 MainWindow 里：这一层够不着 authManager / versionManager
            // （ui/ 不 include src/ 下的头）。见 .hpp 里那个信号的说明。
            emit exportLaunchScriptRequested(picked);
        });
        column->addWidget(button, 0, Qt::AlignLeft);
        auto *hint = new QLabel(exportScriptHint(), cell);
        hint->setWordWrap(true);
        column->addWidget(hint);
        grid->addWidget(cell, row, 1);
        ++row;
    }
    {
        addRowTitle(grid, row, repairFilesRowText(), this);
        auto *cell = new QWidget(this);
        auto *column = new QVBoxLayout(cell);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(4);
        auto *button = new QPushButton(repairFilesText(), cell);
        button->setObjectName(repairFilesButtonName());
        // 只报"用户想干这件事"：能不能开始（有没有下载在跑 / 这个实例装没装）由
        // MainWindow 说了算，它才知道。**不问路径**，所以这一层没什么可问的了。
        connect(button, &QPushButton::clicked, this,
                &InstanceSettingsDialog::repairFilesRequested);
        column->addWidget(button, 0, Qt::AlignLeft);
        auto *hint = new QLabel(repairFilesHint(), cell);
        hint->setWordWrap(true);
        column->addWidget(hint);
        grid->addWidget(cell, row, 1);
        ++row;
    }

    // ---------- 文件管理（八页）----------
    //
    // 这八页说的是**这个实例自己的目录**：`--gameDir` 就是 `versions/<实例名>/`，所以
    // `mods/ resourcepacks/ shaderpacks/ datapacks/ saves/` 都在里头 —— 实例之间天然隔离，
    // 在这一页里动什么都不会碰到别的实例。
    //
    // NOTE: 每页**真正的内容**（列目录 / 启停 / 删除 / 添加 / 打开文件夹）还没做，下一轮填。
    //       现在先把八页的骨架与说明摆上，让"点左栏第 N 条 = 右栏第 N 页"这条不变量当场
    //       就能验（见 ui_check 那几条）。
    {
        const QHash<QString, QString> hints = {
            {QStringLiteral("shortcut"),
             tr("一排按钮，每颗打开一个文件夹：版本目录、存档、截图、资源包、光影包……")},
            {QStringLiteral("mod"), tr("这个实例的 mods 文件夹：装了什么、开没开、删掉哪一个。")},
            {QStringLiteral("resourcepack"), tr("这个实例的 resourcepacks 文件夹。")},
            {QStringLiteral("datapack"), tr("这个实例的 datapacks 文件夹。")},
            {QStringLiteral("shader"), tr("这个实例的 shaderpacks 文件夹。")},
            {QStringLiteral("schematic"), tr("这个实例的 schematics 文件夹（Litematica 那些投影）。")},
            {QStringLiteral("save"), tr("这个实例的 saves 文件夹：一个一个存档。")},
            {QStringLiteral("server"), tr("这个实例的 servers.dat：服务器列表。")},
        };
        const QStringList keys = filePageKeys();
        const QStringList texts = filePageTexts();
        for (int i = 0; i < keys.size(); ++i) {
            QWidget *page = addPage(keys.at(i));
            auto *layout = qobject_cast<QVBoxLayout *>(page->layout());
            if (layout == nullptr) {
                continue;
            }
            auto *heading = new QLabel(texts.value(i), page);
            QFont headingFont = heading->font();
            headingFont.setBold(true);
            heading->setFont(headingFont);
            layout->addWidget(heading);
            auto *hint = new QLabel(hints.value(keys.at(i)), page);
            hint->setWordWrap(true);
            layout->addWidget(hint);
            layout->addStretch(1);
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
    if (m_groupCombo == nullptr) {
        return;
    }
    m_groupCombo->clear();
    // 「未分组」排在最前：它是这一列的"没有归属"，和别的组不是一类东西，摆在末尾会让人
    // 以为它也是一个能排序的组。
    m_groupCombo->addItem(ungroupedGroupText(), QString());
    for (const QPair<QString, QString> &group : groups) {
        m_groupCombo->addItem(group.first, group.second);
    }
    // 按 **userData** 找（组名会变），找不到就停在第一项「未分组」—— 那一组刚被删掉的
    // 实例自然落到未分组，和实例树那边同一条规矩，不为它写清理代码。
    const int index = m_groupCombo->findData(currentGroupId);
    m_groupCombo->setCurrentIndex(index >= 0 ? index : 0);
}

void InstanceSettingsDialog::setPinned(bool pinned, const QString &pinnedGroupId) {
    m_pinnedGroupId = pinnedGroupId;
    if (m_pinCheck != nullptr) {
        // 挡掉 toggled：那一发会调 applyPinState()，而它这时候读到的 m_pinCheck 还是旧值
        // （setChecked 里那句赋值发生在发信号之后）。摆状态只走下面那一处，两处都摆容易
        // 摆出两个不一样的结果来。
        const QSignalBlocker blocker(m_pinCheck);
        m_pinCheck->setChecked(pinned);
    }
    applyPinState();
}

void InstanceSettingsDialog::applyPinState() {
    if (m_groupCombo == nullptr || m_pinCheck == nullptr) {
        return;
    }
    const bool pinned = m_pinCheck->isChecked();
    // 这一份 .minecraft 里没有置顶组（用户把它删了）：勾上这一下真的没有去处
    // （MainWindow::pinInstance() 直接返回），所以下拉框一个字都不动 —— 把它挪到一个不存在
    // 的组上、或者禁用它，都比"看起来什么也没发生"更让人糊涂。
    if (m_pinnedGroupId.isEmpty()) {
        return;
    }
    m_groupCombo->setEnabled(!pinned);
    if (!pinned) {
        // 取消置顶之后下拉框**不动**：回到置顶前那一组是写的那一侧的事（那条记录在这一层
        // 够不着，见 .hpp 里 setGroupChoices() 那段）。它恢复可用之后，用户想改就自己改；
        // 不改的话那边会把"下拉框和打开时一样"读成"用户没表过态"、只做取消置顶。
        return;
    }
    const int index = m_groupCombo->findData(m_pinnedGroupId);
    if (index >= 0) {
        m_groupCombo->setCurrentIndex(index);
    }
}

QString InstanceSettingsDialog::iconChoice() const { return m_iconChoice; }

QString InstanceSettingsDialog::pickedIconPath() const { return m_pickedIconPath; }

QString InstanceSettingsDialog::chosenGroupId() const {
    if (m_groupCombo == nullptr) {
        return QString();
    }
    // 读 userData、不读文字：组名用户随时能改，拿文字当身份当场就散。
    return m_groupCombo->currentData().toString();
}

bool InstanceSettingsDialog::pinned() const {
    return m_pinCheck != nullptr && m_pinCheck->isChecked();
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
    addRowTitle(grid, row, title, this);

    // 一行 = 「跟随全局 / 自定义」那一对单选钮 + 控件（都在右边那一格里）。
    // 控件也加在这一格，是为了内存那两行"整行禁用"能一次把单选钮和数字框都灰掉 ——
    // 只禁数字框会留下一个能选的「自定义」，选了却什么都不发生。
    auto *cell = new QWidget(this);
    auto *cellLayout = new QVBoxLayout(cell);
    cellLayout->setContentsMargins(0, 0, 0, 0);
    cellLayout->setSpacing(4);

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

    grid->addWidget(cell, row, 1);
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
