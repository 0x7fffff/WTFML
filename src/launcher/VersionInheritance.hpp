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

}  // namespace VersionInheritance
