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
    // 能用 Tab 走到，空格切换（QAbstractButton 自带）。只给 Tab 焦点：鼠标点它不抢输入框的焦点。
    // 焦点环不用 Fusion 的虚线矩形（它按 sizeHint 铺，比药丸形的控件还大一圈）：
    // paintEvent 里按「键盘焦点」自己画一圈药丸形的环。
    setFocusPolicy(Qt::TabFocus);
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
    QPainter painter(this);
    paintSwitch(&painter, rect(), palette().color(QPalette::Accent), isChecked(), m_hovered, isDown(),
                isEnabled());
    // 键盘焦点环：只在键盘到达时画（`wtfmlKeyboardFocus` 由 Theme 的焦点过滤器按「最后一次输入是键盘」挂，
    // 不看 QFocusEvent::reason()），和 QCheckBox 开关（Theme 里的 SwitchPainter）同一个形状同一个色。
    if (hasFocus() && property("wtfmlKeyboardFocus").toBool()) {
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPen ring(Theme::focusRingColor(palette()));
        ring.setWidthF(2.0);
        painter.setPen(ring);
        painter.setBrush(Qt::NoBrush);
        const QRectF track = QRectF(rect()).adjusted(1, 1, -1, -1);
        painter.drawRoundedRect(track, track.height() / 2.0, track.height() / 2.0);
    }
}

void ToggleSwitch::paintSwitch(QPainter *painter, const QRect &area, const QColor &accent, bool on,
                               bool hoveredIn, bool down, bool enabled) {
    // 关 = fill-press 实心轨道（不描边）+ ink-2 圆点 12；开 = 强调色实心 + on-accent 圆点 14；
    // 悬停圆点放大 1.12（关着时轨道压深一点）；按下圆点拉长到 17 宽；禁用整枚 .45。
    const QRectF box(area.x() + 0.75, area.y() + 0.75, area.width() - 1.5, area.height() - 1.5);
    const Theme::Tokens t = Theme::tokens();
    const bool hovered = hoveredIn && enabled;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    if (!enabled) painter->setOpacity(painter->opacity() * 0.45);
    const qreal radius = box.height() / 2.0;
    if (on) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(hovered ? (accent == t.primary ? t.primaryHover : accent.lighter(110)) : accent);
    } else {
        QColor track = t.fillPress;  // 悬停把同一档压深一点（浅深主题都是加 alpha）
        if (hovered) track.setAlphaF(qMin(1.0, track.alphaF() * 1.6));
        painter->setPen(Qt::NoPen);
        painter->setBrush(track);
    }
    painter->drawRoundedRect(box, radius, radius);

    qreal knob = on ? 14.0 : 12.0;
    if (hovered) knob *= 1.12;
    const qreal knobWidth = down ? qMax(knob, 17.0) : knob;
    const qreal inset = (area.height() - knob) / 2.0;
    const qreal x = on ? area.x() + area.width() - inset - knobWidth : area.x() + inset;
    painter->setPen(Qt::NoPen);
    painter->setBrush(on ? t.onPrimary : t.secondary);
    painter->drawRoundedRect(QRectF(x, area.y() + inset, knobWidth, knob), knob / 2.0, knob / 2.0);
    painter->restore();
}
