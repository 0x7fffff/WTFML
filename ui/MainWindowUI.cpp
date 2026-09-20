/**
 * @file MainWindowUI.cpp
 * @brief 实现 UI 构建器类，实际创建主窗口的控件、布局并设置窗口属性。
 * @author 0x7fffff
 * @date 2026-04-11
 * @note 完成主窗口所有 UI 元素的初始化与排列，供 MainWindow 构造函数调用。
 */

#include "MainWindowUI.hpp"

#include <QHBoxLayout>
#include <QMenuBar>
#include <QStatusBar>
#include <QVBoxLayout>

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
    downloadButton = new QPushButton(QStringLiteral("下载版本"), leftContainer);

    QSizePolicy btnPolicy = startButton->sizePolicy();
    btnPolicy.setHorizontalPolicy(QSizePolicy::Expanding);
    btnPolicy.setVerticalPolicy(QSizePolicy::Expanding);
    startButton->setSizePolicy(btnPolicy);
    downloadButton->setSizePolicy(btnPolicy);
    startButton->setMinimumHeight(0);
    downloadButton->setMinimumHeight(0);

    leftLayout->addWidget(startButton);
    leftLayout->addWidget(downloadButton);
    leftLayout->setStretchFactor(startButton, 1);
    leftLayout->setStretchFactor(downloadButton, 1);

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

    // ---------- 版本管理组 ----------
    versionGroupBox = new QGroupBox(QStringLiteral("版本管理"), centralWidget);
    QVBoxLayout *versionLayout = new QVBoxLayout(versionGroupBox);
    versionLayout->setContentsMargins(9, 9, 9, 9);
    versionList = new QListWidget(versionGroupBox);
    versionLayout->addWidget(versionList);
    mainLayout->addWidget(versionGroupBox);

    // ---------- 日志输出组 ----------
    logGroupBox = new QGroupBox(QStringLiteral("日志输出"), centralWidget);
    QVBoxLayout *logLayout = new QVBoxLayout(logGroupBox);
    logLayout->setContentsMargins(9, 9, 9, 9);
    logTextEdit = new QTextEdit(logGroupBox);
    logTextEdit->setReadOnly(true);
    logLayout->addWidget(logTextEdit);
    mainLayout->addWidget(logGroupBox);

    // 垂直拉伸分配(1:3:4)
    mainLayout->setStretchFactor(topLevelLayout, 1);
    mainLayout->setStretchFactor(versionGroupBox, 3);
    mainLayout->setStretchFactor(logGroupBox, 4);

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