/**
 * @file MainWindowUI.hpp
 * @brief 声明 UI 构建器类，负责创建主窗口的所有控件和布局。
 * @author 0x7fffff
 * @date 2026-04-11
 * @note 实现界面与业务逻辑的分离，提高代码可维护性。
 */

#pragma once

#include <QCheckBox>
#include <QGroupBox>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QTextEdit>

class MainWindowUI {
public:
    explicit MainWindowUI(QMainWindow *parent);

    // 控件访问接口
    QPushButton *getStartButton() const { return startButton; }
    QPushButton *getDownloadButton() const { return downloadButton; }
    QPushButton *getLoginButton() const { return loginButton; }
    QCheckBox *getOfflineModeCheckBox() const { return offlineModeCheckBox; }
    QPushButton *getAddFolderButton() const { return addFolderButton; }
    QPushButton *getRemoveFolderButton() const { return removeFolderButton; }
    QListWidget *getVersionList() const { return versionList; }
    QTextEdit *getLogTextEdit() const { return logTextEdit; }

private:
    void setupUi(QMainWindow *parent);

    // 控件指针
    QPushButton *startButton;
    QPushButton *downloadButton;
    QPushButton *loginButton;
    QCheckBox *offlineModeCheckBox;
    QPushButton *addFolderButton;
    QPushButton *removeFolderButton;
    QListWidget *versionList;
    QTextEdit *logTextEdit;
    QGroupBox *versionGroupBox;
    QGroupBox *logGroupBox;
};