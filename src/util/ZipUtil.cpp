/**
 * @file ZipUtil.cpp
 * @brief ZipUtil 的实现。
 * @author 0x7fffff
 * @date 2026-09
 * @note 用 PowerShell + .NET System.IO.Compression 解压，一次进程调用可处理
 *       任意多个压缩包；脚本经 Base64 编码后交给 -EncodedCommand，绕开
 *       cmd/PowerShell 两层的引号与转义问题。
 */
#include "ZipUtil.hpp"

#include <QDir>
#include <QProcess>

namespace {

/**
 * @brief 把字符串包成 PowerShell 单引号字面量。
 * @param value 原始字符串。
 * @return 返回已转义的 PowerShell 字面量（含外层单引号）。
 */
QString psQuote(const QString &value) {
    QString escaped = value;
    // PowerShell 单引号字符串里，单引号本身要写成两个
    escaped.replace(QLatin1Char('\''), QLatin1String("''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

/**
 * @brief 把字符串列表转成 PowerShell 数组字面量。
 * @param values 原始字符串列表。
 * @return 返回形如 @('a','b') 的字面量。
 */
QString psArray(const QStringList &values) {
    QStringList quoted;
    quoted.reserve(values.size());
    for (const QString &value : values) {
        quoted << psQuote(value);
    }
    return QStringLiteral("@(") + quoted.join(QLatin1Char(',')) + QLatin1Char(')');
}

/**
 * @brief 按 -EncodedCommand 的要求编码脚本。
 * @param script 脚本正文。
 * @return 返回 UTF-16LE + Base64 的字符串。
 */
QString encodeCommand(const QString &script) {
    // -EncodedCommand 要求 Base64(UTF-16LE)，这里手工拼一遍，不依赖主机字节序
    QByteArray utf16le;
    utf16le.reserve(script.size() * 2);
    for (const QChar ch : script) {
        utf16le.append(static_cast<char>(ch.unicode() & 0xFF));
        utf16le.append(static_cast<char>((ch.unicode() >> 8) & 0xFF));
    }
    return QString::fromLatin1(utf16le.toBase64());
}

}  // namespace

bool ZipUtil::extractArchives(const QStringList &archives, const QString &targetDir,
                              const QStringList &excludePrefixes, QString *errorOut) {
    if (archives.isEmpty()) {
        if (errorOut) {
            *errorOut = QStringLiteral("没有需要解压的压缩包");
        }
        return false;
    }

    if (!QDir().mkpath(targetDir)) {
        if (errorOut) {
            *errorOut = QStringLiteral("无法创建目录：%1").arg(targetDir);
        }
        return false;
    }

    QStringList skipConditions;
    // 防 zip slip：正常 Minecraft jar 不会有 .. 条目，但解压工具没理由不设防
    skipConditions << QStringLiteral("$e.FullName.Contains('..')");
    for (const QString &prefix : excludePrefixes) {
        const QString normalized = prefix.endsWith(QLatin1Char('/')) ? prefix : prefix + QLatin1Char('/');
        skipConditions << QStringLiteral("$e.FullName.StartsWith(%1)").arg(psQuote(normalized));
    }

    // 用 .NET 的 Path/Directory 而不是 PowerShell 的 Join-Path / New-Item：
    // 前者纯字符串运算，不会被 PowerShell provider 把路径"解析"成别的形状。
    const QString script =
        QStringLiteral("$ErrorActionPreference='Stop'\n"
                       "Add-Type -AssemblyName System.IO.Compression.FileSystem\n"
                       "$out=%1\n"
                       "[void][System.IO.Directory]::CreateDirectory($out)\n"
                       "foreach($z in %2){\n"
                       "  $zip=[System.IO.Compression.ZipFile]::OpenRead($z)\n"
                       "  try{\n"
                       "    foreach($e in $zip.Entries){\n"
                       "      if([string]::IsNullOrEmpty($e.Name)){continue}\n"
                       "      if(%3){continue}\n"
                       "      $dest=[System.IO.Path]::Combine($out,$e.FullName)\n"
                       "      [void][System.IO.Directory]::CreateDirectory"
                       "([System.IO.Path]::GetDirectoryName($dest))\n"
                       "      [System.IO.Compression.ZipFileExtensions]::ExtractToFile($e,$dest,$true)\n"
                       "    }\n"
                       "  } finally { $zip.Dispose() }\n"
                       "}\n")
            .arg(psQuote(QDir(targetDir).absolutePath()), psArray(archives),
                 QStringLiteral("(") + skipConditions.join(QStringLiteral(" -or ")) + QLatin1Char(')'));

    QProcess process;
    process.start(QStringLiteral("powershell"),
                  {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                   QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                   QStringLiteral("-EncodedCommand"), encodeCommand(script)});
    if (!process.waitForFinished(180000)) {
        process.kill();
        process.waitForFinished(5000);
        if (errorOut) {
            *errorOut = QStringLiteral("解压超时（%1 个压缩包）").arg(archives.size());
        }
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (errorOut) {
            const QString err = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
            *errorOut = err.isEmpty()
                            ? QStringLiteral("解压失败（PowerShell 退出码 %1）").arg(process.exitCode())
                            : err;
        }
        return false;
    }
    return true;
}
