#include "Support.hpp"
#include "LongPathTests.hpp"

#include "operations/InstanceUpgrade.hpp"
#include "launcher/LauncherManager.hpp"
#include "settings/EffectiveJavaSettings.hpp"
#include "settings/GameWindowSettings.hpp"
#include "util/AppPaths.hpp"
#include "manage/InstanceFiles.hpp"

#include <QDir>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QSaveFile>
#include <QTimer>
#include <QSettings>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "P0AcceptanceProbes.hpp"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QThread>
#include <QUuid>

#include <cstdio>
#include <cstdlib>

namespace {

/// 阶段名给验收脚本解析用：脚本据此把「进程死在哪个阶段」与耗时对起来，
/// 不靠固定 sleep 猜。改名会直接让脚本的 `UPGRADE_HOLD` 等不到，属于协议变更。
const char *stageName(InstanceUpgrade::Stage stage) {
    switch (stage) {
    case InstanceUpgrade::Stage::Checking: return "checking";
    case InstanceUpgrade::Stage::Snapshot: return "snapshot";
    case InstanceUpgrade::Stage::VerifySnapshot: return "verify-snapshot";
    case InstanceUpgrade::Stage::RetainOriginal: return "retain-original";
    case InstanceUpgrade::Stage::Publish: return "publish";
    case InstanceUpgrade::Stage::Verify: return "verify";
    case InstanceUpgrade::Stage::Done: return "done";
    case InstanceUpgrade::Stage::Copy: return "copy";
    case InstanceUpgrade::Stage::VerifyCopy: return "verify-copy";
    }
    return "unknown";
}

void printStage(InstanceUpgrade::Stage stage, const QElapsedTimer &clock) {
    std::printf("UPGRADE_STAGE name=%s elapsedMs=%lld\n", stageName(stage),
                static_cast<long long>(clock.elapsed()));
    std::fflush(stdout);
}

InstanceUpgrade::Core acceptanceCore(const QString &clientJar) {
    InstanceUpgrade::Core core;
    core.clientJar = clientJar;
    core.gameVersion = QStringLiteral("1.21.1");
    core.loaderName = QStringLiteral("Acceptance");
    core.mergedJson = QJsonObject{{QStringLiteral("id"), QStringLiteral("AcceptanceTarget")},
                                  {QStringLiteral("mainClass"), QStringLiteral("ExampleMain")}};
    return core;
}


#ifdef Q_OS_WIN
namespace {
struct WindowSearch {
    DWORD pid = 0;
    bool found = false;
};

BOOL CALLBACK findGameWindow(HWND window, LPARAM data) {
    auto *search = reinterpret_cast<WindowSearch *>(data);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid != search->pid || !IsWindowVisible(window)) return TRUE;
    wchar_t title[512] = {};
    GetWindowTextW(window, title, 511);
    const QString text = QString::fromWCharArray(title);
    if (text.contains(QStringLiteral("Minecraft"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("Forge"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("NeoForge"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("Fabric"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("Quilt"), Qt::CaseInsensitive)) {
        search->found = true;
        return FALSE;
    }
    return TRUE;
}

bool gameWindowFor(DWORD pid) {
    WindowSearch search;
    search.pid = pid;
    EnumWindows(findGameWindow, reinterpret_cast<LPARAM>(&search));
    return search.found;
}
}
#endif

/// 真实启动到主菜单探针（不进 CTest）。
///
/// 用法：`verify_instances --launch-main-menu <gameDir> <versionId> <javaPath> <xmx> <timeoutSec> <logFile>`
///
/// 按人工确认：**到主菜单即可，Forge/NeoForge 不要求进世界**。判据：
/// · 原版/加载器进入主菜单后会打印的声音/资源初始化强标记；或
/// · 游戏窗口出现且进程继续存活 5 秒（窗口可能先于主菜单创建，所以还要存活验证）。
/// 不靠固定 sleep 判成功；失败时输出退出码、耗时与最后若干行输出。
int runLaunchProbeIfRequested(const QStringList &arguments) {
    if (arguments.size() < 8 || arguments.at(1) != QLatin1String("--launch-main-menu")) {
        return -1;
    }
    const QString gameDir = arguments.at(2);
    const QString versionId = arguments.at(3);
    const QString javaPath = arguments.at(4);
    const int xmx = qMax(512, arguments.at(5).toInt());
    const int timeoutSec = qMax(30, arguments.at(6).toInt());
    const QString logFile = arguments.at(7);
    AppPaths::setMinecraftDir(gameDir);
    VersionManager versions;
    const QJsonObject json = versions.getVersionJson(versionId);
    if (json.isEmpty()) {
        std::printf("LAUNCH_MAIN_MENU ok=0 reason=version-json-missing version=%s\n",
                    versionId.toUtf8().constData());
        return 3;
    }
    QStringList errors;
    LauncherManager launcher;
    // **顺序要和产品一致**：MainWindow 那条启动路上是 prepareNatives() 先、checkPreLaunch()
    // 后（见 LaunchController 里"旧格式（1.18.2 及以下）的 natives 必须先落地"那句）。
    // 倒过来会用一个**第一次启动本来就不存在**的 natives 目录去判，1.7.10 的包（GTNH 2.8.4
    // 就是）当场被拦成 "该版本需要 natives 组件，但 …/natives 里没有解压出 DLL" ——
    // 那是探针自己的顺序错，不是实例缺东西。
    launcher.prepareNatives(json, versionId, &errors);
    if (!errors.isEmpty()) {
        std::printf("LAUNCH_MAIN_MENU ok=0 reason=natives %s\n",
                    errors.join(QStringLiteral(" | ")).toUtf8().constData());
        return 4;
    }
    if (!launcher.checkPreLaunch(versionId, &errors, json)) {
        std::printf("LAUNCH_MAIN_MENU ok=0 reason=prelaunch %s\n",
                    errors.join(QStringLiteral(" | ")).toUtf8().constData());
        return 4;
    }
    const QString assetIndex = json.value(QStringLiteral("assetIndex")).toObject()
                                   .value(QStringLiteral("id")).toString().isEmpty()
        ? json.value(QStringLiteral("assets")).toString()
        : json.value(QStringLiteral("assetIndex")).toObject().value(QStringLiteral("id")).toString();
    const int javaMajor = json.value(QStringLiteral("javaVersion")).toObject()
                              .value(QStringLiteral("majorVersion")).toInt();
    EffectiveJavaSettings java;
    java.javaPath = javaPath;
    java.xmx = xmx;
    java.xms = qMax(128, xmx / 4);
    GameWindowSettings window;
    ScreenWorkArea screen;
    screen.width = 1280;
    screen.height = 720;
    QStringList output;
    QStringList stderrLines;
    int sessionId = 0;
    bool finished = false;
    int exitCode = -1;
    bool soundMarker = false;
    bool loaderMarker = false;
    bool windowFound = false;
    qint64 windowSeenAt = 0;
    const QStringList loaderMarkers{
        QStringLiteral("Mod loading complete"), QStringLiteral("Forge mod loading complete"),
        QStringLiteral("Loading complete"), QStringLiteral("Finished loading")};
    QObject::connect(&launcher, &LauncherManager::processOutput, &launcher,
                     [&](int id, const QString &text) {
                         if (id != sessionId) return;
                         output.append(text);
                         if (text.contains(QStringLiteral("Sound engine started"),
                                           Qt::CaseInsensitive)
                             || text.contains(QStringLiteral("OpenAL initialized"),
                                              Qt::CaseInsensitive)) {
                             soundMarker = true;
                         }
                         for (const QString &marker : loaderMarkers) {
                             if (text.contains(marker, Qt::CaseInsensitive)) loaderMarker = true;
                         }
                     });
    QObject::connect(&launcher, &LauncherManager::processError, &launcher,
                     [&](int id, const QString &text) {
                         if (id == sessionId) stderrLines.append(text);
                     });
    QObject::connect(&launcher, &LauncherManager::gameFinished, &launcher,
                     [&](int id, int code, QProcess::ExitStatus) {
                         if (id != sessionId) return;
                         finished = true;
                         exitCode = code;
                     });
    const QStringList jvmArgs = launcher.generateJvmArguments(
        QString(), versionId, assetIndex, json, java, true, javaMajor,
        versions.versionJvmArgs(versionId), QStringLiteral("Acceptance"), QString(),
        window, screen);
    QElapsedTimer clock;
    clock.start();
    sessionId = launcher.startGame(jvmArgs, java, versionId, versionId,
                                   QStringLiteral("Acceptance"), QString());
    if (sessionId <= 0) {
        std::printf("LAUNCH_MAIN_MENU ok=0 reason=start-game-failed elapsedMs=%lld\n",
                    static_cast<long long>(clock.elapsed()));
        return 5;
    }
    const qint64 gamePid = launcher.session(sessionId).processId;
    QEventLoop loop;
    QTimer poll;
    bool success = false;
    QString reason;
    const bool requireSound = qEnvironmentVariableIsSet("WTFML_ACCEPT_REQUIRE_SOUND");
    const int holdMs = qBound(0, qEnvironmentVariableIntValue("WTFML_ACCEPT_HOLD_MS"), 60000);
    qint64 soundSeenAt = -1;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (finished) {
            reason = QStringLiteral("process-exited");
            loop.quit();
            return;
        }
        if (soundMarker) {
            if (soundSeenAt < 0) soundSeenAt = clock.elapsed();
            if (clock.elapsed() - soundSeenAt < holdMs) return;
            success = true;
            reason = QStringLiteral("sound-marker");
            loop.quit();
            return;
        }
#ifdef Q_OS_WIN
        if (!windowFound && gamePid > 0 && gameWindowFor(static_cast<DWORD>(gamePid))) {
            windowFound = true;
            windowSeenAt = clock.elapsed();
        }
        if (!requireSound && windowFound && clock.elapsed() - windowSeenAt >= 20000) {
            success = true;
            reason = QStringLiteral("window-alive-20s");
            loop.quit();
            return;
        }
#endif
        if (clock.elapsed() >= static_cast<qint64>(timeoutSec) * 1000) {
            reason = QStringLiteral("timeout");
            loop.quit();
        }
    });
    poll.start(250);
    loop.exec();
    poll.stop();
    const qint64 elapsed = clock.elapsed();
    if (launcher.runningCount() > 0) {
        launcher.stopGame(sessionId);
        for (int i = 0; i < 20 && launcher.runningCount() > 0; ++i) QThread::msleep(100);
    }
    if (!logFile.isEmpty()) {
        QSaveFile log(logFile);
        if (log.open(QIODevice::WriteOnly)) {
            const QByteArray text = (QStringLiteral("elapsedMs=%1 success=%2 reason=%3 exit=%4\n")
                                         .arg(elapsed).arg(success ? 1 : 0).arg(reason)
                                         .arg(exitCode)
                                     + QStringLiteral("--- stdout ---\n")
                                     + output.join(QString())
                                     + QStringLiteral("\n--- stderr ---\n")
                                     + stderrLines.join(QString()))
                                        .toUtf8();
            if (log.write(text) == text.size()) log.commit();
        }
    }
    std::printf("LAUNCH_MAIN_MENU ok=%d reason=%s elapsedMs=%lld pid=%lld exit=%d\n",
                success ? 1 : 0, reason.toUtf8().constData(),
                static_cast<long long>(elapsed), static_cast<long long>(gamePid), exitCode);
    return success ? 0 : 6;
}


namespace {
bool acceptanceCore(VersionManager &versions, const QString &targetId,
                    InstanceUpgrade::Core *core, QString *problem) {
    core->mergedJson = versions.getVersionJson(targetId);
    core->clientJar = InstanceFiles::jarPath(targetId);
    core->gameVersion = versions.instanceGameVersion(targetId);
    QString loaderName;
    QString loaderVersion;
    versions.instanceModLoader(targetId, &loaderName, &loaderVersion);
    core->loaderName = loaderName;
    core->loaderVersion = loaderVersion;
    core->disableOldMods = true;
    if (core->mergedJson.isEmpty() || core->gameVersion.isEmpty()
        || !QFileInfo(core->clientJar).isFile()) {
        *problem = QStringLiteral("target core is incomplete");
        return false;
    }
    return true;
}
}

/// 真实升级探针（不进 CTest）：
/// `--upgrade-copy <gameDir> <sourceId> <newId> <targetCoreId>`
/// `--upgrade-inplace <gameDir> <instanceId> <targetCoreId>`
/// `--upgrade-rollback <gameDir> <instanceId> <transactionId>`
int runUpgradeProbeIfRequested(const QStringList &arguments) {
    if (arguments.size() < 2) return -1;
    const QString mode = arguments.at(1);
    if (mode != QLatin1String("--upgrade-copy")
        && mode != QLatin1String("--upgrade-inplace")
        && mode != QLatin1String("--upgrade-rollback")
        && mode != QLatin1String("--upgrade-inplace-crash")) {
        return -1;
    }
    DirectorySessionLock::enabled = true;
    if (mode == QLatin1String("--upgrade-copy") && arguments.size() >= 6) {
        AppPaths::setMinecraftDir(arguments.at(2));
        VersionManager versions;
        InstanceUpgrade::Core core;
        QString problem;
        if (!acceptanceCore(versions, arguments.at(5), &core, &problem)) {
            std::printf("UPGRADE_COPY ok=0 problem=%s\n", problem.toUtf8().constData());
            return 3;
        }
        const QString sourceDir = AppPaths::versionDir(arguments.at(3));
        const QString transaction = QUuid::createUuid().toString(QUuid::WithoutBraces);
        InstanceUpgrade::Request request{arguments.at(2), arguments.at(4), transaction,
                                        arguments.at(4)};
        const auto result = InstanceUpgrade::prepareCopyAndCommit(request, core, sourceDir);
        std::printf("UPGRADE_COPY ok=%d transaction=%s disabled=%d changes=%lld problem=%s\n",
                    result.ok ? 1 : 0, transaction.toUtf8().constData(), result.disabledMods,
                    static_cast<long long>(result.changes.size()),
                    result.problem.toUtf8().constData());
        return result.ok ? 0 : 4;
    }
    if (mode == QLatin1String("--upgrade-inplace") && arguments.size() >= 5) {
        AppPaths::setMinecraftDir(arguments.at(2));
        VersionManager versions;
        InstanceUpgrade::Core core;
        QString problem;
        if (!acceptanceCore(versions, arguments.at(4), &core, &problem)) {
            std::printf("UPGRADE_INPLACE ok=0 problem=%s\n", problem.toUtf8().constData());
            return 3;
        }
        const QString transaction = QUuid::createUuid().toString(QUuid::WithoutBraces);
        InstanceUpgrade::Request request{arguments.at(2), arguments.at(3), transaction,
                                        arguments.at(3)};
        const auto result = InstanceUpgrade::prepareAndCommit(request, core);
        std::printf("UPGRADE_INPLACE ok=%d transaction=%s rolledBack=%d snapshot=%s problem=%s\n",
                    result.ok ? 1 : 0, transaction.toUtf8().constData(),
                    result.rolledBack ? 1 : 0, result.snapshotZip.toUtf8().constData(),
                    result.problem.toUtf8().constData());
        return result.ok ? 0 : 4;
    }
    if (mode == QLatin1String("--upgrade-inplace-crash") && arguments.size() >= 5) {
        AppPaths::setMinecraftDir(arguments.at(2));
        VersionManager versions;
        InstanceUpgrade::Core core;
        QString problem;
        if (!acceptanceCore(versions, arguments.at(4), &core, &problem)) {
            std::printf("UPGRADE_INPLACE_CRASH ok=0 problem=%s\n", problem.toUtf8().constData());
            return 3;
        }
        const QString transaction = QUuid::createUuid().toString(QUuid::WithoutBraces);
        InstanceUpgrade::Request request{arguments.at(2), arguments.at(3), transaction,
                                        arguments.at(3)};
        try {
            InstanceUpgrade::prepareAndCommit(request, core, [](InstanceUpgrade::Stage stage) {
                if (stage == InstanceUpgrade::Stage::Publish)
                    throw std::runtime_error("accepted crash at publish");
            });
        } catch (const std::runtime_error &) {
            std::printf("UPGRADE_INPLACE_CRASH transaction=%s crashedAt=publish\n",
                        transaction.toUtf8().constData());
            return 0;
        }
        std::printf("UPGRADE_INPLACE_CRASH ok=0 reason=did-not-crash\n");
        return 4;
    }
    if (mode == QLatin1String("--upgrade-rollback") && arguments.size() >= 5) {
        AppPaths::setMinecraftDir(arguments.at(2));
        InstanceUpgrade::Request request{arguments.at(2), arguments.at(3), arguments.at(4),
                                        arguments.at(3)};
        const auto result = InstanceUpgrade::rollback(request);
        std::printf("UPGRADE_ROLLBACK ok=%d rolledBack=%d problem=%s\n",
                    result.ok ? 1 : 0, result.rolledBack ? 1 : 0,
                    result.problem.toUtf8().constData());
        return result.ok && result.rolledBack ? 0 : 4;
    }
    return 2;
}

/// 人工验收用的真实大实例探针。**不注册进 CTest**：它按调用方给的真实目录干活，
/// 默认测试集不该去碰用户盘上的大文件。输出一行机器可读的结果给
/// `tools/acceptance_large_instance.py` 判定。
int runAcceptanceProbeIfRequested(const QStringList &arguments) {
    const auto flag = [&arguments](const char *name) {
        return arguments.size() > 1 && arguments.at(1) == QLatin1String(name);
    };

    if (flag("--large-copy") && arguments.size() >= 5) {
        DirectorySessionLock::enabled = true;
        VersionManager vm;
        const QString source = arguments.at(2);
        QElapsedTimer clock;
        clock.start();
        QString error;
        const bool ok = vm.copyInstanceFrom(source, QFileInfo(source).fileName(),
                                            arguments.at(3), arguments.at(4), &error);
        std::printf("LARGE_COPY ok=%d elapsedMs=%lld name=%s error=%s\n", ok ? 1 : 0,
                    static_cast<long long>(clock.elapsed()),
                    arguments.at(4).toUtf8().constData(), error.toUtf8().constData());
        return ok ? 0 : 3;
    }

    if ((flag("--large-upgrade-cancel") || flag("--large-upgrade-abort-publish")) && arguments.size() >= 7) {
        const bool abortAtPublish = flag("--large-upgrade-abort-publish");
        DirectorySessionLock::enabled = true;
        const int cancelMs = arguments.at(5).toInt();
        InstanceUpgrade::Request request{arguments.at(2), arguments.at(3), arguments.at(6),
                                         QStringLiteral("Acceptance")};
        QElapsedTimer clock;
        clock.start();
        const auto result = InstanceUpgrade::prepareAndCommit(
            request, acceptanceCore(arguments.at(4)),
            [abortAtPublish, &clock](InstanceUpgrade::Stage stage) {
                printStage(stage, clock);
                if (abortAtPublish && stage == InstanceUpgrade::Stage::Publish) {
                    // 真正走到原目录已保留、尚未发布的边界，再无析构退出。
                    std::printf("ABORT_AT_PUBLISH\n");
                    std::fflush(stdout);
                    std::_Exit(86);
                }
            },
            [&clock, cancelMs] { return clock.elapsed() >= cancelMs; });
        std::printf("LARGE_UPGRADE cancelled=%d ok=%d elapsedMs=%lld problem=%s\n",
                    result.cancelled ? 1 : 0, result.ok ? 1 : 0,
                    static_cast<long long>(clock.elapsed()),
                    result.problem.toUtf8().constData());
        return result.cancelled ? 0 : (result.ok ? 2 : 3);
    }

    // 外部终止验收：走到指定阶段后停住（不自己退出、不吞掉事务），
    // 由验收脚本 TerminateProcess 再起一个进程跑恢复。固定 sleep 杀进程
    // 会随磁盘快慢落在不同阶段（快盘上甚至已经升级完），不能当证据。
    if (flag("--large-upgrade-hold") && arguments.size() >= 7) {
        DirectorySessionLock::enabled = true;
        const QString holdStage = arguments.at(5);
        InstanceUpgrade::Request request{arguments.at(2), arguments.at(3), arguments.at(6),
                                         QStringLiteral("Acceptance")};
        QElapsedTimer clock;
        clock.start();
        bool held = false;
        const auto result = InstanceUpgrade::prepareAndCommit(
            request, acceptanceCore(arguments.at(4)),
            [holdStage, &held, &clock](InstanceUpgrade::Stage stage) {
                printStage(stage, clock);
                if (held || holdStage != QLatin1String(stageName(stage))) return;
                held = true;
                std::printf("UPGRADE_HOLD name=%s elapsedMs=%lld\n", stageName(stage),
                            static_cast<long long>(clock.elapsed()));
                std::fflush(stdout);
                // 只有当父进程没来杀时才走到这里：600 秒后按失败退出，避免手动跑挂死。
                QElapsedTimer wait;
                wait.start();
                while (wait.elapsed() < 600000) QThread::msleep(100);
                std::printf("UPGRADE_HOLD_TIMEOUT name=%s\n", stageName(stage));
                std::fflush(stdout);
                std::_Exit(87); // 失联时留待恢复，不在超时后继续发布。
            },
            [] { return false; });
        std::printf("LARGE_UPGRADE held=%d ok=%d elapsedMs=%lld problem=%s\n", held ? 1 : 0,
                    result.ok ? 1 : 0, static_cast<long long>(clock.elapsed()),
                    result.problem.toUtf8().constData());
        return held ? 4 : (result.ok ? 0 : 3);
    }

    if (flag("--large-upgrade-copy-audit") && arguments.size() >= 6) {
        DirectorySessionLock::enabled = true;
        InstanceUpgrade::Request request{arguments.at(2), arguments.at(3), arguments.at(5),
                                         QStringLiteral("Acceptance")};
        auto core = acceptanceCore(arguments.at(4));
        // Deliberately reject the new client AFTER copying/verifying user files.
        // This audits a failed fixture without publishing over its original instance.
        core.mergedJson.insert("downloads", QJsonObject{{"client", QJsonObject{{"sha1", QString(40, '0')}}}});
        const auto result = InstanceUpgrade::prepareAndCommit(request, core);
        const bool copied = !result.ok && result.problem == QStringLiteral("目标客户端文件未通过校验，原实例未改变。");
        std::printf("COPY_AUDIT copied=%d problem=%s\n", copied ? 1 : 0, qPrintable(result.problem));
        return copied ? 0 : 3;
    }

    if (flag("--large-upgrade-complete") && arguments.size() >= 6) {
        DirectorySessionLock::enabled = true;
        InstanceUpgrade::Request request{arguments.at(2), arguments.at(3), arguments.at(5),
                                         QStringLiteral("Acceptance")};
        QElapsedTimer clock;
        clock.start();
        const auto result = InstanceUpgrade::prepareAndCommit(
            request, acceptanceCore(arguments.at(4)),
            [&clock](InstanceUpgrade::Stage stage) { printStage(stage, clock); });
        std::printf("LARGE_UPGRADE ok=%d elapsedMs=%lld problem=%s\n", result.ok ? 1 : 0,
                    static_cast<long long>(clock.elapsed()),
                    result.problem.toUtf8().constData());
        return result.ok ? 0 : 3;
    }

    if (flag("--upgrade-recover") && arguments.size() >= 3) {
        DirectorySessionLock::enabled = true;
        const QStringList messages = InstanceUpgrade::recoverInterrupted(arguments.at(2));
        std::printf("UPGRADE_RECOVER count=%lld\n", static_cast<long long>(messages.size()));
        for (const QString &message : messages) {
            std::printf("  %s\n", message.toUtf8().constData());
        }
        return 0;
    }

    return -1;
}

}  // namespace

int main(int argc, char **argv) {
 QCoreApplication app(argc, argv);
 if (const int probe = runP0AcceptanceProbe(app.arguments()); probe >= 0) return probe;
 if (const int probe = runDirectoryLockProbeIfRequested(app.arguments()); probe >= 0) return probe;
 if (const int probe = runUpgradeProbeIfRequested(app.arguments()); probe >= 0) return probe;
 if (const int probe = runLaunchProbeIfRequested(app.arguments()); probe >= 0) return probe;
 if (const int probe = runAcceptanceProbeIfRequested(app.arguments()); probe >= 0) return probe;
 if (app.arguments().size() > 1) {
     std::fprintf(stderr, "Unknown acceptance probe argument; refusing to run unrelated regression tests.\n");
     return 2;
 }
 int moduleStatus = 0;
 app.setOrganizationName("WTFML"); app.setApplicationName("WTFML");
 QTemporaryDir fixture; if (!fixture.isValid()) return 2;
 QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,fixture.path());
 QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,fixture.filePath("system-settings"));
 const QString sandbox=fixture.filePath(".minecraft"); QDir().mkpath(sandbox); AppPaths::setMinecraftDir(sandbox);
 app.setProperty("wtfml.testDefaultMinecraftDir", sandbox);
 const QString metaDir=QStringLiteral(REPO_ROOT_PATH)+"/tests/meta";
    moduleStatus |= runVersionNameTests(sandbox);
    moduleStatus |= runVersionJvmArgsTests(sandbox);
    moduleStatus |= runInstanceFileTests(sandbox);
    moduleStatus |= runVersionMetaWriteTests(sandbox);
    moduleStatus |= runInstanceTraitsTests(sandbox);
    moduleStatus |= runInstallOccupancyTests(sandbox);
    moduleStatus |= runRenameInstanceTests(sandbox);
    moduleStatus |= runInstanceGroupTests(sandbox);
    moduleStatus |= runInstalledScanTests(sandbox);
    runVersionChannelTests();
    runIconCacheTests();
    runImageDecodeTests();
    runCopyInstanceAndWorldTests();
    runVersionIntegrityTests();
    runBackupTests();
    runInstanceFilterTests();
    runUpdatePolicyTests();
    runUpdateRollbackTests();
    runFolderRemovalTests();
    runTransferPlannerTests();
    runOperationReportTests();
    runLongPathTests();
 out()<<"instances: "<<g_checks-g_failures<<"/"<<g_checks<<"\n"; return g_failures==0 && moduleStatus==0 ? 0 : 1;
}

