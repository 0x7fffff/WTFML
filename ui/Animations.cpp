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
#include <QElapsedTimer>   // 步进器里那一位"上一拍是第几毫秒推的"（见 `runProgress()`）
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
#include <QSharedPointer>
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
/// 「按帧走」那一发底下那个步进定时器的名字。认它就是"我们自己那个"（一个控件上可能
/// 前后叠着好几发，重启一发时要能把上一发那个停掉）。
constexpr const char kStepperName[] = "wtfmlAnimStepper";

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
        // ★★ 底下那一层（2026-09-29 加的，当时是给"干等那 70 ms"垫的；那一段 2026-10-11
        //    拿掉了，这一层留着管**起手那一帧**）：**整块铺成窗口底色、而且不跟着内容一起淡**。
        //
        //    为什么不铺就出事：淡的那个控件（内容区）**底下那些不透明的子控件**
        //    （列表/树的视口 —— `QAbstractScrollArea` 的视口自带底色，Qt 认它"自己会画满"）
        //    平时由它们自己画，所以**上面几层的底色都会跳过它们那一块**。可一旦这个控件挂了
        //    效果，子控件就改成画进效果那一张离屏图里了 —— 于是那块地方**屏幕上没有任何人去
        //    画**：`grab()` 出来的是一块黑（那张 pixmap 的原始字节），屏幕上则是**上一帧留下的
        //    像素**（也就是"上一页的列表还挂在那儿"）。内容越是半透明（起手那几帧最虚）
        //    越看得清楚 —— 所以换页那一发从头到尾都垫着它。
        //    铺一层底色把这整块盖住，那一整块就总是"我们自己画满"的 —— 跟旁边一圈完全接得上。
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

/// 「按帧走」那一发的记账本（怎么走见 `runProgress()` 里那一大段）。定时器那个 lambda
/// 攥着它（`QSharedPointer`），效果被摘掉时定时器跟着走、这本账也跟着走。
struct StepRun {
    QPointer<QVariantAnimation> animation;
    int index = 0;   ///< 已经推了几档。
    int steps = 1;   ///< 一共几档。
    bool removeWhenDone = false;
    /// 这一发起跑那一刻 + 上一拍是第几毫秒推的（**只为了丢掉积压的那一拍**，见 `runProgress()`）。
    QElapsedTimer clock;
    qint64 lastStepMs = -1;
};

/**
 * 跑一发进度动画，每一帧交给 `apply()` 去摆效果上的那几个数。
 *
 * `apply` 收到的是**已经映射好的值**（`from` → `to`，走 `OutSine`），不是 0→1。
 *
 * `stepMs`：
 *   * `> 0` —— **按帧走**：定时器每一拍推一档（"为什么"整段写在这个函数下半截，见 ★★ 那一段）。
 *     换页 / 覆盖层 / 滚动条这三发走这条。
 *   * `0`   —— 按 Qt 自己的墙上时钟走。**按下 / 松手那两发走这条**：手指按着的时候那一档
 *     要立刻见、要一直挂着（按住不放可以是任意长），跟"一段固定时长的转场"不是一回事。
 *
 * `removeWhenDone`：跑完把效果摘掉（淡入、松手这两条）。按下那一发**不摘** —— 手指还按着，
 * 那个"暗着"的状态要一直在。
 *
 * ★ 2026-10-11：这里原来还有一个 `delayMs` 参数（"起手先干等一段"），只有换页那一发用。
 *   那一段跟着 `pageDelayMs()` 一起拿掉了，见 `Animations.hpp` 里 `appearMs()` 上面那段。
 *   同一天又发现"拿掉干等"之后**剩下来的那一发自己在跳**（下详），于是加了 `stepMs`。
 */
void runProgress(QWidget *widget, AnimEffect *effect, qreal from, qreal to, int ms, int stepMs,
                 bool removeWhenDone, const std::function<void(qreal)> &apply) {
    // 同一个控件上又进来一发（连点两下、连切两页）：把上一发停掉。不停的话两个动画会
    // 交叉着写同一组数，看着像"抖"。
    const QList<QVariantAnimation *> running = effect->findChildren<QVariantAnimation *>();
    for (QVariantAnimation *old : running) {
        old->stop();
        old->deleteLater();
    }
    const QList<QTimer *> stepping = effect->findChildren<QTimer *>(QLatin1String(kStepperName));
    for (QTimer *old : stepping) {
        old->stop();
        old->deleteLater();
    }

    auto *animation = new QVariantAnimation(effect);
    animation->setDuration(qMax(1, ms));
    animation->setStartValue(from);
    animation->setEndValue(to);
    // ★ 2026-10-11：`OutCubic` → **`OutSine`**。同样是"起手快、收尾慢"，但 `OutCubic`
    //   把动感堆得太靠前：换页一共才 8~13 拍，它**第一拍就走掉 25%、头三拍走掉 74%**，
    //   后面每拍不到 1 px —— 看着就是"跳一下，然后一条不动的尾巴"。`OutSine` 每拍最多
    //   摊 13%，十几拍下来是一条匀匀的滑行（实测真帧见下面 ★★ 那一段）。
    animation->setEasingCurve(QEasingCurve::OutSine);
    QObject::connect(animation, &QVariantAnimation::valueChanged, effect,
                     [apply](const QVariant &value) { apply(value.toReal()); });

    // NOTE: 摘效果这件事**不能**在 finished 里直接做：那一刻我们是站在动画自己的信号里，
    //       而效果是动画的父亲 —— 删父亲等于把这个正在发信号的动画也删了。挪到下一轮做。
    const auto dropLater = [w = QPointer<QWidget>(widget)]() {
        QTimer::singleShot(0, w, [w]() { dropEffect(w); });
    };

    if (stepMs <= 0) {
        if (removeWhenDone) {
            QObject::connect(animation, &QVariantAnimation::finished, widget, dropLater);
        }
        animation->start(QAbstractAnimation::DeleteWhenStopped);
        return;
    }

    // ---------------------------------------------------------------------
    // ★★ 按帧走（2026-10-11 修用户报的「动画有严重的卡顿感」）
    // ---------------------------------------------------------------------
    //
    // 【症状】换页那一发里，前面一大截是**定住的**，然后"啪"地跳掉三分之一。
    //
    // 【真因】**不是掉帧**，是这一发自己在跳。`QVariantAnimation` 的进度是按墙上时钟算的
    //   （`当前时间 = 现在 - 起跑那一刻`），而它"什么时候被叫醒"取决于 `QUnifiedTimer`：
    //   换页那一刻主线程正忙着（摆版、新那一页第一遍画），第一拍**来得很晚**。2026-10-11
    //   在 `ui_shot` 里临时插了一段追踪量出来的原话（`画` = 窗口画了一帧、括号里是距上一
    //   条过了多少毫秒、数字 = 那一刻动画走到哪儿）：
    //
    //       900x620  : (2)画 (0)(6)画 (0)(1)画 (23)0.46 (0)画 (15)0.66 (1)画 (14)0.80 ...
    //       1600x1000: (0)画 (64)0.43 (1)画 (14)0.65 (1)画 (14)0.78 (1)画 (14)0.89 ...
    //
    //   也就是：切过去之后**先定住 23~64 ms**（一帧都没动），第一拍一到就直接跳到 0.43 ——
    //   这一发一共才 `appearMs()`（130 ms），等于三分之一是跳过去的。后面的每一拍都稳稳
    //   14~16 ms（帧率其实好好的）。用户说的"卡"就是这个。
    //
    // 【修法】**不按墙上时钟算，每一拍推一档**。定时器摆在 `stepMs` 上，每响一次
    //   `index += 1`，再把动画时间戳摆到 `index / steps` 那一档去。迟到的第一拍于是只推
    //   一档，而不是一口气跳到半路 —— **"第一拍什么时候来"不再影响这一发走到哪儿**，
    //   最多是起手多定住一两帧（那一段的观感由 `pageStartOpacity()` 和 `pageRiseDistance()`
    //   管着，那两个数就是照这份真帧定的）。
    //
    //   ★ 同一天下午：这一条修完用户还是说"像是从下面瞬移上去的，没有过渡"。再量真帧
    //     （见下面步进器那一段），还剩两个毛病：**两拍会挤在一起**（`OutCubic` 让第一拍
    //     就走 25%，一挤就是一次瞬移）、**后面几拍几乎不动**（每拍不到 1 px）。于是同一天
    //     又补了三刀：曲线换 `OutSine`、时长 130 → 200（拍到 13 拍）、步进器在"离上一拍
    //     太近"的那一拍上原地丢掉（单发链试过，慢一倍，见步进器那一段）。
    //
    // 【为什么动画对象留着、却不 start】`setCurrentTime()` 在"停着"的状态下照样算值、
    //   照样发 `valueChanged`、`currentValue()` 也跟着走 —— 2026-10-11 用一次性探针验过
    //   （`build/tmp-wtfml-animprobe/ct.cpp`，跑完即删）：停着和"start 完再 pause"两种状态
    //   下，同样四个采样点（16/32/48/130 ms）量出来的值一模一样
    //   （0.3256 / 0.5716 / 0.7490 / 1）。留着它，`ui_check` ⑬ 那三条判据
    //   （`findChildren<QVariantAnimation *>()` / `duration()` / `currentValue()`）一个字
    //   都不用改。
    //
    //   NOTE: 走这条路时 `finished` **不会**发（从来没 start 过），所以"跑完摘效果"得由
    //         步进器自己收尾。
    auto state = QSharedPointer<StepRun>::create();
    state->animation = animation;
    state->steps = qMax(1, qRound(qreal(animation->duration()) / stepMs));
    state->removeWhenDone = removeWhenDone;

    auto *stepper = new QTimer(effect);
    stepper->setObjectName(QLatin1String(kStepperName));
    stepper->setTimerType(Qt::PreciseTimer);
    stepper->setInterval(stepMs);
    // ★★ 2026-10-11 下午：**周期定时器 + 丢掉"积压的那一拍"**。
    //
    //   上半天那版是"周期 16 ms"，我按"Qt 每颗定时器最多只补发**一个**迟到事件"来推理
    //   （结论来自一个一次性探针）。同一天下午在 `ui_shot` 里量真帧，直接推翻了它 ——
    //   主线程被占 40 ms（建页 + 第一帧 + 截图）之后，**两拍是连着来的**：
    //
    //       t=5   画（现场照了一张 4ms）  0.800 / +10.0
    //       t=10  画（定住的那张）        0.800 / +10.0
    //       t=16  画（定住的那张）        0.800 / +10.0
    //       ----  空档 30 ms  ----
    //       t=46  推 1/8 → 画            0.865 / +6.7
    //       t=48  推 2/8 → 画            0.917 / +4.2     ← 两拍只差 2 ms
    //       t=64  推 3/8 → 画            0.952 / +2.4
    //       …
    //
    //   那 2 ms 里一口气推掉的是 5.8 px 和 12% 浓淡 —— **用户说的"瞬移"就是这一下**。
    //
    //   【试过一版"单发链"（推完一档再 start 下一发），不好】两拍之间确实至少隔一个
    //   `stepMs()` 了，可节拍也跟着变成"16 ms + 这一拍之后主线程还要干的活儿"——
    //   同样这一发，实测总时长从 208 ms 变成 **416 ms**（每拍 30 ms 上下），太拖。
    //   周期定时器那边节拍是稳的 16 ms 网格，**只有补发那一下会挤**。
    //
    //   所以最后是：**周期 16 ms 不动**，只在"离上一拍太近"的那一拍上原地返回。
    //   判据取 `stepMs() * 3 / 4`（12 ms）：正常那一拍是 16 ms 的网格（抖动 0~1 ms），
    //   一定过得去；补发那一拍是 2~8 ms，一定被丢掉。丢掉的只是"多推一档"，
    //   定时器还在跑 ⇒ 下一拍（16 ms 后）照常接上，这一发最多晚一个 `stepMs()` 结束。
    QObject::connect(stepper, &QTimer::timeout, effect, [state, stepper, dropLater, stepMs]() {
        const qint64 nowMs = state->clock.elapsed();
        if (state->lastStepMs >= 0 && nowMs - state->lastStepMs < stepMs * 3 / 4) {
            return;
        }
        state->lastStepMs = nowMs;
        ++state->index;
        QVariantAnimation *animation = state->animation.data();
        if (animation == nullptr) {   // 效果已经被摘了（定时器也快跟着走了）
            stepper->stop();
            return;
        }
        const bool last = state->index >= state->steps;
        if (last) {
            // 最后一拍**直接落到终点**：`steps * stepMs` 和时长不一定正好相等
            // （200 / 16 = 13 拍 = 208 ms），差那 8 ms 不能让这一发停在 0.999 上。
            animation->setCurrentTime(animation->duration());
        } else {
            animation->setCurrentTime(
                qRound(qreal(state->index) * animation->duration() / state->steps));
        }
        if (!last) {
            return;
        }
        stepper->stop();
        if (state->removeWhenDone) {
            dropLater();
        }
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
    runProgress(button, effect, effect->scrim(), target, Anim::pressInMs(), /*stepMs=*/0,
                /*removeWhenDone=*/false, [effect](qreal value) { effect->setScrim(value); });
}

/// 松手：弹回原样，然后把这层效果摘掉。
void pressUp(QAbstractButton *button) {
    AnimEffect *effect = ourEffect(button);
    if (effect == nullptr) {
        return;
    }
    runProgress(button, effect, effect->scrim(), 0.0, Anim::pressOutMs(), /*stepMs=*/0,
                /*removeWhenDone=*/true, [effect](qreal value) { effect->setScrim(value); });
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
    // ★ 按帧走（`stepMs()`）：它跟换页是**同一时刻**起来的（滚动条是那一页进页之后才露的
    //   头），主线程忙的是同一件事 —— 按墙上时钟走的话它一样会"定住再跳"。
    runProgress(bar, effect, 0.0, 1.0, scrollBarMs(), stepMs(), /*removeWhenDone=*/true,
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
    // 第一帧就摆到"还没进来"的样子：`pageStartOpacity()` 那么透 + 往下 `rise` 那么多
    // （`draw()` 里是整块往下挪着画，所以多出来的那一条在**上面**、被裁掉的是下面那一条）。
    // ★★ 底下垫一层底色（调色板哪一档由调用方说，见两参数那一份的说明）：这一整块地方
    //    底下可能是"谁都不画"的（不透明子控件视口 + 半透明的输出），垫一层它就总是满的。
    //    那一层的来龙去脉写在 `AnimEffect::draw()` 里。
    //    NOTE: 设了页面背景图（`Theme::Part::PageBackground`）时那张图是**页面自己**画的、
    //          也跟着一起淡，这一层垫的还是底色 —— 那一档下起手那一刻会短暂地少一层图。
    effect->setBackdrop(backdrop);
    // ★★ 起手**不是全透明**（0.65，不是 0）—— 这一发切过去之后头一两帧是躲不掉的
    //    （主线程正忙着摆版、画新那一页），理由整段见 `Animations.hpp` 里
    //    `pageStartOpacity()` 上面那段。
    effect->setOpacity(pageStartOpacity());
    effect->setShift(rise);
    // ★★ 这一发淡的是**一张定住的图**（第一帧照一次，后面每一帧都拿它）—— 理由整段写在
    //    `AnimEffect::draw()` 里那一段。一句话：换页那一刻正是"内容还在往里灌"的时候，
    //    现照现淡的话，淡的每一帧都不是同一张画面（用户 2026-09-29：「服务器列表那栏动画
    //    卡卡的」）。定住之后这一发只剩一个动作：blit + 改不透明度。
    effect->beginSnapshot();
    // 「起手那一位」只在下面这个 lambda 里用一次，收在两个常数里（`pageStartOpacity()` /
    // `pageRiseDistance()`），调用点别写数。
    const qreal from = pageStartOpacity();
    runProgress(widget, effect, 0.0, 1.0, appearMs(), stepMs(), /*removeWhenDone=*/true,
                [effect, rise, from](qreal t) {
                    effect->setOpacity(from + (1.0 - from) * t);
                    effect->setShift(rise * (1.0 - t));
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
    effect->setBackdrop(QColor());  // 同上：提示卡那一层是盖在**已经画好的界面**上的，不垫底
    effect->setOpacity(0.0);
    // ★ 按帧走（`stepMs()`）：它开页那一刻主线程也在忙（建页、第一遍画）。
    //   NOTE: 它起手仍是**全透明**（不是 `pageStartOpacity()`）—— 它是"从无到有"，
    //         底下那层的观感问题跟换页不一样（见 `pageStartOpacity()` 那一节）。
    runProgress(widget, effect, 0.0, 1.0, overlayMs(), stepMs(), /*removeWhenDone=*/true,
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
    // NOTE: 这一发**故意留着 `OutCubic`**（"整页转场"那几发 2026-10-11 换成了 `OutSine`）：
    //       它是"一小块面板弹出来"，要的是干脆利落 —— 16 px 走完就该能点上面的东西了。
    //       收尾圆滑在这儿的代价是"最后 1~2 px 还在慢慢挪"。
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
