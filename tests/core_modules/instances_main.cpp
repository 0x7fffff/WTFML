#include "Support.hpp"

#include "operations/InstanceUpgrade.hpp"
#include "P0AcceptanceProbes.hpp"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QThread>

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
 if (const int probe = runAcceptanceProbeIfRequested(app.arguments()); probe >= 0) return probe;
 if (app.arguments().size() > 1) {
     std::fprintf(stderr, "Unknown acceptance probe argument; refusing to run unrelated regression tests.\n");
     return 2;
 }
 int moduleStatus = 0;
 app.setOrganizationName("WTFML"); app.setApplicationName("WTFML");
 QTemporaryDir fixture; if (!fixture.isValid()) return 2;
 QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,fixture.path());
 const QString sandbox=fixture.filePath(".minecraft"); QDir().mkpath(sandbox); AppPaths::setMinecraftDir(sandbox);
 const QString metaDir=QStringLiteral(REPO_ROOT_PATH)+"/tests/meta";
    moduleStatus |= runVersionNameTests(sandbox);
    moduleStatus |= runVersionJvmArgsTests(sandbox);
    moduleStatus |= runInstanceFileTests(sandbox);
    moduleStatus |= runVersionMetaWriteTests(sandbox);
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
 out()<<"instances: "<<g_checks-g_failures<<"/"<<g_checks<<"\n"; return g_failures==0 && moduleStatus==0 ? 0 : 1;
}

