#include <QApplication>

#include "MainWindow.hpp"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // Configure QSettings to use proper registry/config paths
    app.setApplicationName("WTFML");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("WTFML");

    MainWindow window;
    window.show();
    return app.exec();
}
