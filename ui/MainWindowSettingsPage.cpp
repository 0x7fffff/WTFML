// Settings navigation, panels and semantic-color controls.
#include "MainWindowUI.hpp"

#include "Animations.hpp"  // 换页 / 展开折叠 / 上拉面板那几段动效
#include "ElidedLabel.hpp"
#include "InstanceGroupDelegate.hpp"
#include "MemorySlider.hpp"
#include "OverlayHost.hpp"
#include "PopupPlacement.hpp"
#include "SkinModel.hpp"
#include "TaskProgressBar.hpp"
#include "Theme.hpp"
#include "ThemeAdvancedEditor.hpp"
#include "ThemeBackground.hpp"
#include "TitleBar.hpp"
#include "ToolboxPage.hpp"  // 工具箱那张表的键与名字（底栏面板那六颗按钮按 tools() 摆）
#include "VersionIcons.hpp"
#include "VersionTreeDelegate.hpp"
#include "WindowChrome.hpp"

#include <QColor>
#include <QEvent>
#include <QFont>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QScreen>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSettings>
#include <QStyle>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QResizeEvent>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSet>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

#include "MainWindowPageLayout.hpp"
using namespace MainWindowPageLayout;

QWidget *MainWindowUI::setupSettingsTab() {
    // ---------- 设置：左 = 选项名称，右 = 选项内容 ----------
    QWidget *settingsTab = new ThemeBackground(Theme::Part::PageBackground, tabWidget);
    settingsTab->setObjectName(settingsTabName());
    setupSettingsPage(settingsTab);
    return settingsTab;
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
 * （`grid->setRowStretch(row, 1)`：剩下的竖直空间留底部，设置项不跟着窗高被拉长）。
 *
 * 每一栏**各自**套一层滚动区，不是整页套一层 —— 整页套一层的话，左栏那五条分类会跟着
 * 右栏的内容一起滚走，而那五条是导航。这条和下载页左栏那一列是同一条理由。
 */
SettingsPanel addSettingsPanel(QStackedWidget *stack, const QString &key) {
    QScrollArea *scroll = new Theme::GutterScrollArea(stack);
    scroll->setObjectName(MainWindowUI::settingsPanelName(key));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    // NOTE: 宽度是给**表单**限的，不是给滚动区限的：滚动条仍然贴着正文面最右边那条线
    // （和别的页一个规矩），只有表单本身收窄。
    // NOTE: 窗口默认 900 逻辑像素时，正文那一栏只有 620 上下 ⇒ 这个上限还够不着，
    // 拉宽窗口才生效。所以量它的判据（ui_check）量的是 `maximumWidth()` 本身，
    // 不是"这一趟实际多宽"。
    scroll->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

    SettingsPanel panel;
    panel.form = new QWidget(scroll);
    panel.form->setObjectName(MainWindowUI::settingsFormName(key));
    panel.form->setMaximumWidth(MainWindowUI::settingsFormMaxWidth());

    panel.grid = new QGridLayout(panel.form);
    panel.grid->setContentsMargins(0, 0, 0, 0);
    panel.grid->setHorizontalSpacing(12);
    panel.grid->setVerticalSpacing(10);
    // 这一对 3:7 只摆表格的形状：每一节都是一张跨两列的卡片（见
    // addSettingsSection()），不分隔任何一行。
    panel.grid->setColumnStretch(0, MainWindowUI::leftColumnStretch());
    panel.grid->setColumnStretch(1, MainWindowUI::rightColumnStretch());
    scroll->setWidget(panel.form);

    stack->addWidget(scroll);
    return panel;
}

} // namespace

void MainWindowUI::setupSettingsPage(QWidget *page) {
    // 设置页分成若干栏，每一栏各自成一页、各滚各的（外观 / 账户 / Java 版本 / 内存
    // 分配 / 启动后 / 启动选项 / 启动提醒 / JVM 参数 / 下载源 / 版本名格式）。十节连成
    // 一竖条时"找一个开关"只能靠滚，而且"哪几项是一回事"只能靠相邻关系去猜。
    //
    // NOTE: 滚动归**栏**管（addSettingsPanel 里那段），理由见它上面。
    // NOTE: 这一份**不是 const**（另几页那三份是）：页头那行标题是搭完之后才由这一页填的
    // （`fillPageHeader()` 收的是 `Column&`）。
    TwoColumns columns = buildTwoColumnPage(page, QStringLiteral("settings"));
    // 页头：标题「设置」。**没有说明那一行**（`fillPageHeader()` 收到空串就把它整行收起来
    // 连位置都不占）—— 这一页"是什么"不需要解释，左栏那五条分类已经说完了。
    fillPageHeader(columns.right, settingsPageTitle(), QString());

    const QStringList categoryKeys = settingsCategoryKeys();
    const QStringList categoryTexts = settingsCategoryTexts();

    // ---- 左栏：那五条分类（和下载页左栏那一列同一个代理、同一套写法）----
    settingsCategoryList = new QListWidget(columns.left.surface);
    settingsCategoryList->setObjectName(settingsCategoryListName());
    styleSidebarList(settingsCategoryList);
    settingsCategoryList->setUniformItemSizes(true);
    // 同一个代理 = 一样高的行、大一号的字、"分类"那种粗体 —— 两栏里是同一种东西。
    // NOTE: **不设** rowRefreshRole()。那一格里画的刷新圈是下载页专有的（这一列没有
    // "重新拉一遍"这回事），代理见到没设就不画。
    // 末一个参数 true = 和下载页左栏那一列**同一档**（侧栏导航）。两处的形状一样，
    // 判据也一样（见 VersionTreeDelegate 构造函数那段）。
    settingsCategoryList->setItemDelegate(
        new VersionTreeDelegate(settingsCategoryList, /*leafArrow=*/true, /*iconSize=*/0,
                                /*navRow=*/true));
    // 悬停底色要它，理由见版本树那第一处：视图不填 State_MouseOver 就不出底色，也不报错。
    settingsCategoryList->setMouseTracking(true);
    QFont categoryFont = settingsCategoryList->font();
    categoryFont = Theme::textFont(categoryFont, Theme::TextRole::Body);
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
    // 按职责收口：启动器行为/进程/日志归启动，下载的前置提醒归安全提醒。
    const auto moveSetting = [this](QWidget *control, const QString &sectionTitle) {
        if (!control) return;
        QFrame *target = nullptr;
        for (auto *card : settingsCategoryStack->findChildren<QFrame *>())
            if (card->property("settingsSectionTitle").toString() == sectionTitle) { target = card; break; }
        QWidget *box = control->parentWidget();
        auto *source = box && box->parentWidget() ? qobject_cast<QVBoxLayout *>(box->parentWidget()->layout()) : nullptr;
        if (!target || !source) return;
        const int index = source->indexOf(box);
        QWidget *label = index > 0 ? source->itemAt(index - 1)->widget() : nullptr;
        if (label && label->property("settingsItemLabel").toBool()) {
            label->setParent(target);
            target->layout()->addWidget(label);
        }
        box->setParent(target);
        target->layout()->addWidget(box);
    };
    moveSetting(showGameLogCheck, QStringLiteral("运行与日志"));
    moveSetting(processPriorityCombo, QStringLiteral("运行与日志"));
    moveSetting(warnMissingDependenciesCheck, QStringLiteral("安全提醒"));

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

void MainWindowUI::setupSettingsLaunchPanel(QStackedWidget *stack) {
    // 这一栏的键。**必须**和 settingsCategoryKeys() 里那一格一字不差（面板的 objectName
    // 由它拼出来），ui_check 拿那张表逐页核。
    const SettingsPanel panel = addSettingsPanel(stack, QStringLiteral("launch"));
    QWidget *form = panel.form;
    QGridLayout *grid = panel.grid;
    int row = 0;

    // ---------- Java 版本 ----------
    // 分栏时这一节拆成两半：**这一行**（运行时）留在这儿，那行"实例的 Java
    // 对不上时要不要问一句"搬去了「提示」栏 —— 它挡的是启动那一下，和那两条提醒是一回事。
    // ---- 第 1 节：标题 + 一张小节卡片（见 addSettingsSection()）----
    const SettingsSection section1 = addSettingsSection(grid, row, QStringLiteral("Java 版本"), form);
    {
        auto [box, boxLayout] = makeHintedControlWithLayout(form);
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
        // 一回事：全局档挑中的 Java 对不上就是直接报错（不替你换、也不问）；
        // 而「实例设置…」里给某个实例单独钉的 Java 对不上时，会弹一个框问要不要
        // 改用匹配的那个（那个开关在「提示」栏里）。全局档不问，是因为它不匹配时
        // 自动档本来就会挑一个替换掉，钉死的那档是用户对**整台机器**的选择 ——
        // 那种一眼就该是自己配错的，不值得每次启动都问一遍。
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
        // 「最大内存」这一行是**一根条 + 一个数字框**（两件东西说的是同一件事）。
        // 条是主要入口（拖动条要像 pcl 那样灵活变通），数字框留着是为了
        // "我就要 6144 这个数"的时候能直接打进去 —— 少了它，拖是拖不到精确值的。
        auto [box, boxLayout] = makeHintedControlWithLayout(form);

        auto [line, lineLayout] = makeInlineRow(box);
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

        // 最右边那颗「自动分配 / 手动分配」。由 `JavaSettingsController` 灌当前档位；
        // 这里只把两项摆好，默认选「手动分配」（和 `GlobalJavaSettings` 的默认一致 ——
        // 老用户打开设置页看到的还是他们自己定的那个数）。
        memoryModeCombo = new QComboBox(line);
        memoryModeCombo->setObjectName(memoryModeComboName());
        memoryModeCombo->addItem(QStringLiteral("自动分配"), true);
        memoryModeCombo->addItem(QStringLiteral("手动分配"), false);
        memoryModeCombo->setCurrentIndex(1);
        memoryModeCombo->setMinimumWidth(kMemoryModeComboWidth);
        WheelGuard::install(memoryModeCombo);
        lineLayout->addWidget(memoryModeCombo, 0);

        // 这一行要**撑满整格**（不是 addInlineControl 那种"按 sizeHint 靠左"）：条的长度
        // 就是它的精度，短了拖不准。
        boxLayout->addWidget(line);
        // 文字由 MainWindow 填（要说清系统总内存 / 当前可用 / 现在用的是多少），这里先给
        // 一句占位，免得空标签把行高压成 0、布局跳一下。
        memoryHint = addDynamicHint(box, QStringLiteral("正在读取系统内存…"), form);
        memoryHint->setObjectName(memoryHintName());
        addSettingItem(section2, QStringLiteral("最大内存"), box, form);
    }
    // NOTE: 「最小内存」（-Xms）**这一行不在界面上**。界面上不让他管这一项，值照旧存在
    // 设置里
    // （默认 1024 MB，命令行照发 -Xms），所以"这一项现在是多少"改由「最大内存」
    // 那一行的说明文字说全（见 MainWindow::refreshMemoryUi 里那三支 setText）。

    // ---------- 启动后 ----------
    // ---- 第 3 节：标题 + 一张小节卡片（见 addSettingsSection()）----
    const SettingsSection section3 = addSettingsSection(grid, row, QStringLiteral("运行与日志"), form);
    {
        auto [box, boxLayout] = makeHintedControlWithLayout(form);
        launcherBehaviourCombo = new QComboBox(box);
        launcherBehaviourCombo->setObjectName(launcherBehaviourComboName());
        WheelGuard::install(launcherBehaviourCombo);
        // **每颗下拉框都按 `sizeHint` 靠左摆**（`AlignLeft`）。不带它时另几颗吃到
        // 拉伸 ⇒ 「进程优先级」那一条（项就「正常（默认）」几个字）被拉成整行那么宽，
        // 右边一大片空白。这几颗是同一件事的同一个控件（选一项），落到的宽度就该是
        // 同一条规矩。
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
        auto [box, boxLayout] = makeHintedControlWithLayout(form);
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

    // NOTE: 「实例预设」的入口在实例页顶部的模式行（客户端实例 | 服务端 | 预设）：
    // 设置里再留一颗门就是同一个东西两处入口（移除全局设置中的重复入口）。

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
    // NOTE: 这三项单独成一栏（这一栏的**全部**内容就是它们）：它们一边区别于
    // 「启动后」（那是启动器自己）、一边区别于「启动提醒」（那是要不要问一句）。
    // ---- 第 1 节：标题 + 一张小节卡片（见 addSettingsSection()）----
    const SettingsSection section1 = addSettingsSection(grid, row, QStringLiteral("游戏窗口"), form);
    {
        auto [box, boxLayout] = makeHintedControlWithLayout(form);
        showGameLogCheck = new QCheckBox(showGameLogCheckText(), box);
        showGameLogCheck->setObjectName(showGameLogCheckName());
        // 勾选状态由 MainWindow 灌（它才知道这个开关存在哪），这里只搭控件。
        boxLayout->addWidget(showGameLogCheck, 0, Qt::AlignLeft);
        addHint(box, QStringLiteral("打开之后，每启动一份游戏都会另外开一个小窗，"
                                    "把那一份游戏自己的输出实时写进去 —— 崩溃前那几行报错就在那里。"
                                    "同时开好几份时一份游戏一个窗，不会混在一起。"
                                    "关掉时游戏输出只进日志文件。"),
                form);
        // 行标题用 RowText() 那一句，**不是**勾选框上的那句话 —— 否则同一句话
        // 并排出现两遍；见 showGameLogRowText() 上面那段。
        addSettingItem(section1, showGameLogRowText(), box, form);
    }
    {
        auto [box, boxLayout] = makeHintedControlWithLayout(form);
        processPriorityCombo = new QComboBox(box);
        processPriorityCombo->setObjectName(processPriorityComboName());
        WheelGuard::install(processPriorityCombo);
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
        auto [box, boxLayout] = makeHintedControlWithLayout(form);
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
        auto [box, boxLayout] = makeHintedControlWithLayout(form);
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
    // NOTE: 这三条并到一栏：前两条（重复启动 / 进度提醒）与「启动选项」分开，第三条
    // （Java 版本）不挂在「启动」栏的 Java 那一节底下 —— 它们管的是同一件事："现在
    // 这一下要不要再问一句"。第三条那一行的行标题是「实例指定的 Java 版本不符时提醒」，
    // 不是说不清提醒什么的「版本提醒」。
    // ---- 第 1 节：标题 + 一张小节卡片（见 addSettingsSection()）----
    const SettingsSection section1 = addSettingsSection(grid, row, QStringLiteral("安全提醒"), form);
    {
        auto [box, boxLayout] = makeHintedControlWithLayout(form);
        // 勾选状态由 MainWindow 灌（它才知道这个开关存在哪），这里只搭控件 ——
        // 和内存那一行的说明文字同一条约定。
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
        auto [box, boxLayout] = makeHintedControlWithLayout(form);
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
        auto [box, boxLayout] = makeHintedControlWithLayout(form);
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
    // 条上那行字（`TitleBar::defaultBrandText()` 那五个字母）能在这儿改成别的，
    // **留空就是默认那五个**。
    // 分栏时它连同「账户」「版本名格式」一起并进「个性化」：这三节管的都不是
    // "游戏怎么起来"，而是"这东西看起来是什么样 / 我叫什么名字"。
    // ---- 第 1 节：标题 + 一张小节卡片（见 addSettingsSection()）----
    const SettingsSection section1 = addSettingsSection(grid, row, QStringLiteral("外观"), form);
    {
        auto [box, boxLayout] = makeHintedControlWithLayout(form);
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

    // ---------- 主题 ----------
    // 「各个地方的背景等允许切换……」—— `ui/Theme` 的
    // Spec / Role / Part 在这一节有了入口：**配色 + 主题色 + 文字不透明度 + 四个
    // 语义色**，外加恢复默认与导入/导出。
    //
    // 这一节只搭控件。**改完立刻生效也立刻落盘**那三步（改内存里那份规格 → 落盘 →
    // `Theme::apply()`）在 `MainWindow::applyThemeSpec()` —— `ui/` 不许 include `src/`。
    // 「配色」那三项要从 `Theme::presetIds()` 来（它是唯一出处），所以项**在这里灌**，
    // 不在 MainWindow —— 和下面「摆法」那个下拉框是同一个写法（那一档的枚举也在 ui/）。
    // ---- 第 2 节：标题 + 一张小节卡片（见 addSettingsSection()）----
    const SettingsSection section2 = addSettingsSection(grid, row, QStringLiteral("主题"), form);
    {
        // ---- 配色（跟随系统 / 浅色 / 深色）----
        {
            auto [box, boxLayout] = makeHintedControlWithLayout(form);
            themePresetCombo = new QComboBox(box);
            themePresetCombo->setObjectName(themePresetComboName());
            const QStringList presets = Theme::presetIds();
            for (const QString &id : presets) {
                themePresetCombo->addItem(Theme::presetName(id), id);
            }
            themePresetCombo->setMinimumWidth(themePresetComboWidth());
            boxLayout->addWidget(themePresetCombo, 0, Qt::AlignLeft);
            addHint(box,
                    QStringLiteral("翡翠实验室与海洋蓝为浅色主题，炭灰适合夜间使用；"
                                   "也可以选择跟随系统的深浅模式。"),
                    form);
            addSettingItem(section2, themePresetRowText(), box, form);
        }
        {
            auto *colors = new QCheckBox(QStringLiteral("日志颜色显示"), form);
            colors->setObjectName(QStringLiteral("logColorsCheck"));
            colors->setChecked(QSettings().value(QStringLiteral("Look/LogColors"), true).toBool());
            QObject::connect(colors, &QCheckBox::toggled, colors, [](bool enabled) {
                QSettings().setValue(QStringLiteral("Look/LogColors"), enabled);
            });
            QWidget *box = makeHintedControl(form);
            qobject_cast<QVBoxLayout *>(box->layout())->addWidget(colors);
            addHint(box, QStringLiteral("对新打开的日志窗口生效。关闭后使用统一文字与背景色，等级计数、过滤和导出仍然保留。"), form);
            addSettingItem(section2, QStringLiteral("游戏日志"), box, form);
        }

        // ---- 主题色（顶栏那一块、当前那一颗导航底下那条线、确认性按钮的字和边）----
        {
            auto [box, boxLayout] = makeHintedControlWithLayout(form);
            auto [line, lineLayout] = makeInlineRow(box);
            lineLayout->setSpacing(themeSwatchRowSpacing());

            themeAccentButton = new QPushButton(line);
            themeAccentButton->setObjectName(themeAccentButtonName());
            themeAccentButton->setFixedSize(themeSwatchWidth(), themeSwatchHeight());
            // 这一颗的**面**是它现在那个色（`Theme::markColorSwatch()` 挂的样式表），
            // 所以「它现在是什么色」不需要额外一行字来说。
            Theme::markColorSwatch(themeAccentButton, Theme::current().accent);

            themeAccentResetButton = new QPushButton(themeAccentResetText(), line);
            themeAccentResetButton->setObjectName(themeAccentResetName());
            // 一般性（和「清除图片」同一档：把这一项拿掉，不是丢掉一份东西）。
            Theme::markButton(themeAccentResetButton, Theme::ButtonKind::Ghost);
            themeAccentResetButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

            lineLayout->addWidget(themeAccentButton);
            lineLayout->addWidget(themeAccentResetButton);
            lineLayout->addStretch(1);
            boxLayout->addWidget(line);
            addHint(box,
                    QStringLiteral("点左边那块色挑一个颜色。顶栏、侧栏选中项那根竖条、"
                                   "确认性按钮（启动 / 下载 / 搜索那些）都用它。"),
                    form);
            addSettingItem(section2, themeAccentRowText(), box, form);
        }

        // ---- 文字不透明度 ----
        {
            auto [box, boxLayout] = makeHintedControlWithLayout(form);
            auto [line, lineLayout] = makeInlineRow(box);
            lineLayout->setSpacing(themeSwatchRowSpacing());

            themeTextOpacitySlider = new QSlider(Qt::Horizontal, line);
            themeTextOpacitySlider->setObjectName(themeTextOpacitySliderName());
            themeTextOpacitySlider->setRange(0, 100);
            themeTextOpacitySlider->setValue(Theme::current().textOpacity);
            themeTextOpacitySlider->setFixedWidth(themeOpacityWidth());
            themeTextOpacityPercent = new QLabel(themePercentText(Theme::current().textOpacity), line);
            themeTextOpacityPercent->setObjectName(themeTextOpacityPercentName());
            themeTextOpacityPercent->setMinimumWidth(themePercentWidth());

            lineLayout->addWidget(themeTextOpacitySlider);
            lineLayout->addWidget(themeTextOpacityPercent);
            lineLayout->addStretch(1);
            boxLayout->addWidget(line);
            addHint(box,
                    QStringLiteral("界面上所有字的不透明度。调低 = 字变淡（下面那张背景图透出来"
                                   "更明显）。100% 是出厂那一档。"),
                    form);
            addSettingItem(section2, themeTextOpacityRowText(), box, form);
        }

        // ---- 语义色（危险 / 警告 / 正常 / 报错）----
        //
        // 四个色块摆成两行两列：一行四块（连名字）在窄窗口下会被挤到换行，而换行是**中间
        // 断开**的那种（第二对掉下去、第一对还撑着），看着像排版坏了。两行两列在宽窄两种
        // 窗口下都是同一个形状。
        {
            auto [box, boxLayout] = makeHintedControlWithLayout(form);
            auto [line, lineLayout] = makeInlineRow(box);
            lineLayout->setSpacing(themeSwatchGroupSpacing());

            QWidget *grid = new QWidget(line);
            auto *gridLayout = new QGridLayout(grid);
            gridLayout->setContentsMargins(0, 0, 0, 0);
            gridLayout->setHorizontalSpacing(themeSwatchRowSpacing() * 2);
            gridLayout->setVerticalSpacing(themeSwatchRowSpacing());
            m_themeSemanticButtons.clear();
            for (int i = 0; i < themeSemanticCount(); ++i) {
                auto *swatch = new QPushButton(grid);
                swatch->setObjectName(themeSemanticButtonName(i));
                swatch->setFixedSize(themeSwatchWidth(), themeSwatchHeight());
                // 尺寸那一档得**自己挂**：应用级样式表里 `QPushButton { min-height: 32px }`
                // 会把 `setFixedSize()` 那一句按回去（四块色算出来是 64×**32**）——
                // 只有 `markIconButton()` 挂的这个属性配得上那条复位规则。
                // **颜色**这一层故意不管（那要按当前深浅现推，只有 `MainWindow` 知道，
                // 见 `refreshThemeControls()`）—— 这里只说"这是一颗图标尺寸的按钮"。
                Theme::markIconButton(swatch);
                m_themeSemanticButtons.append(swatch);

                auto *label = new QLabel(themeSemanticText(i), grid);
                // 名字在**色块左边**（和另外三行"先看名字再看控件"不一样，但这一格是
                // "红色是哪个红"的对照表，名字挨着色块读才对得上）。
                gridLayout->addWidget(label, i / 2, (i % 2) * 2);
                gridLayout->addWidget(swatch, i / 2, (i % 2) * 2 + 1);
            }
            gridLayout->setColumnStretch(4, 1);

            themeSemanticResetButton = new QPushButton(themeSemanticResetText(), line);
            themeSemanticResetButton->setObjectName(themeSemanticResetName());
            Theme::markButton(themeSemanticResetButton, Theme::ButtonKind::Ghost);
            themeSemanticResetButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

            lineLayout->addWidget(grid);
            lineLayout->addSpacing(themeSwatchGroupSpacing());
            // 和上面那颗「恢复默认」**对不齐**是故意的：它管的是四块色，摆在四块色右边。
            lineLayout->addWidget(themeSemanticResetButton, 0, Qt::AlignTop);
            lineLayout->addStretch(1);
            boxLayout->addWidget(line);
            addHint(box,
                    QStringLiteral("红黄绿各是哪个红黄绿（删除失败那种红、警告的黄、"
                                   "「一切正常」的绿、表单报错那处红）。"
                                   "不挑就按当前深浅现推 —— 「跟随预设」把它还给预设。"),
                    form);
            addSettingItem(section2, themeSemanticRowText(), box, form);
        }

        m_themeAdvancedEditor = new ThemeAdvancedEditor(form);
        addSettingItem(section2, QStringLiteral("高级配色"), m_themeAdvancedEditor, form);

        // ---- 整份主题：恢复全部默认 + 导出 / 导入 ----
        {
            auto [box, boxLayout] = makeHintedControlWithLayout(form);
            auto [line, lineLayout] = makeInlineRow(box);
            lineLayout->setSpacing(themeSwatchRowSpacing());

            themeResetAllButton = new QPushButton(themeResetAllText(), line);
            themeResetAllButton->setObjectName(themeResetAllButtonName());
            // 危险那一档（"不可撤销"）：它把这一节**所有**改动一起抹掉
            // ——包括下面「背景图」那一节那两块（都是同一份规格）。
            Theme::markButton(themeResetAllButton, Theme::ButtonKind::Danger);

            themeExportButton = new QPushButton(themeExportText(), line);
            themeExportButton->setObjectName(themeExportButtonName());
            // 「导出」是**看一眼 / 挪个地方**（`ButtonKind` 那张表里点名的）。
            Theme::markButton(themeExportButton, Theme::ButtonKind::Ghost);

            themeImportButton = new QPushButton(themeImportText(), line);
            themeImportButton->setObjectName(themeImportButtonName());
            // 「导入」是"干这件事"那一档（确认性）。
            Theme::markButton(themeImportButton, Theme::ButtonKind::Accent);

            for (QPushButton *button : {themeResetAllButton, themeExportButton, themeImportButton}) {
                button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
                lineLayout->addWidget(button);
            }
            lineLayout->addStretch(1);
            boxLayout->addWidget(line);
            addHint(box,
                    QStringLiteral("「恢复全部默认」把这一栏（含下面的背景图）一起还原成出厂那一份。"
                                   "「导出」存成一个 JSON 文件，能自己改、也能带到别的机器上导回来；"
                                   "背景图那几张图**不在**这个文件里（只有路径）。"),
                    form);
            addSettingItem(section2, themeFileRowText(), box, form);
        }

        // ---- Tab 顺序：这一节按"从上到下、从左到右"排一遍 ----
        //
        // 「合理 Tab 顺序」落到这一节就是这一段。**不写也不会错**（Qt 默认顺序 =
        // 控件建出来的先后，而这一节正是从上往下建的）—— 写下来是为了把它**钉住**：以后谁
        // 在这一节中间插一颗控件，默认顺序会跟着变（屏幕上一点异常都没有，只是按 Tab 的顺序
        // 变得莫名其妙），而这一串不会。
        {
            QVector<QWidget *> chain;
            chain << themePresetCombo << themeAccentButton << themeAccentResetButton
                  << themeTextOpacitySlider;
            for (QPushButton *swatch : m_themeSemanticButtons) {
                chain << swatch;
            }
            chain << themeSemanticResetButton << themeResetAllButton << themeExportButton
                  << themeImportButton;
            for (int i = 1; i < chain.size(); ++i) {
                if (chain.at(i - 1) != nullptr && chain.at(i) != nullptr) {
                    QWidget::setTabOrder(chain.at(i - 1), chain.at(i));
                }
            }
        }
    }

    // ---------- 背景图 ----------
    // 主页左侧自定义图像、四个页面的背景图 —— 两块底各一行，
    // 主页左栏、四个页面的内容区（见 ui/Theme.hpp 里 Part 那两项）。
    //
    // NOTE: 挑图那一下**不是**把用户那个路径记下来：文件会被拷进数据目录
    // （`src/util/ThemeImage.hpp`）。这一节只摆控件，收图和落盘都在 MainWindow。
    // ---- 第 3 节：标题 + 一张小节卡片（见 addSettingsSection()）----
    const SettingsSection section3 = addSettingsSection(grid, row, QStringLiteral("背景图"), form);
    {
        const auto addImageRow = [&](Theme::Part part, const QString &what) {
            const QString key = Theme::partKey(part);
            auto [box, boxLayout] = makeHintedControlWithLayout(form);

            auto [line, lineLayout] = makeInlineRow(box);
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
            addSettingItem(section3, Theme::partName(part), box, form);

            m_themeImageRows.append(widgets);
        };

        addImageRow(Theme::Part::HomeLeft,
                    QStringLiteral("主页左栏那一块（「已安装版本」到「启动游戏」那一片）。"));
        addImageRow(Theme::Part::PageBackground,
                    QStringLiteral("四个页面（主页 / 实例 / 下载 / 设置）内容区那一整块底。"));
    }

    // ---------- 账户 ----------
    // 账号那一整块只在一个窗里做（ui/AccountSkinDialog）：加离线账号、切换账号、
    // 删账号、换皮肤披风都在那儿。这里留一个入口。
    // ---- 第 4 节：标题 + 一张小节卡片（见 addSettingsSection()）----
    const SettingsSection section4 = addSettingsSection(grid, row, QStringLiteral("账户"), form);
    section4.card->hide(); // 账号管理集中在主页；不显示空入口和过时说明。
    {
        auto [box, boxLayout] = makeHintedControlWithLayout(form);
        accountSkinSettingsButton = new QPushButton(accountSkinButtonText(), box);
        // 文案和主页那个入口**共用一份**（accountSkinButtonText()，见上面那段说明）。
        accountSkinSettingsButton->setObjectName(accountSkinSettingsButtonName());
        // 和主页那一颗同一个判断（一般性）—— 两颗字都一样，颜色不该两样。
        Theme::markButton(accountSkinSettingsButton, Theme::ButtonKind::Neutral);
        accountSkinSettingsButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        accountSkinSettingsButton->setMinimumWidth(kAccountSkinButtonMinWidth);
        boxLayout->addWidget(accountSkinSettingsButton, 0, Qt::AlignLeft);
        // 账号入口集中在主页右上身份区；设置页不保留第二扇门。
        accountSkinSettingsButton->hide();
        addHint(box, QStringLiteral("正版登录、添加离线账号、切换账号、换皮肤和披风都在这个窗里。"
                                    "登录后用户名显示在窗口左下角，启动游戏时用的也是它。"),
                form);
        addSettingItem(section4, QStringLiteral("账号"), box, form);
    }

    // ---------- 版本名格式 ----------
    // 实例名长什么样，也是"看起来是什么样"那一类 —— 和「外观」同一栏。
    // ---- 第 5 节：标题 + 一张小节卡片（见 addSettingsSection()）----
    const SettingsSection section5 = addSettingsSection(grid, row, QStringLiteral("版本名格式"), form);
    {
        auto [box, boxLayout] = makeHintedControlWithLayout(form);

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
        addSettingItem(section5, QStringLiteral("显示哪些字段"), box, form);
    }

    // ---------- 动效 ----------
    // 「动画总开关继续保留，并尊重系统"减少动态效果"」。
    // 这一节就是动效那一层（`ui/Animations`）总开关的界面入口。
    //
    // 另外还有一档"系统说别动"（`Animations::platformReducedMotion()`）：Windows 的
    // 「辅助功能 → 视觉效果 → 动画效果」关掉时，这一档为真，**这个勾勾着也不动**。
    // 所以下面那行说明必须说清"它可能被系统按住" —— 否则用户勾了没反应，只会以为坏了。
    // ---- 第 6 节：标题 + 一张小节卡片（见 addSettingsSection()）----
    const SettingsSection section6 = addSettingsSection(grid, row, QStringLiteral("动效"), form);
    {
        auto [box, boxLayout] = makeHintedControlWithLayout(form);
        animationsCheck = new QCheckBox(animationsCheckText(), box);
        animationsCheck->setObjectName(animationsCheckName());
        boxLayout->addWidget(animationsCheck, 0, Qt::AlignLeft);
        addHint(box,
                QStringLiteral("按钮按下的反馈、换页、列表展开折叠那几段。"
                               "关掉之后它们立刻完成 —— 界面一样能用。\n"
                               "Windows 的「辅助功能 → 视觉效果 → 动画效果」关着的时候，"
                               "这一档是不动的（系统说了算）。"),
                form);
        addSettingItem(section6, QStringLiteral("切换"), box, form);
    }

    const SettingsSection section7 =
        addSettingsSection(grid, row, QStringLiteral("主页快速启动"), form);
    const auto addCount = [&](const QString &label, const QString &name, QSpinBox **out) {
        auto [box, boxLayout] = makeHintedControlWithLayout(form);
        auto *spin = new QSpinBox(box);
        spin->setObjectName(name);
        spin->setRange(0, 5);
        spin->setSuffix(QStringLiteral(" 个"));
        WheelGuard::install(spin);
        boxLayout->addWidget(spin, 0, Qt::AlignLeft);
        addHint(box, QStringLiteral("设为 0 时整排隐藏；窗口变窄时条目自动精简为图标。"), form);
        addSettingItem(section7, label, box, form);
        *out = spin;
    };
    addCount(QStringLiteral("收藏排数量"), homeFavoriteCountSpinName(), &homeFavoriteCountSpin);
    addCount(QStringLiteral("上次游玩排数量"), homeRecentCountSpinName(), &homeRecentCountSpin);

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
        auto [box, boxLayout] = makeHintedControlWithLayout(form);
        downloadSourceCombo = new QComboBox(box);
        downloadSourceCombo->setObjectName(downloadSourceComboName());
        WheelGuard::install(downloadSourceCombo);
        boxLayout->addWidget(downloadSourceCombo, 0, Qt::AlignLeft);
        addHint(box, QStringLiteral("镜像源是第三方服务（BMCLAPI），国内通常快很多。"
                                    "下回来的文件不对时可以换回官方源重下。"),
                form);
        addSettingItem(section1, QStringLiteral("来源"), box, form);
    }
    {
        // 「同时最多下几个」。放在「下载源」这一组里：和上面那两格是
        // 同一件事 —— 这一批文件**怎么走**。四档那个表在 src/ 的 DownloadConcurrency.hpp
        // 里（唯一出处），这里是**只有框没有项**，项和选中的那一档都由 MainWindow 灌。
        auto [box, boxLayout] = makeHintedControlWithLayout(form);
        downloadConcurrencyCombo = new QComboBox(box);
        downloadConcurrencyCombo->setObjectName(downloadConcurrencyComboName());
        WheelGuard::install(downloadConcurrencyCombo);
        boxLayout->addWidget(downloadConcurrencyCombo, 0, Qt::AlignLeft);
        addHint(box, QStringLiteral("同时最多几个文件在下载。调大在快网线上更快，"
                                    "网线慢或者被限流时反而更容易超时 —— 默认那一档是实测"
                                    "挑出来的平衡点。改完立刻生效，正在下的那一批也按新值走"
                                    "（已经在下的那几个不会被叫停）。"),
                form);
        addSettingItem(section1, downloadConcurrencyRowText(), box, form);
    }
    {
        // 「下载速度上限」。和上面那格同一个位置、同一个形状：它也是
        // "这一批文件怎么走"的一部分。档位表在 src/ 的 DownloadSpeedLimit.hpp（唯一出处），
        // 这里是**只有框没有项**，项和选中的那一档都由 MainWindow 灌。
        auto [box, boxLayout] = makeHintedControlWithLayout(form);
        downloadSpeedLimitCombo = new QComboBox(box);
        downloadSpeedLimitCombo->setObjectName(downloadSpeedLimitComboName());
        WheelGuard::install(downloadSpeedLimitCombo);
        boxLayout->addWidget(downloadSpeedLimitCombo, 0, Qt::AlignLeft);
        addHint(box, QStringLiteral("给自动下载（游戏文件、模组、资源包这些）压一个总上限，"
                                    "免得把整条网线占满。0 = 不限。\n"
                                    "改完立刻生效：正在下的那一批也会慢下来（不会重新开始）。"
                                    "工具箱里的「直链下载」是另一件工具，不受这一项管。"),
                form);
        addSettingItem(section1, downloadSpeedLimitRowText(), box, form);
    }
    {
        // 「代理」。和上面几格同一个位置：它也是"请求怎么走"。
        // 三档的含义与地址解析在 src/ 的 DownloadProxy.hpp（唯一出处），这里是**只有框
        // 没有项**；手动那一格只在「手动」档露出来（开/关与回填都由 MainWindow 管）。
        auto [box, boxLayout] = makeHintedControlWithLayout(form);
        downloadProxyCombo = new QComboBox(box);
        downloadProxyCombo->setObjectName(downloadProxyComboName());
        WheelGuard::install(downloadProxyCombo);
        boxLayout->addWidget(downloadProxyCombo, 0, Qt::AlignLeft);
        downloadProxyManualEdit = new QLineEdit(box);
        downloadProxyManualEdit->setObjectName(downloadProxyManualEditName());
        downloadProxyManualEdit->setPlaceholderText(
            QStringLiteral("127.0.0.1:7890 或 socks5://127.0.0.1:7890"));
        downloadProxyManualEdit->setVisible(false);
        boxLayout->addWidget(downloadProxyManualEdit);
        addHint(box, QStringLiteral("启动器所有的网络请求都走这一项。\n"
                                    "「跟随系统」= 用 Windows 的 Internet 选项里配的那个；"
                                    "「直连」= 一个代理都不走；「手动」= 填一个 HTTP 或 SOCKS5 "
                                    "代理地址（只认主机和端口，不带路径）。\n"
                                    "改完立刻生效 —— 已经在跑的那几个请求不会被掐断，下一次"
                                    "请求就走新的。"),
                form);
        addSettingItem(section1, downloadProxyRowText(), box, form);
    }
    {
        // CurseForge 的 API key。放在「下载源」这一组里是因为它和上面那格
        // 是同一件事：**下载走哪条路**。那一格管 Minecraft 自己的文件，这一格管整合包
        // 清单里那批文件的"换地址"那一发。
        auto [box, boxLayout] = makeHintedControlWithLayout(form);
        curseForgeApiKeyEdit = new QLineEdit(box);
        curseForgeApiKeyEdit->setObjectName(curseForgeApiKeyEditName());
        curseForgeApiKeyEdit->setPlaceholderText(curseForgeApiKeyPlaceholder());
        // NOTE: 明文显示（不是 Password 那种圆点）。理由：① 这一格是"用户自己去申请来的一串"，
        // 贴错了得能看一眼；② 它本来就明文躺在配置里，圆点给的是**假的**安全感。
        // 它是这一项而不是"账号密码"那一类：泄露的代价是那个用户自己的额度被用掉。
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

    // ---------- 下下来的东西放在哪儿 ----------
    // 要求见 LauncherSettings 里那一节（那是**唯一**的语义出处）。这里只搭控件：
    // 勾选状态由 MainWindow 灌（它才知道这项设置存在哪）。
    // ---- 第 2 节：标题 + 一张小节卡片（见 addSettingsSection()）----
    const SettingsSection section2 = addSettingsSection(grid, row, QStringLiteral("下载的文件"), form);
    {
        auto [box, boxLayout] = makeHintedControlWithLayout(form);
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

    // ---------- 「下载缺前置时提醒」 ----------
    // 和上面那一项同一个栏、同一种形状：一个勾选框 + 一句话说清勾上/不勾分别是什么。
    // 要求见 LauncherSettings 里那一节（那是**唯一**的语义出处）。
    {
        auto [box, boxLayout] = makeHintedControlWithLayout(form);
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

QPushButton *MainWindowUI::getThemeSemanticButton(int index) const {
    // 和上面同一个路数：越界给 nullptr，调用方自己判空。
    if (index < 0 || index >= m_themeSemanticButtons.size()) {
        return nullptr;
    }
    return m_themeSemanticButtons.at(index);
}

QString MainWindowUI::themeSemanticKey(int index) {
    // 这四串是 `Theme::Spec` 里那四位（以及盘上 `Theme/Accents/<Key>` 那四个后缀）。
    // 认的是**名字**不是下标：以后加一个语义色、或者把顺序动了，这边改一处就够
    // （`specToJson()` / `accentSettingKey()` 那两边照旧）。
    switch (index) {
    case 0:
        return QStringLiteral("danger");
    case 1:
        return QStringLiteral("warning");
    case 2:
        return QStringLiteral("ok");
    case 3:
        return QStringLiteral("errorText");
    default:
        // 越界给空串：`themeSemanticButtonName()` 拿它拼名字，空串拼出来是个别的控件名
        // （不会撞上任何一颗真按钮），调用方按"认不出"处理就行。
        return QString();
    }
}

QString MainWindowUI::themeSemanticText(int index) {
    // 上下文写死 "MainWindowUI"：这个类不是 QObject（没有 tr()），和别的表同一个写法。
    switch (index) {
    case 0:
        return QCoreApplication::translate("MainWindowUI", "危险");
    case 1:
        return QCoreApplication::translate("MainWindowUI", "警告");
    case 2:
        return QCoreApplication::translate("MainWindowUI", "正常");
    case 3:
        return QCoreApplication::translate("MainWindowUI", "报错");
    default:
        return QString();
    }
}
