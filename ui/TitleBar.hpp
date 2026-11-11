/**
 * @file TitleBar.hpp
 * @brief 左导航轨：顶上品牌，中间五颗导航（线性图标 + 手写体标签），底下挂后台任务与工具箱。
 *
 * 和窗口同一层底（不铺色块），靠选中那一格的淡主题色底 + 左边一根指示条分层；指示条在
 * 两颗之间滑动。最小化 / 关闭不在轨上，在 `captionStrip()` 那条透明的条上（摆在右栏最上面）。
 *
 * 轨上除了按钮和底部挂件没有别的子控件：品牌、导航之间的空白都是画的，`ui/WindowChrome`
 * 把这些空白判成拖窗区（`childAt()` 认不到子控件就是）。
 *
 * 按钮只发信号（`navRequested`、`minimizeRequested`、`closeRequested`），翻页、收窗是调用方的事。
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

    /// 轨宽（逻辑像素）。
    static int railWidth();
    /// 右上那条窗口按钮条的高。
    static int barHeight();
    /// 窗口按钮的边长。
    static int buttonSize();
    /// 窗口按钮上面留的空白，必须 ≥ `WindowChrome::resizeBorder()`（上沿那圈缩放边）。
    static int buttonInset();
    /// 每一颗导航的宽 / 高。
    static int navItemWidth();
    static int navItemHeight();
    /// 导航标签的像素字号（手写体）。
    static int navFontPixelSize();
    /// 两颗窗口按钮离窗口右边缘多远。
    static int windowButtonsRightMargin();
    /// 默认品牌名（设置页「个性化」里可以改）。
    static QString defaultBrandText();

    QAbstractButton *minimizeButton() const { return m_minimize; }
    QAbstractButton *closeButton() const { return m_close; }
    /// 装着最小化 / 关闭的那条透明条（调用方把它摆进右栏最上面；它也是一条拖窗区）。
    QWidget *captionStrip() const { return m_caption; }

    /// 摆上（或换掉）那一排导航。标签由调用方给（`QTabWidget::tabText()`）。
    void setNavItems(const QStringList &labels);
    void setCurrentNav(int index);
    int currentNav() const { return m_currentNav; }
    int navCount() const { return static_cast<int>(m_navButtons.size()); }
    QAbstractButton *navButton(int index) const;

    /// 轨底部那一块（后台任务、工具箱）。交进来的控件归轨管。
    void setFooter(QWidget *footer);

    /// 换品牌名；两头空白去掉后为空 = 回默认。
    void setBrandText(const QString &text);
    QString titleText() const;

signals:
    void minimizeRequested();
    void closeRequested();
    void navRequested(int index);
    /// 根导航已经应用完成（从哪儿来 / 去哪儿 / 是否重点当前页）。
    void navigationApplied(int sourceIndex, int targetIndex, bool reselected);

protected:
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void applyColors();

    QVBoxLayout *m_layout = nullptr;
    QVBoxLayout *m_navLayout = nullptr;
    QWidget *m_caption = nullptr;
    QWidget *m_footer = nullptr;
    QAbstractButton *m_minimize = nullptr;
    QAbstractButton *m_close = nullptr;
    QList<QAbstractButton *> m_navButtons;
    int m_currentNav = -1;
    QString m_brand;
    bool m_applyingColors = false;
    /// 选中指示从哪一颗滑过来（-1 = 不滑）、滑到哪了（0~1）。
    int m_pillFrom = -1;
    qreal m_pillProgress = 1.0;
    QVariantAnimation *m_pillAnimation = nullptr;
};

/**
 * @brief 轨上 / 窗口按钮条上的一颗按钮：底与字形都是自己画的（颜色在代码里，能对着像素断言）。
 */
class TitleBarButton : public QAbstractButton {
    Q_OBJECT

public:
    enum class Glyph { Minimize, Close, Text };
    /// 导航图标（细线条）。
    enum class NavIcon { None, Home, Instances, Download, Settings, Agent };

    TitleBarButton(Glyph glyph, const QColor &hoverColor, const QColor &hoverInk, QWidget *parent = nullptr);

    void setLabel(const QString &text) { setText(text); }
    QString label() const { return text(); }
    void setNavIcon(NavIcon icon);
    NavIcon navIcon() const { return m_icon; }

    void setCurrent(bool current);
    bool isCurrent() const { return m_current; }
    bool isHovered() const { return m_hovered; }

    void setBaseColor(const QColor &color);
    void setInkColor(const QColor &color);
    void setHoverColors(const QColor &hoverColor, const QColor &hoverInk);

protected:
    void paintEvent(QPaintEvent *event) override;
    bool event(QEvent *event) override;

private:
    Glyph m_glyph;
    NavIcon m_icon = NavIcon::None;
    QColor m_hoverColor;
    QColor m_hoverInk;
    QColor m_baseColor = Qt::transparent;
    QColor m_ink;
    QVariantAnimation *m_hoverAnimation = nullptr;
    qreal m_hoverProgress = 0.0;
    bool m_hovered = false;
    bool m_current = false;
};
