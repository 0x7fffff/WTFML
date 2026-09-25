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
 *   1. 左边一颗草方块（`VersionIcons::grassBlock`，和版本树那几行同一张图）+ 那五个字母
 *      （`brandText()`，**写死的品牌名**，不再跟窗口标题走）；
 *   2. 紧跟着**四个导航按钮**（主页 / 实例 / 下载 / 设置）—— 也就是把原来贴在内容区顶上
 *      那条选项卡栏搬到这儿来（用户原话：「把四个选项卡放到最顶上去，通过顶端按钮打开
 *      四个功能」）。按钮只发 `navRequested(下标)`，谁去翻页是调用方的事；
 *   3. 右边两颗按钮：最小化（—）与关闭（×），字形同样出自 `VersionIcons`（自己画的，
 *      仓库里没有图片文件）；
 *   4. 中间那一片**什么都没有** —— 拖窗靠的是 `WindowChrome::handleNativeEvent()` 把这一片
 *      判成 HTCAPTION（系统拖窗 + 贴边吸附白拿），这里不需要任何代码。
 *
 * 【为什么导航按钮摆在这儿而不是把 QTabBar 搬出来】
 * `QTabWidget::setTabBar()` 换得掉那一份，但标签栏的**几何**仍然归 QTabWidget 管
 * （位置、圆角、和内容区那条接缝都是它画的），搬出来只会得到一个"画风不对、位置还会被
 * 拽回去"的条。所以这边走的是"按钮 + 隐藏标签栏"：`MainWindowUI::setupUi()` 里
 * `tabWidget->tabBar()->hide()`，Qt 的 `initStyleOption()` 里那句 `isVisibleTo()` 一判，
 * `tabBarSize` 就归零、内容页于是铺满整个 QTabWidget —— 见那一处的 NOTE。
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
#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

class QHBoxLayout;
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
    /// 三处按钮（两颗窗口按钮 + 每一颗导航）的**高**（逻辑像素）。
    static int buttonSize();
    /// 按钮**上面**留的空白（见文件头那条 NOTE）。它必须 ≥ `WindowChrome::resizeBorder()`
    /// —— 两颗窗口按钮和四颗导航按钮**都**压在这条规矩上（它们的高都是 buttonSize()）。
    static int buttonInset();
    /// 每一颗导航按钮的宽（逻辑像素）。
    static int navItemWidth();
    /// 品牌那五个字母。**唯一出处** —— 画的是它，断言比的是它。
    static QString brandText();

    /// 右边那两颗按钮。验证工程靠它们点那两下。
    QAbstractButton *minimizeButton() const { return m_minimize; }
    QAbstractButton *closeButton() const { return m_close; }

    /// 摆上（或换掉）那一排导航按钮。标签一律由调用方给 —— 现在给的是
    /// `QTabWidget::tabText()`，于是"四个选项卡"的名字只有一处出处。
    /// @param labels 从左到右的标签；空表 = 这一排按钮整个没有。
    void setNavItems(const QStringList &labels);

    /// 把第 @p index 颗标成"当前这一页"（其余几颗退回次要色）。越界 = 一颗都不标。
    void setCurrentNav(int index);

    /// 现在标着的是第几颗（一颗都没标 = −1）。给断言用。
    int currentNav() const { return m_currentNav; }
    /// 导航按钮的颗数。
    int navCount() const { return static_cast<int>(m_navButtons.size()); }
    /// 第 @p index 颗导航按钮（越界给 nullptr）。
    QAbstractButton *navButton(int index) const;

    /// 条上写着的那行字 —— 就是 `brandText()`（**不再跟窗口标题走**：用户要的是"顶上标题
    /// 换成五个字母"，而窗口标题还要在 Alt-Tab/任务栏里能读，见 `MainWindow::setWindowTitle`）。
    QString titleText() const;

signals:
    /// 那颗「—」被点了。
    void minimizeRequested();
    /// 那颗「×」被点了。
    void closeRequested();
    /// 第 @p index 颗导航按钮被点了（**只报"点的是哪一颗"**，翻不翻页由调用方决定）。
    void navRequested(int index);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QHBoxLayout *m_layout = nullptr;
    QAbstractButton *m_minimize = nullptr;
    QAbstractButton *m_close = nullptr;
    QList<QAbstractButton *> m_navButtons;
    int m_currentNav = -1;
};

/**
 * @brief 条上的一颗按钮（窗口那两颗方块按钮 + 那四颗导航按钮都是它）：底和字形都是自己画的。
 *
 * 为什么不用 QToolButton + QSS：这一条上按钮的颜色只有"普通 / 悬停 / 当前"三档、
 * 形状只有"图标"和"一行字"两种，画出来十几行、颜色全在代码里 ⇒ 能直接对着像素断言
 * （和 `VersionIcons` 那一路的理由一样）。
 * 继承 `QAbstractButton` 是为了白拿 hover 跟踪、按下高亮、`clicked()` 与 `click()`（验证工程要用）。
 */
class TitleBarButton : public QAbstractButton {
    Q_OBJECT

public:
    /// 画哪种字形。其余一切（底色、悬停色、墨色）由构造参数给。
    /// `Text` 那一档画的是**标签文字**（导航按钮用），前两档画的是 `VersionIcons` 里的字形。
    enum class Glyph { Minimize, Close, Text };

    /**
     * @param glyph      画哪种字形。
     * @param hoverColor 悬停时铺的底色（关闭那颗是红 —— 那个红来自
     *                   `MainWindowUI::folderDangerColor()`，全仓只有一个出处）。
     * @param hoverInk   悬停时字形的颜色（关闭那颗悬停时字形要转白，不然红底上看不清）。
     * @param parent     父控件。
     */
    TitleBarButton(Glyph glyph, const QColor &hoverColor, const QColor &hoverInk, QWidget *parent = nullptr);

    /// `Glyph::Text` 那一档的标签。走 `QAbstractButton::text()` 那一份（画的就是它），
    /// 这里只是给个同名同义的入口，免得调用方还要知道"标签存在基类里"。
    void setLabel(const QString &text) { setText(text); }
    QString label() const { return text(); }

    /// 这一颗是不是"当前这一页"。**只影响画法**（加粗 + 底下一条横线），
    /// 选中态本身（当前是第几页）不归这里管 —— 那一份在 `TitleBar::m_currentNav`。
    void setCurrent(bool current);
    bool isCurrent() const { return m_current; }

    /// 现在是不是悬停态（验证工程要能问，不然"悬停变红"这件事没法验）。
    bool isHovered() const { return m_hovered; }

    /// 不铺底色时那格的底（默认是全透 —— 条自己的底色透上来）。
    void setBaseColor(const QColor &color);
    /// 不悬停时字形的颜色（默认取条上的正文色）。
    /// NOTE: 导航按钮那几颗**每次改当前页都重设一次**：当前那颗用正文色、其余几颗用
    ///       `QPalette::PlaceholderText`（全仓"比正文浅一档的次要操作"就是它，
    ///       `VersionTreeDelegate` 里那个取消小叉用的也是它）。所以这里存的是**静态**墨色，
    ///       悬停时另外走 `m_hoverInk`。
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
    /// 当前这一页（只有导航按钮会置位）。
    bool m_current = false;
};
