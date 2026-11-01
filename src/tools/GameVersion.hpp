/**
 * @file GameVersion.hpp
 * @brief 「目标版本」那一栏的共用判据：版本次序表 + 「这一项从哪一版起才有」。
 *
 * 旗帜设计、酿造助手这几页各有一个「目标版本」下拉框，**版本表与比较规则只在这里写
 * 一次**（以前只有 `BannerDesign` 里那一份，别处再抄一张就会各走各的）。
 *
 * 【比较按段，不按字面】
 *
 * `1.20.4 < 1.21` 这条按字符串比是错的（`"1.20.4" > "1.21"`）。这里一律走
 * `QVersionNumber`：先归一化（去掉末尾的 `.0`）再比段。
 *
 * 【空串 = 没有门槛】
 *
 * 表里那一条 `since` 是空串时表示各版本都有（表里绝大多数药水、1.14 那批图案）；
 * 用户选的 `version` 是空串时表示「不限版本」。两种方向都按「有」处理。
 */
#pragma once

#include <QString>
#include <QStringList>
#include <QVersionNumber>

namespace GameVersion {

/// 界面上「目标版本」下拉框的次序（**第一项空串 = 不限版本**）。
///
/// NOTE: 只列到 `1.21` —— 排在后面的 `1.21.x` 与 `1.21` 在这些判据里等价（旗帜图案与
/// 酿造配方在这一段没有再变），列出来只会让下拉框变长。
inline QStringList versions() {
    return {QString(), QStringLiteral("1.14"), QStringLiteral("1.15"), QStringLiteral("1.16"),
            QStringLiteral("1.17"), QStringLiteral("1.18"), QStringLiteral("1.19"),
            QStringLiteral("1.20"), QStringLiteral("1.20.4"), QStringLiteral("1.21")};
}

/**
 * @brief `since` 这一档在 @p version 里有没有。
 *
 * @param since 这一项从哪一版起才有（空串 = 各版本都有）
 * @param version 用户选的目标版本（空串 = 不限版本）
 *
 * 认不出来的版本文本按「有」处理 —— 宁可让人看见一行提示，也不把一项事实悄悄判成不存在。
 */
inline bool availableIn(const QString &since, const QString &version) {
    if (version.isEmpty() || since.isEmpty()) {
        return true;
    }
    const QVersionNumber sinceNumber = QVersionNumber::fromString(since);
    const QVersionNumber target = QVersionNumber::fromString(version);
    if (sinceNumber.isNull() || target.isNull()) {
        return true;
    }
    return QVersionNumber::compare(sinceNumber.normalized(), target.normalized()) <= 0;
}

}  // namespace GameVersion
