/**
 * @file MainWindow.hpp
 * @brief 声明主启动器窗口及其UI交互。
 * @author 0x7fffff
 * @date 2026-04-11
 * @note 按钮事件与版本选择在此文件中统一管理。
 */

#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QMainWindow>
#include <QProcess>

class MainWindowUI;
class LauncherManager;
class AuthManager;
class FolderManager;
class VersionManager;
class GlobalJavaSettings;
class LauncherSettings;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onStartButtonClicked();
    void onLoginButtonClicked();
    void onAddFolderClicked();
    void onRemoveFolderClicked();
    void onProcessOutput(const QString &text);
    void onProcessError(const QString &text);
    void onGameFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onDownloadButtonClicked();
    void onVersionsFetched(const QStringList &versions);
    void onVersionDetailsFetched(const QString &versionId, const QJsonObject &versionJson);
    void onVersionDownloadStats(int done, int total, int failed, const QString &currentFile);
    void onVersionDownloadFinished(const QString &versionId, bool success,
                                   const QString &errorMessage);

private:
    void setupConnections();
    /// 把一条消息交给 Qt 的日志系统（命令行里看）。界面里已经没有日志面板了，
    /// 必须让用户看到的消息走的是 QMessageBox，不靠这一条 —— 见 .cpp 里的说明。
    void appendLog(const QString &text);
    /// 刷新底部状态条那三格（头像/用户名、进度、实例运行状态）。
    /// 显示规则见 .cpp；界面那边只负责把控件搭出来。
    void updateBottomBar();
    /// 一个版本号在界面上的显示名（"已安装版本"下拉框里的那条文本）。
    /// 下拉框里没有它时退回版本号本身。
    QString instanceDisplayName(const QString &versionId) const;
    // 重灌"已安装版本"下拉框：扫本地 .minecraft/versions，同步返回。
    // 保住用户原来选中的那一项的逻辑在 fillVersionCombo() 里。
    void refreshInstalledVersions();
    // 启动时把"上次启动过的版本"选回来。**只在构造函数里调一次**——
    // 名单每次刷新（清单到达、下载完成）都调它就会变成"用户手选完又被抢回去"。
    // 记着的那个版本已经不在磁盘上时留空不选，占位文案会说明该怎么选。
    void restoreLastLaunchedVersion();
    // 下载期间禁用"启动游戏""下载版本"和两个版本下拉框，并显示状态栏进度条
    void setDownloadUiBusy(bool busy);
    // 把"启动游戏"按钮的文字刷成两行：第一行固定是"启动游戏"，第二行是"已安装版本"
    // 下拉框当前选中的版本号（没选时是"未选择版本"）。见 .cpp 里的说明。
    void updateStartButtonText();
    void updateDownloadProgressUi(int done, int total, int failed, const QString &currentFile);

    MainWindowUI *m_ui;
    LauncherManager *m_launcherManager;
    AuthManager *m_authManager;
    FolderManager *m_folderManager;
    VersionManager *m_versionManager;
    GlobalJavaSettings *m_javaSettings;
    LauncherSettings *m_settings;
    // 下载是否在进行中。注意不能只问 m_versionManager：从点击"下载"到版本 JSON
    // 拉回来之前，VersionManager 那边还没有任何批次，这段时间也得算"忙"。
    bool m_downloadInFlight = false;
    // "有版本目录但 jar 名对不上"这件事是否已经写过日志。refreshInstalledVersions()
    // 一次会话会被调三次，不记一笔就是同样的告警刷三遍。
    bool m_reportedUnrecognizedVersions = false;
    // 启动进度是否还在走。从"进程真的起来了"（startGame 返回 true）到游戏自己吐出
    // 第一行输出为止 —— 中间那段是 JVM 和游戏自己在初始化，界面上没别的反馈。
    // 命名刻意不叫 m_launching：它表示的是"这段进度要显示"而不是"正在启动"。
    bool m_launchInFlight = false;
    // 上面那段进度说的是哪个版本（中间那格要显示"正在启动 <显示名>"）。
    QString m_launchingVersionId;
    // 进度按网络分片到达（5000 多个 asset），节流一下再刷界面
    QElapsedTimer m_progressThrottle;
};