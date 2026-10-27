#pragma once
#include <QDialog>
#include "presets/PresetModInstaller.hpp"

class QTreeWidget;
class QLabel;

class PresetModReview final : public QDialog {
public:
    explicit PresetModReview(const Presets::ModInstallPlan &plan, QWidget *parent = nullptr);
    Presets::PresetModInstaller::ReviewDecision decision() const { return m_decision; }
private:
    void confirm();
    Presets::ModInstallPlan m_plan;
    Presets::PresetModInstaller::ReviewDecision m_decision;
    QTreeWidget *m_rows;
    QLabel *m_problem;
};
