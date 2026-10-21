/**
 * @file MemorySlider.hpp
 * @brief 「最大内存」用的拖动条：按档位吸附，支持自动分配时的虚条展示。
 *
 * 内部保存档位下标，真值由 valueSteps() 给出；步长随内存增大而变，避免大内存时档位过密。
 * 这一层不读设置、不认识物理内存，范围由调用方设置。
 */
#pragma once

#include <QList>
#include <QSlider>

class MemorySlider : public QSlider {
public:
    explicit MemorySlider(QWidget *parent = nullptr);

    /// 可停靠的档位（MB，升序）。步长：<2 GB 为 128，<8 GB 为 256，否则 512；
    /// minMb 和 maxMb 一定包含在结果中。
    static QList<int> valueSteps(int minMb, int maxMb);

    /// 最接近 @p mb 的档位下标；同样近时取较小值（宁小勿大，避免 JVM 起不来）。空表返回 -1。
    static int nearestIndex(const QList<int> &steps, int mb);

    /// 换一套范围。当前值按新表就近吸附（换范围不该把用户刚拖到的位置冲掉）。
    void setRangeMb(int minMb, int maxMb);

    int minimumMb() const;
    int maximumMb() const;
    /// 现在停在哪一格（MB）。范围还没设过（空表）时回 0。
    int valueMb() const;
    /// 停在离 @p mb 最近的那一格上。**不发出信号的那一路也走它**（调用方自己挡）。
    void setValueMb(int mb);

    /// 自动分配的展示值（MB）。只影响绘制：进度条走到该值，把手仍停在 valueMb()。
    /// 负值表示手动档。
    void setAutoValueMb(int mb);
    /// 清掉自动档展示值，回到手动档画法。
    void clearAutoValue();
    /// 现在有没有自动档展示值。
    bool hasAutoValue() const { return m_autoValueMb >= 0; }
    /// 自动档展示值（没有时回 -1）。
    int autoValueMb() const { return m_autoValueMb; }

protected:
    /// 自绘进度条外观；把手几何由 QStyle 的 SC_SliderHandle 决定，避免与点击位置错位。
    void paintEvent(QPaintEvent *event) override;

private:
    /// "第几格"。空表 = 还没 setRangeMb() 过（构造函数到第一次设范围之间只有一瞬）。
    QList<int> m_steps;
    /// 自动档的展示值（MB，-1 = 手动档）。见 setAutoValueMb() 那段。
    int m_autoValueMb = -1;
};
