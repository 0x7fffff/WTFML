#pragma once

#include "manage/ModpackExport.hpp"
#include "tasks/TaskId.hpp"
#include "tasks/TaskSnapshot.hpp"

#include <QObject>
#include <functional>

class LauncherSettings;
class OverlayHost;
class VersionManager;

class ModpackExportController final : public QObject {
    Q_OBJECT
public:
    /// 任务中心那一格的口子（coding plan §9.9：导出也走「规划 → 兼容性确认 → 执行 →
    /// 验证」，不在 UI 线程里闷头打包）。四个回调都允许为空 —— 验证工程里只关心
    /// 导出本身时可以一个都不接。
    struct Host {
        std::function<bool(const QString &)> isBeingModified;
        std::function<QString(const QString &)> displayName;
        std::function<void(const QString &)> log;
        std::function<void(const QString &)> setStatus;
        std::function<Tasks::TaskId(Tasks::TaskKind, const QString &, const QString &,
                                    const QList<Tasks::TaskStep> &)>
            beginTask;
        std::function<void(Tasks::TaskId, const QString &)> setTaskDetail;
        std::function<void(Tasks::TaskId, qint64, qint64)> setTaskProgress;
        std::function<void(Tasks::TaskId, int, Tasks::TaskStepState)> setTaskStepState;
        /// 收尾：`success` 为真走 `succeed()`，为假走 `fail(原因)`。
        std::function<void(Tasks::TaskId, bool, const QString &)> finishTask;
        /// 用户按了「取消」（兼容性确认那一问选了取消）。和失败分开记。
        std::function<void(Tasks::TaskId)> cancelTask;
    };
    ModpackExportController(VersionManager *versions, LauncherSettings *settings,
                            OverlayHost *overlay, Host host, QObject *parent = nullptr);
    void exportPack(const QString &versionId, const QString &targetPath);
    bool busy() const { return m_busy; }

private:
    void continueExport(const ModpackExport::ExportPlan &plan, const QString &targetPath,
                        ModpackExport::Format format);
    QString displayName(const QString &id) const;
    void log(const QString &text) const;
    void setStatus(const QString &text) const;

    // ---------- 任务中心那一格 ----------
    //
    // 步骤表是这一层的**唯一出处**（`exportStepNames()`）：起手按它建任务，之后只按
    // 下标改状态。四步对着 coding plan 那条流程：规划 / 兼容性确认 / 执行 / 验证。
    QList<Tasks::TaskStep> initialSteps() const;
    void setTaskDetail(const QString &text) const;
    void setTaskProgress(qint64 doneUnits, qint64 totalUnits) const;
    void setTaskStepState(int index, Tasks::TaskStepState state) const;
    void finishTask(bool success, const QString &errorSummary = QString());
    void cancelTask();

    VersionManager *m_versions = nullptr;
    LauncherSettings *m_settings = nullptr;
    OverlayHost *m_overlay = nullptr;
    Host m_host;
    bool m_busy = false;
    Tasks::TaskId m_taskId = Tasks::invalidTaskId();
};
