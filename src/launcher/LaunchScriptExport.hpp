/**
 * @file LaunchScriptExport.hpp
 * @brief 「导出启动脚本」写出去的那份 .bat 的**正文**：一串已经展开好的启动参数怎么变成
 * cmd 认的一段文本。
 *
 * 【为什么单独一个文件】
 *
 * 拼这段文本的规矩全是**看不见的**：哪些字符要加引号、`%` 为什么要写两遍、注释为什么在
 * 英文版 Windows 上要换成英文、正版的令牌为什么换成环境变量引用、以及"参数里还带着已知
 * 凭据就不许写"那条闸。这些规矩以前是 `MainWindow::exportLaunchScript()` 里的一段内联
 * 代码加一个文件内的 `static` 函数 —— 谁都跑不到它（要开对话框、要真令牌），错一个字节
 * 用户双击脚本时才会发现。
 *
 * 现在这一层只收"值进、文本出"：不碰盘、不问用户、不认数据目录（那个也由调用方给）。
 * 所以验证工程能逐条钉住它（见 `tests/core_modules/LaunchScriptExportTests.cpp`），
 * 主窗口那边只剩"凑齐这些值、把结果写进文件、说一句结果"。
 *
 * 【这一层不做什么】
 *
 * 不写文件（`QSaveFile` 和"写不进去怎么报"是调用方的事）、不选 Java、不下运行时、不碰
 * 账号（令牌由调用方取好传进来）、不决定脚本存在哪儿。
 */

#pragma once

#include <QDir>
#include <QString>
#include <QStringList>

#include "util/LaunchScriptCredentials.hpp"
#include "util/SecretRedactor.hpp"

namespace LaunchScriptExport {

/**
 * @brief 按 cmd.exe 的规矩把一个参数包起来。
 *
 * 两条规矩，都得照做：
 * - `%` 在批处理里是**变量展开**（`%PATH%`），而且写在双引号里**照样展开** ⇒ 一律写 `%%`；
 * - 含空格/制表符、空串、或者带 `&|<>()^"` 的参数要加双引号（不加的话那个参数会被 cmd
 *   拆成好几段，或者整个消失）。
 *
 * NOTE: 参数里真出现一个 `"` 是**没救**的（cmd 没有"引号里的引号"这种写法）。这条路上
 * 的参数来自版本 json 和用户设置（路径、UUID、玩家名），到不了那一步；真到了也只是那一个
 * 参数被拆开，脚本里看得见，不是静默错。
 * NOTE: 故意**不**动 `!`：它只在"开了延迟展开"的 cmd 里有意义（脚本自己 `@echo off`，
 * 默认就没开），而按延迟展开去转义（`^^!`）会让正常那一档凭空多出一个 `^`。
 */
inline QString quote(const QString &arg) {
    QString escaped = arg;
    escaped.replace(QLatin1Char('%'), QStringLiteral("%%"));
    static const QString kSpecial = QStringLiteral(" \t&|<>()^\"");
    bool needsQuotes = escaped.isEmpty();
    for (const QChar &ch : escaped) {
        if (kSpecial.contains(ch)) {
            needsQuotes = true;
            break;
        }
    }
    if (!needsQuotes) {
        return escaped;
    }
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

/**
 * @brief 这一层要拼的那一份东西 —— **全是值**，没有一个 ui 类型。
 */
struct Input {
    /// 启动时的工作目录（数据目录）。脚本里第一句 `cd /d` 就是切到这儿。
    QString dataDir;
    /// Java 可执行文件。**空串** = 这个实例还没挑过 Java ⇒ 写裸 `java`（和启动器那边
    /// "从 PATH 找"是同一个兜底）。
    QString program;
    /// 已经展开好的启动参数（和启动器这一趟发的是同一份）。
    QStringList args;
    /// 正版那一步取到的令牌；离线时为空。
    QString token;
    /// 离线身份：不带上线用的那几句前言，也不做令牌替换。
    bool offlineMode = false;
};

/**
 * @brief 拼出整份脚本正文；返回**空串** = 里面有已知凭据，这份不许写。
 *
 * 编码上有一条规矩：正文按**本地 8 位**写出去（调用方 `toLocal8Bit()`），不写 UTF-8 +
 * `chcp 65001` —— cmd 是按当前代码页读文件的，而"双击一个 .bat"时先跑起来的正是 cmd
 * 自己，代码页要等它读到那一行 `chcp` 才能改，在那之前的字节已经按旧代码页解过了。写本地
 * 代码页则两边天然一致。所以里面那句注释也要按本地代码页写得出来：英文版 Windows 的代码页
 * 装不下汉字，`toLocal8Bit()` 会把它们变成 '?'，那一行就成了一句问号 —— 注释是给人看的，
 * 宁可换一种写得出来的语言。参数里那种非 ASCII 没法替（那是用户的路径/名字），只能照实写。
 */
inline QString compose(const Input &input) {
    // 这一份要能直接双击跑起来：第一句必须是 `cd /d`。参数里 classpath 那几段是**相对
    // <数据目录>** 写的（启动器那边靠 QProcess 的 setWorkingDirectory(minecraftDir) 兜住），
    // 脚本不在那个目录下跑就找不到 jar。`/d` 是连盘符一起换 —— 数据目录在 E 盘、脚本导到
    // C 盘桌面上是最常见的情形。
    const auto localOrAscii = [](const QString &local, const QString &ascii) {
        return QString::fromLocal8Bit(local.toLocal8Bit()) == local ? local : ascii;
    };
    QString script;
    script += QStringLiteral("@echo off\r\n");
    script += localOrAscii(
        QStringLiteral("rem 由 WTFML 导出：按这个实例现在的参数启动。\r\n"),
        QStringLiteral("rem Exported by WTFML: starts with this instance's current arguments.\r\n"));
    if (!input.offlineMode) {
        script += LaunchScriptCredentials::preamble();
    }
    script += QStringLiteral("cd /d ") +
              quote(QDir::toNativeSeparators(input.dataDir)) +
              QStringLiteral("\r\n");
    const QString program = input.program.isEmpty() ? QStringLiteral("java")
                                                    : QDir::toNativeSeparators(input.program);
    script += quote(program);
    for (const QString &arg : input.args) {
        QString safeArgument = arg;
        // 令牌只在这一处替换：正版那一路的占位符换成环境变量引用（见 LaunchScriptCredentials），
        // 原始令牌一个字节都不许落到盘上。离线那一路没有令牌，照实写。
        if (!input.offlineMode && !input.token.isEmpty()) {
            safeArgument.replace(input.token, LaunchScriptCredentials::placeholder());
        }
        script += QLatin1Char(' ') +
                  (input.offlineMode
                       ? quote(safeArgument)
                       : LaunchScriptCredentials::argument(quote(safeArgument)));
    }
    script += QStringLiteral("\r\n");
    // 拼完之后再过一道已知凭据：用户自己写进追加参数的、或者从哪个版本 json 抄来的，
    // 只要和登记过的某个令牌对得上，这一份就不写（返回空串，调用方去说"为什么没导"）。
    if (SecretRedactor::redact(script) != script) {
        return QString();
    }
    return script;
}

} // namespace LaunchScriptExport
