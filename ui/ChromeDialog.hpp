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
 */
#pragma once

#include <QDialog>

class QAbstractButton;
class QLabel;
class WindowChrome;

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

protected:
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    QWidget *m_strip = nullptr;
    QLabel *m_title = nullptr;
    QWidget *m_body = nullptr;
    QAbstractButton *m_maximize = nullptr;
    WindowChrome *m_chrome = nullptr;
};
