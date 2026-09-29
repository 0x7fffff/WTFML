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
#include <QPalette>
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

    /// 底下垫的那一层（**整块不透明、不跟着内容一起淡**）。见 `draw()` 里那一段。
    QColor backdrop() const { return m_backdrop; }
    void setBackdrop(const QColor &color) {
        if (m_backdrop == color) {
            return;
        }
        m_backdrop = color;
        update();
    }

    /// 「这一发淡的是**一张定住的图**」—— 见 `pageAppear()` 里那段（换页专用）。
    /// 每叫一次就把上一张丢掉：下一帧重照一张。
    void beginSnapshot() {
        m_snapshot = true;
        m_snapshotPixmap = QPixmap();
        m_snapshotOffset = QPoint();
    }

    /// ★ 这一条就是"不许有元素超出"的那一句：效果的有效范围**就是源矩形自己**
    /// （默认实现也是这么返回的，这里明写出来，免得将来有人"顺手"给它加个外扩）。
    QRectF boundingRectFor(const QRectF &sourceRect) const override { return sourceRect; }

protected:
    void draw(QPainter *painter) override {
        QPoint offset;
        // ★★ 「定住的那一张」（换页那一发，见 `pageAppear()`）。**照一次，后面每一帧都拿它**。
        //
        //    为什么非定住不可：`sourcePixmap()` 是**现照** —— 控件这一帧画成什么样，淡的就是
        //    什么样。换页那一刻正好是"内容还在往里灌"的时候（典型：服务器列表那一页，
        //    进页就自动测速，结果一个一个回来 —— 行会从一行字长成两行、favicon 也会补上），
        //    于是**淡的过程中页面自己在长高、自己在下跳**。用户看起来就是"这一发动画卡卡的"
        //    （2026-09-29 原话：「服务器列表那栏动画卡卡的」）：帧其实没丢（那一页照一帧
        //    只要 1 ms 出头），丢的是"这一发到底在淡什么" —— 每一帧淡的都不是同一张画面。
        //
        //    定住之后这一发只有一个动作：把那一张图 blit 上去、改一改不透明度。中途进来的
        //    数据照旧落到控件上（控件本身一个字节没动），只是**要等这一发跑完才看得见**。
        //    NOTE: 只在换页那一发这样干。按下那一发**不能**定住 —— 手指按着的时候那个按钮
        //          自己还在重画（悬停、文字），定住就成了一块贴死的旧图。
        QPixmap source;
        if (m_snapshot && !m_snapshotPixmap.isNull()) {
            source = m_snapshotPixmap;
            offset = m_snapshotOffset;
        } else {
            source = sourcePixmap(Qt::DeviceCoordinates, &offset,
                                  QGraphicsEffect::PadToEffectiveBoundingRect);
            if (m_snapshot && !source.isNull()) {
                m_snapshotPixmap = source;
                m_snapshotOffset = offset;
            }
        }
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
        // ★★ 底下那一层（用户 2026-09-29 点名要"延迟"之后才有的）：**整块铺成窗口底色、
        //    而且不跟着内容一起淡**。
        //
        //    为什么不铺就出事：淡的那个控件（内容区）**底下那些不透明的子控件**
        //    （列表/树的视口 —— `QAbstractScrollArea` 的视口自带底色，Qt 认它"自己会画满"）
        //    平时由它们自己画，所以**上面几层的底色都会跳过它们那一块**。可一旦这个控件挂了
        //    效果，子控件就改成画进效果那一张离屏图里了 —— 于是那块地方**屏幕上没有任何人去
        //    画**：`grab()` 出来的是一块黑（那张 pixmap 的原始字节），屏幕上则是**上一帧留下的
        //    像素**（也就是"上一页的列表还挂在那儿"）。干等那一段透明度是 0，看得最清楚。
        //    铺一层底色把这整块盖住，那一整块就总是"我们自己画满"的 —— 干等时看到的就是
        //    干净的窗口底色（"卡片还没上来"），跟旁边一圈完全接得上。
        if (m_backdrop.isValid() && m_backdrop.alpha() > 0) {
            painter->setOpacity(1.0);
            painter->fillRect(QRectF(offset, source.size()), m_backdrop);
        }
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
    QColor m_backdrop;  // 无效色 = 不铺（按下 / 提示卡 / 滚动条那几种都不铺）
    /// 「这一发淡的是一张定住的图」（换页那一发；见 `beginSnapshot()` 与 `draw()`）。
    bool m_snapshot = false;
    /// 定住的那一张（第一帧照的）+ 它当时的落点。`isNull()` = 还没照到。
    QPixmap m_snapshotPixmap;
    QPoint m_snapshotOffset;
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
 *
 * `delayMs`：**起手先干等这么久**，到点了才开始动（只有换页那一发用得上，见
 * `Anim::pageDelayMs()`）。默认 0 = 和从前一模一样。
 */
void runProgress(QWidget *widget, AnimEffect *effect, qreal from, qreal to, int ms, bool removeWhenDone,
                 const std::function<void(qreal)> &apply, int delayMs = 0) {
    // 同一个控件上又进来一发（连点两下、连切两页）：把上一发停掉。不停的话两个动画会
    // 交叉着写同一组数，看着像"抖"。
    //
    // NOTE: 带延迟的那一发也在这条路上收住 —— 它不是"另起一个定时器等会儿再开"，而是**同一
    //       个动画**前面多走一段（见下面 `delayMs` 那一段）。所以连切两页时不会有一个
    //       没人管的定时器事后又把上一页淡一遍。
    const QList<QVariantAnimation *> running = effect->findChildren<QVariantAnimation *>();
    for (QVariantAnimation *old : running) {
        old->stop();
        old->deleteLater();
    }

    auto *animation = new QVariantAnimation(effect);
    animation->setDuration(qMax(1, qMax(0, delayMs) + qMax(1, ms)));
    animation->setStartValue(from);
    animation->setEndValue(to);
    if (delayMs <= 0) {
        animation->setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(animation, &QVariantAnimation::valueChanged, effect,
                         [apply](const QVariant &value) { apply(value.toReal()); });
    } else {
        // ★ 带起手延迟的那一发：**同一条时间轴上先一动不动，到点了才开始缓动**。
        //
        // 两个坑，都在这里：
        //   1) **不能**"另起一个 `QTimer` 等会儿再调 `runProgress()`" —— 那样连切两页时
        //      会有两个动画（和一个没人管的定时器）交叉着写同一组数。
        //   2) 缓动曲线**必须自己算**。`QVariantAnimation` 的缓动是套在整条时长上的，
        //      直接把 `OutCubic` 摆上去，前面干等的那一段也会被一起缓动进去 —— 到点那一刻
        //      进度已经走掉一大截，"延迟"就白设了。所以这里把动画自己的曲线换成线性
        //      （`0→1` 均匀走），真正那一档缓动在下面按"去掉前面那一段之后"的进度算。
        animation->setEasingCurve(QEasingCurve::Linear);
        animation->setStartValue(0.0);
        animation->setEndValue(1.0);
        const QEasingCurve ease(QEasingCurve::OutCubic);
        const qreal lead = qreal(delayMs) / qreal(delayMs + qMax(1, ms));
        QObject::connect(animation, &QVariantAnimation::valueChanged, effect,
                         [apply, from, to, lead, ease](const QVariant &value) {
                             const qreal t = value.toReal();
                             if (t <= lead) {
                                 apply(from);  // 干等那一段：一直是"还没进来"的样子
                                 return;
                             }
                             const qreal p = (t - lead) / (1.0 - lead);
                             apply(from + (to - from) * ease.valueForProgress(p));
                         });
    }
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
    // （垫底色只有换页那一发要 —— 见 `pageAppear()`。同一个控件上换着花样跑动画时
    //  这一句保证不会把上一发垫的那层留着。）
    effect->setBackdrop(QColor());
    effect->setOpacity(0.0);
    runProgress(bar, effect, 0.0, 1.0, scrollBarMs(), /*removeWhenDone=*/true,
                [effect](qreal progress) { effect->setOpacity(progress); });
}

void pageAppear(QWidget *widget) {
    if (widget == nullptr) {
        return;
    }
    // 顶层那一档（内容区整个 `QTabWidget`）：垫的是**顶层窗口**的窗口底色 —— 内容区之外
    // 那一圈（标题栏、底栏）铺的就是它。内层那几层要垫的是视口的 `Base`，见两参数那一份。
    pageAppear(widget, widget->window()->palette().color(QPalette::Window));
}

void pageAppear(QWidget *widget, const QColor &backdrop) {
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
    // ★ 这一句在 `pageDelayMs()` 那一段干等里也得一直成立 —— 它是"起手延迟"看得见的那一半
    //   （另一半在 `runProgress()` 里，见它上面那段）。
    // ★★ 底下垫一层底色（调色板哪一档由调用方说，见两参数那一份的说明）：干等那一段
    //    **整块**就是这个色，淡上去的时候内容是从它上面浮出来的（那一层的来龙去脉写在
    //    `AnimEffect::draw()` 里）。
    //    NOTE: 设了页面背景图（`Theme::Part::PageBackground`）时那张图是**页面自己**画的、
    //          也跟着一起淡，这一层垫的还是底色 —— 那一档下干等那一刻会短暂地少一层图。
    effect->setBackdrop(backdrop);
    effect->setOpacity(0.0);
    effect->setShift(rise);
    // ★★ 这一发淡的是**一张定住的图**（第一帧照一次，后面每一帧都拿它）—— 理由整段写在
    //    `AnimEffect::draw()` 里那一段。一句话：换页那一刻正是"内容还在往里灌"的时候，
    //    现照现淡的话，淡的每一帧都不是同一张画面（用户 2026-09-29：「服务器列表那栏动画
    //    卡卡的」）。定住之后这一发只剩一个动作：blit + 改不透明度。
    effect->beginSnapshot();
    runProgress(widget, effect, 0.0, 1.0, appearMs(), /*removeWhenDone=*/true,
                [effect, rise](qreal progress) {
                    effect->setOpacity(progress);
                    effect->setShift(rise * (1.0 - progress));
                },
                /*delayMs=*/pageDelayMs());
}

void overlayAppear(QWidget *widget) {
    if (widget == nullptr || !enabled()) {
        return;
    }
    AnimEffect *effect = ensureEffect(widget);
    if (effect == nullptr) {
        return;
    }
    effect->setBackdrop(QColor());  // 同上：提示卡那一层是盖在**已经画好的界面**上的，不垫底
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
