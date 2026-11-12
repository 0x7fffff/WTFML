/**
 * @file InstanceUpdateWizard.hpp
 * @brief 独立实例更新向导，固定导航并隔离控件滚轮。
 */
#pragma once
#include <QWizard>
#include <functional>

class OverlayHost;
class ChromeStrip;
class WindowChrome;
class InstanceUpdateWizard final : public QWizard {
public:
    InstanceUpdateWizard(QWidget *target, QWidget *mods, QWidget *confirmation,
                         QWidget *progress, QWidget *parent = nullptr);
    void setStepReady(int step, bool ready);
    void setRunning(bool running);
    OverlayHost *promptHost() const { return m_promptHost; }
    std::function<bool()> cancelRequested;
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void reject() override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
private:
    OverlayHost *m_promptHost = nullptr;
    bool m_running = false;
    // 系统标题栏收掉、顶上换 ChromeDialog 那条窄条；QWizard 自己排的那整块内容往下挪一条。
    ChromeStrip *m_strip = nullptr;
    WindowChrome *m_chrome = nullptr;
    QWidget *m_content = nullptr;
};
