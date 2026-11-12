#include "ToggleSwitch.hpp"

#include <QEnterEvent>
#include <QPainter>
#include <QPaintEvent>

#include "MainWindowUI.hpp"
#include "Theme.hpp"

ToggleSwitch::ToggleSwitch(QWidget *parent) : QAbstractButton(parent) {
    setCheckable(true);
    // 光标停在上面要看得出来（这一格是可以点的一件事，不是一行文字）。
    setCursor(Qt::PointingHandCursor);
    // 这一格自己不画焦点框：键盘走到它上面时由 Fusion 画的那个虚线矩形在这枚小控件上
    // 比控件本身还大一圈（它按 sizeHint 铺，而那是个药丸形）。
    setFocusPolicy(Qt::NoFocus);
}

int ToggleSwitch::switchWidth() {
    return MainWindowUI::toggleSwitchWidth();
}

int ToggleSwitch::switchHeight() {
    return MainWindowUI::toggleSwitchHeight();
}

QSize ToggleSwitch::sizeHint() const {
    return QSize(switchWidth(), switchHeight());
}

void ToggleSwitch::enterEvent(QEnterEvent *event) {
    QAbstractButton::enterEvent(event);
    m_hovered = true;
    update();
}

void ToggleSwitch::leaveEvent(QEvent *event) {
    QAbstractButton::leaveEvent(event);
    m_hovered = false;
    update();
}

void ToggleSwitch::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    // 关 = fill-press 实心轨道（不描边）+ ink-2 圆点 12；开 = 强调色实心 + on-accent 圆点 14；
    // 悬停圆点放大 1.12（关着时轨道压深一点）；按下圆点拉长到 17 宽；禁用整枚 .45。
    const QRectF box(0.75, 0.75, width() - 1.5, height() - 1.5);
    const Theme::Tokens t = Theme::tokens();
    const QColor accent = palette().color(QPalette::Accent);
    const bool hovered = m_hovered && isEnabled();
    const bool on = isChecked();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (!isEnabled()) painter.setOpacity(0.45);
    const qreal radius = box.height() / 2.0;
    if (on) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(hovered ? (accent == t.primary ? t.primaryHover : accent.lighter(110)) : accent);
    } else {
        QColor track = t.fillPress;  // 悬停把同一档压深一点（浅深主题都是加 alpha）
        if (hovered) track.setAlphaF(qMin(1.0, track.alphaF() * 1.6));
        painter.setPen(Qt::NoPen);
        painter.setBrush(track);
    }
    painter.drawRoundedRect(box, radius, radius);

    qreal knob = on ? 14.0 : 12.0;
    if (hovered) knob *= 1.12;
    const qreal knobWidth = isDown() ? qMax(knob, 17.0) : knob;
    const qreal inset = (height() - knob) / 2.0;
    const qreal x = on ? width() - inset - knobWidth : inset;
    painter.setPen(Qt::NoPen);
    painter.setBrush(on ? t.onPrimary : t.secondary);
    painter.drawRoundedRect(QRectF(x, inset, knobWidth, knob), knob / 2.0, knob / 2.0);
}
