/**
 * @file Glass.cpp
 * @brief 整窗磨砂的实现：四档与回退、着色 alpha 现算、风景底与主页场景的缓存。
 */

#include "Glass.hpp"

#include "Theme.hpp"

#include <QApplication>
#include <QEvent>
#include <QOperatingSystemVersion>
#include <QPainter>
#include <QResizeEvent>
#include <QSettings>
#include <QThread>
#include <QTimer>

#include <cmath>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

/// 场景图在 icons.qrc 里；它编在静态库里时没人引用那个初始化函数就会被链接器丢掉。
/// Q_INIT_RESOURCE 不能写在命名空间里。
static void initGlassResources() { Q_INIT_RESOURCE(icons); }

namespace Glass {
namespace {

/// 拖窗口改尺寸时，停手 120 ms 再按新尺寸重算（中间那几帧把旧缓存拉伸着贴）。
constexpr int kDebounceMs = 120;
/// 模糊在 1/8 尺寸上做，放大回来约等于 26 px 高斯（半径 3 时）。
constexpr int kDownscale = 8;
/// 主页场景左沿渐隐进导航轨的宽度（逻辑像素）。

QPointer<Backdrop> &activeBackdrop() {
    static QPointer<Backdrop> backdrop;
    return backdrop;
}

int &modeCache() {
    static int value = -1;
    return value;
}

Mode parseMode(const QString &key, Mode fallback) {
    for (Mode m : {Mode::Acrylic, Mode::Mica, Mode::Scene, Mode::Solid}) {
        if (key.compare(modeKey(m), Qt::CaseInsensitive) == 0) return m;
    }
    return fallback;
}

bool highContrast() {
#ifdef Q_OS_WIN
    HIGHCONTRASTW hc{};
    hc.cbSize = sizeof(hc);
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) != FALSE)
        return (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
#endif
    return false;
}

/// 系统「透明效果」开关（设置 → 个性化 → 颜色）。
bool transparencyOff() {
#ifdef Q_OS_WIN
    const QSettings personalize(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QSettings::NativeFormat);
    return personalize.value(QStringLiteral("EnableTransparency"), 1).toInt() == 0;
#else
    return false;
#endif
}

bool systemBackdropSupported() {
    return QGuiApplication::platformName() == QLatin1String("windows") &&
           QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows11_22H2;
}

/// 当前主题用哪一张风景（「跟着系统」按这一刻的深浅挑）。
QString sceneId() {
    const QString id = Theme::current().presetId;
    if (id == QLatin1String("dark") || id == QLatin1String("emerald") || id == QLatin1String("ocean"))
        return id;
    return Theme::isDark() ? QStringLiteral("dark") : QStringLiteral("emerald");
}

bool darkTokens() { return Theme::tokens().page.lightness() < 128; }

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
            line[x] = qRgb(qBound(0, qRound(r), 255), qBound(0, qRound(g), 255), qBound(0, qRound(b), 255));
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

/// `source` 按 Cover 铺满 `size`。
QImage coverScaled(const QImage &source, const QSize &size, const QColor &fill) {
    QImage out(size, QImage::Format_RGB32);
    out.fill(fill);
    if (source.isNull()) return out;
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QSize scaled = source.size().scaled(size, Qt::KeepAspectRatioByExpanding);
    p.drawImage(QRect(QPoint((size.width() - scaled.width()) / 2, (size.height() - scaled.height()) / 2), scaled),
                source);
    return out;
}


}  // namespace

// ---------------------------------------------------------------------------
// 四档与回退
// ---------------------------------------------------------------------------

QString settingsKey() { return QStringLiteral("Theme/Backdrop"); }

QString modeKey(Mode mode) {
    switch (mode) {
    case Mode::Acrylic: return QStringLiteral("acrylic");
    case Mode::Mica: return QStringLiteral("mica");
    case Mode::Scene: return QStringLiteral("scene");
    case Mode::Solid: return QStringLiteral("solid");
    }
    return QStringLiteral("acrylic");
}

QString modeName(Mode mode) {
    switch (mode) {
    case Mode::Acrylic: return QStringLiteral("亚克力");
    case Mode::Mica: return QStringLiteral("云母");
    case Mode::Scene: return QStringLiteral("风景底");
    case Mode::Solid: return QStringLiteral("纯色");
    }
    return QString();
}

Mode mode() {
    const QByteArray env = qgetenv("WTFML_BACKDROP");
    if (!env.isEmpty()) return parseMode(QString::fromLatin1(env), Mode::Acrylic);
    int &cache = modeCache();
    if (cache < 0) {
        QSettings settings;
        // 出厂：亚克力；核数太少的机器（≤ 2 线程）出厂纯色。旧版「磨砂效果」关着 ⇒ 纯色。
        Mode fallback = QThread::idealThreadCount() > 2 ? Mode::Acrylic : Mode::Solid;
        if (settings.contains(QStringLiteral("Theme/Glass")) && !settings.value(QStringLiteral("Theme/Glass")).toBool())
            fallback = Mode::Solid;
        cache = int(parseMode(settings.value(settingsKey()).toString(), fallback));
    }
    return Mode(cache);
}

Mode effectiveMode() {
    const Mode chosen = mode();
    if (chosen == Mode::Solid || highContrast() || transparencyOff()) return Mode::Solid;
    if ((chosen == Mode::Acrylic || chosen == Mode::Mica) && !systemBackdropSupported()) return Mode::Scene;
    return chosen;
}

QString fallbackReason() {
    const Mode chosen = mode();
    if (chosen == Mode::Solid) return QString();
    if (highContrast()) return QStringLiteral("系统开着高对比度，用纯色。");
    if (transparencyOff()) return QStringLiteral("系统关了透明效果，用纯色。");
    if ((chosen == Mode::Acrylic || chosen == Mode::Mica) && !systemBackdropSupported())
        return QStringLiteral("系统背板要 Windows 11 22H2 或更新，这里用风景底。");
    return QString();
}

void setMode(Mode m) {
    QSettings().setValue(settingsKey(), modeKey(m));
    modeCache() = int(m);
    if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance())) {
        Theme::apply(*app);
        for (QWidget *top : QApplication::topLevelWidgets()) top->update();
    }
}

bool enabled() { return effectiveMode() != Mode::Solid; }
bool sceneShown() { return !highContrast(); }

bool systemBackdrop() {
    const Mode m = effectiveMode();
    return m == Mode::Acrylic || m == Mode::Mica;
}

int tintStrength() { return qBound(0, QSettings().value(QStringLiteral("Theme/BackdropTint"), 30).toInt(), 100); }
int blurStrength() { return qBound(0, QSettings().value(QStringLiteral("Theme/BackdropBlur"), 50).toInt(), 100); }

void setTintStrength(int value) {
    QSettings().setValue(QStringLiteral("Theme/BackdropTint"), qBound(0, value, 100));
    if (Backdrop *b = Backdrop::active()) b->update();
}

void setBlurStrength(int value) {
    QSettings().setValue(QStringLiteral("Theme/BackdropBlur"), qBound(0, value, 100));
    if (Backdrop *b = Backdrop::active()) {
        b->rebuildNow();
        b->update();
    }
}

void install(QApplication &) { initGlassResources(); }

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
    return QColor(mix(top.red(), bottom.red()), mix(top.green(), bottom.green()), mix(top.blue(), bottom.blue()));
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
                if (contrast(over(ink.first, result), result) < ink.second) ok = false;
            }
        }
        if (ok) return step / 100.0;
    }
    return 1.0;
}

QList<QColor> systemBackdropRange() {
    // 深色亚克力 / 云母自带一层深灰着色，背后是纯白窗口时也到不了 #8c8c8c；浅色那一支背后
    // 是纯黑时也在 #b4b4b4 以上（拿本机截图量过再放宽一档）。
    if (darkTokens()) return {QColor(0x0a, 0x0a, 0x0a), QColor(0x8c, 0x8c, 0x8c)};
    return {QColor(0xb4, 0xb4, 0xb4), QColor(0xff, 0xff, 0xff)};
}

QList<QPair<QColor, double>> bodyInks() {
    const Theme::Tokens t = Theme::tokens();
    return {{t.text, 4.5}, {t.secondary, 4.5}};
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
// 整窗那一层底
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

void Backdrop::setHeroWidget(QWidget *widget) {
    if (m_hero != nullptr) m_hero->removeEventFilter(this);
    m_hero = widget;
    if (widget != nullptr) widget->installEventFilter(this);
    update();
}

bool Backdrop::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_hero) {
        switch (event->type()) {
        case QEvent::Show:
        case QEvent::Hide:
        case QEvent::Move:
        case QEvent::Resize:
            scheduleRebuild();
            update();
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

QRect Backdrop::heroRect() const {
    if (m_hero == nullptr || !m_hero->isVisible() || m_hero->window() != window()) return QRect();
    // 全出血：铺满整窗，导航轨和右上那条窗口按钮条下面也是清晰风景（设计稿 .window.hero）。
    return rect();
}

QColor Backdrop::tint() const {
    const Theme::Tokens t = Theme::tokens();
    QColor base = t.page;
    const Mode m = effectiveMode();
    if (m == Mode::Solid) return base;
    const qreal floor = tintStrength() / 100.0;
    const QList<QColor> range = m == Mode::Scene ? QList<QColor>{m_darkest, m_brightest} : systemBackdropRange();
    base.setAlphaF(minimumTintAlpha(base, bodyInks(), range, floor));
    return base;
}

QImage Backdrop::sourceImage() {
    const Theme::PartImage custom = Theme::partImage(Theme::Part::PageBackground);
    const QString builtIn = QStringLiteral(":/assets/scenes/%1.jpg").arg(sceneId());
    const QString path = custom.isSet() ? custom.path : builtIn;
    if (path != m_sourceKey || m_source.isNull()) {
        m_sourceKey = path;
        m_source = QImage(path);
        // 用户那张读不出来（拔了 U 盘之类）：退回主题自带的风景，不报错。
        if (m_source.isNull() && custom.isSet()) m_source = QImage(builtIn);
    }
    return m_source;
}

void Backdrop::rebuildNow() {
    m_debounce->stop();
    const qreal dpr = devicePixelRatioF();
    const QSize device = (QSizeF(size()) * dpr).toSize();
    if (device.isEmpty()) return;
    const QImage source = sourceImage();
    const Theme::Tokens t = Theme::tokens();
    const bool dark = darkTokens();

    // 风景底：在 1/8 尺寸上铺满、模糊、往令牌色混，贴的时候放大（平滑缩放本身再糊一层）。
    const int blurRadius = 1 + blurStrength() * 5 / 100;
    const QString frostKey = m_sourceKey + QLatin1Char('|') + QString::number(device.width()) + QLatin1Char('x') +
                             QString::number(device.height()) + QLatin1Char('|') + t.page.name() +
                             QLatin1Char('|') + QString::number(blurRadius);
    if (frostKey != m_frostKey || m_frost.isNull()) {
        m_frostKey = frostKey;
        const QSize small(qMax(1, device.width() / kDownscale), qMax(1, device.height() / kDownscale));
        QImage frost = boxBlur(coverScaled(source, small, t.page), blurRadius);
        processFrost(frost, t.page, dark ? 0.35 : 0.3, dark ? 1.05 : 1.15);
        extremes(frost, frost.rect(), &m_darkest, &m_brightest);
        m_frost = QPixmap::fromImage(frost.scaled(device, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        m_frost.setDevicePixelRatio(dpr);
        m_frostSize = size();
    }
    rebuildHero();
}

void Backdrop::rebuildHero() {
    const QRect hero = heroRect();
    if (hero.isEmpty()) return;
    const qreal dpr = devicePixelRatioF();
    const QSize device = (QSizeF(hero.size()) * dpr).toSize();
    const Theme::Tokens t = Theme::tokens();
    const QWidget *rail = findChild<QWidget *>(QStringLiteral("titleBar"));
    // 雾只在亮主题有；深色主题不把轨宽放进键里，折叠动画时不必每帧重画整张图。
    const int railWidth = t.railFog > 0 && rail != nullptr && rail->isVisible() ? rail->width() : 0;
    const QString key = m_sourceKey + QLatin1Char('|') + QString::number(device.width()) + QLatin1Char('x') +
                        QString::number(device.height()) + QLatin1Char('|') + t.heroVeil.name() +
                        QLatin1Char('|') + QString::number(t.heroTop) + QLatin1Char('|') +
                        QString::number(t.heroScrim) + QLatin1Char('|') + QString::number(t.railFog) +
                        QLatin1Char('|') + QString::number(railWidth);
    if (key == m_heroKey && !m_heroPixmap.isNull()) {
        m_heroCacheRect = hero;
        return;
    }
    m_heroKey = key;
    QImage sharp = coverScaled(sourceImage(), device, t.page).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    {
        // 设计稿 tokens.md 第 4 节：四层 hero-veil 色渐变压暗角（左侧线性、底部线性、左上径向
        // 压标题、启动区径向），亮主题再在导航轨下加一层雾。逻辑像素按 dpr 换算。
        QPainter p(&sharp);
        p.setRenderHint(QPainter::Antialiasing, true);
        const qreal w = device.width();
        const qreal h = device.height();
        const auto veil = [&t](qreal alpha) {
            QColor c = t.heroVeil;
            c.setAlphaF(qBound(0.0, alpha, 1.0));
            return c;
        };
        QLinearGradient left(0, 0, 520 * dpr, 0);
        left.setColorAt(0.0, veil(0.82));
        left.setColorAt(200.0 / 520.0, veil(0.55));
        left.setColorAt(1.0, veil(0));
        p.fillRect(QRectF(0, 0, qMin(w, 520 * dpr), h), left);
        QLinearGradient bottom(0, h, 0, h - 420 * dpr);
        bottom.setColorAt(0.0, veil(0.86));
        bottom.setColorAt(190.0 / 420.0, veil(0.55));
        bottom.setColorAt(1.0, veil(0));
        p.fillRect(QRectF(0, qMax(0.0, h - 420 * dpr), w, qMin(h, 420 * dpr)), bottom);
        // CSS radial-gradient(rx ry at cx cy)：在单位圆上画，再按 rx / ry 拉成椭圆。
        const auto ellipse = [&](qreal cx, qreal cy, qreal rx, qreal ry, qreal alpha, qreal end) {
            p.save();
            p.translate(cx, cy);
            p.scale(rx, ry);
            QRadialGradient g(QPointF(0, 0), 1.0);
            g.setColorAt(0.0, veil(alpha));
            g.setColorAt(end, veil(0));
            g.setColorAt(1.0, veil(0));
            p.fillRect(QRectF(-cx / rx, -cy / ry, w / rx, h / ry), g);
            p.restore();
        };
        ellipse(w * 0.25, h * 0.12, w * 0.70, h * 0.55, t.heroTop, 0.70);
        ellipse(w * 0.30, h * 0.66, w * 0.42, h * 0.30, t.heroScrim, 1.0);
        if (t.railFog > 0 && railWidth > 0) {
            QLinearGradient fog(0, 0, railWidth * dpr, 0);
            fog.setColorAt(0.0, veil(t.railFog));
            fog.setColorAt(0.55, veil(t.railFog));
            fog.setColorAt(1.0, veil(0));
            p.fillRect(QRectF(0, 0, railWidth * dpr, h), fog);
        }
        m_heroSmall = sharp.scaled(qMax(1, device.width() / kDownscale), qMax(1, device.height() / kDownscale),
                                   Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    m_heroPixmap = QPixmap::fromImage(sharp);
    m_heroPixmap.setDevicePixelRatio(dpr);
    m_heroCacheRect = hero;
}

QColor Backdrop::brightestUnder(const QWidget *widget) const {
    const QRect hero = heroRect();
    if (m_heroSmall.isNull() || widget == nullptr || hero.isEmpty()) return QColor(Qt::white);
    const QRect area = QRect(widget->mapTo(this, QPoint()), widget->size()).translated(-hero.topLeft());
    const qreal sx = qreal(m_heroSmall.width()) / hero.width();
    const qreal sy = qreal(m_heroSmall.height()) / hero.height();
    const QRect small(qFloor(area.left() * sx), qFloor(area.top() * sy), qCeil(area.width() * sx) + 1,
                      qCeil(area.height() * sy) + 1);
    QColor brightest(Qt::black);
    extremes(m_heroSmall, small, nullptr, &brightest);
    return brightest;
}

void Backdrop::scheduleRebuild() {
    if (m_frost.isNull()) {
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
    const Mode m = effectiveMode();
    // `grab()` 往 QPixmap 里画（屏上那一趟重定向到的是 backing store 的 QImage）——
    // DWM 那一层拍不进位图，这一趟按风景底画。
    const QPaintDevice *redirect = redirected(nullptr);
    const bool toPixmap = redirect != nullptr && redirect->devType() == QInternal::Pixmap;
    if (m == Mode::Solid) {
        painter.fillRect(rect(), Theme::tokens().page);
    } else if ((m == Mode::Acrylic || m == Mode::Mica) && !toPixmap) {
        // 系统背板在 DWM 那一层；这里只换掉窗口里的像素（Source：连 alpha 一起写）。
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(rect(), tint());
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    } else {
        if (m_frost.isNull()) rebuildNow();
        painter.setRenderHint(QPainter::SmoothPixmapTransform, m_frostSize != size());
        painter.drawPixmap(rect(), m_frost, m_frost.rect());
        QColor layer = Theme::tokens().page;
        layer.setAlphaF(minimumTintAlpha(layer, bodyInks(), {m_darkest, m_brightest}, tintStrength() / 100.0));
        painter.fillRect(rect(), layer);
    }
    const QRect hero = heroRect();
    if (!hero.isEmpty() && sceneShown()) {
        rebuildHero();  // 键没变时立刻返回（尺寸、主题、导航轨宽都在键里）
        painter.setRenderHint(QPainter::SmoothPixmapTransform, m_heroCacheRect.size() != hero.size());
        painter.drawPixmap(hero, m_heroPixmap, m_heroPixmap.rect());
    }
}

}  // namespace Glass
