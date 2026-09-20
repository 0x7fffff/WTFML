/**
 * @file MainWindow.cpp
 * @brief 实现主应用程序窗口及用户交互逻辑。
 * @author 0x7fffff
 * @date 2026-04-11
 * @note 本文件实现了 UI 事件处理函数并委托给各个管理器的方法。
 *
 * 本文件将 UI 控件连接到启动器，处理用户命令，
 * 并协调身份验证、下载和启动操作。
 */

#include "MainWindow.hpp"

#include "..\ui\MainWindowUI.hpp"

// 添加以下头文件（防止hpp变动导致文件失效）
#include <QCheckBox>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QProgressBar>
#include <QProcess>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

#include "auth/AuthManager.hpp"
#include "launcher/LauncherManager.hpp"
#include "manage/FolderManager.hpp"
#include "manage/VersionManager.hpp"
#include "settings/GlobalJavaSettings.hpp"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_ui(new MainWindowUI(this)),
      m_launcherManager(new LauncherManager(this)),
      m_authManager(new AuthManager(this)),
      m_folderManager(new FolderManager(this)),
      m_versionManager(new VersionManager(this)),
      m_javaSettings(new GlobalJavaSettings(this)) {
    setupConnections();
    refreshVersionList();
}

MainWindow::~MainWindow() { delete m_ui; }

void MainWindow::setupConnections() {
    // UI 按钮信号
    connect(m_ui->getStartButton(), &QPushButton::clicked, this, &MainWindow::onStartButtonClicked);
    connect(m_ui->getLoginButton(), &QPushButton::clicked, this, &MainWindow::onLoginButtonClicked);
    connect(m_ui->getAddFolderButton(), &QPushButton::clicked, this,
            &MainWindow::onAddFolderClicked);
    connect(m_ui->getRemoveFolderButton(), &QPushButton::clicked, this,
            &MainWindow::onRemoveFolderClicked);
    connect(m_ui->getDownloadButton(), &QPushButton::clicked, this,
            &MainWindow::onDownloadButtonClicked);

    // 游戏进程信号
    connect(m_launcherManager, &LauncherManager::processOutput, this, &MainWindow::onProcessOutput);
    connect(m_launcherManager, &LauncherManager::processError, this, &MainWindow::onProcessError);
    connect(m_launcherManager, &LauncherManager::gameFinished, this, &MainWindow::onGameFinished);

    // 版本管理器信号
    // NOTE: 这里以前接的是 MainWindow 自己 new 出来的一个 DownloadManager，
    // 但从来没有任何地方在它上面发起过下载 —— 真正的下载全走 VersionManager
    // 内部的 DownloadManager。于是 "下载完成" 那个弹窗从来没有被触发过，
    // 用户看着刷屏的日志以为还没下完就去点启动。现在只接真正在下载的那条链路。
    connect(m_versionManager, &VersionManager::versionsFetched, this,
            &MainWindow::onVersionsFetched);
    connect(m_versionManager, &VersionManager::versionDetailsFetched, this,
            &MainWindow::onVersionDetailsFetched);
    connect(m_versionManager, &VersionManager::versionDownloadStats, this,
            &MainWindow::onVersionDownloadStats);
    connect(m_versionManager, &VersionManager::versionDownloadFinished, this,
            &MainWindow::onVersionDownloadFinished);
}

void MainWindow::setDownloadUiBusy(bool busy) {
    m_downloadInFlight = busy;
    m_ui->getStartButton()->setEnabled(!busy);
    m_ui->getDownloadButton()->setEnabled(!busy);

    QProgressBar *progressBar = m_ui->getProgressBar();
    QLabel *statusLabel = m_ui->getStatusLabel();
    progressBar->setVisible(busy);
    statusLabel->setVisible(busy);
    if (busy) {
        // 还不知道总任务数：先转成不确定态的滚动条，拿到 stats 再换成确定态
        progressBar->setRange(0, 0);
        statusLabel->setText(tr("正在准备下载..."));
    } else {
        progressBar->setRange(0, 100);
        progressBar->setValue(0);
    }
}

void MainWindow::updateDownloadProgressUi(int done, int total, int failed,
                                          const QString &currentFile) {
    QProgressBar *progressBar = m_ui->getProgressBar();
    QLabel *statusLabel = m_ui->getStatusLabel();

    if (total <= 0) {
        progressBar->setRange(0, 0);
        statusLabel->setText(tr("正在准备下载..."));
        return;
    }

    progressBar->setRange(0, total);
    progressBar->setValue(done);

    QString text = tr("已下载 %1 / %2").arg(done).arg(total);
    if (failed > 0) {
        text += tr("，失败 %1").arg(failed);
    }
    if (!currentFile.isEmpty()) {
        // 路径可能很长（asset 目录层级深），只显示文件名
        text += QStringLiteral("  ") + QFileInfo(currentFile).fileName();
    }
    statusLabel->setText(text);
}

void MainWindow::onVersionDownloadStats(int done, int total, int failed,
                                        const QString &currentFile) {
    // 前缘节流：200 ms 内的后续更新直接丢掉，只保留最后一次的状态由结束时补刷。
    // 不这么做的话，5000 多个 asset 会疯狂重排状态栏，界面反而卡住。
    if (m_progressThrottle.isValid() && m_progressThrottle.elapsed() < 200) {
        return;
    }
    m_progressThrottle.start();
    updateDownloadProgressUi(done, total, failed, currentFile);
}

void MainWindow::appendLog(const QString &text) { m_ui->getLogTextEdit()->append(text); }

void MainWindow::refreshVersionList() {
    QListWidget *versionList = m_ui->getVersionList();
    versionList->clear();
    const auto versions = m_versionManager->availableVersions();
    if (!versions.isEmpty()) {
        versionList->addItems(versions);
    } else {
        const auto installed = m_versionManager->installedVersions();
        if (!installed.isEmpty()) {
            versionList->addItems(installed);
        } else {
            versionList->addItem(tr("自定义版本"));
        }
    }
    m_versionManager->fetchAvailableVersions();
}

void MainWindow::onVersionsFetched(const QStringList &versions) {
    QListWidget *versionList = m_ui->getVersionList();
    versionList->clear();
    if (!versions.isEmpty()) {
        versionList->addItems(versions);
    } else {
        const auto installed = m_versionManager->installedVersions();
        if (!installed.isEmpty()) {
            versionList->addItems(installed);
        } else {
            versionList->addItem(tr("自定义版本"));
        }
    }
    appendLog(tr("已更新在线版本列表 (%1 个版本)。").arg(versionList->count()));
}

void MainWindow::onStartButtonClicked() {
    // 防御：下载期间按钮本来就是灰的，但这里再拦一道。改动前没有任何阻拦，
    // 用户看着日志以为下完了直接点启动，游戏会因为文件不全崩掉。
    if (m_downloadInFlight || m_versionManager->isDownloading()) {
        QMessageBox::warning(this, tr("下载进行中"),
                             tr("该版本的文件还没有下载完成，请等下载结束后再启动。"));
        return;
    }

    bool offlineMode = m_ui->getOfflineModeCheckBox()->isChecked();

    QString token;
    if (!offlineMode) {
        token = m_authManager->loadSavedToken();
        if (token.isEmpty()) {
            QMessageBox::warning(this, tr("登录缺失"),
                                 tr("请先登录 Microsoft 账户，或使用离线模式。"));
            return;
        }
    }

    QListWidgetItem *item = m_ui->getVersionList()->currentItem();
    if (!item) {
        QMessageBox::warning(this, tr("未选择版本"), tr("请先在版本列表中选择一个版本。"));
        return;
    }

    QString versionId = item->text();
    if (versionId == tr("自定义版本")) {
        QMessageBox::warning(this, tr("不支持的版本"), tr("自定义版本暂不支持启动。"));
        return;
    }

    // version.json 要提前取：natives 的准备和启动前检查都依赖它
    QJsonObject versionJson = m_versionManager->getVersionJson(versionId);

    // 拿不到 version.json 就不能猜。以前这里会静默用 javaMajorVersion = 21 和
    // assetIndex = "29" 继续启动，26.1（需要 Java 25）就会崩在
    // UnsupportedClassVersionError，且看不出原因。
    if (versionJson.isEmpty()) {
        appendLog(tr("找不到版本 %1 的 version.json").arg(versionId));
        QMessageBox::warning(
            this, tr("版本信息缺失"),
            tr("找不到版本 %1 的 version.json，无法确定所需的 Java 版本。\n"
               "请先选中该版本并点击\"下载\"重新获取版本信息。")
                .arg(versionId));
        return;
    }

    // 旧格式（1.18.2 及以下）的 natives 必须先落地，否则会被 checkPreLaunch 拦下。
    // 该方法自带守卫，已经解压过的版本直接返回，不会重复解压。
    QStringList nativeErrors;
    const QString nativesDir =
        m_launcherManager->prepareNatives(versionJson, versionId, &nativeErrors);
    if (nativesDir.isEmpty() && !nativeErrors.isEmpty()) {
        appendLog(tr("准备 natives 失败："));
        for (const QString &error : nativeErrors) {
            appendLog(error);
        }
        return;
    }
    if (!nativesDir.isEmpty()) {
        appendLog(tr("natives 已就绪：%1").arg(nativesDir));
    }

    QStringList errors;
    if (!m_launcherManager->checkPreLaunch(versionId, &errors, versionJson)) {
        appendLog(tr("启动前检查失败："));
        for (const QString &error : errors) {
            appendLog(error);
        }
        return;
    }

    // 兜底值只在 version.json 没写这两个字段时才用得上
    QString assetIndexId = "29";
    int javaMajorVersion = 21;
    QJsonObject assetIndexObj = versionJson["assetIndex"].toObject();
    const QString actualAssetIndexId = assetIndexObj["id"].toString();
    if (!actualAssetIndexId.isEmpty()) {
        assetIndexId = actualAssetIndexId;
    }
    // version.json 里的 javaVersion.component 指明了 Mojang 为该版本准备的运行时，
    // 例如 26.1 是 java-runtime-epsilon（Java 25）。
    const QJsonObject javaVersionObj = versionJson["javaVersion"].toObject();
    const int majorVersion = javaVersionObj["majorVersion"].toInt();
    if (majorVersion > 0) {
        javaMajorVersion = majorVersion;
    }
    const QString javaComponent = javaVersionObj["component"].toString();

    if (!m_javaSettings->ensureJavaForVersion(javaMajorVersion, javaComponent)) {
        appendLog(tr("无法获取所需的 Java 运行时环境：版本 %1").arg(javaMajorVersion));
        QMessageBox::warning(this, tr("Java 运行时缺失"),
                             tr("找不到或无法下载 Java %1，请手动指定 Java 路径。")
                                 .arg(javaMajorVersion));
        return;
    }

    const QStringList jvmArgs = m_launcherManager->generateJvmArguments(
        token, versionId, assetIndexId, versionJson, *m_javaSettings, offlineMode,
        javaMajorVersion);
    m_launcherManager->startGame(jvmArgs, *m_javaSettings, versionId);
    appendLog(tr("已开始启动游戏，正在执行 JVM 参数..."));
}

void MainWindow::onLoginButtonClicked() {
    if (m_authManager->loginMicrosoft()) {
        appendLog(tr("打开了登录网页，请完成 Microsoft 登录。"));
    } else {
        appendLog(tr("登录流程启动失败。"));
    }
}

void MainWindow::onAddFolderClicked() {
    const QString folder =
        QInputDialog::getText(this, tr("添加游戏文件夹"), tr("请输入 Minecraft 根目录路径："));
    if (folder.isEmpty()) return;
    if (m_folderManager->addFolder(folder)) {
        appendLog(tr("已添加文件夹：%1").arg(folder));
    } else {
        appendLog(tr("添加文件夹失败：%1").arg(folder));
    }
}

void MainWindow::onRemoveFolderClicked() {
    const QString folder =
        QInputDialog::getText(this, tr("删除游戏文件夹"), tr("请输入要删除的文件夹路径："));
    if (folder.isEmpty()) return;
    if (m_folderManager->removeFolder(folder)) {
        appendLog(tr("已删除文件夹：%1").arg(folder));
    } else {
        appendLog(tr("删除文件夹失败：%1").arg(folder));
    }
}

void MainWindow::onProcessOutput(const QString &text) { appendLog(tr("[游戏输出] %1").arg(text)); }

void MainWindow::onProcessError(const QString &text) { appendLog(tr("[游戏错误] %1").arg(text)); }

void MainWindow::onGameFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (exitStatus == QProcess::CrashExit) {
        appendLog(tr("游戏崩溃，退出码：%1").arg(exitCode));
    } else {
        appendLog(tr("游戏进程已结束，退出码：%1").arg(exitCode));
    }
}

void MainWindow::onDownloadButtonClicked() {
    if (m_downloadInFlight) {
        appendLog(tr("已有下载任务正在进行，请等它结束。"));
        return;
    }

    QListWidgetItem *item = m_ui->getVersionList()->currentItem();
    if (!item) {
        QMessageBox::warning(this, tr("未选择版本"), tr("请先在版本列表中选择一个版本。"));
        return;
    }

    QString versionId = item->text();
    if (versionId == tr("自定义版本")) {
        QMessageBox::warning(this, tr("不支持的版本"), tr("自定义版本暂不支持下载。"));
        return;
    }

    const QString versionJsonPath = QCoreApplication::applicationDirPath() +
                                    "/.minecraft/versions/" + versionId + "/" + versionId +
                                    ".json";
    if (QFile::exists(versionJsonPath)) {
        // 已经下过：不给"直接重下"（几 GB 白跑一趟），改成校验并补齐缺失/损坏的文件。
        const auto answer = QMessageBox::question(
            this, tr("版本已存在"),
            tr("版本 %1 已经下载过了。\n是否校验并修复缺失或损坏的文件？").arg(versionId),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer != QMessageBox::Yes) {
            appendLog(tr("已取消对版本 %1 的校验。").arg(versionId));
            return;
        }
        appendLog(tr("开始校验版本 %1 的文件（已存在的会跳过）...").arg(versionId));
    } else {
        appendLog(tr("正在获取版本 %1 的信息...").arg(versionId));
    }

    // busy 必须在点击这一刻就置上：版本 JSON 是异步拉回来的，
    // 中间这段时间如果按钮还能点，用户会重复触发。
    setDownloadUiBusy(true);
    m_progressThrottle.invalidate();
    m_versionManager->fetchVersionDetails(versionId);
}

void MainWindow::onVersionDownloadFinished(const QString &versionId, bool success,
                                           const QString &errorMessage) {
    // 整批结束，无论成败都要把界面恢复回来
    setDownloadUiBusy(false);
    m_progressThrottle.invalidate();

    if (success) {
        appendLog(tr("✓ 版本 %1 下载完成：%2").arg(versionId, errorMessage));
        // 这里不刷新版本列表：列表显示的是在线版本清单，下载完成不会改变它；
        // 而 refreshVersionList() 会 clear() 掉列表、连用户当前选中的版本一起丢掉，
        // 刚下完想点启动还得重新选一次。
        QMessageBox::information(this, tr("下载完成"),
                                 tr("版本 %1 的文件已全部就绪。\n\n%2").arg(versionId, errorMessage));
    } else {
        appendLog(tr("✗ 版本 %1 下载失败：%2").arg(versionId, errorMessage));
        QMessageBox::warning(this, tr("下载失败"),
                             tr("版本 %1 有文件未能下载完成。\n\n%2").arg(versionId, errorMessage));
    }
}

void MainWindow::onVersionDetailsFetched(const QString &versionId, const QJsonObject &versionJson) {
    if (versionJson.isEmpty()) {
        appendLog(tr("获取版本 %1 的信息失败。").arg(versionId));
        setDownloadUiBusy(false);
        QMessageBox::warning(this, tr("下载失败"),
                             tr("无法获取版本 %1 的信息，请检查网络后重试。").arg(versionId));
        return;
    }

    appendLog(tr("版本信息获取成功，开始下载游戏文件..."));
    QString versionsPath =
        QCoreApplication::applicationDirPath() + "/.minecraft/versions/" + versionId;
    m_versionManager->downloadVersion(versionId, versionsPath);
}