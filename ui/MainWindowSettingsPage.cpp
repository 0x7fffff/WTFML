// Settings navigation, panels and semantic-color controls.
#include "MainWindowUI.hpp"

#include "Animations.hpp"  // 换页 / 展开折叠 / 上拉面板那几段动效
#include "ElidedLabel.hpp"
#include "Glass.hpp"
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
#include <QSlider>
#include <QSettings>
#include <QStyle>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QResizeEvent>
#include <QRegularExpression>
#include <QScrollArea>
#include <QLineEdit>
#include <QScrollBar>
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
// Static help stays available on demand; dynamic state/error labels remain inline.
void addSettingHelp(QWidget *box, const QString &text, QWidget *) {
    box->setAccessibleDescription(text);
    for (auto *child : box->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
        if (child->accessibleDescription().isEmpty()) child->setAccessibleDescription(text);
    }
    // 说明放在控件下一行，次要色、小一号（7a 设置页样张）。
    addDynamicHint(box, text, box)->setProperty("settingsHelp", true);
}

void addSettingsControl(const SettingsSection &section, const QString &label,
                        QWidget *control, QWidget *parent) {
    // A checkbox already names its setting; avoid a duplicate heading above it.
    const bool namedCheckbox = control->findChild<QCheckBox *>(QString(), Qt::FindDirectChildrenOnly) != nullptr;
    addSettingItem(section, namedCheckbox ? section.title : label, control, parent);
}

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
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
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
    panel.grid->setVerticalSpacing(24);
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
    if (columns.right.title != nullptr) {  // 设计稿：页名手写体 30
        QFont font = Theme::handFont(columns.right.title->font(), Theme::HandFont::Kai);
        font.setPixelSize(30);
        columns.right.title->setFont(font);
    }

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
    // 设计稿 .set-index：纯文字索引，手写体 15。
    QFont categoryFont = Theme::handFont(settingsCategoryList->font(), Theme::HandFont::Kai);
    categoryFont.setPixelSize(15);
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
        // 「标签 | 控件」那一行整行搬（标签在行里）。
        while (box && box->parentWidget() && !box->property("settingsItemRow").toBool()
               && box->parentWidget()->property("settingsSectionTitle").isNull())
            box = box->parentWidget();
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
    // ---- 左栏分类下面：当前这一栏的小节索引（设计稿 .set-index 子项），点一下滚到那一节 ----
    auto *subIndex = new QWidget(columns.left.surface);
    subIndex->setObjectName(QStringLiteral("settingsSubIndex"));
    auto *subLayout = new QVBoxLayout(subIndex);
    subLayout->setContentsMargins(14, 0, 0, 0);
    subLayout->setSpacing(0);
    const int listIndex = columns.left.layout->indexOf(settingsCategoryList);
    columns.left.layout->insertWidget(listIndex + 1, subIndex);
    const auto sectionsOf = [this](int index) {
        QList<QFrame *> cards;
        if (QWidget *panel = settingsCategoryStack->widget(index)) {
            for (QFrame *card : panel->findChildren<QFrame *>())
                if (!card->property("settingsSectionTitle").toString().isEmpty()) cards.append(card);
        }
        return cards;
    };
    const auto scrollTo = [this](int index, QWidget *target) {
        auto *scroll = qobject_cast<QScrollArea *>(settingsCategoryStack->widget(index));
        if (scroll == nullptr || target == nullptr || scroll->widget() == nullptr) return;
        const int y = target->mapTo(scroll->widget(), QPoint(0, 0)).y();
        scroll->verticalScrollBar()->setValue(qMax(0, y - 8));
    };
    const auto rebuildSubIndex = [this, subIndex, subLayout, sectionsOf, scrollTo](int index) {
        while (QLayoutItem *item = subLayout->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        const QList<QFrame *> cards = sectionsOf(index);
        subIndex->setVisible(cards.size() > 1);
        for (QFrame *card : cards) {
            auto *link = new QPushButton(card->property("settingsSectionTitle").toString(), subIndex);
            Theme::markButton(link, Theme::ButtonKind::Ghost);
            link->setProperty("settingsSubIndexItem", true);
            link->setCursor(Qt::PointingHandCursor);
            link->setStyleSheet(QStringLiteral("QPushButton { text-align: left; padding: 4px 8px; min-height: 0; }"));
            QFont f = Theme::textFont(link->font(), Theme::TextRole::Caption);
            link->setFont(f);
            QObject::connect(link, &QPushButton::clicked, link, [scrollTo, index, card] { scrollTo(index, card); });
            subLayout->addWidget(link);
        }
    };
    QObject::connect(settingsCategoryList, &QListWidget::currentRowChanged, settingsCategoryList,
                     [this, rebuildSubIndex](int row) {
                         if (row >= 0 && row < settingsCategoryStack->count()) {
                             settingsCategoryStack->setCurrentIndex(row);
                             rebuildSubIndex(row);
                         }
                     });

    // ---- 页头右上角：搜索设置。按行名 / 小节名找，跳到那一栏并滚到那一行 ----
    auto *search = new QLineEdit(columns.right.header != nullptr ? columns.right.header : page);
    search->setObjectName(QStringLiteral("settingsSearchEdit"));
    search->setPlaceholderText(QStringLiteral("搜索设置"));
    search->setClearButtonEnabled(true);
    search->setFixedWidth(240);
    search->addAction(VersionIcons::line(QStringLiteral("search"), 16, Theme::tokens().secondary),
                      QLineEdit::LeadingPosition);
    if (columns.right.actions != nullptr) columns.right.actions->addWidget(search);
    const auto findSetting = [this, scrollTo](const QString &text) {
        const QString needle = text.trimmed();
        if (needle.isEmpty()) return;
        for (int i = 0; i < settingsCategoryStack->count(); ++i) {
            QWidget *panel = settingsCategoryStack->widget(i);
            for (QLabel *label : panel->findChildren<QLabel *>()) {
                const bool name = label->property("settingsItemLabel").toBool() ||
                                  label->property(Theme::textRolePropertyName().toUtf8().constData()).toString() ==
                                      Theme::sectionTitleTextRoleValue();
                if (!name || !label->text().contains(needle, Qt::CaseInsensitive)) continue;
                settingsCategoryList->setCurrentRow(i);
                QWidget *row = label;
                while (row->parentWidget() && !row->property("settingsItemRow").toBool() &&
                       row->property("settingsSectionTitle").isNull())
                    row = row->parentWidget();
                scrollTo(i, row);
                return;
            }
        }
    };
    QObject::connect(search, &QLineEdit::textChanged, search, findSetting);
    QObject::connect(search, &QLineEdit::returnPressed, search, [search, findSetting] { findSetting(search->text()); });
    // 打开就是第一条：左栏亮着哪一条、右栏就是哪一页，两边不能各说各的。
    settingsCategoryList->setCurrentRow(0);
    settingsCategoryStack->setCurrentIndex(0);
    rebuildSubIndex(0);
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
        addSettingHelp(box, QStringLiteral("自动选择符合游戏版本要求的 Java；可在实例设置中单独指定。"),
                form);
        addSettingsControl(section1, QStringLiteral("运行时"), box, form);
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
        addSettingsControl(section2, QStringLiteral("最大内存"), box, form);
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
        addSettingHelp(box, QStringLiteral("“收起”仅隐藏启动器窗口，游戏继续运行。"),
                form);
        addSettingsControl(section3, QStringLiteral("启动器行为"), box, form);
    }
    {
        auto [box, boxLayout] = makeHintedControlWithLayout(form);
        fileAssociationsButton = new QPushButton(QStringLiteral("文件关联…"), box);
        fileAssociationsButton->setObjectName(fileAssociationsButtonName());
        fileAssociationsButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        boxLayout->addWidget(fileAssociationsButton, 0, Qt::AlignLeft);
        addSettingHelp(box,
                       QStringLiteral("把 .nbt / .litematic / .schematic / .mrpack 的默认打开方式设成本启动器；"
                                      ".jar / .zip 只进“打开方式”列表，不改默认。"),
                       form);
        addSettingsControl(section3, QStringLiteral("文件关联"), box, form);
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

        addSettingHelp(box, QStringLiteral("参数以空格或换行分隔，点击“应用”保存。实例参数优先于全局参数。"),
                form);
        addSettingsControl(section4, QStringLiteral("全局额外参数"), box, form);
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
        addSettingHelp(box, QStringLiteral("每个游戏独立显示日志窗口；关闭后仍保留日志文件。"),
                form);
        // 行标题用 RowText() 那一句，**不是**勾选框上的那句话 —— 否则同一句话
        // 并排出现两遍；见 showGameLogRowText() 上面那段。
        addSettingsControl(section1, showGameLogRowText(), box, form);
    }
    {
        auto [box, boxLayout] = makeHintedControlWithLayout(form);
        processPriorityCombo = new QComboBox(box);
        processPriorityCombo->setObjectName(processPriorityComboName());
        WheelGuard::install(processPriorityCombo);
        boxLayout->addWidget(processPriorityCombo, 0, Qt::AlignLeft);
        addSettingHelp(box, QStringLiteral("设置游戏进程的 CPU 调度优先级。较高优先级可能影响其它程序。"),
                form);
        addSettingsControl(section1, QStringLiteral("进程优先级"), box, form);
    }
    {
        auto [box, boxLayout] = makeHintedControlWithLayout(form);
        gameWindowModeCombo = new QComboBox(box);
        gameWindowModeCombo->setObjectName(gameWindowModeComboName());
        WheelGuard::install(gameWindowModeCombo);
        boxLayout->addWidget(gameWindowModeCombo, 0, Qt::AlignLeft);
        addSettingHelp(box, QStringLiteral("全屏下的尺寸用于退出全屏后。"), form);
        addSettingsControl(section1, QStringLiteral("窗口模式"), box, form);
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
        addSettingHelp(box, QStringLiteral("启动窗口尺寸，单位为像素。游戏可能优先使用自身保存的尺寸。"),
                form);
        addSettingsControl(section1, QStringLiteral("窗口大小"), box, form);
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
        addSettingHelp(box, QStringLiteral("关闭后仍使用实例指定的 Java，版本不符可能导致启动失败。"),
                form);
        addSettingsControl(section1, javaWarnMismatchRowText(), box, form);
    }
    {
        auto [box, boxLayout] = makeHintedControlWithLayout(form);
        warnDuplicateLaunchCheck = new QCheckBox(warnDuplicateLaunchCheckText(), box);
        warnDuplicateLaunchCheck->setObjectName(warnDuplicateLaunchCheckName());
        // 勾选状态由 MainWindow 灌（它才知道这个开关存在哪），这里只搭控件。
        boxLayout->addWidget(warnDuplicateLaunchCheck, 0, Qt::AlignLeft);
        addSettingHelp(box, QStringLiteral("同一账号在 10 秒内重复启动同一实例时确认。"),
                form);
        addSettingsControl(section1, warnDuplicateLaunchRowText(), box, form);
    }
    {
        auto [box, boxLayout] = makeHintedControlWithLayout(form);
        warnWorldLossCheck = new QCheckBox(warnWorldLossCheckText(), box);
        warnWorldLossCheck->setObjectName(warnWorldLossCheckName());
        boxLayout->addWidget(warnWorldLossCheck, 0, Qt::AlignLeft);
        addHint(box, QStringLiteral("同一实例同时运行可能损坏存档。"),
                form);
        addSettingsControl(section1, warnWorldLossRowText(), box, form);
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
        addSettingHelp(box,
                QStringLiteral("留空使用默认标题 %1。")
                    .arg(TitleBar::defaultBrandText()),
                form);
        addSettingsControl(section1, QStringLiteral("标题文字"), box, form);
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
            addSettingHelp(box,
                    QStringLiteral("可选择固定主题或跟随系统。"),
                    form);
            addSettingsControl(section2, themePresetRowText(), box, form);
        }
        {
            // 磨砂：总开关 + 材质（亚克力 / 云母 / 风景底）+ 两条强度。关掉 = 纯色令牌。
            // 存盘与重新铺主题都在 Glass::setMode() / setTintStrength() 里。
            QWidget *glassBox = makeHintedControl(form);
            auto *glass = new QCheckBox(QStringLiteral("磨砂效果"), glassBox);
            glass->setObjectName(glassEffectCheckName());
            glass->setChecked(Glass::mode() != Glass::Mode::Solid);
            qobject_cast<QVBoxLayout *>(glassBox->layout())->addWidget(glass);
            addSettingHelp(glassBox, QStringLiteral("窗口透出背后的风景，电脑较慢时可以关掉。"), form);
            addSettingsControl(section2, QString(), glassBox, form);

            // 材质：三档分段（亚克力 / 云母需 Windows 11；风景底哪儿都行）。
            QWidget *materialBox = makeHintedControl(form);
            auto *material = new QWidget(materialBox);
            material->setObjectName(QStringLiteral("backdropModeSegments"));
            auto *segLayout = new QHBoxLayout(material);
            segLayout->setContentsMargins(3, 3, 3, 3);
            segLayout->setSpacing(2);
            QList<QPushButton *> segments;
            for (Glass::Mode m : {Glass::Mode::Acrylic, Glass::Mode::Mica, Glass::Mode::Scene}) {
                auto *segment = new QPushButton(Glass::modeName(m), material);
                segment->setProperty("glassMode", int(m));
                segLayout->addWidget(segment);
                segments.append(segment);
            }
            Theme::markSegmented(material, segments);
            const auto currentMode = [segments]() {
                for (QPushButton *segment : segments)
                    if (segment->isChecked()) return Glass::Mode(segment->property("glassMode").toInt());
                return Glass::Mode::Scene;
            };
            bool picked = false;
            for (QPushButton *segment : segments) {
                if (segment->property("glassMode").toInt() == int(Glass::mode())) { segment->setChecked(true); picked = true; }
            }
            if (!picked) segments.last()->setChecked(true);
            material->setEnabled(glass->isChecked());
            auto *reason = new QLabel(materialBox);
            reason->setObjectName(QStringLiteral("backdropFallbackLabel"));
            reason->setWordWrap(true);
            reason->setMaximumWidth(320);
            const auto refreshReason = [reason]() {
                QPalette p = reason->palette();
                p.setColor(QPalette::WindowText, p.color(QPalette::PlaceholderText));
                reason->setPalette(p);
                reason->setText(Glass::fallbackReason());
                reason->setVisible(!reason->text().isEmpty());
            };
            const auto apply = [glass, material, currentMode, refreshReason]() {
                material->setEnabled(glass->isChecked());
                Glass::setMode(glass->isChecked() ? currentMode() : Glass::Mode::Solid);
                refreshReason();
            };
            QObject::connect(glass, &QCheckBox::toggled, glass, apply);
            for (QPushButton *segment : segments) QObject::connect(segment, &QPushButton::clicked, segment, apply);
            refreshReason();
            Theme::follow(reason, refreshReason);
            qobject_cast<QVBoxLayout *>(materialBox->layout())->addWidget(material);
            qobject_cast<QVBoxLayout *>(materialBox->layout())->addWidget(reason);
            addSettingHelp(materialBox, QStringLiteral("风景底最通透；亚克力与云母需 Windows 11。"), form);
            addSettingsControl(section2, QStringLiteral("材质"), materialBox, form);
        }
        {
            // 着色 / 模糊强度：两条滑块，拖完才存（拖的过程中不重建）。
            const auto makeStrength = [form](const QString &name, int value, auto setter) {
                auto *slider = new QSlider(Qt::Horizontal, form);
                slider->setObjectName(name);
                slider->setRange(0, 100);
                slider->setValue(value);
                slider->setMinimumWidth(200);
                WheelGuard::install(slider);
                QObject::connect(slider, &QSlider::sliderReleased, slider, [slider, setter]() { setter(slider->value()); });
                QObject::connect(slider, &QSlider::valueChanged, slider, [slider, setter](int v) {
                    if (!slider->isSliderDown()) setter(v);
                });
                return slider;
            };
            QWidget *tintBox = makeHintedControl(form);
            qobject_cast<QVBoxLayout *>(tintBox->layout())->addWidget(
                makeStrength(QStringLiteral("backdropTintSlider"), Glass::tintStrength(), [](int v) { Glass::setTintStrength(v); }),
                0, Qt::AlignLeft);
            addSettingHelp(tintBox, QStringLiteral("盖在背景上的主题色有多浓。为了正文看得清，太淡时会自动加深。"), form);
            addSettingsControl(section2, QStringLiteral("着色强度"), tintBox, form);
            QWidget *blurBox = makeHintedControl(form);
            qobject_cast<QVBoxLayout *>(blurBox->layout())->addWidget(
                makeStrength(QStringLiteral("backdropBlurSlider"), Glass::blurStrength(), [](int v) { Glass::setBlurStrength(v); }),
                0, Qt::AlignLeft);
            addSettingHelp(blurBox, QStringLiteral("风景底模糊得多厉害（亚克力 / 云母由系统模糊）。"), form);
            addSettingsControl(section2, QStringLiteral("模糊强度"), blurBox, form);
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
            addSettingHelp(box, QStringLiteral("对新日志窗口生效，不影响等级过滤、计数和导出。"), form);
            addSettingsControl(section2, QStringLiteral("游戏日志"), box, form);
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
            addSettingHelp(box,
                    QStringLiteral("用于标题栏、选中标记和主要操作按钮。"),
                    form);
            addSettingsControl(section2, themeAccentRowText(), box, form);
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
            addSettingHelp(box,
                    QStringLiteral("调整界面文字的不透明度。"),
                    form);
            addSettingsControl(section2, themeTextOpacityRowText(), box, form);
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
            addSettingHelp(box,
                    QStringLiteral("自定义错误、警告和成功状态的颜色；“跟随预设”恢复主题配色。"),
                    form);
            addSettingsControl(section2, themeSemanticRowText(), box, form);
        }

        m_themeAdvancedEditor = new ThemeAdvancedEditor(form);
        addSettingsControl(section2, QStringLiteral("高级配色"), m_themeAdvancedEditor, form);

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
            addSettingHelp(box,
                    QStringLiteral("恢复默认包含背景设置。导出文件仅保存图片路径，不包含图片。"),
                    form);
            addSettingsControl(section2, themeFileRowText(), box, form);
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
            widgets.opacity->setMinimumWidth(80);
            widgets.opacity->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

            widgets.percent = new QLabel(MainWindowUI::themeImagePercentText(100), line);
            widgets.percent->setObjectName(MainWindowUI::themeImagePercentName(key));
            widgets.percent->setMinimumWidth(MainWindowUI::themeImagePercentWidth());

            lineLayout->addWidget(widgets.choose);
            lineLayout->addWidget(widgets.clear);
            lineLayout->addStretch(1);
            boxLayout->addWidget(line);
            auto [fitLine, fitLayout] = makeInlineRow(box);
            fitLayout->addWidget(new QLabel(QStringLiteral("摆放方式"), fitLine));
            fitLayout->addWidget(widgets.fit, 1);
            boxLayout->addWidget(fitLine);
            auto [opacityLine, opacityLayout] = makeInlineRow(box);
            opacityLayout->addWidget(new QLabel(QStringLiteral("不透明度"), opacityLine));
            opacityLayout->addWidget(widgets.opacity, 1);
            opacityLayout->addWidget(widgets.percent);
            boxLayout->addWidget(opacityLine);

            // "现在铺的是哪一张"那一行：**字号正常、不用 hint 那档灰** —— 它是这一格当前
            // 的状态（和上面那行说明不是一个意思），用户扫一眼要知道自己挑了哪张。
            widgets.state = new QLabel(MainWindowUI::themeImageNoneText(), box);
            widgets.state->setObjectName(MainWindowUI::themeImageStateName(key));
            widgets.state->setWordWrap(true);
            boxLayout->addWidget(widgets.state);

            addSettingHelp(box,
                    QStringLiteral("%1图片会复制到启动器数据目录。透明度影响图片与底色的混合。")
                        .arg(what),
                    form);
            addSettingsControl(section3, Theme::partName(part), box, form);

            m_themeImageRows.append(widgets);
        };

        addImageRow(Theme::Part::HomeLeft,
                    QStringLiteral("主页内容区。"));
        addImageRow(Theme::Part::PageBackground,
                    QStringLiteral("主页、管理、下载和设置的页面背景。"));
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
        addSettingHelp(box, QStringLiteral("管理账号、皮肤和披风。"),
                form);
        addSettingsControl(section4, QStringLiteral("账号"), box, form);
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
        auto *preview = new QLabel(box);
        preview->setObjectName(QStringLiteral("versionNameFormatPreview"));
        preview->setWordWrap(true);
        const auto updatePreview = [this, preview] {
            QStringList pieces;
            if (versionNameCustomCheck->isChecked()) pieces << QStringLiteral("我的世界");
            if (versionNameRealCheck->isChecked()) pieces << QStringLiteral("1.21.1");
            if (versionNameLoaderCheck->isChecked()) pieces << QStringLiteral("Fabric");
            if (versionNameLoaderVersionCheck->isChecked()) pieces << QStringLiteral("0.19.3");
            preview->setText(QStringLiteral("预览：%1").arg(pieces.isEmpty() ? QStringLiteral("1.21.1") : pieces.join(QLatin1Char('-'))));
        };
        for (auto *check : {versionNameCustomCheck, versionNameRealCheck, versionNameLoaderCheck, versionNameLoaderVersionCheck})
            QObject::connect(check, &QCheckBox::toggled, preview, updatePreview);
        updatePreview();
        QTimer::singleShot(0, preview, updatePreview);
        QObject::connect(stack, &QStackedWidget::currentChanged, preview, [updatePreview](int) { updatePreview(); });
        boxLayout->addWidget(preview);

        addSettingHelp(box, QStringLiteral("按所选字段组合显示名称，不更改实例目录名。"),
                form);
        addSettingsControl(section5, QStringLiteral("显示哪些字段"), box, form);
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
        addSettingHelp(box,
                QStringLiteral("控制按压、换页和展开动画；同时遵循系统的减少动态效果设置。"),
                form);
        addSettingsControl(section6, QStringLiteral("切换"), box, form);
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
        addSettingHelp(box, QStringLiteral("0 表示隐藏；窄屏自动精简为图标。"), form);
        addSettingsControl(section7, label, box, form);
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
        addHint(box, QStringLiteral("镜像服务由 BMCLAPI 提供。"),
                form);
        addSettingsControl(section1, QStringLiteral("来源"), box, form);
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
        addSettingHelp(box, QStringLiteral("同时下载的文件上限。降低上限可减少限流和超时；不会中断正在下载的文件。"),
                form);
        addSettingsControl(section1, downloadConcurrencyRowText(), box, form);
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
        addSettingHelp(box, QStringLiteral("0 表示不限速，对正在进行的自动下载生效，不影响工具箱的直链下载。"),
                form);
        addSettingsControl(section1, downloadSpeedLimitRowText(), box, form);
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
        addSettingHelp(box, QStringLiteral("跟随系统、直连或使用 HTTP/SOCKS5 代理。更改对后续请求生效。"),
                form);
        addSettingsControl(section1, downloadProxyRowText(), box, form);
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
        addSettingHelp(box, QStringLiteral("用于 CurseForge 搜索和整合包解析。留空时使用社区镜像。"),
                form);
        addSettingsControl(section1, curseForgeApiKeyRowText(), box, form);
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
        addSettingHelp(box, QStringLiteral("从实例设置发起的下载直接保存到该实例，关闭后每次选择位置；下载页仍需选择位置。"),
                form);
        addSettingsControl(section2, downloadAutoIntoInstanceRowText(), box, form);
    }

    // ---------- 「下载缺前置时提醒」 ----------
    // 和上面那一项同一个栏、同一种形状：一个勾选框 + 一句话说清勾上/不勾分别是什么。
    // 要求见 LauncherSettings 里那一节（那是**唯一**的语义出处）。
    {
        auto [box, boxLayout] = makeHintedControlWithLayout(form);
        warnMissingDependenciesCheck = new QCheckBox(warnMissingDependenciesCheckText(), box);
        warnMissingDependenciesCheck->setObjectName(warnMissingDependenciesCheckName());
        boxLayout->addWidget(warnMissingDependenciesCheck, 0, Qt::AlignLeft);
        addSettingHelp(box, QStringLiteral("下载前检查目标实例的必要前置；关闭后可能因缺少依赖而无法运行。"),
                form);
        addSettingsControl(section2, warnMissingDependenciesRowText(), box, form);
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
