/**
 * @file ChromeDialog.hpp
 * @brief 带自绘窄标题条的独立对话框：系统标题栏收掉（WindowChrome），顶上一条标题 + 关闭钮。
 *
 * 和游戏日志窗同一套做法（WindowChrome 收标题栏、TitleBar::styleCaptionButtons 配色、
 * 主窗那张磨砂风景铺底），只是条更窄、默认只留关闭钮。标题字就是 `windowTitle()`，
 * 调用方照旧 `setWindowTitle()`。内容排在 `body()` 上：
 *
 *     auto *layout = new QVBoxLayout(body());
 *
 * 往 `body()` 的布局里加的控件，哪怕是以 `this` 为父 new 出来的，也会被布局挪到 body 下，
 * `findChild` 照样找得到。
 *
 * 换不了基类的窗（QWizard）直接 new 一条 `ChromeStrip` 摆到顶上、自己挂 WindowChrome，
 * 见 InstanceUpdateWizard。
 */
#pragma once

#include <QDialog>

class QAbstractButton;
class QLabel;
class WindowChrome;

/// 窄标题条本体：标题跟着窗口的 windowTitle、× = reject()（不是 QDialog 就 close()），
/// `minMax` 时多给最小化 / 最大化。它自己盯窗口的标题 / 状态变化，宿主什么都不用转。
class ChromeStrip final : public QWidget {
public:
    ChromeStrip(QWidget *window, bool minMax);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *m_window;
    QLabel *m_title = nullptr;
    QAbstractButton *m_maximize = nullptr;
};

class ChromeDialog : public QDialog {
    Q_OBJECT

public:
    /// `minMax` = 除了关闭再给最小化 / 最大化（大一点、想摊开看的窗才要）。
    explicit ChromeDialog(QWidget *parent = nullptr, bool minMax = false);

    QWidget *body() const { return m_body; }
    QWidget *titleStrip() const { return m_strip; }
    /// 窄标题条的高（比主窗那条 40 矮）。
    static int stripHeight();
    static QString titleStripName() { return QStringLiteral("chromeDialogTitleStrip"); }
    static QString titleLabelName() { return QStringLiteral("chromeDialogTitle"); }
    static QString closeButtonName() { return QStringLiteral("chromeDialogClose"); }
    /// 铺底：主窗那张磨砂风景；没有主窗时铺窗口色（WindowChrome 开了半透明，别透成空的）。
    static void paintBackdrop(QWidget *window);

protected:
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
    void paintEvent(QPaintEvent *event) override;

private:
    ChromeStrip *m_strip = nullptr;
    QWidget *m_body = nullptr;
    WindowChrome *m_chrome = nullptr;
};
