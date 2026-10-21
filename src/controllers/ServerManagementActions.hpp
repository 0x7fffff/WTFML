/**
 * @file ServerManagementActions.hpp
 * @brief 服务端管理界面操作；状态仍由所属窗口持有，不引入额外异步生命周期。
 */
#pragma once
#include <QString>

class MainWindow;

class ServerManagementActions final {
public:
    static void refreshServerPlayers(MainWindow *owner);
    static void addServerPlayer(MainWindow *owner);
    static void removeServerPlayer(MainWindow *owner, const QString &identity);
    static void pickServerResourcePack(MainWindow *owner);
    static QString serverContentFolder(const MainWindow *owner);
    static void refreshServerContent(MainWindow *owner);
    static void addServerContent(MainWindow *owner);
    static void toggleServerContent(MainWindow *owner, const QString &name, bool enable);
    static void openServerContentFolder(MainWindow *owner);
    static void kickServerPlayer(MainWindow *owner, const QString &name);
    static void runServerCheckAction(MainWindow *owner, const QString &token);
    static QString serverWorldName(const MainWindow *owner, const QString &serverId);
    static QString serverWorldDir(const MainWindow *owner, const QString &serverId);
    static int serverWorldBackupCount(const MainWindow *owner, const QString &serverId);
    static void backupServerWorld(MainWindow *owner, const QString &serverId);
    static void restoreServerWorldBackup(MainWindow *owner, const QString &serverId, const QString &zipPath);
    static void deleteServerBackup(MainWindow *owner, const QString &zipPath);
};
