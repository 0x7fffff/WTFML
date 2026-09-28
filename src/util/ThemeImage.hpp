/**
 * @file ThemeImage.hpp
 * @brief 把用户挑的那张背景图收进数据目录（并顺手缩到能用的尺寸）。
 * @author 0x7fffff
 * @date 2026-09-28
 *
 * 【为什么不是"把他的路径记下来"】
 *
 * 记用户自己那个路径最省事，但三件事都会坏：
 *   - 他把图挪走 / 删掉（桌面上的图十有八九会挪）⇒ 界面上那一块变回纯底色，而设置页里
 *     还写着"已选一张图"；
 *   - 图在 U 盘 / 网络盘上 ⇒ 每次启动的绘制都挂在一次可能几秒的读上；
 *   - 一张手机拍的照片是 4000×3000 —— 原样解出来是 **48 MB 位图**，而它只是铺在一块
 *     1200 px 宽的底上。用户要的是"背景是我这张图"，不是"启动器替我记着它多大"。
 *
 * 【所以收进来的那一份是什么样的】
 *
 * 长边缩到 ≤ `kMaxSide`（2560），存成 JPEG（没有透明通道时，质量 92）或 PNG。名字是
 * **固定的**：`<数据目录>/cache/theme/<槽位>.jpg|png` —— 一个槽位永远只有一份，用户换图
 * 就是把它盖掉，不会攒下一堆再也用不上的旧图（那才是"主题"这种东西最容易失控的地方）。
 *
 * NOTE: 解码走 `QImageReader::setScaledSize()`，**不是**先读全图再缩：原图要是 20000×15000，
 *       后者在解码那一步就得先要 1.2 GB 内存。`setScaledSize` 是在解码里顺手缩的，
 *       峰值就是目标尺寸那一张。
 *
 * NOTE: 这份 Qt 自己解不了的（**webp** 是最大的一档：浏览器存下来的图十有八九是它，而
 *       `plugins/imageformats/` 里只有 gif / ico / jpeg / svg）退给 `ImageDecode`（WIC）
 *       那条路 —— 挑图那个对话框的筛选里写着 webp，它就得真的打得开。
 *
 * NOTE: 这一层只认识"槽位"这个字符串（`Theme::partKey()` 那一串，例如 `pageBackground`），
 *       **不认识 `Theme::Part`** —— `src/util/` 不往 `ui/` 上看（分层见各文件头）。
 */

#pragma once

#include <QString>

namespace ThemeImage {

/// 收进来那一份的长边上限（像素）。理由见文件头：这不是"画质取舍"，是"48 MB 位图换一块
/// 1200 px 宽的底"。
constexpr int kMaxSide = 2560;

/// 认下来的源文件大小上限（字节）。**只是"这看着不像一张背景图"的粗判**，真正的保护是
/// 上面那个长边上限（解码出来的像素数才是内存）；这一条挡的是"用户手滑选了个视频/压缩包"。
constexpr qint64 kMaxSourceBytes = 64LL * 1024 * 1024;

/**
 * @brief 把 @p sourcePath 收进 `AppPaths::themeImageDir()`，返回盘上那一份的绝对路径。
 * @param slot 槽位名（`Theme::partKey()` 的结果，纯 ASCII）。
 * @param sourcePath 用户挑的那个文件。
 * @param errorOut 非空时，失败那一支写一句**给用户看的话**（成功了会清空）。
 * @return 成功返回收进来那一份的绝对路径（`/` 分隔）；失败返回空串。
 *
 * 失败分三种，各有各的说法：文件读不出来（不存在 / 没权限 / 不是图片）、太大、写不进去。
 * **失败时不动盘上原来那一份** —— 用户挑了一张坏的，不该顺手把他原来那张也弄没。
 */
QString store(const QString &slot, const QString &sourcePath, QString *errorOut = nullptr);

/// 把某一槽位已经收进来的那份删掉（没有就什么都不做）。**设置里那个路径不归它管**：
/// 清了图之后那一项由调用方（`Theme::PartStyle::imagePath`）自己清。
void remove(const QString &slot);

}  // namespace ThemeImage
