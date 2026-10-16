/**
 * @file Animations.cpp
 * @brief `Animations.hpp` 那一套的实现。规矩（不许有元素超出 / 几何一个像素都不动）写在头文件里。
 *
 * ★★ 2026-10-11 下午：**换页那一发从 `QGraphicsEffect` 换成了"自己盖一块幕布"**
 *    （`PageCurtain`）。理由整段写在那个类上 —— 一句话：挂在控件上的效果**不会把它的
 *    子控件跟着一起重画**，于是"抬"只抬得动控件自己画的那点东西。按下 / 滚动条 / 覆盖层
 *    那三发**仍然是效果**（它们要么没有子控件，要么是"刚建出来就淡"的新控件，实测没问题）。
 */

#include "Animations.hpp"

#include <QAbstractAnimation>
#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QColor>
#include <QEasingCurve>
#include <QElapsedTimer>   // 步进器里那一位"上一拍是第几毫秒推的"（见 `runProgress()`）
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

#if defined(Q_OS_WIN)
// NOTE: 放在所有 Qt 头之后 —— windows.h 定义的东西太多（min/max 宏、`interface`……），
//       先于 Qt 包含容易撞名。写法和 `src/launcher/LauncherManager.cpp` 里那一套一致。
//       用途只有一个：下面的 `refreshPlatformReducedMotion()`（SystemParametersInfo）。
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

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
/// 「按帧走」那一发底下那个步进定时器的名字。认它就是"我们自己那个"（一个控件上可能
/// 前后叠着好几发，重启一发时要能把上一发那个停掉）。
constexpr const char kStepperName[] = "wtfmlAnimStepper";
/// 进度条上那一位"上一趟见到的 `maximum()`"（见 `Anim::smoothProgress()`）。
/// 记在**进度条自己身上**：这一层没有实例、也不该为进度条另立一张表（那张表还得管生灭）。
constexpr const char kProgressMaxMark[] = "wtfmlAnimProgressMax";

/// 动效总开关。默认开。
bool g_enabled = true;

/// 系统那一档「减少动态效果」。**默认"没关"**（= 界面默认有动效）。
/// 只有 `refreshPlatformReducedMotion()` 去问系统，`setPlatformReducedMotion()` 直接摆。
bool g_platformReducedMotion = false;

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
 * 调色板）**一个字节都不动** —— 用户点名的"不许有元素超出"于是变成结构上做不到，
 * 而不是靠分寸拿捏。
 *
 * NOTE: 2026-10-11 下午之后，这一层**只剩"淡"和"压暗"**（换页那一位挪去了 `PageCurtain`）：
 *       曾经这里还有"抬"（`setShift()` / `setContentRect()`）与"定住的那一张"，
 *       那两样都是给换页用的，跟着一起搬走了。
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

    /// ★ 这一条就是"不许有元素超出"的那一句：效果的有效范围**就是源矩形自己**
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
        // ★★ 这个画笔的坐标空间是**逻辑像素**（原点在控件父坐标系里，`offset` 就是控件左上角
        //    在那一格里的逻辑位置）。实地标定（2026-10-11）：`ui_shot` 里那颗按下按钮外面
        //    正好留了 16 px 边距，这里量到的 `offset` **就是 `QPoint(16,16)`**（不是 24 =
        //    16×1.5），而 `source` 那张图是**设备像素**（`dpr` 1.5，180×36 的按钮照出 270×54）。
        //
        //    ★ 这一条以前是含糊的（那一格一直混着用），后果**看得见**：真帧里内容被推到
        //      1.5 倍的位置上。所以这里把两把尺子分清楚：**画笔这一层一律逻辑像素，
        //      `source` 的像素矩形一律设备像素**。
        const qreal dpr = source.devicePixelRatio() > 0.0 ? source.devicePixelRatio() : 1.0;
        const QRectF widgetRect(offset, QSizeF(source.size()) / dpr);
        // ★ 再上一道保险：不管上面那些数怎么算错，都画不到源矩形之外去。
        painter->setClipRect(widgetRect, Qt::IntersectClip);
        // NOTE: 这里原来还有"底下垫一层底色"（`setBackdrop()`）：那是给换页那一发准备的，
        //       它跟着换页一起搬去了 `PageCurtain`（那一层仍然照旧垫 —— 起手那几帧是半透明
        //       的，不垫的话不透明子控件视口那一块屏幕上留的是上一帧的像素）。剩下这三发
        //       （按下 / 滚动条 / 覆盖层）都是**盖在已经画好的界面**上的，不垫。
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
 * ★★ 换页那一发用的那块**幕布**（2026-10-11 下午加的）。用户原话：
 *    「这个动画，在有框的时候框会瞬移，能不能考虑一下用其它组件之类的解决」。
 *
 * 【为什么不接着用 `QGraphicsEffect` —— 在真屏上量出来的】
 *
 * 挂在控件上的效果**只重画控件自己那一层**。它身上的**子控件**（列表 / 树的视口这类
 * 不透明控件）那一块**停在起手那一帧**：2026-10-11 在真机上逐帧量过（`build/tmp-run/
 * wtfml-bandscan.ps1` 扫"一行字在第几行"），换页那一发里：
 *
 *   * 页面自己画的那些文字（「游戏文件夹」「实例列表」）**每一拍都在动**；
 *   * **子控件里的那些行**整整一发一动不动地停在 +27 设备像素（= `pageRiseDistance()`
 *     当时的 18 逻辑像素 × dpr 1.5）、浓淡停在当时的起手值 0.65（黑字压在白底上 = 亮度 90，
 *     帧帧都是 90）—— 也就是说那一块屏幕显示的是**这一发第一帧的合成结果**，
 *     跑完之后效果一摘才"啪"地对齐。用户看到的就是「瞬移」。
 *
 *   换 `repaint()`（同步整块重画）、把抬的量按整块走（不分"框 / 内容"）、只淡不抬，
 *   三种都试过 —— 子控件那一块**照样定住**（只淡不抬那一版里它站的位置是对的、
 *   浓淡仍然是死的：判据 `f01` 到 `f17` 全是 `min 90`）。这一层里的病因不在我们的
 *   `draw()`（那里画的每一帧都真的上屏了），在 Qt 的效果管线本身。
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
 * 起手照一次（懒照：第一帧真要画的时候才照），之后每一帧都拿那一张 —— 理由和以前
 * 一样（见 `pageAppear()`）：换页那一刻常是"内容还在往里灌"的时候，现照现淡的话
 * 这一发淡的每一帧都不是同一张画面。尺寸变了才重照（第一帧可能还没摆完版）。
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
        m_snapshot = QPixmap();   // 这一发重新照一张
        m_snapshotSize = QSize();
        if (target != nullptr && parentWidget() != nullptr) {
            setGeometry(QRect(target->mapTo(parentWidget(), QPoint(0, 0)), target->size()));
        }
        show();
        raise();
    }

    /// 这一帧长什么样。`shift` = 内容往下挪多少（逻辑像素），`opacity` = 整块多不透。
    void setFrame(qreal shift, qreal opacity) {
        m_shift = shift;
        m_opacity = opacity;
        update();   // ★ 普通控件，这一句就是"这一帧真的会重画"的唯一出处
    }

protected:
    void paintEvent(QPaintEvent *) override {
        if (m_target == nullptr || m_grabbing) {
            return;
        }
        // NOTE: 幕布和它照的那一块都是 `parentWidget()` 的孩子（幕布是**兄弟**，不是祖先）
        //       —— 所以这里量位置问的是 `parentWidget()`，不是 `this`（问 `this` 的话
        //       Qt 会报 `QWidget::mapTo(): parent must be in parent hierarchy` 然后返回一个
        //       没人认的坐标，症状是整块幕布一个像素都不画）。
        QWidget *host = parentWidget();
        if (host == nullptr) {
            return;
        }
        const QRect inHost(m_target->mapTo(host, QPoint(0, 0)), m_target->size());
        const QRect inMe = rect();
        if (inHost.isEmpty() || inMe.isEmpty()) {
            return;
        }
        // ★ 「淡的是一张定住的图」：第一帧照一次就定住（尺寸变了才重照）。
        if (m_snapshot.isNull() || m_snapshotSize != m_target->size()) {
            // NOTE: 不能直接 `m_target->grab()`：页面本身经常是透明光板，单独抓它时 Qt 会
            //       用 `QPalette::Window` 补底（Fusion 下就是灰的），而屏幕上真正透出来的是
            //       父层那张白卡片。动画期间于是会先灰、收掉幕布后才突然变白。
            //       从 host 的对应区域抓，拿到的才是用户此刻真正看见的合成结果。
            //       幕布自己可能就在 host 里面，所以照相时让自己一个像素都不画，避免照镜子。
            //
            // ★★ 2026-10-12（ui_check ⑬ 那条「透明页面换页时背景从第一帧起就是白的」逮住的）：
            //   上面那条「从 host 抓」**漏了 host 就是 m_target 的那一路** —— `liftTarget()`
            //   什么都没抬的时候（页面自己没有滚动区、也不是选项卡，`lift == widget`）幕布的
            //   父层**就是**要照的那一块，`host->grab()` 于是又回到"抓一张透明光板"：
            //   Qt 拿 `QPalette::Window` 补底，实测灰 `#808080` 配 `0.72` 的起手浓淡正好是
            //   屏幕上那块先灰一下再变白。这时往**顶层窗口**的那一片区域抓，合成本身才是对的
            //   （幕布是目标的**孩子**，它自己那一下照样由 `m_grabbing` 挡着不画）。
            //   NOTE: 只改这一路 —— host 不是 target 那一路（选项卡 / 滚动区，也是界面上
            //         真正跑得最多的那一路）一个字节都没动。
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
            m_snapshot = grabSource->grab(inGrab);
            m_snapshotSize = m_target->size();
            m_grabbing = false;
        }
        QPainter painter(this);
        // `setGeometry()` 已经把幕布搬到了 inHost；QPainter 用的是幕布**自己的**坐标，
        // 这里必须从 (0, 0) 画。再用 inHost 会把目标相对父层的偏移重复加一次，幕布左上会
        // 留出一块透明区域，动画时正好透出后面的灰色窗口。
        // 「不许有元素超出」：画之前先把范围收到这一格（挪下去的那一条也不会跑出去）。
        painter.setClipRect(inMe, Qt::IntersectClip);
        // ★ 底下先铺满底色：起手那几帧是半透明的，而 `target` 底下可能有"谁都不画"的
        //   不透明子控件视口（那一块屏幕上留着的是上一帧的像素）。铺一层就总是满的。
        if (m_backdrop.isValid() && m_backdrop.alpha() > 0) {
            painter.setOpacity(1.0);
            painter.fillRect(inMe, m_backdrop);
        }
        painter.setOpacity(qBound(0.0, m_opacity, 1.0));
        // 「抬」这一位按**逻辑像素**说；落到屏幕上再吸到**整设备像素**上
        // （150% 缩放下 1 逻辑 px = 1.5 设备 px —— 不吸的话内容每一拍都停在半像素上，
        //  文字会发虚）。
        const qreal dpr = m_snapshot.devicePixelRatio() > 0.0 ? m_snapshot.devicePixelRatio() : 1.0;
        const qreal shift = m_shift > 0.0 ? qRound(m_shift * dpr) / dpr : 0.0;
        painter.drawPixmap(QPointF(inMe.topLeft()) + QPointF(0.0, shift), m_snapshot);
    }

private:
    QPointer<QWidget> m_target;   ///< 要抬的那一块（照它、也按它的尺寸定位）。
    QPixmap m_snapshot;           ///< 起手照的那一张（定住）。
    QSize m_snapshotSize;
    QColor m_backdrop;
    qreal m_shift = 0.0;
    qreal m_opacity = 1.0;
    bool m_grabbing = false;      ///< 正在照那一张（照的时候自己别画）。
};

/// 「按帧走」那一发的记账本（怎么走见 `runProgress()` 里那一大段）。定时器那个 lambda
/// 攥着它（`QSharedPointer`），效果被摘掉时定时器跟着走、这本账也跟着走。
struct StepRun {
    QPointer<QVariantAnimation> animation;
    int index = 0;   ///< 已经推了几档。
    int steps = 1;   ///< 一共几档。
    /// 跑完要干什么（摘效果 / 把幕布收起来）。空 = 什么都不做（比如"按下"那一发，手指还按着）。
    std::function<void()> finish;
    /// 这一发起跑那一刻 + 上一拍是第几毫秒推的（**只为了丢掉积压的那一拍**，见 `runProgress()`）。
    QElapsedTimer clock;
    qint64 lastStepMs = -1;
};

/**
 * 跑一发进度动画，每一帧交给 `apply()` 去摆那个效果 / 那块幕布上的几个数。
 *
 * `apply` 收到的是**已经映射好的值**（`from` → `to`，走 `OutSine`），不是 0→1。
 *
 * `owner`：这一发动画和步进器挂在谁身上（效果的拥有者那几发就是效果自己；换页那几发是
 *          那块幕布）。同一个 `owner` 上又进来一发时，上一发会被停掉。
 * `repaintTarget`：每一拍推完之后要 `update()` 的那一块（效果那几发就是挂着效果的那个
 *          控件；换页那几发是幕布自己）。
 * `finish`：跑完做的那一下（可能为空 = 不收尾）。
 *
 * `stepMs`：
 *   * `> 0` —— **按帧走**：定时器每一拍推一档（"为什么"整段写在这个函数下半截）。
 *     换页 / 覆盖层 / 滚动条这三发走这条。
 *   * `0`   —— 按 Qt 自己的墙上时钟走。**按下 / 松手那两发走这条**：手指按着的时候那一档
 *     要立刻见、要一直挂着（按住不放可以是任意长），跟"一段固定时长的转场"不是一回事。
 *
 * ★ 2026-10-11：这里原来还有一个 `delayMs` 参数（"起手先干等一段"），只有换页那一发用。
 *   那一段跟着 `pageDelayMs()` 一起拿掉了，见 `Animations.hpp` 里 `appearMs()` 上面那段。
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
    // ★ 2026-10-11：`OutCubic` → **`OutSine`**。同样是"起手快、收尾慢"，但 `OutCubic`
    //   把动感堆得太靠前：换页一共才 12 拍，它**第一拍就走掉 25%、头三拍走掉 74%**，
    //   后面每拍不到 1 px —— 看着就是"跳一下，然后一条不动的尾巴"。`OutSine` 每拍最多
    //   摊 13%，十几拍下来是一条匀匀的滑行（实测真帧见下面 ★★ 那一段）。
    animation->setEasingCurve(QEasingCurve::OutSine);
    // ★ 每一拍推完之后让那一块自己重画一遍。效果那几发这儿只叫得动"控件自己"那一层
    //   （子控件那一块要等它自己脏了才画 —— 这正是换页那一发不能用效果的原因，见
    //   `PageCurtain` 那段）；幕布是一块普通控件，这一句就是"整块都新"。
    QObject::connect(animation, &QVariantAnimation::valueChanged, owner,
                     [apply, w = QPointer<QWidget>(repaintTarget)](const QVariant &value) {
                         apply(value.toReal());
                         if (w != nullptr) {
                             w->update();
                         }
                     });

    // NOTE: 收尾那一下**不能**在 `finished` 里直接做：那一刻我们是站在动画自己的信号里，
    //       而动画是 `owner` 的孩子 —— 删父亲等于把这个正在发信号的动画也删了。
    //       挪到下一轮做。
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
    //   当时这一发一共 `appearMs()`（200 ms），等于五分之一是跳过去的。后面的每一拍都稳稳
    //   14~16 ms（帧率其实好好的）。用户说的"卡"就是这个。
    //
    // 【修法】**不按墙上时钟算，每一拍推一档**。定时器摆在 `stepMs` 上，每响一次
    //   `index += 1`，再把动画时间戳摆到 `index / steps` 那一档去。迟到的第一拍于是只推
    //   一档，而不是一口气跳到半路 —— **"第一拍什么时候来"不再影响这一发走到哪儿**，
    //   最多是起手多定住一两帧（那一段的观感由 `pageStartOpacity()` 和 `pageRiseDistance()`
    //   管着，那两个数就是照这份真帧定的）。
    //
    //   ★ 同一天下午：这一条修完用户还是说"像是从下面瞬移上去的，没有过渡"。再量真帧，
    //     还剩两个毛病：**两拍会挤在一起**、**后面几拍几乎不动**。于是同一天又补了三刀：
    //     曲线换 `OutSine`、时长当时由 130 → 200（拍到 13 拍）、步进器在"离上一拍太近"的那一拍上
    //     原地丢掉（单发链试过，慢一倍，见下面那一段）。
    //
    // 【为什么动画对象留着、却不 start】`setCurrentTime()` 在"停着"的状态下照样算值、
    //   照样发 `valueChanged`、`currentValue()` 也跟着走 —— 2026-10-11 用一次性探针验过
    //   （`build/tmp-wtfml-animprobe/ct.cpp`，跑完即删）：停着和"start 完再 pause"两种状态
    //   下，同样四个采样点量出来的值一模一样。留着它，`ui_check` ⑬ 那几条判据
    //   （`findChildren<QVariantAnimation *>()` / `duration()` / `currentValue()`）一个字都
    //   不用改。
    //
    //   NOTE: 走这条路时 `finished` **不会**发（从来没 start 过），所以收尾那一下得由
    //         步进器自己叫。
    auto state = QSharedPointer<StepRun>::create();
    state->animation = animation;
    state->steps = qMax(1, qRound(qreal(animation->duration()) / stepMs));
    state->finish = finish;

    auto *stepper = new QTimer(owner);
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
    QObject::connect(stepper, &QTimer::timeout, owner, [state, stepper, finishLater, stepMs]() {
        const qint64 nowMs = state->clock.elapsed();
        if (state->lastStepMs >= 0 && nowMs - state->lastStepMs < stepMs * 3 / 4) {
            return;
        }
        state->lastStepMs = nowMs;
        ++state->index;
        QVariantAnimation *animation = state->animation.data();
        if (animation == nullptr) {   // 拥有者已经被销毁了（定时器也快跟着走了）
            stepper->stop();
            return;
        }
        const bool last = state->index >= state->steps;
        if (last) {
            // 最后一拍**直接落到终点**：`steps * stepMs` 和时长不一定正好相等
            // （例如 180 / 16 = 11 拍），余数不能让这一发停在 0.999 上。
            animation->setCurrentTime(animation->duration());
        } else {
            animation->setCurrentTime(
                qRound(qreal(state->index) * animation->duration() / state->steps));
        }
        if (!last) {
            return;
        }
        stepper->stop();
        finishLater();
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

/**
 * 「这一块抬起来的时候，**抬的到底是哪一块**」—— `pageAppear()` 用它（整段理由见那儿）。
 *
 * 规矩一句话：**自己有那圈外框的控件，抬它里面那一块**。那圈外框是屏幕上一个**不动的**
 * 参照物（换页前后它都在原地），让它跟着往下挪，看着就是"框瞬移"；里面的内容是新上来的，
 * 往下挪再滑上去才像一次转场。
 *
 *   * `QTabWidget`（顶上那四颗导航翻出来的内容区）⇒ `currentWidget()`：用户看到的那圈
 *     卡片和外框**是选项卡自己画的**，页面是它里面那一块（`ThemeBackground` 一个像素都不画）。
 *   * `QAbstractScrollArea`（列表 / 树 / 滚动区 —— 它们自带那圈 `#b9b9b9` 的框）⇒ `viewport()`：
 *     框、表头、滚动条都长在视口**外面**，抬视口就正好是"内容抬、框不动"。
 *   * 别的（下载页/设置页里那些光板一页、实例设置那一列页表）⇒ 就是它自己：那些块没有单独
 *     画的框，整块抬和以前一模一样。
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
    qreal m_progress = 0;
};

}  // namespace

namespace Anim {

bool enabled() {
    // 两把闸串起来：用户那把（设置页那个勾）**和**系统那一档。任一条说"不要"，就一个
    // 效果都不装（下面那些函数全是"不 enabled 就原地返回"）。
    return g_enabled && !g_platformReducedMotion;
}

bool userEnabled() {
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

bool platformReducedMotion() {
    return g_platformReducedMotion;
}

void setPlatformReducedMotion(bool on) {
    g_platformReducedMotion = on;
}

void refreshPlatformReducedMotion() {
#if defined(Q_OS_WIN)
    // Windows：设置 → 辅助功能 → 视觉效果 → 「动画效果」。
    // `SPI_GETCLIENTAREAANIMATION` 写进一个 `BOOL`；调不通（老系统/被挡）就当"没关"。
    BOOL animationsOn = TRUE;
    if (::SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animationsOn, 0)) {
        g_platformReducedMotion = (animationsOn == FALSE);
    } else {
        g_platformReducedMotion = false;
    }
#else
    // 别的平台**故意不猜**：这一层要的是"系统说不要动"这个确定的事实，猜错了就是替用户
    // 做了决定。Linux/macOS 上 Qt 没有一条跨平台的等价 API（要碰各自的桌面环境设置）。
    g_platformReducedMotion = false;
#endif
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
    // ★ 按帧走（`stepMs()`）：它跟换页是**同一时刻**起来的（滚动条是那一页进页之后才露的
    //   头），主线程忙的是同一件事 —— 按墙上时钟走的话它一样会"定住再跳"。
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
    // ★★ 「抬的是**内容**、框一个像素都不动」（2026-10-11）。用户原话：「这个动画，在有框的
    //    时候框会瞬移，能不能考虑一下用其它组件之类的解决」。
    //
    //    真因：这一发原来是**整块**（连它自己那圈外框）一起往下挪着画的，而其中有些控件
    //    （内容区那个 `QTabWidget` 的卡片、实例列表那棵树）**是换页前后都在的同一块** ——
    //    于是那圈外框起手先跳下去十几像素、再滑回原处，看着就是"框瞬移"，而内容因为是新
    //    上来的、没有参照物，反倒像正常的滑入。修法不是"抬得少一点"，是**让框压根不参与**：
    //    盖上去的那块幕布**就是内容自己的矩形**（见 `liftTarget()`），外框长在幕布外面，
    //    这一发从头到尾碰都没碰过它。
    //
    //    ★★ 同一天下午又换了一次**怎么画**：原来是把 `QGraphicsEffect` 挂在整块上、自己
    //       分两趟贴图（外框原位一趟、内容抬着趟），真机上量出来**子控件那一块定在起手
    //       那一帧**（完整来龙去脉写在 `PageCurtain` 那个类上）。现在改成**一块普通控件
    //       盖在上面**，`paintEvent` 自己画 —— 普通控件走 Qt 平时的绘制路径，每一帧整块
    //       都是新的，那一类"定住"从根上没有了。
    QWidget *lift = liftTarget(widget);
    if (lift == nullptr) {
        lift = widget;
    }
    // NOTE: `PageCurtain` 没有 `Q_OBJECT`（这一层不需要信号槽、也不需要 moc），所以这里
    //       够不到它的类型：按**名字**找回来再 `static_cast` —— 那个名字只有这一处会写。
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
    //       也躲开"上一发还在跑、下一发就来了"那一下的悬空指针）。
    runProgress(curtain, curtain, 0.0, 1.0, appearMs(), stepMs(),
                [c = QPointer<QWidget>(curtain)]() {
                    if (c != nullptr) {
                        c->hide();
                    }
                },
                [curtain, rise, from](qreal t) {
                    curtain->setFrame(rise * (1.0 - t), from + (1.0 - from) * t);
                });
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
    change();
    if (!guard) return;
    if (surface->layout()) surface->layout()->activate();
    for (auto *child : surface->findChildren<QWidget *>())
        if (child->isVisible() && child->layout()) child->layout()->activate();
    if (!animate || !enabled() || !surface->isVisible() ||
        !surface->rect().contains(area)) return;
    auto *curtain = new LayoutCurtain(surface, area, before, capture(), expanding);
    curtain->show();
    curtain->raise();
    runProgress(curtain, curtain, 0, 1, panelMs(), stepMs(),
                [curtain]() { curtain->hide(); curtain->deleteLater(); },
                [curtain](qreal progress) { curtain->frame(progress); });
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
    // ★ 按帧走（`stepMs()`）：它开页那一刻主线程也在忙（建页、第一遍画）。
    //   NOTE: 它起手仍是**全透明**（不是 `pageStartOpacity()`）—— 它是"从无到有"，
    //         底下那层的观感问题跟换页不一样（见 `pageStartOpacity()` 那一节）。
    runProgress(effect, widget, 0.0, 1.0, overlayMs(), stepMs(),
                [w = QPointer<QWidget>(widget)]() { dropEffect(w); },
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

/// 往下弹的那一档（主页右上那张账号卡片那块面板）：从落点上面 `panelSlideDistance()`
/// 滑下来。**和 `slideUp()` 是同一条动效的两头** —— 时长、缓动、收尾兜底全都一样，
/// 只有起手位在另一边（2026-10-16 加的，见 MainWindowUI 里 BottomBarPanel::popUp()）。
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
    //       16 px 走完就该能点上面的东西。
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
    // ★ 起手全透明，一路淡到 1 —— 和滚动条那一发同一条路（`stepMs()`：这一趟往往正赶上
    //   主线程在忙，按墙上时钟算进度的话第一拍会直接跳到半路）。
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
    //   · 总量变了（换了一批 / 从不确定态换回来 / 复位成 0–100）；
    //   · 真进度倒退（重试、跳过校验那种情报不能被动画盖住）；
    //   · 已经等于目标了（没什么好追的）；
    //   · 这一格现在停在 0（"还回来"那一路：启动借走时被钳回 0，记下的总量却没变过
    //     ⇒ 不拦的话用户看到的是下载在重头爬，见头文件那一段）；
    //   以及动效关着的时候。
    if (!enabled() || maximum != previousMaximum || value <= bar->value() || bar->value() <= 0) {
        bar->setValue(value);
        return;
    }
    // ★ 追值**不走效果器**：进度条要动的是它自己的那个数，不是"贴一张图"。
    //   几何照旧一个像素都不动（这一层那条硬要求管着每一条路）。
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
