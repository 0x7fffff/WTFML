#include "ToggleSwitch.hpp"

#include <QEnterEvent>
#include <QPainter>
#include <QPaintEvent>

#include "MainWindowUI.hpp"

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
    const QRect box(0, 0, width(), height());
    const int margin = MainWindowUI::toggleSwitchKnobMargin();
    // 圆钮是个**整圆**，直径就是控件的内高 —— 外高减掉上下各一道留白。
    const int knob = qMax(4, box.height() - 2 * margin);

    const QPalette &palette = this->palette();
    // ★ 开是"高亮"那一档、关是"中灰"那一档 —— 都从调色板取，不写死颜色（用户换主题、
    //   或者哪天把 Fusion 换掉，这一格跟着走）。这和底栏那枚状态方块取色的规矩一样。
    QColor track = isChecked() ? palette.color(QPalette::Highlight)
                               : palette.color(QPalette::Mid);
    if (m_hovered && isEnabled()) {
        // 悬停亮一档（`lighter` 比手写一个色号稳：它按当前这一档算）。
        track = track.lighter(115);
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (!isEnabled()) {
        // 禁用：整枚淡下去（和 VersionTreeDelegate 里"整行淡下去"同一个做法 —— 一眼看得出
        // "这一格现在点不得"，而形状还在、还认得出是个开关）。
        painter.setOpacity(0.5);
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(track);
    // 药丸形：圆角半径取外高的一半，两头就是半圆。
    painter.drawRoundedRect(box, box.height() / 2.0, box.height() / 2.0);

    // 圆钮：开着贴右边、关着贴左边，中间那一段就是"拨过去"的距离。
    const int knobX = isChecked() ? box.width() - margin - knob : margin;
    const QRect knobRect(knobX, margin, knob, knob);
    painter.setBrush(isEnabled() ? palette.color(QPalette::Base)
                                 : palette.color(QPalette::Window));
    painter.drawEllipse(knobRect);
}
