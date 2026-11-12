#pragma once
#include <QObject>
#include <QString>
class InstanceSettingsDialog;
class VersionManager;
class ResourceStore;

// Read-only assessment. Owned by the instance page; never installs or changes files.
namespace UpgradeAssessment {
void attach(InstanceSettingsDialog *dialog, VersionManager *versions, ResourceStore *store,
            const QString &sourceId, const QString &root, const QString &identityCache);
/// 测试用：关掉扫描里按**墙钟**计的预算（嵌套 JAR 10 s、旧 Forge 注解 8 s）。这两步每次都要起一个
/// PowerShell 进程，机器一慢就会超预算、把结果如实标成"无法确认" —— 产品里这是对的，测试里
/// 这就成了"看机器快不快"。按条数计的预算照旧生效。默认开着。
void setWallClockBudgetsForTesting(bool enabled);
}
