/**
 * @file InstanceUpdateFlow.hpp
 * @brief 当前实例的目标选择、核心准备与更新流程。
 */
#pragma once
class MainWindow;
class InstanceSettingsDialog;
class QString;

class InstanceUpdateFlow final {
public:
    static void attach(MainWindow *owner, InstanceSettingsDialog *dialog, const QString &sourceId);
};
