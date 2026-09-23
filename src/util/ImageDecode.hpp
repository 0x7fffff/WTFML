/**
 * @file ImageDecode.hpp
 * @brief 把"图标的那一串字节"解成一张 QImage：Qt 认得的一律归 Qt，Qt 认不得的
 *        **webp** 交给 Windows 自带的 WIC（Windows Imaging Component）。
 * @author 0x7fffff
 * @date 2026-09-23
 * @note 为什么不引第三方解码器（libwebp / Wuffs）：见 webpAvailable() 那段 —— 这台机器
 *       上 WIC 里就有那个编解码器，链三个系统 DLL 就够，一个字节的第三方代码都不用带。
 *
 * 【这一层为什么存在】
 *
 * 列表里每个资源那张小图有两个地址（同一张图）：
 *   * `…/data/<id>/icon.png`        —— 原图，样张实测 **35218 字节**，这份 Qt 直接能解；
 *   * `…/data/<id>/<hash>_96.webp`  —— 96px 缩略图，同一张实测 **4800 字节**（省 7 倍）。
 *     大小按张会变（2026-09-23 真跑一页 20 张：webp 那些 3.6~22 KB），**小得多**才是
 *     要记住的那一句，"7 倍"只是这张样张的数。
 * 两个地址**都会**被 307 到 `cdn-alt.modrinth.com`（实测：那个跳转是 CDN 选路，不是别名
 * 解析，换哪个地址都一样），而那条链子从国内又慢又飘 —— 所以"少传 30 KB"是实打实的
 * 收益：同一时刻交替各取 5 次，png 中位 **3.09 s**、webp 中位 **1.99 s**。
 *
 * 拿到那 4.8 KB 之后还得解得动它，而这份 Qt 没有 webp 解码器（`QImageReader::
 * supportedImageFormats` 实测没有 webp，`plugins/imageformats/` 里也没有 `qwebp.dll`）。
 * WIC 有 —— 只是**不一定每台机器都有**，所以这里还有个 webpAvailable()。
 */
#pragma once

#include <QByteArray>
#include <QImage>

namespace ImageDecode {

/// 这台机器**现在**能不能解 webp。
///
/// 【为什么不能直接假定有】微软那个 webp 编解码器**不在 WIC 本体里**（2026-09-23 实测）：
/// 本机 `Microsoft Webp Decoder {7693E886-51C9-4070-8419-9F70738EC8FA}` **没有**
/// `HKCR\CLSID\{…}\InprocServer32` 注册项（而 PNG 那个有，指着
/// `C:\Windows\System32\windowscodecs.dll`），它的实体在
/// `C:\Program Files\WindowsApps\Microsoft.WebpImageExtension_1.2.31.0_…` ——
/// 是微软**单独发的一个扩展包**（appx 那套打包 COM）。
/// 本机是 Win11、带着它；**Windows 10 上可能没有**（要装商店里的「WebP 图像扩展」）。
/// 没有的时候 WIC 回 `0x88982F50`（WINCODEC_ERR_COMPONENTNOTFOUND）。
///
/// 所以"要不要去问那条 webp"得先问这一句：有 → 走 4.8 KB 那条，没有 → 老老实实
/// 走 35 KB 的 PNG。**两种情况下界面都是对的，区别只是一个快一点。**
///
/// NOTE: 结论**只探测一次就记住**（函数内静态）：它背后是一次 COM 枚举，不该每张图
///       都做一遍。探测本身不联网、不读盘。
bool webpAvailable();

/// 一串字节 → 一张图。解不出来回**一张 null 的 QImage**（不是抛异常、也不是 1×1 的空图）。
///
/// 顺序：先让 Qt 自己试（png / jpg / gif / bmp…这份 Qt 认得的那一整套都在它那儿），
/// 不行而且 webpAvailable() 才交给 WIC。
///
/// NOTE: 这一层**按内容判，不按文件后缀判** —— 盘上那份缓存里两种字节会混着躺
///（见 ResourceStore::iconCachePath），后缀只说明"这是个图标文件"。
QImage decode(const QByteArray &bytes);

} // namespace ImageDecode
