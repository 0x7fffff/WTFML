/**
 * @file SkinModel.hpp
 * @brief 玩家皮肤模型的几何、投影与光栅化：把一张 64×64 的皮肤贴图（+ 可选的披风）
 * 画成一个能任意角度看的立体小人。
 * @note 本文件是**纯函数 + 值**：不 include 任何 src/ 下的头，也不碰任何窗口/网络。
 * 之所以要这样切开，是因为"这块面该取贴图上哪一块矩形""这个角度下哪几个面看得见"
 * 这些判断**没有一行能靠肉眼看截图验准** —— 拆成纯函数之后验证工程可以把它们
 * 逐条钉住（见 SkinViewer.hpp 顶部那段说明）。
 *
 * 【为什么是自己算，不是 QOpenGLWidget / Qt Quick 3D】
 * 避免引入 OpenGL/Quick 依赖，并让几何与光栅化能在无窗口测试中验证。
 */

#pragma once

#include <QColor>
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
    /// 只有脑袋是 —— 老皮肤是 64 宽而不是 32，多出来的右半边就是放帽子的。身体和四肢
    /// 的第二层只在新格式里有，老皮肤里那片区域根本不存在，照着画会去取一片"贴图外面"的东西。
    bool overlayInLegacy = false;

    // ---- 姿势：让部件绕自己的关节转 ----
    //
    // NOTE: 这两个字段是**绕关节转**用的（肩膀、胯、披风那个翻面），不是"整块摆着好看"。
    // 关节位置是几何事实，所以放在这张表里跟 lo/hi 一起定义；摆多少度是走路姿势
    // 的事，由 walkingParts()/walkingCape() 现填。全 0（默认）= 立正，此时下面
    // 那两个变换**是恒等变换、连浮点误差都没有**（isNull() 那一步就挡掉了）。
    //
    // / 关节（旋转中心）在模型空间里的位置。要绕关节转就得**先把 pivot 挪到原点**，
    // / 转完再挪回去 —— 少了这一步，一个 32° 的抬腿会把整条腿平移到别处去。
    QVector3D pivot = QVector3D(0, 0, 0);
    /// 绕 pivot 转多少度。顺序是**先 Y、再 X、最后 Z**（见 posedDirection）。
    QVector3D rotation = QVector3D(0, 0, 0);
    /// Additional shells (armor): geometry expands without changing the texture UV size.
    float inflation = 0.0f;
    bool mirror = false;
    bool doubleSided = false; // Hollow armor can be viewed through its openings.
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
 * NOTE: 这条公式有两处**互相独立**的旁证：
 * - 头（基准 (0,0)、8×8×8）算出来前脸是 (8,8,8,8)、帽子层前脸是 (40,8,8,8)；
 * - 披风（基准 (0,0)、10×16×1）算出来正面是 (1,1,10,16)、背面是 (12,1,10,16)，
 * 和公认的披风贴图布局一模一样（披风图跟皮肤图是两套不同的贴图，能一起对上
 * 说明这条公式不是照着皮肤那一张凑出来的）。
 */
QRect faceRect(const QPoint &base, int w, int h, int d, Face face);

/// 玩家模型的全部部件。同一份表既用来渲染也用来算断言，不各写一份。
/// @param slim "纤细"（Alex）模型：手臂 3 单位宽而不是 4。
QVector<Cuboid> playerCuboids(bool slim);

/// 披风。贴在背后（**-z 那一侧** —— 玩家面朝 +z，所以背后是负的），从肩膀（y=24）
/// 垂到 y=8。贴图是另一张 64×32 的图，基准点也是 (0,0)。
///
/// NOTE: 它带着一个 **180° 的 Y 旋转**（外加关节定在肩膀那条线上），见实现里那段说明 ——
/// 那是"朝外那一面该取披风图上哪一块"这件事的出处，不是可选的装饰。
Cuboid capeCuboid();

// ---------- 走路姿势 ----------
//
// 官启里那个小人**是动着的**（站在那儿走原地步），静止的立正反而看不出这是个人。
// 姿势在这里做成**纯函数**：给一个相位，返回摆好角度的部件表。这样"腿摆多少度"
// "披风翘多少"这些数验证工程能直接钉住，不用去截一帧图数像素。
//
// NOTE: 只有**四条肢体**吃相位。披风是常量（见 walkingCape）—— 它翘起来是为了别挡住
// 身体，跟着步频一翘一落反而把眼睛引到披风上去。
//
// 相位 0..1 是一个来回。**0 = 迈到最大的那一帧**（前腿伸到最远），restWalkPhase()
// 那一帧则是腿正好竖直 —— 立正那一帧是特意留出来的：投影出来的几何和 playerCuboids()
// 一模一样，所以"姿势没写歪"这件事可以拿立正那帧当基准比。

/// 摆好姿势的部件表（在 playerCuboids() 的基础上填 rotation）。
QVector<Cuboid> walkingParts(bool slim, double walkPhase);

/// 摆好姿势的披风（在 capeCuboid() 的基础上填那个**固定**的仰角）。
///
/// NOTE: 它不收相位 —— 披风的仰角是常量，不跟着步频晃（理由见实现里 kCapeLiftDegrees
/// 上面那段）。签名里留着相位会让人以为它跟步频有关。
Cuboid walkingCape();

/// 腿正好竖直（立正）的那个相位。用相位而不是"少摆一点"，是为了让"立正"和"迈步"
/// 落在同一根轴上的两个点，随时能互相切换着比对。
double restWalkPhase();

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
 * `overlayBase` 那一块 —— 只膨胀**几何**、不放大取图范围，也就是把同一块贴图
 * 摊在稍大一点的面上，这正是游戏里的做法（覆盖层比里层大一圈）。
 *
 * 四个角的顺序必须和 uv 矩形的四个角一一对应，否则贴图会**镜像**（脸左右反了、
 * 衣服上的字反了）。所以姿势不是逐面硬编码的，而是每个面配一对 (right, up) 基向量：
 * 沿着贴图 +u 走就是模型空间的 right 方向，沿着贴图 +v（贴图上往下）走就是 -up。
 * 每个面的这对基向量都满足 `right × up == normal`（右手系里"从外面看"的朝向），
 * 这条恒等式就是它们没写反的判据，六个面一条都不能例外。
 *
 * `box.rotation` 非零时（站姿/披风那 180°），**中心、两个基向量、法线四个量各自被转过去**
 * —— 不是"把四个角当成整体转一下"就完事。两件事不一样：中心要绕 pivot 转（才叫绕关节），
 * 三个方向向量只能转、不能挪。而且四个量得用同一个正交变换转，`right × up == normal`
 * 这条恒等式才会在转完之后依然成立（旋转不改变手性）—— 上面那条"不会镜像"的判据
 * 因此对**摆过姿势的部件同样有效**，这正是它值钱的地方。
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
 * 正好 ±90° 时"屏幕向右"那个基向量会退化成零向量（视线和世界正上方平行）。
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
/// @param walkPhase 走路的相位（见 walkingParts）。**没有默认值**：包围盒随姿势变
/// （腿摆出去之后侧着看就宽一截），要哪个姿势就得写明是哪个，别让它随默认值漂。
QRectF projectedBounds(const QRectF &target, double yaw, double pitch, double zoom, bool slim,
                       bool withCape, double walkPhase);

// ---------- 场景与绘制 ----------

/// 画什么、从哪个角度看。
struct TexturedPart {
    Cuboid box;
    QImage texture;
};

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
    /// 走路的相位（0..1）。默认**立正**：姿势是视角那一层（SkinViewer）的事，几何层
    /// 给一个可复现的初值 —— 谁想要走动的那一帧就自己传（产品就是 SkinViewer 传的）。
    double walkPhase = restWalkPhase();
    QVector<TexturedPart> additionalParts;
};

/**
 * @brief 把场景画到 target 矩形里。**不画背景**（背景是控件自己的事：颜色要从
 * palette 现推，那是 UI 层的知识，不该混进几何代码里）。
 *
 * 画法是自己写一遍**逐像素深度缓冲**（z-buffer）：每个面投到屏幕上，像素中心落在面里
 * 就比深度 —— 谁近谁的像素。画家算法（按面中心的深度排序、远的先画）**看着对、其实是错的**：
 * 只有"几个互不相交的凸多面体、而且谁近谁远在整幅画上一致"时它才精确，而这个模型不满足
 * —— 摆过姿势的四肢和那件斜着的披风都是"又长又斜"的面，面中心比出来是一个中间值，跟别的面
 * **在画面上交叠的区域里**谁近谁远并不一致，症状就是"手脚穿过披风""一条腿盖在另一条腿上"。
 * 代价是每帧 0.7ms —— 60fps 的预算有 16ms，花得起。
 *
 * 两遍走：**不透明**的 texel 一遍写色写深度（顺序无所谓，看不看得见由深度决定）；
 * 带**半透明** texel 的面最后再走一遍，按远近从后往前混上去 —— 半透明的像素不写深度，
 * 不然它背后那层不透明的面就被挡住了。绝大多数皮肤只有 0 和 255 两种 alpha，第二遍是空的。
 */
void render(QPainter &painter, const QRectF &target, const Scene &scene);

// ---------- 贴图工具 ----------

/// 默认外观：**整身 `#000000`、脸上一枚 `#ffffff` 的问号**的占位小人。
///
/// 什么时候看得见它：还没登录、贴图还在路上、贴图没取到 —— 右栏那个小人任何时候都不是
/// 空白的，靠的就是这张兜底图（`applyPlayerSkin(QString(), …)` 这一支）。
///
/// NOTE: 名字里那个 "steve" 只是称呼，**别从名字去猜它画的是什么** —— 验证工程和界面上的话
/// 都这么叫它，为这个改名不值当。
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
/// 身体和四肢的第二层（只有脑袋有帽子层）。
bool hasSecondLayer(const QImage &skin);

// ---------- 什么都没有的时候：首字母方块 ----------

/**
 * @brief 首字母方块的底色，**由名字决定**（同名永远同色）。
 * @param name 账号名。空串也给一个确定的颜色（见 letterAvatar）。
 *
 * 为什么要按名字算而不是取一个固定的灰：底栏那个账号面板里一行一个人，三行都是同一个
 * 灰方块加上三个不同的字母时，"谁是哪个"要靠读字母；换个色一眼就分得开。
 * 色相走一圈，**明度/饱和度钉死** —— 那两个数一变，上面的白字就会在某一档上糊掉
 * （亮黄底上写白字是最典型的）。
 *
 * NOTE: 它只需要"稳定 + 分散"，不需要"跨版本稳定"：这个颜色不落盘（盘上没有任何东西
 * 记着它），换个颜色不算"用户的东西变了"。
 */
QColor avatarFallbackColor(const QString &name);

/**
 * @brief 连一张皮肤图都没有时用的头像：**一个首字母方块**。
 * @param name 账号名（取第一个字符，转大写；空串画一个 `?`）。
 * @param size 输出边长（像素）。
 *
 * 什么时候走到它：底栏账号面板里那一行的人**一张图都不在本地** —— 从没登录过的正版账号
 * （列表里那条只是个凭据副本）、或者刚加进来还没挑过皮肤的离线账号。这是**常态**，
 * 不是异常：面板不能因为某一项没有图就少画一块（三行的行首一高一低更难认）。
 *
 * NOTE: 字是**用字体画的**，不像 `defaultSteveSkin()` 那枚问号那样手摆像素 —— 这里要画的是
 * 任意一个字符（用户名第一个字符可能是汉字、数字、符号），手摆字形不可能覆盖。
 * 底色是 `avatarFallbackColor()`，字一律纯白：底色是算出来的，只有把字钉成白的，
 * 对比度才有个下限。
 */
QImage letterAvatar(const QString &name, int size);

/**
 * @brief 把老格式（64×32）的皮肤补齐成 64×64：**左臂和左腿那两块在图里根本没有**。
 *
 * 数一下就明白为什么没有：每块四肢要占 16 列 × 16 行，而 32 行高的图扣掉头（16 行）
 * 只剩 16 行，只够右臂右腿挤在 64 列里。客户端对老格式画左边那两块时是拿右边那份
 * **镜像**过去的，这里照做。
 *
 * @param skin 皮肤贴图。`height >= 64` 时**原样返回**（什么都不缺）。
 * @return 一张 64×64 的图：上半张是原图，左臂左腿由右臂右腿镜像补上，
 * 第二层（外套/袖子/裤腿）那几块保持透明 —— 老格式本来就没有第二层。
 *
 * NOTE: 渲染之前必须先过这一道，否则左臂左腿取图越界 —— `QImage::copy()` 越界返回
 * 空图，整块就不画了，小人看上去"少了半拉身子"（而且右边那块的**内侧面**会
 * 顶上来，因为挡住它的左腿不在了）。老账号里 64×32 的皮肤很常见。
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
