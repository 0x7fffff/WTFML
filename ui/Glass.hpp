/**
 * @file Glass.hpp
 * @brief 整窗磨砂：窗口底那一层（系统背板 / 风景预模糊 / 纯色）与主页那张全出血场景。
 *
 * 整个主窗口只有**一层**底，由中央控件 `Glass::Backdrop` 画；页面内容直接排在它上面，
 * 不再给侧栏、正文、小节各铺一块面（分组靠留白、小标题、细线）。四档：
 * - 亚克力 / 云母：Win11 22H2 起的 DWM 系统背板（`ui/WindowChrome` 把它贴到 HWND 上，失焦也保持激活态），
 *   这里只铺一层半透明令牌色；alpha 按系统背板可能的明暗区间现算（`ui/SystemBackdrop`），保证正文 ≥ 4.5:1。
 * - 风景底：一张风景缩小、盒式模糊、往令牌色混一点，再盖一层令牌色（alpha 按模糊图的明暗极值现算）。
 *   系统背板用不了（Win10、非 windows 平台）时亚克力 / 云母落到这一档。
 * - 纯色：不透明令牌。系统开了高对比度或关了「透明效果」时，前三档都落到这一档。
 *
 * 截图（`QWidget::grab()`）拍不到 DWM 合成的那一层，所以往位图里画时按风景底画。
 */

#pragma once

#include <QColor>
#include <QImage>
#include <QList>
#include <QPair>
#include <QPixmap>
#include <QPointer>
#include <QWidget>

class QApplication;
class QPainter;
class QTimer;

namespace Glass {

enum class Mode { Acrylic, Mica, Scene, Solid };

/// QSettings 键 `Theme/Backdrop`，值 acrylic / mica / scene / solid。环境变量
/// `WTFML_BACKDROP` 同样的四个词，优先于设置（强制走回退、截图核对用）。
QString settingsKey();
QString modeKey(Mode mode);
QString modeName(Mode mode);
/// 用户选的那一档。
Mode mode();
/// 实际生效的那一档（按系统能力与无障碍设置回退之后）。
Mode effectiveMode();
/// 为什么没按用户选的来（生效的就是选的那档时为空）。
QString fallbackReason();
/// 改档：落盘，然后重新铺一遍主题（样式表、调色板、所有控件重画、外框重贴）。
void setMode(Mode mode);

/// 生效的不是纯色。
bool enabled();
/// 主页那张清晰的场景图画不画（只有系统高对比度时不画）。主页上的字按它选白墨还是正文色。
bool sceneShown();
/// 生效的是 DWM 系统背板（亚克力或云母）。
bool systemBackdrop();

/// 着色浓度、风景模糊程度（0~100），存在 `Theme/BackdropTint`、`Theme/BackdropBlur`。
/// 浓度只往上加：对比度要的下限由现算保证，用户调到 0 也不会低于它。
inline constexpr int kDefaultTintStrength = 20;
int tintStrength();
int blurStrength();
void setTintStrength(int value);
void setBlurStrength(int value);

/// 产品与验证工程各调一次：把场景图所在的 qrc 拉进来。
void install(QApplication &app);

// ---- 对比度那一套（也给 ui_check 用） ----

/// WCAG 相对亮度对比度（1~21）。
double contrast(const QColor &a, const QColor &b);
/// `top`（带 alpha）盖在不透明的 `bottom` 上的结果。
QColor over(const QColor &top, const QColor &bottom);
/// 让每一种墨色（色, 需要的对比度）在 `backgrounds` 每一种底上都达标的最小 alpha（不低于 `floor`）。
/// 怎么都达不到时返回 1.0（完全不透明）。
qreal minimumTintAlpha(const QColor &tint, const QList<QPair<QColor, double>> &inks,
                       const QList<QColor> &backgrounds, qreal floor);
/// 亚克力 / 云母在当前深浅下可能落到的颜色：背后是纯黑、纯白、六个纯色时配方合出来的那些
/// （`SystemBackdrop::composite`，用来现算着色 alpha）。
QList<QColor> systemBackdropRange();
/// 正文墨色 4.5、次要墨色 4.5（次要字也是正文字号）。
QList<QPair<QColor, double>> bodyInks();

/// 三遍盒式模糊（半径按像素），给场景副本用。
QImage boxBlur(const QImage &source, int radius);

/// 主窗口的中央控件：画整窗那一层底和主页场景，持有缓存。
class Backdrop : public QWidget {
    Q_OBJECT

public:
    explicit Backdrop(QWidget *parent = nullptr);
    ~Backdrop() override;

    static Backdrop *of(const QWidget *widget);
    static Backdrop *active();

    /// 主页场景铺在哪一块：这个控件可见时，它占的那一块画清晰风景（左沿 32 px 渐隐进导航轨）。
    void setHeroWidget(QWidget *widget);
    /// 主页场景被整页盖住（覆盖层整页 / 铺满的提示开在主页上）：整窗改回磨砂底，
    /// 否则轨和顶栏还是清晰风景、内容区却是磨砂，看着是三块拼起来的。
    void setHeroCovered(bool covered);
    bool heroShown() const { return !heroRect().isEmpty(); }

signals:
    /// 主页场景露没露（主页进出、被整页盖住 / 揭开）：导航轨据此换主页字色。
    void heroShownChanged(bool shown);

public:

    /// 当前这一刻整窗底铺的着色层（带 alpha）。
    QColor tint() const;
    /// 风景底模糊副本的明暗极值（着色 alpha 现算用）。
    QColor darkest() const { return m_darkest; }
    QColor brightest() const { return m_brightest; }

    /// 立刻按当前尺寸与主题重算（验证工程截图前用；产品里走防抖）。
    void rebuildNow();

    /// 往 `target`（同一扇窗里的控件）上画它那一块整窗底，不画主页场景。覆盖层整页、独立小窗、浮层借它透出同一层磨砂。
    /// `toPixmap`：这一趟画进位图（grab），系统背板拍不进去，按风景底画。
    void paintBase(QPainter &painter, const QWidget *target, bool toPixmap);
    /// 独立顶层窗（游戏日志、各对话框）铺底：和主窗口同一档 —— 亚克力 / 云母只写着色层（真透桌面），
    /// 风景档把整张磨砂风景缩进 `rect` 再盖同一层着色。
    void paintScene(QPainter &painter, const QRect &rect);

    /// 主页场景在 `widget` 那一块上最亮的颜色 —— 直接压在风景上的标题拿它验对比度。
    QColor brightestUnder(const QWidget *widget) const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QImage sourceImage();
    void scheduleRebuild();
    QRect heroRect() const;
    void rebuildHero();

    QTimer *m_debounce = nullptr;
    QPointer<QWidget> m_hero;
    QString m_sourceKey;
    QImage m_source;
    QPixmap m_frost;       // 风景底：模糊 + 混色后的整窗图
    QSize m_frostSize;
    QString m_frostKey;
    QPixmap m_heroPixmap;  // 主页场景：清晰图 + 暗角 + 左沿渐隐
    QImage m_heroSmall;    // 主页场景缩小版（量标题底下的明暗）
    QRect m_heroCacheRect;
    bool m_heroCovered = false;
    bool m_heroWasShown = false;
    void noteHeroShown();
    QString m_heroKey;
    QColor m_darkest = QColor(Qt::black);
    QColor m_brightest = QColor(Qt::white);
};

}  // namespace Glass
