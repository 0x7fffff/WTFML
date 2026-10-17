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
#include "../ui/Animations.hpp"  // 按下按钮那一份动效（见下面那段 NOTE）
#include "../ui/Theme.hpp"   // 主题从盘上读回来、铺开（见下面那段 NOTE）
#include "util/AppPaths.hpp"
#include "util/DownloadDiagnostics.hpp"

namespace {

/// 这一次启动的日志文件。**它开没开就是唯一那份状态**：没开 = 还早，先攒着（见下面那段
/// NOTE）；开了 = 直接写进去。不另立一个 bool —— 两份状态迟早会不一致。
QFile g_logFile;
QTextStream g_logStream;
/// 文件还没开时先攒下的那些行（openFileLog() 时一起补进去）。
QStringList g_pendingLog;
/// ★ 2026-09-27：那一次开文件**试过了、没开成**。它一旦为真，后面每一行就只走 stderr，
/// 不再往 g_pendingLog 里塞。
///
/// 原来这里没有这一位，只靠"g_logFile.isOpen() 为假就攒着"判断，于是 openFileLog()
/// 开文件失败之后（数据目录不可写、目录权限被改、杀软拦新建文件）**每一条日志都还在往
/// 那个表里塞**，而它再也没有第二处读 —— openFileLog() 里那句 clear() 只跑那一次，
/// 之后没人清了。开着日志窗跑一个模组包（几万行游戏输出，qDebug 走的就是这个 handler）
/// 就是几十 MB 只涨不落。那句「攒下的行照样丢掉，不再越攒越多」当时只对**开文件之前**
/// 攒的那批成立。
/// NOTE: 判据必须是"试过了"而不是"文件没开着" —— 这两件事在 openFileLog() 之前长得一样，
///       而那时候攒着是对的。
bool g_logFileGaveUp = false;

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
        } else if (!g_logFileGaveUp) {
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
    } else {
        // ★ 2026-09-27：**记下"试过了、没开成"**。不然 handler 那边分不清"还轮不到开文件"
        //   和"开了但失败了"，会把后面每一行继续往 g_pendingLog 里塞（见那一位的说明）。
        g_logFileGaveUp = true;
    }
    g_pendingLog.clear();
}

} // namespace

int main(int argc, char *argv[]) {
    // ★ 2026-09-27：**handler 排在 QApplication 构造之前**。上面那段 NOTE 说设计动机正是
    // 要留住"字体库那几句"（它们比当前游戏文件夹还早），而原来这一句写在 QApplication
    // 构造**之后** —— 那几句话发生在 handler 装上之前，既没进 g_pendingLog 也没进日志文件，
    // 于是"按 NOTE 去日志文件里查那句警告"永远查不到，而它正是设计时要留住的那几条。
    // NOTE: 这一步不需要 QApplication 已经存在（qInstallMessageHandler / qFormatLogMessage
    //       都是纯 QtCore 的东西），所以提前是安全的。
    installFileLog();

    QApplication app(argc, argv);

    // 刻意不跟随系统原生样式，固定用 Fusion。
    //
    // 原因是原生样式（Windows 11 样式）和自定义外观互斥：只要给复杂控件（QComboBox、
    // QScrollBar 这类）写上一段 QSS，原生绘制就整体让位给样式表 —— 只写了一半的地方
    // 会露出残破外观，控制台还会刷警告。想自己定义外观，就得先离开原生样式。
    //
    // NOTE: 这一段从前还写着"全仓的 QSS 只有两段、三次应用"——那句话早就不成立了（现在
    //       11 处，散在 ui/ 的五个文件里，`grep -rn setStyleSheet ui/` 是唯一出处）。
    //       换样式这件事**早就换过来了**，那几句"等界面铺开之后再换"的顾虑没有了。
    //
    // NOTE: 这也决定了主题那一层的取色方式：样式固定 Fusion 之后，`QStyle::standardPalette()`
    //       就是"这一档深浅下我们该有的那一份调色板"，`Theme::apply()` 拿它当基色
    //       （见 `ui/Theme.cpp`）。跟随原生样式的话，那份基色就是系统给的，改不动。
    // ★ 从 `Theme::createStyle()` 拿（= Fusion + 下拉框那个 `∨` 箭头）。基样式还是 Fusion，
    //   只是外面套了一层 QProxyStyle —— 换样式基线的时候四处（这里 + 三个验证工程）一起换。
    QApplication::setStyle(Theme::createStyle());

    // Configure QSettings to use proper registry/config paths
    app.setApplicationName("WTFML");
    // 版本号来自 CMakeLists.txt 的 `project(... VERSION ...)`（`WTFML_VERSION` 那个宏），
    // **不许在这儿写第二份字面量** —— 这两处长期对不上（CMakeLists 0.1.0 / 这儿 1.0.0）。
    app.setApplicationVersion(QStringLiteral(WTFML_VERSION));
    app.setOrganizationName("WTFML");

    // 主题（背景 / 字色 / 文字不透明度 / 语义色）从盘上读回来铺开。
    //
    // **必须在 `MainWindow` 构造之前**：界面里那些"建的时候取一次色"的东西（条底色、
    // 底栏底色、几张卡片、11 处 QSS）都是按**那一刻**的调色板算的 —— 铺晚了它们就全是
    // 出厂色，而且是那种"改了设置重启之后只有一半地方变了"的错。
    //
    // NOTE: 盘上一项都没有（新装、或者用户从没改过）时 `load()` 给的是出厂那一份，而
    //       出厂那一份 `apply()` **连调色板都不碰** —— 也就是"加这一层之前的样子"。
    Theme::setSpec(Theme::load());
    Theme::apply(app);

    // 动效那一层（coding plan §8.3 后半句）：
    //   · `load()` 读用户那个总开关（`Look/Animations`，**不在 `Theme/` 底下** ——
    //     `Theme::save()` 会把 `Theme` 这一整组清掉重写，开关放那儿会被顺手抹了）；
    //   · `refreshPlatformReducedMotion()` 问一次系统"动画效果"关没关（Windows 的辅助功能）。
    //     它**只在产品这条路上调**（验证工程不调：那些工程要在 offscreen 下量几何和像素，
    //     "这台机器的辅助功能是怎么设的"不该进判据）。
    Anim::load();
    Anim::refreshPlatformReducedMotion();

    // 动效那一层（2026-09-29，用户点名的那三样：按下按钮 / 切换页面 / 展开折叠）。
    //
    // 这一句是**按钮那一份动效的唯一接线点**：它往 `QApplication` 上挂一个事件过滤器，
    // 谁被按下去就给谁压暗一档（全仓 92 个 `new QPushButton` 一个字都不用改，见 ui/Animations.hpp）。
    // 换页和展开折叠不走这里：它们由真搭界面的那两处调（`Anim::pageAppear()` /
    // `Anim::animateExpansion()`）。
    //
    // NOTE: 验证工程（ui_check / mw_check / ui_shot）**刻意不调它** —— 那些工程要在
    //       offscreen 下量几何和像素，"半路飞进来一段动画"只会让判据时灵时不灵。
    Anim::installPressAnimation(app);
    // 另一份：**滚动条一露面就淡进来**（用户 2026-09-29：「每一次切页面的时候滚动条都是瞬间
    // 出现的」）。同样是往上挂一个事件过滤器，全仓每一处滚动区都不用改。
    Anim::installScrollBarAnimation(app);

    MainWindow window;
    window.show();

    // 日志文件到这一刻才开：窗口立起来 = FolderManager 已经把「当前游戏文件夹」从
    // QSettings 里恢复好了，落点才是对的（见 installFileLog 的 NOTE）。
    openFileLog();

    // 特供版（-DWTFML_DIAG_DOWNLOAD=ON）的下载明细：和 wtfml.log 同一个目录、同一份
    // "这一次启动"。普通构建里 enabled() 是编译期常量 false，这两句什么都不做。
    DownloadDiag::init(AppPaths::minecraftDir() + QStringLiteral("/logs"));
    DownloadDiag::logEnvironment();

    return app.exec();
}
