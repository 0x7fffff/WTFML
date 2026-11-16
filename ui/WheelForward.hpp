/**
 * @file WheelForward.hpp
 * @brief 「没焦点的输入控件不认滚轮，但鼠标路过它时页面还得能滚」——这一条的唯一下发处。
 *
 * 数字框、下拉框、条在没有焦点时把滚轮吃掉是**对的**（不然鼠标划过去就悄悄改了值），但吃掉
 * 之后这一发就到此为止了：外层滚动区再也收不到，页面停在原地不动。用户报的就是这个
 * （设置页鼠标停在数字框 / 下拉框上滚不动）。
 *
 * 所以吃掉之前把这一发**转给最近的外层滚动区**。转的时候位置要换成接收方自己的坐标系：
 * 滚动区按位置分派（落在横条 / 竖条上算拖条，不算滚内容），位置不换算就会被当成"点在滚动条上"。
 */
#pragma once

#include <QAbstractScrollArea>
#include <QApplication>
#include <QWheelEvent>
#include <QWidget>

namespace WheelForward {

/// 把 @p event（坐标是 @p widget 自己的）交给最近的外层滚动区。
/// 交出去了回 true；一个滚动区祖先都没有回 false（调用方自己决定要不要吃掉）。
inline bool toScrollArea(QWidget *widget, QWheelEvent *event) {
    if (widget == nullptr || event == nullptr) {
        return false;
    }
    QAbstractScrollArea *area = nullptr;
    for (QWidget *parent = widget->parentWidget(); parent != nullptr;
         parent = parent->parentWidget()) {
        if (auto *candidate = qobject_cast<QAbstractScrollArea *>(parent)) {
            area = candidate;
            break;
        }
    }
    if (area == nullptr || area->viewport() == nullptr) {
        return false;
    }
    QWidget *view = area->viewport();
    QWheelEvent forwarded(view->mapFromGlobal(event->globalPosition()), event->globalPosition(),
                          event->pixelDelta(), event->angleDelta(), event->buttons(),
                          event->modifiers(), event->phase(), event->inverted(), event->source());
    QApplication::sendEvent(view, &forwarded);
    return true;
}

}  // namespace WheelForward
