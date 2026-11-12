/**
 * @file WindowEdge.cpp
 * @brief 窗口最外沿那一圈的绘制与盖层（见 WindowEdge.hpp）。
 */

#include "WindowEdge.hpp"

#include "Theme.hpp"

#include <QEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QOperatingSystemVersion>
#include <QPainterPath>
#include <QRegion>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace {

const char *const kOverlayName = "wtfmlWindowEdge";

QColor withAlpha(QColor c, double alpha) {
    c.setAlphaF(float(std::clamp(alpha, 0.0, 1.0)));
    return c;
}

/// 盖在窗口所有子控件上面、只画边的那一层。不吃鼠标（childAt 也跳过它）。
class EdgeOverlay final : public QWidget {
public:
    explicit EdgeOverlay(QWidget *window) : QWidget(window), m_window(window) {
        setObjectName(QLatin1String(kOverlayName));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
        setGeometry(window->rect());
        updateMask();
        window->installEventFilter(this);
        raise();
        show();
        Theme::follow(this, [this] { update(); });
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (watched == m_window) {
            switch (event->type()) {
            case QEvent::Resize:
                setGeometry(m_window->rect());
                updateMask();
                break;
            case QEvent::ChildAdded:
                // 后加的子控件会盖到它上面：等这一拍加完再抬回最上层。
                QTimer::singleShot(0, this, [this] { raise(); });
                break;
            case QEvent::WindowStateChange:
                update();
                break;
            default:
                break;
            }
        }
        return QWidget::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent *) override {
        if (m_window->isMaximized() || m_window->isFullScreen()) return;
        QPainter painter(this);
        WindowEdge::paint(painter, QRectF(rect()), Theme::tokens().page.lightness() < 128);
    }

private:
    /// 只留最外沿那一圈：中间的内容重绘时不经过这一层。
    void updateMask() {
        // 线 1px + 淡出 kFadeWidth，再多留 2px 给圆角处往里弯的那一段。
        const int band = 1 + WindowEdge::kFadeWidth + 2;
        const QRect all = rect();
        setMask(QRegion(all).subtracted(QRegion(all.adjusted(band, band, -band, -band))));
    }

    QWidget *m_window;
};

}  // namespace

namespace WindowEdge {

bool supported() {
#ifdef Q_OS_WIN
    return QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows11;
#else
    return false;
#endif
}

bool enabled() { return supported() && qEnvironmentVariable("WTFML_WINDOW_EDGE") != QLatin1String("0"); }

void paint(QPainter &painter, const QRectF &rect, bool dark) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    // 深色底：白线上亮下淡。浅色底：上沿白高光（再亮一档才看得见），下沿换成极淡的深色收边。
    const QColor top = withAlpha(Qt::white, dark ? kTopAlpha : kTopAlpha * 2.5);
    const QColor bottom = dark ? withAlpha(Qt::white, kBottomAlpha) : withAlpha(Qt::black, kLightBottomAlpha);
    for (int i = 0; i <= kFadeWidth; ++i) {
        // i = 0 是那根线；往里每一圈按平方衰减，到 kFadeWidth 归零。
        const double k = i == 0 ? 1.0 : 0.35 * std::pow(1.0 - double(i) / (kFadeWidth + 1), 2.0);
        const QRectF r = rect.adjusted(i + 0.5, i + 0.5, -i - 0.5, -i - 0.5);
        if (r.width() <= 0 || r.height() <= 0) break;
        QLinearGradient grad(r.topLeft(), r.bottomLeft());
        grad.setColorAt(0.0, withAlpha(top, top.alphaF() * k));
        grad.setColorAt(1.0, withAlpha(bottom, bottom.alphaF() * k));
        painter.setPen(QPen(QBrush(grad), 1.0));
        const double radius = std::max(0.0, kCornerRadius - i);
        painter.drawRoundedRect(r, radius, radius);
    }
    painter.restore();
}

void install(QWidget *window) {
    if (window == nullptr || !enabled()) return;
    if (window->findChild<QWidget *>(QLatin1String(kOverlayName), Qt::FindDirectChildrenOnly) != nullptr) return;
    new EdgeOverlay(window);
}

}  // namespace WindowEdge
