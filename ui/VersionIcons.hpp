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

}  // namespace VersionIcons
