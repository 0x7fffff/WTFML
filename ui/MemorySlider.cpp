/**
 * @file MemorySlider.cpp
 * @brief 内存那根拖着改的条：档位表、就近吸附、两头那几个访问器。
 * @note 为什么位置是"第几格"而不是"多少 MB"，见 .hpp 开头那段 —— 那儿是这一份的说明书。
 */

#include "MemorySlider.hpp"

#include <QApplication>
#include <QPaintEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QtGlobal>

#include "Theme.hpp"

MemorySlider::MemorySlider(QWidget *parent) : QSlider(Qt::Horizontal, parent) {
    // 一格 = 一档（值域是下标，见 .hpp）。键盘左右键因此正好挪一档，PgUp/PgDn 挪四档 ——
    // 条上没有刻度也没有数字（数字在它旁边那个框里），动一下要能看得出变化，挪四档合适。
    setSingleStep(1);
    setPageStep(4);
    // 拖动中就要发 valueChanged：旁边那个数字框要**跟着走**（松手才更新的话，用户拖的时候
    // 屏幕上那个数一直不动，看着像坏的）。落盘那件事由调用方按 isSliderDown() 分开处理 ——
    // 拖动全程是几十上百次变化，每一次都写一遍注册表是不行的。
    setTracking(true);
    // 刻度线一律不画：32 GB 的机器上是五十多格，画出来就是一条灰色的墙。
    setTickPosition(QSlider::NoTicks);
}

QList<int> MemorySlider::valueSteps(int minMb, int maxMb) {
    if (minMb < 0) {
        minMb = 0;
    }
    if (maxMb < minMb) {
        maxMb = minMb;
    }

    QList<int> steps;
    steps.append(minMb);
    while (steps.constLast() < maxMb) {
        const int value = steps.constLast();
        const int step = value < 2048 ? 128 : (value < 8192 ? 256 : 512);
        int next = value + step;
        if (next > maxMb) {
            // 上限不在格上（比如物理内存 12287 MB）：最后一格就停在它身上。少这一句的话
            // 最右端够不到上限，用户拖到头也只到 12032，看着像"上限被吃掉了"。
            next = maxMb;
        }
        if (next <= value) {
            break;  // 兜底：真出现算不动的输入时别在这儿转死
        }
        steps.append(next);
    }
    return steps;
}

int MemorySlider::nearestIndex(const QList<int> &steps, int mb) {
    if (steps.isEmpty()) {
        return -1;
    }
    int best = 0;
    for (int i = 1; i < steps.size(); ++i) {
        // **严格小于**：同样近时停在小的那一格（见 .hpp 里那句"往下压比往上顶安全"）。
        if (qAbs(steps.at(i) - mb) < qAbs(steps.at(best) - mb)) {
            best = i;
        }
    }
    return best;
}

void MemorySlider::setRangeMb(int minMb, int maxMb) {
    const bool hadRange = !m_steps.isEmpty();
    const int keep = hadRange ? valueMb() : minMb;
    m_steps = valueSteps(minMb, maxMb);
    setRange(0, m_steps.size() - 1);
    setValueMb(keep);
}

int MemorySlider::minimumMb() const {
    return m_steps.isEmpty() ? 0 : m_steps.constFirst();
}

int MemorySlider::maximumMb() const {
    return m_steps.isEmpty() ? 0 : m_steps.constLast();
}

int MemorySlider::valueMb() const {
    if (m_steps.isEmpty()) {
        return 0;
    }
    // 下标夹一道：范围还没设过、或者别处直接调了 QSlider::setValue() 时，value() 可能
    // 落在表外（Qt 允许）—— 这里回一个合法的值，别让调用方拿到越界的下标。
    return m_steps.at(qBound(0, value(), int(m_steps.size()) - 1));
}

void MemorySlider::setValueMb(int mb) {
    const int index = nearestIndex(m_steps, mb);
    if (index >= 0) {
        setValue(index);
    }
}

void MemorySlider::setAutoValueMb(int mb) {
    if (mb < 0) {
        clearAutoValue();
        return;
    }
    m_autoValueMb = mb;
    update();
}

void MemorySlider::clearAutoValue() {
    if (m_autoValueMb < 0) {
        return;
    }
    m_autoValueMb = -1;
    update();
}

void MemorySlider::paintEvent(QPaintEvent *) {
    if (height() <= 0 || width() <= 0) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 几何问 QStyle：handle 的中心就是 QSlider 认鼠标位置时用的那一个（见 .hpp 里那段）。
    // 样式表里没有 QSlider 的规则，所以这里拿到的是 Fusion 自己的 groove/handle。
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    const QRect groove =
        style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
    const QRect handle =
        style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);

    // 色取**应用那一份**调色板（和 TaskProgressBar 同一条理由：不认某一格自己那份）。
    const QPalette palette = QApplication::palette();
    QColor track = Theme::progressTrackColor(palette);
    QColor fill = Theme::progressFillColor(palette);
    if (!isEnabled()) {
        // 灰掉的时候整根条跟着退一档 —— 光靠"拖不动"看不出它现在是禁用的。
        track.setAlphaF(track.alphaF() * 0.5);
        fill.setAlphaF(fill.alphaF() * 0.4);
    }
    // 把手中间那一圈用**轨道色**而不是调色板的 Base：这一行坐在哪儿（设置页的表单、实例
    // 设置那一栏）底色不一定等于 Base，拿 Base 会在深色档 / 卡片底上冒出一个白点。用轨道色
    // 它就永远只是"填充色的一枚环"，和底下的条自己是一套。
    const QColor knobInner = track;

    // 轨道比底栏那条（4 px）略粗一点：这条要用手拖，太细了不好瞄；但仍是"一条圆角细线"，
    // 不是从前那种一整格高的灰块。
    const int railHeight = qBound(2, height() / 4, 6);
    const qreal radius = railHeight / 2.0;
    const QRectF rail(groove.left(), (height() - railHeight) / 2.0, groove.width(), railHeight);
    painter.setPen(Qt::NoPen);
    painter.setBrush(track);
    painter.drawRoundedRect(rail, radius, radius);

    // 填充到"该到的那一格"：手动档 = 把手中心；自动档 = 自动算出来那一格。
    //
    // 自动档的**把手留在原地**（= 上次手动档停的位置），只有进度条跟着自动值走：
    // 用户一眼能看出"自动现在给这么多、我原来手动设的是那么多"。条本身画成半透明的
    // 虚条，和可拖的手动档区分开（用户 点名要的"虚条，圆圈不跟着走"）。
    const qreal centerX = qBound(rail.left(), qreal(handle.center().x()), rail.right());
    const bool autoMode = m_autoValueMb >= 0;
    const int autoIndex = autoMode ? nearestIndex(m_steps, m_autoValueMb) : -1;
    qreal fillCenterX = centerX;
    if (autoMode && autoIndex >= 0) {
        QStyleOptionSlider autoOption;
        initStyleOption(&autoOption);
        autoOption.sliderPosition = autoIndex;
        autoOption.sliderValue = autoIndex;
        const QRect autoHandle = style()->subControlRect(
            QStyle::CC_Slider, &autoOption, QStyle::SC_SliderHandle, this);
        fillCenterX = qBound(rail.left(), qreal(autoHandle.center().x()), rail.right());
    }
    const qreal filledWidth = fillCenterX - rail.left();
    if (autoMode) {
        if (filledWidth > 0.0) {
            QColor ghostFill = fill;
            ghostFill.setAlphaF(ghostFill.alphaF() * 0.55);
            const QRectF ghostRect(rail.left(), rail.top(),
                                   qMax(filledWidth, radius * 2.0), railHeight);
            QPen ghostPen(fill);
            ghostPen.setStyle(Qt::DashLine);
            ghostPen.setWidthF(1.0);
            painter.setPen(ghostPen);
            painter.setBrush(ghostFill);
            painter.drawRoundedRect(ghostRect, radius, radius);
            painter.setPen(Qt::NoPen);
        }
    } else if (filledWidth > 0.0) {
        painter.setBrush(fill);
        painter.drawRoundedRect(
            QRectF(rail.left(), rail.top(), qMax(filledWidth, radius * 2.0), railHeight), radius,
            radius);
    }

    // 把手：一圈主题色的环，中间透出底色。纯色实心球在深色档下和填充粘成一团、看不出
    // 哪儿是"现在的位置"。
    const qreal knobDiameter = qMin<qreal>(qreal(height() - 2), 16.0);
    if (knobDiameter >= 8.0) {
        const qreal outer = knobDiameter / 2.0;
        const qreal inner = qMax(1.5, outer - 3.0);
        const QPointF knobCenter(centerX, height() / 2.0);
        // 自动档下这颗圆**不跟着自动值走**，只当"上次手动的位置"的标记。控件已经
        // disabled（上面 fill/track 各降过一档 alpha），这里不再额外压淡 —— 再淡一档
        // 就只剩一个看不清的点，反而不知道圆圈停在哪儿。
        //
        // 自动档把外圈**再压深一档**：它正好压在那条半透明的自动虚条上时，原来的
        // 外圈和虚条是同一个色，圆就"看不见"了 —— 用户会以为把手跟着自动值走了。
        // 深一档的环压在虚条 / 轨道上都看得见，中间还是原来那枚轨道色的芯。
        QColor knobOuter = fill;
        if (autoMode) {
            knobOuter = fill.darker(135);
        }
        const QColor knobInnerColor = knobInner;
        painter.setBrush(knobOuter);
        painter.drawEllipse(knobCenter, outer, outer);
        painter.setBrush(knobInnerColor);
        painter.drawEllipse(knobCenter, inner, inner);
    }
}
