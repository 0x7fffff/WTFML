#include "Support.hpp"
#include "util/DirectorySessionLock.hpp"
#include <cstdio>
#include <QThread>
int main(int argc, char **argv) {
 QCoreApplication app(argc, argv);
 if (app.arguments().size() >= 3 && app.arguments().at(1) == QStringLiteral("--directory-lock-probe")) {
     DirectorySessionLock::enabled = true;
     if (!DirectorySessionLock::acquire(app.arguments().at(2))) return 73;
     if (app.arguments().contains(QStringLiteral("--hold"))) {
         std::puts("LOCKED"); std::fflush(stdout);
         QThread::sleep(60); // Parent kills this fixture; bounded even if the parent dies.
     }
     return 0;
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
 out()<<"instances: "<<g_checks-g_failures<<"/"<<g_checks<<"\n"; return g_failures==0 && moduleStatus==0 ? 0 : 1;
}

