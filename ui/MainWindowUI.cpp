/**
 * @file MainWindowUI.cpp
 * @brief 实现 UI 构建器类，实际创建主窗口的控件、布局并设置窗口属性。
 * @author 0x7fffff
 * @date 2026-04-11
 * @note 完成主窗口所有 UI 元素的初始化与排列，供 MainWindow 构造函数调用。
 */

#include "MainWindowUI.hpp"

#include <QColor>
#include <QEvent>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QPalette>
#include <QVBoxLayout>

namespace {

/**
 * @brief 挡掉没有焦点时的滚轮事件，防止鼠标划过就把下拉框的选中项改掉。
 *
 * QComboBox 默认把滚轮当成"切换选中项"处理，而滚轮事件是按鼠标位置派发的、
 * 跟焦点无关 —— 鼠标从下拉框上滚过（哪怕焦点在别处）就会静默换一个版本。
 * 「下载」页那个下拉框旁边就是「下载版本」，手一滑可能把一次 GB 级的下载指到
 * 别的版本上。
 *
 * NOTE: QObject::eventFilter 是虚函数、不是槽，所以这里不需要 Q_OBJECT，
 *       AUTOMOC 不会牵扯进来，也不用在 cpp 末尾补 .moc 包含。
 */
class WheelGuard : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        // hasFocus() 是 QWidget 的，eventFilter 交回来的却是 QObject*，所以得转一手。
        // 转不出来就原样放行（这个过滤器只挂在 QComboBox 上，正常不会走到）。
        QWidget *widget = qobject_cast<QWidget *>(watched);
        if (widget != nullptr && event->type() == QEvent::Wheel && !widget->hasFocus()) {
            return true;  // 吃掉：没有焦点就不认滚轮
        }
        return QObject::eventFilter(watched, event);
    }
};

/// 每个选项卡页面共用的边距：贴着选项卡内容区留一圈呼吸空间。
constexpr int kPageMargin = 9;

}  // namespace

MainWindowUI::MainWindowUI(QMainWindow *parent) { setupUi(parent); }

bool MainWindowUI::isHomeTabCurrent() const {
    // 主页恒定是第一页（下面 addTab 的顺序就是契约），所以比下标就够，
    // 不用再存一份页面指针。
    return tabWidget != nullptr && tabWidget->currentIndex() == homeTabIndex();
}

void MainWindowUI::setupUi(QMainWindow *parent) {
    parent->setWindowTitle(QStringLiteral("WTF Minecraft Launcher"));
    parent->resize(900, 620);

    parent->setWindowFlags(parent->windowFlags() & ~Qt::WindowMaximizeButtonHint);  // 禁用最大化按钮

    QWidget *centralWidget = new QWidget(parent);
    parent->setCentralWidget(centralWidget);

    // NOTE: 边距和间距都是 0 —— 选项卡栏和底部状态条要**通栏**贴到窗口边缘
    //       （Steam 那条底栏就是通栏的）。页面内部自己留边距，见 kPageMargin。
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ================= 选项卡：主页 / 实例 / 下载 / 设置 =================
    // 四页各自只放自己那摊控件。注意两个版本下拉框被**分到了两页**：
    // 「已安装版本」在主页（启动用），「可安装版本」在下载页（下载用）。它们本来就
    // 是两个互不相干的数据源，以前并排放着只是因为都叫"版本"。
    tabWidget = new QTabWidget(centralWidget);
    tabWidget->setObjectName(tabWidgetName());

    // ---------- 主页：挑一个已装好的版本，启动 ----------
    QWidget *homeTab = new QWidget(tabWidget);
    homeTab->setObjectName(homeTabName());

    QVBoxLayout *homeLayout = new QVBoxLayout(homeTab);
    homeLayout->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);
    homeLayout->setSpacing(12);

    QHBoxLayout *installedRow = new QHBoxLayout();
    installedRow->setSpacing(10);
    QLabel *installedVersionLabel = new QLabel(QStringLiteral("已安装版本"), homeTab);

    installedVersionCombo = new QComboBox(homeTab);

    // 空状态用"没选中任何项(currentIndex() == -1) + placeholderText"表示，
    // 不再往列表里塞 "自定义版本" 那种假条目：控件自己会把占位文案画成灰字。
    // NOTE: 只要占位文案非空，往空下拉框里 addItems() 就不会自动选中第 0 项
    // （QComboBoxPrivate::rowsInserted 里那条 setCurrentIndex(0) 带了
    //  !placeholderText.isEmpty() 的条件）。所以"空着"能一直保持到用户真的挑一项，
    // 代价是刷新之后必须自己把选中项捡回来，见 MainWindow::fillVersionCombo()。
    // 这里只是初始值；灌完数据后由 MainWindow 在两个串之间切换。
    installedVersionCombo->setPlaceholderText(noInstalledVersionPlaceholder());

    // 版本号长度不定（26.2-snapshot-1 之类），默认的 AdjustToContentsOnFirstShow 会在
    // 第一次显示（那时还是空的）时定一个宽度然后一直沿用，之后灌进上千个长版本号就容易
    // 把窗口顶宽。改成按"最短内容长度"算，宽度确定，灌数据时也不会跳。
    installedVersionCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    installedVersionCombo->setMinimumContentsLength(16);
    // 滚轮防护：见本文件顶部的 WheelGuard
    installedVersionCombo->installEventFilter(new WheelGuard(installedVersionCombo));

    installedRow->addWidget(installedVersionLabel);
    installedRow->addWidget(installedVersionCombo, 1);
    homeLayout->addLayout(installedRow);

    startButton = new QPushButton(QStringLiteral("启动游戏"), homeTab);
    // 文字是两行（"启动游戏\n<版本号>"，由 MainWindow::updateStartButtonText() 维护），
    // 高度固定给足，免得两行被压成一行半。
    startButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    startButton->setMinimumHeight(88);
    homeLayout->addWidget(startButton);

    offlineModeCheckBox = new QCheckBox(QStringLiteral("离线模式"), homeTab);
    offlineModeCheckBox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    homeLayout->addWidget(offlineModeCheckBox, 0, Qt::AlignLeft);

    // 剩下的竖直空间留在底部：按钮不跟着窗口高度一起被拉长
    homeLayout->addStretch(1);

    // ---------- 实例：游戏文件夹与实例管理 ----------
    QWidget *instanceTab = new QWidget(tabWidget);
    instanceTab->setObjectName(instanceTabName());

    QVBoxLayout *instanceLayout = new QVBoxLayout(instanceTab);
    instanceLayout->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);
    instanceLayout->setSpacing(12);

    QGroupBox *folderGroupBox = new QGroupBox(QStringLiteral("游戏文件夹"), instanceTab);
    QVBoxLayout *folderLayout = new QVBoxLayout(folderGroupBox);
    folderLayout->setContentsMargins(9, 9, 9, 9);
    folderLayout->setSpacing(10);

    QHBoxLayout *folderButtonRow = new QHBoxLayout();
    folderButtonRow->setSpacing(10);
    addFolderButton = new QPushButton(QStringLiteral("添加文件夹"), folderGroupBox);
    removeFolderButton = new QPushButton(QStringLiteral("删除文件夹"), folderGroupBox);
    // 占位按钮：只写一行日志。
    // TODO: 换成真正的文件夹管理页面（列出已添加的目录、点一下切换）。
    manageFolderButton = new QPushButton(QStringLiteral("文件夹管理"), folderGroupBox);
    folderButtonRow->addWidget(addFolderButton);
    folderButtonRow->addWidget(removeFolderButton);
    folderButtonRow->addWidget(manageFolderButton);
    folderLayout->addLayout(folderButtonRow);

    QLabel *folderHint = new QLabel(
        QStringLiteral("添加文件夹可以指向别的 Minecraft 根目录（例如另一个启动器的实例目录）。"),
        folderGroupBox);
    folderHint->setWordWrap(true);
    folderLayout->addWidget(folderHint);
    instanceLayout->addWidget(folderGroupBox);

    // TODO: 实例列表（重命名 / 分组 / 每实例独立设置）落在这里。真机上实例数可以到
    //       61 个，PCL 用 5 张卡片分组（PCL.ini 的 CardKey/CardValue）。
    QLabel *instanceTodo = new QLabel(
        QStringLiteral("实例列表、重命名与分组尚未实现；当前版本的启动与下载分别在"
                       "「主页」和「下载」页。"),
        instanceTab);
    instanceTodo->setWordWrap(true);
    instanceLayout->addWidget(instanceTodo);
    instanceLayout->addStretch(1);

    // ---------- 下载：从在线清单挑一个版本下载 ----------
    QWidget *downloadTab = new QWidget(tabWidget);
    downloadTab->setObjectName(downloadTabName());

    QVBoxLayout *downloadPageLayout = new QVBoxLayout(downloadTab);
    downloadPageLayout->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);
    downloadPageLayout->setSpacing(12);

    QGroupBox *versionGroupBox = new QGroupBox(QStringLiteral("版本下载"), downloadTab);
    QGridLayout *versionLayout = new QGridLayout(versionGroupBox);
    versionLayout->setContentsMargins(9, 9, 9, 9);
    versionLayout->setHorizontalSpacing(10);
    versionLayout->setVerticalSpacing(10);

    QLabel *availableVersionLabel = new QLabel(QStringLiteral("可安装版本"), versionGroupBox);
    availableVersionCombo = new QComboBox(versionGroupBox);

    availableVersionCombo->setObjectName(availableVersionComboName());
    installedVersionCombo->setObjectName(installedVersionComboName());
    availableVersionCombo->setPlaceholderText(QStringLiteral("正在获取在线版本列表..."));
    availableVersionCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    availableVersionCombo->setMinimumContentsLength(16);
    availableVersionCombo->installEventFilter(new WheelGuard(availableVersionCombo));

    // 「下载版本」只占一行、贴在可安装下拉框右边。Fixed 策略：它只吃自己的宽度
    // （配合 setMinimumWidth 取 96），剩下的全给下拉框。
    downloadButton = new QPushButton(QStringLiteral("下载版本"), versionGroupBox);
    downloadButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    downloadButton->setMinimumWidth(96);

    //        列0 = 标签（按自身宽度）  列1 = 下拉框（吃满剩余宽度）  列2 = 下载按钮
    versionLayout->addWidget(availableVersionLabel, 0, 0);
    versionLayout->addWidget(availableVersionCombo, 0, 1);
    versionLayout->addWidget(downloadButton, 0, 2);
    versionLayout->setColumnStretch(0, 0);
    versionLayout->setColumnStretch(1, 1);
    versionLayout->setColumnStretch(2, 0);
    // 下拉框是固定高度，多出来的竖直空间全丢给第 1 行（空行），这一行贴顶。
    // QGridLayout 没有 addStretch()，setRowStretch() 到不存在的行是官方写法，
    // 内部 expand() 会把行数撑到 r+1。
    versionLayout->setRowStretch(1, 1);
    downloadPageLayout->addWidget(versionGroupBox);

    QLabel *downloadHint = new QLabel(
        QStringLiteral("下载进度显示在窗口底部那条状态栏上。已经下过的版本会改成"
                       "「校验并补齐」，不会从头重下。"),
        downloadTab);
    downloadHint->setWordWrap(true);
    downloadPageLayout->addWidget(downloadHint);
    downloadPageLayout->addStretch(1);

    // ---------- 设置：账户与 Java ----------
    QWidget *settingsTab = new QWidget(tabWidget);
    settingsTab->setObjectName(settingsTabName());

    QVBoxLayout *settingsLayout = new QVBoxLayout(settingsTab);
    settingsLayout->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);
    settingsLayout->setSpacing(12);

    QGroupBox *accountGroupBox = new QGroupBox(QStringLiteral("账户"), settingsTab);
    QVBoxLayout *accountLayout = new QVBoxLayout(accountGroupBox);
    accountLayout->setContentsMargins(9, 9, 9, 9);
    accountLayout->setSpacing(10);
    loginButton = new QPushButton(QStringLiteral("微软登录"), accountGroupBox);
    loginButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    loginButton->setMinimumWidth(120);
    QLabel *accountHint = new QLabel(
        QStringLiteral("登录后当前账户显示在窗口左下角。不登录也可以用「离线模式」启动。"),
        accountGroupBox);
    accountHint->setWordWrap(true);
    accountLayout->addWidget(loginButton, 0, Qt::AlignLeft);
    accountLayout->addWidget(accountHint);
    settingsLayout->addWidget(accountGroupBox);

    QGroupBox *javaGroupBox = new QGroupBox(QStringLiteral("Java 设置"), settingsTab);
    QVBoxLayout *javaLayout = new QVBoxLayout(javaGroupBox);
    javaLayout->setContentsMargins(9, 9, 9, 9);
    // TODO: Java 路径 / 最大最小堆 / 全局额外启动参数的界面入口落在这里
    //       （数据层 GlobalJavaSettings 早就有了，缺的只是这几个输入框）。
    QLabel *javaTodo = new QLabel(
        QStringLiteral("Java 路径、内存上限与全局额外启动参数目前只能改配置文件，"
                       "界面入口尚未实现。"),
        javaGroupBox);
    javaTodo->setWordWrap(true);
    javaLayout->addWidget(javaTodo);
    settingsLayout->addWidget(javaGroupBox);
    settingsLayout->addStretch(1);

    // NOTE: 顺序就是契约：homeTabIndex() == 0 靠的就是"主页第一个 addTab"。
    tabWidget->addTab(homeTab, QStringLiteral("主页"));
    tabWidget->addTab(instanceTab, QStringLiteral("实例"));
    tabWidget->addTab(downloadTab, QStringLiteral("下载"));
    tabWidget->addTab(settingsTab, QStringLiteral("设置"));
    mainLayout->addWidget(tabWidget, 1);

    setupBottomBar(centralWidget, mainLayout);

    // 空的菜单栏：窗口上那条菜单现在什么都没挂。
    // TODO: 设置类入口（Java 设置 / 打开 .minecraft 目录 / 关于）挂这里。
    parent->setMenuBar(new QMenuBar(parent));
}

void MainWindowUI::setupBottomBar(QWidget *parent, QVBoxLayout *mainLayout) {
    // ---------- 底部通栏状态条（Steam 风格） ----------
    // 三个格子：左 = 头像与用户名，中 = 启动/下载进度，右 = 实例运行状态。
    // 这里只**搭**它：显示什么由 MainWindow::updateBottomBar() 决定，
    // 界面这边不持有任何业务状态。
    bottomBar = new QFrame(parent);
    bottomBar->setObjectName(bottomBarName());
    bottomBar->setFrameShape(QFrame::NoFrame);
    bottomBar->setFixedHeight(56);
    // 底色比窗口底色深一档把它从内容区里分出来。**不写死颜色**：从当前调色板推出来，
    // 浅色主题和深色主题下都成立（darker() 在两种主题上的效果都是"更沉一点"）。
    bottomBar->setAutoFillBackground(true);
    QPalette barPalette = bottomBar->palette();
    barPalette.setColor(QPalette::Window, barPalette.color(QPalette::Window).darker(105));
    bottomBar->setPalette(barPalette);

    QHBoxLayout *barLayout = new QHBoxLayout(bottomBar);
    barLayout->setContentsMargins(12, 6, 12, 6);
    barLayout->setSpacing(12);

    // ---------- 左：头像 + 用户名 ----------
    // 真正的手绘皮肤头像要等 Minecraft profile API（登录流程现在只存了令牌，没取
    // 玩家档案），这里先画一块"首字母圆牌"。颜色同样取自调色板，不写死。
    avatarLabel = new QLabel(QStringLiteral("?"), bottomBar);
    avatarLabel->setObjectName(avatarLabelName());
    avatarLabel->setFixedSize(32, 32);
    avatarLabel->setAlignment(Qt::AlignCenter);
    const QColor avatarBackground = bottomBar->palette().color(QPalette::Highlight);
    const QColor avatarForeground = bottomBar->palette().color(QPalette::HighlightedText);
    avatarLabel->setStyleSheet(
        QStringLiteral("QLabel { background-color: %1; color: %2; border-radius: 16px; }")
            .arg(avatarBackground.name(), avatarForeground.name()));

    accountLabel = new QLabel(QStringLiteral("?"), bottomBar);
    accountLabel->setObjectName(accountLabelName());

    QHBoxLayout *accountRow = new QHBoxLayout();
    accountRow->setSpacing(8);
    accountRow->addWidget(avatarLabel);
    accountRow->addWidget(accountLabel);
    barLayout->addLayout(accountRow, 0);

    // ---------- 中：进度（启动优先于下载） ----------
    statusLabel = new QLabel(bottomBar);
    statusLabel->setObjectName(statusLabelName());
    // 固定一个最小宽度：文件名长短变化时这一格不会再跳来跳去
    statusLabel->setMinimumWidth(260);
    progressBar = new QProgressBar(bottomBar);
    progressBar->setObjectName(progressBarName());
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setTextVisible(true);

    // 文字在上、进度条在下：底栏只有 56 px 高，横着并排会把进度条挤得太短
    QVBoxLayout *progressLayout = new QVBoxLayout();
    progressLayout->setSpacing(2);
    progressLayout->addWidget(statusLabel);
    progressLayout->addWidget(progressBar);
    // 拉伸因子 1：左右两格按自身宽度，中间这一格吃掉所有富余宽度
    barLayout->addLayout(progressLayout, 1);
    // 一开始什么都没在下：整格藏起来（"没在下载就看不见进度条"）
    statusLabel->hide();
    progressBar->hide();

    // ---------- 右：当前实例运行状态 ----------
    // 第一行是状态正文，第二行是实例名。**在主页且选中的就是正在跑的那个实例**时
    // 只显示第一行 —— 那种情况下用户眼前就是那个实例，再说一遍名字是废话。
    runStateLabel = new QLabel(bottomBar);
    runStateLabel->setObjectName(runStateLabelName());
    runStateLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    runStateDetailLabel = new QLabel(bottomBar);
    runStateDetailLabel->setObjectName(runStateDetailLabelName());
    runStateDetailLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    // 第二行小一号、灰一点，跟第一行区分开。字号按当前字号减 1（不用点值，
    // 免得不支持点值的平台出现负字号）；颜色用调色板里的 PlaceholderText。
    QFont detailFont = runStateDetailLabel->font();
    detailFont.setPointSize(qMax(1, detailFont.pointSize() - 1));
    runStateDetailLabel->setFont(detailFont);
    QPalette detailPalette = runStateDetailLabel->palette();
    detailPalette.setColor(QPalette::WindowText, detailPalette.color(QPalette::PlaceholderText));
    runStateDetailLabel->setPalette(detailPalette);

    QVBoxLayout *runStateLayout = new QVBoxLayout();
    runStateLayout->setSpacing(0);
    runStateLayout->addWidget(runStateLabel);
    runStateLayout->addWidget(runStateDetailLabel);
    barLayout->addLayout(runStateLayout, 0);
    // 没有实例在跑时右下角什么都不显示（跟 Steam 一样，不占着一句"未运行"）
    runStateLabel->hide();
    runStateDetailLabel->hide();

    mainLayout->addWidget(bottomBar);
}
