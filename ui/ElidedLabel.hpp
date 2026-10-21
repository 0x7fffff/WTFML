/**
 * @file ElidedLabel.hpp
 * @brief 一个会**省略号收尾**的 QLabel（装不下的时候从指定的那一头截，而不是硬裁）。
 * @note **为什么非它不可**：QLabel 在控件比文字窄的时候是**硬裁**的 —— 最后一个字被切掉
 * 一半、右边什么都没有，看着像画坏了（`fabric-api-0.119.3+1.21.4.jar` 那一类比行名那
 * 一格宽，截出来的是一串看不出是"被截了"的名字）。也没法靠 QLabel 自己解决：翻遍
 * qlabel.h 都没有 elide 相关的 API。
 *
 * 底栏那三行会变的文字在用（左边那格钉死了固定宽度、右边那格封了上限，见 MainWindowUI 的
 * bottomBarRunStateColumnWidth() / bottomBarAccountColumnWidth()），文件管理那几页的行名
 * 也要同一件事，而 `ui/` 底下一个 .cpp 拿不到另一个 .cpp 里的匿名类 —— 所以集中在这里。
 * **一份实现，两处用**。
 *
 * `paintEvent` 里自己画（不走 QLabel 那套），所以：
 * - `text()` 永远是**完整**那串（截的只是画出来的样子）；
 * - `sizeHint()` 可以被 `setMaximumWidth()` 封顶（见下），于是"文字长短"不会把旁边的东西
 * 抻来抻去。
 */
#pragma once

#include <QLabel>
#include <QPainter>
#include <QString>

class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(QWidget *parent = nullptr) : QLabel(parent) {
        // 属性在**构造时**就写上（不是等谁调 setElideMode() 才写）：没调过的标签也该读得出
        // 它当下用的就是默认那一档，否则验证工程读一个没写过的属性只会拿到 0。
        setProperty("elideMode", static_cast<int>(m_elideMode));
    }

    /// 从**哪一头**省略。默认是尾巴（底栏那几行是句子，尾巴上的字最不要紧）。
    ///
    /// NOTE: 两头都有用的文字要显式改成 Qt::ElideMiddle —— 路径那一类（盘符和最后一级
    /// 目录名都要看）、以及文件管理那几页的行名（后面挂着「（已禁用）」那四个字，
    /// 从尾巴截掉就等于把"这一条是关着的"这个最要紧的信息先扔了）。
    void setElideMode(Qt::TextElideMode mode) {
        m_elideMode = mode;
        // 动态属性是给**验证工程**读的（工程那边不一定认得出这个类型）：有人把上面那句
        // setElideMode() 删掉，属性就退回 ElideRight，断言当场红。
        setProperty("elideMode", static_cast<int>(m_elideMode));
        update();
    }
    Qt::TextElideMode elideMode() const { return m_elideMode; }

    QSize sizeHint() const override {
        const QSize full = QLabel::sizeHint();
        // 宽度上限由 setMaximumWidth() 给。**没设过时它是 QWIDGETSIZE_MAX（16777215），
        // 不是 -1** —— 拿"有没有设小上限"当判据，别拿"是不是负数"。
        const int cap = maximumWidth();
        if (cap >= QWIDGETSIZE_MAX || full.width() <= cap) {
            return full;
        }
        return QSize(cap, full.height());
    }

protected:
    void paintEvent(QPaintEvent *) override {
        // NOTE: 这里**每次都重算**，不缓存。缓存（"宽度没变就接着用上次那串"）看着是省事，
        // 但底栏那几个格子的宽度是**钉死的**：文字改了宽度也不改，resizeEvent 压根
        // 不触发 —— 缓存就永远不失效，第一句话会一直挂在底栏上（「实例运行中」在三个
        // 实例都跑着的时候还是「实例运行中」）。elidedText() 只在重画时算一次，
        // 这几处重画都不频繁，不值当为它冒这个险。
        const QRect box = contentsRect();
        const QString shown = fontMetrics().elidedText(text(), m_elideMode, box.width());
        QPainter painter(this);
        painter.setPen(palette().color(foregroundRole()));
        painter.drawText(box, alignment() | Qt::TextSingleLine, shown);
    }

private:
    Qt::TextElideMode m_elideMode = Qt::ElideRight;
};
