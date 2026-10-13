/**
 * @file ServerConsolePage.hpp
 * @brief 一个服务端的**控制台**那一页（coding plan §6.2 / 里程碑 M4-3）。
 * @author 0x7fffff
 * @date 2026-10-13
 *
 * 【它是干什么的】
 *
 * 服务端跑起来之后它是唯一能看见"它现在在说什么"的地方：上面那一大片是它的输出（原样，
 * 一行一行往下滚），底下一行是往里打字的地方（`say`、`op <玩家>`、`stop`… 它自己认得什么
 * 就是什么，我们一个命令都不拦、也不解释）。
 *
 * 【这一页只摆控件，判据一条都不在这儿】
 *
 * `ui/` **不 include `src/`**（见 `GroupDialog.hpp` 文件头那段老规矩），所以：
 *
 *   * **哪一档状态、那句话怎么说** —— 由 `MainWindow` 翻好（`ServerModel::statusName()`）
 *     用 `setStatusText()` 喂进来；
 *   * **「停止」/「强制结束」能不能点** —— 同样由那一层判（宽限时间那套在
 *     `ServerRun::gracefulStopMs()` / `ServerProcess::forceKillAllowed()` 里），这一页只
 *     照着一个 bool 摆灰/亮；
 *   * **写进它 stdin 的那一行** —— 这一页只把用户敲的字原样发出去（`commandSubmitted`），
 *     加不加换行、要不要拦下 `stop`，是那一层的事。
 *
 * 所以验证工程能离线把这一页整页驱动一遍（灌日志 → 读回来、敲一行 → 看信号）。
 *
 * 【日志是**有上限**的】
 *
 * `setMaxLines()` 就是 `QPlainTextEdit::setMaximumBlockCount()` —— 一个跑了几天的服务端能吐
 * 出几十万行，全留在这一页的文档里，滚动条会先疯掉、内存跟着涨。上限那个数由那一层给
 * （`ServerProcess::maxLogLines()`），这一页不自己定一个。
 */

#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

class ServerConsolePage : public QDialog {
    Q_OBJECT

public:
    explicit ServerConsolePage(QWidget *parent = nullptr);

    /// 覆盖层页眉上那行字（`OverlayHost::execPage()` 的标题）。
    static QString windowTitle() { return QStringLiteral("服务端控制台"); }

    /// 顶上那行大字：这个控制台说的是**哪一个**服务端（显示名，不是目录名）。
    void setServerName(const QString &name);
    /// 状态那行小字（`ServerModel::statusName()` 的结论，**由 `MainWindow` 翻好**）。
    void setStatusText(const QString &text);
    /// 「停止服务端」那颗能不能点（在跑、而且还没在停的时候才是亮的）。
    void setStopAvailable(bool available);
    /// 「强制结束」那颗能不能点（宽限时间到了才是亮的，见 `ServerRun::gracefulStopMs()`）。
    void setForceKillAvailable(bool available);
    /// 底下那一行输入能不能用（没在跑的时候不能 —— 写进去也没人读）。
    void setCommandAvailable(bool available);

    /// 日志最多留几行（`QPlainTextEdit::setMaximumBlockCount()`）。见文件头那段。
    void setMaxLines(int lines);
    /**
     * @brief 把日志整片换掉（开页时灌一次历史）。
     * @param lines 一行一条（不带换行）。空表 = 清空。
     */
    void setLog(const QStringList &lines);
    /// 往末尾续一行（服务端刚吐出来的那一行）。
    void appendLine(const QString &line);
    /// 现在这一页上留着的那些行（验证工程读它）。
    QStringList logLines() const;

    /// 底下那行输入框里现在写着什么。
    QString commandText() const;

signals:
    /// 用户敲了一行并按了「发送」（或按了回车）。**原样**，没加换行也没过滤。
    void commandSubmitted(const QString &command);
    /// 用户按了「停止服务端」（走 `ServerProcess::requestStop()`）。
    void stopRequested();
    /// 用户按了「强制结束」。
    void forceKillRequested();

private:
    QLabel *m_titleLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPlainTextEdit *m_logView = nullptr;
    QLineEdit *m_commandEdit = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_forceKillButton = nullptr;
};
