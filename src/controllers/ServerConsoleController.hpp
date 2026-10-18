/**
 * @file ServerConsoleController.hpp
 * @brief 服务端控制台那一页（coding plan §6.2 / M4-3）：页对象 +「这一页现在说的是谁」。
 * @author Agent
 * @date 2026-10
 *
 * ★ 2026-10-15 从 MainWindow 搬出来的（「接着拆，拆点小的功能」）。搬走的是**功能**而不是
 *   几个辅助函数：这一页的**状态**（页对象 + 现在说的是哪一个服务端）和**决定**（状态那一行
 *   说什么、那三颗按钮此刻能不能按、往哪个进程送命令）都在这一层了。
 *
 * 这一页的三件事，各有一条自己的规矩：
 *
 *   ① **认人认 `m_serverId`，不认"列表上现在选中谁"** —— 页面是懒建一次、之后复用的，
 *      关页之后它还活在内存里；用户完全可能边看控制台边点别处（和 `m_updateCenterInstanceId`
 *      一个路数）。换一个服务端开控制台时这一格跟着换，关页时清掉。
 *   ② **开页要说清 `ownerTab`、而且要先把选项卡切过去**（见 `OverlayHost::execPage()` 那段）：
 *      这一页是从服务端列表上开出来的，那一位就是「实例」这一颗。不切的话，收页之后用户
 *      会落在跟刚才没关系的选项卡上。
 *   ③ **开页时把攒下来的行灌一遍**：这一页关着的时候服务端一直在跑（`ServerProcess` 自己
 *      攒着 `logLines()`），重新打开不该只剩"从打开这一刻起"的日志。
 */
#pragma once

#include <QObject>
#include <QString>

#include <functional>

class MainWindowUI;
class ServerConsolePage;
class ServerWorkspaceController;
class QWidget;

class ServerConsoleController final : public QObject {
    Q_OBJECT

public:
    /// 这一层做不了、得回头问主窗口的三件事（都是"别处那一段"的入口）。
    struct Host {
        /// 往启动器日志里写一句（主窗口那句 `appendLog`）。
        std::function<void(const QString &)> log;
        /// 让它停下来（`MainWindow::stopServer`）—— 页上那颗「停止」。
        std::function<void(const QString &)> stop;
        /// 强制结束（`MainWindow::forceKillServer`）—— 页上那颗「强制结束」。
        std::function<void(const QString &)> forceKill;
    };

    ServerConsoleController(ServerWorkspaceController *workspace, MainWindowUI *ui,
                            QWidget *pageParent, Host host, QObject *parent = nullptr);

    /// 开这一页（覆盖层整页）。已经有覆盖层开着 = 什么都不做（见 .cpp 里那段）。
    void open(const QString &serverId);
    /// 把控制台那一页现在的状态 / 三颗按钮该不该按摆一遍。**没开着那一页就什么都不做**
    /// （状态变了、来了一行日志、宽限到点，都走它）——**状态那几行的唯一出处**。
    void refresh();
    /// @p serverId 吐了一行：正开着它那一页就追加这一行，否则什么都不做。
    void appendLine(const QString &serverId, const QString &line);
    /// 「现在可以强杀了」那一句（宽限到点）：正开着它那一页就插一行说明 + 重摆状态。
    /// @param seconds 宽限期（秒）—— 由调用方算好（它自己那句日志也要用同一个数）。
    void announceForceKill(const QString &serverId, int seconds);
    /// 这个服务端没了（被删掉）：这一页不再说它（行都没了，它再收到日志就是对着空气说）。
    void forget(const QString &serverId);

    /// 这一页此刻说的就是 @p serverId 吗。
    bool showing(const QString &serverId) const { return !m_serverId.isEmpty() && m_serverId == serverId; }

private:
    /// 「导出日志…」：把控制台**整份**输出写到用户挑的那个文件（§9.8 的导出接口）。
    void exportLog();

private:
    ServerWorkspaceController *m_workspace = nullptr;
    MainWindowUI *m_ui = nullptr;
    QWidget *m_pageParent = nullptr;
    Host m_host;
    /// 控制台那一页（懒建一次、之后复用，和「新建服务端」同一个路数）。
    ServerConsolePage *m_page = nullptr;
    /// 控制台那一页**现在说的是哪一个**服务端（见文件头 ①）。
    QString m_serverId;
};
