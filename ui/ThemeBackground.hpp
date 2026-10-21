/**
 * @file ThemeBackground.hpp
 * @brief 一块"先铺底色、再铺一张图"的容器控件 —— 四个页面和主页左栏的底就是它。
 * @note 主页左侧和四个页面的底都可以铺一张自定义图。
 *
 * 【这一层为什么必须是个控件，而不是一句 QSS】
 *
 * `background-image:` 那套 QSS 对**普通 QWidget** 不生效（要么得设 `WA_StyledBackground`，
 * 要么得自己画），而我们要做的事比 QSS 能表达的多两件：
 * - 图外面那圈底色得是**这一块自己的**那种算法（`Theme::partBackground()`，见 Theme.hpp），
 * 不是一句写死的 `#f0f0f0`；
 * - 「铺满 / 完整放下 / 拉伸 / 平铺」这四种摆法里，前三种要按**当前尺寸**重新缩放 ——
 * 一张 4000 px 的照片每次重绘都缩一遍，拖窗口的时候就是一顿一顿的。
 *
 * 【没设图时它一个像素都不画】
 *
 * 【缓存】
 *
 * 两级，都挂在控件自己身上（**没有全局缓存**：带图的那两块一共就两个实例，全局表反而
 * 要在"主题换了""用户换图了"几处各记得去清）：
 * - `m_source`：解出来的原图。判据是 `路径|文件大小|修改时间` 这一串 —— 用户把同一个
 * 路径下的文件换成另一张时，光看路径是不知道的；
 * - `m_scaled`：按**当前控件尺寸**缩好的那一张。尺寸变一次重算一次（拖窗口时每帧一次，
 * 这正是要缓存它的原因）。
 *
 * NOTE: 图读不出来（删了、挪走了、U 盘拔了、根本不是图片）时**不弹窗也不报错**，画到哪儿
 * 算哪儿（多半就是底下那一层原样透上来）。这一层在每一次重绘上，往这儿塞一个对话框
 * 等于"界面画不出来"。用户自己挑的图自己删掉，看到的就是那一块回到原来的样子 ——
 * 这是他说得出口、也看得懂的结果。
 */

#pragma once

#include <QPixmap>
#include <QSize>
#include <QString>
#include <QWidget>

#include "Theme.hpp"

class ThemeBackground : public QWidget {
    Q_OBJECT

public:
    /// @param part 这一块是 Theme 里的哪一块（决定底色和铺哪张图）。
    explicit ThemeBackground(Theme::Part part, QWidget *parent = nullptr);

    Theme::Part part() const { return m_part; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    /// 这一块现在那一张**源图**（`Theme::partImage(m_part)` 指的）。没设 / 读不出来 = 空。
    QPixmap sourceImage();

    Theme::Part m_part;
    /// 源图 + 它是从哪儿来的（`路径|大小|修改时间`）。
    QPixmap m_source;
    QString m_sourceKey;
    /// 缩好的那一张 + 它是**按什么条件**缩的（源图那一串 | 摆法 | 控件尺寸）。
    QPixmap m_scaled;
    QString m_scaledKey;
};
