/**
 * @file TitleBar.cpp
 * @brief TitleBar 的实现（条上那几样东西的摆法与画法）。
 * @author 0x7fffff
 * @date 2026-09-25
 */

#include "TitleBar.hpp"

#include "MainWindowUI.hpp"   // folderDangerColor()：全仓那一处"危险操作的红"
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
/// 那两颗按钮的边长（逻辑像素）。
constexpr int kButtonSize = 28;
/// 草方块那颗图标的边长。16 的倍数最清楚（见 `VersionIcons::grassBlock()` 的说明）。
constexpr int kLogoSize = 16;
/// 草方块离窗口左边缘多远。
constexpr int kLeftPadding = 12;
/// 草方块和名字之间空多少。
constexpr int kLogoGap = 8;
/// 那两颗按钮的字形边长。18 ⇒ `VersionIcons` 那套 22%~78% 的边距正好给出 10 px 的笔画跨度，
/// 和系统那两颗按钮上的字形差不多大。
constexpr int kGlyphSize = 18;

/// 条的颜色：窗口底色压暗一点。**和底栏那一句一模一样**（见 `MainWindowUI::setupBottomBar`
/// 里那句 `darker(105)`）—— 上下两条框同一档颜色，中间那块内容区才像"嵌在里面"。
QColor barColor(const QPalette &palette) {
    return palette.color(QPalette::Window).darker(105);
}

}  // namespace

int TitleBar::barHeight() {
    return kTitleBarHeight;
}

int TitleBar::buttonSize() {
    return kButtonSize;
}

int TitleBar::buttonInset() {
    return (kTitleBarHeight - kButtonSize) / 2;
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

void TitleBarButton::setInkColor(const QColor &color) {
    m_ink = color;
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
    const QPixmap glyph = m_glyph == Glyph::Close ? VersionIcons::cross(kGlyphSize, ink)
                                                 : VersionIcons::windowMinimize(kGlyphSize, ink);
    p.drawPixmap(QPoint((width() - kGlyphSize) / 2, (height() - kGlyphSize) / 2), glyph);
}

TitleBar::TitleBar(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("titleBar"));
    setFixedHeight(kTitleBarHeight);
    setAutoFillBackground(true);
    // NOTE: 条底色**先算出来、存一份**，再写进调色板。不能"写进调色板之后再 `barColor(palette())`
    //       取一次"——那时候 palette() 里的 Window 已经是压暗过一档的那个，再 `barColor()` 就是
    //       **第二次压暗**（2026-09-25 之前就是这么写的，于是最小化那颗的悬停色比它该有的
    //       深了两档：量出来 #bdbdbd，而"条底色再压暗一档"应该是 #c6c6c6）。注释一直写的是对的，
    //       代码不是 —— ui_check Z11 那条像素断言就是照着这句话量的。
    const QColor chrome = barColor(palette());
    QPalette barPalette = palette();
    barPalette.setColor(QPalette::Window, chrome);
    setPalette(barPalette);

    // 悬停底色：最小化那颗是条底色再压暗一档（"有个东西在里头"就够了），
    // 关闭那颗是**红底白叉** —— 那个红取自 folderDangerColor()，全仓只有这一个出处。
    m_minimize = new TitleBarButton(TitleBarButton::Glyph::Minimize, chrome.darker(115),
                                     palette().color(QPalette::WindowText), this);
    m_minimize->setObjectName(QStringLiteral("titleBarMinimize"));
    m_close = new TitleBarButton(TitleBarButton::Glyph::Close, MainWindowUI::folderDangerColor(palette()),
                                  QColor(Qt::white), this);
    m_close->setObjectName(QStringLiteral("titleBarClose"));

    // 条上只有这两个子控件。**这不是随手摆的**：`WindowChrome::handleNativeEvent()` 判"这一手
    // 是不是落在按钮上"用的就是 `strip->childAt()`，多一个子控件（哪怕是个铺满的空白 label）
    // 就会把拖窗区从它底下抢走。中间那一片必须是空的，草方块和名字都是 paintEvent 画的。
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addStretch(1);
    // 显式 AlignVCenter：靠尺寸策略去居中是在赌 QBoxLayout 对 Fixed 那一档的摆法。
    layout->addWidget(m_minimize, 0, Qt::AlignVCenter);
    layout->addWidget(m_close, 0, Qt::AlignVCenter);

    connect(m_minimize, &QAbstractButton::clicked, this, &TitleBar::minimizeRequested);
    connect(m_close, &QAbstractButton::clicked, this, &TitleBar::closeRequested);
}

QString TitleBar::titleText() const {
    const QWidget *top = window();
    return top != nullptr ? top->windowTitle() : QString();
}

void TitleBar::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    p.drawPixmap(QPoint(kLeftPadding, (height() - kLogoSize) / 2), VersionIcons::grassBlock(kLogoSize));

    const QString title = titleText();
    if (title.isEmpty()) {
        return;
    }
    // 右边到哪儿为止：那一列按钮的左边缘（布局摆完之后 x() 才是准的；没摆过就整条都能写）。
    const int right = m_minimize != nullptr && m_minimize->x() > 0 ? m_minimize->x() : width();
    const int left = kLeftPadding + kLogoSize + kLogoGap;
    const QRect textRect(left, 0, qMax(0, right - kLogoGap - left), height());
    if (textRect.width() <= 0) {
        return;
    }
    QFont titleFont = font();
    titleFont.setBold(true);
    p.setFont(titleFont);
    p.setPen(palette().color(QPalette::WindowText));
    const QFontMetrics metrics(titleFont);
    p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
               metrics.elidedText(title, Qt::ElideRight, textRect.width()));
}
