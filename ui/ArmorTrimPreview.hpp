#pragma once
#include "SkinModel.hpp"
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QWidget>

/// No game textures are embedded. Scene images are supplied at runtime.
class ArmorTrimPreview final : public QWidget {
public:
    explicit ArmorTrimPreview(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName(QStringLiteral("armorTrimPreview"));
        setMinimumSize(200, 220);
        setMaximumHeight(360);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setToolTip(tr("拖动旋转 / 滚轮缩放 / 双击复位"));
    }
    void setScene(const SkinModel::Scene &scene) {
        const bool hadScene = !m_scene.skin.isNull();
        const auto yaw = m_scene.yaw, pitch = m_scene.pitch, zoom = m_scene.zoom;
        m_scene = scene;
        if (hadScene) { m_scene.yaw = yaw; m_scene.pitch = pitch; m_scene.zoom = zoom; }
        update();
    }
    const SkinModel::Scene &scene() const { return m_scene; }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        // 不铺底：它摆在工具页那块柔和面板里，底色由面板给（原来那块 alternateBase 在浅色主题里是一整块白）。
        if (m_scene.skin.isNull()) {
            painter.setPen(palette().color(QPalette::PlaceholderText));
            painter.drawText(rect().adjusted(16,16,-16,-16), Qt::AlignCenter | Qt::TextWordWrap,
                             tr("载入游戏资源后显示纹饰预览"));
        } else SkinModel::render(painter, QRectF(rect()).adjusted(12,12,-12,-12), m_scene);
    }
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) { m_last = event->position(); m_drag = true; }
    }
    void mouseReleaseEvent(QMouseEvent *) override { m_drag = false; }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (!m_drag) return;
        const auto delta = event->position() - m_last; m_last = event->position();
        // 和 SkinViewer 同一个方向：抓住模型拧 —— 往右拖，正面往右滑，于是转到它的右侧。
        // yaw 正数是相机绕到玩家左侧，所以要**减**。
        m_scene.yaw -= delta.x() * .7;
        m_scene.pitch = qBound(-70.0, m_scene.pitch + delta.y() * .5, 70.0); update();
    }
    void wheelEvent(QWheelEvent *event) override {
        m_scene.zoom = qBound(.6, m_scene.zoom + event->angleDelta().y() / 1200.0, 1.7); update();
        event->accept();
    }
    void mouseDoubleClickEvent(QMouseEvent *) override {
        m_scene.yaw = 25; m_scene.pitch = -8; m_scene.zoom = 1; update();
    }
private:
    SkinModel::Scene m_scene;
    QPointF m_last;
    bool m_drag = false;
};
