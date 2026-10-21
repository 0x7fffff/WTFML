/**
 * @file InstanceUpdateWizard.hpp
 * @brief 独立实例更新向导，固定导航并隔离控件滚轮。
 */
#pragma once
#include <QWizard>
#include <functional>

class OverlayHost;
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
private:
    OverlayHost *m_promptHost = nullptr;
    bool m_running = false;
};
