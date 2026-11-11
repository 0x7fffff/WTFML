/**
 * @file Glass.hpp
 * @brief 磨砂玻璃界面：窗口底下那张风景（场景底）、预模糊副本，以及每块玻璃面板怎么画。
 *
 * Qt Widgets 没有 backdrop-filter，做法是：
 * - `Glass::Backdrop` 是主窗口的中央控件，铺满一张风景（主题自带的，或用户在「个性化」里给
 *   页面背景设的那张）。尺寸变了按 120 ms 防抖重算两份缓存：按窗口尺寸铺满的清晰图、和它的
 *   模糊副本（缩到 1/8 → 三遍盒式模糊 → 提亮/压暗 → 放大回来，等效约 26 px 高斯）。
 * - 挂了 `wtfmlGlass` 属性的控件（或 `wtfmlSurface` 为 sidebar/workspace/section 的那几块面），
 *   由装在 QApplication 上的绘制过滤器在它自己的 paintEvent 之前画：模糊副本里自己那一块 +
 *   着色层 + 1 px 亮边。场景是静态的，所以每次重绘只是一次贴图。
 * - 着色层的 alpha 不是拍脑袋的数：按当前场景模糊副本里最暗、最亮的那两种颜色现算，保证
 *   正文对玻璃 ≥ 4.5:1、次要字 ≥ 3:1（见 `minimumTintAlpha()`），再和每一档的审美下限取大。
 *
 * 关掉「磨砂效果」（设置 → 个性化）后一切回到不透明令牌，和改版前逐像素一致。
 */

#pragma once

#include <QColor>
#include <QImage>
#include <QList>
#include <QPair>
#include <QPixmap>
#include <QWidget>

class QApplication;
class QPainter;
class QTimer;

namespace Glass {

/// 玻璃的几档。Chrome = 顶栏（主题色那一支、白墨）；Bar = 底栏；Panel = 侧栏、正文面这种
/// 大面；Card = 悬浮卡片（底下有柔阴影）；Inset = 叠在别的玻璃上的那一层（不再取模糊，
/// 只铺一层半透明）。
enum class Kind { Chrome, Bar, Panel, Card, Inset };

/// 「磨砂效果」开关（默认开），存在 QSettings 的 `Theme/Glass`。
bool enabled();
/// 改开关：落盘，然后重新铺一遍主题（样式表、调色板、所有控件重画）。
void setEnabled(bool on);
QString settingsKey();

/// 给控件挂上玻璃（属性 `wtfmlGlass`）。控件自己不要再铺不透明底色。
void mark(QWidget *widget, Kind kind);
QString kindPropertyName();
/// 这个控件要不要画玻璃、画哪一档（只看自己身上的属性，不看开关）。
bool kindOf(const QWidget *widget, Kind *out);

/// 在 QApplication 上装绘制过滤器（产品与验证工程各装一次；重复调用无害）。
void install(QApplication &app);

/// 每一档的圆角（逻辑像素）：Chrome / Bar 0、Panel 16、Card 14、Inset 10。
int radius(Kind kind);
/// 每一档的着色层（带 alpha）。alpha 按当前场景现算，见文件头。
QColor tint(Kind kind);

/// 画一块玻璃：`rect` 是 `widget` 自己坐标里的那一块。找不到场景底（弹窗、独立窗口）时
/// 退回同色不透明底。
void paint(QPainter &painter, const QWidget *widget, const QRectF &rect, Kind kind);

// ---- 对比度那一套（也给 ui_check 用） ----

/// WCAG 相对亮度对比度（1~21）。
double contrast(const QColor &a, const QColor &b);
/// `top`（带 alpha）盖在不透明的 `bottom` 上的结果。
QColor over(const QColor &top, const QColor &bottom);
/// 让每一种墨色（色, 需要的对比度）在 `backgrounds` 每一种底上都达标的最小 alpha（不低于 `floor`）。
/// 怎么都达不到时返回 1.0（完全不透明）。
qreal minimumTintAlpha(const QColor &tint, const QList<QPair<QColor, double>> &inks,
                       const QList<QColor> &backgrounds, qreal floor);

/// 三遍盒式模糊（半径按像素），给场景副本用。
QImage boxBlur(const QImage &source, int radius);

/// 主窗口的中央控件：画场景底，持有两份缓存。
class Backdrop : public QWidget {
    Q_OBJECT

public:
    explicit Backdrop(QWidget *parent = nullptr);
    ~Backdrop() override;

    /// `widget` 所在窗口的那一块场景底（没有就是空）。
    static Backdrop *of(const QWidget *widget);
    /// 当前正在用的那一块（着色层 alpha 现算要用它的明暗极值）。
    static Backdrop *active();

    /// 模糊副本（设备像素比已设好，按逻辑坐标贴）。尺寸还没重算完时是旧尺寸那一张。
    const QPixmap &blurred() const { return m_blurred; }
    /// 模糊副本是按多大的逻辑尺寸算的（拖窗口时与 `size()` 暂时不一致）。
    QSize cacheSize() const { return m_cacheSize; }
    /// 模糊副本里最暗、最亮的那两种颜色。
    QColor darkest() const { return m_darkest; }
    QColor brightest() const { return m_brightest; }

    /// 立刻按当前尺寸与主题重算（验证工程截图前用；产品里走防抖）。
    void rebuildNow();

    /// 场景底在 `widget` 那一块上最亮的颜色（清晰图，不是模糊副本）—— 直接压在风景上的
    /// 标题拿它挑墨色、验对比度。
    QColor brightestUnder(const QWidget *widget) const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QImage sourceImage();
    void scheduleRebuild();
    void paintShadows(QPainter &painter);

    QImage m_sharpSmall;  // 清晰图缩到 1/8（量标题底下的明暗用）

    QTimer *m_debounce = nullptr;
    QString m_sourceKey;
    QImage m_source;
    QPixmap m_sharp;
    QPixmap m_blurred;
    QSize m_cacheSize;
    QString m_cacheKey;
    QColor m_darkest = QColor(Qt::black);
    QColor m_brightest = QColor(Qt::white);
};

}  // namespace Glass
