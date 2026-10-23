#include "Support.hpp"
int main(int argc, char **argv) {
 QCoreApplication app(argc, argv);
 int moduleStatus = 0;
 app.setOrganizationName("WTFML"); app.setApplicationName("WTFML");
 QTemporaryDir fixture; if (!fixture.isValid()) return 2;
 QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,fixture.path());
 const QString sandbox=fixture.filePath(".minecraft"); QDir().mkpath(sandbox); AppPaths::setMinecraftDir(sandbox);
 const QString metaDir=QStringLiteral(REPO_ROOT_PATH)+"/tests/meta";
    runModpackImportTests(sandbox);
    runCurseForgeTests();
    runModpackCompletionTests(sandbox);
    runResourceCatalogTests();
    runResourceDetailTests();
 out()<<"resources: "<<g_checks-g_failures<<"/"<<g_checks<<"\n"; return g_failures==0 && moduleStatus==0 ? 0 : 1;
}

