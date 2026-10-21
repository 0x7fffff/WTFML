/**
 * @file InstanceMaintenanceController.hpp
 * @brief 管理实例配置与世界 level.dat 的轻量备份，不依赖主窗口状态。
 */
#pragma once

#include <QString>
#include <QStringList>

class InstanceMaintenanceController final {
public:
    /// 指定配置文件的历史备份，最新在前；忽略缺失文件和越出备份根的链接。
    static QStringList configBackups(const QString &instanceId, const QString &configDir,
                                     const QString &path);
    static QString latestConfigBackup(const QString &instanceId, const QString &configDir,
                                      const QString &path);
    static void pruneConfigBackups(const QString &instanceId, int keep);

    static bool backupWorldLevelData(const QString &worldDir, int keep, QString *problemOut);
    static QString latestWorldBackup(const QString &worldDir);

    /// 实例改名之后，把存档 level.dat 的备份格子搬到新名字底下。
    ///
    /// 格子名由 `worldBackupSlot()` 算出来，里面有实例目录名**和**世界目录绝对路径的
    /// 哈希 —— 改名把两样都改了，不搬的话 `latestWorldBackup()` 从此查不到这一份、
    /// 清理也够不着它（用户眼里就是"改个名，自动备份没了"）。
    /// 目标格子已经有东西时**不合并**（理由和 `InstanceFiles::migrateInstanceCacheDirs()`
    /// 那条逐字一样：两边的"第 N 代"没法排先后）。
    ///
    /// @param movedOut 真搬成功的格子数（可空）。
    /// @return 没搬成的那几处，一句一处（空 = 全搬好了或本来就没有）。
    static QStringList migrateWorldBackupSlots(const QString &oldInstanceDir,
                                               const QString &newInstanceDir,
                                               int *movedOut = nullptr);
};
