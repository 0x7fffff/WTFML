/**
 * @file TaskHistory.hpp
 * @brief 任务历史的**落盘**：只存摘要与错误，存成一份 JSON。
 *
 * 任务历史只持久化摘要与错误，**不持久化 QObject、进程句柄或活动
 * 网络请求**。启动器异常退出后，上次未完成的项目在历史里标记为'已中断'，不自动假装继续」。
 *
 * 所以这个文件里存的是 `TaskSnapshot` 的**值那一半**：号、种类、状态、标题、目标、两个
 * 时间戳、错因。不存步骤（步骤是"这一次跑到哪儿"的细节，重开一次启动器之后那一串「✓ 校验
 * 文件」已经没有意义，留着反而像"它还在跑"）、不存进度（同上）、不存能力位（能力是**当下**
 * 它允许什么操作，和盘上那行历史没关系）。
 *
 * 【落在哪、为什么不叫 .json】
 *
 * 默认落在 `<启动器自带数据目录>/wtfml.tasks`。它是启动器级账本，不能跟着用户当前选中的
 * 游戏文件夹切换；否则内存里的任务会在下一次落盘时被整本写进另一份 `.minecraft`。这个
 * 固定目录仍在 exe 旁边，因此保留“整个启动器连带数据可以整体拷走”的约定。
 * **扩展名故意不是 `.json`**：那一条约定写在
 * `src/util/AppPaths.hpp` 的 `instanceGroupFilePath()` 那一段（"它是给启动器读写的配置文件，
 * 扩展名跟别的 json 混在一起，早晚有人拿它当'某个版本的元数据'去读"）。设计草图
 * 里写的是 `tasks.json`，落成 `wtfml.tasks` 是为了合这条已经立过的规矩。
 *
 * 【这个文件不写坏任何东西】
 *
 * 读：读不动 / 那一截不是 JSON / 里面是别的形状 ⇒ 回空表，**一个字都不嘟囔**（调用方那边
 * 是"这次启动没有历史可看"，不是"出事了"）。
 * 写：先写一个 `.new` 再改名盖上去（和别处那几份落盘同一个路数）。写到一半断电最多丢掉这
 * 一次的历史，绝不会把上一份好文件截成半截。
 */

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include "TaskSnapshot.hpp"

namespace Tasks {

/// 任务历史的落点：`<启动器自带数据目录>/wtfml.tasks`（**唯一出处**）。
/// 它是启动器级账本，不跟随当前游戏文件夹切换。
QString defaultHistoryPath();

/// 账本里最多留几条**已完成**的（多了从最老的开始丢）。第 0 条是最近的。
/// 盘上与运行期内存用**同一把尺子**：`saveHistory()` 和 `TaskManager::pruneFinished()`
/// 都读它，不再出现"面板里有 N 条、重启后只剩最近 50 条"。
int finishedHistoryLimit();

/// 账本 ⇒ JSON 字节。**纯函数**：喂什么写什么，不看盘也不看时钟。
QByteArray toJson(const QList<TaskSnapshot> &tasks);

/// JSON 字节 ⇒ 账本。读不动就回空表（`ok` 不是 nullptr 时写 false）。
///
/// 它**原样**读回来：盘上写着 `Running` 就回 `Running`。"上次没跑完的那些算已中断"那条
/// 规则不在这里，在 `load()` —— 分开是为了两件事各自可测（`markInterrupted()` 有自己
/// 的真值表，见 tests/main.cpp）。
QList<TaskSnapshot> fromJson(const QByteArray &bytes, bool *ok = nullptr);

/// 把**非终态**的那几项改成 `Interrupted`。
///
/// 「启动器异常退出后，上次未完成的项目在历史里标记为'已中断'，不自动假装继续」。
/// 读盘之后**必须**过一遍它：不过的话盘上那几项回来还是 `Running`，界面上就成了"两项正在
/// 下载"而其实一个网络请求都没有 —— 用户会一直等下去。
void markInterrupted(QList<TaskSnapshot> &tasks);

/// 从 @p path 读一份账本（含 `markInterrupted()`），读不动 / 文件不在 ⇒ 空表。
QList<TaskSnapshot> load(const QString &path);

/// 把 @p tasks 写进 @p path（父目录会建）。成功返回 true。
bool save(const QString &path, const QList<TaskSnapshot> &tasks);

}  // namespace Tasks
