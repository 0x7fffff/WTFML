/**
 * @file StartupDiagnosis.cpp
 * @brief StartupDiagnosis.hpp 的实现：只读盘、只算，给一份启动前体检报告。
 * @author 0x7fffff
 * @date 2026-10-18
 */

#include "diagnose/StartupDiagnosis.hpp"

#include <algorithm>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

namespace StartupDiagnosis {

namespace {

constexpr qint64 kLogTailBytes = 512 * 1024;
constexpr qint64 kCrashHeadBytes = 64 * 1024;
constexpr int kDisabledNameLimit = 5;

QString readHead(const QString &path, qint64 maxBytes) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(file.read(maxBytes));
}

QString readTail(const QString &path, qint64 maxBytes) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QString();
    const qint64 size = file.size();
    if (size > maxBytes) file.seek(size - maxBytes);
    return QString::fromUtf8(file.readAll());
}

QString firstMeaningfulLine(const QString &text);

/// 找崩溃报告里那两行（`Description:` / `Time:`）。Minecraft 的 `crash-*.txt` 一直是
/// 这个形状；找不到就把整份报告的第一句非空行拿来当证据。
QString crashLine(const QString &text, const QString &prefix) {
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.startsWith(prefix)) return line.trimmed();
    }
    // ★ 兜底那一步（上面注释说的）：没有那两行的时候，第一句非空行就是这份报告在说什么。
    //   少了它，一份形状不太一样的报告会落成一句空证据（"崩溃了，原因不详"）。
    return firstMeaningfulLine(text);
}

QString firstMeaningfulLine(const QString &text) {
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty() && !trimmed.startsWith(QStringLiteral("----"))) return trimmed;
    }
    return QString();
}

int severityRank(Severity severity) {
    switch (severity) {
    case Severity::Problem: return 0;
    case Severity::Warning: return 1;
    case Severity::Info: return 2;
    case Severity::Ok: return 3;
    }
    return 4;
}

void appendFinding(Report *report, Severity severity, const QString &title,
                   const QString &detail = QString(), const QString &hint = QString()) {
    Finding finding;
    finding.severity = severity;
    finding.title = title;
    finding.detail = detail;
    finding.hint = hint;
    report->findings.append(finding);
    if (severity == Severity::Problem) ++report->problems;
    if (severity == Severity::Warning) ++report->warnings;
}

QString javaExecutableOf(const QString &path) {
    if (path.isEmpty()) return QString();
    const QFileInfo info(path);
    if (info.isDir()) {
        const QString inside = path + QStringLiteral("/bin/java.exe");
        return QFileInfo::exists(inside) ? inside : QString();
    }
    return path;
}

struct ModScan {
    QStringList enabled;
    QStringList disabled;
};

ModScan scanMods(const QString &modsDir) {
    ModScan scan;
    if (modsDir.isEmpty()) return scan;
    const QDir dir(modsDir);
    if (!dir.exists()) return scan;
    scan.enabled = dir.entryList({QStringLiteral("*.jar")}, QDir::Files, QDir::Name);
    scan.disabled = dir.entryList({QStringLiteral("*.jar.disabled")}, QDir::Files, QDir::Name);
    return scan;
}

struct CrashScan {
    QString path;
    QString name;
    QDateTime modified;
    QString text;
};

CrashScan newestCrash(const QString &crashDir) {
    CrashScan scan;
    if (crashDir.isEmpty()) return scan;
    const QDir dir(crashDir);
    if (!dir.exists()) return scan;
    const QFileInfoList files =
        dir.entryInfoList({QStringLiteral("crash-*.txt")}, QDir::Files, QDir::Time);
    if (files.isEmpty()) return scan;
    const QFileInfo &newest = files.first();
    scan.path = newest.absoluteFilePath();
    scan.name = newest.fileName();
    scan.modified = newest.lastModified();
    scan.text = readHead(scan.path, kCrashHeadBytes);
    return scan;
}

struct LogScan {
    bool exists = false;
    bool hasExitCode = false;
    int exitCode = 0;
    QString lastGameError;
    bool hasOutOfMemory = false;
    bool hasUnsupportedClassVersion = false;
};

LogScan scanLog(const QString &logPath) {
    LogScan scan;
    if (logPath.isEmpty() || !QFileInfo::exists(logPath)) return scan;
    scan.exists = true;
    const QString tail = readTail(logPath, kLogTailBytes);
    // `qDebug() << "Exit code:" << exitCode` 写出来就是这一行（前面可能带时间戳）。
    static const QRegularExpression exitPattern(QStringLiteral("Exit code:\\s*(-?\\d+)"));
    QRegularExpressionMatchIterator matches = exitPattern.globalMatch(tail);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        bool ok = false;
        const int code = match.captured(1).toInt(&ok);
        if (ok) {
            scan.hasExitCode = true;
            scan.exitCode = code;
        }
    }
    const QStringList lines = tail.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.contains(QStringLiteral("[Game Error]"))) scan.lastGameError = line.trimmed();
    }
    scan.hasOutOfMemory = tail.contains(QStringLiteral("OutOfMemoryError"));
    scan.hasUnsupportedClassVersion =
        tail.contains(QStringLiteral("UnsupportedClassVersionError"));
    return scan;
}

}  // namespace

QString severityName(Severity severity) {
    switch (severity) {
    case Severity::Problem: return QStringLiteral("问题");
    case Severity::Warning: return QStringLiteral("提醒");
    case Severity::Info: return QStringLiteral("信息");
    case Severity::Ok: return QStringLiteral("正常");
    }
    return QString();
}

QString Report::headline() const {
    if (problems > 0 || warnings > 0) {
        QStringList parts;
        if (problems > 0) parts << QStringLiteral("%1 个问题").arg(problems);
        if (warnings > 0) parts << QStringLiteral("%1 条提醒").arg(warnings);
        return QStringLiteral("启动诊断：发现 %1").arg(parts.join(QStringLiteral("、")));
    }
    return QStringLiteral("启动诊断：没有发现明显问题");
}

QString Report::toText() const {
    QStringList lines;
    lines << headline();
    if (!instanceLine.isEmpty()) lines << instanceLine;
    for (const Finding &finding : findings) {
        lines << QString();
        lines << QStringLiteral("【%1】%2").arg(severityName(finding.severity), finding.title);
        if (!finding.detail.isEmpty()) {
            const QStringList detailLines = finding.detail.split(QLatin1Char('\n'));
            for (const QString &line : detailLines) {
                lines << QStringLiteral("  %1").arg(line);
            }
        }
        if (!finding.hint.isEmpty()) lines << QStringLiteral("  → %1").arg(finding.hint);
    }
    return lines.join(QLatin1Char('\n'));
}

Report analyze(const Inputs &inputs) {
    Report report;
    QStringList where;
    if (!inputs.instanceName.isEmpty()) where << inputs.instanceName;
    QString version = inputs.gameVersion;
    if (version.isEmpty()) version = inputs.versionId;
    if (!version.isEmpty()) {
        if (!inputs.loaderName.isEmpty()) {
            version += inputs.loaderVersion.isEmpty()
                           ? QStringLiteral(" · %1").arg(inputs.loaderName)
                           : QStringLiteral(" · %1 %2").arg(inputs.loaderName, inputs.loaderVersion);
        }
        where << version;
    }
    if (!where.isEmpty()) {
        report.instanceLine = where.size() >= 2
                                  ? QStringLiteral("实例：%1（%2）").arg(where.at(0), where.at(1))
                                  : QStringLiteral("实例：%1").arg(where.at(0));
    }

    const ModScan mods = scanMods(inputs.modsDir);

    // ---------- 1. 版本与加载器 ----------
    if (inputs.versionId.isEmpty()) {
        appendFinding(&report, Severity::Problem,
                      QStringLiteral("这个实例没有指向任何游戏版本"),
                      QStringLiteral("实例目录里读不到可以启动的版本信息。"),
                      QStringLiteral("在实例设置里重新确认版本，或者重新安装这个实例。"));
    } else if (inputs.gameVersion.isEmpty()) {
        appendFinding(&report, Severity::Warning,
                      QStringLiteral("读不出这个实例真正的游戏版本"),
                      QStringLiteral("版本记录里没有 realVersion，界面上只能显示目录名。"),
                      QStringLiteral("检查版本 json 是否完整（「完整性检查」那一页可以扫）。"));
    }
    if (inputs.loaderName.isEmpty()) {
        if (inputs.versionId.isEmpty()) {
            // 上面那条已经说了，不重复。
        } else if (mods.enabled.size() > 0) {
            appendFinding(
                &report, Severity::Problem,
                QStringLiteral("没有模组加载器，却有 %1 个启用的模组").arg(mods.enabled.size()),
                QStringLiteral("mods/ 里躺着 %1 个 .jar，但这个实例是原版（没有 Fabric / Forge / "
                               "NeoForge / Quilt），游戏不会读它们。")
                    .arg(mods.enabled.size()),
                QStringLiteral("在这个版本上装一个模组加载器，或者把 mods/ 里那些 jar 移走。"));
        } else {
            appendFinding(&report, Severity::Ok, QStringLiteral("原版实例（没有模组加载器）"));
        }
    } else {
        appendFinding(&report, Severity::Ok,
                      QStringLiteral("加载器：%1%2").arg(
                          inputs.loaderName,
                          inputs.loaderVersion.isEmpty()
                              ? QString()
                              : QStringLiteral(" %1").arg(inputs.loaderVersion)));
    }

    // ---------- 2. Java ----------
    if (inputs.configuredJavaPath.isEmpty()) {
        if (inputs.autoSelectJava) {
            appendFinding(
                &report, Severity::Info, QStringLiteral("没有预先指定 Java，启动时自动挑"),
                inputs.requiredJavaMajor > 0
                    ? QStringLiteral("这个版本要 Java %1。").arg(inputs.requiredJavaMajor)
                    : QStringLiteral("版本 json 里没写要哪个 Java，启动时按游戏版本推断。"),
                QStringLiteral("如果本机没有匹配的 Java，第一次启动会先下载一份运行时。"));
        } else {
            appendFinding(&report, Severity::Problem,
                          QStringLiteral("Java 选的是「手动指定」，但路径是空的"),
                          QStringLiteral("手动档下启动器不会自己去挑 Java。"),
                          QStringLiteral("在「启动设置」里填一个 java.exe，或者切回「自动」。"));
        }
    } else {
        const QString executable = javaExecutableOf(inputs.configuredJavaPath);
        if (executable.isEmpty() || !QFileInfo::exists(executable)) {
            appendFinding(&report, Severity::Problem,
                          QStringLiteral("指定的 Java 用不了"),
                          QStringLiteral("路径：%1").arg(inputs.configuredJavaPath),
                          QStringLiteral("把路径改成一个真实存在的 java.exe（或 JDK/JRE 目录），"
                                         "或者切回自动。"));
        } else {
            appendFinding(
                &report, Severity::Ok, QStringLiteral("已指定 Java"),
                QStringLiteral("路径：%1").arg(executable),
                inputs.requiredJavaMajor > 0
                    ? QStringLiteral("这个版本要 Java %1；诊断不启动 JVM，具体主版本以启动日志为准。")
                          .arg(inputs.requiredJavaMajor)
                    : QString());
        }
    }

    // ---------- 3. 内存 ----------
    {
        QStringList memoryParts;
        if (inputs.xmxMb > 0) memoryParts << QStringLiteral("Xmx %1 MB").arg(inputs.xmxMb);
        if (inputs.xmsMb > 0) memoryParts << QStringLiteral("Xms %1 MB").arg(inputs.xmsMb);
        if (inputs.autoMemory) memoryParts << QStringLiteral("自动");
        if (inputs.systemTotalMb > 0) {
            memoryParts << QStringLiteral("机器共 %1 MB").arg(inputs.systemTotalMb);
        }
        const QString memoryDetail = memoryParts.join(QStringLiteral(" · "));

        if (inputs.xmxMb > 0 && inputs.systemTotalMb > 0
            && inputs.xmxMb > inputs.systemTotalMb) {
            appendFinding(
                &report, Severity::Problem,
                QStringLiteral("分给游戏的内存超过了机器总内存"),
                memoryDetail,
                QStringLiteral("把 Xmx 调小到机器内存以内；分得比物理内存还大，JVM 起来后"
                               "很可能在分配堆的时候就失败。"));
        } else if (inputs.xmxMb > 0 && inputs.systemTotalMb > 0
                   && inputs.xmxMb > inputs.systemTotalMb * 4 / 5) {
            appendFinding(&report, Severity::Warning,
                          QStringLiteral("分给游戏的内存偏多"),
                          memoryDetail,
                          QStringLiteral("留一点给系统和其他程序，尤其模组整合包还要加载资源。"));
        } else if (inputs.xmxMb > 0) {
            appendFinding(&report, Severity::Info, QStringLiteral("内存分配"), memoryDetail);
        }
        if (inputs.xmxMb > 0 && inputs.xmxMb < 512) {
            appendFinding(&report, Severity::Warning,
                          QStringLiteral("分给游戏的内存偏小（%1 MB）").arg(inputs.xmxMb),
                          memoryDetail,
                          QStringLiteral("模组整合包很容易 OutOfMemoryError；原版 1.20 起"
                                         "也建议至少 1024 MB。"));
        }
        if (inputs.systemTotalMb <= 0) {
            appendFinding(&report, Severity::Info,
                          QStringLiteral("读不到这台机器的物理内存"),
                          QStringLiteral("内存那一栏只报了配置值，没法判断相对大小。"));
        }
    }

    // ---------- 4. mods/ ----------
    if (!inputs.modsDir.isEmpty() && !QDir(inputs.modsDir).exists()) {
        appendFinding(&report, Severity::Info,
                      QStringLiteral("这个实例没有 mods 目录"),
                      QStringLiteral("路径：%1").arg(inputs.modsDir),
                      QStringLiteral("第一次往里面放模组时目录会自动建出来。"));
    } else if (mods.enabled.size() > 0 && !inputs.loaderName.isEmpty()) {
        appendFinding(&report, Severity::Ok,
                      QStringLiteral("已启用 %1 个模组").arg(mods.enabled.size()),
                      inputs.modsDir);
    } else if (!inputs.loaderName.isEmpty() && mods.enabled.isEmpty()) {
        appendFinding(&report, Severity::Info, QStringLiteral("mods/ 里没有启用的模组"),
                      inputs.modsDir.isEmpty() ? QString() : inputs.modsDir);
    }
    if (mods.disabled.size() > 0) {
        QStringList listed = mods.disabled.mid(0, kDisabledNameLimit);
        if (mods.disabled.size() > kDisabledNameLimit) {
            listed << QStringLiteral("…还有 %1 个").arg(mods.disabled.size() - kDisabledNameLimit);
        }
        appendFinding(&report, Severity::Info,
                      QStringLiteral("有 %1 个模组处于禁用状态").arg(mods.disabled.size()),
                      listed.join(QStringLiteral("、")),
                      QStringLiteral("它们在盘上叫 `*.jar.disabled`，游戏不会加载。"));
    }

    // ---------- 5. 最近的崩溃报告 ----------
    bool javaVersionProblemSeen = false;
    bool outOfMemorySeen = false;
    {
        const CrashScan crash = newestCrash(inputs.crashDir);
        if (crash.path.isEmpty()) {
            appendFinding(&report, Severity::Info, QStringLiteral("没有崩溃报告"));
        } else {
            const QString description = crashLine(crash.text, QStringLiteral("Description:"));
            const QString time = crashLine(crash.text, QStringLiteral("Time:"));
            QStringList detail;
            detail << QStringLiteral("文件：%1").arg(crash.name);
            if (!time.isEmpty()) detail << time;
            if (!description.isEmpty()) detail << description;
            const QString where = detail.join(QLatin1Char('\n'));
            const QString hint = QStringLiteral("完整内容在「崩溃报告」那一页。");

            if (crash.text.contains(QStringLiteral("java.lang.OutOfMemoryError"))) {
                outOfMemorySeen = true;
                appendFinding(&report, Severity::Warning,
                              QStringLiteral("最近一份崩溃报告是内存不足（OutOfMemoryError）"),
                              where,
                              QStringLiteral("把 Xmx 调大（先确认机器真的有空闲内存），"
                                             "或者减少同时加载的模组。"));
            } else if (crash.text.contains(QStringLiteral("UnsupportedClassVersionError"))) {
                javaVersionProblemSeen = true;
                appendFinding(
                    &report, Severity::Problem,
                    QStringLiteral("最近一份崩溃报告是 Java 版本不对（UnsupportedClassVersionError）"),
                    where,
                    QStringLiteral("这个崩溃说明有东西是用比当前 Java 更新的版本编译的；"
                                   "换一个更新的 Java 再试。"));
            } else if (crash.text.contains(QStringLiteral("NoClassDefFoundError"))
                       || crash.text.contains(QStringLiteral("ClassNotFoundException"))) {
                appendFinding(&report, Severity::Warning,
                              QStringLiteral("最近一份崩溃报告里是缺类异常"),
                              where,
                              QStringLiteral("多半是模组依赖没装齐，或者装了一个给别的游戏版本"
                                             "编的模组；看报告里那个类名属于哪个模组。"));
            } else if (crash.text.contains(QStringLiteral("Mixin"))
                       && (crash.text.contains(QStringLiteral("InjectionError"))
                           || crash.text.contains(QStringLiteral("Apply"))
                           || crash.text.contains(QStringLiteral("apply")))) {
                appendFinding(&report, Severity::Warning,
                              QStringLiteral("最近一份崩溃报告发生在模组注入阶段（Mixin）"),
                              where,
                              QStringLiteral("报告里通常会点名一个模组；先单独移走它再启动，"
                                             "确认是不是它引起的。"));
            } else {
                appendFinding(&report, Severity::Info,
                              QStringLiteral("最近一份崩溃报告：%1").arg(crash.name),
                              where, hint);
            }
        }
    }

    // ---------- 6. 上一次启动的日志 ----------
    {
        const LogScan log = scanLog(inputs.logPath);
        if (!log.exists) {
            appendFinding(&report, Severity::Info,
                          QStringLiteral("还没有启动日志"),
                          QStringLiteral("这个启动器还没在这个数据目录里启动过游戏。"));
        } else if (log.hasUnsupportedClassVersion && !javaVersionProblemSeen) {
            javaVersionProblemSeen = true;
            appendFinding(
                &report, Severity::Problem,
                QStringLiteral("启动日志里出现 UnsupportedClassVersionError（Java 太旧）"),
                log.lastGameError,
                QStringLiteral("换一个更新的 Java；具体要哪个版本查版本 json 或模组说明。"));
        }
        if (log.exists) {
            if (log.hasExitCode && log.exitCode != 0) {
                QString detail = log.lastGameError;
                if (detail.isEmpty()) detail = QStringLiteral("日志末尾没有 [Game Error] 那一行。");
                appendFinding(
                    &report, Severity::Warning,
                    QStringLiteral("上一次游戏以退出码 %1 结束").arg(log.exitCode), detail,
                    QStringLiteral("「日志」那一页有完整输出；崩溃报告能对上同一个时间的话，"
                                   "「崩溃报告」那一页有现场。"));
            } else if (log.hasExitCode) {
                appendFinding(&report, Severity::Ok,
                              QStringLiteral("上一次游戏正常退出（退出码 0）"),
                              log.lastGameError.isEmpty() ? QString() : log.lastGameError);
            } else if (!log.lastGameError.isEmpty()) {
                appendFinding(&report, Severity::Warning,
                              QStringLiteral("启动日志里有错误，但没有看到退出码"),
                              log.lastGameError,
                              QStringLiteral("可能是启动还没走完；「日志」那一页看最后几行。"));
            } else {
                appendFinding(&report, Severity::Info,
                              QStringLiteral("启动日志里没有看到游戏退出记录"),
                              QStringLiteral("可能是这次启动器启动之后还没启动过游戏。"));
            }
        }
        if (log.hasOutOfMemory && !outOfMemorySeen) {
            appendFinding(&report, Severity::Warning,
                          QStringLiteral("启动日志里出现 OutOfMemoryError"),
                          QStringLiteral("游戏在运行中内存不够了（不一定留下一份崩溃报告）。"),
                          QStringLiteral("把 Xmx 调大，或者关掉一些模组再试。"));
        }
    }

    std::stable_sort(report.findings.begin(), report.findings.end(),
                     [](const Finding &a, const Finding &b) {
                         return severityRank(a.severity) < severityRank(b.severity);
                     });
    return report;
}

}  // namespace StartupDiagnosis
