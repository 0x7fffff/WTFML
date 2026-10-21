/**
 * 【为什么单独一个文件，而不是写在 MainWindow 里拼】
 *
 * 这一份文本**是要发给别人的**（用户把文件拖进群里、贴进 issue），所以它长什么样得能
 * 被断言钉住 —— 而 MainWindow 那份类里拼出来的字符串没有任何办法在验证工程里量一遍
 * （它要一整个主窗、要真的游戏会话）。这里全是纯值进、QString 出：时间由调用方传进来
 * （不读当前时钟），系统那两行由这里现读（QSysInfo 是 Qt 的，不是业务层的东西）。
 *
 * 【它不知道"日志窗"是什么】
 *
 * 错误摘录和完整日志都是**调用方读好了递进来的两段字符串**（`ui/GameLogWindow` 给了
 * `logTextFrom()` / `allLogText()` 两个口子）。于是"哪几档算错误"这件事只有窗那边一个
 * 出处 —— 这里不去按字符串重新认一遍级别（那样迟早会出现"屏幕上标红的那行、报告里没有"）。
 */

#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace ErrorReport {

/**
 * 全部由调用方从**它自己那边**取：这个文件读不到盘、也看不见会话表。
 */
struct Facts {
    /// 启动器版本（`QCoreApplication::applicationVersion()`，空的时候调用方给个兜底）。
    QString launcherVersion;
    /// 这个实例的**显示名**（用户认得出的那个）。
    QString instanceName;
    /// 实例 id（= `versions/` 下面那个目录名）。
    QString instanceId;
    /// 这次用的身份（账号名）。
    QString accountName;
    /// 操作系统那一行（**用下面那个 systemDescription() 填**；空的时候这一行整个不写）。
    QString systemDescription;
    /// 进程 ID（0 = 没拿到）。
    qint64 processId = 0;
    /// 进程起来那一刻（无效 = 没拿到）。
    QDateTime startedAt;
    /// 有没有收到过"这一份结束了"（见 finished 那一句）。
    bool finished = false;
    /// 是不是异常终止（`QProcess::ExitStatus == CrashExit`）。
    bool crashed = false;
    /// 退出码（只在 finished 为真时有意义）。
    int exitCode = 0;
    int errorLines = 0;
    int totalLines = 0;
    /// 递进来的那两段文本是不是**只截了末尾一截**。
    ///
    /// 崩溃报告那条路为了"发到群里的文件不该有几十兆"，只留错误最后 200 行、日志最后
    /// 500 行（`GameExitDiagnosis::kExcerptLineLimit` / `kLogTailLineLimit`）。原来那两个
    /// 标题照样写「共 N 行」，可正文只有末尾一截 —— 拿到报告的人以为线索全在，其实早期
    /// 的报错已经被截掉了。这两个位为 true 时标题改成「末尾 N 行 / 原始共 M 行」。
    ///
    /// NOTE: 默认 false（**不截就当没截**）—— 日志窗那条路给的就是全量（自己的行数也按
    /// 全量数），它必须逐字节保持原来的标题。
    bool errorTruncated = false;
    bool logTruncated = false;

    // ---------- 下面这几样只有「游戏异常退出」那一项会填（§4.3）----------

    /// 这一趟实际用的 Java：`21（D:\...\java.exe）`。空 = 不写这一行。
    QString javaVersion;
    /// 这个实例的加载器：`Fabric 0.15.11`。空 = 不写这一行（原版实例就是空）。
    QString loaderVersion;
    QString memoryArgs;
    /// 「可能的原因」那几句（`GameExitDiagnosis::hints()` 给的）。空 = 那一节整个不写。
    QStringList hints;
};

/// 这台机器是什么（`Windows 11 Home China (10.0.26200) x86_64` 这样一行）。
///
/// NOTE: 只有 `QSysInfo` 一处出处，所以它放在这个文件里而不是调用方那边 —— 报告的头部
/// 是**给别人看的**，那一行的形状要能跟着报告一起被断言钉住。
QString systemDescription();

QString header(const Facts &facts, const QDateTime &now);

/// 两段之间的那行分隔。`sectionDivider("错误与致命（共 3 行）")`。
QString sectionDivider(const QString &title);

/**
 */
QString text(const Facts &facts, const QDateTime &now, const QString &errors, const QString &log);

/// NOTE: 实例名里可能有文件名不允许的字符（冒号、斜杠…），这里**换成下划线**：
/// 存不下的时候用户的感受是"导出失败了"，而原因只是一个名字里有冒号。
QString suggestedFileName(const QString &instanceName, const QDateTime &now);

/// 日志文件名的推荐值（`日志-实例名-20260928-210311.log`）。和上面同一个理由与同一条
/// 清洗规矩（**一处**，见 .cpp 里那段）。
QString suggestedLogFileName(const QString &instanceName, const QDateTime &now);

}  // namespace ErrorReport
