#include "Support.hpp"
int MwActivitySelectionCheck() {

        QTemporaryDir fixture(QDir::current().filePath(QStringLiteral(".activity-check-XXXXXX")));
        if (!fixture.isValid()) return 2;
        int failures = 0;
        const auto expect = [&failures](bool ok, const char *message) {
            printf("%s %s\n", ok ? "PASS" : "FAIL", message);
            if (!ok) ++failures;
        };
        const QString path = fixture.filePath(QStringLiteral("tasks.json"));
        ActivityCenterController activity;
        activity.tasks()->setHistoryPath(path);
        const auto speedTask = activity.begin(Tasks::TaskKind::Download, QStringLiteral("Speed"));
        activity.setTransferSpeed(speedTask, 1048576);
        expect(activity.item(activity.tasks()->snapshot(speedTask), false, false, false).bytesText.contains("/s"),
               "game download speed remains visible when total byte size is unknown");
        activity.finish(speedTask, true, QString(), false);
        expect(activity.transferSpeed(speedTask) < 0, "finished tasks do not retain a stale live speed");
        const auto failed = activity.begin(Tasks::TaskKind::Crash, QStringLiteral("Game error"), {},
            {{QStringLiteral("登录"), Tasks::TaskStepState::Done, {}},
             {QStringLiteral("检查"), Tasks::TaskStepState::Running, {}},
             {QStringLiteral("启动"), Tasks::TaskStepState::Pending, {}}});
        activity.finish(failed, false, QStringLiteral("crash"), false);
        const auto other = activity.begin(Tasks::TaskKind::Download, QStringLiteral("Other error"));
        activity.finish(other, false, QStringLiteral("network"), false);
        expect(activity.markSeen(QString::number(failed)), "visible failure clears unread indication");
        expect(!activity.item(activity.tasks()->snapshot(failed), false, false, false).attentionAcknowledged,
               "viewport visibility does not resolve attention");
        activity.markAllViewed();
        expect(!activity.item(activity.tasks()->snapshot(failed), false, false, false).attentionAcknowledged,
               "opening the task center does not acknowledge error attention");
        expect(activity.markViewed(QString::number(failed)), "viewing a failed task acknowledges its attention");
        expect(activity.item(activity.tasks()->snapshot(failed), false, false, false).attentionAcknowledged &&
                   !activity.item(activity.tasks()->snapshot(other), false, false, false).attentionAcknowledged,
               "attention acknowledgment is scoped to a single task");
        expect(activity.tasks()->snapshot(failed).state == Tasks::TaskState::Failed,
               "acknowledging an error never changes its failure result");
        expect(activity.tasks()->snapshot(failed).steps.size() == 3 &&
                   activity.tasks()->snapshot(failed).steps.at(0).state == Tasks::TaskStepState::Done &&
                   activity.tasks()->snapshot(failed).steps.at(1).state == Tasks::TaskStepState::Failed &&
                   activity.tasks()->snapshot(failed).steps.at(2).state == Tasks::TaskStepState::Pending,
               "failure retains workflow and marks only the active stage failed");
        const auto active = activity.begin(Tasks::TaskKind::Other, QStringLiteral("Active"));
        expect(!activity.markViewed(QString::number(active)) && !activity.markViewed(QStringLiteral("invalid")),
               "viewing active tasks or invalid keys does not acknowledge attention");
        ActivityCenterController restored;
        restored.tasks()->setHistoryPath(path);
        restored.loadHistory();
        expect(restored.item(restored.tasks()->snapshot(failed), false, false, false).attentionAcknowledged,
               "acknowledged attention survives history reload");
        expect(restored.tasks()->snapshot(active).state == Tasks::TaskState::Interrupted &&
                   restored.markViewed(QString::number(active)), "interrupted tasks can be acknowledged on viewing");
        // C2：「清除已完成」把行删了，**没看过的失败**那一份必须跟着收窄 ——
        // 否则 `markViewed()` 再也没有出口（它要求 `has(id)`），面板里一行不剩、底栏那颗
        // 红点却挂到重启为止。触发它不需要特殊操作：面板停在「进行中」或搜索没命中那一行
        // 时点「清除已完成」即可。顺带钉住"收窄之后要有人重画底栏"（clearFinished 里那一发
        // `activityChanged`）—— 少了它，红点要等账本下一次变动才灭。
        //
        // 位置摆在最后：`clearFinished()` 会把前面那些终态行从账本里删掉，而上面
        // `restored` 那几条读的正是盘上那些行。
        {
            const auto c2 = activity.begin(Tasks::TaskKind::Download, QStringLiteral("C2 error"));
            activity.finish(c2, false, QStringLiteral("boom"), false);
            expect(activity.isUnreadFailure(c2), "C2 前提：那一条失败还没被看过");
            int activityEmissions = 0;
            const QMetaObject::Connection counter = QObject::connect(
                &activity, &ActivityCenterController::activityChanged, &activity,
                [&activityEmissions] { ++activityEmissions; });
            activity.clearFinished();
            expect(!activity.tasks()->has(c2),
                   "C2 「清除已完成」把终态那一条收掉了");
            expect(!activity.isUnreadFailure(c2),
                   "C2 行没了，「没看过的失败」也跟着摘掉（红点的出处不再指着一个不存在的号）");
            expect(!activity.collapsedSummary(QDateTime::currentDateTime(), 10 * 60 * 1000).hasFailure,
                   "C2 收起态摘要里那颗红点真的灭了");
            expect(activityEmissions >= 1,
                   "C2 收窄之后自己再发一发 activityChanged（底栏立刻重画，不等下一次变动）");
            QObject::disconnect(counter);
        }
        // C3：每一类任务的默认能力表 —— **不能取消的必须说清为什么**。
        //
        // 用户看到的是详情页上"没有取消按钮"或者一颗按下去没反应的按钮；那份
        // `cancelBlockedReason` 是唯一一句解释（`TaskCenterPanel` 把它摆在详情页上）。
        // 判据两条，缺一条都会让那句话变成"有时候有、有时候没有"：
        // ① 允许取消的那三类（下载 / 安装 / 导入）**不许**带这句话 —— 否则详情页会一边
        //    露着「取消」、一边写着"这一项不支持取消"；
        // ② 明确不支持取消的那五类必须带一句非空的人话。
        // 崩溃报告与 `Other` 是兜底/终态档（`Other` 里真正长命的那些 —— 迁移、升级副本
        // —— 由各自的执行器覆盖能力表并自己写原因，见 InstanceDialogController），
        // 默认表对它们不表态。
        {
            const auto reasonOf = [](Tasks::TaskKind kind) {
                return ActivityCenterController::defaultCapabilities(kind).cancelBlockedReason;
            };
            const auto canCancelOf = [](Tasks::TaskKind kind) {
                return ActivityCenterController::defaultCapabilities(kind).canCancel;
            };
            for (Tasks::TaskKind kind : {Tasks::TaskKind::Download, Tasks::TaskKind::Install,
                                         Tasks::TaskKind::Import}) {
                expect(canCancelOf(kind) && reasonOf(kind).isEmpty(),
                       qPrintable(QStringLiteral("C3 允许取消的那一类（%1）不带「为什么不能取消」")
                                      .arg(int(kind))));
            }
            for (Tasks::TaskKind kind : {Tasks::TaskKind::Launch, Tasks::TaskKind::Export,
                                         Tasks::TaskKind::Backup, Tasks::TaskKind::Java,
                                         Tasks::TaskKind::Integrity}) {
                expect(!canCancelOf(kind) && !reasonOf(kind).trimmed().isEmpty(),
                       qPrintable(QStringLiteral("C3 不支持取消的那一类（%1）**写清了原因**")
                                      .arg(int(kind))));
            }
        }
        return failures == 0 ? 0 : 1;
    
    return 0;
}

