/**
 * @file InstanceMaintenanceController.hpp
 * @brief 管理实例配置与世界 level.dat 的轻量备份，不依赖主窗口状态。
 * @author Agent
 * @date 2026-10
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
};
