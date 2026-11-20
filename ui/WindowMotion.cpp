#include "WindowMotion.hpp"

#include "Animations.hpp"
#include "Glass.hpp"
#include "TitleBar.hpp"

#include <QCloseEvent>
#include <QColor>
#include <QDebug>
#include <QElapsedTimer>
#include <QEvent>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QPointer>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>


namespace {
// 时间线（毫秒，从首帧画完算起）。雾 0–520 在 Glass.cpp；这里是内容和品牌标。
constexpr int kRiseMs = 380;
constexpr qreal kRise = 18.0;
constexpr int kGroupDelay[] = {60, 100, 140, 220};  // 问候、角色、当前实例、启动行
constexpr int kWriteDelay = 40;
constexpr int kWriteMs = 520;
constexpr int kGlowDelay = 440;
constexpr int kGlowMs = 180;
constexpr int kCloseMs = 120;  // 之后系统自己的关闭动画还有约 150 ms
constexpr qreal kSink = 8.0;

struct Held {
    QPointer<QWidget> widget;
    int group;
    bool retain;
};

/// 只认一次：底那一层的第一次 Paint（= 首帧）。下一轮事件循环再开动，不占首帧。
class FirstPaint : public QObject {
public:
    FirstPaint(QWidget *watched, std::function<void()> go) : QObject(watched), m_go(std::move(go)) {
        watched->installEventFilter(this);
    }
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() == QEvent::Paint) {
            watched->removeEventFilter(this);
            QTimer::singleShot(0, parent(), m_go);
            deleteLater();
        }
        return false;
    }
private:
    std::function<void()> m_go;
};
}  // namespace

namespace WindowMotion {

void armOpening(QWidget *window) {
    if (window == nullptr || !Anim::enabled()) return;
    auto *backdrop = window->findChild<Glass::Backdrop *>();
    if (backdrop == nullptr) return;
    backdrop->deferHeroUntilFirstPaint();

    // 四组内容：主页是开窗那一页时才动（从文件关联打开到别的页就只散雾、写 W）。
    static const char *const names[][2] = {{"homeHeroTitle", "homeHeroQuote"},
                                           {"skinViewer", nullptr},
                                           {"homeCurrentPicker", nullptr},
                                           {"homeCurrentLaunch", "homeCurrentEdit"}};
    QList<Held> held;
    for (int group = 0; group < 4; ++group) {
        for (const char *name : names[group]) {
            if (name == nullptr) continue;
            QWidget *w = window->findChild<QWidget *>(QLatin1String(name));
            if (w == nullptr || !w->isVisibleTo(window)) continue;
            QSizePolicy policy = w->sizePolicy();
            held.append({w, group, policy.retainSizeWhenHidden()});
            policy.setRetainSizeWhenHidden(true);  // 藏起来也占位：首帧版面和动完一模一样，不跳
            w->setSizePolicy(policy);
            w->hide();
        }
    }
    QPointer<QGraphicsDropShadowEffect> glow;
    if (QWidget *launch = window->findChild<QWidget *>(QStringLiteral("homeCurrentLaunch"))) {
        glow = qobject_cast<QGraphicsDropShadowEffect *>(launch->graphicsEffect());
        if (glow != nullptr) glow->setEnabled(false);  // 柔光画在按钮外面，幕布盖不住：等按钮浮到位再亮
    }
    QPointer<TitleBarButton> logo = window->findChild<TitleBarButton *>(QStringLiteral("titleBarLogo"));
    if (logo != nullptr) logo->setWriteProgress(0.0);

    new FirstPaint(backdrop, [held, glow, logo]() {
        for (const Held &h : held) {
            if (h.widget == nullptr) continue;
            Anim::riseIn(h.widget, kGroupDelay[h.group], kRiseMs, kRise);
            QSizePolicy policy = h.widget->sizePolicy();
            policy.setRetainSizeWhenHidden(h.retain);
            h.widget->setSizePolicy(policy);
        }
        if (glow != nullptr) {
            QTimer::singleShot(kGlowDelay, glow, [glow]() {
                if (glow == nullptr) return;
                const QColor full = glow->color();
                glow->setEnabled(true);
                auto *ramp = new QVariantAnimation(glow);
                ramp->setDuration(kGlowMs);
                ramp->setStartValue(0.0);
                ramp->setEndValue(full.alphaF());
                QObject::connect(ramp, &QVariantAnimation::valueChanged, glow, [glow, full](const QVariant &a) {
                    QColor c = full;
                    c.setAlphaF(a.toReal());
                    glow->setColor(c);
                });
                ramp->start(QAbstractAnimation::DeleteWhenStopped);
            });
        }
        if (logo != nullptr) {
            QTimer::singleShot(kWriteDelay, logo, [logo]() {
                if (logo == nullptr) return;
                auto *write = new QVariantAnimation(logo);
                write->setDuration(kWriteMs);
                write->setStartValue(0.0);
                write->setEndValue(1.0);  // 线性：节奏在书写时刻表里
                QObject::connect(write, &QVariantAnimation::valueChanged, logo,
                                 [logo](const QVariant &v) { logo->setWriteProgress(v.toReal()); });
                write->start(QAbstractAnimation::DeleteWhenStopped);
            });
        }
    });
}

bool interceptClose(QWidget *window, QCloseEvent *event, CloseFog &state) {
    if (window == nullptr || event == nullptr || state == CloseFog::Done) return false;
    if (state == CloseFog::Running) {  // 合雾途中又按了一次关：等它播完（120 ms）
        event->ignore();
        return true;
    }
    // 看不见就没什么可播的；关机 / 注销时拦下关窗会被系统当成「拒绝结束」，一律直接放行。
    if (!Anim::enabled() || !window->isVisible() || window->isMinimized() || qGuiApp->isSavingSession()) return false;
    state = CloseFog::Running;
    event->ignore();
    // state 是 window 的成员：window 还在，它就还在。
    Anim::fogClose(window, kCloseMs, kSink, [w = QPointer<QWidget>(window), s = &state]() {
        if (w == nullptr) return;
        *s = CloseFog::Done;
        w->close();
    });
    return true;
}

}  // namespace WindowMotion
