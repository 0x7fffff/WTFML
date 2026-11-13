#pragma once
/**
 * @file SettingsIsolation.hpp
 * @brief 测试进程的设置沙盒判据：QSettings 没落进本进程的沙盒就当场失败。
 *
 * 为什么要这一层：Windows 上产品的 `QSettings` 就是 `HKCU\Software\WTFML\WTFML`。测试里
 * 只写 `QSettings::setPath()` 是**不够**的 —— 它只改 IniFormat 的全局路径，而默认格式在
 * Windows 上是 NativeFormat；默认格式没切成 IniFormat 的话，`QSettings()` 照样打到真注册表，
 * 而且一声不响。同理，沙盒根目录传错一个相对路径，写下去的也是别处。
 *
 * 判据三条，任何一条不成立就打印原因并以 97 退出：
 *   1. `QSettings::defaultFormat()` 是 `IniFormat`；
 *   2. `QSettings().fileName()` 解析出来的文件在调用方给的沙盒根目录**里面**；
 *   3. 那条路径不是真实用户设置那一份（`NativeFormat/UserScope` 解析出来的那条）。
 *
 * 用法（`tests/` 下每个带 `main()` 的入口都写一行，`tools/check-test-modules.ps1` 会扫）：
 * @code
 *     requireIsolatedSettings(processSandbox.path(), "verify");
 * @endcode
 *
 * 注意：`mw_check` 的 `auth-live` 探针要读真注册表里那份令牌（只读、不进默认那一跑），
 * 它在沙盒重定向**之前**就 return 了，走不到这里。
 */

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QString>

#include <cstdio>
#include <cstdlib>

namespace wtfml_test {

/// 真实用户设置会落到哪个文件（Windows 上是 `HKCU\Software\WTFML\WTFML` 这一支）。
/// `QSettings::setPath(IniFormat, …)` 改不到它 —— 这正是它能当"真家伙"用的原因。
inline QString realUserSettingsFile() {
    const QSettings native(QSettings::NativeFormat, QSettings::UserScope,
                           QCoreApplication::organizationName(),
                           QCoreApplication::applicationName());
    return QDir::cleanPath(native.fileName());
}

/// 沙盒判据。不成立时 `why` 写清是哪一条、路径是什么。
inline bool settingsAreIsolated(const QString &sandboxRoot, QString *why) {
    const QFileInfo root(sandboxRoot);
    if (sandboxRoot.isEmpty() || !root.isDir()) {
        *why = QStringLiteral("沙盒根目录不是一个已存在的目录: %1").arg(sandboxRoot);
        return false;
    }
    if (QSettings::defaultFormat() != QSettings::IniFormat) {
        *why = QStringLiteral("默认格式不是 IniFormat（setPath 只改 IniFormat 的路径，"
                              "默认格式还是 NativeFormat 就会直奔注册表）");
        return false;
    }
    const QString file = QDir::cleanPath(QSettings().fileName());
    if (file.contains(QStringLiteral("HKEY_"), Qt::CaseInsensitive)) {
        *why = QStringLiteral("设置文件解析成了注册表路径: %1").arg(file);
        return false;
    }
    const QString base = QDir::cleanPath(root.absoluteFilePath());
    const bool inside = file.compare(base, Qt::CaseInsensitive) == 0 ||
                        file.startsWith(base + QLatin1Char('/'), Qt::CaseInsensitive);
    if (!inside) {
        *why = QStringLiteral("设置文件不在沙盒里: %1（沙盒 %2）").arg(file, base);
        return false;
    }
    const QString real = realUserSettingsFile();
    if (!real.isEmpty() && file.compare(real, Qt::CaseInsensitive) == 0) {
        *why = QStringLiteral("设置文件就是真实用户那一条: %1").arg(file);
        return false;
    }
    return true;
}

/// 每个测试入口的守卫：不隔离就退出（97），不打折、不静默。
inline void requireIsolatedSettings(const QString &sandboxRoot, const char *who) {
    QString why;
    if (settingsAreIsolated(sandboxRoot, &why)) return;
    std::fprintf(stderr,
                 "\n[settings-isolation] %s 的 QSettings 没有隔离，这一趟不跑：\n  %s\n"
                 "  （真实用户设置 = %s；测试一律 QSettings::setDefaultFormat(IniFormat) + "
                 "setPath 到本进程的临时目录）\n",
                 who, qPrintable(why), qPrintable(realUserSettingsFile()));
    std::fflush(stderr);
    std::exit(97);
}

}  // namespace wtfml_test

using wtfml_test::requireIsolatedSettings;
