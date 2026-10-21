/**
 * @file GameWindowSettings.hpp
 * @brief 游戏窗口那一组设置：窗口模式（小窗 / 最大化 / 全屏）+ 分辨率，以及"这一次启动到底
 * 发哪几个参数"那一个纯函数。
 * @note 这一层和 EffectiveJavaSettings 是一对：**全局那一份**（LauncherSettings 里那三个键）
 * 加**实例那一份**（wtfml.meta 里的三个键）合成出"这一次用哪一档"，再由
 * gameWindowArguments() 变成命令行上的那几个参数。三层各自是纯的，所以验证工程
 * 可以直接喂值。
 */

#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

/**
 * @brief 游戏窗口的模式。
 *
 * 三档的来源是要求「游戏的启动参数 小窗/最大化/全屏 以及游戏分辨率 放到设置和实例
 * 设置里面」。
 *
 * **三档在游戏那一侧完全不是一回事**，这一节每一档都要说清，因为它们能发的参数不一样：
 * - `Windowed`：**只发** `--width` / `--height`，游戏按这个大小开一个普通窗口。
 * - `Maximized`：原版**没有**"最大化"这个参数（游戏自己不会去调窗口管理器 —— 那个
 * "最大化"是窗口的**状态**，只有操作系统能给）。所以这一档是**照屏幕工作区算一个
 * 大小**，再当 `--width` / `--height` 发下去：屏幕上看起来就是一个铺满工作区的窗口，
 * 标题栏还在、也能拖。见 maximizedContentSize() 里那条"为什么还要减掉边框"。
 * - `Fullscreen`：发原版的 `--fullscreen`。这一位是**真的**，不是我们编的：本地三份
 * 客户端（1.12.2 / 1.18.2 / 1.20.1）的 `net/minecraft/client/main/Main.class` 里都有
 * 这个选项名（1.18.2 和 1.20.1 那两份还多一对 `fullscreenWidth` / `fullscreenHeight`）。
 * 这一档**照样把 `--width` / `--height` 一起发下去** —— 那是"按 F11 退出全屏之后回到
 * 多大"的那个大小（游戏自己的窗口对象记的就是这一对）。
 *
 * NOTE: 启动器**不替游戏写 options.txt**。游戏那份文件里也有一个 `fullscreen`，它和命令行
 * 谁说了算**是游戏自己的事**，界面和文档都不许把"发出去的参数"说成"游戏最后的样子"。
 */
enum class GameWindowMode {
    Windowed,
    Maximized,
    Fullscreen,
};

/// 这一次要用的那一组值（全局 + 实例覆盖合成之后的结果）。
struct GameWindowSettings {
    GameWindowMode mode = GameWindowMode::Windowed;
    /// 分辨率（像素）。默认的 854×480 就是原版的默认窗口大小 —— 没动过这一项时和
    /// 加这个功能之前一模一样。
    int width = 854;
    int height = 480;
};

/**
 * @brief 实例那一层的覆盖（wtfml.meta 里那三个键）。
 *
 * 每一项都可以没有（= 那一项跟随全局），照 JavaSettingsOverride 的规矩：一个实例一个键
 * 都不用覆盖时，合成出来的值和没有覆盖层时**逐字节一致**。
 */
struct GameWindowOverride {
    std::optional<GameWindowMode> mode;
    std::optional<int> width;
    std::optional<int> height;
};

/**
 * @brief 屏幕工作区（像素）。**只有「最大化」那一档用得上。**
 *
 * <= 0 = 调用方没给（验证工程里那些纯值的构造点、或者这台机器上没有屏幕）—— 那一档
 * 这时退回用户/全局设的那两格（照发一个普通窗口大小），而不是发一个 0×0 出去。
 */
struct ScreenWorkArea {
    int width = 0;
    int height = 0;
};

/// 全局 + 覆盖 → 这一次用哪一档。合成规矩和 resolveJavaSettings() 一样：覆盖里有的用覆盖，
/// 没有的用全局。
///
/// 覆盖里的数**要钳位**（钳进 LauncherSettings 那四个界里）。这一点和 Xmx 那条"覆盖
/// 里的数没有上限"**故意不同**：这两个数会**原样进命令行**，而 wtfml.meta 是用户手写的
/// 文件 —— 写一个负数进去，对着的就是一个坏掉的参数（而不是"用户自己选的一个怪值"）。
GameWindowSettings resolveGameWindow(const GameWindowSettings &global,
                                     const GameWindowOverride &override);

/// 从 wtfml.meta 的对象里读出覆盖层。字段名：windowMode / windowWidth / windowHeight。
///
/// 类型不对或者串认不出来的项**当没有**（= 跟随全局），和 javaOverrideFromJson() 的态度
/// 一致：文件是用户手写的，写错当它没写，不猜。
GameWindowOverride gameWindowOverrideFromJson(const QJsonObject &meta);

/// 覆盖层 → 要写进 meta 的那批字段。没有的那几项写成 QJsonValue::Null（= 删键），
/// 正好是用户点「跟随全局」时要的结果，可以直接交给 updateVersionMeta()。
QJsonObject gameWindowOverrideToJson(const GameWindowOverride &override);

/// meta 里那三个键的名字。**读侧和写侧共用这一个出处** —— 两边各写一遍字面量的错法是
/// 静默不生效（键名对不上不报错，覆盖就是没写进去，而界面上看起来一切正常）。
QString gameWindowModeMetaField();
QString gameWindowWidthMetaField();
QString gameWindowHeightMetaField();

/// 模式 → 存进盘里的那个串。这几个串是**持久化格式的一部分**，不是内部标识：改掉它们，
/// 用户盘上已经存好的档位就会被当成"认不出来"、静默退回默认档。
QString gameWindowModeToken(GameWindowMode mode);

/// 串 → 模式。认不出来返回 false，并且**不碰** @p out（调用方自己给兜底值）。
bool gameWindowModeFromToken(const QString &token, GameWindowMode *out);

/// 这一次启动要发的那几个参数（原样可以拼进命令行）。
///
/// 顺序：`--fullscreen`（全屏那一档才有）在前，然后 `--width W --height H`。两格不是正数
/// 时**一条都不发**（老规矩：宁可不发，也不发一个 0 出去）。
QStringList gameWindowArguments(const GameWindowSettings &settings, const ScreenWorkArea &screen);

/**
 * @brief 「最大化」那一档实际要发给游戏的**内容区**大小。
 *
 * = 屏幕工作区**减去窗口自己的标题栏和边框**。
 *
 * 为什么非减不可：`--width/--height` 给游戏的是**内容区**（GLFW 的
 * `glfwSetWindowSize` 就是这个语义），而一个"最大化"的窗口铺满的是**工作区**（含标题栏
 * 和外框）。不减的话，窗口的整体高度 = 工作区 + 标题栏，底下那一截就钻到任务栏下面去了
 * —— 而游戏的物品栏正好在底下那一截上。
 *
 * 边框是**量出来的**（临时起一个不透明的窗口问一次 `frameGeometry() - size()`，DPI 变了
 * 也是对的），不是写死的常数。量不出来时（离屏平台、没有窗口系统）原样返回工作区 ——
 * 那时候差几十像素已经不是用户能看见的事了。
 *
 * NOTE: 结果按工作区缓存（同一块屏幕的量法不会变），所以一次启动只有第一回真的起那个
 * 探测窗口，而且它是不透明的、只在 `show()` 到 `hide()` 之间存在几十微秒。
 */
ScreenWorkArea maximizedContentSize(const ScreenWorkArea &workArea);
