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
}
