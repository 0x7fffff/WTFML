#pragma once
#include <QGroupBox>
inline QGroupBox *nearestGroupBox(QWidget *w) {
        for (QWidget *p = w != nullptr ? w->parentWidget() : nullptr; p != nullptr;
             p = p->parentWidget()) {
            if (auto *box = qobject_cast<QGroupBox *>(p)) {
                return box;
            }
        }
        return nullptr;
    }

