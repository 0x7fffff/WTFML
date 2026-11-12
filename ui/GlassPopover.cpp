/**
 * @file GlassPopover.cpp
 * @brief 玻璃浮层底座（见 .hpp）。
 */
#include "GlassPopover.hpp"

#include "Animations.hpp"
#include "Glass.hpp"
#include "Theme.hpp"

#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>

GlassPopover::GlassPopover(QWidget *anchor)
    : QFrame(anchor != nullptr ? anchor->window() : nullptr), m_anchor(anchor) {
    setAttribute(Qt::WA_DeleteOnClose);
    setFocusPolicy(Qt::StrongFocus);
    applyShadow();
    qApp->installEventFilter(this);
}

GlassPopover::~GlassPopover() {
    if (qApp != nullptr) qApp->removeEventFilter(this);
}

void GlassPopover::applyShadow() {
    // 柔和大阴影（设计原则 5：浮层不描硬边，只靠阴影和底色差离开下面那一层）。
    auto *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(36);
    shadow->setOffset(0, 10);
    shadow->setColor(QColor(0, 0, 0, Theme::isDark() ? 120 : 60));
    setGraphicsEffect(shadow);
}

void GlassPopover::popUp() {
    show();
    place();
    raise();
    setFocus(Qt::PopupFocusReason);
    if (!Anim::enabled()) return;
    // 一个控件只能挂一个 QGraphicsEffect：淡入期间换成透明度，淡完把阴影挂回来。
    auto *fade = new QGraphicsOpacityEffect(this);
    fade->setOpacity(0.0);
    setGraphicsEffect(fade);
    auto *animation = new QVariantAnimation(this);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->setDuration(kFadeMs);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(animation, &QVariantAnimation::valueChanged, fade,
            [fade](const QVariant &value) { fade->setOpacity(value.toReal()); });
    connect(animation, &QVariantAnimation::finished, this, [this] { applyShadow(); });
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void GlassPopover::closeAllIn(QWidget *window) {
    if (window == nullptr) return;
    // 弹层类不写 Q_OBJECT（没有信号），只能按 QFrame 找再 dynamic_cast。
    for (QFrame *frame : window->findChildren<QFrame *>()) {
        if (auto *pop = dynamic_cast<GlassPopover *>(frame); pop != nullptr && pop->isVisible()) pop->close();
    }
}

bool GlassPopover::eventFilter(QObject *watched, QEvent *event) {
    if (!isVisible()) return false;
    if (event->type() == QEvent::MouseButtonPress) {
        auto *widget = qobject_cast<QWidget *>(watched);
        // 点在面板里 / 点在锚点上都不归这里管：后者由锚点按钮自己的 toggle 收起。
        if (widget == nullptr || widget->window() != window()) return false;
        if (widget == this || isAncestorOf(widget)) return false;
        if (m_anchor != nullptr && (widget == m_anchor || m_anchor->isAncestorOf(widget))) return false;
        close();
        return false;
    }
    if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape &&
        qobject_cast<QWidget *>(watched) != nullptr && static_cast<QWidget *>(watched)->window() == window()) {
        close();
        return true;
    }
    if (watched == window() && event->type() == QEvent::Resize) place();
    return false;
}

void GlassPopover::paintEvent(QPaintEvent *) {
    // 面：先铺它那一块整窗磨砂（和整页覆盖层同一层），把下面的正文整块盖住，再压玻璃色
    // + 顶上一道极淡高光，不描边。只压一层半透明玻璃色的话，底下的主按钮、大标题会透上来。
    // 系统背板（亚克力 / 云母）拍不进阴影效果那张位图，退回不透明的页底色。
    const Theme::Tokens t = Theme::tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()), kRadius, kRadius);
    p.save();
    p.setClipPath(path);
    Glass::Backdrop *backdrop = window()->findChild<Glass::Backdrop *>();
    if (backdrop != nullptr && !Glass::systemBackdrop()) backdrop->paintBase(p, this, true);
    else p.fillRect(rect(), t.page);
    p.restore();
    p.fillPath(path, t.glass);
    p.setPen(QPen(t.glassEdge, 1));
    p.drawLine(QPointF(kRadius, 0.5), QPointF(width() - kRadius, 0.5));
}

void GlassPopover::closeEvent(QCloseEvent *event) {
    // 关了要等 deleteLater 才真删：先把名字摘掉，别让按名字找的人摸到这块残影。
    setObjectName(QString());
    QFrame::closeEvent(event);
}
