#pragma once
#include <QObject>
#include <atomic>
#include "MainWindow.hpp"
class LaunchController final : public QObject {
 Q_OBJECT
public:
 explicit LaunchController(MainWindow *owner);
    /// 这一趟启动是被**谁**取消的。早退时的日志和任务的终态都按它分：
    /// 关窗取消照旧写「启动已取消：启动器正在关闭。」，任务中心 / 主页卡片取消写「启动已取消。」。
    enum class StartCancelSource {
        None,           ///< 没人取消
        LauncherClosing,///< 关窗（`MainWindow::closeEvent()` 问过用户之后）
        TaskCenter,     ///< 任务中心（或主页启动卡片 #65）的「取消」
    };
    QString launchIdentityKey() const;
    bool confirmLaunchWhileRunning(const QString &versionId);
    QStringList launchArguments(const QString &versionId, const QJsonObject &versionJson, const EffectiveJavaSettings &javaSettings, const QString &token, bool offlineMode, const QuickPlay::Target &quickPlay) const;
    void onStartButtonClicked();
    void appendLaunchRecord(const QString &versionId, const QuickPlay::Target &quickPlay, qint64 startedAtMs,
                            const QString &accountKey = QString());
    /// 这一条启动路**此刻还走在半路**（见 `m_startInProgress`）。关窗那一问要用它：
    /// 换令牌 / 下 Java 的同步等待里用户照样能点关闭，那时游戏一个都还没起来，
    /// `closeEvent` 按"没有游戏在跑"放行 —— 于是窗口关了、启动却还在继续跑，
    /// 最后在用户看不见的地方把一个游戏/一个运行时下载留在那儿。
    bool isStarting() const { return m_startInProgress; }
    /// 取消正在走的那一趟启动。**只是竖旗子**，真正的退出发生在下一个检查点
    /// （换令牌之后 / natives 之后 / 问完 Java 之后 / 下完 Java 之后 / `startGame()` 之前）——
    /// 那些正是嵌套事件循环把用户输入放进来过的地方。
    /// @param source 谁要取消。它只决定日志那一句和任务终态，不改流程。任务中心与主页
    ///   卡片走 `TaskCenter`；关窗那一问走 `LauncherClosing`。
    void abortStart(StartCancelSource source);
    bool ownsStartupRepair(const QString &versionId) const {
        return !m_startupRepairVersion.isEmpty() && m_startupRepairVersion == versionId;
    }
    /// 启动流程早退时给那件任务收尾（`TaskAbortGuard` 调的就是这一句）：中途被取消的
    /// 按「已取消」收（用户自己按的，不是失败），其余早退照旧按失败收。
    void finishAbortedStart(Tasks::TaskId launchTask, const QString &failReason = QString());

    /// 测试用：直接摆出/读回「这一趟正在走」和取消旗子 —— 真实入口只有
    /// `onStartButtonClicked()`、`abortStart()` 两处。
    void setStartingForTest(bool starting) { m_startInProgress = starting; }
    bool abortRequestedForTest() const { return m_abortRequested; }
    StartCancelSource cancelSourceForTest() const { return m_cancelSource; }
private:
 QString verifyStartupFiles(const QString &versionId, const QJsonObject &versionJson,
                            Tasks::TaskId launchTask);
 MainWindow *m_owner = nullptr;
 QString m_startupRepairVersion;
 /// 这一条启动路**此刻正在走**。判据在 `onStartButtonClicked()` 开头（见那儿的长注释）：
 /// 换令牌 / 下 Java 那两段同步等待里会转起嵌套事件循环，那个循环不排用户输入，
 /// 「启动」按钮照样点得动 —— 不挡的话同一条路会并行走两遍。
 bool m_startInProgress = false;
 /// `abortStart()` 竖的旗子。**每趟开头清掉**（否则上一趟的取消会把下一趟立刻掐死）。
 bool m_abortRequested = false;
 /// 这一趟是被谁取消的：只在早退写日志和收尾那两处读。
 StartCancelSource m_cancelSource = StartCancelSource::None;
 /// 给 `GlobalJavaSettings::ensureJavaForVersion()` 的合作式取消位：它跑在工作线程上，
 /// 取消时靠这一位让下载循环在下一个数据块上停下并删掉半截压缩包。同样每趟开头清掉。
 std::atomic_bool m_abortWork{false};
};

