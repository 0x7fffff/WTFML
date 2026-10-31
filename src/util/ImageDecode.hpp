/**
 * @file ImageDecode.hpp
 * @brief 把图标字节解码为 QImage：Qt 支持的格式归 Qt，webp 交给 Windows WIC。
 * @note 不引入第三方解码器；WIC 的 webp 解码器可能由系统扩展包提供，因此必须先探测。
 */
#pragma once

#include <QByteArray>
#include <QImage>
#include <QSize>

namespace ImageDecode {

/// 一张图**解出来**的尺寸上限（默认那一档）。见 `Limits` 与 `decode()` 里那段。
constexpr int kMaxDecodedSide = 4096;
constexpr qint64 kMaxDecodedPixels = 16LL * 1024 * 1024;  // 4096 × 4096

/**
 * @brief 一档尺寸上限：长边不超过 @c maxSide、总像素不超过 @c maxPixels。
 *
 * 为什么要判：走进这一层的字节是**外部输入**（Modrinth 图标、服务器图标、皮肤）。
 * PNG / BMP 的头部可以声明 6 万乘 6 万，而压缩后只有几十个字节（"解压炸弹"）——
 * 解码器会照着那个声明去分配，一次就是几个 GB。所以**解码前**先问尺寸（只读头部），
 * 超了直接回 null，一个像素都不解。
 *
 * 默认那一档是给图标 / 皮肤用的（它们本来就只有几十到几百像素）；用户自己挑的主题
 * 背景图另给一档（见 `Store` 那一侧），因为那是一份**本机**文件。
 */
struct Limits {
    int maxSide = kMaxDecodedSide;
    qint64 maxPixels = kMaxDecodedPixels;
};

/// 当前系统是否能解 webp。结果只探测一次并缓存。
/// 没有 WebP 图像扩展时 WIC 返回 WINCODEC_ERR_COMPONENTNOTFOUND，此时退回 PNG。
bool webpAvailable();

/// @p size 在 @p limits 之内（宽高都得是正的）。纯函数，测试直接钉它。
bool sizeWithinLimits(const QSize &size, const Limits &limits = Limits());

/// 字节 → 图片。失败（含"超出尺寸上限"）返回 null QImage。
/// 先让 Qt 尝试，失败且系统支持 webp 时再走 WIC；按内容判断，不依赖文件后缀。
QImage decode(const QByteArray &bytes, const Limits &limits = Limits());

} // namespace ImageDecode
