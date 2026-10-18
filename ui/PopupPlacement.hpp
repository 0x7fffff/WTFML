#pragma once
#include <QRect>
#include <QSize>
#include <algorithm>

namespace PopupPlacement {
// Work entirely in logical screen coordinates, including negative-origin monitors.
inline QRect around(const QRect &anchor, QSize size, QRect available) {
    available.adjust(8, 8, -8, -8);
    if (available.isEmpty()) return QRect(anchor.topLeft(), size);
    size.setWidth(qBound(1, size.width(), available.width()));
    size.setHeight(qBound(1, size.height(), available.height()));
    const int above = anchor.top() - 6 - size.height();
    const int below = anchor.bottom() + 7;
    int y = above;
    if (above < available.top()) {
        if (below + size.height() - 1 <= available.bottom()
            || available.bottom() - below > anchor.top() - available.top()) y = below;
    }
    return QRect(QPoint(std::clamp(anchor.left(), available.left(), available.right() - size.width() + 1),
        std::clamp(y, available.top(), available.bottom() - size.height() + 1)), size);
}
}
