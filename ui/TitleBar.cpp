/**
 * @file TitleBar.cpp
 * @brief TitleBar 的实现（条上那几样东西的摆法与画法）。
 * @author 0x7fffff
 * @date 2026-09-25
 */

#include "TitleBar.hpp"

#include "Theme.hpp"   // 条底色（Theme::Part::TitleBar）、危险操作的红（Theme::dangerColor()）
#include "VersionIcons.hpp"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>

namespace {

/// 条高（逻辑像素）。44 = 8 + 28 + 8，也就是"缩放边 + 按钮 + 缩放边"，
/// 见 `TitleBar::buttonInset()` 那段 —— 条再矮就得把按钮压进缩放边里了。
constexpr int kTitleBarHeight = 44;
/// 那几颗按钮的高（逻辑像素）。
constexpr int kButtonSize = 28;
/// 每一颗**导航**按钮的宽（逻辑像素）。四个汉字标签（"主页"两个字）+ 左右各 20 的余量：
/// 两字的正文是 24~26 px 宽，20 的余量让悬停时那块底色不至于贴着字。
constexpr int kNavItemWidth = 68;
/// 草方块那颗图标的边长。16 的倍数最清楚（见 `VersionIcons::grassBlock()` 的说明）。
constexpr int kLogoSize = 16;
/// 草方块离窗口左边缘多远。
constexpr int kLeftPadding = 12;
/// 草方块和名字之间空多少。
constexpr int kLogoGap = 8;
/// 名字和导航那一组之间**至少**空多少（画名字时的右边留白，见 `paintEvent`）。
/// NOTE: 这一格**是量出来的**：名字那几个字母一长、"主页"就贴到它脸上（中间连一口气都
///       没有），所以这儿的留白比 kLogoGap 大一倍。
constexpr int kBrandGap = 16;

/// 导航那一组的**左边距**。
///
/// ★ 这个数不是审美，是「四个按钮的正中 == 整条的正中」（用户原话「上面的四个居中」）
///   的解法，也是**唯一**的调法。布局是
///
///       [这一格留白][stretchA][导航那一组][stretchB][最小化][关闭]
///
///   两段 stretch 的因子都是 1 ⇒ 导航那一组的正中
///
///       = L + (W - L - R - B) / 2
///
///   （W = 条宽、L/R = 布局左右边距、B = 右边那两颗窗口按钮占的宽）。要它等于 W/2，
///   等价于
///
///       L - R = B
///
///   右边距 R = 0（那两颗按钮顶到条的最右边），而 B = 2 * buttonSize()
///   ⇒ 左边距就**必须是** `2 * buttonSize()`。
///
///   NOTE: 导航那一组自己的宽 N **消掉了** —— 加一颗按钮、改一次 `navItemWidth()` 都不用
///         回来调这个数。ui_check 里那条"组的中正 == 条的中正"就是照着这句量的。
constexpr int kNavGroupLeftMargin = 2 * kButtonSize;
/// 当前那一颗底下那条横线的高。
constexpr int kCurrentMarkHeight = 2;
/// 那条横线左右各缩进多少（贴着字的宽度不好看，缩一点像"这一格被标住了"）。
constexpr int kCurrentMarkInset = 10;
/// 那两颗窗口按钮的字形边长。18 ⇒ `VersionIcons` 那套 22%~78% 的边距正好给出 10 px 的笔画跨度，
/// 和系统那两颗按钮上的字形差不多大。
constexpr int kGlyphSize = 18;

// ★ 条的颜色原来是这儿的一个 `barColor()`：把窗口底色压暗一档（`darker(105)`，和底栏
//   同一档）。2026-09-28 起它挪进了 `Theme::Part::TitleBar` —— 只有那一层才知道"用户把
//   标题栏单独改过了没有"，算两遍的话覆盖项只会在一半地方生效。条上每一处颜色现在都问
//   `Theme::partBackground(Part::TitleBar)`，**别在这儿再写一遍那个 darker(105)**。

}  // namespace

QString TitleBar::defaultBrandText() {
    // 用户原话：「现在把顶上标题换成五个字母」+「左上角五个字母默认是这五个」。就这五个，
    // **写死**（要改成别的走设置页「个性化」栏的「外观」，见 `setBrandText()`）。
    return QStringLiteral("WTFML");
}

int TitleBar::barHeight() {
    return kTitleBarHeight;
}

int TitleBar::buttonSize() {
    return kButtonSize;
}

int TitleBar::buttonInset() {
    return (kTitleBarHeight - kButtonSize) / 2;
}

int TitleBar::navItemWidth() {
    return kNavItemWidth;
}

TitleBarButton::TitleBarButton(Glyph glyph, const QColor &hoverColor, const QColor &hoverInk, QWidget *parent)
    : QAbstractButton(parent),
      m_glyph(glyph),
      m_hoverColor(hoverColor),
      m_hoverInk(hoverInk) {
    m_ink = palette().color(QPalette::WindowText);
    setFixedSize(kButtonSize, kButtonSize);
    // 悬停那一位**默认没人填**：Qt 不会自己产生 HoverEnter/HoverLeave，得自己开。
    // WA_Hover 让它产生那两个事件，mouseTracking 让"没按着鼠标在动"也算数 ——
    // #### 两条缺一不可（少哪条都是"悬停色再也不出现"，而这件事从代码上看不出来）。
    setAttribute(Qt::WA_Hover, true);
    setMouseTracking(true);
    // 进了条上的按钮不该把键盘焦点从内容区抢走（条上没有可以用键盘操作的东西）。
    setFocusPolicy(Qt::NoFocus);
}

void TitleBarButton::setBaseColor(const QColor &color) {
    m_baseColor = color;
    update();
}

void TitleBarButton::setHoverColors(const QColor &hoverColor, const QColor &hoverInk) {
    m_hoverColor = hoverColor;
    m_hoverInk = hoverInk;
    update();
}

void TitleBarButton::setInkColor(const QColor &color) {
    m_ink = color;
    update();
}

void TitleBarButton::setCurrent(bool current) {
    if (m_current == current) {
        return;
    }
    m_current = current;
    update();
}

bool TitleBarButton::event(QEvent *event) {
    switch (event->type()) {
    case QEvent::HoverEnter:
        m_hovered = true;
        update();
        break;
    case QEvent::HoverLeave:
        m_hovered = false;
        update();
        break;
    default:
        break;
    }
    return QAbstractButton::event(event);
}

void TitleBarButton::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    // 底：不悬停时一般是全透（条自己的底色透上来），悬停时铺那一格色。
    const QColor base = m_hovered ? m_hoverColor : m_baseColor;
    if (base.alpha() > 0) {
        p.fillRect(rect(), base);
    }
    const QColor ink = m_hovered ? m_hoverInk : m_ink;
    if (m_glyph == Glyph::Text) {
        // 导航按钮：一行字 + （当前那一颗）底下一条横线。
        QFont labelFont = font();
        labelFont.setBold(m_current);
        p.setFont(labelFont);
        p.setPen(ink);
        p.drawText(rect(), Qt::AlignCenter, text());
        if (m_current) {
            p.fillRect(QRect(kCurrentMarkInset, height() - kCurrentMarkHeight,
                             qMax(0, width() - 2 * kCurrentMarkInset), kCurrentMarkHeight),
                       ink);
        }
        return;
    }
    const QPixmap glyph = m_glyph == Glyph::Close ? VersionIcons::cross(kGlyphSize, ink)
                                                 : VersionIcons::windowMinimize(kGlyphSize, ink);
    p.drawPixmap(QPoint((width() - kGlyphSize) / 2, (height() - kGlyphSize) / 2), glyph);
}

TitleBar::TitleBar(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("titleBar"));
    setFixedHeight(kTitleBarHeight);
    setAutoFillBackground(true);

    // 悬停底色：最小化那颗是条底色再压暗一档（"有个东西在里头"就够了），
    // 关闭那颗是**红底白叉** —— 那个红取自 Theme::dangerColor()，全仓只有这一个出处。
    // NOTE: 这一带**只负责把按钮建出来**，颜色一律由下面的 `applyColors()` 给（含这四个
    //       色，它们都是"条底色/调色板算出来的"）。从前是在这儿现算现传的，于是主题一换
    //       它们就停在旧色上 —— 见 `applyColors()` 那段。
    m_minimize = new TitleBarButton(TitleBarButton::Glyph::Minimize, QColor(), QColor(), this);
    m_minimize->setObjectName(QStringLiteral("titleBarMinimize"));
    m_close = new TitleBarButton(TitleBarButton::Glyph::Close, QColor(), QColor(), this);
    m_close->setObjectName(QStringLiteral("titleBarClose"));

    // 条上的子控件**只有按钮**（四颗导航 + 最小化 + 关闭）。**这不是随手摆的**：
    // `WindowChrome::handleNativeEvent()` 判"这一手是不是落在控件上"用的就是
    // `strip->childAt()`，多一个子控件（哪怕是个铺满的空白 label）就会把拖窗区从它底下
    // 抢走。草方块、名字、以及导航组左右那两片空白都是 paintEvent 画的。
    m_layout = new QHBoxLayout(this);
    // 左边那一段留白就是"居中"的全部秘密（推导见 kNavGroupLeftMargin）—— 它和名字有多宽
    // **没关系**：名字画在条的左头（paintEvent），导航那一组自己居中，两边互不干扰。
    m_layout->setContentsMargins(kNavGroupLeftMargin, 0, 0, 0);
    m_layout->setSpacing(0);
    // 导航那一组**自己一层布局**，夹在两段等长 stretch 中间 ⇒ 它就是条的中正。
    // NOTE: 为什么不是把四颗按钮直接 addWidget 到主布局里：主布局上还挂着右边那两颗窗口
    //       按钮，往中间插控件要按下标算，而"一共几颗导航"是 setupUi 灌进来的
    //       （0 到 4 颗都合法，见 setNavItems）。独立一层之后 setNavItems() 只管这一层。
    m_navLayout = new QHBoxLayout;
    m_navLayout->setContentsMargins(0, 0, 0, 0);
    m_navLayout->setSpacing(0);
    m_layout->addStretch(1);
    m_layout->addLayout(m_navLayout);
    // 这一段 stretch 顶在"最后一颗导航按钮"和"最小化"之间 —— 拖窗区就是它和左边那一段。
    m_layout->addStretch(1);
    // 显式 AlignVCenter：靠尺寸策略去居中是在赌 QBoxLayout 对 Fixed 那一档的摆法。
    m_layout->addWidget(m_minimize, 0, Qt::AlignVCenter);
    m_layout->addWidget(m_close, 0, Qt::AlignVCenter);

    connect(m_minimize, &QAbstractButton::clicked, this, &TitleBar::minimizeRequested);
    connect(m_close, &QAbstractButton::clicked, this, &TitleBar::closeRequested);

    // 条上所有颜色现在算一遍（构造这一刻四颗导航还没有，`setNavItems()` 会再来一遍）。
    applyColors();
}

void TitleBar::applyColors() {
    // ★ 防自咬：下面那句 `setPalette()` 会发一次 `PaletteChange`，而我们就挂在那个事件上
    //   （`changeEvent()`）—— 不拦的话就是"改 → 收到 → 再改"。见 `Theme::follow()` 的 NOTE。
    if (m_applyingColors) {
        return;
    }
    m_applyingColors = true;

    // 条底色：用户单独改过就是那个，没改过 = 窗口底色压暗一档（原来是 `barColor()`）。
    const QColor chrome = Theme::partBackground(Theme::Part::TitleBar);
    // ★ 基色调色板一律取**应用那一份**，不能取 `palette()`：条自己持有一份（下面就把 Window
    //   改成了 chrome），第二次进来 `palette().color(Window)` 已经是压暗过一档的那个，
    //   再算就是**第二次压暗**（2026-09-25 之前就是这么写的，于是最小化那颗的悬停色比它
    //   该有的深了两档：量出来 #bdbdbd，而"条底色再压暗一档"应该是 #c6c6c6）。ui_check Z11
    //   那条像素断言量的就是这件事。
    QPalette barPalette = QApplication::palette();
    barPalette.setColor(QPalette::Window, chrome);
    // 条上那一份"正文色"也一起定下来：条上的字（`paintEvent` 那行品牌名）是拿
    // `palette().color(WindowText)` 画的，而条自己的底现在是**主题色那一支绿** ——
    // 不给它一个绿底上读得出来的墨色，那五个字母就还是调色板里的深灰、糊在绿里。
    // 子控件（那六颗按钮）的颜色不走这儿（它们各自 `setInkColor()`，见下）。
    const QColor strong = Theme::partForeground(Theme::Part::TitleBar,
                                               barPalette.color(QPalette::WindowText));
    barPalette.setColor(QPalette::WindowText, strong);
    setPalette(barPalette);

    // 次要墨色（不是当前那一颗导航写的那个色）：
    //   - 条是主题色那一支（白墨）时 = **白墨压到 68%**。这一档不能取调色板里那个次要灰：
    //     灰的压在白绿条上就是"两个字糊在一起"（界面上看着像没渲染完）。
    //   - 其余情形（用户把条单独改成深色底）照旧用调色板那个次要色。
    // NOTE: 判据取的是"这一块的出厂字色是不是白墨"（`partLightInk()`），不是"底色深不深"
    //       —— 用户点名要的就是白墨，而这一支绿的明度并不低（拿明度判会给回深色）。
    const QColor strongFallback =
        Theme::partLightInk(Theme::Part::TitleBar)
            ? QColor(0xff, 0xff, 0xff, 0xad)
            : barPalette.color(QPalette::PlaceholderText);
    const QColor muted = Theme::partForeground(Theme::Part::TitleBar, strongFallback);
    const QColor hover = chrome.darker(115);
    if (auto *button = qobject_cast<TitleBarButton *>(m_minimize)) {
        button->setHoverColors(hover, strong);
        button->setInkColor(strong);
    }
    if (auto *button = qobject_cast<TitleBarButton *>(m_close)) {
        // ★ 两态两套色，别把悬停那一套当成常驻的：
        //   不悬停 = **条上那几笔正文色**（和旁边那颗最小化同一个墨色，所以它平时就是条上
        //   一个普通记号）；
        //   悬停  = 红底 + 白叉，那个红是全仓唯一一处"危险操作的红"，白叉**只在红底上**成立
        //   （底色上除了白没有别的能读的颜色）。
        //   `setHoverColors()` 给的是悬停那一套、`setInkColor()` 给的是平时那一套 —— 从前
        //   这里两样都写成白的，于是不悬停时那颗叉也是白的（ui_check Z11 那条像素断言量的
        //   就是这件事：正文色 0 个像素）。
        //   **故意不走 `partForeground()` 的是红底那一半**：那是"这一块的字色"，而红底上的
        //   墨色不是"条上的字"。平时那一半照走。
        button->setHoverColors(Theme::dangerColor(barPalette), QColor(Qt::white));
        button->setInkColor(strong);
    }
    for (int i = 0; i < m_navButtons.size(); ++i) {
        if (auto *button = qobject_cast<TitleBarButton *>(m_navButtons.at(i))) {
            button->setHoverColors(hover, strong);
            // 当前那一颗是正文色、其余几颗是次要色；**当前那一颗还加粗、底下还有一条横线**
            // （见 `TitleBarButton::paintEvent`）—— 所以哪怕用户把这一块的前景覆盖成一个色，
            // "现在是第几页"照样读得出来。
            button->setInkColor(i == m_currentNav ? strong : muted);
        }
    }

    m_applyingColors = false;
}

void TitleBar::changeEvent(QEvent *event) {
    switch (event->type()) {
    case QEvent::PaletteChange:
    case QEvent::ApplicationPaletteChange:
    case QEvent::StyleChange:
        // 调色板换了（换主题、系统换深浅、父窗口换样式）⇒ 条上存的那些颜色重算一遍。
        applyColors();
        break;
    default:
        break;
    }
    QWidget::changeEvent(event);
}

void TitleBar::setNavItems(const QStringList &labels) {
    for (QAbstractButton *button : m_navButtons) {
        // 先摘出布局再删：留在布局里的控件被 deleteLater 之后，那一格会空到下一次
        // 布局重算，中间那一下量出来的几何是没有这几颗按钮的。
        m_navLayout->removeWidget(button);
        button->setParent(nullptr);
        button->deleteLater();
    }
    m_navButtons.clear();
    m_currentNav = -1;

    for (int i = 0; i < labels.size(); ++i) {
        // 颜色同样交给 `applyColors()`（含悬停色）—— 从前是在这儿现算现传的，主题换了
        // 它们就停在旧色上。
        auto *button = new TitleBarButton(TitleBarButton::Glyph::Text, QColor(), QColor(), this);
        button->setObjectName(QStringLiteral("titleBarNav%1").arg(i));
        button->setLabel(labels.at(i));
        button->setFixedSize(kNavItemWidth, kButtonSize);
        m_navButtons.append(button);
        // 从左到右挨着摆在那一层自己的布局里（那一层被两段等长 stretch 夹在条的中正）。
        // 显式 AlignVCenter：这一层的**高**是整条（44），按钮只有 28 —— 不写对齐就是
        // 赌 QBoxLayout 对 Fixed 那一档的摆法（constructor 里那颗窗口按钮同一个道理）。
        m_navLayout->addWidget(button, 0, Qt::AlignVCenter);
        connect(button, &QAbstractButton::clicked, this, [this, i]() { emit navRequested(i); });
    }
    // 这一批新按钮的颜色（`m_currentNav` 刚被清成 -1 ⇒ 四颗一律次要色；要留下来的是
    // `setCurrentNav()` 那一次重设）。
    applyColors();
}

void TitleBar::setCurrentNav(int index) {
    m_currentNav = (index >= 0 && index < m_navButtons.size()) ? index : -1;
    for (int i = 0; i < m_navButtons.size(); ++i) {
        if (auto *button = qobject_cast<TitleBarButton *>(m_navButtons.at(i))) {
            button->setCurrent(i == m_currentNav);
        }
    }
    // 墨色（正文 / 次要）跟 `m_currentNav` 一起重算。
    applyColors();
}

QAbstractButton *TitleBar::navButton(int index) const {
    return index >= 0 && index < m_navButtons.size() ? m_navButtons.at(index) : nullptr;
}

void TitleBar::setBrandText(const QString &text) {
    const QString trimmed = text.trimmed();
    if (m_brand == trimmed) {
        return;
    }
    m_brand = trimmed;
    // 位置不用重算：名字那一段地方是**画出来**的（左留白不跟着它变，见 constructor），
    // 所以这里只有"重画一次"。
    update();
}

QString TitleBar::titleText() const {
    // 空 = 没换过（也含"用户把框清空了"）⇒ 回默认那五个字母。
    return m_brand.isEmpty() ? defaultBrandText() : m_brand;
}

void TitleBar::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    p.drawPixmap(QPoint(kLeftPadding, (height() - kLogoSize) / 2), VersionIcons::grassBlock(kLogoSize));

    const QString brand = titleText();
    if (brand.isEmpty()) {
        return;
    }
    // 右边到哪儿为止：**第一颗导航按钮**的左边缘（不是最小化那颗 —— 那一段是拖窗区）。
    // 导航那一组居中之后名字能用的地方长了，但一样要有个头：名字是用户自己填的
    // （最长 MainWindowUI::titleTextMaxLength()），撞上导航组就省略号截掉。
    // 布局摆完之后 x() 才是准的；一颗导航都没有时退到最小化那颗，再退到整条。
    int right = width();
    if (m_navButtons.isEmpty()) {
        right = m_minimize != nullptr && m_minimize->x() > 0 ? m_minimize->x() : width();
    } else if (m_navButtons.first()->x() > 0) {
        right = m_navButtons.first()->x();
    }
    const int left = kLeftPadding + kLogoSize + kLogoGap;
    const QRect textRect(left, 0, qMax(0, right - kBrandGap - left), height());
    if (textRect.width() <= 0) {
        return;
    }
    QFont titleFont = font();
    titleFont.setBold(true);
    p.setFont(titleFont);
    p.setPen(palette().color(QPalette::WindowText));
    const QFontMetrics metrics(titleFont);
    p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
               metrics.elidedText(brand, Qt::ElideRight, textRect.width()));
}
