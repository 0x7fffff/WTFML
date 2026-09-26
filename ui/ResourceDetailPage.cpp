/**
 * @file ResourceDetailPage.cpp
 * @brief Implements the resource detail page (see ResourceDetailPage.hpp for the whole picture).
 * @author 0x7fffff
 * @date 2026-09-26
 */

#include "ResourceDetailPage.hpp"

#include <QFrame>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShowEvent>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "MainWindowUI.hpp"
#include "RowActivation.hpp"
#include "VersionTreeDelegate.hpp"

namespace {

// 这一页自己的几个尺寸。**都不跟着窗口走**：嵌进覆盖层那一层的时候这一页拿到的是内容区
// 的整幅宽（见 OverlayHost::execEmbedded），"整页"看着才像一页；单独拿出来用时构造函数
// 里那个 resize() 只是给验证工程一个像样的初始大小。
constexpr int kPageWidth = 820;
constexpr int kPageHeight = 560;
constexpr int kCardPadding = 14;
/// 头卡上那个图标多大（正方形）。PCL 那一页也是这么大一圈。
constexpr int kIconSize = 64;
/// chip 那一行的高。**比 chip 本体高**：横向滚动条出现时要占掉十几像素，
/// 不留余量的话那一条会把 chip 吃掉半截。
constexpr int kChipRowHeight = 40;
constexpr int kChipHeight = 24;

/// 这条标签**现在会画出来的字**（截断之后的那一串）。paintEvent 和验证工程读的那个读数
/// 都走这一份 —— 两处各算一遍的话，"量到的那串"和"屏幕上的那串"迟早不是一回事。
///
/// NOTE: 它得摆在 ElidedLabel **前面**：成员函数的函数体是按"类定义之后"那一处编译的，
///       但名字查找只认**写在类前面**的那些声明。
QString elidedFor(const QLabel *label);

/// 放不下时把尾巴截成省略号的 QLabel。
///
/// **为什么非它不可**：资源名和简介都能长过这一格（CurseForge 上的简介常常是整段话），
/// 而 QLabel 在控件比文字窄的时候是**硬裁**的 —— 最后一个字被切掉半个、右边什么都没有，
/// 看着像画坏了（`VersionTreeDelegate::paint()` 里那条 2026-09-26 的注释讲的是同一件事）。
/// QLabel 自己没有 elide 相关的 API（翻遍 qlabel.h 都没有），所以只能自己接 paintEvent。
///
/// NOTE: 和 `MainWindowUI.cpp` 里底栏用的那一个**是同一种东西、不是同一份代码**（那个在
///       它的匿名命名空间里，这一层看不见）。这边只留了这两行字需要的那两档：字号与字重
///       由外面设，截哪一头固定是尾巴。
class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(QWidget *parent = nullptr) : QLabel(parent) {}

    /// 宽度报 **0**：这一格**可以被压窄**，压窄之后字被截成省略号（见 paintEvent）。
    ///
    /// NOTE: 不覆写的话 QLabel 报的是**整句话的宽度**（单行标签的 minimumSizeHint 就是它），
    ///       而"最小宽度"是布局里唯一一个能把父窗口顶开的量：症状是这一页的最小宽度等于
    ///       最长那句简介的宽度，窄窗口下页面底下多出一根横向滚动条，怎么拖窗口都消不掉
    ///       （`OverlayHost` 那个整页滚动区的 widgetResizable 认的正是这个量）。
    QSize minimumSizeHint() const override {
        return QSize(0, QLabel::minimumSizeHint().height());
    }

protected:
    void paintEvent(QPaintEvent *) override {
        const QRect box = contentsRect();
        QPainter painter(this);
        painter.setPen(palette().color(foregroundRole()));
        painter.drawText(box, alignment() | Qt::TextSingleLine, elidedFor(this));
    }
};

QString elidedFor(const QLabel *label) {
    return label->fontMetrics().elidedText(label->text(), Qt::ElideRight,
                                          qMax(0, label->contentsRect().width()));
}

/// 一圈细边、底色是 Base 的卡（头卡用它）。
///
/// 和 `OverlayHost` 里那张提示卡同一种做法（`QFrame::StyledPanel` + 把 Window 那档颜色
/// 换成 Base）—— 那一样是"一块浮起来的白底"，两边长得不一样的话，同一套界面里会看出来
/// 是两个人做的。
QFrame *makeCard(QWidget *parent) {
    auto *card = new QFrame(parent);
    card->setFrameShape(QFrame::StyledPanel);
    card->setAutoFillBackground(true);
    QPalette cardPalette = card->palette();
    cardPalette.setColor(QPalette::Window, cardPalette.color(QPalette::Base));
    card->setPalette(cardPalette);
    return card;
}

/// chip 那颗按钮的外观。颜色**全从调色板来**（不写死灰）—— 换主题、换深色，这一排跟着走。
/// 这是这一页唯一一处 QSS（其余外观都交给 Fusion + 代理，见用户定的 UI 样式基线）。
QString chipStyle(const QPalette &palette) {
    const QColor border = palette.color(QPalette::Mid);
    QColor hover = palette.color(QPalette::Highlight);
    hover.setAlpha(48);
    return QStringLiteral("QPushButton { border: 1px solid %1; border-radius: %2px;"
                          " padding: 2px 10px; background: transparent; }"
                          "QPushButton:hover:!checked { background-color: %3; }"
                          "QPushButton:checked { background-color: %4; color: %5;"
                          " border-color: %4; }")
        .arg(border.name(QColor::HexArgb))
        .arg(kChipHeight / 2)
        .arg(hover.name(QColor::HexArgb), palette.color(QPalette::Highlight).name(QColor::HexArgb),
             palette.color(QPalette::HighlightedText).name(QColor::HexArgb));
}

}  // namespace

// ---------- 版本行左边那个字母方块 ----------

namespace ResourceDetailMarks {

int badgeSize() {
    // **和下载页那四张表的缩略图同出一处**（见头文件）。改那边这一格跟着走 —— 两处都是
    // "列表行前面那一格"，不一样大的话并列看过去一眼就看得出来。
    return MainWindowUI::resourceTreeIconSize();
}

QColor channelColor(int channel) {
    switch (channel) {
    case 0:
        return QColor(0x2e, 0x7d, 0x32);  // 正式版：绿
    case 1:
        return QColor(0xef, 0x6c, 0x00);  // 测试版：橙
    case 2:
        return QColor(0xc6, 0x28, 0x28);  // 早期测试版：红
    default:
        // 认不出来的档给灰的。**不是"不画"**：同组里别的行前面都有方块，这一行没有的话
        // 它的正文会往左挪一截，看着像列表坏了。
        return QColor(0x75, 0x75, 0x75);
    }
}

QString channelLetter(int channel) {
    switch (channel) {
    case 0:
        return QStringLiteral("R");
    case 1:
        return QStringLiteral("B");
    case 2:
        return QStringLiteral("A");
    default:
        return QString();
    }
}

QColor channelLetterColor() {
    return QColor(0xff, 0xff, 0xff);
}

QPixmap channelBadge(int channel, int size) {
    if (size <= 0) {
        return QPixmap();
    }
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(channelColor(channel));
    // 圆角半径 = 边长的八分之一（32 px 上是 4）：够圆润，又还看得出是个方块 ——
    // 和 chip 那种"胶囊"是两回事，这一枚在列表里是个标记，不是按钮。
    const qreal radius = qMax(2.0, size / 8.0);
    painter.drawRoundedRect(QRectF(0, 0, size, size), radius, radius);
    const QString letter = channelLetter(channel);
    if (!letter.isEmpty()) {
        QFont font = painter.font();
        font.setBold(true);
        // 半个方块高：32 px 上是 16 px 的字，方块里那一枚字母撑得开又不贴边。
        font.setPixelSize(qMax(8, size / 2));
        painter.setFont(font);
        painter.setPen(channelLetterColor());
        painter.drawText(QRect(0, 0, size, size), Qt::AlignCenter, letter);
    }
    return pixmap;
}

}  // namespace ResourceDetailMarks

// ---------- 这一页 ----------

using ResourceDetail::Chip;
using ResourceDetail::Group;
using ResourceDetail::Header;
using ResourceDetail::Row;

ResourceDetailPage::ResourceDetailPage(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("资源详情"));
    // 只影响"单独拿出来用"（验证工程那几处）：嵌进覆盖层的时候，这一页拿到的是内容区的
    // 整幅宽（见 OverlayHost::execEmbedded 里那句 area->layout()->addWidget(dialog)）。
    resize(kPageWidth, kPageHeight);

    auto *outer = new QVBoxLayout(this);
    // 页眉（返回箭头 + 标题）由覆盖层那一层画，这一页从卡开始 —— 边上留的那一圈和设置页
    // 一样是 12（覆盖层自己还有 9，见 OverlayHost::kOverlayPageMargin）。
    outer->setContentsMargins(12, 12, 12, 12);
    outer->setSpacing(10);

    // ---------- 头卡 ----------
    QFrame *card = makeCard(this);
    card->setObjectName(headerCardName());
    auto *cardLayout = new QHBoxLayout(card);
    cardLayout->setContentsMargins(kCardPadding, kCardPadding, kCardPadding, kCardPadding);
    cardLayout->setSpacing(kCardPadding);

    m_icon = new QLabel(card);
    m_icon->setObjectName(iconLabelName());
    // 尺寸**钉死**：图是异步回来的（走 ResourceStore 那一套），回来之前也得占着同样大一格
    // —— 不然图一到位，右边那几行字整体往左跳一下。
    m_icon->setFixedSize(kIconSize, kIconSize);
    m_icon->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(m_icon, 0, Qt::AlignTop);

    auto *textColumn = new QVBoxLayout;
    textColumn->setContentsMargins(0, 0, 0, 0);
    textColumn->setSpacing(4);

    m_title = new ElidedLabel(card);
    m_title->setObjectName(titleLabelName());
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 2);
    m_title->setFont(titleFont);
    textColumn->addWidget(m_title);

    m_description = new ElidedLabel(card);
    m_description->setObjectName(descriptionLabelName());
    // 简介只画一行、放不下就截（全文挂在鼠标提示上，见 setHeader）。让它换行的话，
    // CurseForge 那种整段的简介会把这张卡撑成半屏高 —— 而这一页的主角是下面那份版本列表。
    textColumn->addWidget(m_description);

    m_meta = new ElidedLabel(card);
    m_meta->setObjectName(metaLabelName());
    QFont metaFont = m_meta->font();
    metaFont.setPointSize(qMax(6, metaFont.pointSize() - 1));
    m_meta->setFont(metaFont);
    {
        // 次要信息用次要那一档颜色（和列表里第二行小字同一个角色，见 VersionTreeDelegate）。
        QPalette metaPalette = m_meta->palette();
        metaPalette.setColor(QPalette::WindowText,
                             metaPalette.color(QPalette::PlaceholderText));
        m_meta->setPalette(metaPalette);
    }
    textColumn->addWidget(m_meta);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(8);
    // 三颗按钮**都在**，只是按"拼不拼得出网址"来显示 —— 拼不出地址的那一颗不画
    // （见 Header 里那两段 NOTE：点了没反应的按钮比没有这颗按钮更糟）。
    m_open = new QPushButton(card);
    m_open->setObjectName(openButtonName());
    m_open->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_open->setVisible(false);
    connect(m_open, &QPushButton::clicked, this,
            [this] { emit openExternalRequested(m_header.openUrl); });
    buttonRow->addWidget(m_open);

    m_mcmod = new QPushButton(tr("转到 MC 百科"), card);
    m_mcmod->setObjectName(mcmodButtonName());
    m_mcmod->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_mcmod->setVisible(false);
    connect(m_mcmod, &QPushButton::clicked, this,
            [this] { emit openExternalRequested(m_header.mcmodUrl); });
    buttonRow->addWidget(m_mcmod);

    m_copyName = new QPushButton(tr("复制名称"), card);
    m_copyName->setObjectName(copyNameButtonName());
    m_copyName->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_copyName->setVisible(false);
    connect(m_copyName, &QPushButton::clicked, this,
            [this] { emit copyNameRequested(m_header.nameToCopy); });
    buttonRow->addWidget(m_copyName);
    buttonRow->addStretch(1);
    textColumn->addLayout(buttonRow);
    textColumn->addStretch(1);

    cardLayout->addLayout(textColumn, 1);
    outer->addWidget(card);

    // ---------- 那一排游戏版本 chip ----------
    //
    // 套一层横向滚动区：一个资源能用到的游戏版本可能十几个（加上「全部」），一行放不下。
    // **不能让整页跟着变宽** —— 那会让底下的版本列表也一起变宽、页面底下多出横向滚动条。
    // 这里只让这一排自己滚。
    auto *chipScroll = new QScrollArea(this);
    chipScroll->setFrameShape(QFrame::NoFrame);
    chipScroll->setWidgetResizable(true);
    chipScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    chipScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    chipScroll->setFixedHeight(kChipRowHeight);
    chipScroll->setObjectName(chipRowName() + QStringLiteral("Scroll"));
    m_chipRow = new QWidget(chipScroll);
    m_chipRow->setObjectName(chipRowName());
    auto *chipLayout = new QHBoxLayout(m_chipRow);
    chipLayout->setContentsMargins(0, 0, 0, 0);
    chipLayout->setSpacing(6);
    chipLayout->addStretch(1);
    chipScroll->setWidget(m_chipRow);
    outer->addWidget(chipScroll);

    // ---------- 版本列表 ----------
    //
    // 三层设置和下载页那四张表**逐项相同**（表头藏起来、行高常数、根上那个小三角不画、
    // 同一个字号偏移、同一个代理），只有两处不一样：
    //   ① 图标换成那一枚字母方块（代理见到条目给了 DecorationRole 就画它）；
    //   ② 叶子那一层**不画**右边的 `>`（点是"下它"这个动作，不是"还有下一页"）。
    m_list = new QTreeWidget(this);
    m_list->setObjectName(versionListName());
    m_list->setHeaderHidden(true);
    m_list->setUniformRowHeights(true);
    m_list->setRootIsDecorated(false);
    m_list->setColumnCount(1);
    m_list->setItemDelegate(new VersionTreeDelegate(m_list, /*leafArrow=*/false,
                                                    ResourceDetailMarks::badgeSize()));
    m_list->setExpandsOnDoubleClick(false);
    QFont listFont = m_list->font();
    listFont.setPointSize(listFont.pointSize() + MainWindowUI::versionTreeFontStep());
    m_list->setFont(listFont);
    m_list->setIconSize(QSize(ResourceDetailMarks::badgeSize(),
                              ResourceDetailMarks::badgeSize()));
    outer->addWidget(m_list, 1);

    // 组头那一行是**开关**（点一下展开、再点一下收起），版本那一行是**动作**（下它）——
    // 这两档的分别正是 connectRowActivation() 那个判据在管的事（点得快的时候开关行
    // 后面那几下会被 Qt 压掉，得由它补上）。
    connectRowActivation(
        m_list, [this](QTreeWidgetItem *item) { onRowActivated(item); },
        [](QTreeWidgetItem *item) { return item != nullptr && item->parent() == nullptr; });
}

// ---------- 名字 ----------

QString ResourceDetailPage::headerCardName() { return QStringLiteral("resourceDetailHeaderCard"); }
QString ResourceDetailPage::iconLabelName() { return QStringLiteral("resourceDetailIcon"); }
QString ResourceDetailPage::titleLabelName() { return QStringLiteral("resourceDetailTitle"); }
QString ResourceDetailPage::descriptionLabelName() {
    return QStringLiteral("resourceDetailDescription");
}
QString ResourceDetailPage::metaLabelName() { return QStringLiteral("resourceDetailMeta"); }
QString ResourceDetailPage::openButtonName() { return QStringLiteral("resourceDetailOpen"); }
QString ResourceDetailPage::mcmodButtonName() { return QStringLiteral("resourceDetailMcmod"); }
QString ResourceDetailPage::copyNameButtonName() {
    return QStringLiteral("resourceDetailCopyName");
}
QString ResourceDetailPage::chipRowName() { return QStringLiteral("resourceDetailChips"); }
QString ResourceDetailPage::chipButtonNamePrefix() {
    return QStringLiteral("resourceDetailChip");
}
QString ResourceDetailPage::versionListName() { return QStringLiteral("resourceDetailVersions"); }
int ResourceDetailPage::rowKeyRole() { return Qt::UserRole; }

QString ResourceDetailPage::titleText() const {
    return m_header.title.isEmpty() ? tr("资源详情") : m_header.title;
}

// ---------- 头卡 ----------

void ResourceDetailPage::setHeader(const Header &header) {
    m_header = header;
    m_title->setText(header.title);
    m_title->setToolTip(header.title);
    m_description->setText(header.description);
    // 全文挂鼠标提示：这一行是**截过的**，截掉的那部分只有这儿看得到。
    m_description->setToolTip(header.description);
    m_meta->setText(header.metaLine);
    m_meta->setToolTip(header.metaLine);
    if (!header.openUrl.isEmpty()) {
        m_open->setText(tr("转到 %1").arg(header.openLabel));
        m_open->setVisible(true);
    } else {
        m_open->setVisible(false);
    }
    m_mcmod->setVisible(!header.mcmodUrl.isEmpty());
    m_copyName->setVisible(!header.nameToCopy.isEmpty());
    // 这一页自己的窗口标题也换掉：覆盖层那一层显示的是 titleText()，两处同一个出处。
    setWindowTitle(titleText());
}

void ResourceDetailPage::setIcon(const QPixmap &icon) {
    if (icon.isNull()) {
        m_icon->clear();
        return;
    }
    m_icon->setPixmap(icon.scaled(kIconSize, kIconSize, Qt::KeepAspectRatio,
                                  Qt::SmoothTransformation));
}

QString ResourceDetailPage::openButtonText() const { return m_open->text(); }
QString ResourceDetailPage::shownDescription() const { return elidedFor(m_description); }
QString ResourceDetailPage::descriptionToolTip() const { return m_description->toolTip(); }

// ---------- chip ----------

void ResourceDetailPage::setChips(const QList<Chip> &chips, const QString &preselectedKey) {
    m_chips = chips;
    rebuildChips();
    // 默认按哪一颗由调用方说（它知道用户在下载页选的是哪个游戏版本）——
    // 这儿**不猜**、也不发信号：信号是"用户点了"的意思，开页那一发会去重算一遍分组，
    // 而调用方摆好 chip 之后本来就要自己 setGroups()（那才是第一份内容）。
    m_currentChip = preselectedKey;
    applyChipChecked(preselectedKey);
}

void ResourceDetailPage::rebuildChips() {
    auto *layout = qobject_cast<QHBoxLayout *>(m_chipRow->layout());
    if (layout == nullptr) {
        return;
    }
    // 整排重建（和实例设置页里那几行同一个做法）：**当场 delete，不 deleteLater** ——
    // 名字按下标给，按名字找控件的验证工程会先撞上悬着的旧那颗。
    for (QPushButton *button : m_chipButtons) {
        delete button;
    }
    m_chipButtons.clear();
    for (int i = 0; i < m_chips.size(); ++i) {
        const Chip &chip = m_chips.at(i);
        auto *button = new QPushButton(chip.text, m_chipRow);
        button->setObjectName(chipButtonNamePrefix() + QString::number(i));
        button->setCheckable(true);
        button->setFixedHeight(kChipHeight);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        button->setStyleSheet(chipStyle(button->palette()));
        // 身份挂属性上（和 rowKeyRole 那条同一个理由：文字是给人看的，会随文案改）。
        button->setProperty("chipKey", chip.key);
        connect(button, &QPushButton::clicked, this, [this, button] { onChipClicked(button); });
        // 插在最后那个 stretch 之前（构造时摆了一个，整排靠左）。
        layout->insertWidget(layout->count() - 1, button);
        m_chipButtons.append(button);
    }
}

void ResourceDetailPage::onChipClicked(QPushButton *button) {
    if (button == nullptr) {
        return;
    }
    const QString key = button->property("chipKey").toString();
    if (key == m_currentChip) {
        // 点的是**已经按着的那一颗**：把勾选状态摆回去就行（Qt 点一下会自己翻成没按），
        // 别再重算一遍分组 —— 重算会让下面那份列表整个重建、用户眼前闪一下。
        applyChipChecked(m_currentChip);
        return;
    }
    m_currentChip = key;
    applyChipChecked(key);
    emit chipChanged(key);
}

void ResourceDetailPage::applyChipChecked(const QString &key) {
    // 一排 chip 是**互斥**的（和单选钮一样），一个都不按是不合法的状态：点「全部」那一颗
    // 就是"不筛"。所以这儿总能挑出一颗来按 —— 挑不中（调用方给了个表里没有的 key）
    // 就按"全部"那一颗，别再往下一层层猜。
    QPushButton *fallback = nullptr;
    QPushButton *wanted = nullptr;
    for (QPushButton *button : m_chipButtons) {
        const QString chipKey = button->property("chipKey").toString();
        if (chipKey.isEmpty() && fallback == nullptr) {
            fallback = button;
        }
        if (chipKey == key && wanted == nullptr) {
            wanted = button;
        }
    }
    QPushButton *target = wanted != nullptr ? wanted : fallback;
    for (QPushButton *button : m_chipButtons) {
        button->setChecked(button == target);
    }
    if (target != nullptr) {
        // 挑中的不是要的那一颗 ⇒ "当前值"跟着纠正（不然界面按着一颗、currentChip()
        // 说的是另一颗）。
        //
        // NOTE: 判据得比**挑中的那一颗**，不能比 `key != m_currentChip` —— 那两个入参
        //       在三个调用点上都已经被调成相等了（setChips 先赋再调、onChipClicked 那两处
        //       也一样），拿它当判据等于这一句永远不执行，而它要修的正是"调用方给了一个
        //       表里没有的 key"那一档（界面上按着「全部」、currentChip() 却回那个空 key）。
        const QString picked = target->property("chipKey").toString();
        if (picked != m_currentChip) {
            m_currentChip = picked;
        }
    }
}

QString ResourceDetailPage::currentChip() const { return m_currentChip; }
QList<QPushButton *> ResourceDetailPage::chips() const { return m_chipButtons; }

QString ResourceDetailPage::chipTextAt(int index) const {
    return index >= 0 && index < m_chipButtons.size() ? m_chipButtons.at(index)->text()
                                                     : QString();
}

QString ResourceDetailPage::chipKeyAt(int index) const {
    return index >= 0 && index < m_chipButtons.size()
               ? m_chipButtons.at(index)->property("chipKey").toString()
               : QString();
}

// ---------- 版本列表 ----------

void ResourceDetailPage::setGroups(const QList<Group> &groups) {
    if (groups.isEmpty()) {
        // 空表**不摆一片空白**：一个资源还没发过版本是正常的（刚建的项目），
        // 一片空白看着像"读失败了"。
        showPlaceholder(tr("这个资源一个版本都没发过"));
        return;
    }
    fillGroups(groups);
}

void ResourceDetailPage::fillGroups(const QList<Group> &groups) {
    m_list->clear();
    m_placeholder.clear();
    for (const Group &group : groups) {
        auto *groupItem = new QTreeWidgetItem(m_list);
        groupItem->setText(0, group.title);
        for (const Row &row : group.rows) {
            auto *item = new QTreeWidgetItem(groupItem);
            item->setText(0, row.title);
            item->setData(0, MainWindowUI::rowSubTextRole(), row.subText);
            item->setData(0, MainWindowUI::rowIconRole(),
                          ResourceDetailMarks::channelBadge(row.channel,
                                                            ResourceDetailMarks::badgeSize()));
            item->setData(0, rowKeyRole(), row.key);
            if (!row.tooltip.isEmpty()) {
                item->setToolTip(0, row.tooltip);
            }
        }
        // 组头一律**展开**：用户点进这一页就是想看版本，先收着等于让他再点十几下。
        // NOTE: 换一颗 chip 会把整份列表重建（分组的算法在业务层），展开状态也就跟着复位 ——
        //       这是这一版有意接受的（要留住它得按组名记一份，而组名会随 chip 改）。
        groupItem->setExpanded(true);
    }
}

void ResourceDetailPage::setPlaceholder(const QString &text) { showPlaceholder(text); }

void ResourceDetailPage::showPlaceholder(const QString &text) {
    m_list->clear();
    m_placeholder = text;
    if (text.isEmpty()) {
        return;
    }
    // 顶层条目（代理给顶层画粗体）——和下载页那四张表的"没取到"那句是同一个形状。
    auto *item = new QTreeWidgetItem(m_list);
    item->setText(0, text);
}

QString ResourceDetailPage::placeholderText() const { return m_placeholder; }
QTreeWidget *ResourceDetailPage::versionList() const { return m_list; }

int ResourceDetailPage::groupCount() const {
    return m_list->topLevelItemCount();
}

int ResourceDetailPage::versionRowCount() const {
    int count = 0;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        count += m_list->topLevelItem(i)->childCount();
    }
    return count;
}

QString ResourceDetailPage::groupTitleAt(int index) const {
    QTreeWidgetItem *item = m_list->topLevelItem(index);
    return item != nullptr ? item->text(0) : QString();
}

bool ResourceDetailPage::groupExpandedAt(int index) const {
    QTreeWidgetItem *item = m_list->topLevelItem(index);
    return item != nullptr && item->isExpanded();
}

void ResourceDetailPage::onRowActivated(QTreeWidgetItem *item) {
    if (item == nullptr) {
        return;
    }
    if (item->parent() == nullptr) {
        // 组头：开/关（**自己翻**，Qt 那个双击展开在接线时就关掉了，见 RowActivation.hpp）。
        item->setExpanded(!item->isExpanded());
        return;
    }
    const QString key = item->data(0, rowKeyRole()).toString();
    if (!key.isEmpty()) {
        emit rowActivated(key);
    }
}

// ---------- 几何 ----------

void ResourceDetailPage::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    // 三个省略号标签是按"当下这一格的宽度"算的（见 elidedFor），宽度变了要重画一次 ——
    // 不然窄窗口下会留着按宽窗口算出来的那一串（甩出去半行字）。
    m_title->update();
    m_description->update();
    m_meta->update();
}

void ResourceDetailPage::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    // 摆上去那一刻这一页才拿到最终宽度（覆盖层给的），所以这里再催一次。
    m_title->update();
    m_description->update();
    m_meta->update();
}
