/**
 * @file FileOperationRecoveryController.hpp
 * @brief 「继续未完成操作」：把一份 `OperationReport` 里还没做完的文件操作接着做完。
 *
 * 任务中心只负责**聚合状态和操作**（那是它的规矩），所以真正的续做在这里：
 * · 世界导入 —— 复用同一份暂存，已拷好且内容一致的文件跳过，缺的补上再发布；
 * · 迁移 / 升级副本 —— 重新跑一遍「只增不删」的搬文件与收尾校验（本来就幂等）；
 * · 单文件复制 —— 目标已存在按当时选的冲突策略处理，否则原子重写一次。
 *
 * 续做**不重新问一遍冲突**：用报告里记下的那个选择。做不完时把
 * 「为什么做不完 + 每一项的状态 + 下一步」写成 `<报告>.failure.txt`，并把路径告诉用户
 * （见 `writeFailureReport()`）—— 那是"实在无法完成"时唯一有用的产物。
 */

#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <functional>

#include "operations/OperationReport.hpp"
#include "tasks/TaskSnapshot.hpp"

class OverlayHost;

class FileOperationRecoveryController final : public QObject {
    Q_OBJECT

public:
    struct Host {
        std::function<QString(const QString &)> displayName;
        /// 实例正在跑 / 正在被另一项文件操作占用（续做前要重新问一次）。
        std::function<bool(const QString &)> isInstanceBusy;
        std::function<void()> refreshInstances;
        std::function<void(const QString &)> log;
        std::function<Tasks::TaskId(Tasks::TaskKind, const QString &, const QString &,
                                    const QList<Tasks::TaskStep> &)>
            beginTask;
        std::function<void(Tasks::TaskId, const QString &)> setTaskDetail;
        std::function<void(Tasks::TaskId, int, Tasks::TaskStepState)> setTaskStepState;
        std::function<void(Tasks::TaskId, bool, const QString &)> finishTask;
        /// 收尾之后把「继续未完成操作」那一位写到账本上（报告钥匙）。
        std::function<void(Tasks::TaskId, const QString &)> setTaskRecovery;
        /// 续做**做完**之后，把原来那条任务上挂着的恢复入口按报告钥匙清掉。
        ///
        /// 不请的话：成功之后报告和 job 都删了，而当初那条失败任务上那颗「继续未完成操作」
        /// 还在 —— 再点一下只会得到"这一项的现场已经不在"，而屏幕上看起来像是我们没做完。
        std::function<void(const QString &)> clearTaskRecovery;
    };

    FileOperationRecoveryController(OverlayHost *overlay, Host host, QObject *parent = nullptr);

    /// 登记一份报告：写盘、清掉本模块的陈旧暂存、记住这份可以续。@p problem 写失败原因。
    bool registerReport(const OperationReport::Report &report, QString *problem = nullptr);
    bool has(const QString &token) const;
    /// 任务中心那一颗「继续未完成操作」：是报告就开一趟新任务把它补完。
    bool resume(const QString &token);
    /// 报告正文（读盘一份最新的；读不到就用内存里的）。
    OperationReport::Report reportFor(const QString &token) const;
    QString reportPathFor(const QString &token) const;
    /// 「为什么还是没做完」写成 `<报告>.failure.txt`，返回路径（失败时空）。
    QString writeFailureReport(const QString &token, const QString &reason);
    /// 启动 / 登记时清理本模块自己的陈旧暂存（只碰 `cache/operation-staging/<uuid>`）。
    QStringList cleanupStaleStaging(const QString &dataRoot);

private:
    struct Job {
        QString reportPath;
        QString dataRoot;
        OperationReport::Report report;
    };
    void runResume(const Job &job, Tasks::TaskId task);
    bool resumeWorldImport(const Job &job, Tasks::TaskId task, QString *problem);
    bool resumeTransfer(const Job &job, Tasks::TaskId task, QString *problem);
    bool resumeFileCopy(const Job &job, Tasks::TaskId task, QString *problem);
    /// 续做之后把报告的计数与完成态刷新，并按结果收尾任务 / 重新挂恢复入口。
    void settle(Tasks::TaskId task, OperationReport::Report &report, const QString &reportPath,
                bool ok, bool cancelled, const QString &problem);
    void log(const QString &text) const;

    OverlayHost *m_overlay = nullptr;
    Host m_host;
    QHash<QString, Job> m_jobs;
    /// 正在续做的报告钥匙（R5）：同一份报告连点两次「继续」，两条续做会共用同一个
    /// stagingDir 和目标文件 —— 世界导入 / 批量复制会被写坏。收尾（成功、失败、取消、
    /// 连开都开不起来）时移出。
    QSet<QString> m_resuming;
};
