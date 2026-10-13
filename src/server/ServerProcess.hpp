/**
 * @file ServerProcess.hpp
 * @brief 让一个装好的服务端**真的跑起来**，并且能干净地停下来（coding plan §6.2 的操作那一段 /
 *        里程碑 M4-3）。
 * @author 0x7fffff
 * @date 2026-10-13
 *
 * 【它和 `ServerInstallController` 差在哪】
 *
 * 那个是"装"（下文件、跑安装器、核对），一次一件事、跑完就没了。这个是"跑"：一条 `QProcess`
 * 从起来到退出可能要几天，中间要一直读它的 stdout、要能往里写 `stop`、要能强杀。
 * **一个服务端一份**（同时开几个服务端是允许的 —— 各有一份进程，互不干扰；这一点和客户端
 * 那条"一次 startGame = 一个会话"不同）。
 *
 * 【它不知道盘长什么样、也不知道界面长什么样】
 *
 *   * 该发什么命令（java 在哪、内存那几项插到第几位）是调用方按 `ServerRun::launchArguments()`
 *     拼好传进来的 —— 所以**验证工程能拿自己的 exe 当"服务端"**跑一整趟（那一段判据在
 *     mw_check 的 T41）；
 *   * "要等多久才允许强杀"是算好传进来的（`ServerRun::gracefulStopMs()`）；
 *   * 状态那七个词、以及"退成什么样算正常"沿用 `ServerModel` / `ServerRun::statusAfterExit()`。
 *
 * 【状态是怎么走的】
 *
 *          start()               读到 `Done (`              自己退 / 我们发 stop
 *   可启动 ────────► 启动中 ────────────────► 运行中 ──────────────────────► 已停止
 *                     │                        │                             ▲
 *                     │  进程自己退了（eula     │  非 0 退出                  │
 *                     │  没同意 / 端口被占）    └──────────────────────► 异常退出
 *                     └────────────────────────────────────────────────────┘
 *
 *   * `stop` 发出去之后停在**停止中**，`gracefulMs` 到点才发 `forceKillAvailable()` ——
 *     在那之前界面那颗「强制结束」是灰的（用户 2026-10-13 定的：宽限时间按模组数量和存档
 *     大小算，见 `ServerRun::gracefulStopMs()`）。存盘中途被打死正是丢存档的姿势。
 *   * ★ 「启动中 → 运行中」只认那一行 `Done (`（`ServerRun::isReadyLine()`）—— 服务端没有
 *     "窗口出现了"这种信号。
 */

#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

#include "server/ServerModel.hpp"

class QTimer;

namespace ServerRun {

class ServerProcess : public QObject {
    Q_OBJECT

public:
    explicit ServerProcess(QObject *parent = nullptr);
    /// 析构时**进程还活着就杀掉**（启动器关了不能让 QProcess 的析构去等 —— 那一等就是
    /// 卡住界面；收尾那一问在 `MainWindow` 那边先问过了）。
    ~ServerProcess() override;

    QString serverId() const { return m_serverId; }
    ServerModel::Status status() const { return m_status; }
    /// 进程现在还在（启动中 / 运行中 / 停止中）。
    bool isRunning() const;
    /// 「强制结束」现在能不能点（见文件头那段）。
    bool forceKillAllowed() const { return m_forceKillAllowed; }
    /// 这一趟等的是多少毫秒（发过 `stop` 之后到允许强杀那一段）。界面要把这句话说出来
    /// （`ServerRun::forceKillNote()` 要一个秒数）。
    int gracefulMs() const { return m_gracefulMs; }
    /// 这一趟最后是**我们按了强杀**才收场的（`exited` 的接收方据此换一句话 ——
    /// "异常退出"和"你让我强杀的"是两件事，后者不该弹一个预警框）。
    bool wasForceKilled() const { return m_killed; }

    /**
     * @brief 起手。
     * @param serverId 哪个服务端（信号带回去用）。
     * @param dir 工作目录 = 服务端目录（`Servers/<id>`）。
     * @param program `java` 那个可执行文件（验证工程里换成自己的 exe）。
     * @param arguments 见 `ServerRun::launchArguments()`。
     * @param gracefulMs 发过 `stop` 之后等多久才允许强杀。
     */
    void start(const QString &serverId, const QString &dir, const QString &program,
               const QStringList &arguments, int gracefulMs);

    /// 正常停：往里写一行 `stop`。**不是**"停止中"的唯一入口（它自己 `/stop` 也行）。
    /// @return 真的写出去了才 true（没在跑 / 通道写不进去 = false）。
    bool requestStop();
    /// 强制结束（`QProcess::kill()`）。宽限时间没到也照样执行 —— 这一层不替界面把关，
    /// 那颗按钮亮不亮是界面的事（`forceKillAllowed()`）。
    void forceKill();

    /**
     * @brief 正常停，并且**在这儿等它走完**（关掉启动器时用户选了「一起停掉」）。
     * @return true = 它退干净了；false = 是强杀收的场（写不进那一句，或者等过了头）。
     *
     * ★ 这一发**是阻塞的**（`QProcess::waitForFinished()`，界面在这几秒里不动）。选择让它
     *   静态地等，是因为另一种做法（发完 `stop` 就走人、让析构去杀）等于把"要不要停"这一
     *   问白问了 —— 那正是丢存档的那条路。等多久由 `gracefulMs` 决定（模组数量 / 存档大小
     *   算出来的那个数，见 `ServerRun::gracefulStopMs()`），再加一点点收尾余量。
     */
    bool stopAndWait();

    /**
     * @brief 把进程**交出去**：关掉启动器之后它还活着（用户选了「留着它们继续跑」）。
     *
     * 做法和 `LauncherManager::detachRunningGame()` 逐字同源（断信号 + `setParent(nullptr)`
     * + 故意不释放那个 `QProcess`）：析构链走到这儿时那份"进程还在跑就 kill()"的逻辑就够不着
     * 它了。交出去之后这个对象**再也不认那条进程**（`isRunning()` 会回 false）。
     */
    void detach();

    /**
     * @brief 往它的 stdin 写**一行**（控制台底下那格敲的东西）。
     * @param command 用户敲的那一行，**不带换行**（换行由这一层补 —— 服务端那边是按行读的，
     *        少了它这条命令会一直躺在缓冲区里等下一行）。
     * @return 真的写出去了才 true（没在跑 / 通道关了 = false）。
     *
     * ★ 这一层**一个字都不解释、也不拦**：它自己认什么命令就是什么（`say` / `op` / `stop`
     *   / 它装的插件自己加的那些）。我们唯一特判过的是"正常停"那条路（`requestStop()`）。
     */
    bool sendCommand(const QString &command);

    /// 控制台里攒着的那几行（最多 `maxLogLines()` 行，超了从最老的开始丢）。
    QStringList logLines() const { return m_log; }
    /// 日志缓冲区留多少行 —— **规格数**，验证工程读它，别在测试里写死。
    static int maxLogLines();

signals:
    void statusChanged(const QString &serverId, ServerModel::Status status);
    /// 吐了一行（已经按行切开、去掉了行尾）。
    void lineAppended(const QString &serverId, const QString &line);
    /// 宽限时间到了 ⇒ 界面可以把「强制结束」放出来了。
    void forceKillAvailable(const QString &serverId);
    /// 进程收了。@param exitCode 退出码（`FailedToStart` 那种是 −1）。
    void exited(const QString &serverId, int exitCode);
    /// 写进启动器日志的一句话。
    void message(const QString &text);

private:
    void setStatus(ServerModel::Status status);
    void appendLine(const QString &line);
    void drainOutput();

    QProcess *m_process = nullptr;
    QTimer *m_gracefulTimer = nullptr;
    QString m_serverId;
    QString m_dir;      ///< 工作目录（起手时原样记着，报错那种句子要用它）。
    QString m_program;  ///< `java`（或验证工程里那个 exe）—— 起不来时那句报错要写它。
    QString m_pending;  ///< 还没凑够一行的那些字节（见 `drainOutput()`）。
    QStringList m_log;
    ServerModel::Status m_status = ServerModel::Status::Stopped;
    int m_gracefulMs = 0;
    bool m_forceKillAllowed = false;
    bool m_started = false;  ///< 读到过 `Done (` 没有。
    bool m_killed = false;   ///< 这一趟是不是我们按过强杀（`statusAfterExit()` 要它）。
};

}  // namespace ServerRun
