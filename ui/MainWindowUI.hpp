/**
 * @file MainWindowUI.hpp
 * @brief 声明 UI 构建器类，负责创建主窗口的所有控件和布局。
 * @author 0x7fffff
 * @date 2026-04-11
 * @note 实现界面与业务逻辑的分离，提高代码可维护性。
 *
 * 窗口结构自 2026-09 起是：顶部 QTabWidget（主页 / 实例 / 下载 / 设置）+
 * 底部一条通栏状态条（左：头像与用户名；中：启动/下载进度；右：实例运行状态）。
 * 原先的「日志输出」面板已经去掉 —— 日志走 Qt 的日志系统，在命令行里看。
 *
 * 四个选项卡**都是左右两栏 3:7**（仿 PCL）：左栏是要操作的那点东西（选版本、按钮），
 * 右栏是列表或一整摊设置项。前三个（主页/实例/下载）由 buildTwoColumnPage() 搭出**真控件**，
 * 比例取自 leftColumnStretch()/rightColumnStretch()；「设置」页是一张两列的 QGridLayout，
 * 用同一对份数表达比例（栏不是控件）。界面和验证工程都引这一对份数，不各写一份。
 */

#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QString>
#include <QTabWidget>
#include <QWidget>

class QGridLayout;
class QScrollArea;
class QVBoxLayout;

class MainWindowUI {
public:
    explicit MainWindowUI(QMainWindow *parent);

    // ---------- 控件的 objectName ----------
    // 验证工程靠这些名字认控件。**不要**改回按占位文案或条目文本去认——那些是
    // 会被产品逻辑改写的显示内容（占位文案现在就要随"有没有可选版本"切换），
    // 拿显示内容当身份标识的测试会在产品改动时以看不懂的方式挂掉。
    static QString installedVersionComboName() { return QStringLiteral("installedVersionCombo"); }
    static QString availableVersionComboName() { return QStringLiteral("availableVersionCombo"); }
    static QString installedVersionListName() { return QStringLiteral("installedVersionList"); }
    static QString availableVersionListName() { return QStringLiteral("availableVersionList"); }
    static QString tabWidgetName() { return QStringLiteral("mainTabWidget"); }
    static QString homeTabName() { return QStringLiteral("homeTab"); }
    static QString instanceTabName() { return QStringLiteral("instanceTab"); }
    static QString downloadTabName() { return QStringLiteral("downloadTab"); }
    static QString settingsTabName() { return QStringLiteral("settingsTab"); }
    static QString bottomBarName() { return QStringLiteral("bottomBar"); }
    static QString avatarLabelName() { return QStringLiteral("avatarLabel"); }
    static QString accountLabelName() { return QStringLiteral("accountLabel"); }
    static QString runStateLabelName() { return QStringLiteral("runStateLabel"); }
    static QString runStateDetailLabelName() { return QStringLiteral("runStateDetailLabel"); }
    static QString progressBarName() { return QStringLiteral("progressBar"); }
    static QString statusLabelName() { return QStringLiteral("statusLabel"); }

    // ---------- 左右两栏（主页/实例/下载三页用；设置页是两列 grid） ----------
    // 栏是**真的控件**（不是裸布局）：这样才能按 objectName 找回来量几何、
    // 也才能在验证工程里断言"3:7"这条比例。
    //
    // NOTE: 只有前三个选项卡用 buildTwoColumnPage() 搭出这两个栏控件；「设置」页是
    //       一张两列的 QGridLayout（选项名称在列 0、控件在列 1），栏没有对应的控件，
    //       所以那两列**没有** objectName 可找 —— 设置页的比例靠 settingsForm 上
    //       setColumnStretch() 的两份份数表达，验证工程去量列 0/列 1 里控件的几何。
    /// @param tab "home" / "instance" / "download"。
    static QString leftColumnName(const QString &tab) {
        return tab + QStringLiteral("LeftColumn");
    }
    /// @param tab 同上。
    static QString rightColumnName(const QString &tab) {
        return tab + QStringLiteral("RightColumn");
    }
    /// 左栏占的份数。左:右 = 3:7。
    static int leftColumnStretch() { return 3; }
    /// 右栏占的份数。
    static int rightColumnStretch() { return 7; }
    /// 右栏在页面上是不是真的更宽（验证工程拿它当"3:7 生效了"的判据）。
    static bool rightColumnIsWider() { return rightColumnStretch() > leftColumnStretch(); }

    // ---------- 设置页 ----------
    static QString settingsScrollName() { return QStringLiteral("settingsScroll"); }
    static QString settingsFormName() { return QStringLiteral("settingsForm"); }
    static QString javaVersionComboName() { return QStringLiteral("javaVersionCombo"); }
    static QString javaPathLabelName() { return QStringLiteral("javaPathLabel"); }
    static QString memoryAutoCheckName() { return QStringLiteral("memoryAutoCheck"); }
    /// 「内存分配」那一行下面那句说明。文字随系统内存变（可用内存不是一个定值），
    /// 所以它是唯一一个要 MainWindow 反复改文字的 QLabel，得能按名字取回来。
    static QString memoryAutoHintName() { return QStringLiteral("memoryAutoHint"); }
    static QString memoryMaxSpinName() { return QStringLiteral("memoryMaxSpin"); }
    static QString memoryMinSpinName() { return QStringLiteral("memoryMinSpin"); }
    static QString launcherBehaviourComboName() { return QStringLiteral("launcherBehaviourCombo"); }
    static QString extraJvmArgsEditName() { return QStringLiteral("extraJvmArgsEdit"); }
    static QString applyExtraJvmArgsButtonName() {
        return QStringLiteral("applyExtraJvmArgsButton");
    }
    static QString downloadSourceComboName() { return QStringLiteral("downloadSourceCombo"); }
    static QString versionNameCustomCheckName() {
        return QStringLiteral("versionNameCustomCheck");
    }
    static QString versionNameRealCheckName() { return QStringLiteral("versionNameRealCheck"); }
    static QString versionNameLoaderCheckName() { return QStringLiteral("versionNameLoaderCheck"); }
    static QString versionNameLoaderVersionCheckName() {
        return QStringLiteral("versionNameLoaderVersionCheck");
    }

    // ---------- 内存分配的取值范围 ----------
    // 和 GlobalJavaSettings 的钳位保持一致（那边 setXmx 最少 256、setXms 最少 128），
    // 否则界面允许填的值会被后端悄悄改掉，用户看到"填了不生效"。
    static int memoryMaxMinMb() { return 256; }
    static int memoryMinMinMb() { return 128; }
    static int memoryLimitMaxMb() { return 65536; }
    static int memoryStepMb() { return 256; }

    // ---------- 「已安装版本」下拉框的占位文案 ----------
    // 空状态有两种，由 MainWindow::refreshInstalledVersions() 按当前情形切换：
    //   NoInstalledVersion —— 磁盘上一个装好的版本都没有
    //   ChooseVersion      —— 有可选版本，但用户还没选（含"上次启动的版本已经没了"）
    // 两个串都必须非空：空串会让 QComboBox 在往里灌数据时静默选中第 0 项，
    // 见本文件 .cpp 里的说明。放在这里当唯一出处，免得界面初始值和 MainWindow
    // 刷新时的值各写一份、慢慢就写歪了。
    static QString noInstalledVersionPlaceholder() { return QStringLiteral("暂无已安装版本"); }
    static QString chooseVersionPlaceholder() { return QStringLiteral("请选择一个版本"); }

    // ---------- 底栏右下角"实例运行中"那行的正文 ----------
    // 放在这里当唯一出处：MainWindow 拿它填文字，验证工程拿它做断言，两边不会写歪。
    static QString runStateRunningText() { return QStringLiteral("实例运行中"); }

    // ---------- 选项卡下标 ----------
    // 底栏右侧的显示规则要知道"当前是不是在启动界面（主页）"：在主页且选中的就是
    // 正在跑的那个实例时只显示一行，否则加第二行说明是哪个实例。
    static int homeTabIndex() { return 0; }
    /// 「设置」页的下标。用来做"第一次切到设置页才扫 Java"这条懒加载。
    static int settingsTabIndex() { return 3; }
    /// 当前显示的选项卡是不是主页（即"启动界面"）。
    bool isHomeTabCurrent() const;

    /// 底栏右下角要不要显示第二行（正在跑的那个实例的名字）。
    /// @param homeTabCurrent 当前是不是停在主页（启动界面）。
    /// @param selectedVersionId "已安装版本"下拉框当前选中的版本号（没选是空串）。
    /// @param runningVersionId 正在运行的版本号（没有实例在跑是空串）。
    /// @return 只有"在主页、且选中的就是正在跑的那个实例"这一种情况返回 false。
    ///
    /// 只有这一种情况能省掉名字：用户眼前这个界面说的就是它。其余情况（在别的选项卡
    /// 上，或者下拉框里选着另一个版本）都得靠名字说清楚跑的是谁 —— 尤其是"选中的和
    /// 在跑的不是同一个"这种最容易看错的场面。
    ///
    /// 做成静态纯函数是为了能把这四种组合逐条钉住：它是这条规则的唯一出处，
    /// MainWindow::updateBottomBar() 只负责把结果画出来。
    static bool runStateShowsDetail(bool homeTabCurrent, const QString &selectedVersionId,
                                    const QString &runningVersionId) {
        return !(homeTabCurrent && selectedVersionId == runningVersionId);
    }

    // 控件访问接口
    QPushButton *getStartButton() const { return startButton; }
    QPushButton *getDownloadButton() const { return downloadButton; }
    QPushButton *getLoginButton() const { return loginButton; }
    QCheckBox *getOfflineModeCheckBox() const { return offlineModeCheckBox; }
    QPushButton *getAddFolderButton() const { return addFolderButton; }
    QPushButton *getRemoveFolderButton() const { return removeFolderButton; }
    // 占位按钮：只写一行日志，真正的文件夹管理页面还没做（见 .cpp 里的 TODO）
    QPushButton *getManageFolderButton() const { return manageFolderButton; }
    // 两个下拉框，数据源互不相干，**也因此不在一页上**：
    //   installedVersionCombo —— 磁盘上已经装好的（.minecraft/versions/<id>/<id>.jar），
    //                            在「主页」，启动用它
    //   availableVersionCombo —— 在线清单里可下载的版本，在「下载」页，下载用它
    QComboBox *getInstalledVersionCombo() const { return installedVersionCombo; }
    QComboBox *getAvailableVersionCombo() const { return availableVersionCombo; }
    // 两个列表和两个下拉框是同一份数据的两种看法（条目文本也是同一套显示名），
    // 选中项双向同步：点列表 = 在下拉框里选中它，反之亦然。
    QListWidget *getInstalledVersionList() const { return installedVersionList; }
    QListWidget *getAvailableVersionList() const { return availableVersionList; }

    // 设置页的控件。**只搭出来、只负责显隐**，值由 MainWindow 灌（它才知道
    // 那些设置存在哪、改了要不要落盘）。
    QComboBox *getJavaVersionCombo() const { return javaVersionCombo; }
    QLabel *getJavaPathLabel() const { return javaPathLabel; }
    QCheckBox *getMemoryAutoCheck() const { return memoryAutoCheck; }
    QLabel *getMemoryAutoHint() const { return memoryAutoHint; }
    QSpinBox *getMemoryMaxSpin() const { return memoryMaxSpin; }
    QSpinBox *getMemoryMinSpin() const { return memoryMinSpin; }
    QComboBox *getLauncherBehaviourCombo() const { return launcherBehaviourCombo; }
    QPlainTextEdit *getExtraJvmArgsEdit() const { return extraJvmArgsEdit; }
    QPushButton *getApplyExtraJvmArgsButton() const { return applyExtraJvmArgsButton; }
    QComboBox *getDownloadSourceCombo() const { return downloadSourceCombo; }
    QCheckBox *getVersionNameCustomCheck() const { return versionNameCustomCheck; }
    QCheckBox *getVersionNameRealCheck() const { return versionNameRealCheck; }
    QCheckBox *getVersionNameLoaderCheck() const { return versionNameLoaderCheck; }
    QCheckBox *getVersionNameLoaderVersionCheck() const { return versionNameLoaderVersionCheck; }

    QTabWidget *getTabWidget() const { return tabWidget; }
    // NOTE: 产品代码没有取过底栏本身（只取它里面的几个标签/进度条），只有验证工程
    //       用它来量底栏的几何。
    QFrame *getBottomBar() const { return bottomBar; }
    QLabel *getAvatarLabel() const { return avatarLabel; }
    QLabel *getAccountLabel() const { return accountLabel; }
    // 进度只出现在底栏中间那一格，不进日志（旧版是钉在 QStatusBar 上的）
    QProgressBar *getProgressBar() const { return progressBar; }
    QLabel *getStatusLabel() const { return statusLabel; }
    // 底栏右侧：第一行是状态正文，第二行是实例名（只在需要区分时显示）
    QLabel *getRunStateLabel() const { return runStateLabel; }
    QLabel *getRunStateDetailLabel() const { return runStateDetailLabel; }

private:
    void setupUi(QMainWindow *parent);
    /// 搭设置页：滚动区里一张两列表格，列 0 是选项名称、列 1 是控件（3:7）。
    void setupSettingsPage(QWidget *page);
    /// 搭底部那条通栏状态条（左：头像/用户名，中：进度，右：实例运行状态）。
    void setupBottomBar(QWidget *parent, QVBoxLayout *mainLayout);

    // 控件指针
    QPushButton *startButton;
    QPushButton *downloadButton;
    QPushButton *manageFolderButton;
    QPushButton *loginButton;
    QCheckBox *offlineModeCheckBox;
    QPushButton *addFolderButton;
    QPushButton *removeFolderButton;
    QComboBox *installedVersionCombo;
    QComboBox *availableVersionCombo;
    QListWidget *installedVersionList;
    QListWidget *availableVersionList;
    QComboBox *javaVersionCombo;
    QLabel *javaPathLabel;
    QCheckBox *memoryAutoCheck;
    QLabel *memoryAutoHint;
    QSpinBox *memoryMaxSpin;
    QSpinBox *memoryMinSpin;
    QComboBox *launcherBehaviourCombo;
    QPlainTextEdit *extraJvmArgsEdit;
    QPushButton *applyExtraJvmArgsButton;
    QComboBox *downloadSourceCombo;
    QCheckBox *versionNameCustomCheck;
    QCheckBox *versionNameRealCheck;
    QCheckBox *versionNameLoaderCheck;
    QCheckBox *versionNameLoaderVersionCheck;
    QTabWidget *tabWidget;
    QFrame *bottomBar;
    QLabel *avatarLabel;
    QLabel *accountLabel;
    QProgressBar *progressBar;
    QLabel *statusLabel;
    QLabel *runStateLabel;
    QLabel *runStateDetailLabel;
};
