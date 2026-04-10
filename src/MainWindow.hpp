#pragma once

#include <QJsonObject>
#include <QMainWindow>
#include <QProcess>
#include <QString>
#include <QStringList>

namespace Ui {
class MainWindow;
}

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

    Ui::MainWindow *ui;
    LauncherManager *m_launcherManager;
    AuthManager *m_authManager;
    FolderManager *m_folderManager;
    VersionManager *m_versionManager;
    GlobalJavaSettings *m_javaSettings;
    DownloadManager *m_downloadManager;
};
