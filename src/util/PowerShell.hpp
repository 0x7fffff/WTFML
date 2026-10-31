/**
 * @file PowerShell.hpp
 * @brief 系统 `powershell.exe` 的**绝对路径** —— 全工程只此一份。
 *
 * 为什么不直接拿裸名 `powershell` 起进程：Qt 在 Windows 上把整条命令行交给
 * `CreateProcess`（`applicationName` 是空），而它只对**带路径**的程序名直接用；只有名字
 * 的话就按"应用目录 → **当前目录** → 系统目录 → PATH"找。便携版启动器可能就从下载目录
 * 或游戏文件夹里跑起来，同目录里放一个 `powershell.exe`，我们这一发就亲手把它执行了 ——
 * 拿到的是用户自己的权限。
 *
 * 用 `%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe`：Windows PowerShell
 * 从 Vista 起就在这个固定位置；32 位进程走 WOW64 重定向到 SysWOW64 那份，同样在。
 * 连 SystemRoot 都读不到（构造出来的环境）才退回裸名 —— 那时起不来会报一句人话，
 * 比猜一个路径强。
 *
 * NOTE: 实现放在头里（`inline` + 函数内静态），不是图省事 —— 这个仓库有好几份 CMake
 * 源文件清单（产品、三套测试、network_diag 各列一遍 `ZipUtil.cpp`），一个只有十几行、
 * 没有别的依赖的解析函数每加一处清单就多一处会漏的地方。
 */
#pragma once

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QtGlobal>

namespace PowerShell {

/// 系统那份 `powershell.exe` 的绝对路径（拿不到时才回退成裸名 `powershell`）。
inline QString executable() {
    // 只解析一次：这个值在进程活着的这段时间里不会变，而起进程的地方有五处。
    static const QString path = []() -> QString {
        const QString root = qEnvironmentVariable("SystemRoot");
        if (!root.isEmpty()) {
            const QString candidate =
                root + QStringLiteral("/System32/WindowsPowerShell/v1.0/powershell.exe");
            if (QFileInfo(candidate).isFile()) {
                return QDir::toNativeSeparators(candidate);
            }
        }
        return QStringLiteral("powershell");
    }();
    return path;
}

}  // namespace PowerShell
