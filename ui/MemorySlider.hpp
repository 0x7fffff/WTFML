/**
 * @file MemorySlider.hpp
 * @brief 内存那根拖着改的条（「最大内存」用的就是它）。
 * @author 0x7fffff
 * @date 2026-09-26
 * @note 用户原话：「这个 xmx xms 弄一个像 pcl 那样子的灵活变通的拖动条不用管最大最小的
 *       那种」—— 要的是**一根条**，拖到哪儿就是哪儿，不用先想"我该填多少"。
 *
 * 【位置不是"多少 MB"，是"第几格"】
 *
 * 条里存的是下标，真值在 valueSteps() 那张表里。这么绕一层是因为**步长要变**：小内存时
 * 128 MB 一格（1024 / 1152 / 1280 那些档位在 4 GB 的机器上是真有人用的），大内存时
 * 512 MB 一格（不然 32 GB 那台机器上要划两百多格、一格只有一两个像素宽，手根本停不准）。
 * 直接拿 MB 当范围的话，Qt 是按像素线性映射的 —— 拖出来的是 3071、5386 这种数，写进
 * 设置里难看，而且和数字框那边"步长 256"的约定对不上。
 *
 * 【两个纯函数是故意摘出来的】
 *
 * valueSteps() / nearestIndex() 都是纯函数，验证工程能直接钉这张表（每一格都落在人看得懂
 * 的数上、严格递增、没有重复项），不必真去点一根滑条 —— 点滑条只能验到"拖动是通的"，
 * 验不到"每一格都是该有的那个值"。
 *
 * 【它不知道自己是给谁的】
 *
 * 这一层只认 MB，不认识"物理内存"也不读任何设置（ui/ 不依赖业务层那条老规矩）：上限由
 * 调用方 setRangeMb() 决定 —— 设置页那边是 MainWindow 按本机物理内存给的（见
 * MainWindow::applyMemoryLimits），实例设置那边给的是同一个数。
 */
#pragma once

#include <QList>
#include <QSlider>

class MemorySlider : public QSlider {
public:
    explicit MemorySlider(QWidget *parent = nullptr);

    /// 这条能停在**哪些值**上（MB，升序、严格递增）。
    ///
    /// 一格的跨度随值变大：< 2 GB 时 128、< 8 GB 时 256、再往上 512。
    /// @param minMb 最左端；@param maxMb 最右端（**一定在表里**，哪怕它落不到格上）。
    /// @note 上限比下限高不到一格时，表里就两个数（两头），不会退化成一串重复值。
    static QList<int> valueSteps(int minMb, int maxMb);

    /// 表里最接近 @p mb 的那一格的下标。同样近时取**小**的那一格（往下压比往上顶安全：
    /// 内存给大了 JVM 直接起不来，给小了只是慢一点）。空表回 -1。
    static int nearestIndex(const QList<int> &steps, int mb);

    /// 换一套范围。当前值按新表就近吸附（换范围不该把用户刚拖到的位置冲掉）。
    void setRangeMb(int minMb, int maxMb);

    int minimumMb() const;
    int maximumMb() const;
    /// 现在停在哪一格（MB）。范围还没设过（空表）时回 0。
    int valueMb() const;
    /// 停在离 @p mb 最近的那一格上。**不发出信号的那一路也走它**（调用方自己挡）。
    void setValueMb(int mb);

protected:
    /// 2026-10-18（用户原话「这个拖动的条做成和底栏那个进度条一个样式」）：
    /// 自绘成底栏那条进度条的样子 —— 圆角细轨道 + 主题色的填充，右端一个圆形把手。
    /// 轨道 / 填充的色和 `TaskProgressBar` 同一处（`Theme::progressTrackColor()` /
    /// `progressFillColor()`），所以两处永远是同一个颜色。
    ///
    /// 把手的位置**不自己算比例**：走 `style()->subControlRect(SC_SliderHandle)` ——
    /// 那是 QSlider 自己认鼠标位置用的同一份几何（Fusion 的 groove/handle 各留了半个
    /// 把手的边距）。自己按 `value()/maximum()` 乘宽度的话，画出来的把手会跟"点哪儿
    /// 跳到哪儿"差一小截，越靠两头越明显。
    void paintEvent(QPaintEvent *event) override;

private:
    /// "第几格"。空表 = 还没 setRangeMb() 过（构造函数到第一次设范围之间只有一瞬）。
    QList<int> m_steps;
};
