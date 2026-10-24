#include "Support.hpp"

#include "operations/InstanceUpgrade.hpp"

#include <QElapsedTimer>
#include <QFileInfo>

#include <cstdio>

namespace {

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

    if (flag("--large-upgrade-cancel") && arguments.size() >= 7) {
        DirectorySessionLock::enabled = true;
        const QString game = arguments.at(2);
        const QString instanceId = arguments.at(3);
        const int cancelMs = arguments.at(5).toInt();
        InstanceUpgrade::Request request{game, instanceId, arguments.at(6),
                                         QStringLiteral("Acceptance")};
        InstanceUpgrade::Core core;
        core.clientJar = arguments.at(4);
        core.gameVersion = QStringLiteral("1.21.1");
        core.loaderName = QStringLiteral("Acceptance");
        core.mergedJson = QJsonObject{{QStringLiteral("id"), QStringLiteral("AcceptanceTarget")},
                                      {QStringLiteral("mainClass"), QStringLiteral("ExampleMain")}};
        QElapsedTimer clock;
        clock.start();
        const auto result = InstanceUpgrade::prepareAndCommit(
            request, core, {},
            [&clock, cancelMs] { return clock.elapsed() >= cancelMs; });
        std::printf("LARGE_UPGRADE cancelled=%d ok=%d elapsedMs=%lld problem=%s\n",
                    result.cancelled ? 1 : 0, result.ok ? 1 : 0,
                    static_cast<long long>(clock.elapsed()),
                    result.problem.toUtf8().constData());
        return result.cancelled ? 0 : (result.ok ? 2 : 3);
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
 if (const int probe = runDirectoryLockProbeIfRequested(app.arguments()); probe >= 0) return probe;
 if (const int probe = runAcceptanceProbeIfRequested(app.arguments()); probe >= 0) return probe;
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
 out()<<"instances: "<<g_checks-g_failures<<"/"<<g_checks<<"\n"; return g_failures==0 && moduleStatus==0 ? 0 : 1;
}

