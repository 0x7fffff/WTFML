/**
 * @file SkinModel.cpp
 * @brief 实现玩家皮肤模型的几何、投影与光栅化（见头文件里"为什么自己算"那段）。
 * @author 0x7fffff
 * @date 2026-09-21
 */

#include "SkinModel.hpp"

#include <QPolygonF>
#include <QTransform>
#include <QtMath>

#include <algorithm>

namespace SkinModel {
namespace {

/// 相机绕模型转的半径（皮肤像素）。固定值：画面大小靠焦距调（见 makeCamera），
/// 所以这个数只决定**透视有多强** —— 越小近大远小的效果越夸张。
constexpr double kCameraDistance = 64.0;

/// 模型的总高（脚底到头顶）。用来把焦距算成"让小人正好占满画面高度的一大半"。
constexpr double kModelHeight = 32.0;

/// 模型连手臂带披风的横向跨度（预估）。只在画面很扁的时候起作用 ——
/// 那种情况下要按**宽度**而不是高度来收，否则小人会横着溢出画面。
constexpr double kModelWidth = 24.0;

/// 自适应缩放：模型占画面高度的这个比例。留出余量给旋转时轮廓的变化
/// （俯仰角一大，投影高度就会变矮，得留够）。
constexpr double kFillByHeight = 0.85;
/// 画面很扁时按宽度算的那个比例。
constexpr double kFillByWidth = 0.90;

/// 模型中心的高度（32 高的小人，中心在 16）。相机绕着这个点转。
constexpr double kModelCenterY = 16.0;

/// 覆盖层向外膨胀的量（皮肤像素）。游戏里那层就是稍微大一圈的壳。
constexpr double kOverlayInflate = 0.5;

/// 俯仰角上限（度）。**正好 90° 是坏的**：那时视线和世界正上方平行，相机那对基向量
/// 里的"屏幕向右"退化成零向量（叉积为零），归一化出来是 NaN，整幅画面直接没了。
/// 留 1° 余量。这个值是硬约束不是审美，所以控件那一层夹的也是同一个（maxPitch()）。
constexpr double kMaxPitchDegrees = 89.0;

/// 一个面的"贴图往右"和"贴图往下"分别对应模型空间的哪个方向。
///
/// 这一张表是整个渲染里**最容易写反**的地方：反了的表现是贴图左右镜像（脸反了），
/// 而那种错误在缩略图大小下几乎看不出来。所以不逐面手写四个角，而是用这两个方向
/// 现算；表里每一行都必须满足 `right × up == normal`（右手系里从外面看的朝向），
/// 验证工程有一条断言把六个面这条恒等式全查一遍。
struct FaceBasis {
    QVector3D normal;
    QVector3D right;
    QVector3D up;
};

FaceBasis faceBasis(Face face) {
    switch (face) {
    case Face::Right:  // 玩家自己的右边（-x）。绕着他转一圈的下一个面是正面（+z）。
        return {QVector3D(-1, 0, 0), QVector3D(0, 0, 1), QVector3D(0, 1, 0)};
    case Face::Front:  // 脸。往右是 +x。
        return {QVector3D(0, 0, 1), QVector3D(1, 0, 0), QVector3D(0, 1, 0)};
    case Face::Left:  // +x。再往下绕就是背面（-z）。
        return {QVector3D(1, 0, 0), QVector3D(0, 0, -1), QVector3D(0, 1, 0)};
    case Face::Back:  // -z。绕回玩家自己的右边（-x）。
        return {QVector3D(0, 0, -1), QVector3D(-1, 0, 0), QVector3D(0, 1, 0)};
    case Face::Top:
        // 顶面：贴图上那块是接在**正面**上面的（这就是为什么头顶的头发出现在
        // 顶面那块矩形的最下面一行 —— 那一行贴着额头的发际线）。
        // 所以"贴图往下"指向正面那侧，也就是 +z。
        return {QVector3D(0, 1, 0), QVector3D(1, 0, 0), QVector3D(0, 0, -1)};
    case Face::Bottom:
        // 底面：这块几乎永远看不见（不是被身体挡着就是踩在脚底下），取一组满足
        // `right × up == normal` 的方向就行。这里让"贴图往右"跟顶面一致。
        return {QVector3D(0, -1, 0), QVector3D(1, 0, 0), QVector3D(0, 0, 1)};
    }
    return {QVector3D(0, 0, 1), QVector3D(1, 0, 0), QVector3D(0, 1, 0)};
}

/// 面中心：把长方体的中心按法线那一根轴推到对应的那一面上去。
QVector3D faceCenter(const QVector3D &lo, const QVector3D &hi, const QVector3D &normal) {
    QVector3D center = (lo + hi) / 2.0f;
    if (normal.x() != 0.0f) {
        center.setX(normal.x() > 0.0f ? hi.x() : lo.x());
    }
    if (normal.y() != 0.0f) {
        center.setY(normal.y() > 0.0f ? hi.y() : lo.y());
    }
    if (normal.z() != 0.0f) {
        center.setZ(normal.z() > 0.0f ? hi.z() : lo.z());
    }
    return center;
}

/// 一个部件某一面的尺寸（宽高厚，皮肤像素）。模型尺寸都是整数，四舍五入回来即可。
void boxSize(const Cuboid &box, int *w, int *h, int *d) {
    *w = qRound(box.hi.x() - box.lo.x());
    *h = qRound(box.hi.y() - box.lo.y());
    *d = qRound(box.hi.z() - box.lo.z());
}

/// 长方体的一个面：整体向外推 inflate（覆盖层用，普通层传 0）。
Cuboid inflatedBox(const Cuboid &box, double inflate) {
    if (inflate == 0.0) {
        return box;
    }
    Cuboid out = box;
    const QVector3D delta(static_cast<float>(inflate), static_cast<float>(inflate),
                          static_cast<float>(inflate));
    out.lo = box.lo - delta;
    out.hi = box.hi + delta;
    return out;
}

/// 画一个面要用的东西：屏幕上的四边形、贴图上的矩形、离眼睛多远。
struct DrawFace {
    QPolygonF screen;
    QRect uv;
    double depth = 0.0;
    const QImage *texture = nullptr;
};

/// 把一个部件的某一面投到屏幕上，塞进 faces 里。
void collectFace(QVector<DrawFace> &faces, const Cuboid &box, Face face, bool overlay,
                 const QImage &texture, const Camera &cam) {
    const FaceGeometry geometry = faceGeometry(box, face, overlay);
    if (!isFaceVisible(cam, geometry)) {
        return;  // 背面：看不见，连投都不用投
    }

    DrawFace draw;
    draw.uv = geometry.uv;
    draw.texture = &texture;
    double depthSum = 0.0;
    for (const QVector3D &corner : geometry.corners) {
        double depth = 0.0;
        draw.screen << project(cam, corner, &depth);
        depthSum += depth;
    }
    draw.depth = depthSum / static_cast<double>(geometry.corners.size());
    faces << draw;
}

// ---------- 默认外观的配色 ----------
// 照经典配色画的，不是官方贴图的复刻（见头文件里的说明）。
const QColor kSkinTone(0xC8, 0xA0, 0x78);
const QColor kHair(0x46, 0x2C, 0x16);
const QColor kShirt(0x00, 0xA8, 0xA8);
const QColor kPants(0x3F, 0x3F, 0xA5);
const QColor kShoe(0x54, 0x54, 0x54);
const QColor kEyeWhite(0xF0, 0xF0, 0xF0);
const QColor kEyeIris(0x2E, 0x3A, 0x8C);
const QColor kMouth(0x8C, 0x5A, 0x3C);

}  // namespace

const QVector<Face> &allFaces() {
    static const QVector<Face> faces = {Face::Right, Face::Front, Face::Left,
                                        Face::Back,  Face::Top,   Face::Bottom};
    return faces;
}

QString faceName(Face face) {
    switch (face) {
    case Face::Right:  return QStringLiteral("右侧");
    case Face::Front:  return QStringLiteral("正面");
    case Face::Left:   return QStringLiteral("左侧");
    case Face::Back:   return QStringLiteral("背面");
    case Face::Top:    return QStringLiteral("顶面");
    case Face::Bottom: return QStringLiteral("底面");
    }
    return QStringLiteral("?");
}

QRect faceRect(const QPoint &base, int w, int h, int d, Face face) {
    switch (face) {
    // 中间那一横排：右 → 前 → 左 → 后，每一块的宽度就是它对应的那一维。
    case Face::Right: return QRect(base.x(), base.y() + d, d, h);
    case Face::Front: return QRect(base.x() + d, base.y() + d, w, h);
    case Face::Left: return QRect(base.x() + d + w, base.y() + d, d, h);
    case Face::Back: return QRect(base.x() + d + w + d, base.y() + d, w, h);
    // 上面那一条：顶面接在正面那一列的上方，底面接在左面那一列的上方。
    case Face::Top: return QRect(base.x() + d, base.y(), w, d);
    case Face::Bottom: return QRect(base.x() + d + w, base.y(), w, d);
    }
    return {};
}

QVector<Cuboid> playerCuboids(bool slim) {
    // 单位是皮肤像素，y 向上、脚底在 0，所以整个人 32 高：
    // 腿 0~12、身体 12~24、头 24~32，手臂跟身体一样高、挂在两侧。
    // 贴图基准点那一列就是公认的那套值（头 (0,0)、身体 (16,16)、右腿 (0,16)……）。
    const double armWidth = slim ? 3.0 : 4.0;

    Cuboid head;
    head.name = QStringLiteral("头");
    head.lo = QVector3D(-4, 24, -4);
    head.hi = QVector3D(4, 32, 4);
    head.base = QPoint(0, 0);
    head.overlayBase = QPoint(32, 0);
    head.overlayInLegacy = true;  // 帽子层老皮肤里也有

    Cuboid body;
    body.name = QStringLiteral("身体");
    body.lo = QVector3D(-4, 12, -2);
    body.hi = QVector3D(4, 24, 2);
    body.base = QPoint(16, 16);
    body.overlayBase = QPoint(16, 32);

    Cuboid rightArm;
    rightArm.name = QStringLiteral("右臂");
    rightArm.lo = QVector3D(static_cast<float>(-4 - armWidth), 12, -2);
    rightArm.hi = QVector3D(-4, 24, 2);
    rightArm.base = QPoint(40, 16);
    rightArm.overlayBase = QPoint(40, 32);

    Cuboid leftArm;
    leftArm.name = QStringLiteral("左臂");
    leftArm.lo = QVector3D(4, 12, -2);
    leftArm.hi = QVector3D(static_cast<float>(4 + armWidth), 24, 2);
    leftArm.base = QPoint(32, 48);
    leftArm.overlayBase = QPoint(48, 48);

    Cuboid rightLeg;
    rightLeg.name = QStringLiteral("右腿");
    rightLeg.lo = QVector3D(-4, 0, -2);
    rightLeg.hi = QVector3D(0, 12, 2);
    rightLeg.base = QPoint(0, 16);
    rightLeg.overlayBase = QPoint(0, 32);

    Cuboid leftLeg;
    leftLeg.name = QStringLiteral("左腿");
    leftLeg.lo = QVector3D(0, 0, -2);
    leftLeg.hi = QVector3D(4, 12, 2);
    leftLeg.base = QPoint(16, 48);
    leftLeg.overlayBase = QPoint(0, 48);

    return {head, body, rightArm, leftArm, rightLeg, leftLeg};
}

Cuboid capeCuboid() {
    Cuboid cape;
    cape.name = QStringLiteral("披风");
    // 挂在后背，从肩膀（y=24）垂到 y=8。
    // NOTE: z 是**负**的 —— 玩家面朝 +z，所以"背后"在 -z 那侧（身体占 z -2..2）。
    //       从 -2.2 起是留了一条 0.2 的缝：和身体共面的话两个面会互相穿插着闪。
    //       披风比身体宽（10 对 8）也略低（垂到 8，而身体只到 12），这是有意的 ——
    //       游戏里从正面能看到披风从肩膀两侧和后腰下摆露出来一点。
    cape.lo = QVector3D(-5, 8, -3.2f);
    cape.hi = QVector3D(5, 24, -2.2f);
    cape.base = QPoint(0, 0);  // 披风自己那张 64×32 的图
    cape.hasOverlay = false;
    return cape;
}

FaceGeometry faceGeometry(const Cuboid &box, Face face, bool overlay) {
    const FaceBasis basis = faceBasis(face);
    const Cuboid shaped = inflatedBox(box, overlay ? kOverlayInflate : 0.0);
    const QVector3D size = shaped.hi - shaped.lo;

    int w = 0;
    int h = 0;
    int d = 0;
    boxSize(box, &w, &h, &d);

    FaceGeometry geometry;
    geometry.normal = basis.normal;
    geometry.uv = faceRect(overlay ? box.overlayBase : box.base, w, h, d, face);

    // 面在模型上的两个跨度。膨胀之后这两个跨度也跟着变大（覆盖层比里层大一圈），
    // 而贴图矩形**不变** —— 也就是把同一块贴图摊在稍大一点的面上。
    const bool alongX = (face == Face::Front || face == Face::Back || face == Face::Top ||
                         face == Face::Bottom);
    const double spanRight = alongX ? size.x() : size.z();
    const double spanUp = (face == Face::Top || face == Face::Bottom) ? size.z() : size.y();

    const QVector3D center = faceCenter(shaped.lo, shaped.hi, basis.normal);
    const QVector3D halfRight = basis.right * static_cast<float>(spanRight / 2.0);
    const QVector3D halfUp = basis.up * static_cast<float>(spanUp / 2.0);

    // 顺序照"从外面看"的左上 → 右上 → 右下 → 左下，和 uv 的四个角一一对应。
    geometry.corners = {center - halfRight + halfUp, center + halfRight + halfUp,
                        center + halfRight - halfUp, center - halfRight - halfUp};
    return geometry;
}

Camera makeCamera(const QRectF &target, double yaw, double pitch, double zoom) {
    // 夹一下俯仰角（理由见 kMaxPitchDegrees）。控件那一层也夹了，这里是兜底 ——
    // 这个函数是公开的，谁调都可能塞一个离谱的角度进来。
    const double pitchDegrees = qBound(-kMaxPitchDegrees, pitch, kMaxPitchDegrees);
    const double yawRadians = qDegreesToRadians(yaw);
    const double pitchRadians = qDegreesToRadians(pitchDegrees);

    const QVector3D center(0.0f, static_cast<float>(kModelCenterY), 0.0f);
    Camera cam;
    cam.eye = center + static_cast<float>(kCameraDistance) *
                           QVector3D(static_cast<float>(qSin(yawRadians) * qCos(pitchRadians)),
                                     static_cast<float>(qSin(pitchRadians)),
                                     static_cast<float>(qCos(yawRadians) * qCos(pitchRadians)));
    cam.forward = (center - cam.eye).normalized();
    cam.right = QVector3D::crossProduct(cam.forward, QVector3D(0, 1, 0)).normalized();
    cam.up = QVector3D::crossProduct(cam.right, cam.forward).normalized();
    cam.center = target.center();

    // 焦距按画面大小自适应：让 32 单位高的小人占画面高度的 kFillByHeight。
    // 画面很扁的时候改按宽度收 —— 不然小人会横着溢出。
    const double byHeight = kFillByHeight * target.height() * kCameraDistance / kModelHeight;
    const double byWidth = kFillByWidth * target.width() * kCameraDistance / kModelWidth;
    cam.focal = qMin(byHeight, byWidth) * zoom;
    return cam;
}

QPointF project(const Camera &cam, const QVector3D &modelPoint, double *depthOut) {
    const QVector3D v = modelPoint - cam.eye;
    const double depth = QVector3D::dotProduct(v, cam.forward);
    if (depthOut != nullptr) {
        *depthOut = depth;
    }
    if (depth <= 0.0) {
        // 在眼睛后面（只有缩到很近时才可能）：给个退化点，调用方靠背面剔除挡住它。
        return cam.center;
    }
    return cam.center + QPointF(cam.focal * QVector3D::dotProduct(v, cam.right) / depth,
                                -cam.focal * QVector3D::dotProduct(v, cam.up) / depth);
}

bool isFaceVisible(const Camera &cam, const FaceGeometry &face) {
    if (face.corners.isEmpty()) {
        return false;
    }
    // 一条对角线上的两个角的中点就是面中心（矩形对角线的中点）。
    const QVector3D center = (face.corners.first() + face.corners.last()) / 2.0f;
    return QVector3D::dotProduct(face.normal, cam.eye - center) > 0.0f;
}

QRectF projectedBounds(const QRectF &target, double yaw, double pitch, double zoom, bool slim,
                       bool withCape) {
    const Camera cam = makeCamera(target, yaw, pitch, zoom);
    QVector<Cuboid> boxes = playerCuboids(slim);
    if (withCape) {
        boxes << capeCuboid();
    }

    double minX = 0.0;
    double maxX = 0.0;
    double minY = 0.0;
    double maxY = 0.0;
    bool started = false;
    for (const Cuboid &box : boxes) {
        for (Face face : allFaces()) {
            for (const QVector3D &corner : faceGeometry(box, face).corners) {
                const QPointF point = project(cam, corner);
                if (!started) {
                    minX = maxX = point.x();
                    minY = maxY = point.y();
                    started = true;
                    continue;
                }
                minX = qMin(minX, point.x());
                maxX = qMax(maxX, point.x());
                minY = qMin(minY, point.y());
                maxY = qMax(maxY, point.y());
            }
        }
    }
    return started ? QRectF(QPointF(minX, minY), QPointF(maxX, maxY)) : QRectF();
}

void render(QPainter &painter, const QRectF &target, const Scene &scene) {
    if (scene.skin.isNull()) {
        return;
    }

    const Camera cam = makeCamera(target, scene.yaw, scene.pitch, scene.zoom);

    // 老格式的皮肤先把左臂左腿补齐（见 completeLegacySkin：那张图里没这两块）。
    // 注意下面判"有没有第二层"用的仍然是**原来那张**（64×32 就是没有第二层），
    // 补齐只是为了让取图不越界。
    const QImage skin = completeLegacySkin(scene.skin);

    QVector<DrawFace> faces;
    for (const Cuboid &box : playerCuboids(scene.slim)) {
        for (Face face : allFaces()) {
            collectFace(faces, box, face, false, skin, cam);
        }
        // 覆盖层：老格式的皮肤只有脑袋有第二层（见 Cuboid::overlayInLegacy）。
        if (box.hasOverlay && (box.overlayInLegacy || hasSecondLayer(scene.skin))) {
            for (Face face : allFaces()) {
                collectFace(faces, box, face, true, skin, cam);
            }
        }
    }
    if (!scene.cape.isNull()) {
        const Cuboid cape = capeCuboid();
        for (Face face : allFaces()) {
            collectFace(faces, cape, face, false, scene.cape, cam);
        }
    }

    // 画家算法：远的先画。对手模型这种"几个互不相交的凸多面体"来说这个顺序是精确的，
    // 理由见头文件里 render() 的说明。
    std::sort(faces.begin(), faces.end(),
              [](const DrawFace &a, const DrawFace &b) { return a.depth > b.depth; });

    painter.save();
    // 抗锯齿和插值**都关掉**，用最近邻。两个理由：皮肤是像素画（插值只会糊成一团，
    // 头像那边同一个理由），而且关了之后相邻两面的边缘是硬的、能严丝合缝地接上。
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    for (const DrawFace &face : faces) {
        if (face.texture == nullptr || face.texture->isNull()) {
            continue;
        }
        // 把这一面的贴图**先裁下来**再画，而不是"画整张图 + 裁到那一块"。
        // 后者有两个看得见的毛病，都是像素级的缝：
        //   ① 裁剪是按每个目标像素的采样点判的，采样点落在矩形外的那个像素整个被丢掉 ——
        //      于是每一面都比它该占的范围小半圈，相邻两面之间露出一条背景色的缝；
        //   ② 边缘像素的采样会落到隔壁那块贴图上（往往是透明的），边缘被"冲淡"，
        //      模型轮廓上多出一圈浅色描边。
        // 裁下来的小图两头都不沾：它的坐标系从 (0,0) 开始，采样永远不会越出这一面
        // 自己的像素，于是四面接得上、边缘也是干净的。
        const QImage patch = face.texture->copy(face.uv);
        if (patch.isNull()) {
            continue;
        }
        QTransform transform;
        // 四边形退化（几乎侧对着镜头）时这个会返回 false。**必须挡住**：
        // 用没被写过的 QTransform 画出来的东西会是一团乱。
        if (!QTransform::quadToQuad(
                QPolygonF(QRectF(0.0, 0.0, patch.width(), patch.height())), face.screen,
                transform)) {
            continue;
        }
        painter.setTransform(transform, false);
        painter.drawImage(QPointF(0.0, 0.0), patch);
    }
    painter.restore();
}

QImage defaultSteveSkin() {
    QImage skin(64, 64, QImage::Format_ARGB32_Premultiplied);
    skin.fill(Qt::transparent);

    QPainter painter(&skin);
    painter.setPen(Qt::NoPen);

    /// 给某个部件的某一面刷一种颜色。
    const auto fillFace = [&painter](const QPoint &base, int w, int h, int d, Face face,
                                     const QColor &color) {
        painter.fillRect(faceRect(base, w, h, d, face), color);
    };
    /// 整个部件六面同色。
    const auto fillBox = [&fillFace](const QPoint &base, int w, int h, int d,
                                     const QColor &color) {
        for (Face face : allFaces()) {
            fillFace(base, w, h, d, face, color);
        }
    };

    // ---- 脑袋：肤色打底，顶面/背面/四周上半截是头发 ----
    const QPoint head(0, 0);
    fillBox(head, 8, 8, 8, kSkinTone);
    fillFace(head, 8, 8, 8, Face::Top, kHair);
    fillFace(head, 8, 8, 8, Face::Back, kHair);
    for (Face face : {Face::Front, Face::Left, Face::Right}) {
        // 前额那三行。贴图上每一面都是从它的基准角往下数，所以直接在最上面盖一条。
        painter.fillRect(faceRect(head, 8, 8, 8, face).adjusted(0, 0, 0, -5), kHair);
    }
    // 眼睛：一白一蓝，蓝的在靠中间那一侧。
    painter.fillRect(QRect(9, 12, 1, 1), kEyeWhite);
    painter.fillRect(QRect(10, 12, 1, 1), kEyeIris);
    painter.fillRect(QRect(13, 12, 1, 1), kEyeIris);
    painter.fillRect(QRect(14, 12, 1, 1), kEyeWhite);
    // 嘴（一条深色的横线）。
    painter.fillRect(QRect(10, 14, 4, 1), kMouth);

    // ---- 身体：整件上衣 ----
    const QPoint body(16, 16);
    fillBox(body, 8, 12, 4, kShirt);

    // ---- 两条手臂和两条腿：一整件衣服打底，再把下面四行换成"袖子/裤脚以下露出来的部分" ----
    //
    // NOTE: 下面四行是**把上边往下挪 (h-4)** 得到的，不是"把上边往上挪 4"。
    //       QRect::adjusted 调的是两条边：adjusted(0,-4,0,0) 是把上边**往上**扩 4 行 ——
    //       那会画到隔壁那一面（甚至隔壁那个部件）的贴图上。踩过一次：整条手臂变成了
    //       肤色，因为往上扩的那 4 行正好盖住了它自己的全部。
    const auto fillLimb = [&painter](const QPoint &base, const QColor &cloth, const QColor &skin) {
        for (Face face : allFaces()) {
            const QRect r = faceRect(base, 4, 12, 4, face);
            painter.fillRect(r, cloth);
            if (face == Face::Top) {
                continue;  // 肩膀/胯：还是衣服（那一面本来就贴着身体）
            }
            if (face == Face::Bottom) {
                painter.fillRect(r, skin);  // 手心 / 鞋底
                continue;
            }
            // 四个侧面在贴图上是**同一段行号**（都从基准角往下数），所以四张脸的下四行
            // 落在同一片区域上，改一次就够 —— 这里还是逐面写，是为了不依赖那个巧合。
            painter.fillRect(r.adjusted(0, r.height() - 4, 0, 0), skin);
        }
    };
    fillLimb(QPoint(40, 16), kShirt, kSkinTone);  // 右臂
    fillLimb(QPoint(32, 48), kShirt, kSkinTone);  // 左臂
    fillLimb(QPoint(0, 16), kPants, kShoe);       // 右腿
    fillLimb(QPoint(16, 48), kPants, kShoe);      // 左腿

    // ---- 覆盖层：只画头发那几块，其余**保持透明**（透明才对：那层是"比里层多出来的
    //      部分"，没画到的地方本来就该露出里层）。这一层同时也是"alpha 混和没写错"
    //      的活证据 —— 它盖在头上，而脸还看得见。 ----
    const QPoint hat(32, 0);
    fillFace(hat, 8, 8, 8, Face::Top, kHair);
    fillFace(hat, 8, 8, 8, Face::Back, kHair);
    for (Face face : {Face::Front, Face::Left, Face::Right}) {
        // 覆盖层上的刘海比里层窄一行（里层三行、这层两行），不然两层一模一样、
        // 看不出这一层在起作用。
        painter.fillRect(faceRect(hat, 8, 8, 8, face).adjusted(0, 0, 0, -6), kHair);
    }

    return skin;
}

QImage headAvatar(const QImage &skin, int size) {
    if (skin.isNull() || size <= 0 || skin.width() < 64 || skin.height() < 32) {
        return {};
    }

    QImage head(8, 8, QImage::Format_ARGB32_Premultiplied);
    head.fill(Qt::transparent);
    {
        QPainter painter(&head);
        painter.drawImage(QPoint(0, 0), skin, QRect(8, 8, 8, 8));
        // 帽子层。老格式（64×32）的皮肤也有这一块 —— 那半边正是为它留的。
        painter.drawImage(QPoint(0, 0), skin, QRect(40, 8, 8, 8));
    }
    // 最近邻放大：8×8 的像素画，插值只会糊成一团。
    return head.scaled(size, size, Qt::IgnoreAspectRatio, Qt::FastTransformation);
}

QImage completeLegacySkin(const QImage &skin) {
    // 老格式（64×32）里**根本没有左臂和左腿那两块贴图** —— 摊开数一下就知道放不下：
    // 每块四肢要占 16 列 × 16 行，而 32 行高的图扣掉头和帽子只剩 16 行，只够右臂右腿
    // 挤在 64 列里（左臂左腿那两块是 1.8 把图扩成 64×64 时才加的）。那个年代游戏是
    // 拿右边那份**镜像**过去画的。
    //
    // 不补这一步的表现是：左臂左腿会去取贴图外面 —— copy() 越界返回空图，于是整块
    // 不画，小人看上去"少了半拉身子"（而且右边那块的**内侧面**会顶上来，因为挡住它的
    // 左腿不在了）。这不是边角料：老账号里 64×32 的皮肤很常见。

    if (skin.isNull() || skin.height() >= 64) {
        return skin;  // 新格式什么都不缺
    }

    QImage out(64, 64, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter painter(&out);
    // 上半张原样搬过来：头、帽子、身体、右臂、右腿全在这 32 行里。
    painter.drawImage(QPoint(0, 0), skin);

    // 把右边那半镜像到左边。配对是**镜像**下的配对：左边那块的"右面"对应右边那块的
    // "左面"，而每一块贴图都要左右翻一下（照 faceGeometry 的基向量推的：x 取反之后
    // 每个面的"贴图往右"都正好反向）。
    const auto mirrorFace = [](Face face) {
        if (face == Face::Right) {
            return Face::Left;
        }
        if (face == Face::Left) {
            return Face::Right;
        }
        return face;  // 前后上下四面镜像之后还是自己
    };
    const struct {
        QPoint right;
        QPoint left;
    } limbs[] = {
        {QPoint(0, 16), QPoint(16, 48)},   // 右腿 → 左腿
        {QPoint(40, 16), QPoint(32, 48)},  // 右臂 → 左臂
    };
    for (const auto &limb : limbs) {
        for (Face face : allFaces()) {
            const QRect src = faceRect(limb.right, 4, 12, 4, mirrorFace(face));
            const QRect dst = faceRect(limb.left, 4, 12, 4, face);
            const QImage piece = skin.copy(src).flipped(Qt::Horizontal);
            if (!piece.isNull()) {
                painter.drawImage(dst.topLeft(), piece);
            }
        }
    }
    return out;
}

bool hasSecondLayer(const QImage &skin) { return skin.height() >= 64; }

bool isSlimVariant(const QString &model) {
    return model.compare(QLatin1String("slim"), Qt::CaseInsensitive) == 0;
}

double defaultYaw() { return 25.0; }
double defaultPitch() { return 12.0; }
double maxPitch() { return kMaxPitchDegrees; }
double minZoom() { return 0.4; }
double maxZoom() { return 4.0; }

}  // namespace SkinModel
