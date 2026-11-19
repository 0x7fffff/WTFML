/**
 * @file ServerMenuController.hpp
 * @brief 「服务端」那一页的右键菜单：**摆哪几项**和**点下去做什么**。
 *
 * 【为什么这一层值得单独站着】
 *
 * 菜单上八项里，七项只是转发（开控制台 / 开目录 / 设置页 / 修复 / 强制结束 / 删除都
 * 已经各有出处），真正有判断的只有两处：
 *
 * ① **用不上的那几项不摆**（而不是摆一颗灰的）：装好了就没有「修复」、没在跑就没有
 *    「强制结束」。摆一颗永远点不动的项，比不摆它更容易让人以为界面坏了。
 * ② 「异常退出后自动重启」是一个**长期设置**：勾没勾读的是盘上那一份
 *    （`.wtfml-server` 的 `restartOnCrash`），勾一下就**读—改—写**回去 ——
 *    写不进去要当场说一声，一项"看起来勾上了、其实没记住"的设置比没有这一项更糟；
 *    取消勾选还要把这一轮的崩溃计数清掉（再打开时从第 1 次算起）。
 *
 * 【边界】
 *
 * · 在：那两条判断、`.wtfml-server` 那一次读写、那一行日志。
 * · 不在：七项转发到的那些动作本身（它们各自有出处），也不在"服务端起没起"这个事实
 *   （问 `ServerWorkspaceController`）。这一层只问，不记。
 */

#pragma once

#include <QObject>
#include <QPoint>
#include <QString>

class MainWindow;

class ServerMenuController final : public QObject {
public:
    explicit ServerMenuController(MainWindow *owner);

    /// 在服务端列表的第 @p pos 处右键：认行 → 摆菜单 → 执行选中的那一项。
    /// 右键落在空白处什么都不做（这一页没有"对整个列表做的事"）。
    void showAt(const QPoint &pos);

    /// 在 @p globalPos 处给 @p serverId 摆菜单（行尾「更多」与右键共用）。
    void showFor(const QString &serverId, const QPoint &globalPos);

    /// 执行某一项（下标 = `MainWindowUI::ServerMenuAction`）。
    void run(const QString &serverId, int action);

private:
    MainWindow &m_owner;
};
