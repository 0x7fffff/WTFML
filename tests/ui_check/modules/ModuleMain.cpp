#include "Support.hpp"
#include "support/SettingsIsolation.hpp"
#include "util/AppPaths.hpp"
#include <type_traits>
int main(int argc, char **argv) {
 QApplication app(argc, argv);
 // 设置沙盒：这些「模块可执行文件」同样是独立进程，里面立的是真的 MainWindowUI。
 // 不在这里重定向的话 `QSettings()` 直接打到 HKCU\Software\WTFML\WTFML：读出来是这台机器
 // 的用户在启动器里挑的主题（断言跟着人走），写下去就是改用户的真设置。位置必须在任何
 // MainWindowUI / Theme 之前 —— 模块自己再 setPath 也改不了已经读过的那一份。
 QTemporaryDir processSandbox(QCoreApplication::applicationDirPath() + QStringLiteral("/ui-module-XXXXXX"));
 if (!processSandbox.isValid()) return 2;
 app.setProperty("wtfml.testDefaultMinecraftDir", processSandbox.filePath(QStringLiteral("minecraft")));
 app.setProperty("wtfml.testAgentDataDir", processSandbox.filePath(QStringLiteral("agent")));
 AppPaths::setMinecraftDir(QString());
 app.setOrganizationName(QStringLiteral("WTFML-check"));
 app.setApplicationName(QStringLiteral("ui-module"));
 QSettings::setDefaultFormat(QSettings::IniFormat);
 QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                    processSandbox.filePath(QStringLiteral("settings")));
 QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope,
                    processSandbox.filePath(QStringLiteral("system-settings")));
 requireIsolatedSettings(processSandbox.path(), "ui_check module");
 app.setFont(Theme::textFont(app.font(), Theme::TextRole::Body));
    Q_INIT_RESOURCE(icons);
 std::setvbuf(stdout, nullptr, _IONBF, 0);
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

