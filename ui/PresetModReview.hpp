#pragma once
#include "ChromeDialog.hpp"
#include "presets/PresetModInstaller.hpp"

class QTreeWidget;
class QLabel;

class PresetModReview final : public ChromeDialog {
public:
    explicit PresetModReview(const Presets::ModInstallPlan &plan, QWidget *parent = nullptr);
    Presets::PresetModInstaller::ReviewDecision decision() const { return m_decision; }
    /// 底部「确认安装 / 检查替代」那颗钮（不是标准 Ok 钮，测试按这个名字找它）。
    static QString confirmButtonName() { return QStringLiteral("presetModReviewConfirm"); }
private:
    void confirm();
    Presets::ModInstallPlan m_plan;
    Presets::PresetModInstaller::ReviewDecision m_decision;
    QTreeWidget *m_rows;
    QLabel *m_problem;
};
