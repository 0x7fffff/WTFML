/**
 * @file ToggleSwitch.hpp
 * @brief 一个**拨动开关**（药丸形轨道 + 一个圆钮）：左边关、右边开。
 * @author 0x7fffff
 * @date 2026-09-27
 * @note 用户 2026-09-27 原话：「启用和禁用使用开关的那种形式」。上一版是两颗文字按钮
 *       （「禁用」/「启用」），它说的是"点下去会发生什么"；开关说的是"现在是什么状态"
 *       —— 一列下来扫一眼就知道哪几个是关着的，不用逐个读字。
 *
 * 【为什么是画的，不是一个 QCheckBox】
 *
 * 用户要的是「开关的那种形式」，而 QCheckBox 在 Fusion 下就是一个小方框（勾上多一笔），
 * 那是"多选"的形状不是"开关"的形状。改成自绘之后，这个控件在仓库里和「底栏左下角那枚
 * 状态方块」是同一类东西（用户原话就是「方块」）：**画出来的**，不是某个现成控件换了
 * 一层皮。
 *
 * 【状态归谁】
 *
 * 这个控件**只表示"现在是什么状态"**，一件事都不做：点它只发 `toggled`（QAbstractButton
 * 自带），由接的人去改盘上的名字、改完再回头摆状态。这一点和 `InstanceFilesPage` 那条
 * 「这一层只显示，不碰盘」是同一条规矩 —— 所以**读状态的唯一入口是 `isChecked()`**，
 * 而"开关拨过去之后真生效了吗"要看下一轮刷新。
 *
 * NOTE: 它是 `QAbstractButton`（不是 QWidget + 自己处理鼠标）：悬停、按下、禁用、
 *       `click()`、键盘（空格）这些事全由基类管，这里只管画。
 */

#pragma once

#include <QAbstractButton>

class ToggleSwitch : public QAbstractButton {
    Q_OBJECT

public:
    explicit ToggleSwitch(QWidget *parent = nullptr);

    /// 这个开关要占多大。**唯一出处** —— sizeHint() 和布局都读它，写死两份的话
    /// "画出来的"和"留出来的地方"会各算各的（症状是圆钮被裁掉半个）。
    static int switchWidth();
    static int switchHeight();

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

    /// 悬停时整条轨道亮一档（和仓库里别的可点控件一个规矩：鼠标停在什么上面要看得出来）。
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    bool m_hovered = false;
};
