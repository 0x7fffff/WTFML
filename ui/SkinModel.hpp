/**
 * @file SkinModel.hpp
 * @brief 玩家皮肤模型的几何、投影与光栅化：把一张 64×64 的皮肤贴图（+ 可选的披风）
 *        画成一个能任意角度看的立体小人。
 * @author 0x7fffff
 * @date 2026-09-21
 * @note 本文件是**纯函数 + 值**：不 include 任何 src/ 下的头，也不碰任何窗口/网络。
 *       之所以要这样切开，是因为"这块面该取贴图上哪一块矩形""这个角度下哪几个面看得见"
 *       这些判断**没有一行能靠肉眼看截图验准** —— 拆成纯函数之后验证工程可以把它们
 *       逐条钉住（见 SkinViewer.hpp 顶部那段说明）。
 *
 * 【为什么是自己算，不是 QOpenGLWidget / Qt Quick 3D】
 *
 * 皮肤就是一张现成的 2D 贴图和六个长方体，用 QPainter 把每个面按透视投影贴上去就够了。
 * 换 OpenGL 那条路的代价是这个仓库付不起的：
 *   - 验证工程的截图走的是 QWidget::grab()，它对普通控件是光栅合成，对 QOpenGLWidget
 *     返回的是**空白**（GL 的内容在帧缓冲里，不在控件的 backing store 里）；
 *   - offscreen 平台本身不提供原生 GL 表面，还得靠 QT_OPENGL=software 才起得来。
 * 软件光栅化没有这些门槛，而且**数学部分成了可单测的纯函数** —— 这正是本文件存在的理由。
 */

#pragma once

#include <QImage>
#include <QPainter>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QVector3D>

/// 玩家皮肤的几何与绘制。单位一律是**皮肤像素**（1 像素 = 游戏里的 1/16 格）。
namespace SkinModel {

/// 长方体的六个面。名字按**玩家自己的左右**叫（玩家面朝 +z、头顶是 +y 时，
/// 他的右手边是 -x），皮肤贴图那一圈展平的顺序也是照这个来得，见 faceRect()。
enum class Face {
    Right,  ///< -x，玩家自己的右侧
    Front,  ///< +z，玩家面朝的方向（脸在这一面）
    Left,   ///< +x
    Back,   ///< -z
    Top,    ///< +y
    Bottom, ///< -y
};

/// 六个面各来一遍（渲染和断言都要遍历）。顺序固定，不依赖枚举值本身。
const QVector<Face> &allFaces();

/// 把 Face 翻译成中文，只给断言失败时的提示用。
QString faceName(Face face);

/**
 * @brief 一个长方体在皮肤贴图上占的那一大块矩形的左上角（"基准点"）。
 *
 * 一块长方体在贴图上是**摊平**的一整块：中间横着一条是四个侧面（按"右→前→左→后"
 * 的顺序绕一圈），它上面并排摆着"顶面"和"底面"两个方块。基准点指的就是这一整块的
 * 左上角，比如头的基准点是 (0,0)、身体是 (16,16)、右腿是 (0,16)。
 */
struct Cuboid {
    /// 只给断言和调试看（"哪个部件错了"比"哪个下标错了"好找得多）。
    QString name;
    /// 模型空间里的最小/最大角。y 向上，脚底在 y=0，所以小人一共 32 单位高。
    QVector3D lo;
    QVector3D hi;
    /// 贴图上的基准点。
    QPoint base;
    /// 覆盖层（帽子/外套/袖子/裤腿）在贴图上的基准点；`hasOverlay == false` 时无意义。
    QPoint overlayBase;
    /// 有没有第二层。
    bool hasOverlay = true;
    /// 这第二层在**老格式（64×32）的皮肤里也存在**吗。
    ///
    /// 只有脑袋是 —— 帽子层那个年代就有了（这正是老皮肤为什么是 64 宽而不是 32：
    /// 多出来的右半边就为了放帽子）。身体和四肢的第二层是 1.8 才有的，老皮肤里那片
    /// 区域根本不存在，照着画会去取一片"贴图外面"的东西。
    bool overlayInLegacy = false;
};

/**
 * @brief 某个面在贴图上占的矩形。
 * @param base 长方体的基准点（见 Cuboid::base）。
 * @param w 长方体的宽（x 方向，皮肤像素）。
 * @param h 高（y 方向）。
 * @param d 厚（z 方向）。
 * @param face 哪个面。
 *
 * 这就是整个皮肤贴图布局的**唯一出处**。摊平的规则是：中间横排四个侧面（右→前→左→后），
 * 上下各摆一块"顶"和"底"，都按 d（厚度）的偏移量对齐。
 *
 * NOTE: 这条公式有两处**互相独立**的旁证，两处都对得上才敢用：
 *       - 头（基准 (0,0)、8×8×8）算出来前脸是 (8,8,8,8)、帽子层前脸是 (40,8,8,8)；
 *       - 披风（基准 (0,0)、10×16×1）算出来正面是 (1,1,10,16)、背面是 (12,1,10,16)，
 *         和公认的披风贴图布局一模一样（披风图跟皮肤图是两套不同的贴图，能一起对上
 *         说明这条公式不是照着皮肤那一张凑出来的）。
 */
QRect faceRect(const QPoint &base, int w, int h, int d, Face face);

/// 玩家模型的全部部件。同一份表既用来渲染也用来算断言，不各写一份。
/// @param slim "纤细"（Alex）模型：手臂 3 单位宽而不是 4。
QVector<Cuboid> playerCuboids(bool slim);

/// 披风。贴在背后（**-z 那一侧** —— 玩家面朝 +z，所以背后是负的），从肩膀（y=24）
/// 垂到 y=8。贴图是另一张 64×32 的图，基准点也是 (0,0)。
Cuboid capeCuboid();

/// 某个面摊平之后，四个角在模型空间里的位置（顺序：左上、右上、右下、左下 ——
/// **从外面看**的上下左右）、外法线、以及它在贴图上占的矩形。
struct FaceGeometry {
    /// 4 个角，顺序对应 uv 的 (0,0) (w,0) (w,h) (0,h)。
    QVector<QVector3D> corners;
    QVector3D normal;
    QRect uv;
};

/**
 * @brief 算出某个面的四个角、法线和贴图矩形。
 * @param box 部件（用 lo/hi/base）。
 * @param face 哪个面。
 * @param overlay 算的是不是覆盖层（第二层）。覆盖层整体向外膨胀 0.5 单位、贴图取
 *        `overlayBase` 那一块 —— 只膨胀**几何**、不放大取图范围，也就是把同一块贴图
 *        摊在稍大一点的面上，这正是游戏里的做法（覆盖层比里层大一圈）。
 *
 * 四个角的顺序必须和 uv 矩形的四个角一一对应，否则贴图会**镜像**（脸左右反了、
 * 衣服上的字反了）。所以姿势不是逐面硬编码的，而是每个面配一对 (right, up) 基向量：
 * 沿着贴图 +u 走就是模型空间的 right 方向，沿着贴图 +v（贴图上往下）走就是 -up。
 * 每个面的这对基向量都满足 `right × up == normal`（右手系里"从外面看"的朝向），
 * 这条恒等式就是它们没写反的判据，六个面一条都不能例外。
 */
FaceGeometry faceGeometry(const Cuboid &box, Face face, bool overlay = false);

// ---------- 相机 ----------

/// 相机。模型空间里的一个点 -> 屏幕上的一个点。
struct Camera {
    QVector3D eye;              ///< 眼睛的位置（模型空间）
    QVector3D right;            ///< 屏幕向右对应的模型方向
    QVector3D up;               ///< 屏幕向上对应的模型方向
    QVector3D forward;          ///< 视线方向（从眼睛指向模型中心）
    double focal = 1.0;         ///< 焦距（已经含了自适应缩放和用户缩放）
    QPointF center;             ///< 画面中心（目标矩形的中心）
};

/**
 * @brief 造一个绕着模型转的相机。
 * @param target 画面矩形（控件里留给模型的那块地方）。
 * @param yaw 水平角（度）。0 = 正对着脸，正数往玩家的左侧转。
 * @param pitch 俯仰角（度）。正数 = 从上方往下看。**会被夹在 ±89°** ——
 *        正好 ±90° 时"屏幕向右"那个基向量会退化成零向量（视线和世界正上方平行）。
 * @param zoom 缩放倍数，乘在焦距上。
 *
 * 相机距离是固定的，画面大小靠**焦距**调 —— 焦距按 target 算成"让模型正好占满高度的大
 * 约 85%"，所以模型在什么尺寸的控件里看起来都一样大，不用每个尺寸各调一套数。
 */
Camera makeCamera(const QRectF &target, double yaw, double pitch, double zoom);

/// 把一个模型空间的点投到屏幕上。@param depthOut 可选，接这个点到眼睛的距离
/// （**越大越远**）；点在相机背后时它的值 ≤ 0，调用方得自己挡住。
QPointF project(const Camera &cam, const QVector3D &modelPoint, double *depthOut = nullptr);

/// 这个面从相机这边看得见吗（背面剔除）。
///
/// 用"法线是否朝着眼睛"判，而不是用投影之后多边形的绕向判：绕向要先把四个角投出来、
/// 还得挑一个正负号约定，而这个判据只用模型空间的量，写错了当场就能看出来。
bool isFaceVisible(const Camera &cam, const FaceGeometry &face);

/// 模型（含披风、含覆盖层）在屏幕上的包围盒。给断言用（"投影高度占控件的多少"）。
QRectF projectedBounds(const QRectF &target, double yaw, double pitch, double zoom, bool slim,
                       bool withCape);

// ---------- 场景与绘制 ----------

/// 画什么、从哪个角度看。
struct Scene {
    /// 皮肤贴图。空的话什么都不画（调用方负责给默认外观）。
    QImage skin;
    /// 披风贴图（64×32）。可空 —— 没披风就不画那一条。
    QImage cape;
    /// 纤细模型（手臂 3 宽）。
    bool slim = false;
    double yaw = 0.0;
    double pitch = 0.0;
    double zoom = 1.0;
};

/**
 * @brief 把场景画到 target 矩形里。**不画背景**（背景是控件自己的事：颜色要从
 *        palette 现推，那是 UI 层的知识，不该混进几何代码里）。
 *
 * 画法是画家算法：把每个面投到屏幕上，剔掉背面的，按离眼睛的远近从远到近贴上去。
 * 对手模型这种"几个互不相交的凸多面体"来说这个做法是**精确的**：
 *   - 一个凸多面体剔掉背面之后剩下的三个面在屏幕上互不重叠，所以它们之间的先后无所谓；
 *   - 跨部件（手臂盖在身上、披风挂在背后）按面中心比远近，顺序就是对的。
 * 覆盖层和基础层**混在一起排序**，不分开画两遍 —— 分开画的话，覆盖层整体罩在基础层
 * 上面会把"袖子盖住手、头发盖住后脑"这种本该各管一段的关系画错。
 */
void render(QPainter &painter, const QRectF &target, const Scene &scene);

// ---------- 贴图工具 ----------

/// 默认外观：认得出是"经典配色"的那种小人（肤色、棕发、青上衣、蓝裤、灰鞋）。
///
/// NOTE: 是**照经典配色画的**，不是把官方那张 Steve 贴图抄进代码里 —— 把 Mojang 的
///       贴图字节打进仓库是版权问题。看着像，但不是原图。
QImage defaultSteveSkin();

/// 从皮肤贴图上抠出脑袋，做成方形头像。
/// @param skin 皮肤贴图（64×64 或老格式的 64×32）。
/// @param size 输出边长（像素）。
/// @return 抠好的图；贴图不合法（太小/为空）时返回空图。
///
/// 放大用**最近邻**（Qt::FastTransformation）：皮肤是 8×8 的像素画，插值放大只会糊成
/// 一团。同样的理由写在头像那个 QLabel 为什么不加圆角上（ui/MainWindowUI.cpp）。
QImage headAvatar(const QImage &skin, int size);

/// 贴图上那个模型用的第二层（帽子/外套那层）存不存在：老格式 64×32 的皮肤没有
/// 身体和四肢的第二层（只有脑袋的帽子层是那个年代就有的）。
bool hasSecondLayer(const QImage &skin);

/**
 * @brief 把老格式（64×32）的皮肤补齐成 64×64：**左臂和左腿那两块在图里根本没有**。
 *
 * 数一下就明白为什么没有：每块四肢要占 16 列 × 16 行，而 32 行高的图扣掉头（16 行）
 * 只剩 16 行，只够右臂右腿挤在 64 列里 —— 左臂左腿是 1.8 把图扩成 64×64 时才加的。
 * 那个年代客户端画左边那两块时是拿右边那份**镜像**过去的，这里照做。
 *
 * @param skin 皮肤贴图。`height >= 64` 时**原样返回**（什么都不缺）。
 * @return 一张 64×64 的图：上半张是原图，左臂左腿由右臂右腿镜像补上，
 *         第二层（外套/袖子/裤腿）那几块保持透明 —— 老格式本来就没有第二层。
 *
 * NOTE: 渲染之前必须先过这一道，否则左臂左腿取图越界 —— `QImage::copy()` 越界返回
 *       空图，整块就不画了，小人看上去"少了半拉身子"（而且右边那块的**内侧面**会
 *       顶上来，因为挡住它的左腿不在了）。老账号里 64×32 的皮肤很常见。
 */
QImage completeLegacySkin(const QImage &skin);

/// profile 里那个 model 字段是不是"纤细"。认不出来的都当经典（4 宽）——
/// 宁可画粗一点，也不要因为一个没见过的字符串把手臂画瘦。
bool isSlimVariant(const QString &model);

// ---------- 默认视角 ----------

/// 初始视角：偏 3/4 侧、稍微俯视一点（正对着脸看不出立体感）。
double defaultYaw();
double defaultPitch();
/// 俯仰角的绝对值上限。**正好 90° 是不行的**（见 makeCamera 里的说明），
/// 所以这个上限是硬性的、不是审美选择。控件那一层夹的是同一个值。
double maxPitch();
/// 缩放范围。
double minZoom();
double maxZoom();

}  // namespace SkinModel
