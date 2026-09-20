/**
 * @file MainWindowUI.cpp
 * @brief 实现 UI 构建器类，实际创建主窗口的控件、布局并设置窗口属性。
 * @author 0x7fffff
 * @date 2026-04-11
 * @note 完成主窗口所有 UI 元素的初始化与排列，供 MainWindow 构造函数调用。
 */

#include "MainWindowUI.hpp"

#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QStatusBar>
#include <QVBoxLayout>

namespace {

/**
 * @brief 挡掉没有焦点时的滚轮事件，防止鼠标划过就把下拉框的选中项改掉。
 *
 * QComboBox 默认把滚轮当成"切换选中项"处理，而滚轮事件是按鼠标位置派发的、
 * 跟焦点无关 —— 鼠标从下拉框上滚过（哪怕焦点在别处）就会静默换一个版本。
 * 这两个下拉框就挨着「下载版本」，手一滑可能把一次 GB 级的下载指到别的版本上。
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

}  // namespace

MainWindowUI::MainWindowUI(QMainWindow *parent) { setupUi(parent); }

void MainWindowUI::setupUi(QMainWindow *parent) {
    parent->setWindowTitle(QStringLiteral("WTF Minecraft Launcher"));
    parent->resize(900, 620);

    parent->setWindowFlags(parent->windowFlags() & ~Qt::WindowMaximizeButtonHint);  // 禁用最大化按钮

    QWidget *centralWidget = new QWidget(parent);
    parent->setCentralWidget(centralWidget);

    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(9, 9, 9, 9);
    mainLayout->setSpacing(6);

    // ---------- 顶部整体水平布局（左右分组，宽度比1:1，各占一半） ----------
    QHBoxLayout *topLevelLayout = new QHBoxLayout();
    topLevelLayout->setSpacing(48);  // 左右两组间距

    // ========== 左侧组：两个按钮水平并排，竖直可拉伸 ==========
    QWidget *leftContainer = new QWidget(centralWidget);
    leftContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    leftContainer->setMinimumHeight(0);

    QHBoxLayout *leftLayout = new QHBoxLayout(leftContainer);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(10);  // 两个按钮之间间距

    startButton = new QPushButton(QStringLiteral("启动游戏"), leftContainer);
    // 占位按钮：暂时不接任何槽。
    // TODO: 接上文件夹管理页面（添加 / 删除 / 切换 .minecraft 目录）。
    manageFolderButton = new QPushButton(QStringLiteral("文件夹管理"), leftContainer);

    QSizePolicy btnPolicy = startButton->sizePolicy();
    btnPolicy.setHorizontalPolicy(QSizePolicy::Expanding);
    btnPolicy.setVerticalPolicy(QSizePolicy::Expanding);
    startButton->setSizePolicy(btnPolicy);
    manageFolderButton->setSizePolicy(btnPolicy);
    startButton->setMinimumHeight(0);
    manageFolderButton->setMinimumHeight(0);

    leftLayout->addWidget(startButton);
    leftLayout->addWidget(manageFolderButton);
    leftLayout->setStretchFactor(startButton, 1);
    leftLayout->setStretchFactor(manageFolderButton, 1);

    // ========== 右侧组：两行两列，高度固定，水平可拉伸 ==========
    QWidget *rightContainer = new QWidget(centralWidget);
    rightContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    QGridLayout *rightLayout = new QGridLayout(rightContainer);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setHorizontalSpacing(48);
    rightLayout->setVerticalSpacing(10);

    loginButton = new QPushButton(QStringLiteral("微软登录"), rightContainer);
    offlineModeCheckBox = new QCheckBox(QStringLiteral("离线模式"), rightContainer);
    addFolderButton = new QPushButton(QStringLiteral("添加文件夹"), rightContainer);
    removeFolderButton = new QPushButton(QStringLiteral("删除文件夹"), rightContainer);

    QSizePolicy rightBtnPolicy = loginButton->sizePolicy();
    rightBtnPolicy.setHorizontalPolicy(QSizePolicy::Expanding);
    rightBtnPolicy.setVerticalPolicy(QSizePolicy::Fixed);
    loginButton->setSizePolicy(rightBtnPolicy);
    addFolderButton->setSizePolicy(rightBtnPolicy);
    removeFolderButton->setSizePolicy(rightBtnPolicy);
    offlineModeCheckBox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // 添加到布局，并设置第二行第一列的复选框水平居中对齐
    rightLayout->addWidget(loginButton, 0, 0);
    rightLayout->addWidget(addFolderButton, 0, 1);
    rightLayout->addWidget(offlineModeCheckBox, 1, 0,
                           Qt::AlignCenter);  // 由于是复选框,所以居中处理
    rightLayout->addWidget(removeFolderButton, 1, 1);

    rightLayout->setColumnStretch(0, 1);
    rightLayout->setColumnStretch(1, 1);
    rightLayout->setRowStretch(0, 0);
    rightLayout->setRowStretch(1, 0);

    // ---------- 组装顶层（左右各占一半） ----------
    topLevelLayout->addWidget(leftContainer);
    topLevelLayout->addWidget(rightContainer);
    topLevelLayout->setStretchFactor(leftContainer, 1);
    topLevelLayout->setStretchFactor(rightContainer, 1);

    mainLayout->addLayout(topLevelLayout);

    // ---------- 版本管理组：两个下拉框，一个只读本地、一个只读在线 ----------
    versionGroupBox = new QGroupBox(QStringLiteral("版本管理"), centralWidget);
    QGridLayout *versionLayout = new QGridLayout(versionGroupBox);
    versionLayout->setContentsMargins(9, 9, 9, 9);
    versionLayout->setHorizontalSpacing(10);
    versionLayout->setVerticalSpacing(10);

    QLabel *installedVersionLabel = new QLabel(QStringLiteral("已安装版本"), versionGroupBox);
    QLabel *availableVersionLabel = new QLabel(QStringLiteral("可安装版本"), versionGroupBox);

    installedVersionCombo = new QComboBox(versionGroupBox);
    availableVersionCombo = new QComboBox(versionGroupBox);

    // objectName 是为了让验证工程能稳定地认出这两个控件。以前 mw_check 是按占位文案
    // 「暂无已安装版本」去找的，而那个文案正好要改成随上下文变 —— 测试就会以一个
    // 完全看不懂的方式整体中止（installed == nullptr，后面 22 条断言全不跑）。
    installedVersionCombo->setObjectName(installedVersionComboName());
    availableVersionCombo->setObjectName(availableVersionComboName());

    // 空状态用"没选中任何项(currentIndex() == -1) + placeholderText"表示，
    // 不再往列表里塞 "自定义版本" 那种假条目：控件自己会把占位文案画成灰字。
    // NOTE: 只要占位文案非空，往空下拉框里 addItems() 就不会自动选中第 0 项
    // （QComboBoxPrivate::rowsInserted 里那条 setCurrentIndex(0) 带了
    //  !placeholderText.isEmpty() 的条件）。所以"空着"能一直保持到用户真的挑一项，
    // 代价是刷新之后必须自己把选中项捡回来，见 MainWindow::fillVersionCombo()。
    // 这里只是初始值；灌完数据后由 MainWindow 在两个串之间切换。
    installedVersionCombo->setPlaceholderText(noInstalledVersionPlaceholder());
    availableVersionCombo->setPlaceholderText(QStringLiteral("正在获取在线版本列表..."));

    // 版本号长度不定（26.2-snapshot-1 之类），默认的 AdjustToContentsOnFirstShow 会在
    // 第一次显示（那时还是空的）时定一个宽度然后一直沿用，之后灌进上千个长版本号就容易
    // 把窗口顶宽。改成按"最短内容长度"算，宽度确定，灌数据时也不会跳。
    installedVersionCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    installedVersionCombo->setMinimumContentsLength(16);
    availableVersionCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    availableVersionCombo->setMinimumContentsLength(16);

    // 滚轮防护：见本文件顶部的 WheelGuard
    installedVersionCombo->installEventFilter(new WheelGuard(installedVersionCombo));
    availableVersionCombo->installEventFilter(new WheelGuard(availableVersionCombo));

    // 「下载版本」从顶行搬到这里，只占第 1 行、贴在可安装下拉框右边。
    // Fixed 策略：它只吃自己的宽度（配合 setMinimumWidth 取 96），剩下的全给下拉框。
    downloadButton = new QPushButton(QStringLiteral("下载版本"), versionGroupBox);
    downloadButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    downloadButton->setMinimumWidth(96);

    //        列0 = 标签（按自身宽度）  列1 = 下拉框（吃满剩余宽度）  列2 = 下载按钮
    versionLayout->addWidget(installedVersionLabel, 0, 0);
    versionLayout->addWidget(installedVersionCombo, 0, 1);
    versionLayout->addWidget(availableVersionLabel, 1, 0);
    versionLayout->addWidget(availableVersionCombo, 1, 1);
    versionLayout->addWidget(downloadButton, 1, 2);

    // 两个下拉框同处列 1：宽度和左右边界天然一致，按钮只落在第 1 行的列 2 上
    versionLayout->setColumnStretch(0, 0);
    versionLayout->setColumnStretch(1, 1);
    versionLayout->setColumnStretch(2, 0);
    // 下拉框都是固定高度，多出来的竖直空间全丢给第 2 行（空行），两行贴顶。
    // QGridLayout 没有 addStretch()，setRowStretch() 到不存在的行是官方写法，
    // 内部 expand() 会把行数撑到 r+1。
    versionLayout->setRowStretch(2, 1);

    mainLayout->addWidget(versionGroupBox);

    // ---------- 日志输出组 ----------
    logGroupBox = new QGroupBox(QStringLiteral("日志输出"), centralWidget);
    QVBoxLayout *logLayout = new QVBoxLayout(logGroupBox);
    logLayout->setContentsMargins(9, 9, 9, 9);
    logTextEdit = new QTextEdit(logGroupBox);
    logTextEdit->setReadOnly(true);
    logLayout->addWidget(logTextEdit);
    mainLayout->addWidget(logGroupBox);

    // 垂直拉伸分配(1:2:6)：按钮行 : 版本区 : 日志区。
    // 原来版本区是 3（大列表要占地方），换成两个下拉框后只要两行，
    // 腾出来的高度全给日志区。
    mainLayout->setStretchFactor(topLevelLayout, 1);
    mainLayout->setStretchFactor(versionGroupBox, 2);
    mainLayout->setStretchFactor(logGroupBox, 6);

    parent->setMenuBar(new QMenuBar(parent));

    // ---------- 状态栏：下载进度就放这里，不往日志里写 ----------
    // NOTE: setStatusBar() 必须在 addPermanentWidget() 之前调用。后设的 QStatusBar
    // 会把父窗口上原有的状态栏整个删掉，先挂上去的进度条会跟着一起消失。
    parent->setStatusBar(new QStatusBar(parent));
    QStatusBar *statusBar = parent->statusBar();

    statusLabel = new QLabel(statusBar);
    // 固定一个最小宽度：文件名长短变化时状态栏不会再跳来跳去
    statusLabel->setMinimumWidth(260);
    statusLabel->hide();

    progressBar = new QProgressBar(statusBar);
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setFixedWidth(220);
    progressBar->setTextVisible(true);
    progressBar->hide();

    statusBar->addPermanentWidget(statusLabel);
    statusBar->addPermanentWidget(progressBar);
}