#pragma once

#include "tasks/TaskId.hpp"
#include "tasks/TaskSnapshot.hpp"

#include <QHash>
#include <QObject>
#include <atomic>
#include <functional>
#include <memory>

class LauncherManager;
class OverlayHost;
class VersionManager;
namespace InstanceUpgrade { struct Request; struct Core; }

class InstanceTransferController final : public QObject {
    Q_OBJECT
public:
    struct Host {
        std::function<bool(const QString &)> isBeingModified;
        std::function<QString(const QString &)> displayName;
        std::function<QString(const QString &, const QString &)> instanceDirForKey;
        std::function<void()> refreshInstances;
        std::function<void(const QString &)> log;
        /// 后台拷树期间把源实例记进/摘出「正在搬文件」名单（产品那边就是
        /// `MainWindow::m_fileOperationInstanceIds`）。不记的话，复制跑到一半用户
        /// 照样能启动这一份 —— 游戏一边写存档、拷树一边读，副本里就是半截世界。
        std::function<void(const QString &, bool)> setFileOperationBusy;
        // 任务中心那一格（coding plan §9.9「不在 UI 线程直接复制整个目录」）。全可空。
        std::function<Tasks::TaskId(Tasks::TaskKind, const QString &, const QString &,
                                    const QList<Tasks::TaskStep> &)>
            beginTask;
        std::function<void(Tasks::TaskId, const QString &)> setTaskDetail;
        std::function<void(Tasks::TaskId, qint64, qint64)> setTaskProgress;
        std::function<void(Tasks::TaskId, int, Tasks::TaskStepState)> setTaskStepState;
        std::function<void(Tasks::TaskId, bool, const QString &)> finishTask;
        /// 用户按了「取消」、工人随即停下之后，把账本里那一项收成 Cancelled
        /// （**不是** Failed：取消不算失败，不该亮红点、也不该弹失败提示）。
        std::function<void(Tasks::TaskId)> markTaskCancelled;
    };
    InstanceTransferController(VersionManager *versions, LauncherManager *launcher,
                               OverlayHost *overlay, Host host, QObject *parent = nullptr);

    /// 「复制实例」**同步**那一版：四道闸 + 拷树都在调用线程里做完，回"成没成"。
    ///
    /// NOTE: 验证工程（`mw_check` / 升级副本）按同步约定钉着它；界面上的那一颗「复制实例」
    /// 走下面那个后台版 —— 拷一整个实例动辄几十秒，不能把 UI 线程按住。
    bool copyInstance(const QString &sourceId, const QString &destGameFolder,
                      const QString &newName);

    /// 同一套闸，但**拷树在后台线程**（见 .cpp 里那段 NOTE）。完成之后回调回到主线程；
    /// 任务中心那一项会把「规划 → 执行 → 验证」三步走完。
    void copyInstanceInBackground(const QString &sourceId, const QString &destGameFolder,
                                  const QString &newName,
                                  std::function<void(bool, const QString &)> whenDone = {},
                                  bool announce = true);

    /// 后台那一份还在拷吗（再按一次「复制实例」会被挡下来）。
    bool copying() const { return m_copyBusy; }
    void upgradeInPlace(QString sourceId, QString targetId,
                        std::function<void(bool, const QString &)> whenDone = {},
                        const QStringList &reviewedKeptMods = {}, bool reviewedModPlan = false);
    void rollbackUpgrade(QString sourceId,
                         std::function<void(bool, const QString &)> whenDone = {});

    bool importWorld(const QString &instanceId, const QString &worldDir, const QString &name);
    /// 界面入口：复制在工作线程执行，完成后才刷新；同步入口保留给兼容调用。
    void importWorldInBackground(const QString &instanceId, const QString &worldDir,
                                 const QString &name,
                                 std::function<void(bool, const QString &)> whenDone = {});
    /// 「删除世界」那一趟：确认过了才进来，整棵目录树在工作线程里删。
    ///
    /// NOTE: 删除**不提供取消**。删到一半停下留下的是"半截世界"，比删完更难收拾；
    /// 账本按 `TaskKind::Other` 记账，默认能力位里本来就没有「取消」。
    /// 删不干净时照实报失败（`removeTreeWithoutFollowingLinks()` 会尽力删完剩下的）。
    void deleteWorldInBackground(const QString &instanceId, const QString &worldDir,
                                 std::function<void(bool, const QString &)> whenDone = {});
    /// 任务中心那一项此刻按「取消」真的停得下来吗（那一颗按钮的唯一判据）。
    bool canCancelTask(Tasks::TaskId id) const;
    /// 掐掉一次后台导入。复制是**协作式**取消：工人每 1 MiB 看一次取消位；
    /// 已经发布到 saves 的正式世界不会因为"取消得晚了一点"被删掉。
    bool cancelTask(Tasks::TaskId id);
    bool renameWorld(const QString &instanceId, const QString &worldDir, const QString &newName);

private:
    void runUpgradeJob(const InstanceUpgrade::Request &request, const InstanceUpgrade::Core &core,
                       const QStringList &lockedIds, bool restore,
                       std::function<void(bool, const QString &)> whenDone);
    QString displayName(const QString &id) const;
    void log(const QString &text) const;
    void announceCopySuccess(const QString &sourceId, const QString &destGameFolder,
                             const QString &name);

    /// 一次后台导入的取消状态（工人只碰这两个原子位，主线程管这张表）。
    struct ImportState {
        std::atomic_bool abort{false};
        /// 「是用户按的取消」和「启动器要关了 / 超时」是两回事：前者收成 Cancelled、
        /// 不弹失败提示；后者照旧按失败说清楚。
        std::atomic_bool cancelledByUser{false};
    };

    VersionManager *m_versions = nullptr;
    LauncherManager *m_launcher = nullptr;
    OverlayHost *m_overlay = nullptr;
    Host m_host;
    bool m_copyBusy = false;
    Tasks::TaskId m_copyTaskId = Tasks::invalidTaskId();
    QHash<Tasks::TaskId, std::shared_ptr<ImportState>> m_importStates;
};
