#pragma once
/**
 * @file GlassPopover.hpp
 * @brief 窗口里浮起的一张玻璃卡片：任务中心浮层、主页实例切换都用它。
 *
 * 是主窗口里的子卡片（不是 Qt::Popup 顶层窗口），透得出整窗磨砂。外观一套：圆角 14、
 * 不描边、柔和大阴影、顶上一道极淡高光；打开时 150 ms 淡入（「界面动效」关掉时直接出现）。
 * 点外面、按 Esc 收起；点锚点本身不归这里管（锚点按钮自己 toggle）。子类只管内容和 place()。
 */

#include <QFrame>
#include <QPointer>

class QPainter;

class GlassPopover : public QFrame {
public:
    explicit GlassPopover(QWidget *anchor);
    ~GlassPopover() override;

    QWidget *anchor() const { return m_anchor; }
    /// 摆好位置、显示、淡入、拿焦点（Esc 要能关）。
    void popUp();
    /// 收掉这个窗里开着的所有弹层（整页摆上来时让位用，见 OverlayHost::openPage()）。
    static void closeAllIn(QWidget *window);

    static constexpr int kRadius = 14;
    static constexpr int kFadeMs = 150;

    /// 浮层那一块面：圆角 + 磨砂玻璃 + 顶上 1px 高光，不描边。
    ///
    /// 窗口里的卡片（本类）、以及**顶层浮层**（下拉弹层、菜单、tooltip —— 那是 Qt 自己的
    /// 弹出窗口，装不进窗口里）都画这一份，免得两边慢慢长得不一样。
    /// @param fillBase true = 先在底下垫一层页底色（窗口里那张卡片要盖住下面的正文）；
    ///                 false = 只压玻璃色，给挂了系统瞬态亚克力的顶层浮层用。
    static void paintSurface(QPainter &painter, const QRectF &rect, qreal radius, bool fillBase);

protected:
    /// 按锚点摆位置。主窗口尺寸变了会再叫一次。
    virtual void place() = 0;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

    QPointer<QWidget> m_anchor;

private:
    void applyShadow();
};
