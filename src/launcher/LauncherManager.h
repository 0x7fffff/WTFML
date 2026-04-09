#pragma once

#include <QProcess>
#include <QStringList>
#include <QObject>

class GlobalJavaSettings;

class LauncherManager : public QObject
{
    Q_OBJECT

public:
    explicit LauncherManager(QObject *parent = nullptr);
    bool checkPreLaunch(const QString &versionId, QStringList *errors = nullptr) const;
    QStringList generateJvmArguments(const QString &token, const QString &versionId, const QString &assetIndexId, const GlobalJavaSettings &settings, bool offlineMode = false) const;
    void startGame(const QStringList &arguments, GlobalJavaSettings &settings);

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
