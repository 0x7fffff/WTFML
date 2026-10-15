/**
 * @file ToolboxPage.cpp
 * @brief 工具箱那一页的实现（形状与"为什么五件挤一页"见头文件）。
 * @author 0x7fffff
 * @date 2026-10-16
 */

#include "ToolboxPage.hpp"

#include <QClipboard>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFont>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "MainWindowUI.hpp"
#include "Theme.hpp"

namespace {

/// 五件工具的那张表（**键 / 中文名 / 一句话说明的唯一出处**）。
struct ToolInfo {
    ToolboxPage::Tool tool;
    const char *key;
    const char *name;
    const char *summary;
};

const ToolInfo kToolInfos[] = {
    {ToolboxPage::Tool::Anvil, "anvil", "附魔顺序助手",
     "照 Java 版铁砧那套惩罚机制，算一条最省等级的顺序。"},
    {ToolboxPage::Tool::Text, "text", "文本生成器",
     "带 § 码的文本、JSON 组件、tellraw 命令 —— 三种写法互相转。"},
    {ToolboxPage::Tool::Coord, "coord", "坐标换算",
     "主世界与下界互转、区块与 region 文件；粘一行 F3+C 的 tp 命令也能认。"},
    {ToolboxPage::Tool::Server, "server", "服务器在线检测",
     "连上去问一句，看 MOTD、版本、在线人数和延迟。"},
    {ToolboxPage::Tool::Hash, "hash", "文件哈希校验",
     "算一个文件的 SHA-256 / SHA-1 / MD5，并和手上那串已知值对一下。"},
};

const ToolInfo *infoOf(ToolboxPage::Tool tool) {
    for (const ToolInfo &info : kToolInfos) {
        if (info.tool == tool) {
            return &info;
        }
    }
    return &kToolInfos[0];
}

/// 一行小字（次要信息）。挂 objectName 是为了验证工程找得到它。
QLabel *makeMutedLabel(QWidget *parent, const QString &objectName = QString()) {
    auto *label = new QLabel(parent);
    if (!objectName.isEmpty()) {
        label->setObjectName(objectName);
    }
    label->setProperty(Theme::textRolePropertyName().toUtf8().constData(),
                       Theme::secondaryTextRoleValue());
    label->setWordWrap(true);
    return label;
}

/// 隔开两块内容的一条空档（和别的页同一种做法：一条没有形状的 8px）。
QFrame *makeSeparator(QWidget *parent) {
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::NoFrame);
    line->setFixedHeight(8);
    return line;
}

/// 小节标题（`预览`、`输出` 那种）。
QLabel *makeSectionTitle(QWidget *parent, const QString &text) {
    auto *label = new QLabel(text, parent);
    label->setProperty(Theme::textRolePropertyName().toUtf8().constData(),
                       Theme::sectionTitleTextRoleValue());
    QFont font = label->font();
    font.setBold(true);
    font.setPointSize(font.pointSize() + 1);
    label->setFont(font);
    return label;
}

/// 一张**小节卡片**（`wtfmlSurface="section"` 那一档面：浅一档的底 + 圆角、不画边）。
///
/// ★ 这个面**只有 `QFrame` 认得**（样式表那条选择器是 `QFrame[wtfmlSurface="section"]`）——
///   挂在一个 `QLabel` / `QPlainTextEdit` 上等于什么都没发生。所以"要有卡片底"的地方一律
///   先起一张 `QFrame`、把东西装进去，别指望给内容控件挂个属性就长出来。
QFrame *makeSectionCard(QWidget *parent) {
    auto *card = new QFrame(parent);
    card->setProperty(Theme::surfacePropertyName().toUtf8().constData(),
                      Theme::sectionSurfaceValue());
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(6);
    return card;
}

/// 一块**只读**的输出框（结果、生成出来的命令、摘要）。
///
/// 用等宽字体：这里面放的是十六进制摘要、JSON、`/tellraw` 命令 —— 都不是自然语言，等宽才
/// 看得清"哪一位对哪一位"（用户拿着两串摘要对眼睛扫的时候，比例字体会让他数错）。
QPlainTextEdit *makeOutputBox(QFrame *card, const QString &objectName = QString()) {
    auto *box = new QPlainTextEdit(card);
    if (!objectName.isEmpty()) {
        box->setObjectName(objectName);
    }
    box->setReadOnly(true);
    box->setFrameShape(QFrame::NoFrame);
    box->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    box->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    // 高度按两行钉一档（**不设 maximumHeight**：窗口拉高时这些框跟着长，读长命令舒服）。
    box->setMinimumHeight(box->fontMetrics().lineSpacing() * 2 + 12);
    // 卡片自己已经是那个面了 —— 框里不要再铺一层底（两层底会变成一块深色的疙瘩）。
    box->setStyleSheet(QStringLiteral("QPlainTextEdit { background: transparent; }"));
    return box;
}

/// 每一片顶上那两行：大字标题 + 一句话说明（五片共用，免得五份各写一套字号）。
/// @param extra 这一片自己还要多说的那一句（可为空）。
QLabel *addPanelHeader(QVBoxLayout *layout, QWidget *parent, ToolboxPage::Tool tool,
                       const QString &extra = QString()) {
    auto *title = new QLabel(ToolboxPage::toolName(tool), parent);
    QFont font = title->font();
    font.setBold(true);
    font.setPointSize(font.pointSize() + 3);
    title->setFont(font);
    layout->addWidget(title);
    auto *hint = makeMutedLabel(parent);
    QString text = ToolboxPage::toolSummary(tool);
    if (!extra.isEmpty()) {
        text.append(QChar(' '));
        text.append(extra);
    }
    hint->setText(text);
    layout->addWidget(hint);
    return hint;
}

/// 「复制」那一颗（它拷的是**紧挨着的那个框**里的字，不是"当前选中的那一页"）。
///
/// 页面上不做别的动作：剪贴板是这一页自己就能碰的东西（`ui/` 用 QtGui 是许可的，
/// 见全仓那几处），不像"算哈希"那种要交给 controller 的活。
QPushButton *makeCopyButton(QWidget *parent, const QString &objectName = QString()) {
    auto *button = new QPushButton(QStringLiteral("复制"), parent);
    if (!objectName.isEmpty()) {
        button->setObjectName(objectName);
    }
    button->setToolTip(QStringLiteral("把这一格里的内容复制到剪贴板"));
    Theme::markButton(button, Theme::ButtonKind::Ghost);
    return button;
}

void copyToClipboard(const QString &text) {
    if (QClipboard *clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(text);
    }
}

/// 表单里的一个小标题那样的行内标签（`附魔` / `等级`）。
QLabel *makeFieldLabel(QWidget *parent, const QString &text) {
    auto *label = new QLabel(text, parent);
    label->setWordWrap(false);
    return label;
}

}  // namespace

// ---------------------------------------------------------------------------
// 五件工具那张表（键 / 名字 / 说明）
// ---------------------------------------------------------------------------

QString ToolboxPage::toolKey(Tool tool) {
    return QString::fromLatin1(infoOf(tool)->key);
}

ToolboxPage::Tool ToolboxPage::toolFromKey(const QString &key, bool *ok) {
    for (const ToolInfo &info : kToolInfos) {
        if (key == QLatin1String(info.key)) {
            if (ok != nullptr) {
                *ok = true;
            }
            return info.tool;
        }
    }
    if (ok != nullptr) {
        *ok = false;
    }
    return Tool::Anvil;
}

QString ToolboxPage::toolName(Tool tool) {
    return QString::fromUtf8(infoOf(tool)->name);
}

QString ToolboxPage::toolSummary(Tool tool) {
    return QString::fromUtf8(infoOf(tool)->summary);
}

QList<ToolboxPage::Tool> ToolboxPage::tools() {
    QList<Tool> list;
    for (const ToolInfo &info : kToolInfos) {
        list.append(info.tool);
    }
    return list;
}

QString ToolboxPage::toolChipName(Tool tool) {
    return QStringLiteral("toolboxChip_%1").arg(toolKey(tool));
}

QString ToolboxPage::toolPanelName(Tool tool) {
    return QStringLiteral("toolboxPanel_%1").arg(toolKey(tool));
}

QString ToolboxPage::toolButtonName(Tool tool) {
    return QStringLiteral("toolboxTool_%1").arg(toolKey(tool));
}

// ---------------------------------------------------------------------------
// 搭页
// ---------------------------------------------------------------------------

ToolboxPage::ToolboxPage(QWidget *parent) : QDialog(parent) {
    setObjectName(QStringLiteral("toolboxPage"));
    setWindowTitle(windowTitle());
    // 只影响"单独拿出来用"（验证工程那几处）：嵌进覆盖层时这一页拿的是内容区的整幅宽。
    resize(760, 560);
    setAcceptDrops(true);  // 哈希那一件要能"拖一个文件进来"

    m_tools = tools();

    auto *box = new QVBoxLayout(this);
    box->setContentsMargins(12, 12, 12, 12);
    box->setSpacing(10);

    box->addWidget(buildChipRow());
    box->addWidget(makeSeparator(this));

    m_stack = new QStackedWidget(this);
    // **加进去的次序 = m_tools 的次序**（`showTool()` 按下标切，两边不许各写一遍次序）。
    for (Tool tool : m_tools) {
        QWidget *panel = nullptr;
        switch (tool) {
        case Tool::Anvil:
            panel = buildAnvilPanel();
            break;
        case Tool::Text:
            panel = buildTextPanel();
            break;
        case Tool::Coord:
            panel = buildCoordPanel();
            break;
        case Tool::Server:
            panel = buildServerPanel();
            break;
        case Tool::Hash:
            panel = buildHashPanel();
            break;
        }
        panel->setObjectName(toolPanelName(tool));
        m_stack->addWidget(panel);
    }
    box->addWidget(m_stack, 1);

    showTool(Tool::Anvil);
}

QWidget *ToolboxPage::buildChipRow() {
    auto *row = new QWidget(this);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    for (Tool tool : m_tools) {
        auto *chip = new QPushButton(toolName(tool), row);
        chip->setObjectName(toolChipName(tool));
        chip->setCheckable(true);  // ★ chip 的样式只描述"勾上什么样"，能不能勾是调用方的事
        chip->setToolTip(toolSummary(tool));
        Theme::markChipButton(chip);
        connect(chip, &QPushButton::clicked, this, [this, tool]() { showTool(tool); });
        layout->addWidget(chip);
        m_chips.append(chip);
    }
    layout->addStretch(1);
    return row;
}

void ToolboxPage::syncChips() {
    for (int index = 0; index < m_chips.size() && index < m_tools.size(); ++index) {
        // 只认"当前那一颗"（和下载页那几颗分类 chip 同一套判据）。
        const bool current = m_tools.at(index) == m_current;
        m_chips.at(index)->setChecked(current);
    }
}

void ToolboxPage::showTool(Tool tool) {
    int index = 0;
    for (int i = 0; i < m_tools.size(); ++i) {
        if (m_tools.at(i) == tool) {
            index = i;
        }
    }
    m_current = m_tools.at(index);
    m_stack->setCurrentIndex(index);
    syncChips();
}

// ---------------------------------------------------------------------------
// 一、附魔顺序助手
// ---------------------------------------------------------------------------

QWidget *ToolboxPage::buildAnvilPanel() {
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    addPanelHeader(layout, panel, Tool::Anvil,
                   QStringLiteral("把要合的几件东西列出来（默认按附魔书算）。等级上限 39 —— "
                                  "哪一步的代价超了就整条做不出来，会单独提示。"));

    m_anvilTable = new QTableWidget(0, 4, panel);
    m_anvilTable->setObjectName(QStringLiteral("anvilTable"));
    m_anvilTable->setHorizontalHeaderLabels(
        {QStringLiteral("附魔"), QStringLiteral("等级"), QStringLiteral("这一件"),
         QStringLiteral("在铁砧上用过几次")});
    m_anvilTable->verticalHeader()->setVisible(false);
    m_anvilTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_anvilTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_anvilTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_anvilTable->setMinimumHeight(160);
    layout->addWidget(m_anvilTable, 1);

    auto *rowButtons = new QHBoxLayout;
    rowButtons->setContentsMargins(0, 0, 0, 0);
    rowButtons->setSpacing(8);
    auto *addRow = new QPushButton(QStringLiteral("加一行"), panel);
    addRow->setObjectName(QStringLiteral("anvilAddRow"));
    Theme::markButton(addRow, Theme::ButtonKind::Neutral);
    auto *removeRow = new QPushButton(QStringLiteral("删掉这一行"), panel);
    removeRow->setObjectName(QStringLiteral("anvilRemoveRow"));
    Theme::markButton(removeRow, Theme::ButtonKind::Neutral);
    auto *plan = new QPushButton(QStringLiteral("算一下"), panel);
    plan->setObjectName(QStringLiteral("anvilPlan"));
    Theme::markButton(plan, Theme::ButtonKind::Accent);
    rowButtons->addWidget(addRow);
    rowButtons->addWidget(removeRow);
    rowButtons->addStretch(1);
    rowButtons->addWidget(plan);
    layout->addLayout(rowButtons);

    layout->addWidget(makeSeparator(panel));

    m_anvilError = makeMutedLabel(panel, QStringLiteral("anvilError"));
    layout->addWidget(m_anvilError);

    m_anvilResult = new QLabel(panel);
    m_anvilResult->setObjectName(QStringLiteral("anvilResult"));
    m_anvilResult->setWordWrap(true);
    m_anvilResult->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_anvilResult->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    layout->addWidget(m_anvilResult, 1);

    connect(addRow, &QPushButton::clicked, this, [this]() { appendAnvilRow(); });
    connect(removeRow, &QPushButton::clicked, this, [this]() {
        const int row = m_anvilTable->currentRow();
        if (row >= 0) {
            m_anvilTable->removeRow(row);
        }
    });
    connect(plan, &QPushButton::clicked, this, [this]() { emit anvilPlanRequested(); });

    // 一进来就摆一行 —— 空表格看着像没搭好。
    appendAnvilRow();
    return panel;
}

void ToolboxPage::appendAnvilRow() {
    if (m_anvilTable == nullptr) {
        return;
    }
    const int row = m_anvilTable->rowCount();
    m_anvilTable->insertRow(row);
    // ★ 四格**一眼就能看出是输入控件**：附魔和"这一件"是下拉框、等级和次数是数字框。
    //   用 `setCellWidget` 而不是可编辑的 item：下拉框和数字框的合法值域由控件自己管着，
    //   让用户敲一串文本再回来判"这算不算一条附魔"是白费一道工。
    auto *combo = new QComboBox(m_anvilTable);
    for (const auto &pair : m_anvilChoices) {
        combo->addItem(pair.second, pair.first);
    }
    m_anvilTable->setCellWidget(row, 0, combo);
    auto *level = new QSpinBox(m_anvilTable);
    level->setRange(1, 9);  // 附魔等级没有超过 5 的；给到 9 是留个余地，真正的上限在表里
    level->setValue(1);
    m_anvilTable->setCellWidget(row, 1, level);
    auto *kind = new QComboBox(m_anvilTable);
    kind->addItem(QStringLiteral("附魔书"), true);
    kind->addItem(QStringLiteral("装备（已经附过魔的）"), false);
    m_anvilTable->setCellWidget(row, 2, kind);
    auto *uses = new QSpinBox(m_anvilTable);
    uses->setRange(0, 9);
    uses->setValue(0);
    uses->setToolTip(QStringLiteral("这一件在铁砧上加工过几次 —— 加工一次前科就是 2^n−1"));
    m_anvilTable->setCellWidget(row, 3, uses);
}

void ToolboxPage::setEnchantmentChoices(const QList<QPair<QString, QString>> &idAndName) {
    m_anvilChoices = idAndName;
    if (m_anvilTable == nullptr) {
        return;
    }
    // 已经摆着的那几行也得跟上（表是构造之后才喂进来的）。
    for (int row = 0; row < m_anvilTable->rowCount(); ++row) {
        if (auto *combo = qobject_cast<QComboBox *>(m_anvilTable->cellWidget(row, 0))) {
            const QString previous = combo->currentData().toString();
            combo->clear();
            for (const auto &pair : idAndName) {
                combo->addItem(pair.second, pair.first);
            }
            const int index = combo->findData(previous);
            if (index >= 0) {
                combo->setCurrentIndex(index);
            }
        }
    }
}

QList<ToolboxPage::AnvilRow> ToolboxPage::anvilRows() const {
    QList<AnvilRow> rows;
    if (m_anvilTable == nullptr) {
        return rows;
    }
    for (int row = 0; row < m_anvilTable->rowCount(); ++row) {
        AnvilRow entry;
        if (auto *combo = qobject_cast<QComboBox *>(m_anvilTable->cellWidget(row, 0))) {
            entry.id = combo->currentData().toString();
        }
        if (auto *level = qobject_cast<QSpinBox *>(m_anvilTable->cellWidget(row, 1))) {
            entry.level = level->value();
        }
        if (auto *kind = qobject_cast<QComboBox *>(m_anvilTable->cellWidget(row, 2))) {
            entry.isBook = kind->currentData().toBool();
        }
        if (auto *uses = qobject_cast<QSpinBox *>(m_anvilTable->cellWidget(row, 3))) {
            entry.uses = uses->value();
        }
        if (!entry.id.isEmpty()) {
            rows.append(entry);
        }
    }
    return rows;
}

void ToolboxPage::setAnvilView(const AnvilView &view) {
    if (m_anvilError == nullptr || m_anvilResult == nullptr) {
        return;
    }
    m_anvilError->setText(view.error);
    m_anvilError->setVisible(!view.error.isEmpty());
    if (!view.ok) {
        m_anvilResult->clear();
        return;
    }
    QString html;
    if (!view.summary.isEmpty()) {
        html.append(QStringLiteral("<p style='margin:0 0 8px 0'><b>%1</b></p>")
                        .arg(view.summary.toHtmlEscaped()));
    }
    if (!view.notes.isEmpty()) {
        html.append(QStringLiteral("<p style='margin:0 0 8px 0'>%1</p>")
                        .arg(view.notes.join(QStringLiteral("<br>")).toHtmlEscaped()));
    }
    html.append(QStringLiteral("<ol style='margin:0'>"));
    for (const QString &step : view.steps) {
        html.append(QStringLiteral("<li>%1</li>").arg(step.toHtmlEscaped()));
    }
    html.append(QStringLiteral("</ol>"));
    m_anvilResult->setText(html);
}

// ---------------------------------------------------------------------------
// 二、文本生成器
// ---------------------------------------------------------------------------

QWidget *ToolboxPage::buildTextPanel() {
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    addPanelHeader(layout, panel, Tool::Text,
                   QStringLiteral("也可以把一段 JSON 组件粘进下面「JSON 组件」那格、点"
                                  "「从 JSON 读回来」反着转。"));

    m_textSource = new QPlainTextEdit(panel);
    m_textSource->setObjectName(QStringLiteral("textSource"));
    m_textSource->setPlaceholderText(QStringLiteral("比如：欢迎来到 §b服务器§r！"));
    m_textSource->setMinimumHeight(72);
    m_textSource->setMaximumHeight(120);
    connect(m_textSource, &QPlainTextEdit::textChanged, this, [this]() {
        emit textSourceChanged(m_textSource->toPlainText());
    });
    layout->addWidget(m_textSource);

    // 色码 + 字形那一排（按钮全部由 controller 喂进来的表生成 —— 十六色那张表在
    // `TextFormat` 里，这一页不另抄一份）。
    m_textPaletteRow = new QWidget(panel);
    m_textPaletteRow->setObjectName(QStringLiteral("textPalette"));
    m_textPaletteLayout = new QGridLayout(m_textPaletteRow);
    m_textPaletteLayout->setContentsMargins(0, 0, 0, 0);
    m_textPaletteLayout->setHorizontalSpacing(4);
    m_textPaletteLayout->setVerticalSpacing(4);
    layout->addWidget(m_textPaletteRow);

    auto *jsonRow = new QHBoxLayout;
    jsonRow->setContentsMargins(0, 0, 0, 0);
    jsonRow->setSpacing(8);
    auto *fromJson = new QPushButton(QStringLiteral("从 JSON 读回来"), panel);
    fromJson->setObjectName(QStringLiteral("textFromJson"));
    fromJson->setToolTip(QStringLiteral("把下面「JSON 组件」那一格里的内容读回上面的原文"));
    Theme::markButton(fromJson, Theme::ButtonKind::Neutral);
    auto *toJson = new QPushButton(QStringLiteral("从原文生成"), panel);
    toJson->setObjectName(QStringLiteral("textFromSource"));
    Theme::markButton(toJson, Theme::ButtonKind::Neutral);
    auto *clear = new QPushButton(QStringLiteral("清空"), panel);
    clear->setObjectName(QStringLiteral("textClear"));
    Theme::markButton(clear, Theme::ButtonKind::Neutral);
    jsonRow->addWidget(fromJson);
    jsonRow->addWidget(toJson);
    jsonRow->addWidget(clear);
    jsonRow->addStretch(1);
    layout->addLayout(jsonRow);

    m_textNotice = makeMutedLabel(panel, QStringLiteral("textNotice"));
    layout->addWidget(m_textNotice);

    layout->addWidget(makeSectionTitle(panel, QStringLiteral("预览")));
    {
        QFrame *card = makeSectionCard(panel);
        m_textPreview = new QLabel(card);
        m_textPreview->setObjectName(QStringLiteral("textPreview"));
        m_textPreview->setTextFormat(Qt::RichText);
        m_textPreview->setWordWrap(true);
        m_textPreview->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_textPreview->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        m_textPreview->setMinimumHeight(40);
        card->layout()->addWidget(m_textPreview);
        layout->addWidget(card);
    }

    layout->addWidget(makeSectionTitle(panel, QStringLiteral("三种写法")));
    const struct {
        const char *objectName;
        const char *title;
        const char *tooltip;
    } kOutputs[] = {
        {"textLegacy", "§ 写法（游戏里 /give、书与笔那种）",
         "直接带 § 码的文本；贴在命令里、写进 server.properties 的 motd 都用它"},
        {"textJson", "JSON 组件", "命令里的 text 字段、物品的 display.Name 用它"},
        {"textTellraw", "/tellraw 命令", "整条命令，可以直接粘进聊天栏按回车"},
    };
    for (const auto &spec : kOutputs) {
        auto *head = new QHBoxLayout;
        head->setContentsMargins(0, 0, 0, 0);
        head->setSpacing(8);
        head->addWidget(makeSectionTitle(panel, QString::fromUtf8(spec.title)));
        auto *copy = makeCopyButton(panel);
        copy->setToolTip(QString::fromUtf8(spec.tooltip));
        head->addWidget(copy);
        head->addStretch(1);
        layout->addLayout(head);
        QFrame *card = makeSectionCard(panel);
        QPlainTextEdit *box = makeOutputBox(card, QString::fromLatin1(spec.objectName));
        card->layout()->addWidget(box);
        layout->addWidget(card);
        connect(copy, &QPushButton::clicked, this,
                [box]() { copyToClipboard(box->toPlainText()); });
        if (qstrcmp(spec.objectName, "textLegacy") == 0) {
            m_textLegacy = box;
        } else if (qstrcmp(spec.objectName, "textJson") == 0) {
            m_textJson = box;
        } else {
            m_textTellraw = box;
        }
    }

    connect(fromJson, &QPushButton::clicked, this,
            [this]() { emit textJsonRequested(m_textJson->toPlainText()); });
    connect(toJson, &QPushButton::clicked, this,
            [this]() { emit textSourceChanged(m_textSource->toPlainText()); });
    connect(clear, &QPushButton::clicked, this, [this]() { m_textSource->clear(); });
    return panel;
}

QString ToolboxPage::textSource() const {
    return m_textSource != nullptr ? m_textSource->toPlainText() : QString();
}

void ToolboxPage::setTextSource(const QString &text) {
    if (m_textSource != nullptr) {
        m_textSource->setPlainText(text);  // 这一步自己会发 textChanged → 那一发重算一遍
    }
}

void ToolboxPage::setTextPalette(const QList<PaletteEntry> &entries) {
    if (m_textPaletteLayout == nullptr || m_textPaletteRow == nullptr) {
        return;
    }
    // 整排重来（喂进来的表是唯一出处，不叠加）。
    while (QLayoutItem *item = m_textPaletteLayout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
    int column = 0;
    for (const PaletteEntry &entry : entries) {
        auto *button = new QPushButton(m_textPaletteRow);
        button->setObjectName(QStringLiteral("textCode_%1").arg(entry.code));
        button->setToolTip(QStringLiteral("%1（§%2）").arg(entry.name, entry.code));
        button->setFixedSize(30, Theme::chipHeight());
        if (entry.isColor) {
            // 颜色码那一颗：**它长什么样就是什么色**（和设置页那排色块一个道理）。字用反差色。
            const QColor color(entry.rgbHex);
            const QString ink = color.lightness() < 128 ? QStringLiteral("#ffffff")
                                                        : QStringLiteral("#000000");
            button->setStyleSheet(QStringLiteral("QPushButton { background-color: %1; color: %2;"
                                                 " border: 1px solid %3; border-radius: %4px;"
                                                 " padding: 0; }")
                                      .arg(entry.rgbHex, ink,
                                           Theme::swatchBorder(color).name(QColor::HexArgb))
                                      .arg(Theme::chipHeight() / 2));
            button->setText(entry.code.toUpper());
        } else {
            // 格式码那一颗：它没有"自己的色"，就按普通 chip 画，字写那个字母对应的意思。
            Theme::markChipButton(button);
            button->setText(entry.code == QStringLiteral("r") ? QStringLiteral("复位")
                                                              : entry.name.left(1));
        }
        const QString code = entry.code;
        connect(button, &QPushButton::clicked, this, [this, code]() {
            // 插在光标处（不是在末尾）：用户改的往往是中间那一段。
            if (m_textSource != nullptr) {
                m_textSource->insertPlainText(QStringLiteral("§%1").arg(code));
            }
        });
        m_textPaletteLayout->addWidget(button, column / 11, column % 11);
        ++column;
    }
}


void ToolboxPage::setTextPreview(const QString &html) {
    if (m_textPreview != nullptr) {
        m_textPreview->setText(html);
    }
}

void ToolboxPage::setTextOutputs(const QString &legacy, const QString &json, const QString &tellraw) {
    if (m_textLegacy != nullptr) {
        m_textLegacy->setPlainText(legacy);
    }
    if (m_textJson != nullptr) {
        m_textJson->setPlainText(json);
    }
    if (m_textTellraw != nullptr) {
        m_textTellraw->setPlainText(tellraw);
    }
}

void ToolboxPage::setTextNotice(const QString &notice) {
    if (m_textNotice == nullptr) {
        return;
    }
    m_textNotice->setText(notice);
    m_textNotice->setVisible(!notice.isEmpty());
}

// ---------------------------------------------------------------------------
// 三、坐标换算
// ---------------------------------------------------------------------------

QWidget *ToolboxPage::buildCoordPanel() {
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    addPanelHeader(layout, panel, Tool::Coord,
                   QStringLiteral("（主世界与下界是 8:1，末地不按比例算。）"));

    // ---------- 从 tp 命令认坐标 ----------
    auto *tpRow = new QHBoxLayout;
    tpRow->setContentsMargins(0, 0, 0, 0);
    tpRow->setSpacing(8);
    m_tpInput = new QLineEdit(panel);
    m_tpInput->setObjectName(QStringLiteral("coordTpInput"));
    m_tpInput->setPlaceholderText(
        QStringLiteral("粘一行 /execute in minecraft:overworld run tp @s 80.50 63.00 -76.50 0.00 0.00"));
    auto *parse = new QPushButton(QStringLiteral("认一下"), panel);
    parse->setObjectName(QStringLiteral("coordParseTp"));
    Theme::markButton(parse, Theme::ButtonKind::Neutral);
    tpRow->addWidget(m_tpInput, 1);
    tpRow->addWidget(parse);
    layout->addLayout(tpRow);

    m_tpNotice = makeMutedLabel(panel, QStringLiteral("coordTpNotice"));
    layout->addWidget(m_tpNotice);

    layout->addWidget(makeSeparator(panel));

    // ---------- 三格坐标 + 维度 ----------
    auto *form = new QHBoxLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(8);
    m_coordDimension = new QComboBox(panel);
    m_coordDimension->setObjectName(QStringLiteral("coordDimension"));
    form->addWidget(makeFieldLabel(panel, QStringLiteral("从")));
    form->addWidget(m_coordDimension);
    const auto makeSpin = [panel](const QString &objectName, double value) {
        auto *spin = new QDoubleSpinBox(panel);
        spin->setObjectName(objectName);
        spin->setDecimals(2);
        spin->setRange(-30000000.0, 30000000.0);
        spin->setValue(value);
        spin->setMinimumWidth(120);
        return spin;
    };
    m_coordX = makeSpin(QStringLiteral("coordX"), 0.0);
    m_coordY = makeSpin(QStringLiteral("coordY"), 64.0);
    m_coordZ = makeSpin(QStringLiteral("coordZ"), 0.0);
    form->addWidget(makeFieldLabel(panel, QStringLiteral("x")));
    form->addWidget(m_coordX);
    form->addWidget(makeFieldLabel(panel, QStringLiteral("y")));
    form->addWidget(m_coordY);
    form->addWidget(makeFieldLabel(panel, QStringLiteral("z")));
    form->addWidget(m_coordZ);
    auto *convert = new QPushButton(QStringLiteral("换算"), panel);
    convert->setObjectName(QStringLiteral("coordConvert"));
    Theme::markButton(convert, Theme::ButtonKind::Accent);
    form->addWidget(convert);
    form->addStretch(1);
    layout->addLayout(form);

    layout->addWidget(makeSectionTitle(panel, QStringLiteral("换算结果")));
    m_coordResult = new QLabel(panel);
    m_coordResult->setObjectName(QStringLiteral("coordResult"));
    m_coordResult->setWordWrap(true);
    m_coordResult->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_coordResult->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_coordResult->setMinimumHeight(60);
    layout->addWidget(m_coordResult, 1);

    // ---------- 复制成 tp 命令 ----------
    auto *commandRow = new QHBoxLayout;
    commandRow->setContentsMargins(0, 0, 0, 0);
    commandRow->setSpacing(8);
    m_tpCommand = new QLineEdit(panel);
    m_tpCommand->setObjectName(QStringLiteral("coordTpCommand"));
    m_tpCommand->setReadOnly(true);
    m_tpCommand->setPlaceholderText(QStringLiteral("按「复制成 tp 命令」生成一条 /tp 命令"));
    auto *makeCommand = new QPushButton(QStringLiteral("生成 tp 命令"), panel);
    makeCommand->setObjectName(QStringLiteral("coordMakeTp"));
    Theme::markButton(makeCommand, Theme::ButtonKind::Neutral);
    auto *copyCommand = makeCopyButton(panel, QStringLiteral("coordCopyTp"));
    commandRow->addWidget(m_tpCommand, 1);
    commandRow->addWidget(makeCommand);
    commandRow->addWidget(copyCommand);
    layout->addLayout(commandRow);

    connect(parse, &QPushButton::clicked, this,
            [this]() { emit teleportParseRequested(m_tpInput->text()); });
    connect(m_tpInput, &QLineEdit::returnPressed, this,
            [this]() { emit teleportParseRequested(m_tpInput->text()); });
    connect(convert, &QPushButton::clicked, this, [this]() {
        emit coordinateRequested(m_coordDimension->currentIndex(), m_coordX->value(),
                                 m_coordY->value(), m_coordZ->value());
    });
    connect(makeCommand, &QPushButton::clicked, this, [this]() {
        emit teleportCommandRequested(m_coordDimension->currentIndex(), m_coordX->value(),
                                      m_coordY->value(), m_coordZ->value());
    });
    connect(copyCommand, &QPushButton::clicked, this,
            [this]() { copyToClipboard(m_tpCommand->text()); });
    return panel;
}

void ToolboxPage::setDimensionChoices(const QStringList &names) {
    if (m_coordDimension == nullptr) {
        return;
    }
    const int previous = m_coordDimension->currentIndex();
    m_coordDimension->clear();
    m_coordDimension->addItems(names);
    if (previous >= 0 && previous < m_coordDimension->count()) {
        m_coordDimension->setCurrentIndex(previous);
    }
}

int ToolboxPage::dimensionIndex() const {
    return m_coordDimension != nullptr ? m_coordDimension->currentIndex() : 0;
}

double ToolboxPage::coordinate(int axis) const {
    const QDoubleSpinBox *spin = axis == 0 ? m_coordX : (axis == 1 ? m_coordY : m_coordZ);
    return spin != nullptr ? spin->value() : 0.0;
}

void ToolboxPage::setCoordinates(int dimensionIndex, double x, double y, double z) {
    if (m_coordDimension != nullptr && dimensionIndex >= 0
        && dimensionIndex < m_coordDimension->count()) {
        m_coordDimension->setCurrentIndex(dimensionIndex);
    }
    if (m_coordX != nullptr) {
        m_coordX->setValue(x);
    }
    if (m_coordY != nullptr) {
        m_coordY->setValue(y);
    }
    if (m_coordZ != nullptr) {
        m_coordZ->setValue(z);
    }
}

void ToolboxPage::setCoordinateRows(const QStringList &rows) {
    if (m_coordResult == nullptr) {
        return;
    }
    QString html;
    for (const QString &row : rows) {
        html.append(QStringLiteral("<div>%1</div>").arg(row.toHtmlEscaped()));
    }
    m_coordResult->setText(html);
}

void ToolboxPage::setTeleportParse(bool ok, const QString &message) {
    if (m_tpNotice == nullptr) {
        return;
    }
    m_tpNotice->setText(message);
    m_tpNotice->setVisible(!message.isEmpty());
    // 认出来了那一条是好消息，用正文色；没认出来才是"要留意的"（这里不改色 ——
    // 这一页只说清"认出来了没有"，是否要紧由那句话自己说）。
    Q_UNUSED(ok);
}

void ToolboxPage::setTeleportCommand(const QString &command) {
    if (m_tpCommand != nullptr) {
        m_tpCommand->setText(command);
    }
}

// ---------------------------------------------------------------------------
// 四、服务器在线检测
// ---------------------------------------------------------------------------

QWidget *ToolboxPage::buildServerPanel() {
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    addPanelHeader(layout, panel, Tool::Server,
                   QStringLiteral("地址写 host 或者 host:port（不写端口按 25565 算）。问到的是"
                                  "服务器**这会儿**那一份 MOTD，和列表里缓存的那份不是一回事。"));

    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    m_serverInput = new QLineEdit(panel);
    m_serverInput->setObjectName(QStringLiteral("serverAddress"));
    m_serverInput->setPlaceholderText(QStringLiteral("mc.hypixel.net"));
    m_serverCheck = new QPushButton(QStringLiteral("检测"), panel);
    m_serverCheck->setObjectName(QStringLiteral("serverCheck"));
    Theme::markButton(m_serverCheck, Theme::ButtonKind::Accent);
    m_serverCancel = new QPushButton(QStringLiteral("停下"), panel);
    m_serverCancel->setObjectName(QStringLiteral("serverCancel"));
    Theme::markButton(m_serverCancel, Theme::ButtonKind::Neutral);
    m_serverCancel->setEnabled(false);
    row->addWidget(m_serverInput, 1);
    row->addWidget(m_serverCheck);
    row->addWidget(m_serverCancel);
    layout->addLayout(row);

    auto *body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(12);
    m_serverIcon = new QLabel(panel);
    m_serverIcon->setObjectName(QStringLiteral("serverIcon"));
    m_serverIcon->setFixedSize(64, 64);
    m_serverIcon->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    m_serverIcon->setScaledContents(false);
    body->addWidget(m_serverIcon, 0, Qt::AlignTop);

    m_serverResult = new QLabel(panel);
    m_serverResult->setObjectName(QStringLiteral("serverResult"));
    m_serverResult->setTextFormat(Qt::RichText);
    m_serverResult->setWordWrap(true);
    m_serverResult->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_serverResult->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    body->addWidget(m_serverResult, 1);
    layout->addLayout(body, 1);

    connect(m_serverCheck, &QPushButton::clicked, this,
            [this]() { emit serverCheckRequested(m_serverInput->text().trimmed()); });
    connect(m_serverInput, &QLineEdit::returnPressed, this,
            [this]() { emit serverCheckRequested(m_serverInput->text().trimmed()); });
    connect(m_serverCancel, &QPushButton::clicked, this,
            [this]() { emit serverCheckCancelRequested(); });
    return panel;
}

QString ToolboxPage::serverAddress() const {
    return m_serverInput != nullptr ? m_serverInput->text().trimmed() : QString();
}

void ToolboxPage::setServerAddress(const QString &address) {
    if (m_serverInput != nullptr) {
        m_serverInput->setText(address);
    }
}

void ToolboxPage::setServerView(const ServerView &view) {
    if (m_serverResult == nullptr) {
        return;
    }
    if (!view.icon.isNull()) {
        m_serverIcon->setPixmap(view.icon.scaled(m_serverIcon->size(), Qt::KeepAspectRatio,
                                                Qt::SmoothTransformation));
    } else {
        m_serverIcon->clear();
    }
    QString html;
    const auto line = [&html](const QString &name, const QString &value) {
        if (value.isEmpty()) {
            return;
        }
        html.append(QStringLiteral("<div><b>%1</b> %2</div>")
                        .arg(name.toHtmlEscaped(), value.toHtmlEscaped()));
    };
    if (!view.ok) {
        html.append(QStringLiteral("<div><b>%1</b></div>")
                        .arg((view.timedOut ? QStringLiteral("超时") : QStringLiteral("没问成"))
                                 .toHtmlEscaped()));
        html.append(QStringLiteral("<div>%1</div>").arg(view.error.toHtmlEscaped()));
    } else {
        // MOTD 那一行是**服务器自己配的富文本**（controller 已经把 § 码翻成 HTML 了）。
        html.append(QStringLiteral("<div style='margin-bottom:6px'>%1</div>").arg(view.motd));
        line(QStringLiteral("版本"), view.version);
        line(QStringLiteral("在线"), view.players);
        line(QStringLiteral("延迟"), view.latency);
    }
    line(QStringLiteral("地址"), view.address);
    m_serverResult->setText(html);
}

void ToolboxPage::setServerBusy(bool busy) {
    if (m_serverCheck != nullptr) {
        m_serverCheck->setEnabled(!busy);
        m_serverCheck->setText(busy ? QStringLiteral("正在问…") : QStringLiteral("检测"));
    }
    if (m_serverCancel != nullptr) {
        m_serverCancel->setEnabled(busy);
    }
}

// ---------------------------------------------------------------------------
// 五、文件哈希校验
// ---------------------------------------------------------------------------

QWidget *ToolboxPage::buildHashPanel() {
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    addPanelHeader(layout, panel, Tool::Hash,
                   QStringLiteral("把一个文件拖到这一页上，或者点「选一个…」。有已知值就粘到"
                                  "那格 —— 各种写法（大写、带连字符、带 `sha256:` 前缀）都认。"));

    auto *pathRow = new QHBoxLayout;
    pathRow->setContentsMargins(0, 0, 0, 0);
    pathRow->setSpacing(8);
    m_hashPath = new QLineEdit(panel);
    m_hashPath->setObjectName(QStringLiteral("hashPath"));
    m_hashPath->setPlaceholderText(QStringLiteral("拖一个文件进来，或者点「选一个…」"));
    auto *browse = new QPushButton(QStringLiteral("选一个…"), panel);
    browse->setObjectName(QStringLiteral("hashBrowse"));
    Theme::markButton(browse, Theme::ButtonKind::Neutral);
    pathRow->addWidget(m_hashPath, 1);
    pathRow->addWidget(browse);
    layout->addLayout(pathRow);

    auto *optionRow = new QHBoxLayout;
    optionRow->setContentsMargins(0, 0, 0, 0);
    optionRow->setSpacing(8);
    m_hashAlgorithm = new QComboBox(panel);
    m_hashAlgorithm->setObjectName(QStringLiteral("hashAlgorithm"));
    m_hashExpected = new QLineEdit(panel);
    m_hashExpected->setObjectName(QStringLiteral("hashExpected"));
    m_hashExpected->setPlaceholderText(QStringLiteral("（可选）粘上人家给的那一串摘要"));
    m_hashRun = new QPushButton(QStringLiteral("算一下"), panel);
    m_hashRun->setObjectName(QStringLiteral("hashRun"));
    Theme::markButton(m_hashRun, Theme::ButtonKind::Accent);
    m_hashCancel = new QPushButton(QStringLiteral("停下"), panel);
    m_hashCancel->setObjectName(QStringLiteral("hashCancel"));
    Theme::markButton(m_hashCancel, Theme::ButtonKind::Neutral);
    m_hashCancel->setEnabled(false);
    optionRow->addWidget(m_hashAlgorithm);
    optionRow->addWidget(m_hashExpected, 1);
    optionRow->addWidget(m_hashRun);
    optionRow->addWidget(m_hashCancel);
    layout->addLayout(optionRow);

    m_hashProgress = makeMutedLabel(panel, QStringLiteral("hashProgress"));
    layout->addWidget(m_hashProgress);

    m_hashResult = new QLabel(panel);
    m_hashResult->setObjectName(QStringLiteral("hashResult"));
    m_hashResult->setWordWrap(true);
    m_hashResult->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_hashResult->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    layout->addWidget(m_hashResult, 1);

    auto *copyRow = new QHBoxLayout;
    copyRow->setContentsMargins(0, 0, 0, 0);
    copyRow->setSpacing(8);
    auto *copy = makeCopyButton(panel, QStringLiteral("hashCopy"));
    copy->setToolTip(QStringLiteral("复制算出来的那串摘要"));
    copyRow->addWidget(copy);
    copyRow->addStretch(1);
    layout->addLayout(copyRow);

    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选一个文件"),
                                                          QString(), QString(),
                                                          nullptr, QFileDialog::DontResolveSymlinks);
        if (!path.isEmpty()) {
            m_hashPath->setText(path);
        }
    });
    connect(m_hashRun, &QPushButton::clicked, this, [this]() {
        emit hashRequested(m_hashPath->text().trimmed(), m_hashAlgorithm->currentIndex(),
                           m_hashExpected->text());
    });
    connect(m_hashPath, &QLineEdit::returnPressed, this, [this]() {
        emit hashRequested(m_hashPath->text().trimmed(), m_hashAlgorithm->currentIndex(),
                           m_hashExpected->text());
    });
    connect(m_hashCancel, &QPushButton::clicked, this, [this]() { emit hashCancelRequested(); });
    connect(copy, &QPushButton::clicked, this, [this]() {
        // 拷的是**摘要那一串本身**（`m_hashHex`），不是结果标签里那一段带 HTML 的排版 ——
        // 把标签的文字整段拷出去，用户粘到人家那格里是"一行说明 + 一串摘要"。
        copyToClipboard(m_hashHex);
    });
    return panel;
}

void ToolboxPage::setAlgorithmChoices(const QStringList &names) {
    if (m_hashAlgorithm == nullptr) {
        return;
    }
    const int previous = m_hashAlgorithm->currentIndex();
    m_hashAlgorithm->clear();
    m_hashAlgorithm->addItems(names);
    if (previous >= 0 && previous < m_hashAlgorithm->count()) {
        m_hashAlgorithm->setCurrentIndex(previous);
    }
}

int ToolboxPage::algorithmIndex() const {
    return m_hashAlgorithm != nullptr ? m_hashAlgorithm->currentIndex() : 0;
}

QString ToolboxPage::hashPath() const {
    return m_hashPath != nullptr ? m_hashPath->text().trimmed() : QString();
}

void ToolboxPage::setHashPath(const QString &path) {
    if (m_hashPath != nullptr) {
        m_hashPath->setText(path);
    }
}

QString ToolboxPage::hashExpected() const {
    return m_hashExpected != nullptr ? m_hashExpected->text() : QString();
}

void ToolboxPage::setHashView(const HashView &view) {
    if (m_hashResult == nullptr) {
        return;
    }
    m_hashHex = view.hex;
    QString html;
    if (!view.ok) {
        html = QStringLiteral("<div>%1</div>").arg(view.error.toHtmlEscaped());
    } else {
        html.append(QStringLiteral("<div><b>%1</b></div>")
                        .arg(view.algorithm.toHtmlEscaped()));
        html.append(QStringLiteral("<div style='font-family:monospace'>%1</div>")
                        .arg(view.hex.toHtmlEscaped()));
        if (!view.sizeText.isEmpty()) {
            html.append(QStringLiteral("<div>文件大小 %1</div>").arg(view.sizeText.toHtmlEscaped()));
        }
        if (view.hasVerdict) {
            html.append(QStringLiteral("<div style='margin-top:6px'><b>%1</b>%2</div>")
                            .arg(view.matches ? QStringLiteral("一致：") : QStringLiteral("不一样："),
                                 view.verdict.toHtmlEscaped()));
        }
    }
    m_hashResult->setText(html);
}

void ToolboxPage::setHashProgress(qint64 done, qint64 total) {
    if (m_hashProgress == nullptr) {
        return;
    }
    if (total <= 0) {
        m_hashProgress->setText(QStringLiteral("已经读了 %1").arg(done));
        return;
    }
    m_hashProgress->setText(QStringLiteral("已经读了 %1 / %2（%3%）")
                                .arg(done)
                                .arg(total)
                                .arg(total > 0 ? done * 100 / total : 0));
}

void ToolboxPage::setHashBusy(bool busy) {
    if (m_hashRun != nullptr) {
        m_hashRun->setEnabled(!busy);
        m_hashRun->setText(busy ? QStringLiteral("正在算…") : QStringLiteral("算一下"));
    }
    if (m_hashCancel != nullptr) {
        m_hashCancel->setEnabled(busy);
    }
    if (!busy && m_hashProgress != nullptr) {
        m_hashProgress->clear();
    }
}

// ---------------------------------------------------------------------------
// 拖一个文件进来（哈希那一件；别的工具不管文件）
// ---------------------------------------------------------------------------

void ToolboxPage::dragEnterEvent(QDragEnterEvent *event) {
    if (m_current == Tool::Hash && event->mimeData()->hasUrls()
        && !event->mimeData()->urls().isEmpty()) {
        event->acceptProposedAction();
        return;
    }
    QDialog::dragEnterEvent(event);
}

void ToolboxPage::dropEvent(QDropEvent *event) {
    if (m_current == Tool::Hash && event->mimeData()->hasUrls()) {
        const QList<QUrl> urls = event->mimeData()->urls();
        if (!urls.isEmpty()) {
            // 只认本地文件（拖一个网址进来不是"要算哈希"）。
            const QString path = urls.first().toLocalFile();
            if (!path.isEmpty()) {
                setHashPath(path);
                event->acceptProposedAction();
                return;
            }
        }
    }
    QDialog::dropEvent(event);
}
