/**
 * @file TitleBar.hpp
 * @brief 顶上那条**自己画的**标题栏：左边一个草方块 + 启动器名字，右边最小化与关闭。
 * @author 0x7fffff
 * @date 2026-09-25
 *
 * 用户原话：「能把整个外边框设计成完全自定义的吗，就是不用默认的那个顶上一条白色的那个，
 * 而是用pcl的那种自己设计顶上」。
 *
 * 【它和 WindowChrome 是两件事】
 *   · `ui/WindowChrome`：把系统那条标题栏**去掉**（Win32 那一层，见它的文件头）；
 *   · 本文件：去掉之后顶上留出来的那块地方**摆什么**。
 * 所以这里一个 Win32 调用都没有 —— 搬不走的东西在 WindowChrome 里，这一份是纯控件。
 *
 * 【条上有几样东西】
 *   1. 左边一颗草方块（`VersionIcons::grassBlock`，和版本树那几行同一张图）+ 启动器名字；
 *   2. 右边两颗按钮：最小化（—）与关闭（×），字形同样出自 `VersionIcons`（自己画的，
 *      仓库里没有图片文件）；
 *   3. 中间那一片**什么都没有** —— 拖窗靠的是 `WindowChrome::handleNativeEvent()` 把这一片
 *      判成 HTCAPTION（系统拖窗 + 贴边吸附白拿），这里不需要任何代码。
 *
 * NOTE: 不做最大化。主窗口本来就不许最大化（`MainWindowUI::setupUi` 里摘了
 *       `Qt::WindowMaximizeButtonHint`，`WindowChrome` 加回外框样式时也没给 WS_MAXIMIZEBOX），
 *       所以这里没有第三颗按钮 —— 有按钮有、点下去没反应是最差的一种。
 *
 * NOTE: 那两颗按钮**上面**留的空白（buttonInset()）不能小于系统那圈缩放边：条的上沿压着
 *       缩放边，按钮压进去就成了"按按钮最上面几个像素 = 缩放窗口"。这个数由
 *       `WindowChrome::resizeBorder()` 在验证工程里对着核（见 ui_check 那条断言）。
 */

#pragma once

#include <QAbstractButton>
#include <QColor>
#include <QWidget>

class TitleBarButton;

/**
 * @brief 顶上那条自己画的标题栏。
 *
 * 点最小化/关闭**不自己动手**（不去 call `window()->showMinimized()`），只发信号 ——
 * 这样它在验证工程里能单独摆出来、单独点，不用真开窗。
 */
class TitleBar : public QWidget {
    Q_OBJECT

public:
    explicit TitleBar(QWidget *parent = nullptr);

    /// 条高（逻辑像素）。**唯一出处**：`MainWindowUI` 摆它、验证工程量它、写断言都引这里。
    static int barHeight();
    /// 两颗按钮的边长（逻辑像素）。
    static int buttonSize();
    /// 两颗按钮**上面**留的空白（见文件头那条 NOTE）。它必须 ≥ `WindowChrome::resizeBorder()`。
    static int buttonInset();

    /// 右边那两颗按钮。验证工程靠它们点那两下（条上只有这两个子控件，见 .cpp 里的 NOTE）。
    QAbstractButton *minimizeButton() const { return m_minimize; }
    QAbstractButton *closeButton() const { return m_close; }

    /// 条上写着的那行字（现在就是窗口标题）。给断言用，不去猜字体怎么摆的。
    QString titleText() const;

signals:
    /// 那颗「—」被点了。
    void minimizeRequested();
    /// 那颗「×」被点了。
    void closeRequested();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QAbstractButton *m_minimize = nullptr;
    QAbstractButton *m_close = nullptr;
};

/**
 * @brief 条上的一颗方形按钮：底和字形都是自己画的。
 *
 * 为什么不用 QToolButton + QSS：这一条上只有两颗按钮，颜色只有"普通 / 悬停"两档，
 * 画出来十几行、颜色全在代码里 ⇒ 能直接对着像素断言（和 `VersionIcons` 那一路的理由一样）。
 * 继承 `QAbstractButton` 是为了白拿 hover 跟踪、按下高亮、`clicked()` 与 `click()`（验证工程要用）。
 */
class TitleBarButton : public QAbstractButton {
    Q_OBJECT

public:
    /// 画哪种字形。其余一切（底色、悬停色、墨色）由构造参数给。
    enum class Glyph { Minimize, Close };

    /**
     * @param glyph      画哪种字形。
     * @param hoverColor 悬停时铺的底色（关闭那颗是红 —— 那个红来自
     *                   `MainWindowUI::folderDangerColor()`，全仓只有一个出处）。
     * @param hoverInk   悬停时字形的颜色（关闭那颗悬停时字形要转白，不然红底上看不清）。
     * @param parent     父控件。
     */
    TitleBarButton(Glyph glyph, const QColor &hoverColor, const QColor &hoverInk, QWidget *parent = nullptr);

    /// 现在是不是悬停态（验证工程要能问，不然"悬停变红"这件事没法验）。
    bool isHovered() const { return m_hovered; }

    /// 不铺底色时那格的底（默认是全透 —— 条自己的底色透上来）。
    void setBaseColor(const QColor &color);
    /// 不悬停时字形的颜色（默认取条上的正文色）。
    void setInkColor(const QColor &color);

protected:
    void paintEvent(QPaintEvent *event) override;
    bool event(QEvent *event) override;

private:
    Glyph m_glyph;
    QColor m_hoverColor;
    QColor m_hoverInk;
    QColor m_baseColor = Qt::transparent;
    QColor m_ink;
    bool m_hovered = false;
};
