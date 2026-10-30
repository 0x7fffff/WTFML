/**
 * @file PresetManagePage.cpp
 * @brief 实现 PresetManagePage.hpp。
 *
 * 布局从上到下：左栏（预设列表 + 增删改）→ 右栏（这一份是什么 / 从实例提取 / 应用到实例）。
 * 右栏放在滚动区里：窗口矮的时候下边那两段还能滚到，不会被布局压成一条。
 */

#include "PresetManagePage.hpp"
#include "Animations.hpp"
#include "InstanceGroupDelegate.hpp"
#include "MainWindowUI.hpp"
#include "Theme.hpp"

#include <QCheckBox>
#include <QIcon>
#include <QComboBox>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace {

/// 单独开出来时的大小（内嵌到覆盖层里时由那一层给整个内容区，见 OverlayHost）。
constexpr int kDialogWidth = 980;
constexpr int kDialogHeight = 640;
/// 左栏宽度：够摆下「MaLiLib 热键」这种名字 + 一个「（默认）」尾巴。
constexpr int kLeftColumnWidth = 232;
/// 段表 / 计划表的高度：三四行可见，再多就滚。
constexpr int kSectionListHeight = 118;
constexpr int kPlanListHeight = 132;
/// 左栏那张分组列表的行高和行首图标。行高比实例页那一档（58）矮：这一栏是 232 px 的
/// 侧栏，58 一屏看不见三行。组头、悬停、选中那几层画法仍然走 `InstanceGroupDelegate`
/// —— 两边看着是同一张表。
constexpr int kPresetRowHeight = 46;
constexpr int kPresetIconSize = 20;
/// 预设行上那三个数据槽（外面喂进来的值，页面里只用不翻）。
///
/// ★ 号码从 `Qt::UserRole + 20` 起，别往 +1 / +2 上挪：`MainWindowUI::groupIdRole()`
/// 是 `Qt::UserRole + 1`、资源那几张表还有 `+3`，撞上去的症状是"每一行都被当成组头"
/// （选中不生效、右边那一栏永远空着）。`rowBrokenRole()` 必须和这里那一个逐字一致。
constexpr int kRoleId = Qt::UserRole;
constexpr int kRoleBroken = Qt::UserRole + 21;
constexpr int kRoleDefault = Qt::UserRole + 22;
/// 搜索用的一份"名字 + 说明"（原先那个代理模型上的过滤串，现在直接挂在这一格上）。
constexpr int kRoleSearchText = Qt::UserRole + 23;
static_assert(kRoleBroken == PresetManagePage::rowBrokenRole(),
              "rowBrokenRole() 是给外面认行用的那把钥匙，两边不许各写各的");
/// 「读不回来」那一组的钥匙。它不是预设类别（类别钥匙由外面给），才用一个不可能撞上的串。
QString brokenGroupKey() {
    return QStringLiteral("!broken");
}

bool itemIsBroken(const QTreeWidgetItem *item) {
    return item != nullptr && item->data(0, kRoleBroken).toBool();
}

bool itemIsGroup(const QTreeWidgetItem *item) {
    return item != nullptr && item->data(0, MainWindowUI::groupIdRole()).isValid();
}

/// 小一号 + PlaceholderText 那档墨色。和 JavaManageDialog / MainWindowUI 里那几个
/// 匿名 namespace 里的同名帮手是同一套形状（跨不过来，只能各留一份）。
void applyHintStyle(QLabel *hint) {
    hint->setWordWrap(true);
    QFont hintFont = hint->font();
    hintFont = Theme::textFont(hintFont, Theme::TextRole::Caption);
    hint->setFont(hintFont);
    QPalette hintPalette = hint->palette();
    hintPalette.setColor(QPalette::WindowText, hintPalette.color(QPalette::PlaceholderText));
    hint->setPalette(hintPalette);
}

/// 区块标题（「这一份是什么」「从实例提取」……）。粗一号，不带框 —— `QGroupBox` 那一圈
/// 边在 的视觉收口里已经全仓退场了（见 MainWindowUI 那段"最后一个 QGroupBox"）。
QLabel *makeSectionTitle(const QString &text, QWidget *parent) {
    auto *label = new QLabel(text, parent);
    QFont font = label->font();
    font.setBold(true);
    font = Theme::textFont(font, Theme::TextRole::Heading);
    label->setFont(font);
    return label;
}

}  // namespace


PresetManagePage::PresetManagePage(QWidget *parent) : QDialog(parent) {
    setWindowTitle(titleText());
    resize(kDialogWidth, kDialogHeight);
    buildUi();
    refreshActions();
    refreshCaptureEnabled();
}

QString PresetManagePage::titleText() const {
    return QStringLiteral("实例预设");
}

void PresetManagePage::buildUi() {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 12, 12, 12);
    outer->setSpacing(10);

    auto *columns = new QHBoxLayout;
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(12);

    // ------------------------------------------------------------------
    // 左栏：预设列表 + 增删改
    // ------------------------------------------------------------------
    auto *left = new QWidget(this);
    left->setMinimumWidth(0);
    left->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Preferred);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);
    leftLayout->addWidget(makeSectionTitle(QStringLiteral("预设"), left));

    m_searchEdit = new QLineEdit(left);
    m_searchEdit->setObjectName(QStringLiteral("presetSearchEdit"));
    m_searchEdit->setPlaceholderText(tr("搜索名称或说明"));
    m_searchEdit->setClearButtonEnabled(true);
    leftLayout->addWidget(m_searchEdit);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this] { rebuildPresetList(QString(), true); });

    // 一张**按类别分组**的列表：组头是类别名（粗体 + 右端箭头，和实例页 / 服务端页
    // 那两张表同一个代理），组里一行一份预设。窗格本身和实例树同一条规矩：不画外框、
    // 左边不画展开三角（展开/收起画在行尾那个箭头上）、每行等高、开着悬停跟踪
    // （组头的悬停底色和实例页是同一件事）。
    m_presetList = new QTreeWidget(left);
    m_presetList->setObjectName(presetListName());
    m_presetList->setHeaderHidden(true);
    m_presetList->setFrameShape(QFrame::NoFrame);
    m_presetList->setRootIsDecorated(false);
    m_presetList->setUniformRowHeights(true);
    m_presetList->setIndentation(14);
    m_presetList->setIconSize(QSize(kPresetIconSize, kPresetIconSize));
    m_presetList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_presetList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_presetList->setAlternatingRowColors(false);
    m_presetList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_presetList->setMouseTracking(true);
    auto *listDelegate = new InstanceGroupDelegate(m_presetList);
    // 这一页的行尾**不挂动作**（增删改在下面那六颗按钮上）—— 挂着的话行尾会多出三格
    // 点下去没有去处的图标。
    listDelegate->setInlineRowActions(false);
    listDelegate->setRowHeight(kPresetRowHeight);
    m_presetList->setItemDelegate(listDelegate);
    Anim::animateExpansion(m_presetList);
    leftLayout->addWidget(m_presetList, 1);
    connect(m_presetList, &QTreeWidget::currentItemChanged, this,
            &PresetManagePage::onCurrentPresetChanged);
    // 组头点一下 = 展开 / 收起（它不可选中，所以不会抢走"现在看的是哪一份"）。
    m_presetList->setExpandsOnDoubleClick(false);
    connect(m_presetList, &QTreeWidget::itemClicked, this,
            [](QTreeWidgetItem *item, int) {
                if (itemIsGroup(item)) {
                    item->setExpanded(!item->isExpanded());
                }
            });

    // 「新建」建出来的是哪一类：★ 必须四类里选一个 —— 跨类总预设不在产品里。
    auto *newCategoryRow = new QHBoxLayout;
    newCategoryRow->setContentsMargins(0, 0, 0, 0);
    newCategoryRow->setSpacing(8);
    auto *newCategoryLabel = new QLabel(QStringLiteral("新建为"), left);
    newCategoryLabel->setObjectName("presetNewCategoryLabel");
    newCategoryRow->addWidget(newCategoryLabel);
    m_newCategoryCombo = new QComboBox(left);
    m_newCategoryCombo->setObjectName(newCategoryComboName());
    m_newCategoryCombo->setToolTip(
        QStringLiteral("新建（以及「提取为新预设」）时这一份属于哪一类。"));
    newCategoryRow->addWidget(m_newCategoryCombo, 1);
    leftLayout->addLayout(newCategoryRow);

    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);
    m_newButton = new QPushButton(QStringLiteral("新建"), left);
    m_newButton->setObjectName(newButtonName());
    m_newButton->setAutoDefault(false);
    m_newButton->setToolTip(QStringLiteral("建一份空预设；建好之后在右边改名字。"));
    Theme::markButton(m_newButton, Theme::ButtonKind::Accent);
    m_duplicateButton = new QPushButton(QStringLiteral("复制"), left);
    m_duplicateButton->setObjectName(duplicateButtonName());
    m_duplicateButton->setAutoDefault(false);
    m_duplicateButton->setToolTip(QStringLiteral("复制一份，内容和文件原样保留。"));
    m_defaultButton = new QPushButton(QStringLiteral("设为默认"), left);
    m_defaultButton->setObjectName(defaultButtonName());
    m_defaultButton->setAutoDefault(false);
    m_defaultButton->setToolTip(
        QStringLiteral("安装实例时优先选中这一份。"));
    m_deleteButton = new QPushButton(QStringLiteral("删除"), left);
    m_deleteButton->setObjectName(deleteButtonName());
    m_deleteButton->setAutoDefault(false);
    Theme::markButton(m_deleteButton, Theme::ButtonKind::Danger);
    m_deleteButton->setToolTip(
        QStringLiteral("连这一份的目录一起删掉，删掉的东西会进回收站，可以从那里还原。"));
    m_moveUpButton = new QPushButton(QStringLiteral("上移"), left);
    m_moveUpButton->setObjectName(moveUpButtonName());
    m_moveUpButton->setAutoDefault(false);
    m_moveDownButton = new QPushButton(QStringLiteral("下移"), left);
    m_moveDownButton->setObjectName(moveDownButtonName());
    m_moveDownButton->setAutoDefault(false);
    grid->addWidget(m_newButton, 0, 0);
    grid->addWidget(m_duplicateButton, 0, 1);
    grid->addWidget(m_defaultButton, 1, 0);
    grid->addWidget(m_deleteButton, 1, 1);
    grid->addWidget(m_moveUpButton, 2, 0);
    grid->addWidget(m_moveDownButton, 2, 1);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    leftLayout->addLayout(grid);

    connect(m_newButton, &QPushButton::clicked, this, [this] {
        emit createRequested(m_newCategoryCombo->currentData().toString());
    });
    connect(m_duplicateButton, &QPushButton::clicked, this, [this] {
        const QString id = currentPresetId();
        if (!id.isEmpty()) emit duplicateRequested(id);
    });
    connect(m_deleteButton, &QPushButton::clicked, this, [this] {
        const QString id = currentPresetId();
        if (id.isEmpty()) return;
        if (!m_deleteArmed) {
            armDelete();
            return;
        }
        disarmDelete();
        emit deleteRequested(id);
    });
    connect(m_defaultButton, &QPushButton::clicked, this, [this] {
        const QString id = currentPresetId();
        if (id.isEmpty()) return;
        const auto *item = m_presetList->currentItem();
        const bool nowDefault = item != nullptr && item->data(0, kRoleDefault).toBool();
        emit defaultRequested(id, !nowDefault);
    });
    connect(m_moveUpButton, &QPushButton::clicked, this, [this] {
        const QString id = currentPresetId();
        if (!id.isEmpty()) emit moveRequested(id, -1);
    });
    connect(m_moveDownButton, &QPushButton::clicked, this, [this] {
        const QString id = currentPresetId();
        if (!id.isEmpty()) emit moveRequested(id, 1);
    });

    auto *leftScroll = new Theme::GutterScrollArea(this);
    leftScroll->setObjectName("presetCatalogSidebarScroll");
    leftScroll->setFixedWidth(kLeftColumnWidth);
    leftScroll->setWidgetResizable(true);
    leftScroll->setFrameShape(QFrame::NoFrame);
    leftScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    leftScroll->setWidget(left);
    columns->addWidget(leftScroll);

    // ------------------------------------------------------------------
    // 右栏：放进滚动区（矮窗口里下边那两段还能滚到）
    // ------------------------------------------------------------------
    auto *scroll = new Theme::GutterScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *right = new QWidget(scroll);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 6, 0);
    rightLayout->setSpacing(8);

    // ---------- 这一份是什么 ----------
    rightLayout->addWidget(makeSectionTitle(QStringLiteral("预设内容"), right));

    auto *details = new QGridLayout;
    details->setContentsMargins(0, 0, 0, 0);
    details->setHorizontalSpacing(8);
    details->setVerticalSpacing(8);
    details->setColumnStretch(1, 1);
    details->addWidget(new QLabel(QStringLiteral("名称"), right), 0, 0, Qt::AlignRight | Qt::AlignVCenter);
    m_nameEdit = new QLineEdit(right);
    m_nameEdit->setObjectName(nameEditName());
    m_nameEdit->setPlaceholderText(QStringLiteral("预设名称"));
    details->addWidget(m_nameEdit, 0, 1);

    details->addWidget(new QLabel(QStringLiteral("说明"), right), 1, 0, Qt::AlignRight | Qt::AlignVCenter);
    m_descriptionEdit = new QLineEdit(right);
    m_descriptionEdit->setObjectName(descriptionEditName());
    m_descriptionEdit->setPlaceholderText(QStringLiteral("说明（可选）"));
    details->addWidget(m_descriptionEdit, 1, 1);
    m_saveDetailsButton = new QPushButton(QStringLiteral("保存"), right);
    m_saveDetailsButton->setObjectName(saveDetailsButtonName());
    m_saveDetailsButton->setAutoDefault(false);
    Theme::markButton(m_saveDetailsButton, Theme::ButtonKind::Neutral);
    details->addWidget(m_saveDetailsButton, 0, 2, 2, 1, Qt::AlignVCenter);
    rightLayout->addLayout(details);
    connect(m_saveDetailsButton, &QPushButton::clicked, this, [this] {
        const QString id = currentPresetId();
        if (id.isEmpty()) return;
        emit detailsEdited(id, m_nameEdit->text(), m_descriptionEdit->text());
    });
    // Enter 也算「保存」（名字框里打完字顺手回车是最常见的动作）。
    connect(m_nameEdit, &QLineEdit::returnPressed, m_saveDetailsButton, &QPushButton::click);
    connect(m_descriptionEdit, &QLineEdit::returnPressed, m_saveDetailsButton,
            &QPushButton::click);

    m_presetMeta = new QLabel(right);
    m_presetMeta->setObjectName(QStringLiteral("presetMetaLabel"));
    applyHintStyle(m_presetMeta);
    rightLayout->addWidget(m_presetMeta);

    m_sectionList = new QListWidget(right);
    m_sectionList->setObjectName(sectionListName());
    m_sectionList->setSelectionMode(QAbstractItemView::NoSelection);
    m_sectionList->setFocusPolicy(Qt::NoFocus);
    m_sectionList->setFixedHeight(kSectionListHeight);
    rightLayout->addWidget(m_sectionList);
    auto *editMods = new QPushButton(tr("编辑模组清单"), right);
    editMods->setObjectName(QStringLiteral("presetEditModsButton"));
    Theme::markButton(editMods, Theme::ButtonKind::Neutral);
    editMods->setVisible(false);
    rightLayout->addWidget(editMods, 0, Qt::AlignLeft);
    connect(editMods, &QPushButton::clicked, this, [this] {
        if (!currentPresetId().isEmpty()) emit editModsRequested(currentPresetId());
    });
    auto *editOptions = new QPushButton(tr("编辑游戏设置与按键"), right);
    editOptions->setObjectName(QStringLiteral("presetEditOptionsButton"));
    Theme::markButton(editOptions, Theme::ButtonKind::Neutral);
    editOptions->setVisible(false);
    rightLayout->addWidget(editOptions, 0, Qt::AlignLeft);
    connect(editOptions, &QPushButton::clicked, this, [this] {
        if (!currentPresetId().isEmpty()) emit editOptionsRequested(currentPresetId());
    });

    // ---------- 从实例提取 ----------
    auto *captureToggle = new QPushButton(tr("从实例提取…"), right);
    captureToggle->setObjectName(QStringLiteral("presetCaptureToggle"));
    captureToggle->setCheckable(true);
    captureToggle->setAutoDefault(false);
    Theme::markButton(captureToggle, Theme::ButtonKind::Neutral);
    rightLayout->addWidget(captureToggle, 0, Qt::AlignLeft);
    auto *captureBody = new QWidget(right);
    captureBody->setObjectName(QStringLiteral("presetCaptureBody"));
    auto *captureLayout = new QVBoxLayout(captureBody);
    captureLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(captureBody);
    captureBody->hide();
    connect(captureToggle, &QPushButton::toggled, captureBody, &QWidget::setVisible);

    auto *captureGrid = new QGridLayout;
    captureGrid->setObjectName(QStringLiteral("presetCaptureGrid"));
    captureGrid->setContentsMargins(0, 0, 0, 0);
    captureGrid->setHorizontalSpacing(12);
    captureGrid->setVerticalSpacing(6);
    auto addCheck = [this, captureGrid, right](int row, int column, const QString &text,
                                               const QString &tooltip, bool checked,
                                               QCheckBox **out) {
        auto *box = new QCheckBox(text, right);
        box->setChecked(checked);
        box->setToolTip(tooltip);
        m_captureTips.insert(box, tooltip);
        captureGrid->addWidget(box, row, column);
        *out = box;
        connect(box, &QCheckBox::toggled, this, &PresetManagePage::refreshCaptureEnabled);
        return box;
    };
    captureGrid->addWidget(new QLabel(QStringLiteral("游戏设置"), right), 0, 0);
    addCheck(0, 1, QStringLiteral("基础设置"),
             QStringLiteral("FOV、界面缩放、语言、音量、渲染距离等。"),
             true, &m_captureOptions);
    addCheck(0, 2, QStringLiteral("标准按键"),
             QStringLiteral("options.txt 里的 key_*。"),
             true, &m_captureKeys);
    captureGrid->addWidget(new QLabel(QStringLiteral("服务器"), right), 1, 0);
    addCheck(1, 1, QStringLiteral("服务器列表"),
             QStringLiteral("servers.dat；追加到目标实例，已有地址不重复。"),
             true, &m_captureServers);
    captureGrid->addWidget(new QLabel(QStringLiteral("配置文件"), right), 2, 0);
    addCheck(2, 1, QStringLiteral("config/"),
             QStringLiteral("整份按「缺了才拷」落下去，不会盖掉目标上已经有的同名项。"),
             true, &m_captureConfig);
    addCheck(2, 2, QStringLiteral("defaultconfigs/"),
             QStringLiteral("新实例第一次启动时模组会照它生成配置。"), false,
             &m_captureDefaultConfigs);
    captureGrid->addWidget(new QLabel(QStringLiteral("资源包 / 光影"), right), 3, 0);
    addCheck(3, 1, QStringLiteral("资源包"),
             QStringLiteral("resourcepacks/；这一档动辄几十上百 MB，默认不收。"), false,
             &m_captureResourcePacks);
    addCheck(3, 2, QStringLiteral("光影包"),
             QStringLiteral("shaderpacks/；同上，默认不收。"), false, &m_captureShaderPacks);
    captureGrid->addWidget(new QLabel(QStringLiteral("MaLiLib"), right), 4, 0);
    addCheck(4, 1, QStringLiteral("热键"),
             QStringLiteral("按结构认这一族，一个模组一段；默认只收绑好的热键。"),
             true, &m_captureMalilib);
    addCheck(4, 2, QStringLiteral("连值一起收"),
             QStringLiteral("把开关和别的值也收进来。"),
             false, &m_captureMalilibValues);
    captureGrid->addWidget(new QLabel(QStringLiteral("数据包"), right), 5, 0);
    addCheck(5, 1, QStringLiteral("存档数据包"),
             QStringLiteral("saves/<世界>/datapacks/；目标实例没有该存档时先记为「待应用」。"),
             false, &m_captureDataPacks);
    captureGrid->setColumnStretch(3, 1);
    captureLayout->addLayout(captureGrid);

    auto *captureRow = new QHBoxLayout;
    captureRow->setContentsMargins(0, 0, 0, 0);
    captureRow->setSpacing(8);
    captureRow->addWidget(new QLabel(QStringLiteral("来源实例"), right));
    m_captureInstanceCombo = new QComboBox(right);
    m_captureInstanceCombo->setObjectName(captureInstanceComboName());
    m_captureInstanceCombo->setMinimumWidth(220);
    captureRow->addWidget(m_captureInstanceCombo, 1);
    captureLayout->addLayout(captureRow);

    auto *captureButtons = new QHBoxLayout;
    captureButtons->setContentsMargins(0, 0, 0, 0);
    captureButtons->setSpacing(8);
    m_captureButton = new QPushButton(QStringLiteral("更新当前预设"), right);
    m_captureButton->setObjectName(captureButtonName());
    m_captureButton->setAutoDefault(false);
    m_captureButton->setToolTip(
        QStringLiteral("收进来的那几段替换掉这一份里同名的那几段；这一份里别的段原样留着。"));
    Theme::markButton(m_captureButton, Theme::ButtonKind::Accent);
    captureButtons->addWidget(m_captureButton);
    m_newPresetNameEdit = new QLineEdit(right);
    m_newPresetNameEdit->setObjectName(newPresetNameEditName());
    m_newPresetNameEdit->setPlaceholderText(QStringLiteral("新预设名称（可选）"));
    m_newPresetNameEdit->setMaximumWidth(220);
    captureButtons->addWidget(m_newPresetNameEdit);
    m_captureAsNewButton = new QPushButton(QStringLiteral("提取为新预设"), right);
    m_captureAsNewButton->setObjectName(captureAsNewButtonName());
    m_captureAsNewButton->setAutoDefault(false);
    Theme::markButton(m_captureAsNewButton, Theme::ButtonKind::Neutral);
    captureButtons->addWidget(m_captureAsNewButton);
    captureButtons->addStretch(1);
    captureLayout->addLayout(captureButtons);

    connect(m_captureButton, &QPushButton::clicked, this, [this] {
        const QString presetId = currentPresetId();
        const QString instanceId = comboInstanceId(m_captureInstanceCombo);
        if (presetId.isEmpty() || instanceId.isEmpty()) return;
        emit captureRequested(presetId, instanceId, captureChoice());
    });
    connect(m_captureAsNewButton, &QPushButton::clicked, this, [this] {
        const QString instanceId = comboInstanceId(m_captureInstanceCombo);
        if (instanceId.isEmpty()) return;
        emit captureAsNewRequested(instanceId, m_newPresetNameEdit->text().trimmed(),
                                   m_newCategoryCombo->currentData().toString(),
                                   captureChoice());
    });
    connect(m_captureInstanceCombo, &QComboBox::currentIndexChanged, this,
            &PresetManagePage::refreshCaptureEnabled);

    // ---------- 应用到实例 ----------
    rightLayout->addWidget(makeSectionTitle(QStringLiteral("应用到实例"), right));

    auto *applyRow = new QHBoxLayout;
    applyRow->setContentsMargins(0, 0, 0, 0);
    applyRow->setSpacing(8);
    applyRow->addWidget(new QLabel(QStringLiteral("目标实例"), right));
    m_applyInstanceCombo = new QComboBox(right);
    m_applyInstanceCombo->setObjectName(applyInstanceComboName());
    m_applyInstanceCombo->setMinimumWidth(220);
    applyRow->addWidget(m_applyInstanceCombo, 1);
    m_previewButton = new QPushButton(QStringLiteral("预览计划"), right);
    m_previewButton->setObjectName(previewButtonName());
    m_previewButton->setAutoDefault(false);
    m_previewButton->setToolTip(
        QStringLiteral("查看更改和冲突，不写入实例。"));
    Theme::markButton(m_previewButton, Theme::ButtonKind::Neutral);
    applyRow->addWidget(m_previewButton);
    rightLayout->addLayout(applyRow);
    connect(m_previewButton, &QPushButton::clicked, this, [this] {
        if (m_planLoading) {
            emit previewCancelRequested();
            return;
        }
        const QString presetId = currentPresetId();
        const QString instanceId = comboInstanceId(m_applyInstanceCombo);
        if (presetId.isEmpty() || instanceId.isEmpty()) return;
        clearPlan();
        emit planRequested(presetId, instanceId);
    });
    // 换实例 / 换预设都会让上一份计划作废（计划是"对着那个实例那一刻"算出来的）。
    connect(m_applyInstanceCombo, &QComboBox::currentIndexChanged, this, [this] {
        clearPlan();
        emit applyTargetChanged(comboInstanceId(m_applyInstanceCombo));
        refreshActions();
    });

    m_planList = new QListWidget(right);
    m_planList->setObjectName(planListName());
    m_planList->setSelectionMode(QAbstractItemView::NoSelection);
    m_planList->setFocusPolicy(Qt::NoFocus);
    m_planList->setFixedHeight(kPlanListHeight);
    rightLayout->addWidget(m_planList);

    auto *applyButtons = new QHBoxLayout;
    applyButtons->setContentsMargins(0, 0, 0, 0);
    applyButtons->setSpacing(8);
    m_planHint = new QLabel(right);
    applyHintStyle(m_planHint);
    applyButtons->addWidget(m_planHint, 1);
    m_applyButton = new QPushButton(QStringLiteral("应用"), right);
    m_applyButton->setObjectName(applyButtonName());
    m_applyButton->setAutoDefault(false);
    Theme::markButton(m_applyButton, Theme::ButtonKind::Primary);
    applyButtons->addWidget(m_applyButton);
    auto *reportButton = new QPushButton(tr("执行报告"), right);
    reportButton->setObjectName("presetExecutionReportButton");
    Theme::markButton(reportButton, Theme::ButtonKind::Neutral); applyButtons->addWidget(reportButton);
    auto *resumeButton = new QPushButton(tr("继续未完成"), right);
    resumeButton->setObjectName("presetResumeButton");
    Theme::markButton(resumeButton, Theme::ButtonKind::Neutral); applyButtons->addWidget(resumeButton);
    connect(reportButton, &QPushButton::clicked, this, [this] {
        if (!m_busy) emit executionReportRequested(currentPresetId(), comboInstanceId(m_applyInstanceCombo));
    });
    connect(resumeButton, &QPushButton::clicked, this, [this, resumeButton] {
        if (m_busy || currentPresetId().isEmpty() || comboInstanceId(m_applyInstanceCombo).isEmpty()) return;
        const QString selection = currentPresetId() + QLatin1Char(':') + comboInstanceId(m_applyInstanceCombo);
        if (resumeButton->property("armedSelection").toString() != selection) {
            emit executionReportRequested(currentPresetId(), comboInstanceId(m_applyInstanceCombo));
            resumeButton->setProperty("armedSelection", selection);
            resumeButton->setText(tr("再点一次继续未完成"));
            QTimer::singleShot(4000, resumeButton, [resumeButton] {
                resumeButton->setProperty("armedSelection", QString()); resumeButton->setText(tr("继续未完成"));
            });
            return;
        }
        resumeButton->setProperty("armedSelection", QString()); resumeButton->setText(tr("继续未完成"));
        emit resumeRequested(currentPresetId(), comboInstanceId(m_applyInstanceCombo));
    });
    rightLayout->addLayout(applyButtons);
    connect(m_applyButton, &QPushButton::clicked, this, [this] {
        if (!m_planCanApply || m_busy) return;
        const QString presetId = currentPresetId();
        const QString instanceId = comboInstanceId(m_applyInstanceCombo);
        if (presetId.isEmpty() || instanceId.isEmpty()) return;
        if (m_planNeedsConfirm && !m_applyArmed) {
            armApply();
            return;
        }
        const bool confirmed = m_planNeedsConfirm || m_planReplaceConfirmed;
        disarmApply();
        emit applyRequested(presetId, instanceId, confirmed);
    });

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(statusLabelName());
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_statusLabel->hide();
    // Loading/errors must remain visible without scrolling through the editor.
    outer->addWidget(m_statusLabel);

    // 收尾的弹簧：多出来的高度归它，别把每一块均匀撑开。
    rightLayout->addStretch(1);

    scroll->setWidget(right);
    columns->addWidget(scroll, 1);
    outer->addLayout(columns, 1);
}

// ---------------------------------------------------------------------------
// 外面灌进来的值
// ---------------------------------------------------------------------------

void PresetManagePage::setPresets(const QList<PresetItem> &presets,
                                  const QList<BrokenItem> &broken, const QString &selectId) {
    m_presets = presets;
    m_broken = broken;
    rebuildPresetList(selectId, false);
    m_currentId = currentPresetId();
}

void PresetManagePage::rebuildPresetList(const QString &selectId, bool emitSelection) {
    const QString previousId = m_currentId;
    const QString keep = selectId.isEmpty() ? m_currentId : selectId;
    const QString query = m_searchEdit->text().trimmed();
    m_fillingList = true;
    m_presetList->clear();

    // 先按类别分桶。桶与桶之间按 `setCategories()` 给的那个次序摆（和外面那四类
    // + 旧版 / 综合 / 草稿的固定次序一致），不认识的钥匙排在后面、按第一次出现的次序
    // —— 页面不认识钥匙背后的意思，它只负责"同一类的摆在一起"。
    QHash<QString, QList<const PresetItem *>> buckets;
    QStringList keys;
    for (const PresetItem &preset : m_presets) {
        if (!buckets.contains(preset.categoryKey)) {
            buckets.insert(preset.categoryKey, {});
            keys.append(preset.categoryKey);
        }
        buckets[preset.categoryKey].append(&preset);
    }
    QStringList ordered;
    for (const QString &key : m_groupOrder) {
        if (buckets.contains(key)) ordered.append(key);
    }
    for (const QString &key : keys) {
        if (!ordered.contains(key)) ordered.append(key);
    }

    auto makeGroup = [this](const QString &text) {
        auto *group = new QTreeWidgetItem(m_presetList);
        group->setText(0, text);
        // 组头这一格是 `InstanceGroupDelegate` 认"这是组"的判据（和实例页同一把钥匙）。
        group->setData(0, MainWindowUI::groupIdRole(), text);
        group->setFlags(Qt::ItemIsEnabled);
        group->setFirstColumnSpanned(true);
        return group;
    };

    for (const QString &key : ordered) {
        QString text = m_groupText.value(key);
        if (text.isEmpty() && !buckets.value(key).isEmpty()) {
            text = buckets.value(key).first()->categoryText;
        }
        if (text.isEmpty()) text = key;
        auto *group = makeGroup(text);
        for (const PresetItem *preset : buckets.value(key)) {
            auto *item = new QTreeWidgetItem(group);
            QString label = preset->name;
            if (preset->isDefault) {
                label += QStringLiteral("（默认）");
            }
            item->setText(0, label);
            item->setData(0, kRoleId, preset->id);
            item->setData(0, kRoleBroken, false);
            item->setData(0, kRoleDefault, preset->isDefault);
            item->setData(0, kRoleSearchText,
                          preset->name + QLatin1Char(' ') + preset->description);
            if (!preset->iconPath.isEmpty()) item->setIcon(0, QIcon(preset->iconPath));
            QString tip = preset->description;
            if (!preset->categoryText.isEmpty()) {
                if (!tip.isEmpty()) tip += QLatin1Char('\n');
                tip += QStringLiteral("类别：%1").arg(preset->categoryText);
            }
            if (!preset->updatedText.isEmpty()) {
                if (!tip.isEmpty()) tip += QLatin1Char('\n');
                tip += preset->updatedText;
            }
            tip += QStringLiteral("\n%1 段").arg(preset->sectionCount);
            item->setToolTip(0, tip);
        }
    }

    // 读不回来的那几份：它们没写类别，硬塞进某一类里等于替它们猜一个身份 —— 所以单开
    // 一组摆在最后。照样列出来但不可选：用户至少知道"它还在，只是读不动"。
    // 这一组**不跟着搜索走**（它们没有可信的名字可比），和原先"在全部那一档里一直露着"
    // 是同一条规矩。
    if (!m_broken.isEmpty()) {
        auto *group = makeGroup(tr("读不出来"));
        // 这一组的钥匙要**真的**写成 `brokenGroupKey()`：下面按搜索藏行时靠它把这一组
        // 摘出去（组里那几行没有可比的名字，跟着搜索一起消失才是撒谎）。
        group->setData(0, MainWindowUI::groupIdRole(), brokenGroupKey());
        for (const BrokenItem &one : m_broken) {
            auto *item = new QTreeWidgetItem(group);
            QString name = one.id;
            if (name.size() > 12) name = name.left(12) + QStringLiteral("…");
            item->setText(0, QStringLiteral("⚠ 读不出来：%1").arg(name));
            item->setData(0, kRoleId, one.id);
            item->setData(0, kRoleBroken, true);
            item->setFlags(Qt::NoItemFlags);
            item->setToolTip(0, one.reason);
        }
    }

    // 搜索：只藏行，不重新建行（一遍一遍重建会把滚动位置和展开状态一起丢掉）。
    for (int g = 0; g < m_presetList->topLevelItemCount(); ++g) {
        auto *group = m_presetList->topLevelItem(g);
        if (group->data(0, MainWindowUI::groupIdRole()).toString() == brokenGroupKey()) {
            continue;  // 上面那段：那一组不跟着搜索走
        }
        int visible = 0;
        for (int i = 0; i < group->childCount(); ++i) {
            auto *child = group->child(i);
            const bool match = query.isEmpty() ||
                               child->data(0, kRoleSearchText)
                                   .toString()
                                   .contains(query, Qt::CaseInsensitive);
            child->setHidden(!match);
            if (match) ++visible;
        }
        group->setHidden(visible == 0);
    }

    // 选中的那一份不在列表里（或者被搜索藏掉了）就退回第一条看得见的。
    QTreeWidgetItem *target = nullptr;
    QTreeWidgetItem *first = nullptr;
    for (int g = 0; g < m_presetList->topLevelItemCount() && target == nullptr; ++g) {
        auto *group = m_presetList->topLevelItem(g);
        if (group->isHidden()) continue;
        group->setExpanded(true);
        for (int i = 0; i < group->childCount(); ++i) {
            auto *child = group->child(i);
            if (child->isHidden() || itemIsBroken(child)) continue;
            if (first == nullptr) first = child;
            if (child->data(0, kRoleId).toString() == keep) {
                target = child;
                break;
            }
        }
    }
    m_presetList->setCurrentItem(target != nullptr ? target : first);
    m_fillingList = false;

    if (currentPresetId().isEmpty()) {
        m_currentId.clear();
        m_nameEdit->clear();
        m_descriptionEdit->clear();
        m_presetMeta->clear();
        m_sectionList->clear();
        m_presetMeta->setText(m_presets.isEmpty()
            ? tr("尚无预设")
            : tr("未找到匹配的预设"));
    }
    refreshActions();

    // 用户自己换了搜索词：新选中的那一份要跟着刷新右栏（外面靠这个信号知道"现在看的是谁"）。
    const QString nowId = currentPresetId();
    if (emitSelection && nowId != previousId) {
        m_currentId = nowId;
        clearPlan();
        emit presetSelected(nowId);
    }
}

void PresetManagePage::setCategories(const QList<CategoryOption> &categories) {
    // 分组次序与组名：空钥匙 = 「全部」那一项，它不是一组。
    m_groupOrder.clear();
    m_groupText.clear();
    for (const CategoryOption &option : categories) {
        if (option.key.isEmpty()) continue;
        m_groupOrder.append(option.key);
        m_groupText.insert(option.key, option.text);
    }

    // 「新建为」：只列真正能新建的那些（「全部」那一项不进下拉框）。
    const QString keepNew = m_newCategoryCombo->currentData().toString();
    {
        const QSignalBlocker blocker(m_newCategoryCombo);
        m_newCategoryCombo->clear();
        for (const CategoryOption &option : categories) {
            if (!option.key.isEmpty() && !option.filterOnly) {
                m_newCategoryCombo->addItem(option.text, option.key);
            }
        }
        const int index = m_newCategoryCombo->findData(keepNew);
        if (index >= 0) {
            m_newCategoryCombo->setCurrentIndex(index);
        }
    }

    // 组名换了（或者第一次来）⇒ 组头跟着换一遍。没数据时什么都不用建。
    if (!m_presets.isEmpty() || !m_broken.isEmpty()) {
        rebuildPresetList(m_currentId, false);
    }
}

void PresetManagePage::setCaptureCheckEnabled(QCheckBox *box, quint32 bit, quint32 mask,
                                              const QString &note) {
    if (box == nullptr) {
        return;
    }
    const bool allowed = (mask & bit) != 0;
    if (!allowed) {
        box->setChecked(false);
    }
    box->setEnabled(allowed && !m_busy && m_detailsAvailable);
    box->setVisible(allowed);
    QString tip = m_captureTips.value(box, box->toolTip());
    if (!allowed && !note.isEmpty()) {
        tip += QStringLiteral("\n%1").arg(note);
    }
    box->setToolTip(tip);
}

void PresetManagePage::setCaptureAvailability(quint32 mask, const QString &note) {
    m_captureMask = mask;
    m_captureNote = note;
    setCaptureCheckEnabled(m_captureOptions, CaptureVanillaOptions, mask, note);
    setCaptureCheckEnabled(m_captureKeys, CaptureVanillaKeys, mask, note);
    setCaptureCheckEnabled(m_captureServers, CaptureServers, mask, note);
    setCaptureCheckEnabled(m_captureConfig, CaptureConfig, mask, note);
    setCaptureCheckEnabled(m_captureDefaultConfigs, CaptureDefaultConfigs, mask, note);
    setCaptureCheckEnabled(m_captureResourcePacks, CaptureResourcePacks, mask, note);
    setCaptureCheckEnabled(m_captureShaderPacks, CaptureShaderPacks, mask, note);
    setCaptureCheckEnabled(m_captureMalilib, CaptureMalilib, mask, note);
    setCaptureCheckEnabled(m_captureMalilibValues, CaptureMalilibValues, mask, note);
    setCaptureCheckEnabled(m_captureDataPacks, CaptureDataPacks, mask, note);
    if (auto *toggle = findChild<QPushButton *>(QStringLiteral("presetCaptureToggle"))) {
        toggle->setVisible(mask != 0);
        if (auto *body = findChild<QWidget *>(QStringLiteral("presetCaptureBody")))
            body->setVisible(mask != 0 && toggle->isChecked());
    }
    refreshCaptureEnabled();
}

void PresetManagePage::showPreset(const QString &id, const QString &name,
                                  const QString &description, const QString &categoryText,
                                  const QList<SectionItem> &sections) {
    m_detailsAvailable = true;
    m_currentId = id;
    m_nameEdit->setText(name);
    m_descriptionEdit->setText(description);

    QStringList parts;
    if (!categoryText.isEmpty()) {
        parts.append(QStringLiteral("类别：%1").arg(categoryText));
    }
    parts.append(QStringLiteral("%1 段").arg(sections.size()));
    m_presetMeta->setText(parts.join(QStringLiteral(" / ")));

    m_sectionList->clear();
    if (sections.isEmpty()) {
        auto *item = new QListWidgetItem(tr("暂无内容"),
                                         m_sectionList);
        item->setFlags(Qt::NoItemFlags);
    } else {
        for (const SectionItem &section : sections) {
            const QString title = section.title.isEmpty() ? section.kindText : section.title;
            QString text = QStringLiteral("%1 / %2 / %3 项")
                               .arg(title, section.modeText)
                               .arg(section.itemCount);
            if (!section.summary.isEmpty()) {
                text += QStringLiteral("（%1）").arg(section.summary);
            }
            auto *item = new QListWidgetItem(text, m_sectionList);
            item->setToolTip(QStringLiteral("%1（%2）").arg(title, section.kindText));
        }
    }
    clearPlan();
    refreshActions();
}

void PresetManagePage::fillInstances(QComboBox *combo, const QList<InstanceItem> &instances,
                                     const QString &selectId) {
    const QString keep = selectId.isEmpty() ? comboInstanceId(combo) : selectId;
    const QSignalBlocker blocker(combo);
    combo->clear();
    int selectRow = -1;
    for (const InstanceItem &instance : instances) {
        QString text = instance.name;
        if (instance.running) {
            text += QStringLiteral("（正在运行）");
        }
        combo->addItem(text, instance.id);
        if (instance.id == keep) {
            selectRow = combo->count() - 1;
        }
    }
    if (selectRow < 0 && combo->count() > 0) {
        selectRow = 0;
    }
    if (selectRow >= 0) {
        combo->setCurrentIndex(selectRow);
    }
}

void PresetManagePage::setInstances(const QList<InstanceItem> &instances,
                                    const QString &selectId) {
    fillInstances(m_captureInstanceCombo, instances, selectId);
    fillInstances(m_applyInstanceCombo, instances, selectId);
    // The same instance id may now refer to another game root or run state.
    clearPlan();
    emit applyTargetChanged(selectedApplyInstanceId());
    refreshCaptureEnabled();
    refreshActions();
}

void PresetManagePage::setPlanLoading(bool loading) {
    if (m_planLoading == loading) return;
    m_planLoading = loading;
    clearPlan();
    m_previewButton->setText(loading ? tr("取消预览") : tr("预览计划"));
    m_previewButton->setToolTip(loading ? tr("取消本次预览，不会修改实例。")
        : tr("预览会改动的段落与条目，这一步不写文件。"));
    m_planHint->setText(loading ? tr("正在读取实例并生成计划…") : QString());
    refreshActions();
}

void PresetManagePage::showPlan(const PlanView &plan) {
    setPlanLoading(false);
    m_planList->clear();
    m_planCanApply = plan.canApply;
    m_planNeedsConfirm = plan.needsConfirm;
    m_planReplaceConfirmed = plan.replaceConfirmed;
    m_applyArmed = false;
    m_applyToken++;

    if (!plan.ok) {
        auto *item = new QListWidgetItem(
            plan.problem.isEmpty() ? QStringLiteral("算不出这一趟要做什么。") : plan.problem,
            m_planList);
        item->setFlags(Qt::NoItemFlags);
        m_planHint->setText(QString());
        m_applyButton->setText(QStringLiteral("应用"));
        refreshActions();
        return;
    }

    for (const PlanLine &line : plan.rows) {
        auto *item = new QListWidgetItem((line.risky ? QStringLiteral("⚠ ") : QString()) +
                                             line.text,
                                         m_planList);
        item->setFlags(Qt::NoItemFlags);
    }
    for (const QString &conflict : plan.keyConflicts) {
        auto *item = new QListWidgetItem(QStringLiteral("⚠ 键位冲突：%1").arg(conflict),
                                         m_planList);
        item->setFlags(Qt::NoItemFlags);
    }

    QString hint = plan.summary;
    if (!plan.blockedReason.isEmpty()) {
        hint = plan.blockedReason;
    } else if (plan.needsConfirm) {
        hint = QStringLiteral("有「覆盖」段：点「应用」之后还要再点一次确认。");
    } else if (!plan.canApply) {
        hint = QStringLiteral("这一趟没有能落下去的东西。");
    }
    m_planHint->setText(hint);
    m_applyButton->setText(QStringLiteral("应用"));
    refreshActions();
}

void PresetManagePage::showApplyResult(const ApplyView &result) {
    m_planList->clear();
    m_planCanApply = false;
    m_planNeedsConfirm = false;
    m_planReplaceConfirmed = false;
    m_applyArmed = false;
    m_applyToken++;

    if (result.needsConfirm) {
        // 第一趟：计划里有覆盖，应用这一层拒绝动手了。按钮回到可点状态，再点一次就确认。
        auto *item = new QListWidgetItem(QStringLiteral("有「覆盖」段，还没确认 —— "
                                                        "再点一次「应用」就按覆盖执行。"),
                                         m_planList);
        item->setFlags(Qt::NoItemFlags);
        m_planHint->setText(QStringLiteral("点「应用」确认覆盖（会先备份）。"));
        m_planCanApply = true;
        m_planNeedsConfirm = true;
        m_applyButton->setText(QStringLiteral("应用"));
        refreshActions();
        return;
    }

    if (!result.problem.isEmpty()) {
        const QString head = result.lines.isEmpty()
                                 ? result.problem
                                 : QStringLiteral("%1：%2").arg(result.problem,
                                                                result.lines.first());
        auto *item = new QListWidgetItem(head, m_planList);
        item->setFlags(Qt::NoItemFlags);
    }
    for (const QString &line : result.lines) {
        auto *item = new QListWidgetItem(line, m_planList);
        item->setFlags(Qt::NoItemFlags);
    }
    if (!result.backupDir.isEmpty()) {
        auto *item = new QListWidgetItem(QStringLiteral("备份：%1").arg(result.backupDir),
                                         m_planList);
        item->setFlags(Qt::NoItemFlags);
        item->setToolTip(QStringLiteral("还原时把这一份里的文件按原路径拷回实例目录。"));
    }
    m_planHint->setText(result.ok ? QStringLiteral("应用完成。")
                                  : QStringLiteral("应用没有全部完成，看上面哪一段没做成。"));
    m_applyButton->setText(QStringLiteral("应用"));
    refreshActions();
}

void PresetManagePage::showStatus(const QString &text) {
    m_statusLabel->setText(text);
    m_statusLabel->setVisible(!text.isEmpty());
}

void PresetManagePage::clearPlan() {
    m_planList->clear();
    m_planHint->clear();
    m_planCanApply = false;
    m_planNeedsConfirm = false;
    m_planReplaceConfirmed = false;
    m_applyArmed = false;
    m_applyToken++;
    m_applyButton->setText(QStringLiteral("应用"));
    refreshActions();
}

// ---------------------------------------------------------------------------
// 状态
// ---------------------------------------------------------------------------

void PresetManagePage::showPresetLoading(const QString &id) {
    m_detailsAvailable = false;
    m_currentId = id;
    m_nameEdit->clear();
    m_descriptionEdit->clear();
    m_sectionList->clear();
    m_presetMeta->setText(tr("正在读取预设内容…"));
    clearPlan();
    for (const auto &name : {QStringLiteral("presetEditModsButton"), QStringLiteral("presetEditOptionsButton")})
        if (auto *button = findChild<QPushButton *>(name)) button->hide();
    refreshActions();
}

void PresetManagePage::showPresetReadError(const QString &reason) {
    m_detailsAvailable = false;
    m_presetMeta->setText(tr("无法读取预设内容"));
    showStatus(tr("这一份读不出来：%1").arg(reason));
    refreshActions();
}

void PresetManagePage::setBusy(bool busy) {
    m_busy = busy;
    refreshActions();
    refreshCaptureEnabled();
}

QString PresetManagePage::currentPresetId() const {
    const auto *item = m_presetList->currentItem();
    if (item == nullptr || itemIsBroken(item) || itemIsGroup(item)) {
        return QString();
    }
    return item->data(0, kRoleId).toString();
}

QString PresetManagePage::selectedApplyInstanceId() const {
    return comboInstanceId(m_applyInstanceCombo);
}

PresetManagePage::CaptureChoice PresetManagePage::captureChoice() const {
    CaptureChoice choice;
    choice.vanillaOptions = m_captureOptions->isChecked();
    choice.vanillaKeys = m_captureKeys->isChecked();
    choice.servers = m_captureServers->isChecked();
    choice.config = m_captureConfig->isChecked();
    choice.defaultConfigs = m_captureDefaultConfigs->isChecked();
    choice.resourcePacks = m_captureResourcePacks->isChecked();
    choice.shaderPacks = m_captureShaderPacks->isChecked();
    choice.malilib = m_captureMalilib->isChecked();
    // 「连值一起收」只在「热键」也勾着的时候才有意义（不勾这一族就一段都不收）。
    choice.malilibValues = m_captureMalilib->isChecked() && m_captureMalilibValues->isChecked();
    choice.dataPacks = m_captureDataPacks->isChecked();
    return choice;
}

void PresetManagePage::refreshActions() {
    const bool valid = selectionValid();
    const bool editable = valid && !m_busy && m_detailsAvailable;
    const bool draft = currentPresetId().startsWith(QStringLiteral("draft:"));
    for (const auto &name : {QStringLiteral("presetEditModsButton"), QStringLiteral("presetEditOptionsButton")})
        if (auto *button = findChild<QPushButton *>(name)) button->setEnabled(editable);

    m_newButton->setEnabled(!m_busy);
    m_duplicateButton->setEnabled(editable && !draft);
    m_deleteButton->setEnabled(editable);
    m_defaultButton->setEnabled(editable && !draft);
    m_saveDetailsButton->setEnabled(editable);
    m_nameEdit->setEnabled(editable);
    m_descriptionEdit->setEnabled(editable);

    m_presetList->setEnabled(true);

    // 上移 / 下移：能挪的判据是"同一个组里还有前一份 / 后一份"。跨组挪没有意义 ——
    // 列表按类别分组，挪到别组去屏幕上什么都不会变。
    m_moveUpButton->setEnabled(editable && !draft && siblingPreset(-1) != nullptr);
    m_moveDownButton->setEnabled(editable && !draft && siblingPreset(1) != nullptr);

    const auto *item = m_presetList->currentItem();
    const bool isDefault = item != nullptr && item->data(0, kRoleDefault).toBool();
    m_defaultButton->setText(isDefault ? QStringLiteral("取消默认")
                                       : QStringLiteral("设为默认"));

    if (!m_deleteArmed) {
        m_deleteButton->setText(QStringLiteral("删除"));
    }

    m_previewButton->setEnabled(!draft && (m_planLoading || (editable && !comboInstanceId(m_applyInstanceCombo).isEmpty())));
    m_applyButton->setEnabled(!m_busy && m_detailsAvailable && m_planCanApply && valid);
    if (!m_applyArmed) {
        m_applyButton->setText(QStringLiteral("应用"));
    }

    refreshCaptureEnabled();
}

void PresetManagePage::refreshCaptureEnabled() {
    if (m_captureButton == nullptr) {
        return;  // 构造里第一轮 refreshActions() 可能先到（见 buildUi 末尾）
    }
    // 每一格能不能点 = 「这一类收不收它」（`m_captureMask`）×「忙不忙」。
    // 只按忙碌状态写的话，切回这一份预设之后被禁掉的格子会重新亮起来 —— 那就是
    // "不串类别"在界面上破了一个口子。
    const quint32 mask = m_captureMask;
    auto enable = [this, mask](QCheckBox *box, quint32 bit, bool extra = true) {
        if (box != nullptr) {
            box->setEnabled((mask & bit) != 0 && !m_busy && m_detailsAvailable && extra);
        }
    };
    enable(m_captureOptions, CaptureVanillaOptions);
    enable(m_captureKeys, CaptureVanillaKeys);
    enable(m_captureServers, CaptureServers);
    enable(m_captureConfig, CaptureConfig);
    enable(m_captureDefaultConfigs, CaptureDefaultConfigs);
    enable(m_captureResourcePacks, CaptureResourcePacks);
    enable(m_captureShaderPacks, CaptureShaderPacks);
    enable(m_captureMalilib, CaptureMalilib);
    enable(m_captureMalilibValues, CaptureMalilibValues, m_captureMalilib->isChecked());
    enable(m_captureDataPacks, CaptureDataPacks);
    if (auto *grid = findChild<QGridLayout *>(QStringLiteral("presetCaptureGrid"))) {
        for (int row = 0; row < grid->rowCount(); ++row) {
            bool visible = false;
            for (int col = 1; col <= 2; ++col) {
                const auto *item = grid->itemAtPosition(row, col);
                if (item && item->widget() && !item->widget()->isHidden()) visible = true;
            }
            if (auto *label = grid->itemAtPosition(row, 0); label && label->widget()) label->widget()->setVisible(visible);
        }
    }

    const bool hasPreset = !currentPresetId().isEmpty();
    const bool hasInstance = !comboInstanceId(m_captureInstanceCombo).isEmpty();
    const bool any = !captureChoice().isEmpty();
    m_captureButton->setEnabled(!m_busy && m_detailsAvailable && hasPreset && hasInstance && any);
    m_captureAsNewButton->setEnabled(!m_busy && m_detailsAvailable && hasInstance && any);
}

void PresetManagePage::armDelete() {
    m_deleteArmed = true;
    m_deleteButton->setText(QStringLiteral("再点一次确认删除"));
    const int token = ++m_deleteToken;
    QTimer::singleShot(4000, this, [this, token] {
        if (token == m_deleteToken && m_deleteArmed) {
            disarmDelete();
        }
    });
}

void PresetManagePage::disarmDelete() {
    m_deleteArmed = false;
    m_deleteToken++;
    m_deleteButton->setText(QStringLiteral("删除"));
}

void PresetManagePage::armApply() {
    m_applyArmed = true;
    m_applyButton->setText(QStringLiteral("再点一次确认覆盖"));
    const int token = ++m_applyToken;
    QTimer::singleShot(4000, this, [this, token] {
        if (token == m_applyToken && m_applyArmed) {
            disarmApply();
        }
    });
}

void PresetManagePage::disarmApply() {
    m_applyArmed = false;
    m_applyToken++;
    m_applyButton->setText(QStringLiteral("应用"));
}

// ---------------------------------------------------------------------------
// 列表
// ---------------------------------------------------------------------------

void PresetManagePage::onCurrentPresetChanged(QTreeWidgetItem *current,
                                              QTreeWidgetItem *previous) {
    Q_UNUSED(previous);
    if (m_fillingList) {
        return;
    }
    refreshActions();
    // 组头不可选中（见 buildUi），所以走到这儿的一定是组里那一行；真为空 / 读不回来 /
    // 组头时按"什么都没选"处理 —— 右边那一栏跟着清空，不给拿组头当代替品的机会。
    if (current == nullptr || itemIsBroken(current) || itemIsGroup(current)) {
        m_currentId.clear();
        m_nameEdit->clear();
        m_descriptionEdit->clear();
        m_sectionList->clear();
        m_presetMeta->setText(tr("未选择预设"));
        clearPlan();
        refreshActions();
        emit presetSelected(QString());
        return;
    }
    const QString id = current->data(0, kRoleId).toString();
    m_currentId = id;
    clearPlan();
    emit presetSelected(id);
}

bool PresetManagePage::selectionValid() const {
    const auto *item = m_presetList->currentItem();
    return item != nullptr && !itemIsBroken(item) && !itemIsGroup(item);
}

QTreeWidgetItem *PresetManagePage::siblingPreset(int delta) const {
    const auto *item = m_presetList->currentItem();
    if (item == nullptr || item->parent() == nullptr) return nullptr;
    auto *parent = item->parent();
    for (int i = parent->indexOfChild(const_cast<QTreeWidgetItem *>(item)) + delta;
         i >= 0 && i < parent->childCount(); i += delta) {
        auto *candidate = parent->child(i);
        if (!candidate->isHidden() && !itemIsBroken(candidate)) return candidate;
    }
    return nullptr;
}

QString PresetManagePage::comboInstanceId(const QComboBox *combo) {
    if (combo == nullptr || combo->currentIndex() < 0) {
        return QString();
    }
    return combo->currentData().toString();
}
