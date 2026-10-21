/**
 * @file LaunchHistoryController.cpp
 * @brief 「上一次启动」那本账的驱动层：见头文件那段"为什么"。判据全在 `manage/LaunchHistory`。
 *
 * 这一片原先整段长在 `MainWindow.cpp` 的「「上一次启动」那本账」那一节里， 原样
 * 搬过来，只做了两处**边界上的**改动（行为一个字没变）：
 *
 * · `recordStart()` 多两个入参（实例显示名 / 账号键）—— 那两格分别要读界面上的下拉框和
 * 当前账号，是主窗口才知道的事，这一层不许自己去问。
 * · 原来直接调 `updateHomeRecent()` 的三处改成 `emit changed()`，主窗口把它接回同一个
 * 槽函数上（发的位置逐处照旧：`load()` 和 `recordStart()` 收尾各一发，`recordFinish()`
 * **只在真改到了一条的时候**发 —— 空转那一趟什么都不变，不必重画）。
 */

#include "LaunchHistoryController.hpp"

#include "manage/ServerListFile.hpp"
#include "util/NbtUtil.hpp"

#include <QDateTime>
#include <QDir>

LaunchHistoryController::LaunchHistoryController(QObject *parent) : QObject(parent) {}

LaunchHistory::Result LaunchHistoryController::resultFor(bool crashed, bool neverStarted) {
    // 判据的**唯一出处**在这儿，两个读者都走它：产品那一侧（onGameFinished 里现算的两个
    // 事实 —— 退出码是不是异常、游戏有没有打出一行日志）和验证工程摆的那一位。
    if (neverStarted) {
        return LaunchHistory::Result::StartFailed;
    }
    return crashed ? LaunchHistory::Result::Crash : LaunchHistory::Result::NormalExit;
}

QuickPlay::Kind LaunchHistoryController::quickPlayKindFor(LaunchHistory::Target target) {
    return target == LaunchHistory::Target::Server ? QuickPlay::Kind::Multiplayer
                                                   : QuickPlay::Kind::Singleplayer;
}

void LaunchHistoryController::load() {
    // 文件不在 / 认不出来 = 空账（`LaunchHistory::load()` 的规矩），不是错误：一次都没
    // 启动过的新用户就是这样，那时主页那张卡整块收起来。
    m_records = LaunchHistory::load(LaunchHistory::historyPath());
    emit changed();
}

void LaunchHistoryController::recordStart(const QString &instanceId, const QString &instanceName,
                                          const QString &accountKey,
                                          const QuickPlay::Target &quickPlay, qint64 startedAtMs) {
    LaunchHistory::Record record;
    record.instanceId = instanceId;
    // **启动那一刻**那一句显示名（里面已经带着版本和加载器）。实例以后改名、被删都不影响
    // 这一条 —— "记录"该有的样子就是当时那句话。
    record.instanceName = instanceName;
    record.accountKey = accountKey;
    record.startedAt = startedAtMs > 0 ? QDateTime::fromMSecsSinceEpoch(startedAtMs)
                                       : QDateTime::currentDateTime();
    // 写下去那一刻它是"还在跑"：结局在 onGameFinished 里改写（`recordFinish()`）。
    record.result = LaunchHistory::Result::Running;
    if (!quickPlay.isEmpty()) {
        // 「世界 / 服务器只有通过 Quick Play 启动时才记录，普通进主菜单不能猜」（§5.1）——
        // 走到这一支就说明**这一趟确实是游戏自己去那个进度**，值就是命令行上发出去的那一串
        // （存档目录名 / 服务器地址），不另算一遍。
        record.target = quickPlay.kind == QuickPlay::Kind::Multiplayer
                            ? LaunchHistory::Target::Server
                            : LaunchHistory::Target::World;
        record.targetId = quickPlay.value;
        if (record.target == LaunchHistory::Target::World) {
            // 世界就是按**目录名**认的（见 WorldFiles.hpp 那段"两边都不读 level.dat 里的
            // 世界名"），所以显示名 = 那一串本身就是对的。
            record.targetDisplayName = quickPlay.value;
        } else {
            // 服务器：地址是稳定键，名字会改 —— 卡片上写名字更好认，所以现查一遍
            // `servers.dat`（实例设置里那一页读的就是同一份）。查不到（这一台刚被删掉）
            // 就让它空着，`targetLine()` 会退回写地址，卡片上不会多一句假话。
            QList<NbtUtil::ServerEntry> entries;
            QString problem;
            NbtUtil::readServerList(ServerListFile::pathFor(instanceId), &entries, &problem);
            for (const NbtUtil::ServerEntry &entry : entries) {
                if (entry.address == quickPlay.value) {
                    record.targetDisplayName = entry.name;
                    break;
                }
            }
        }
    }
    m_records = LaunchHistory::prepend(m_records, record);
    // 写不进去只记一句日志，**不弹窗**：这一趟的启动一点没被挡住（游戏照样起来了），只是
    // 主页那张卡下次少一条记录 / 还显示上一条 —— 拿一个提示拦在用户和"进游戏"之间不成
    // 比例。和 `ResourceStore` / `InstanceIconLookup` 那几处"best-effort 写盘"同一个口径。
    if (!LaunchHistory::save(LaunchHistory::historyPath(), m_records)) {
        emit logMessage(tr("没能把这次启动记进「上次游玩」：%1 写不进去。")
                            .arg(QDir::toNativeSeparators(LaunchHistory::historyPath())));
    }
    emit changed();
}

void LaunchHistoryController::recordFinish(qint64 startedAtMs, LaunchHistory::Result result,
                                           qint64 finishedAtMs) {
    if (startedAtMs <= 0) {
        return;  // 没记过这一趟（账上那条本该在），什么都不用改
    }
    for (LaunchHistory::Record &record : m_records) {
        // 认的是**启动时刻**这一把钥匙：同一次启动写下去的就是它，退出那一刻会话早从
        // LauncherManager 的表里摘掉了（见 onGameFinished 那段 NOTE）。
        // `result == Running` 那一位是第二道闸：终态只落一次（重复的退出信号不改写第二次）。
        if (record.result != LaunchHistory::Result::Running
            || record.startedAt.toMSecsSinceEpoch() != startedAtMs) {
            continue;
        }
        record.result = result;
        if (finishedAtMs >= startedAtMs) {
            record.durationSeconds = (finishedAtMs - startedAtMs) / 1000;
        }
        if (!LaunchHistory::save(LaunchHistory::historyPath(), m_records)) {
            emit logMessage(tr("没能把这次启动的结局写进「上次游玩」：%1 写不进去。")
                                .arg(QDir::toNativeSeparators(LaunchHistory::historyPath())));
        }
        emit changed();
        return;
    }
}
