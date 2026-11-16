/**
 * @file TitleBar.hpp
 * @brief 左导航轨：顶上品牌（点 W 收展），中间五颗导航（线性图标 + 手写体标签），底下挂后台任务与工具箱。
 *
 * 和窗口同一层底（不铺色块），靠选中那一格的淡主题色底分层（不画指示条）；那块底在
 * 两颗之间滑动（≤180 ms，`Anim::enabled()` 关掉就直接跳）。最小化 / 关闭不在轨上，
 * 在 `captionStrip()` 那条透明的条上（摆在右栏最上面，高 40）。
 *
 * 轨宽 208，可折成 64 的图标轨：每次启动先是图标轨，用户点品牌标 W 收起 / 展开；窗口宽 < 1024 时强制折，
 * 这时点 W 不动。
 *
 * 轨上除了按钮和底部挂件没有别的子控件：品牌、导航之间的空白都是画的，`ui/WindowChrome`
 * 把这些空白判成拖窗区（`childAt()` 认不到子控件就是）。
 *
 * 按钮只发信号（`navRequested`、`minimizeRequested`…），翻页、收窗是调用方的事。
 */

#pragma once

#include <QAbstractButton>
#include <QColor>
#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

class QHBoxLayout;
class QVBoxLayout;
class QVariantAnimation;

class TitleBar : public QWidget {
    Q_OBJECT

public:
    explicit TitleBar(QWidget *parent = nullptr);

    /// 展开时的轨宽（逻辑像素）。
    static int railWidth();
    /// 折叠后的图标轨宽。
    static int collapsedRailWidth();
    /// 窗口宽低于这个数时轨自动折叠。
    static int narrowBreakpoint();
    /// 右上那条窗口按钮条的高（顶部拖动区高）。
    static int barHeight();
    /// 窗口按钮（最小化 / 最大化 / 关闭）的配色：主窗口这条（只有最小化和关闭，maximize 传 nullptr）和独立窗口自绘的标题条共用一份。
    /// 传进来的应是 `TitleBarButton`（别的按钮原样跳过）；hero = 往主页压在风景上那一档字色走了多少（0~1）。
    static void styleCaptionButtons(QAbstractButton *minimize, QAbstractButton *maximize,
                                    QAbstractButton *close, qreal hero = 0.0);
    /// 窗口按钮的高（= 条高；宽是 `windowButtonWidth()`）。
    static int buttonSize();
    static int windowButtonWidth();
    /// 窗口按钮上面留的空白（按钮贴顶，0）。
    static int buttonInset();
    /// 每一颗导航的宽 / 高（展开时）。
    static int navItemWidth();
    static int navItemHeight();
    /// 导航标签的像素字号（系统字，Medium）。
    static int navFontPixelSize();
    /// 窗口按钮离窗口右边缘多远。
    static int windowButtonsRightMargin();
    /// 商标那五个字母（`WTFML`）。**条上画的就是它，不给用户改**（UI 问题 #3）。
    static QString defaultBrandText();

    QAbstractButton *minimizeButton() const { return m_minimize; }
    QAbstractButton *closeButton() const { return m_close; }
    /// 品牌标 W：点它（或 Tab 到它按回车 / 空格）收起 / 展开导航。
    QAbstractButton *logoButton() const { return m_logo; }
    /// 装着窗口按钮的那条透明条（调用方把它摆进右栏最上面；它也是一条拖窗区）。
    QWidget *captionStrip() const { return m_caption; }

    /// 摆上（或换掉）那一排导航。标签由调用方给（`QTabWidget::tabText()`）。
    void setNavItems(const QStringList &labels);
    void setCurrentNav(int index);
    int currentNav() const { return m_currentNav; }
    int navCount() const { return static_cast<int>(m_navButtons.size()); }
    QAbstractButton *navButton(int index) const;

    /// 轨底部那一块（后台任务、工具箱）。交进来的控件归轨管。
    void setFooter(QWidget *footer);

    /// 用户手动折叠（窄窗自动折叠另算，见 `isCollapsed()`）。默认 true：每次启动导航都收起。
    void setCollapsed(bool collapsed);
    /// 现在是不是图标轨（用户折了，或窗口窄）。
    bool isCollapsed() const { return m_userCollapsed || m_narrow; }
    /// 主页风景此刻的不透明度（Glass::Backdrop::heroOpacity）：窗口按钮的墨色按它在普通 / 主页字色之间渐变。
    /// 品牌标与品牌字不跟它走，换页不变色。
    void setHeroOpacity(qreal opacity);

signals:
    void minimizeRequested();
    void closeRequested();
    void navRequested(int index);
    /// 根导航已经应用完成（从哪儿来 / 去哪儿 / 是否重点当前页）。
    void navigationApplied(int sourceIndex, int targetIndex, bool reselected);
    /// 轨在展开 / 图标轨之间切换了（底部挂件据此收起文字）。
    void collapsedChanged(bool collapsed);

protected:
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void applyColors();
    void applyWidth(bool animate);
    void layoutForWidth(int width);

    QVBoxLayout *m_layout = nullptr;
    QVBoxLayout *m_navLayout = nullptr;
    QWidget *m_caption = nullptr;
    QWidget *m_footer = nullptr;
    QAbstractButton *m_minimize = nullptr;
    QAbstractButton *m_close = nullptr;
    QAbstractButton *m_logo = nullptr;
    QList<QAbstractButton *> m_navButtons;
    int m_currentNav = -1;
    bool m_applyingColors = false;
    bool m_userCollapsed = true;
    bool m_narrow = false;
    qreal m_heroOpacity = 0.0;
    bool m_watchingWindow = false;
    int m_pillFrom = -1;
    qreal m_pillProgress = 1.0;
    QVariantAnimation *m_pillAnimation = nullptr;
    QVariantAnimation *m_widthAnimation = nullptr;
};

/// 轨上和窗口按钮条上的那几颗按钮：自己画（线性图标 + 可选文字），悬停 / 按下走中性灰。
class TitleBarButton : public QAbstractButton {
    Q_OBJECT

public:
    enum class Glyph { Minimize, Maximize, Close, Text, Logo };
    enum class NavIcon { None, Home, Instances, Download, Settings, Agent, Toolbox };

    TitleBarButton(Glyph glyph, const QColor &hoverColor, const QColor &hoverInk, QWidget *parent = nullptr);

    void setLabel(const QString &text) { setText(text); }
    QString label() const { return text(); }
    void setNavIcon(NavIcon icon);
    NavIcon navIcon() const { return m_icon; }
    /// 当前页那一颗：底由轨画（会滑），自己只换字色。
    void setCurrent(bool current);
    bool isCurrent() const { return m_current; }
    bool isHovered() const { return m_hovered; }
    void setBaseColor(const QColor &color);
    void setInkColor(const QColor &color);
    void setHoverColors(const QColor &hoverColor, const QColor &hoverInk);
    void setPressColor(const QColor &color);
    /// 「最大化」那颗显示成「还原」。
    void setRestoreGlyph(bool restore);
    /// 焦点是用 Tab / Shift+Tab 选来的（品牌标 W 只在这时画焦点框、接回车 / 空格）。
    /// 窗口一激活 Qt 会把焦点自动交给链上第一个能 Tab 的控件（就是 W），那种焦点不算。
    bool hasKeyboardFocus() const { return m_keyboardFocus && hasFocus(); }

protected:
    void paintEvent(QPaintEvent *event) override;
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    Glyph m_glyph;
    NavIcon m_icon = NavIcon::None;
    QColor m_hoverColor;
    QColor m_hoverInk;
    QColor m_pressColor;
    QColor m_baseColor = Qt::transparent;
    QColor m_ink;
    QVariantAnimation *m_hoverAnimation = nullptr;
    qreal m_hoverProgress = 0.0;
    bool m_hovered = false;
    bool m_current = false;
    bool m_restore = false;
    bool m_keyboardFocus = false;
};
