/**
 */

#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QPointF>
#include <QTimer>
#include <QWidget>

/**
 * @brief 可拖动旋转、并且一直在走路的皮肤预览控件。
 *
 * 进参出参全是值（QImage / bool / double），不牵扯任何业务对象：谁拿到贴图谁就能喂给它，
 * 所以验证工程不用登录、不用联网也能把它验完。
 *
 * NOTE: 动画（走路那点相位）**是界面的责任**：几何层只认"给我一个 0..1 的相位"，
 * 时间从哪来、什么时候停，都是控件自己的事。所以截图/断言要定格时，`setAnimated(false)`
 * + `setWalkPhase(p)` 一设，像素就是确定的 —— 定时器一动图就不可复现了。
 * 另外它**只在可见时走**（showEvent 起、hideEvent 停，停下时把当前那一帧记住）：
 * 主页切到别的页、窗口最小化的时候，这个控件不该还在后台每秒重画 60 次。
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

    // ---- 走路 ----

    /// 走路的相位（0..1 一个来回）。0 = 迈到最大的那一帧，SkinModel::restWalkPhase()
    /// = 腿正好竖直。动画开着的时候它随时间往前走。
    double walkPhase() const;

    void setWalkPhase(double phase);

    /// 走路的动画开不开。默认**开** —— 产品要的就是官启那样"站着走原地步"的小人。
    /// 关掉之后相位停在当前那一帧（不是回到 0），再开起来接着走。
    /// NOTE: 打开只是"允许走"：控件看不见的时候它不会真的开始转，得等显示出来。
    void setAnimated(bool on);
    bool isAnimated() const { return m_animated; }

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
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    /// 把三个角度一次性夹到位再决定要不要重绘。**夹的方法是唯一的**（构造函数、拖动、
    /// 滚轮都走这里），否则"俯仰角上限"这种东西迟早会在某一条路径上漏掉。
    void setView(double yaw, double pitch, double zoom);
    /// 回到初始视角（双击）。
    void resetView();
    /// 走动那两张：起的时候接着当前那一帧往下走、停的时候把当前那一帧存下来。
    void startWalking();
    void stopWalking();

    QImage m_skin;
    QImage m_cape;
    bool m_slim = false;

    double m_yaw = 0.0;
    double m_pitch = 0.0;
    double m_zoom = 1.0;

    /// 走路。相位 = m_phaseBase + (这一轮走了多久 / 一个来回的时长)，所以定时器抖一下
    /// 不会让步子忽快忽慢（**按真实时间推**，不是"每 tick 加一个固定量"）。
    QTimer *m_walkTimer = nullptr;
    QElapsedTimer m_walkClock;
    /// 当前这一轮的起始相位；定时器停着的时候它就是"现在停在哪一帧"。
    double m_phaseBase = 0.0;
    bool m_animated = true;

    /// 拖动中：记住上一次的鼠标位置，按下之后每次移动都按**增量**转（而不是按
    /// "从按下点到现在一共多远"绝对量）—— 增量式的转法在转满好几圈之后也不会跳。
    bool m_dragging = false;
    QPointF m_lastMousePos;
};
