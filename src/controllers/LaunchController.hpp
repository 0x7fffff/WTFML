#pragma once
#include <QObject>
#include "MainWindow.hpp"
class LaunchController final : public QObject {
 Q_OBJECT
public:
 explicit LaunchController(MainWindow *owner);
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
    /// 取消正在走的那一趟启动。**只是竖个旗子**，真正的退出发生在下一个检查点
    /// （换令牌之后 / 问完 Java 之后 / 下完 Java 之后 / `startGame()` 之前）——
    /// 那些正是嵌套事件循环把用户输入放进来过的地方。
    /// @p byUser 为 true：用户在主页启动卡片上点了「取消」（日志写「启动已取消。」）；
    /// 默认是关窗那一路（日志写「启动器正在关闭」）。
    void abortStart(bool byUser = false);
private:
 MainWindow *m_owner = nullptr;
 /// 这一条启动路**此刻正在走**。判据在 `onStartButtonClicked()` 开头（见那儿的长注释）：
 /// 换令牌 / 下 Java 那两段同步等待里会转起嵌套事件循环，那个循环不排用户输入，
 /// 「启动」按钮照样点得动 —— 不挡的话同一条路会并行走两遍。
 bool m_startInProgress = false;
 /// `abortStart()` 竖的旗子。**每趟开头清掉**（否则上一趟的取消会把下一趟立刻掐死）。
 bool m_abortRequested = false;
 bool m_abortByUser = false;
};

