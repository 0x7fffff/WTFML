/**
 * @file MainWindow.hpp
 * @brief 声明主启动器窗口及其UI交互。
 * @author 0x7fffff
 * @date 2026-04-11
 * @note 按钮事件与版本选择在此文件中统一管理。
 */

#pragma once

#include <QJsonObject>
#include <QMainWindow>
#include <QProcess>

class MainWindowUI;
class LauncherManager;
class AuthManager;
class FolderManager;
class VersionManager;
class GlobalJavaSettings;
class DownloadManager;

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
    void onDownloadProgress(const QString &fileName, qint64 bytesReceived, qint64 bytesTotal);
    void onDownloadFinished(const QString &fileName, bool success, const QString &errorMessage);
    void onAllDownloadsFinished(bool success);
    void onVersionJsonFetched(const QJsonObject &versionJson);
    void onVersionsFetched(const QStringList &versions);
    void onVersionDetailsFetched(const QString &versionId, const QJsonObject &versionJson);
    void onVersionDownloadFinished(const QString &versionId, bool success,
                                   const QString &errorMessage);

private:
    void setupConnections();
    void appendLog(const QString &text);
    void refreshVersionList();

    MainWindowUI *m_ui;
    LauncherManager *m_launcherManager;
    AuthManager *m_authManager;
    FolderManager *m_folderManager;
    VersionManager *m_versionManager;
    GlobalJavaSettings *m_javaSettings;
    DownloadManager *m_downloadManager;
};