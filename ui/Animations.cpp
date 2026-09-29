/**
 * @file Animations.cpp
 * @brief `Animations.hpp` 那一套的实现。规矩（不许有元素超出 / 几何一个像素都不动）写在头文件里。
 */

#include "Animations.hpp"

#include <QAbstractAnimation>
#include <QAbstractButton>
#include <QApplication>
#include <QColor>
#include <QEasingCurve>
#include <QEvent>
#include <QGraphicsEffect>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QTimer>
#include <QTransform>
#include <QTreeView>
#include <QVariantAnimation>
#include <QWidget>

#include <functional>

namespace {

/// 我们自己装的那个效果叫什么。别人（将来）在同一个控件上装过别的效果时，靠这个名字认出来
/// 那是"不是我们的"，然后**一个字节都不碰**。
constexpr const char kEffectName[] = "wtfmlAnimEffect";
/// 事件过滤器装没装过的那一位（挂在 QApplication 自己身上，所以不用静态变量）。
constexpr const char kFilterMark[] = "wtfmlAnimPressFilter";
/// 滚动条那一份过滤器装没装过的那一位（同上）。
constexpr const char kScrollFilterMark[] = "wtfmlAnimScrollFilter";

/// 动效总开关。默认开。
bool g_enabled = true;

/**
 * 动效用的那一个 `QGraphicsEffect`：把控件**渲染好的那一张图**贴回来，贴上之前可以
 * 整体确定不透明度（淡入淡出）、整体压暗一档（按下）、整体往下挪几个像素（换页抬起来）。
 *
 * 【为什么是"贴一张图"而不是改控件自己】
 *
 * 因为改不了：底下的颜色是样式表画的、按下的那一档也是，而"动画"要的是**每一帧都不一样**。
 * 渲染成图之后，"这一帧是什么样"就完全攥在这一层手里了，控件自己（几何、布局、样式表、
 * 调色板）**一个字节都不动** —— 用户点名的"不许有元素超出"于是变成结构上做不到，
 * 而不是靠分寸拿捏。
 */
class AnimEffect : public QGraphicsEffect {
public:
    qreal opacity() const { return m_opacity; }
    void setOpacity(qreal value) {
        if (qFuzzyCompare(m_opacity, value)) {
            return;
        }
        m_opacity = value;
        update();
    }

    qreal shift() const { return m_shift; }
    void setShift(qreal value) {
        if (qFuzzyCompare(m_shift, value)) {
            return;
        }
        m_shift = value;
        update();
    }

    qreal scrim() const { return m_scrim; }
    void setScrim(qreal value) {
        if (qFuzzyCompare(m_scrim, value)) {
            return;
        }
        m_scrim = value;
        update();
    }

    /// ★ 这一条就是"不许有元素超出"的那一句：效果的有效范围**就是源矩形自己**
    /// （默认实现也是这么返回的，这里明写出来，免得将来有人"顺手"给它加个外扩）。
    QRectF boundingRectFor(const QRectF &sourceRect) const override { return sourceRect; }

protected:
    void draw(QPainter *painter) override {
        QPoint offset;
        const QPixmap source =
            sourcePixmap(Qt::DeviceCoordinates, &offset, QGraphicsEffect::PadToEffectiveBoundingRect);
        if (source.isNull()) {
            return;
        }

        QPixmap out = source;
        if (m_scrim > 0.0) {
            // 压暗一档：只动**已经有内容**的那些像素（SourceAtop 连 alpha 一起保住），
            // 所以圆角、透明边这些不会被刷成一块方块。
            QPainter tint(&out);
            tint.setCompositionMode(QPainter::CompositionMode_SourceAtop);
            QColor ink(0, 0, 0);
            ink.setAlphaF(qBound(0.0, m_scrim, 1.0));
            tint.fillRect(out.rect(), ink);
        }

        painter->setWorldTransform(QTransform());
        // ★ 再上一道保险：不管上面那些数怎么算错，都画不到源矩形之外去。
        painter->setClipRect(QRectF(offset, source.size()), Qt::IntersectClip);
        painter->setOpacity(qBound(0.0, m_opacity, 1.0));

        QPoint at = offset;
        if (m_shift > 0.0) {
            // 抬起来那一段是按**逻辑像素**说的，而这个画笔眼下是设备坐标 —— 差一个
            // devicePixelRatio（150% 缩放下差 1.5 倍）。
            at.ry() += qRound(m_shift * source.devicePixelRatio());
        }
        painter->drawPixmap(at, out);
    }

private:
    qreal m_opacity = 1.0;
    qreal m_shift = 0.0;
    qreal m_scrim = 0.0;
};

/// 这个控件上是不是我们的效果；是就返回它，不是（或没有）返回空。
AnimEffect *ourEffect(QWidget *widget) {
    if (widget == nullptr) {
        return nullptr;
    }
    QGraphicsEffect *current = widget->graphicsEffect();
    if (current == nullptr || current->objectName() != QLatin1String(kEffectName)) {
        return nullptr;
    }
    return static_cast<AnimEffect *>(current);
}

/// 确保这个控件上挂着的是我们的效果（已经挂着就复用）。
AnimEffect *ensureEffect(QWidget *widget) {
    if (widget == nullptr) {
        return nullptr;
    }
    if (AnimEffect *existing = ourEffect(widget)) {
        return existing;
    }
    if (widget->graphicsEffect() != nullptr) {
        // 别人装的效果：不动它，这一发就算了（宁可不动画，也不能把别人的效果顶掉）。
        return nullptr;
    }
    auto *effect = new AnimEffect;
    effect->setObjectName(QLatin1String(kEffectName));
    widget->setGraphicsEffect(effect);
    return effect;
}

/// 动画跑完之后把效果摘掉（`setGraphicsEffect(nullptr)` 会把上一个删掉）。存在过的那一
/// 段时间里，这个控件每画一次都要先渲进一张图，摘掉之后就跟没装过一样。
void dropEffect(QWidget *widget) {
    if (ourEffect(widget) != nullptr) {
        widget->setGraphicsEffect(nullptr);
    }
}

/**
 * 跑一发 0→1 的进度动画，每一帧交给 `apply()` 去摆效果上的那几个数。
 *
 * `removeWhenDone`：跑完把效果摘掉（淡入、松手这两条）。按下那一发**不摘** —— 手指还按着，
 * 那个"暗着"的状态要一直在。
 */
void runProgress(QWidget *widget, AnimEffect *effect, qreal from, qreal to, int ms, bool removeWhenDone,
                 const std::function<void(qreal)> &apply) {
    // 同一个控件上又进来一发（连点两下、连切两页）：把上一发停掉。不停的话两个动画会
    // 交叉着写同一组数，看着像"抖"。
    const QList<QVariantAnimation *> running = effect->findChildren<QVariantAnimation *>();
    for (QVariantAnimation *old : running) {
        old->stop();
        old->deleteLater();
    }

    auto *animation = new QVariantAnimation(effect);
    animation->setDuration(qMax(1, ms));
    animation->setStartValue(from);
    animation->setEndValue(to);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(animation, &QVariantAnimation::valueChanged, effect,
                     [apply](const QVariant &value) { apply(value.toReal()); });
    if (removeWhenDone) {
        // NOTE: 摘效果这件事**不能**在 finished 里直接做：那一刻我们是站在动画自己的
        //       信号里，而效果是动画的父亲 —— 删父亲等于把这个正在发信号的动画也删了。
        //       挪到下一轮事件循环里做。
        QObject::connect(animation, &QVariantAnimation::finished, widget,
                         [w = QPointer<QWidget>(widget)]() {
                             QTimer::singleShot(0, w, [w]() { dropEffect(w); });
                         });
    }
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

/// 这个控件（或它上面几层父控件里最近的那一个）是不是一颗按钮。
QAbstractButton *buttonUnder(QWidget *widget) {
    for (QWidget *w = widget; w != nullptr; w = w->parentWidget()) {
        if (auto *button = qobject_cast<QAbstractButton *>(w)) {
            return button;
        }
    }
    return nullptr;
}

/// 按住：压暗 `pressDepthPercent()` 那么一档。
void pressDown(QAbstractButton *button) {
    AnimEffect *effect = ensureEffect(button);
    if (effect == nullptr) {
        return;
    }
    const qreal target = Anim::pressDepthPercent() / 100.0;
    if (effect->scrim() >= target - 0.001) {
        return;  // 已经暗到那儿了（按住不放的时候会重复发按下事件）
    }
    runProgress(button, effect, effect->scrim(), target, Anim::pressInMs(), /*removeWhenDone=*/false,
                [effect](qreal value) { effect->setScrim(value); });
}

/// 松手：弹回原样，然后把这层效果摘掉。
void pressUp(QAbstractButton *button) {
    AnimEffect *effect = ourEffect(button);
    if (effect == nullptr) {
        return;
    }
    runProgress(button, effect, effect->scrim(), 0.0, Anim::pressOutMs(), /*removeWhenDone=*/true,
                [effect](qreal value) { effect->setScrim(value); });
}

/**
 * 挂在 `QApplication` 上的那个事件过滤器：谁被按下去就动谁。
 *
 * NOTE: 它**从不吞事件**（永远返回 `false`）—— 这一层只是"顺便看一眼"，按钮自己该怎么
 *       响应还怎么响应。
 */
class PressFilter : public QObject {
public:
    explicit PressFilter(QObject *parent) : QObject(parent) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (!Anim::enabled()) {
            return false;
        }
        const QEvent::Type type = event->type();
        const bool down = (type == QEvent::MouseButtonPress || type == QEvent::KeyPress);
        const bool up = (type == QEvent::MouseButtonRelease || type == QEvent::KeyRelease);
        if (!down && !up) {
            return false;
        }

        auto *widget = qobject_cast<QWidget *>(watched);
        if (widget == nullptr) {
            return false;
        }
        QAbstractButton *button = buttonUnder(widget);
        if (button == nullptr || !button->isEnabled() || !button->isVisible()) {
            return false;
        }

        if (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonRelease) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() != Qt::LeftButton) {
                return false;
            }
        } else {
            // 键盘只看空格和回车（Qt 上"按下去会画成按下"的就是这两个键）。
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->isAutoRepeat()) {
                return false;
            }
            if (key->key() != Qt::Key_Space && key->key() != Qt::Key_Return && key->key() != Qt::Key_Enter) {
                return false;
            }
        }

        if (down) {
            pressDown(button);
        } else {
            pressUp(button);
        }
        return false;
    }
};

/**
 * 挂在 `QApplication` 上的另一个事件过滤器：**滚动条一露面就淡进来**。
 *
 * 用户 2026-09-29：「每一次切页面的时候滚动条都是瞬间出现的」。滚动条是按需出现的
 * （内容不够长它就不在），Qt 出现的那一下是硬切 —— 页面在淡，它"啪"地跳出来。
 *
 * NOTE: 和按下那一份一样，它**从不吞事件**（`Show` 本来也不归谁"处理"）。
 */
class ScrollBarFilter : public QObject {
public:
    explicit ScrollBarFilter(QObject *parent) : QObject(parent) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() != QEvent::Show || !Anim::enabled()) {
            return false;
        }
        // NOTE: 这个过滤器会看到**每一个**控件的 `Show`（窗口一露头，里面所有子控件都会走
        //       一趟）—— 所以这一句 `qobject_cast` 就是它的全部开销，其余一律原样放行。
        auto *bar = qobject_cast<QScrollBar *>(watched);
        if (bar == nullptr) {
            return false;
        }
        Anim::scrollBarAppear(bar);
        return false;
    }
};

}  // namespace

namespace Anim {

bool enabled() {
    return g_enabled;
}

void setEnabled(bool on) {
    g_enabled = on;
}

bool installPressAnimation(QApplication &app) {
    if (app.property(kFilterMark).toBool()) {
        return true;
    }
    app.setProperty(kFilterMark, true);
    app.installEventFilter(new PressFilter(&app));
    return true;
}

bool installScrollBarAnimation(QApplication &app) {
    if (app.property(kScrollFilterMark).toBool()) {
        return true;
    }
    app.setProperty(kScrollFilterMark, true);
    app.installEventFilter(new ScrollBarFilter(&app));
    return true;
}

void scrollBarAppear(QWidget *bar) {
    if (bar == nullptr || !enabled()) {
        return;
    }
    AnimEffect *effect = ensureEffect(bar);
    if (effect == nullptr) {
        return;
    }
    // 和 `overlayAppear()` 是同一条路（**只淡、不抬**）：滚动条就贴在它自己那一格里，
    // "往下挪着画"没有任何意义，只会让它看着离开自己那条边线。
    effect->setOpacity(0.0);
    runProgress(bar, effect, 0.0, 1.0, scrollBarMs(), /*removeWhenDone=*/true,
                [effect](qreal progress) { effect->setOpacity(progress); });
}

void pageAppear(QWidget *widget) {
    if (widget == nullptr || !enabled()) {
        return;
    }
    AnimEffect *effect = ensureEffect(widget);
    if (effect == nullptr) {
        return;
    }
    const qreal rise = pageRiseDistance();
    // 第一帧就摆到"还没进来"的样子：0 不透明 + 往下 `rise` 那么多（`draw()` 里是整块往下挪
    // 着画，所以多出来的那一条在**上面**、被裁掉的是下面那一条）。
    effect->setOpacity(0.0);
    effect->setShift(rise);
    runProgress(widget, effect, 0.0, 1.0, appearMs(), /*removeWhenDone=*/true, [effect, rise](qreal progress) {
        effect->setOpacity(progress);
        effect->setShift(rise * (1.0 - progress));
    });
}

void overlayAppear(QWidget *widget) {
    if (widget == nullptr || !enabled()) {
        return;
    }
    AnimEffect *effect = ensureEffect(widget);
    if (effect == nullptr) {
        return;
    }
    effect->setOpacity(0.0);
    runProgress(widget, effect, 0.0, 1.0, overlayMs(), /*removeWhenDone=*/true,
                [effect](qreal progress) { effect->setOpacity(progress); });
}

void animateExpansion(QTreeView *view) {
    if (view == nullptr) {
        return;
    }
    // 这一位交给 Qt 自己（`QTreeView` 的展开/折叠动画就是它）。关掉动效时关回去 ——
    // 出厂值也是关着的，所以验证工程那边跑起来和以前一模一样。
    view->setAnimated(enabled());
}

void slideUp(QWidget *window, const QPoint &target) {
    if (window == nullptr) {
        return;
    }
    if (!enabled()) {
        window->move(target);
        return;
    }
    const QPoint start = target + QPoint(0, panelSlideDistance());
    window->move(start);
    auto *animation = new QPropertyAnimation(window, "pos", window);
    animation->setDuration(panelMs());
    animation->setStartValue(start);
    animation->setEndValue(target);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    // 兜底：跑完一定落到算好的那个点上（中途被打断、被拖动这些情况也算）。
    QObject::connect(animation, &QPropertyAnimation::finished, window,
                     [w = QPointer<QWidget>(window), target]() {
                         if (w != nullptr) {
                             w->move(target);
                         }
                     });
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

}  // namespace Anim
