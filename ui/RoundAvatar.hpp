/**
 * @file RoundAvatar.hpp
 * @brief 账号头像一律画成圆：主页右上那颗、账号下拉面板里每一行、账号页列表。
 *
 * 只在**绘制时**裁成圆，头像位图本身照旧是方的（`SkinModel::headAvatar()` / `letterAvatar()`
 * 的输出和 QLabel::pixmap() 都不变 —— 测试按像素比对的是那张方图）。
 */

#pragma once

#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>

namespace RoundAvatar {

/// 把方头像居中裁成圆画进 rect。先按物理像素缩放，再用抗锯齿的圆收边。
inline void paint(QPainter &painter, const QRectF &rect, const QImage &face) {
    if (face.isNull() || rect.isEmpty()) return;
    const qreal dpr = painter.device() != nullptr ? painter.device()->devicePixelRatioF() : 1.0;
    const QSize phys = (rect.size() * dpr).toSize();
    if (phys.isEmpty()) return;
    const int side = qMin(face.width(), face.height());
    const QImage square = face.copy((face.width() - side) / 2, (face.height() - side) / 2, side, side);
    // 放大用最近邻（像素脸不糊）；缩小用平滑（字母头像那个字缩下来不起锯齿）。
    const Qt::TransformationMode mode = phys.width() >= side ? Qt::FastTransformation : Qt::SmoothTransformation;
    const QImage scaled = square.scaled(phys, Qt::IgnoreAspectRatio, mode);
    QImage out(phys, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    {
        QPainter p(&out);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(QBrush(scaled));
        p.drawEllipse(QRectF(QPointF(0, 0), QSizeF(phys)));
    }
    out.setDevicePixelRatio(dpr);
    painter.drawImage(rect.topLeft(), out);
}

/// 显示成圆的头像标签：pixmap() 还是那张方图，只是画出来是圆的。
class Label final : public QLabel {
public:
    using QLabel::QLabel;

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        paint(painter, QRectF(contentsRect()), pixmap().toImage());
    }
};

}  // namespace RoundAvatar
