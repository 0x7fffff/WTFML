#pragma once
#include <QAbstractItemView>
#include <QComboBox>
#include <QWheelEvent>

#include "WheelForward.hpp"

// Wheel selection is allowed only inside the explicitly opened popup.
// 弹层没开时（鼠标只是路过）既不认滚轮改值，也不把这一发吃掉：转给外层滚动区，
// 否则鼠标停在下拉框上就滚不动页面（见 WheelForward.hpp）。
class WheelSafeComboBox final : public QComboBox {
public:
    using QComboBox::QComboBox;
protected:
    void wheelEvent(QWheelEvent *event) override {
        if (view()->isVisible()) {
            QComboBox::wheelEvent(event);
            return;
        }
        WheelForward::toScrollArea(this, event);
        event->accept();
    }
};
