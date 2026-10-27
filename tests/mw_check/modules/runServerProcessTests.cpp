#include "Support.hpp"
void runServerProcessTests() {
    const QString exe = QCoreApplication::applicationFilePath();
    check(!exe.isEmpty() && QFileInfo::exists(exe),
          "T41.0 这个 exe 自己就是那个「假服务端」（连程序都找不到就什么都别谈了）", exe);

    QTemporaryDir dir;
    check(dir.isValid(), "T41.0 一个临时工作目录建出来了");
    {
        ServerRun::ServerProcess process;
        int exits = 0, code = -1;
        QObject::connect(&process, &ServerRun::ServerProcess::exited, &process,
                         [&](const QString &, int exitCode) { ++exits; code = exitCode; });
        process.start(QStringLiteral("startup-zero"), dir.path(), exe,
                      {QStringLiteral("--fake-server"), QStringLiteral("startup-zero")}, 4000);
        check(pumpUntil([&] { return exits > 0; }, 10000) && exits == 1 && code == 0,
              "T41.0b 启动失败但返回零的真实子进程只发一次终态");
        check(process.status() == ServerModel::Status::Crashed && !process.isRunning(),
              "T41.0b 未就绪也未主动停止时，退出零仍属于启动失败");
        process.start(QStringLiteral("startup-wait"), dir.path(), exe,
                      {QStringLiteral("--fake-server"), QStringLiteral("startup-wait")}, 4000);
        check(pumpUntil([&] { return !process.logLines().isEmpty(); }, 10000) &&
                  process.status() == ServerModel::Status::Starting && process.requestStop(),
              "T41.0c 用户可在尚未就绪时请求停止");
        check(pumpUntil([&] { return exits == 2; }, 10000) && code == 0 &&
                  process.status() == ServerModel::Status::Stopped,
              "T41.0c 启动中主动停止并干净退出，不误报启动失败");
    }

    // ---- T41.1 起 → 读到 `Done (` → 运行中 ----
    {
        ServerRun::ServerProcess process;
        int ended = 0;
        QObject::connect(&process, &ServerRun::ServerProcess::exited, &process, [&](const QString &, int) { ++ended; });
        process.start("wrongjava-hang", dir.path(), exe, {"--fake-server", "wrongjava-hang"}, 4000);
        check(pumpUntil([&] { return ended > 0; }, 5000) && ended == 1 && !process.isRunning() &&
            process.status() == ServerModel::Status::Crashed && !process.wasForceKilled(),
            "T41 fatal startup Java mismatch is bounded and classified as failure, not user force-kill");
    }
    {
        ServerRun::ServerProcess process;
        QString lastStatus;
        QList<QPair<QString, int>> exits;
        QObject::connect(&process, &ServerRun::ServerProcess::statusChanged, &process,
                         [&lastStatus](const QString &, ServerModel::Status status) {
                             lastStatus = ServerModel::statusName(status);
                         });
        QObject::connect(&process, &ServerRun::ServerProcess::exited, &process,
                         [&exits](const QString &id, int code) { exits.append({id, code}); });

        process.start(QStringLiteral("t41"), dir.path(), exe,
                      {QStringLiteral("--fake-server"), QStringLiteral("normal")}, 4000);
        check(process.isRunning(), "T41.1 起手那一刻进程就在跑");
        check(process.status() == ServerModel::Status::Starting,
              "T41.1 状态先停在「启动中」（那一行 `Done (` 还没到）",
              ServerModel::statusName(process.status()));

        const bool ready = pumpUntil(
            [&process] { return process.status() == ServerModel::Status::Running; }, 20000);
        check(ready, "T41.1 读到 `Done (` 之后自己翻成「运行中」（服务端没有窗口能替它说话）",
              lastStatus);

        // ---- T41.2 日志是**逐行**切开的 ----
        // 一次 `readyRead` 里到的可能是三行、也可能是半行 —— 切不开的话控制台里会是一大坨。
        QStringList lines = process.logLines();
        check(lines.size() >= 3, "T41.2 开场白那三行一行一条（按行尾切开了）",
              QString::number(lines.size()));
        bool sawDone = false;
        bool sawStarting = false;
        for (const QString &line : lines) {
            if (line.contains(QStringLiteral("Starting minecraft server"))) {
                sawStarting = true;
            }
            if (ServerRun::isReadyLine(line)) {
                sawDone = true;
            }
            check(!line.contains(QLatin1Char('\n')) && !line.contains(QLatin1Char('\r')),
                  "T41.2 每一行里都不带行尾那种字符", line);
        }
        check(sawStarting && sawDone, "T41.2 那两行都在（而且 `isReadyLine` 认得出后者）");

        // ---- T41.3 往它的 stdin 写一行：真的进去了 ----
        // 假服务端会把每一行**原样回声**，所以"回声到了"就是"写下去了、而且补了换行"
        // （少了换行它会一直躺在缓冲区里等下一行 —— 那句话永远不来）。
        //
        // 中文只在**本地代码页写得出来**的时候才用。`sendCommand()` 走的是
        // `toLocal8Bit()`（JVM 的 stdin 也是按系统代码页解），英文版 Windows 上它会把
        // "你好"变成 "??" —— 回声当然对不上，而这跟换行、分包一点关系都没有。
        // 写得出来就用中文（顺带钉住多字节那一趟），写不出来退回 ASCII。
        const QString echoCommand =
            QString::fromLocal8Bit(QStringLiteral("say 你好").toLocal8Bit()) ==
                    QStringLiteral("say 你好")
                ? QStringLiteral("say 你好")
                : QStringLiteral("say hello");
        check(process.sendCommand(echoCommand), "T41.3 那一行写出去了");
        const bool echoed = pumpUntil(
            [&process, &echoCommand] {
                for (const QString &line : process.logLines()) {
                    if (line.contains(QStringLiteral("[echo] ") + echoCommand)) {
                        return true;
                    }
                }
                return false;
            },
            10000);
        check(echoed, "T41.3 服务端那边读到了它（回声回来了 ⇒ 换行补上了）");

        // ---- T41.4 正常停：写 `stop` → 它自己退 0 → 已停止 ----
        check(process.requestStop(), "T41.4 那一句 `stop` 写出去了");
        check(process.status() == ServerModel::Status::Stopping,
              "T41.4 写完就停在「停止中」（等它存盘收尾）",
              ServerModel::statusName(process.status()));
        check(!process.forceKillAllowed(),
              "T41.4 刚写完 `stop` 的时候「强制结束」还不给按（宽限时间就是给它存盘的）");

        const bool stopped = pumpUntil([&process] { return !process.isRunning(); }, 20000);
        check(stopped, "T41.4 它自己退干净了");
        check(process.status() == ServerModel::Status::Stopped,
              "T41.4 退出码 0 ⇒ 「已停止」（不是「异常退出」）",
              ServerModel::statusName(process.status()));
        check(exits.size() == 1 && exits.first().second == 0,
              "T41.4 `exited` 发了一发、带的是 0",
              QStringLiteral("%1 发").arg(exits.size()));
        check(!process.wasForceKilled(), "T41.4 这一趟不是强杀收的场（界面上那句话才对得上）");
    }

    // ---- T41.5 它自己崩了：非 0 退出 = 异常退出 ----
    {
        ServerRun::ServerProcess process;
        int code = 0;
        bool got = false;
        QObject::connect(&process, &ServerRun::ServerProcess::exited, &process,
                         [&got, &code](const QString &, int c) {
                             got = true;
                             code = c;
                         });
        process.start(QStringLiteral("t41"), dir.path(), exe,
                      {QStringLiteral("--fake-server"), QStringLiteral("crash")}, 4000);
        const bool ended = pumpUntil([&got] { return got; }, 20000);
        check(ended && code == 1, "T41.5 非 0 退出照原样报出来（退出码 1）",
              QString::number(code));
        check(process.status() == ServerModel::Status::Crashed,
              "T41.5 那一档状态是「异常退出」", ServerModel::statusName(process.status()));
    }

    // ---- T41.6 宽限时间到点 → 才允许强杀（用户 定的那一条）----
    {
        ServerRun::ServerProcess process;
        bool forceSignalled = false;
        bool got = false;
        int code = 0;
        QObject::connect(&process, &ServerRun::ServerProcess::forceKillAvailable, &process,
                         [&forceSignalled](const QString &) { forceSignalled = true; });
        QObject::connect(&process, &ServerRun::ServerProcess::exited, &process,
                         [&got, &code](const QString &, int c) {
                             got = true;
                             code = c;
                         });
        // `never` 那一档：听见 `stop` 也不退（真机上就是"卡在存盘里"那个样子）。
        const int gracefulMs = 1500;
        process.start(QStringLiteral("t41"), dir.path(), exe,
                      {QStringLiteral("--fake-server"), QStringLiteral("never")}, gracefulMs);
        const bool ready = pumpUntil(
            [&process] { return process.status() == ServerModel::Status::Running; }, 20000);
        check(ready, "T41.6 这一档也起得来");

        check(process.requestStop(), "T41.6 `stop` 写进去了（它就是不退）");
        check(!process.forceKillAllowed(), "T41.6 宽限时间没到 ⇒ 还不给强杀");
        QElapsedTimer clock;
        clock.start();
        const bool armed = pumpUntil([&forceSignalled] { return forceSignalled; }, 20000);
        check(armed, "T41.6 到点之后 `forceKillAvailable` 发出来了",
              QStringLiteral("等了 %1 ms").arg(clock.elapsed()));
        check(clock.elapsed() >= gracefulMs - 200,
              "T41.6 而且不是当场就发 —— 真的等了那一整段宽限时间",
              QStringLiteral("%1 ms").arg(clock.elapsed()));
        check(process.forceKillAllowed() && process.gracefulMs() == gracefulMs,
              "T41.6 那一层记着的就是这一趟传进去的数（界面上那句话要拿它算秒）",
              QString::number(process.gracefulMs()));

        process.forceKill();
        const bool killed = pumpUntil([&got] { return got; }, 20000);
        check(killed && code != 0, "T41.6 强杀收场，退出码非 0",
              QStringLiteral("%1").arg(code));
        check(process.wasForceKilled(),
              "T41.6 `wasForceKilled()` 记着这一趟是强杀收的场（那一句话据此换掉）");
        check(process.status() == ServerModel::Status::Crashed,
              "T41.6 强杀也是「异常退出」那一档（进程确实不是正常退的）",
              ServerModel::statusName(process.status()));
    }

    // ---- T41.7 `stopAndWait()`：关掉启动器时用户选了「一起停掉」那一发 ----
    {
        ServerRun::ServerProcess process;
        process.start(QStringLiteral("t41"), dir.path(), exe,
                      {QStringLiteral("--fake-server"), QStringLiteral("normal")}, 4000);
        const bool ready = pumpUntil(
            [&process] { return process.status() == ServerModel::Status::Running; }, 20000);
        check(ready, "T41.7 起得来");
        QElapsedTimer clock;
        clock.start();
        const bool clean = process.stopAndWait();
        check(clean, "T41.7 `stopAndWait()` 回来是「退干净了」那一档");
        check(!process.isRunning(), "T41.7 回来的时候它真的不在了（这一发是等的，不是排队的）");
        check(clock.elapsed() < 10000, "T41.7 而且它一退就回来了（没白等满那一整段）",
              QStringLiteral("%1 ms").arg(clock.elapsed()));
    }

    // ---- T41.8 强杀那一趟 `stopAndWait()`：等不到就强杀收场 ----
    {
        ServerRun::ServerProcess process;
        process.start(QStringLiteral("t41"), dir.path(), exe,
                      {QStringLiteral("--fake-server"), QStringLiteral("never")}, 800);
        const bool ready = pumpUntil(
            [&process] { return process.status() == ServerModel::Status::Running; }, 20000);
        check(ready, "T41.8 起得来");
        QElapsedTimer clock;
        clock.start();
        const bool clean = process.stopAndWait();
        check(!clean, "T41.8 它不肯退 ⇒ 回来是「没退干净」那一档；但进程已经收掉了");
        check(!process.isRunning(), "T41.8 回来时进程一定不在（这一条是「关窗那一下不许挂」的底线）",
              QStringLiteral("%1 ms").arg(clock.elapsed()));
        check(process.wasForceKilled(), "T41.8 是强杀收的场");
    }

    // ---- T41.9 没在跑的时候那几发都是安全的（不崩、不误报）----
    {
        ServerRun::ServerProcess process;
        check(!process.requestStop(), "T41.9 没在跑 ⇒ `requestStop()` 回 false，什么都不做");
        check(!process.sendCommand(QStringLiteral("say 你好")),
              "T41.9 没在跑 ⇒ `sendCommand()` 回 false（控制台底下那格这时候也不该能用）");
        process.forceKill();
        check(!process.isRunning(), "T41.9 没在跑 ⇒ `forceKill()` 是空操作");
        check(process.status() == ServerModel::Status::Stopped,
              "T41.9 一个没用过的对象就是「已停止」", ServerModel::statusName(process.status()));
        check(ServerRun::ServerProcess::maxLogLines() > 0,
              "T41.9 日志上限是个正数（控制台那一页拿它设 `setMaximumBlockCount`）",
              QString::number(ServerRun::ServerProcess::maxLogLines()));
    }

    // ---- T41.10 起不来的那一路（java 路径写错 / 权限不够）----
    // 真机上最容易撞到的收场：`QProcess` 在这种时候**不会**发 `finished`，只有
    // `errorOccurred(FailedToStart)` —— 只接前者的写法会让这一行永远停在「启动中」。
    {
        ServerRun::ServerProcess process;
        bool got = false;
        int code = 0;
        QObject::connect(&process, &ServerRun::ServerProcess::exited, &process,
                         [&got, &code](const QString &, int c) {
                             got = true;
                             code = c;
                         });
        const QString missing =
            dir.filePath(QStringLiteral("这个-java-不存在.exe"));
        process.start(QStringLiteral("t41"), dir.path(), missing, {}, 4000);
        const bool ended = pumpUntil([&got] { return got; }, 20000);
        check(ended, "T41.10 起不来也**一定**有收场（不会永远停在「启动中」）");
        check(code == -1, "T41.10 退出码报 −1（「没起得来」不是任何一种退出码）",
              QString::number(code));
        check(process.status() == ServerModel::Status::Crashed,
              "T41.10 那一档状态是「异常退出」", ServerModel::statusName(process.status()));
        check(!process.isRunning(), "T41.10 而且它确实没在跑");
    }
}

