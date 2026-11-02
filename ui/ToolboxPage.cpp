/**
 * @file ToolboxPage.cpp
 * @brief 工具箱那一页的实现（形状、"为什么五件挤一页"、第六件为什么单开窗，见头文件）。
 */

#include "ToolboxPage.hpp"
#include "ArmorTrimPreview.hpp"

#include <QCheckBox>
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
#include <QListWidget>
#include <QListView>
#include <QStandardItemModel>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
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

/// 十件工具的那张表（**键 / 中文名 / 面板短名 / 一句话说明的唯一出处**）。
/// NOTE: `Download` 只在这张表里出现 —— chip 那排不摆它（构造里从 `m_tools` 摘掉），
/// 底栏那块面板按 `panelTools()` 另排一张（两列五排，见 MainWindowUI.cpp 那头）。
struct ToolInfo {
    ToolboxPage::Tool tool;
    const char *key;
    const char *name;
    /// 底栏那块面板上写的短名（面板按「不要搜索框、两列五排」排）。
    /// **只归面板用**：页面标题和 chip 仍取 `name`（完整名字）。
    const char *shortName;
    const char *summary;
};

const ToolInfo kToolInfos[] = {
    {ToolboxPage::Tool::Download, "download", "高速下载", "高速下载",
     "HTTP/HTTPS 直链下载，复用下载队列并在任务中心跟踪。"},
    {ToolboxPage::Tool::Anvil, "anvil", "附魔顺序助手", "附魔助手",
     "照 Java 版铁砧那套惩罚机制，算一条最省等级的顺序。"},
    {ToolboxPage::Tool::Text, "text", "文本生成器", "彩色文本",
     "§ 文本、JSON 组件与 tellraw 命令互相转换。"},
    {ToolboxPage::Tool::Coord, "coord", "坐标换算", "坐标换算",
     "主世界与下界互转，也认 F3+C 的 tp 命令。"},
    {ToolboxPage::Tool::Server, "server", "服务器在线检测", "延迟测试",
     "连上去问一句，看 MOTD、版本、在线人数，并量一个来回的延迟。"},
    {ToolboxPage::Tool::Hash, "hash", "文件哈希校验", "SHA校验",
     "算文件的 SHA-256 / SHA-1 / MD5，可对照已知值。"},
    {ToolboxPage::Tool::Beacon, "beacon", "信标计算", "信标计算",
     "信标几级要多少块、效果范围多大、这一级能给出哪些效果。"},
    {ToolboxPage::Tool::Brewing, "brewing", "酿造助手", "酿造助手",
     "选一种药水，摆出从水瓶开始每一步该加什么材料。"},
    {ToolboxPage::Tool::Banner, "banner", "旗帜设计", "旗帜设计",
     "选底色、一层一层叠图案，算织布机步骤和一共要多少染料。"},
    {ToolboxPage::Tool::Trim, "trim", "纹饰搭配", "纹饰搭配",
     "盔甲材质 + 纹饰图案 + 纹饰材料，算锻造台怎么摆、材料要多少。"},
};

const ToolInfo *infoOf(ToolboxPage::Tool tool) {
    for (const ToolInfo &info : kToolInfos) {
        if (info.tool == tool) {
            return &info;
        }
    }
    return &kToolInfos[0];
}

// ---------------------------------------------------------------------------
// 底栏面板上的那十枚线稿图标
// ---------------------------------------------------------------------------
//
// 现画，不落盘：48×48 的画布按 2 倍像素密度交出去（逻辑 24×24），线宽 3.4 在这个
// 坐标系里 = 屏幕上 1.7 px —— 和界面里正文笔画差不多粗细。色由调用方给：面板那边
// 拿主题色的 `Ink` 档（深色主题下换 `Pure`），不在这里另立一条配色。
//
// NOTE: 十件一件一个 switch 分支，**没有 default 省略** —— 以后往 `Tool` 里加一件，
// 编译器会在这里点名。
QIcon drawToolIcon(ToolboxPage::Tool tool, const QColor &ink) {
    constexpr int kSide = 48;
    QPixmap pixmap(kSide, kSide);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(ink, 3.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    switch (tool) {
    case ToolboxPage::Tool::Hash:
        // 摘要符号，避免小尺寸下指纹多重弧线糊成一团。
        painter.drawLine(QPointF(20, 9), QPointF(15, 39));
        painter.drawLine(QPointF(33, 9), QPointF(28, 39));
        painter.drawLine(QPointF(10, 19), QPointF(39, 19));
        painter.drawLine(QPointF(8, 30), QPointF(37, 30));
        break;
    case ToolboxPage::Tool::Download:
        // 往托盘里落一支箭头。
        painter.drawLine(QPointF(24, 6), QPointF(24, 27));
        painter.drawLine(QPointF(16, 20), QPointF(24, 28));
        painter.drawLine(QPointF(32, 20), QPointF(24, 28));
        painter.drawLine(QPointF(10, 30), QPointF(10, 39));
        painter.drawLine(QPointF(10, 39), QPointF(38, 39));
        painter.drawLine(QPointF(38, 39), QPointF(38, 30));
        break;
    case ToolboxPage::Tool::Server:
        // 两层服务器机架，和坐标定位/无线网络区分。
        painter.drawRoundedRect(QRectF(9, 9, 30, 12), 2, 2);
        painter.drawRoundedRect(QRectF(9, 27, 30, 12), 2, 2);
        painter.drawPoint(QPointF(15, 15));
        painter.drawPoint(QPointF(15, 33));
        painter.drawLine(QPointF(25, 15), QPointF(33, 15));
        painter.drawLine(QPointF(25, 33), QPointF(33, 33));
        break;
    case ToolboxPage::Tool::Anvil:
        // 铁砧：平整工作面、收腰底座和独立砧角。
        {
            QPainterPath anvil;
            anvil.moveTo(9, 12); anvil.lineTo(39, 12);
            anvil.lineTo(36, 21); anvil.lineTo(29, 25);
            anvil.lineTo(29, 32); anvil.lineTo(35, 38);
            anvil.lineTo(13, 38); anvil.lineTo(19, 32);
            anvil.lineTo(19, 25); anvil.lineTo(9, 22);
            anvil.lineTo(5, 17); anvil.lineTo(9, 17);
            anvil.closeSubpath(); painter.drawPath(anvil);
        }
        break;
    case ToolboxPage::Tool::Coord:
        // 地图针：一滴 + 中间一个洞。
        {
            QPainterPath pin;
            pin.moveTo(24, 41);
            pin.cubicTo(13, 29, 12, 22, 12, 18);
            pin.cubicTo(12, 11, 17, 7, 24, 7);
            pin.cubicTo(31, 7, 36, 11, 36, 18);
            pin.cubicTo(36, 22, 35, 29, 24, 41);
            painter.drawPath(pin);
            painter.drawEllipse(QPointF(24, 18), 4.5, 4.5);
        }
        break;
    case ToolboxPage::Tool::Text:
        // 文字 T + 色卡，减少小尺寸中的碎点。
        painter.drawLine(QPointF(9, 11), QPointF(35, 11));
        painter.drawLine(QPointF(22, 11), QPointF(22, 32));
        painter.drawLine(QPointF(16, 32), QPointF(28, 32));
        painter.drawRoundedRect(QRectF(9, 38, 30, 4), 1, 1);
        painter.drawLine(QPointF(19, 38), QPointF(19, 42));
        painter.drawLine(QPointF(29, 38), QPointF(29, 42));
        break;
    case ToolboxPage::Tool::Brewing:
        // 圆底烧瓶 + 液面。
        painter.drawLine(QPointF(18, 7), QPointF(30, 7));
        painter.drawLine(QPointF(20, 7), QPointF(20, 16));
        painter.drawLine(QPointF(28, 7), QPointF(28, 16));
        {
            QPainterPath flask;
            flask.moveTo(20, 16);
            flask.lineTo(12, 32);
            flask.quadTo(10, 41, 24, 41);
            flask.quadTo(38, 41, 36, 32);
            flask.lineTo(28, 16);
            painter.drawPath(flask);
            painter.drawLine(QPointF(16, 29), QPointF(32, 29));
        }
        break;
    case ToolboxPage::Tool::Banner:
        // Minecraft 竖旗，而非横向的燕尾旗。
        painter.drawLine(QPointF(24, 32), QPointF(24, 42));
        painter.drawLine(QPointF(17, 42), QPointF(31, 42));
        painter.drawLine(QPointF(10, 8), QPointF(38, 8));
        {
            QPainterPath flag;
            flag.moveTo(13, 8);
            flag.lineTo(35, 8);
            flag.lineTo(35, 34);
            flag.lineTo(24, 29);
            flag.lineTo(13, 34);
            flag.closeSubpath();
            painter.drawPath(flag);
        }
        break;
    case ToolboxPage::Tool::Beacon:
        // 发光立方体和阶梯底座。
        painter.drawLine(QPointF(24, 5), QPointF(24, 11));
        painter.drawRect(QRectF(18, 16, 12, 12));
        painter.drawLine(QPointF(12, 34), QPointF(36, 34));
        painter.drawLine(QPointF(7, 41), QPointF(41, 41));
        break;
    case ToolboxPage::Tool::Trim:
        // 一件上衣：盔甲纹饰。
        {
            QPainterPath shirt;
            shirt.moveTo(18, 9);
            shirt.lineTo(10, 11);
            shirt.lineTo(10, 22);
            shirt.lineTo(15, 25);
            shirt.lineTo(15, 39);
            shirt.lineTo(33, 39);
            shirt.lineTo(33, 25);
            shirt.lineTo(38, 22);
            shirt.lineTo(38, 11);
            shirt.lineTo(30, 9);
            shirt.cubicTo(28, 13, 20, 13, 18, 9);
            shirt.closeSubpath();
            painter.drawPath(shirt);
            painter.drawLine(QPointF(20, 23), QPointF(24, 29));
            painter.drawLine(QPointF(24, 29), QPointF(28, 23));
        }
        break;
    }
    painter.end();
    pixmap.setDevicePixelRatio(2.0);
    return QIcon(pixmap);
}

/// 标题用带提手和锁扣的工具箱。
QIcon drawToolboxIcon(const QColor &ink) {
    QPixmap pixmap(48, 48);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(ink, 3.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(7, 17, 34, 24), 3, 3);
    QPainterPath handle;
    handle.moveTo(17, 17); handle.lineTo(17, 10);
    handle.lineTo(31, 10); handle.lineTo(31, 17);
    painter.drawPath(handle);
    painter.drawLine(QPointF(7, 27), QPointF(20, 27));
    painter.drawLine(QPointF(28, 27), QPointF(41, 27));
    painter.drawRoundedRect(QRectF(20, 24, 8, 7), 1, 1);
    painter.end();
    pixmap.setDevicePixelRatio(2.0);
    return QIcon(pixmap);
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
    font = Theme::textFont(font, Theme::TextRole::Heading);
    label->setFont(font);
    return label;
}

/// 一张**小节卡片**（`wtfmlSurface="section"` 那一档面：浅一档的底 + 圆角、不画边）。
///
/// 这个面**只有 `QFrame` 认得**（样式表那条选择器是 `QFrame[wtfmlSurface="section"]`）——
/// 挂在一个 `QLabel` / `QPlainTextEdit` 上等于什么都没发生。所以"要有卡片底"的地方一律
/// 先起一张 `QFrame`、把东西装进去，别指望给内容控件挂个属性就长出来。
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
    font = Theme::textFont(font, Theme::TextRole::Heading);
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

void addResultCopy(QVBoxLayout *layout, QLabel *result) {
    auto *button = new QPushButton(QStringLiteral("复制结果"), result->parentWidget());
    button->setObjectName(result->objectName() + QStringLiteral("Copy"));
    button->setEnabled(false);
    button->setAutoDefault(false);
    Theme::markButton(button, Theme::ButtonKind::Ghost);
    layout->addWidget(button, 0, Qt::AlignRight);
    QObject::connect(button, &QPushButton::clicked, result, [result] {
        const auto text = result->property("copyText").toString();
        if (!text.isEmpty()) copyToClipboard(text);
    });
}

void updateResultCopy(QLabel *result, const QString &plainText) {
    result->setProperty("copyText", plainText);
    auto *button = result->parentWidget()->findChild<QPushButton *>(result->objectName() + QStringLiteral("Copy"));
    if (button != nullptr) button->setEnabled(!plainText.trimmed().isEmpty());
}

/// 表单里的一个小标题那样的行内标签（`附魔` / `等级`）。
QLabel *makeFieldLabel(QWidget *parent, const QString &text) {
    auto *label = new QLabel(text, parent);
    label->setWordWrap(false);
    return label;
}

}  // namespace

// ---------------------------------------------------------------------------
// 六件工具那张表（键 / 名字 / 说明）
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

QList<ToolboxPage::Tool> ToolboxPage::panelTools() {
    // 面板的次序（两列五排、**行优先**）：
    // SHA校验 | 高速下载
    // 延迟测试 | 附魔助手
    // 坐标换算 | 彩色文本
    // 酿造助手 | 旗帜设计
    // 信标计算 | 纹饰搭配
    return {Tool::Hash,  Tool::Download, Tool::Server, Tool::Anvil, Tool::Coord,
            Tool::Text,  Tool::Brewing,  Tool::Banner, Tool::Beacon, Tool::Trim};
}

QString ToolboxPage::toolShortName(Tool tool) {
    return QString::fromUtf8(infoOf(tool)->shortName);
}

QIcon ToolboxPage::toolIcon(Tool tool, const QColor &ink) {
    return drawToolIcon(tool, ink);
}

QIcon ToolboxPage::panelTitleIcon(const QColor &ink) {
    return drawToolboxIcon(ink);
}

QString ToolboxPage::toolChipName(Tool tool) {
    return QStringLiteral("toolboxChip_%1").arg(toolKey(tool));
}

QString ToolboxPage::toolPanelName(Tool tool) {
    return QStringLiteral("toolboxPanel_%1").arg(toolKey(tool));
}

QString ToolboxPage::toolPageName(Tool tool) {
    return toolPanelName(tool) + QStringLiteral("Scroll");
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
    // 直链下载使用独立小窗口，所以不在这张工具页的 chip 里。
    m_tools.removeAll(Tool::Download);

    auto *box = new QVBoxLayout(this);
    box->setContentsMargins(8, 2, 8, 6);
    box->setSpacing(6);

    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(0,0,0,0); toolbar->setSpacing(8);
    toolbar->addWidget(buildChipRow(), 1);
    auto *remember = new QCheckBox(QStringLiteral("记住输入"), this);
    remember->setToolTip(QStringLiteral("仅保存在本机；关闭后清除工具输入记忆。"));
    remember->setObjectName(QStringLiteral("toolboxRememberInputs"));
    remember->setChecked(true);
    toolbar->addWidget(remember);
    box->addLayout(toolbar);
    box->addWidget(makeSeparator(this));

    m_stack = new QStackedWidget(this);
    // **加进去的次序 = m_tools 的次序**（`showTool()` 按下标切，两边不许各写一遍次序）。
    for (Tool tool : m_tools) {
        QWidget *panel = nullptr;
        switch (tool) {
        case Tool::Download:
            continue;
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
        case Tool::Beacon:
            panel = buildBeaconPanel();
            break;
        case Tool::Brewing:
            panel = buildBrewingPanel();
            break;
        case Tool::Banner:
            panel = buildBannerPanel();
            break;
        case Tool::Trim:
            panel = buildTrimPanel();
            break;
        }
        panel->setObjectName(toolPanelName(tool));
        // 长工具页在自身滚动，不把整个窗口的最小高度撑大。
        auto *scroll = new QScrollArea(m_stack);
        scroll->setObjectName(toolPageName(tool));
        scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(panel); m_stack->addWidget(scroll);
    }
    box->addWidget(m_stack, 1);

    showTool(Tool::Anvil);
}

QWidget *ToolboxPage::buildChipRow() {
    auto *row = new QWidget(this);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);
    for (Tool tool : m_tools) {
        auto *chip = new QPushButton(toolShortName(tool), row);
        chip->setFixedHeight(28);
        chip->setStyleSheet(QStringLiteral("padding: 2px 7px; min-height: 18px;"));
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
    if (tool == Tool::Download) { emit downloadToolRequested(); return; }
    int index = 0;
    for (int i = 0; i < m_tools.size(); ++i) {
        if (m_tools.at(i) == tool) {
            index = i;
        }
    }
    m_current = m_tools.at(index);
    m_stack->setCurrentIndex(index);
    syncChips();
    // 顶上那排 chip 只走到这里（不经过 controller），所以"换了哪一件"必须从这里发出去 ——
    // 纹饰那一页要借它去读预览资源，漏了就是"从 chip 进去不渲染盔甲"。
    emit toolShown(m_current);
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
                                  "哪一步的代价超了就整条做不出来，会单独提示；算的是"
                                  "每一步都做得到的前提下最省的那条。第五列勾上 = 这一条附魔"
                                  "并进上一行那一件（一本书能带好几条附魔）。"));

    m_anvilTable = new QTableWidget(0, 5, panel);
    m_anvilTable->setObjectName(QStringLiteral("anvilTable"));
    m_anvilTable->setHorizontalHeaderLabels(
        {QStringLiteral("附魔"), QStringLiteral("等级"), QStringLiteral("这一件"),
         QStringLiteral("在铁砧上用过几次"), QStringLiteral("和上一行同一件")});
    m_anvilTable->verticalHeader()->setVisible(false);
    m_anvilTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_anvilTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
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
    auto *rename = new QCheckBox(QStringLiteral("顺带改个名"), panel);
    rename->setObjectName(QStringLiteral("anvilRename"));
    // 铁砧那边改名只加在**一次操作**上：合的时候顺手改（+1 级），或者合完单独改一次
    // （合完那件的前科 + 1、封顶 39）—— 这一格勾上就按"哪条便宜 / 塞得进去"来算。
    rename->setToolTip(QStringLiteral("勾上就按「要改名」算：能塞进某一步合的时候就只多 1 级；"
                                      "每一步都到 39 级了，就合完单独改一次"
                                      "（合完那件的前科 + 1，封顶 39）。"));
    m_anvilRename = rename;
    auto *plan = new QPushButton(QStringLiteral("计算"), panel);
    plan->setObjectName(QStringLiteral("anvilPlan"));
    Theme::markButton(plan, Theme::ButtonKind::Accent);
    rowButtons->addWidget(addRow);
    rowButtons->addWidget(removeRow);
    rowButtons->addSpacing(12);
    rowButtons->addWidget(rename);
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
            refreshAnvilSameColumn();
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
    // 四格**一眼就能看出是输入控件**：附魔和"这一件"是下拉框、等级和次数是数字框。
    // 用 `setCellWidget` 而不是可编辑的 item：下拉框和数字框的合法值域由控件自己管着，
    // 让用户敲一串文本再回来判"这算不算一条附魔"是白费一道工。
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
    uses->setToolTip(QStringLiteral("在铁砧上加工过几次（前科 = 2^n−1）"));
    m_anvilTable->setCellWidget(row, 3, uses);

    // 第五格：这一条附魔和**上一行**是不是同一件东西。一本书能带好几条附魔、一件装备
    // 本来也可能已经有好几条 —— 勾上就并进上一行那一件（不是"又一件"）。
    auto *sameWrap = new QWidget(m_anvilTable);
    auto *sameLayout = new QHBoxLayout(sameWrap);
    sameLayout->setContentsMargins(0, 0, 0, 0);
    sameLayout->setAlignment(Qt::AlignCenter);
    auto *same = new QCheckBox(sameWrap);
    same->setObjectName(QStringLiteral("anvilSame"));
    same->setToolTip(QStringLiteral("勾上表示与上一行是同一件（例如一本书带两条附魔）。"));
    sameLayout->addWidget(same);
    m_anvilTable->setCellWidget(row, 4, sameWrap);
    // 勾上之后"这一件"和"用过几次"就归上一行那一件管了 —— 这两格跟着灰掉，
    // 免得用户以为这里再填一遍会改到别的东西。
    connect(same, &QCheckBox::toggled, this, [this](bool) { refreshAnvilSameColumn(); });
    refreshAnvilSameColumn();
}

void ToolboxPage::refreshAnvilSameColumn() {
    if (m_anvilTable == nullptr) {
        return;
    }
    for (int row = 0; row < m_anvilTable->rowCount(); ++row) {
        QWidget *wrap = m_anvilTable->cellWidget(row, 4);
        auto *same = wrap != nullptr ? wrap->findChild<QCheckBox *>() : nullptr;
        if (same == nullptr) {
            continue;
        }
        if (row == 0) {
            // 第一行上面没有东西可并 —— 灰着，而且不能替它记着一个 true。
            same->setChecked(false);
            same->setEnabled(false);
            same->setToolTip(QStringLiteral("第一行没有上一行：这一件就是新的一件。"));
        } else {
            same->setEnabled(true);
            same->setToolTip(QStringLiteral("勾上 = 这一条附魔属于上一行那一件（比如一本书带着两条附魔）。"
                                            "不勾 = 这是新的一件。"));
        }
        // 「这一件」和「用过几次」描述的是**那一件东西**：并进上一行时它们归上一行管，
        // 这一行自己那两格灰掉（不然摆着两个能改、真正生效的却是上面那个）。
        const bool inherit = row > 0 && same->isChecked();
        if (auto *kind = qobject_cast<QComboBox *>(m_anvilTable->cellWidget(row, 2))) {
            kind->setEnabled(!inherit);
        }
        if (auto *uses = qobject_cast<QSpinBox *>(m_anvilTable->cellWidget(row, 3))) {
            uses->setEnabled(!inherit);
        }
    }
}

void ToolboxPage::setAnvilRows(const QList<AnvilRow> &rows) {
    m_anvilTable->setRowCount(0);
    for (const auto &entry : rows.mid(0, 64)) {
        appendAnvilRow();
        const int row = m_anvilTable->rowCount() - 1;
        auto *enchant = qobject_cast<QComboBox *>(m_anvilTable->cellWidget(row, 0));
        const int index = enchant->findData(entry.id);
        if (index < 0) { m_anvilTable->removeRow(row); continue; }
        enchant->setCurrentIndex(index);
        qobject_cast<QSpinBox *>(m_anvilTable->cellWidget(row, 1))->setValue(entry.level);
        qobject_cast<QComboBox *>(m_anvilTable->cellWidget(row, 2))->setCurrentIndex(entry.isBook ? 0 : 1);
        qobject_cast<QSpinBox *>(m_anvilTable->cellWidget(row, 3))->setValue(entry.uses);
        m_anvilTable->cellWidget(row, 4)->findChild<QCheckBox *>()->setChecked(row > 0 && entry.sameAsPrevious);
    }
    refreshAnvilSameColumn();
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

bool ToolboxPage::anvilRename() const {
    return m_anvilRename != nullptr && m_anvilRename->isChecked();
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
        if (QWidget *wrap = m_anvilTable->cellWidget(row, 4)) {
            if (auto *same = wrap->findChild<QCheckBox *>()) {
                entry.sameAsPrevious = same->isChecked();
            }
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

    // 1.16 起那份自定颜色：十六色表里没有的颜色。这一格只收 `#RRGGBB` 六位 ——
    // `§x§R§R§G§G§B§B` 那串由 controller 算（规矩在 `TextFormat` 里，这一页不抄）。
    auto *hexRow = new QHBoxLayout;
    hexRow->setContentsMargins(0, 0, 0, 0);
    hexRow->setSpacing(8);
    m_textHex = new QLineEdit(panel);
    m_textHex->setObjectName(QStringLiteral("textHex"));
    m_textHex->setPlaceholderText(QStringLiteral("#ff8800"));
    m_textHex->setMaxLength(7);
    m_textHex->setFixedWidth(96);
    m_textHex->setToolTip(QStringLiteral("六位十六进制（#RRGGBB）。§ 写法需 1.16 起。"));
    auto *hexInsert = new QPushButton(QStringLiteral("插自定颜色"), panel);
    hexInsert->setObjectName(QStringLiteral("textHexInsert"));
    hexInsert->setToolTip(m_textHex->toolTip());
    Theme::markButton(hexInsert, Theme::ButtonKind::Neutral);
    hexRow->addWidget(m_textHex);
    hexRow->addWidget(hexInsert);
    hexRow->addStretch(1);
    layout->addLayout(hexRow);
    connect(hexInsert, &QPushButton::clicked, this,
            [this]() { emit textHexColorRequested(m_textHex->text().trimmed()); });
    connect(m_textHex, &QLineEdit::returnPressed, this,
            [this]() { emit textHexColorRequested(m_textHex->text().trimmed()); });

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

    layout->addWidget(makeSectionTitle(panel, QStringLiteral("四种写法")));
    const struct {
        const char *objectName;
        const char *title;
        const char *tooltip;
    } kOutputs[] = {
        {"textLegacy", "§ 写法（游戏里 /give、书与笔那种）",
         "直接带 § 码的文本；贴在命令里都用它"},
        {"textJson", "JSON 组件", "命令里的 text 字段、物品的 display.Name 用它"},
        {"textTellraw", "/tellraw 命令", "整条命令，可以直接粘进聊天栏按回车"},
        {"textMotd", "server.properties 的 motd 那一行",
         "整行粘进 server.properties；非 ASCII 已转成 \\uXXXX"},
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
        } else if (qstrcmp(spec.objectName, "textTellraw") == 0) {
            m_textTellraw = box;
        } else {
            m_textMotd = box;
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

void ToolboxPage::setTextMotd(const QString &motd) {
    if (m_textMotd != nullptr) {
        m_textMotd->setPlainText(motd);
    }
}

void ToolboxPage::insertTextAtCursor(const QString &text) {
    if (m_textSource != nullptr) {
        m_textSource->insertPlainText(text);  // 插在光标处：改的往往是中间那一段
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
    addResultCopy(layout, m_coordResult);

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
    updateResultCopy(m_coordResult, rows.join(QLatin1Char('\n')));
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
                   QStringLiteral("地址填 host 或 host:port（默认端口 25565）。查实时 MOTD，量的是状态握手一个来回。"));

    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    m_serverInput = new QLineEdit(panel);
    m_serverInput->setObjectName(QStringLiteral("serverAddress"));
    m_serverInput->setPlaceholderText(QStringLiteral("mc.hypixel.net"));
    m_serverCheck = new QPushButton(QStringLiteral("检测"), panel);
    m_serverCheck->setObjectName(QStringLiteral("serverCheck"));
    Theme::markButton(m_serverCheck, Theme::ButtonKind::Accent);
    m_serverCancel = new QPushButton(QStringLiteral("取消"), panel);
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
    // 和按钮共享禁用状态，检测中敲回车不能偷偷再发起一趟请求。
    connect(m_serverInput, &QLineEdit::returnPressed, m_serverCheck, &QPushButton::click);
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
        // 分类那一格用的是 `ServerPing::kindName()`（controller 摆进来的）：它比弹一句
        // socket 错误原文更好懂 —— 「连上了但没答话」和「连接被拒」不是同一件事。
        const QString headline = !view.kind.isEmpty()
                                     ? view.kind
                                     : (view.timedOut ? QStringLiteral("超时")
                                                      : QStringLiteral("检测失败"));
        html.append(QStringLiteral("<div><b>%1</b></div>").arg(headline.toHtmlEscaped()));
        html.append(QStringLiteral("<div>%1</div>").arg(view.error.toHtmlEscaped()));
    } else {
        // MOTD 那一行是**服务器自己配的富文本**（controller 已经把 § 码翻成 HTML 了）。
        html.append(QStringLiteral("<div style='margin-bottom:6px'>%1</div>").arg(view.motd));
        line(QStringLiteral("版本"), view.version);
        line(QStringLiteral("在线"), view.players);
        // 写「握手往返」不写「延迟」：这一次测的是状态查询那一个来回，不是游戏内延迟。
        line(QStringLiteral("握手往返"), view.latency);
    }
    line(QStringLiteral("地址"), view.address);
    m_serverResult->setText(html);
}

void ToolboxPage::setServerBusy(bool busy) {
    if (m_serverCheck != nullptr) {
        m_serverCheck->setEnabled(!busy);
        m_serverCheck->setText(busy ? QStringLiteral("检测中…") : QStringLiteral("检测"));
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
                   QStringLiteral("把文件拖到这一页，或点「选一个…」。已知值可粘进那格，各种写法都认。"));

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
    m_hashRun = new QPushButton(QStringLiteral("计算"), panel);
    m_hashRun->setObjectName(QStringLiteral("hashRun"));
    Theme::markButton(m_hashRun, Theme::ButtonKind::Accent);
    m_hashCancel = new QPushButton(QStringLiteral("取消"), panel);
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
    // 算完之后文件变过的那句提醒：平时一个像素都不占（空文本 + 收起来）。
    m_hashNotice = new QLabel(panel);
    m_hashNotice->setObjectName(hashNoticeName());
    m_hashNotice->setWordWrap(true);
    m_hashNotice->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_hashNotice->hide();
    layout->addWidget(m_hashNotice);

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
    // ---------- 批量（「补多文件、复制/导出列表」） ----------
    m_hashBatch = new QPushButton(QStringLiteral("批量…"), panel);
    m_hashBatch->setObjectName(hashBatchName());
    m_hashBatch->setToolTip(QStringLiteral("一次选好几个文件，按同一档算法逐个算。"));
    Theme::markButton(m_hashBatch, Theme::ButtonKind::Neutral);
    m_hashCopyAll = new QPushButton(QStringLiteral("复制全部"), panel);
    m_hashCopyAll->setObjectName(hashCopyAllName());
    m_hashCopyAll->setToolTip(QStringLiteral("复制成标准的校验和格式（一行一个「摘要  文件名」）。"));
    Theme::markButton(m_hashCopyAll, Theme::ButtonKind::Neutral);
    m_hashExport = new QPushButton(QStringLiteral("导出…"), panel);
    m_hashExport->setObjectName(hashExportName());
    m_hashExport->setToolTip(QStringLiteral("把这张清单存成一个 .txt。"));
    Theme::markButton(m_hashExport, Theme::ButtonKind::Neutral);
    m_hashCopyAll->setEnabled(false);
    m_hashExport->setEnabled(false);
    copyRow->addWidget(m_hashBatch);
    copyRow->addStretch(1);
    copyRow->addWidget(m_hashCopyAll);
    copyRow->addWidget(m_hashExport);
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
    connect(m_hashBatch, &QPushButton::clicked, this, [this]() {
        const QStringList paths =
            QFileDialog::getOpenFileNames(this, QStringLiteral("选几个文件"), QString(), QString(),
                                          nullptr, QFileDialog::DontResolveSymlinks);
        if (!paths.isEmpty()) {
            emit hashBatchRequested(paths, m_hashAlgorithm != nullptr ? m_hashAlgorithm->currentIndex() : 0);
        }
    });
    connect(m_hashCopyAll, &QPushButton::clicked, this,
            [this]() { copyToClipboard(m_hashBatchText); });
    connect(m_hashExport, &QPushButton::clicked, this, [this]() {
        if (m_hashBatchText.isEmpty()) {
            return;
        }
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("把清单存成一个 .txt"),
            QStringLiteral("checksums.txt"),
            QStringLiteral("文本文件 (*.txt)"));
        if (!path.isEmpty()) {
            emit hashExportRequested(path, m_hashBatchText);
        }
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
    // 单文件这一档把批量那一份收掉：屏幕上只留"这一次算的是什么"。
    m_hashBatchText.clear();
    if (m_hashCopyAll != nullptr) {
        m_hashCopyAll->setEnabled(false);
    }
    if (m_hashExport != nullptr) {
        m_hashExport->setEnabled(false);
    }
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

void ToolboxPage::setHashBatchView(const QList<HashBatchRow> &rows, const QString &summary) {
    if (m_hashResult == nullptr) {
        return;
    }
    // 批量这一档把单文件那一串收掉（两档各自摆自己那份）。
    m_hashHex.clear();
    QStringList display;
    display.append(summary);
    display.append(QString());
    QStringList checksumLines;
    for (const HashBatchRow &row : rows) {
        display.append(row.name);
        if (row.ok) {
            display.append(QStringLiteral("  %1").arg(row.hex));
            if (!row.sizeText.isEmpty()) {
                display.append(QStringLiteral("  %1").arg(row.sizeText));
            }
            // 标准校验和格式：一行一个「摘要 文件名」。导出/复制都走这一份文本。
            checksumLines.append(QStringLiteral("%1  %2").arg(row.hex, row.name));
        } else {
            display.append(QStringLiteral("  没算成：%1")
                               .arg(row.error.isEmpty() ? QStringLiteral("（没给原因）") : row.error));
        }
        display.append(QString());
    }
    m_hashBatchText =
        checksumLines.isEmpty() ? QString() : checksumLines.join(QLatin1Char('\n')) + QLatin1Char('\n');
    m_hashResult->setText(display.join(QLatin1Char('\n')).trimmed());
    if (m_hashCopyAll != nullptr) {
        m_hashCopyAll->setEnabled(!m_hashBatchText.isEmpty());
    }
    if (m_hashExport != nullptr) {
        m_hashExport->setEnabled(!m_hashBatchText.isEmpty());
    }
}

QString ToolboxPage::hashBatchText() const { return m_hashBatchText; }

void ToolboxPage::setHashProgressNote(const QString &text) {
    if (m_hashProgress != nullptr) {
        m_hashProgress->setText(text);
    }
}

void ToolboxPage::setHashProgress(qint64 done, qint64 total) {    if (m_hashProgress == nullptr) {
        return;
    }
    if (total <= 0) {
        m_hashProgress->setText(QStringLiteral("已读取 %1").arg(done));
        return;
    }
    m_hashProgress->setText(QStringLiteral("已读取 %1 / %2（%3%）")
                                .arg(done)
                                .arg(total)
                                .arg(total > 0 ? done * 100 / total : 0));
}

void ToolboxPage::setHashStaleNote(const QString &text) {
    if (m_hashNotice == nullptr) {
        return;
    }
    m_hashNotice->setText(text);
    m_hashNotice->setVisible(!text.isEmpty());
}

void ToolboxPage::setHashBusy(bool busy) {
    if (m_hashRun != nullptr) {
        m_hashRun->setEnabled(!busy);
        m_hashRun->setText(busy ? QStringLiteral("正在计算…") : QStringLiteral("计算"));
    }
    if (m_hashCancel != nullptr) {
        m_hashCancel->setEnabled(busy);
    }
    if (!busy && m_hashProgress != nullptr) {
        m_hashProgress->clear();
    }
}

// ---------------------------------------------------------------------------
// 六、信标计算
// ---------------------------------------------------------------------------

QWidget *ToolboxPage::buildBeaconPanel() {
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    addPanelHeader(layout, panel, Tool::Beacon,
                   QStringLiteral("（底座方块数、矿物折算与效果范围；材质只影响折算，不影响效果。）"));

    auto *form = new QHBoxLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(8);
    form->addWidget(makeFieldLabel(panel, QStringLiteral("现在已有")));
    m_beaconFrom = new QSpinBox(panel);
    m_beaconFrom->setObjectName(beaconFromSpinName());
    m_beaconFrom->setRange(0, 4);
    m_beaconFrom->setValue(0);
    m_beaconFrom->setSuffix(QStringLiteral(" 级"));
    m_beaconFrom->setToolTip(QStringLiteral("0 表示还没搭；这一格只影响「还要再加多少块」"));
    form->addWidget(m_beaconFrom);
    form->addWidget(makeFieldLabel(panel, QStringLiteral("想搭到")));
    m_beaconLevel = new QSpinBox(panel);
    m_beaconLevel->setObjectName(beaconLevelSpinName());
    m_beaconLevel->setRange(1, 4);
    m_beaconLevel->setValue(4);
    m_beaconLevel->setSuffix(QStringLiteral(" 级"));
    form->addWidget(m_beaconLevel);
    form->addStretch(1);
    layout->addLayout(form);

    // 第二排：底座材质 + 同一座金字塔上放几颗（「矿物折算」「多信标共用底座」）。
    auto *form2 = new QHBoxLayout;
    form2->setContentsMargins(0, 0, 0, 0);
    form2->setSpacing(8);
    form2->addWidget(makeFieldLabel(panel, QStringLiteral("底座方块")));
    m_beaconMaterial = new QComboBox(panel);
    m_beaconMaterial->setObjectName(beaconMaterialComboName());
    m_beaconMaterial->setToolTip(QStringLiteral("只影响折成多少个锭或宝石。"));
    form2->addWidget(m_beaconMaterial);
    form2->addWidget(makeFieldLabel(panel, QStringLiteral("信标颗数")));
    m_beaconCount = new QSpinBox(panel);
    m_beaconCount->setObjectName(beaconCountSpinName());
    m_beaconCount->setRange(1, 9);
    m_beaconCount->setValue(1);
    m_beaconCount->setSuffix(QStringLiteral(" 颗"));
    m_beaconCount->setToolTip(
        QStringLiteral("多颗信标挨着摆就能共用底座（重叠的方块只算一次，按最省的那几种摆法算；"
                       "这里最多算 9 颗）。"));
    form2->addWidget(m_beaconCount);
    form2->addStretch(1);
    layout->addLayout(form2);

    layout->addWidget(makeSectionTitle(panel, QStringLiteral("算出来的结果")));
    m_beaconResult = new QLabel(panel);
    m_beaconResult->setObjectName(beaconResultName());
    m_beaconResult->setWordWrap(true);
    m_beaconResult->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_beaconResult->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_beaconResult->setMinimumHeight(140);
    layout->addWidget(m_beaconResult);
    addResultCopy(layout, m_beaconResult);
    layout->addWidget(makeSectionTitle(panel, QStringLiteral("光柱配色 / 现代 Java 版")));
    auto *glassRow = new QHBoxLayout;
    auto *glassColor = new QComboBox(panel);
    glassColor->setObjectName(QStringLiteral("beamGlassColor"));
    auto *glassAdd = new QPushButton(QStringLiteral("添加玻璃"), panel);
    glassAdd->setObjectName(QStringLiteral("beamGlassAdd"));
    Theme::markButton(glassAdd, Theme::ButtonKind::Neutral);
    glassAdd->setAutoDefault(false);
    glassRow->addWidget(glassColor); glassRow->addWidget(glassAdd); glassRow->addStretch();
    layout->addLayout(glassRow);
    auto *glassBody = new QHBoxLayout;
    auto *glassList = new QListWidget(panel);
    glassList->setObjectName(QStringLiteral("beamGlassList"));
    glassList->setMaximumHeight(150);
    glassList->setToolTip(QStringLiteral("从下往上排列；颜色按实际顺序逐层混合。最多 64 层。"));
    glassBody->addWidget(glassList, 1);
    auto *beam = new QLabel(panel);
    beam->setObjectName(QStringLiteral("beamColorPreview"));
    beam->setFixedSize(64, 150);
    glassBody->addWidget(beam);
    layout->addLayout(glassBody);
    const auto recalculate = [this, glassList] {
        QList<int> colors;
        for (int i = 0; i < glassList->count(); ++i) colors.append(glassList->item(i)->data(Qt::UserRole).toInt());
        emit beamRequested(colors);
    };
    connect(glassAdd, &QPushButton::clicked, this, [glassList, glassColor, recalculate] {
        if (glassList->count() >= 64 || glassColor->currentIndex() < 0) return;
        auto *item = new QListWidgetItem(glassColor->currentText() + QStringLiteral("玻璃"), glassList);
        item->setData(Qt::UserRole, glassColor->currentIndex());
        glassList->setCurrentItem(item); recalculate();
    });
    auto *glassActions = new QHBoxLayout;
    for (int action = 0; action < 3; ++action) {
        auto *button = new QPushButton(QStringList{QStringLiteral("上移"), QStringLiteral("下移"), QStringLiteral("删除")}[action], panel);
        button->setObjectName(QStringLiteral("beamGlassAction%1").arg(action));
        Theme::markButton(button, Theme::ButtonKind::Ghost); button->setAutoDefault(false);
        glassActions->addWidget(button);
        connect(button, &QPushButton::clicked, this, [glassList, action, recalculate] {
            const int row = glassList->currentRow(); if (row < 0) return;
            if (action == 2) delete glassList->takeItem(row);
            else {
                const int to = row + (action == 0 ? -1 : 1);
                if (to < 0 || to >= glassList->count()) return;
                auto *item = glassList->takeItem(row); glassList->insertItem(to, item); glassList->setCurrentRow(to);
            }
            recalculate();
        });
    }
    glassActions->addStretch(); layout->addLayout(glassActions);
    auto *beamReport = new QLabel(QStringLiteral("未放置染色玻璃：白色光柱。"), panel);
    beamReport->setObjectName(QStringLiteral("beamColorReport")); beamReport->setWordWrap(true);
    beamReport->setTextFormat(Qt::PlainText); layout->addWidget(beamReport); addResultCopy(layout, beamReport);
    layout->addWidget(makeMutedLabel(panel, QStringLiteral("beamScopeNotice")));
    panel->findChild<QLabel *>(QStringLiteral("beamScopeNotice"))->setText(QStringLiteral("玻璃与玻璃板同色；光柱图仅示意分段颜色。"));
    layout->addStretch(1);

    const auto emitNow = [this]() {
        emit beaconRequested(m_beaconFrom->value(), m_beaconLevel->value(),
                             m_beaconMaterial->currentIndex(), m_beaconCount->value());
    };
    connect(m_beaconFrom, &QSpinBox::valueChanged, this, [emitNow](int) { emitNow(); });
    connect(m_beaconLevel, &QSpinBox::valueChanged, this, [emitNow](int) { emitNow(); });
    connect(m_beaconCount, &QSpinBox::valueChanged, this, [emitNow](int) { emitNow(); });
    connect(m_beaconMaterial, &QComboBox::currentIndexChanged, this, [emitNow](int) { emitNow(); });
    return panel;
}

void ToolboxPage::setBeamColors(const QStringList &names) {
    auto *combo = findChild<QComboBox *>(QStringLiteral("beamGlassColor"));
    if (combo != nullptr) { combo->clear(); combo->addItems(names); }
}

void ToolboxPage::setBeamLayers(const QList<int> &colors) {
    auto *list = findChild<QListWidget *>(QStringLiteral("beamGlassList"));
    auto *combo = findChild<QComboBox *>(QStringLiteral("beamGlassColor"));
    list->clear();
    for (int color : colors.mid(0, 64)) {
        if (color < 0 || color >= combo->count()) continue;
        auto *item = new QListWidgetItem(combo->itemText(color) + QStringLiteral("玻璃"), list);
        item->setData(Qt::UserRole, color);
    }
    emit beamRequested(colors.mid(0, 64));
}
void ToolboxPage::setBeamResult(const QList<QColor> &segments, const QString &report) {
    auto *label = findChild<QLabel *>(QStringLiteral("beamColorReport"));
    if (label != nullptr) { label->setText(report); updateResultCopy(label, report); }
    auto *preview = findChild<QLabel *>(QStringLiteral("beamColorPreview"));
    if (preview == nullptr) return;
    QPixmap image(64, 150); image.fill(Qt::transparent); QPainter painter(&image);
    const int count = qMax(1, int(segments.size()));
    for (int i = 0; i < count; ++i) {
        const int top = 150 - (i + 1) * 150 / count, bottom = 150 - i * 150 / count;
        painter.fillRect(12, top, 40, bottom - top, segments.isEmpty() ? QColor(Qt::white) : segments[i]);
    }
    painter.end(); preview->setPixmap(image);
}

void ToolboxPage::setBeaconMaterials(const QStringList &names) {
    if (m_beaconMaterial == nullptr) return;
    const QSignalBlocker blocker(m_beaconMaterial);
    m_beaconMaterial->clear();
    m_beaconMaterial->addItems(names);
    if (m_beaconMaterial->count() > 0 && m_beaconMaterial->currentIndex() < 0) {
        m_beaconMaterial->setCurrentIndex(0);
    }
}

int ToolboxPage::beaconMaterialIndex() const {
    return m_beaconMaterial != nullptr ? m_beaconMaterial->currentIndex() : -1;
}

int ToolboxPage::beaconCount() const {
    return m_beaconCount != nullptr ? m_beaconCount->value() : 1;
}

void ToolboxPage::setBeaconLevels(int fromLevel, int targetLevel) {
    if (m_beaconFrom != nullptr) {
        const QSignalBlocker blocker(m_beaconFrom);
        m_beaconFrom->setValue(fromLevel);
    }
    if (m_beaconLevel != nullptr) {
        const QSignalBlocker blocker(m_beaconLevel);
        m_beaconLevel->setValue(targetLevel);
    }
}

int ToolboxPage::beaconFromLevel() const {
    return m_beaconFrom != nullptr ? m_beaconFrom->value() : 0;
}

int ToolboxPage::beaconTargetLevel() const {
    return m_beaconLevel != nullptr ? m_beaconLevel->value() : 1;
}

void ToolboxPage::setBeaconResult(const QString &text) {
    if (m_beaconResult != nullptr) {
        m_beaconResult->setText(text);
        updateResultCopy(m_beaconResult, text);
    }
}

// ---------------------------------------------------------------------------
// 七、酿造助手
// ---------------------------------------------------------------------------

QWidget *ToolboxPage::buildBrewingPanel() {
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    addPanelHeader(layout, panel, Tool::Brewing,
                   QStringLiteral("（红石粉和荧石粉在游戏里二选一；龙息只能加在喷溅药水上。）"));

    auto *form = new QHBoxLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(8);
    form->addWidget(makeFieldLabel(panel, QStringLiteral("要做")));
    m_brewingPotion = new QComboBox(panel);
    m_brewingPotion->setObjectName(brewingPotionComboName());
    form->addWidget(m_brewingPotion);
    m_brewingExtended = new QCheckBox(QStringLiteral("延时（红石）"), panel);
    m_brewingExtended->setObjectName(brewingExtendedName());
    form->addWidget(m_brewingExtended);
    m_brewingLevel2 = new QCheckBox(QStringLiteral("升一级（荧石）"), panel);
    m_brewingLevel2->setObjectName(brewingLevel2Name());
    form->addWidget(m_brewingLevel2);
    form->addWidget(makeFieldLabel(panel, QStringLiteral("形态")));
    m_brewingForm = new QComboBox(panel);
    m_brewingForm->setObjectName(brewingFormComboName());
    form->addWidget(m_brewingForm);
    form->addWidget(makeFieldLabel(panel, QStringLiteral("目标版本")));
    auto *version = new QComboBox(panel);
    version->setObjectName(brewingVersionComboName());
    version->setToolTip(QStringLiteral("按药水首次出现的版本提示；已经选的药水不会被改动。"));
    form->addWidget(version);
    form->addWidget(makeFieldLabel(panel, QStringLiteral("瓶数")));
    m_brewingBottles = new QSpinBox(panel);
    m_brewingBottles->setObjectName(brewingBottlesSpinName());
    m_brewingBottles->setRange(1, 64);
    m_brewingBottles->setValue(1);
    m_brewingBottles->setSuffix(QStringLiteral(" 瓶"));
    m_brewingBottles->setToolTip(QStringLiteral("一次酿造最多 3 瓶；材料与次数按这个瓶数算"));
    form->addWidget(m_brewingBottles);
    form->addStretch(1);
    layout->addLayout(form);

    layout->addWidget(makeSectionTitle(panel, QStringLiteral("一步一步")));
    m_brewingResult = new QLabel(panel);
    m_brewingResult->setObjectName(brewingResultName());
    m_brewingResult->setWordWrap(true);
    m_brewingResult->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_brewingResult->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_brewingResult->setMinimumHeight(180);
    layout->addWidget(m_brewingResult, 1);
    addResultCopy(layout, m_brewingResult);

    const auto emitNow = [this]() {
        emit brewingRequested(m_brewingPotion->currentIndex(), m_brewingExtended->isChecked(),
                              m_brewingLevel2->isChecked(), m_brewingForm->currentIndex(),
                              m_brewingBottles->value());
    };
    connect(m_brewingPotion, &QComboBox::currentIndexChanged, this,
            [emitNow](int) { emitNow(); });
    connect(m_brewingExtended, &QCheckBox::toggled, this, [emitNow](bool) { emitNow(); });
    connect(m_brewingLevel2, &QCheckBox::toggled, this, [emitNow](bool) { emitNow(); });
    connect(m_brewingForm, &QComboBox::currentIndexChanged, this,
            [emitNow](int) { emitNow(); });
    connect(m_brewingBottles, &QSpinBox::valueChanged, this, [emitNow](int) { emitNow(); });
    connect(version, &QComboBox::currentIndexChanged, this, [emitNow](int) { emitNow(); });
    return panel;
}

void ToolboxPage::setBrewingPotions(const QStringList &names) {
    if (m_brewingPotion == nullptr) {
        return;
    }
    // 程序摆的那一份**不发**信号（controller 填完表之后会自己算一次）。
    const QSignalBlocker blocker(m_brewingPotion);
    m_brewingPotion->clear();
    m_brewingPotion->addItems(names);
}

void ToolboxPage::setBrewingForms(const QStringList &names) {
    if (m_brewingForm == nullptr) {
        return;
    }
    const QSignalBlocker blocker(m_brewingForm);
    m_brewingForm->clear();
    m_brewingForm->addItems(names);
}

void ToolboxPage::setBrewingVersions(const QStringList &versions) {
    auto *combo = findChild<QComboBox *>(brewingVersionComboName());
    if (combo == nullptr) {
        return;
    }
    const QSignalBlocker blocker(combo);
    combo->clear();
    for (const auto &version : versions) {
        combo->addItem(version.isEmpty() ? QStringLiteral("不限版本") : version, version);
    }
}

QString ToolboxPage::brewingVersion() const {
    const auto *combo = findChild<QComboBox *>(brewingVersionComboName());
    return combo != nullptr ? combo->currentData().toString() : QString();
}

void ToolboxPage::setBrewingResult(const QString &text) {
    if (m_brewingResult != nullptr) {
        m_brewingResult->setText(text);
        updateResultCopy(m_brewingResult, text);
    }
}

int ToolboxPage::brewingPotionIndex() const {
    return m_brewingPotion != nullptr ? m_brewingPotion->currentIndex() : -1;
}

bool ToolboxPage::brewingExtended() const {
    return m_brewingExtended != nullptr && m_brewingExtended->isChecked();
}

bool ToolboxPage::brewingLevel2() const {
    return m_brewingLevel2 != nullptr && m_brewingLevel2->isChecked();
}

int ToolboxPage::brewingFormIndex() const {
    return m_brewingForm != nullptr ? m_brewingForm->currentIndex() : 0;
}

int ToolboxPage::brewingBottles() const {
    return m_brewingBottles != nullptr ? m_brewingBottles->value() : 1;
}

// ---------------------------------------------------------------------------
// 八、旗帜设计
// ---------------------------------------------------------------------------

QWidget *ToolboxPage::buildBannerPanel() {
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    addPanelHeader(layout, panel, Tool::Banner,
                   QStringLiteral("（原版最多叠 6 层；预览读取本地游戏贴图，不联网下载素材。）"));

    auto *designRow = new QHBoxLayout;
    auto *version = new QComboBox(panel);
    version->setObjectName(QStringLiteral("bannerGameVersion"));
    version->setToolTip(QStringLiteral("按图案首次出现的版本筛选；已有图层不会被删除。"));
    designRow->addWidget(makeFieldLabel(panel, QStringLiteral("目标版本")));
    designRow->addWidget(version);
    for (bool save : {true, false}) {
        auto *button = new QPushButton(save ? QStringLiteral("保存 / 导出设计…") : QStringLiteral("载入 / 导入设计…"), panel);
        button->setObjectName(save ? QStringLiteral("bannerSaveDesign") : QStringLiteral("bannerLoadDesign"));
        Theme::markButton(button, Theme::ButtonKind::Neutral);
        designRow->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, save] {
            if (save) emit bannerSaveRequested(); else emit bannerLoadRequested();
        });
    }
    designRow->addStretch(); layout->addLayout(designRow);
    connect(version, &QComboBox::currentIndexChanged, this, [this] { emitBannerNow(); });

    auto *form = new QHBoxLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(8);
    form->addWidget(makeFieldLabel(panel, QStringLiteral("底色")));
    m_bannerBase = new QComboBox(panel);
    m_bannerBase->setObjectName(bannerBaseComboName());
    form->addWidget(m_bannerBase);
    form->addWidget(makeFieldLabel(panel, QStringLiteral("图案")));
    m_bannerPattern = new QComboBox(panel);
    m_bannerPattern->setObjectName(bannerPatternComboName());
    form->addWidget(m_bannerPattern);
    form->addWidget(makeFieldLabel(panel, QStringLiteral("颜色")));
    m_bannerColor = new QComboBox(panel);
    m_bannerColor->setObjectName(bannerColorComboName());
    form->addWidget(m_bannerColor);
    m_bannerAdd = new QPushButton(QStringLiteral("加一层"), panel);
    m_bannerAdd->setObjectName(bannerAddName());
    m_bannerAdd->setToolTip(QStringLiteral("把所选图案和颜色叠到最上面（最多 6 层）"));
    Theme::markButton(m_bannerAdd, Theme::ButtonKind::Primary);
    form->addWidget(m_bannerAdd);
    form->addStretch(1);
    layout->addLayout(form);

    auto *loadTextures = new QPushButton(QStringLiteral("加载游戏 JAR / 资源包贴图…"), panel);
    loadTextures->setObjectName(QStringLiteral("bannerLoadTextures"));
    Theme::markButton(loadTextures, Theme::ButtonKind::Neutral); loadTextures->setAutoDefault(false);
    layout->addWidget(loadTextures, 0, Qt::AlignLeft);
    connect(loadTextures, &QPushButton::clicked, this, &ToolboxPage::bannerTexturesRequested);
    auto *bannerPreviewRow = new QHBoxLayout;
    auto *bannerPreview = new QLabel(panel);
    bannerPreview->setObjectName(QStringLiteral("bannerVisualPreview"));
    bannerPreview->setFixedSize(96, 192); bannerPreview->setAlignment(Qt::AlignCenter);
    bannerPreviewRow->addWidget(bannerPreview);
    auto *bannerNotice = new QLabel(QStringLiteral("加载原版客户端 JAR 后显示图案缩略图与二维叠层预览。"), panel);
    bannerNotice->setObjectName(QStringLiteral("bannerVisualNotice")); bannerNotice->setWordWrap(true);
    bannerNotice->setMinimumWidth(0); bannerNotice->setTextFormat(Qt::PlainText);
    layout->addWidget(bannerNotice);
    layout->addWidget(makeSectionTitle(panel, QStringLiteral("已经排好的层（从下往上）")));
    m_bannerList = new QListWidget(panel);
    m_bannerList->setObjectName(bannerListName());
    m_bannerList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_bannerList->setDragDropMode(QAbstractItemView::InternalMove);
    m_bannerList->setDefaultDropAction(Qt::MoveAction);
    m_bannerList->setDropIndicatorShown(true);
    m_bannerList->setToolTip(QStringLiteral("拖动调整图层顺序；列表末尾的图层覆盖前面的图层。"));
    bannerPreviewRow->addWidget(m_bannerList, 1);
    layout->addLayout(bannerPreviewRow, 1);

    auto *buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(8);
    m_bannerUp = new QPushButton(QStringLiteral("上移"), panel);
    m_bannerUp->setObjectName(bannerUpName());
    Theme::markButton(m_bannerUp, Theme::ButtonKind::Ghost);
    m_bannerDown = new QPushButton(QStringLiteral("下移"), panel);
    m_bannerDown->setObjectName(bannerDownName());
    Theme::markButton(m_bannerDown, Theme::ButtonKind::Ghost);
    m_bannerRemove = new QPushButton(QStringLiteral("删除这一层"), panel);
    m_bannerRemove->setObjectName(bannerRemoveName());
    Theme::markButton(m_bannerRemove, Theme::ButtonKind::Ghost);
    m_bannerUndo = new QPushButton(QStringLiteral("撤销"), panel);
    m_bannerUndo->setObjectName(bannerUndoName());
    m_bannerUndo->setToolTip(QStringLiteral("把列表退回上一步（最多留最近二十步）"));
    Theme::markButton(m_bannerUndo, Theme::ButtonKind::Ghost);
    buttons->addWidget(m_bannerUp);
    buttons->addWidget(m_bannerDown);
    buttons->addWidget(m_bannerRemove);
    buttons->addWidget(m_bannerUndo);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    {
        auto *head = new QHBoxLayout;
        head->setContentsMargins(0, 0, 0, 0);
        head->setSpacing(8);
        head->addWidget(makeSectionTitle(panel, QStringLiteral("算出来的结果")));
        m_bannerCopy = new QPushButton(QStringLiteral("复制指令"), panel);
        m_bannerCopy->setObjectName(QStringLiteral("toolboxBannerCopy"));
        m_bannerCopy->setToolTip(QStringLiteral("把下面那条 /give 指令复制走（1.13–1.20.4 的写法）"));
        Theme::markButton(m_bannerCopy, Theme::ButtonKind::Neutral);
        m_bannerCopy->setEnabled(false);
        head->addWidget(m_bannerCopy);
        head->addStretch(1);
        layout->addLayout(head);
        connect(m_bannerCopy, &QPushButton::clicked, this,
                [this]() { copyToClipboard(m_bannerCommand); });
    }
    m_bannerResult = new QLabel(panel);
    m_bannerResult->setObjectName(bannerResultName());
    m_bannerResult->setWordWrap(true);
    m_bannerResult->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_bannerResult->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_bannerResult->setMinimumHeight(120);
    layout->addWidget(m_bannerResult, 1);

    const auto pushUndo = [this]() {
        m_bannerUndoStack.append(m_bannerLayers);
        while (m_bannerUndoStack.size() > 20) {
            m_bannerUndoStack.removeFirst();
        }
    };
    connect(m_bannerList->model(), &QAbstractItemModel::rowsMoved, this,
            [this, pushUndo]() {
        if (m_bannerSyncing) return;
        QStringList reordered;
        for (int i = 0; i < m_bannerList->count(); ++i)
            reordered.append(m_bannerList->item(i)->data(Qt::UserRole).toString());
        // Do not rebuild the model inside rowsMoved: the drop handler still owns its indexes.
        for (int i = 0; i < m_bannerList->count(); ++i) {
            const auto parts = reordered.at(i).split(QChar(':'));
            m_bannerList->item(i)->setText(QStringLiteral("第 %1 层：%2（%3）")
                .arg(i + 1).arg(m_bannerPattern->itemText(parts.value(0).toInt()))
                .arg(m_bannerColor->itemText(parts.value(1).toInt())));
        }
        if (reordered == m_bannerLayers) return;
        pushUndo();
        m_bannerLayers = reordered;
        m_bannerUndo->setEnabled(true);
        const int row = m_bannerList->currentRow();
        m_bannerUp->setEnabled(row > 0);
        m_bannerDown->setEnabled(row >= 0 && row + 1 < m_bannerList->count());
        m_bannerRemove->setEnabled(row >= 0);
        emitBannerNow();
    });
    connect(m_bannerBase, &QComboBox::currentIndexChanged, this,
            [this](int) { emitBannerNow(); });
    connect(m_bannerAdd, &QPushButton::clicked, this, [this, pushUndo]() {
        if (m_bannerLayers.size() >= 6 || m_bannerPattern->currentIndex() < 0) return;
        pushUndo();
        m_bannerLayers.append(QStringLiteral("%1:%2")
                                  .arg(m_bannerPattern->currentIndex())
                                  .arg(m_bannerColor->currentIndex()));
        syncBannerList();
        m_bannerList->setCurrentRow(m_bannerLayers.size() - 1);
        emitBannerNow();
    });
    connect(m_bannerUp, &QPushButton::clicked, this, [this, pushUndo]() {
        const int row = m_bannerList->currentRow();
        if (row <= 0 || row >= m_bannerLayers.size()) {
            return;
        }
        pushUndo();
        m_bannerLayers.swapItemsAt(row, row - 1);
        syncBannerList();
        m_bannerList->setCurrentRow(row - 1);
        emitBannerNow();
    });
    connect(m_bannerDown, &QPushButton::clicked, this, [this, pushUndo]() {
        const int row = m_bannerList->currentRow();
        if (row < 0 || row + 1 >= m_bannerLayers.size()) {
            return;
        }
        pushUndo();
        m_bannerLayers.swapItemsAt(row, row + 1);
        syncBannerList();
        m_bannerList->setCurrentRow(row + 1);
        emitBannerNow();
    });
    connect(m_bannerRemove, &QPushButton::clicked, this, [this, pushUndo]() {
        const int row = m_bannerList->currentRow();
        if (row < 0 || row >= m_bannerLayers.size()) {
            return;
        }
        pushUndo();
        m_bannerLayers.removeAt(row);
        syncBannerList();
        emitBannerNow();
    });
    connect(m_bannerUndo, &QPushButton::clicked, this, [this]() {
        if (m_bannerUndoStack.isEmpty()) {
            return;
        }
        m_bannerLayers = m_bannerUndoStack.takeLast();
        syncBannerList();
        emitBannerNow();
    });
    connect(m_bannerList, &QListWidget::currentRowChanged, this, [this](int) {
        if (m_bannerSyncing) {
            return;
        }
        const int row = m_bannerList->currentRow();
        m_bannerUp->setEnabled(row > 0);
        m_bannerDown->setEnabled(row >= 0 && row + 1 < m_bannerList->count());
        m_bannerRemove->setEnabled(row >= 0);
    });
    syncBannerList();
    return panel;
}

void ToolboxPage::syncBannerList() {
    if (m_bannerList == nullptr) {
        return;
    }
    m_bannerSyncing = true;
    const int row = m_bannerList->currentRow();
    m_bannerList->clear();
    for (int index = 0; index < m_bannerLayers.size(); ++index) {
        const QStringList parts = m_bannerLayers.at(index).split(QChar(':'));
        const int pattern = parts.value(0).toInt();
        const int color = parts.value(1).toInt();
        const QString patternName =
            m_bannerPattern != nullptr ? m_bannerPattern->itemText(pattern) : QString();
        const QString colorName =
            m_bannerColor != nullptr ? m_bannerColor->itemText(color) : QString();
        m_bannerList->addItem(QStringLiteral("第 %1 层：%2（%3）")
                                  .arg(index + 1)
                                  .arg(patternName)
                                  .arg(colorName));
        m_bannerList->item(index)->setData(Qt::UserRole, m_bannerLayers.at(index));
    }
    if (row >= 0 && row < m_bannerList->count()) {
        m_bannerList->setCurrentRow(row);
    }
    m_bannerSyncing = false;

    const int current = m_bannerList->currentRow();
    if (m_bannerUp != nullptr) {
        m_bannerUp->setEnabled(current > 0);
    }
    if (m_bannerDown != nullptr) {
        m_bannerDown->setEnabled(current >= 0 && current + 1 < m_bannerList->count());
    }
    if (m_bannerRemove != nullptr) {
        m_bannerRemove->setEnabled(current >= 0);
    }
    if (m_bannerUndo != nullptr) {
        m_bannerUndo->setEnabled(!m_bannerUndoStack.isEmpty());
    }
}

void ToolboxPage::emitBannerNow() {
    emit bannerRequested(m_bannerBase != nullptr ? m_bannerBase->currentIndex() : -1,
                         m_bannerLayers);
}

void ToolboxPage::setBannerColors(const QStringList &names) {
    if (m_bannerBase == nullptr) {
        return;
    }
    const QSignalBlocker blockBase(m_bannerBase);
    const QSignalBlocker blockColor(m_bannerColor);
    m_bannerBase->clear();
    m_bannerBase->addItems(names);
    m_bannerColor->clear();
    m_bannerColor->addItems(names);
    syncBannerList();  // 行里那截颜色名跟着换一套说法
}

void ToolboxPage::setBannerPatterns(const QStringList &names) {
    if (m_bannerPattern == nullptr) {
        return;
    }
    const QSignalBlocker blocker(m_bannerPattern);
    m_bannerPattern->clear();
    m_bannerPattern->addItems(names);
    syncBannerList();
}

void ToolboxPage::setBannerResult(const QString &text) {
    if (m_bannerResult != nullptr) {
        m_bannerResult->setText(text);
    }
}

void ToolboxPage::setBannerPreview(const QImage &image, const QString &notice) {
    auto *preview = findChild<QLabel *>(QStringLiteral("bannerVisualPreview"));
    if (preview != nullptr) {
        if (image.isNull()) preview->clear();
        else preview->setPixmap(QPixmap::fromImage(image).scaled(preview->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
    }
    auto *label = findChild<QLabel *>(QStringLiteral("bannerVisualNotice"));
    if (label != nullptr) label->setText(notice.isEmpty() ? QStringLiteral("二维旗面预览 / 按贴图实际层序合成。") : notice);
}
void ToolboxPage::setBannerPatternIcons(const QList<QIcon> &icons) {
    if (m_bannerPattern == nullptr) return;
    m_bannerPattern->setIconSize(QSize(20, 40));
    for (int i = 0; i < m_bannerPattern->count(); ++i) m_bannerPattern->setItemIcon(i, icons.value(i));
}

void ToolboxPage::setBannerCommand(const QString &command) {
    m_bannerCommand = command;
    if (m_bannerCopy != nullptr) {
        m_bannerCopy->setEnabled(!command.isEmpty());
    }
}

int ToolboxPage::bannerBaseColor() const {
    return m_bannerBase != nullptr ? m_bannerBase->currentIndex() : -1;
}

QStringList ToolboxPage::bannerLayers() const { return m_bannerLayers; }

QString ToolboxPage::bannerVersion() const {
    return findChild<QComboBox *>(QStringLiteral("bannerGameVersion"))->currentData().toString();
}
void ToolboxPage::setBannerVersions(const QStringList &versions) {
    auto *combo = findChild<QComboBox *>(QStringLiteral("bannerGameVersion"));
    const QSignalBlocker blocker(combo);
    combo->clear();
    for (const auto &version : versions)
        combo->addItem(version.isEmpty() ? QStringLiteral("不限版本") : version, version);
}
void ToolboxPage::setBannerDesign(int base, const QStringList &layers, const QString &version) {
    auto *combo = findChild<QComboBox *>(QStringLiteral("bannerGameVersion"));
    const QSignalBlocker blockBase(m_bannerBase), blockVersion(combo);
    m_bannerBase->setCurrentIndex(base);
    combo->setCurrentIndex(qMax(0, combo->findData(version)));
    setBannerLayers(layers);
}
void ToolboxPage::setBannerPatternAvailability(const QList<bool> &available) {
    auto *model = qobject_cast<QStandardItemModel *>(m_bannerPattern->model());
    auto *view = qobject_cast<QListView *>(m_bannerPattern->view());
    if (!model || !view) return;
    const QSignalBlocker blocker(m_bannerPattern);
    int first = -1;
    for (int i = 0; i < m_bannerPattern->count(); ++i) {
        const bool enabled = available.value(i, true);
        model->item(i)->setEnabled(enabled);
        view->setRowHidden(i, !enabled);
        if (enabled && first < 0) first = i;
    }
    const int current = m_bannerPattern->currentIndex();
    if (current < 0 || !available.value(current, true)) m_bannerPattern->setCurrentIndex(first);
    m_bannerAdd->setEnabled(first >= 0 && m_bannerLayers.size() < 6);
}

void ToolboxPage::setBannerLayers(const QStringList &layers) {
    m_bannerLayers = layers;
    m_bannerUndoStack.clear();
    syncBannerList();
}

// ---------------------------------------------------------------------------
// 九、纹饰搭配
// ---------------------------------------------------------------------------

QWidget *ToolboxPage::buildTrimPanel() {
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 2, 0, 0); layout->setSpacing(6);
    // 标题与那一句总述已经在覆盖页顶上（`toolName()` / `toolSummary()`），这里不重复。
    // 表格下面只留结果那几行：每件选了什么都写在上面三排下拉框里，不再复述一遍。
    auto *body = new QHBoxLayout; body->setSpacing(12);
    auto *left = new QVBoxLayout; left->setSpacing(8);
    auto *form = new QGridLayout; form->setHorizontalSpacing(6); form->setVerticalSpacing(6);
    const QStringList parts{tr("头盔"),tr("胸甲"),tr("护腿"),tr("靴子")};
    const QStringList rows{tr("材质"),tr("纹饰"),tr("材料")};
    for (int row=0; row<3; ++row) form->addWidget(makeFieldLabel(panel,rows[row]),row+1,0);
    for (int slot=0; slot<4; ++slot) {
        auto *heading=makeSectionTitle(panel,parts[slot]); heading->setAlignment(Qt::AlignCenter);
        form->addWidget(heading,0,slot+1); form->setColumnStretch(slot+1,1);
        const QList<QList<QComboBox *> *> groups{&m_trimSlotArmor,&m_trimSlotPattern,&m_trimSlotMaterial};
        const QStringList keys{"trimArmorSlot","trimPatternSlot","trimMaterialSlot"};
        for (int row=0; row<3; ++row) {
            auto *combo=new QComboBox(panel);
            combo->setObjectName(keys[row]+QString::number(slot));
            combo->setMinimumWidth(0); combo->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
            groups[row]->append(combo); form->addWidget(combo,row+1,slot+1);
            connect(combo,&QComboBox::currentIndexChanged,this,[this](int) {
                if (!m_trimSyncing) restoreTrimEquipment(true);
            });
        }
    }
    // 「目标版本」在盔甲格子上方：它管的是下面八格纹饰的可用性（只在结果里提一句，
    // 不悄悄改动已经选好的搭配 —— 与旗帜 / 酿造同一套判据）。
    auto *versionRow = new QHBoxLayout;
    versionRow->setContentsMargins(0, 0, 0, 0);
    versionRow->setSpacing(6);
    versionRow->addWidget(makeFieldLabel(panel, tr("目标版本")));
    auto *trimVersion = new QComboBox(panel);
    trimVersion->setObjectName(trimVersionComboName());
    trimVersion->setToolTip(tr("按纹饰图案 / 材料的首次出现版本提示；已经选好的搭配不会被改动。"));
    versionRow->addWidget(trimVersion);
    versionRow->addStretch(1);
    left->addLayout(versionRow);
    left->addLayout(form);
    m_trimResult=new QLabel(panel); m_trimResult->setObjectName(trimResultName());
    m_trimResult->setWordWrap(true); m_trimResult->setAlignment(Qt::AlignTop|Qt::AlignLeft);
    m_trimResult->setTextInteractionFlags(Qt::TextSelectableByMouse);
    left->addWidget(m_trimResult); addResultCopy(left,m_trimResult); left->addStretch(1);
    body->addLayout(left,2);
    auto *right=new QVBoxLayout; right->setSpacing(4);
    m_trimPreview=new ArmorTrimPreview(panel); right->addWidget(m_trimPreview,1);
    // 披风是选修项：默认不画（纹饰看的是盔甲本身），勾上才把账号现有的那件披上去。
    m_trimCape=new QCheckBox(tr("显示披风"),panel);
    m_trimCape->setObjectName(trimCapeCheckName());
    m_trimCape->setChecked(false);
    // 还没收到账号外观之前一律当"没有披风"：置灰比勾了却什么都不发生好。
    m_trimCape->setEnabled(false);
    m_trimCape->setToolTip(tr("默认不显示；勾上后使用当前账号的披风。"));
    connect(m_trimCape,&QCheckBox::toggled,this,[this](bool visible){ emit trimCapeVisibilityChanged(visible); });
    right->addWidget(m_trimCape);
    m_trimResourceStatus=new QLabel(tr("未载入盔甲资源"),panel);
    m_trimResourceStatus->setObjectName(QStringLiteral("trimResourceStatus"));
    m_trimResourceStatus->setWordWrap(true); right->addWidget(m_trimResourceStatus);
    auto *actions=new QGridLayout; actions->setSpacing(4);
    const QStringList titles{tr("读取本地"),tr("选择 JAR"),tr("下载资源"),tr("取消")};
    for(int i=0;i<titles.size();++i) {
        auto *button=new QPushButton(titles[i],panel);
        button->setObjectName(QStringLiteral("trimResourceAction%1").arg(i)); button->setAutoDefault(false);
        Theme::markButton(button,Theme::ButtonKind::Neutral);
        connect(button,&QPushButton::clicked,this,[this,i]{emit trimResourcesRequested(i);});
        m_trimResourceButtons.append(button); actions->addWidget(button,i/2,i%2);
    }
    m_trimResourceButtons.last()->setEnabled(false); right->addLayout(actions); right->addStretch(0);
    body->addLayout(right,1); layout->addLayout(body,1);

    // Legacy controls are migration adapters only, never part of the visible editor.
    m_trimArmor=new QComboBox(panel); m_trimArmor->setObjectName(trimArmorComboName());
    m_trimPart=new QComboBox(panel); m_trimPart->setObjectName(trimPartComboName());
    m_trimPattern=new QComboBox(panel); m_trimPattern->setObjectName(trimPatternComboName());
    m_trimMaterial=new QComboBox(panel); m_trimMaterial->setObjectName(trimMaterialComboName());
    m_trimPieces=new QSpinBox(panel); m_trimPieces->setObjectName(trimPiecesSpinName());
    m_trimPieces->setRange(1,4); m_trimPieces->setValue(1);
    m_trimWholeSet=new QCheckBox(panel); m_trimWholeSet->setObjectName(trimWholeSetName());
    const QList<QWidget *> legacy{m_trimArmor,m_trimPart,m_trimPattern,m_trimMaterial,m_trimPieces,m_trimWholeSet};
    for(auto *widget:legacy) widget->hide();
    const auto oldChanged=[this] {
        restoreTrimEquipment(false);
        emit trimRequested(trimArmorIndex(),trimPartIndex(),trimPatternIndex(),trimMaterialIndex(),trimPieces(),trimWholeSet());
    };
    for(auto *combo:{m_trimArmor,m_trimPart,m_trimPattern,m_trimMaterial})
        connect(combo,&QComboBox::currentIndexChanged,this,[oldChanged](int){oldChanged();});
    connect(m_trimPieces,&QSpinBox::valueChanged,this,[oldChanged](int){oldChanged();});
    connect(m_trimWholeSet,&QCheckBox::toggled,this,[oldChanged](bool){oldChanged();});
    connect(trimVersion,&QComboBox::currentIndexChanged,this,[oldChanged](int){oldChanged();});
    return panel;
}

QList<ToolboxPage::TrimEquipmentSlot> ToolboxPage::trimEquipment() const {
    QList<TrimEquipmentSlot> result;
    for(int i=0;i<m_trimSlotArmor.size();++i)
        result.append({m_trimSlotArmor[i]->currentIndex()<0 ? -1 : m_trimSlotArmor[i]->currentData().toInt(),
                       m_trimSlotPattern[i]->currentIndex()<0 ? -1 : m_trimSlotPattern[i]->currentData().toInt(),
                       m_trimSlotMaterial[i]->currentData().toInt()});
    return result;
}

void ToolboxPage::restoreTrimEquipment(bool perSlot) {
    if(m_trimSyncing || m_trimSlotArmor.size()!=4) return;
    m_trimSyncing=true;
    const bool turtle=m_trimArmor->count()==7 && trimArmorIndex()==6;
    if(turtle) { const QSignalBlocker blocker(m_trimPart); m_trimPart->setCurrentIndex(0); }
    m_trimPart->setEnabled(!turtle&&!trimWholeSet()); m_trimPieces->setEnabled(!trimWholeSet());
    for(int slot=0;slot<4;++slot) {
        if(!perSlot) {
            const bool equipped=turtle ? slot==0 : trimWholeSet() || trimPartIndex()==slot;
            const QSignalBlocker a(m_trimSlotArmor[slot]),b(m_trimSlotPattern[slot]),c(m_trimSlotMaterial[slot]);
            m_trimSlotArmor[slot]->setCurrentIndex(m_trimSlotArmor[slot]->findData(equipped?trimArmorIndex():-1));
            m_trimSlotPattern[slot]->setCurrentIndex(m_trimSlotPattern[slot]->findData(trimPatternIndex()));
            m_trimSlotMaterial[slot]->setCurrentIndex(m_trimSlotMaterial[slot]->findData(trimMaterialIndex()));
        }
        auto *armor=m_trimSlotArmor[slot];
        if(slot>0 && armor->currentData().toInt()==6) {
            const QSignalBlocker blocker(armor); armor->setCurrentIndex(armor->findData(-1));
        }
        const bool equipped=armor->currentIndex()>=0 && armor->currentData().toInt()>=0;
        m_trimSlotPattern[slot]->setEnabled(equipped);
        m_trimSlotMaterial[slot]->setEnabled(equipped && m_trimSlotPattern[slot]->currentData().toInt()>=0);
    }
    m_trimSyncing=false;
    emit trimEquipmentChanged();
}

void ToolboxPage::setTrimPreview(const SkinModel::Scene &scene) {
    if (m_trimPreview) m_trimPreview->setScene(scene);
}

void ToolboxPage::setTrimCapeAvailable(bool available) {
    if (m_trimCape == nullptr) return;
    // 账号换了 / 披风没载进来：先把勾收回来（不带着一个「勾着却画不出来」的状态），
    // 再决定这一格能不能点。收回时挡信号 —— 那不是用户改的，controller 自己会重画。
    if (!available && m_trimCape->isChecked()) {
        const QSignalBlocker blocker(m_trimCape);
        m_trimCape->setChecked(false);
    }
    m_trimCape->setEnabled(available);
    m_trimCape->setToolTip(available ? tr("默认不显示；勾上后使用当前账号的披风。")
                                     : tr("当前账号没有可用的披风。"));
}

bool ToolboxPage::trimShowCape() const {
    return m_trimCape != nullptr && m_trimCape->isEnabled() && m_trimCape->isChecked();
}

void ToolboxPage::setTrimResourceStatus(const QString &text, bool busy) {
    if (m_trimResourceStatus) m_trimResourceStatus->setText(text);
    for (int i = 0; i < m_trimResourceButtons.size(); ++i)
        m_trimResourceButtons[i]->setEnabled(i == 3 ? busy : !busy);
}

namespace {
void fillTrimSlots(const QList<QComboBox *> &combos, const QStringList &names, const QString &emptyLabel,
                   bool armor = false) {
    for (int slot=0;slot<combos.size();++slot) {
        auto *combo=combos[slot]; const bool had=combo->count()>0;
        const int previous=combo->currentData().toInt(); const QSignalBlocker blocker(combo);
        combo->clear();
        if(!emptyLabel.isEmpty()) combo->addItem(emptyLabel,-1);
        for(int i=0;i<names.size();++i) combo->addItem(names[i],i);
        if(armor && slot>0 && names.size()==7) {
            if(auto *model=qobject_cast<QStandardItemModel *>(combo->model()))
                if(auto *item=model->item(combo->findData(6))) item->setEnabled(false);
        }
        combo->setCurrentIndex(combo->findData(had?previous:0));
    }
}
}

void ToolboxPage::setTrimArmorMaterials(const QStringList &names) {
    fillTrimSlots(m_trimSlotArmor,names,tr("未装备"),true);
    if (m_trimArmor == nullptr) {
        return;
    }
    const QSignalBlocker blocker(m_trimArmor);
    m_trimArmor->clear();
    m_trimArmor->addItems(names);
}

void ToolboxPage::setTrimParts(const QStringList &names) {
    if (m_trimPart == nullptr) {
        return;
    }
    const QSignalBlocker blocker(m_trimPart);
    m_trimPart->clear();
    m_trimPart->addItems(names);
}

void ToolboxPage::setTrimPatterns(const QStringList &names) {
    fillTrimSlots(m_trimSlotPattern,names,tr("无纹饰"));
    if (m_trimPattern == nullptr) {
        return;
    }
    const QSignalBlocker blocker(m_trimPattern);
    m_trimPattern->clear();
    m_trimPattern->addItems(names);
}

void ToolboxPage::setTrimMaterials(const QStringList &names) {
    fillTrimSlots(m_trimSlotMaterial,names,QString());
    if (m_trimMaterial == nullptr) {
        return;
    }
    const QSignalBlocker blocker(m_trimMaterial);
    m_trimMaterial->clear();
    m_trimMaterial->addItems(names);
}

void ToolboxPage::setTrimVersions(const QStringList &versions) {
    auto *combo = findChild<QComboBox *>(trimVersionComboName());
    if (combo == nullptr) {
        return;
    }
    const QSignalBlocker blocker(combo);
    combo->clear();
    for (const auto &version : versions) {
        combo->addItem(version.isEmpty() ? tr("不限版本") : version, version);
    }
}

QString ToolboxPage::trimVersion() const {
    const auto *combo = findChild<QComboBox *>(trimVersionComboName());
    return combo != nullptr ? combo->currentData().toString() : QString();
}

void ToolboxPage::setTrimResult(const QString &text) {
    if (m_trimResult != nullptr) {
        m_trimResult->setText(text);
        updateResultCopy(m_trimResult, text);
    }
}

bool ToolboxPage::trimWholeSet() const {
    return m_trimWholeSet != nullptr && m_trimWholeSet->isChecked();
}

int ToolboxPage::trimArmorIndex() const {
    return m_trimArmor != nullptr ? m_trimArmor->currentIndex() : -1;
}

int ToolboxPage::trimPartIndex() const {
    return m_trimPart != nullptr ? m_trimPart->currentIndex() : -1;
}

int ToolboxPage::trimPatternIndex() const {
    return m_trimPattern != nullptr ? m_trimPattern->currentIndex() : -1;
}

int ToolboxPage::trimMaterialIndex() const {
    return m_trimMaterial != nullptr ? m_trimMaterial->currentIndex() : -1;
}

int ToolboxPage::trimPieces() const {
    return m_trimPieces != nullptr ? m_trimPieces->value() : 1;
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
