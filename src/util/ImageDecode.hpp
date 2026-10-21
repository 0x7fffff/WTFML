/**
 * @file ImageDecode.hpp
 * @brief 把图标字节解码为 QImage：Qt 支持的格式归 Qt，webp 交给 Windows WIC。
 * @note 不引入第三方解码器；WIC 的 webp 解码器可能由系统扩展包提供，因此必须先探测。
 */
#pragma once

#include <QByteArray>
#include <QImage>

namespace ImageDecode {

/// 当前系统是否能解 webp。结果只探测一次并缓存。
/// 没有 WebP 图像扩展时 WIC 返回 WINCODEC_ERR_COMPONENTNOTFOUND，此时退回 PNG。
bool webpAvailable();

/// 字节 → 图片。失败返回 null QImage。
/// 先让 Qt 尝试，失败且系统支持 webp 时再走 WIC；按内容判断，不依赖文件后缀。
QImage decode(const QByteArray &bytes);

} // namespace ImageDecode
