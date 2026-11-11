/**
 * @file SkinViewer.cpp
 * @brief 实现 SkinViewer：鼠标事件 → 视角，视角 + 贴图 → SkinModel::render()。
 */

#include "SkinViewer.hpp"

#include "SkinModel.hpp"
#include "Glass.hpp"
#include "Theme.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QRadialGradient>
#include <QShowEvent>
#include <QWheelEvent>
#include <QtMath>

#include <cmath>

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

/// 走一个来回要多久（毫秒）。一步约 0.8 秒 —— 这是**慢走**的节奏（官启那个小人比这快，
/// 但预览里的小人是给眼睛看的：慢一点才好把皮肤看清，快走看着像在赶路）。
constexpr int kWalkPeriodMs = 1600;

/// 定时器间隔（毫秒）。16 ≈ 60 帧/秒，肉眼看是连续的。**它不决定走多快** ——
/// 走多快由 kWalkPeriodMs 和真实时间定（见 walkPhase()），这个数只决定画面多久刷一次。
constexpr int kWalkFrameMs = 16;

}  // namespace

SkinViewer::SkinViewer(QWidget *parent) : QWidget(parent) {
    setObjectName(viewerName());
    QSizePolicy viewerPolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    viewerPolicy.setHeightForWidth(true);
    setSizePolicy(viewerPolicy);
    // 初始视角就是默认视角。别把这三个初值写到成员初始化列表里再在这里改一遍 ——
    // 那样"初始视角"就有两个出处了。
    m_yaw = defaultYaw();
    m_pitch = defaultPitch();
    m_zoom = defaultZoom();

    // 走路：相位 0 起步（迈得最大的那一帧 —— 第一眼就看得出来他是在走，而不是立正）。
    // 定时器本身不推进相位，它只负责"该重画了"；相位是现算的。
    m_walkTimer = new QTimer(this);
    m_walkTimer->setInterval(kWalkFrameMs);
    connect(m_walkTimer, &QTimer::timeout, this, [this]() { update(); });

    // 鼠标形状是"能抓"的唯一提示（没有别的地方告诉用户这个图能拖）。
    setCursor(Qt::OpenHandCursor);
    setFocusPolicy(Qt::ClickFocus);  // 点一下能拿焦点，免得拖动被当成"划过窗口"
}

double SkinViewer::walkPhase() const {
    if (!m_walkTimer->isActive()) {
        return m_phaseBase;  // 停着的时候"当前那一帧"就是起始相位
    }
    const double turns = static_cast<double>(m_walkClock.elapsed()) / kWalkPeriodMs;
    // 折到 0..1：fmod 而不是取余数运算符（浮点用不了 %）。
    return std::fmod(m_phaseBase + turns, 1.0);
}

void SkinViewer::setWalkPhase(double phase) {
    // 先折到 0..1（相位是个环，传 1.25 和 0.25 是同一帧），再从现在这一帧重新起算，
    // 这样动画开着的时候也能中途插进去而不跳帧。
    m_phaseBase = phase - std::floor(phase);
    m_walkClock.restart();
    update();
}

void SkinViewer::setAnimated(bool on) {
    if (m_animated == on) {
        return;
    }
    m_animated = on;
    if (!on) {
        stopWalking();
    } else if (isVisible()) {
        // 藏着的控件上打开动画**先不转**：等转到可见那条路（showEvent）再去开定时器。
        // 少了 isVisible() 这个判断，"只在看得见的时候走"就有一个后门 —— 谁在窗口还没
        // 显示的时候调一次 setAnimated(true)，这个控件就会在后台一直每秒重画 60 次。
        startWalking();
    }
}

void SkinViewer::startWalking() {
    if (m_walkTimer->isActive()) {
        return;
    }
    // m_phaseBase 不动 —— 接着上次停下那一帧往下走，而不是每次都从 0 开始。
    m_walkClock.start();
    m_walkTimer->start();
}

void SkinViewer::stopWalking() {
    if (!m_walkTimer->isActive()) {
        return;
    }
    m_phaseBase = walkPhase();  // 先记下现在停在哪一帧，再收工
    m_walkTimer->stop();
}

void SkinViewer::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    // 只在看得见的时候走：切到别的页、窗口最小化的时候不该还在后台每秒重画 60 次。
    if (m_animated) {
        startWalking();
    }
}

void SkinViewer::hideEvent(QHideEvent *event) {
    QWidget::hideEvent(event);
    stopWalking();
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

namespace {
// SkinModel 按模型高 32、最大横向跨度 24，并分别让它占视口高度的 85%、宽度的 90%。
// 两个限制恰好同时生效时，人物上下接近顶格，左右也只留少量安全边距。
constexpr double kIdealViewportHeightPerWidth = (0.90 * 32.0) / (0.85 * 24.0);
}

QSize SkinViewer::sizeHint() const { return QSize(180, heightForWidth(180)); }

QSize SkinViewer::minimumSizeHint() const {
    // 最小尺寸也沿用同一比例；缩小时不会重新变成一块横向很宽、人物很小的画布。
    return QSize(96, heightForWidth(96));
}

bool SkinViewer::hasHeightForWidth() const { return true; }

int SkinViewer::heightForWidth(int width) const {
    return qMax(1, qRound(qMax(1, width) * kIdealViewportHeightPerWidth));
}

void SkinViewer::setView(double yaw, double pitch, double zoom) {
    // yaw **不夹也不取模**：转满好几圈是正常的操作，取模会让"转一圈"这件事在
    // 断言的数字上凭空跳一下。它只在渲染时进 sin/cos，多大都无所谓。
    const double nextYaw = yaw;
    const double nextPitch = qBound(-SkinModel::maxPitch(), pitch, SkinModel::maxPitch());
    const double nextZoom = qBound(SkinModel::minZoom(), zoom, SkinModel::maxZoom());

    // 真的变了才重绘：滚轮滚到头之后继续滚不该一直重画。
    // （走路那点动画是定时器自己触发的重绘，跟这条无关 —— 这条管的是"视角没变就别画"。）
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
    if (Glass::enabled()) {
        // 磨砂开着：取景框就是卡片那块玻璃本身，不再贴一块不透明底；脚下垫一圈柔和的
        // 落地阴影，人站在玻璃上而不是飘着。
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF floor(width() * 0.18, height() - kPadding - 14.0, width() * 0.64, 18.0);
        QRadialGradient glow(floor.center(), floor.width() / 2);
        glow.setColorAt(0.0, QColor(0, 0, 0, Theme::isDark() ? 110 : 60));
        glow.setColorAt(1.0, QColor(0, 0, 0, 0));
        painter.save();
        painter.translate(floor.center());
        painter.scale(1.0, floor.height() / floor.width());
        painter.translate(-floor.center());
        painter.setPen(Qt::NoPen);
        painter.setBrush(glow);
        painter.drawEllipse(floor.center(), floor.width() / 2, floor.width() / 2);
        painter.restore();
    } else {
        painter.fillRect(rect(), palette().color(QPalette::Base));
        painter.setPen(palette().color(QPalette::Mid));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }

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
    // 走动的那一帧由控件现算（见 walkPhase）：几何层只认"给我一个相位"。
    scene.walkPhase = walkPhase();
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
    // 水平：**往哪边拖，小人的正面就往哪边走**（按住正面往右推，正面就朝右滑，于是转到
    // 它的**右侧**去）—— 这是"抓住模型拧"的直觉，三维浏览器都是这个方向。往下拖 = 把
    // 小人的正面往下扳 = 头顶倒向自己（相机升高，从上面看）。
    //
    // NOTE: 水平是**减**、垂直是**加** —— 看着不对称，但这是算出来的，不是挑的。
    // yaw 正数 = 相机绕到玩家的左侧（见 makeCamera）。相机往左挪，小人的正面在
    // 画面上就是往**左**滑（正面那一点在 yaw=+30° 时的屏幕横坐标是负的），
    // 所以"正面跟着鼠标往右走"要的是 yaw **减小**。俯仰那边正好相反：pitch 正数
    // = 相机升高、从上往下看，正面对着的那一块因此往**下**走，跟"往下拖"同向，
    // 于是那里是加。
    setView(m_yaw - delta.x() * kDegreesPerPixel, m_pitch + delta.y() * kDegreesPerPixel, m_zoom);
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
