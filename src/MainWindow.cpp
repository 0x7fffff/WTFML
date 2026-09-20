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
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QProcess>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>

#include "auth/AuthManager.hpp"
#include "launcher/LauncherManager.hpp"
#include "manage/FolderManager.hpp"
#include "manage/VersionManager.hpp"
#include "settings/GlobalJavaSettings.hpp"
#include "settings/LauncherSettings.hpp"
#include "util/AppPaths.hpp"

namespace {

/**
 * @brief 把 MB 数说成人话：1 GB 以上按 GB 显示一位小数，否则按 MB。
 * @param mb 内存量（MB）；<= 0 表示"读不到"。
 * @return 给界面看的字符串；读不到时是空串（调用方自己决定怎么说这件事）。
 *
 * 只说数字和单位，不带句子 —— 句子在 MainWindow 里拼，那里有 tr()。这个文件里的
 * 自由函数没有翻译上下文，别在这儿硬塞中文句子。
 */
QString humanMemorySize(qint64 mb) {
    if (mb <= 0) {
        return QString();
    }
    if (mb >= 1024) {
        return QStringLiteral("%1 GB").arg(QString::number(double(mb) / 1024.0, 'f', 1));
    }
    return QStringLiteral("%1 MB").arg(mb);
}

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
 * @brief 让列表的选中项跟上下拉框（两边都按 id 找，**不按文本**）。
 * @param combo 版本下拉框。
 * @param list 与它同一份数据的列表。
 *
 * 按 id 而不是文本：同一个版本在两个控件里可能显示得不一样，而且显示名会随
 * 「版本名格式」的开关变。拿文本当身份标识一定找错。
 */
void syncListToCombo(const QComboBox *combo, QListWidget *list) {
    const QString id = selectedVersionId(combo);
    // 挡信号：这里是"跟着别人变"，不该反过来再把下拉框动一遍（两条路互相触发
    // 就成环了）。真正的用户操作才发信号。
    const QSignalBlocker blocker(list);
    if (id.isEmpty()) {
        list->setCurrentRow(-1);
        return;
    }
    for (int row = 0; row < list->count(); ++row) {
        if (list->item(row)->data(Qt::UserRole).toString() == id) {
            list->setCurrentRow(row);
            return;
        }
    }
    // 列表里没有它（清单里被删掉的版本）：留空，别硬选一个别的。
    list->setCurrentRow(-1);
}

/**
 * @brief 让下拉框跟上列表的选中项。
 * @param combo 版本下拉框。
 * @param list 与它同一份数据的列表。
 *
 * 已经是同一项时**什么都不做** —— 这就是"列表 → 下拉框 → 列表"不会死循环的原因：
 * 第二条腿发现两边一致，立刻停。下拉框真被改动时它会发 currentIndexChanged，
 * 按钮文字和底栏跟着刷新的那条链路照跑不误。
 */
void syncComboToList(QComboBox *combo, const QListWidget *list) {
    const QListWidgetItem *item = list->currentItem();
    if (item == nullptr) {
        return;
    }
    const int index = combo->findData(item->data(Qt::UserRole).toString());
    if (index >= 0 && combo->currentIndex() != index) {
        combo->setCurrentIndex(index);
    }
}

/**
 * @brief 把一批版本同时灌进"下拉框 + 列表"这一对，并让两边选中同一项。
 * @param combo 版本下拉框。
 * @param list 与它同一份数据的列表。
 * @param parts 每个版本的显示名组成部分。
 * @param options 全局「版本名格式」开关。
 *
 * 界面上这两个控件是**同一份数据的两种看法**（下拉框是"选一个"，列表是"看一眼都有
 * 什么"），所以文本也走同一个 formatVersionName()，不许各拼一套 —— 各拼一套就会出现
 * "列表里叫 A、下拉框里叫 B"，用户根本认不出是同一个版本。
 */
void fillVersionViews(QComboBox *combo, QListWidget *list,
                      const QList<VersionDisplayParts> &parts, const VersionNameOptions &options) {
    fillVersionCombo(combo, parts, options);

    {
        const QSignalBlocker blocker(list);
        list->clear();
        for (const VersionDisplayParts &part : parts) {
            // 版本号存进 Qt::UserRole，和下拉框同一个约定（显示名不是身份标识）。
            auto *item = new QListWidgetItem(formatVersionName(part, options), list);
            item->setData(Qt::UserRole, part.id);
        }
    }
    // 重灌之后把列表的选中项对齐到下拉框：下拉框那边已经负责"保住原来选的",
    // 列表跟着它走就行。不对齐的话，用户会看到上面选着 A、下面高亮着 B。
    syncListToCombo(combo, list);
}

/**
 * @brief 按 itemData 里的枚举值选中一项。
 * @return 找到并选中了返回 true；没有这一项返回 false（调用方决定要不要兜底）。
 *
 * NOTE: 枚举以 int 存进 itemData。界面这边只认"存进去的那个值"，不去 include
 *       src/settings 里的枚举 —— 界面层不该为了显示一个下拉框去依赖设置模块。
 */
template <typename Enum> bool selectByEnumData(QComboBox *combo, Enum value) {
    const int index = combo->findData(static_cast<int>(value));
    if (index < 0) {
        return false;
    }
    combo->setCurrentIndex(index);
    return true;
}

/**
 * @brief 往「Java 版本」下拉框里加一条运行时。
 * @param combo 目标下拉框。
 * @param majorVersion 主版本号；<= 0 表示不知道（钉死的那个路径就不一定知道）。
 * @param path java 可执行文件的完整路径。
 * @return 这条（新加的或本来就有的）条目的下标。
 *
 * NOTE: itemData 存的是**路径**，不是下标。这个下拉框里的条目会随扫描结果变多，
 *       拿下标当身份标识会在下次扫描后指到另一个 Java 上。
 *       空路径是保留给「自动」那一条的（它没有路径），所以这里拒绝空路径。
 */
int addJavaRuntimeItem(QComboBox *combo, int majorVersion, const QString &path) {
    if (path.isEmpty()) {
        return -1;
    }
    const int existing = combo->findData(path);
    if (existing >= 0) {
        return existing;
    }
    // 版本号不知道时只显示路径（钉死的那个路径不在扫描结果里就是这样）
    const QString label = majorVersion > 0
                              ? QStringLiteral("Java %1 — %2").arg(majorVersion).arg(path)
                              : path;
    combo->addItem(label, path);
    return combo->count() - 1;
}

/**
 * @brief 按当前设定把「Java 版本」下拉框的选中项摆正（调用方负责挡信号）。
 * @param combo 目标下拉框。
 * @param autoSelect 设定是不是"自动"。
 * @param path 设定的路径。
 *
 * 自动档选那一条 itemData 为**空串**的条目（只有它一条是空的）。
 * NOTE: 「手动档但路径是空的」这种配置只可能来自手改注册表（界面这条路上手动档
 *       一定带着一个真实路径）—— 那种情况下这里会落到「自动」那一条，而
 *       updateJavaPathLabel() 会照实说明它其实不会自动换 Java。不替用户改数据。
 */
void selectConfiguredJava(QComboBox *combo, bool autoSelect, const QString &path) {
    const QString wanted = autoSelect ? QString() : path;
    const int index = combo->findData(wanted);
    combo->setCurrentIndex(index >= 0 ? index : 0);
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
    // 设置页先按盘上的值摆好（不扫 Java，那一段等第一次切到设置页，见
    // refreshJavaList()）。必须在下面 fetchAvailableVersions() **之前**：
    // 清单请求要不要走镜像就是在这一步交给 VersionManager 的。
    applySettingsToUi();
    // 本地已安装的先填上（同步扫盘）；在线清单是异步拉的，回来之后由
    // onVersionsFetched() 填"可安装"下拉框 —— 在那之前它显示占位文案。
    refreshInstalledVersions();
    // 顺序要紧：必须在 refreshInstalledVersions() **之后**（下拉框里得先有条目可选），
    // 并且在 fetchAvailableVersions() 之前（免得用户在等待网络期间就手选了一个，
    // 那样启动时反而会把用户的选择抢掉）。也**只在启动时**跑这一次 ——
    // 不能塞进 refreshInstalledVersions()，那个函数每次清单到达、每次下载完成都会跑，
    // 塞进去就破坏了它"只保住、不新增"的语义。
    restoreLastLaunchedVersion();
    // 底栏那三格的初始值：登录名可能已经从存下来的令牌里恢复了，进度两格先藏起来，
    // 运行状态也要等第一次真的启动才有内容。
    updateBottomBar();
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
            [this] {
                updateStartButtonText();
                // 底栏右下角那条状态要看"选中的是不是正在跑的那个实例"，
                // 所以换版本也得跟着刷一遍。
                updateBottomBar();
                // 右栏那张列表是同一份数据的另一种看法，跟着一起亮。
                syncListToCombo(m_ui->getInstalledVersionCombo(),
                                m_ui->getInstalledVersionList());
            });
    // 列表 → 下拉框。点列表里的一行 = 在上面把它选中（用户要求的行为）。
    connect(m_ui->getInstalledVersionList(), &QListWidget::currentItemChanged, this,
            [this] {
                syncComboToList(m_ui->getInstalledVersionCombo(),
                                m_ui->getInstalledVersionList());
            });
    connect(m_ui->getAvailableVersionCombo(), &QComboBox::currentIndexChanged, this,
            [this] {
                syncListToCombo(m_ui->getAvailableVersionCombo(),
                                m_ui->getAvailableVersionList());
            });
    connect(m_ui->getAvailableVersionList(), &QListWidget::currentItemChanged, this,
            [this] {
                syncComboToList(m_ui->getAvailableVersionCombo(),
                                m_ui->getAvailableVersionList());
            });
    connect(m_ui->getDownloadButton(), &QPushButton::clicked, this,
            &MainWindow::onDownloadButtonClicked);

    // ---------- 设置页 ----------
    // 「Java 版本」：选自动，或者钉死一个具体运行时。
    connect(m_ui->getJavaVersionCombo(), &QComboBox::currentIndexChanged, this,
            &MainWindow::onJavaSelectionChanged);
    // 内存：先接「自动 / 手动」总开关，再接两个数字框。
    // 自动档下两个框是禁用的（见 refreshMemoryUi），所以"用户改数字"这件事只可能发生在
    // 手动档，onMemoryChanged 里那句 autoMemory() 守卫是给程序内部 setValue 兜底的。
    connect(m_ui->getMemoryAutoCheck(), &QCheckBox::toggled, this,
            &MainWindow::onMemoryAutoChanged);
    // 两个 spin 共用一个槽。谁改的在这里区分不了（也不该区分，见 onMemoryChanged）。
    connect(m_ui->getMemoryMaxSpin(), &QSpinBox::valueChanged, this,
            &MainWindow::onMemoryChanged);
    connect(m_ui->getMemoryMinSpin(), &QSpinBox::valueChanged, this,
            &MainWindow::onMemoryChanged);
    connect(m_ui->getLauncherBehaviourCombo(), &QComboBox::currentIndexChanged, this,
            &MainWindow::onLauncherBehaviourChanged);
    connect(m_ui->getDownloadSourceCombo(), &QComboBox::currentIndexChanged, this,
            &MainWindow::onDownloadSourceChanged);
    // JVM 参数刻意**不**接 textChanged：边打字边存会把打到一半的半条参数写进配置，
    // 而它是直接塞进命令行的东西，半条参数会让游戏起不来。改完点「应用」。
    connect(m_ui->getApplyExtraJvmArgsButton(), &QPushButton::clicked, this,
            &MainWindow::onApplyExtraJvmArgsClicked);
    // 四个「版本名格式」勾选框共用一个槽：归一化要同时看四个值。
    const auto nameOptionChanged = [this] { onVersionNameOptionChanged(); };
    connect(m_ui->getVersionNameCustomCheck(), &QCheckBox::toggled, this, nameOptionChanged);
    connect(m_ui->getVersionNameRealCheck(), &QCheckBox::toggled, this, nameOptionChanged);
    connect(m_ui->getVersionNameLoaderCheck(), &QCheckBox::toggled, this, nameOptionChanged);
    connect(m_ui->getVersionNameLoaderVersionCheck(), &QCheckBox::toggled, this,
            nameOptionChanged);

    // 选项卡切换：底栏右下角的显示规则依赖"当前是不是在主页"，切页就得重算；
    // 第一次切到设置页还要顺带扫一遍 Java。
    connect(m_ui->getTabWidget(), &QTabWidget::currentChanged, this,
            &MainWindow::onMainTabChanged);

    // 登录成功之后左下角要换成登录名（头像那块字母也跟着换）
    connect(m_authManager, &AuthManager::loginSucceeded, this, [this] { updateBottomBar(); });

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
    // 两张列表是同一份数据的另一种看法，也一起灰掉：不然用户能在列表里点来点去、
    // 看着选中的版本变了，而实际在下的是另一个。
    m_ui->getInstalledVersionList()->setEnabled(!busy);
    m_ui->getAvailableVersionList()->setEnabled(!busy);

    QProgressBar *progressBar = m_ui->getProgressBar();
    if (busy) {
        // 还不知道总任务数：先转成不确定态的滚动条，拿到 stats 再换成确定态
        progressBar->setRange(0, 0);
        m_ui->getStatusLabel()->setText(tr("正在准备下载..."));
    } else {
        progressBar->setRange(0, 100);
        progressBar->setValue(0);
    }
    // 这一格的显隐**不在这里定**：底栏中间那格要同时受"在下载"和"在启动"两条影响，
    // 只有 updateBottomBar() 一处说了算，免得两处各改一次互相盖掉。
    updateBottomBar();
}

void MainWindow::updateBottomBar() {
    // ---------- 左：头像 + 用户名 ----------
    // NOTE: AuthManager 的登录流程只把令牌落盘，**没有**去取玩家档案，所以
    //       playerName() 目前恒为它构造时那个默认值。真名与皮肤头像要等 profile
    //       API（并给 AuthManager 补一个"读回来的名字"）。这里不自己造假名字。
    const QString name = m_authManager->playerName();
    m_ui->getAccountLabel()->setText(name);
    m_ui->getAvatarLabel()->setText(name.isEmpty() ? QStringLiteral("?")
                                                    : name.left(1).toUpper());

    // ---------- 中：进度（启动优先于下载） ----------
    // 两件事都会占这一格，启动优先 —— 下载可以慢慢来，用户刚点完启动那几秒需要反馈。
    QProgressBar *progressBar = m_ui->getProgressBar();
    QLabel *statusLabel = m_ui->getStatusLabel();
    const bool showProgress = m_launchInFlight || m_downloadInFlight;
    progressBar->setVisible(showProgress);
    statusLabel->setVisible(showProgress);
    if (m_launchInFlight) {
        // 启动进度没有可测量的百分比（JVM 和游戏自己在初始化，没有任何进度回调），
        // 所以是不确定态的滚动条。等游戏吐出第一行输出（或进程结束）就收起来。
        progressBar->setRange(0, 0);
        statusLabel->setText(tr("正在启动 %1 ...").arg(instanceDisplayName(m_launchingVersionId)));
    } else if (!m_downloadInFlight) {
        // 没有下载时把进度条复位：下一次下载开始前它会先显示上次的旧值，闪一下。
        progressBar->setRange(0, 100);
        progressBar->setValue(0);
        statusLabel->clear();
    }
    // 下载态下的文字和数值由 updateDownloadProgressUi() 维护，这里不覆盖。

    // ---------- 右：当前实例运行状态 ----------
    QLabel *runStateLabel = m_ui->getRunStateLabel();
    QLabel *runStateDetailLabel = m_ui->getRunStateDetailLabel();
    const QString running = m_launcherManager->runningVersionId();
    if (running.isEmpty()) {
        // 没有实例在跑：右下角整格藏起来，不显示"未运行"这种废话
        runStateLabel->setVisible(false);
        runStateDetailLabel->setVisible(false);
        return;
    }

    runStateLabel->setText(MainWindowUI::runStateRunningText());
    runStateLabel->setVisible(true);
    // 第二行要不要出，判据在 runStateShowsDetail() 里（那里也是它唯一的出处）。
    const bool showDetail = MainWindowUI::runStateShowsDetail(
        m_ui->isHomeTabCurrent(), selectedVersionId(m_ui->getInstalledVersionCombo()), running);
    runStateDetailLabel->setVisible(showDetail);
    runStateDetailLabel->setText(showDetail ? instanceDisplayName(running) : QString());
}

QString MainWindow::instanceDisplayName(const QString &versionId) const {
    // 用下拉框里那条文本，也就是用户看惯了的显示名（可能是"我的整合包-1.20.1-Forge"），
    // 而不是裸目录名。找不到（版本刚被删、或者压根不在本地）时退回版本号本身 ——
    // 底栏宁可显示一个用户没见过的目录名，也不能什么都不显示。
    const QComboBox *combo = m_ui->getInstalledVersionCombo();
    const int index = combo->findData(versionId);
    return index >= 0 ? combo->itemText(index) : versionId;
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
    // 前缘节流：200 ms 内的后续更新直接丢掉。不这么做的话，5000 多个 asset 会疯狂重排
    // 状态栏，界面反而卡住。
    // NOTE: 丢掉的那些**没有**"结束时补刷"这回事 —— 进度条和状态栏停在上一次越过阈值的
    //       那一帧，最多比真实进度旧 200 ms。整批结束由 setDownloadUiBusy(false) 把进度条
    //       归零收尾，所以停在哪一帧不影响收尾。
    if (m_progressThrottle.isValid() && m_progressThrottle.elapsed() < 200) {
        return;
    }
    m_progressThrottle.start();
    updateDownloadProgressUi(done, total, failed, currentFile);
}

void MainWindow::appendLog(const QString &text) {
    // 界面里**不再有**日志面板（2026-09 分选项卡时去掉的）：命令行里本来就有
    // LauncherManager 的全套输出，多加一块面板只是把同一批东西抄第二遍。
    //
    // NOTE: 所以这条通道是"给人事后看"的，不是"必须让人看见"的。**必须让用户看到**
    //       的消息（缺 Java、下载失败、未选版本、找不到 version.json）一律走
    //       QMessageBox 挡住操作，绝不能只写这里 —— 从资源管理器双击启动时，
    //       这里写的东西没有任何地方能显示出来。
    qDebug().noquote() << QStringLiteral("[WTFML]") << text;
}

void MainWindow::refreshInstalledVersions() {
    // 只扫本地磁盘（.minecraft/versions/<id>/<id>.jar），同步返回，随叫随到。
    //
    // 识别不出来的版本只在第一次上报：本函数一次会话会被调很多次（构造、版本清单到达、
    // 下载完成、每勾一下「版本名格式」都会走一遍），每回都报就是刷屏。第一次报过之后
    // 连扫都不扫了 —— 传 nullptr 时
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
        // 开销上不必担心 —— 真机上实例数可以到 61 个，而这个函数一次会话没有调用上界。
        parts.append(m_versionManager->displayParts(id));
    }
    fillVersionViews(combo, m_ui->getInstalledVersionList(), parts,
                     m_settings->versionNameOptions());

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
    //   1. 下拉框设了 minimumContentsLength(12)，长显示名会被省略号截断；
    //   2. 更要紧的是，id 在界面上**任何开关组合下都不会出现** —— 显示名那边参与
    //      拼装的"实际版本"是 parts.realVersion（为空才退化成 id），所以只要
    //      wtfml.meta 里写了 realVersion，目录名就彻底从界面上消失了。而它才是真正
    //      进 --version 和文件路径的那个值。用户要排查"怎么启动错了版本"时，
    //      悬停是唯一能看到它的地方。
    // tooltip 必须走这条函数（而不是在 refreshInstalledVersions 里直接设），因为
    // fillVersionCombo() 套了 QSignalBlocker，刷新路径只能靠这一处收尾。
    const QString id = selectedVersionId(combo);
    combo->setToolTip(id.isEmpty() ? QString() : tr("版本目录名：%1").arg(id));
}

void MainWindow::refreshAvailableVersions() {
    // 只填"可安装"那一对控件：它们唯一的数据源就是在线的版本清单。以前清单为空时
    // 这里会退回显示本地已安装版本，两个数据源混在一个列表里，用户分不清点下去
    // 到底是启动还是下载 —— 现在两边各管各的。
    // 在线清单只填 id：800+ 条逐条去读 wtfml.meta 是不可接受的 I/O，而且那些版本
    // 压根还没下载、磁盘上没有元数据可读。显示名因此退化成纯版本号。
    const QStringList versions = m_versionManager->availableVersions();
    if (versions.isEmpty()) {
        // 名单还没拉回来（或者拉失败了）。这时候**什么都不动**：清空一次既会把
        // 占位文案（"正在获取..." / "获取失败"）冲掉，也会把用户已经选中的版本丢掉。
        return;
    }
    fillVersionViews(m_ui->getAvailableVersionCombo(), m_ui->getAvailableVersionList(),
                     plainVersionParts(versions), m_settings->versionNameOptions());
}

// ======================================================================
// 「设置」选项卡
// ======================================================================
//
// 分工：MainWindowUI 只把控件搭出来（含"选项名称在左、控件在右"的两列布局），值和
// 持久化全在这一层。枚举 → 文案的映射也在这里 —— 界面层不该为了画一个下拉框去
// include src/settings，而 tr() 的文案本来就该和业务逻辑待在一起。
//
// 落盘时机逐项不同，这是这块最容易做错的地方，所以每一项都在自己的函数里说清楚：
//   Java / 内存 / 启动后 / 下载源 / 版本名格式 —— 改完立刻存
//   JVM 参数 —— 点「应用」才存（那是直接进命令行的东西，不能存半条）

void MainWindow::applySettingsToUi() {
    // 灌值时把信号全挡上：下面这些控件现在反映的是**盘上的值**，不是用户改的。
    // 不挡的话每条 currentIndexChanged/valueChanged 都会触发一遍"保存"，白写几次盘，
    // 更要紧的是那几条槽里还夹着真动作（改「版本名格式」会重刷两张版本列表）。
    const QSignalBlocker blockJava(m_ui->getJavaVersionCombo());
    const QSignalBlocker blockBehaviour(m_ui->getLauncherBehaviourCombo());
    const QSignalBlocker blockSource(m_ui->getDownloadSourceCombo());
    const QSignalBlocker blockCustom(m_ui->getVersionNameCustomCheck());
    const QSignalBlocker blockReal(m_ui->getVersionNameRealCheck());
    const QSignalBlocker blockLoader(m_ui->getVersionNameLoaderCheck());
    const QSignalBlocker blockLoaderVersion(m_ui->getVersionNameLoaderVersionCheck());

    // ---------- Java 版本 ----------
    // 本地扫到的那些运行时**不在这里加**：扫描要逐个跑 `java -version`，
    // 放到第一次切到设置页时再做（refreshJavaList()）。这里只放「自动」那条
    // （itemData 是空串，只有它是空的，见 selectConfiguredJava()）。
    m_ui->getJavaVersionCombo()->addItem(tr("自动（按版本挑选）"), QString());
    if (!m_javaSettings->autoSelectJava()) {
        // 手动档：把一个条目摆出来，好让下面这句选中它。路径来自盘上，
        // 可能指向一个早就删掉的 Java —— 那也是这个设定当前的真实状态，照实显示。
        addJavaRuntimeItem(m_ui->getJavaVersionCombo(), 0, m_javaSettings->javaPath());
    }
    selectConfiguredJava(m_ui->getJavaVersionCombo(), m_javaSettings->autoSelectJava(),
                         m_javaSettings->javaPath());
    updateJavaPathLabel();

    // ---------- 内存分配 ----------
    // 交给 refreshMemoryUi()：自动档下它要按当前系统内存算一遍值、禁用两个框、写说明
    // 文字，还要落盘。两个 spin 的信号在那边自己挡（这里的局部 blocker 就不再重复了）。
    refreshMemoryUi();

    // ---------- 启动后 ----------
    // 枚举值存在 itemData 里，文案是这里的 tr()。两边的对应关系只在这四行里。
    QComboBox *behaviourCombo = m_ui->getLauncherBehaviourCombo();
    behaviourCombo->addItem(tr("不变"), int(LauncherBehaviour::Unchanged));
    behaviourCombo->addItem(tr("最小化启动器"), int(LauncherBehaviour::Minimize));
    behaviourCombo->addItem(tr("收起启动器"), int(LauncherBehaviour::Close));
    behaviourCombo->addItem(tr("收起启动器，游戏退出后自动回来"),
                            int(LauncherBehaviour::CloseAndRestore));
    selectByEnumData(behaviourCombo, m_settings->launcherBehaviour());

    // ---------- JVM 参数 ----------
    m_ui->getExtraJvmArgsEdit()->setPlainText(m_javaSettings->extraJvmArgs());

    // ---------- 下载源 ----------
    QComboBox *sourceCombo = m_ui->getDownloadSourceCombo();
    sourceCombo->addItem(tr("官方源（Mojang）"), int(DownloadSource::Official));
    sourceCombo->addItem(tr("镜像源（BMCLAPI）"), int(DownloadSource::Mirror));
    selectByEnumData(sourceCombo, m_settings->downloadSource());

    // ---------- 版本名格式 ----------
    pushVersionNameOptionsToUi(m_settings->versionNameOptions());

    // 下载源得立刻交给发请求的那一层：构造函数里紧接着的
    // fetchAvailableVersions() 就会走它。
    syncDownloadSource();
}

void MainWindow::pushVersionNameOptionsToUi(const VersionNameOptions &options) {
    // 挡信号：这是"把结果画出来"，不是用户操作。不挡的话，下面第一个 setChecked
    // 会触发 onVersionNameOptionChanged()，它又回头去归一化、又回头调这个函数。
    const QSignalBlocker blockCustom(m_ui->getVersionNameCustomCheck());
    const QSignalBlocker blockReal(m_ui->getVersionNameRealCheck());
    const QSignalBlocker blockLoader(m_ui->getVersionNameLoaderCheck());
    const QSignalBlocker blockLoaderVersion(m_ui->getVersionNameLoaderVersionCheck());
    m_ui->getVersionNameCustomCheck()->setChecked(options.showCustomName);
    m_ui->getVersionNameRealCheck()->setChecked(options.showRealVersion);
    m_ui->getVersionNameLoaderCheck()->setChecked(options.showLoaderName);
    m_ui->getVersionNameLoaderVersionCheck()->setChecked(options.showLoaderVersion);
}

void MainWindow::updateJavaPathLabel() {
    QLabel *label = m_ui->getJavaPathLabel();
    if (!m_javaSettings->autoSelectJava()) {
        const QString path = m_javaSettings->javaPath();
        label->setText(path.isEmpty()
                           ? tr("手动指定：还没有路径，启动会直接失败。")
                           : tr("手动指定：%1").arg(path));
        return;
    }
    const QString cached = m_javaSettings->javaPath();
    label->setText(cached.isEmpty()
                       ? tr("自动：还没挑过，启动时按版本要求找。")
                       : tr("自动：上次用的是 %1").arg(cached));
}

void MainWindow::refreshJavaList() {
    if (m_javaListScanned) {
        return;
    }
    m_javaListScanned = true;

    QComboBox *combo = m_ui->getJavaVersionCombo();
    const QSignalBlocker blocker(combo);

    const QMap<int, QString> local = m_javaSettings->scanLocalJavaVersions();
    for (auto it = local.cbegin(); it != local.cend(); ++it) {
        addJavaRuntimeItem(combo, it.key(), it.value());
    }
    // 钉死的那个路径也要在列表里（它可能不在扫描结果里：用户在别的盘上装的 Java、
    // 或者那个路径已经失效）。没有它的话，用户一进设置页看到的是「自动」亮着，
    // 而设定其实是手动 —— 界面在说谎。
    addJavaRuntimeItem(combo, 0, m_javaSettings->javaPath());
    selectConfiguredJava(combo, m_javaSettings->autoSelectJava(), m_javaSettings->javaPath());
}

void MainWindow::onMainTabChanged() {
    // 底栏右下角的显示规则依赖"当前是不是在主页"，切页就得重算。
    updateBottomBar();
    if (m_ui->getTabWidget()->currentIndex() == MainWindowUI::settingsTabIndex()) {
        // 进设置页才做的两件事：扫 Java（要跑几次 `java -version`）、重算自动档内存
        // （可用内存是个一直在变的数，用户看到的那句说明得是**现在**的）。
        refreshJavaList();
        refreshMemoryUi();
    }
}

void MainWindow::onJavaSelectionChanged() {
    const QString path = m_ui->getJavaVersionCombo()->currentData().toString();
    if (path.isEmpty()) {
        // 「自动」那一条。**刻意不清空 m_javaPath**：那个字段同时是"上次挑中的
        // 路径"的缓存，清掉的话每次启动都要重新扫一遍本地 Java。
        m_javaSettings->setAutoSelectJava(true);
    } else {
        m_javaSettings->setAutoSelectJava(false);
        m_javaSettings->setJavaPath(path);
    }
    // 这两个 setter 只改内存，落盘要显式调（GlobalJavaSettings 的约定）。
    m_javaSettings->save();
    updateJavaPathLabel();
}

void MainWindow::refreshMemoryUi() {
    // 自动档下这里会按**现在**的系统内存重算一次 Xmx/Xms。手动档下 refreshSuggestedMemory()
    // 只读不动 —— 用户定好的两个数一个都不许改。
    const SystemMemory::Usage usage = m_javaSettings->refreshSuggestedMemory();
    const bool autoMemory = m_javaSettings->autoMemory();
    if (autoMemory) {
        // 自动算出来的值也落盘：盘上那份就是"上次按系统内存算出来的值"，用户从自动切到
        // 手动时正好拿它当起点，也让"界面显示的就是盘上存的"这条一直成立。
        m_javaSettings->save();
    }

    const QSignalBlocker blockCheck(m_ui->getMemoryAutoCheck());
    const QSignalBlocker blockMax(m_ui->getMemoryMaxSpin());
    const QSignalBlocker blockMin(m_ui->getMemoryMinSpin());
    m_ui->getMemoryAutoCheck()->setChecked(autoMemory);
    m_ui->getMemoryMaxSpin()->setValue(m_javaSettings->xmx());
    m_ui->getMemoryMinSpin()->setValue(m_javaSettings->xms());
    // 自动档下把两个框禁掉：里面是刚算出来的建议值，让人以为能改就是"改了不生效"。
    m_ui->getMemoryMaxSpin()->setEnabled(!autoMemory);
    m_ui->getMemoryMinSpin()->setEnabled(!autoMemory);

    // 说明文字：把"这台机器有多少内存、现在还剩多少、最后给了多少"三件事说全 ——
    // 自动档下用户看不到（也改不了）那两个数，只能靠这句话知道为什么是这个值。
    const QString totalText = humanMemorySize(usage.totalMb);
    const QString availableText = humanMemorySize(usage.availableMb);
    if (usage.totalMb <= 0) {
        // 读不到系统内存（非 Windows 平台或系统调用失败）：照实说，别让用户以为是"按你的
        // 内存算的"。实际用的是内置默认值。
        // NOTE: 这一支**不看 autoMemory**，是照着自动档写的：手动档下会对着用户自己刚
        //       定的那两个数说"自动分配用的是内置默认值"。手动档下那句话两头都不对，
        //       只是数值本身在上面两个框里能直接看到，就先这么写着。
        m_ui->getMemoryAutoHint()->setText(
            tr("读不到系统内存，自动分配用的是内置默认值（最大 %1 MB / 最小 %2 MB）。")
                .arg(m_javaSettings->xmx())
                .arg(m_javaSettings->xms()));
        return;
    }
    if (autoMemory) {
        m_ui->getMemoryAutoHint()->setText(
            tr("系统总内存 %1、当前可用 %2，自动给到 最大 %3 MB / 最小 %4 MB。"
               "每次启动前都会按当时的可用内存重算。")
                .arg(totalText, availableText)
                .arg(m_javaSettings->xmx())
                .arg(m_javaSettings->xms()));
        return;
    }
    m_ui->getMemoryAutoHint()->setText(
        tr("系统总内存 %1、当前可用 %2；现在是手动指定（最大 %3 MB / 最小 %4 MB）。")
            .arg(totalText, availableText)
            .arg(m_javaSettings->xmx())
            .arg(m_javaSettings->xms()));
}

void MainWindow::onMemoryAutoChanged() {
    const bool autoMemory = m_ui->getMemoryAutoCheck()->isChecked();
    m_javaSettings->setAutoMemory(autoMemory);  // 内部落盘
    // 切到自动：按当前系统内存算一次并禁掉两个框。切到手动：**留着**刚才那两个建议值，
    // 用户是想微调，不是想从内置默认值重来（refreshMemoryUi 里那条 autoMemory() 守卫
    // 保证手动档下不会去改值）。
    refreshMemoryUi();
    appendLog(autoMemory ? tr("内存分配：已改为按系统内存自动分配。")
                         : tr("内存分配：已改为手动指定。"));
}

void MainWindow::onMemoryChanged() {
    if (m_javaSettings->autoMemory()) {
        // 自动档下这两个框是禁用的，用户改不到这里。真走到这一句（程序内部 setValue）
        // 也不能把自动算出来的值当成"用户手动调的值"存下去 —— 那样下次进设置页就会看到
        // 手动档亮着，而用户从没动过它。
        return;
    }
    int maxMb = m_ui->getMemoryMaxSpin()->value();
    const int minMb = m_ui->getMemoryMinSpin()->value();

    if (minMb > maxMb) {
        // 最大 < 最小是 JVM 直接拒绝启动的配置。越界时**总是**把最小内存拉下来，
        // 不去顶用户刚填的那个数：两个 spin 共用一个槽，这里区分不了是谁刚被改的
        // （猜错的代价是把用户正在输入的那个数改掉，比"最小内存被拉平"难受得多）。
        // 同时挡信号：下面还要手动把两个值一起写回设置，不需要再来一轮。
        const QSignalBlocker blocker(m_ui->getMemoryMinSpin());
        m_ui->getMemoryMinSpin()->setValue(maxMb);
    }

    m_javaSettings->setXmx(maxMb);
    m_javaSettings->setXms(m_ui->getMemoryMinSpin()->value());
    m_javaSettings->save();
}

void MainWindow::onLauncherBehaviourChanged() {
    const int value = m_ui->getLauncherBehaviourCombo()->currentData().toInt();
    // setter 内部会比较，值没变就不写盘（构造时灌值那条路有 QSignalBlocker，走不到这里）。
    m_settings->setLauncherBehaviour(static_cast<LauncherBehaviour>(value));
}

void MainWindow::onDownloadSourceChanged() {
    const int value = m_ui->getDownloadSourceCombo()->currentData().toInt();
    m_settings->setDownloadSource(static_cast<DownloadSource>(value));
    syncDownloadSource();
}

void MainWindow::syncDownloadSource() {
    // 改完立刻生效，不用重启启动器 —— 设置是**发请求那一刻**读的（见 DownloadManager）。
    // 正在下着的那一批不会被中途改道，这是对的：半批官方半批镜像更难查。机制上是"一个
    // 批次的 URL 在入队那一刻就定死了"（downloadFile 收到的是一整个字符串），所以代价是
    // 这次下载开始之后再改设置，要等下一次下载才见效。
    m_versionManager->setUseMirror(m_settings->downloadSource() == DownloadSource::Mirror);
}

void MainWindow::onVersionNameOptionChanged() {
    VersionNameOptions options;
    options.showCustomName = m_ui->getVersionNameCustomCheck()->isChecked();
    options.showRealVersion = m_ui->getVersionNameRealCheck()->isChecked();
    options.showLoaderName = m_ui->getVersionNameLoaderCheck()->isChecked();
    options.showLoaderVersion = m_ui->getVersionNameLoaderVersionCheck()->isChecked();
    // setter 内部会 normalize 并落盘。
    m_settings->setVersionNameOptions(options);

    // 归一化可能把从属的那一项关掉（"加载器版本"的前提是"加载器名"）。必须把结果
    // 回灌到勾选框上：不回灌的话，用户取消"加载器名"之后界面上的"加载器版本"还亮着，
    // 而拼名字时它已经不参与 —— 界面在说谎，而且下次打开设置时它又自己灭了。
    pushVersionNameOptionsToUi(m_settings->versionNameOptions());

    // 显示名变了，界面上所有显示版本名的地方都得重拼。两处都不读磁盘以外的东西，
    // 重来一遍很便宜：已安装的那份本来就要读每个版本的 wtfml.meta。
    refreshInstalledVersions();
    refreshAvailableVersions();
}

void MainWindow::onApplyExtraJvmArgsClicked() {
    // 去首尾空白：末尾多一个空格会让命令行里多出一个空参数。
    // 空串是合法的（就是"不加额外参数"）。
    const QString args = m_ui->getExtraJvmArgsEdit()->toPlainText().trimmed();
    m_javaSettings->setExtraJvmArgs(args);
    m_javaSettings->save();
    appendLog(tr("已保存全局 JVM 参数：%1").arg(args.isEmpty() ? tr("（空）") : args));
}

void MainWindow::applyLauncherBehaviourAfterLaunch() {
    switch (m_settings->launcherBehaviour()) {
    case LauncherBehaviour::Minimize:
        showMinimized();
        break;
    case LauncherBehaviour::Close:
        // hide() 而不是 close()：close() 是"关窗口"，启动器是最后一个窗口时那等于
        // 退出进程，而游戏是它的子进程 —— 用户以为只是把启动器收起来，游戏跟着没了。
        hide();
        break;
    case LauncherBehaviour::CloseAndRestore:
        m_hiddenForGame = true;
        hide();
        break;
    case LauncherBehaviour::Unchanged:
        break;
    }
}

void MainWindow::onVersionsFetched(const QStringList &versions) {
    refreshAvailableVersions();

    // 顺带把已安装的也刷一遍：一次网络往返之后两个下拉框都是最新的，这条不变式
    // 很便宜（就是一次 QDir 扫描），但能兜住"清单在拉的这段时间用户往
    // .minecraft/versions 里拷了个版本目录"这种情况。
    refreshInstalledVersions();

    if (versions.isEmpty()) {
        // 拉清单失败时 DownloadManager 也会发这个信号，发的是**上一次成功的结果**
        // （首次拉取就失败才是空的），所以这一支实际拦的是"从来没拉到过"。别让
        // "正在获取..." 一直挂着骗人。列表那边同上：空名单不动它（本来就是空的）。
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

    // 自动档内存要**在这里**再算一次（不是开窗时算的那次）：从打开启动器到点下"启动"
    // 可能过了很久，中间可能开了浏览器、IDE，可用内存差好几 GB，而 JVM 只认这一刻的机器
    // 状态。手动档下这一句什么都不动。顺手也把设置页那两个框刷成这次实际用的值。
    refreshMemoryUi();

    // 按版本的自定义参数来自 <id>/wtfml.meta 的 "jvmArgs"（PCL 那半边"自定义启动
    // 参数"的对应物）；全局那半边是 GlobalJavaSettings::extraJvmArgs()，在
    // generateJvmArguments() 里比它先入列，所以单个实例能盖掉全局默认值。
    // 玩家名和底栏左下角显示的是**同一个**（AuthManager::playerName()），离线与正版
    // 两条分支都用它 —— 以前离线分支自己拼 "Player<时间戳>"、正版分支写死 "Player"，
    // 于是底栏上的名字和游戏里看到的名字可以不一样。
    const QStringList jvmArgs = m_launcherManager->generateJvmArguments(
        token, versionId, assetIndexId, versionJson, *m_javaSettings, offlineMode,
        javaMajorVersion, m_versionManager->versionJvmArgs(versionId),
        m_authManager->playerName());
    // 记住"上次启动的版本"的时机是**进程真的起来了**，不是"表达式算完了"。
    // startGame() 有三条早退路径（进程已在运行 / 找不到 Java / waitForStarted 超时），
    // 以前它们都会先骗一句"已开始启动游戏"再显示错误。以"起来了"为准也是对的：
    // 因为库缺失之类的原因崩掉，恰恰是最需要下次打开还选着这个版本的时候。
    if (m_launcherManager->startGame(jvmArgs, *m_javaSettings, versionId)) {
        m_settings->setLastLaunchedVersion(versionId);
        appendLog(tr("已开始启动游戏，正在执行 JVM 参数..."));
        // 启动进度从这里开始走到"游戏自己吐出第一行输出"为止。startGame() 返回之前
        // 那段（准备 natives、探 Java、等进程起来）是同步阻塞的，界面根本没机会重画，
        // 所以那一段不可能显示进度 —— 这里能覆盖的是随后的 JVM/游戏初始化。
        // 顺序上安全：startGame() 内部那条 "游戏已启动" 的 processOutput 是在它
        // return 之前同步发出的，会先于这次赋值到达，不会被当成"游戏已经出话了"。
        m_launchInFlight = true;
        m_launchingVersionId = versionId;
        // 这一档只对"真的起来了"生效 —— startGame() 那三条早退路径（进程已在跑 /
        // 找不到 Java / 超时）下面根本走不到这里。
        applyLauncherBehaviourAfterLaunch();
    }
    updateBottomBar();
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

void MainWindow::onProcessOutput(const QString &text) {
    appendLog(tr("[游戏输出] %1").arg(text));
    // 游戏自己出话了 = JVM 和游戏都初始化到能打日志了，"正在启动"这段到此为止。
    // 底栏中间那格让给下载进度（如果有），右下角换成"实例运行中"。
    if (m_launchInFlight) {
        m_launchInFlight = false;
        updateBottomBar();
    }
}

void MainWindow::onProcessError(const QString &text) {
    appendLog(tr("[游戏错误] %1").arg(text));
    // stderr 出东西也算"起来了"：有些版本把日志全写 stderr，等 stdout 会一直等下去。
    if (m_launchInFlight) {
        m_launchInFlight = false;
        updateBottomBar();
    }
}

void MainWindow::onGameFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (exitStatus == QProcess::CrashExit) {
        appendLog(tr("游戏崩溃，退出码：%1").arg(exitCode));
    } else {
        appendLog(tr("游戏进程已结束，退出码：%1").arg(exitCode));
    }
    // 进程没了，"启动中"和右下角那条"实例运行中"都得收起来（runningVersionId() 在
    // LauncherManager 里已经清了，这里只管重画）。
    m_launchInFlight = false;
    updateBottomBar();

    // 「收起启动器，游戏退出后自动回来」那一档：见 applyLauncherBehaviourAfterLaunch()。
    // 只有**为了启动游戏才藏起来**的窗口才会被叫回来 —— 用户自己最小化、自己收起来的
    // 窗口不该因为游戏退出就跳出来。
    if (m_hiddenForGame) {
        m_hiddenForGame = false;
        showNormal();
        raise();
        activateWindow();
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