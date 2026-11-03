/**
 * @file ZipMagic.hpp
 * @brief zip 文件头那四个字节的**唯一判据**（流式落盘、分段合并、本地复用三处共用）。
 *
 * 开头能是什么，按 PKWARE APPNOTE 只有三种：
 *  * `PK\x03\x04` 本地文件头 —— 归档里至少有一个条目；
 *  * `PK\x05\x06` EOCD —— 空归档（22 个字节）；
 *  * `PK\x07\x08` 跨卷标记（spanning signature，APPNOTE 4.4.1）—— **单卷归档也可能带**：
 *    PKZIP 2.5 以后的版本在"按跨卷方式写、最后其实只用了一张盘"时照样写这四位，后面紧跟
 *    `PK\x03\x04`。它只是**合法的少见形状**，不是"数据描述符跑到了开头"（同一个值在条目
 *    数据之后还有另一个意思——可选签名的数据描述符——位置不同、含义不同）。
 *
 * NOTE: 第三种的来历正是以前"一处松、一处严"的那个洞：`StreamedFile` / 分段合并认三种，
 * `DownloadManager` 的复用检查只认前两种 ⇒ 一份带跨卷标记的加载器安装包**下下来算成功、
 * 下次复用又判坏**，每次启动都重下一遍（而且永远修不好）。现在三处同源。
 *
 * NOTE: 这一条只是"挡住 200 + 一段 HTML 错误页"的启发式，不是完整校验（真正认 zip 的
 * 是后面的 SHA-1 / 字节数 / 解压那几层）。多认一种 4 字节开头不会放过错误页 —— HTML 不
 * 可能以 `PK` 起头。
 */
#pragma once

#include <QByteArray>

namespace ZipMagic {

/// @p head 的前四个字节（不足四个 = 不是）能不能作为一个 zip 的开头。
inline bool looksLikeZipHead(const QByteArray &head) {
    return head.startsWith(QByteArrayLiteral("PK\x03\x04")) ||
           head.startsWith(QByteArrayLiteral("PK\x05\x06")) ||
           head.startsWith(QByteArrayLiteral("PK\x07\x08"));
}

} // namespace ZipMagic
