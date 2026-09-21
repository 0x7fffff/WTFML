/**
 * @file SkinModel.cpp
 * @brief 实现玩家皮肤模型的几何、投影与光栅化（见头文件里"为什么自己算"那段）。
 * @author 0x7fffff
 * @date 2026-09-21
 */

#include "SkinModel.hpp"

#include <QPointF>
#include <QtMath>

#include <algorithm>
#include <cmath>

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

// ---------- 姿势：绕关节旋转 ----------

/// 绕 +x 转 degrees 度（右手系）。
///
/// NOTE: **正号 = 关节下面那一段往 -z（身后）摆**：取关节正下方一点 (0,-1,0)，
///       转一个小角度得到 z' = sin(θ)·(-1) < 0。这条"正负号长什么样"是整个走路姿势
///       里唯一需要推一次的地方 —— 腿、手臂、披风的仰角全都按它读，谁都别再自己定一套。
QVector3D rotateX(const QVector3D &v, double degrees) {
    const double r = qDegreesToRadians(degrees);
    const double c = qCos(r);
    const double s = qSin(r);
    return QVector3D(v.x(), static_cast<float>(c * v.y() - s * v.z()),
                     static_cast<float>(s * v.y() + c * v.z()));
}

QVector3D rotateY(const QVector3D &v, double degrees) {
    const double r = qDegreesToRadians(degrees);
    const double c = qCos(r);
    const double s = qSin(r);
    return QVector3D(static_cast<float>(c * v.x() + s * v.z()), v.y(),
                     static_cast<float>(-s * v.x() + c * v.z()));
}

QVector3D rotateZ(const QVector3D &v, double degrees) {
    const double r = qDegreesToRadians(degrees);
    const double c = qCos(r);
    const double s = qSin(r);
    return QVector3D(static_cast<float>(c * v.x() - s * v.y()),
                     static_cast<float>(s * v.x() + c * v.y()), v.z());
}

/// 把一个**方向**按部件的姿势转过去。顺序是**先 Y、再 X、最后 Z**。
///
/// 这个顺序是有意的，不能换：披风那个 180° 的 Y 是"这块板朝哪边"的一部分（见 capeCuboid），
/// 得先把它转过去，之后绕 X 的仰角才是"下摆往身后翘"；反过来先翘再翻，翘的方向就跟着翻过去，
/// 披风会在走路时**朝身体贴**而不是往外抬。
QVector3D posedDirection(const Cuboid &box, const QVector3D &v) {
    if (box.rotation.isNull()) {
        return v;  // 立正：连浮点误差都不留（下面的布局断言靠这个当基准）
    }
    return rotateZ(rotateX(rotateY(v, box.rotation.y()), box.rotation.x()), box.rotation.z());
}

/// 把一个**点**按部件的姿势转过去：先把 pivot 挪到原点、转完再挪回去。
/// 少了"挪回去"这一步，一个 32° 的抬腿会把整条腿平移到别处去。
QVector3D posedPoint(const Cuboid &box, const QVector3D &p) {
    return box.pivot + posedDirection(box, p - box.pivot);
}

// ---------- 走路的幅度 ----------
//
// 这几个数是照游戏里 ModelBiped 的系数折算的，不是随手挑的：
//   腿 = cos(步频) * 1.4 * 步幅，而正常走路时步幅 ≈ 0.4  ->  1.4 * 0.4 ≈ 0.56 rad ≈ 32°
//   臂 = cos(步频) * 1.0 * 步幅 * 2 * 0.5  ->  跟腿同源再乘 0.5，于是 ≈ 23°
// 手臂摆得比腿小是**对的**（游戏里就是这样），照 1:1 摆反而像在走正步。
constexpr double kLegSwingDegrees = 32.0;
constexpr double kArmSwingDegrees = 23.0;

/// 披风的仰角（度）：**固定**这么翘，不跟着步频晃。
///
/// NOTE: 它曾经是"基础 6° + 走路时多翘 5°"（每迈一步颠一下）。改成固定值是按用户要求
///       定下来的观感：披风飘起来是为了**别挡住身体**（翘起来之后腿和背都看得见），
///       而跟着步频一翘一落会让人一直盯着它看、反而看不清皮肤。所以它现在是**常量** ——
///       `walkingCape()` 连相位都不收（签名里留着相位的话，读的人会以为它跟步频有关）。
constexpr double kCapeLiftDegrees = 24.0;

/// 画一个面要用的东西：屏幕上的四边形、贴图上的矩形、每个角离眼睛多远。
struct DrawFace {
    /// 四个角在**画布**上的位置（顺序和 geometry.corners 一致，就是贴图矩形的
    /// (0,0)(w,0)(w,h)(0,h)）。已经减掉画布原点、乘过设备像素比。
    QPointF screen[4];
    /// 每个角离眼睛多远（**越大越远**）。
    double depth[4] = {0, 0, 0, 0};
    /// 贴图上占的矩形。
    QRect uv;
    /// 贴图本体（预乘 ARGB，采样直接按这个格式解释）。
    const QImage *texture = nullptr;
    /// 面中心的深度。只有"半透明那一遍"排序用得上（见 rasterizeFace 的说明）。
    double meanDepth = 0.0;
    /// 这块面上有没有**半透明**像素（alpha 既不是 0 也不是 255）。有的话它不能一次画完，
    /// 见 render() 里两遍的说明。
    bool translucent = false;
};

/// 这块面上有没有半透明像素。查一次是几十个 texel，一帧几十块面，忽略不计。
///
/// NOTE: 扫的范围要跟贴图本身取**交集** —— 调用方给的图不一定够大（披风是从网上
///       下来的，一张比 22×17 还小的图照样是合法 PNG）。以前这一步是
///       `drawImage(rect, image, srcRect)`，Qt 会自己把源矩形裁到图里；现在取样是
///       自己做的，不裁就是读越界。
bool hasTranslucentPixels(const QImage &texture, const QRect &uv) {
    const QRect area = uv.intersected(QRect(0, 0, texture.width(), texture.height()));
    for (int y = area.top(); y <= area.bottom(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(texture.constScanLine(y));
        for (int x = area.left(); x <= area.right(); ++x) {
            const int alpha = qAlpha(line[x]);
            if (alpha != 0 && alpha != 255) {
                return true;
            }
        }
    }
    return false;
}

/// 把一个部件的某一面投到画布上，塞进 faces 里。
/// @param origin 画布左上角在画笔坐标系里的位置（投影出来的点是画笔坐标，要减掉它）。
/// @param scale 设备像素比：画布是按**设备像素**开的（高 DPI 屏上才不糊）。
void collectFace(QVector<DrawFace> &faces, const Cuboid &box, Face face, bool overlay,
                 const QImage &texture, const Camera &cam, const QPointF &origin, double scale) {
    const FaceGeometry geometry = faceGeometry(box, face, overlay);
    if (!isFaceVisible(cam, geometry)) {
        return;  // 背面：看不见，连投都不用投
    }

    DrawFace draw;
    draw.uv = geometry.uv;
    draw.texture = &texture;
    double depthSum = 0.0;
    for (int i = 0; i < geometry.corners.size(); ++i) {
        double depth = 0.0;
        const QPointF point = project(cam, geometry.corners[i], &depth);
        if (depth <= 0.0) {
            // 角跑到相机背后了（只有把模型缩得极大才可能）：投影出来的点没有意义，
            // 整块丢掉。以前是靠"四边形退化成一条线、quadToQuad 返回 false"兜住的。
            return;
        }
        draw.screen[i] = (point - origin) * scale;
        draw.depth[i] = depth;
        depthSum += depth;
    }
    draw.meanDepth = depthSum / static_cast<double>(geometry.corners.size());
    draw.translucent = hasTranslucentPixels(texture, draw.uv);
    faces << draw;
}

/// 画布 + 深度缓冲。深度缓冲里只记**不透明**像素写进去的深度 —— 半透明的像素不写深度，
/// 不然它背后那层不透明的面就被挡住了（透明的地方该透出后面的东西）。
struct Canvas {
    QImage *image = nullptr;
    QVector<float> depth;
    int width = 0;
    int height = 0;
};

/// 把一块面**逐像素**画进画布：像素中心落在面里就取它的深度和贴图上的 texel，
/// 比深度缓冲近才写。@param blendOnly true = 半透明那一遍（见 render()）。
///
/// 采样是**最近邻**：贴图坐标取完 floor 就是那个 texel（和以前 drawImage 关掉平滑
/// 一个意思，皮肤是像素画，插值只会糊）。透视校正是必须的 —— 贴图坐标和深度都要先
/// 插 1/z 再取倒数，直接线性插会在近处明显偏。
void rasterizeFace(const DrawFace &face, Canvas &canvas, bool blendOnly) {
    const int tri[2][3] = {{0, 1, 2}, {0, 2, 3}};
    const double rectW = face.uv.width();
    const double rectH = face.uv.height();
    const double cornerU[4] = {0.0, rectW, rectW, 0.0};
    const double cornerV[4] = {0.0, 0.0, rectH, rectH};

    for (const auto &t : tri) {
        double x[3], y[3], iz[3], u[3], v[3];
        for (int i = 0; i < 3; ++i) {
            x[i] = face.screen[t[i]].x();
            y[i] = face.screen[t[i]].y();
            iz[i] = 1.0 / face.depth[t[i]];
            u[i] = cornerU[t[i]] * iz[i];
            v[i] = cornerV[t[i]] * iz[i];
        }
        const double area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
        if (qAbs(area) < 1e-9) {
            continue;  // 这个角度下这条三角形没有面积（正侧对着镜头）
        }
        const double invArea = 1.0 / area;
        const int x0 = qMax(0, static_cast<int>(std::floor(qMin(x[0], qMin(x[1], x[2])))));
        const int x1 = qMin(canvas.width - 1, static_cast<int>(std::ceil(qMax(x[0], qMax(x[1], x[2])))));
        const int y0 = qMax(0, static_cast<int>(std::floor(qMin(y[0], qMin(y[1], y[2])))));
        const int y1 =
            qMin(canvas.height - 1, static_cast<int>(std::ceil(qMax(y[0], qMax(y[1], y[2])))));

        for (int py = y0; py <= y1; ++py) {
            const double sy = py + 0.5;
            QRgb *line = reinterpret_cast<QRgb *>(canvas.image->scanLine(py));
            float *depthLine = canvas.depth.data() + py * canvas.width;
            for (int px = x0; px <= x1; ++px) {
                const double sx = px + 0.5;
                // 屏幕空间的重心坐标（三个分量和为 1、都非负 = 落在三角形里）。
                const double l0 = ((x[1] - sx) * (y[2] - sy) - (x[2] - sx) * (y[1] - sy)) * invArea;
                const double l1 = ((x[2] - sx) * (y[0] - sy) - (x[0] - sx) * (y[2] - sy)) * invArea;
                const double l2 = 1.0 - l0 - l1;
                if (l0 < 0.0 || l1 < 0.0 || l2 < 0.0) {
                    continue;
                }
                const double invZ = l0 * iz[0] + l1 * iz[1] + l2 * iz[2];
                if (invZ <= 0.0) {
                    continue;
                }
                const double depth = 1.0 / invZ;
                if (blendOnly) {
                    // 第二遍：只测试，不写。被前面那层不透明的面挡住的半透明像素不画。
                    if (depth > depthLine[px] + 1e-4) {
                        continue;
                    }
                } else if (depth >= depthLine[px]) {
                    continue;  // 已经有更近的面了
                }

                // 透视校正的贴图坐标：u 那边已经乘过 1/z 了，除以 1/z 的和就还原了。
                const double uu = (l0 * u[0] + l1 * u[1] + l2 * u[2]) / invZ;
                const double vv = (l0 * v[0] + l1 * v[1] + l2 * v[2]) / invZ;
                int tu = face.uv.x() + static_cast<int>(std::floor(uu));
                int tv = face.uv.y() + static_cast<int>(std::floor(vv));
                // 夹两下：先夹进 uv 矩形（浮点误差可能让边缘那一列/行落到矩形外面，
                // 落到隔壁那块贴图上就串色了），再夹进贴图本身（图可能比 uv 矩形小，
                // 见 hasTranslucentPixels 那段说明 —— 这一下是防读越界的，不是防串色的）。
                tu = qBound(0, qBound(face.uv.x(), tu, face.uv.right()), face.texture->width() - 1);
                tv =
                    qBound(0, qBound(face.uv.y(), tv, face.uv.bottom()), face.texture->height() - 1);
                const QRgb texel =
                    reinterpret_cast<const QRgb *>(face.texture->constScanLine(tv))[tu];
                const int alpha = qAlpha(texel);
                if (alpha == 0) {
                    continue;  // 透明：不画、也不写深度（后面的东西照样看得见）
                }
                if (blendOnly) {
                    if (alpha == 255) {
                        continue;  // 不透明的那一遍画过了
                    }
                    const QRgb under = line[px];
                    const int keep = 255 - alpha;
                    line[px] = qRgba(qRed(texel) + qRed(under) * keep / 255,
                                     qGreen(texel) + qGreen(under) * keep / 255,
                                     qBlue(texel) + qBlue(under) * keep / 255,
                                     alpha + qAlpha(under) * keep / 255);
                } else {
                    if (alpha != 255) {
                        continue;  // 半透明的留到第二遍（那一遍要按远近从后往前混）
                    }
                    line[px] = texel;
                    depthLine[px] = static_cast<float>(depth);
                }
            }
        }
    }
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

    // 四个关节（见 Cuboid::pivot）：手臂在肩膀那条线上（y=24）、腿在胯那条线上（y=12），
    // x 取各自的中线、z 取中面 —— 从 lo/hi 现算而不是再抄一遍数字，纤细模型（手臂 3 宽）
    // 的关节因此自动跟着挪。头和身体不设关节：走路时它们不单独转，整块身子跟着走就是了。
    const auto setJoint = [](Cuboid &box, double axisY) {
        box.pivot = QVector3D((box.lo.x() + box.hi.x()) / 2.0f, static_cast<float>(axisY),
                              (box.lo.z() + box.hi.z()) / 2.0f);
    };
    setJoint(rightArm, 24.0);
    setJoint(leftArm, 24.0);
    setJoint(rightLeg, 12.0);
    setJoint(leftLeg, 12.0);

    // NOTE: 这个顺序是**对外承诺**的（walkingParts 按下标往对应部件上填角度，
    //       ui_check 有一条断言把它钉住）：头、身体、右臂、左臂、右腿、左腿。
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

    // 关节定在肩膀那条横线上，z 取这块板自己的中面（(-3.2 + -2.2)/2 = -2.7）。
    cape.pivot = QVector3D(0, 24, -2.7f);
    // **整块板绕 Y 转 180°**。这一条是"披风朝外那面取贴图哪一块"的出处，踩过一次：
    //
    // 贴图公式（faceRect）本身没得挑 —— 摊平之后就摆在那儿，前/后两块分别是 (1,1,10,16)
    // 和 (12,1,10,16)，两条独立旁证都对得上。错的是**这两块分别该贴到哪一面上**：
    // 直接照"背面那一面取背面那块图"贴，从背后看见的就是披风图的**内侧**那块，
    // 于是整件披风是反的（用户报的"披风放反了"）。游戏里这块板就是转过来挂的
    // （原版那一层有一记绕 Y 的转），转了之后贴图左边那块才是朝外给人看的那一面。
    //
    // 转 180° 之后**几何一模一样**（pivot 在这块板的中面上，转完这块板落回原处），
    // 变的只是"哪块贴图落在哪一面" —— 所以 W2 里那些关于披风几何的断言不受影响，
    // 而"朝外那面取 (1,1,10,16)"这条可以单独钉住（见 ui_check W2）。
    cape.rotation = QVector3D(0, 180, 0);

    // 顺带一提：那个固定的仰角（walkingCape）是往 rotation.x() 上填的，也就是"绕同一根
    // 关节、在转过 180° 之后的那套轴上往后翘"。顺序见 posedDirection。
    return cape;
}

QVector<Cuboid> walkingParts(bool slim, double walkPhase) {
    QVector<Cuboid> parts = playerCuboids(slim);
    // 迈步的"张合"：+1 = 迈到最大、0 = 腿并拢。用 cos 而不是 sin，是为了让**相位 0
    // 正好是迈得最大的那一帧**（截一张图就是一腿前一腿后、一眼看出在走），
    // 而"腿正好竖直"落在 restWalkPhase() 上。
    const double stride = qCos(2.0 * M_PI * walkPhase);

    // playerCuboids() 的固定顺序：0 头、1 身体、2 右臂、3 左臂、4 右腿、5 左腿。
    // 按**下标**填而不是拿名字去认：这个顺序在 playerCuboids 里是写明的，
    // ui_check 有一条断言把它钉住，谁改了顺序会当场红。
    const int kRightArm = 2;
    const int kLeftArm = 3;
    const int kRightLeg = 4;
    const int kLeftLeg = 5;

    // 左右**反相**（一条往前一条往后），手臂跟**同侧的腿**也反相。这两条就是"像在
    // 走路"的全部：同相的话四条肢体一起前后荡，看起来像在跳。
    //
    // NOTE: 正负号照游戏里那一套原样照搬（右腿取 +cos、右臂取 -cos），没有为了"看着顺眼"
    //       再翻一次。照我们的约定（旋转正号 = 关节下面那段往身后摆，见 rotateX）读出来就是：
    //       相位 0 那一帧右腿在后、左腿在前、右臂在前、左臂在后 —— 正是一个正常的走路交替，
    //       而且**同侧的手脚永远一前一后**（右腿在后时右臂在前），这条错了会看着像顺拐。
    parts[kRightLeg].rotation.setX(kLegSwingDegrees * stride);
    parts[kLeftLeg].rotation.setX(-kLegSwingDegrees * stride);
    parts[kRightArm].rotation.setX(-kArmSwingDegrees * stride);
    parts[kLeftArm].rotation.setX(kArmSwingDegrees * stride);
    return parts;
}

Cuboid walkingCape() {
    Cuboid cape = capeCuboid();
    // 仰角**固定**（见 kCapeLiftDegrees）：绕肩膀那条线往后翘，把背和腿让出来。
    // 它绕的是转过 180° 之后那套轴上的 X —— 所以是"下摆往身后去"而不是"贴向身体"，
    // 顺序那件事见 posedDirection。
    cape.rotation.setX(kCapeLiftDegrees);
    return cape;
}

double restWalkPhase() {
    // cos(2π·0.25) = 0 —— 腿正好竖直。用"相位"表示而不是另外给一套立正的数，
    // 是为了让立正和迈步是同一条曲线上的两个点：改幅度的时候两边不会脱节。
    return 0.25;
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
    // 贴图矩形**不跟着姿势转**：一块布怎么转，它上面那块图案还是从贴图同一个地方取
    // （转了之后"哪一块落在哪一面上"自然就变了 —— 披风那 180° 正是靠这个起作用的）。
    geometry.uv = faceRect(overlay ? box.overlayBase : box.base, w, h, d, face);

    // 面在模型上的两个跨度。膨胀之后这两个跨度也跟着变大（覆盖层比里层大一圈），
    // 而贴图矩形**不变** —— 也就是把同一块贴图摊在稍大一点的面上。
    const bool alongX = (face == Face::Front || face == Face::Back || face == Face::Top ||
                         face == Face::Bottom);
    const double spanRight = alongX ? size.x() : size.z();
    const double spanUp = (face == Face::Top || face == Face::Bottom) ? size.z() : size.y();

    // 姿势：中心绕关节转，法线和两个基向量只转不挪（见头文件里那一段）。
    // 四个量走同一个正交变换，所以 right × up == normal 转完还成立 —— 于是
    // "这块面没写反"那条判据对摆过姿势的部件同样管用。
    geometry.normal = posedDirection(shaped, basis.normal);
    const QVector3D center = posedPoint(shaped, faceCenter(shaped.lo, shaped.hi, basis.normal));
    const QVector3D halfRight = posedDirection(shaped, basis.right) * static_cast<float>(spanRight / 2.0);
    const QVector3D halfUp = posedDirection(shaped, basis.up) * static_cast<float>(spanUp / 2.0);

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
                       bool withCape, double walkPhase) {
    const Camera cam = makeCamera(target, yaw, pitch, zoom);
    QVector<Cuboid> boxes = walkingParts(slim, walkPhase);
    if (withCape) {
        boxes << walkingCape();
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
    // 统一转成**预乘 ARGB**：光栅化那一段是自己按字节解释像素的（见 rasterizeFace），
    // 格式不统一就没法解释。转一次，一帧几十块面共用。
    const QImage skin =
        completeLegacySkin(scene.skin).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QImage cape = scene.cape.isNull()
                            ? QImage()
                            : scene.cape.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    // 画布按**设备像素**开：高 DPI 屏上（缩放 125%、150%）一个逻辑像素是两三个物理像素，
    // 按逻辑尺寸开再被放大就会糊 —— 以前是直接把面画到屏幕上，没有这一步。
    const double scale =
        painter.device() != nullptr ? painter.device()->devicePixelRatioF() : 1.0;
    const int canvasW = static_cast<int>(std::ceil(target.width() * scale));
    const int canvasH = static_cast<int>(std::ceil(target.height() * scale));
    if (canvasW <= 0 || canvasH <= 0) {
        return;
    }
    const QPointF origin = target.topLeft();

    // 老格式（64×32）没有身体和四肢的第二层（见 Cuboid::overlayInLegacy）。
    const bool secondLayer = hasSecondLayer(scene.skin);

    QVector<DrawFace> faces;
    // 走动的那一帧（scene.walkPhase 默认立正）。覆盖层跟着部件一起摆 ——
    // inflatedBox() 是把整个 Cuboid 拷一份再胀大，pivot/rotation 因此原样带过去。
    for (const Cuboid &box : walkingParts(scene.slim, scene.walkPhase)) {
        for (Face face : allFaces()) {
            collectFace(faces, box, face, false, skin, cam, origin, scale);
        }
        // 覆盖层：老格式的皮肤只有脑袋有第二层（见 Cuboid::overlayInLegacy）。
        if (box.hasOverlay && (box.overlayInLegacy || secondLayer)) {
            for (Face face : allFaces()) {
                collectFace(faces, box, face, true, skin, cam, origin, scale);
            }
        }
    }
    if (!cape.isNull()) {
        const Cuboid capedBox = walkingCape();
        for (Face face : allFaces()) {
            collectFace(faces, capedBox, face, false, cape, cam, origin, scale);
        }
    }

    Canvas canvas;
    QImage image(canvasW, canvasH, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    canvas.image = &image;
    canvas.width = canvasW;
    canvas.height = canvasH;
    canvas.depth.fill(1e30f, canvasW * canvasH);

    // 不透明的一遍：谁近谁写进去，**跟画的先后无关**（这就是它比画家算法强的地方：
    // 不再需要"按远近排序"这个前提，也就没有"排错序"这回事）。
    for (const DrawFace &face : faces) {
        if (face.translucent) {
            continue;  // 带半透明的面走下面那两遍，见 rasterizeFace 的说明
        }
        rasterizeFace(face, canvas, false);
    }
    // 带半透明像素的面：先按**不透明**那一遍的规矩画一遍（它身上不透明的那些 texel
    // 该挡谁挡谁），再按远近从后往前画一遍半透明的部分（混上去、不写深度）。
    QVector<const DrawFace *> layered;
    for (const DrawFace &face : faces) {
        if (face.translucent) {
            layered << &face;
        }
    }
    for (const DrawFace *face : layered) {
        rasterizeFace(*face, canvas, false);
    }
    std::sort(layered.begin(), layered.end(),
              [](const DrawFace *a, const DrawFace *b) { return a->meanDepth > b->meanDepth; });
    for (const DrawFace *face : layered) {
        rasterizeFace(*face, canvas, true);
    }

    image.setDevicePixelRatio(scale);
    painter.drawImage(origin, image);
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
