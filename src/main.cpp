/**
 * @file main.cpp
 * @brief Entry point for the WTFML Minecraft launcher application.
 * @author LoserKugua
 * @date 2026-04-10
 * @note Sets up QApplication, application metadata, and shows the main window.
 */

#include <QApplication>
#include <QStyleFactory>

#include "MainWindow.hpp"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // 刻意不跟随系统原生样式，固定用 Fusion。
    //
    // 原因是原生样式（Windows 11 样式）和自定义外观互斥：只要给复杂控件（QComboBox、
    // QScrollBar 这类）写上一段 QSS，原生绘制就整体让位给样式表 —— 只写了一半的地方
    // 会露出残破外观，控制台还会刷警告。想自己定义外观，就得先离开原生样式。
    //
    // 现在换的代价只有一行：全仓的 QSS 目前只有 MainWindowUI.cpp 里那一处头像底色。
    // 等界面铺开、间距和控件高度都按原生样式的度量手调好之后再换，那些数值就得整体重估。
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    // Configure QSettings to use proper registry/config paths
    app.setApplicationName("WTFML");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("WTFML");

    MainWindow window;
    window.show();
    return app.exec();
}
