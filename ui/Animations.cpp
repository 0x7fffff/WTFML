/**
 * @file Animations.cpp
 * @brief `Animations.hpp` 那一套的实现。规矩（不许有元素超出 / 几何一个像素都不动）写在头文件里。
 */

#include "Animations.hpp"

#include <QAbstractAnimation>
#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QColor>
#include <QDebug>
#include <QEasingCurve>
#include <QElapsedTimer>
#include <QEvent>
#include <QGraphicsEffect>
#include <QKeyEvent>
#include <QLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPointer>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QSettings>
#include <QSharedPointer>
#include <QTabWidget>
#include <QTimer>
#include <QTransform>
#include <QTreeView>
#include <QVariant>
#include <QVariantAnimation>
#include <QWidget>

#include <functional>

namespace {

/// 我们自己装的那个效果叫什么。别人（将来）在同一个控件上装过别的效果时，靠这个名字认出来
/// 那是"不是我们的"，然后**一个字节都不碰**。
constexpr const char kEffectName[] = "wtfmlAnimEffect";
/// 换页那块幕布叫什么（见 `PageCurtain`）。认它就是"我们自己那一块"。
constexpr const char kCurtainName[] = "wtfmlPageCurtain";
/// 事件过滤器装没装过的那一位（挂在 QApplication 自己身上，所以不用静态变量）。
constexpr const char kFilterMark[] = "wtfmlAnimPressFilter";
/// 滚动条那一份过滤器装没装过的那一位（同上）。
constexpr const char kScrollFilterMark[] = "wtfmlAnimScrollFilter";
/// 动画采样定时器的名字；同一控件重启动画时据此停止上一发。
constexpr const char kStepperName[] = "wtfmlAnimStepper";
/// 进度条上那一位"上一趟见到的 `maximum()`"（见 `Anim::smoothProgress()`）。
/// 记在**进度条自己身上**：这一层没有实例、也不该为进度条另立一张表（那张表还得管生灭）。
constexpr const char kProgressMaxMark[] = "wtfmlAnimProgressMax";

/// 动效总开关。默认开。**只看这一位**：不跟随系统「动画效果」（理由见 `enabled()`）。
bool g_enabled = true;

/// 用户那把开关存哪儿。**不在 `Theme/` 底下**（`Theme::save()` 会整组抹掉那个前缀）。
const char *const kEnabledKey = "Look/Animations";

/**
 * 动效用的那一个 `QGraphicsEffect`：把控件**渲染好的那一张图**贴回来，贴上之前可以
 * 整体确定不透明度（淡入淡出）、整体压暗一档（按下）。
 *
 * 【为什么是"贴一张图"而不是改控件自己】
 *
 * 因为改不了：底下的颜色是样式表画的、按下的那一档也是，而"动画"要的是**每一帧都不一样**。
 * 渲染成图之后，"这一帧是什么样"就完全攥在这一层手里了，控件自己（几何、布局、样式表、
 * 调色板）**一个字节都不动** —— "不许有元素超出"于是变成结构上做不到，
 * 而不是靠分寸拿捏。
 *
 * NOTE: 这一层**只管"淡"和"压暗"**（换页那一位在 `PageCurtain` 里，包括"抬"与
 * "定住的那一张"）。
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

    qreal scrim() const { return m_scrim; }
    void setScrim(qreal value) {
        if (qFuzzyCompare(m_scrim, value)) {
            return;
        }
        m_scrim = value;
        update();
    }

    /// 这一条就是"不许有元素超出"的那一句：效果的有效范围**就是源矩形自己**
    /// （默认实现也是这么返回的，这里明写出来，免得将来有人"顺手"给它加个外扩）。
    QRectF boundingRectFor(const QRectF &sourceRect) const override { return sourceRect; }

protected:
    void draw(QPainter *painter) override {
        QPoint offset;
        const QPixmap source = sourcePixmap(Qt::DeviceCoordinates, &offset,
                                            QGraphicsEffect::PadToEffectiveBoundingRect);
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
        // 两把尺子必须分清楚：**画笔这一层一律逻辑像素，`source` 的像素矩形一律设备
        // 像素**。混着用的话，真帧里内容会被推到 1.5 倍的位置上。
        const qreal dpr = source.devicePixelRatio() > 0.0 ? source.devicePixelRatio() : 1.0;
        const QRectF widgetRect(offset, QSizeF(source.size()) / dpr);
        // 再上一道保险：不管上面那些数怎么算错，都画不到源矩形之外去。
        painter->setClipRect(widgetRect, Qt::IntersectClip);
        // NOTE: 底下垫底色那一层在 `PageCurtain` 里（那一层照旧垫 —— 起手那几帧是
        // 半透明的，不垫的话不透明子控件视口那一块屏幕上留的是上一帧的像素）。剩下这三发
        // （按下 / 滚动条 / 覆盖层）都是**盖在已经画好的界面**上的，不垫。
        painter->setOpacity(qBound(0.0, m_opacity, 1.0));
        painter->drawPixmap(offset, out);
    }

private:
    qreal m_opacity = 1.0;
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
    if (AnimEffect *effect = ourEffect(widget)) {
        Q_UNUSED(effect);
        widget->setGraphicsEffect(nullptr);
    }
}

/**
 * 换页那一发用的那块**幕布**。
 *
 * 【为什么不接着用 `QGraphicsEffect`】
 *
 * 挂在控件上的效果**只重画控件自己那一层**。它身上的**子控件**（列表 / 树的视口这类
 * 不透明控件）那一块**停在起手那一帧**：页面自己画的文字每一拍都在动，而子控件里的
 * 那些行整整一发一动不动地停在起手位置和起手浓淡 —— 也就是说那一块屏幕显示的是
 * **这一发第一帧的合成结果**，跑完之后效果一摘才"啪"地对齐，看着就是瞬移。
 *
 * 换 `repaint()`（同步整块重画）、把抬的量按整块走（不分"框 / 内容"）、只淡不抬，
 * 三种都不行 —— 子控件那一块**照样定住**（只淡不抬那一版里它站的位置是对的、
 * 浓淡仍然是死的）。这一层里的病因不在我们的
 * `draw()`（那里画的每一帧都真的上屏了），在 Qt 的效果管线本身。
 *
 * 【修法：不用效果，自己盖一块普通控件】
 *
 * 幕布是一块**普通的 `QWidget`**，自己 `paintEvent` 把"起手照的那一张图"按
 * `shift` / `opacity` 画出来。普通控件走的是 Qt 平时的绘制路径（`update()` → 攒脏区 →
 * 重画），**没有效果管线那一层**，所以每一帧整块都是新的。
 *
 * 【它怎么做到"抬的是内容、框一个像素都不动"】
 *
 * 幕布的**几何就是那一块内容自己的矩形**（`liftTarget()` 算出来的），一个像素都不多 ——
 * 外框（选项卡那张卡片、列表那圈框）长在幕布**外面**，这一发从头到尾没碰过它。
 * 这也是"不许有元素超出"的结构保证：`paintEvent` 里还自己钉了一道 `setClipRect`。
 *
 * 【淡的仍然是"一张定住的图"】
 *
 * 起手照一次，之后每一帧都拿那一张 —— 理由见 `pageAppear()`：换页那一刻
 * 常是"内容还在往里灌"的时候，现照现淡的话这一发淡的每一帧都不是同一张画面。尺寸变了
 * 才重照。
 */
class PageCurtain : public QWidget {
public:
    explicit PageCurtain(QWidget *parent) : QWidget(parent) {
        setObjectName(QLatin1String(kCurtainName));
        // 它只是一层"画上去的膜"：不吃鼠标、不吃键盘、不参与布局、不抢焦点。
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setFocusPolicy(Qt::NoFocus);
        hide();
    }

    /// 摆好位置、照一张新的、露头。`target` = 要抬的那一块（`liftTarget()` 算的）。
    void start(QWidget *target, const QColor &backdrop) {
        m_target = target;
        m_backdrop = backdrop;
        // 实底由幕布整幅填满，不必每帧重新绘制下面被遮住的整页。
        // 半透明底色仍需 Qt 合成下层，不能声明不透明。
        setAttribute(Qt::WA_OpaquePaintEvent, backdrop.isValid() && backdrop.alpha() == 255);
        m_snapshot = QPixmap();   // 这一发重新照一张
        m_snapshotSize = QSize();
        if (target != nullptr && parentWidget() != nullptr) {
            setGeometry(QRect(target->mapTo(parentWidget(), QPoint(0, 0)), target->size()));
        }
        // 照这一张**必须**在 paintEvent 外面（理由见类头上 那一段）。
        ensureSnapshot();
        show();
        raise();
    }

    /// 这一帧长什么样。`shift` = 内容往下挪多少（逻辑像素），`opacity` = 整块多不透。
    void setFrame(qreal shift, qreal opacity) {
        m_shift = shift;
        m_opacity = opacity;
        // 目标中间改了尺寸（第一拍迟到、摆版还没落定的那一下）就在这儿补照：每一拍都在
        // paint 外面，是唯一安全的补照时机。尺寸没变时它只是一句比较，一个像素都不抓。
        ensureSnapshot();
        update();   // ★ 普通控件，这一句就是"这一帧真的会重画"的唯一出处
    }

protected:
    void paintEvent(QPaintEvent *) override {
        if (m_target == nullptr || m_grabbing) {
            return;
        }
        // NOTE: 幕布和它照的那一块都是 `parentWidget()` 的孩子（幕布是**兄弟**，不是祖先）
        // —— 所以这里量位置问的是 `parentWidget()`，不是 `this`（问 `this` 的话
        // Qt 会报 `QWidget::mapTo(): parent must be in parent hierarchy` 然后返回一个
        // 没人认的坐标，症状是整块幕布一个像素都不画）。
        QWidget *host = parentWidget();
        if (host == nullptr) {
            return;
        }
        const QRect inHost(m_target->mapTo(host, QPoint(0, 0)), m_target->size());
        const QRect inMe = rect();
        if (inHost.isEmpty() || inMe.isEmpty()) {
            return;
        }
        // 「淡的是一张定住的图」：快照已经由 `ensureSnapshot()` 在 paint 外面照好了。
        // 这里**绝不**抓图 —— 在这一摞栈上 `grab()` 会把正在画的这块幕布也重画一遍
        // （递归重绘）。
        QPainter painter(this);
        // `setGeometry()` 已经把幕布搬到了 inHost；QPainter 用的是幕布**自己的**坐标，
        // 这里必须从 (0, 0) 画。再用 inHost 会把目标相对父层的偏移重复加一次，幕布左上会
        // 留出一块透明区域，动画时正好透出后面的灰色窗口。
        // 「不许有元素超出」：画之前先把范围收到这一格（挪下去的那一条也不会跑出去）。
        painter.setClipRect(inMe, Qt::IntersectClip);
        // 底下先铺满底色：起手那几帧是半透明的，而 `target` 底下可能有"谁都不画"的
        // 不透明子控件视口（那一块屏幕上留着的是上一帧的像素）。铺一层就总是满的。
        if (m_backdrop.isValid() && m_backdrop.alpha() > 0) {
            painter.setOpacity(1.0);
            painter.fillRect(inMe, m_backdrop);
        }
        painter.setOpacity(qBound(0.0, m_opacity, 1.0));
        // 「抬」这一位按**逻辑像素**说；落到屏幕上再吸到**整设备像素**上
        // （150% 缩放下 1 逻辑 px = 1.5 设备 px —— 不吸的话内容每一拍都停在半像素上，
        // 文字会发虚）。
        const qreal dpr = m_snapshot.devicePixelRatio() > 0.0 ? m_snapshot.devicePixelRatio() : 1.0;
        const qreal shift = m_shift > 0.0 ? qRound(m_shift * dpr) / dpr : 0.0;
        painter.drawPixmap(QPointF(inMe.topLeft()) + QPointF(0.0, shift), m_snapshot);
    }

private:
    /// 照那一张"起手定住"的图。**只能在 paintEvent 外面调**（在里面抓 = 抓到自己，
    /// 见类头上那一段）。
    void ensureSnapshot() {
        if (m_target == nullptr || parentWidget() == nullptr) {
            return;
        }
        if (!m_snapshot.isNull() && m_snapshotSize == m_target->size()) {
            return;   // 已经照好了（尺寸也没变），一个字节都不碰。
        }
        QWidget *host = parentWidget();
        const QRect inHost(m_target->mapTo(host, QPoint(0, 0)), m_target->size());
        if (inHost.isEmpty()) {
            return;
        }
        // NOTE: 不能直接 `m_target->grab()`：页面本身经常是透明光板，单独抓它时 Qt 会
        // 用 `QPalette::Window` 补底（Fusion 下就是灰的），而屏幕上真正透出来的是
        // 父层那张白卡片。动画期间于是会先灰、收掉幕布后才突然变白。
        // 从 host 的对应区域抓，拿到的才是用户此刻真正看见的合成结果。
        // 幕布自己可能就在 host 里面，所以照相时让自己一个像素都不画，避免照镜子
        // （`m_grabbing` 那道早退 —— 抓图会把这块幕布也重画一遍）。
        QWidget *grabSource = host;
        if (grabSource == m_target) {
            if (QWidget *top = m_target->window()) {
                grabSource = top;
            }
        }
        const QRect inGrab(grabSource == host
                               ? inHost
                               : QRect(m_target->mapTo(grabSource, QPoint(0, 0)),
                                       m_target->size()));
        m_grabbing = true;
        // 抓图时幕布不画自己，也不能遮挡下层的绘制，否则快照会留下空洞。
        const bool opaque = testAttribute(Qt::WA_OpaquePaintEvent);
        setAttribute(Qt::WA_OpaquePaintEvent, false);
        QElapsedTimer captureClock;
        captureClock.start();
        m_snapshot = grabSource->grab(inGrab);
        if (captureClock.elapsed() > 32)
            qInfo() << "[UI timing] slow page snapshot" << m_target->metaObject()->className()
                    << inGrab.size() << captureClock.elapsed() << "ms";
        m_snapshotSize = m_target->size();
        setAttribute(Qt::WA_OpaquePaintEvent, opaque);
        m_grabbing = false;
    }

    QPointer<QWidget> m_target;   ///< 要抬的那一块（照它、也按它的尺寸定位）。
    QPixmap m_snapshot;           ///< 起手照的那一张（定住）。
    QSize m_snapshotSize;
    QColor m_backdrop;
    qreal m_shift = 0.0;
    qreal m_opacity = 1.0;
    bool m_grabbing = false;      ///< 正在照那一张（照的时候自己别画）。
};

/// 定时器只决定采样频率，动画进度始终按真实经过的时间推进。
struct StepRun {
    QPointer<QVariantAnimation> animation;
    QElapsedTimer clock;
};

/**
 * 跑一发进度动画，每一帧交给 `apply()` 去摆那个效果 / 那块幕布上的几个数。
 *
 * `apply` 收到的是**已经映射好的值**（`from` → `to`，走 `OutCubic`），不是 0→1。
 *
 * `owner`：这一发动画和步进器挂在谁身上（效果的拥有者那几发就是效果自己；换页那几发是
 * 那块幕布）。同一个 `owner` 上又进来一发时，上一发会被停掉。
 * `repaintTarget`：每一拍推完之后要 `update()` 的那一块（效果那几发就是挂着效果的那个
 * 控件；换页那几发是幕布自己）。
 * `finish`：跑完做的那一下（可能为空 = 不收尾）。
 *
 * `stepMs`：大于 0 时按指定周期采样真实经过时间；0 时使用 Qt 的动画时钟。
 * 两条路径都以 `ms` 为时长，界面繁忙时跳过过期帧，不延长整段动画。
 *
 * **没有 `delayMs` 这种"起手先干等一段"的参数**：换页那一发也无需干等，
 * 起手直接淡，理由见 `Animations.hpp` 里 `appearMs()` 上面那段。
 */
void runProgress(QObject *owner, QWidget *repaintTarget, qreal from, qreal to, int ms, int stepMs,
                 const std::function<void()> &finish, const std::function<void(qreal)> &apply) {
    if (owner == nullptr) {
        return;
    }
    // 同一个控件上又进来一发（连点两下、连切两页）：把上一发停掉。不停的话两个动画会
    // 交叉着写同一组数，看着像"抖"。
    const QList<QVariantAnimation *> running = owner->findChildren<QVariantAnimation *>();
    for (QVariantAnimation *old : running) {
        old->stop();
        old->deleteLater();
    }
    const QList<QTimer *> stepping = owner->findChildren<QTimer *>(QLatin1String(kStepperName));
    for (QTimer *old : stepping) {
        old->stop();
        old->deleteLater();
    }

    auto *animation = new QVariantAnimation(owner);
    animation->setDuration(qMax(1, ms));
    animation->setStartValue(from);
    animation->setEndValue(to);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    // 每一拍推完之后让那一块自己重画一遍。效果那几发这儿只叫得动"控件自己"那一层
    // （子控件那一块要等它自己脏了才画 —— 这正是换页那一发不能用效果的原因，见
    // `PageCurtain` 那段）；幕布是一块普通控件，这一句就是"整块都新"。
    QObject::connect(animation, &QVariantAnimation::valueChanged, owner,
                     [apply, w = QPointer<QWidget>(repaintTarget)](const QVariant &value) {
                         apply(value.toReal());
                         if (w != nullptr) {
                             w->update();
                         }
                     });

    // NOTE: 收尾那一下**不能**在 `finished` 里直接做：那一刻我们是站在动画自己的信号里，
    // 而动画是 `owner` 的孩子 —— 删父亲等于把这个正在发信号的动画也删了。
    // 挪到下一轮做。
    const auto finishLater = [owner, finish]() {
        if (!finish) {
            return;
        }
        QTimer::singleShot(0, owner, [finish]() { finish(); });
    };

    if (stepMs <= 0) {
        if (finish) {
            QObject::connect(animation, &QVariantAnimation::finished, owner,
                             [finishLater]() { finishLater(); });
        }
        animation->start(QAbstractAnimation::DeleteWhenStopped);
        return;
    }

    // 不能按 timeout 次数计步：主线程忙时 Qt 会合并定时器事件，
    // 等够固定拍数会把短动画拖成长时间停顿。迟到的采样直接追到当前时间。
    auto state = QSharedPointer<StepRun>::create();
    state->animation = animation;
    auto *stepper = new QTimer(owner);
    stepper->setObjectName(QLatin1String(kStepperName));
    stepper->setTimerType(Qt::PreciseTimer);
    stepper->setInterval(stepMs);
    QObject::connect(stepper, &QTimer::timeout, owner, [state, stepper, finishLater]() {
        QVariantAnimation *animation = state->animation.data();
        if (animation == nullptr) {
            stepper->stop();
            stepper->deleteLater();
            return;
        }
        const qint64 elapsed = state->clock.elapsed();
        const int duration = animation->duration();
        const int current = int(qMin(elapsed, qint64(duration)));
        animation->setCurrentTime(current);
        // valueChanged 可重入；应用这一帧时若页面已销毁，不再访问其定时器。
        if (state->animation.isNull()) return;
        if (current < duration) return;
        stepper->stop();
        stepper->deleteLater();
        finishLater();
        animation->deleteLater();
    });
    state->clock.start();
    stepper->start();
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
    runProgress(effect, button, effect->scrim(), target, Anim::pressInMs(), /*stepMs=*/0,
                /*finish=*/nullptr, [effect](qreal value) { effect->setScrim(value); });
}

/// 松手：弹回原样，然后把这层效果摘掉。
void pressUp(QAbstractButton *button) {
    AnimEffect *effect = ourEffect(button);
    if (effect == nullptr) {
        return;
    }
    runProgress(effect, button, effect->scrim(), 0.0, Anim::pressOutMs(), /*stepMs=*/0,
                [w = QPointer<QWidget>(button)]() { dropEffect(w); },
                [effect](qreal value) { effect->setScrim(value); });
}

/**
 * 挂在 `QApplication` 上的那个事件过滤器：谁被按下去就动谁。
 *
 * NOTE: 它**从不吞事件**（永远返回 `false`）—— 这一层只是"顺便看一眼"，按钮自己该怎么
 * 响应还怎么响应。
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
 * 滚动条不能是瞬间蹦出来的。滚动条是按需出现的
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
        // 一趟）—— 所以这一句 `qobject_cast` 就是它的全部开销，其余一律原样放行。
        auto *bar = qobject_cast<QScrollBar *>(watched);
        if (bar == nullptr) {
            return false;
        }
        Anim::scrollBarAppear(bar);
        return false;
    }
};

/**
 * 「这一块抬起来的时候，**抬的到底是哪一块**」—— `pageAppear()` 用它（整段理由见那儿）。
 *
 * 规矩一句话：**自己有那圈外框的控件，抬它里面那一块**。那圈外框是屏幕上一个**不动的**
 * 参照物（换页前后它都在原地），让它跟着往下挪，看着就是"框瞬移"；里面的内容是新上来的，
 * 往下挪再滑上去才像一次转场。
 *
 * * `QTabWidget`（顶上那四颗导航翻出来的内容区）⇒ `currentWidget()`：用户看到的那圈
 * 卡片和外框**是选项卡自己画的**，页面是它里面那一块（`ThemeBackground` 一个像素都不画）。
 * * `QAbstractScrollArea`（列表 / 树 / 滚动区 —— 它们自带那圈 `#b9b9b9` 的框）⇒ `viewport()`：
 * 框、表头、滚动条都长在视口**外面**，抬视口就正好是"内容抬、框不动"。
 * * 别的（下载页/设置页里那些光板一页、实例设置那一列页表）⇒ 就是它自己：那些块没有单独
 * 画的框，整块抬也成立。
 */
QWidget *liftTarget(QWidget *widget) {
    if (widget == nullptr) {
        return nullptr;
    }
    if (auto *tabs = qobject_cast<QTabWidget *>(widget)) {
        if (QWidget *page = tabs->currentWidget()) {
            return page;
        }
    }
    if (auto *area = qobject_cast<QAbstractScrollArea *>(widget)) {
        if (QWidget *view = area->viewport()) {
            return view;
        }
    }
    return widget;
}

// 布局切换只绘制两个稳定状态，避免在每帧重排列表或缩放文字。
class LayoutCurtain : public QWidget {
public:
    LayoutCurtain(QWidget *surface, const QRect &area, const QPixmap &before, const QPixmap &after,
                  bool expanding)
        : QWidget(surface), m_before(before), m_after(after), m_expanding(expanding) {
        setObjectName(QStringLiteral("wtfmlLayoutCurtain"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
        setGeometry(area);
        surface->installEventFilter(this);
    }
    void frame(qreal progress) { m_progress = progress; update(); }
    void captureDestination(const std::function<QPixmap()> &capture) {
        // 保持幕布可见，仅在离屏抓图时跳过自身，避免抓进旧快照。
        m_capturing = true;
        m_after = capture();
        m_capturing = false;
    }
protected:
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::Hide || event->type() == QEvent::Resize) {
            hide();
            for (auto *timer : findChildren<QTimer *>()) timer->stop();
            for (auto *animation : findChildren<QVariantAnimation *>()) animation->stop();
            deleteLater();
        }
        return false;
    }
    void paintEvent(QPaintEvent *) override {
        if (m_capturing) return;
        QPainter painter(this);
        painter.setClipRect(rect());
        painter.drawPixmap(0, 0, m_before);
        painter.setOpacity(m_progress);
        const qreal dpr = qMax(qreal(1), m_after.devicePixelRatio());
        const qreal shift = qRound((m_expanding ? 1 : -1) * Anim::pageRiseDistance() *
                                  (1 - m_progress) * dpr) / dpr;
        painter.drawPixmap(QPointF(0, shift), m_after);
    }
private:
    QPixmap m_before, m_after;
    bool m_expanding;
    bool m_capturing = false;
    qreal m_progress = 0;
};

}  // namespace

namespace Anim {

bool enabled() {
    // **只认本程序自己的开关**（设置页「界面动效」那个勾，默认开）。下面那些函数全是
    // "不 enabled 就原地返回"。
    //
    // 不读 Windows 的「动画效果」（`SPI_GETCLIENTAREAANIMATION`）：用户关掉系统动画时，
    // 整套动效会被静默关掉，界面上就是"动效一个都没有"。要减少动效在本程序设置里关。
    return g_enabled;
}

void setEnabled(bool on) {
    g_enabled = on;
}

void load() {
    QSettings settings;
    g_enabled = settings.value(QLatin1String(kEnabledKey), true).toBool();
}

void save() {
    QSettings settings;
    settings.setValue(QLatin1String(kEnabledKey), g_enabled);
    settings.sync();
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
    // 与页面转场共用真实时钟，迟到的绘制不会延长滚动条的淡入。
    runProgress(effect, bar, 0.0, 1.0, scrollBarMs(), stepMs(),
                [w = QPointer<QWidget>(bar)]() { dropEffect(w); },
                [effect](qreal progress) { effect->setOpacity(progress); });
}

void pageAppear(QWidget *widget) {
    if (widget == nullptr) {
        return;
    }
    // 默认垫的是**顶层窗口**的窗口底色 —— 内容和它那一圈之间露出来的地方通常就是它。
    // 内容区那一发（`QTabWidget`）垫的不是它，见 `MainWindowUI` 里那个调用点的说明。
    pageAppear(widget, widget->window()->palette().color(QPalette::Window));
}

void pageAppear(QWidget *widget, const QColor &backdrop) {
    if (widget == nullptr || !enabled()) {
        return;
    }
    // 「抬的是**内容**、框一个像素都不动」。
    //
    // 为什么框不能参与：这一发如果是**整块**（连它自己那圈外框）一起往下挪着画，而其中
    // 有些控件（内容区那个 `QTabWidget` 的卡片、实例列表那棵树）**是换页前后都在的
    // 同一块** —— 那圈外框就会起手先跳下去十几像素、再滑回原处，看着就是"框瞬移"。
    // 所以框压根不参与：盖上去的那块幕布**就是内容自己的矩形**（见 `liftTarget()`），
    // 外框长在幕布外面，这一发从头到尾碰都没碰过它。
    //
    // 为什么用普通控件而不是 `QGraphicsEffect`：效果器挂在整块上时，真机上子控件那一块
    // **定在起手那一帧**（完整理由写在 `PageCurtain` 那个类上）。幕布是**一块普通控件**
    // 盖在上面，`paintEvent` 自己画 —— 普通控件走 Qt 平时的绘制路径，每一帧整块
    // 都是新的，那一类"定住"从根上没有了。
    QWidget *lift = liftTarget(widget);
    if (lift == nullptr) {
        lift = widget;
    }
    // NOTE: `PageCurtain` 没有 `Q_OBJECT`（这一层不需要信号槽、也不需要 moc），所以这里
    // 够不到它的类型：按**名字**找回来再 `static_cast` —— 那个名字只有这一处会写。
    PageCurtain *curtain = nullptr;
    if (QWidget *existing = widget->findChild<QWidget *>(QLatin1String(kCurtainName))) {
        curtain = static_cast<PageCurtain *>(existing);
    } else {
        curtain = new PageCurtain(widget);
    }
    const qreal rise = pageRiseDistance();
    const qreal from = pageStartOpacity();
    curtain->start(lift, backdrop);
    // 第一帧就摆到"还没进来"的样子：`pageStartOpacity()` 那么透 + 往下 `rise` 那么多
    // （幕布里是整块往下挪着画，所以多出来的那一条在**上面**、被裁掉的是下面那一条）。
    curtain->setFrame(rise, from);
    // NOTE: 收尾只是**收起来**，不删 —— 同一个控件上下一次换页接着用这一块（省一次生灭，
    // 也躲开"上一发还在跑、下一发就来了"那一下的悬空指针）。
    // Page transitions follow elapsed time: dropped frames must not stretch a
    // 250 ms transition into a visible stall while a large list is laid out.
    runProgress(curtain, curtain, 0.0, 1.0, appearMs(), 0,
                [c = QPointer<QWidget>(curtain)]() {
                    if (c != nullptr) {
                        c->hide();
                    }
                },
                [curtain, rise, from](qreal t) {
                    // Movement uses ease-out; a gentler opacity ramp keeps the fade visible
                    // instead of becoming opaque in the first few movement frames.
                    curtain->setFrame(rise * (1.0 - t), from + (1.0 - from) * t * t);
                });
}


void vanishWidget(QWidget *widget, const std::function<void()> &done) {
    const auto finish = [done] { if (done) done(); };
    if (widget == nullptr || !enabled() || !widget->isVisible()) { finish(); return; }
    QWidget *host = widget->parentWidget();
    if (host == nullptr) { finish(); return; }
    auto *curtain = new PageCurtain(host);
    curtain->start(widget, QColor());
    curtain->setFrame(0.0, 1.0);
    widget->hide();
    const qreal rise = pageRiseDistance();
    runProgress(curtain, curtain, 0.0, 1.0, exitMs(), 0,
                [curtain, finish]() {
                    curtain->hide();
                    curtain->deleteLater();
                    finish();
                },
                [curtain, rise](qreal t) { curtain->setFrame(rise * t, 1.0 - t); });
}

void cancelLayoutTransition(QWidget *surface) {
    if (surface == nullptr) return;
    for (auto *old : surface->findChildren<QWidget *>(QStringLiteral("wtfmlLayoutCurtain"), Qt::FindDirectChildrenOnly))
        delete old;
}

void layoutTransition(QWidget *surface, const std::function<void()> &change,
                      bool expanding, const QRect &region) {
    if (!change) return;
    if (surface == nullptr) { change(); return; }
    QPointer<QWidget> guard(surface);
    const QRect area = (region.isEmpty() ? surface->rect() : region).intersected(surface->rect());
    const bool animate = enabled() && surface->isVisible() && !area.isEmpty();
    const auto capture = [surface, area]() {
        QWidget *window = surface->window();
        return window->grab(QRect(surface->mapTo(window, area.topLeft()), area.size()));
    };
    // 连点时从当前视觉帧起步，再移除旧幕布；旧动画不会晚到一步覆盖新状态。
    const QPixmap before = animate ? capture() : QPixmap();
    cancelLayoutTransition(surface);
    // 主选项卡刚切换时可能仍有旧的换页快照，不能把它当作新布局的终点。
    for (QWidget *ancestor = surface; ancestor != nullptr; ancestor = ancestor->parentWidget()) {
        for (auto *old : ancestor->findChildren<QWidget *>(QLatin1String(kCurtainName), Qt::FindDirectChildrenOnly))
            delete old;
    }
    for (auto *old : surface->findChildren<QWidget *>(QLatin1String(kCurtainName)))
        delete old;
    // 先遮住旧画面，再切换真实布局；延迟到下一轮事件后采集稳定终点。
    QPointer<LayoutCurtain> curtain;
    if (animate) {
        curtain = new LayoutCurtain(surface, area, before, QPixmap(), expanding);
        curtain->show();
        curtain->raise();
    }
    change();
    if (!guard) return;
    // openPage() 会 raise 覆盖层，动画快照必须重新位于它上方。
    if (curtain && !curtain->isHidden()) curtain->raise();
    if (surface->layout()) surface->layout()->activate();
    for (auto *child : surface->findChildren<QWidget *>())
        if (child->isVisible() && child->layout()) child->layout()->activate();
    if (!curtain) return;
    const int durationMs = expanding ? panelMs() : exitMs();
    QTimer::singleShot(0, curtain, [guard, curtain, area, capture, durationMs]() {
        if (!guard || !curtain) return;
        if (!enabled() || !guard->isVisible() || curtain->isHidden() ||
            !guard->rect().contains(area)) {
            curtain->hide();
            curtain->deleteLater();
            return;
        }
        if (guard->layout()) guard->layout()->activate();
        // 终点快照必须等 change() 之后排队的布局/润色都跑完再照。只 activate 顶层布局不够：
        // 新露出来的列表控件（滚动条、行高、按钮换字后的宽度）会再排一两轮，
        // 照早了动画结束时真实布局还会跳一下（"最后瞬移"）。
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::PolishRequest);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
        if (guard->layout()) guard->layout()->activate();
        for (auto *child : guard->findChildren<QWidget *>()) {
            if (child->isVisible() && child->layout() != nullptr) child->layout()->activate();
        }
        guard->update();
        curtain->raise();
        curtain->captureDestination(capture);
        runProgress(curtain, curtain, 0, 1, durationMs, 0,
                    [curtain]() { if (curtain) { curtain->hide(); curtain->deleteLater(); } },
                    [curtain](qreal progress) { if (curtain) curtain->frame(progress); });
    });
}

void clearOpacityEffect(QWidget *widget) { dropEffect(widget); }

void overlayAppear(QWidget *widget) {
    if (widget == nullptr || !enabled()) {
        return;
    }
    AnimEffect *effect = ensureEffect(widget);
    if (effect == nullptr) {
        return;
    }
    effect->setOpacity(0.0);
    // 覆盖层从全透明淡入；按真实经过时间收尾，避免建页的忙碌拖长转场。
    runProgress(effect, widget, 0.0, 1.0, overlayMs(), stepMs(),
                [w = QPointer<QWidget>(widget)]() { dropEffect(w); },
                [effect](qreal progress) { effect->setOpacity(progress); });
}

void animateExpansion(QTreeView *view) {
    if (view == nullptr) {
        return;
    }
    // 这一位交给 Qt 自己（`QTreeView` 的展开/折叠动画就是它）。关掉动效时关回去 ——
    // 出厂值也是关着的，验证工程跑起来就是瞬间展开。
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
    // 小面板与整页转场共用 OutCubic，避免进入动作使用不同的收尾节奏。
    // 它是"一小块面板弹出来"，要的是干脆利落 —— 16 px 走完就该能点上面的东西了。
    // 收尾圆滑在这儿的代价是"最后 1~2 px 还在慢慢挪"。
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

/// 往下弹的那一档（主页右上那张账号卡片那块面板）：从落点上面 `panelSlideDistance()`
/// 滑下来。**和 `slideUp()` 是同一条动效的两头** —— 时长、缓动、收尾兜底全都一样，
/// 只有起手位在另一边（调用方见 MainWindowUI 里 BottomPanel 的弹出那一段）。
void slideDown(QWidget *window, const QPoint &target) {
    if (window == nullptr) {
        return;
    }
    if (!enabled()) {
        window->move(target);
        return;
    }
    const QPoint start = target - QPoint(0, panelSlideDistance());
    window->move(start);
    auto *animation = new QPropertyAnimation(window, "pos", window);
    animation->setDuration(panelMs());
    animation->setStartValue(start);
    animation->setEndValue(target);
    // NOTE: 缓动曲线和 `slideUp()` 一致（那一发的理由写在它那儿）：面板弹出来要干脆，
    // 16 px 走完就该能点上面的东西。
    animation->setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(animation, &QPropertyAnimation::finished, window,
                     [w = QPointer<QWidget>(window), target]() {
                         if (w != nullptr) {
                             w->move(target);
                         }
                     });
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void rowAppear(QWidget *row) {
    if (row == nullptr || !enabled()) {
        return;
    }
    AnimEffect *effect = ensureEffect(row);
    if (effect == nullptr) {
        return;
    }
    // 起手全透明，与滚动条共用按真实时间推进的淡入。
    effect->setOpacity(0.0);
    runProgress(effect, row, 0.0, 1.0, rowAppearMs(), stepMs(),
                [w = QPointer<QWidget>(row)]() { dropEffect(w); },
                [effect](qreal progress) { effect->setOpacity(progress); });
}

void smoothProgress(QProgressBar *bar, int value) {
    if (bar == nullptr) {
        return;
    }
    // 上一趟见到的那个总量（含"第一次调用"：`isValid()` 为假时按"变了"处理 ⇒ 原地落实）。
    const QVariant remembered = bar->property(kProgressMaxMark);
    const int previousMaximum = remembered.isValid() ? remembered.toInt() : -1;
    const int maximum = bar->maximum();
    bar->setProperty(kProgressMaxMark, maximum);
    // 四条"如实更新"的路（规矩写在头文件那一段）：
    // · 总量变了（换了一批 / 从不确定态换回来 / 复位成 0–100）；
    // · 真进度倒退（重试、跳过校验那种情报不能被动画盖住）；
    // · 已经等于目标了（没什么好追的）；
    // · 这一格现在停在 0（"还回来"那一路：启动借走时被钳回 0，记下的总量却没变过
    // ⇒ 不拦的话用户看到的是下载在重头爬，见头文件那一段）；
    // 以及动效关着的时候。
    if (!enabled() || maximum != previousMaximum || value <= bar->value() || bar->value() <= 0) {
        bar->setValue(value);
        return;
    }
    // 追值**不走效果器**：进度条要动的是它自己的那个数，不是"贴一张图"。
    // 几何照旧一个像素都不动（这一层那条硬要求管着每一条路）。
    runProgress(bar, bar, qreal(bar->value()), qreal(value), progressMs(), stepMs(),
                // 收尾再钉一次：`qRound()` 那一串中间值可能停在 value-1 上。
                [b = QPointer<QProgressBar>(bar), value]() {
                    if (b != nullptr) {
                        b->setValue(value);
                    }
                },
                [b = QPointer<QProgressBar>(bar)](qreal current) {
                    if (b != nullptr) {
                        b->setValue(qRound(current));
                    }
                });
}

}  // namespace Anim
