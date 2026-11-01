/**
 * @file ManualRecovery.hpp
 * @brief 「人工恢复目录」：人挑一份旧目录，把现在这一份整个换掉。
 *
 * 【它解决的是哪一种现场】
 * 还原事务在「旧目录已经挪走、新目录还没发布」之间被打断时，启动恢复能自己判断；
 * 但它**故意不判断的两侧目录都在**那种现场（`prepared`/`published` 且目标与旧目录
 * 同时存在）—— 那一趟到底想要新的还是旧的，只有人知道。以前只有「标记已检查」，
 * 用户想换回旧目录只能自己到资源管理器里改名，绕开了整套事务与运行状态检查。
 * 这一层把「换回去」变成一次**和还原同一个模子**的操作。
 *
 * 【为什么不是 `BackupFiles::restoreBackup()`】
 * 那个入口的源是一份 zip + 清单，会先解压、逐文件对账；这里的源**已经是一个目录**，
 * 没有清单可对。能复用的只有两段：`RestoreJournal` 的事务记录与启动恢复判据，以及
 * `DirectorySessionLock`。所以这一层只做两次改名（都与目标同一个父目录、同卷），
 * 记录在动盘之前落盘。
 *
 * 【四条规矩（计划里那句写的就是这四条）】
 *  · 新建可回滚事务：动盘之前把记录写成 `prepared`；两次改名之间断掉，启动恢复按
 *    记录把「现在这一份」放回原位（换回旧目录这件事等于没发生，两份都还在）。
 *  · 检查运行状态：调用方先把「游戏在跑 / 正被别的文件操作占用」判完并置 `inUse`；
 *    这一层再守一道数据目录会话锁（另一份启动器开着就不动盘）。
 *  · 保留双方：换下来的「现在这一份」改名到旁边的 `.wtfml-restore-previous-<uuid>`，
 *    **不删**；记录保持 `published` 留在 `cache/restore-transactions`，由「中断恢复
 *    检查」显示、由「标记已检查」归档。
 *  · 不清理未知暂存：这一层一个 `removeTree` 都不调，也不碰 `OwnedStaging` 的任何目录。
 *
 * 【同一个目标只留一条事务记录】
 * 如果挑中的旧目录本来就是某个待处理事务里的 `retained`，这里**改写那一条记录**
 * （新的 `retained` 指向刚换下来的那一份），而不是再写一条。两条记录都指向同一个
 * 目标时，启动恢复谁先跑谁赢，结果取决于文件名排序 —— 那种不确定不能留。
 */
#pragma once

#include <QString>
#include <QStringList>

namespace ManualRecovery {

/// 一次「用旧目录换回来」要动的那两只目录。
struct Request {
    /// 数据根（游戏文件夹）。两个目录都必须落在它里面。
    QString gameRoot;
    /// 现在这一份（被换下来的）。**不存在也行**：目标目录已经被删掉时，把旧目录
    /// 直接放回去就是这一档的全部内容。
    QString targetDir;
    /// 用户挑的那一份旧目录（要换上去的）。必须存在，且和 @ref targetDir 同一个父目录
    /// —— 事务记录与启动恢复都按「同父目录的两段改名」来判。
    QString replacementDir;
    /// 调用方已经判出来「这一份正在运行 / 正在被别的文件操作占用」。**true 时一个字节
    /// 都不动。** 这一位由界面那一侧填（它才知道游戏进程和实例忙碌表）；这一层不去猜。
    bool inUse = false;
};

struct Result {
    bool ok = false;
    /// 失败原因（直接摆给用户的人话）。成功时为空。
    QString problem;
    /// 换下来的「现在这一份」留在哪儿。**只有目标原来存在时才非空**；成功后它仍在盘上。
    QString retainedPath;
    /// 这一趟写（或改写）的事务记录。成功后仍在 `cache/restore-transactions`。
    QString journalPath;
    /// 不影响成败、但用户该看见的几句（记录没能更新成「已发布」之类）。
    QStringList notes;
};

/**
 * @brief 用 @p request.replacementDir 换掉 @p request.targetDir，两边都保留。
 *
 * 判据失败（路径越界 / 含链接 / 不同父目录 / 被占用 / 记录对不上）时**一个字节都不动**。
 *
 * 动盘这一段只有两次 `QDir::rename()`：先「目标 → 新的 retained」，再「旧目录 → 目标」。
 * 第二段失败会把第一段倒回去；倒不回去时两份都在原处（位置写进 `problem`），
 * 记录留着给启动恢复与人工检查。
 */
Result swapDirectories(const Request &request);

}  // namespace ManualRecovery
