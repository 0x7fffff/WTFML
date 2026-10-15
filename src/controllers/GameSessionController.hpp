#pragma once
#include <QObject>
#include <QProcess>
class MainWindow;
class GameLogWindow;
class GameSessionController final : public QObject {
 Q_OBJECT
public:
 explicit GameSessionController(MainWindow *owner);
    void onProcessOutput(int sessionId, const QString &text);
    void onProcessError(int sessionId, const QString &text);
    void showGameLogLine(int sessionId, const QString &text);
    GameLogWindow *ensureGameLogWindow(int sessionId, bool userAsked);
    void onGameFinished(int sessionId, int exitCode, QProcess::ExitStatus exitStatus);
    QString launchingInstanceName() const;
    QString launchingStatusName() const;
    bool confirmStopGame(const QString &displayName, int sessions);
    void stopGameSession(int sessionId);
private:
 MainWindow *m_owner = nullptr;
};

