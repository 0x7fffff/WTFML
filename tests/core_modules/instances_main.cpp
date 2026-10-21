#include "Support.hpp"
int main(int argc, char **argv) {
 QCoreApplication app(argc, argv);
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

