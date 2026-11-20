/**
 * @file WindowMotion.hpp
 * @brief 开关程序的动效「雾散开」：只动客户区里的内容层，窗口矩形交给 Windows 自己的打开 / 关闭动画。
 *
 * 打开：首帧画成起手样子（只有磨砂、四组内容藏着占位、品牌标空着），首帧之后：风景从磨砂里淡出来
 * （`Glass::Backdrop` 的风景淡变，0→1），问候 / 角色 / 当前实例 / 启动行依次浮上来（`Anim::riseIn`，换页幕布），
 * 标题栏的品牌 W 写一遍。约 0.6 s，不挡点击。
 * 关闭：确认都过了之后，内容淡进磨砂并下沉（`Anim::fogClose`，120 ms），最后一帧只剩磨砂，再真的关窗。
 * 不用 setWindowOpacity（亚克力 / 云母下会丢系统背板），四档材质同一条路径。
 */
#pragma once

class QCloseEvent;
class QWidget;

namespace WindowMotion {
/// 关窗合雾走到哪：主窗口自己存一份（成员），交给 interceptClose() 读写。
enum class CloseFog { Idle, Running, Done };

/// 在 `show()` 之前调：把首帧摆成起手样子，并挂好「首帧画完就开始」。动效关着（测试截图）时什么都不做。
void armOpening(QWidget *window);
/// 在 closeEvent 要放行的地方调：返回 true = 这一次先拦下、正在合雾，播完置 @p state 为 Done 并再 close() 一次。
/// 不可见、最小化、关机注销、动效关着时直接放行（返回 false）；合雾途中再关一次吞掉。
bool interceptClose(QWidget *window, QCloseEvent *event, CloseFog &state);
}  // namespace WindowMotion
