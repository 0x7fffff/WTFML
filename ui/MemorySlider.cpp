/**
 * @file MemorySlider.cpp
 * @brief 内存那根拖着改的条：档位表、就近吸附、两头那几个访问器。
 * @author 0x7fffff
 * @date 2026-09-26
 * @note 为什么位置是"第几格"而不是"多少 MB"，见 .hpp 开头那段 —— 那儿是这一份的说明书。
 */

#include "MemorySlider.hpp"

#include <QtGlobal>

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
    // 换范围前先记住现在停在哪 —— 换范围（设置页那边是"按本机物理内存收一次口"）不该把
    // 用户刚拖到的位置冲掉。
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
