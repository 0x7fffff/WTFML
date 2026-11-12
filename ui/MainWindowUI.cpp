/**
 * @file MainWindowUI.cpp
 * @brief 实现 UI 构建器类，实际创建主窗口的控件、布局并设置窗口属性。
 * @note 完成主窗口所有 UI 元素的初始化与排列，供 MainWindow 构造函数调用。
 */

#include "MainWindowUI.hpp"
#include "MainWindowPageLayout.hpp"

#include "TaskCenterPanel.hpp"
#include "AgentPage.hpp"
#include "Animations.hpp"  // 换页 / 展开折叠 / 上拉面板那几段动效
#include "ElidedLabel.hpp"
#include "Glass.hpp"
#include "InstanceGroupDelegate.hpp"
#include "MemorySlider.hpp"
#include "OverlayHost.hpp"
#include "PopupPlacement.hpp"
#include "SkinModel.hpp"
#include "TaskProgressBar.hpp"
#include "Theme.hpp"
#include "ThemeAdvancedEditor.hpp"
#include "ThemeBackground.hpp"
#include "TitleBar.hpp"
#include "ToolboxPage.hpp"  // 工具箱那张表的键与名字（底栏面板那六颗按钮按 tools() 摆）
#include "VersionIcons.hpp"
#include "VersionTreeDelegate.hpp"
#include "WindowChrome.hpp"

#include <QColor>
#include <QEvent>
#include <QFont>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QPointer>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QScreen>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSettings>
#include <QStyle>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QResizeEvent>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSet>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

using namespace MainWindowPageLayout;

namespace {

/// 主页快捷卡片点开的那一块小面板（`homeQuickPopup`）。
///
/// 为什么不是裸的 `QFrame(..., Qt::Popup)`：**点面板外面收掉它的那一下会被重放一遍**。
/// Qt 关一块 `Qt::Popup` 时，如果光标不在它的几何里，会把这同一个 press 重放给光标底下
/// 那个控件（`QApplicationPrivate::closePopup()` → `replayMousePress` →
/// `QWidgetWindow::handleMouseEvent` 尾巴上那段；Windows 上那个 style hint 是 true）。
/// 底下正好是「列表 / 返回」时，重放那一下就把实例列表收了 —— 用户看到的症状是"点详情
/// 之后上方压缩失效、整块长回去"（列表收起后本来就该是那个高度）。同一份日志里连开两块
/// 面板、隔着面板换「最近 / 收藏」，也都是这发重放打到卡片和标签上。
///
/// 底栏两块面板 / 文件夹齿轮菜单 / 实例设置里那些框是**同一道闸**，注释放的那几处见
/// `MainWindowPanels.cpp`（`ToolboxPopup`、`FolderMenu::handleAnchorPress`）。
///
/// NOTE: 属性**必须在 close() 之前钉** —— Qt 是关窗那一刻读它（`hide_helper`）；而且它
/// 会被 Qt 在下一次把鼠标事件投给这个控件时清掉（`qapplication.cpp` 里
/// `setAttribute(WA_NoMouseReplay, false)`），所以挂在构造里没用，只能在外面那一下按需要钉。
class HomeQuickPopup final : public QFrame {
public:
    using QFrame::QFrame;

protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (rect().contains(event->position().toPoint())) {
            QFrame::mousePressEvent(event);  // 落在面板自己身上：照旧往下走
            return;
        }
        // 外面那一下 = "面板该收了"。钉上再收：既收了面板，也不让底下的控件跟着挨一下。
        setAttribute(Qt::WA_NoMouseReplay, true);
        close();
        event->accept();
    }
    void mouseDoubleClickEvent(QMouseEvent *event) override { mousePressEvent(event); }
};
/// 主页压在风景上的字色：场景画着时是 heroInk / heroInk2（设计稿 --hero-ink / --hero-ink-2），
/// 高对比度（没有场景）回到正文 / 次级色。
QColor heroInk(bool secondary) {
    const Theme::Tokens t = Theme::tokens();
    if (!Glass::sceneShown()) return secondary ? t.secondary : t.text;
    return secondary ? t.heroInk2 : t.heroInk;
}

/// 让 @p w 的字跟着 heroInk 走（换主题 / 场景开关时重取）。
void followHeroInk(QWidget *w, bool secondary) {
    const auto apply = [w, secondary]() {
        QPalette p = w->palette();
        p.setColor(QPalette::WindowText, heroInk(secondary));
        p.setColor(QPalette::ButtonText, heroInk(secondary));
        p.setColor(QPalette::Link, heroInk(secondary));  // 文字按钮（Ghost）的字走 Link
        w->setPalette(p);
    };
    apply();
    Theme::follow(w, apply);
}

/// 把 @p font 换成手写楷体、按像素定号并加字距（设计稿 --hand）。
QFont handPx(QFont font, Theme::HandFont which, int px, qreal spacing) {
    font = Theme::handFont(font, which);
    font.setPixelSize(px);
    font.setLetterSpacing(QFont::AbsoluteSpacing, spacing);
    return font;
}

/// 主页压在风景上的那几颗"玻璃"按钮：账号胶囊、启动区的切换 / 设置、实例带上的卡片和那颗
/// 悬停才出来的播放钮。底色是场景暗角色（heroVeil）按设计稿的浓度叠上去 + 1px glass-edge
/// 内描边（CSS `backdrop-filter` 那层模糊 Qt 里没有，见差异清单）；没有场景时退回中性淡底。
/// 颜色在绘制时现取，换主题不必重挂样式表；尺寸仍归 QSS（min-height / max-height）。
class HeroButton final : public QPushButton {
public:
    enum class Shape { Pill, Square, Tile, Play };
    HeroButton(Shape shape, QWidget *parent) : QPushButton(parent), m_shape(shape) {
        setAttribute(Qt::WA_Hover, true);
        setCursor(Qt::PointingHandCursor);
    }
    /// 卡片上那颗只在悬停 / 键盘焦点时出现的子控件（设计稿：悬停才出播放钮）。
    void setRevealOnHover(QWidget *child) {
        m_reveal = child;
        syncReveal();
    }
    void setDashed(bool dashed) { m_dashed = dashed; }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const Theme::Tokens t = Theme::tokens();
        const bool scene = Glass::sceneShown();
        const bool hot = isEnabled() && (underMouse() || isDown());
        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = m_shape == Shape::Pill || m_shape == Shape::Play ? box.height() / 2 : 14.0;
        if (m_shape == Shape::Play) {
            p.setPen(Qt::NoPen);
            p.setBrush(isDown() ? t.primaryPressed : underMouse() ? t.primaryHover : t.primary);
            p.drawEllipse(box);
        } else {
            qreal alpha = m_shape == Shape::Pill ? 0.38 : m_shape == Shape::Square ? 0.40 : 0.0;
            if (m_shape == Shape::Tile && (hot || hasFocus())) alpha = 0.42;
            if (hot) alpha += isDown() ? 0.18 : 0.10;
            if (alpha > 0 || m_dashed) {
                QColor fill = scene ? t.heroVeil : t.subtle;
                if (scene) fill.setAlphaF(qMin(1.0, alpha));
                else if (hot) fill = t.hover;
                p.setBrush(alpha > 0 ? QBrush(fill) : Qt::NoBrush);
                QPen edge(m_dashed ? heroInk(true) : (scene ? t.glassEdge : t.stroke));
                if (m_dashed) {
                    QColor c = edge.color();
                    c.setAlphaF(c.alphaF() * 0.45);
                    edge.setColor(c);
                    edge.setStyle(Qt::DashLine);
                }
                edge.setWidthF(1.0);
                p.setPen(alpha > 0 || m_dashed ? edge : QPen(Qt::NoPen));
                p.drawRoundedRect(box, radius, radius);
            }
        }
        if (!icon().isNull()) {
            const QSize s = iconSize();
            const QRect r(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s);
            icon().paint(&p, r);
        }
        if (hasFocus() && property("wtfmlKeyboardFocus").toBool()) {
            QPen ring(Theme::focusRingColor(palette()));
            ring.setWidthF(2.0);
            p.setPen(ring);
            p.setBrush(Qt::NoBrush);
            const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
            p.drawRoundedRect(r, radius - 1, radius - 1);
        }
    }
    void enterEvent(QEnterEvent *event) override {
        QPushButton::enterEvent(event);
        syncReveal();
    }
    void leaveEvent(QEvent *event) override {
        QPushButton::leaveEvent(event);
        syncReveal();
    }
    void focusInEvent(QFocusEvent *event) override {
        QPushButton::focusInEvent(event);
        syncReveal();
    }
    void focusOutEvent(QFocusEvent *event) override {
        QPushButton::focusOutEvent(event);
        syncReveal();
    }

private:
    void syncReveal() {
        if (m_reveal != nullptr) m_reveal->setVisible(underMouse() || hasFocus());
        update();
    }
    Shape m_shape;
    QPointer<QWidget> m_reveal;
    bool m_dashed = false;
};

/// 实例带上的「最近 / 收藏」页签（设计稿 .tab）：手写楷体 15，平时 heroInk2，选中 heroInk +
/// 字下方 16×2 的强调色短线。自绘：QSS 的下边框只能铺满整颗按钮宽。
class HomeStripTab final : public QPushButton {
public:
    using QPushButton::QPushButton;
    QSize sizeHint() const override {
        const QFontMetrics fm(font());
        return QSize(fm.horizontalAdvance(text()) + 16, fm.height() + 12 + 4);
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRect textRect = rect().adjusted(0, 0, 0, -4);
        p.setPen(heroInk(!isChecked() && !underMouse()));
        p.setFont(font());
        p.drawText(textRect, Qt::AlignCenter, text());
        if (isChecked()) {
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::tokens().deep);
            const QFontMetrics fm(font());
            const int baseline = textRect.center().y() + fm.height() / 2;
            p.drawRoundedRect(QRectF(width() / 2.0 - 8, baseline + 4, 16, 2), 1, 1);
        }
        if (hasFocus() && property("wtfmlKeyboardFocus").toBool()) {
            QPen ring(Theme::focusRingColor(palette()));
            ring.setWidthF(2.0);
            p.setPen(ring);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 8, 8);
        }
    }
};

/// 实例带那一排卡片的"取景框"：卡片按自己的宽度一张挨一张排，排不下就从右边裁掉，
/// 最后 120px 渐隐（设计稿 `.cards { overflow: hidden; mask-image: … }`）。
/// 里面那一排是个普通的 QHBoxLayout（`row()`），重建卡片的代码照旧往里塞。
class HomeStripRow final : public QWidget {
public:
    explicit HomeStripRow(QWidget *parent) : QWidget(parent), m_content(new QWidget(this)) {
        m_row = new QHBoxLayout(m_content);
        m_row->setContentsMargins(0, 0, 0, 0);
        m_row->setSpacing(6);
        m_row->setSizeConstraint(QLayout::SetFixedSize);
        m_content->installEventFilter(this);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    QHBoxLayout *row() const { return m_row; }
    QSize sizeHint() const override { return QSize(0, m_content->sizeHint().height()); }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void resizeEvent(QResizeEvent *event) override {
        QWidget::resizeEvent(event);
        relayout();
    }
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (watched == m_content && event->type() == QEvent::Resize) {
            if (m_content->height() != height()) updateGeometry();
            syncFade();
        }
        return QWidget::eventFilter(watched, event);
    }

private:
    void relayout() {
        m_content->move(0, 0);
        syncFade();
    }
    void syncFade() {
        const bool overflow = m_content->width() > width() && width() > 160;
        auto *effect = qobject_cast<QGraphicsOpacityEffect *>(graphicsEffect());
        if (!overflow) {
            if (effect != nullptr) setGraphicsEffect(nullptr);
            return;
        }
        if (effect == nullptr) {
            effect = new QGraphicsOpacityEffect(this);
            effect->setOpacity(1.0);
            setGraphicsEffect(effect);
        }
        QLinearGradient mask(0, 0, width(), 0);
        mask.setColorAt(0.0, Qt::black);
        mask.setColorAt(qMax(0.0, 1.0 - 120.0 / width()), Qt::black);
        mask.setColorAt(1.0, Qt::transparent);
        effect->setOpacityMask(mask);
    }
    QWidget *m_content;
    QHBoxLayout *m_row;
};

/// 只在窗口尺寸变化时跑一段函数（主页窄窗那一档：标题收一号、立绘缩小）。
class ResizeHook final : public QObject {
public:
    ResizeHook(QWidget *watched, std::function<void()> fn) : QObject(watched), m_fn(std::move(fn)) {
        watched->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show) m_fn();
        return false;
    }

private:
    std::function<void()> m_fn;
};

/// 实例带卡片下面那行小字（设计稿 .cv）："Minecraft 1.21.4 / Fabric 0.16.9" → "Fabric 1.21.4"。
QString homeTileSubtitle(const QString &detail) {
    const QString line = detail.section(QLatin1Char('\n'), 0, 0);
    const QString game = line.section(QStringLiteral(" / "), 0, 0).remove(QStringLiteral("Minecraft ")).trimmed();
    const QString loader = line.section(QStringLiteral(" / "), 1, 1).section(QLatin1Char(' '), 0, 0).trimmed();
    if (game.isEmpty()) return line;
    return loader.isEmpty() ? game : loader + QLatin1Char(' ') + game;
}

/// 启动区那一行元信息（设计稿 .meta）：加载器 · Minecraft 版本 · 上次游玩 · 模组数。
QString homeLaunchMeta(const MainWindowUI::HomeQuickItem &item) {
    const QString versionLine = item.detail.section(QLatin1Char('\n'), 0, 0);
    const QString activityLine = item.detail.section(QLatin1Char('\n'), 1, 1);
    QStringList parts;
    const QString game = versionLine.section(QStringLiteral(" / "), 0, 0).trimmed();
    const QString loader = versionLine.section(QStringLiteral(" / "), 1).trimmed();
    if (!loader.isEmpty()) parts << loader;
    if (!game.isEmpty()) parts << game;
    QString when = activityLine.section(QStringLiteral(" / "), 0, 0).trimmed();
    when.replace(QStringLiteral("上次启动："), QStringLiteral("上次游玩 "));
    if (!when.isEmpty()) parts << when;
    if (item.modCount > 0) parts << QStringLiteral("%1 个模组").arg(item.modCount);
    return parts.join(QStringLiteral("  ·  "));
}

/// 实例带卡片左边那块 48×48 封面：圆角暗底 + 居中的实例图标。
/// 没挑图标的实例：按名字稳定配色的等距方块（设计稿 cover）。
QPixmap homeTileCover(const QIcon &icon, const QString &name, int side) {
    const qreal dpr = qApp != nullptr ? qApp->devicePixelRatio() : 1.0;
    QPixmap cover(QSize(side, side) * dpr);
    cover.setDevicePixelRatio(dpr);
    cover.fill(Qt::transparent);
    QPainter p(&cover);
    p.setRenderHint(QPainter::Antialiasing, true);
    const Theme::Tokens t = Theme::tokens();
    QColor fill = Glass::sceneShown() ? t.heroVeil : t.subtle;
    if (Glass::sceneShown()) fill.setAlphaF(0.40);
    p.setPen(QPen(Glass::sceneShown() ? t.glassEdge : t.stroke, 1.0));
    p.setBrush(fill);
    p.drawRoundedRect(QRectF(0.5, 0.5, side - 1, side - 1), 12, 12);
    const int inner = side * 7 / 12;
    const QPixmap mark = icon.isNull() ? VersionIcons::isoBlock(VersionIcons::isoBlockKindFor(name), inner)
                                      : icon.pixmap(inner, inner);
    p.drawPixmap(QRect((side - inner) / 2, (side - inner) / 2, inner, inner), mark);
    return cover;
}
} // namespace

MainWindowUI::MainWindowUI(QMainWindow *parent) { setupUi(parent); }

const MainWindowUI::ThemeImageRow &MainWindowUI::getThemeImageRow(int index) const {
    // 越界**不崩**：那几行的下标是调用方自己数出来的（一共两行），而这一层在
    // 构造之后才被问到 —— 给一个空行（指针全是 nullptr）比断言更好，调用方本来就要判空
    // （见头文件那句"调用方自己判空"）。
    static const ThemeImageRow kEmpty;
    if (index < 0 || index >= m_themeImageRows.size()) {
        return kEmpty;
    }
    return m_themeImageRows.at(index);
}







QString MainWindowUI::runningDurationText(qint64 startedAtMs, qint64 nowMs) {
    // 负数（时钟被往回调过、或者 startedAtMs 是个没写过的 0）当"刚开始"：这一行是个说明，
    // 不该出现"已运行 -3 分钟"这种算术漏出来的话。
    const qint64 seconds = qMax<qint64>(0, (nowMs - startedAtMs) / 1000);
    if (seconds < 60) {
        return QCoreApplication::translate("MainWindowUI", "已运行不到 1 分钟");
    }
    const qint64 minutes = seconds / 60;
    if (minutes < 60) {
        return QCoreApplication::translate("MainWindowUI", "已运行 %1 分钟").arg(minutes);
    }
    const qint64 hours = minutes / 60;
    if (hours < 24) {
        const qint64 rest = minutes % 60;
        // 整点就不缀那半句："已运行 2 小时 0 分钟"读起来是台机器写的。
        return rest == 0 ? QCoreApplication::translate("MainWindowUI", "已运行 %1 小时").arg(hours)
                         : QCoreApplication::translate("MainWindowUI", "已运行 %1 小时 %2 分钟")
                               .arg(hours)
                               .arg(rest);
    }
    const qint64 days = hours / 24;
    // 再往上一档（"N 个月"）没有意义：一个实例连着跑好几天已经是要不要收掉的问题了，
    // 具体几天几小时够用。
    return QCoreApplication::translate("MainWindowUI", "已运行 %1 天 %2 小时")
        .arg(days)
        .arg(hours % 24);
}

bool MainWindowUI::isHomeTabCurrent() const {
    // 主页恒定是第一页（下面 addTab 的顺序就是契约），所以比下标就够，
    // 不用再存一份页面指针。
    return tabWidget != nullptr && tabWidget->currentIndex() == homeTabIndex();
}

void MainWindowUI::setupUi(QMainWindow *parent) {
    parent->setWindowTitle(QStringLiteral("WTF Minecraft Launcher"));
    // 设计稿主尺寸 1280×800；屏幕放不下就按可用区缩（最小 900×620，窄于 1024 时导航轨自动折）。
    {
        QSize size(1280, 800);
        if (const QScreen *screen = QGuiApplication::primaryScreen()) {
            const QSize room = screen->availableGeometry().size() * 0.9;
            size = size.boundedTo(room).expandedTo(QSize(kMainWindowMinWidth, kMainWindowMinHeight));
        }
        parent->resize(size);
    }
    parent->setMinimumSize(kMainWindowMinWidth, kMainWindowMinHeight);

    parent->setWindowFlags(parent->windowFlags() | Qt::WindowMaximizeButtonHint);

    // 中央控件就是整窗那一层底（系统背板 / 风景预模糊底 / 纯色，见 ui/Glass.hpp）。
    // 去套娃：底只有这一层，导航轨和页面都直接铺在它上面，不再各自垫面板。
    auto *backdrop = new Glass::Backdrop(parent);
    QWidget *centralWidget = backdrop;
    parent->setCentralWidget(centralWidget);

    // 左 = 导航轨；右 = 顶上一条透明的窗口按钮条 + 页面。
    auto *shellLayout = new QHBoxLayout(centralWidget);
    shellLayout->setContentsMargins(0, 0, 0, 0);
    shellLayout->setSpacing(0);
    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ================= 顶上那条自己画的标题栏 =================
    // 系统那条白色标题栏由 WindowChrome 去掉（客户区铺满整窗，见它的文件头），
    // 顶上这一行就是替它的：左边草方块 + 启动器名字（默认是 `TitleBar::defaultBrandText()`
    // 那五个字母，设置页「个性化」栏的「外观」里可以改），
    // 然后是**五个功能的导航按钮**（下面 addTab 完之后 setNavItems() 灌进去），
    // 右边最小化与关闭。第一行就是它、第二行才是标签页 —— 顺序由 addWidget 的先后决定。
    //
    // NOTE: 覆盖层（OverlayHost）挂在 tabWidget 上、条在 tabWidget 上面那一行，所以开设置页
    // 时这条**不会被盖住** —— 和"底下那一条不用覆盖"是同一条规矩；
    // "顶上四个选项卡也不覆盖"由"四个功能在条上、条不被覆盖"接手。
    bar = new TitleBar(centralWidget);
    shellLayout->addWidget(bar);
    shellLayout->addLayout(mainLayout, 1);
    bar->captionStrip()->setParent(centralWidget);
    mainLayout->addWidget(bar->captionStrip());
    // 条只发信号、不自己动手（见 TitleBar.hpp）：谁接这两下是这一层的事。
    // NOTE: 这个类**不是 QObject**（没有 tr()、没有自己的信号槽），所以得写全 `QObject::connect`。
    QObject::connect(bar, &TitleBar::minimizeRequested, parent, &QMainWindow::showMinimized);
    QObject::connect(bar, &TitleBar::closeRequested, parent, &QMainWindow::close);
    QObject::connect(bar, &TitleBar::maximizeRequested, parent, [parent]() {
        parent->isMaximized() ? parent->showNormal() : parent->showMaximized();
    });

    // ================= 选项卡：主页 / 管理 / 下载 / 设置 / 智能体 =================
    // 四页各自只放自己那摊控件，都排成左右两栏：左栏是要操作的那点东西
    // （选版本、按钮），右栏是列表或一整摊设置项。份数按页不同，见 leftColumnIsWider()。
    // 注意"选版本"这件事现在分在两个地方：「已安装版本」那个**下拉框**在主页、只管启动；
    // 下载页上没有下拉框，是**一棵版本树**（点分类展开、点一个版本号进安装页装它）。
    // 两边本来就是互不相干的数据源（一个是盘上装好的、一个是清单里能下的）。
    tabWidget = new QTabWidget(centralWidget);
    tabWidget->setObjectName(tabWidgetName());
    // QTabWidget 默认把所有页面里最大的 minimumSizeHint 当成自己的下限。下载页那棵
    // 固定行数的版本树会把它顶到 568px，再加标题栏和底栏后整窗被强制到 710px；即使
    // 当前停在主页也缩不下去，只能在上半部留下大块空白。竖直方向忽略这个跨页提示，
    // 让当前窗口尺寸说了算；各页已有自己的滚动区域，空间不足时由页面内部处理。
    tabWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);

    // ---------- 四页：主页 / 实例 / 下载 / 设置 ----------
    //
    // 四个页面都是 `ThemeBackground`：其他页面的背景也能换图（`Theme::Part::PageBackground`）。
    // 没设图时它画的就是这一页的底色，一个像素都不差（见 ui/ThemeBackground.hpp 的文件头）。
    // 每一页都在自己的 `setup*Page()` 里搭（见下面那几个函数），这里只按顺序摆出来。
    QWidget *homeTab = setupHomePage();
    QWidget *instanceTab = setupInstancePage();
    QWidget *downloadTab = setupDownloadPage();
    QWidget *settingsTab = setupSettingsTab();
    agentPage = new AgentPage(tabWidget);

    // NOTE: 顺序就是契约：homeTabIndex() == 0 靠的就是"主页第一个 addTab"。
    tabWidget->addTab(homeTab, QStringLiteral("主页"));
    tabWidget->addTab(instanceTab, QStringLiteral("管理"));
    tabWidget->addTab(downloadTab, QStringLiteral("下载"));
    tabWidget->addTab(settingsTab, QStringLiteral("设置"));
    // D1：智能体**放最后**；启动默认仍停在主页（下面 setCurrentIndex(homeTabIndex())）。
    tabWidget->addTab(agentPage, QStringLiteral("智能体"));
    tabWidget->setCurrentIndex(homeTabIndex());
    mainLayout->addWidget(tabWidget, 1);

    // ================= 那四个选项卡放在顶上那一条上 =================
    // 四个选项卡放在最顶上，通过顶端按钮打开四个功能。
    //
    // 做法是"条上一排按钮 + 把 QTabWidget 自己那条标签栏藏起来"，而不是把 QTabBar 搬出来：
    // 标签栏的几何归 QTabWidget 管（和内容区那条接缝、圆角都是它画的），搬出来只会得到一个
    // 位置还会被拽回去的条。藏起来的后果是好的那一半 —— Qt 的 `initStyleOption()` 里那句
    // `if (d->tabs->isVisibleTo(this))` 一判，`tabBarSize` 归零，于是 `contentsRect` 变成
    // 整个 QTabWidget：**内容页铺满整块**（所以覆盖层也跟着铺满，见 OverlayHost）。
    tabWidget->tabBar()->hide();
    // 标签**就是** tabWidget 自己的那一份：名字、顺序、条数都只有一处出处。
    // NOTE: 这一句必须在上面四个 addTab **之后** —— tabText() 这会儿才有东西。
    QStringList navLabels;
    for (int i = 0; i < tabWidget->count(); ++i) {
        navLabels << tabWidget->tabText(i);
    }
    bar->setNavItems(navLabels);
    bar->setCurrentNav(tabWidget->currentIndex());
    // 条只发信号、不自己动手（见 TitleBar.hpp）：翻页是这一层的事。
    QObject::connect(bar, &TitleBar::navRequested, tabWidget, [this](int index) {
        if (navigationGuard && !navigationGuard()) return;
        const int sourceIndex = tabWidget->currentIndex();
        const bool reselected = index == sourceIndex;
        // 点上方那四大钮时要**收起任务中心**，不能卡在别的页上没反应。
        // 任务中心是**整页覆盖**那一档：它在自己的选项卡上是摊开的，切到别的选项卡上
        // 只是"收着"（OverlayHost::parked()），而那一位**还占着覆盖层**。于是：
        // · 在别的选项卡上点导航 —— `dismiss()` 对"收着"的页什么都不做（它按设计不
        // 替用户收看不见的页），任务中心原样占着那一位；
        // · 再回到原来那一页 —— 它又摊开，看着像"哪颗都没反应、页面卡住了"。
        // 所以点导航这一下先把它**收掉**（evict 连"收着"那一档一起收），再照常翻页 /
        // dismiss。只认任务中心：别的覆盖页（资源详情之类）仍按同一条规矩停放、切回来还在。
        // NOTE: `pageOpen()` 那一条是"**最上面**那一层是页"的意思 —— 页上面压着一句提示
        // 的时候不在这儿收（那句话是要人回答的，不能因为切个页就替他答"取消"）。
        if (taskCenterPanel() != nullptr && overlayHost != nullptr && overlayHost->promptOpen() &&
            overlayHost->promptFullSize()) {
            overlayHost->dismiss();
        } else if (taskCenterPanel() != nullptr && overlayHost != nullptr && overlayHost->pageOpen()) {
            overlayHost->evict();
        }
        // 顶端四个按钮点下去要**自动到达对应选项卡的首页**（比如正开着 sodium 的下载页，
        // 点启动选项卡就切过去、下载那一层自动收起）；在下载页点顶上那个「下载」（不是
        // 下载确认键）也回到下载页首页，四个都适用。于是这一颗有两种意思：
        //
        // ① 点的**不是**现在这一颗 ⇒ 翻页。切走之后那页不会丢，只是"收着"（见
        // OverlayHost.hpp 那段），切回来还是原来那个界面、连滚到哪儿都在；
        // ② 点的**就是**现在这一颗 ⇒ "回到这个功能的首页"：把上面那一层收掉 —— 页面那一档
        // 走"返回箭头"那一发（收页要经过页面自己的 reject，理由见 dismiss()）。
        if (reselected) {
            if (overlayHost != nullptr) {
                overlayHost->dismiss();
            }
        } else {
            tabWidget->setCurrentIndex(index);
        }
        // 翻页 / dismiss 都已经发出后再报业务语义。比让 MainWindow 也监听
        // navRequested、并假设“UI 那条先 connect 所以先跑”更稳定。
        emit bar->navigationApplied(sourceIndex, index, reselected);
    });
    // 反方向也要通：别处（底栏、主页那几个入口）翻页之后，条上标着的也得跟着换。
    QObject::connect(tabWidget, &QTabWidget::currentChanged, bar, &TitleBar::setCurrentNav);
    QObject::connect(tabWidget, &QTabWidget::currentChanged, tabWidget, [this](int index) {
        if (index != homeTabIndex()) {
            closeHomeInstancePicker();
        }
    });
    auto *closePickerShortcut = new QShortcut(QKeySequence(Qt::Key_Escape),
                                              tabWidget->widget(homeTabIndex()));
    closePickerShortcut->setObjectName(QStringLiteral("homePickerCloseShortcut"));
    closePickerShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(closePickerShortcut, &QShortcut::activated, tabWidget,
                     [this] { closeHomeInstancePicker(); });

    // ================= 换页那一下 =================
    // 换页动效（「切换页面」）：新那一块**先等一拍、再淡进来**，
    // 顺带从下面抬 `Anim::pageRiseDistance()` 那么一小段。规矩（尤其是"一个像素的几何都
    // 不动"）见 ui/Animations.hpp。
    //
    // 这里动的**是内容区整个控件**（`tabWidget` 自己），**不是** `currentWidget()` 那一页。
    // 换页时整页的卡片要淡进来、不能瞬间出现。原因在这一层底下：那张卡片和它的外框**是 `QTabWidget`
    // 自己画的** —— 四个页面都是 `ThemeBackground(Part::PageBackground)`，而它在"没设图"
    // 时 `paintEvent()` 直接 return（见那个文件头上那一大段）。所以只给页面装效果的话，
    // 卡片和外框不在这张图里，淡的是"卡片上浮着的一层内容"，虚的那几帧能看见卡片那层底
    // 从中间透出来。
    //
    // 改的是**抬**那一半：淡的仍然是整块（卡片一起淡），但**往下挪着画的只有
    // 它里面的页面**（`ui/Animations.cpp` 里那个文件内的 `liftTarget()` 把它认出来）。
    // 有框的时候框会瞬移，所以抬的是内容、不是框 —— 卡片那圈外框换页前后都在同一
    // 个地方，整块一起挪就是**框自己跳了一下再滑回来**；内容是新上来的，没有参照物，反倒
    // 像正常的滑入。整段理由与做法见 `Animations.hpp` 里 `pageAppear()` 那段。
    //
    // NOTE: 垫的底色是**卡片那一层**（`QPalette::Base`）：内容抬起来让出来的那一条露的就是
    // 它。垫成窗口底色（`#f0f0f0`）的话，卡片上会横着一条偏灰的带子（卡片自己是
    // `#fcfcfc`，Fusion 画这一层用的就是 `Base`）。
    // NOTE: 挂的是 `currentChanged`，不是某一次点击 —— 四颗导航、底栏、主页那几个入口
    // 都会翻页，只有挂在这儿才一处管住全部（"唯一出处"那条规矩）。
    // NOTE: 只在**已经露着头**的时候动。搭界面这一路会挨个 `addTab()`，`currentChanged`
    // 在那时候就会响，而那些页还没上屏、几何也没定下来，给它们装效果纯属白干
    // （`tabWidget` 是 `centralWidget` 里生出来的，这条判据在它身上同样说得通）。
    QObject::connect(tabWidget, &QTabWidget::currentChanged, tabWidget, [this](int) {
        Anim::cancelLayoutTransition(tabWidget->parentWidget());
        if (overlayHost && overlayHost->promptOpen() && overlayHost->promptFullSize()) {
            if (TaskCenterPanel *panel = taskCenterPanel()) {
                QWidget *workspace = panel->parentWidget();
                overlayHost->closeOverlay();
                overlayHost->setPromptBlockedWidget(bottomBar);
                workspace->deleteLater();
            }
        }
        // 覆盖层页面在别的主选项卡上“停放”时，切回所属页会由 OverlayHost 立即恢复。
        // 此时若再给底下的 QTabWidget 盖一层换页幕布，幕布的 Base 色会短暂从页面周围
        // 露出来，形成用户看到的灰屏和一拍延迟。普通根页面仍有换页动画。
        if (tabWidget->isVisible()
            && (overlayHost == nullptr || !overlayHost->pageOpen())) {
            Anim::pageAppear(tabWidget, tabWidget->palette().color(QPalette::Base));
        }
    });

    // 内层那三层"换页"走同一条路（"切换页面"不只顶上那四颗）：
    // * 下载页右栏：游戏 / 模组 / 资源包 / 数据包 / 光影 / 整合包 之间横着换；
    // * 下载页里"游戏"那一栏自己还有一层（版本树 ↔ 安装页）；
    // * 设置页右栏：启动 / 游戏 / 提示 / 个性化 / 下载。
    // 都是 `QStackedWidget`，所以一条 `currentChanged` 就够了（谁换的都算）。
    //
    // 垫的底色是 `QPalette::Base`（**不是**顶上那一档的窗口底色）：这三层右栏每一页都
    // 活在一层滚动区里，视口铺的是 `Base` —— 垫窗口那份的话，起手那一帧会看见一整块
    // 偏灰的方块贴在页面正中（见 `Anim::pageAppear(widget, backdrop)` 那段）。
    // 这几页是滚动区：抬的会是它们的**视口**（`liftTarget()`，`ui/Animations.cpp` 里那个），滚动区自己那圈框
    // 留在原地 —— 和顶上那一档同一条规矩（见 `Animations.hpp` 里 `pageAppear()` 那段）。
    const auto appearOnSwitch = [](QStackedWidget *stack) {
        if (stack == nullptr) {
            return;
        }
        QObject::connect(stack, &QStackedWidget::currentChanged, stack, [stack](int) {
            QWidget *page = stack->currentWidget();
            if (page != nullptr && page->isVisible()) {
                Anim::pageAppear(page, page->palette().color(QPalette::Base));
            }
        });
    };
    appearOnSwitch(downloadStack);
    appearOnSwitch(gameStack);
    appearOnSwitch(settingsCategoryStack);
    // 实例页那一层"客户端实例 / 服务端"也走同一条换页动画。
    appearOnSwitch(instanceModeStack);

    setupBottomBar(bar);
    bar->setFooter(bottomBar);
    // 图标轨：任务卡只剩进度环、实例状态那格收起、工具箱只剩图标（tokens.md 第 7 节）。
    QObject::connect(bar, &TitleBar::collapsedChanged, bottomBar, [this](bool collapsed) {
        if (auto *text = taskSummaryButton->findChild<QWidget *>(QStringLiteral("taskSummaryText"))) {
            text->setVisible(!collapsed);
        }
        if (auto *card = taskSummaryButton->layout()) {
            card->setContentsMargins(collapsed ? QMargins(8, 8, 8, 8) : QMargins(10, 8, 10, 8));
        }
        runStateButton->setVisible(m_runStateActive && !collapsed);
    });
    backdrop->setHeroWidget(homeTab);
    // 主页上轨和窗口按钮压在清晰风景上：品牌、窗口按钮换主页字色。
    // 跟着「主页场景此刻露没露」走，而不是只看选项卡：主页上开了整页覆盖层时场景收起，
    // 轨也得换回普通字色（不然深底上一圈主页字色，和内容区是两种样子）。
    bar->setHeroMode(tabWidget->currentIndex() == homeTabIndex());
    QObject::connect(tabWidget, &QTabWidget::currentChanged, bar,
                     [this](int index) { bar->setHeroMode(index == homeTabIndex()); });
    QObject::connect(backdrop, &Glass::Backdrop::heroShownChanged, bar, [this](bool shown) {
        bar->setHeroMode(shown && tabWidget->currentIndex() == homeTabIndex());
    });

    // ---------- 内容区上面那一层 ----------
    // 设置界面和各种提示都开在这儿，不另开窗口（见 OverlayHost.hpp 开头那段）。
    // NOTE: 建在**最后**：它不吃布局，几何靠盯住 tabWidget 的 Resize/Move 现算，所以
    // 搭它的时候上面那四个 addTab 必须已经跑完了。
    overlayHost = new OverlayHost(tabWidget, centralWidget);
    // **提示开着时把底栏一起按住**。这一句必须在底栏建好之后（上面的
    // setupBottomBar 已经跑完），理由见 OverlayHost::setPromptBlockedWidget()：
    // 底栏不在覆盖层底下（这一层挂在 tabWidget 上），"被盖住 = 点不着"那条判据管不到它，
    // 于是提示开着时底栏照样能点开上拉面板 → 面板里点「改设置」被挡住 → 而那句解释
    // 为什么的话又要被"提示叠提示不做"吞掉 ⇒ 用户点了一下，屏幕上什么都没有。
    overlayHost->setPromptBlockedWidget(bottomBar);

    // ---------- 外框 ----------
    // 标题栏收进客户区 + 系统背板（见 WindowChrome.hpp 开头那两段）。拖窗区两条：
    // 右上那条窗口按钮条、导航轨的空白处。
    windowChrome = new WindowChrome(parent, bar->captionStrip(), parent);
    windowChrome->addDragStrip(bar);

}

/// 主页那一页：左 = 挑一个已装好的版本、启动、三种快捷入口；右 = 我的角色（3D 预览）。
///
/// 原先是 `setupUi()` 中间那两百多行。搬出来只是为了让"哪一页长什么样"有个名字：
/// `setupUi()` 现在只剩"按契约顺序把四页摆出来"和最后那一段接线。
QWidget *MainWindowUI::setupHomePage() {
    QWidget *homeTab = new ThemeBackground(Theme::Part::PageBackground, tabWidget);
    homeTab->setObjectName(homeTabName());
    auto *homePageLayout = new QVBoxLayout(homeTab);
    homePageLayout->setContentsMargins(0, 0, 0, 0);
    homePageLayout->setSpacing(0);
    // 设计稿 home.html 的版式（页面在 40 高的拖动条下面）：标题和启动区离页左 44，账号胶囊
    // 离页右 28、离顶 12；启动区底到实例带顶留 70；实例带离页左 32、离页底 22。
    // 两栏的页边距左右相等（验收量这一条），取 kHomeHeroMargin，左栏里再缩 kHomeHeroInset。
    homeHeroArea = new QWidget(homeTab);
    homeHeroArea->setObjectName(QStringLiteral("homeHeroArea"));
    const TwoColumns home = buildTwoColumnPage(homeHeroArea, QStringLiteral("home"));
    if (QLayout *row = homeHeroArea->layout()) {
        row->setContentsMargins(kHomeHeroMargin, 12, kHomeHeroMargin, 78 - kHomeLaunchGlowRoom);
    }
    home.left.layout->setContentsMargins(kHomeHeroInset, 0, 0, 0);
    home.left.layout->setSpacing(0);
    home.right.layout->setSpacing(0);
    homePageLayout->addWidget(homeHeroArea, 1);
    auto *homeBottomArea = new QWidget(homeTab);
    homeBottomArea->setObjectName(QStringLiteral("homeBottomArea"));
    auto *homeBottomLayout = new QVBoxLayout(homeBottomArea);
    homeBottomLayout->setContentsMargins(32, 0, 0, 22);
    homeBottomLayout->setSpacing(12);
    homePageLayout->addWidget(homeBottomArea);
    {
        home.left.layout->addSpacing(40);
        auto *welcome = new QLabel(QStringLiteral("下一个世界，等你开启。"), home.left.widget);
        welcome->setObjectName(QStringLiteral("homeHeroTitle"));
        welcome->setWordWrap(true);
        home.left.layout->addWidget(welcome);
        // 副题：西文手写体，和大标题一中一西。
        auto *subtitle = new QLabel(QStringLiteral("Your next world awaits."), home.left.widget);
        subtitle->setObjectName(QStringLiteral("homeHeroSubtitle"));
        subtitle->setContentsMargins(4, 6, 0, 0);
        home.left.layout->addWidget(subtitle);
        followHeroInk(welcome, false);
        followHeroInk(subtitle, true);
        // 窄于 1024 的窗口：标题收一号（50 → 40、副题 32 → 26），立绘缩到七成二（设计稿 @media）。
        const auto applyScale = [this, homeTab, welcome, subtitle]() {
            const bool narrow = homeTab->window()->width() < 1024;
            welcome->setFont(handPx(welcome->font(), Theme::HandFont::KaiLight,
                                    narrow ? 40 : kHomeHeroTitlePixelSize, narrow ? 3 : 4));
            subtitle->setFont(handPx(subtitle->font(), Theme::HandFont::Script, narrow ? 26 : 32, 0));
            if (skinViewer != nullptr) {
                skinViewer->setMaximumHeight(narrow ? kHomeSkinViewerHeight * 72 / 100 : kHomeSkinViewerHeight);
            }
        };
        new ResizeHook(homeTab, applyScale);
        applyScale();
    }
    {
        // 下拉框继续作为启动逻辑的内部选择模型，但主页上不出现。用户从启动区的「切换实例」
        // 或实例带的「全部实例」进入完整选择页；这样不用复制启动状态机。
        installedVersionCombo = new QComboBox(home.left.widget);

        // 空状态用"没选中任何项(currentIndex() == -1) + placeholderText"表示，
        // 不往列表里塞 "自定义版本" 那种假条目：控件自己会把占位文案画成灰字。
        // NOTE: 只要占位文案非空，往空下拉框里 addItems() 就不会自动选中第 0 项
        // （QComboBoxPrivate::rowsInserted 里那条 setCurrentIndex(0) 带了
        // !placeholderText.isEmpty() 的条件）。所以"空着"能一直保持到用户真的挑一项，
        // 代价是刷新之后必须自己把选中项捡回来，见 MainWindow.cpp 里的 fillVersionCombo()
        // （它是那个文件匿名命名空间里的自由函数，不是 MainWindow 的成员）。
        // 这里只是初始值；灌完数据后由 MainWindow 在两个串之间切换。
        installedVersionCombo->setPlaceholderText(noInstalledVersionPlaceholder());

        // 版本号长度不定（26.2-snapshot-1 之类），默认的 AdjustToContentsOnFirstShow 会在
        // 第一次显示（那时还是空的）时定一个宽度然后一直沿用，之后灌进上千个长版本号就容易
        // 把窗口顶宽。按"最短内容长度"算，宽度确定，灌数据时也不会跳。
        //
        // NOTE: "宽度确定"有个前提，很容易踩：**占位文案一旦超过 minimumContentsLength
        // 就会被算进 sizeHint**。（Qt 6.10.1，Fusion）13 字的占位会把框顶宽
        // 12px，而**条目文本和当前选中文本不影响**（空 / 加了一行超长条目 / 选中
        // 那行超长条目，算出来都是同一个宽度）。
        // 所以：给这两个下拉框设的每一条占位文案都必须 ≤12 字。现在共四处 ——
        // 本文件两处 + MainWindow.cpp 两处（那句取数失败的文案是 10 字，超过 12 字
        // 会让框在失败时突然变宽）。要放长句就写到旁边的 hint
        // 标签上去，QLabel 没有这个约束。
        installedVersionCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        installedVersionCombo->setMinimumContentsLength(12);
        // 滚轮防护：见 MainWindowUI.hpp 里的 WheelGuard
        WheelGuard::install(installedVersionCombo);
        installedVersionCombo->setObjectName(installedVersionComboName());
        installedVersionCombo->hide();

        startButton = new QPushButton(QStringLiteral("启动游戏"), home.left.widget);
        // 文字是两行（"启动游戏\n<显示名>"，由 MainWindow::updateStartButtonText() 维护；
        // 高度固定给足，免得两行被压成一行半。
        startButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        startButton->setMinimumHeight(kStartButtonMinHeight);
        QFont startFont = startButton->font();
        startFont.setBold(true);
        startFont = Theme::textFont(startFont, Theme::TextRole::Heading);
        startButton->setFont(startFont);
        Theme::markButton(startButton, Theme::ButtonKind::Primary);
        // 启动入口在左下那块启动区（`homeCurrentLaunch`）。保留这个兼容控件供既有业务连接
        // 使用，但不参与主页布局，否则会出现两颗功能完全相同的按钮。
        startButton->hide();

        // 启动用哪种身份**只由当前账号是什么**决定 —— 离线就去账号窗里加一个
        // 离线账号、切过去，没有第二个开关。判据的唯一出处是 MainWindow::offlineIdentity()。

        // 左下启动区（设计稿 .launch）：当前实例的名字、元信息、「启动游戏」+ 切换 + 设置。
        // 正常态压在风景左下（左栏底部，上面那段弹簧把它推下去）；展开实例列表时整块挪到
        // 页面下半区顶上、压成一行（见 openHomeInstancePicker()），同一组控件，不复制第二套。
        homeLaunchSpacer = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
        home.left.layout->addItem(homeLaunchSpacer);
        homeQuickCurrentSection = new QWidget(home.left.widget);
        homeQuickCurrentSection->setObjectName(QStringLiteral("homeQuickCurrentSection"));
        homeQuickCurrentSection->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        homeQuickCurrentRow = new QHBoxLayout(homeQuickCurrentSection);
        homeQuickCurrentRow->setContentsMargins(0, 0, 0, 0);
        homeQuickCurrentRow->setSpacing(0);
        home.left.layout->addWidget(homeQuickCurrentSection);

        // 下半区：「最近 / 收藏」实例带（设计稿 .strip）。
        homeLaunchArea = new QWidget(homeBottomArea);
        homeLaunchArea->setObjectName(QStringLiteral("homeLaunchArea"));
        homeLaunchArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto *homeLaunchLayout = new QVBoxLayout(homeLaunchArea);
        homeLaunchLayout->setContentsMargins(0, 0, 0, 0);
        homeLaunchLayout->setSpacing(12);
        homeQuickSection = new QWidget(homeLaunchArea);
        homeQuickSection->setObjectName(QStringLiteral("homeQuickSection"));
        homeQuickSection->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto *quickColumn = new QVBoxLayout(homeQuickSection);
        quickColumn->setContentsMargins(0, 0, 0, 0);
        quickColumn->setSpacing(6);
        auto *quickTabs = new QHBoxLayout;
        quickTabs->setContentsMargins(12, 0, 24, 0);
        quickTabs->setSpacing(6);
        const auto makeQuickTab = [this, quickTabs](const QString &text, const QString &name,
                                                    HomeQuickMode mode) {
            auto *button = new HomeStripTab(text, homeQuickSection);
            button->setObjectName(name);
            button->setCheckable(true);
            button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            button->setProperty("wtfmlHomeQuickTab", true);
            button->setCursor(Qt::PointingHandCursor);
            button->setFont(handPx(button->font(), Theme::HandFont::Kai, 15, 1.5));
            button->setMinimumWidth(48);
            QObject::connect(button, &QPushButton::clicked, button,
                             [this, mode]() { setHomeQuickMode(mode); });
            quickTabs->addWidget(button);
            return button;
        };
        // 「当前」不再是实例带里的一页（当前实例就是上面那块启动区，一直摆着）；这颗页签
        // 留着给既有接口，不显示。
        homeQuickCurrentTab = makeQuickTab(QStringLiteral("当前"),
                                            QStringLiteral("homeQuickCurrentTab"),
                                            HomeQuickMode::Current);
        homeQuickCurrentTab->hide();
        homeQuickRecentTab = makeQuickTab(QStringLiteral("最近"),
                                           QStringLiteral("homeQuickRecentTab"),
                                           HomeQuickMode::Recent);
        homeQuickFavoriteTab = makeQuickTab(QStringLiteral("收藏"),
                                             QStringLiteral("homeQuickFavoriteTab"),
                                             HomeQuickMode::Favorite);
        quickTabs->addStretch(1);
        homeInstanceSelectButton = new QPushButton(QStringLiteral("全部实例"), homeQuickSection);
        homeInstanceSelectButton->setObjectName(homeInstanceSelectButtonName());
        homeInstanceSelectButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        homeInstanceSelectButton->setMinimumWidth(80);
        homeInstanceSelectButton->setToolTip(QStringLiteral("展开或收起实例列表"));
        homeInstanceSelectButton->setLayoutDirection(Qt::RightToLeft);  // 箭头在字右边
        Theme::markButton(homeInstanceSelectButton, Theme::ButtonKind::Ghost);
        homeInstanceSelectButton->setFont(handPx(homeInstanceSelectButton->font(), Theme::HandFont::Kai, 14, 1));
        {
            const auto applyAllInk = [button = homeInstanceSelectButton]() {
                button->setIcon(QIcon(VersionIcons::chevron(false, 14, heroInk(true))));
            };
            followHeroInk(homeInstanceSelectButton, true);
            applyAllInk();
            Theme::follow(homeInstanceSelectButton, applyAllInk);
        }
        quickTabs->addWidget(homeInstanceSelectButton);
        quickColumn->addLayout(quickTabs);
        const auto makeQuickPage = [quickColumn, this](const QString &name, QWidget **sectionOut,
                                                       QHBoxLayout **rowOut) {
            auto *section = new HomeStripRow(homeQuickSection);
            section->setObjectName(name);
            quickColumn->addWidget(section);
            *sectionOut = section;
            *rowOut = section->row();
        };
        makeQuickPage(QStringLiteral("homeFavoriteSection"), &homeFavoriteSection,
                      &homeFavoriteRow);
        makeQuickPage(QStringLiteral("homeQuickRecentSection"), &homeQuickRecentSection,
                      &homeQuickRecentRow);
        homeLaunchLayout->addWidget(homeQuickSection);
        homeBottomLayout->addWidget(homeLaunchArea);
        homePickerHost = new QWidget(home.left.widget);
        homePickerHost->setObjectName(homeInstancePickerName());
        homePickerHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        homePickerHost->hide();
        homeBottomLayout->addWidget(homePickerHost, 1);
    }
    // ---------- 主页右栏：账号胶囊 + 站在风景里的立绘 ----------
    // 设计稿：账号胶囊贴右上；立绘不进任何盒子，脚踩一圈落地影站在右下，脚底和启动按钮底边齐。
    {
        auto *profileCard = new QWidget(home.right.widget);
        homeProfileCard = profileCard;
        homeRightColumn = home.right.widget;
        profileCard->setObjectName(QStringLiteral("homeProfileCard"));
        profileCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        auto *profileLayout = new QVBoxLayout(profileCard);
        profileLayout->setContentsMargins(0, 0, 0, 0);
        profileLayout->setSpacing(0);

        auto *pill = new HeroButton(HeroButton::Shape::Pill, profileCard);
        accountSkinButton = pill;
        accountSkinButton->setObjectName(MainWindowUI::accountSkinButtonName());
        accountSkinButton->setToolTip(MainWindowUI::accountSkinButtonText());
        accountSkinButton->setProperty("wtfmlAccountHeader", true);
        accountSkinButton->setAccessibleName(MainWindowUI::accountSkinButtonText());
        accountSkinButton->setFixedSize(kHomeAccountPillWidth, 44);
        auto *accountHeader = new QHBoxLayout(accountSkinButton);
        accountHeader->setContentsMargins(6, 0, 10, 0);
        accountHeader->setSpacing(10);
        homeAvatarLabel = new QLabel(accountSkinButton);
        homeAvatarLabel->setObjectName(QStringLiteral("homeAvatarLabel"));
        homeAvatarLabel->setFixedSize(32, 32);
        homeAvatarLabel->setAlignment(Qt::AlignCenter);
        homeAvatarLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        accountHeader->addWidget(homeAvatarLabel);
        auto *accountIdentity = new QVBoxLayout;
        accountIdentity->setSpacing(0);
        accountIdentity->setContentsMargins(0, 0, 0, 0);
        accountIdentity->addStretch(1);

        // 账号身份跟玩家模型放在一起：这里是主页的唯一账号入口，避免别处再出现一份同样的
        // 头像、名字和登录状态。文字由 MainWindow::updateBottomBar() 同步灌入。
        homeAccountLabel = new ElidedLabel(accountSkinButton);
        homeAccountLabel->setObjectName(QStringLiteral("homeAccountLabel"));
        QFont accountFont = homeAccountLabel->font();
        accountFont.setPixelSize(14);
        accountFont.setWeight(QFont::DemiBold);
        homeAccountLabel->setFont(accountFont);
        homeAccountLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        homeAccountLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        followHeroInk(homeAccountLabel, false);
        accountIdentity->addWidget(homeAccountLabel);
        homeAccountTypeLabel = new ElidedLabel(accountSkinButton);
        homeAccountTypeLabel->setObjectName(QStringLiteral("homeAccountTypeLabel"));
        QFont typeFont = homeAccountTypeLabel->font();
        typeFont.setPixelSize(12);
        homeAccountTypeLabel->setFont(typeFont);
        homeAccountTypeLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        homeAccountTypeLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        followHeroInk(homeAccountTypeLabel, true);
        accountIdentity->addWidget(homeAccountTypeLabel);
        accountIdentity->addStretch(1);
        accountHeader->addLayout(accountIdentity, 1);
        // 那颗小箭头**是画的**，不是字体里的字形。原来这里写的是 U+2304「⌄」：
        // 默认界面字体里没有这一格，Qt 于是第一次排这段文字时要去字体库里做一遍回退查找
        // ——本机实测 268 ms，整个启动里最大的一块（见 docs/dev/PERFORMANCE.md「主页那颗箭头」）。
        // `VersionIcons::chevron()` 和底栏、版本树里那些箭头是同一份笔画，也不吃字体。
        auto *accountArrow = new QLabel(accountSkinButton);
        accountArrow->setObjectName(MainWindowUI::homeAccountArrowName());
        accountArrow->setAttribute(Qt::WA_TransparentForMouseEvents);
        const auto applyAccountArrow = [accountArrow]() {
            accountArrow->setPixmap(VersionIcons::chevron(true, homeAccountArrowSize(), heroInk(true)));
        };
        applyAccountArrow();
        // 位图的颜色已经落在像素里了 ⇒ 换主题要重画一张（`VersionIcons` 按边长城里缓存）。
        Theme::follow(accountArrow, applyAccountArrow);
        accountArrow->setFixedSize(homeAccountArrowSize(), homeAccountArrowSize());
        accountHeader->addWidget(accountArrow);
        profileLayout->addWidget(accountSkinButton, 0, Qt::AlignRight | Qt::AlignTop);
        QObject::connect(accountSkinButton, &QPushButton::clicked, accountSkinButton, [this]() {
            toggleAccountsPanel();
        });

        skinViewer = new SkinViewer(profileCard);
        skinViewer->setObjectName(skinViewerName());
        // 宽钉在右栏宽；高按 SkinViewer 的人物比例算（heightForWidth），竖向放不下时被压矮、
        // 人跟着缩，窄窗再封顶到七成二（见上面 applyScale）。底边贴右栏底 = 启动按钮底边。
        skinViewer->setFixedWidth(kHomeSkinViewerWidth);
        skinViewer->setMinimumHeight(140);
        skinViewer->setMaximumHeight(kHomeSkinViewerHeight);
        // 立刻喂一套外观。这个控件**任何时候都不该是空白的** —— 没登录、还没取到、
        // 取不到，这三种情况看到的都是这套默认外观，所以兜底放在这里而不是等业务层来灌：
        // 少一处"谁忘了设"的可能。业务层拿到真皮肤时再覆盖它。
        skinViewer->setSkin(SkinModel::defaultSteveSkin(), QImage(), false);
        profileLayout->addWidget(skinViewer, 1, Qt::AlignHCenter | Qt::AlignBottom);

        skinHintLabel = new QLabel(skinHintNotLoggedInText(), profileCard);
        skinHintLabel->setObjectName(skinHintLabelName());
        skinHintLabel->setWordWrap(true);
        // 横向上把自己从布局的度量里摘出去：wordWrap 的 QLabel 报出来的 sizeHint 是
        // "整句排成一行"的宽度，这一栏是定宽的，不能让一句说明去顶它。
        skinHintLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        // 状态仍由业务层维护，供无障碍/测试读取；主页不显示这句冗余说明。
        skinHintLabel->hide();
        home.right.layout->addWidget(profileCard, 1);
    }
    // NOTE: 控件一个都不放也得把这栏建出来 —— 验证工程靠 leftColumnName()/rightColumnName()
    // 找这两栏量几何，主页右栏没了就等于主页的比例没地方量。
    return homeTab;
}

/// 实例那一页：左 = 游戏文件夹列表；右 = 那个文件夹里的实例（客户端 / 服务端 / 预设三页）。
QWidget *MainWindowUI::setupInstancePage() {
    // ---------- 实例：左 = 游戏文件夹列表；右 = 那个文件夹里的实例 ----------
    QWidget *instanceTab = new ThemeBackground(Theme::Part::PageBackground, tabWidget);
    instanceTab->setObjectName(instanceTabName());
    // NOTE: 这一份**不是 const**（另几页那三份是）：页头那两行字和右上角那条操作是
    // `buildTwoColumnPage()` 搭完之后才由这一页填的（`fillPageHeader()` 收的是
    // `Column&`）。
    TwoColumns instance = buildTwoColumnPage(instanceTab, QStringLiteral("instance"));
    // ---- 页头：标题「实例」+ 副标题（路径 + 实例数量，即 `instanceFolderLabel`）----
    //
    // 那句副标题**就是** `instanceFolderLabel`：它一直说的是"这一页的实例是从哪一份
    // .minecraft 里扫出来的、一共有几个"，"页面说明 / 路径 /
    // 当前状态"要的正是它。所以页头那个说明标签**就是它**（objectName 是
    // `instanceFolderLabel`）—— 文案、灌数据那条路（MainWindow::refreshInstanceList）、
    // 验证工程找它用的名字都落在这一处（标题下面那行说明）。
    fillPageHeader(instance.right, instancePageTitle(), QString());
    // 设计稿 page-title：一级页标题用 Display 档（28px），比卡片小标题大一级。
    instance.right.title->setFont(Theme::textFont(instance.right.title->font(), Theme::TextRole::Display));
    instanceFolderLabel = instance.right.subtitle;
    instanceFolderLabel->setObjectName(MainWindowUI::instanceFolderLabelName());
    // 它就是这一页的状态栏那一行，**不跟着"文字空不空"收起来**（文字由 MainWindow 灌，
    // 界面这边不知道盘上有几份）。
    instanceFolderLabel->setVisible(true);
    // 右上角那颗页面级操作：「分组设置…」（它管的是**分组清单本身**，不是选中的那一个
    // 实例 —— 见下面那段，那是 就定下的语义）。
    groupInstanceButton = new QPushButton(groupInstanceText(), instance.right.header);
    groupInstanceButton->setObjectName(groupInstanceButtonName());
    // 「分组设置…」= 打开另一个窗（一般性），不是"干这件事"。
    Theme::markButton(groupInstanceButton, Theme::ButtonKind::Neutral);
    instance.right.actions->addWidget(groupInstanceButton);
    // 设计稿 manage.html：没有左栏。标题右边一颗「游戏文件夹」胶囊，点开是文件夹列表浮层
    // （原左栏的 folderList / 添加文件夹… 原样搬进去，数据流不变）；说明行收进胶囊的悬停提示。
    instance.left.widget->setVisible(false);
    instanceFolderLabel->setVisible(false);
    auto *folderPill = new QPushButton(instance.right.header);
    folderPill->setObjectName(instanceFolderPillName());
    folderPill->setCursor(Qt::PointingHandCursor);
    folderPill->setProperty("wtfmlFolderPill", true);
    folderPill->setIconSize(QSize(16, 16));
    const auto paintPill = [folderPill]() {
        const Theme::Tokens t = Theme::tokens();
        folderPill->setIcon(QIcon(VersionIcons::line(QStringLiteral("folder"), 16, t.secondary)));
        folderPill->setStyleSheet(QStringLiteral(
            "QPushButton { background: transparent; color: %1; border: 1px solid %2; border-radius: 10px;"
            " padding: 0 30px 0 10px; min-height: 34px; max-height: 34px; text-align: left; font-weight: 600; }"
            "QPushButton:hover { background: %3; }")
            .arg(t.text.name(QColor::HexArgb), t.stroke.name(QColor::HexArgb), t.hover.name(QColor::HexArgb)));
    };
    paintPill();
    Theme::follow(folderPill, paintPill);
    {   // 右端 chevron：画在胶囊自己身上（QPushButton 的图标只能在左边）
        auto *chev = new QLabel(folderPill);
        chev->setAttribute(Qt::WA_TransparentForMouseEvents);
        const auto paintChev = [chev]() {
            chev->setPixmap(VersionIcons::line(QStringLiteral("chevD"), 14, Theme::tokens().secondary));
        };
        paintChev();
        Theme::follow(chev, paintChev);
        chev->setFixedSize(14, 14);
        auto *pillLayout = new QHBoxLayout(folderPill);
        pillLayout->setContentsMargins(0, 0, 10, 0);
        pillLayout->addStretch(1);
        pillLayout->addWidget(chev);
    }
    if (auto *titleRow = qobject_cast<QHBoxLayout *>(instance.right.header->layout()->itemAt(0)->layout())) {
        titleRow->insertWidget(1, folderPill, 0, Qt::AlignVCenter);
    }
    // 文件夹浮层：圆角 16 的磨砂面（浮层才有自己的面）。
    auto *folderPopup = new QFrame(instanceTab, Qt::Popup | Qt::FramelessWindowHint);
    folderPopup->setObjectName(instanceFolderPopupName());
    folderPopup->setAttribute(Qt::WA_TranslucentBackground);
    folderPopup->setAttribute(Qt::WA_StyledBackground);
    const auto paintPopup = [folderPopup]() {
        const Theme::Tokens t = Theme::tokens();
        folderPopup->setStyleSheet(QStringLiteral("QFrame#%1 { background: %2; border: 1px solid %3; border-radius: 16px; }")
                                       .arg(instanceFolderPopupName(), t.card.name(QColor::HexArgb),
                                            t.glassEdge.name(QColor::HexArgb)));
    };
    paintPopup();
    Theme::follow(folderPopup, paintPopup);
    auto *popupLayout = new QVBoxLayout(folderPopup);
    popupLayout->setContentsMargins(12, 12, 12, 12);
    popupLayout->setSpacing(8);
    instance.left.layout = popupLayout;
    instance.left.surface = folderPopup;
    QObject::connect(folderPill, &QPushButton::clicked, folderPopup, [folderPill, folderPopup]() {
        folderPopup->setFixedWidth(qMax(360, folderPill->width()));
        folderPopup->adjustSize();
        folderPopup->move(folderPill->mapToGlobal(QPoint(0, folderPill->height() + 6)));
        folderPopup->show();
    });

    {
        // ---- 侧栏：一个页面小标题「游戏目录」+ 文件夹列表 + 两颗按钮（都在侧栏底部）----
        QLabel *folderTitle = new QLabel(folderSectionTitleText(), instance.left.surface);
        folderTitle->setObjectName(folderSectionTitleName());
        markTextRole(folderTitle, Theme::sectionTitleTextRoleValue());
        {
            QFont titleFont = folderTitle->font();
            titleFont.setBold(true);
            folderTitle->setFont(titleFont);
        }
        instance.left.layout->addWidget(folderTitle);

        // 左边这张列表就是"启动器用哪一份 .minecraft"的选择器：选中哪一项，版本、资源、
        // 库、自管 Java 就全从那一份里找（PCL 左边那几张卡片是同一个意思）。
        // 自带的那一份永远排第一（见 FolderManager），所以列表不会是空的。
        //
        // NOTE: 每一行的内容**不是**条目自己画的（条目上没有文字），而是 setItemWidget()
        // 塞进去的一个 FolderRowWidget（见 setFolderRows()）——两行字 + 行尾一颗
        // 齿轮需要一个真控件才摆得下。省略因此完全由那一行里的 ElidedLabel 自己做。
        //
        // NOTE: 这里**不**设 `setTextElideMode(Qt::ElideMiddle)`。它是个
        // **死设置**：条目上一个字都没有，列表没有任何东西可省略，这一句不起
        // 作用（真正拦住横向滚动条的是下面那条 ScrollBarAlwaysOff）。留着更糟 ——
        // 它让人以为"路径从中间省略"这件事已经配好了，而真正画字的标签按**尾巴**
        // 省略（路径最后一级目录名正是唯一认得出是哪一份的那截）。
        // 省略放在那个标签上：见 FolderRowWidget 里的 `subtitle->setElideMode(...)`。
        folderList = new QListWidget(instance.left.surface);
        folderList->setObjectName(folderListName());
        folderList->setUniformItemSizes(true);
        folderList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        folderList->setSelectionMode(QAbstractItemView::SingleSelection);
        // 列表**不画自己那圈边**：侧栏面已经是一层底了，再套一圈就是"框套框"
        // （"减少框"第一条要减掉的正是它）。这条列表的行本来就是自绘的两行式行
        // （见 FolderRowWidget），有没有外框在观感上是"一块面板"和"一串行"的区别。
        folderList->setFrameShape(QFrame::NoFrame);
        styleSidebarList(folderList);
        folderList->setMinimumHeight(folderRowHeight() * 3 + 8);
        instance.left.layout->addWidget(folderList, 1);

        // 下面两颗按钮**贴着侧栏底部**（列表那一格吃掉剩下的高度）。
        // 这一行**只有「添加文件夹…」一个按钮**；「移除」只在行尾齿轮菜单里那一项（同一个
        // MainWindow::removeFolderFromList()，同一个确认框）。
        //
        // 两颗的身份是**有意分开**的（它们本来就是两种样子）：「添加文件夹…」
        // 是"加东西进来"那一档（主题色描边，`ButtonKind::Accent` = 默认那一份样式）；
        // 「导入整合包…」是**另一条**路（一般性），走中性那档白底黑字。两颗都顶满侧栏。
        addFolderButton = new QPushButton(addFolderText(), instance.left.surface);
        addFolderButton->setObjectName(addFolderButtonName());
        addFolderButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        instance.left.layout->addWidget(addFolderButton);
        // 浮层里点了一份文件夹 / 要添加文件夹：先收起浮层（切换本身照旧由 MainWindow 接）。
        QObject::connect(folderList, &QListWidget::itemClicked, folderPopup, &QWidget::hide);
        QObject::connect(addFolderButton, &QPushButton::clicked, folderPopup, &QWidget::hide);

        // 「导入整合包…」：从 Modrinth 格式的整合包（.zip / .mrpack）里**造一个新实例**。
        // 它和「添加文件夹…」是同一类动作（往这一列里添东西）—— 而右栏那三个都是
        // "对选中的实例做什么"，混在一排里用户得先猜它作用于选中项还是新建。
        importModpackButton = new QPushButton(importModpackText(), instance.left.surface);
        importModpackButton->setObjectName(importModpackButtonName());
        importModpackButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        // 设计稿：「导入整合包…」在页头右上，和「分组设置…」一排。
        Theme::markButton(importModpackButton, Theme::ButtonKind::Neutral);
        importModpackButton->setParent(instance.right.header);
        importModpackButton->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        instance.right.actions->insertWidget(0, importModpackButton);

        importModpackButton->setToolTip(QStringLiteral("导入平台整合包或可识别的本地压缩包。"));

    }
    {
        // 「分组设置…」是**页头右上角那颗页面级操作**（见上面 `instance.right.actions`
        // 那一段）。
        //
        // 它不该跟"对选中实例做什么"的那一排站在一起：那几颗作用于**选中的那一个实例**，
        // 它作用于**分组清单本身**（有哪些组、叫什么名字、什么顺序、默认展开还是收着、
        // 哪一个是置顶组 —— 见 ui/GroupDialog）。"选中的实例"对它没有意义，混在一排里
        // 用户得先猜它是"对这条做什么"还是"对这一列做什么"。摆在页头、正文之上，说的是
        // "下面这一整块怎么组织" —— 「框外右上角」就是**页头的操作位**。
        //
        // ---- 正文：就只有这一棵树 ----
        //
        // 第一条规矩就是"减少框"—— 页面不画边框、正文只有一个面。
        // 树的四周本来就是正文面（`workspace`），再套一圈就是"框套框"。那句
        // 「当前文件夹：…」是**页头那行说明**（见上面），而且和标题同级、比框里那行
        // 更像"这一页的状态"。
        //
        // 主页那个「已安装版本」下拉框的同一份数据，只是摆在这儿配左边的文件夹看。
        // 点击 = 在主页把它选中（两边按 id 双向同步，见 MainWindow::setupConnections）。
        //
        // 是**树**不是列表：实例可以归到用户建的组里，组是父节点、实例是子节点。
        // 一个组都没有的时候它就是平铺的一层（照样不插组头），观感和列表一样。
        // ---------- 搜索与筛选 ----------
        //
        // 搜索实例名、版本号、加载器；筛选原版/模组实例、可更新、最近运行；
        // 搜索不改变分组归属；清空搜索恢复原展开状态。四句里**三句都不是界面这一层的
        // 事**：判据在 `src/manage/InstanceFilter.hpp`（纯函数），"不改分组归属 / 清空就
        // 还原展开状态"两条落在 `MainWindow::rebuildInstanceTree()`（它才知道分组和展开
        // 状态从哪儿来）。这一层只摆控件、只把"用户点了/打了什么"如实报出去。
        //
        // 摆进**正文面里**、树的上方（不是页头右上角那个"页面级操作"位）：搜索和筛选是
        // "下面这棵树怎么摆"，和那颗「分组设置…」不是一类东西 —— 后者管的是分组清单本身。
        // 这一整套（筛选行 + 空态 + 树）**装在一个页面里**，好和"服务端那一页"
        // 一起塞进 `instanceModeStack`。
        QWidget *clientPage = new QWidget(instance.right.surface);
        clientPage->setObjectName(QStringLiteral("instanceClientPage"));
        auto *clientLayout = new QVBoxLayout(clientPage);
        clientLayout->setContentsMargins(0, 0, 0, 0);
        clientLayout->setSpacing(kFilterRowSpacing);

        instanceFilterRow = new QWidget(clientPage);
        instanceFilterRow->setObjectName(instanceFilterRowName());
        {
            auto *row = new QHBoxLayout(instanceFilterRow);
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(kFilterRowSpacing);
            instanceSearchEdit = new QLineEdit(instanceFilterRow);
            instanceSearchEdit->setObjectName(instanceSearchName());
            instanceSearchEdit->setPlaceholderText(instanceSearchPlaceholder());
            addSearchIcon(instanceSearchEdit);
            // 一个 ✕（一行一次清干净）。**不加**"清空搜索"那颗独立按钮：要的是
            // "清空搜索恢复原展开状态"，而那一格自己就带着这个动作，再摆一颗是同一件事
            // 说两遍。
            instanceSearchEdit->setClearButtonEnabled(true);
            instanceSearchEdit->setFixedWidth(280);  // 设计稿：搜索框靠右、定宽，chip 在左
            instanceSearchEdit->setFixedHeight(Theme::chipHeight());
            instanceSearchEdit->setProperty("wtfmlChipHeight", true);

            // 四颗 chip **全都建着**（和下载页左栏那六个分类、文件页那四颗筛子同一套做法）：
            // 会变的是"露不露"和"亮不亮"，不是"有没有这一颗"—— 建/不建会让"这一页该有什么"
            // 散成两处知识。四颗恒露着（「可更新」那一颗**不**按"查过没有"收起来：
            // 它是跨实例的筛子，收起来就没人知道"还能按更新筛"，见
            // `MainWindow::rebuildInstanceTree()` 里那段 —— 那条规矩是**文件页**的）。
            for (int i = 0; i < 4; ++i) {
                const auto which = static_cast<InstanceFilterButton>(i);
                auto *button = new QPushButton(instanceFilterText(which), instanceFilterRow);
                button->setObjectName(instanceFilterButtonName(which));
                button->setCheckable(true);
                button->setToolTip(instanceFilterTipText(which));
                // chip 那一档（24px 高、圆角、勾上是实底主题色）—— **和资源详情页那排
                // 游戏版本同一处出处**（`Theme::markChipButton()`）。
                Theme::markChipButton(button);
                instanceFilterButtons.append(button);
                row->addWidget(button);
            }
            row->addStretch(1);
            row->addWidget(instanceSearchEdit);
        }
        clientLayout->addWidget(instanceFilterRow);
        // Tab 顺序跟着设计稿的版面走（左→右）：页头胶囊 → 导入 → 分组设置；四颗 chip → 搜索框。
        QWidget::setTabOrder(folderPill, importModpackButton);
        QWidget::setTabOrder(importModpackButton, groupInstanceButton);
        QWidget::setTabOrder(instanceFilterButtons.last(), instanceSearchEdit);

        // 「没有符合筛选条件的实例」那一行：摆在这儿（筛选行下面、树上面）而不是树的
        // 下边 —— 筛空了的时候树是空的，那句话直接跟着筛选行读下来才对得上"是上面那些
        // 条件把它滤没的"。平时收着（`setVisible(false)`，连位置都不占）。
        instanceNoMatchLabel = new QLabel(instanceNoMatchText(), clientPage);
        instanceNoMatchLabel->setObjectName(instanceNoMatchLabelName());
        markTextRole(instanceNoMatchLabel, Theme::secondaryTextRoleValue());
        instanceNoMatchLabel->setAlignment(Qt::AlignCenter);
        instanceNoMatchLabel->setWordWrap(true);
        // 和别处那些说明文字同一条规矩：纯说明**不许**参与横向度量（折行 QLabel 报出来的
        // sizeHint 是"整句排成一行"的宽度）。
        instanceNoMatchLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        instanceNoMatchLabel->setVisible(false);
        clientLayout->addWidget(instanceNoMatchLabel);
        // 空文件夹那一句：同一个位置、同一档次要色，只是说的是另一件事（见 instanceEmptyText()）。
        // 设计稿 manage-empty：插画 + 大标题 + 一句引导 + 两颗主操作，整块居中；
        // MainWindow::rebuildInstanceTree() 按"空文件夹且没在筛"开关整块（标签跟着容器）。
        auto *emptyState = new QWidget(clientPage);
        emptyState->setObjectName(instanceEmptyStateName());
        emptyState->setVisible(false);
        auto *emptyLayout = new QVBoxLayout(emptyState);
        emptyLayout->setContentsMargins(0, 0, 0, 40);
        emptyLayout->setSpacing(10);
        emptyLayout->addStretch(1);
        auto *emptyArt = new QLabel(emptyState);
        emptyArt->setAlignment(Qt::AlignCenter);
        const auto paintArt = [emptyArt]() {
            QPixmap art(160, 140);
            art.fill(Qt::transparent);
            QPainter p(&art);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(Theme::tokens().stroke, 1.0, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QRectF(5, 2, 150, 126));
            p.drawPixmap(68, 12, VersionIcons::isoBlock(4, 30));   // 沙
            p.drawPixmap(26, 46, VersionIcons::isoBlock(0, 62));   // 草
            p.drawPixmap(72, 66, VersionIcons::isoBlock(6, 46));   // 石
            emptyArt->setPixmap(art);
        };
        paintArt();
        Theme::follow(emptyArt, paintArt);
        emptyLayout->addWidget(emptyArt, 0, Qt::AlignHCenter);
        auto *emptyTitle = new QLabel(instanceEmptyTitleText(), emptyState);
        emptyTitle->setAlignment(Qt::AlignCenter);
        {
            QFont f = Theme::textFont(emptyTitle->font(), Theme::TextRole::Display);
            f.setLetterSpacing(QFont::AbsoluteSpacing, 3);
            emptyTitle->setFont(f);
        }
        emptyLayout->addWidget(emptyTitle);
        instanceEmptyLabel = new QLabel(instanceEmptyText(), emptyState);
        instanceEmptyLabel->setObjectName(instanceEmptyLabelName());
        markTextRole(instanceEmptyLabel, Theme::secondaryTextRoleValue());
        instanceEmptyLabel->setAlignment(Qt::AlignCenter);
        instanceEmptyLabel->setWordWrap(true);
        instanceEmptyLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        instanceEmptyLabel->setVisible(false);
        emptyLayout->addWidget(instanceEmptyLabel);
        auto *emptyActions = new QHBoxLayout;
        emptyActions->setSpacing(10);
        emptyActions->addStretch(1);
        auto *emptyDownload = new QPushButton(instanceEmptyDownloadText(), emptyState);
        Theme::markButton(emptyDownload, Theme::ButtonKind::Primary);
        QObject::connect(emptyDownload, &QPushButton::clicked, emptyState,
                         [this]() { tabWidget->setCurrentIndex(downloadTabIndex()); });
        emptyActions->addWidget(emptyDownload);
        auto *emptyImport = new QPushButton(importModpackText(), emptyState);
        Theme::markButton(emptyImport, Theme::ButtonKind::Neutral);
        QObject::connect(emptyImport, &QPushButton::clicked, emptyState, [this]() {
            if (importModpackButton != nullptr) importModpackButton->click();
        });
        emptyActions->addWidget(emptyImport);
        emptyActions->addStretch(1);
        emptyLayout->addSpacing(8);
        emptyLayout->addLayout(emptyActions);
        emptyLayout->addStretch(2);
        clientLayout->addWidget(emptyState, 1);

        instanceList = new QTreeWidget(clientPage);
        instanceList->setObjectName(instanceListName());
        instanceList->setHeaderHidden(true);
        // 这一棵树**自己那圈边不画**：它四周已经是正文那个面（`workspace`），
        // 再套一圈就是第一条要减掉的"框套框"。行本身（组行 / 实例行）都是自绘的，
        // 有没有外框在观感上差的是"一块面板"还是"一串行"。
        instanceList->setFrameShape(QFrame::NoFrame);
        // 左边**不画** Qt 自带那个展开三角：展开/收起这件事画在行的**右**端（一个箭头，
        // 见 InstanceGroupDelegate），和版本树那几条分类长得一样（组行和版本下载那一套
        // 几乎一致）。开着的话一行上会有两个箭头，而且左边那个还会把组名往里推。
        // NOTE: 正路就是 setRootIsDecorated(false) —— 拿 `DontShowIndicator` 那条 item flag
        // 顶替会让整条分类的子条目一起消失（版本树那边踩过）。
        instanceList->setRootIsDecorated(false);
        // 组头 36px、实例行 60px 两档高度，不能开 uniformRowHeights（会全按第一行的组头算）。
        instanceList->setUniformRowHeights(false);
        instanceList->setIndentation(0);  // 设计稿：组内实例行和组头左缘对齐
        instanceList->viewport()->setAttribute(Qt::WA_Hover, true);
        instanceList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        // 分组头那一行不是默认画法：组名粗体、右边一个箭头 + 一颗齿轮（"点一下打开/关闭"
        // 和"这一组的更多操作"都画在那两个格里）。代理只接管挂着 groupIdRole() 的行，
        // 实例行原样走基类（见 InstanceGroupDelegate 文件头）。
        instanceList->setItemDelegate(new InstanceGroupDelegate(instanceList));
        // 实例行前面那个图标（原版草方块 / Forge 铁砧 / Fabric…）：**这一份树必须设
        // iconSize** —— 不设就用样式给的默认值，而图标是按 instanceTreeIconSize() 画的，
        // 会被缩到默认那一档、和实例名那行字挤在一起。
        //
        // 这个边长是**实例列表自己**那一档（26，版本树那一档是 18）。它和
        // MainWindow::refreshInstanceList() 里现算那些
        // QPixmap 时递进去的边长**必须同一个数** —— 两处对不上的症状是图标被 QIcon
        // 重新采样一遍（屏幕上只是略糊，验证工程里那条"行首那张图 == 现画的那张"会红）。
        instanceList->setIconSize(QSize(instanceTreeIconSize(), instanceTreeIconSize()));
        // 悬停那两颗按钮（上箭头 / 垃圾桶）**必须**有这一句，而且它是缺一不可的两半里的
        // 一半（另一半是代理里那个 `&& hovered`，见 InstanceGroupDelegate::paint()）：
        // 干净的视图 `WA_Hover=0` / `mouseTracking=0`，**真鼠标移动一个字都不改**
        // `QStyle::State_MouseOver`（下载页左栏那六个刷新圈就是这个判据）。少这一句的
        // 表现是"那两颗按钮再也不出现"，而两边
        // 各自的断言还都是绿的。
        instanceList->setMouseTracking(true);
        // 展开/折叠动画：组行那一按，子条目是**滑**出来的。交给 Qt 自己
        // （`QTreeView::setAnimated`），规矩见 ui/Animations.hpp。
        Anim::animateExpansion(instanceList);
        clientLayout->addWidget(instanceList, 1);

        // ---------- 顶部模式切换 + 服务端那一页 ----------
        //
        // 这一排是这一页**最上面**那一行：两颗按钮（「客户端实例」/「服务端」），下面那个
        // `QStackedWidget` 按它换页。摆进正文面里（不是页头右上角那排页面级操作）—— 它
        // 切的是"下面这一页说的是什么"，和「分组设置…」不是一类东西。
        {
            auto *modeRow = new QWidget(instance.right.surface);
            modeRow->setObjectName(instanceModeRowName());
            auto *modeLayout = new QHBoxLayout(modeRow);
            modeLayout->setContentsMargins(0, 0, 0, 0);
            modeLayout->setSpacing(kFilterRowSpacing);
            for (int i = 0; i < 3; ++i) {
                const auto mode = static_cast<InstanceMode>(i);
                auto *button = new QPushButton(instanceModeText(mode), modeRow);
                button->setObjectName(instanceModeButtonName(mode));
                button->setCheckable(true);
                button->setToolTip(instanceModeTipText(mode));
                // chip 那一档（和筛选那几颗同一处出处）。**互斥**由 `MainWindow` 那一层认
                // （点一颗就把另一颗的勾摘掉）—— 这里不挂 `setAutoExclusive`：那是"单选钮"
                // 那一套，和这一页别处的手感不一致（见 InstanceFilterButton 那段）。
                instanceModeButtons.append(button);
            }
            // 设计稿分段控件（客户端实例 / 服务端 / 预设）：一个容器包三段。
            auto *segment = new QWidget(modeRow);
            auto *segmentLayout = new QHBoxLayout(segment);
            segmentLayout->setContentsMargins(3, 3, 3, 3);
            segmentLayout->setSpacing(2);
            for (QPushButton *button : std::as_const(instanceModeButtons)) segmentLayout->addWidget(button);
            Theme::markSegmented(segment, instanceModeButtons);
            modeLayout->insertWidget(0, segment);
            // 两颗靠左，剩下的空当由弹簧吃掉。
            modeLayout->addStretch(1);
            instance.right.layout->addWidget(modeRow);
        }
        // 焦点链：左栏底下那两颗按钮 → 这一排模式 → 搜索框。
        // 这一排是这一页**最上面**那一行，可它是**最后**建的（筛选行 / 树那一整套先建，
        // 这一排是最后建的），于是按 Tab 的顺序是"搜索框 → 四颗筛子 → 树 → 模式按钮"
        // ——焦点从下面那棵树又窜回到最上面这一行。串成"左栏 → 模式 → 搜索"，一路往下。
        // （左栏那两颗是建在这一排之前的，所以这里从它们接出去。）
        if (importModpackButton != nullptr && instanceModeButtons.size() == 3) {
            QWidget::setTabOrder(importModpackButton, instanceModeButtons.at(0));
            QWidget::setTabOrder(instanceModeButtons.at(0), instanceModeButtons.at(1));
            QWidget::setTabOrder(instanceModeButtons.at(1), instanceModeButtons.at(2));
            if (instanceSearchEdit != nullptr) {
                QWidget::setTabOrder(instanceModeButtons.at(2), instanceSearchEdit);
            }
        }

        instanceModeStack = new QStackedWidget(instance.right.surface);
        instanceModeStack->setObjectName(instanceModeStackName());
        instanceModeStack->addWidget(clientPage);  // 第 0 页 = 客户端实例

        // ---- 第 1 页：服务端 ----
        //
        // "一行一个服务端"的一份清单（灌数据在 MainWindow::refreshServerList()）。这一页
        // 是**平铺的**（服务端没有分组），所以条目就是最上面一层、没有组头。
        QWidget *serverPage = new QWidget(instanceModeStack);
        serverPage->setObjectName(QStringLiteral("instanceServerPage"));
        auto *serverLayout = new QVBoxLayout(serverPage);
        serverLayout->setContentsMargins(0, 0, 0, 0);
        serverLayout->setSpacing(kFilterRowSpacing);

        serverEmptyLabel = new QLabel(serverEmptyText(), serverPage);
        serverEmptyLabel->setObjectName(serverEmptyLabelName());
        markTextRole(serverEmptyLabel, Theme::secondaryTextRoleValue());
        serverEmptyLabel->setAlignment(Qt::AlignCenter);
        serverEmptyLabel->setWordWrap(true);
        serverEmptyLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        serverEmptyLabel->setVisible(false);
        serverLayout->addWidget(serverEmptyLabel);

        serverList = new QTreeWidget(serverPage);
        serverList->setObjectName(serverListName());
        serverList->setHeaderHidden(true);
        serverList->setFrameShape(QFrame::NoFrame);
        serverList->setRootIsDecorated(false);
        serverList->setUniformRowHeights(true);
        serverList->setSelectionMode(QAbstractItemView::SingleSelection);
        serverList->setColumnCount(2);
        if (QHeaderView *header = serverList->header()) {
            header->setStretchLastSection(false);
            header->setSectionResizeMode(0, QHeaderView::Stretch);
            header->setSectionResizeMode(1, QHeaderView::Fixed);
            header->resizeSection(1, serverRowActionWidth());
        }
        // 行尾那颗按钮要能从行上右键弹出来（"其余几项进右键菜单"）。
        serverList->setContextMenuPolicy(Qt::CustomContextMenu);
        // 行首那个图标（按档画的那张 64×64 瓦片，见 assets/loader/*.png 与
        // MainWindow::refreshServerList()）—— 边长和实例行同一个数，两处对不上 QIcon 会重采样。
        serverList->setIconSize(QSize(instanceTreeIconSize(), instanceTreeIconSize()));
        serverList->setMouseTracking(true);
        serverLayout->addWidget(serverList, 1);

        newServerButton = new QPushButton(newServerText(), serverPage);
        newServerButton->setObjectName(newServerButtonName());
        // 造一个新东西出来 = 这一页的主操作，走主题色那一档（和左栏「添加文件夹…」同一档）。
        Theme::markButton(newServerButton, Theme::ButtonKind::Accent);
        serverLayout->addWidget(newServerButton);

        instanceModeStack->addWidget(serverPage);  // 第 1 页 = 服务端

        // ---- 第 2 页：预设----
        //
        // 只是一个**宿主**：整页的内容（列表 / 提取 / 计划）由 `PresetController` 建好塞进来。
        // 这一层不 include 预设那一层，也不该知道它长什么样。
        instancePresetPage = new QWidget(instanceModeStack);
        instancePresetPage->setObjectName(instancePresetPageName());
        auto *presetHostLayout = new QVBoxLayout(instancePresetPage);
        presetHostLayout->setContentsMargins(0, 0, 0, 0);
        presetHostLayout->setSpacing(0);
        instanceModeStack->addWidget(instancePresetPage);  // 第 2 页 = 预设

        instance.right.layout->addWidget(instanceModeStack, 1);
        // 起手停在第 0 页（客户端实例）—— 顺带把那两颗按钮的勾摆正（`setInstanceMode()`
        // 是"停在哪一页 + 谁勾着"的唯一出处）。
        setInstanceMode(InstanceMode::Client);

        // 这一排的三颗按钮已经分别有入口：
        // · 「实例设置…」= **点这一条实例行**（空白、左边那个图标、版本名都算），
        //   行尾还会常显一颗齿轮 —— 两颗做的是同一件事；
        // · 「重命名…」在「实例设置」那一页（改实例名就在那一页里改）；
        // · 「分组设置…」是框**外面**的右上角那颗页面级操作（见那一段 NOTE）。
        //
        // 「停止游戏…」归底栏左面板里那一行的电源（见 MainWindow::stopGameSession()）：
        // 那一格写着"哪一个会话在跑"，停哪一份比在"对选中实例做什么"的排里更说得清。
        // 它只停**这一份**（会话号）—— 多开的那几份要逐个停。
        // 「导入整合包…」在左栏「游戏文件夹」那一组（在「添加文件夹…」下面），那一组
        // 就是侧栏本身，不是框。
        // 正文那一格由上面那棵树吃满 —— 它上面再没有别的东西了（页头在面外面）。
    }
    return instanceTab;
}

/// 下载那一页：左 = 一列分类（游戏 + 五类资源 + 服务端）；右 = 那个分类的内容。
///
/// 「游戏」那一页是版本树 + 安装页；其余几页在下面那段循环里搭（搜索框 + 三层树）。


void MainWindowUI::setInstanceFolderText(const QString &text) {
    instanceFolderText = text;
    if (instanceFolderLabel != nullptr && instanceMode != InstanceMode::Preset) {
        instanceFolderLabel->setText(text);
    }
    if (tabWidget != nullptr)
        if (auto *pill = tabWidget->findChild<QPushButton *>(instanceFolderPillName())) pill->setToolTip(text);
}

void MainWindowUI::setPresetSubtitle(const QString &text) {
    presetSubtitleText = text;
    if (instanceFolderLabel != nullptr && instanceMode == InstanceMode::Preset) {
        instanceFolderLabel->setText(text);
    }
}

void MainWindowUI::setServerFormIntro(const QString &text) {
    if (serverFormIntroLabel != nullptr) {
        serverFormIntroLabel->setText(text);
    }
}

void MainWindowUI::setInstanceMode(InstanceMode mode) {
    if (navigationGuard && !navigationGuard()) {
        const int current = instanceModeStack ? instanceModeStack->currentIndex() : 0;
        for (int i=0;i<instanceModeButtons.size();++i)
            if (instanceModeButtons[i]) instanceModeButtons[i]->setChecked(i==current);
        return;
    }
    const int index = static_cast<int>(mode);
    if (instanceModeStack == nullptr || index < 0 || index >= instanceModeStack->count()) {
        return;  // 界面没搭出来 / 越界：什么都不改（见头文件那段）
    }
    // 三颗按钮的勾和停着的那一页**一起**改（这是这个函数存在的全部意义）。
    instanceModeStack->setCurrentIndex(index);
    // Presets live in the selected game folder too (they serve that folder's instances),
    // so the folder column stays visible in every mode.
    if (auto *page = tabWidget->widget(instancesTabIndex())) {
        if (auto *title = page->findChild<QLabel *>(pageTitleName(QStringLiteral("instance"))))
            title->setText(mode==InstanceMode::Preset ? QStringLiteral("预设") : QStringLiteral("实例"));
    }
    if (groupInstanceButton) groupInstanceButton->setVisible(mode != InstanceMode::Preset);
    instanceMode = mode;
    // 页头那句副标题**一直都有**（"这一页列的是什么"）：换档只是换一句话。
    // 预设和实例同属当前游戏文件夹，所以两份底稿说的都是这个文件夹。
    if (instanceFolderLabel) {
        // 实例 / 服务端：路径与数量收进文件夹胶囊的悬停提示；预设页那句说明仍在标题下。
        instanceFolderLabel->setVisible(mode == InstanceMode::Preset);
        instanceFolderLabel->setText(mode == InstanceMode::Preset ? presetSubtitleText
                                                                  : instanceFolderText);
    }
    for (int i = 0; i < instanceModeButtons.size(); ++i) {
        if (instanceModeButtons.at(i) != nullptr) {
            instanceModeButtons.at(i)->setChecked(i == index);
        }
    }
}




void MainWindowUI::setHomeQuickRows(const QList<HomeQuickItem> &current,
                                    const QList<HomeQuickItem> &favorites,
                                    const QList<HomeQuickItem> &recent) {
    const auto rebuildRow = [this](QWidget *section, QHBoxLayout *row,
                                   const QList<HomeQuickItem> &items, bool favoriteRow) {
        if (section == nullptr || row == nullptr) {
            return;
        }
        while (QLayoutItem *child = row->takeAt(0)) {
            delete child->widget();
            delete child;
        }
        if (items.isEmpty()) {
            return;
        }
        const int cardHeight = kHomeTileHeight;
        for (int itemIndex = 0; itemIndex < items.size(); ++itemIndex) {
            const HomeQuickItem &item = items.at(itemIndex);
            if (section == homeQuickCurrentSection) {
                // 左下启动区（设计稿 .launch）：小眉题、实例名（手写 40）、元信息一行、
                // 「启动游戏」+ 切换实例 + 实例设置。没有当前实例时同一块说"先挑一个"。
                auto *card = new QWidget(section);
                card->setObjectName(QStringLiteral("homeCurrentInstanceCard"));
                card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
                auto *column = new QVBoxLayout(card);
                // 底下留 kHomeLaunchGlowRoom：主按钮那圈柔光画在按钮外面，卡片边就是裁切边。
                column->setContentsMargins(0, 0, 0, kHomeLaunchGlowRoom);
                column->setSpacing(0);
                auto *eyebrow = new QLabel(QStringLiteral("当前实例"), card);
                eyebrow->setObjectName(QStringLiteral("homeCurrentEyebrow"));
                eyebrow->setFont(handPx(eyebrow->font(), Theme::HandFont::Kai, 13, 2));
                followHeroInk(eyebrow, true);
                column->addWidget(eyebrow);
                auto *name = new ElidedLabel(card);
                name->setObjectName(QStringLiteral("homeCurrentName"));
                name->setText(item.empty ? QStringLiteral("还没有选实例") : item.name);
                name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
                name->setFont(handPx(name->font(), Theme::HandFont::Kai, 40, 3));
                // 上 4 下 8 的留白放进名字自己的边距：压成一行时它整个藏起来，留白跟着走。
                name->setContentsMargins(0, 4, 0, 8);
                name->setMinimumHeight(52 + 12);
                followHeroInk(name, false);
                column->addWidget(name);
                auto *meta = new QLabel(item.empty ? QStringLiteral("从全部实例里挑一个，下次打开就从它开始。")
                                                   : homeLaunchMeta(item),
                                        card);
                meta->setObjectName(QStringLiteral("homeCurrentVersion"));
                meta->setToolTip(item.detail);
                QFont metaFont = meta->font();
                metaFont.setPixelSize(13);
                meta->setFont(metaFont);
                meta->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
                followHeroInk(meta, true);
                // 设计稿 .meta：行首一枚加载器小图标（没装加载器 = 草方块，和 loaderMark() 越界一条规矩）。
                auto *metaRow = new QHBoxLayout;
                metaRow->setContentsMargins(0, 0, 0, 0);
                metaRow->setSpacing(6);
                if (!item.empty) {
                    const QString versionLine = item.detail.section(QLatin1Char('\n'), 0, 0);
                    const QString loader = versionLine.section(QStringLiteral(" / "), 1).trimmed();
                    const QStringList loaderNames = modLoaderOptionTexts();
                    int loaderIndex = loaderNames.size();
                    for (int i = 0; i < loaderNames.size(); ++i) {
                        if (!loader.isEmpty() && loader.startsWith(loaderNames.at(i), Qt::CaseInsensitive)) {
                            loaderIndex = i;
                            break;
                        }
                    }
                    auto *loaderIcon = new QLabel(card);
                    loaderIcon->setObjectName(QStringLiteral("homeCurrentLoaderIcon"));
                    loaderIcon->setFixedSize(14, 14);
                    loaderIcon->setScaledContents(true);
                    loaderIcon->setPixmap(VersionIcons::loaderMark(loaderIndex, 28));
                    metaRow->addWidget(loaderIcon, 0, Qt::AlignVCenter);
                }
                metaRow->addWidget(meta, 1);
                column->addLayout(metaRow);
                auto *gap = new QWidget(card);
                gap->setObjectName(QStringLiteral("homeCurrentGap"));
                gap->setFixedHeight(22);
                column->addWidget(gap);
                auto *actions = new QHBoxLayout;
                actions->setContentsMargins(0, 0, 0, 0);
                actions->setSpacing(10);
                auto *launch = new QPushButton(item.empty ? QStringLiteral("选择实例")
                                                          : QStringLiteral("启动游戏"),
                                               card);
                launch->setObjectName(QStringLiteral("homeCurrentLaunch"));
                launch->setProperty("wtfmlHeroStart", true);
                launch->setMinimumWidth(220);
                // 按自己的宽度来（设计稿 min-width 220），不吃掉这一行剩下的空。
                launch->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
                launch->setFont(handPx(launch->font(), Theme::HandFont::Kai, 20, 3));
                Theme::markButton(launch, Theme::ButtonKind::Primary);
                const auto applyLaunchLook = [launch]() {
                    const Theme::Tokens t = Theme::tokens();
                    launch->setIcon(QIcon(VersionIcons::line(QStringLiteral("play"), 18, t.onPrimary, 2.0)));
                    launch->setIconSize(QSize(18, 18));
                    // 设计稿：主按钮下面一圈强调色的柔光（box-shadow 0 10 30 -8 accent-fill）。
                    auto *glow = qobject_cast<QGraphicsDropShadowEffect *>(launch->graphicsEffect());
                    if (glow == nullptr) {
                        glow = new QGraphicsDropShadowEffect(launch);
                        launch->setGraphicsEffect(glow);
                    }
                    QColor color = t.primary;
                    color.setAlphaF(0.55);
                    glow->setColor(color);
                    glow->setBlurRadius(30);
                    glow->setOffset(0, 10);
                };
                applyLaunchLook();
                Theme::follow(launch, applyLaunchLook);
                const HomeQuickAction launchAction =
                    item.empty ? HomeQuickAction::ChooseInstance : HomeQuickAction::Launch;
                QObject::connect(launch, &QPushButton::clicked, launch,
                                 [this, key = item.key, launchAction]() {
                    if (m_homeQuickActionHandler) {
                        m_homeQuickActionHandler(key, launchAction);
                    }
                });
                actions->addWidget(launch);
                const auto makeSquare = [card, actions](const QString &objectName, const QString &icon,
                                                        const QString &tip) {
                    auto *button = new HeroButton(HeroButton::Shape::Square, card);
                    button->setObjectName(objectName);
                    button->setProperty("wtfmlHeroSquare", true);
                    button->setFixedSize(52, 52);
                    button->setToolTip(tip);
                    button->setAccessibleName(tip);
                    button->setIconSize(QSize(18, 18));
                    const auto applyIcon = [button, icon]() {
                        button->setIcon(QIcon(VersionIcons::line(icon, 18, heroInk(false), 1.8)));
                    };
                    applyIcon();
                    Theme::follow(button, applyIcon);
                    actions->addWidget(button);
                    return button;
                };
                auto *swap = makeSquare(QStringLiteral("homeCurrentSwap"), QStringLiteral("swap"),
                                        QStringLiteral("切换实例"));
                QObject::connect(swap, &QPushButton::clicked, swap, [this]() {
                    if (homeInstanceSelectButton != nullptr) homeInstanceSelectButton->click();
                });
                if (!item.empty) {
                    auto *edit = makeSquare(QStringLiteral("homeCurrentEdit"), QStringLiteral("gear"),
                                            QStringLiteral("实例设置"));
                    QObject::connect(edit, &QPushButton::clicked, edit, [this, key = item.key]() {
                        if (m_homeQuickActionHandler) {
                            m_homeQuickActionHandler(key, HomeQuickAction::Edit);
                        }
                    });
                }
                // 列表展开时整块压成这一行：名字挪到按钮右边（见 setHomeQuickMode）。
                auto *compactName = new ElidedLabel(card);
                compactName->setObjectName(QStringLiteral("homeCurrentCompactName"));
                compactName->setText(name->text());
                compactName->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
                compactName->setFont(handPx(compactName->font(), Theme::HandFont::Kai, 20, 2));
                compactName->hide();
                followHeroInk(compactName, false);
                actions->addSpacing(8);
                actions->addWidget(compactName, 1);
                actions->addStretch(0);
                column->addLayout(actions);
                row->addWidget(card, 1);
                continue;
            }
            // 实例带上的一张卡（设计稿 .card）：196 宽，封面 48 + 名字 / 版本两行；平时无底，
            // 悬停（或键盘焦点）铺一层玻璃底并在右边出一颗播放钮。点卡片本身照旧开详情面板。
            auto *card = new HeroButton(HeroButton::Shape::Tile, section);
            card->setObjectName(item.empty
                                    ? QStringLiteral("homeQuickEmpty_%1_%2")
                                          .arg(favoriteRow ? QStringLiteral("favorite")
                                                           : QStringLiteral("recent"))
                                          .arg(itemIndex)
                                    : QStringLiteral("homeQuickCard_%1").arg(item.key));
            card->setToolTip(item.name);
            card->setAccessibleName(item.name);
            // QPushButton 的全局 QSS 也声明了 min-height；只写 setMinimumHeight() 会在 polish
            // 后被它压回普通按钮高度。属性选择器负责复位 QSS，这里再用固定尺寸表达卡片的布局尺寸。
            card->setProperty("wtfmlQuickCard", true);
            card->setProperty("wtfmlQuickCardSize", QStringLiteral("normal"));
            card->setFixedSize(kHomeTileWidth, cardHeight);
            Theme::markButton(card, item.empty ? Theme::ButtonKind::Ghost
                                               : Theme::ButtonKind::Neutral);

            if (item.empty) {
                card->setProperty("wtfmlQuickEmpty", true);
                card->setDashed(true);
                auto *emptyLayout = new QHBoxLayout(card);
                emptyLayout->setContentsMargins(8, 0, 8, 0);
                auto *hint = new QLabel(QStringLiteral("＋  添加实例"), card);
                hint->setObjectName(QStringLiteral("homeQuickEmptyHint"));
                hint->setAlignment(Qt::AlignCenter);
                hint->setAttribute(Qt::WA_TransparentForMouseEvents);
                followHeroInk(hint, true);
                emptyLayout->addWidget(hint, 1);
                QObject::connect(card, &QPushButton::clicked, card, [this]() {
                    if (m_homeQuickActionHandler) {
                        m_homeQuickActionHandler(QString(), HomeQuickAction::ChooseInstance);
                    }
                });
                row->addWidget(card);
                continue;
            }

            auto *content = new QHBoxLayout(card);
            content->setContentsMargins(8, 0, 8, 0);
            content->setSpacing(12);
            auto *icon = new QLabel(card);
            icon->setFixedSize(48, 48);
            icon->setAttribute(Qt::WA_TransparentForMouseEvents);
            const auto applyCover = [icon, itemIcon = item.icon, name = item.name]() {
                icon->setPixmap(homeTileCover(itemIcon, name, 48));
            };
            applyCover();
            Theme::follow(icon, applyCover);
            content->addWidget(icon, 0, Qt::AlignVCenter);
            auto *identity = new QVBoxLayout;
            identity->setSpacing(1);
            identity->setContentsMargins(0, 0, 0, 0);
            identity->addStretch(1);
            auto *nameRow = new QHBoxLayout;
            nameRow->setSpacing(6);
            auto *name = new ElidedLabel(card);
            name->setText(item.name);
            // 名字按自己宽度排，「当前」紧跟在后；放不下时名字先省略。
            name->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
            name->setMinimumWidth(1);
            name->setAttribute(Qt::WA_TransparentForMouseEvents);
            QFont nameFont = name->font();
            nameFont.setPixelSize(14);
            nameFont.setWeight(QFont::Medium);
            name->setFont(nameFont);
            followHeroInk(name, false);
            nameRow->addWidget(name);
            if (item.current) {
                auto *badge = new QLabel(QStringLiteral("当前"), card);
                badge->setObjectName(QStringLiteral("homeQuickCurrentBadge"));
                badge->setFont(handPx(badge->font(), Theme::HandFont::Kai, 11, 1));
                badge->setAttribute(Qt::WA_TransparentForMouseEvents);
                const auto applyBadge = [badge]() {
                    QPalette p = badge->palette();
                    p.setColor(QPalette::WindowText, Theme::tokens().deep);
                    badge->setPalette(p);
                };
                applyBadge();
                Theme::follow(badge, applyBadge);
                nameRow->addWidget(badge);
            }
            nameRow->addStretch(1);
            identity->addLayout(nameRow);
            auto *sub = new QLabel(homeTileSubtitle(item.detail), card);
            sub->setObjectName(QStringLiteral("homeQuickTileVersion"));
            QFont subFont = sub->font();
            subFont.setPixelSize(12);
            sub->setFont(subFont);
            sub->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            sub->setAttribute(Qt::WA_TransparentForMouseEvents);
            followHeroInk(sub, true);
            identity->addWidget(sub);
            identity->addStretch(1);
            content->addLayout(identity, 1);
            auto *play = new HeroButton(HeroButton::Shape::Play, card);
            play->setObjectName(QStringLiteral("homeQuickPlay"));
            play->setProperty("wtfmlHeroPlay", true);
            play->setFixedSize(30, 30);
            play->setToolTip(QStringLiteral("启动 %1").arg(item.name));
            play->setAccessibleName(play->toolTip());
            play->setFocusPolicy(Qt::NoFocus);  // 键盘：焦点在卡片上时它就露出来，回车开详情
            play->setIconSize(QSize(13, 13));
            const auto applyPlay = [play]() {
                play->setIcon(QIcon(VersionIcons::line(QStringLiteral("play"), 13, Theme::tokens().onPrimary, 2.0)));
            };
            applyPlay();
            Theme::follow(play, applyPlay);
            QObject::connect(play, &QPushButton::clicked, play, [this, key = item.key]() {
                if (m_homeQuickActionHandler) {
                    m_homeQuickActionHandler(key, HomeQuickAction::Launch);
                }
            });
            content->addWidget(play, 0, Qt::AlignVCenter);
            card->setRevealOnHover(play);

            QObject::connect(card, &QPushButton::clicked, card, [this, card, item]() {
                auto *popup = new HomeQuickPopup(card->window(), Qt::Popup | Qt::FramelessWindowHint);
                popup->setAttribute(Qt::WA_DeleteOnClose);
                popup->setObjectName(QStringLiteral("homeQuickPopup"));
                markSurface(popup, Theme::workspaceSurfaceValue());
                popup->setAutoFillBackground(true);
                auto *scroll = new QScrollArea(popup);
                scroll->setFrameShape(QFrame::NoFrame);
                scroll->setWidgetResizable(true);
                scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                auto *body = new QWidget(scroll);
                scroll->setWidget(body);
                auto *outer = new QVBoxLayout(popup);
                outer->setContentsMargins(0, 0, 0, 0); outer->addWidget(scroll);
                auto *layout = new QVBoxLayout(body);
                layout->setContentsMargins(16, 14, 16, 14);
                layout->setSpacing(8);
                auto *identity = new QHBoxLayout;
                identity->setSpacing(12);
                auto *icon = new QLabel(body);
                icon->setFixedSize(48, 48);
                icon->setPixmap(item.icon.isNull() ? VersionIcons::isoBlock(VersionIcons::isoBlockKindFor(item.name), 48)
                                                  : item.icon.pixmap(48, 48));
                identity->addWidget(icon, 0, Qt::AlignTop);
                auto *information = new QVBoxLayout;
                information->setSpacing(4);
                auto *title = new QLabel(item.name, body);
                title->setTextFormat(Qt::PlainText);
                QFont titleFont = Theme::textFont(title->font(), Theme::TextRole::Heading);
                titleFont.setBold(true); title->setFont(titleFont); title->setWordWrap(true);
                information->addWidget(title);
                if (!item.detail.isEmpty()) {
                    auto *detail = makeHomeHint(item.detail, body);
                    detail->setWordWrap(true);
                    information->addWidget(detail);
                }
                identity->addLayout(information, 1);
                layout->addLayout(identity);
                auto *actions = new QHBoxLayout;
                auto *edit = new QPushButton(QStringLiteral("实例设置"), popup);
                Theme::markButton(edit, Theme::ButtonKind::Ghost);
                auto *launch = new QPushButton(QStringLiteral("启动实例"), popup);
                auto *folder = new QPushButton(QStringLiteral("打开目录"), popup);
                folder->setObjectName(QStringLiteral("homeQuickOpenFolder"));
                Theme::markButton(folder, Theme::ButtonKind::Ghost);
                Theme::markButton(launch, Theme::ButtonKind::Primary);
                const QString key = item.key;
                QObject::connect(edit, &QPushButton::clicked, popup, [this, popup, key]() {
                    popup->close();
                    if (m_homeQuickActionHandler) {
                        m_homeQuickActionHandler(key, HomeQuickAction::Edit);
                    }
                });
                QObject::connect(launch, &QPushButton::clicked, popup, [this, popup, key]() {
                    popup->close();
                    if (m_homeQuickActionHandler) {
                        m_homeQuickActionHandler(key, HomeQuickAction::Launch);
                    }
                });
                actions->addWidget(edit);
                actions->addWidget(folder);
                QObject::connect(folder, &QPushButton::clicked, popup, [this, popup, key]() {
                    popup->close();
                    if (m_homeQuickActionHandler) m_homeQuickActionHandler(key, HomeQuickAction::OpenFolder);
                });
                actions->addStretch(1);
                actions->addWidget(launch);
                layout->addLayout(actions);
                const QRect cardRect(card->mapToGlobal(QPoint()), card->size());
                QScreen *screen = QGuiApplication::screenAt(cardRect.center());
                if (!screen) screen = card->screen();
                const QRect available = screen ? screen->availableGeometry() : QRect(card->window()->mapToGlobal(QPoint()), card->window()->size());
                popup->setFixedWidth(qMin(440, qMax(1, available.width() - 16)));
                const int contentWidth = qMax(1, popup->width() - 32);
                const int contentHeight = qMax(layout->sizeHint().height(), layout->totalHeightForWidth(contentWidth));
                popup->setGeometry(PopupPlacement::around(cardRect, QSize(popup->width(), contentHeight + 2), available));
                popup->show();
            });
            row->addWidget(card);
        }
    };
    rebuildRow(homeQuickCurrentSection, homeQuickCurrentRow, current, false);
    rebuildRow(homeFavoriteSection, homeFavoriteRow, favorites, true);
    rebuildRow(homeQuickRecentSection, homeQuickRecentRow, recent, false);
    // 收藏/最近数量设为 0 时，不留一颗点了只会看空白的标签。当前页总有一张当前实例或
    // 「选择实例」空槽，因而始终可达。
    if (homeQuickFavoriteTab != nullptr) {
        homeQuickFavoriteTab->setVisible(!favorites.isEmpty());
    }
    if (homeQuickRecentTab != nullptr) {
        homeQuickRecentTab->setVisible(!recent.isEmpty());
    }
    if (current.isEmpty()) {
        // 仅旧适配入口会给空 current；让它仍然展示传进来的第一块内容，保住既有验证与
        // 外部嵌入调用的可见语义。正式主页永远传入一张当前实例/空槽，不走这里。
        m_homeQuickMode = !favorites.isEmpty() ? HomeQuickMode::Favorite
                                                : HomeQuickMode::Recent;
    } else if ((m_homeQuickMode == HomeQuickMode::Favorite && favorites.isEmpty())
               || (m_homeQuickMode == HomeQuickMode::Recent && recent.isEmpty())) {
        m_homeQuickMode = HomeQuickMode::Current;
    }
    setHomeQuickMode(m_homeQuickMode);
}

void MainWindowUI::setHomeQuickMode(HomeQuickMode mode) {
    // 已经隐藏的类别不能成为当前页；这一层守住，设置更新和点击标签两条路都不会留下空面板。
    const bool recentShown = homeQuickRecentTab != nullptr && !homeQuickRecentTab->isHidden();
    const bool favoriteShown = homeQuickFavoriteTab != nullptr && !homeQuickFavoriteTab->isHidden();
    if (mode == HomeQuickMode::Favorite && !favoriteShown) {
        mode = HomeQuickMode::Current;
    }
    if (mode == HomeQuickMode::Recent && !recentShown) {
        mode = HomeQuickMode::Current;
    }
    m_homeQuickMode = mode;
    // 「当前」那一块是左下启动区，一直在；停在「当前」时实例带显示最近（没有最近就收藏）。
    HomeQuickMode strip = mode;
    if (strip == HomeQuickMode::Current) {
        strip = recentShown ? HomeQuickMode::Recent
                            : (favoriteShown ? HomeQuickMode::Favorite : HomeQuickMode::Current);
    }
    const auto apply = [strip](QPushButton *tab, QWidget *page, HomeQuickMode ownMode) {
        const bool active = strip == ownMode;
        if (tab != nullptr) {
            const QSignalBlocker blocker(tab);
            tab->setChecked(active);
            tab->setProperty("wtfmlHomeQuickTabActive", active);
            tab->update();
        }
        if (page != nullptr) {
            page->setVisible(active);
        }
    };
    apply(homeQuickCurrentTab, nullptr, HomeQuickMode::Current);
    apply(homeQuickRecentTab, homeQuickRecentSection, HomeQuickMode::Recent);
    apply(homeQuickFavoriteTab, homeFavoriteSection, HomeQuickMode::Favorite);
    const bool compact = homeInstancePickerOpen();
    // 列表展开：启动区压成一行（眉题 / 大名字 / 元信息收起，名字挪到按钮右边），实例带的卡片
    // 压到 52 高。
    for (QWidget *root : {homeQuickCurrentSection, homeQuickSection}) {
        if (root == nullptr) continue;
        for (auto *widget : root->findChildren<QWidget *>()) {
            const QString name = widget->objectName();
            if (name.startsWith(QStringLiteral("homeQuickCard_")) || name.startsWith(QStringLiteral("homeQuickEmpty_"))) {
                widget->setFixedHeight(compact ? 52 : kHomeTileHeight);
                // ★ 只 `setFixedHeight()` 不够：高度上下限的**真出处是 QSS**。
                // `QStyleSheetStyle::setGeometry()` 在每一次 polish（悬停、按下、换主题都会来）
                // 拿 `wtfmlQuickCardSize="normal"` 那一档的 min-height 去 `setMinimumHeight()`，
                // 把 52 顶回去 —— 用户看到的就是"点一下卡片，压小的那截又长回去"。
                // 所以档位要跟着切，切完按 QSS 的规矩重新 polish 一次。
                const QString size = compact ? QStringLiteral("compact") : QStringLiteral("normal");
                if (widget->property("wtfmlQuickCardSize").toString() != size) {
                    widget->setProperty("wtfmlQuickCardSize", size);
                    widget->style()->unpolish(widget);
                    widget->style()->polish(widget);
                }
            }
            if (name == QStringLiteral("homeCurrentEyebrow") || name == QStringLiteral("homeCurrentName") ||
                name == QStringLiteral("homeCurrentVersion") || name == QStringLiteral("homeCurrentLoaderIcon") ||
                name == QStringLiteral("homeCurrentGap")) {  // 行首加载器图标跟元信息一起收，不然压不成一行
                widget->setVisible(!compact);
            }
            if (name == QStringLiteral("homeCurrentCompactName")) widget->setVisible(compact);
        }
    }
    // 启动区的高度当场定下来（不等下一轮布局）：展开 / 收起列表那一下就要让出 / 收回空间。
    if (homeQuickCurrentSection != nullptr) {
        if (auto *card = homeQuickCurrentSection->findChild<QWidget *>(QStringLiteral("homeCurrentInstanceCard"))) {
            // 压成一行时紧贴下面的实例带，不再给柔光留底。
            card->layout()->setContentsMargins(0, 0, 0, compact ? 0 : kHomeLaunchGlowRoom);
            // invalidate() 不往下清子布局的缓存（元信息那一行是嵌套布局，会带着旧的 17px）；
            // activate() 会把子布局一并作废再量。
            card->layout()->invalidate();
            card->layout()->activate();
            card->setFixedHeight(card->layout()->sizeHint().height());
        }
    }
}

void MainWindowUI::openHomeInstancePicker(
    const QList<HomePickerFolder> &folders, const QList<HomePickerInstance> &instances,
    const HomePickerFolderHandler &folderHandler,
    const HomePickerInstanceHandler &instanceHandler) {
    if (homePickerHost == nullptr || homeLaunchSpacer == nullptr) {
        return;
    }
    if (homeInstancePickerOpen()) {
        closeHomeInstancePicker();
        return;
    }
    while (QLayoutItem *child = homePickerHost->layout() == nullptr
                                      ? nullptr
                                      : homePickerHost->layout()->takeAt(0)) {
        delete child->widget();
        delete child;
    }
    delete homePickerHost->layout();
    QWidget *page = homePickerHost;
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(16);

    auto *instancePane = new QWidget(page);
    instancePane->setObjectName(QStringLiteral("homePickerInstancePane"));
    markSurface(instancePane, Theme::workspaceSurfaceValue());
    auto *instanceLayout = new QVBoxLayout(instancePane);
    instanceLayout->setContentsMargins(16, 14, 16, 14);
    instanceLayout->setSpacing(10);
    auto *heading = new QHBoxLayout;
    heading->addWidget(makeHomeTitle(QStringLiteral("实例"), instancePane));
    heading->addStretch(1);
    auto *folderFilter = new QComboBox(instancePane);
    folderFilter->setObjectName(QStringLiteral("homePickerFolderFilter"));
    folderFilter->setToolTip(QStringLiteral("按游戏文件夹筛选"));
    folderFilter->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    folderFilter->setMinimumContentsLength(10);
    folderFilter->addItem(QStringLiteral("全部文件夹"), QString());
    WheelGuard::install(folderFilter);
    heading->addWidget(folderFilter);
    auto *search = new QLineEdit(instancePane);
    search->setObjectName(QStringLiteral("homePickerSearch"));
    search->setPlaceholderText(QStringLiteral("搜索名称、版本、加载器、文件夹或标签"));
    addSearchIcon(search);
    search->setToolTip(QStringLiteral("支持空格组合关键词，例如 1.21 fabric。"));
    search->setClearButtonEnabled(true);
    search->setMinimumWidth(140);
    heading->addWidget(search);
    auto *filter = new QComboBox(instancePane);
    filter->setObjectName(QStringLiteral("homePickerLoaderFilter"));
    filter->addItem(QStringLiteral("全部"), QStringLiteral("all"));
    filter->addItem(QStringLiteral("收藏"), QStringLiteral("favorite"));
    filter->addItem(QStringLiteral("最近"), QStringLiteral("recent"));
    filter->addItem(QStringLiteral("原版"), QStringLiteral("vanilla"));
    filter->addItem(QStringLiteral("含模组加载器"), QStringLiteral("modded"));
    for (const QString &loader : {QStringLiteral("Fabric"), QStringLiteral("Forge"),
                                  QStringLiteral("NeoForge"), QStringLiteral("Quilt"), QStringLiteral("OptiFine")}) {
        filter->addItem(loader, loader.toLower());
    }
    WheelGuard::install(filter);
    heading->addWidget(filter);
    instanceLayout->addLayout(heading);
    auto *instanceList = new QListWidget(instancePane);
    instanceList->setObjectName(QStringLiteral("homePickerInstanceList"));
    instanceList->setFrameShape(QFrame::NoFrame);
    instanceList->setIconSize(QSize(36, 36));
    instanceList->setSpacing(4);
    instanceLayout->addWidget(instanceList, 1);
    auto *empty = makeHomeHint(QStringLiteral("这个文件夹里没有符合条件的实例。"), instancePane);
    empty->setObjectName(QStringLiteral("homePickerEmpty"));
    empty->setAlignment(Qt::AlignCenter);
    empty->hide();
    instanceLayout->addWidget(empty);
    root->addWidget(instancePane, 1);

    for (const HomePickerFolder &folder : folders) {
        folderFilter->addItem(folder.title, folder.path);
        folderFilter->setItemData(folderFilter->count() - 1, folder.subtitle, Qt::ToolTipRole);
    }

    const auto rebuild = [folderFilter, instanceList, empty, search, filter, instances]() {
        const QString folderPath = folderFilter->currentData().toString();
        const QString needle = search->text().trimmed();
        const QStringList terms = needle.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        const QString mode = filter->currentData().toString();
        instanceList->clear();
        for (const HomePickerInstance &instance : instances) {
            if (!folderPath.isEmpty() && instance.folderPath != folderPath) {
                continue;
            }
            QString haystack = instance.title + QLatin1Char(' ') + instance.subtitle
                                     + QLatin1Char(' ') + instance.id + QLatin1Char(' ')
                                     + instance.loaderKey + QLatin1Char(' ') + instance.folderPath
                                     + QLatin1Char(' ') + instance.searchKeywords;
            if (instance.loaderKey.isEmpty()) haystack += QStringLiteral(" 原版 纯净 vanilla 无加载器");
            else haystack += QStringLiteral(" 模组 modded");
            if (instance.favorite) haystack += QStringLiteral(" 收藏 置顶 favorite starred");
            if (instance.recent) haystack += QStringLiteral(" 最近 上次游玩 recent played");
            if (!std::all_of(terms.cbegin(), terms.cend(), [&haystack](const QString &term) {
                    return haystack.contains(term, Qt::CaseInsensitive);
                })) {
                continue;
            }
            if (mode == QStringLiteral("favorite") && !instance.favorite) {
                continue;
            }
            if (mode == QStringLiteral("recent") && !instance.recent) {
                continue;
            }
            if (mode == QStringLiteral("vanilla") && !instance.loaderKey.isEmpty()) {
                continue;
            }
            if (mode == QStringLiteral("modded") && instance.loaderKey.isEmpty()) continue;
            if (mode != QStringLiteral("all") && mode != QStringLiteral("favorite")
                && mode != QStringLiteral("recent") && mode != QStringLiteral("vanilla")
                && mode != QStringLiteral("modded")
                && instance.loaderKey.compare(mode, Qt::CaseInsensitive) != 0) {
                continue;
            }
            QString text = instance.title;
            if (!instance.subtitle.isEmpty()) {
                text += QLatin1Char('\n') + instance.subtitle;
            }
            auto *item = new QListWidgetItem(instance.icon, text, instanceList);
            item->setData(Qt::UserRole, instance.id);
            item->setData(Qt::UserRole + 1, instance.folderPath);
            item->setSizeHint(QSize(0, 56));
        }
        empty->setVisible(instanceList->count() == 0);
    };
    // 文件夹只作为局部筛选；真正切换活动文件夹由选中实例的回调完成。
    Q_UNUSED(folderHandler);
    QObject::connect(folderFilter, &QComboBox::currentIndexChanged, page,
                     [rebuild](int) { rebuild(); });
    QObject::connect(search, &QLineEdit::textChanged, page,
                     [rebuild](const QString &) { rebuild(); });
    QObject::connect(filter, &QComboBox::currentIndexChanged, page,
                     [rebuild](int) { rebuild(); });
    QObject::connect(instanceList, &QListWidget::itemClicked, page,
                     [this, instanceHandler](QListWidgetItem *item) {
                         if (item == nullptr || !instanceHandler) {
                             return;
                         }
                         instanceHandler(item->data(Qt::UserRole + 1).toString(),
                                         item->data(Qt::UserRole).toString());
                         closeHomeInstancePicker();
                     });
    rebuild();
    Anim::layoutTransition(tabWidget->widget(0), [this, page]() {
    homeLaunchSpacer->changeSize(0, 0, QSizePolicy::Minimum, QSizePolicy::Fixed);
    homeLaunchSpacer->invalidate();
    if (homeHeroArea != nullptr) {
        homeHeroArea->hide();
    }
    if (auto *layout = qobject_cast<QVBoxLayout *>(homePickerHost->parentWidget()->parentWidget()->layout())) {
        layout->setStretch(0, 0);
        layout->setStretch(1, 1);
    }
    if (homeProfileCard != nullptr) {
        homeProfileCard->hide();
    }
    if (homeRightColumn != nullptr) {
        homeRightColumn->hide();
    }
    if (homeInstanceSelectButton != nullptr) {
        homeInstanceSelectButton->setText(QStringLiteral("返回"));
    }
    // 启动区从风景左下挪到下半区顶上（风景那一块收起来了），列表接管剩下的高度。
    if (homeLaunchArea != nullptr && homeQuickCurrentSection != nullptr) {
        if (auto *launchLayout = qobject_cast<QBoxLayout *>(homeLaunchArea->layout())) {
            launchLayout->insertWidget(0, homeQuickCurrentSection);
        }
    }
    if (QLayout *bottom = homePickerHost->parentWidget()->layout()) {
        bottom->setContentsMargins(32, 12, kHomeHeroMargin, 22);
    }
    homePickerHost->show();
    setHomeQuickMode(m_homeQuickMode);
    if (homeLaunchArea != nullptr) {
        homeLaunchArea->show();
    }
    // QSpacerItem::changeSize() 只标记自身失效。这里的父布局同时有一个隐藏/显示的
    // 列表区，必须立即激活布局，才会真正收回顶部空白、让列表接管全部余高。
    if (QWidget *column = page->parentWidget(); column != nullptr && column->layout() != nullptr) {
        column->layout()->invalidate();
        column->layout()->activate();
        column->updateGeometry();
    }
    });
}

bool MainWindowUI::homeInstancePickerOpen() const {
    // 页面切走后祖先已隐藏，但仍须复位选择器自身的展开状态。
    return homePickerHost != nullptr && !homePickerHost->isHidden();
}

void MainWindowUI::closeHomeInstancePicker() {
    if (!homeInstancePickerOpen()) {
        return;
    }
    Anim::layoutTransition(tabWidget->widget(0), [this]() {
    homePickerHost->hide();
    setHomeQuickMode(m_homeQuickMode);
    if (homeHeroArea != nullptr) {
        homeHeroArea->show();
    }
    if (auto *layout = qobject_cast<QVBoxLayout *>(homePickerHost->parentWidget()->parentWidget()->layout())) {
        layout->setStretch(0, 1);
        layout->setStretch(1, 0);
    }
    if (homeLaunchSpacer != nullptr) {
        homeLaunchSpacer->changeSize(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
        homeLaunchSpacer->invalidate();
    }
    if (homeRightColumn != nullptr) {
        homeRightColumn->show();
    }
    if (homeProfileCard != nullptr) {
        homeProfileCard->show();
    }
    if (homeInstanceSelectButton != nullptr) {
        homeInstanceSelectButton->setText(QStringLiteral("全部实例"));
    }
    if (homeHeroArea != nullptr && homeQuickCurrentSection != nullptr) {
        auto *left = homeHeroArea->findChild<QWidget *>(leftColumnName(QStringLiteral("home")));
        if (auto *leftLayout = left != nullptr ? qobject_cast<QBoxLayout *>(left->layout()) : nullptr) {
            leftLayout->addWidget(homeQuickCurrentSection);
        }
    }
    if (QLayout *bottom = homePickerHost->parentWidget()->layout()) {
        bottom->setContentsMargins(32, 0, 0, 22);
    }
    if (homePickerHost->parentWidget() != nullptr && homePickerHost->parentWidget()->layout() != nullptr) {
        QLayout *layout = homePickerHost->parentWidget()->layout();
        layout->invalidate();
        layout->activate();
        homePickerHost->parentWidget()->updateGeometry();
    }
    }, false);
}
