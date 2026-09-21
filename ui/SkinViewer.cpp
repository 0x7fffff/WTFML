/**
 * @file SkinViewer.cpp
 * @brief 实现 SkinViewer：鼠标事件 → 视角，视角 + 贴图 → SkinModel::render()。
 * @author 0x7fffff
 * @date 2026-09-21
 */

#include "SkinViewer.hpp"

#include "SkinModel.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QtMath>

namespace {

/// 画面四周留的一点边距：模型贴边看着很挤，而且自适应缩放是按"占高度的 85%"算的，
/// 留出边距之后那 15% 的余量才有地方落。
constexpr int kPadding = 4;

/// 拖动时一像素鼠标位移转多少度。0.5 = 拖过整个控件宽度（约 260 px）转 130°，
/// 差不多是"手腕动一下"的范围。太大就控制不住，太小又得拖好几下。
constexpr double kDegreesPerPixel = 0.5;

/// 滚轮一"格"（angleDelta 的 120 单位）缩放多少倍。用乘法而不是加法：放大之后再缩小
/// 要能原路回到原来的倍数（加法在两头被夹住之后就回不去了）。
constexpr double kZoomPerNotch = 1.1;

}  // namespace

SkinViewer::SkinViewer(QWidget *parent) : QWidget(parent) {
    setObjectName(viewerName());
    // 初始视角就是默认视角。别把这三个初值写到成员初始化列表里再在这里改一遍 ——
    // 那样"初始视角"就有两个出处了。
    m_yaw = defaultYaw();
    m_pitch = defaultPitch();
    m_zoom = defaultZoom();

    // 鼠标形状是"能抓"的唯一提示（没有别的地方告诉用户这个图能拖）。
    setCursor(Qt::OpenHandCursor);
    setFocusPolicy(Qt::ClickFocus);  // 点一下能拿焦点，免得拖动被当成"划过窗口"
}

void SkinViewer::setSkin(const QImage &skin, const QImage &cape, bool slim) {
    m_skin = skin;
    m_cape = cape;
    m_slim = slim;
    update();
}

double SkinViewer::defaultYaw() { return SkinModel::defaultYaw(); }
double SkinViewer::defaultPitch() { return SkinModel::defaultPitch(); }
double SkinViewer::defaultZoom() { return 1.0; }

QSize SkinViewer::sizeHint() const { return QSize(180, 200); }

QSize SkinViewer::minimumSizeHint() const {
    // 比这个再小就分不出是个人了。右栏最窄时（窗口拖窄）也仍有 260 上下，够。
    return QSize(96, 120);
}

void SkinViewer::setView(double yaw, double pitch, double zoom) {
    // yaw **不夹也不取模**：转满好几圈是正常的操作，取模会让"转一圈"这件事在
    // 断言的数字上凭空跳一下。它只在渲染时进 sin/cos，多大都无所谓。
    const double nextYaw = yaw;
    const double nextPitch = qBound(-SkinModel::maxPitch(), pitch, SkinModel::maxPitch());
    const double nextZoom = qBound(SkinModel::minZoom(), zoom, SkinModel::maxZoom());

    // 真的变了才重绘：滚轮滚到头之后继续滚不该一直重画（不自转，所以空闲时零重绘）。
    // qFuzzyCompare 对 0 不灵（两个 0 会被判成不相等），所以两边各加 1 挪开 0 点。
    const bool changed = !qFuzzyCompare(nextYaw + 1.0, m_yaw + 1.0) ||
                         !qFuzzyCompare(nextPitch + 1.0, m_pitch + 1.0) ||
                         !qFuzzyCompare(nextZoom + 1.0, m_zoom + 1.0);
    m_yaw = nextYaw;
    m_pitch = nextPitch;
    m_zoom = nextZoom;
    if (changed) {
        update();
    }
}

void SkinViewer::resetView() {
    setView(defaultYaw(), defaultPitch(), defaultZoom());
}

void SkinViewer::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);

    QPainter painter(this);
    // 背景色从 palette 现推，不写死 —— 用户换主题/系统反色时这块不会突兀地亮着
    // （底栏那条通栏的配色也是这么来的，见 MainWindowUI 里 bottomBar 那一段）。
    // 画一块不一样的底色是**故意的**：这个控件是个"取景框"，读起来要能和周围
    // 那层页面背景分开。
    painter.fillRect(rect(), palette().color(QPalette::Base));
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    const QRectF target = QRectF(rect()).adjusted(kPadding, kPadding, -kPadding, -kPadding);
    if (target.width() <= 1.0 || target.height() <= 1.0) {
        return;  // 控件被压没了，投影出来只会是 NaN
    }

    SkinModel::Scene scene;
    scene.skin = m_skin;
    scene.cape = m_cape;
    scene.slim = m_slim;
    scene.yaw = m_yaw;
    scene.pitch = m_pitch;
    scene.zoom = m_zoom;
    SkinModel::render(painter, target, scene);
}

void SkinViewer::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_dragging = true;
    m_lastMousePos = event->position();
    setCursor(Qt::ClosedHandCursor);
    event->accept();
}

void SkinViewer::mouseMoveEvent(QMouseEvent *event) {
    if (!m_dragging) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    // 按**增量**转：绝对量那种写法（"从按下点到现在一共偏了多少"）在转满一圈之后
    // 是靠取模绕回去的，交界处会跳一下。
    const QPointF delta = event->position() - m_lastMousePos;
    m_lastMousePos = event->position();
    // 往右拖 = 把小人往右推 = 露出它的左侧。往下拖 = 把小人的头顶扳向自己（相机升高）。
    // 这两个方向是照大多数三维浏览器的习惯定的，不是随便挑的正负号。
    setView(m_yaw + delta.x() * kDegreesPerPixel, m_pitch + delta.y() * kDegreesPerPixel, m_zoom);
    event->accept();
}

void SkinViewer::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    m_dragging = false;
    setCursor(Qt::OpenHandCursor);
    event->accept();
}

void SkinViewer::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }
    // 转晕了没有别的路回去（这个控件上没有任何按钮），所以双击复位是**必需**的，
    // 不是锦上添花。
    resetView();
    event->accept();
}

void SkinViewer::wheelEvent(QWheelEvent *event) {
    const int delta = event->angleDelta().y();
    if (delta == 0) {
        QWidget::wheelEvent(event);
        return;
    }
    // 除 120 是角度制滚轮（120 单位一格）；触摸板那种像素级的 delta 会得到小于 1 的
    // 指数，也就是"滚得很小就缩得很小"，正是想要的。
    setView(m_yaw, m_pitch, m_zoom * qPow(kZoomPerNotch, delta / 120.0));
    event->accept();
}
