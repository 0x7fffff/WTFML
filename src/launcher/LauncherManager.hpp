#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>

class GlobalJavaSettings;
class QJsonObject;

class LauncherManager : public QObject {
    Q_OBJECT

public:
    explicit LauncherManager(QObject *parent = nullptr);
    bool checkPreLaunch(const QString &versionId, QStringList *errors = nullptr) const;
    QStringList generateJvmArguments(const QString &token, const QString &versionId,
                                     const QString &assetIndexId, const QJsonObject &versionJson,
                                     const GlobalJavaSettings &settings,
                                     bool offlineMode = false) const;
    void startGame(const QStringList &arguments, GlobalJavaSettings &settings,
                   const QString &versionId);

signals:
    void processOutput(const QString &text);
    void processError(const QString &text);
    void gameFinished(int exitCode, QProcess::ExitStatus exitStatus);

private slots:
    void handleReadyReadStandardOutput();
    void handleReadyReadStandardError();
    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    QProcess *m_gameProcess;
};
