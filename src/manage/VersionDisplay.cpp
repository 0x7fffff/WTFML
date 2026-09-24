/**
 * @file VersionDisplay.cpp
 * @brief 实现版本显示名的归一化与拼装。
 * @author 0x7fffff
 * @date 2026-09-20
 * @note 见 VersionDisplay.hpp 的说明。
 */

#include "VersionDisplay.hpp"

#include <QDate>
#include <QDateTime>
#include <QStringList>

namespace {

/// 单段的最大长度。手写的 wtfml.meta 里塞一整句话进来是很容易的事，
/// 这里封顶，免得它把「启动游戏」按钮和下拉框撑得没法看。
constexpr int kMaxComponentLength = 64;

/**
 * @brief 清洗一段用户可控的文本。
 * @param raw 原始文本。
 * @return 清掉首尾空白、把内部连续空白（含 \n \r \t）折成单个空格、并截断到长度上限的文本。
 *
 * QString::simplified() 正好是前两件事：它按 QChar::isSpace() 判空白，换行和制表符都算。
 * 必须清换行的原因很直接——「启动游戏」按钮的文字是"启动游戏\n<显示名>"，
 * 名字里带一个 '\n' 就变成三行，而不是两行了。
 */
QString sanitizeComponent(const QString &raw) {
    QString text = raw.simplified();
    if (text.size() > kMaxComponentLength) {
        text.truncate(kMaxComponentLength);
        // 截断点正好落在一个空格上时，会留下一个尾随空格。simplified() 只保证
        // **截断前**没有首尾空白，截断本身可能重新制造一个。而这里的返回值会被
        // 直接拿去用 '-' 拼接，尾随空格会变成 "xxx -1.20.1" 这种看着像 bug 的东西。
        while (text.endsWith(QLatin1Char(' '))) {
            text.chop(1);
        }
    }
    return text;
}

}  // namespace

VersionNameOptions normalizeVersionNameOptions(VersionNameOptions options) {
    // 自定义名和版本名不能同时关：都关了名称槽和版本段都没人收，拼出来是空串。
    // 兜底打开版本名——没有自定义名时名称槽本来就退化成实际版本，所以这正好是
    // 改动前"就显示版本号"的行为，是最安全的落点。
    if (!options.showCustomName && !options.showRealVersion) {
        options.showRealVersion = true;
    }

    // 加载器版本是加载器名的从属项：用户要求"勾选加载器版本号的前提是显示加载器"。
    // 前提不成立时关掉从属项，而不是反过来替用户打开前提——那样等于篡改他的勾选。
    if (options.showLoaderVersion && !options.showLoaderName) {
        options.showLoaderVersion = false;
    }

    return options;
}

QString formatVersionName(const VersionDisplayParts &parts, const VersionNameOptions &globalDefault) {
    // 归一化放在这里，不能只放在加载设置和 setter 里：那两道都可能被绕过
    // （手改注册表、以后设置界面直接构造 options、按实例覆盖），而漏过去的后果
    // 是拼出空串、界面上一块空白。纯函数是最后一个能兜住的地方。
    const VersionNameOptions options =
        normalizeVersionNameOptions(parts.nameFormatOverride.value_or(globalDefault));

    const QString id = sanitizeComponent(parts.id);
    const QString customName = sanitizeComponent(parts.customName);
    const QString loaderName = sanitizeComponent(parts.loaderName);
    const QString loaderVersion = sanitizeComponent(parts.loaderVersion);

    // 实际版本：元数据里写了 "realVersion" 就用它，否则退化成目录名 —— 后者就是
    // 加这个字段之前的行为，所以老实例（没有 wtfml.meta）逐字节不变。
    // NOTE: 必须是 realVersion 而不是 id 参与后面两段，否则目录名里已经揉进去的加载器
    //       （`1.18.2-Forge_40.3.12`）会和 loaderName 拼出重复段，见 .hpp 里的说明。
    const QString realVersion =
        parts.realVersion.isEmpty() ? id : sanitizeComponent(parts.realVersion);

    // 名称槽：没重命名过就退化成实际版本
    const QString label = customName.isEmpty() ? realVersion : customName;

    // 四段都走同一个收口，重复的文本只留第一份。对称是刻意的：只对某一段去重的话，
    // 加载器名恰好等于版本号（或者等于名称）时会拼出 `Forge-Forge` 这种重复段。
    QStringList chosen;
    const auto appendIfNew = [&chosen](const QString &text) {
        if (text.isEmpty() || chosen.contains(text)) {
            return;
        }
        chosen.append(text);
    };

    if (options.showCustomName) {
        appendIfNew(label);
    }
    if (options.showRealVersion) {
        appendIfNew(realVersion);
    }
    if (options.showLoaderName) {
        appendIfNew(loaderName);
    }
    if (options.showLoaderVersion) {
        appendIfNew(loaderVersion);
    }

    return chosen.join(QLatin1Char('-'));
}

QString launchStatusName(const VersionDisplayParts &parts) {
    const QString customName = sanitizeComponent(parts.customName);
    // 有名字就**只用名字**（理由见 .hpp 里那两条）。
    if (!customName.isEmpty()) {
        return customName;
    }

    // 没有名字才轮到"版本号-加载器名"。实际版本那条兜底和 formatVersionName() 一模一样
    // （realVersion 为空就退化成目录名）—— 两处必须一致，否则同一个实例在"启动中"这一句
    // 和下拉框里会报出两个不同的版本号。
    const QString id = sanitizeComponent(parts.id);
    const QString base = parts.realVersion.isEmpty() ? id : sanitizeComponent(parts.realVersion);
    const QString loaderName = sanitizeComponent(parts.loaderName);
    if (base.isEmpty()) {
        // 目录名也是空的（理论上不会，id 的注释说它永远非空）—— 有什么给什么，
        // 别拼出 "-Forge" 这种以分隔符打头的东西。
        return loaderName;
    }
    return loaderName.isEmpty() ? base : QStringLiteral("%1-%2").arg(base, loaderName);
}

QString releaseDateText(const QString &releaseTime) {
    const QString trimmed = releaseTime.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }
    // 清单里是 ISO8601（`2026-09-15T11:23:02+00:00`，也有 `…Z` 那种写法）。fromString
    // 认这两种，而且取出来的**就是那个偏移量下的日期**（Qt 不会偷偷替谁换算一遍），
    // 所以 UTC 的清单不会因为本机在 UTC+8 就把当天下午发布的版本显示成第二天。
    const QDateTime parsed = QDateTime::fromString(trimmed, Qt::ISODate);
    if (parsed.isValid()) {
        return parsed.date().toString(QStringLiteral("yyyy-MM-dd"));
    }
    // 认不出来时还有一条兜底：前 10 个字符正好长得像 `YYYY-MM-DD` 就认它。
    // （哪天清单换成像 `2026-09-15 11:23:02` 这种不带 T 的写法，fromString 会认不出来，
    //   但那 10 个字符仍然是对的。）
    const QString head = trimmed.left(10);
    if (head.size() == 10 && head.at(4) == QLatin1Char('-') && head.at(7) == QLatin1Char('-') &&
        QDate::fromString(head, QStringLiteral("yyyy-MM-dd")).isValid()) {
        return head;
    }
    return QString();
}
