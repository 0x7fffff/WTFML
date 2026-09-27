/**
 * @file ZipUtil.cpp
 * @brief ZipUtil 的实现。
 * @author 0x7fffff
 * @date 2026-09
 * @note 用 PowerShell + .NET System.IO.Compression 解压，一次进程调用可处理
 *       任意多个压缩包；脚本经 Base64 编码后交给 -EncodedCommand，绕开
 *       cmd/PowerShell 两层的引号与转义问题。
 * @note `-EncodedCommand` 换来的是**命令行长度上限**：整个脚本只能占一万二千来个字符
 *       （32767 的命令行 ÷ Base64 的 4/3 ÷ UTF-16LE 的 2）。所以脚本里**只放常量**，
 *       随数据长大的东西（打包那张条目表）一律走临时文件 —— 见 createZip 里那段。
 */
#include "ZipUtil.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

/**
 * @brief waitForFinished() 返回 false 之后，到底发生了什么。
 * @param failure process.error()（**必须在 kill 之前读**，kill 之后它可能已经变了）。
 * @param detail process.errorString()。
 * @param what 这次动作的名字（「解压」/「读整合包清单」），拼进句子里。**由调用方给**：这几句
 *        里有一部分是直接弹给用户看的，主语写错了就是一句说不通的话（见 readEntry 的 what）。
 * @return 返回给用户看的一句话。
 *
 * NOTE: 超时**不是**唯一一种"waitForFinished() 给 false"。进程压根没起来（PowerShell
 *       不存在、被安全软件拦下）也是 false，而三个调用点以前一律报「解压超时」——
 *       用户和排查的人都会往网络/磁盘上找，而真因在"进程没起来"那一头（2026-09-24 改的）。
 */
QString describeProcessFailure(QProcess::ProcessError failure, const QString &detail,
                               const QString &what) {
    switch (failure) {
    case QProcess::FailedToStart:
        return QStringLiteral("%1没跑起来（%2）。").arg(what, detail);
    case QProcess::Crashed:
        return QStringLiteral("%1时进程被强行结束了（%2）。").arg(what, detail);
    default:
        return QStringLiteral("%1超时").arg(what);
    }
}

/**
 * @brief 从 PowerShell 的错误输出里挑一句能给用户看的话。
 * @param raw runPowershell() 写进 errorOut 的东西。
 * @return 挑出来的那一句；**一句都挑不出来时返回空串**，让调用方用它自己那句兜底
 *         （宁可少说，也不要把 `#< CLIXML` 这种内部形状弹到用户脸上）。
 *
 * NOTE: **stderr 上从来不止我们那一句**。PowerShell 第一次跑起来时会往 stderr 上塞一条
 *       **进度记录**（「Preparing modules for first use.」），而进度/错误流在 stderr 被
 *       重定向（QProcess 就是重定向）时会被**序列化成 CLIXML**。实测拿到的是这个形状：
 *
 *           #< CLIXML
 *           The compression mode specified in GZip header is unknown.
 *           <Objs Version="1.1.0.1" xmlns="...">...</Objs>
 *
 *       —— **人话夹在中间那两行机器话之间**。只取第一行得到的是 `#< CLIXML` 七个字符
 *       （等于什么也没说）；只判「头一行是不是 CLIXML 就整段丢掉」会把真因一起丢掉
 *       （这个坑踩过：脚本里加了 catch、原因也写出来了，用户看到的还是兜底那句话，
 *       原因就在这儿）。所以按行过一遍，把机器话那两种行跳过，取第一条活下来的。
 * NOTE: 脚本 catch 里那句是 `[Console]::Error.WriteLine()` 写的，走**原生 stderr**、
 *       不经过 PowerShell 的错误流序列化，所以它自己能干净地占一行 —— 上面那个形状
 *       就是这么来的。没被 catch 住的错则整个躺在 `<Objs>` 的那条 XML 里（换行写成
 *       `_x000D__x000A_` 那种转义），那种**不解**：解出来是给排查的人看的，不是给用户
 *       看的；那种形态只说明脚本自己就没跑起来，弹给用户的框里说不了。
 */
QString readProcessError(const QString &raw) {
    const QStringList lines = raw.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString text = line.trimmed();
        // 机器话那两种：CLIXML 的头，和序列化出来的那条 XML（整条就一行）。
        if (text.isEmpty() || text == QLatin1String("#< CLIXML") ||
            text.startsWith(QLatin1String("<Objs"))) {
            continue;
        }
        return text;
    }
    return QString();
}

/// 起一次 PowerShell 跑这段脚本。返回 false 时 errorOut 里是原因。
///
/// NOTE: 三个 - 号开头的开关一个都不能少，理由见 extractArchives 里原来那段（-NoProfile
///       挡用户 profile 里的别名、-ExecutionPolicy Bypass 挡执行策略、-EncodedCommand
///       绕开两层引号转义）。四个调用点共用这一份，免得哪天只给其中一个加了新开关。
/// NOTE: `what` 是**这次动作的名字**，由调用方给 —— 这几句是要弹给用户看的，主语写错就是
///       一句说不通的话：打包那一条原来报的是「**解压**没跑起来」（2026-09-26 真实例上撞
///       出来的），用户拿着这句话往解压上找，而全程根本没解压。
bool runPowershell(const QString &script, int timeoutMs, QByteArray *stdoutOut,
                   QString *errorOut, const QString &what) {
    const QString encoded = encodeCommand(script);
    // 兜底闸：Windows 一条命令行只有 32767 个字符，超了 CreateProcess 直接失败，QProcess
    // 报的是「文件名或扩展名太长」（ERROR_FILENAME_EXCED_RANGE）—— 那句话和真正的原因
    // （脚本太长）一个字都对不上，2026-09-26 就是拿着它查了半天。这里自己先拦一道，
    // 把它变成一句说得通的话。
    //
    // NOTE: 撞到这一道说明**我们自己的脚本里放了跟着数据长大的东西**，那是写错了，
    //       不是用户在做什么奇怪的事：脚本正文里只该有常量，实例相关的东西（打包那张
    //       条目表）走临时文件 —— 见 createZip。3 万这个数留了七百来个字符给
    //       `powershell` 那几个开关和引号 —— 编码后的正文是 ASCII，字节数就是字符数。
    if (encoded.size() > 32000) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("%1失败：递给 PowerShell 的脚本太长了（编码后 %2 个字符，"
                                       "Windows 一条命令行最多三万多）。")
                            .arg(what)
                            .arg(encoded.size());
        }
        return false;
    }
    QProcess process;
    process.start(QStringLiteral("powershell"),
                  {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                   QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                   QStringLiteral("-EncodedCommand"), encoded});
    if (!process.waitForFinished(timeoutMs)) {
        // error() / errorString() 要在 kill 之前读：kill 会把状态改成 Crashed。
        const QProcess::ProcessError failure = process.error();
        const QString detail = process.errorString();
        process.kill();
        process.waitForFinished(5000);
        if (errorOut != nullptr) {
            *errorOut = describeProcessFailure(failure, detail, what);
        }
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (errorOut != nullptr) {
            // NOTE: fromLocal8Bit 是对的，**不要**跟着游戏进程改成 fromUtf8 —— 这里的
            // 输出来自 PowerShell，不是 JVM，-Dstdout.encoding 管不到它（见
            // LauncherManager::decodeGameOutput 的说明）。
            //
            // ★ 这一段**不能直接交给用户看**：stderr 被重定向时 PowerShell 会把错误流
            //   序列化成 CLIXML（见 readProcessError() 上面那段），原文的头一行是
            //   `#< CLIXML` 七个字符。三个调用点全都走这里，所以过滤放在这一层 ——
            //   调用方拿到的永远是一句人话（挑不出来时才回它自己那句兜底的）。
            const QString raw = QString::fromLocal8Bit(process.readAllStandardError());
            const QString err = readProcessError(raw);
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
    //   - `..`：翻到 $out 上面去（`../x`、`a/../../x`，`\` 分隔也一样）；
    //   - 头一个字符是 `/`、或者名字里带 `:`：.NET 的 Path.Combine 认为第二个参数已经是
    //     绝对路径（`/x`、`\x`、`C:/x` 都算）→ **把 $out 丢掉**，文件写到目标目录外面；
    //   - 判的必须是**归一过 `\`→`/` 的名字**：Windows 上 `\` 同样是分隔符，不归一的话
    //     `META-INF\MANIFEST.MF` 这类条目还会从 excludePrefixes 底下溜过去。
    const QString fullName = QStringLiteral("$e.FullName.Replace('\\','/')");
    // `..` 判的是**路径分量**，不是子串（2026-09-25 修）。`.Contains('..')` 会把名字里
    // 正好带两个点的**正常文件**一并丢掉 —— `mods/foo..bar.jar`、`assets/a..b.png` 都是
    // 合法名字，而这里的丢弃是**静默的**（`continue`，不报错、不计入任何清单），于是用户
    // 看到的是"整合包导完了但少东西"。判分量之后 `..` 仍然拦得住（`../x` 拆出来第一段
    // 就是 `..`），而 `a..b` 拆出来是一段 `a..b`、不等于 `..`，放行。
    skipConditions << QStringLiteral("(%1.Split('/') -contains '..')").arg(fullName);
    skipConditions << QStringLiteral("%1.StartsWith('/')").arg(fullName);
    skipConditions << QStringLiteral("%1.Contains(':')").arg(fullName);
    // NOTE: exclude 这几条**区分大小写**（.NET 的 StartsWith(string) 就是区分大小写的），
    //       和 extractSubtrees() 里那条**前缀**比对不一样 —— 那边是
    //       `$full.ToLowerInvariant().StartsWith($r.ToLowerInvariant())`，故意不区分
    //       （真实整合包里 `Overrides/` / `overrides/` 两种写法都有，认不出来就会一条都
    //       不解压，而那种失败是静默的）。这里不跟着放宽，是因为排的是 `META-INF/` 这种
    //       规范拼法：放宽只会多丢几个本来也无害的条目，收紧反而可能漏掉签名的目录。
    //       上面那句"和 extractSubtrees() 里那组一样严"说的是**三条防穿越的判据**，
    //       前缀比对这一条两边口径不同，这里写明白，别让读的人以为它们逐字一样。
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
                       "  $zip=$null\n"
                       "  try{\n"
                       "    $zip=[System.IO.Compression.ZipFile]::OpenRead($z)\n"
                       "    foreach($e in $zip.Entries){\n"
                       "      if([string]::IsNullOrEmpty($e.Name)){continue}\n"
                       "      $full=$e.FullName.Replace('\\','/')\n"
                       "      if(%3){continue}\n"
                       "      $dest=[System.IO.Path]::Combine($out,$full)\n"
                       "      [void][System.IO.Directory]::CreateDirectory"
                       "([System.IO.Path]::GetDirectoryName($dest))\n"
                       "      [System.IO.Compression.ZipFileExtensions]::ExtractToFile($e,$dest,$true)\n"
                       "    }\n"
                       "  } catch {\n"
                       // ★ 2026-09-27：这一段 catch 是**必须的**，理由和 gunzipTo / createZip
                       //   里那两段一模一样（见 readProcessError 上面那段）：没兜住的话 PowerShell
                       //   会把错误流序列化成 `#< CLIXML` + 一整条 `<Objs>` XML，而真因（路径太长、
                       //   目标被占用、包是半截下载）**就埋在那条 XML 里**，readProcessError 按行
                       //   过滤时连同机器话一起跳过 ⇒ 用户拿到的是「解压失败：PowerShell 退出码 1」，
                       //   一个字都说不出是哪个包怎么了 —— 而这一条是要弹给他看的。
                       //   GetBaseException() 取最里面那一层（真因在包装里，和那两段同一个理由）。
                       //   退出码 3 和 gunzipTo / createZip 保持一致（"脚本跑到了、里面出错了"，
                       //   和"进程没起来/超时"分开）。
                       "    $msg=$_.Exception.GetBaseException().Message\n"
                       "    if([string]::IsNullOrEmpty($msg)){ $msg=$_.Exception.Message }\n"
                       "    [Console]::Error.WriteLine(('解压「' + $z + '」时出错：' + $msg))\n"
                       "    exit 3\n"
                       "  } finally { if($zip){ $zip.Dispose() } }\n"
                       "}\n")
            .arg(psQuote(QDir(targetDir).absolutePath()), psArray(archives),
                 QStringLiteral("(") + skipConditions.join(QStringLiteral(" -or ")) + QLatin1Char(')'));

    QString failure;
    if (!runPowershell(script, 180000, nullptr, &failure, QStringLiteral("解压"))) {
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
                                        QByteArray *out, QString *errorOut, const QString &what) {
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
        // 同上：先读 error()/errorString()，再 kill。
        const QProcess::ProcessError failure = process.error();
        const QString detail = process.errorString();
        process.kill();
        process.waitForFinished(5000);
        QFile::remove(tempPath);
        if (errorOut != nullptr) {
            *errorOut = describeProcessFailure(failure, detail, what);
        }
        return EntryStatus::Failed;
    }
    const int code = process.exitCode();
    const QByteArray stderrBytes = process.readAllStandardError();
    if (process.exitStatus() != QProcess::NormalExit) {
        QFile::remove(tempPath);
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("%1时 PowerShell 被强行结束了").arg(what);
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
            // ★ 2026-09-27 修：这里原来直接把 stderr 原文交出去，而**这条路和上面那两处
            //   是同一件事**（readProcessError 上面那段说的：stderr 被重定向时 PowerShell
            //   会把错误流序列化成 CLIXML，头一行是 `#< CLIXML`）。readEntry() 自己 new 了
            //   一个 QProcess（它是 runPowershell() 之外的第四个跑 PowerShell 的地方，
            //   见本文件 518 行附近那段），所以它不在那一层过滤的覆盖范围内。
            //   走得到这儿的一例：导入整合包时临时目录写不进去（%TEMP% 被清理工具占着、
            //   盘满、杀软拦新建文件）→ 脚本里 ExtractToFile 抛异常 → 退出码不是 3/4 那两档
            //   → 调用方（MainWindow 读清单那条路）把 `#< CLIXML` 和一整条 <Objs> XML 拼进
            //   「读这个压缩包的时候出错了：…」弹给用户，而真因就埋在那条 XML 里。
            const QString err = readProcessError(QString::fromLocal8Bit(stderrBytes));
            *errorOut = err.isEmpty() ? QStringLiteral("%1失败（退出码 %2）").arg(what).arg(code) : err;
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

    // NOTE: 这一段是**剥掉前缀之后**的那几条守卫，和 extractArchives 里那三条是同一个口径
    //       （那边有完整来路）：剥完之后如果剩下的是 `C:/x` 或 `/x`，.NET 的 Path.Combine 会
    //       **丢掉**前半截（它认为第二个参数已经是绝对路径了），于是文件被写到 $out 外面去。
    //       条目名里的 `\` 也先换成 `/` 再判 —— Windows 上它同样是分隔符。
    //
    //       这里以前写的是「比 extractArchives 里那条**只判 `..`** 的严一点」——那句话在
    //       extractArchives 补上另外两条（绝对路径 / `:`）之后就**说反了**，两边现在一样严。
    const QString script =
        QStringLiteral("$ErrorActionPreference='Stop'\n"
                       "Add-Type -AssemblyName System.IO.Compression.FileSystem\n"
                       "$out=%1\n"
                       "[void][System.IO.Directory]::CreateDirectory($out)\n"
                       "$roots=%2\n"
                       "$count=0\n"
                       "foreach($z in %3){\n"
                       "  $zip=$null\n"
                       "  try{\n"
                       "    $zip=[System.IO.Compression.ZipFile]::OpenRead($z)\n"
                       "    foreach($r in $roots){\n"
                       "      $low=$r.ToLowerInvariant()\n"
                       "      foreach($e in $zip.Entries){\n"
                       "        if([string]::IsNullOrEmpty($e.Name)){continue}\n"
                       "        $full=$e.FullName.Replace('\\','/')\n"
                       "        if(-not $full.ToLowerInvariant().StartsWith($low)){continue}\n"
                       "        $rel=$full.Substring($r.Length)\n"
                       "        if($rel.Split('/') -contains '..'){continue}\n"
                       "        if($rel.StartsWith('/') -or $rel.Contains(':')){continue}\n"
                       "        $dest=[System.IO.Path]::Combine($out,$rel)\n"
                       "        [void][System.IO.Directory]::CreateDirectory"
                       "([System.IO.Path]::GetDirectoryName($dest))\n"
                       "        [System.IO.Compression.ZipFileExtensions]::ExtractToFile"
                       "($e,$dest,$true)\n"
                       "        $count=$count+1\n"
                       "      }\n"
                       "    }\n"
                       "  } catch {\n"
                       // 同上（整包解压那一段的 NOTE 逐条适用）。整合包导入这条路走的**就是**
                       // 这一段：`overrides/` 里出现一个超长路径或者被占用的文件时，从前用户
                       // 拿到的是「PowerShell 退出码 1」，现在拿得到是哪个包、哪句 .NET 的原话。
                       "    $msg=$_.Exception.GetBaseException().Message\n"
                       "    if([string]::IsNullOrEmpty($msg)){ $msg=$_.Exception.Message }\n"
                       "    [Console]::Error.WriteLine(('解压「' + $z + '」时出错：' + $msg))\n"
                       "    exit 3\n"
                       "  } finally { if($zip){ $zip.Dispose() } }\n"
                       "}\n"
                       "[Console]::Out.Write($count)\n")
            .arg(psQuote(QDir(targetDir).absolutePath()), psArray(normalized), psArray(archives));

    QByteArray stdoutBytes;
    QString failure;
    if (!runPowershell(script, 30 * 60 * 1000, &stdoutBytes, &failure,
                       QStringLiteral("解压"))) {
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

bool ZipUtil::gunzipTo(const QString &sourceGz, const QString &targetPath, QString *errorOut) {
    if (errorOut != nullptr) {
        errorOut->clear();
    }

    // 解到目标路径，**不让 PowerShell 把字节打到 stdout** —— 理由和 readEntry 里那段
    // 一样（stdout 是文本通道，二进制过一趟就不再是原来的字节）。
    //
    // NOTE: 两个 Add-Type 都要有：GZipStream 与 CompressionMode 在
    //       System.IO.Compression 里，只加 FileSystem 的话在只装了 .NET Framework
    //       的机器上这一步会报「找不到类型」（FileSystem 依赖它，但别指望程序集
    //       加载顺序替我们把类型也带进来）。
    // NOTE: $dest 只出现一次（先赋给变量再用），不让同一个 %2 在脚本里出现两遍 ——
    //       QString::arg 的占位符替换对「同一个编号出现多次」这件事不好读，绕开它。
    // NOTE: **整段脚本**（连 Add-Type 和建目录）都包在那一个 try 里，catch 里把原因
    //       用 [Console]::Error.WriteLine() 写到**原生 stderr** 上再 exit 3。
    //       两个原因：① 不兜住的话 PowerShell 把错误流序列化成 CLIXML 丢到 stderr 上
    //       （见 readProcessError 的说明），调用方拿到的是 `#< CLIXML` 这种没人看得懂的
    //       东西；② 退出码要和"进程没起来/超时"分开 —— 那两档 runPowershell 自己会说。
    const QString script =
        QStringLiteral("$ErrorActionPreference='Stop'\n"
                       "try{\n"
                       "  Add-Type -AssemblyName System.IO.Compression\n"
                       "  Add-Type -AssemblyName System.IO.Compression.FileSystem\n"
                       "  $dest=%1\n"
                       "  $dir=[System.IO.Path]::GetDirectoryName($dest)\n"
                       "  if(-not [string]::IsNullOrEmpty($dir)){\n"
                       "    [void][System.IO.Directory]::CreateDirectory($dir)\n"
                       "  }\n"
                       "  $in=[System.IO.File]::OpenRead(%2)\n"
                       "  try{\n"
                       "    $gz=New-Object System.IO.Compression.GZipStream"
                       "($in,[System.IO.Compression.CompressionMode]::Decompress)\n"
                       "    try{\n"
                       "      $out=[System.IO.File]::Create($dest)\n"
                       "      try{ $gz.CopyTo($out) } finally { $out.Dispose() }\n"
                       "    } finally { $gz.Dispose() }\n"
                       "  } finally { $in.Dispose() }\n"
                       "} catch {\n"
                       // GetBaseException() 取的是**最里面**那一层：CopyTo 抛出来的是个
                       // 包装（MethodInvocationException），真正有用的那句（比如「这不是
                       // gzip」）在它里面；只取 Message 会拿到「Exception calling CopyTo…」
                       // 这种把真因埋在引号里的话。
                       "  $msg=$_.Exception.GetBaseException().Message\n"
                       "  if([string]::IsNullOrEmpty($msg)){ $msg=$_.Exception.Message }\n"
                       "  [Console]::Error.WriteLine($msg)\n"
                       "  exit 3\n"
                       "}\n")
            .arg(psQuote(QDir::toNativeSeparators(targetPath)),
                 psQuote(QDir::toNativeSeparators(sourceGz)));

    // 15 秒。这个数不是随便定的：真机上 servers.dat 是几百字节，连起进程算上也就
    // 一秒出头；给它 15 秒是为了"机器正忙"时别误判，同时"没装 PowerShell / 被安全
    // 软件拦下"那一档不会让用户干等（那一档根本不进等待，直接 FailedToStart）。
    QString failure;
    if (!runPowershell(script, 15000, nullptr, &failure, QStringLiteral("解压 gzip"))) {
        if (errorOut != nullptr) {
            const QString reason = readProcessError(failure);
            *errorOut = reason.isEmpty() ? QStringLiteral("解压 gzip 失败") : reason;
        }
        return false;
    }
    return true;
}

bool ZipUtil::createZip(const QString &targetZip, const QList<ZipSource> &entries,
                        QString *errorOut) {
    if (targetZip.trimmed().isEmpty()) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("没有给出要写到哪去。");
        }
        return false;
    }
    for (const ZipSource &entry : entries) {
        if (entry.entryName.trimmed().isEmpty() || entry.sourcePath.trimmed().isEmpty()) {
            if (errorOut != nullptr) {
                *errorOut = QStringLiteral("打包的条目表里有空名字或者空来源。");
            }
            return false;
        }
        // 名字里带 `\` 的条目在别的启动器眼里就是一个**名字里带反斜杠的文件**（zip 规范
        // 只用 `/`），等于整层 overrides 全废 —— 而那种包我们自己读得回来（.NET 读的时候
        // 会归一），所以这种错在自测里是看不见的。在这里直接拦掉。
        if (entry.entryName.contains(QLatin1Char('\\'))) {
            if (errorOut != nullptr) {
                *errorOut = QStringLiteral("打包的条目名里有反斜杠：%1").arg(entry.entryName);
            }
            return false;
        }
    }

    // 条目表**写成一个临时文件，不拼进脚本正文**。
    //
    // NOTE: 这是 2026-09-26 在真实例上撞出来的一条硬限制：脚本是经 `-EncodedCommand`
    //       用**命令行**递给 PowerShell 的，而 Windows 给一条命令行只有 32767 个字符；
    //       脚本要先做 Base64（×4/3）再算 UTF-16LE（×2）⇒ 能塞进去的脚本大约 1.2 万字符。
    //       一个装着 133 个模组的实例，光这张 `@('条目名','盘上路径')` 表就一万七千多字符
    //       ⇒ CreateProcess 当场失败，QProcess 报「文件名或扩展名太长」
    //       （ERROR_FILENAME_EXCED_RANGE），用户看到的是「打包失败」而盘上什么都没留下。
    //       **小夹具（两条三条）永远看不见这个** —— 断言得照真实例的量级摆（见 verify V5q）。
    // NOTE: 表用 JSON 而不是"一行一条、TAB 分隔"：条目名和路径里什么都可能有（空格、单引号、
    //       中文、`.`），拼分隔符迟早踩雷，而 JSON 由 Qt 这一侧写、PowerShell 那一侧
    //       ConvertFrom-Json 读，两边都不用自己转义。
    const QString tablePath = QDir::tempPath() + QStringLiteral("/wtfml-ziptable-") +
                              QUuid::createUuid().toString(QUuid::WithoutBraces) +
                              QStringLiteral(".json");
    {
        QJsonArray table;
        for (const ZipSource &entry : entries) {
            QJsonObject item;
            item.insert(QStringLiteral("name"), entry.entryName);
            item.insert(QStringLiteral("src"), QDir::toNativeSeparators(entry.sourcePath));
            table.append(item);
        }
        QFile tableFile(tablePath);
        if (!tableFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (errorOut != nullptr) {
                *errorOut = QStringLiteral("打包的条目表写不出来：%1").arg(tablePath);
            }
            return false;
        }
        // ★ 2026-09-27：这一发的返回值**从前是丢掉的**。写不进去（磁盘满、临时目录没有写
        //   权限、被安全软件拦下）时 `write()` 回 -1，而流程照旧往下走：PowerShell 那一侧
        //   拿到一个空文件，`ConvertFrom-Json` 报「不能对 Null 值表达式调用方法」，用户看到
        //   的是这么一句和真因（盘满）毫无关系的话，而目标 zip 里**一个条目都没有**。
        //   这里当场拦下。NOTE: 短写（盘刚好在半路满）也一并算失败 —— 半截 JSON 同样过不了
        //   ConvertFrom-Json，早点说清楚比让它去猜强。
        const QByteArray payload = QJsonDocument(table).toJson(QJsonDocument::Compact);
        if (tableFile.write(payload) != payload.size()) {
            if (errorOut != nullptr) {
                *errorOut = QStringLiteral("打包的条目表写不完整：%1（临时目录还剩多少空间？）")
                                .arg(tablePath);
            }
            tableFile.close();
            QFile::remove(tablePath);
            return false;
        }
    }

    const QString script =
        QStringLiteral("$ErrorActionPreference='Stop'\n"
                       "Add-Type -AssemblyName System.IO.Compression.FileSystem\n"
                       "$target=%1\n"
                       "$table=%2\n"
                       // 外头这层**必须是 `[array]` 而不是 `@()`**：两种形态都得接住 ——
                       // 只有一条时 ConvertFrom-Json 回的是**一个对象**（不是数组），
                       // 它要能兜成一条；而多条时 ConvertFrom-Json 是**不展开**的（整个数组
                       // 当**一个**输出写出去），`@()` 会把它包成一个"装着数组的一元数组"
                       // ⇒ foreach 只跑一圈、`$p.name` 由成员枚举变成**所有名字拼成的一串**
                       // ⇒ CreateEntryFromFile 收到一个数组当路径，报
                       // 「The given path's format is not supported.」
                       // （2026-09-26 撞的，改用临时文件后又踩了这一脚；`[array]` 两种形态
                       // 都是平铺的，实测见 verify V5q 与"只有一条条目"那些用例。）
                       "$pairs=[array]([System.IO.File]::ReadAllText"
                       "($table,[System.Text.Encoding]::UTF8) | ConvertFrom-Json)\n"
                       "$dir=[System.IO.Path]::GetDirectoryName($target)\n"
                       "if(-not [string]::IsNullOrEmpty($dir)){\n"
                       "  [void][System.IO.Directory]::CreateDirectory($dir)\n"
                       "}\n"
                       "$current=''\n"
                       "try{\n"
                       "  $zip=[System.IO.Compression.ZipFile]::Open($target,'Create')\n"
                       "  try{\n"
                       "    foreach($p in $pairs){\n"
                       "      $current=$p.name\n"
                       "      [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile"
                       "($zip,$p.src,$p.name,[System.IO.Compression.CompressionLevel]::Optimal)\n"
                       "    }\n"
                       "  } finally { $zip.Dispose() }\n"
                       "} catch {\n"
                       // 同上：GetBaseException() 取最里面那一层（缺文件/被占用那些真因
                       // 在包装里面）。
                       "  $msg=$_.Exception.GetBaseException().Message\n"
                       "  if([string]::IsNullOrEmpty($msg)){ $msg=$_.Exception.Message }\n"
                       // 那一句 .NET 的原文（英文、带着完整路径）**照旧带上** —— 它是唯一
                       // 说得出"哪个文件怎么了"的东西；前面这半句是给用户看的主语，因为
                       // 这一条最后是要**弹出来的**（能挡住用户操作的那种错）。
                       "  [Console]::Error.WriteLine(('打包「' + $current + '」时出错：' + $msg))\n"
                       "  exit 3\n"
                       "}\n")
            .arg(psQuote(QDir::toNativeSeparators(targetZip)), psQuote(tablePath));

    // 10 分钟。这个数按**大包**来定：实测一个 1.66 GB / 422 个文件的整合包（导入那一次）
    // 解压就跑了十几分钟，打包只会更慢（Optimal 要真去压一遍）。
    QString failure;
    const bool ok = runPowershell(script, 600000, nullptr, &failure, QStringLiteral("打包"));
    // 临时表两份出路都要删（成功、失败都一样）—— 它躺在系统临时目录里，里面是用户的
    // 实例里都有哪些文件，没理由留一份。
    QFile::remove(tablePath);
    if (!ok) {
        // 半成品删掉（见 .hpp）。NOTE: 删的是**我们写的那个目标路径**，不碰别的。
        QFile::remove(targetZip);
        if (errorOut != nullptr) {
            *errorOut = failure.isEmpty() ? QStringLiteral("打包失败") : failure;
        }
        return false;
    }
    return true;
}
