/**
 * @file Glass.cpp
 * @brief 磨砂玻璃的实现：场景底的两份缓存、绘制过滤器、着色层 alpha 现算。
 */

#include "Glass.hpp"

#include "Theme.hpp"

#include <QApplication>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QResizeEvent>
#include <QSettings>
#include <QThread>
#include <QTimer>

#include <cmath>

/// 场景图和标题字体在 icons.qrc 里；它编在静态库里时，没人引用那个初始化函数就会被链接器
/// 丢掉（验收程序就是这样）。在这里引用一次：所有铺主题的进程都有。Q_INIT_RESOURCE 不能在命名空间里。
static void initGlassResources() { Q_INIT_RESOURCE(icons); }

namespace Glass {
namespace {

constexpr char kKindProperty[] = "wtfmlGlass";
/// 拖窗口改尺寸时，停手 120 ms 再按新尺寸重算（中间那几帧把旧缓存拉伸着贴）。
constexpr int kDebounceMs = 120;
/// 模糊在 1/8 尺寸上做：3 遍半径 3 的盒式模糊 ≈ σ 3.5 px，放大回来约等于 26 px 高斯。
constexpr int kDownscale = 8;
constexpr int kBlurRadius = 3;
/// 卡片柔阴影伸出去多远、往下错多少（逻辑像素）。
constexpr int kShadowSpread = 22;
constexpr int kShadowOffsetY = 8;

int &enabledCache() {
    static int value = -1;
    return value;
}

QPointer<Backdrop> &activeBackdrop() {
    static QPointer<Backdrop> backdrop;
    return backdrop;
}

/// 挂了 Card 的那几块（场景底替它们画柔阴影）。
QList<QPointer<QWidget>> &shadowed() {
    static QList<QPointer<QWidget>> list;
    return list;
}

const char *kindKey(Kind kind) {
    switch (kind) {
    case Kind::Chrome: return "chrome";
    case Kind::Bar: return "bar";
    case Kind::Panel: return "panel";
    case Kind::Card: return "card";
    case Kind::Inset: return "inset";
    }
    return "panel";
}

/// 当前主题用哪一张风景（「跟着系统」按这一刻的深浅挑）。
QString sceneId() {
    const QString id = Theme::current().presetId;
    if (id == QLatin1String("dark") || id == QLatin1String("emerald") ||
        id == QLatin1String("ocean")) {
        return id;
    }
    return Theme::isDark() ? QStringLiteral("dark") : QStringLiteral("emerald");
}

double linearChannel(int value) {
    const double c = value / 255.0;
    return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double luminance(const QColor &color) {
    return 0.2126 * linearChannel(color.red()) + 0.7152 * linearChannel(color.green()) +
           0.0722 * linearChannel(color.blue());
}

/// 每个像素往 `target` 混 `amount`，再按 `saturation` 拉饱和（1 = 不动）。
void processFrost(QImage &image, const QColor &target, qreal amount, qreal saturation) {
    const int tr = target.red(), tg = target.green(), tb = target.blue();
    for (int y = 0; y < image.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb px = line[x];
            qreal r = qRed(px), g = qGreen(px), b = qBlue(px);
            const qreal grey = 0.299 * r + 0.587 * g + 0.114 * b;
            r = grey + (r - grey) * saturation;
            g = grey + (g - grey) * saturation;
            b = grey + (b - grey) * saturation;
            r += (tr - r) * amount;
            g += (tg - g) * amount;
            b += (tb - b) * amount;
            line[x] = qRgb(qBound(0, qRound(r), 255), qBound(0, qRound(g), 255),
                           qBound(0, qRound(b), 255));
        }
    }
}

/// 一张图里最暗、最亮的两个像素（按相对亮度）。
void extremes(const QImage &image, const QRect &area, QColor *darkest, QColor *brightest) {
    double lo = 2.0, hi = -1.0;
    const QRect r = area.intersected(image.rect());
    for (int y = r.top(); y <= r.bottom(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = r.left(); x <= r.right(); ++x) {
            const QColor c = QColor::fromRgb(line[x]);
            const double l = luminance(c);
            if (l < lo) {
                lo = l;
                if (darkest != nullptr) *darkest = c;
            }
            if (l > hi) {
                hi = l;
                if (brightest != nullptr) *brightest = c;
            }
        }
    }
}

/// 这块玻璃和场景底之间隔着别的玻璃或不透明底 ⇒ 只铺一层半透明，不再取模糊。
bool coveredBelow(const QWidget *widget, const Backdrop *backdrop) {
    for (const QWidget *w = widget->parentWidget(); w != nullptr && w != backdrop;
         w = w->parentWidget()) {
        Kind ignored;
        if (w->autoFillBackground() || kindOf(w, &ignored)) return true;
    }
    return false;
}

class PaintFilter final : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        const QEvent::Type type = event->type();
        if (type != QEvent::Paint && type != QEvent::Move && type != QEvent::Resize &&
            type != QEvent::Show && type != QEvent::Hide) {
            return false;
        }
        if (!object->isWidgetType() || !enabled()) return false;
        auto *widget = static_cast<QWidget *>(object);
        Kind kind;
        if (!kindOf(widget, &kind)) return false;
        if (type == QEvent::Paint) {
            QPainter painter(widget);
            paint(painter, widget, QRectF(widget->rect()), kind);
        } else if (kind == Kind::Card) {
            // 柔阴影画在场景底上、伸出卡片外面：卡片一动，场景底那一块得跟着重画。
            if (Backdrop *backdrop = Backdrop::of(widget)) backdrop->update();
        }
        return false;
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// 开关与标记
// ---------------------------------------------------------------------------

QString settingsKey() { return QStringLiteral("Theme/Glass"); }

bool enabled() {
    int &cache = enabledCache();
    if (cache < 0) {
        // 出厂：开。核数太少的机器（≤ 2 线程）默认关，回到不透明令牌。
        const bool fallback = QThread::idealThreadCount() > 2;
        cache = QSettings().value(settingsKey(), fallback).toBool() ? 1 : 0;
    }
    return cache == 1;
}

void setEnabled(bool on) {
    QSettings().setValue(settingsKey(), on);
    enabledCache() = on ? 1 : 0;
    if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance())) {
        Theme::apply(*app);
        for (QWidget *top : QApplication::topLevelWidgets()) top->update();
    }
}

QString kindPropertyName() { return QString::fromLatin1(kKindProperty); }

void mark(QWidget *widget, Kind kind) {
    if (widget == nullptr) return;
    widget->setProperty(kKindProperty, QString::fromLatin1(kindKey(kind)));
    if (kind == Kind::Card) shadowed().append(widget);
}

bool kindOf(const QWidget *widget, Kind *out) {
    const QVariant marked = widget->property(kKindProperty);
    if (marked.isValid()) {
        const QString key = marked.toString();
        for (Kind kind : {Kind::Chrome, Kind::Bar, Kind::Panel, Kind::Card, Kind::Inset}) {
            if (key == QLatin1String(kindKey(kind))) {
                *out = kind;
                return true;
            }
        }
        return false;
    }
    const QVariant surface = widget->property(Theme::surfacePropertyName().toLatin1().constData());
    if (!surface.isValid()) return false;
    const QString value = surface.toString();
    if (value == Theme::sidebarSurfaceValue() || value == Theme::workspaceSurfaceValue()) {
        *out = Kind::Panel;
        return true;
    }
    if (value == Theme::sectionSurfaceValue()) {
        *out = Kind::Inset;
        return true;
    }
    return false;
}

void install(QApplication &app) {
    initGlassResources();
    if (app.findChild<QObject *>(QStringLiteral("wtfmlGlassFilter")) != nullptr) return;
    auto *filter = new PaintFilter(&app);
    filter->setObjectName(QStringLiteral("wtfmlGlassFilter"));
    app.installEventFilter(filter);
}

// ---------------------------------------------------------------------------
// 一块玻璃怎么画
// ---------------------------------------------------------------------------

int radius(Kind kind) {
    switch (kind) {
    case Kind::Chrome:
    case Kind::Bar: return 0;
    case Kind::Panel: return 16;
    case Kind::Card: return 14;
    case Kind::Inset: return 10;
    }
    return 0;
}

QColor tint(Kind kind) {
    const Theme::Tokens t = Theme::tokens();
    const bool dark = Theme::isDark();
    const QPalette palette = QApplication::palette();
    QList<QPair<QColor, double>> inks = {{palette.color(QPalette::WindowText), 4.5},
                                         {palette.color(QPalette::PlaceholderText), 3.0}};
    QColor base;
    qreal floor = 0.6;
    switch (kind) {
    case Kind::Chrome:
        base = Theme::partBackground(Theme::Part::TitleBar);
        inks = {{Theme::partForeground(Theme::Part::TitleBar, QColor(Qt::white)), 4.5}};
        floor = dark ? 0.5 : 0.62;
        break;
    case Kind::Bar:
        base = Theme::partBackground(Theme::Part::BottomBar);
        floor = dark ? 0.55 : 0.6;
        break;
    case Kind::Panel:
        base = dark ? t.page : t.main;
        floor = dark ? 0.5 : 0.55;
        break;
    case Kind::Card:
        base = t.card;
        floor = dark ? 0.55 : 0.62;
        break;
    case Kind::Inset:
        // 叠在玻璃上的一层：明度往正文色那头再推一点点就够分层，对比度由下面那块保证。
        base = dark ? QColor(255, 255, 255) : t.card;
        base.setAlphaF(dark ? 0.05 : 0.45);
        return base;
    }
    Backdrop *backdrop = Backdrop::active();
    if (!enabled() || backdrop == nullptr) {
        base.setAlpha(255);
        return base;
    }
    base.setAlphaF(minimumTintAlpha(base, inks, {backdrop->darkest(), backdrop->brightest()}, floor));
    return base;
}

void paint(QPainter &painter, const QWidget *widget, const QRectF &rect, Kind kind) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const qreal r = radius(kind);
    QPainterPath path;
    if (r > 0) {
        path.addRoundedRect(rect, r, r);
    } else {
        path.addRect(rect);
    }
    const bool dark = Theme::isDark();
    Backdrop *backdrop = Backdrop::of(widget);
    QColor fill = tint(kind);
    if (backdrop == nullptr || backdrop->blurred().isNull()) {
        if (kind != Kind::Inset) fill.setAlpha(255);
        painter.fillPath(path, fill);
    } else {
        if (kind != Kind::Inset && !coveredBelow(widget, backdrop)) {
            painter.setClipPath(path, Qt::IntersectClip);
            const QPoint offset = widget->mapTo(backdrop, QPoint());
            const QRectF target(QPointF(-offset), QSizeF(backdrop->size()));
            painter.setRenderHint(QPainter::SmoothPixmapTransform,
                                  backdrop->cacheSize() != backdrop->size());
            painter.drawPixmap(target, backdrop->blurred(), QRectF(backdrop->blurred().rect()));
        }
        painter.fillPath(path, fill);
    }
    // 亮边：上沿亮、下沿淡，像一块玻璃的切边。顶栏只在下沿、底栏只在上沿留一根发丝线。
    const QColor rim = dark ? QColor(255, 255, 255, 34) : QColor(255, 255, 255, 150);
    painter.setClipping(false);
    if (kind == Kind::Chrome || kind == Kind::Bar) {
        const qreal y = kind == Kind::Chrome ? rect.bottom() - 0.5 : rect.top() + 0.5;
        painter.setPen(QPen(dark ? QColor(255, 255, 255, 22) : QColor(255, 255, 255, 90), 1.0));
        painter.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
    } else {
        QLinearGradient edge(rect.topLeft(), rect.bottomLeft());
        edge.setColorAt(0.0, rim);
        QColor low = rim;
        low.setAlpha(rim.alpha() / 4);
        edge.setColorAt(1.0, low);
        painter.setPen(QPen(QBrush(edge), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), r, r);
    }
    painter.restore();
}

// ---------------------------------------------------------------------------
// 对比度
// ---------------------------------------------------------------------------

double contrast(const QColor &a, const QColor &b) {
    const double la = luminance(a), lb = luminance(b);
    return (qMax(la, lb) + 0.05) / (qMin(la, lb) + 0.05);
}

QColor over(const QColor &top, const QColor &bottom) {
    const qreal a = top.alphaF();
    const auto mix = [a](int t, int b) { return qBound(0, qRound(t * a + b * (1.0 - a)), 255); };
    return QColor(mix(top.red(), bottom.red()), mix(top.green(), bottom.green()),
                  mix(top.blue(), bottom.blue()));
}

qreal minimumTintAlpha(const QColor &tint, const QList<QPair<QColor, double>> &inks,
                       const QList<QColor> &backgrounds, qreal floor) {
    // 逐 1% 往上试：亮度沿 alpha 不一定单调（三个通道可能往不同方向走），二分不保险。
    for (int step = qBound(0, qCeil(floor * 100), 100); step <= 100; ++step) {
        QColor layer = tint;
        layer.setAlphaF(step / 100.0);
        bool ok = true;
        for (const QColor &bg : backgrounds) {
            const QColor result = over(layer, bg);
            for (const auto &ink : inks) {
                // 墨色自己也可能带 alpha（次要字常这样）：先把它落到这一层上再量。
                if (contrast(over(ink.first, result), result) < ink.second) ok = false;
            }
        }
        if (ok) return step / 100.0;
    }
    return 1.0;
}

QImage boxBlur(const QImage &source, int radius) {
    QImage image = source.convertToFormat(QImage::Format_RGB32);
    if (radius <= 0 || image.isNull()) return image;
    const int w = image.width(), h = image.height();
    QList<QRgb> buffer(qMax(w, h));
    const auto pass = [&](bool horizontal) {
        const int lines = horizontal ? h : w;
        const int length = horizontal ? w : h;
        for (int line = 0; line < lines; ++line) {
            const auto at = [&](int i) -> QRgb & {
                const int x = horizontal ? i : line, y = horizontal ? line : i;
                return reinterpret_cast<QRgb *>(image.scanLine(y))[x];
            };
            for (int i = 0; i < length; ++i) buffer[i] = at(i);
            int sr = 0, sg = 0, sb = 0;
            const int window = 2 * radius + 1;
            for (int k = -radius; k <= radius; ++k) {
                const QRgb px = buffer[qBound(0, k, length - 1)];
                sr += qRed(px);
                sg += qGreen(px);
                sb += qBlue(px);
            }
            for (int i = 0; i < length; ++i) {
                at(i) = qRgb(sr / window, sg / window, sb / window);
                const QRgb out = buffer[qBound(0, i - radius, length - 1)];
                const QRgb in = buffer[qBound(0, i + radius + 1, length - 1)];
                sr += qRed(in) - qRed(out);
                sg += qGreen(in) - qGreen(out);
                sb += qBlue(in) - qBlue(out);
            }
        }
    };
    for (int i = 0; i < 3; ++i) {
        pass(true);
        pass(false);
    }
    return image;
}

// ---------------------------------------------------------------------------
// 场景底
// ---------------------------------------------------------------------------

Backdrop::Backdrop(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("glassBackdrop"));
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kDebounceMs);
    connect(m_debounce, &QTimer::timeout, this, [this] {
        rebuildNow();
        update();
    });
    activeBackdrop() = this;
    Theme::follow(this, [this] {
        rebuildNow();
        update();
    });
}

Backdrop::~Backdrop() = default;

Backdrop *Backdrop::of(const QWidget *widget) {
    for (const QWidget *w = widget; w != nullptr; w = w->parentWidget()) {
        if (auto *backdrop = qobject_cast<Backdrop *>(const_cast<QWidget *>(w))) return backdrop;
        if (w->isWindow()) break;
    }
    return nullptr;
}

Backdrop *Backdrop::active() { return activeBackdrop().data(); }

QImage Backdrop::sourceImage() {
    const Theme::PartImage custom = Theme::partImage(Theme::Part::PageBackground);
    const QString path = custom.isSet() ? custom.path
                                        : QStringLiteral(":/assets/scenes/%1.jpg").arg(sceneId());
    if (path != m_sourceKey || m_source.isNull()) {
        m_sourceKey = path;
        m_source = QImage(path);
        if (m_source.isNull() && custom.isSet()) {
            // 用户那张读不出来（拔了 U 盘之类）：退回主题自带的风景，不报错。
            m_source = QImage(QStringLiteral(":/assets/scenes/%1.jpg").arg(sceneId()));
        }
    }
    return m_source;
}

void Backdrop::rebuildNow() {
    m_debounce->stop();
    const qreal dpr = devicePixelRatioF();
    const QSize logical = size();
    const QSize device = (QSizeF(logical) * dpr).toSize();
    if (device.isEmpty()) return;
    const QImage source = sourceImage();
    const bool dark = Theme::isDark();
    const QString key = m_sourceKey + QLatin1Char('|') + QString::number(device.width()) +
                        QLatin1Char('x') + QString::number(device.height()) +
                        (dark ? QStringLiteral("|d") : QStringLiteral("|l"));
    if (key == m_cacheKey && !m_blurred.isNull()) return;
    m_cacheKey = key;

    // 清晰图：按窗口铺满（Cover），再压一层顶部暗角，让顶栏和大标题站得住。
    QImage sharp(device, QImage::Format_RGB32);
    sharp.fill(dark ? QColor(0x12, 0x18, 0x1c) : QColor(0xdd, 0xe8, 0xe4));
    {
        QPainter p(&sharp);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        if (!source.isNull()) {
            const QSize scaled = source.size().scaled(device, Qt::KeepAspectRatioByExpanding);
            const QRect target(QPoint((device.width() - scaled.width()) / 2,
                                      (device.height() - scaled.height()) / 2),
                               scaled);
            p.drawImage(target, source);
        }
        QLinearGradient veil(0, 0, 0, device.height());
        veil.setColorAt(0.0, QColor(0, 0, 0, dark ? 120 : 84));
        veil.setColorAt(0.45, QColor(0, 0, 0, dark ? 40 : 0));
        veil.setColorAt(1.0, QColor(0, 0, 0, dark ? 70 : 24));
        p.fillRect(sharp.rect(), veil);
        // 左上那一团暗角：主页大标题压在这一带，白墨要站得住（对比度在 ui_check 里量）。
        QRadialGradient hero(QPointF(device.width() * 0.12, device.height() * 0.16),
                             device.width() * 0.55);
        hero.setColorAt(0.0, QColor(0, 0, 0, dark ? 90 : 120));
        hero.setColorAt(1.0, QColor(0, 0, 0, 0));
        p.fillRect(sharp.rect(), hero);
    }

    // 模糊副本：缩小 → 三遍盒式模糊 → 提亮（浅色）/ 压暗（深色）→ 放大回去。
    const QSize smallSize(qMax(1, device.width() / kDownscale), qMax(1, device.height() / kDownscale));
    m_sharpSmall = sharp.scaled(smallSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QImage blurred = boxBlur(m_sharpSmall, kBlurRadius);
    if (dark) {
        processFrost(blurred, QColor(0x0e, 0x13, 0x17), 0.28, 1.1);
    } else {
        processFrost(blurred, QColor(Qt::white), 0.28, 1.25);
    }
    extremes(blurred, blurred.rect(), &m_darkest, &m_brightest);

    m_sharp = QPixmap::fromImage(sharp);
    m_sharp.setDevicePixelRatio(dpr);
    m_blurred = QPixmap::fromImage(
        blurred.scaled(device, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    m_blurred.setDevicePixelRatio(dpr);
    m_cacheSize = logical;
}

QColor Backdrop::brightestUnder(const QWidget *widget) const {
    if (m_sharpSmall.isNull() || widget == nullptr || m_cacheSize.isEmpty()) return QColor(Qt::white);
    const QRect area(widget->mapTo(this, QPoint()), widget->size());
    const qreal sx = qreal(m_sharpSmall.width()) / m_cacheSize.width();
    const qreal sy = qreal(m_sharpSmall.height()) / m_cacheSize.height();
    const QRect small(qFloor(area.left() * sx), qFloor(area.top() * sy),
                      qCeil(area.width() * sx) + 1, qCeil(area.height() * sy) + 1);
    QColor brightest(Qt::black);
    extremes(m_sharpSmall, small, nullptr, &brightest);
    return brightest;
}

void Backdrop::scheduleRebuild() {
    if (m_blurred.isNull()) {
        rebuildNow();
    } else {
        m_debounce->start();
    }
}

void Backdrop::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    activeBackdrop() = this;
    scheduleRebuild();
}

void Backdrop::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    if (!enabled()) {
        painter.fillRect(rect(), palette().color(QPalette::Window));
        return;
    }
    if (m_sharp.isNull()) rebuildNow();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, m_cacheSize != size());
    painter.drawPixmap(rect(), m_sharp, m_sharp.rect());
    paintShadows(painter);
}

void Backdrop::paintShadows(QPainter &painter) {
    // 每张卡片一张阴影图，按尺寸缓存（1/4 尺寸上画圆角块 + 模糊，贴的时候放大）。
    static QHash<quint64, QImage> cache;
    const bool dark = Theme::isDark();
    QList<QPointer<QWidget>> &list = shadowed();
    list.removeAll(nullptr);
    for (const QPointer<QWidget> &card : std::as_const(list)) {
        if (card->window() != window() || !card->isVisible()) continue;
        if (Backdrop::of(card) != this) continue;
        const QSize s = card->size();
        if (s.isEmpty()) continue;
        const quint64 key = (quint64(s.width()) << 32) | quint64(s.height()) | (dark ? 1ull << 63 : 0);
        QImage shadow = cache.value(key);
        if (shadow.isNull()) {
            constexpr int q = 4;
            const QSize outer((s.width() + 2 * kShadowSpread) / q, (s.height() + 2 * kShadowSpread) / q);
            QImage block(outer, QImage::Format_ARGB32_Premultiplied);
            block.fill(Qt::transparent);
            QPainter p(&block);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0));
            p.drawRoundedRect(QRectF(kShadowSpread / q, kShadowSpread / q, s.width() / q,
                                     s.height() / q),
                              radius(Kind::Card) / q, radius(Kind::Card) / q);
            p.end();
            // 盒式模糊只认不透明像素：把 alpha 当灰度模糊，再塞回 alpha。
            QImage alpha(outer, QImage::Format_RGB32);
            for (int y = 0; y < outer.height(); ++y) {
                for (int x = 0; x < outer.width(); ++x) {
                    const int a = qAlpha(block.pixel(x, y));
                    alpha.setPixel(x, y, qRgb(a, a, a));
                }
            }
            alpha = boxBlur(alpha, 3);
            const int strength = dark ? 150 : 70;
            shadow = QImage(outer, QImage::Format_ARGB32_Premultiplied);
            for (int y = 0; y < outer.height(); ++y) {
                for (int x = 0; x < outer.width(); ++x) {
                    const int a = qRed(alpha.pixel(x, y)) * strength / 255;
                    shadow.setPixel(x, y, qPremultiply(qRgba(0, 0, 0, a)));
                }
            }
            if (cache.size() > 32) cache.clear();
            cache.insert(key, shadow);
        }
        const QPoint at = card->mapTo(this, QPoint());
        const QRect target(at.x() - kShadowSpread, at.y() - kShadowSpread + kShadowOffsetY,
                           s.width() + 2 * kShadowSpread, s.height() + 2 * kShadowSpread);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(target, shadow);
    }
}

}  // namespace Glass
