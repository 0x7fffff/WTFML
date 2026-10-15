#pragma once
#include <QObject>
#include "manage/InstanceIconLookup.hpp"
class MainWindow;
class InstanceSettingsDialog;
class InstanceIconController final : public QObject {
 Q_OBJECT
public:
 explicit InstanceIconController(MainWindow *owner);
    static QString instanceIconCacheKey(const QString &path);
    QStringList instanceIconCandidatePaths(const QString &versionId) const;
    void requestInstanceFileIcons(InstanceSettingsDialog *dialog, const QString &versionId);
    void startInstanceFileIconLookup();
    void onInstanceIconLookupFinished(const InstanceIconLookup::Result &result);
    void pushInstanceFileIcons();
    void pushInstanceFileUpdates(InstanceSettingsDialog *dialog, const QString &instanceId);
private:
 MainWindow *m_owner = nullptr;
};

