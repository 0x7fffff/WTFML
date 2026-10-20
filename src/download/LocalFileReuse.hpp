/**
 * @file LocalFileReuse.hpp
 * @brief 从**已经添加过的**游戏文件夹里捡现成文件，省掉一次网络下载。
 * @author 0x7fffff
 * @date 2026-10-20
 * @note 用户原话：「下载新版本的时候（甚至在全新文件夹里下载新东西的时候）复用其它 mc
 *       文件夹/版本的文件以减少等待时间」，以及随后补的那道闸：「文件夹的使用仅限用户
 *       添加了的和官方启动器默认文件夹（如果存在），不要全盘扫描」。
 *
 * 【为什么单独一层】
 * 三个判据（路径怎么对上、来源算不算数、怎么落到目标）都是纯值进、结论出：临时目录里
 * 造两棵树就能逐格念，不用起界面、也不用联网。真正的调度（丢到哪个线程、算不算进
 * `DownloadStats`）在 `DownloadManager` 那边，这一层不碰 QObject。
 *
 * 【来源目录从哪来】
 * 只有两条：`FolderManager::folders()`（自带那份 + 用户添加的）与
 * `FolderManager::officialLauncherFolder()`（`%APPDATA%/.minecraft`，存在才算）。
 * 本层**只认调用方递进来的这一串根**，自己不扫盘、不猜路径 —— "不许全盘扫描"是接口
 * 形状保证的，不是靠调用方自觉。
 */
#pragma once

#include <QString>
#include <QStringList>

#include <atomic>

namespace LocalFileReuse {

/// 这个文件是怎么落到目标上的。
enum class Method {
    None,      ///< 没做成（来源没有、判据不过、或者落盘失败）
    HardLink,  ///< 同一卷：硬链接，一个字节都不拷
    Copy,      ///< 跨卷或链接失败：复制到临时文件再原子改名
};

/// 来源文件的判据档位，和 `DownloadManager::VerifyMode` 那两档一一对应。
enum class Check {
    Exists,      ///< 文件在、不是 0 字节（资源对象那一档，和盘上那份同一个信任级别）
    SizeAndSha1, ///< 先比字节数，再比 SHA-1（库 / 客户端 jar / 模组）
};

/**
 * @brief 目标在当前游戏文件夹里的相对路径（`/` 分隔）。
 * @param path 绝对路径（`DownloadManager` 收到的目标路径）。
 * @param root 当前游戏文件夹（`AppPaths::minecraftDir()`）。
 * @return 在里面就返回相对路径；不在、或者相对路径里有 `.` / `..` 段时返回空串。
 *
 * NOTE: 只做字面比较（`QDir::cleanPath` + 不区分大小写的前缀），**不**解 junction /
 *       符号链接。目标路径全是 `AppPaths` 拼出来的，字面比较就是它们的真实关系；每一条
 *       下载都去 `canonicalFilePath()` 一次，几千个资源对象会把主线程拖住。
 */
QString relativePathWithin(const QString &path, const QString &root);

/**
 * @brief 按相对路径列出候选来源，按调用方给的顺序。
 * @param relativePath `relativePathWithin()` 的结果。
 * @param sourceRoots 允许借文件的那些根（`FolderManager` 给的，已经过滤过存在性）。
 * @param currentRoot 当前游戏文件夹，**它自己会被跳过**（同一份目录没有"借"这回事）。
 *
 * 除了 `<root>/<相对路径>` 之外，还补一条"版本 jar 的正统位置"：
 * `versions/<某个实例>/<owner>.jar` 这种形状会再试 `<root>/versions/<owner>/<owner>.jar`，
 * 因为实例改名之后 jar 的落点跟着实例走，而别的文件夹里那份可能还在原名目录下。
 */
QStringList candidatePaths(const QString &relativePath, const QStringList &sourceRoots,
                           const QString &currentRoot);

/**
 * @brief 这个来源文件能不能直接拿来当目标用。
 *
 * `Check::SizeAndSha1` 走的就是 `DownloadManager::fileAlreadyValid()` 对目标那份的同一套
 * 判据（大小 → SHA-1）；`Check::Exists` 只要求"是个非空文件"，和资源对象那一档一致 ——
 * 资源对象有 3 GB 出头，全量哈希会让"减少等待"变成"换一种等法"。
 */
bool sourceMatches(const QString &path, const QString &expectedSha1, qint64 expectedSize,
                   Check check, const std::atomic_bool *cancel = nullptr);

/**
 * @brief 把 @p source 落成 @p destination：同卷硬链接，不行就复制。
 * @param expectedSha1 复制的同一条路上会边写边算，和它对不上就整份作废。
 * @param verifyWhileCopying 复制时要不要核 SHA-1（`Check::SizeAndSha1` 那一档为真）。
 * @param cancel 取消标志（批次取消 / 程序退出），复制循环每个块看一次。
 * @param allowHardLink false = 跳过硬链接、直接复制。**只给验证工程用**：同一个临时目录
 *        永远在同一卷上，要覆盖"跨卷"那条分支只能让它认这个开关。
 * @param errorOut 失败原因（可以传空）。
 * @return 怎么落成的；`None` = 没做成，调用方按"没复用"处理。
 *
 * 目标已存在时硬链接会失败（`ERROR_ALREADY_EXISTS`），随后复制那条路用 `QSaveFile`：
 * 它先写同目录的临时文件、`commit()` 时原子改名，所以**不会**留下半份目标。
 */
Method materialize(const QString &source, const QString &destination,
                   const QString &expectedSha1, bool verifyWhileCopying,
                   const std::atomic_bool *cancel, bool allowHardLink,
                   QString *errorOut = nullptr);

/**
 * @brief 一整套：按候选顺序找第一个能用的来源，并把它落成目标。
 * @param destinationPath 目标的绝对路径。
 * @param relativePath 目标在当前游戏文件夹里的相对路径。
 * @param sourceRoots 允许借文件的那些根（见 candidatePaths）。
 * @param currentRoot 当前游戏文件夹。
 * @param expectedSha1 非空（调用方已经保证；没有 SHA-1 的文件不借）。
 * @param expectedSize -1 = 不知道。
 * @param check 来源判据档位。
 * @param cancel 取消标志。
 * @param allowHardLink 同上（验证工程用）。
 * @param errorOut 所有候选都没成时，最后一条失败原因。
 * @return 落成的方式；`None` = 一个来源都没用上。
 */
Method reuseFrom(const QString &destinationPath, const QString &relativePath,
                 const QStringList &sourceRoots, const QString &currentRoot,
                 const QString &expectedSha1, qint64 expectedSize, Check check,
                 const std::atomic_bool *cancel, bool allowHardLink,
                 QString *errorOut = nullptr);

}  // namespace LocalFileReuse
