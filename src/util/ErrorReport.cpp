/**
 * @file ErrorReport.cpp
 * @brief Implements the error-report text composition.
 * @author 0x7fffff
 * @date 2026-09-28
 */

#include "ErrorReport.hpp"

#include <QStringList>
#include <QSysInfo>

namespace {

/// 报告头部那几行里，一个字段写成 `名字：值`。
QString field(const QString &name, const QString &value) {
    return name + QStringLiteral("：") + value;
}

/// 文件名里不许出现的那些字符（Windows 那一套；换成一个下划线）。
QString sanitizeForFileName(const QString &text) {
    QString out = text;
    static const QString kForbidden = QStringLiteral("\\/:*?\"<>|");
    for (int i = 0; i < out.size(); ++i) {
        if (kForbidden.contains(out.at(i))) {
            out[i] = QLatin1Char('_');
        }
    }
    // 首尾的空白会让这个文件名在资源管理器里看着像"少了一截"，顺手收掉。
    return out.trimmed();
}

/// 时间那一格统一写成这个形状（两种文件名里也是它，**只有一处**）。
QString stampForName(const QDateTime &now) {
    return now.toString(QStringLiteral("yyyyMMdd-HHmmss"));
}

}  // namespace

namespace ErrorReport {

QString systemDescription() {
    // `prettyProductName()` 是"Windows 11 Home China"这种给人看的名字，内核版本号在
    // `kernelVersion()` 里 —— 两个都要：只看前一个不知道是哪一版，只看后一个不知道是哪个
    // 发行版（排查"只有 Win11 出的问题"时两样都用得上）。
    return QStringLiteral("%1 (%2) %3")
        .arg(QSysInfo::prettyProductName(), QSysInfo::kernelVersion(),
             QSysInfo::currentCpuArchitecture());
}

QString header(const Facts &facts, const QDateTime &now) {
    QStringList lines;
    lines.append(QStringLiteral("WTFML 错误报告"));
    lines.append(field(QStringLiteral("生成时间"),
                       now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    if (!facts.launcherVersion.isEmpty()) {
        lines.append(field(QStringLiteral("启动器版本"), facts.launcherVersion));
    }
    if (!facts.systemDescription.isEmpty()) {
        lines.append(field(QStringLiteral("系统"), facts.systemDescription));
    }
    // 实例那两行：显示名和 id **都写**。显示名是人认的，id 是盘上那个目录名 ——
    // 拿到报告的人要照着它去找是哪一个实例（用户自定义过名字时两者不一样）。
    if (!facts.instanceName.isEmpty()) {
        lines.append(field(QStringLiteral("实例"), facts.instanceName));
    }
    if (!facts.instanceId.isEmpty()) {
        lines.append(field(QStringLiteral("实例 id"), facts.instanceId));
    }
    if (!facts.accountName.isEmpty()) {
        lines.append(field(QStringLiteral("账号"), facts.accountName));
    }
    if (facts.processId > 0) {
        lines.append(field(QStringLiteral("进程 ID"), QString::number(facts.processId)));
    }
    if (facts.startedAt.isValid()) {
        lines.append(field(QStringLiteral("启动时间"),
                           facts.startedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    }
    // 退出那一行只在**真的收到过**的时候写。游戏还在跑的时候编一个"退出码：0"出来
    // 是最坏的一种假话：看报告的人会照着它去排除"崩溃"这个方向。
    if (facts.finished) {
        lines.append(field(QStringLiteral("退出码"),
                           facts.crashed
                               ? QStringLiteral("%1（异常终止）").arg(facts.exitCode)
                               : QString::number(facts.exitCode)));
    } else {
        lines.append(field(QStringLiteral("退出码"), QStringLiteral("（导出时游戏还在运行）")));
    }
    return lines.join(QLatin1Char('\n'));
}

QString sectionDivider(const QString &title) {
    return QStringLiteral("------------------------------ %1 ------------------------------").arg(title);
}

QString text(const Facts &facts, const QDateTime &now, const QString &errors, const QString &log) {
    QString out = header(facts, now);
    out += QStringLiteral("\n\n");

    out += sectionDivider(QStringLiteral("错误与致命（共 %1 行）").arg(facts.errorLines));
    out += QLatin1Char('\n');
    if (errors.isEmpty()) {
        // 照样写上那一段（理由见 .hpp）：空着的话看的人分不清"没出错"和"导出漏了"。
        out += QStringLiteral("（这一段里一行都没有 —— 这一趟没有 ERROR 以上的输出。日志本身"
                              "在下面。）");
        out += QLatin1Char('\n');
    } else {
        out += errors;
        // `logTextFrom()` 每行都带换行，只有末尾那半行可能不带 —— 缺了就补一个，
        // 免得两段的分隔线粘在最后一行后面。
        if (!out.endsWith(QLatin1Char('\n'))) {
            out += QLatin1Char('\n');
        }
    }

    out += QLatin1Char('\n');
    out += sectionDivider(QStringLiteral("完整日志（共 %1 行）").arg(facts.totalLines));
    out += QLatin1Char('\n');
    out += log;
    if (!out.endsWith(QLatin1Char('\n'))) {
        out += QLatin1Char('\n');
    }
    return out;
}

QString suggestedFileName(const QString &instanceName, const QDateTime &now) {
    const QString name = sanitizeForFileName(instanceName);
    // 实例名拿不到（会话快照是空的）时不留一个空档：那样文件名会是"错误报告--20260928..."，
    // 看着像坏了。
    const QString middle = name.isEmpty() ? QString() : name + QLatin1Char('-');
    return QStringLiteral("错误报告-%1%2.txt").arg(middle, stampForName(now));
}

QString suggestedLogFileName(const QString &instanceName, const QDateTime &now) {
    const QString name = sanitizeForFileName(instanceName);
    const QString middle = name.isEmpty() ? QString() : name + QLatin1Char('-');
    return QStringLiteral("日志-%1%2.log").arg(middle, stampForName(now));
}

}  // namespace ErrorReport
