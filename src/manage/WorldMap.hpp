/**
 * @file WorldMap.hpp
 * @brief 一个世界的**只读概况**：种子、玩家位置、出生点、已生成区域与范围、占多大。
 *
 * 【这是拿来干什么的】
 *
 * 「世界地图第一阶段只读 `level.dat` 与 `region/` 里的 `.mca`，展示已
 * 生成区域、范围和玩家位置；Mod 世界明确标注结果可能不完整」。这一层就是那句话里**读**
 * 的那一半 —— 它只读盘、只算，一个字节都不写。
 *
 * 界面怎么摆（世界设置页顶上那行只读的事实 + 一句 tooltip）由 ui/ 和 controller 决定；
 * 这一层不 include 任何 ui/。
 *
 * 【为什么分成两截】
 *
 * · `readLevelFacts()` —— **纯函数**：`level.dat` 已经由 `WorldFiles::readLevelData()`
 * 解成一棵树了，这里只是从 `Data` 底下把那几格捞出来（种子 / 玩家位置 / 出生点）。
 * 验证工程喂一棵手搭的树就能断言，不用造一个真存档；
 * · `scan()` —— 真正碰盘的那一截：列 `region/` 里的 `.mca`、每个文件只读前 8 KiB 的头部表、
 * 顺便把整个世界目录的字节数加起来。
 *
 * 【区域文件头是什么】
 *
 * `.mca` 的前 8 KiB 是两张表：前 4 KiB 是 1024 个"区块 → 位置"的 4 字节项（前 3 字节是
 * 扇区偏移、第 4 字节占几个扇区），后 4 KiB 是 1024 个时间戳。**前三字节为 0 就是那个
 * 区块还没生成过** —— 这一层只要这一件事，所以不读后面的区块正文、不解 zlib。
 *
 * 区块坐标 = 区域坐标 × 32 + 表里那一项在本区域里的下标（列 = 下标 % 32，行 = 下标 / 32）。
 *
 * 【算不出来的地方一律不编】
 *
 * · 老存档没有 `WorldGenSettings`（1.16 起才有）⇒ 退回 `RandomSeed`；两个都没有就
 * 说「盘上没有这一项」；
 * · `Data/Player` 不在（从没进过的世界、服务端存档）⇒ `hasPlayer = false`，界面上
 * 那一格整条不出现；
 * · 区域文件头部读不全（文件短于 8 KiB、或者那个 `.mca` 坏了）⇒ 它不计入区块数与范围，
 * 但 `unreadableRegions` 会记一笔，界面可以把"结果可能不完整"说出来；
 * · 只扫**主世界**的 `region/`。下界 / 末地（`DIM-1/region`、`DIM1/region`）和模组维度
 * （`dimensions/`）这一版都不算 —— 存在与否由 `hasOtherDimensions` 如实报出来。
 *
 * 这不是"世界地图"的绘制层：不读区块内容、不画方块。"区块分析与删除"是后置的，
 * 而且删除必须先生成世界备份 —— 这一层刻意没有任何写盘动作，以后也不要往这里加。
 */

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include "util/NbtTree.hpp"

namespace WorldMap {

/// 一个 `region/r.<x>.<z>.mca`。
struct RegionFile {
    int x = 0;             ///< 区域坐标（文件名里那两个数）
    int z = 0;
    qint64 bytes = 0;      ///< 文件大小
    int chunks = 0;        ///< 头部表里**非空**的格子数（0 ~ 1024）
    bool headerRead = false; ///< 前 8 KiB 读全了没有（没有的话 `chunks` 不代表什么）
};

/// 已生成区块在**区块坐标**里的包络（闭区间）。`ok = false` 表示一个区块都没扫到。
struct ChunkBounds {
    bool ok = false;
    int minX = 0;
    int maxX = 0;
    int minZ = 0;
    int maxZ = 0;
    /// 包络在**方块坐标**里是 `minX * 16` ~ `(maxX + 1) * 16 - 1`（界面要的是这个）。
    int minBlockX() const { return ok ? minX * 16 : 0; }
    int maxBlockX() const { return ok ? (maxX + 1) * 16 - 1 : 0; }
    int minBlockZ() const { return ok ? minZ * 16 : 0; }
    int maxBlockZ() const { return ok ? (maxZ + 1) * 16 - 1 : 0; }
};

/// 一个世界的只读概况（`level.dat` 那几件 + `region/` 那几件）。
struct Snapshot {
    /// `scan()` 跑下来了没有（`region/` 目录在不在都算跑下来了）。
    bool ok = false;
    /// 扫不出来时的一句人话（`region/` 整个不可读之类）。
    QString problem;

    // ---------- level.dat 那几件（readLevelFacts 填） ----------
    bool hasSeed = false;
    qint64 seed = 0;
    bool hasPlayer = false;
    double playerX = 0.0;
    double playerY = 0.0;
    double playerZ = 0.0;
    /// 玩家所在维度（盘上写的是 `minecraft:overworld` / `-1` 这种；原样留着）。
    bool hasPlayerDimension = false;
    QString playerDimension;
    bool hasSpawn = false;
    int spawnX = 0;
    int spawnY = 0;
    int spawnZ = 0;

    // ---------- region/ 那几件（scan 填） ----------
    bool hasRegionDir = false;      ///< `<worldDir>/region` 在不在
    bool hasOtherDimensions = false; ///< `DIM-1` / `DIM1` / `dimensions` 有没有
    QList<RegionFile> regions;      ///< 按文件名排（x 再 z）
    ChunkBounds chunkBounds;
    int chunkCount = 0;             ///< 所有区域文件里非空格子之和
    qint64 regionBytes = 0;         ///< 所有 `.mca` 的字节数之和
    int unreadableRegions = 0;      ///< 头部读不全的 `.mca` 个数

    // ---------- 整个目录 ----------
    quint64 dirBytes = 0;           ///< 递归加起来的总字节数（**不跟 junction / 链接**）
    int fileCount = 0;              ///< 文件个数（同上）
};

/// 从一棵已经解出来的 `level.dat` 里读种子 / 玩家位置 / 出生点。**纯函数**。
///
/// 读不到的那几位**留 `hasXxx = false`**，不拿 0 顶替（和 `WorldFiles::readWorldInfo()`
/// 同一条纪律）。`out` 里 `region/` 那几件不会被碰。
void readLevelFacts(const NbtTree::Document &document, Snapshot *out);

/**
 * @brief 解一个区域文件的头部（前 8 KiB）。**纯函数** —— 验证工程直接喂假头。
 * @param header 文件开头的字节（至少要 8192；短了返回 false）。
 * @param regionX / @param regionZ 这个文件自己的区域坐标（用来把本地下标换成区块坐标）。
 * @param chunksOut 非空时写入非空格子数。
 * @param boundsOut 非空时写入这个区域里已生成区块的包络（一个都没有时 `ok = false`）。
 * @return 头部够长且解出来了 true。
 */
bool parseRegionHeader(const QByteArray &header, int regionX, int regionZ, int *chunksOut,
                       ChunkBounds *boundsOut);

/**
 * @brief 扫一个世界目录：`region/` 里的 `.mca`、区块包络、总字节数。**只读，不写一个字节**。
 *
 * `readLevelFacts()` 那一组字段**不会被碰**（调用方先调哪个都行，这里只往 `region/` 和
 * 目录大小那几位里写）。
 */
Snapshot scan(const QString &worldDir);

} // namespace WorldMap
