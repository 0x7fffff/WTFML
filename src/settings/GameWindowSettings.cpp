/**
 * @file GameWindowSettings.cpp
 * @brief 实现游戏窗口那一组设置的合成、读写与命令行拼接。
 * @note 见 GameWindowSettings.hpp。这一份里全是纯函数，唯一有副作用的是
 * maximizedContentSize()（它要起一个看不见的窗口量一次边框）。
 */

#include "GameWindowSettings.hpp"

#include "LauncherSettings.hpp"  // 分辨率的取值范围（唯一出处在那里）

#include <QCoreApplication>
#include <QJsonValue>

// 只有**链了 Widgets 的那几份**才编得出探测窗口那一段：verify 那个工程是纯 Core 的
// （它是"喂纯值、断纯函数"的那一档，一个窗口都不开），在那儿 QWidget 这个头根本不存在。
// 所以这一段按 `QT_WIDGETS_LIB` 编 —— 那是 Qt6Widgets 这个 CMake 目标自己带的编译宏
// （QWidget 那套头文件里也是拿它认的），链了 Widgets 才有。
//
// 没有它的那一份里 maximizedContentSize() 退回"量不出来"那条路（原样返回工作区），
// 而那正是**它的行为在那种进程里本来就该是的样子**（那时连 QApplication 都没有，下面
// 那一段的运行时判据也过不去）。两条路不是两份实现：能跑的那一份走的还是同一个分支。
//
// NOTE: 别改用 `QT_CONFIG(widgets)` —— 那个宏展开成 `1/QT_FEATURE_widgets`，而
// `QT_FEATURE_widgets` 定义在 QtWidgets 自己的那份 config 里，得先 include 一个
// Widgets 头才看得见（于是它判不出"这个工程链没链 Widgets"，只会报"division by
// zero in #if"）。
#if defined(QT_WIDGETS_LIB)
#include <QApplication>
#include <QEventLoop>  // QEventLoop::ExcludeUserInputEvents
#include <QScreen>
#include <QWidget>
#endif
namespace {

// meta 里那三个键（见 .hpp 里为什么读侧写侧要共用）。
static constexpr const char kModeField[] = "windowMode";
static constexpr const char kWidthField[] = "windowWidth";
static constexpr const char kHeightField[] = "windowHeight";

// 盘上那个串（持久化格式的一部分，见 .hpp）。
static constexpr const char kTokenWindowed[] = "windowed";
static constexpr const char kTokenMaximized[] = "maximized";
static constexpr const char kTokenFullscreen[] = "fullscreen";

/// 分辨率钳位。宽度和高度用的是**全局那两格的同一套界**（LauncherSettings 那四个函数是
/// 唯一出处 —— 界面那份拷贝有一条断言盯着它们相等）。
int clampWidth(int width) {
    return qBound(LauncherSettings::gameWindowMinWidth(), width,
                  LauncherSettings::gameWindowMaxWidth());
}

int clampHeight(int height) {
    return qBound(LauncherSettings::gameWindowMinHeight(), height,
                  LauncherSettings::gameWindowMaxHeight());
}

}  // namespace

GameWindowSettings resolveGameWindow(const GameWindowSettings &global,
                                     const GameWindowOverride &override) {
    GameWindowSettings out;
    out.mode = override.mode.value_or(global.mode);
    // 钳位放在**合成之后**：全局那份自己在 LauncherSettings 里已经钳过一遍了，这里再钳
    // 一次是为了覆盖那一半（meta 是手写的文件），而且"两个来源都过同一道界"比"只有一半
    // 过"更好讲清楚。见 .hpp 里那条"和 Xmx 故意不同"。
    out.width = clampWidth(override.width.value_or(global.width));
    out.height = clampHeight(override.height.value_or(global.height));
    return out;
}

GameWindowOverride gameWindowOverrideFromJson(const QJsonObject &meta) {
    GameWindowOverride out;

    // 模式：串认不出来就当没写（不猜）。**注意这里不看大小写**也不是刻意宽容 ——
    // 盘上这个串是我们自己写下去的，认不出来只有两种可能：手改过、或者是字段名改过
    // 之后留下的值。两种都该退回"跟随全局"。
    const QJsonValue modeValue = meta.value(QLatin1String(kModeField));
    if (modeValue.isString()) {
        GameWindowMode parsed = GameWindowMode::Windowed;
        if (gameWindowModeFromToken(modeValue.toString(), &parsed)) {
            out.mode = parsed;
        }
    }

    // 两个数：`isDouble()` 挡住字符串和 null；非整数（比如 1920.5）当没写 —— 分辨率没有
    // 半个像素这回事，四舍五入进来的是别人猜的一个值（同 javaOverrideFromJson 的态度）。
    const QJsonValue widthValue = meta.value(QLatin1String(kWidthField));
    if (widthValue.isDouble()) {
        const double raw = widthValue.toDouble();
        if (raw == static_cast<double>(static_cast<int>(raw))) {
            out.width = static_cast<int>(raw);
        }
    }
    const QJsonValue heightValue = meta.value(QLatin1String(kHeightField));
    if (heightValue.isDouble()) {
        const double raw = heightValue.toDouble();
        if (raw == static_cast<double>(static_cast<int>(raw))) {
            out.height = static_cast<int>(raw);
        }
    }
    return out;
}

QJsonObject gameWindowOverrideToJson(const GameWindowOverride &override) {
    QJsonObject out;
    // 没有的那几项写 Null：updateVersionMeta() 把 Null 解释成"删掉这个键"，正是「跟随全局」
    // 要的结果。**不要**在这儿判"和全局一样就别写"—— 用户点的是「跟随全局」，那是一个
    // 意图（以后改全局要跟着走），不是"现在这两个数正好相等"。
    out.insert(QLatin1String(kModeField),
               override.mode.has_value() ? QJsonValue(gameWindowModeToken(*override.mode))
                                         : QJsonValue(QJsonValue::Null));
    out.insert(QLatin1String(kWidthField),
               override.width.has_value() ? QJsonValue(*override.width)
                                          : QJsonValue(QJsonValue::Null));
    out.insert(QLatin1String(kHeightField),
               override.height.has_value() ? QJsonValue(*override.height)
                                           : QJsonValue(QJsonValue::Null));
    return out;
}

QString gameWindowModeMetaField() { return QString::fromLatin1(kModeField); }

QString gameWindowWidthMetaField() { return QString::fromLatin1(kWidthField); }

QString gameWindowHeightMetaField() { return QString::fromLatin1(kHeightField); }

QString gameWindowModeToken(GameWindowMode mode) {
    switch (mode) {
        case GameWindowMode::Maximized:
            return QString::fromLatin1(kTokenMaximized);
        case GameWindowMode::Fullscreen:
            return QString::fromLatin1(kTokenFullscreen);
        case GameWindowMode::Windowed:
            break;
    }
    return QString::fromLatin1(kTokenWindowed);
}

bool gameWindowModeFromToken(const QString &token, GameWindowMode *out) {
    if (out == nullptr) {
        return false;
    }
    if (token == QLatin1String(kTokenWindowed)) {
        *out = GameWindowMode::Windowed;
        return true;
    }
    if (token == QLatin1String(kTokenMaximized)) {
        *out = GameWindowMode::Maximized;
        return true;
    }
    if (token == QLatin1String(kTokenFullscreen)) {
        *out = GameWindowMode::Fullscreen;
        return true;
    }
    return false;
}

QStringList gameWindowArguments(const GameWindowSettings &settings, const ScreenWorkArea &screen) {
    QStringList args;

    // 全屏那一档先放 `--fullscreen`：它是"这一趟要在全屏里"，和后面那两个数是两件事。
    if (settings.mode == GameWindowMode::Fullscreen) {
        args << QStringLiteral("--fullscreen");
    }

    int width = settings.width;
    int height = settings.height;
    if (settings.mode == GameWindowMode::Maximized) {
        // 拿得到屏幕就用屏幕算出来的那一份；拿不到就退回用户那两格 —— 总比什么都不发、
        // 让游戏按它自己的 854×480 开一个窗口强（用户点的是"最大化"，眼前却是一个小窗）。
        const ScreenWorkArea maximized = maximizedContentSize(screen);
        if (maximized.width > 0 && maximized.height > 0) {
            width = maximized.width;
            height = maximized.height;
        }
    }

    // 两格不是正数就一条都不发（规则）。全屏那一档**照样发**这两个数 —— 见 .hpp 里
    // 「按 F11 退出全屏之后回到多大」那一段。
    if (width > 0 && height > 0) {
        args << QStringLiteral("--width") << QString::number(width) << QStringLiteral("--height")
             << QString::number(height);
    }
    return args;
}

ScreenWorkArea maximizedContentSize(const ScreenWorkArea &workArea) {
    if (workArea.width <= 0 || workArea.height <= 0) {
        return workArea;
    }

    // 量一次就够：同一块屏幕、同一个缩放，边框尺寸不会变（用户中途改缩放的话，那也得
    // 下次启动才看得出来 —— 而"下次启动"本来就是重新量一次的时机）。
    static ScreenWorkArea cachedScreen;
    static ScreenWorkArea cachedContent;
    if (cachedScreen.width == workArea.width && cachedScreen.height == workArea.height) {
        return cachedContent;
    }

    ScreenWorkArea content = workArea;
    // 起一个**真的**窗口问一次边框 —— 不能从样式里读常数（Windows 那个标题栏是 DWM 算的，
    // 和样式表里写的高对不上），也不能写死 30 像素（DPI 一变就错）。离屏平台下这个探测
    // 窗口拿不到边框（量出来是 0），正好等于"量不出来就原样返回"。
    //
    // NOTE: 这就是本文件唯一的副作用。它凭什么可以留在这儿：量出来的结果**只**用于
    // 「最大化」那一档，而且量不出来时是"退化成工作区原样"，不会把谁弄崩。
#if defined(QT_WIDGETS_LIB)
    // NOTE: 判的是"真的有一个 QApplication"（不是 `instance() != nullptr` —— 那一句在一个
    // 只有 QCoreApplication 的进程里也成立，而在那种进程里造 QWidget 会直接崩）。
    if (qobject_cast<QApplication *>(QCoreApplication::instance()) != nullptr) {
        QWidget probe;
        probe.setWindowFlags(Qt::Window);
        // 不透明：一个透明窗口在 Windows 上可能拿不到真正的装饰尺寸（分层窗口那条路不一样）。
        probe.resize(200, 120);
        // 摆到桌面外面去：量边框**不影响窗口在哪儿**，而摆在屏幕里会让用户看见一个白框闪一下。
        probe.move(-32000, -32000);
        probe.show();
        // 只转事件循环、**不收用户输入**。这一句是在拼启动参数的最后一步里跑的，
        // 一次 `processEvents()` 会把此刻积在队列里的鼠标/键盘事件派发出去 —— 用户正好在点
        // 「启动」时按下的东西（比如再点一下别的按钮、或者勾/取消勾一个选项）会插进这一趟
        // 正在拼的命令行中间。量边框只需要窗口被真正创建出来，和用户输入无关。
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        const QSize frame = probe.frameGeometry().size() - probe.size();
        probe.hide();
        if (frame.width() > 0 && frame.height() > 0 &&
            workArea.width - frame.width() > 0 && workArea.height - frame.height() > 0) {
            content.width = workArea.width - frame.width();
            content.height = workArea.height - frame.height();
        }
    }
#endif  // QT_WIDGETS_LIB

    cachedScreen = workArea;
    cachedContent = content;
    return content;
}
