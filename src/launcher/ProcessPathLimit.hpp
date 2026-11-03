/**
 * @file ProcessPathLimit.hpp
 * @brief Windows 起进程时那条 260 字符的路径墙 —— 只负责说清楚，不负责拦。
 *
 * 【为什么有这一层】
 *
 * 启动器有四处要把一个 exe（或一个 jar 的宿主 java.exe）交给 `QProcess`：游戏启动、
 * 服务端启动、加载器安装器、服务端安装器。前两者还各要一条目录当那个子进程的
 * 工作目录。Windows 的 `CreateProcess` 对「可执行文件」和「当前目录」两个参数都只有
 * `MAX_PATH`（260 个字符，含结尾 NUL）：超了返回
 * `ERROR_PATH_NOT_FOUND` / `ERROR_BAD_PATHNAME` / `ERROR_FILENAME_EXCED_RANGE`，
 * 而 Qt 把它翻成一句 `Process failed to start: %1 …`。用户看到的要么是
 * 「Java可执行文件未找到」（`QFileInfo::exists()` 在没开长路径的机器上也看不穿它），
 * 要么是一句与本机毫不相干的系统话术 —— 两种都把「路径太长」这个真因藏起来了。
 *
 * 【实测（2026-11，见 docs/TESTING.md「长路径」那一节）】
 *
 * 本机 `HKLM\SYSTEM\CurrentControlSet\Control\FileSystem\LongPathsEnabled=0`，
 * 工程自己也**不许**改注册表去冒充支持，所以下面这些是这台机器上的事实：
 * - `CreateFileW` / `GetFileAttributesW` 带 `\\?\` 前缀能打开 > 260 的路径；
 * - `CreateProcessW` 起 > 260 的 exe：裸路径、加 `\\?\`（161 `/` 206）、只给命令行
 *   —— 全部失败；
 * - `SetCurrentDirectoryW("\\?\…")` 失败（206），用相对路径起进程也不行；
 * - 这块卷没有 8.3 短名（`GetShortPathNameW` 原样返回长路径），「换成短名再起」
 *   这条退路也不存在。
 *
 * 所以这里不做「我替你换一条路」：做不到，而且把 `\\?\` 塞给 `QProcess` 只会把一个
 * 本来能起的进程弄坏。这里只在**启动真的失败之后**补一句人话。判断是纯的、可单测的，
 * 而且**永远不会拦住一个本来能起的进程** —— 这正是它和「预检」的区别：换一台
 * `LongPathsEnabled=1` 的机器，超长路径也许起得来，预检会误杀。
 */

#pragma once

#include <QString>
#include <QStringList>

namespace ProcessPathLimit {

/// `CreateProcess` 能吃下的路径长度上限（含结尾 NUL）。字符数必须**小于**它。
inline constexpr int kMaxPathChars = 260;

/// 这条路径在起进程时可能撞上 260 的上限吗？空串不算。
bool tooLong(const QString &path);

/// `paths` 里最长的那条超限时给一句可操作的话；一条都不超限返回空串。
///
/// 只用于**启动失败之后**追加在错误信息后面，不做预检（见文件头最后一段）。
/// 挑最长的那条而不是第一条：一条命令里程序和目录同时超限时，用户该改的是更深的那个。
QString tooLongHint(const QStringList &paths);

}  // namespace ProcessPathLimit
