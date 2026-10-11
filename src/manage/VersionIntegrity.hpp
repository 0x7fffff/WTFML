/**
 * @file VersionIntegrity.hpp
 * @brief 「体检」：把一个已经装好的实例该有的文件逐条比一遍，**只报不装**。
 * @author 0x7fffff
 * @date 2026-10-11
 * @note 待办里那条「独立的完整性校验入口」的判据层。重复点「安装」那条路早就在了
 *       （VersionManager::repairVersionFiles —— 它缺什么补什么），缺的是**不装新的、
 *       只体检**：跑一趟，然后告诉用户"你这 3120 个文件里有 2 个要修"。
 *
 * 【它只算：不联网、不改盘】
 *
 * 这里一个字节都不写、一条请求都不发。`plan()` 说"这一份版本该有哪些文件"，`check()`
 * 逐条比。于是它能在验证工程里对着一个搭出来的沙盒目录跑（mw_check 的 T19.15 就是这么
 * 干的），也方便以后那条界面路自己安排节奏 —— 一条一条比、把进度吐出来，而不是把
 * "扫描"和"界面"焊进同一个函数里。
 *
 * 【哪些文件算数：唯一出处是装配侧】

 * 和 VersionManager::registerVersionFiles() **同一个来源**：客户端 jar 看
 * `downloads.client`、库走 DownloadManager::extractLibraries()、资源索引走
 * extractAssetIndex()、资源对象照索引 json 里那份 objects 表（DownloadManager 的
 * parseAndDownloadAssets() 用的是同一条规矩）。**两侧要一起改**：那边多下一类东西，
 * 这边就会漏检一类（反过来也一样 —— 这边多算一类，体检会报一堆并不存在的问题）。
 *
 * 【深浅两档，各自照抄装配侧的同一条判据】

 * - 客户端 jar / 库：先比字节数，对得上再比 SHA1（= 装配侧的 VerifyMode::SizeAndSha1）。
 *   jar 40 MB 上下、库几百 KB，这一档全量哈希是划得来的。
 * - 资源对象：只看在不在、字节数对不对（= 装配侧的 ExistsOnly）。那 3000 个对象**不做
 *   哈希**：assets 目录 3 GB 出头，全量哈希是几十秒的事，而"0 字节"和"字节数对不上"
 *   已经能逮住"写盘写到一半被中断"那一种（"0 字节永远不算数"这条规矩的出处是
 *   DownloadManager::fileAlreadyValid()）。
 *   NOTE: 这里比装配侧多比了一样 —— 字节数。索引 json 里本来就有 size，不额外花代价。
 *
 * 【哪几种情况叫"要修"】

 * 见 Verdict::Status。**Missing / Empty 算"缺"，其余算"坏"** —— 界面那句提示照这个分，
 * summary() 已经分好了，别再在别处按 status 重新数一遍。
 */

#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

#include "../download/DownloadManager.hpp"

namespace VersionIntegrity {

/// 体检清单里的一条：一个"本该在盘上"的文件，连同比它用的判据。
struct ExpectedFile {
    QString label;      ///< 给用户看的名字（`客户端 jar` / 库坐标 / `资源索引 5` / `资源对象`）
    QString path;       ///< 绝对路径
    QString sha1;       ///< 空 = 这一条没有哈希判据（只比在不在、字节数）
    qint64 size = -1;   ///< <= 0 = 判据里没有字节数这一档
};

/// 一条比对结论。
enum class Status {
    Ok,
    Missing,     ///< 盘上没有这个文件
    Empty,       ///< 有，但是 0 字节（下载写了一半被中断留下的那种）
    WrongSize,   ///< 字节数对不上
    WrongSha1,   ///< 字节数对得上、内容不对
    Unreadable,  ///< 打不开读不动（被别的进程占着、权限不对）
};

/// @brief 比一条的结果。**每个文件都有一条**（好的那条 status == Ok），
///        Report 里只收不好的那些。
struct Verdict {
    Status status = Status::Ok;
    QString label;
    QString path;
    qint64 actualSize = -1;    ///< 盘上真实的字节数（Missing / Unreadable 时是 -1）
    qint64 expectedSize = -1;  ///< 判据里那个字节数（<= 0 = 没有）

    bool ok() const { return status == Status::Ok; }
    /// 一句话（`缺少文件` / `0 字节（下载写了一半留下的）` / `大小 123 字节，应为 456` /
    /// `校验不过（内容与 sha1 对不上）` / `读不动（打不开或读出错）`）。
    QString detail() const;
};

/// @brief 一趟体检的结论。
struct Report {
    /// 非空 = 这一趟**压根没跑起来**（现在只有一种：读不出这个实例的 version.json）。
    /// 和"跑了、发现一堆问题"是两回事，界面上那两句话也不一样。
    QString error;
    int checked = 0;              ///< 清单上有几条（error 非空时是 0）
    QList<Verdict> problems;      ///< 只收 status != Ok 的

    /// 缺的条数（Missing + Empty）。
    int missing() const;
    /// 坏的条数（WrongSize + WrongSha1 + Unreadable）。
    int corrupt() const;
    bool ok() const { return error.isEmpty() && problems.isEmpty(); }
    /// 一句话结论（`3120 个文件都完好` / `3120 个文件里 2 个要修：缺 1 个、坏 1 个`）。
    /// error 非空时就把那句 error 原样回出去。
    QString summary() const;
};

/// @brief 算出这一份版本"该有哪些文件"。
/// @param mergedJson getVersionJson() 那份**合并过**的（父版本的东西要能看见）。
/// @param versionId  版本目录名（jar 的路径按它算 —— 主人可能是父版本）。
/// @param libraries  调用方解好的（`DownloadManager::extractLibraries(mergedJson)`）。
/// @param assetIndex 调用方解好的（`DownloadManager::extractAssetIndex(mergedJson)`）。
/// @return 清单，顺序是 jar → 库 → 资源索引 → 资源对象。
///
/// NOTE: 唯一的读盘是**资源索引那个 json**（要拿里面那份 objects 表）。索引本身不在盘上
///       时 objects 一条都列不出来 —— 那一趟的结论里会有"索引文件缺了"这一条，够用户
///       修了，不另外报错。
QList<ExpectedFile> plan(const QJsonObject &mergedJson, const QString &versionId,
                         const QList<Library> &libraries, const AssetIndex &assetIndex);

/// @brief 比一条。要读盘的只有 WrongSha1 那一档（字节数对得上才会去算哈希）。
/// @note 哈希走 DownloadManager::calculateFileSha1()，和装配侧是**同一个**算法 ——
///       这里不另写一份读文件的循环。
Verdict check(const ExpectedFile &file);

}  // namespace VersionIntegrity
