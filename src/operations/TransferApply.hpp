/**
 * @file TransferApply.hpp
 * @brief 按 `TransferPlanner::Plan` 把选中的那几块**搬进目标实例**（coding plan §9.9 第 15 项）。
 * @author 0x7fffff
 * @date 2026-10-18
 *
 * 这一层负责写盘，规矩只有四条，每一条都是"失败时用户还能收场"：
 *   ① **只增不删、不覆盖**：目标里已经有同名的文件就跳过并计数，绝不改它；
 *      （迁移 / 升级副本不是同步工具 —— 用户没要求过"让两边一样"）
 *   ② **链接一律不跟**：`mods/shared` 是 junction 时，一个字节都不进去；
 *   ③ **源和目标不能是同一份、也不能互相嵌套**（判据复用
 *      `InstanceFiles::pathIsSameOrInside()`，它解 junction / 8.3 短名）；
 *   ④ 中途失败**不删已经拷出来的那半份**（和「复制实例」不同：那一边失败时整份副本
 *      没有意义；这一边搬进去的是用户点名要的文件，删掉反而会丢掉已经成功的那部分）。
 *
 * 调用方（controller）负责那几个"现在能不能搬"的前置条件：游戏 / 服务端没在跑、这一份
 * 没在装。这一层只保证路径与覆盖语义。
 */

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

#include "operations/TransferPlanner.hpp"

namespace TransferApply {

struct Result {
    bool ok = false;
    QString problem;  ///< `ok == false` 时的一句话（什么都没做 / 或做了但目标不可用）
    int copiedFiles = 0;
    qint64 copiedBytes = 0;
    int skippedFiles = 0;  ///< 目标里已经有、按规矩没覆盖
    int failedFiles = 0;
    QStringList failures;  ///< 最多记前 5 条（全部失败都计数，只列前 5 个路径）

    /// 「复制了 N 个（X MB），跳过 N 个，失败 N 个」那一句（失败清单另起行）。
    QString text() const;
};

/**
 * @brief 每搬完**一块**（一个勾选项）报一次（coding plan §9.9 bullet 5「逐项报告」）。
 *
 * @param key 那一项的键（`TransferPlanner::Item::key`；任务中心拿它找对应的那一步）。
 * @param ok  这一块有没有失败的文件。**跳过不算失败**（目标里已经有同名的，按规矩没动它）。
 *
 * NOTE: 调用点是在主线程同步回调 —— 这一层本来就在主线程跑（`QFile::copy` 那点活不够开
 *       线程），回调里只该改任务账本，别在这儿再起嵌套事件循环。
 */
using ProgressCallback = std::function<void(const QString &key, bool ok)>;

/**
 * @brief 把 @p keys 点名的那几块从 @p sourceDir 搬到 @p targetDir。
 *
 * @param sourceDir 源实例目录（已存在）。
 * @param targetDir 目标实例目录（已存在；升级副本那一档是先建好副本再调这里）。
 * @param items     `TransferPlanner::plan()` 的 `items`（顺序与相对路径都取它）。
 * @param keys      用户勾选的那些键；不认识的键与盘上不存在的项直接忽略。
 * @param progress  每搬完一块的回调（可空）。
 *
 * NOTE: @p targetDir 必须是**实例目录本身**（`versions/<id>`），不是游戏文件夹 ——
 *       这一层按 `relativePath` 直接落在它底下。
 *
 * NOTE: 这一层天然**可续**：中断之后再跑一遍，已经搬过去的按「目标里已经有」跳过，
 *       只有没搬完的那些真正写盘（§9.9 bullet 5「可恢复中断」）。
 */
Result apply(const QString &sourceDir, const QString &targetDir,
             const QList<TransferPlanner::Item> &items, const QStringList &keys,
             const ProgressCallback &progress = ProgressCallback());

/// 迁移 / 升级副本**收尾校验**的结论（coding plan §9.9 bullet 3「…→ 迁移 → 校验」）。
struct VerifySummary {
    bool ok = false;
    QString problem;          ///< `ok == false` 时的一句话（目录 / 路径不可用）
    int checkedFiles = 0;     ///< 源里点名搬的那些文件，一共看了多少个
    int missingFiles = 0;     ///< 目标里**没有**的那些（真正的问题）
    int sizeMismatchFiles = 0;///< 目标里本来就有同名文件、但大小不同（按规矩没覆盖，只是报一下）
    QStringList missing;      ///< 最多记前 5 个缺失路径
    QStringList mismatched;   ///< 最多记前 5 个大小不同路径

    /// 「校验：看过 214 个文件，缺 0 个」那一句（有问题时附清单）。
    QString text() const;
};

/**
 * @brief 搬完之后对一遍账：点名搬的那些文件，目标里是不是**都有**、大小是不是一样。
 *
 * 只读盘。判据与 `apply()` 同一套：同样的 `items` / `keys` / `safeRelativePath`，
 * 源里的链接不跟（`apply()` 本来也不会拷它们，所以它们不算"该在目标里"）。
 *
 * NOTE: 大小不同**不算失败**：`apply()` 的规矩是"目标里已经有同名文件就跳过"，那份文件
 *       可能本来就是用户自己的。这一档只如实报数，由调用方决定怎么说。
 */
VerifySummary verify(const QString &sourceDir, const QString &targetDir,
                     const QList<TransferPlanner::Item> &items, const QStringList &keys);

}  // namespace TransferApply
