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

namespace {

constexpr int kRailWidth = 196;
constexpr int kRailPadding = 12;
constexpr int kNavItemWidth = kRailWidth - 2 * kRailPadding;
constexpr int kNavItemHeight = 40;
constexpr int kNavSpacing = 4;
constexpr int kNavFontPixels = 16;
/// 品牌那一格的高：草方块 + 名字，也是一段拖窗区。
constexpr int kHeaderHeight = 72;
constexpr int kLogoSize = 18;
/// 右上那条窗口按钮条：高 44 = 8 + 28 + 8，上下各留的 8 不小于上沿那圈缩放边。
constexpr int kCaptionHeight = 44;
constexpr int kButtonSize = 28;
constexpr int kWindowButtonsRightMargin = 8;
constexpr int kRadius = 10;
constexpr int kIndicatorWidth = 3;
constexpr int kHoverAnimationMs = 120;
constexpr int kSlideAnimationMs = 180;

QColor blendedColor(const QColor &from, const QColor &to, qreal progress) {
    const qreal t = qBound(0.0, progress, 1.0);
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * t,
                            from.greenF() + (to.greenF() - from.greenF()) * t,
                            from.blueF() + (to.blueF() - from.blueF()) * t,
                            from.alphaF() + (to.alphaF() - from.alphaF()) * t);
}

QColor withAlpha(QColor c, int alpha) {
    c.setAlpha(alpha);
    return c;
}

TitleBarButton::NavIcon iconForLabel(const QString &label) {
    if (label == QStringLiteral("主页")) return TitleBarButton::NavIcon::Home;
    if (label == QStringLiteral("管理")) return TitleBarButton::NavIcon::Instances;
    if (label == QStringLiteral("下载")) return TitleBarButton::NavIcon::Download;
    if (label == QStringLiteral("设置")) return TitleBarButton::NavIcon::Settings;
    if (label == QStringLiteral("智能体")) return TitleBarButton::NavIcon::Agent;
    return TitleBarButton::NavIcon::None;
}

/// 18 × 18 的细线条图标，`box` 是它占的那一格。
void drawNavIcon(QPainter &p, TitleBarButton::NavIcon icon, const QRectF &box, const QColor &ink) {
    p.save();
    p.translate(box.topLeft());
    p.scale(box.width() / 18.0, box.height() / 18.0);
    p.setPen(QPen(ink, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    QPainterPath path;
    switch (icon) {
    case TitleBarButton::NavIcon::Home:
        path.moveTo(2.5, 8.5);
        path.lineTo(9, 3);
        path.lineTo(15.5, 8.5);
        path.moveTo(4.5, 7.2);
        path.lineTo(4.5, 15);
        path.lineTo(13.5, 15);
        path.lineTo(13.5, 7.2);
        path.moveTo(7.6, 15);
        path.lineTo(7.6, 11);
        path.lineTo(10.4, 11);
        path.lineTo(10.4, 15);
        break;
    case TitleBarButton::NavIcon::Instances:
        path.addRoundedRect(QRectF(3, 3, 5, 5), 1.4, 1.4);
        path.addRoundedRect(QRectF(10, 3, 5, 5), 1.4, 1.4);
        path.addRoundedRect(QRectF(3, 10, 5, 5), 1.4, 1.4);
        path.addRoundedRect(QRectF(10, 10, 5, 5), 1.4, 1.4);
        break;
    case TitleBarButton::NavIcon::Download:
        path.moveTo(9, 2.8);
        path.lineTo(9, 11);
        path.moveTo(5.6, 7.8);
        path.lineTo(9, 11.2);
        path.lineTo(12.4, 7.8);
        path.moveTo(3, 12.2);
        path.lineTo(3, 15);
        path.lineTo(15, 15);
        path.lineTo(15, 12.2);
        break;
    case TitleBarButton::NavIcon::Settings:
        for (const auto &[y, knob] : {std::pair{4.5, 11.5}, std::pair{9.0, 6.0}, std::pair{13.5, 12.5}}) {
            path.moveTo(3, y);
            path.lineTo(knob - 1.8, y);
            path.moveTo(knob + 1.8, y);
            path.lineTo(15, y);
            path.addEllipse(QPointF(knob, y), 1.8, 1.8);
        }
        break;
    case TitleBarButton::NavIcon::Agent:
        path.moveTo(8, 2.5);
        path.quadTo(8.6, 7.4, 13.5, 8);
        path.quadTo(8.6, 8.6, 8, 13.5);
        path.quadTo(7.4, 8.6, 2.5, 8);
        path.quadTo(7.4, 7.4, 8, 2.5);
        path.moveTo(13.5, 12);
        path.lineTo(13.5, 15.5);
        path.moveTo(11.75, 13.75);
        path.lineTo(15.25, 13.75);
        break;
    case TitleBarButton::NavIcon::None:
        path.addEllipse(QPointF(9, 9), 2, 2);
        break;
    }
    p.drawPath(path);
    p.restore();
}

}  // namespace

QString TitleBar::defaultBrandText() { return QStringLiteral("WTFML"); }
int TitleBar::railWidth() { return kRailWidth; }
int TitleBar::barHeight() { return kCaptionHeight; }
int TitleBar::buttonSize() { return kButtonSize; }
int TitleBar::buttonInset() { return (kCaptionHeight - kButtonSize) / 2; }
int TitleBar::navItemWidth() { return kNavItemWidth; }
int TitleBar::navItemHeight() { return kNavItemHeight; }
int TitleBar::navFontPixelSize() { return kNavFontPixels; }
int TitleBar::windowButtonsRightMargin() { return kWindowButtonsRightMargin; }

TitleBarButton::TitleBarButton(Glyph glyph, const QColor &hoverColor, const QColor &hoverInk, QWidget *parent)
    : QAbstractButton(parent), m_glyph(glyph), m_hoverColor(hoverColor), m_hoverInk(hoverInk) {
    m_ink = palette().color(QPalette::WindowText);
    setFixedSize(kButtonSize, kButtonSize);
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

void TitleBarButton::setInkColor(const QColor &color) {
    m_ink = color;
    update();
}

void TitleBarButton::setCurrent(bool current) {
    if (m_current == current) return;
    m_current = current;
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
    // 当前那颗的底由轨自己画（会滑），这里只画悬停那层更淡的底。
    const QColor base = blendedColor(m_baseColor, m_current ? QColor(Qt::transparent) : m_hoverColor, m_hoverProgress);
    if (base.alpha() > 0) {
        p.setPen(Qt::NoPen);
        p.setBrush(base);
        p.drawRoundedRect(QRectF(rect()), kRadius, kRadius);
    }
    const QColor ink = m_current ? m_ink : blendedColor(m_ink, m_hoverInk, m_hoverProgress);
    if (m_glyph == Glyph::Text) {
        const qreal iconSize = 18;
        const QRectF iconBox(14, (height() - iconSize) / 2.0, iconSize, iconSize);
        drawNavIcon(p, m_icon, iconBox, ink);
        p.setFont(font());
        p.setPen(ink);
        p.drawText(QRect(int(iconBox.right()) + 12, 0, width() - int(iconBox.right()) - 16, height()),
                   Qt::AlignVCenter | Qt::AlignLeft, text());
        return;
    }
    const QPointF center(width() / 2.0, height() / 2.0);
    p.setPen(QPen(ink, 1.3, Qt::SolidLine, Qt::RoundCap));
    if (m_glyph == Glyph::Close) {
        constexpr qreal halfSpan = 4.5;
        p.drawLine(center + QPointF(-halfSpan, -halfSpan), center + QPointF(halfSpan, halfSpan));
        p.drawLine(center + QPointF(halfSpan, -halfSpan), center + QPointF(-halfSpan, halfSpan));
        return;
    }
    p.drawLine(center + QPointF(-5.0, 0.0), center + QPointF(5.0, 0.0));
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

    // 右上那条：透明，只有两颗窗口按钮（调用方把它摆进右栏顶上）。
    m_caption = new QWidget(this);
    m_caption->setObjectName(QStringLiteral("titleBarCaption"));
    m_caption->setFixedHeight(kCaptionHeight);
    auto *captionLayout = new QHBoxLayout(m_caption);
    captionLayout->setContentsMargins(0, 0, kWindowButtonsRightMargin, 0);
    captionLayout->setSpacing(4);
    captionLayout->addStretch(1);
    m_minimize = new TitleBarButton(TitleBarButton::Glyph::Minimize, QColor(), QColor(), m_caption);
    m_minimize->setObjectName(QStringLiteral("titleBarMinimize"));
    m_close = new TitleBarButton(TitleBarButton::Glyph::Close, QColor(), QColor(), m_caption);
    m_close->setObjectName(QStringLiteral("titleBarClose"));
    captionLayout->addWidget(m_minimize, 0, Qt::AlignVCenter);
    captionLayout->addWidget(m_close, 0, Qt::AlignVCenter);
    connect(m_minimize, &QAbstractButton::clicked, this, &TitleBar::minimizeRequested);
    connect(m_close, &QAbstractButton::clicked, this, &TitleBar::closeRequested);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(kRailPadding, kHeaderHeight, kRailPadding, kRailPadding);
    m_layout->setSpacing(0);
    m_navLayout = new QVBoxLayout;
    m_navLayout->setContentsMargins(0, 0, 0, 0);
    m_navLayout->setSpacing(kNavSpacing);
    m_layout->addLayout(m_navLayout);
    m_layout->addStretch(1);
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
    railPalette.setColor(QPalette::WindowText, t.text);
    setPalette(railPalette);
    setAutoFillBackground(false);
    const QColor hover = withAlpha(t.text, t.page.lightness() < 128 ? 14 : 12);
    if (auto *button = qobject_cast<TitleBarButton *>(m_minimize)) {
        button->setHoverColors(hover, t.text);
        button->setInkColor(t.secondary);
    }
    if (auto *button = qobject_cast<TitleBarButton *>(m_close)) {
        button->setHoverColors(Theme::dangerColor(railPalette), QColor(Qt::white));
        button->setInkColor(t.secondary);
    }
    for (int i = 0; i < m_navButtons.size(); ++i) {
        if (auto *button = qobject_cast<TitleBarButton *>(m_navButtons.at(i))) {
            button->setHoverColors(hover, t.text);
            button->setInkColor(i == m_currentNav ? t.focus : t.secondary);
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
    for (int i = 0; i < labels.size(); ++i) {
        auto *button = new TitleBarButton(TitleBarButton::Glyph::Text, QColor(), QColor(), this);
        button->setObjectName(QStringLiteral("titleBarNav%1").arg(i));
        button->setLabel(labels.at(i));
        button->setNavIcon(iconForLabel(labels.at(i)));
        button->setFont(navFont);
        button->setFixedSize(kNavItemWidth, kNavItemHeight);
        button->setAccessibleName(labels.at(i));
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

    // 选中那一格：淡主题色底 + 左边一根指示条，从上一颗滑过来。
    if (QAbstractButton *to = navButton(m_currentNav)) {
        QRectF rect = to->geometry();
        if (QAbstractButton *from = navButton(m_pillFrom); from != nullptr && m_pillProgress < 1.0) {
            const QRectF a = from->geometry();
            rect.moveTop(a.top() + (rect.top() - a.top()) * m_pillProgress);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(withAlpha(t.focus, t.page.lightness() < 128 ? 34 : 30));
        p.drawRoundedRect(rect, kRadius, kRadius);
        p.setBrush(t.focus);
        const qreal barHeight = rect.height() * 0.44;
        p.drawRoundedRect(QRectF(rect.left() + 2, rect.center().y() - barHeight / 2, kIndicatorWidth, barHeight),
                          kIndicatorWidth / 2.0, kIndicatorWidth / 2.0);
    }

    // 品牌：草方块 + 名字（西文手写体）。
    const int brandY = 22;
    const int logoX = kRailPadding + 10;
    p.drawPixmap(QPoint(logoX, brandY + 4), VersionIcons::grassBlock(kLogoSize));
    const QString brand = titleText();
    if (brand.isEmpty()) return;
    QFont brandFont = Theme::handFont(font(), Theme::HandFont::Script);
    brandFont.setPixelSize(28);
    p.setFont(brandFont);
    p.setPen(t.text);
    const int left = logoX + kLogoSize + 10;
    const QRect textRect(left, brandY - 6, width() - left - kRailPadding, 38);
    p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
               QFontMetrics(brandFont).elidedText(brand, Qt::ElideRight, textRect.width()));
}
