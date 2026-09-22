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
#include <QFile>
#include <QProcess>
#include <QUuid>

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

/// 起一次 PowerShell 跑这段脚本。返回 false 时 errorOut 里是原因。
///
/// NOTE: 三个 - 号开头的开关一个都不能少，理由见 extractArchives 里原来那段（-NoProfile
///       挡用户 profile 里的别名、-ExecutionPolicy Bypass 挡执行策略、-EncodedCommand
///       绕开两层引号转义）。三个调用点共用这一份，免得哪天只给其中一个加了新开关。
bool runPowershell(const QString &script, int timeoutMs, QByteArray *stdoutOut,
                   QString *errorOut) {
    QProcess process;
    process.start(QStringLiteral("powershell"),
                  {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                   QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                   QStringLiteral("-EncodedCommand"), encodeCommand(script)});
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(5000);
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("解压超时");
        }
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (errorOut != nullptr) {
            // NOTE: fromLocal8Bit 是对的，**不要**跟着游戏进程改成 fromUtf8 —— 这里的
            // 输出来自 PowerShell，不是 JVM，-Dstdout.encoding 管不到它（见
            // LauncherManager::decodeGameOutput 的说明）。
            const QString err = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
            *errorOut = err.isEmpty()
                            ? QStringLiteral("PowerShell 退出码 %1").arg(process.exitCode())
                            : err;
        }
        return false;
    }
    if (stdoutOut != nullptr) {
        *stdoutOut = process.readAllStandardOutput();
    }
    return true;
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
    // 防 zip slip。这几条和 extractSubtrees() 里那组**一样严**（以前这里只判 `..`，漏了
    // "条目名本身就是绝对路径"那种 —— 见下面 NOTE）。正常 Minecraft jar 不会有这种条目，
    // 但解压工具没理由不设防。
    //
    // NOTE: 三条各自的来路：
    //   - `..`：翻到 $out 上面去（`../x`、`a/../../x`，`\` 分隔也一样，因为判的是子串）；
    //   - 头一个字符是 `/`、或者名字里带 `:`：.NET 的 Path.Combine 认为第二个参数已经是
    //     绝对路径（`/x`、`\x`、`C:/x` 都算）→ **把 $out 丢掉**，文件写到目标目录外面；
    //   - 判的必须是**归一过 `\`→`/` 的名字**：Windows 上 `\` 同样是分隔符，不归一的话
    //     `META-INF\MANIFEST.MF` 这类条目还会从 excludePrefixes 底下溜过去。
    const QString fullName = QStringLiteral("$e.FullName.Replace('\\','/')");
    skipConditions << QStringLiteral("%1.Contains('..')").arg(fullName);
    skipConditions << QStringLiteral("%1.StartsWith('/')").arg(fullName);
    skipConditions << QStringLiteral("%1.Contains(':')").arg(fullName);
    for (const QString &prefix : excludePrefixes) {
        const QString normalized = prefix.endsWith(QLatin1Char('/')) ? prefix : prefix + QLatin1Char('/');
        skipConditions << QStringLiteral("%1.StartsWith(%2)").arg(fullName, psQuote(normalized));
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
                       "      $full=$e.FullName.Replace('\\','/')\n"
                       "      if(%3){continue}\n"
                       "      $dest=[System.IO.Path]::Combine($out,$full)\n"
                       "      [void][System.IO.Directory]::CreateDirectory"
                       "([System.IO.Path]::GetDirectoryName($dest))\n"
                       "      [System.IO.Compression.ZipFileExtensions]::ExtractToFile($e,$dest,$true)\n"
                       "    }\n"
                       "  } finally { $zip.Dispose() }\n"
                       "}\n")
            .arg(psQuote(QDir(targetDir).absolutePath()), psArray(archives),
                 QStringLiteral("(") + skipConditions.join(QStringLiteral(" -or ")) + QLatin1Char(')'));

    QString failure;
    if (!runPowershell(script, 180000, nullptr, &failure)) {
        if (errorOut) {
            *errorOut = failure.isEmpty()
                            ? QStringLiteral("解压失败（%1 个压缩包）").arg(archives.size())
                            : failure;
        }
        return false;
    }
    return true;
}

ZipUtil::EntryStatus ZipUtil::readEntry(const QString &archive, const QString &entryName,
                                        QByteArray *out, QString *errorOut) {
    if (out != nullptr) {
        out->clear();
    }
    if (errorOut != nullptr) {
        errorOut->clear();
    }

    // 条目先落到一个临时文件再读回来，而不是让 PowerShell 把字节打到 stdout：
    // stdout 是**文本**通道，经过一次编码转换之后，二进制内容（zip 里的 json 是 UTF-8 的
    // 字节流）就不再是原来的字节了。临时文件没有这一层。
    const QString tempPath = QDir::tempPath() + QStringLiteral("/wtfml-entry-") +
                             QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QByteArray temp = QFile::encodeName(tempPath);

    // 退出码：3 = 没有这个条目，4 = 打不开（不是 zip / 坏了）。两个都要和"别的失败"分开。
    const QString script =
        QStringLiteral("$ErrorActionPreference='Stop'\n"
                       "Add-Type -AssemblyName System.IO.Compression.FileSystem\n"
                       "$target=%1\n"
                       "try{ $zip=[System.IO.Compression.ZipFile]::OpenRead(%2) }"
                       " catch { exit 4 }\n"
                       "try{\n"
                       "  $e=$zip.GetEntry($target)\n"
                       "  if($e -eq $null){\n"
                       "    $lower=$target.ToLowerInvariant()\n"
                       "    foreach($c in $zip.Entries){\n"
                       "      if($c.FullName.ToLowerInvariant() -eq $lower){ $e=$c; break }\n"
                       "    }\n"
                       "  }\n"
                       "  if($e -eq $null){ exit 3 }\n"
                       "  [System.IO.Compression.ZipFileExtensions]::ExtractToFile"
                       "($e,%3,$true)\n"
                       "} finally { $zip.Dispose() }\n")
            .arg(psQuote(entryName), psQuote(QDir::toNativeSeparators(archive)),
                 psQuote(QFile::decodeName(temp)));

    QProcess process;
    process.start(QStringLiteral("powershell"),
                  {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                   QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                   QStringLiteral("-EncodedCommand"), encodeCommand(script)});
    if (!process.waitForFinished(60000)) {
        process.kill();
        process.waitForFinished(5000);
        QFile::remove(tempPath);
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("读整合包清单超时");
        }
        return EntryStatus::Failed;
    }
    const int code = process.exitCode();
    const QByteArray stderrBytes = process.readAllStandardError();
    if (process.exitStatus() != QProcess::NormalExit) {
        QFile::remove(tempPath);
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("读整合包清单时 PowerShell 被强行结束了");
        }
        return EntryStatus::Failed;
    }
    if (code == 3) {
        QFile::remove(tempPath);
        return EntryStatus::NotFound;
    }
    if (code == 4) {
        QFile::remove(tempPath);
        return EntryStatus::NotAnArchive;
    }
    if (code != 0) {
        QFile::remove(tempPath);
        if (errorOut != nullptr) {
            const QString err = QString::fromLocal8Bit(stderrBytes).trimmed();
            *errorOut = err.isEmpty() ? QStringLiteral("读整合包清单失败（退出码 %1）").arg(code) : err;
        }
        return EntryStatus::Failed;
    }

    QFile file(tempPath);
    const bool read = file.open(QIODevice::ReadOnly);
    const QByteArray bytes = read ? file.readAll() : QByteArray();
    file.close();
    QFile::remove(tempPath);

    if (!read) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("读不出临时文件 %1").arg(tempPath);
        }
        return EntryStatus::Failed;
    }
    if (out != nullptr) {
        *out = bytes;
    }
    return EntryStatus::Ok;
}

bool ZipUtil::extractSubtrees(const QStringList &archives, const QString &targetDir,
                              const QStringList &roots, int *extractedOut, QString *errorOut) {
    if (extractedOut != nullptr) {
        *extractedOut = 0;
    }
    if (archives.isEmpty() || roots.isEmpty()) {
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

    // 前缀统一成"以 / 结尾"的形状：调用方写 `overrides` 还是 `overrides/` 都得认。
    QStringList normalized;
    for (const QString &root : roots) {
        if (root.isEmpty()) {
            continue;
        }
        normalized << (root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/'));
    }
    if (normalized.isEmpty()) {
        if (errorOut) {
            *errorOut = QStringLiteral("没有需要解压的目录");
        }
        return false;
    }

    // NOTE: 这一段是**剥掉前缀之后**的那几条守卫，比 extractArchives 里那条只判 `..` 的
    //       严一点：剥完之后如果剩下的是 `C:/x` 或 `/x`，.NET 的 Path.Combine 会**丢掉**
    //       前半截（它认为第二个参数已经是绝对路径了），于是文件被写到 $out 外面去。
    //       条目名里的 `\` 也先换成 `/` 再判 —— Windows 上它同样是分隔符。
    const QString script =
        QStringLiteral("$ErrorActionPreference='Stop'\n"
                       "Add-Type -AssemblyName System.IO.Compression.FileSystem\n"
                       "$out=%1\n"
                       "[void][System.IO.Directory]::CreateDirectory($out)\n"
                       "$roots=%2\n"
                       "$count=0\n"
                       "foreach($z in %3){\n"
                       "  $zip=[System.IO.Compression.ZipFile]::OpenRead($z)\n"
                       "  try{\n"
                       "    foreach($r in $roots){\n"
                       "      $low=$r.ToLowerInvariant()\n"
                       "      foreach($e in $zip.Entries){\n"
                       "        if([string]::IsNullOrEmpty($e.Name)){continue}\n"
                       "        $full=$e.FullName.Replace('\\','/')\n"
                       "        if(-not $full.ToLowerInvariant().StartsWith($low)){continue}\n"
                       "        $rel=$full.Substring($r.Length)\n"
                       "        if($rel.Contains('..')){continue}\n"
                       "        if($rel.StartsWith('/') -or $rel.Contains(':')){continue}\n"
                       "        $dest=[System.IO.Path]::Combine($out,$rel)\n"
                       "        [void][System.IO.Directory]::CreateDirectory"
                       "([System.IO.Path]::GetDirectoryName($dest))\n"
                       "        [System.IO.Compression.ZipFileExtensions]::ExtractToFile"
                       "($e,$dest,$true)\n"
                       "        $count=$count+1\n"
                       "      }\n"
                       "    }\n"
                       "  } finally { $zip.Dispose() }\n"
                       "}\n"
                       "[Console]::Out.Write($count)\n")
            .arg(psQuote(QDir(targetDir).absolutePath()), psArray(normalized), psArray(archives));

    QByteArray stdoutBytes;
    QString failure;
    if (!runPowershell(script, 30 * 60 * 1000, &stdoutBytes, &failure)) {
        if (errorOut) {
            *errorOut = failure;
        }
        return false;
    }
    if (extractedOut != nullptr) {
        *extractedOut = QString::fromLatin1(stdoutBytes).trimmed().toInt();
    }
    return true;
}
