#include "Support.hpp"
#include <type_traits>
int main(int argc, char **argv) {
 QApplication app(argc, argv);
    Q_INIT_RESOURCE(icons);
 std::setvbuf(stdout, nullptr, _IONBF, 0);
 Anim::setEnabled(false);
 app.setStyle(Theme::createStyle());
 const int result = []<typename F>(F fn, QApplication &application) {
  if constexpr (std::is_invocable_r_v<int, F, QApplication &>) return fn(application);
  else return fn();
 }(WTFML_MODULE_ENTRY, app);
 std::printf("\nModule assertions: %d passed, %d failed\n", g_pass, g_fail);
 return result != 0 ? result : (g_fail == 0 ? 0 : 1);
}

