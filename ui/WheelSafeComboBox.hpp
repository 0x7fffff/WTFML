#pragma once
#include <QAbstractItemView>
#include <QComboBox>
#include <QWheelEvent>

// Wheel selection is allowed only inside the explicitly opened popup.
class WheelSafeComboBox final : public QComboBox {
public:
    using QComboBox::QComboBox;
protected:
    void wheelEvent(QWheelEvent *event) override {
        if (view()->isVisible()) QComboBox::wheelEvent(event);
        else event->accept();
    }
};
