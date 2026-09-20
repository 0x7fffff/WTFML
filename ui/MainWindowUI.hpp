/**
 * @file MainWindowUI.hpp
 * @brief 声明 UI 构建器类，负责创建主窗口的所有控件和布局。
 * @author 0x7fffff
 * @date 2026-04-11
 * @note 实现界面与业务逻辑的分离，提高代码可维护性。
 */

#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QMainWindow>
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>

class MainWindowUI {
public:
    explicit MainWindowUI(QMainWindow *parent);

    // ---------- 控件的 objectName ----------
    // 验证工程靠这两个名字认控件。**不要**改回按占位文案或条目文本去认——那些是
    // 会被产品逻辑改写的显示内容（占位文案现在就要随"有没有可选版本"切换），
    // 拿显示内容当身份标识的测试会在产品改动时以看不懂的方式挂掉。
    static QString installedVersionComboName() { return QStringLiteral("installedVersionCombo"); }
    static QString availableVersionComboName() { return QStringLiteral("availableVersionCombo"); }

    // ---------- 「已安装版本」下拉框的占位文案 ----------
    // 空状态有两种，由 MainWindow::refreshInstalledVersions() 按当前情形切换：
    //   NoInstalledVersion —— 磁盘上一个装好的版本都没有
    //   ChooseVersion      —— 有可选版本，但用户还没选（含"上次启动的版本已经没了"）
    // 两个串都必须非空：空串会让 QComboBox 在往里灌数据时静默选中第 0 项，
    // 见本文件 .cpp 里的说明。放在这里当唯一出处，免得界面初始值和 MainWindow
    // 刷新时的值各写一份、慢慢就写歪了。
    static QString noInstalledVersionPlaceholder() { return QStringLiteral("暂无已安装版本"); }
    static QString chooseVersionPlaceholder() { return QStringLiteral("请选择一个版本"); }

    // 控件访问接口
    QPushButton *getStartButton() const { return startButton; }
    QPushButton *getDownloadButton() const { return downloadButton; }
    QPushButton *getLoginButton() const { return loginButton; }
    QCheckBox *getOfflineModeCheckBox() const { return offlineModeCheckBox; }
    QPushButton *getAddFolderButton() const { return addFolderButton; }
    QPushButton *getRemoveFolderButton() const { return removeFolderButton; }
    // 占位按钮：暂时不接任何槽，只占住「下载版本」搬进版本区之后空出来的位置
    QPushButton *getManageFolderButton() const { return manageFolderButton; }
    // 版本区的两个下拉框，数据源互不相干：
    //   installedVersionCombo —— 磁盘上已经装好的（.minecraft/versions/<id>/<id>.jar）
    //   availableVersionCombo —— 在线清单里可下载的版本
    QComboBox *getInstalledVersionCombo() const { return installedVersionCombo; }
    QComboBox *getAvailableVersionCombo() const { return availableVersionCombo; }
    QTextEdit *getLogTextEdit() const { return logTextEdit; }
    // 下载进度只出现在状态栏，不进日志
    QProgressBar *getProgressBar() const { return progressBar; }
    QLabel *getStatusLabel() const { return statusLabel; }

private:
    void setupUi(QMainWindow *parent);

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
    QTextEdit *logTextEdit;
    QGroupBox *versionGroupBox;
    QGroupBox *logGroupBox;
    QProgressBar *progressBar;
    QLabel *statusLabel;
};