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
 * 【第二轮放松：带模组加载器的版本一个 jar 都没有】
 *
 * 上面那条判据的第二次撞墙是**继承型实例**：Fabric 装出来的
 * `versions/fabric-loader-0.15.11-1.20.1/` 里只有一份 json（外加用户丢进去的 mods/），
 * 客户端 jar 在原版那个目录里。照"顶层有 jar"判，这些实例**一个都进不了列表** ——
 * 表现和上一次一模一样：装了、磁盘上看得见、启动器里没有。
 *
 * 所以 recognized() 多了一条路（声明了 `inheritsFrom` 也算数），jar 的路径也跟着变成
 * "顺着链找主人"（jarOwnerId()）。这两条是配套的，缺一个就会回到"看得见、点不了"。
 *
 * NOTE: 判据本身**不看 wtfml.meta**，这是刻意的 —— 元数据文件只有启动器写过才有，
 *       而"被外部改过名"的那些目录恰恰一份都没有，拿它当判据等于救不了它们。
 *       wtfml.meta 负责的是别的事：真名（显示名）与稳定 id（抗改名的分组归属）。
 */
#pragma once

#include <QString>
#include <QStringList>

namespace InstanceFiles {

/// 实例目录名的长度上限（`validateDirName()` 用的就是它）。
///
/// NOTE: 放在头文件里是为了让**别的地方也要收长度**时用的是同一个数 —— 整合包那边从
///       包名推实例名（ModpackImport::suggestInstanceName）也要收到 64，各写一个 64
///       的话，改一处漏一处的症状是"候选名字自己就过不了校验"。
constexpr int kMaxDirNameLength = 64;

/// 扫一个实例目录得到的原始材料。纯数据 —— 判据本体 resolve() 只吃它、不碰磁盘，
/// 所以真值表能在验证工程里直接钉住，不用先去造目录。
struct DirListing {
    /// 目录名，同时就是实例 id（进 --version、拼所有路径都用它）。
    QString dirName;
    /// 顶层 *.jar 的文件名（**只有文件名，不含路径**）。
    QStringList jarNames;
    /// 顶层 *.json 的文件名（同上）。
    QStringList jsonNames;
    /// 顶层那份版本 JSON 声明的父版本（`inheritsFrom`）；没有就是空串。
    ///
    /// NOTE: 这个字段**不由 scanDir() 填**（那会让热路径上每个实例多读一次 json），
    ///       而是 resolveInstance() 在"一个 jar 都认不出来"时才去读出来填上 ——
    ///       见那里的说明。手工构造 DirListing 的断言可以随便填。
    QString inheritsFrom;
};

/// 一个实例该用哪个 jar、哪个 json。
struct Resolved {
    /// 客户端 jar 的**文件名**；空 = 这个目录里没有自己的客户端 jar
    /// （可能是"继承型"实例，见 inheritsFrom；也可能就是认不出来）。
    QString jarName;
    /// 版本 JSON 的文件名；空 = 没有版本 JSON。**不影响 recognized()**，理由见 resolve()。
    QString jsonName;
    /// 顶层所有 jar 的名字。认不出来时调用方拿它去写告警，认出来时留着方便排查。
    QStringList jarCandidates;
    /// 版本 JSON 声明的父版本（`inheritsFrom`）。空 = 这份版本是自立的。
    QString inheritsFrom;

    /// 这个目录算不算实例。
    ///
    /// 两条路：① 自己有客户端 jar（老判据，绝大多数实例）；② 自己**声明了父版本** ——
    /// 带模组加载器的版本就长这样：Fabric 装出来的目录里只有一份 json，连 jar 都没有，
    /// 主 jar 在原版那个目录里（见 jarOwnerId()）。少了 ② 的话，用户用官方安装器装好的
    /// Fabric / Forge 版本在列表里**一个都看不见**。
    bool recognized() const { return !jarName.isEmpty() || !inheritsFrom.isEmpty(); }
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
 * @brief 这个实例的**客户端 jar 归哪个版本目录所有**。
 * @param versionId 实例 id（= 目录名）。
 * @return 自己目录里就有客户端 jar 时返回它自己；自己没有就顺着 `inheritsFrom` 链往上找，
 *         返回链上第一个"自己有 jar"的那一层的 id；整条链都没有就返回链上最后一个
 *         （这样报错信息会指着那个真正该有 jar 的目录，而不是加载器目录）。
 *
 * 为什么不能假设"jar 就在自己目录里"：Fabric 装出来的版本目录里**只有一份 json**，
 * 一个 jar 都没有 —— 客户端 jar 是原版那份（`versions/1.20.1/1.20.1.jar`）。拼 classpath
 * 和启动前检查都得按"主人"的路径来，否则要么 classpath 指着一个不存在的文件
 * （JVM 一句 "Could not find or load main class" 就没了），要么直接报"缺少客户端 JAR"。
 *
 * NOTE: 环（手写的 json 首尾相接）和超长链都要挡住 —— 挡法是深度上限 + 访问过的 id 集合，
 *       见实现。这里**不报错**，只是退化成一个"指回自己/最后一个"的答案：真正的报错
 *       由调用方（启动前检查说"缺 jar"、或 VersionManager 说"父版本没装"）去说。
 */
QString jarOwnerId(const QString &versionId);

/**
 * @brief 客户端 jar 的**文件名**（不含路径）。拼 classpath 要的是这个。
 * @param versionId 实例 id（= 目录名）。
 *
 * NOTE: 名字取自 jarOwnerId() 那个目录 —— 对继承型实例（没有自己的 jar）返回的是
 *       **父版本那个 jar 的名字**，不是 `<id>.jar`。
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
 * @brief 客户端 jar 在磁盘上的实际路径
 *        （<数据目录>/versions/<jarOwnerId()>/<jarName()>）。
 * @param versionId 实例 id（= 目录名）。
 */
QString jarPath(const QString &versionId);

/**
 * @brief 客户端 jar 相对 <数据目录> 的路径（`versions/<主人>/<文件名>`），拼 classpath 用。
 * @param versionId 实例 id（= 目录名）。
 *
 * 单独开一个口而不是让调用方自己拼 `versions/%1/%2`：那两段分别来自 jarOwnerId() 和
 * jarName()，调用方拼错了（比如拿实例 id 当主人）只在继承型实例上出错 —— 而那正是
 * 真机上最难碰到的形态。分隔符是正斜杠（Java 在 Windows 上也认），和改动前逐字节一致。
 */
QString jarRelativePath(const QString &versionId);

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
 * @brief 一段路径段（目录名或文件名）合不合法 —— 和"实例名"**同一套规矩**，只是不判长度。
 * @param segment 一段（`mods` / `sodium.jar` 这种），**不含**分隔符。
 * @return 合法返回空串；否则返回给用户看的一句原因。
 *
 * 整合包清单里的相对路径是逐段拿它判的（见 ModpackImport::isSafeRelativePath）：同一个
 * 判据只写一份，不然"哪些名字建不出来"这件事会在两个地方慢慢漂开。
 *
 * NOTE: 长度不在这一层判。名字的长度上限（kMaxDirNameLength）是**实例目录名**的规矩，
 *       而路径段的长度上限是另一回事（真实的 mod 文件名可以到 60+ 个字符，按 64 判会
 *       把一个正常的整合包挡在外面）。
 */
QString validatePathSegment(const QString &segment);

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

/// 一次整合包导入能不能用这个名字。
enum class ImportNameVerdict {
    Ok,        ///< 名字合法、盘上也没有它 —— 正常导入
    OkResume,  ///< 撞上的是"上一次没下完的导入" —— 可以接着往里下（见 isUnfinishedImport）
    Taken,     ///< 名字本身不合法、或者被**别的**实例占了 —— 只能换名字
};

/**
 * @brief 整合包导入的名字判据：validateRenameTarget() + 一条"接着下"的口子。
 * @param rawName 用户输入的名字。
 * @param problemOut 非空时，Taken 那条路会把给用户看的那句话写进来（另两种清空）。
 *
 * 和「重命名」判据的差别**只有这一条**：撞上的是"上次没下完的导入"时不算被占 ——
 * 整合包是几百个文件分着下的，网一断就留下半份（真机上那次是 600+ MB / 300 多个文件），
 * 而重名判据只认目录名。不让接着下的话，用户唯一的办法是"换个名字重导"，也就是把已经
 * 下好的那几百 MB 再下一遍 —— 这跟本启动器「重下=校验跳过」那条规矩是相反的。
 *
 * NOTE: OkResume **不是**"静默覆盖"：调用方必须问一句再往下走（见 MainWindow 里那个
 *       确认框）—— 接着下会重新解开包里的 config/资源包，压在用户改过的文件上。
 */
ImportNameVerdict checkImportTargetName(const QString &rawName,
                                        QString *problemOut = nullptr);

/**
 * @brief 这个目录像不像"一次没下完的整合包导入"。
 * @param id 目录名（= 实例 id）。
 *
 * 三条判据，缺一不可（都只看盘，不联网）：
 *   ① **没有** `wtfml.meta`：启动器把一份实例做成功过就会留下它（导入是在最后一步写的、
 *      装加载器是装完写的、改名时也会补）。没有它 = 启动器从没把这一份做完。
 *   ② **没有**客户端 jar：整合包装出来的实例目录里只有一份薄 json + 下下来的那些文件，
 *      主 jar 在原版/加载器那一层。顶层出现 jar 说明这一份不是导入留下的（用户自己放的，
 *      或者手工做的），那种目录不该被导入盖上去。
 *   ③ 自己的版本 json 声明了父版本（`inheritsFrom`）—— 那正是导入写下的那份 json 的形状。
 *
 * 三条都满足时它**仍然可能是**一台机器上手工拼出来的实例（那种目录也没有 meta、也没有
 * jar），所以调用方要问一句。判据宁可偏保守：认错了的代价是"少一条能接着下的路"，
 * 而放宽的代价是"把用户的实例盖掉"。
 */
bool isUnfinishedImport(const QString &id);

}  // namespace InstanceFiles
