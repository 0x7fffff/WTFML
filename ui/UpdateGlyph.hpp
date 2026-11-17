/**
 * @file UpdateGlyph.hpp
 * @brief 更新按钮与文件列表共用的空心上箭头及底线。
 */
#pragma once

#include <QIcon>
#include <QHash>
#include <QPainter>
#include <QPainterPath>

namespace UpdateGlyph {
inline QIcon icon(const QColor &color) {
    static QHash<QRgb, QIcon> cache;
    const QRgb key = color.rgba();
    const auto found = cache.constFind(key);
    if (found != cache.cend()) return found.value();
    QPixmap pixmap(72, 72);
    pixmap.setDevicePixelRatio(3);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPainterPath path;
    path.moveTo(12, 2);
    path.lineTo(22, 12); path.lineTo(16, 12); path.lineTo(16, 19);
    path.lineTo(8, 19); path.lineTo(8, 12); path.lineTo(2, 12);
    path.closeSubpath();
    path.moveTo(8, 23); path.lineTo(16, 23);
    painter.drawPath(path);
    painter.end();
    const QIcon result(pixmap);
    if (cache.size() >= 64) cache.clear();
    cache.insert(key, result);
    return result;
}
} // namespace UpdateGlyph
