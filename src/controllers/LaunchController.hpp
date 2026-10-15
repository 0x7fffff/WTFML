#pragma once
#include <QObject>
#include "MainWindow.hpp"
class LaunchController final : public QObject {
 Q_OBJECT
public:
 explicit LaunchController(MainWindow *owner);
    QString launchIdentityKey() const;
    bool confirmLaunchWhileRunning(const QString &versionId);
    QStringList launchArguments(const QString &versionId, const QJsonObject &versionJson, const EffectiveJavaSettings &javaSettings, const QString &token, bool offlineMode, const QuickPlay::Target &quickPlay) const;
    void onStartButtonClicked();
    void appendLaunchRecord(const QString &versionId, const QuickPlay::Target &quickPlay, qint64 startedAtMs);
private:
 MainWindow *m_owner = nullptr;
};

