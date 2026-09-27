/**
 * @file ErrorReport.hpp
 * @brief 「错误报告」那一份文本怎么拼（纯函数：给一组事实，回一段可以直接发出去的文本）。
 * @author 0x7fffff
 * @date 2026-09-28
 * @note 用户原话：「把日志和错误报告导出功能加上，就在那个日志窗口下方，右下角放着」。
 *
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

namespace ErrorReport {

/**
 * @brief 一份报告里那些"事实"（报告头部那几行）。
 *
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
    /// 报告里那两段日志各有多少行（**由调用方数好**：行数在这里数等于把两段文本再切一遍）。
    int errorLines = 0;
    int totalLines = 0;
};

/// 这台机器是什么（`Windows 11 Home China (10.0.26200) x86_64` 这样一行）。
///
/// NOTE: 只有 `QSysInfo` 一处出处，所以它放在这个文件里而不是调用方那边 —— 报告的头部
///       是**给别人看的**，那一行的形状要能跟着报告一起被断言钉住。
QString systemDescription();

/// 生成这一份报告时的时间。**由调用方传**（不在这里读时钟）—— 验证工程要能钉死它。
QString header(const Facts &facts, const QDateTime &now);

/// 两段之间的那行分隔。`sectionDivider("错误与致命（共 3 行）")`。
QString sectionDivider(const QString &title);

/**
 * @brief 拼出整份报告：头部 + 错误摘录 + 完整日志。
 * @param errors 不轻于 Error 的那些行（可以是空串 —— 一档都没有时那一段会明说"没有"）。
 * @param log 全部日志。
 *
 * NOTE: `errors` 为空**照样写上那一段**（写一句"这一段里没有 Error/Fatal"）：一份缺了
 *       那一段的报告，看的人分不清"没出错"和"导出的时候漏了"。
 */
QString text(const Facts &facts, const QDateTime &now, const QString &errors, const QString &log);

/// 报告文件名的推荐值（`错误报告-实例名-20260928-210311.txt`）。
///
/// NOTE: 实例名里可能有文件名不允许的字符（冒号、斜杠…），这里**换成下划线**：
///       存不下的时候用户的感受是"导出失败了"，而原因只是一个名字里有冒号。
QString suggestedFileName(const QString &instanceName, const QDateTime &now);

/// 日志文件名的推荐值（`日志-实例名-20260928-210311.log`）。和上面同一个理由与同一条
/// 清洗规矩（**一处**，见 .cpp 里那段）。
QString suggestedLogFileName(const QString &instanceName, const QDateTime &now);

}  // namespace ErrorReport
