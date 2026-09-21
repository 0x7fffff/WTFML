/**
 * @file SkinViewer.hpp
 * @brief 主页右栏那个转得动的 3D 小人控件：把一张皮肤贴图（+ 可选披风）画出来，
 *        按住拖能转、滚轮能缩、双击复位。
 * @author 0x7fffff
 * @date 2026-09-21
 * @note 这个文件里**一行绘制代码都没有** —— 几何和光栅化全在 SkinModel 里（那是纯函数，
 *       验证工程能逐条钉住"哪块面取贴图上哪个矩形"这种肉眼验不准的东西）。这里只管
 *       三件界面的事：鼠标怎么变成角度、背景色从哪来、几何从哪来。
 *       同样地，它**不 include 任何 src/ 下的头** —— 和 GroupDialog / InstanceSettingsDialog
 *       一个层次，ui_check 那个"ui 不依赖业务层"的看门人就是靠这条成立的。
 */

#pragma once

#include <QImage>
#include <QPointF>
#include <QWidget>

/**
 * @brief 可拖动旋转的皮肤预览控件。
 *
 * 进参出参全是值（QImage / bool / double），不牵扯任何业务对象：谁拿到贴图谁就能喂给它，
 * 所以验证工程不用登录、不用联网也能把它验完。
 */
class SkinViewer : public QWidget {
    Q_OBJECT

public:
    explicit SkinViewer(QWidget *parent = nullptr);

    /**
     * @brief 换一套外观。
     * @param skin 皮肤贴图（64×64，或老格式的 64×32）。空图 = 什么都不画。
     * @param cape 披风贴图（64×32）。可空 —— 这个人没有披风，或者皮肤还没取到。
     * @param slim 是不是"纤细"（Alex）模型。
     */
    void setSkin(const QImage &skin, const QImage &cape, bool slim);

    /// 当前外观（验证工程拿它断言"改到界面上了没有"）。
    QImage skin() const { return m_skin; }
    QImage cape() const { return m_cape; }
    bool isSlim() const { return m_slim; }

    // ---- 视角。三个都读得到，验证工程塞合成事件之后按它们断言。----
    double yaw() const { return m_yaw; }
    double pitch() const { return m_pitch; }
    double zoom() const { return m_zoom; }

    /// 初始视角：偏 3/4 侧、略微俯视（正对着脸看不出立体感）。和 SkinModel 里那对
    /// 常量是同一个值 —— 在这里转一手，是为了让"控件的初始视角"这件事只有一个出处。
    static double defaultYaw();
    static double defaultPitch();
    /// 初始缩放。
    static double defaultZoom();

    static QString viewerName() { return QStringLiteral("skinViewer"); }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    /// 把三个角度一次性夹到位再决定要不要重绘。**夹的方法是唯一的**（构造函数、拖动、
    /// 滚轮都走这里），否则"俯仰角上限"这种东西迟早会在某一条路径上漏掉。
    void setView(double yaw, double pitch, double zoom);
    /// 回到初始视角（双击）。
    void resetView();

    QImage m_skin;
    QImage m_cape;
    bool m_slim = false;

    double m_yaw = 0.0;
    double m_pitch = 0.0;
    double m_zoom = 1.0;

    /// 拖动中：记住上一次的鼠标位置，按下之后每次移动都按**增量**转（而不是按
    /// "从按下点到现在一共多远"绝对量）—— 增量式的转法在转满好几圈之后也不会跳。
    bool m_dragging = false;
    QPointF m_lastMousePos;
};
