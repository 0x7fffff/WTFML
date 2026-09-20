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
#include <QComboBox>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QProcess>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTextEdit>
#include <QVBoxLayout>

#include "auth/AuthManager.hpp"
#include "launcher/LauncherManager.hpp"
#include "manage/FolderManager.hpp"
#include "manage/VersionManager.hpp"
#include "settings/GlobalJavaSettings.hpp"
#include "settings/LauncherSettings.hpp"
#include "util/AppPaths.hpp"

namespace {

/**
 * @brief 读下拉框里选中的版本号（**不是**显示文本）。
 * @param combo 版本下拉框。
 * @return 选中的版本号；没选中任何项时返回空串。
 *
 * NOTE: 取的是 Qt::UserRole 里存的裸 id，不是 currentText()。这两个值从"版本显示名"
 *       那次改动起就分家了：条目文本可能是 `我的整合包-1.20.1-Forge`，而真正进
 *       `--version` 和文件路径的永远是 id。拿文本当版本号用会一路静默地拼出错误路径。
 *
 * currentIndex() 为 -1 表示"当前什么都没选"（下拉框空着，或者刚好被 clear() 过），
 * 这时 currentData() 也要当没选。显式写 currentIndex() 是为了说清为什么空串可以当
 * 判据：任何版本号都不会是空串。
 */
QString selectedVersionId(const QComboBox *combo) {
    return combo->currentIndex() < 0 ? QString() : combo->currentData().toString();
}

/**
 * @brief 把一批版本灌进下拉框，并尽量保住用户原来选中的那一项。
 * @param combo 目标下拉框。
 * @param parts 每个版本的显示名组成部分。
 * @param options 全局「版本名格式」开关。
 *
 * 条目文本是 formatVersionName() 拼出来的显示名，版本号存在 Qt::UserRole 里，
 * 两者由 selectedVersionId() / currentText() 分开取。同一个版本在两个下拉框里
 * 可能显示得不一样（比如一个 `我的整合包-1.20.1`、一个 `1.20.1`），这是两个数据源
 * 各自独立设计的必然结果：已安装那边能从磁盘读到自定义名，在线清单 800+ 条只填 id。
 * 别为了"看着一致"给在线清单加逐条文件 I/O。
 *
 * 以前只有一个 QListWidget，clear() 会把用户选中的版本一起丢掉，所以下载完成时
 * 干脆不刷新列表。现在刷新必须自己把选中项捡回来。
 *
 * 语义是"只保住、不新增"：原来选的还在清单里就选回去；已经不在了（目录被删、
 * manifest 变了）就退回 currentIndex() == -1，让占位文案显出来，逼用户重新明确
 * 挑一个 —— 启动和下载都是重操作，不替用户猜。
 */
void fillVersionCombo(QComboBox *combo, const QList<VersionDisplayParts> &parts,
                      const VersionNameOptions &options) {
    const QString previous = selectedVersionId(combo);

    // 重灌过程会发 currentIndexChanged。现在接的这些槽都是幂等的，但先挡上，
    // 免得以后有人接槽时被中间态（先变成 -1 再选中）刷两遍。
    const QSignalBlocker blocker(combo);
    combo->clear();
    for (const VersionDisplayParts &part : parts) {
        // NOTE: 必须逐条 addItem(显示名, id)。addItems(QStringList) 是**不设**
        // Qt::UserRole 的，用它就等于所有条目的 currentData() 都是无效 QVariant，
        // selectedVersionId() 会一路返回空串 —— 启动按钮就再也启不动任何版本了。
        combo->addItem(formatVersionName(part, options), part.id);
    }

    // NOTE: 别指望 QComboBox 自己选中第 0 项。只要下拉框设了非空 placeholderText，
    // 往空模型里插第一批数据时它不会自动选中任何一项。选中项只能自己恢复。
    // 也必须按 id（findData）找回来，不能按显示文本（findText）——显示名会随
    // 自定义名/开关变，拿它当身份标识一定找错。
    if (previous.isEmpty()) {
        return;
    }
    if (const int restored = combo->findData(previous); restored >= 0) {
        combo->setCurrentIndex(restored);
    }
}

/**
 * @brief 只有版本号、没有自定义名和加载器信息的那些版本（在线清单）。
 * @param versionIds 版本号清单。
 * @return 与 VersionDisplayParts 同长的清单，除 id 外全是空。
 *
 * 在线清单有 800+ 条，这一层刻意不做任何文件 I/O：字段留空，显示名就退化成纯版本号。
 */
QList<VersionDisplayParts> plainVersionParts(const QStringList &versionIds) {
    QList<VersionDisplayParts> parts;
    parts.reserve(versionIds.size());
    for (const QString &id : versionIds) {
        VersionDisplayParts part;
        part.id = id;
        parts.append(part);
    }
    return parts;
}

}  // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_ui(new MainWindowUI(this)),
      m_launcherManager(new LauncherManager(this)),
      m_authManager(new AuthManager(this)),
      m_folderManager(new FolderManager(this)),
      m_versionManager(new VersionManager(this)),
      m_javaSettings(new GlobalJavaSettings(this)),
      m_settings(new LauncherSettings(this)) {
    setupConnections();
    // 本地已安装的先填上（同步扫盘）；在线清单是异步拉的，回来之后由
    // onVersionsFetched() 填"可安装"下拉框 —— 在那之前它显示占位文案。
    refreshInstalledVersions();
    // 顺序要紧：必须在 refreshInstalledVersions() **之后**（下拉框里得先有条目可选），
    // 并且在 fetchAvailableVersions() 之前（免得用户在等待网络期间就手选了一个，
    // 那样启动时反而会把用户的选择抢掉）。也**只在启动时**跑这一次 ——
    // 不能塞进 refreshInstalledVersions()，那个函数每次清单到达、每次下载完成都会跑，
    // 塞进去就破坏了它"只保住、不新增"的语义。
    restoreLastLaunchedVersion();
    m_versionManager->fetchAvailableVersions();
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
    // 占位按钮：先只写一行日志，别做成点了完全没反应（那反而像个 bug）。
    // TODO: 换成真正的文件夹管理页面。
    connect(m_ui->getManageFolderButton(), &QPushButton::clicked, this,
            [this] { appendLog(tr("文件夹管理功能尚未实现。")); });
    // 用户在"已安装版本"下拉框里换一个版本，按钮第二行立刻跟着变。
    // NOTE: 这条只管用户手选。fillVersionCombo() 重灌下拉框时信号是被挡住的，
    // 那条路径由 refreshInstalledVersions() 自己收尾，两条路合起来才不会漏。
    // NOTE: 接的是 currentIndexChanged 而不是 currentTextChanged。后者在"两个条目
    // 渲染成同一串文字"之间切换时**不发射** —— QComboBoxPrivate::updateCurrentText()
    // 发现文本没变就提前 return（qcombobox.cpp:2956-2963）。而按下标看就不会漏：
    // 关掉「版本名」档之后，`我的整合包-Forge` 和 `别的名字-Forge` 完全可能显示成
    // 一样，那时按钮第二行和 tooltip 会停在旧值上。
    connect(m_ui->getInstalledVersionCombo(), &QComboBox::currentIndexChanged, this,
            [this] { updateStartButtonText(); });
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
    // 两个下拉框一起灰掉。在下什么，在点击那一刻就已经交给 VersionManager 了，
    // 这时候改选中项并不会改变在下的是哪个版本 —— 灰掉纯粹是不让"界面显示选的 A、
    // 实际在下的却是 B"这种事发生，免得看的人误判。
    // "文件夹管理"是占位按钮，跟下载无关，不跟着灰。
    m_ui->getInstalledVersionCombo()->setEnabled(!busy);
    m_ui->getAvailableVersionCombo()->setEnabled(!busy);

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

void MainWindow::refreshInstalledVersions() {
    // 只扫本地磁盘（.minecraft/versions/<id>/<id>.jar），同步返回，随叫随到。
    //
    // 识别不出来的版本只在第一次上报：本函数一次会话会被调三次（构造、版本列表到达、
    // 下载完成），每回都报就是刷屏。第一次报过之后连扫都不扫了 —— 传 nullptr 时
    // installedVersions() 走的是和加这个参数之前完全一样的那条路径。
    QStringList warnings;
    const QStringList installed = m_versionManager->installedVersions(
        m_reportedUnrecognizedVersions ? nullptr : &warnings);
    if (!warnings.isEmpty()) {
        m_reportedUnrecognizedVersions = true;
        for (const QString &warning : warnings) {
            appendLog(warning);
        }
    }

    QComboBox *combo = m_ui->getInstalledVersionCombo();
    QList<VersionDisplayParts> parts;
    parts.reserve(installed.size());
    for (const QString &id : installed) {
        // 每个版本读一次 <id>/wtfml.meta（几百字节的可选文件，绝大多数版本没有）。
        // 刻意不缓存：这个文件会被"重命名"功能改写，缓存了就会变成"改了名不生效"。
        // 开销上不必担心 —— 一次会话最多调三次，而真机上实例数可以到 61 个。
        parts.append(m_versionManager->displayParts(id));
    }
    fillVersionCombo(combo, parts, m_settings->versionNameOptions());

    // 占位文案随上下文变。**两个串都必须非空**：空占位文案会让 QComboBox 在往空
    // 模型里插第一批数据时静默选中第 0 项（qcombobox.cpp:1753-1759），而这里必须
    // 停在"没选"，否则用户会在毫无察觉的情况下启动一个自己没挑过的版本。
    //
    // 这一段同时治掉一个真实的用户报告：「打开启动器时显示没有已安装版本」——
    // 真因就是这句以前写死成"暂无已安装版本"，灌完数据后没人改它，于是明明装了
    // 好几个版本，画面上却说着一个都没有。
    combo->setPlaceholderText(installed.isEmpty() ? MainWindowUI::noInstalledVersionPlaceholder()
                                                 : MainWindowUI::chooseVersionPlaceholder());

    // 刷完必须手动同步按钮文字和 tooltip：上面 fillVersionCombo() 里套了
    // QSignalBlocker，重灌过程不会发信号，光靠 connect 那条接不到。
    updateStartButtonText();
}

void MainWindow::restoreLastLaunchedVersion() {
    const QString remembered = m_settings->lastLaunchedVersion();
    if (remembered.isEmpty()) {
        // 从没成功启动过任何版本：留空，让用户自己挑。这里不是错误路径，不打日志。
        return;
    }

    QComboBox *combo = m_ui->getInstalledVersionCombo();
    if (combo->currentIndex() >= 0) {
        // 已经有选中项了（理论上不会发生：构造函数里刚刷完、placeholderText 非空所以
        // 灌数据时不会自动选中第 0 项）。真发生了也不抢 —— 当前显示出来的那个才是
        // 用户看得见的选择，偷偷改掉比不恢复更糟。
        return;
    }

    const int index = combo->findData(remembered);
    if (index < 0) {
        // 记着的那个版本已经不在磁盘上了（被删了、目录改过名）。此时**刻意留空**：
        // 退回"再选一个"，而不是替用户改选一个别的版本 —— 启动是重操作，不替他猜。
        // 占位文案是"请选择一个版本"，开头那句"暂无已安装版本"只在真一个都没装时出现。
        qDebug() << "Last launched version is no longer installed:" << remembered;
        return;
    }
    // 显式收尾，**不依赖** currentIndexChanged 那条连接。那条连接是活的（这里没有
    // QSignalBlocker），但依赖它就等于依赖"setupConnections() 已经跑过了"这个隐藏
    // 前提 —— 而这里少刷一次的后果是按钮第二行停在"未选择版本"、和上面选中的版本
    // 对不上，正是 mw_check 那一整摊断言在防的事。这一句是幂等的，重复跑不花钱。
    combo->setCurrentIndex(index);
    updateStartButtonText();
}

void MainWindow::updateStartButtonText() {
    // 按钮文字分两行，第二行显示当前选中的版本，"点下去会启动哪个"不用再回头
    // 看上面的下拉框。名字可能很长（26.2-snapshot-1，或"我的整合包-1.20.1-Forge"），
    // 交给 QPushButton 自己按宽度决定换不换行，这里只保证第一行永远是"启动游戏"。
    QComboBox *combo = m_ui->getInstalledVersionCombo();
    const QString shown =
        combo->currentIndex() < 0 ? tr("未选择版本") : combo->currentText();
    m_ui->getStartButton()->setText(tr("启动游戏\n%1").arg(shown));

    // 下拉框的 tooltip 挂**裸 id**，不是显示名。两个理由：
    //   1. 下拉框设了 minimumContentsLength(16)，长显示名会被省略号截断；
    //   2. 更要紧的是，关掉「版本名」档之后 id 在整个界面上就再也看不见了，
    //      而它才是真正进 --version 和文件路径的那个值。用户要排查"怎么启动错了
    //      版本"时，悬停是唯一能看到它的地方。
    // tooltip 必须走这条函数（而不是在 refreshInstalledVersions 里直接设），因为
    // fillVersionCombo() 套了 QSignalBlocker，刷新路径只能靠这一处收尾。
    const QString id = selectedVersionId(combo);
    combo->setToolTip(id.isEmpty() ? QString() : tr("版本目录名：%1").arg(id));
}

void MainWindow::onVersionsFetched(const QStringList &versions) {
    // 只填"可安装"下拉框：它唯一的数据源就是在线的版本清单。以前清单为空时这里会
    // 退回显示本地已安装版本，两个数据源混在一个列表里，用户分不清点下去到底是
    // 启动还是下载 —— 现在两边各管各的。
    // 在线清单只填 id：800+ 条逐条去读 wtfml.meta 是不可接受的 I/O，而且那些版本
    // 压根还没下载、磁盘上没有元数据可读。显示名因此退化成纯版本号。
    fillVersionCombo(m_ui->getAvailableVersionCombo(), plainVersionParts(versions),
                     m_settings->versionNameOptions());

    // 顺带把已安装的也刷一遍：一次网络往返之后两个下拉框都是最新的，这条不变式
    // 很便宜（就是一次 QDir 扫描），但能兜住"清单在拉的这段时间用户往
    // .minecraft/versions 里拷了个版本目录"这种情况。
    refreshInstalledVersions();

    if (versions.isEmpty()) {
        // 拉清单失败时 DownloadManager 也会发这个信号（发的是空清单），别让
        // "正在获取..." 一直挂着骗人。
        m_ui->getAvailableVersionCombo()->setPlaceholderText(
            tr("在线版本列表获取失败，请检查网络"));
        appendLog(tr("在线版本列表获取失败（网络异常？）。"));
        return;
    }
    m_ui->getAvailableVersionCombo()->setPlaceholderText(tr("请选择要下载的版本"));
    appendLog(tr("已更新在线版本列表 (%1 个版本)。").arg(versions.size()));
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

    // 只认"已安装版本"下拉框：启动只可能发生在本地已经装好的版本上
    const QString versionId = selectedVersionId(m_ui->getInstalledVersionCombo());
    if (versionId.isEmpty()) {
        QMessageBox::warning(this, tr("未选择版本"),
                             tr("请先在\"已安装版本\"下拉框中选择一个版本。"));
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
    // 记住"上次启动的版本"的时机是**进程真的起来了**，不是"表达式算完了"。
    // startGame() 有三条早退路径（进程已在运行 / 找不到 Java / waitForStarted 超时），
    // 以前它们都会先骗一句"已开始启动游戏"再显示错误。以"起来了"为准也是对的：
    // 因为库缺失之类的原因崩掉，恰恰是最需要下次打开还选着这个版本的时候。
    if (m_launcherManager->startGame(jvmArgs, *m_javaSettings, versionId)) {
        m_settings->setLastLaunchedVersion(versionId);
        appendLog(tr("已开始启动游戏，正在执行 JVM 参数..."));
    }
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

    // 只认"可安装版本"下拉框：下载针对的是在线清单里的版本
    const QString versionId = selectedVersionId(m_ui->getAvailableVersionCombo());
    if (versionId.isEmpty()) {
        QMessageBox::warning(this, tr("未选择版本"),
                             tr("请先在\"可安装版本\"下拉框中选择一个版本。"));
        return;
    }

    const QString versionJsonPath = AppPaths::versionJsonPath(versionId);
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

        // 刷新"已安装版本"下拉框：client jar 已经落到
        // .minecraft/versions/<id>/<id>.jar，installedVersions() 下次扫盘就能看到它。
        // NOTE: 老代码注释里写着"这里不刷新列表：refreshVersionList() 会 clear() 掉
        // 列表、连用户选中的版本一起丢"—— 那个顾虑现在没有了，因为
        // fillVersionCombo() 自己负责把原来选中的版本捡回来。
        refreshInstalledVersions();

        // 刷新只能"保住"原来的选中项，保不住"原来压根没选"：那种情况下把刚下完的
        // 版本顶上，省得用户下完还要自己去下拉框里再找一遍。原来已经有选中项时
        // 不覆盖 —— 用户自己挑的版本比"最新下完的那个"更重要。
        QComboBox *installedCombo = m_ui->getInstalledVersionCombo();
        if (installedCombo->currentIndex() < 0) {
            // 按 id 找（findData），不是按显示文本（findText）——刚下完的版本可能
            // 已经有自定义名，显示文本不一定是版本号。
            if (const int index = installedCombo->findData(versionId); index >= 0) {
                installedCombo->setCurrentIndex(index);
            }
        }

        QMessageBox::information(this, tr("下载完成"),
                                 tr("版本 %1 的文件已全部就绪。\n\n%2").arg(versionId, errorMessage));
    } else {
        // 失败就不刷新了：没成功就不该有新条目冒出来（jar 大概率没落地）
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
    QString versionsPath = AppPaths::versionDir(versionId);
    m_versionManager->downloadVersion(versionId, versionsPath);
}