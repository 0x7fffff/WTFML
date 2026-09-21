/**
 * @file InstanceSettingsDialog.cpp
 * @brief 搭出「实例设置…」对话框的控件，并把它们读成一份覆盖值。
 * @author 0x7fffff
 * @date 2026-09-21
 * @note 这里**不落盘、不碰 src/**：值从控件读，落盘在 MainWindow 那边。
 *
 * 这一层唯一的"业务判断"是内存那两行在自动档下的表现（禁用 + 点确定时不写覆盖），
 * 理由写在 .hpp 的 override() 上。
 */
#include "InstanceSettingsDialog.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QFileDialog>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include "MainWindowUI.hpp"

namespace {

/// 数字框的宽度。和设置页那两个用同一个值 —— 同一个"内存 MB 数"在两处宽度不一样
/// 会显得像是两种东西。
constexpr int kMemorySpinWidth = 120;
/// 追加参数那一行的输入高度（三四行够放几条常见参数）。
constexpr int kAppendJvmArgsHeight = 76;

/// 把一行左边那一格（项目名称）加进网格：列 0 是名字、列 1 是控件，和设置页同一套排法。
void addRowTitle(QGridLayout *grid, int row, const QString &title, QWidget *parent) {
    auto *label = new QLabel(title, parent);
    // 顶部对齐：右边那一格有两行（单选钮那一行 + 提示），名字应该跟第一行齐。
    grid->addWidget(label, row, 0, Qt::AlignLeft | Qt::AlignTop);
}

}  // namespace

InstanceSettingsDialog::InstanceSettingsDialog(const QString &instanceName,
                                               const Override &current,
                                               const GlobalValues &global,
                                               const QStringList &appendJvmArgs,
                                               bool warnJavaMismatch, QWidget *parent)
    : QDialog(parent), m_global(global) {
    setWindowTitle(tr("实例设置"));
    // 比分组对话框宽一些：有几行是"单选钮 + 控件"并排。
    resize(600, 480);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 12, 12, 12);
    outer->setSpacing(10);

    auto *title = new QLabel(tr("实例设置：%1").arg(instanceName), this);
    title->setObjectName(titleLabelName());
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    outer->addWidget(title);

    auto *explain = new QLabel(
        tr("每一项都可以「跟随全局」或「自定义」。「跟随全局」的项目在你改全局设置时跟着变，"
           "「自定义」的项目只对这个实例生效。这些都写在这个实例目录下的 wtfml.meta 里。"),
        this);
    explain->setWordWrap(true);
    outer->addWidget(explain);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(10);
    grid->setColumnStretch(0, MainWindowUI::leftColumnStretch());
    grid->setColumnStretch(1, MainWindowUI::rightColumnStretch());
    outer->addLayout(grid);

    int row = 0;

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
            m_advancedBox->setHidden(!expanded);
            m_advancedToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
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
