/**
 * @file fakejar.hpp
 * @brief 造"假 JAR"的那两个小工具（verify 和 mw_check 共用，2026-09-24 加的）。
 *
 * 为什么放在这个位置上、而不是某个工程的 main.cpp 里：**两个工程都要用它** ——
 * verify 的 ZF 段要证"判据是 JAR 里有没有那一条，不是版本号"，mw_check 的 ZG 段要证
 * "真主窗口为离线账号选的那张脸就是 JAR 里那张"。第三条路是各写一份 PowerShell 脚本，
 * 那份脚本里有几个必须踩对的细节（条目名要正斜杠、文件要先 Delete 再 Create），
 * 复制两份的话迟早只修一边。
 *
 * NOTE: 这是 tests/ 里的验证脚手架，不是产品代码；产品读 JAR 走 ZipUtil::readEntry。
 */
#pragma once

#include <QBuffer>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMap>
#include <QProcess>
#include <QString>
#include <QTemporaryDir>

/// 单引号字符串里再嵌单引号要写成两个（PS 的规矩）。这里的路径是调用方自己拼的 ASCII
/// 路径，正常情况下一个都不用转，但拼进去的字面量按规矩来 —— 哪天临时目录换了形状，
/// 报的就不是"路径里有引号"这种看不懂的错。
inline QString psQuote(const QString &value) {
    QString escaped = value;
    escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
    return escaped;
}

/// 用 .NET 的 ZipFile 造一个假 JAR（真的 zip，条目名带 `/`）。
///
/// NOTE: 走 PowerShell 是有意的：产品读 JAR 走的就是这条路（ZipUtil::readEntry），造出来
///       的东西必须是**同一条路读得出来的那种 zip**；而这两个工程连 zlib 都没有（见
///       src/util/ZipUtil.hpp 文件头），本来就只能这么造。
inline bool makeFakeJar(const QString &jarPath, const QMap<QString, QByteArray> &entries) {
    QTemporaryDir srcDir;
    if (!srcDir.isValid()) {
        return false;
    }

    // 逐条建条目、**条目名原样用 key**（不做 CreateFromDirectory 那种"按目录树拼名字"）：
    // 真 JAR 里那些名字是正斜杠，.NET 的 CreateFromDirectory 在 Windows 上拼出来的分隔符
    // 是不是斜杠要看版本 —— 那就成了"验的是巧合"。这里名字我说了算。
    QString scriptText = QStringLiteral("$ErrorActionPreference='Stop'\n"
                                       "Add-Type -AssemblyName System.IO.Compression.FileSystem\n"
                                       "[System.IO.File]::Delete('%1')\n"
                                       "$zip = [System.IO.Compression.ZipFile]::Open('%1','Create')\n")
                             .arg(psQuote(QDir::toNativeSeparators(jarPath)));
    int index = 0;
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it, ++index) {
        const QString src = srcDir.path() + QStringLiteral("/f") + QString::number(index);
        QFile file(src);
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        file.write(it.value());
        file.close();
        scriptText += QStringLiteral(
                          "$b = [System.IO.File]::ReadAllBytes('%1')\n"
                          "$e = $zip.CreateEntry('%2')\n"
                          "$s = $e.Open()\n"
                          "$s.Write($b,0,$b.Length)\n"
                          "$s.Close()\n")
                          .arg(psQuote(QDir::toNativeSeparators(src)), psQuote(it.key()));
    }
    scriptText += QStringLiteral("$zip.Dispose()\n");

    QDir().mkpath(QFileInfo(jarPath).absolutePath());
    const QString scriptPath = srcDir.path() + QStringLiteral("/mk.ps1");
    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    script.write(scriptText.toUtf8());
    script.close();

    QProcess process;
    process.start(QStringLiteral("powershell"),
                  {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                   QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                   QStringLiteral("-File"), QDir::toNativeSeparators(scriptPath)});
    if (!process.waitForFinished(60000)) {
        process.kill();
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 &&
           QFile::exists(jarPath);
}

/// 一张 64×64 的真 PNG（写进假 JAR 里当皮肤用）。
inline QByteArray makePngBytes(const QColor &color) {
    QImage image(64, 64, QImage::Format_ARGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}
