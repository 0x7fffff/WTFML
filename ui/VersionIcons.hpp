/**
 * @file VersionIcons.hpp
 * @brief 版本树和加载器那几行前面的小图标：草方块、四个加载器的标志、最右边那个 >/V。
 * @author 0x7fffff
 * @date 2026-09-22
 * @note 本文件是**纯函数**：不 include 任何 src/ 下的头，不碰网络、不读磁盘。
 *
 * 【为什么是画出来的，不是图片文件】
 *
 * 这个仓库里**一个 .qrc / 一个图片文件都没有**，而图标要么打成一个二进制资源包、
 * 要么在运行时画 —— 用户要的那几个图标（草方块 + 四个加载器）都是"几个色块拼出来的
 * 小图"，画出来是十几行代码，塞个 30 KB 的 PNG 进来反而多一份要维护的二进制。
 * 更要紧的是**能验**：颜色和形状都在代码里，"草方块上面是绿的、下面是土的""展开和
 * 收起画的不是同一张图"这种话可以直接对着像素断言（见 ui_check 的 C 段末尾那组探针断言）。
 *
 * ★ 草方块是照着 MC 那块贴图的样子**自己画**的（16×16 的像素画，颜色是挑的近似色）：
 *   用户明确说"侵权一点点无所谓，不然真没法表示"。四个加载器的标志同理 —— 是
 *   "一眼能认出是哪家"的简化画法，不是搬过来的原始 logo 文件。
 */

#pragma once

#include <QColor>
#include <QPixmap>

/// 版本树 / 加载器那几行前面的小图标。
namespace VersionIcons {

/// 原版版本号前面那个草方块（MC 那块贴图的侧面：上面一层绿、底下一层土，草的下沿是锯齿状）。
/// @param size 边长（像素）。画法是 16×16 的像素画再按整数放大，所以大小取 16 的倍数最清楚。
QPixmap grassBlock(int size);

/// 覆雪草块（上面那层是白的，下面还是土）—— 快照版那一栏版本号前面的图标。
/// 和 grassBlock() 同一份底图，只有"上面那层是哪三个颜色"不同（下沿那张锯齿表共用）。
QPixmap snowyGrassBlock(int size);

/// 土块（一点草都没有，整块都是土）—— 远古版那一栏版本号前面的图标。
QPixmap dirtBlock(int size);

/// 幸运方块（金底 + 深一档的边 + 中间一个奶油色的问号）—— 愚人节版那一栏的图标。
/// 那个问号是**写死的 5×7 位图**，不是画的字：字号一缩放就不是这个形状了，
/// 而且这一格要能对着像素断言。
QPixmap luckyBlock(int size);

/// 版本树上那四栏各自的小图标（用户 2026-09-23 原话：「快照版的草用白色（覆雪草块），
/// 远古版就土块，愚人节版用幸运方块，原版不变」）。
/// @param channelIndex 走的是 `VersionCategory::channelAt()` 那个顺序
///        （0 正式版 / 1 快照版 / 2 远古版 / 3 愚人节版）。**越界给草方块** ——
///        和 loaderMark() 一个路数：认不出来的时候画"原版"，那条路装出来的就是原版。
/// @param size 边长（像素）。四张和 grassBlock() 一样都是 16×16 的像素画再放大。
///
/// NOTE: 这里收的是**下标**、不是 `VersionCategory::Channel`；本文件不许 include
///       `src/` 下的头（见上面那段"为什么是画出来的"）。分类 → 下标那一步由
///       `MainWindow.cpp` 调 `VersionCategory::channelIndex()` 完成，顺序的唯一出处
///       因此仍然是那个枚举。
QPixmap versionChannelMark(int channelIndex, int size);

/// 加载器标志。@p index 走的是 MainWindowUI::modLoaderOptionNames() 那张表的顺序
/// （0 Fabric / 1 Forge / 2 NeoForge / 3 OptiFine），第 4 个（「不装（纯原版）」）
/// 和越界的下标都给草方块 —— 那条路装的就是原版。
QPixmap loaderMark(int index, int size);

/// 最右边那个箭头：收起时朝右（>）、展开时朝下（V）。
/// @param expanded 这一条现在是不是展开的。
/// @param color 线的颜色（树里按"分类 / 版本号"两种深浅给）。
QPixmap chevron(bool expanded, int size, const QColor &color);

/// 朝上那个箭头（^）：底栏那两颗"点开向上展开"的按钮右边缀的就是它。
/// 和 chevron() 同一份笔画（同一个笔宽比例、同一套缓存），只是换了朝向。
/// @param size 边长（像素）。
/// @param color 线的颜色。
QPixmap chevronUp(int size, const QColor &color);

/// 选中那条加载器右边那个**小叉**（在箭头左边）：点它 = 取消装这个加载器、退回纯原版
/// （用户原话："选中以后在那个 > 左侧加一个 x 来取消安装对应版本"）。两条对角线画的，
/// 和 chevron 同一个路数（全是代码画的，仓库里没有图片文件）。
/// @param size 边长（像素）。
/// @param color 线的颜色（比正文浅一档 —— 它是"次要操作"，别和版本号抢眼睛）。
QPixmap cross(int size, const QColor &color);

/// 那一圈**刷新箭头**（↻）：下载页左栏每个分类条目右边挂一个，点它 = 这一类重新问一遍
/// （用户原话："每一项的右边放一个刷新按钮"）。和 cross / chevron 同一个路数 ——
/// 纯代码画的、带缓存、颜色由调用方给。
/// @param size 边长（像素）。
/// @param color 线的颜色（和 cross 一样用次要那一档：它是行里的一个动作，不是正文）。
QPixmap refresh(int size, const QColor &color);

/// 那个**电源**符号（⏻）：左面板每一行右边第一颗按钮，「关闭这个实例」。
/// 画的是一圈留了缺口的环 + 从缺口穿上去的一竖（就是机箱上那个开关的长相）。
/// 和别的字形同一个路数：纯代码画的、带缓存、颜色由调用方给。
/// @param size 边长（像素）。
/// @param color 线的颜色。
QPixmap power(int size, const QColor &color);

/// 那个**齿轮**（⚙）：左面板每一行右边第二颗按钮，「进这个实例的设置」。
/// 八颗平顶的牙 + 中间一个洞（实心填充，不是描边 —— 16 px 下描边会糊成一个圈）。
/// @param size 边长（像素）。
/// @param color 填充的颜色。
QPixmap gear(int size, const QColor &color);

}  // namespace VersionIcons
