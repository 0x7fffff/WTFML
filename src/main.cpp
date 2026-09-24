/**
 * @file main.cpp
 * @brief Entry point for the WTFML Minecraft launcher application.
 * @author LoserKugua
 * @date 2026-04-10
 * @note Sets up QApplication, application metadata, and shows the main window.
 */

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStyleFactory>
#include <QTextStream>

#include "MainWindow.hpp"
#include "util/AppPaths.hpp"

namespace {

/// 这一次启动的日志文件。**它开没开就是唯一那份状态**：没开 = 还早，先攒着（见下面那段
/// NOTE）；开了 = 直接写进去。不另立一个 bool —— 两份状态迟早会不一致。
QFile g_logFile;
QTextStream g_logStream;
/// 文件还没开时先攒下的那些行（openFileLog() 时一起补进去）。
QStringList g_pendingLog;

/// 把 Qt 的日志（qDebug / qWarning；产品里所有 [WTFML] 那句话、以及游戏自己的输出
/// 都从这里过，见 LauncherManager）落一份到盘上，同时照旧往 stderr 写一份。
///
/// 【为什么要它】
///
/// 这个 exe 从 2026-09-23 起是 **GUI 子系统**（见 CMakeLists 里那两行 set_target_properties）：
/// 双击启动时不再弹那个黑框了 —— 那正是用户报上来的话（「运行的时候弹了一串消息」）。
/// 代价是"那些话没人看得见"，所以要有个地方补上：出问题的时候能查。
///
/// 落在 `<数据目录>/logs/wtfml.log`，**每次启动截断重写**：和 Minecraft 自己的
/// `logs/latest.log` 一个习惯（上次那份不保留），也就和它并排放在一个目录里 ——
/// "启动器说了什么"和"游戏说了什么"翻起来都在那儿。
///
/// NOTE: **第一条消息来得比"当前游戏文件夹"还早** —— 最早那几句是 QApplication 构造时
///       字体库打的（实测：`QFontDatabase: Cannot find font directory ...`），那时
///        FolderManager 连构造都还没轮到。所以这里只接消息、把行**攒着**，等窗口立起来
///       （main() 里 show() 之后那一下）再开文件、把攒下的一起补进去。早开的话路径会按
///       默认那一份算，于是"日志落在 exe 旁边、数据在别处"—— 那就成了两处东西。
/// NOTE: 中途换游戏文件夹不会把这个文件搬走：它记的是**这一次启动**，不是"这一份
///       .minecraft 的日志"（这一次启动写在哪，就在哪）。
/// NOTE: 文件写不进去（目录不可写之类）只是不写 —— 日志这件事绝不能影响启动。
/// NOTE: stderr 那一份照旧写。产品里所有验证工程都是"跑 exe、看 stderr"，而且装了自定义
///       handler 之后 Qt 那套"没有控制台就不往 stderr 写"的判断本来就不起作用了 ——
///       所以这里自己写，验证工程连 QT_FORCE_STDERR_LOGGING 都不必再依赖。
void installFileLog() {
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &context,
                              const QString &text) {
        const QString line = qFormatLogMessage(type, context, text);
        if (g_logFile.isOpen()) {
            // 进程结束时由系统收这个句柄 —— 这里没有可挂的析构点，而且最后那几行往往
            // 正是"要退出"那些话，早收反而丢东西。
            g_logStream << line << Qt::endl;
            g_logStream.flush();
        } else {
            g_pendingLog.append(line);
        }
        const QByteArray raw = line.toUtf8();
        std::fprintf(stderr, "%s\n", raw.constData());
        std::fflush(stderr);
    });
}

/// 现在可以把日志写进盘上了：建目录、开文件、把之前攒下的行一起补进去。
/// 调用点见 main()（排在窗口 show() 之后 —— 那一刻"当前游戏文件夹"才是定下来的）。
void openFileLog() {
    const QString dir = AppPaths::minecraftDir() + QStringLiteral("/logs");
    QDir().mkpath(dir);
    g_logFile.setFileName(dir + QStringLiteral("/wtfml.log"));
    // 开不了就只是不写（见 installFileLog 的 NOTE）；攒下的行照样丢掉，不再越攒越多。
    if (g_logFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        g_logStream.setDevice(&g_logFile);
        for (const QString &line : g_pendingLog) {
            g_logStream << line << Qt::endl;
        }
        g_logStream.flush();
    }
    g_pendingLog.clear();
}

} // namespace

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    installFileLog();

    // 刻意不跟随系统原生样式，固定用 Fusion。
    //
    // 原因是原生样式（Windows 11 样式）和自定义外观互斥：只要给复杂控件（QComboBox、
    // QScrollBar 这类）写上一段 QSS，原生绘制就整体让位给样式表 —— 只写了一半的地方
    // 会露出残破外观，控制台还会刷警告。想自己定义外观，就得先离开原生样式。
    //
    // 现在换的代价只有一行：全仓的 QSS 只有两段、三次应用（都在 MainWindowUI.cpp 里）——
    // 头像底色一处，底栏那两颗按钮的 hover / pressed 一处（applyBottomBarButtonStyle()，
    // 被两颗按钮各调一次）。等界面铺开、间距和控件高度都按原生样式的度量手调好之后再换，
    // 那些数值就得整体重估。
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    // Configure QSettings to use proper registry/config paths
    app.setApplicationName("WTFML");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("WTFML");

    MainWindow window;
    window.show();

    // 日志文件到这一刻才开：窗口立起来 = FolderManager 已经把「当前游戏文件夹」从
    // QSettings 里恢复好了，落点才是对的（见 installFileLog 的 NOTE）。
    openFileLog();

    return app.exec();
}
