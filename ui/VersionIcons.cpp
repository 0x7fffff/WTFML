#include "VersionIcons.hpp"

#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QtMath>

#include <functional>

namespace {

// ---------------------------------------------------------------------------
// 草方块：16×16 的像素画，照 MC 那块贴图的侧面（上面一层绿、底下一层土，
// 草的下沿是锯齿状的）。颜色是挑的近似色，不是从游戏里抠出来的。
//
// 快照版用的是同一张底图、只把上面那层换成白色（覆雪草块），远古版一点草都不加
// （整块土）—— 三张共用 blockImage()，用户 2026-09-23 定：「快照版的草用白色
// （覆雪草块），远古版就土块，愚人节版用幸运方块，原版不变」。
// ---------------------------------------------------------------------------
const QColor kGrassLight(0x8c, 0xc8, 0x5a);
const QColor kGrassDark(0x5d, 0x9c, 0x3f);
const QColor kGrassEdge(0x4a, 0x82, 0x30);
const QColor kDirtLight(0x9c, 0x70, 0x4a);
const QColor kDirtMid(0x84, 0x5f, 0x3d);
const QColor kDirtDark(0x6e, 0x4c, 0x30);

/// 覆雪草块上面那三层（雪白 / 阴面 / 下沿）。照着草方块那三层的位置放，只是换成白的。
const QColor kSnowLight(0xf4, 0xf9, 0xfc);
const QColor kSnowDark(0xdd, 0xe9, 0xf2);
const QColor kSnowEdge(0xbc, 0xcf, 0xdc);

/// 幸运方块那几档金（底 / 深一档的斑 / 边），外加问号那个奶油色。
const QColor kLuckyLight(0xe9, 0xc2, 0x4c);
const QColor kLuckyDark(0xd0, 0xa3, 0x2a);
const QColor kLuckyEdge(0xa5, 0x7a, 0x16);
const QColor kLuckyInk(0xf8, 0xf2, 0xd2);

/// 幸运方块上那个问号：5 格宽 × 7 格高，`#` 是要画成奶油色的格子（最后那一行是底下那一点，
/// 中间空的那一行是它和上面那钩子之间的距离）。**写死**，不画字体 —— 字号一缩放就不是这个
/// 形状了，而且这一格要能对着像素断言。
const char *const kQuestionRows[7] = {
    ".###.",
    "#...#",
    "....#",
    "...#.",
    "..#..",
    ".....",
    "..#..",
};

/// 每一列草有多厚（那个锯齿下沿）。**写死**的，不调随机数 —— 截图要每次一模一样，
/// 而且"上面绿、下面土"这件事得能对着像素断言。覆雪草块和土块共用这张表。
const int kGrassDepth[16] = {3, 4, 4, 3, 5, 4, 3, 3, 4, 5, 4, 3, 4, 4, 3, 5};

/// 方块上那些深浅斑点（土上、金底上都是它）：看着不规整就行，不是随机数
/// （同样是"每次画出来必须一样"）。
int blockShade(int x, int y) { return (x * 7 + y * 13 + (x * y) % 5) % 3; }

/// 土那部分某一个格子什么颜色（三档斑）。草方块、覆雪草块、土块下面那截都是它。
QColor dirtPixel(int x, int y) {
    switch (blockShade(x, y)) {
        case 0:
            return kDirtLight;
        case 1:
            return kDirtMid;
        default:
            return kDirtDark;
    }
}

/// 一块 16×16 的方块底图。@p withTop 为假 = 上面一点草都没有（土块）；为真时上面那一层
/// 照 kGrassDepth 分层（顶色 → 阴面色 → 下沿色），底下全是土。
QImage blockImage(const QColor &topLight, const QColor &topDark, const QColor &topEdge,
                  bool withTop) {
    QImage img(16, 16, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    for (int x = 0; x < 16; ++x) {
        const int depth = kGrassDepth[x];
        for (int y = 0; y < 16; ++y) {
            QColor color = dirtPixel(x, y);
            if (withTop) {
                if (y <= depth - 3) {
                    color = topLight;
                } else if (y <= depth - 1) {
                    color = topDark;
                } else if (y == depth) {
                    color = topEdge;
                }
            }
            img.setPixelColor(x, y, color);
        }
    }
    return img;
}

/// 像素画放大到目标边长：最近邻，放大之后还得是方块，不能糊成一团。
QPixmap scaledBlock(const QImage &img, int size) {
    return QPixmap::fromImage(
        img.scaled(size, size, Qt::IgnoreAspectRatio, Qt::FastTransformation));
}

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
        return scaledBlock(blockImage(kGrassLight, kGrassDark, kGrassEdge, true), size);
    });
}

QPixmap VersionIcons::snowyGrassBlock(int size) {
    if (size <= 0) {
        return QPixmap();
    }
    return cached(QStringLiteral("snowy/%1").arg(size), [size]() {
        return scaledBlock(blockImage(kSnowLight, kSnowDark, kSnowEdge, true), size);
    });
}

QPixmap VersionIcons::dirtBlock(int size) {
    if (size <= 0) {
        return QPixmap();
    }
    return cached(QStringLiteral("dirt/%1").arg(size), [size]() {
        // 上面那三个颜色传土自己那三档：withTop 给的是假，它们一格都用不上 ——
        // 这么传是有意的，将来谁把那个假改成真，画出来还是土，不会突然冒出一层草。
        return scaledBlock(blockImage(kDirtLight, kDirtMid, kDirtDark, false), size);
    });
}

QPixmap VersionIcons::luckyBlock(int size) {
    if (size <= 0) {
        return QPixmap();
    }
    return cached(QStringLiteral("lucky/%1").arg(size), [size]() {
        QImage img(16, 16, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        for (int x = 0; x < 16; ++x) {
            for (int y = 0; y < 16; ++y) {
                // 最外一圈深一档：这个方块得看着是"一块"，不能糊成一片金。
                const bool frame = (x == 0 || y == 0 || x == 15 || y == 15);
                if (frame) {
                    img.setPixelColor(x, y, kLuckyEdge);
                } else {
                    img.setPixelColor(x, y,
                                      blockShade(x, y) == 0 ? kLuckyLight : kLuckyDark);
                }
            }
        }
        // 中间那个问号：5×7 的位图摆在 (5,4)，**写死**（见头文件里那句"不是画的字"）。
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if (kQuestionRows[row][col] == '#') {
                    img.setPixelColor(5 + col, 4 + row, kLuckyInk);
                }
            }
        }
        return scaledBlock(img, size);
    });
}

QStringList VersionIcons::iconCandidateKeys() {
    // 前四个走 versionChannelMark() 那个顺序（正式版 / 快照版 / 远古版 / 愚人节版），
    // 后四个走 loaderMark() 那个顺序（Fabric / Forge / NeoForge / OptiFine）。
    // 两张表各自的唯一出处都在它们自己的注释里，这里只是把两组键拼起来。
    return {QStringLiteral("grass"),    QStringLiteral("snowy"), QStringLiteral("dirt"),
            QStringLiteral("lucky"),    QStringLiteral("fabric"), QStringLiteral("forge"),
            QStringLiteral("neoforge"), QStringLiteral("optifine")};
}

QPixmap VersionIcons::iconCandidateMark(int index, int size) {
    if (index < 0 || index >= iconCandidateKeys().size()) {
        return grassBlock(size);  // 越界 = 认不出来 = 当原版（和 loaderMark 一个规矩）
    }
    if (index < 4) {
        return versionChannelMark(index, size);
    }
    return loaderMark(index - 4, size);
}

QPixmap VersionIcons::iconMarkForKey(const QString &key, int size) {
    const int index = iconCandidateKeys().indexOf(key);
    if (index < 0) {
        return QPixmap();  // 认不出来：空图，让调用方自己决定退回什么
    }
    return iconCandidateMark(index, size);
}

QString VersionIcons::iconKeyFromValue(const QString &value) {
    const QString prefix = QStringLiteral("builtin:");
    if (!value.startsWith(prefix)) {
        return QString();
    }
    return value.mid(prefix.size());
}

QString VersionIcons::iconValueForKey(const QString &key) {
    return QStringLiteral("builtin:") + key;
}

QPixmap VersionIcons::fileMark(const QString &path, int size) {
    if (path.isEmpty() || size <= 0) {
        return QPixmap();
    }
    const QFileInfo info(path);
    if (!info.exists()) {
        return QPixmap();  // 文件没了：空图（调用方退回默认那张）
    }
    const QString key = QStringLiteral("file/%1/%2/%3")
                            .arg(path)
                            .arg(info.lastModified().toMSecsSinceEpoch())
                            .arg(size);
    return cached(key, [path, size]() {
        const QPixmap source(path);
        if (source.isNull()) {
            // 认不出来的格式（用户挑了个 .txt）或半截文件：**空图**，不是一张黑图。
            // 调用方退回默认那张，用户看到的是"它没生效"，而不是"我的图变成黑的了"。
            return QPixmap();
        }
        // 先按长宽比缩，再补成方的：树上那一格是方的，直接把一张扁图塞进去会被拉变形。
        const QPixmap scaled =
            source.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QPixmap canvas(size, size);
        canvas.fill(Qt::transparent);
        QPainter p(&canvas);
        p.drawPixmap((size - scaled.width()) / 2, (size - scaled.height()) / 2, scaled);
        return canvas;
    });
}

QPixmap VersionIcons::versionChannelMark(int channelIndex, int size) {
    if (size <= 0) {
        return QPixmap();
    }
    switch (channelIndex) {
        case 0:
            return grassBlock(size);  // 正式版：原版那个草方块，不变
        case 1:
            return snowyGrassBlock(size);
        case 2:
            return dirtBlock(size);
        case 3:
            return luckyBlock(size);
        default:
            return grassBlock(size);  // 越界 = 认不出来 = 当原版（和 loaderMark 一个规矩）
    }
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

namespace {

/// 箭头三个朝向。**笔宽、端头这些只有一份** —— 版本树那两行和底栏那两颗按钮上画的
/// 必须是同一支笔画出来的箭头，不然同一个界面里的箭头会粗细不一。
enum class ChevronDir { Right, Down, Up };

/// 那两笔的坐标（都是边长的比例，和原来的 0.3/0.7/0.65 那几个数是同一组）。
QPolygonF chevronShape(ChevronDir dir, int size) {
    const auto at = [size](qreal v) { return v * size; };
    switch (dir) {
    case ChevronDir::Up:
        // 展开方向朝上 = 朝下那个 V 翻过来（底栏那两颗按钮点开是**向上**弹的）
        return QPolygonF({QPointF(at(0.3), at(0.6)), QPointF(at(0.5), at(0.35)),
                          QPointF(at(0.7), at(0.6))});
    case ChevronDir::Down:
        return QPolygonF({QPointF(at(0.3), at(0.4)), QPointF(at(0.5), at(0.65)),
                          QPointF(at(0.7), at(0.4))});
    case ChevronDir::Right:
        break;
    }
    return QPolygonF({QPointF(at(0.4), at(0.3)), QPointF(at(0.65), at(0.5)),
                      QPointF(at(0.4), at(0.7))});
}

/// 按朝向画一张、带缓存（缓存键里带朝向，三个朝向各存各的）。
QPixmap chevronPixmap(ChevronDir dir, int size, const QColor &color) {
    if (size <= 0) {
        return QPixmap();
    }
    return cached(QStringLiteral("chevron/%1/%2/%3").arg(int(dir)).arg(size).arg(color.name()),
                  [dir, size, color]() {
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
                      p.drawPolyline(chevronShape(dir, size));
                      return pixmap;
                  });
}

/// 多边形上离 p 最近的那一点（三条边各投一次影，取最近的那个）。
/// 只给刷新那个箭头用：转动之后要量"弧的端头离箭头还差多远"。
QPointF closestPointOnPolygon(const QPolygonF &poly, const QPointF &p) {
    QPointF best;
    qreal bestDistance = -1.0;
    for (int i = 0; i < poly.size(); ++i) {
        const QPointF a = poly.at(i);
        const QPointF b = poly.at((i + 1) % poly.size());
        const QPointF edge = b - a;
        const qreal length2 = edge.x() * edge.x() + edge.y() * edge.y();
        qreal t = 0.0;
        if (length2 > 0.0) {
            t = qBound(0.0,
                       ((p.x() - a.x()) * edge.x() + (p.y() - a.y()) * edge.y()) / length2, 1.0);
        }
        const QPointF candidate = a + edge * t;
        const qreal distance = std::hypot(candidate.x() - p.x(), candidate.y() - p.y());
        if (bestDistance < 0.0 || distance < bestDistance) {
            bestDistance = distance;
            best = candidate;
        }
    }
    return best;
}

/// 把多边形绕 pivot 转 deg 度。正数 = **屏幕上顺时针**（y 轴朝下，所以就是普通的旋转矩阵）。
QPolygonF rotatedPolygon(const QPolygonF &poly, const QPointF &pivot, qreal deg) {
    const qreal angle = qDegreesToRadians(deg);
    const qreal c = qCos(angle);
    const qreal s = qSin(angle);
    QPolygonF out;
    out.reserve(poly.size());
    for (const QPointF &point : poly) {
        const qreal dx = point.x() - pivot.x();
        const qreal dy = point.y() - pivot.y();
        out.append(QPointF(pivot.x() + dx * c - dy * s, pivot.y() + dx * s + dy * c));
    }
    return out;
}

}  // namespace

QPixmap VersionIcons::chevron(bool expanded, int size, const QColor &color) {
    return chevronPixmap(expanded ? ChevronDir::Down : ChevronDir::Right, size, color);
}

QPixmap VersionIcons::chevronUp(int size, const QColor &color) {
    return chevronPixmap(ChevronDir::Up, size, color);
}

namespace {

/// ↑ / ↓ 这一对箭头的形状（带杆的实心箭头）—— **只有这一份**，两个方向由 `up` 挑。
///
/// 为什么合成一个函数（2026-09-25 晚上加 ↓ 的时候）：这两颗在**同一格里轮流出现**
/// （不在置顶组 → ↑「置顶」，在置顶组 → ↓「取消置顶」），比例必须逐像素对称 ——
/// 谁照着一份手抄一遍，改了一边比例之后另一边就不动了，而屏幕上表现成"两颗箭头
/// 长得不太一样"（很难认出是哪一处没跟上）。所以上下两个方向由**同一组比例**翻过来：
/// 上箭头里"离上边 0.15 的那个"就是下箭头里"离下边 0.15 的那个"。
QPixmap arrowPixmap(bool up, int size, const QColor &color) {
    if (size <= 0) {
        return QPixmap();
    }
    return cached(QStringLiteral("arrow/%1/%2/%3").arg(up ? 1 : 0).arg(size).arg(color.name()),
                  [up, size, color]() {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing, true);
        // 三角和杆都是**实心填充**（和 triangleUp/gear 一个路数，笔宽在 16 px 下描边会糊）：
        // 一颗 16 px 的按钮里，"一根杆 + 一个箭头"得是两块实心的面才看得清。
        p.setPen(Qt::NoPen);
        p.setBrush(color);

        // 一维尺寸全按边长给比例（别写死像素）：这个字形在实例行里是 16 px，别处要用
        // 也就是同一个函数换个 size。
        //
        // 这几档是**量出来的**（2026-09-25），两个约束一起定死它们：
        //   ① 最外一圈不留色 ⇒ 三角形的左右两个底角、尖端、杆底，四个方向离画布边
        //      都得有 2 px 以上（16 px 那一档：左右各到 2.6 / 13.4，上下到 2.4 / 13.6）；
        //   ② 箭头得比杆宽出一截才读得出"这是个箭头" ⇒ 半底 0.34 对杆半宽 0.10，
        //      差了 3.4 倍（16 px 下箭头宽 10.9、杆宽 3.2）。
        // 杆**不画满整格**：从箭头底边一直伸到 0.85，那一头留出同样的余量。
        const qreal centerX = size * 0.5;
        const qreal headHalf = size * 0.34;  // 箭头底边的半宽
        const qreal shaftHalf = size * 0.10; // 杆的半宽
        // 尖端 / 底边 / 杆末：下箭头就是整条竖直翻过来（`1 - x`），**不是**另给一组数。
        const qreal tip = up ? size * 0.15 : size * 0.85;
        const qreal headBase = up ? size * 0.52 : size * 0.48;
        const qreal shaftEnd = up ? size * 0.85 : size * 0.15;

        // 先画箭头、再画杆：杆的那条边和箭头底边**重合在同一条线**上，同色叠上去之后
        // 接缝处不会留下一条比别处浅的线（抗锯齿只在整幅的外轮廓上走）。
        QPolygonF head({QPointF(centerX, tip), QPointF(centerX + headHalf, headBase),
                        QPointF(centerX - headHalf, headBase)});
        p.drawPolygon(head);
        p.drawRect(QRectF(centerX - shaftHalf, qMin(headBase, shaftEnd), shaftHalf * 2,
                          qAbs(shaftEnd - headBase)));
        return pixmap;
    });
}

}  // namespace

QPixmap VersionIcons::arrowUp(int size, const QColor &color) {
    return arrowPixmap(true, size, color);
}

QPixmap VersionIcons::arrowDown(int size, const QColor &color) {
    return arrowPixmap(false, size, color);
}

namespace {

/// 上移/下移那两颗箭头的形状：一个**实心三角**（▲ / ▼）。
///
/// 为什么不用现成的那两支 chevron（^ / V）：组行行尾本来就有一个 chevron（展开/收起的
/// 那个），再摆两个同一支笔画的 V，一行里三个 V，用户分不出哪个是"展开"、哪个是"挪"。
/// 实心三角是另一族的形状（滚动条/数字框那两个小箭头就是它），一眼就分得开。
///
/// 尺寸取边长的 64% × 44%（宽一点、扁一点，和旁边 12 px 的 chevron 摆在一起才配得上），
/// 上下各留 28% 的边距 —— 贴到边上会看着比同一个格里的箭头大一号。
///
/// NOTE: 44% / 28% 是从下面那四行里**量出来**的（2026-09-25 改）：这里原来写的是
///       "50% / 上下各 25%"，那是另一个形状（更方、更贴边）—— 而代码一直画的是
///       0.28~0.72 这一档。谁照着旧注释去调形状，调出来的都不会是屏幕上这个东西。
QPolygonF triangleShape(bool up, int size) {
    const qreal s = size;
    const qreal left = s * 0.18;
    const qreal right = s * 0.82;
    const qreal near = s * 0.28;   // 底边（朝下那个的上沿）
    const qreal apex = s * 0.72;   // 尖端
    if (up) {
        return QPolygonF({QPointF(s * 0.5, near), QPointF(right, apex), QPointF(left, apex)});
    }
    return QPolygonF({QPointF(left, near), QPointF(right, near), QPointF(s * 0.5, apex)});
}

/// 画一个实心三角，带缓存（两个朝向各存各的）。
QPixmap trianglePixmap(bool up, int size, const QColor &color) {
    if (size <= 0) {
        return QPixmap();
    }
    return cached(QStringLiteral("triangle/%1/%2/%3").arg(up ? 1 : 0).arg(size).arg(color.name()),
                  [up, size, color]() {
                      QPixmap pixmap(size, size);
                      pixmap.fill(Qt::transparent);
                      QPainter p(&pixmap);
                      p.setRenderHint(QPainter::Antialiasing, true);
                      p.setPen(Qt::NoPen);
                      p.setBrush(color);
                      p.drawPolygon(triangleShape(up, size));
                      return pixmap;
                  });
}

}  // namespace

QPixmap VersionIcons::triangleUp(int size, const QColor &color) {
    return trianglePixmap(true, size, color);
}

QPixmap VersionIcons::triangleDown(int size, const QColor &color) {
    return trianglePixmap(false, size, color);
}

QPixmap VersionIcons::cross(int size, const QColor &color) {
    if (size <= 0) {
        return QPixmap();
    }
    return cached(QStringLiteral("cross/%1/%2").arg(size).arg(color.name()), [size, color]() {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(color);
        // 和 chevron 同一个比例：细了看不见、粗了糊成一个点。
        pen.setWidthF(qMax(1.6, size / 7.0));
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        // 两条对角线。留 22% 的边距：贴到边上会看着比同一个格里的箭头大一号。
        const qreal a = size * 0.22;
        const qreal b = size * 0.78;
        p.drawLine(QPointF(a, a), QPointF(b, b));
        p.drawLine(QPointF(b, a), QPointF(a, b));
        return pixmap;
    });
}

QPixmap VersionIcons::windowMinimize(int size, const QColor &color) {
    if (size <= 0) {
        return QPixmap();
    }
    return cached(QStringLiteral("winMin/%1/%2").arg(size).arg(color.name()), [size, color]() {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(color);
        // 和 cross 一支笔：同样的 size/7 + 圆头。两颗按钮就挨着，粗细不一样看得出来。
        pen.setWidthF(qMax(1.6, size / 7.0));
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        // 左右各留 22%（cross 也是 22%）：只画中间那一段横杠，不顶到格子边上。
        const qreal y = size * 0.5;
        p.drawLine(QPointF(size * 0.22, y), QPointF(size * 0.78, y));
        return pixmap;
    });
}

QPixmap VersionIcons::refresh(int size, const QColor &color) {
    if (size <= 0) {
        return QPixmap();
    }
    return cached(QStringLiteral("refresh/%1/%2").arg(size).arg(color.name()), [size, color]() {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing, true);

        // 一笔通到底的粗细：和 chevron / cross 同一个比例（size/7 ≈ 0.14），只是下界
        // 抬高一点 —— 1.6 是"一条线"的下界，圆环那么细会发虚。
        const qreal penWidth = qMax(1.5, size * 0.13);

        // 圆心与半径：四周留 20% 边距（和 cross 那个 22% 是同一个打算 —— 贴到边上会
        // 看着比同一个格里的箭头大一号）。笔宽和箭头都在这圈里，不会顶出去。
        //
        // NOTE: 半径从 0.30 收到 0.29 是 2026-09-24 跟箭头那次一起改的：箭头变长之后，
        //       它那个底角会往圆心外那侧顶出去（见下面那段算账），环小一号正好让出地方。
        const QPointF center(size / 2.0, size / 2.0);
        const qreal radius = size * 0.29;
        const QRectF box(center.x() - radius, center.y() - radius, 2 * radius, 2 * radius);

        // 角度按 Qt 那套：0° 在正右、逆时针为正、**y 轴朝下**（所以 90° 是正上）。
        // 缺口留在**正上方偏右**：起笔 30°、顺时针扫 285°（Qt 里负角就是顺时针）→ 收在
        // 105°（左上），那 75° 就是缺口 —— 不断开的话就只是一个圆圈，读不出"转一圈回来"
        // 这层意思。
        //
        // NOTE: 起笔位置 2026-09-24 从 55° 挪到了 30°（用户原话：「刷新键的"箭头"太小，
        //       调大点」）。箭头是**顺着切向**画的，它的底边落在"来路上"，也就是往
        //       12 点方向退 —— 箭头一长，那个底角就顶到格子顶上去了（55° 那一档实测已经
        //       顶出画布外，会被裁掉一个角）。挪到 30° 之后来路偏向右上、上下留白够，
        //       箭头才敢加大（0.22 → 0.32 长、0.13 → 0.165 半底），缺口还是 75°。
        constexpr qreal kHeadDeg = 30.0;
        constexpr qreal kSweepDeg = -285.0;

        QPen pen(color);
        pen.setWidthF(penWidth);
        // 端头用平头：那一头会被下面的三角盖住，圆帽反而会从三角边上冒出来一小截。
        pen.setCapStyle(Qt::FlatCap);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawArc(box, int(kHeadDeg * 16), int(kSweepDeg * 16));

        // 箭头画在 30° 那一头（两点那个位置）：**实心三角**，顶点顺着"顺时针继续转"
        // 那个方向伸出去一点，底边横在来路上、**按径向**排开。
        //
        // NOTE: 原来是"两笔画一个 V"（顶点在弧上、两翼向来路张开）—— 18 px 下那两翼会
        //       横着穿过圈身，看着像挂在圈上的一个多余疙瘩（用户 2026-09-23 原话：
        //       "图标能不能重新画一个，好诡异"）。同样大小的实心三角干净得多，也是
        //       浏览器刷新钮那种通用长相。
        const qreal headRad = qDegreesToRadians(kHeadDeg);
        const QPointF onArc(center.x() + radius * qCos(headRad),
                            center.y() - radius * qSin(headRad));
        // 顺时针的切向（屏幕坐标，y 朝下）= (sinθ, cosθ)；径向朝外 = (cosθ, −sinθ)。
        const QPointF travel(qSin(headRad), qCos(headRad));
        const QPointF outward(qCos(headRad), -qSin(headRad));
        // 三角的三条边都按边长给比例：长 32%、底边半宽 16.5%（≈ 2.5 倍笔宽，看着才"实"）。
        // 这一档是 2026-09-24 调大的（原来 22% / 13%）—— 老那一档在界面上那个 18 px 的
        // 格子里只有 4.0 px 长、2.3 px 半底，和环身差不多粗，读起来更像环上沾了个疙瘩
        // 而不是"往这儿转"。
        //
        // NOTE: 上限是画布：这个三角**沿切向**伸出去，离圆心最远的那个角是整张图里最靠
        //       边的一点（30° 这一档落在 y = 0.037×size，也就是顶上第二三个像素那一行）。
        //       再长就出画布被裁掉一个角了——ui_check 有一条断言量"最外一圈一个有色像素
        //       都没有"，改这两个数之前先跑它。
        const qreal length = size * 0.32;
        const qreal halfBase = size * 0.165;
        const QPointF tip = onArc + travel * (length * 0.15);
        const QPointF base = tip - travel * length;
        // 摆好之后再整体**绕它自己的重心转 −60°**（符号按 rotatedPolygon 那套：正数是屏幕上
        // 的顺时针，所以 −60 就是屏幕上逆时针 60°）。
        //
        // NOTE: 这个角度是用户 2026-09-24 从一张"每 15° 一格"的扫描图上挑的（原话：「实心
        //       三角的角度不对，右侧边现在是竖直的，感觉要在现在的状态下顺时针旋转45~60deg
        //       才好」）。不转的话，底边是顺着**半径**摆的，那条外边（顶点→外底角）正好落到
        //       竖直上：20 px 那一格里 dx = −0.31、dy = −6.47，看着像面竖着的小旗子。
        constexpr qreal kHeadTurnDeg = -60.0;
        const QPointF centroid = tip - travel * (length * 2.0 / 3.0);
        QPolygonF head({tip, base + outward * halfBase, base - outward * halfBase});
        head = rotatedPolygon(head, centroid, kHeadTurnDeg);

        // 绕重心转完，三角会从弧的端头**飘开**一小截（用户原话：「三角形由于带一点点白边
        // 会使得圆圈和三角形连接处有点白色像断开了」）：转之前弧的端点正好埋在三角里
        // 85% 那一段上，转之后它落到三角外面去了（20 px 那一档差 1.3 px）。
        //
        // 挪回去的办法量的是**最小的一步**：找三角边界上离弧端点最近的那一点，沿"从弧端点
        // 指向三角"这个方向把它推过去，推的比距离多半个笔宽 —— 多埋一点，弧那一笔和三角
        // 才是**实心叠实心**，相接处不会留下一条比别处浅的线（抗锯齿只在整幅的外轮廓上走）。
        const QPointF toHead = closestPointOnPolygon(head, onArc) - onArc;
        const qreal gap = std::hypot(toHead.x(), toHead.y());
        if (gap > 0.0) {
            const QPointF shift = -(toHead / gap) * (gap + penWidth * 0.5);
            for (QPointF &point : head) {
                point += shift;
            }
        }
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPolygon(head);
        return pixmap;
    });
}

QPixmap VersionIcons::power(int size, const QColor &color) {
    if (size <= 0) {
        return QPixmap();
    }
    return cached(QStringLiteral("power/%1/%2").arg(size).arg(color.name()), [size, color]() {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing, true);

        // 笔宽和 cross / chevron 同一个比例（size/7 ≈ 0.14）：同一行里几个图标得看着是
        // 同一支笔画出来的，细一档就会显得"这颗按钮小一号"。
        QPen pen(color);
        pen.setWidthF(qMax(1.4, size / 7.0));
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);

        // 环心**略低于**正中：上面那一竖要从缺口里穿出去，整幅的重心落在格子里才正。
        const QPointF center(size / 2.0, size * 0.54);
        const qreal radius = size * 0.30;
        const QRectF box(center.x() - radius, center.y() - radius, 2 * radius, 2 * radius);
        // 缺口开在**正上方、左右对称**（50° 起、顺时针扫 280° → 收在 130°，空的是
        // 50°~130° 那 80°）：机箱上那个开关就是这个长相，缺口偏一边会读成"转圈箭头"。
        // 角度按 Qt 那套（0° 在正右、逆时针为正、90° 是正上），见 refresh()。
        p.drawArc(box, 50 * 16, -280 * 16);

        // 那一竖：从环的上沿之上一点点，穿进缺口、落到接近环心。
        // NOTE: 起点**不能贴着格子上边** —— RoundCap 两头各撑出半个笔宽，贴边那一头会
        //       顶得比整个环还显眼（这一竖看着比环粗，就是这个）。
        p.drawLine(QPointF(center.x(), center.y() - radius - size * 0.07),
                   QPointF(center.x(), center.y() - size * 0.04));
        return pixmap;
    });
}

QPixmap VersionIcons::gear(int size, const QColor &color) {
    if (size <= 0) {
        return QPixmap();
    }
    return cached(QStringLiteral("gear/%1/%2").arg(size).arg(color.name()), [size, color]() {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing, true);

        // 八颗**平顶**的牙（不是星形）：星形在 16 px 下看着像个雪花，齿顶那一段平的
        // 才读得出是个齿轮。一颗牙占一个齿距的：齿根 0.10 起、齿顶 0.30~0.70、
        // 齿根 0.90 收，于是"齿"宽 40%、两颗牙之间那段根宽 20% —— 两条缝一样宽才对称。
        constexpr int kTeeth = 8;
        const qreal pitch = 360.0 / kTeeth;
        const QPointF center(size / 2.0, size / 2.0);
        const qreal rTip = size * 0.47;
        const qreal rRoot = size * 0.33;

        // 角度按 Qt 那套（90° 是正上），屏幕坐标 y 朝下所以取负号。
        const auto at = [&](qreal degree, qreal r) {
            const qreal rad = qDegreesToRadians(degree);
            return QPointF(center.x() + r * qCos(rad), center.y() - r * qSin(rad));
        };

        QPainterPath toothed;
        for (int i = 0; i < kTeeth; ++i) {
            const qreal base = i * pitch;
            const QPointF corner[4] = {at(base + pitch * 0.10, rRoot),
                                       at(base + pitch * 0.30, rTip),
                                       at(base + pitch * 0.70, rTip),
                                       at(base + pitch * 0.90, rRoot)};
            if (i == 0) {
                toothed.moveTo(corner[0]);
            } else {
                toothed.lineTo(corner[0]);
            }
            toothed.lineTo(corner[1]);
            toothed.lineTo(corner[2]);
            toothed.lineTo(corner[3]);
        }
        // 收口：最后那颗牙的齿根连回第一颗的齿根，跨的也是 0.90~1.10 那 20%。
        toothed.closeSubpath();

        // 中间那个洞走**减去一个圆**，不是描边一个圆：16 px 下描边那一圈糊成一片，
        // 整颗齿轮会变成一个实心疙瘩。
        QPainterPath hole;
        hole.addEllipse(center, size * 0.15, size * 0.15);

        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPath(toothed.subtracted(hole));
        return pixmap;
    });
}

QPixmap VersionIcons::pen(int size, const QColor &color) {
    if (size <= 0) {
        return QPixmap();
    }
    return cached(QStringLiteral("pen/%1/%2").arg(size).arg(color.name()), [size, color]() {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing, true);

        // 整支笔先当**笔尖朝下、笔杆竖直**画，最后把坐标系转 −45°：
        // 这样"杆、箍、尖"三段的坐标全是纵轴上的一维数，比直接算斜着的四个角好读也难写错。
        // 屏幕坐标 y 朝下，所以 −45° 转出来的笔尖落在**左下**（用户要的"斜着的"）。
        p.translate(size / 2.0, size / 2.0);
        p.rotate(-45.0);
        p.setPen(Qt::NoPen);
        p.setBrush(color);

        // 一维尺寸全按边长给：长 0.80、杆宽 0.24、尖长 0.20、箍宽 0.06。
        // 四周留的边距是**转过来之后**最靠外那一点到画布边的余量（0.10×size）——
        // 45° 下对角长度是边长，直接铺满会被裁掉笔尖和笔尾两个角。
        const qreal length = size * 0.80;
        const qreal halfWidth = size * 0.12;
        const qreal top = -length / 2.0;
        const qreal tipStart = length / 2.0 - size * 0.20;
        const qreal bandHeight = size * 0.06;
        /// 箍和杆之间那道缝（也是"挖"出来的，见 folderMark 里那段）。
        const qreal gap = qMax(1.0, size * 0.05);

        // 笔尖：一个三角形，顶点在下（转完在左下角）。
        QPainterPath tip;
        tip.moveTo(-halfWidth, tipStart);
        tip.lineTo(halfWidth, tipStart);
        tip.lineTo(0.0, length / 2.0);
        tip.closeSubpath();
        // 杆：笔尾那一截往下、到笔尖为止。
        const qreal bandBottom = top + bandHeight;
        p.drawRect(QRectF(-halfWidth, bandBottom + gap, halfWidth * 2,
                          tipStart - bandBottom - gap));
        // 笔尾那一小截（箍）：和杆之间留一道透明缝 —— 18 px 下整根实心条读起来像一根
        // 斜杠，断开一截才看得出是"笔"。
        p.drawRect(QRectF(-halfWidth, top, halfWidth * 2, bandHeight));
        p.drawPath(tip);
        return pixmap;
    });
}

QPixmap VersionIcons::folderMark(int size, const QColor &color) {
    if (size <= 0) {
        return QPixmap();
    }
    return cached(QStringLiteral("folder/%1/%2").arg(size).arg(color.name()), [size, color]() {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(color);

        // 后背（含左上角那个突出来的标签页）和前面那块之间横着一道**透明缝**。
        // 缝是"挖掉"的，不是画一条底色线 —— 图标底下是菜单的底色，画死颜色会在
        // 浅色/深色主题里各错一次。
        const qreal left = size * 0.10;
        const qreal right = size * 0.90;
        const qreal top = size * 0.26;
        const qreal bottom = size * 0.84;
        // 标签页：从后背的上沿往上顶出去一块（这就是"文件夹"和"一块方片"的全部区别）。
        p.drawRect(QRectF(left, top - size * 0.13, size * 0.38, size * 0.13 + size * 0.04));
        const qreal creaseY = top + (bottom - top) * 0.26;
        const qreal creaseHeight = qMax(1.5, size * 0.09);
        // 后背：上沿到那道缝。
        p.drawRect(QRectF(left, top, right - left, creaseY - top));
        // 前脸：缝下面到底。
        p.drawRect(QRectF(left, creaseY + creaseHeight, right - left,
                          bottom - creaseY - creaseHeight));
        return pixmap;
    });
}

QPixmap VersionIcons::trash(int size, const QColor &color) {
    if (size <= 0) {
        return QPixmap();
    }
    return cached(QStringLiteral("trash/%1/%2").arg(size).arg(color.name()), [size, color]() {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(color);

        // 提手（盖子上那个小方块）。
        p.drawRect(QRectF(size * 0.38, size * 0.14, size * 0.24, size * 0.09));
        // 盖（比桶身宽一点，才看得出"盖在上面"）。
        p.drawRect(QRectF(size * 0.10, size * 0.25, size * 0.80, size * 0.10));

        // 桶身：往下收的梯形（上宽下窄）。和桶盖之间留一道透明缝。
        QPainterPath body;
        body.moveTo(size * 0.20, size * 0.39);
        body.lineTo(size * 0.80, size * 0.39);
        body.lineTo(size * 0.72, size * 0.86);
        body.lineTo(size * 0.28, size * 0.86);
        body.closeSubpath();

        // 桶身上两条**挖掉**的竖缝（同样不是画底色线）。
        // NOTE: 这个局部变量**不能叫 slots** —— `slots` 是 Qt 的关键字宏（空展开），
        //       `QPainterPath slots;` 会被预处理成 `QPainterPath ;`，编译器报的是
        //       "declaration does not declare anything"，和真正的原因差得很远。
        QPainterPath holes;
        holes.addRect(QRectF(size * 0.39, size * 0.48, size * 0.06, size * 0.28));
        holes.addRect(QRectF(size * 0.55, size * 0.48, size * 0.06, size * 0.28));
        p.drawPath(body.subtracted(holes));
        return pixmap;
    });
}

QPixmap VersionIcons::runStateMark(int size, const QColor &color) {
    if (size <= 0) {
        return QPixmap();
    }
    return cached(QStringLiteral("stateMark/%1/%2").arg(size).arg(color.name()), [size, color]() {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        // NOTE: **刻意不开抗锯齿**。别的字形都开（它们有斜线/圆弧），这一个没有：
        //       它就是个整格铺满的正方形，开了抗锯齿只会把四条边描成半透明的灰，
        //       于是"这一格是不是纯色"这件事就没法写断言了（四角会变成 50% 的色）。
        p.fillRect(QRect(0, 0, size, size), color);
        return pixmap;
    });
}
