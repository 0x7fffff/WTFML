/**
 * @file AgentPanelPaint.hpp
 * @brief 智能体页的"软面板"：比底稍亮一层的圆角面 + 顶上一道极淡高光，不描边（UI 7d）。
 */
#pragma once

#include "Theme.hpp"

#include <QLinearGradient>
#include <QPainter>
#include <QWidget>

inline void paintAgentSoftPanel(QWidget *widget, qreal radius = 12, QColor tint = QColor()) {
    QPainter p(widget);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF box = QRectF(widget->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const QColor fill = tint.isValid() ? tint : QColor(255, 255, 255, Theme::isDark() ? 16 : 120);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawRoundedRect(box, radius, radius);
    QLinearGradient shine(box.topLeft(), QPointF(box.left(), box.top() + 18));
    shine.setColorAt(0, Theme::tokens().glassEdge);
    shine.setColorAt(1, QColor(255, 255, 255, 0));
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QBrush(shine), 1));
    p.drawRoundedRect(box, radius, radius);
}
