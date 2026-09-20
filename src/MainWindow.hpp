/**
 * @file MainWindow.hpp
 * @brief 声明主启动器窗口及其UI交互。
 * @author 0x7fffff
 * @date 2026-04-11
 * @note 按钮事件与版本选择在此文件中统一管理。
 */

#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QMainWindow>
#include <QProcess>

class MainWindowUI;
class LauncherManager;
class AuthManager;
class FolderManager;
class VersionManager;
class GlobalJavaSettings;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onStartButtonClicked();
    void onLoginButtonClicked();
    void onAddFolderClicked();
    void onRemoveFolderClicked();
    void onProcessOutput(const QString &text);
    void onProcessError(const QString &text);
    void onGameFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onDownloadButtonClicked();
    void onVersionsFetched(const QStringList &versions);
    void onVersionDetailsFetched(const QString &versionId, const QJsonObject &versionJson);
    void onVersionDownloadStats(int done, int total, int failed, const QString &currentFile);
    void onVersionDownloadFinished(const QString &versionId, bool success,
                                   const QString &errorMessage);

private:
    void setupConnections();
    void appendLog(const QString &text);
    void refreshVersionList();
    // 下载期间禁用"启动游戏""下载版本"，并显示状态栏进度条
    void setDownloadUiBusy(bool busy);
    void updateDownloadProgressUi(int done, int total, int failed, const QString &currentFile);

    MainWindowUI *m_ui;
    LauncherManager *m_launcherManager;
    AuthManager *m_authManager;
    FolderManager *m_folderManager;
    VersionManager *m_versionManager;
    GlobalJavaSettings *m_javaSettings;
    // 下载是否在进行中。注意不能只问 m_versionManager：从点击"下载"到版本 JSON
    // 拉回来之前，VersionManager 那边还没有任何批次，这段时间也得算"忙"。
    bool m_downloadInFlight = false;
    // 进度按网络分片到达（5000 多个 asset），节流一下再刷界面
    QElapsedTimer m_progressThrottle;
};