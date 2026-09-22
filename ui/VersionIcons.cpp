#include "VersionIcons.hpp"

#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <functional>

namespace {

// ---------------------------------------------------------------------------
// 草方块：16×16 的像素画，照 MC 那块贴图的侧面（上面一层绿、底下一层土，
// 草的下沿是锯齿状的）。颜色是挑的近似色，不是从游戏里抠出来的。
// ---------------------------------------------------------------------------
const QColor kGrassLight(0x8c, 0xc8, 0x5a);
const QColor kGrassDark(0x5d, 0x9c, 0x3f);
const QColor kGrassEdge(0x4a, 0x82, 0x30);
const QColor kDirtLight(0x9c, 0x70, 0x4a);
const QColor kDirtMid(0x84, 0x5f, 0x3d);
const QColor kDirtDark(0x6e, 0x4c, 0x30);

/// 每一列草有多厚（那个锯齿下沿）。**写死**的，不调随机数 —— 截图要每次一模一样，
/// 而且"上面绿、下面土"这件事得能对着像素断言。
const int kGrassDepth[16] = {3, 4, 4, 3, 5, 4, 3, 3, 4, 5, 4, 3, 4, 4, 3, 5};

/// 土上那些深浅斑点：看着不规整就行，不是随机数（同样是"每次画出来必须一样"）。
int dirtShade(int x, int y) { return (x * 7 + y * 13 + (x * y) % 5) % 3; }

/// 16 格坐标系（和草方块那块贴图一个尺度）→ 目标边长的像素。
qreal u(qreal v, int size) { return v * size / 16.0; }

// ---------------------------------------------------------------------------
// 加载器那四块底色（各自的品牌色近似值）。
// ---------------------------------------------------------------------------
const QColor kFabricBg(0x5b, 0x62, 0xa8);
const QColor kForgeBg(0xf0, 0xa1, 0x3c);
const QColor kForgeInk(0x2e, 0x33, 0x38);
const QColor kNeoBg(0x2f, 0x31, 0x36);
const QColor kNeoFox(0xe0, 0x7b, 0x39);
const QColor kOptiBg(0xe8, 0xf0, 0xf8);
const QColor kOptiInk(0x2c, 0x6c, 0xb0);

/// 一块圆角底色（四个加载器的标志都画在它上面，好让它们在单选钮前面站得住）。
void drawTile(QPainter &p, int size, const QColor &bg) {
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    const qreal r = u(3.0, size);
    p.drawRoundedRect(QRectF(0.5, 0.5, size - 1.0, size - 1.0), r, r);
}

QPainterPath tilePath(int size) {
    QPainterPath path;
    const qreal r = u(3.0, size);
    path.addRoundedRect(QRectF(0.5, 0.5, size - 1.0, size - 1.0), r, r);
    return path;
}

/// Fabric：织出来的斜纹（一眼能认出是"布"）。
void drawFabricMark(QPainter &p, int size) {
    drawTile(p, size, kFabricBg);
    p.save();
    p.setClipPath(tilePath(size));
    QPen pen(QColor(255, 255, 255, 220));
    pen.setWidthF(u(1.5, size));
    p.setPen(pen);
    for (int i = -1; i <= 6; ++i) {
        p.drawLine(QPointF(u(i * 3.0, size), u(15.0, size)),
                   QPointF(u(i * 3.0 + 5.5, size), u(1.0, size)));
    }
    p.restore();
}

/// Forge：铁砧剪影（宽台 + 细腰 + 宽底座）。
void drawForgeMark(QPainter &p, int size) {
    drawTile(p, size, kForgeBg);
    QPainterPath anvil;
    anvil.moveTo(u(2.5, size), u(5.0, size));
    anvil.lineTo(u(13.5, size), u(5.0, size));
    anvil.lineTo(u(13.5, size), u(7.5, size));
    anvil.lineTo(u(10.0, size), u(8.5, size));
    anvil.lineTo(u(10.0, size), u(11.0, size));
    anvil.lineTo(u(12.5, size), u(12.0, size));
    anvil.lineTo(u(12.5, size), u(13.5, size));
    anvil.lineTo(u(3.5, size), u(13.5, size));
    anvil.lineTo(u(3.5, size), u(12.0, size));
    anvil.lineTo(u(6.0, size), u(11.0, size));
    anvil.lineTo(u(6.0, size), u(8.5, size));
    anvil.lineTo(u(2.5, size), u(7.5, size));
    anvil.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(kForgeInk);
    p.drawPath(anvil);
}

/// NeoForge：狐狸脑袋（两只尖耳朵 + 下半张白脸 + 两个黑点眼睛）。
void drawNeoForgeMark(QPainter &p, int size) {
    drawTile(p, size, kNeoBg);
    p.setPen(Qt::NoPen);
    p.setBrush(kNeoFox);
    // 耳朵先画（会被脸压住根部，看着像长在头上）
    QPolygonF leftEar({QPointF(u(3.0, size), u(2.0, size)), QPointF(u(6.5, size), u(2.0, size)),
                       QPointF(u(4.2, size), u(7.0, size))});
    QPolygonF rightEar({QPointF(u(13.0, size), u(2.0, size)), QPointF(u(9.5, size), u(2.0, size)),
                        QPointF(u(11.8, size), u(7.0, size))});
    p.drawPolygon(leftEar);
    p.drawPolygon(rightEar);
    const qreal r = u(2.0, size);
    p.drawRoundedRect(QRectF(u(3.5, size), u(5.0, size), u(9.0, size), u(8.5, size)), r, r);
    // 下半张脸是白的
    p.setBrush(QColor(0xf3, 0xf0, 0xea));
    p.drawRoundedRect(QRectF(u(5.5, size), u(9.0, size), u(5.0, size), u(4.5, size)),
                      u(1.5, size), u(1.5, size));
    // 眼睛和鼻子
    p.setBrush(kNeoBg);
    p.drawEllipse(QPointF(u(6.4, size), u(8.0, size)), u(0.9, size), u(0.9, size));
    p.drawEllipse(QPointF(u(9.6, size), u(8.0, size)), u(0.9, size), u(0.9, size));
    p.drawEllipse(QPointF(u(8.0, size), u(11.6, size)), u(0.8, size), u(0.8, size));
}

/// OptiFine：放大镜，镜片里是那块草方块（"把原版看得更清楚"那层意思）。
void drawOptiFineMark(QPainter &p, int size) {
    drawTile(p, size, kOptiBg);
    // 镜片里的草方块
    p.setPen(Qt::NoPen);
    p.setBrush(kGrassDark);
    p.drawRect(QRectF(u(5.0, size), u(5.5, size), u(4.0, size), u(1.6, size)));
    p.setBrush(kDirtMid);
    p.drawRect(QRectF(u(5.0, size), u(7.1, size), u(4.0, size), u(2.4, size)));
    // 镜框 + 手柄
    QPen pen(kOptiInk);
    pen.setWidthF(u(1.4, size));
    pen.setCapStyle(Qt::RoundCap);
    p.setBrush(Qt::NoBrush);
    p.setPen(pen);
    p.drawEllipse(QPointF(u(7.0, size), u(7.0, size)), u(4.2, size), u(4.2, size));
    QPen handle(kOptiInk);
    handle.setWidthF(u(1.8, size));
    handle.setCapStyle(Qt::RoundCap);
    p.setPen(handle);
    p.drawLine(QPointF(u(10.3, size), u(10.3, size)), QPointF(u(13.5, size), u(13.5, size)));
}

/// 画一次就够了：这几张图在一棵树上是**每一行都要用**的（900 多条 × 每次重绘），
/// 每次都现画一遍太浪费。缓存按"要画的东西"键住（颜色那几个组合是有限的）。
QPixmap cached(const QString &key, const std::function<QPixmap()> &make) {
    static QHash<QString, QPixmap> cache;
    const auto it = cache.constFind(key);
    if (it != cache.constEnd()) {
        return it.value();
    }
    const QPixmap pixmap = make();
    cache.insert(key, pixmap);
    return pixmap;
}

/// 加载器标志的画法表。**顺序**就是 MainWindowUI::modLoaderOptionNames() 那个顺序
/// （0 Fabric / 1 Forge / 2 NeoForge / 3 OptiFine），改了那边这里也要跟着改 ——
/// 对不上的症状是"勾了 Forge 前面画着狐狸"，不报错，只是难看。
void drawLoaderMark(QPainter &p, int index, int size) {
    switch (index) {
        case 0:
            drawFabricMark(p, size);
            return;
        case 1:
            drawForgeMark(p, size);
            return;
        case 2:
            drawNeoForgeMark(p, size);
            return;
        case 3:
            drawOptiFineMark(p, size);
            return;
        default:
            return;  // 没画（调用方会退回草方块）
    }
}

}  // namespace

QPixmap VersionIcons::grassBlock(int size) {
    if (size <= 0) {
        return QPixmap();
    }
    return cached(QStringLiteral("grass/%1").arg(size), [size]() {
        QImage img(16, 16, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        for (int x = 0; x < 16; ++x) {
            const int depth = kGrassDepth[x];
            for (int y = 0; y < 16; ++y) {
                QColor color;
                if (y <= depth - 3) {
                    color = kGrassLight;
                } else if (y <= depth - 1) {
                    color = kGrassDark;
                } else if (y == depth) {
                    color = kGrassEdge;
                } else {
                    switch (dirtShade(x, y)) {
                        case 0:
                            color = kDirtLight;
                            break;
                        case 1:
                            color = kDirtMid;
                            break;
                        default:
                            color = kDirtDark;
                            break;
                    }
                }
                img.setPixelColor(x, y, color);
            }
        }
        // 最近邻放大：像素画放大之后还得是方块，不能糊成一团。
        return QPixmap::fromImage(
            img.scaled(size, size, Qt::IgnoreAspectRatio, Qt::FastTransformation));
    });
}

QPixmap VersionIcons::loaderMark(int index, int size) {
    if (size <= 0) {
        return QPixmap();
    }
    // 「不装（纯原版）」和越界的下标都是草方块 —— 那两条路装出来的就是原版。
    if (index < 0 || index > 3) {
        return grassBlock(size);
    }
    return cached(QStringLiteral("mark/%1/%2").arg(index).arg(size), [index, size]() {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing, true);
        drawLoaderMark(p, index, size);
        return pixmap;
    });
}

QPixmap VersionIcons::chevron(bool expanded, int size, const QColor &color) {
    if (size <= 0) {
        return QPixmap();
    }
    return cached(QStringLiteral("chevron/%1/%2/%3").arg(expanded ? 1 : 0).arg(size).arg(color.name()),
                  [expanded, size, color]() {
                      QPixmap pixmap(size, size);
                      pixmap.fill(Qt::transparent);
                      QPainter p(&pixmap);
                      p.setRenderHint(QPainter::Antialiasing, true);
                      QPen pen(color);
                      // 细了看不见、粗了糊成一个三角：按边长给个比例，别用一个写死的像素数。
                      pen.setWidthF(qMax(1.6, size / 7.0));
                      pen.setCapStyle(Qt::RoundCap);
                      pen.setJoinStyle(Qt::RoundJoin);
                      p.setPen(pen);
                      const qreal a = size * 0.3;
                      const qreal b = size * 0.7;
                      const qreal mid = size * 0.5;
                      if (expanded) {
                          // 展开 = 朝下的 V
                          p.drawPolyline(QPolygonF({QPointF(a, size * 0.4),
                                                    QPointF(mid, size * 0.65),
                                                    QPointF(b, size * 0.4)}));
                      } else {
                          // 收起 = 朝右的 >
                          p.drawPolyline(QPolygonF({QPointF(size * 0.4, a),
                                                    QPointF(size * 0.65, mid),
                                                    QPointF(size * 0.4, b)}));
                      }
                      return pixmap;
                  });
}
