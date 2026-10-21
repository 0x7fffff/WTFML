/**
 * @file VersionInheritance.hpp
 * @brief 合并 version.json 的继承链（原版 + 加载器），并做库去重。
 * @note 纯函数：不读盘、不联网。读父版本、防环和深度检查在 VersionManager。
 *
 * 加载器版本的 JSON 通常缺少主 jar、assetIndex、javaVersion 和游戏参数；
 * 必须与父版本合并后才能启动。
 */
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace VersionInheritance {

/// 一份版本 JSON 声明的父版本 id（没有 `inheritsFrom`、或它不是字符串时是空串）。
QString parentIdOf(const QJsonObject &json);

/// 继承链的最大深度。真实链通常只有两层（加载器 → 原版）；上限用于防环和异常长链。
int maxDepth();

/**
 * @brief 把子版本和父版本合成一份扁平版本 JSON。
 * @param child 子版本（可以已经合并过）。
 * @param parent 原版父版本。
 * @return 合并结果，不保留 `inheritsFrom`。
 *
 * 规则沿用原版启动器：父在前、子覆盖。
 * - 标量字段：子有非空值就用子，否则用父。
 * - `complianceLevel` / `minimumLauncherVersion`：取较大值。
 * - `arguments.jvm` / `arguments.game`：父在前、子在后拼接，子可覆盖原版参数。
 * - `libraries`：父在前，子的同名库覆盖且保留父的位置。
 * - 未知字段：子有就用子，否则用父。
 *
 * 丢弃 `inheritsFrom` 用于判断“是否已合并”，避免重复合并把参数和库拼两遍。
 */
QJsonObject merge(const QJsonObject &child, const QJsonObject &parent);

/**
 * @brief 去掉 classpath 中同坐标的旧库，保留最后一个声明。
 * @param libraries 版本 JSON 的 `libraries` 数组。
 *
 * 同一坐标的多个版本同时在 classpath 上会导致类覆盖不确定，Fabric 甚至会因重复 ASM 直接
 * 启动失败。判据保留 classifier（如 `natives-windows`），只合并坐标和 rules 都相同的条目；
 * rules 不同的条目属于不同平台，不能合并。没有 `name` 的条目原样保留。
 */
QJsonArray dedupeLibraries(const QJsonArray &libraries);

}  // namespace VersionInheritance
