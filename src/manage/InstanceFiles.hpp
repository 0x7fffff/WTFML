/**
 * @file InstanceFiles.hpp
 * @brief 判定 versions/ 下的一个目录算不算实例，并解析它真正该用的 jar / json 文件名。
 * @author 0x7fffff
 * @date 2026-09-21
 * @note header-only 的那两个（AppPaths / LibraryRules）分别只管"路径长什么样"和
 *       "版本 JSON 里的平台规则"，本文件是第一个既要纯规则又要扫盘的，所以带 .cpp。
 *
 * 【这个文件存在的理由】
 *
 * 改造前实例的身份是**目录名**：`versions/<id>/<id>.jar` 存在才算装好了。这个判据有一个
 * 真机上真实存在的后果 —— 用户在资源管理器里把目录改了名，却没改里面 jar 的名字
 * （`原版ATT2地图（无模组）/` 里躺着 `ATT2原版地图（无模组）.jar`，388 MB 和 851 MB 的
 * 整合包各一个），这些实例在列表里**静默消失**，观感就是"启动器少了两三个版本"。
 *
 * 现在把判据放松成"顶层有 jar 且认得出哪个是客户端 jar"，同时**路径不再按名字硬拼**
 * —— 认出来和还能启动是两件事，只做前者会造出"列表里看得见、点启动报缺 jar"。
 * jar / json 的解析散在四处（VersionManager::getVersionJson、LauncherManager 的
 * checkPreLaunch 和 classpath），所以收在这里一份，免得四处各自漂移。
 *
 * NOTE: 判据本身**不看 wtfml.meta**，这是刻意的 —— 元数据文件只有启动器写过才有，
 *       而"被外部改过名"的那些目录恰恰一份都没有，拿它当判据等于救不了它们。
 *       wtfml.meta 负责的是别的事：真名（显示名）与稳定 id（抗改名的分组归属）。
 */
#pragma once

#include <QString>
#include <QStringList>

namespace InstanceFiles {

/// 扫一个实例目录得到的原始材料。纯数据 —— 判据本体 resolve() 只吃它、不碰磁盘，
/// 所以真值表能在验证工程里直接钉住，不用先去造目录。
struct DirListing {
    /// 目录名，同时就是实例 id（进 --version、拼所有路径都用它）。
    QString dirName;
    /// 顶层 *.jar 的文件名（**只有文件名，不含路径**）。
    QStringList jarNames;
    /// 顶层 *.json 的文件名（同上）。
    QStringList jsonNames;
};

/// 一个实例该用哪个 jar、哪个 json。
struct Resolved {
    /// 客户端 jar 的文件名；**空 = 这个目录不算实例**（recognized() 就是判它）。
    QString jarName;
    /// 版本 JSON 的文件名；空 = 没有版本 JSON。**不影响 recognized()**，理由见 resolve()。
    QString jsonName;
    /// 顶层所有 jar 的名字。认不出来时调用方拿它去写告警，认出来时留着方便排查。
    QStringList jarCandidates;

    bool recognized() const { return !jarName.isEmpty(); }
};

/**
 * @brief 判据本体：一个目录该被当成哪个版本。
 * @param listing 扫出来的原始材料。
 * @return 解析结果；jarName 为空表示这个目录不算实例。
 *
 * 三条规则，按顺序：
 *   ① `<目录名>.jar` 在清单里 → 用它。精确匹配优先，绝大多数实例走这条。
 *   ② 否则顶层**恰好一个** .jar → 用它。这是"用户自己在资源管理器里改了目录名"
 *      那种实例，以前会被整个忽略掉。
 *   ③ 其余（一个都没有 / 有两个以上且没有精确匹配）→ 不算实例。
 * json 单独按 ①/② 解析，但**解析不到不算认不出来**。
 */
Resolved resolve(const DirListing &listing);

/**
 * @brief 把一个实例目录扫成 DirListing。
 * @param versionId 实例 id（= 目录名）。
 * @return 目录不存在时三个清单都是空的。
 */
DirListing scanDir(const QString &versionId);

/**
 * @brief scanDir() + resolve()。
 * @param versionId 实例 id（= 目录名）。
 */
Resolved resolveInstance(const QString &versionId);

/**
 * @brief 客户端 jar 的**文件名**（不含路径）。拼 classpath 要的是这个。
 * @param versionId 实例 id（= 目录名）。
 *
 * NOTE: 解析不出来时**退回** `<id>.jar`（老名字），而不是返回空串。想要名字的调用方
 *       （checkPreLaunch、拼 classpath）要的是"一个能拿去看在不在的东西"；在正常目录上这个
 *       退回值就是解析结果本身，所以它们的行为逐字节不变，真解析不出来时报的也还是原来那句错。
 */
QString jarName(const QString &versionId);

/**
 * @brief 版本 JSON 的**文件名**（不含路径）。
 * @param versionId 实例 id（= 目录名）。
 * @return 解析不出来时退回 `<id>.json`，理由同 jarName()。
 */
QString jsonName(const QString &versionId);

/**
 * @brief 客户端 jar 在磁盘上的实际路径（<数据目录>/versions/<id>/<jarName()>）。
 * @param versionId 实例 id（= 目录名）。
 */
QString jarPath(const QString &versionId);

/**
 * @brief 版本 JSON 在磁盘上的实际路径。
 * @param versionId 实例 id（= 目录名）。
 */
QString jsonPath(const QString &versionId);

}  // namespace InstanceFiles
