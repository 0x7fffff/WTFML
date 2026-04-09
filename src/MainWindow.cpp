#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "launcher/LauncherManager.h"
#include "auth/AuthManager.h"
#include "manage/FolderManager.h"
#include "manage/VersionManager.h"
#include "settings/GlobalJavaSettings.h"
#include "download/DownloadManager.h"

#include <QMessageBox>
#include <QInputDialog>
#include <QProcess>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFile>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_launcherManager(new LauncherManager(this))
    , m_authManager(new AuthManager(this))
    , m_folderManager(new FolderManager(this))
    , m_versionManager(new VersionManager(this))
    , m_javaSettings(new GlobalJavaSettings(this))
    , m_downloadManager(new DownloadManager(this))
{
    ui->setupUi(this);
    setupConnections();
    refreshVersionList();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setupConnections()
{
    connect(ui->startButton, &QPushButton::clicked, this, &MainWindow::onStartButtonClicked);
    connect(ui->loginButton, &QPushButton::clicked, this, &MainWindow::onLoginButtonClicked);
    connect(ui->addFolderButton, &QPushButton::clicked, this, &MainWindow::onAddFolderClicked);
    connect(ui->removeFolderButton, &QPushButton::clicked, this, &MainWindow::onRemoveFolderClicked);

    connect(m_launcherManager, &LauncherManager::processOutput, this, &MainWindow::onProcessOutput);
    connect(m_launcherManager, &LauncherManager::processError, this, &MainWindow::onProcessError);
    connect(m_launcherManager, &LauncherManager::gameFinished, this, &MainWindow::onGameFinished);

    connect(m_downloadManager, &DownloadManager::downloadProgress, this, &MainWindow::onDownloadProgress);
    connect(m_downloadManager, &DownloadManager::downloadFinished, this, &MainWindow::onDownloadFinished);
    connect(m_downloadManager, &DownloadManager::allDownloadsFinished, this, &MainWindow::onAllDownloadsFinished);
    connect(m_downloadManager, &DownloadManager::versionJsonFetched, this, &MainWindow::onVersionJsonFetched);
    connect(m_versionManager, &VersionManager::versionsFetched, this, &MainWindow::onVersionsFetched);
    connect(m_versionManager, &VersionManager::versionDetailsFetched, this, &MainWindow::onVersionDetailsFetched);

    // Connect download button if it exists
    if (ui->downloadButton) {
        connect(ui->downloadButton, &QPushButton::clicked, this, &MainWindow::onDownloadButtonClicked);
    }
}

void MainWindow::refreshVersionList()
{
    ui->versionList->clear();
    const auto versions = m_versionManager->availableVersions();
    if (!versions.isEmpty()) {
        ui->versionList->addItems(versions);
    } else {
        const auto installed = m_versionManager->installedVersions();
        if (!installed.isEmpty()) {
            ui->versionList->addItems(installed);
        } else {
            ui->versionList->addItem(tr("自定义版本"));
        }
    }

    m_versionManager->fetchAvailableVersions();
}

void MainWindow::onVersionsFetched(const QStringList &versions)
{
    ui->versionList->clear();
    if (!versions.isEmpty()) {
        ui->versionList->addItems(versions);
    } else {
        const auto installed = m_versionManager->installedVersions();
        if (!installed.isEmpty()) {
            ui->versionList->addItems(installed);
        } else {
            ui->versionList->addItem(tr("自定义版本"));
        }
    }
    appendLog(tr("已更新在线版本列表 (%1 个版本)。").arg(ui->versionList->count()));
}

void MainWindow::appendLog(const QString &text)
{
    ui->logTextEdit->append(text);
}

void MainWindow::onStartButtonClicked()
{
    bool offlineMode = ui->offlineModeCheckBox->isChecked();
    
    QString token;
    if (!offlineMode) {
        token = m_authManager->loadSavedToken();
        if (token.isEmpty()) {
            QMessageBox::warning(this, tr("登录缺失"), tr("请先登录 Microsoft 账户，或使用离线模式。"));
            return;
        }
    }

    QListWidgetItem *item = ui->versionList->currentItem();
    if (!item) {
        QMessageBox::warning(this, tr("未选择版本"), tr("请先在版本列表中选择一个版本。"));
        return;
    }

    QString versionId = item->text();
    if (versionId == tr("自定义版本")) {
        QMessageBox::warning(this, tr("不支持的版本"), tr("自定义版本暂不支持启动。"));
        return;
    }

    QStringList errors;
    if (!m_launcherManager->checkPreLaunch(versionId, &errors)) {
        appendLog(tr("启动前检查失败："));
        for (const QString &error : errors) {
            appendLog(error);
        }
        return;
    }

    // Get asset index ID from version JSON
    QString assetIndexId = "29"; // Default for 1.21.11
    QJsonObject versionJson = m_versionManager->getVersionJson(versionId);
    if (!versionJson.isEmpty()) {
        QJsonObject assetIndexObj = versionJson["assetIndex"].toObject();
        QString actualAssetIndexId = assetIndexObj["id"].toString();
        if (!actualAssetIndexId.isEmpty()) {
            assetIndexId = actualAssetIndexId;
        }
    }

    const QStringList jvmArgs = m_launcherManager->generateJvmArguments(token, versionId, assetIndexId, *m_javaSettings, offlineMode);
    m_launcherManager->startGame(jvmArgs, *m_javaSettings);
    appendLog(tr("已开始启动游戏，正在执行 JVM 参数..."));
}

void MainWindow::onLoginButtonClicked()
{
    if (m_authManager->loginMicrosoft()) {
        appendLog(tr("打开了登录网页，请完成 Microsoft 登录。"));
    } else {
        appendLog(tr("登录流程启动失败。"));
    }
}

void MainWindow::onAddFolderClicked()
{
    const QString folder = QInputDialog::getText(this, tr("添加游戏文件夹"), tr("请输入 Minecraft 根目录路径："));
    if (folder.isEmpty()) {
        return;
    }
    if (m_folderManager->addFolder(folder)) {
        appendLog(tr("已添加文件夹：%1").arg(folder));
    } else {
        appendLog(tr("添加文件夹失败：%1").arg(folder));
    }
}

void MainWindow::onRemoveFolderClicked()
{
    const QString folder = QInputDialog::getText(this, tr("删除游戏文件夹"), tr("请输入要删除的文件夹路径："));
    if (folder.isEmpty()) {
        return;
    }
    if (m_folderManager->removeFolder(folder)) {
        appendLog(tr("已删除文件夹：%1").arg(folder));
    } else {
        appendLog(tr("删除文件夹失败：%1").arg(folder));
    }
}

void MainWindow::onProcessOutput(const QString &text)
{
    appendLog(tr("[游戏输出] %1").arg(text));
}

void MainWindow::onProcessError(const QString &text)
{
    appendLog(tr("[游戏错误] %1").arg(text));
}

void MainWindow::onGameFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitStatus == QProcess::CrashExit) {
        appendLog(tr("游戏崩溃，退出码：%1").arg(exitCode));
    } else {
        appendLog(tr("游戏进程已结束，退出码：%1").arg(exitCode));
    }
}

void MainWindow::onDownloadButtonClicked()
{
    QListWidgetItem *item = ui->versionList->currentItem();
    if (!item) {
        QMessageBox::warning(this, tr("未选择版本"), tr("请先在版本列表中选择一个版本。"));
        return;
    }

    QString versionId = item->text();
    
    // Check if already downloaded
    if (item->text() != tr("自定义版本")) {
        QString versionsPath = QCoreApplication::applicationDirPath() + "/.minecraft/versions";
        QString versionJsonPath = versionsPath + "/" + versionId + "/" + versionId + ".json";
        if (QFile::exists(versionJsonPath)) {
            QMessageBox::information(this, tr("版本已存在"), tr("版本 %1 已存在，无需重复下载。").arg(versionId));
            return;
        }
    }

    appendLog(tr("正在获取版本 %1 的信息...").arg(versionId));
    
    // Fetch version details using VersionManager
    m_versionManager->fetchVersionDetails(versionId);
}

void MainWindow::onDownloadProgress(const QString &fileName, qint64 bytesReceived, qint64 bytesTotal)
{
    QString progress;
    if (bytesTotal > 0) {
        int percentage = (bytesReceived * 100) / bytesTotal;
        progress = tr("[%1%] 正在下载: %2 (%3 / %4 MB)")
                   .arg(percentage)
                   .arg(fileName)
                   .arg(bytesReceived / (1024 * 1024))
                   .arg(bytesTotal / (1024 * 1024));
    } else {
        progress = tr("正在下载: %1 (%2 MB)")
                   .arg(fileName)
                   .arg(bytesReceived / (1024 * 1024));
    }
    appendLog(progress);
}

void MainWindow::onDownloadFinished(const QString &fileName, bool success, const QString &errorMessage)
{
    if (success) {
        appendLog(tr("✓ 下载完成: %1").arg(fileName));
    } else {
        appendLog(tr("✗ 下载失败: %1 - %2").arg(fileName, errorMessage));
    }
}

void MainWindow::onAllDownloadsFinished(bool success)
{
    if (success) {
        appendLog(tr("===== 所有文件下载完成 ====="));
        refreshVersionList();
        QMessageBox::information(this, tr("下载完成"), tr("版本文件已全部下载完成！"));
    } else {
        appendLog(tr("===== 部分文件下载失败 ====="));
        QMessageBox::warning(this, tr("下载失败"), tr("部分文件下载失败，请检查日志。"));
    }
}

void MainWindow::onVersionJsonFetched(const QJsonObject &versionJson)
{
    // This is called by DownloadManager, but we use VersionManager now
    Q_UNUSED(versionJson)
}

void MainWindow::onVersionDetailsFetched(const QString &versionId, const QJsonObject &versionJson)
{
    if (versionJson.isEmpty()) {
        appendLog(tr("获取版本 JSON 失败"));
        QMessageBox::warning(this, tr("下载失败"), tr("无法获取版本信息。"));
        return;
    }
    
    appendLog(tr("版本信息获取成功，开始下载游戏文件..."));
    
    // Start downloading the version
    QString versionsPath = QCoreApplication::applicationDirPath() + "/.minecraft/versions/" + versionId;
    m_versionManager->downloadVersion(versionId, versionsPath);
}
