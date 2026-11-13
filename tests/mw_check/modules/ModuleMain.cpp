#include "JavaManageChecks.hpp"
#include "ModActionPanelChecks.hpp"
#include "RangeChecks.hpp"
#include "SegmentInstallChecks.hpp"
#include "ToolFeatureChecks.hpp"
#include "PresetInstallChecks.hpp"
#include "InstanceUpgradeChecks.hpp"
#include "Support.hpp"
#include "support/SettingsIsolation.hpp"
#include <type_traits>
#include "RangeChecks.hpp"
int main(int argc, char **argv) {
 QApplication app(argc, argv);
 // A private fixture marker is required before this executable can impersonate Java.
 // The real installer controller still launches/kills/retries an OS process.
 if (app.arguments().value(1) == "-jar") {
  const auto jar = app.arguments().value(2);
  const auto root = QFileInfo(jar).absolutePath();
  QFile marker(root + "/.watchdog-fixture.json");
  if (!marker.open(QIODevice::ReadOnly) || marker.size() > 4096) return 3;
  const auto fixture = QJsonDocument::fromJson(marker.readAll()).object();
  if (fixture.value("format") != "wtfml-installer-watchdog-test/1" || fixture.value("root") != root) return 3;
  QFile counter(root + "/attempts"); int attempts = 0;
  if (counter.open(QIODevice::ReadOnly)) { attempts = counter.readAll().toInt(); counter.close(); }
  if (!counter.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 3;
  counter.write(QByteArray::number(attempts + 1)); counter.close();
  if (fixture.value("mode") == "retry-success" && attempts > 0) {
   const auto args = root + "/libraries/net/minecraftforge/forge/1.20.1-47.2.0/win_args.txt";
   QDir().mkpath(QFileInfo(args).absolutePath()); QFile file(args);
   QFile jvm(root + "/user_jvm_args.txt");
   return file.open(QIODevice::WriteOnly) && file.write("fixture") == 7 && jvm.open(QIODevice::WriteOnly) ? 0 : 3;
  }
  QTimer output;
  if (fixture.value("mode") == "noisy") {
   QObject::connect(&output, &QTimer::timeout, &app, [] { std::printf("fixture progress\n"); std::fflush(stdout); });
   output.start(30);
  }
  QTimer::singleShot(10000, &app, &QCoreApplication::quit);
  return app.exec();
 }
    Q_INIT_RESOURCE(icons);
 std::setvbuf(stdout, nullptr, _IONBF, 0);
 QString sandboxParent = QCoreApplication::applicationDirPath();
#ifdef WTFML_RANGE_MODULE
 // 断点夹具会故意 _Exit；子进程沙盒交给父夹具统一回收。
 if (app.arguments().contains(QStringLiteral("range-resume-child"))) sandboxParent = QDir::currentPath();
#endif
 QTemporaryDir processSandbox(sandboxParent + QStringLiteral("/mw-module-XXXXXX"));
 if (!processSandbox.isValid()) return 2;
 app.setProperty("wtfml.testDefaultMinecraftDir", processSandbox.filePath(QStringLiteral("minecraft")));
 app.setProperty("wtfml.testAgentDataDir", processSandbox.filePath(QStringLiteral("agent")));
 AppPaths::setMinecraftDir(QString());
 app.setOrganizationName(QStringLiteral("WTFML-check"));
 app.setApplicationName(QStringLiteral("mw-module"));
 QSettings::setDefaultFormat(QSettings::IniFormat);
 QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                    processSandbox.filePath(QStringLiteral("settings")));
 requireIsolatedSettings(processSandbox.path(), "mw_check module");
 check(AppPaths::minecraftDir() == processSandbox.filePath(QStringLiteral("minecraft")) &&
           AppPaths::defaultMinecraftDir() != QCoreApplication::applicationDirPath() + QStringLiteral("/.minecraft"),
       "模块默认数据根使用独立沙盒，不落到 exe 旁的共享目录");
 {
  QSettings settings;
  check(QDir::cleanPath(settings.fileName()).startsWith(QDir::cleanPath(processSandbox.path()) + QLatin1Char('/')),
        "模块默认设置文件位于本进程沙盒");
 }
 if (app.arguments().contains(QStringLiteral("sandbox-probe"))) {
  QSettings settings;
  const bool inheritedParent = settings.contains(QStringLiteral("Sandbox/ParentSentinel"));
  settings.setValue(QStringLiteral("Sandbox/ChildSentinel"), true);
  settings.sync();
  const auto report = QJsonDocument(QJsonObject{
      {QStringLiteral("root"), processSandbox.path()},
      {QStringLiteral("dataRoot"), AppPaths::defaultMinecraftDir()},
      {QStringLiteral("settingsFile"), settings.fileName()},
      {QStringLiteral("inheritedParent"), inheritedParent},
      {QStringLiteral("saved"), settings.status() == QSettings::NoError}}).toJson(QJsonDocument::Compact);
  std::printf("SANDBOX_PROBE:%s\n", report.constData());
  return settings.status() == QSettings::NoError ? 0 : 2;
 }
#ifdef WTFML_RANGE_MODULE
 // 子进程同样先隔离默认账本/设置；下载目标和续传缓存仍由父进程显式传入。
 if (app.arguments().contains(QStringLiteral("range-resume-child"))) return runRangeChild(app.arguments());
#endif
 Anim::setEnabled(false);
 app.setStyle(Theme::createStyle());
 const auto invoke = []<typename F>(F fn, QApplication &application) {
  if constexpr (std::is_invocable_r_v<int, F, QApplication &>) return fn(application);
  else return fn();
 };
#ifdef WTFML_COMPACT_RUNNER
 const QString name = app.arguments().value(1);
 const int result = [&]() -> int {
#include "CompactEntries.inc"
  std::fprintf(stderr, "Unknown module: %s\n", name.toUtf8().constData());
  return 2;
 }();
#else
 const int result = invoke(WTFML_MODULE_ENTRY, app);
#endif
 std::printf("\nModule assertions: %d passed, %d failed\n", g_pass, g_fail);
 return result != 0 ? result : (g_fail == 0 ? 0 : 1);
}

