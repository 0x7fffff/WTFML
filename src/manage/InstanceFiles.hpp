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

/**
 * @brief 一个名字能不能当 versions 下的目录名。**纯函数**，不碰磁盘。
 * @param rawName 用户输入的原始文本（内部先 trim 再判）。
 * @return 合法返回空串；否则返回给用户看的一句原因。
 *
 * 挡掉的东西分两类，理由不同：
 *   - **真会造成事故的**：`/` `\`（同时是路径分隔符，放过去就是路径穿越）、
 *     空名字、`.` / `..`。这一类漏掉的后果不是"名字难看"，是文件跑到 versions 外面去。
 *   - **Windows 会阴人的**：结尾的点（系统**静默**删掉）、保留设备名（`CON`、
 *     `COM1`……根本建不出来）。这一类漏掉的后果是"用户输 A、盘上出现 B"，而且报错
 *     发生在 rename 那一刻，信息通常只有一句"拒绝访问"。
 *
 * 尾随**空格**不在上面那一类里：输入先 trim，所以它根本到不了 Windows 那一步。
 * 用户多敲一个空格不是错误，替他去掉正是他想要的；而尾随的点 trim 管不着，真交给
 * Windows 就会被悄悄吃掉 —— 自己拦下来、把决定权交回用户。
 *
 * 长度上限 64 是**名字**的，不是完整路径的。真机上最长的实例名
 * （`原版ATT2地图（无模组）`）是一层十几个字符，`versions/<id>/<id>.jar` 里 id 要
 * 出现两次，再加上数据目录本身 —— 64 留的余量足够，同时挡掉"把一整句话当名字用"。
 *
 * NOTE: **中文名、全角括号、名字里带空格都是合法的**，真机上就长这样，别顺手加
 *       一条"只许 ASCII"的规则。
 */
QString validateDirName(const QString &rawName);

/**
 * @brief 改名时的完整校验：validateDirName() + "重名了没有 / 是不是没改"。
 * @param rawName 用户输入的新名字。
 * @param currentId 现在这个实例的目录名（不参与重名比较的那个）。
 * @return 合法返回空串；否则返回给用户看的一句原因。
 *
 * 重名比较**不区分大小写**：Windows 的文件系统本来就不区分，`versions/Foo` 和
 * `versions/foo` 是同一个目录，按区分大小写放过去，用户在资源管理器里就会看到
 * 两个"同名"的实例。
 * NOTE: 但只差大小写的那种改名（`Foo` → `foo`）是**合法**的，它改的是磁盘上那个
 *       名字的显示大小写。所以恰好等于 currentId 的那一项不参与比较，也不能因为
 *       "目标已存在"就直接拒 —— 那个"已存在"的目标就是它自己。
 */
QString validateRenameTarget(const QString &rawName, const QString &currentId);

}  // namespace InstanceFiles
