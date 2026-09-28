/**
 * @file VersionInheritance.hpp
 * @brief version.json 的 `inheritsFrom`：把带模组加载器的版本和它基于的原版合成一份。
 * @author 0x7fffff
 * @date 2026-09-21
 * @note **纯函数 + 值**：不碰磁盘、不碰网络，所以"哪些字段该谁赢"这套规则能在验证工程里
 *       逐条钉住。走链条的那一半（读父版本、防环、深度上限）在 VersionManager 里，
 *       因为它得去读盘。
 *
 * 【这个文件存在的理由】
 *
 * 带模组加载器的版本不是一份完整的版本 JSON：Fabric 那份只有 8 个库、一个 mainClass、
 * 一行 jvm 参数和一句 `"inheritsFrom": "1.20.1"`，**没有主 jar、没有 assetIndex、
 * 没有 javaVersion、没有游戏参数** —— 那些全在原版那份里。今天这份代码里一处
 * `inheritsFrom` 都没有（grep 过），所以拿它启动的后果不是"少个参数"，是：
 *   - assetIndex 退回硬编码的 "29"、javaVersion 退回 21（静默选错 Java，26.1 会崩在
 *     UnsupportedClassVersionError）；
 *   - classpath 里只有加载器那 8 个库，没有 Minecraft 本体；
 *   - 主类硬编码成 net.minecraft.client.main.Main，于是加载器**根本没被加载**。
 *
 * 合并规则**照抄原版启动器 Version 类的语义**（父在前、子覆盖，见实现里的逐条说明）：
 * 这不是我们发明的格式，是 Mojang 和加载器作者共同假定的语义，照抄比"自己定一套更合理的"
 * 可靠得多 —— 差异会表现为"某些整合包能起、某些起不来"这种最难查的形态。
 */
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace VersionInheritance {

/// 一份版本 JSON 声明的父版本 id（没有 `inheritsFrom`、或它不是字符串时是空串）。
QString parentIdOf(const QJsonObject &json);

/// 继承链最多走多少层。
///
/// 挡的是"手写/写错的 json 首尾相接成环"和"一条长得离谱的链"这两种情况：走链的两个
/// 地方（VersionManager 合并 JSON、InstanceFiles 找 jar 的主人）都拿它当上限，另外各自
/// 还有一个"访问过的 id"集合 —— 光有深度上限挡不住环，光有集合挡不住一条 10 万层的链。
/// 两条都写在同一个常量上，是为了让"链有多长算离谱"只有一个答案。真实世界里的链
/// **最多两层**（加载器 → 原版），16 是那种"离谱但还没到病态"的位置。
int maxDepth();

/**
 * @brief 把子版本和父版本合成一份**扁平的**版本 JSON。
 * @param child 子版本（带加载器的那份），可以是**已合并过**的（见下面那条 NOTE）。
 * @param parent 父版本（原版那份）。
 * @return 合并结果，**不带 `inheritsFrom` 字段**。
 *
 * 逐字段语义（左边是赢家）：
 *   - `id` / `type` / `releaseTime` / `time` / `assets`：子的非空值，否则父的；
 *   - `mainClass`：子的非空值，否则父的。Fabric 那份写着 KnotClient，原版写着
 *     net.minecraft.client.main.Main —— 加载器就是靠这个字段接管的；
 *   - `assetIndex` / `downloads` / `javaVersion` / `logging`：子有就用子的，否则用父的。
 *     **加载器的 JSON 里这些一律缺席**，值全部来自原版；
 *   - `complianceLevel` / `minimumLauncherVersion`：两个里**取大的**（这是"至少要满足
 *     谁的要求"的意思，取小的等于放行一个我们知道不满足的启动器）；
 *   - `arguments.jvm` / `arguments.game`：**父的在前、子的在后拼起来**，两个数组各自拼。
 *     顺序有意义：JVM 对同一条可重复选项取后写的那个，加载器要能盖掉原版的参数；
 *   - `libraries`：父的全部（按原顺序），再叠上子的 —— 按 `name` 去重，**子赢**，
 *     而且赢的那条留在**父原来的位置**上（照抄原版启动器那个 LinkedHashMap 的语义）。
 *     位置不是小事：Java 在 classpath 上取**先找到**的那个同名类，顺序一变，
 *     "谁盖住谁"就变了；
 *   - 其余字段（我们不认识的、Mojang 以后新加的）：子有就用子的，否则用父的。
 *
 * NOTE: 结果里**刻意丢掉 `inheritsFrom`**。两个理由：
 *       1. "这份是不是已经合并过了"因此可以从字段上判出来 —— 一份合并过的东西再合一次
 *          会把父的参数和库**拼两遍**，而那种错不会报错，只会表现成"某个参数莫名其妙
 *          重复了、某些整合包起不来"；
 *       2. 合并后的目标是"把这份 JSON 当完整的用"，留着它只会诱使以后有人再走一遍链。
 *       要看父版本是谁，用 `parentIdOf()` 读**原始**那份（VersionManager 就是这么做的）。
 */
QJsonObject merge(const QJsonObject &child, const QJsonObject &parent);

/**
 * @brief 按**不看版本号的坐标**（`group:artifact[:classifier]`）去掉同一个库的多个版本。
 * @param libraries 一份版本 JSON 的 `libraries` 数组。
 * @return 去重后的数组：同一个坐标只留**最后一个**，位置留在**第一个**出现的地方。
 *
 * 【为什么必须去重】两个同坐标不同版本的 jar 同时进 classpath 是**实打实的启动失败**，
 * 不是"多占点地方"：
 *   - Fabric：`LoaderUtil.verifyClasspath()` 找到两份 `org/objectweb/asm/ClassReader.class`
 *     就抛 `IllegalStateException: duplicate ASM classes found on classpath`，
 *     任何版本的 Fabric 都起不来。真机上 8 个 Fabric 系实例全崩在这一句
 *     （`1.21.4-Fabric 0.16.14`：classpath 上 `asm-9.6.jar` 和 `asm-9.8.jar` 都在）；
 *   - 更普遍地说，classpath 上同名类的"谁盖住谁"由**顺序**决定，两个版本同时在就是
 *     拿运气启动。
 *
 * 【为什么是"最后一个赢"】PCL 合并出来的 json 是**先铺原版那层、再叠加载器那层**：
 * 原版 1.21.4 声明 asm 9.6，Fabric 追加它要的 asm 9.8；原版 1.12.2 声明 log4j 2.8.1，
 * Forge 追加打了 log4shell 补丁的 2.15.0。**后声明的那个是覆盖意图**，前面那个只是它
 * 盖在底下的旧值。这和 merge() 里"子赢"是同一条规矩（子在后）。
 *
 * NOTE: 判据里**保留 classifier**：`lwjgl:3.3.3` 和 `lwjgl:3.3.3:natives-windows` 是
 *       两个都得在 classpath 上的不同文件（1.19+ 的 natives 就靠后者），把 classifier
 *       也抹掉会顺手删掉整批 natives jar。
 *
 * NOTE: ★ 只有"同坐标**并且** `rules` 逐字相同"的两条才会被合成一条。`rules` 不同的
 *       同坐标条目是**给不同平台的**——`1.8.json` 里 `org.lwjgl.lwjgl:lwjgl` 就有两条
 *       （Windows 那份 `2.9.1` 的 rules 是 `[allow, disallow/osx]`、macOS 那份
 *       `2.9.2-nightly` 的 rules 是 `[allow/osx]`）。按坐标硬合并会把 Windows 那份换掉，
 *       换上去的那条在当前平台上没有任何规则命中 ⇒ 按规则被丢掉 ⇒ lwjgl 从 classpath 上
 *       整个消失，真机上的症状是 `NoClassDefFoundError: org/lwjgl/LWJGLException`。
 *       判据只认字面，不去理解 `os`/`features` —— 平台的语义只有 LibraryRules 那一处说了算。
 *
 * NOTE: 名字认不出来的条目（没有 `name` 字段）**原样留着**、不参与去重 —— 和
 *       mergeLibraries() 同一个口径：看不懂的东西丢掉比留着危险。
 *
 * 纯函数：只吃数组、不碰磁盘，真值表能在验证工程里钉住。
 */
QJsonArray dedupeLibraries(const QJsonArray &libraries);

}  // namespace VersionInheritance
