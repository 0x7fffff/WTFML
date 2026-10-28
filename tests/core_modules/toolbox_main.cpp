#include "Support.hpp"
int main(int argc, char **argv) {
 QCoreApplication app(argc, argv);
 int moduleStatus = 0;
 app.setOrganizationName("WTFML"); app.setApplicationName("WTFML");
 QTemporaryDir fixture; if (!fixture.isValid()) return 2;
 QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,fixture.path());
 const QString sandbox=fixture.filePath(".minecraft"); QDir().mkpath(sandbox); AppPaths::setMinecraftDir(sandbox);
 app.setProperty("wtfml.testDefaultMinecraftDir", sandbox);
 const QString metaDir=QStringLiteral(REPO_ROOT_PATH)+"/tests/meta";
    runToolboxExtraTests();
    runTextFormatTests();
    runCoordConvertTests();
    runFileHashTests();
    runAnvilPlannerTests();
    runSchematicInfoTests();
    runReplayInfoTests();
 out()<<"toolbox: "<<g_checks-g_failures<<"/"<<g_checks<<"\n"; return g_failures==0 && moduleStatus==0 ? 0 : 1;
}

