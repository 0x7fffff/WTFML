/**
 * @file MainWindowSessionEvents.cpp
 * @brief 游戏会话的几发事件：输出、结束、日志窗那几格、以及"停在哪一颗上"的那两句话。
 *
 * 【为什么单独一个翻译单元】
 *
 * 这一片是主窗口与 `GameSessionController` 之间那条**单线**：进程的 stdout / stderr、
 * 会话被丢掉、要日志窗、退出码回来、以及停止前问的那一句。每一发都是薄壳 —— 真正的状态
 * 在控制器那边；放在一个文件里，"主窗口到底从会话那里接了几发、各自转给谁"一眼看得完，
 * 与新加事件时该往哪儿接也清楚。
 *
 * 【判据不在这一层】
 *
 * 会话的事实、日志窗的所有权、停止的确认文案都在 `GameSessionController`
 * （`launchingInstanceName()` / `launchingStatusName()` 也只是把控制器那份名字转出来）。
 */

#include "MainWindow.hpp"

#include "..\ui\GameLogWindow.hpp"
#include "controllers/GameSessionController.hpp"

#include <QProcess>

void MainWindow::onProcessOutput(int sessionId, const QString &text) {
    m_gameSessionController->onProcessOutput(sessionId, text);
}

void MainWindow::onProcessError(int sessionId, const QString &text) {
    m_gameSessionController->onProcessError(sessionId, text);
}

void MainWindow::onSessionDiscarded(int sessionId) {
    m_gameSessionController->onSessionDiscarded(sessionId);
}

void MainWindow::showGameLogLine(int sessionId, const QString &text) {
    m_gameSessionController->showGameLogLine(sessionId, text);
}

GameLogWindow *MainWindow::ensureGameLogWindow(int sessionId, bool userAsked) {
    return m_gameSessionController->ensureGameLogWindow(sessionId, userAsked);
}

void MainWindow::onGameFinished(int sessionId, int exitCode, QProcess::ExitStatus exitStatus) {
    m_gameSessionController->onGameFinished(sessionId, exitCode, exitStatus);
}

QString MainWindow::launchingInstanceName() const {
    return m_gameSessionController->launchingInstanceName();
}

QString MainWindow::launchingStatusName() const {
    return m_gameSessionController->launchingStatusName();
}

bool MainWindow::confirmStopGame(const QString &displayName, int sessions) {
    return m_gameSessionController->confirmStopGame(displayName, sessions);
}

void MainWindow::stopGameSession(int sessionId) {
    m_gameSessionController->stopGameSession(sessionId);
}
