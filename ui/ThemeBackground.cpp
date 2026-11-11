/**
 * @file ThemeBackground.cpp
 * @brief ThemeBackground 的实现：铺底色、按摆法铺图，以及那两级缓存怎么失效。
 */

#include "ThemeBackground.hpp"

#include "Glass.hpp"

#include <QFileInfo>
#include <QPainter>

ThemeBackground::ThemeBackground(Theme::Part part, QWidget *parent)
    : QWidget(parent), m_part(part) {
    // 底色是**自己画的**，所以不用 `autoFillBackground`（那个等于让样式再铺一层一样的
    // 底色，白画一遍）。`WA_StyledBackground` 也不设：这一块上的 QSS 只管它里面的控件。
    // 主题一变就重画一遍：`apply()` 把"该重算了"敲给每一个控件（见 `Theme::follow()`），
    // 而我们这儿记着两份**位图**，不重算的话换了主题图还在、底色却是旧的。
    Theme::follow(this, [this] { update(); });
}

QPixmap ThemeBackground::sourceImage() {
    const Theme::PartImage image = Theme::partImage(m_part);
    if (!image.isSet()) {
        m_source = QPixmap();
        m_sourceKey.clear();
        return m_source;
    }
    const QFileInfo info(image.path);
    // `路径|大小|修改时间`：同一个路径下换了另一张图，只有后两位会变。
    const QString key = image.path + QLatin1Char('|') + QString::number(info.size()) +
                        QLatin1Char('|') + QString::number(info.lastModified().toMSecsSinceEpoch());
    if (key == m_sourceKey && !m_source.isNull()) {
        return m_source;
    }
    m_sourceKey = key;
    // 先清空再读：读失败时**不能**留着上一张图（用户换了一张坏图，屏幕上还挂着他换掉的那
    // 一张，那是"换了没生效"）。
    m_source = QPixmap(image.path);
    return m_source;
}

void ThemeBackground::paintEvent(QPaintEvent *) {
    const QRect area = rect();
    Theme::PartImage image = Theme::partImage(m_part);
    // 磨砂效果开着时页面背景那张图由场景底（Glass::Backdrop）铺在整窗最底下，这里不再叠一遍。
    if (m_part == Theme::Part::PageBackground && Glass::enabled()) image = Theme::PartImage();
    const bool hasBase = Theme::partHasBackground(m_part);

    // 0) **一笔都不画**那一条路。
    //
    // NOTE: 不画不等于"画一个透明色"：这条路上连 QPainter 都不建 —— Qt 的默认行为就是
    // 子控件不盖住父控件画过的地方（我们没开 `autoFillBackground`，也没开
    // `WA_OpaquePaintEvent`）。
    if (!hasBase && !image.isSet()) {
        return;
    }

    QPainter painter(this);

    // 1) 底色。只有**用户自己给这一块设过底色**（`partHasBackground()`）才铺这一层 ——
    // 设过图之后它就派上用场了：不透明度调低时透上来的是它，`contain` 四周留的也是它。
    if (hasBase) {
        const QColor background = Theme::partBackground(m_part);
        if (background.isValid()) {
            painter.fillRect(area, background);
        }
    }

    // 2) 图。
    if (!image.isSet() || area.isEmpty()) {
        return;
    }
    const QPixmap source = sourceImage();
    if (source.isNull()) {
        return;  // 读不出来 / 不是图片：只有底色（见头文件那段 NOTE）
    }

    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setOpacity(qBound(0, image.opacity, 100) / 100.0);

    switch (image.fit) {
    case Theme::ImageFit::Tile:
        // 平铺是**原尺寸**一张张重复（缩过再平铺的话就成了马赛克，那是另一种摆法）。
        painter.drawTiledPixmap(area, source);
        return;
    case Theme::ImageFit::Stretch:
        // 拉伸不保比例，所以不用进那一份"按尺寸缩好"的缓存：一句 drawPixmap 让绘制引擎
        // 自己去缩（它内部就是一次双线性采样，和先缩再贴是一样的结果）。
        painter.drawPixmap(area, source);
        return;
    default:
        break;
    }

    // Cover / Contain：按当前尺寸缩一张出来，条件没变就接着用缓存的那一张。
    //
    // 判据里**必须带上源图那一串**（`m_sourceKey`）：先设一张 4:3 的、再换成一张 16:9 的，
    // 控件尺寸一点没变 —— 光看尺寸的话，屏幕上还是上一张图缩出来的那一份（"换了没生效"）。
    const QString scaledKey = m_sourceKey + QLatin1Char('|') +
                              QString::number(static_cast<int>(image.fit)) + QLatin1Char('|') +
                              QString::number(area.width()) + QLatin1Char('x') +
                              QString::number(area.height());
    if (m_scaled.isNull() || m_scaledKey != scaledKey) {
        const QSize target =
            image.fit == Theme::ImageFit::Cover
                ? source.size().scaled(area.size(), Qt::KeepAspectRatioByExpanding)
                : source.size().scaled(area.size(), Qt::KeepAspectRatio);
        const QPixmap scaled = source.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        // 缩不出来（尺寸退化到 0 之类）就退回原图 —— 画得出来一张比画不出来强。
        m_scaled = scaled.isNull() ? source : scaled;
        m_scaledKey = scaledKey;
    }
    if (m_scaled.isNull()) {
        return;
    }
    if (image.fit == Theme::ImageFit::Cover) {
        // 铺满：缩出来的那张比控件大，多出来的**从中间**裁（左上加右下的裁法会把主体
        // 切到画外，而"人像偏中间"是绝大多数壁纸的构图）。
        const QRect target(area.center().x() - m_scaled.width() / 2,
                           area.center().y() - m_scaled.height() / 2, m_scaled.width(),
                           m_scaled.height());
        painter.drawPixmap(target, m_scaled);
    } else {
        // 完整放下：居中，四周留底色。
        painter.drawPixmap(area.center().x() - m_scaled.width() / 2,
                           area.center().y() - m_scaled.height() / 2, m_scaled);
    }
}
