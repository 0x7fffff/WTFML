/**
 * @file TitleBar.cpp
 * @brief 左导航轨与窗口按钮条的摆法与画法。
 */

#include "TitleBar.hpp"

#include "Animations.hpp"
#include "Theme.hpp"
#include "VersionIcons.hpp"

#include <QApplication>
#include <QEasingCurve>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include <QShowEvent>

namespace {

// 设计稿第二批 tokens.md 第 1、2 节。
constexpr int kRailWidth = 208;
constexpr int kRailMiniWidth = 64;
constexpr int kRailPadding = 12;
constexpr int kRailMiniPadding = 10;
constexpr int kRailTopPadding = 14;
constexpr int kNarrowBreakpoint = 1024;
constexpr int kNavItemWidth = kRailWidth - 2 * kRailPadding;
constexpr int kNavItemHeight = 42;
constexpr int kNavSpacing = 4;
constexpr int kNavFontPixels = 16;
constexpr int kNavIconSize = 20;
constexpr int kNavIconGap = 14;
constexpr int kNavPaddingX = 14;
/// 品牌那一行：高 44，下面再空 18 才是第一颗导航。
constexpr int kBrandHeight = 44;
constexpr int kBrandGap = 18;
constexpr int kLogoSize = 22;
constexpr int kFoldSize = 28;
constexpr int kCaptionHeight = 40;
constexpr int kWindowButtonWidth = 46;
constexpr int kWindowButtonsRightMargin = 0;
constexpr int kRadius = 10;
constexpr int kIndicatorWidth = 3;
constexpr int kIndicatorInset = 13;
constexpr int kHoverAnimationMs = 120;
constexpr int kSlideAnimationMs = 180;
/// 比这个窄就按图标轨画（宽度动画过程中也按这条判）。
constexpr int kCompactButtonWidth = 80;
const QColor kCloseHover(0xC4, 0x2B, 0x1C);
const QColor kClosePressed(0xA8, 0x25, 0x18);

QColor blendedColor(const QColor &from, const QColor &to, qreal progress) {
    const qreal t = qBound(0.0, progress, 1.0);
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * t,
                            from.greenF() + (to.greenF() - from.greenF()) * t,
                            from.blueF() + (to.blueF() - from.blueF()) * t,
                            from.alphaF() + (to.alphaF() - from.alphaF()) * t);
}

TitleBarButton::NavIcon iconForLabel(const QString &label) {
    if (label == QStringLiteral("主页")) return TitleBarButton::NavIcon::Home;
    if (label == QStringLiteral("管理")) return TitleBarButton::NavIcon::Instances;
    if (label == QStringLiteral("下载")) return TitleBarButton::NavIcon::Download;
    if (label == QStringLiteral("设置")) return TitleBarButton::NavIcon::Settings;
    if (label == QStringLiteral("智能体")) return TitleBarButton::NavIcon::Agent;
    if (label == QStringLiteral("工具箱")) return TitleBarButton::NavIcon::Toolbox;
    return TitleBarButton::NavIcon::None;
}

QString iconName(TitleBarButton::NavIcon icon) {
    switch (icon) {
    case TitleBarButton::NavIcon::Home: return QStringLiteral("home");
    case TitleBarButton::NavIcon::Instances: return QStringLiteral("manage");
    case TitleBarButton::NavIcon::Download: return QStringLiteral("download");
    case TitleBarButton::NavIcon::Settings: return QStringLiteral("settings");
    case TitleBarButton::NavIcon::Agent: return QStringLiteral("agent");
    case TitleBarButton::NavIcon::Toolbox: return QStringLiteral("toolbox");
    case TitleBarButton::NavIcon::None: break;
    }
    return QString();
}

int railPaddingFor(int width) { return width < (kRailWidth + kRailMiniWidth) / 2 ? kRailMiniPadding : kRailPadding; }

}  // namespace

QString TitleBar::defaultBrandText() { return QStringLiteral("WTFML"); }
int TitleBar::railWidth() { return kRailWidth; }
int TitleBar::collapsedRailWidth() { return kRailMiniWidth; }
int TitleBar::narrowBreakpoint() { return kNarrowBreakpoint; }
int TitleBar::barHeight() { return kCaptionHeight; }

void TitleBar::styleCaptionButtons(QAbstractButton *minimize, QAbstractButton *maximize,
                                   QAbstractButton *close, bool hero) {
    const Theme::Tokens t = Theme::tokens();
    const QColor captionInk = hero ? t.heroInk2 : t.secondary;
    const QColor captionHoverInk = hero ? t.heroInk : t.text;
    for (QAbstractButton *b : {minimize, maximize}) {
        if (auto *button = qobject_cast<TitleBarButton *>(b)) {
            button->setHoverColors(t.hover, captionHoverInk);
            button->setPressColor(t.fillPress);
            button->setInkColor(captionInk);
        }
    }
    if (auto *button = qobject_cast<TitleBarButton *>(close)) {
        button->setHoverColors(kCloseHover, QColor(Qt::white));
        button->setPressColor(kClosePressed);
        button->setInkColor(captionInk);
    }
}
int TitleBar::buttonSize() { return kCaptionHeight; }
int TitleBar::windowButtonWidth() { return kWindowButtonWidth; }
int TitleBar::buttonInset() { return 0; }
int TitleBar::navItemWidth() { return kNavItemWidth; }
int TitleBar::navItemHeight() { return kNavItemHeight; }
int TitleBar::navFontPixelSize() { return kNavFontPixels; }
int TitleBar::windowButtonsRightMargin() { return kWindowButtonsRightMargin; }

TitleBarButton::TitleBarButton(Glyph glyph, const QColor &hoverColor, const QColor &hoverInk, QWidget *parent)
    : QAbstractButton(parent), m_glyph(glyph), m_hoverColor(hoverColor), m_hoverInk(hoverInk) {
    m_ink = palette().color(QPalette::WindowText);
    if (glyph == Glyph::Minimize || glyph == Glyph::Maximize || glyph == Glyph::Close) {
        setFixedSize(kWindowButtonWidth, kCaptionHeight);
    } else if (glyph == Glyph::Fold) {
        setFixedSize(kFoldSize, kFoldSize);
    } else if (glyph == Glyph::Logo) {
        setFixedSize(kLogoSize, kLogoSize);
    }
    // Qt 不会自己产生 HoverEnter/HoverLeave：WA_Hover + mouseTracking 两条缺一不可。
    setAttribute(Qt::WA_Hover, true);
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    m_hoverAnimation = new QVariantAnimation(this);
    m_hoverAnimation->setDuration(kHoverAnimationMs);
    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_hoverAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_hoverProgress = value.toReal();
        update();
    });
}

void TitleBarButton::setNavIcon(NavIcon icon) {
    m_icon = icon;
    update();
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

void TitleBarButton::setPressColor(const QColor &color) {
    m_pressColor = color;
    update();
}

void TitleBarButton::setInkColor(const QColor &color) {
    m_ink = color;
    update();
}

void TitleBarButton::setCurrent(bool current) {
    if (m_current == current) return;
    m_current = current;
    update();
}

void TitleBarButton::setRestoreGlyph(bool restore) {
    if (m_restore == restore) return;
    m_restore = restore;
    update();
}

bool TitleBarButton::event(QEvent *event) {
    const bool enter = event->type() == QEvent::HoverEnter;
    if (enter || event->type() == QEvent::HoverLeave) {
        m_hovered = enter;
        m_hoverAnimation->stop();
        if (!Anim::enabled()) {
            m_hoverProgress = enter ? 1.0 : 0.0;
            update();
        } else {
            m_hoverAnimation->setStartValue(m_hoverProgress);
            m_hoverAnimation->setEndValue(enter ? 1.0 : 0.0);
            m_hoverAnimation->start();
        }
    }
    return QAbstractButton::event(event);
}

void TitleBarButton::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (m_glyph == Glyph::Logo) {
        p.drawPixmap(rect(), VersionIcons::grassBlock(kLogoSize));
        return;
    }
    if (!isEnabled()) p.setOpacity(0.6);
    // 当前那颗的底由轨自己画（会滑），这里只画悬停 / 按下那层中性灰。
    QColor base = blendedColor(m_baseColor, m_current ? QColor(Qt::transparent) : m_hoverColor, m_hoverProgress);
    if (isDown() && !m_current && m_pressColor.isValid()) base = m_pressColor;
    if (base.alpha() > 0) {
        p.setPen(Qt::NoPen);
        p.setBrush(base);
        if (m_glyph == Glyph::Close) {
            // 窗口右上角那颗：只有右上角跟窗口一起圆（8），其余直角。
            QPainterPath shape;
            const QRectF r(rect());
            shape.moveTo(r.topLeft());
            shape.lineTo(r.right() - 8, r.top());
            shape.quadTo(r.topRight(), QPointF(r.right(), r.top() + 8));
            shape.lineTo(r.bottomRight());
            shape.lineTo(r.bottomLeft());
            shape.closeSubpath();
            p.drawPath(shape);
        } else if (m_glyph == Glyph::Minimize || m_glyph == Glyph::Maximize) {
            p.drawRect(rect());
        } else {
            const qreal radius = m_glyph == Glyph::Fold ? 8 : kRadius;
            p.drawRoundedRect(QRectF(rect()), radius, radius);
        }
    }
    const QColor ink = !isEnabled() ? Theme::tokens().disabled
                       : m_current  ? m_ink
                                    : blendedColor(m_ink, m_hoverInk, m_hoverProgress);
    switch (m_glyph) {
    case Glyph::Text: {
        const bool compact = width() < kCompactButtonWidth;
        const qreal iconX = compact ? (width() - kNavIconSize) / 2.0 : kNavPaddingX;
        const QRectF iconBox(iconX, (height() - kNavIconSize) / 2.0, kNavIconSize, kNavIconSize);
        VersionIcons::paintLine(p, iconName(m_icon), iconBox, ink);
        if (compact) return;
        QFont f = font();
        f.setLetterSpacing(QFont::AbsoluteSpacing, 1);
        p.setFont(f);
        p.setPen(ink);
        const int textLeft = int(iconBox.right()) + kNavIconGap;
        p.drawText(QRect(textLeft, 0, width() - textLeft - 8, height()), Qt::AlignVCenter | Qt::AlignLeft,
                   QFontMetrics(f).elidedText(text(), Qt::ElideRight, width() - textLeft - 8));
        return;
    }
    case Glyph::Fold:
        VersionIcons::paintLine(p, QStringLiteral("fold"), QRectF(5, 5, 18, 18), ink);
        return;
    case Glyph::Minimize:
        VersionIcons::paintLine(p, QStringLiteral("min"), QRectF((width() - 16) / 2.0, (height() - 16) / 2.0, 16, 16), ink, 1.4 * 24 / 16);
        return;
    case Glyph::Maximize:
        VersionIcons::paintLine(p, m_restore ? QStringLiteral("restore") : QStringLiteral("max"),
                                QRectF((width() - 15) / 2.0, (height() - 15) / 2.0, 15, 15), ink, 1.4 * 24 / 15);
        return;
    case Glyph::Close:
        VersionIcons::paintLine(p, QStringLiteral("close"), QRectF((width() - 16) / 2.0, (height() - 16) / 2.0, 16, 16), ink, 1.4 * 24 / 16);
        return;
    case Glyph::Logo:
        return;
    }
}

TitleBar::TitleBar(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("titleBar"));
    setFixedWidth(kRailWidth);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_pillAnimation = new QVariantAnimation(this);
    m_pillAnimation->setDuration(kSlideAnimationMs);
    m_pillAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_pillAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_pillProgress = value.toReal();
        update();
    });
    m_widthAnimation = new QVariantAnimation(this);
    m_widthAnimation->setDuration(kSlideAnimationMs);
    {
        // 设计稿动效曲线 cubic-bezier(.2,.8,.2,1)。
        QEasingCurve curve(QEasingCurve::BezierSpline);
        curve.addCubicBezierSegment(QPointF(0.2, 0.8), QPointF(0.2, 1.0), QPointF(1.0, 1.0));
        m_widthAnimation->setEasingCurve(curve);
        m_pillAnimation->setEasingCurve(curve);
    }
    connect(m_widthAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        layoutForWidth(value.toInt());
    });

    // 右上那条：透明，三颗窗口按钮贴顶贴右（调用方把它摆进右栏顶上）。
    m_caption = new QWidget(this);
    m_caption->setObjectName(QStringLiteral("titleBarCaption"));
    m_caption->setFixedHeight(kCaptionHeight);
    auto *captionLayout = new QHBoxLayout(m_caption);
    captionLayout->setContentsMargins(0, 0, kWindowButtonsRightMargin, 0);
    captionLayout->setSpacing(0);
    captionLayout->addStretch(1);
    m_minimize = new TitleBarButton(TitleBarButton::Glyph::Minimize, QColor(), QColor(), m_caption);
    m_minimize->setObjectName(QStringLiteral("titleBarMinimize"));
    m_minimize->setToolTip(QStringLiteral("最小化"));
    m_maximize = new TitleBarButton(TitleBarButton::Glyph::Maximize, QColor(), QColor(), m_caption);
    m_maximize->setObjectName(QStringLiteral("titleBarMaximize"));
    m_maximize->setToolTip(QStringLiteral("最大化"));
    m_close = new TitleBarButton(TitleBarButton::Glyph::Close, QColor(), QColor(), m_caption);
    m_close->setObjectName(QStringLiteral("titleBarClose"));
    m_close->setToolTip(QStringLiteral("关闭"));
    captionLayout->addWidget(m_minimize, 0, Qt::AlignTop);
    captionLayout->addWidget(m_maximize, 0, Qt::AlignTop);
    captionLayout->addWidget(m_close, 0, Qt::AlignTop);
    connect(m_minimize, &QAbstractButton::clicked, this, &TitleBar::minimizeRequested);
    connect(m_maximize, &QAbstractButton::clicked, this, &TitleBar::maximizeRequested);
    connect(m_close, &QAbstractButton::clicked, this, &TitleBar::closeRequested);

    // 品牌行：草方块（折叠时点它展开）+ 名字（画的）+ 折叠钮。
    m_logo = new TitleBarButton(TitleBarButton::Glyph::Logo, QColor(), QColor(), this);
    m_logo->setObjectName(QStringLiteral("titleBarLogo"));
    connect(m_logo, &QAbstractButton::clicked, this, [this]() {
        if (m_userCollapsed && !m_narrow) setCollapsed(false);
    });
    m_fold = new TitleBarButton(TitleBarButton::Glyph::Fold, QColor(), QColor(), this);
    m_fold->setObjectName(QStringLiteral("titleBarFold"));
    m_fold->setToolTip(QStringLiteral("收起导航"));
    connect(m_fold, &QAbstractButton::clicked, this, [this]() { setCollapsed(true); });

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(kRailPadding, kRailTopPadding + kBrandHeight + kBrandGap, kRailPadding, kRailTopPadding);
    m_layout->setSpacing(0);
    m_navLayout = new QVBoxLayout;
    m_navLayout->setContentsMargins(0, 0, 0, 0);
    m_navLayout->setSpacing(kNavSpacing);
    m_layout->addLayout(m_navLayout);
    m_layout->addStretch(1);
    layoutForWidth(kRailWidth);
    applyColors();
}

void TitleBar::setFooter(QWidget *footer) {
    if (m_footer != nullptr) {
        m_layout->removeWidget(m_footer);
        m_footer->deleteLater();
    }
    m_footer = footer;
    if (footer != nullptr) m_layout->addWidget(footer);
}

void TitleBar::applyColors() {
    if (m_applyingColors) return;
    m_applyingColors = true;
    const Theme::Tokens t = Theme::tokens();
    QPalette railPalette = QApplication::palette();
    // 只改可用 / 非活动两组：Disabled 那一档灰要留着（轨底的实例状态方块「没在跑」就取它）。
    railPalette.setColor(QPalette::Active, QPalette::WindowText, t.text);
    railPalette.setColor(QPalette::Inactive, QPalette::WindowText, t.text);
    setPalette(railPalette);
    setAutoFillBackground(false);
    styleCaptionButtons(m_minimize, m_maximize, m_close, m_hero);
    if (auto *button = qobject_cast<TitleBarButton *>(m_fold)) {
        button->setHoverColors(t.hover, t.secondary);
        button->setPressColor(t.fillPress);
        button->setInkColor(t.disabled);
    }
    for (int i = 0; i < m_navButtons.size(); ++i) {
        if (auto *button = qobject_cast<TitleBarButton *>(m_navButtons.at(i))) {
            button->setHoverColors(t.hover, t.text);
            button->setPressColor(t.fillPress);
            button->setInkColor(i == m_currentNav ? t.deep : t.secondary);
        }
    }
    m_applyingColors = false;
    update();
}

void TitleBar::changeEvent(QEvent *event) {
    switch (event->type()) {
    case QEvent::PaletteChange:
    case QEvent::ApplicationPaletteChange:
    case QEvent::StyleChange:
        applyColors();
        break;
    default:
        break;
    }
    QWidget::changeEvent(event);
}

void TitleBar::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    if (!m_watchingWindow && window() != this) {
        m_watchingWindow = true;
        window()->installEventFilter(this);
    }
    if (window() != this) {
        const bool narrow = window()->width() < kNarrowBreakpoint;
        if (narrow != m_narrow) {
            m_narrow = narrow;
            applyWidth(false);
        }
        setMaximized(window()->isMaximized());
    }
}

bool TitleBar::eventFilter(QObject *watched, QEvent *event) {
    if (watched == window()) {
        if (event->type() == QEvent::Resize) {
            const bool narrow = window()->width() < kNarrowBreakpoint;
            if (narrow != m_narrow) {
                const bool wasCollapsed = isCollapsed();
                m_narrow = narrow;
                if (wasCollapsed != isCollapsed()) applyWidth(isVisible());
                else layoutForWidth(width());
            }
        } else if (event->type() == QEvent::WindowStateChange) {
            setMaximized(window()->isMaximized());
        }
    }
    return QWidget::eventFilter(watched, event);
}

void TitleBar::setCollapsed(bool collapsed) {
    if (m_userCollapsed == collapsed) return;
    const bool wasCollapsed = isCollapsed();
    m_userCollapsed = collapsed;
    if (wasCollapsed != isCollapsed()) applyWidth(isVisible());
    else layoutForWidth(width());
}

void TitleBar::applyWidth(bool animate) {
    const int target = isCollapsed() ? kRailMiniWidth : kRailWidth;
    m_widthAnimation->stop();
    if (animate && Anim::enabled() && width() != target) {
        m_widthAnimation->setStartValue(width());
        m_widthAnimation->setEndValue(target);
        m_widthAnimation->start();
    } else {
        layoutForWidth(target);
    }
    emit collapsedChanged(isCollapsed());
}

void TitleBar::layoutForWidth(int w) {
    setFixedWidth(w);
    const int pad = railPaddingFor(w);
    m_layout->setContentsMargins(pad, kRailTopPadding + kBrandHeight + kBrandGap, pad, kRailTopPadding);
    for (QAbstractButton *button : std::as_const(m_navButtons)) button->setFixedSize(w - 2 * pad, kNavItemHeight);
    const bool compact = w < (kRailWidth + kRailMiniWidth) / 2;
    // 品牌行：展开时 padding 0 10，图标轨时 0 11（草方块对准导航图标）。
    const int logoX = compact ? pad + 11 : pad + 10;
    m_logo->move(logoX, kRailTopPadding + (kBrandHeight - kLogoSize) / 2);
    const bool expandable = isCollapsed() && !m_narrow;
    m_logo->setAttribute(Qt::WA_TransparentForMouseEvents, !expandable);
    m_logo->setCursor(expandable ? Qt::PointingHandCursor : Qt::ArrowCursor);
    m_logo->setToolTip(expandable ? QStringLiteral("展开导航") : QString());
    m_fold->move(w - pad - 10 - kFoldSize + 8, kRailTopPadding + (kBrandHeight - kFoldSize) / 2);
    m_fold->setVisible(!compact);
    update();
}

void TitleBar::setMaximized(bool maximized) {
    if (auto *button = qobject_cast<TitleBarButton *>(m_maximize)) {
        button->setRestoreGlyph(maximized);
        button->setToolTip(maximized ? QStringLiteral("还原") : QStringLiteral("最大化"));
    }
}

void TitleBar::setHeroMode(bool hero) {
    if (m_hero == hero) return;
    m_hero = hero;
    applyColors();
}

void TitleBar::setNavItems(const QStringList &labels) {
    for (QAbstractButton *button : m_navButtons) {
        m_navLayout->removeWidget(button);
        button->setParent(nullptr);
        button->deleteLater();
    }
    m_navButtons.clear();
    m_currentNav = -1;
    QFont navFont = Theme::handFont(font(), Theme::HandFont::Kai);
    navFont.setPixelSize(kNavFontPixels);
    const int pad = railPaddingFor(width());
    for (int i = 0; i < labels.size(); ++i) {
        auto *button = new TitleBarButton(TitleBarButton::Glyph::Text, QColor(), QColor(), this);
        button->setObjectName(QStringLiteral("titleBarNav%1").arg(i));
        button->setLabel(labels.at(i));
        button->setNavIcon(iconForLabel(labels.at(i)));
        button->setFont(navFont);
        button->setFixedSize(width() - 2 * pad, kNavItemHeight);
        button->setAccessibleName(labels.at(i));
        button->setToolTip(labels.at(i));
        m_navButtons.append(button);
        m_navLayout->addWidget(button);
        connect(button, &QAbstractButton::clicked, this, [this, i]() { emit navRequested(i); });
    }
    applyColors();
}

void TitleBar::setCurrentNav(int index) {
    const int previous = m_currentNav;
    m_currentNav = (index >= 0 && index < m_navButtons.size()) ? index : -1;
    m_pillAnimation->stop();
    if (previous >= 0 && previous != m_currentNav && Anim::enabled() && isVisible()) {
        m_pillFrom = previous;
        m_pillProgress = 0.0;
        m_pillAnimation->setStartValue(0.0);
        m_pillAnimation->setEndValue(1.0);
        m_pillAnimation->start();
    } else {
        m_pillFrom = -1;
        m_pillProgress = 1.0;
    }
    for (int i = 0; i < m_navButtons.size(); ++i) {
        if (auto *button = qobject_cast<TitleBarButton *>(m_navButtons.at(i))) button->setCurrent(i == m_currentNav);
    }
    applyColors();
}

QAbstractButton *TitleBar::navButton(int index) const {
    return index >= 0 && index < m_navButtons.size() ? m_navButtons.at(index) : nullptr;
}

void TitleBar::setBrandText(const QString &text) {
    const QString trimmed = text.trimmed();
    if (m_brand == trimmed) return;
    m_brand = trimmed;
    update();
}

QString TitleBar::titleText() const { return m_brand.isEmpty() ? defaultBrandText() : m_brand; }

void TitleBar::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const Theme::Tokens t = Theme::tokens();
    // 轨本身是磨砂玻璃；用户在个性化里给「标题栏」单独设过底色时才铺那一层。
    if (Theme::partHasBackground(Theme::Part::TitleBar))
        p.fillRect(rect(), Theme::partBackground(Theme::Part::TitleBar));

    // 选中那一格：accent-soft 底 + 左边 3px 指示条（上下各缩 13），从上一颗滑过来。
    if (QAbstractButton *to = navButton(m_currentNav)) {
        QRectF rect = to->geometry();
        if (QAbstractButton *from = navButton(m_pillFrom); from != nullptr && m_pillProgress < 1.0) {
            const QRectF a = from->geometry();
            rect.moveTop(a.top() + (rect.top() - a.top()) * m_pillProgress);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(t.selected);
        p.drawRoundedRect(rect, kRadius, kRadius);
        p.setBrush(t.deep);
        p.drawRoundedRect(QRectF(rect.left() + 3, rect.top() + kIndicatorInset, kIndicatorWidth,
                                 rect.height() - 2 * kIndicatorInset),
                          2, 2);
    }

    // 品牌名（西文手写体 30）；图标轨时只剩草方块。
    if (width() < (kRailWidth + kRailMiniWidth) / 2) return;
    const QString brand = titleText();
    if (brand.isEmpty()) return;
    QFont brandFont = Theme::handFont(font(), Theme::HandFont::Script);
    brandFont.setPixelSize(30);
    brandFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
    p.setFont(brandFont);
    p.setPen(m_hero ? t.heroInk : t.text);
    const int left = m_logo->geometry().right() + 1 + 10;
    const int right = m_fold->isVisible() ? m_fold->geometry().left() - 4 : width() - kRailPadding;
    const QRect textRect(left, kRailTopPadding + 2, right - left, kBrandHeight);
    p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
               QFontMetrics(brandFont).elidedText(brand, Qt::ElideRight, textRect.width()));
}
