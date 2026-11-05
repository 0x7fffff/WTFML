/**
 * @file UpgradeOptions.hpp
 * @brief 跨版本搬 `options.txt` 的判据层（U4）：哪些键直接搬、哪些只在新旧都有时才搬、
 *        资源包启用列表怎么按"实际迁入的文件"重建。
 *
 * 【为什么不能整份覆盖】
 *
 * `options.txt` 是"这个版本认识的一串键"。跨版本直接覆盖目标那一份，会把目标版本自己的
 * 新键带走（新版本加了键、旧版本没有），也会把旧版本里已经改名的键塞回去。所以这里的
 * 规矩只有三条，且都能解释：
 *
 * 1. **两边都有的键**：直接搬源的值（用户改过的就是这个），这是"可迁移"；
 * 2. **只有源有的键**：目标版本不认识 ⇒ 默认**不搬**，但列出来给用户看（`unknownKeys`），
 *    不静默丢；
 * 3. **只有目标有的键**：留着目标的值（新版本的默认/用户在新实例上改过的）。
 *
 * 资源包那两行（`resourcePacks` / `incompatibleResourcePacks`）**永远不按源复制**：启用列表
 * 必须按"真的迁入了哪些包"重建（`resourcePacksLine`），否则新实例会引用不存在的文件。
 */
#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

namespace UpgradeOptions {

/// 解析结果：键值 + 原始顺序（`options.txt` 的顺序不保证有意义，但保持稳定更好对账）。
struct Parsed {
    QStringList order;
    QMap<QString, QString> values;
};

Parsed parse(const QString &text);

/// 合并结果。
struct Merge {
    QString text;              ///< 合并后的整份 options.txt 正文
    QStringList migratedKeys;  ///< 两边都有、按源的值搬过来的键
    QStringList unknownKeys;   ///< 只有源有的键（目标版本不认识，默认不搬，列出来）
    QStringList keptTargetKeys;///< 只有目标有的键（保留目标的值）
    QStringList skippedKeys;   ///< 固定跳过的键（resourcePacks / incompatibleResourcePacks）
};

/// 合并：以 @p targetText 为底，按上面三条规矩搬 @p sourceText。
Merge merge(const QString &sourceText, const QString &targetText);

/// 资源包启用列表那一行：只留 @p migratedPackFiles 里真有的包，`vanilla` 永远在最前。
/// @p originalLine 是源 options 里那一行（用来保持原来的相对顺序），可为空。
QString resourcePacksLine(const QString &originalLine, const QStringList &migratedPackFiles);

/// 认不认这条键是"资源包启用列表"（合并时固定跳过）。
bool isResourcePackKey(const QString &key);

/// 副本模式的 options 重写：**没有目标基线**时用这一条。
///
/// 副本是从源实例复制出来的，候选目录里那份 options.txt 就是源的；目标版本自己的默认值
/// 我们没有（不猜）。所以这里保留源的**全部**键（`version` 之类交给目标核心自己覆盖），
/// 只把资源包两行按"真的迁入了哪些包"重建，再把重建那行接在末尾。
///
/// @param sourceText 源 options 正文（候选目录里那份）
/// @param migratedPackFiles 实际迁入 `resourcepacks/` 的包（`file/xxx.zip` 这种相对名）
QString rewriteKeepingAll(const QString &sourceText, const QStringList &migratedPackFiles);

}  // namespace UpgradeOptions

