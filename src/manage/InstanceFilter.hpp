/**
 * @file InstanceFilter.hpp
 * @brief 实例页那张树的搜索与筛选：判据层，纯函数、不碰界面。
 *
 * 【为什么单独一层】
 *
 * 和 `LaunchHistory` / `TaskHistory` 同一个理由：「这一条算不算匹配」是个能逐格念的
 * 判断 —— 搜索词怎么分词、哪几个串参与比对、四档筛子之间是"与"还是"或" —— 收在一个
 * 不碰 widget 的地方，验证工程才不用真去开一棵树。
 *
 * 【"搜什么"就写在这儿】
 *
 * 要求「搜索实例名、版本号、加载器」。那三样在界面上落在**六个**串里（显示名是
 * 按用户那几个开关拼出来的，可能只显示其中一段），所以这一层不挑，`textsFor()` 把六个
 * 全交上去、命中任意一个即算匹配。少交一个的症状是"明明屏幕上写着那几个字，搜不出来"。
 *
 * 【四档筛子是"与"关系】
 *
 * 类型（原版 / 模组）是一对**互斥**的档，可更新 / 最近运行各自独立 —— 但它们一起加在
 * 搜索词上时全是**与**：词命中 **且** 类型对 **且**（勾了可更新就得真有更新）……
 * 只有"一个都没勾"才是"全都要"（`isActive()` 为假，调用方连筛都不用筛）。
 */

#pragma once

#include <QString>
#include <QStringList>

#include "VersionDisplay.hpp"

namespace InstanceFilter {

/// 实例类型的两个档（外加"不管"）。
///
/// 判据是**加载器名有没有检测出来** —— 和实例行左边那个图标、`instanceLoaderName()`
/// 同一个来源（调用方用 `InstancePagesController::loaderName()` 判好再放进 `hasLoader`，
/// 理由见 `textsFor()`：`VersionDisplayParts::loaderName` 只有我们自己的安装器会写）。
/// "原版"在这里的意思是"没装模组加载器"，
/// 不是"没装模组"（光看 mods 目录里有没有文件的话，一个装过又删干净的实例会被算成原版，
/// 而它其实带着 Forge）。
enum class Kind {
    Any,      ///< 不管（两颗筛选按钮都没勾）
    Vanilla,  ///< 「原版」：没检测到模组加载器
    Modded,   ///< 「模组」：检测到了模组加载器
};

/// 一条实例参与筛选的那几样**事实**。
///
/// 由调用方现算（它才知道这一份实例有没有更新、账本上有没有它）—— 这一层只做判断，
/// 不去问任何一个管理器。
struct Facts {
    /// 参与搜索比对的那些串（`textsFor()` 是它的出处）。空串不必放进来。
    QStringList texts;
    bool hasLoader = false;   ///< 检测到模组加载器
    bool updatable = false;   ///< 这个实例**查过更新**、而且真有几条能更
    bool recent = false;      ///< 启动账本上留有它的记录
};

/// 此刻的搜索词 + 四颗筛选按钮。
struct Query {
    QString text;                        ///< 搜索框里那一行（原样，分词见 searchTokens）
    Kind kind = Kind::Any;               ///< 「原版」/「模组」
    bool updatableOnly = false;          ///< 「可更新」
    bool recentOnly = false;             ///< 「最近运行」

    /// 现在到底在不在筛。**假的时候调用方一条都不该滤掉**（全空的搜索词 + 四颗都没勾
    /// 的结果恒等于"原样"，但那是巧合，不是判据 —— 判据就是这一句）。
    bool isActive() const {
        return !text.trimmed().isEmpty() || kind != Kind::Any || updatableOnly || recentOnly;
    }
};

/**
 * @brief 把搜索框里那一行切成若干关键词。
 *
 * 空白（空格 / 制表符 / 换行）分词，多余空白折掉，空串切完是空表。
 *
 * 为什么要分词：真机上实例名长的样子是 `我的整合包-1.18.2-Forge_40.3.12`，用户想找它
 * 的时候手里那两句（"整合包"、"forge"）**不在同一段里**。整串子串匹配的话他得把中间的
 * 连字符和版本号也打对；分词之后打几个词、顺序随便、大小写随便，就能命中。
 * 分词只看 ASCII 空白：中文全角空格（U+3000）也是一个"空格"，但它落进 `QChar::isSpace()`
 * 的**是**而 `simplified()` 里也认 —— 这里走 `simplified()`，所以全角空格照样切得开。
 */
inline QStringList searchTokens(const QString &text) {
    return text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

/// 这一条事实满不满足 @p query。**唯一一处**判断（树那条路只调它，别在别处再写一遍）。
inline bool matches(const Query &query, const Facts &facts) {
    switch (query.kind) {
    case Kind::Vanilla:
        if (facts.hasLoader) {
            return false;
        }
        break;
    case Kind::Modded:
        if (!facts.hasLoader) {
            return false;
        }
        break;
    case Kind::Any:
        break;
    }
    if (query.updatableOnly && !facts.updatable) {
        return false;
    }
    if (query.recentOnly && !facts.recent) {
        return false;
    }
    // 每一个关键词都得**在某一处**命中（词与词之间是与，词与串之间是或）。
    // 空表（没输入）时这一步什么都不拦。
    const QStringList tokens = searchTokens(query.text);
    for (const QString &token : tokens) {
        bool hit = false;
        for (const QString &text : facts.texts) {
            if (text.contains(token, Qt::CaseInsensitive)) {
                hit = true;
                break;
            }
        }
        if (!hit) {
            return false;
        }
    }
    return true;
}

/**
 * @brief 一个实例参与比对的那些串 —— **唯一出处**。
 * @param parts 版本显示用的那几段（`VersionManager::displayParts()` 读出来的）。
 * @param loaderName 调用方**判好**的加载器名（空串 = 没认出来）。
 * @param loaderVersion 同上，加载器版本（空串 = 没认出来）。
 *
 * ⑤ / ⑥ 由调用方递进来、**不读 `parts.loaderName`**：那一格只有我们自己的安装器会写，
 * PCL / HMCL 合并出来的实例（加载器只写在版本 json 的 `libraries` 里）在那儿是空的 ——
 * 读它的话，界面上写着「Fabric 0.16.14」的实例，搜「fabric」搜不到。这一层在 `manage`，
 * 够不着 `controllers`，所以判据由调用方用 `InstancePagesController::loaderName()` 算好
 * 再传进来；这一层不写第二份判据。
 *
 * 六个串，按"用户最可能打的那一个"排在前面（顺序不影响结果，只影响人读它的时候）：
 * ① 显示名 —— 屏幕上那一行**逐字**在里头，用户是照着它打的；
 * ② 目录名 —— 真机上它还带着用户自己起的名字（`1.18.2-Forge_40.3.12` 这种），
 * 而且加载器版本号只在这儿有全的；
 * ③ 自定义名 —— 显示名里**可能没有**（用户把"自定义名"那个开关关掉了，那一行写的就是
 * 版本号），而它仍是这个实例的名字，搜它得搜得到；
 * ④ 实际版本 —— 同上（开关关掉时显示名里没有这一段）；
 * ⑤ 加载器名 / ⑥ 加载器版本 —— 要求的"加载器"。
 *
 * NOTE: 空串不往里放（`contains("")` 恒真 ⇒ 一个空串会让**任何**关键词都命中，
 * 那就等于"搜什么都搜得到"）。字段全空时回一个空表：那样只剩筛子生效。
 */
inline QStringList textsFor(const VersionDisplayParts &parts,
                            const QString &loaderName, const QString &loaderVersion) {
    const QStringList candidates{formatVersionName(parts), parts.id, parts.customName,
                                 parts.realVersion, loaderName, loaderVersion};
    QStringList texts;
    texts.reserve(candidates.size());
    for (const QString &candidate : candidates) {
        if (!candidate.isEmpty()) {
            texts.append(candidate);
        }
    }
    return texts;
}

}  // namespace InstanceFilter
