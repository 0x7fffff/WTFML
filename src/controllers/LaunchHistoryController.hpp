/**
 * @file LaunchHistoryController.hpp
 * @brief 「上一次启动」那本账（coding plan §5.1）从主窗口搬出来：什么时候记、记的是谁、结局怎么改。
 *
 * 【为什么这一件事该有一个自己的 controller】
 *
 * 判据层（`manage/LaunchHistory`）早就有了 —— 纯函数，verify 里逐格念。缺的是**驱动那一层**：
 * 它一直长在 `MainWindow` 里，和"哪一份实例现在选着""这一趟进的是哪个世界"混在一起。搬出来
 * 的这四条是**决策**，不是转手：
 *
 * · **认不认这一趟**：只认"进程真的起来了"那一刻（`onStartButtonClicked()` 里 `sessionId > 0`
 * 才走得到这儿，登录失败 / Java 挑不出来那几条早退路径一个字都不记）。
 * · **记的是谁**：实例显示名取**启动那一刻**那一句（实例以后改名、被删都不影响这一条 ——
 * 这正是"记录"该有的样子），账号键取**当时**的身份（离线账号 uuid 是空的，退回账号名）。
 * · **快捷启动那个目标**：世界按目录名认、显示名就是那一串；服务器地址是稳定键而**名字会改**，
 * 所以现查一遍 `servers.dat`（查不到就让它空着，卡片上退回写地址，不多一句假话）。
 * · **结局只落一次**：靠"启动时刻"这一把钥匙追回启动时写的那一条（退出那一刻会话已经从
 * `LauncherManager` 的表里摘掉了），`result == Running` 是第二道闸。
 *
 * 【边界（谁在这一层里，谁不在）】
 *
 * · 在：内存里那一份账、读盘那一次、写盘的时机、失败只记日志不弹窗、快捷启动目标怎么翻。
 * · 不在：**显示名和身份键从哪儿来**。`recordStart()` 让调用方把这两格喂进来 ——
 * `MainWindow::instanceDisplayName()` 读的是界面那个下拉框、`launchIdentityKey()` 读的是
 * 当前账号，两样都是主窗口才知道的事。这一层不认识中文文案，也不认识 `ui/`。
 * · 不在：**主页两排快速入口怎么摆**（`MainWindow::updateHomeRecent()`）—— 那要读下拉框里
 * 每一行的 `QIcon` 和收藏分组，然后交给 `HomeQuickController::build()`。这一层只在账本
 * 真的动了的时候发一声 `changed()`。
 */

#pragma once

#include "launcher/QuickPlay.hpp"  // QuickPlay::Target（recordStart 的入参，得看见定义）
#include "manage/LaunchHistory.hpp"

#include <QList>
#include <QObject>
#include <QString>

class LaunchHistoryController final : public QObject {
    Q_OBJECT

public:
    explicit LaunchHistoryController(QObject *parent = nullptr);

    /// 账上那一条的结局：从"这一趟结束时的两个事实"翻过来。
    ///
    /// 这里**不是**"崩没崩"两个分支：游戏连第一行日志都没打出来就结束了的那一趟
    /// （`neverStarted`）单独算一档 —— §5.1 说"上一次"以**成功创建游戏进程**为准，那种
    /// 情况进程确实起来了、用户却什么都没玩到，拿它当"上次玩的就是这一份"是撒谎。
    static LaunchHistory::Result resultFor(bool crashed, bool neverStarted);

    /// 账上那个目标 ⇄ 启动参数那一层的 `Kind`（**唯一出处**：写记录、"还能不能接着玩"、
    /// 那一颗按钮按下去三处都读它 —— 三处各写一遍 switch，迟早会有一处写反，症状是
    /// "接着游玩进的是服务器那一页的参数"）。
    static QuickPlay::Kind quickPlayKindFor(LaunchHistory::Target target);

    /// 账本从盘上读回来（构造末尾那一次）。读不出来 = 空账，**不是错误**：一次都没启动过
    /// 的新用户就是这样。读完发一声 `changed()`（哪怕是空账 —— 该把那张卡收起来）。
    void load();

    /// 账本**内存里那一份**（`load()` 读过一次之后就只有 `recordStart()` / `recordFinish()`
    /// 会动它）。
    ///
    /// 不每次现读盘：主页快速入口要在"启动成功""游戏退出"两个时机重画，而那是**热路径**
    /// （启动那一发已经在同步等进程了）。读一次、写的时候顺手更新内存那一份。
    const QList<LaunchHistory::Record> &records() const { return m_records; }

    /// 把"这一趟成功了"记进账本。**产品那一侧唯一的调用点**是 `onStartButtonClicked()`
    /// 里 `sessionId > 0` 那一段（进程真的起来了那一刻）—— §5.1 明写「以成功创建游戏进程
    /// 为准，不以点击启动按钮为准」。
    ///
    /// @param instanceId 实例 id（= `versions/` 下面那个目录名）。
    /// @param instanceName **启动那一刻**那一句显示名（`MainWindow::instanceDisplayName()`）。
    /// @param accountKey 当时用的账号（`MainWindow::launchIdentityKey()`）；空 = 记不下来。
    /// @param quickPlay 这一趟要进哪儿（空的 = 普通启动）。**`Target::None` 不是"不知道"，
    /// 是"这一趟就是普通启动"** —— §5.1：普通进主菜单那一趟不许猜。
    /// @param startedAtMs 启动时刻（毫秒；取会话自己的 `startedAtMs`，不是"现在"）。
    void recordStart(const QString &instanceId, const QString &instanceName,
                     const QString &accountKey, const QuickPlay::Target &quickPlay,
                     qint64 startedAtMs);

    /// 那一趟结束了：把账上**最近那一条还写着"还在跑"、且启动时刻对得上**的记录改写成
    /// 真正的结局与时长。
    ///
    /// 认的是**启动时刻**这个键：退出那一刻会话已经从 LauncherManager 的表里摘掉了
    /// （见 `MainWindow::onGameFinished()` 那段 NOTE），追回"启动那一刻记的那一条"只有它。
    void recordFinish(qint64 startedAtMs, LaunchHistory::Result result, qint64 finishedAtMs);

signals:
    /// 账本动了（读回来 / 记了一条 / 改了一条的结局）。**只在真的动了的那三条路上发** ——
    /// 结局没对上任何一条（`recordFinish()` 空转）那一趟不发，因为什么都没变。
    void changed();

    /// 给人事后看的一行日志（主窗口的 `appendLog()`）。写不进去只记一句，**不弹窗**：
    /// 这一趟的启动一点没被挡住（游戏照样起来了），拿一个提示拦在用户和"进游戏"之间不成
    /// 比例 —— 和 `ResourceStore` / `InstanceIconLookup` 那几处"best-effort 写盘"同一口径。
    void logMessage(const QString &text);

private:
    QList<LaunchHistory::Record> m_records;
};
