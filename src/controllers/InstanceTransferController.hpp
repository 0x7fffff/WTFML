#pragma once

#include "tasks/TaskId.hpp"
#include "tasks/TaskSnapshot.hpp"

#include <QObject>
#include <functional>

class LauncherManager;
class OverlayHost;
class VersionManager;

class InstanceTransferController final : public QObject {
    Q_OBJECT
public:
    struct Host {
        std::function<bool(const QString &)> isBeingModified;
        std::function<QString(const QString &)> displayName;
        std::function<QString(const QString &, const QString &)> instanceDirForKey;
        std::function<void()> refreshInstances;
        std::function<void(const QString &)> log;
        // 任务中心那一格（coding plan §9.9「不在 UI 线程直接复制整个目录」）。全可空。
        std::function<Tasks::TaskId(Tasks::TaskKind, const QString &, const QString &,
                                    const QList<Tasks::TaskStep> &)>
            beginTask;
        std::function<void(Tasks::TaskId, const QString &)> setTaskDetail;
        std::function<void(Tasks::TaskId, qint64, qint64)> setTaskProgress;
        std::function<void(Tasks::TaskId, int, Tasks::TaskStepState)> setTaskStepState;
        std::function<void(Tasks::TaskId, bool, const QString &)> finishTask;
    };
    InstanceTransferController(VersionManager *versions, LauncherManager *launcher,
                               OverlayHost *overlay, Host host, QObject *parent = nullptr);

    /// 「复制实例」**同步**那一版：四道闸 + 拷树都在调用线程里做完，回"成没成"。
    ///
    /// NOTE: 验证工程（`mw_check` / 升级副本）按同步约定钉着它；界面上的那一颗「复制实例」
    ///       走下面那个后台版 —— 拷一整个实例动辄几十秒，不能把 UI 线程按住。
    bool copyInstance(const QString &sourceId, const QString &destGameFolder,
                      const QString &newName);

    /// 同一套闸，但**拷树在后台线程**（见 .cpp 里那段 NOTE）。完成之后回调回到主线程；
    /// 任务中心那一项会把「规划 → 执行 → 验证」三步走完。
    void copyInstanceInBackground(const QString &sourceId, const QString &destGameFolder,
                                  const QString &newName,
                                  std::function<void(bool, const QString &)> whenDone = {});

    /// 后台那一份还在拷吗（再按一次「复制实例」会被挡下来）。
    bool copying() const { return m_copyBusy; }

    bool importWorld(const QString &instanceId, const QString &worldDir, const QString &name);
    bool renameWorld(const QString &instanceId, const QString &worldDir, const QString &newName);

private:
    QString displayName(const QString &id) const;
    void log(const QString &text) const;
    void announceCopySuccess(const QString &sourceId, const QString &destGameFolder,
                             const QString &name);

    VersionManager *m_versions = nullptr;
    LauncherManager *m_launcher = nullptr;
    OverlayHost *m_overlay = nullptr;
    Host m_host;
    bool m_copyBusy = false;
    Tasks::TaskId m_copyTaskId = Tasks::invalidTaskId();
};
