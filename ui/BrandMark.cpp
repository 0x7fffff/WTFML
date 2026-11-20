/**
 * @file BrandMark.cpp
 * @brief 品牌标共用函数与启动卡片手写 W 的实现（取舍见头文件）。
 */

#include "BrandMark.hpp"

#include "Animations.hpp"
#include "Glass.hpp"
#include "Theme.hpp"

#include <QFile>
#include <QPainter>
#include <QRegularExpression>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

// `Q_INIT_RESOURCE` 不能写在命名空间里（见 ui/AppIcon.hpp）。
static void initBrandResources() { Q_INIT_RESOURCE(icons); }

namespace {

QColor blended(const QColor &from, const QColor &to, qreal k) {
    k = qBound(0.0, k, 1.0);
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * k,
                            from.greenF() + (to.greenF() - from.greenF()) * k,
                            from.blueF() + (to.blueF() - from.blueF()) * k,
                            from.alphaF() + (to.alphaF() - from.alphaF()) * k);
}

qreal smooth(qreal x) {
    x = qBound(0.0, x, 1.0);
    return x * x * (3 - 2 * x);
}

// ---------- 时间线（讨论 #43「C 单向流光」，做法一 G=0；数字同样张 anim_c.py）----------
constexpr qreal kLap = 2.80;       // 一圈
constexpr qreal kBase = 0.25;      // 底色常显
constexpr qreal kCore = 0.22;      // 浓墨实心段（占全长）
constexpr qreal kEdge = 0.07;      // 两端羽化（占全长）
constexpr qreal kRhythm = 0.60;    // 书写节奏占比，其余是匀速
constexpr qreal kRhythmSigma = 0.06;  // 笔速曲线按周长的 6% 抹平
constexpr qreal kFill = 0.25;      // 完成时铺满
constexpr qreal kGray = 0.45;      // 失败转灰
constexpr qreal kStillAt = 1.0;    // 关掉动效（测试 / 截图）时书写定在这一刻

struct WritePoint { float x, y, r, s, t; };
const WritePoint kPoints[] = {
#include "LaunchMarkData.inc"
};
constexpr int kPointCount = int(sizeof(kPoints) / sizeof(kPoints[0]));
const qreal kLength = kPoints[kPointCount - 1].s;

/// 墨段中心在环上的位置随时间的表：笔速 = v1 书写节奏（ds/dt）抹平后与匀速按 6:4 混合。
struct Rhythm {
    static constexpr int N = 1000;
    std::vector<qreal> at;  // at[i] = 中心到达弧长 i*L/N 的时刻，at[N] = kLap
    Rhythm() {
        // 书写时刻 t 对弧长 s 的导数（慢 = 大），在等距弧长上取样。
        std::vector<qreal> tOfS(N);
        int j = 1;
        for (int i = 0; i < N; ++i) {
            const qreal s = i * kLength / N;
            while (j < kPointCount - 1 && kPoints[j].s < s) ++j;
            const WritePoint &a = kPoints[j - 1], &b = kPoints[j];
            const qreal u = b.s > a.s ? (s - a.s) / (b.s - a.s) : 0.0;
            tOfS[i] = a.t + (b.t - a.t) * u;
        }
        const qreal ds = kLength / N;
        std::vector<qreal> speed(N);
        for (int i = 0; i < N; ++i) {
            const qreal slope = i == 0       ? (tOfS[1] - tOfS[0]) / ds
                                : i == N - 1 ? (tOfS[N - 1] - tOfS[N - 2]) / ds
                                             : (tOfS[i + 1] - tOfS[i - 1]) / (2 * ds);
            speed[i] = 1.0 / qMax(slope, 1e-6);
        }
        // 高斯抹平（环上取样，首尾相接），截到 4σ。
        const qreal sigma = kRhythmSigma * N;
        const int reach = int(std::ceil(4 * sigma));
        std::vector<qreal> kernel(2 * reach + 1);
        qreal norm = 0;
        for (int k = -reach; k <= reach; ++k) norm += kernel[k + reach] = std::exp(-0.5 * k * k / (sigma * sigma));
        std::vector<qreal> smoothSpeed(N);
        qreal mean = 0;
        for (int i = 0; i < N; ++i) {
            qreal sum = 0;
            for (int k = -reach; k <= reach; ++k) sum += kernel[k + reach] * speed[((i + k) % N + N) % N];
            mean += smoothSpeed[i] = sum / norm;
        }
        mean /= N;
        at.assign(N + 1, 0.0);
        for (int i = 0; i < N; ++i) at[i + 1] = at[i] + 1.0 / ((1 - kRhythm) + kRhythm * smoothSpeed[i] / mean);
        const qreal total = at[N];
        for (qreal &v : at) v = v / total * kLap;
    }
    /// 时刻 → 墨段中心的弧长（单调递增，一圈回到 0 = 起笔处）。
    qreal pos(qreal t) const {
        t = std::fmod(t, kLap);
        if (t < 0) t += kLap;
        const auto it = std::upper_bound(at.begin(), at.end(), t);
        const int i = qBound(1, int(it - at.begin()), N) - 1;
        const qreal u = at[i + 1] > at[i] ? (t - at[i]) / (at[i + 1] - at[i]) : 0.0;
        return (i + u) * kLength / N;
    }
    qreal vel(qreal t) const {
        constexpr qreal h = 1e-3;
        qreal d = pos(t + h) - pos(t - h);
        if (d < -kLength / 2) d += kLength;
        return d / (2 * h);
    }
};

const Rhythm &rhythm() {
    static const Rhythm r;
    return r;
}

const QPainterPath &launchGlyph() {
    static const QPainterPath path = BrandMark::pathFromSvg(QStringLiteral(":/assets/app/wtfml.svg"));
    return path;
}

}  // namespace

namespace BrandMark {

QPainterPath pathFromSvg(const QString &resource) {
    initBrandResources();
    QFile file(resource);
    QPainterPath out;
    out.setFillRule(Qt::WindingFill);  // SVG 默认 nonzero
    if (!file.open(QIODevice::ReadOnly)) return out;
    const QString svg = QString::fromUtf8(file.readAll());
    const QString d = QRegularExpression(QStringLiteral(" d=\"([^\"]*)\"")).match(svg).captured(1);
    QChar command;
    QList<qreal> numbers;
    const auto flush = [&] {
        for (int i = 0; i + 1 < numbers.size(); i += 2) {
            const QPointF p(numbers.at(i), numbers.at(i + 1));
            // M 后面跟着的多组坐标按 L 算（SVG 规矩）。
            if (command == QLatin1Char('M') && i == 0) out.moveTo(p);
            else out.lineTo(p);
        }
        if (command == QLatin1Char('Z')) out.closeSubpath();
        numbers.clear();
    };
    static const QRegularExpression token(QStringLiteral(R"([MLZ]|-?\d*\.?\d+)"));
    for (auto it = token.globalMatch(d); it.hasNext();) {
        const QString t = it.next().captured(0);
        if (t.size() == 1 && t.at(0).isLetter()) {
            flush();
            command = t.at(0);
            if (command == QLatin1Char('Z')) flush();
        } else {
            numbers.append(t.toDouble());
        }
    }
    flush();
    return out;
}

QPair<QColor, QColor> stops() {
    const Theme::Spec spec = Theme::current();
    const QColor presetAccent = Theme::tokens(spec.presetId).primary;
    const bool customAccent = spec.accent.isValid() && spec.accent != presetAccent;
    // 「跟着系统」落在深色那一档时长得和暮色雪山一样，也用原色。
    const bool charcoal = Theme::tokens().primary == Theme::tokens(QStringLiteral("dark")).primary;
    if (charcoal && !customAccent) return {QColor("#8682D4"), QColor("#6C68BC")};
    const QColor base = customAccent ? spec.accent : Theme::tokens().deep;
    return {base.lighter(118), base};
}

double worstContrast(const QColor &c, const QList<QColor> &backgrounds) {
    double low = 21.0;
    for (const QColor &bg : backgrounds) low = qMin(low, Glass::contrast(Glass::over(c, bg), bg));
    return low;
}

QColor ensureGraphicContrast(QColor color, const QColor &ink, const QList<QColor> &backgrounds) {
    // 小步挪（每步 5%），刚够 3:1 就停：离主题色越近越好（出厂暮色雪山的下端只需挪一点）。
    const QColor from = color;
    for (int step = 1; step <= 20 && worstContrast(color, backgrounds) < 3.0; ++step)
        color = blended(from, ink, step * 0.05);
    return color;
}

QColor toGray(const QColor &c, qreal amount) {
    float h, s, l, a;
    c.getHslF(&h, &s, &l, &a);
    return blended(c, QColor::fromHslF(h, s * 0.10f, l * 0.92f + 0.04f, a), amount);
}

}  // namespace BrandMark

LaunchMark::LaunchMark(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    m_tick = new QTimer(this);
    m_tick->setTimerType(Qt::PreciseTimer);
    m_tick->setInterval(16);
    connect(m_tick, &QTimer::timeout, this, [this] {
        update();
        syncTimer();
    });
}

qreal LaunchMark::pathLength() { return kLength; }

void LaunchMark::startWriting() {
    if (m_phase == Phase::Writing) return;
    m_phase = Phase::Writing;
    m_finishAt = -1;
    m_clock.start();
    syncTimer();
    update();
}

void LaunchMark::finish(bool success) {
    const Phase target = success ? Phase::Succeeded : Phase::Failed;
    if (m_phase == target) return;
    if (m_phase == Phase::Idle) m_clock.start();  // 没写过就出结局：从起笔那一刻铺满
    // 成功后又报失败（不该发生）不重铺，只把灰度从此刻起算。
    if (m_phase == Phase::Writing || m_finishAt < 0) m_finishAt = elapsed();
    m_phase = target;
    syncTimer();
    update();
}

void LaunchMark::reset() {
    m_phase = Phase::Idle;
    m_finishAt = -1;
    syncTimer();
    update();
}

qreal LaunchMark::elapsed() const {
    if (m_frozen >= 0) return m_frozen;
    if (!Anim::enabled()) {
        // 测试 / 截图跳终态：书写定在固定一刻，完成直接到终点。
        if (m_phase == Phase::Writing) return kStillAt;
        return (m_finishAt < 0 ? kStillAt : m_finishAt) + kFill + kGray + 1.0;
    }
    return m_clock.isValid() ? m_clock.nsecsElapsed() / 1e9 : 0.0;
}

LaunchMark::Band LaunchMark::bandNow() const {
    const qreal core = kCore * kLength / 2;
    const qreal now = elapsed();
    if (m_phase == Phase::Writing || m_finishAt < 0) return {rhythm().pos(now), core};
    // 铺满：中心按 v(1-x)² 减速停住（不倒退），半宽 smoothstep 长到半个周长 ⇒ 前后沿同时向两头扩。
    const qreal t0 = m_finishAt;
    const qreal c0 = rhythm().pos(t0);
    const qreal v0 = qMax(0.0, rhythm().vel(t0));
    const qreal x = qMin((now - m_finishAt) / kFill, 1.0);
    const qreal c = c0 + v0 * kFill * (1 - std::pow(1 - x, 3)) / 3;
    return {std::fmod(c, kLength), core + (kLength / 2 - core) * smooth(x)};
}

qreal LaunchMark::inkAt(qreal s) const {
    if (m_phase == Phase::Idle) return 0.0;
    const Band band = bandNow();
    const qreal edge = kEdge * kLength;
    qreal d = std::fmod(s - band.center + kLength / 2, kLength);
    if (d < 0) d += kLength;
    d = std::abs(d - kLength / 2);  // 环上距离
    return kBase + (1 - kBase) * smooth((band.half + edge - d) / edge);
}

qreal LaunchMark::grayAmount() const {
    if (m_phase != Phase::Failed || m_finishAt < 0) return 0.0;
    return smooth((elapsed() - m_finishAt - kFill) / kGray);
}

void LaunchMark::syncTimer() {
    bool need = isVisible() && Anim::enabled() && m_phase != Phase::Idle;
    if (need && m_phase != Phase::Writing) need = elapsed() - m_finishAt < kFill + kGray + 0.05;
    if (need == m_tick->isActive()) return;
    if (need) m_tick->start();
    else m_tick->stop();
}

void LaunchMark::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    syncTimer();
}

void LaunchMark::hideEvent(QHideEvent *event) {
    QWidget::hideEvent(event);
    syncTimer();
}

void LaunchMark::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    m_mapSize = QSize();  // 下次画时重算
}

void LaunchMark::rebuildMaps() {
    const qreal dpr = devicePixelRatioF();
    const QSize px = (QSizeF(size()) * dpr).toSize();
    m_mapSize = px;
    m_mapDpr = dpr;
    const int n = px.width() * px.height();
    m_cover.fill(0.0f, n);
    m_arc.fill(std::numeric_limits<float>::infinity(), n);
    m_rowGradient.fill(0.0f, px.height());
    if (n == 0) return;
    // 字形按外框等比撑满、居中（SVG 画布四周留着白）。
    const QPainterPath &glyph = launchGlyph();
    const QRectF box = glyph.boundingRect();
    if (box.isEmpty()) return;
    const qreal k = qMin(px.width() / box.width(), px.height() / box.height());
    const qreal dx = (px.width() - box.width() * k) / 2 - box.left() * k;
    const qreal dy = (px.height() - box.height() * k) / 2 - box.top() * k;

    QImage coverage(px, QImage::Format_ARGB32_Premultiplied);
    coverage.fill(Qt::transparent);
    {
        QPainter p(&coverage);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.translate(dx, dy);
        p.scale(k, k);
        p.fillPath(glyph, Qt::white);
    }
    for (int y = 0; y < px.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(coverage.constScanLine(y));
        for (int x = 0; x < px.width(); ++x) m_cover[y * px.width() + x] = qAlpha(line[x]) / 255.0f;
        // 渐变区间 y 60 → 190（同 wtfml.svg），按像素中心算。
        m_rowGradient[y] = float(qBound(0.0, ((y + 0.5 - dy) / k - 60.0) / 130.0, 1.0));
    }
    // 每个像素最早被哪一点的圆头笔盖到（点按弧长递增，先到先得）。多放半个像素，免得边缘缺。
    for (const WritePoint &pt : kPoints) {
        const qreal cx = pt.x * k + dx, cy = pt.y * k + dy, rr = pt.r * k + 0.5;
        const int x0 = qMax(0, int(cx - rr - 1)), x1 = qMin(px.width() - 1, int(cx + rr + 1));
        const int y0 = qMax(0, int(cy - rr - 1)), y1 = qMin(px.height() - 1, int(cy + rr + 1));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const qreal ox = x + 0.5 - cx, oy = y + 0.5 - cy;
                float &arc = m_arc[y * px.width() + x];
                if (ox * ox + oy * oy <= rr * rr && std::isinf(arc)) arc = pt.s;
            }
    }
    // 字形里极少数没被圆盖到的边缘像素：借相邻已盖到的像素（几遍就收住）。
    for (int pass = 0; pass < 6; ++pass) {
        bool changed = false;
        const QVector<float> from = m_arc;
        for (int y = 0; y < px.height(); ++y)
            for (int x = 0; x < px.width(); ++x) {
                const int i = y * px.width() + x;
                if (m_cover[i] <= 0.0f || !std::isinf(from[i])) continue;
                for (int oy = -1; oy <= 1 && std::isinf(m_arc[i]); ++oy)
                    for (int ox = -1; ox <= 1; ++ox) {
                        const int nx = x + ox, ny = y + oy;
                        if (nx < 0 || ny < 0 || nx >= px.width() || ny >= px.height()) continue;
                        const float v = from[ny * px.width() + nx];
                        if (!std::isinf(v)) {
                            m_arc[i] = v;
                            changed = true;
                            break;
                        }
                    }
            }
        if (!changed) break;
    }
}

void LaunchMark::paintEvent(QPaintEvent *) {
    if (m_phase == Phase::Idle) return;
    if (m_mapSize != (QSizeF(size()) * devicePixelRatioF()).toSize() || m_mapDpr != devicePixelRatioF())
        rebuildMaps();
    if (m_mapSize.isEmpty()) return;

    // 颜色 = 品牌标那两档 + 对比度兜底（往这一处的字色挪，底取这一格在任一页可能落到的风景）。
    const Theme::Tokens t = Theme::tokens();
    const QColor ink = Glass::sceneShown() ? t.heroInk : t.text;
    QList<QColor> under{t.page};
    if (const Glass::Backdrop *backdrop = Glass::Backdrop::of(this)) {
        const QList<QColor> scene = backdrop->colorsUnderAnyPage(this);
        if (!scene.isEmpty()) under = scene;
    }
    const QPair<QColor, QColor> stops = BrandMark::stops();
    const qreal gray = grayAmount();
    const QColor top = BrandMark::toGray(BrandMark::ensureGraphicContrast(stops.first, ink, under), gray);
    const QColor bottom = BrandMark::toGray(BrandMark::ensureGraphicContrast(stops.second, ink, under), gray);

    const Band band = bandNow();
    const qreal edge = kEdge * kLength;
    QImage frame(m_mapSize, QImage::Format_ARGB32_Premultiplied);
    frame.setDevicePixelRatio(m_mapDpr);
    for (int y = 0; y < m_mapSize.height(); ++y) {
        const QColor c = blended(top, bottom, m_rowGradient[y]);
        const qreal r = c.redF(), g = c.greenF(), b = c.blueF();
        QRgb *line = reinterpret_cast<QRgb *>(frame.scanLine(y));
        for (int x = 0; x < m_mapSize.width(); ++x) {
            const int i = y * m_mapSize.width() + x;
            qreal alpha = m_cover[i];
            if (alpha > 0) {
                const qreal s = std::isinf(m_arc[i]) ? 0.0 : m_arc[i];
                qreal d = std::fmod(s - band.center + kLength / 2, kLength);
                if (d < 0) d += kLength;
                d = std::abs(d - kLength / 2);
                alpha *= kBase + (1 - kBase) * smooth((band.half + edge - d) / edge);
            }
            line[x] = qRgba(int(r * alpha * 255 + 0.5), int(g * alpha * 255 + 0.5), int(b * alpha * 255 + 0.5),
                            int(alpha * 255 + 0.5));
        }
    }
    QPainter(this).drawImage(0, 0, frame);
}
