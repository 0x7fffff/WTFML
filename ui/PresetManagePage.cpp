/**
 * @file PresetManagePage.cpp
 * @brief 实现 PresetManagePage.hpp。
 * @author 0x7fffff
 * @date 2026-10-18
 *
 * 布局从上到下：左栏（预设列表 + 增删改）→ 右栏（这一份是什么 / 从实例提取 / 应用到实例）。
 * 右栏放在滚动区里：窗口矮的时候下边那两段还能滚到，不会被布局压成一条。
 */

#include "PresetManagePage.hpp"
#include "Theme.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
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

/// 小一号 + PlaceholderText 那档墨色。和 JavaManageDialog / MainWindowUI 里那几个
/// 匿名 namespace 里的同名帮手是同一套形状（跨不过来，只能各留一份）。
void applyHintStyle(QLabel *hint) {
    hint->setWordWrap(true);
    QFont hintFont = hint->font();
    hintFont.setPointSize(qMax(1, hintFont.pointSize() - 1));
    hint->setFont(hintFont);
    QPalette hintPalette = hint->palette();
    hintPalette.setColor(QPalette::WindowText, hintPalette.color(QPalette::PlaceholderText));
    hint->setPalette(hintPalette);
}

/// 区块标题（「这一份是什么」「从实例提取」……）。粗一号，不带框 —— `QGroupBox` 那一圈
/// 边在 2026-10-12 的视觉收口里已经全仓退场了（见 MainWindowUI 那段"最后一个 QGroupBox"）。
QLabel *makeSectionTitle(const QString &text, QWidget *parent) {
    auto *label = new QLabel(text, parent);
    QFont font = label->font();
    font.setBold(true);
    font.setPointSize(font.pointSize() + 1);
    label->setFont(font);
    return label;
}

bool itemIsBroken(const QListWidgetItem *item) {
    return item != nullptr && item->data(Qt::UserRole + 1).toBool();
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
    left->setFixedWidth(kLeftColumnWidth);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);
    leftLayout->addWidget(makeSectionTitle(QStringLiteral("预设"), left));

    m_presetList = new QListWidget(left);
    m_presetList->setObjectName(presetListName());
    m_presetList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_presetList->setAlternatingRowColors(false);
    leftLayout->addWidget(m_presetList, 1);
    connect(m_presetList, &QListWidget::currentItemChanged, this,
            &PresetManagePage::onCurrentPresetChanged);

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
    m_duplicateButton->setToolTip(QStringLiteral("新的一份，段和 files/ 里的文件原样搬过去。"));
    m_defaultButton = new QPushButton(QStringLiteral("设为默认"), left);
    m_defaultButton->setObjectName(defaultButtonName());
    m_defaultButton->setAutoDefault(false);
    m_defaultButton->setToolTip(
        QStringLiteral("安装实例时可以默认选它。同一时间只有一份是默认项。"));
    m_deleteButton = new QPushButton(QStringLiteral("删除"), left);
    m_deleteButton->setObjectName(deleteButtonName());
    m_deleteButton->setAutoDefault(false);
    Theme::markButton(m_deleteButton, Theme::ButtonKind::Danger);
    m_deleteButton->setToolTip(QStringLiteral("连这一份的目录一起删掉，删了找不回来。"));
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

    connect(m_newButton, &QPushButton::clicked, this, &PresetManagePage::createRequested);
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
        QListWidgetItem *item = m_presetList->currentItem();
        const bool nowDefault = item != nullptr && item->data(Qt::UserRole + 2).toBool();
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

    columns->addWidget(left);

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
    rightLayout->addWidget(makeSectionTitle(QStringLiteral("这一份预设"), right));

    auto *nameRow = new QHBoxLayout;
    nameRow->setContentsMargins(0, 0, 0, 0);
    nameRow->setSpacing(8);
    nameRow->addWidget(new QLabel(QStringLiteral("名字"), right));
    m_nameEdit = new QLineEdit(right);
    m_nameEdit->setObjectName(nameEditName());
    m_nameEdit->setPlaceholderText(QStringLiteral("这一份叫什么"));
    nameRow->addWidget(m_nameEdit, 1);
    rightLayout->addLayout(nameRow);

    auto *descriptionRow = new QHBoxLayout;
    descriptionRow->setContentsMargins(0, 0, 0, 0);
    descriptionRow->setSpacing(8);
    descriptionRow->addWidget(new QLabel(QStringLiteral("说明"), right));
    m_descriptionEdit = new QLineEdit(right);
    m_descriptionEdit->setObjectName(descriptionEditName());
    m_descriptionEdit->setPlaceholderText(QStringLiteral("一句话说明（可空）"));
    descriptionRow->addWidget(m_descriptionEdit, 1);
    m_saveDetailsButton = new QPushButton(QStringLiteral("保存名称和说明"), right);
    m_saveDetailsButton->setObjectName(saveDetailsButtonName());
    m_saveDetailsButton->setAutoDefault(false);
    Theme::markButton(m_saveDetailsButton, Theme::ButtonKind::Neutral);
    descriptionRow->addWidget(m_saveDetailsButton);
    rightLayout->addLayout(descriptionRow);
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

    // ---------- 从实例提取 ----------
    rightLayout->addWidget(makeSectionTitle(QStringLiteral("从实例提取"), right));

    auto *captureGrid = new QGridLayout;
    captureGrid->setContentsMargins(0, 0, 0, 0);
    captureGrid->setHorizontalSpacing(12);
    captureGrid->setVerticalSpacing(6);
    auto addCheck = [this, captureGrid, right](int row, int column, const QString &text,
                                               const QString &tooltip, bool checked,
                                               QCheckBox **out) {
        auto *box = new QCheckBox(text, right);
        box->setChecked(checked);
        box->setToolTip(tooltip);
        captureGrid->addWidget(box, row, column);
        *out = box;
        connect(box, &QCheckBox::toggled, this, &PresetManagePage::refreshCaptureEnabled);
        return box;
    };
    captureGrid->addWidget(new QLabel(QStringLiteral("游戏设置"), right), 0, 0);
    addCheck(0, 1, QStringLiteral("基础设置"),
             QStringLiteral("FOV、GUI 缩放、语言、音量、渲染距离……显示器/全屏分辨率那一类默认不收。"),
             true, &m_captureOptions);
    addCheck(0, 2, QStringLiteral("标准按键"),
             QStringLiteral("options.txt 里的 key_*。"),
             true, &m_captureKeys);
    captureGrid->addWidget(new QLabel(QStringLiteral("服务器"), right), 1, 0);
    addCheck(1, 1, QStringLiteral("服务器列表"),
             QStringLiteral("servers.dat；落到别的实例上是「追加」，已经在列表里的地址不会重复。"),
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
             QStringLiteral("把开关和别的值也收进来（§6 P4 的「可选合并全部已知设置」）。"),
             false, &m_captureMalilibValues);
    captureGrid->setColumnStretch(3, 1);
    rightLayout->addLayout(captureGrid);

    auto *captureRow = new QHBoxLayout;
    captureRow->setContentsMargins(0, 0, 0, 0);
    captureRow->setSpacing(8);
    captureRow->addWidget(new QLabel(QStringLiteral("从这个实例"), right));
    m_captureInstanceCombo = new QComboBox(right);
    m_captureInstanceCombo->setObjectName(captureInstanceComboName());
    m_captureInstanceCombo->setMinimumWidth(220);
    captureRow->addWidget(m_captureInstanceCombo, 1);
    rightLayout->addLayout(captureRow);

    auto *captureButtons = new QHBoxLayout;
    captureButtons->setContentsMargins(0, 0, 0, 0);
    captureButtons->setSpacing(8);
    m_captureButton = new QPushButton(QStringLiteral("收进这一份预设"), right);
    m_captureButton->setObjectName(captureButtonName());
    m_captureButton->setAutoDefault(false);
    m_captureButton->setToolTip(
        QStringLiteral("收进来的那几段替换掉这一份里同名的那几段；这一份里别的段原样留着。"));
    Theme::markButton(m_captureButton, Theme::ButtonKind::Accent);
    captureButtons->addWidget(m_captureButton);
    m_newPresetNameEdit = new QLineEdit(right);
    m_newPresetNameEdit->setObjectName(newPresetNameEditName());
    m_newPresetNameEdit->setPlaceholderText(QStringLiteral("新预设的名字（可空）"));
    m_newPresetNameEdit->setMaximumWidth(220);
    captureButtons->addWidget(m_newPresetNameEdit);
    m_captureAsNewButton = new QPushButton(QStringLiteral("提取为新预设"), right);
    m_captureAsNewButton->setObjectName(captureAsNewButtonName());
    m_captureAsNewButton->setAutoDefault(false);
    Theme::markButton(m_captureAsNewButton, Theme::ButtonKind::Neutral);
    captureButtons->addWidget(m_captureAsNewButton);
    captureButtons->addStretch(1);
    rightLayout->addLayout(captureButtons);

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
                                   captureChoice());
    });
    connect(m_captureInstanceCombo, &QComboBox::currentIndexChanged, this,
            &PresetManagePage::refreshCaptureEnabled);

    // ---------- 应用到实例 ----------
    rightLayout->addWidget(makeSectionTitle(QStringLiteral("应用到实例"), right));

    auto *applyRow = new QHBoxLayout;
    applyRow->setContentsMargins(0, 0, 0, 0);
    applyRow->setSpacing(8);
    applyRow->addWidget(new QLabel(QStringLiteral("落到这个实例"), right));
    m_applyInstanceCombo = new QComboBox(right);
    m_applyInstanceCombo->setObjectName(applyInstanceComboName());
    m_applyInstanceCombo->setMinimumWidth(220);
    applyRow->addWidget(m_applyInstanceCombo, 1);
    m_previewButton = new QPushButton(QStringLiteral("预览计划"), right);
    m_previewButton->setObjectName(previewButtonName());
    m_previewButton->setAutoDefault(false);
    m_previewButton->setToolTip(
        QStringLiteral("先说清楚会动哪几段、写几项、删几项、有哪些冲突；这一步一个字节都不写。"));
    Theme::markButton(m_previewButton, Theme::ButtonKind::Neutral);
    applyRow->addWidget(m_previewButton);
    rightLayout->addLayout(applyRow);
    connect(m_previewButton, &QPushButton::clicked, this, [this] {
        const QString presetId = currentPresetId();
        const QString instanceId = comboInstanceId(m_applyInstanceCombo);
        if (presetId.isEmpty() || instanceId.isEmpty()) return;
        clearPlan();
        emit planRequested(presetId, instanceId);
    });
    // 换实例 / 换预设都会让上一份计划作废（计划是"对着那个实例那一刻"算出来的）。
    connect(m_applyInstanceCombo, &QComboBox::currentIndexChanged, this, [this] {
        clearPlan();
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

    m_statusLabel = new QLabel(right);
    m_statusLabel->setObjectName(statusLabelName());
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rightLayout->addWidget(m_statusLabel);

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
    const QString keep = selectId.isEmpty() ? m_currentId : selectId;

    m_fillingList = true;
    m_presetList->clear();
    int selectRow = -1;
    for (const PresetItem &preset : presets) {
        auto *item = new QListWidgetItem(m_presetList);
        QString text = preset.name;
        if (preset.isDefault) {
            text += QStringLiteral("（默认）");
        }
        item->setText(text);
        item->setData(Qt::UserRole, preset.id);
        item->setData(Qt::UserRole + 1, false);
        item->setData(Qt::UserRole + 2, preset.isDefault);
        QString tip = preset.description;
        if (!preset.updatedText.isEmpty()) {
            if (!tip.isEmpty()) tip += QLatin1Char('\n');
            tip += preset.updatedText;
        }
        tip += QStringLiteral("\n%1 段").arg(preset.sectionCount);
        item->setToolTip(tip);
        if (preset.id == keep) {
            selectRow = m_presetList->count() - 1;
        }
    }
    for (const BrokenItem &one : broken) {
        // ★ 读不回来的那些**照样列出来**，但不可选：用户至少知道"它还在，只是读不动"。
        auto *item = new QListWidgetItem(m_presetList);
        QString name = one.id;
        if (name.size() > 12) name = name.left(12) + QStringLiteral("…");
        item->setText(QStringLiteral("⚠ 读不出来：%1").arg(name));
        item->setData(Qt::UserRole, one.id);
        item->setData(Qt::UserRole + 1, true);
        item->setFlags(Qt::NoItemFlags);
        item->setToolTip(one.reason);
    }
    if (selectRow < 0 && m_presetList->count() > 0 && !presets.isEmpty()) {
        selectRow = 0;
    }
    if (selectRow >= 0) {
        m_presetList->setCurrentRow(selectRow);
    }
    m_fillingList = false;

    if (m_presetList->count() == 0) {
        m_currentId.clear();
        m_nameEdit->clear();
        m_descriptionEdit->clear();
        m_presetMeta->clear();
        m_sectionList->clear();
    }
    refreshActions();
}

void PresetManagePage::showPreset(const QString &id, const QString &name,
                                  const QString &description,
                                  const QList<SectionItem> &sections) {
    m_currentId = id;
    m_nameEdit->setText(name);
    m_descriptionEdit->setText(description);

    QStringList parts;
    parts.append(QStringLiteral("%1 段").arg(sections.size()));
    m_presetMeta->setText(parts.join(QStringLiteral(" · ")));

    m_sectionList->clear();
    if (sections.isEmpty()) {
        auto *item = new QListWidgetItem(QStringLiteral("这一份还是空的 —— 用它下面的"
                                                        "「从实例提取」收一份进来。"),
                                         m_sectionList);
        item->setFlags(Qt::NoItemFlags);
    } else {
        for (const SectionItem &section : sections) {
            const QString title = section.title.isEmpty() ? section.kindText : section.title;
            QString text = QStringLiteral("%1 · %2 · %3 项")
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
    refreshCaptureEnabled();
    refreshActions();
}

void PresetManagePage::showPlan(const PlanView &plan) {
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
        // 一句人话打头；报告里已经有一行明细的话就并到同一行，别让人对着两行猜。
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
        item->setToolTip(QStringLiteral("要还原的话，把这一棵里的文件照原路径拷回实例目录。"));
    }
    m_planHint->setText(result.ok ? QStringLiteral("应用完成。")
                                  : QStringLiteral("应用没有全部完成，看上面哪一段没做成。"));
    m_applyButton->setText(QStringLiteral("应用"));
    refreshActions();
}

void PresetManagePage::showStatus(const QString &text) {
    m_statusLabel->setText(text);
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

void PresetManagePage::setBusy(bool busy) {
    m_busy = busy;
    refreshActions();
    refreshCaptureEnabled();
}

QString PresetManagePage::currentPresetId() const {
    const QListWidgetItem *item = m_presetList->currentItem();
    if (item == nullptr || itemIsBroken(item)) {
        return QString();
    }
    return item->data(Qt::UserRole).toString();
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
    return choice;
}

void PresetManagePage::refreshActions() {
    const int row = currentPresetRow();
    const bool valid = row >= 0 && !rowIsBroken(row);
    const bool editable = valid && !m_busy;

    m_newButton->setEnabled(!m_busy);
    m_duplicateButton->setEnabled(editable);
    m_deleteButton->setEnabled(editable);
    m_defaultButton->setEnabled(editable);
    m_saveDetailsButton->setEnabled(editable);
    m_nameEdit->setEnabled(editable);
    m_descriptionEdit->setEnabled(editable);

    m_presetList->setEnabled(true);

    // 上移 / 下移：列表里只有**读得回来的**那些能挪（坏的那几份不在下标里）。
    int firstValid = -1;
    int lastValid = -1;
    for (int i = 0; i < m_presetList->count(); ++i) {
        if (!rowIsBroken(i)) {
            if (firstValid < 0) firstValid = i;
            lastValid = i;
        }
    }
    m_moveUpButton->setEnabled(editable && row > firstValid);
    m_moveDownButton->setEnabled(editable && row >= 0 && row < lastValid);

    QListWidgetItem *item = m_presetList->currentItem();
    const bool isDefault = item != nullptr && item->data(Qt::UserRole + 2).toBool();
    m_defaultButton->setText(isDefault ? QStringLiteral("取消默认")
                                       : QStringLiteral("设为默认"));

    if (!m_deleteArmed) {
        m_deleteButton->setText(QStringLiteral("删除"));
    }

    m_previewButton->setEnabled(editable && !comboInstanceId(m_applyInstanceCombo).isEmpty());
    m_applyButton->setEnabled(!m_busy && m_planCanApply && valid);
    if (!m_applyArmed) {
        m_applyButton->setText(QStringLiteral("应用"));
    }

    refreshCaptureEnabled();
}

void PresetManagePage::refreshCaptureEnabled() {
    if (m_captureButton == nullptr) {
        return;  // 构造里第一轮 refreshActions() 可能先到（见 buildUi 末尾）
    }
    const bool hasPreset = !currentPresetId().isEmpty();
    const bool hasInstance = !comboInstanceId(m_captureInstanceCombo).isEmpty();
    const bool any = !captureChoice().isEmpty();
    m_captureButton->setEnabled(!m_busy && hasPreset && hasInstance && any);
    m_captureAsNewButton->setEnabled(!m_busy && hasInstance && any);
    m_captureMalilibValues->setEnabled(!m_busy && m_captureMalilib->isChecked());
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

void PresetManagePage::onCurrentPresetChanged(QListWidgetItem *current,
                                              QListWidgetItem *previous) {
    Q_UNUSED(previous);
    if (m_fillingList) {
        return;
    }
    refreshActions();
    if (current == nullptr || itemIsBroken(current)) {
        m_currentId.clear();
        return;
    }
    const QString id = current->data(Qt::UserRole).toString();
    m_currentId = id;
    clearPlan();
    emit presetSelected(id);
}

int PresetManagePage::currentPresetRow() const {
    return m_presetList->currentRow();
}

QString PresetManagePage::presetIdAt(int row) const {
    const QListWidgetItem *item = m_presetList->item(row);
    return item == nullptr ? QString() : item->data(Qt::UserRole).toString();
}

bool PresetManagePage::rowIsBroken(int row) const {
    return itemIsBroken(m_presetList->item(row));
}

QString PresetManagePage::comboInstanceId(const QComboBox *combo) {
    if (combo == nullptr || combo->currentIndex() < 0) {
        return QString();
    }
    return combo->currentData().toString();
}
