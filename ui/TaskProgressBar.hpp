/**
 * @file TaskProgressBar.hpp
 * @brief 自绘的进度条（coding plan §1.2 / §3.3「统一自绘任务进度组件」）。
 * @author 0x7fffff
 * @date 2026-10-13
 *
 * 三处进度条（底栏中间那格的摘要、任务中心面板里每一行、任务详情页）**共用这一条**，
 * 换掉三处原来各自"就地 new 一颗原生 QProgressBar"的写法。
 *
 * 【为什么是 `QProgressBar` 的子类，而不是另起一个 `QWidget`】
 *
 * 因为"进度"这件事的**模型**（`range` / `value` / "总量未知 ⇒ 不确定态"）原生那一位已经
 * 说得很清楚了，而且两样东西已经钉在它身上：
 *
 * - `Anim::smoothProgress(QProgressBar *, int)`：底栏那条"一格格追成连续"的平滑追值，
 *   它读的是 `maximum()`、写的是 `setValue()`（见 `ui/Animations.cpp` 那四条"原地落"的规矩）；
 * - 测试：`ui_check` / `mw_check` / `ui_shot` 一律 `findChild<QProgressBar *>(名字)` +
 *   读 `value()` / `minimum()` / `maximum()` 来断言。
 *
 * 另起一个 `QWidget` 的话，上面两处全得改（连带 `smoothProgress` 的签名）。子类化之后
 * **只把"画"换掉**，模型、动画、断言一个字节都不动 —— 这正是"换一层皮"该有的样子。
 *
 * 【画什么】
 *
 * 一根圆角细轨道：底是 `Theme::progressTrackColor()`、往前走的那一截是
 * `Theme::progressFillColor()`（就是主题色）。**不画百分比文字** —— 数在标题行 / 右边
 * 那一格（`MainWindowUI` 那边挪的），条里再写一遍是重复；连带上游把文字打开的那一位
 * 也一并关掉了（`ui_modernize.py audit` 里那条 "progress text enabled" 就是它）。
 *
 * 总量未知（`minimum() == maximum()`，底栏"有东西在跑、说不清到哪"那一档）画一截**来回
 * 滚的带子**；相位只在 `Anim::enabled()` 时往前推（关掉动效、以及 ui_shot 那种"像素每次
 * 都要一样"的场合 ⇒ 一条**不动**的带子，定住一个确定相位）。
 */

#pragma once

#include <QProgressBar>

class QTimer;

class TaskProgressBar : public QProgressBar {
    Q_OBJECT

public:
    explicit TaskProgressBar(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    /// 这一格现在**该不该**有个定时器在推相位（确定态 / 藏起来 / 关了动效 ⇒ 都不该）。
    /// 幂等：状态没变就什么都不做。paint 一趟、显隐各一趟都会问它。
    void syncBusyTimer();

    /// 不确定态那一截现在滚到哪儿（0..1）。关掉动效时恒 0（定住的那一格）。
    qreal m_phase = 0.0;
    QTimer *m_busyTimer = nullptr;
};
