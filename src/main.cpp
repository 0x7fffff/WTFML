/**
 * @file main.cpp
 * @brief Entry point for the WTFML Minecraft launcher application.
 * @author LoserKugua
 * @date 2026-04-10
 * @note Sets up QApplication, application metadata, and shows the main window.
 */

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
