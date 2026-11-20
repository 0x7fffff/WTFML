/**
 * @file BrandMark.hpp
 * @brief 品牌标「手写的 W」共用的那几样：读字形、主题配色、对比度兜底，以及主页启动卡片里那枚会动的 W。
 *
 * 标题栏左上那枚（`TitleBar.cpp`，24 px 专版）和启动卡片里这枚（256 版）配色、对比度规矩相同，
 * 所以这几个函数放在这里共用；书写中线各用各的数据（尺寸差太多，24 专版的笔画半径另算过）。
 */

#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QList>
#include <QPainterPath>
#include <QPair>
#include <QWidget>

class QTimer;

namespace BrandMark {

/// 读一份只有 M/L/Z 的商标 SVG（资源路径），返回填充路径（nonzero）。读不到给空路径。
QPainterPath pathFromSvg(const QString &resource);

/// 品牌标的两档渐变色（上 → 下）。出厂暮色雪山（没改主题色）用图标原色 #8682D4 → #6C68BC；
/// 别的主题、改过主题色的按主题色走（上一档提亮）。
QPair<QColor, QColor> stops();

/// @p c 叠在 @p backgrounds 每一种底上之后最低的那个对比度（半透明的墨按叠出来的色算）。
double worstContrast(const QColor &c, const QList<QColor> &backgrounds);

/// 把 @p color 小步往 @p ink 挪，直到它在 @p backgrounds 每一种底上都有 3:1（WCAG 图形对比度）。
QColor ensureGraphicContrast(QColor color, const QColor &ink, const QList<QColor> &backgrounds);

/// 失败时 W 转灰的目标色：饱和度 ×0.10、明度 ×0.92+0.04；@p amount 0..1 是从原色走到灰色的几成。
QColor toGray(const QColor &c, qreal amount);

}  // namespace BrandMark

/// 主页启动卡片里的手写 W（规格见 issue #65 与讨论 #43「C 单向流光」）。
///
/// - **加载中**：W 以 25% 淡墨常显作底，一段浓墨（实心 22% + 两端各 7% 羽化，共 36% 全长）沿笔画
///   从起笔往收笔单向匀进，一圈 2.80 s。笔画首尾接成一个环：墨段走出收笔的同时从起笔进来，接缝不跳。
/// - **完成**（成功 / 失败）：0.25 s 内墨段中心减速停住、半宽长到半个周长，墨铺满整个 W；
///   失败再用 0.45 s 转灰到约 10% 饱和度。任何时刻进入完成，终态都是完整的 W。
/// - 每个像素的墨量只看「这个像素最早被笔轨哪一点盖到」的弧长（尺寸变了才重算），每帧只是查表。
class LaunchMark final : public QWidget {
    Q_OBJECT

public:
    enum class Phase { Idle, Writing, Succeeded, Failed };

    explicit LaunchMark(QWidget *parent = nullptr);

    /// 开始（或继续）循环书写。已在书写中不重来。
    void startWriting();
    /// 铺满；@p success 为 false 时铺满后转灰。重复调用同一种结局不重来。
    void finish(bool success);
    /// 回到空闲（不画墨段，停定时器）。
    void reset();

    Phase phase() const { return m_phase; }
    /// 此刻弧长 @p s 处的墨量（0..1）与转灰程度（0..1）——给测试核对接缝与终态用。
    qreal inkAt(qreal s) const;
    qreal grayAmount() const;
    /// 把时钟钉在 @p seconds（从起笔算；负数 = 回到真时钟）。只给测试与逐帧样张用。
    void setFrozenTime(qreal seconds) { m_frozen = seconds; update(); }
    /// 中线全长（256 坐标单位）。
    static qreal pathLength();

    QSize sizeHint() const override { return {76, 52}; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    /// 此刻的墨段：中心弧长、实心半宽。
    struct Band { qreal center; qreal half; };
    Band bandNow() const;
    qreal elapsed() const;
    void syncTimer();
    void rebuildMaps();

    Phase m_phase = Phase::Idle;
    QElapsedTimer m_clock;        // 书写开始那一刻起算
    qreal m_finishAt = -1;        // 进入完成的时刻（秒，m_clock 计）
    qreal m_frozen = -1;          // setFrozenTime() 钉住的时刻；负数 = 真时钟
    QTimer *m_tick = nullptr;
    // 按控件像素（含 dpr）预算的三张图：字形覆盖率、每像素弧长、每行渐变位置。
    QSize m_mapSize;
    qreal m_mapDpr = 0;
    QVector<float> m_cover;
    QVector<float> m_arc;
    QVector<float> m_rowGradient;
};
