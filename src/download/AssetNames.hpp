/**
 * @file AssetNames.hpp
 * @brief 版本 json 里那两个**会变成盘上路径**的名字：资源索引号、资源对象的哈希。
 *
 * 【为什么单独一层】
 *
 * `<assets>/indexes/<id>.json` 和 `<assets>/objects/<hash 前两位>/<hash>` 这两条路都是拿
 * version.json 里的字符串**原样拼**出来的。version.json 平时来自 Mojang，可它不是只有
 * Mojang 会写：别的启动器、手工放进去的实例、出过事之后从别处拷来的那一份，都能改这两
 * 个字段。`"assetIndex": {"id": "../../../..\\启动项"}` 这一类值拼出来的路径会跑到
 * `assets/` 外面 —— 而且那条路上是"先建目录、再把下载内容写进去"，等于让一份 json 决定
 * 往盘上哪儿写。
 *
 * 所以这两个名字在**落盘那一层**当数据看（这里就是那一层，纯函数、不碰盘）：索引号必须
 * 能当**一段文件名**（一套规矩在 `InstanceFiles::validatePathSegment`），哈希必须是
 * 40 位十六进制 —— 后者还顺带保证了 `objects/<前两位>/` 那一段永远是两位十六进制，
 * 而不是 `..`。
 */
#pragma once

#include <QString>

#include "manage/InstanceFiles.hpp"

namespace AssetNames {

/// 资源对象哈希的长度（SHA-1 的十六进制写法）。
constexpr int kSha1HexLength = 40;

/// 资源索引号的长度上限。真机上最长的是 `pre-1.6` / `legacy` 这一档，留的余量足够。
constexpr int kMaxIndexIdLength = 64;

/// @p hash 是不是 40 位十六进制（大小写都收：接口两边都用过，落盘那份就按它原样命名）。
inline bool isSha1Hex(const QString &hash) {
    if (hash.size() != kSha1HexLength) {
        return false;
    }
    for (const QChar ch : hash) {
        const ushort code = ch.unicode();
        const bool digit = code >= u'0' && code <= u'9';
        const bool lower = code >= u'a' && code <= u'f';
        const bool upper = code >= u'A' && code <= u'F';
        if (!digit && !lower && !upper) {
            return false;
        }
    }
    return true;
}

/// @p id 能不能当 `<assets>/indexes/<id>.json` 里那一段。判据只有一份（见文件头）。
inline bool isSafeIndexId(const QString &id) {
    if (id.isEmpty() || id.length() > kMaxIndexIdLength) {
        return false;
    }
    return InstanceFiles::validatePathSegment(id).isEmpty();
}

} // namespace AssetNames
