/**
 * @file InstanceFilesPageDirectoryStats.cpp
 * @brief 底下那行统计：一个目录多大、一个世界上次是什么时候玩的。
 *
 * 【为什么单独一个翻译单元】
 *
 * 这两件事都是**慢的同步读盘**：一个存档要递归遍历整棵树（几十万个文件是真有的），
 * `fillDirectoryStats()` 因此**一趟只算一行**、算完自己排下一趟 —— 不是 while 循环一次
 * 算完，否则行已经摆在屏幕上了，用户看到的是一个点了没反应的界面。
 *
 * 【两条说死的规矩】
 *
 * 一、**不跟链接走**：符号链接和 junction 两条一起判（`isSymLink() || isJunction()`），
 * 下潜**之前**判。Windows 的 junction 不是符号链接 —— 只判前者照样进得去，而进去之后
 * 每个文件都是一条独立结果，数会莫名其妙翻几倍；
 * 二、**世界看 `level.dat` 的时间**，不是目录自己的：目录时间只在增删条目时变，用户连着
 * 玩三个小时它可能一动不动。
 *
 * 【这一层的边界】
 *
 * 只 include Qt（`QDir` / `QFileInfo`），不碰 AppPaths、不认识 src/ 里任何一条规矩 ——
 * 所以 `isLinkToElsewhere()` 那两个条件是**照抄**的，将来要改两处一起改。
 */

#include "InstanceFilesPage.hpp"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QListWidgetItem>
#include <QTimer>

namespace {

/// 一个目录**里面所有东西**的字节数（递归；目录项自己那几十个字节不算，那和用户在资源
/// 管理器里看到的"大小"对不上）。
///
/// NOTE: 为什么这件事落在 ui/ 这一层：它要的只是"一个目录有多大"，用的是 Qt 自己的目录
/// 遍历 —— 不碰 AppPaths、也不认识 src/ 里任何一条规矩。这一层唯一那条硬线是
/// "不 include src/、不算路径"，这一件事两条都没踩（和它已经在用的 `QFileInfo::size()`
/// / `QDir::entryInfoList()` 同一档）。
/// NOTE: 带 `QDir::Hidden | QDir::System`：资源管理器报的大小是**算上隐藏文件**的，漏掉
/// 的话启动器报的数会比用户自己能核对的那个小，而且小得毫无规律（`.DS_Store` 那种
/// 是几百字节，但 `session.lock` 之类在存档目录里到处都是）。
/// NOTE: **不跟符号链接/junction 走**（判据是 `isSymLink() || isJunction()` 两条一起，理由
/// 见底下那一段）。实例的 `saves/` 底下挂一个 junction 是有的（用户把存档挪到别的
/// 盘上），跟着走过去会把**那一头**的整个目录算进来 —— 而那一头可能正是别的世界里
/// 的一份，于是这个数会莫名其妙地翻几倍。
/// NOTE: 这是一个**同步**的递归遍历，几十万文件的存档要几百毫秒。调用它的地方只有
/// fillDirectoryStats()，而那一趟一次只算一行（理由写在那边）。
///
/// 这里**自己递归**，不用 `QDirIterator(QDirIterator::Subdirectories)`：判据必须自己
/// 握着 —— 那条路上"看到链接就 `continue`"是**不生效的**：
///
/// ① **junction 不是符号链接**。Windows 上 `mklink /J` 出来的目录 junction，
/// `QFileInfo::isSymLink()` 是 **false**、只有 `isJunction()` 是 true。Qt 自己是这么
/// 分的：`qfilesystemmetadata_p.h:364-370` —— `IO_REPARSE_TAG_SYMLINK` → `LinkType`，
/// `IO_REPARSE_TAG_MOUNT_POINT`（junction）→ **只有** `JunctionType`。仓库在
/// `src/manage/FolderManager.cpp` 里也量过同一件事（那儿的 `isLinkToElsewhere()`
/// 就是两个条件一起判）。
/// ② **`continue` 拦不住下潜**。QDirIterator 拒绝进一个子目录只有一条判据
/// （Qt 6.10.1 `qdirlisting.cpp:384` 的 `checkAndPushDirectory()`：不开
/// `FollowDirSymlinks` 时看 `isSymLink()`）。junction 不是符号链接 ⇒ 它照进不误，
/// 而**进去之后每个文件都是一条独立的结果**，`continue` 只挡住了它自己那一格
/// （目录项，`size()` 是 0）—— 数照样翻倍，只是翻得看不出来。
///
/// 所以这里走本地递归：**下潜前**自己判两个条件，判完再决定进不进。
///
/// NOTE: 这里**故意不 include `src/manage/FolderManager.hpp`**：ui/ 这一层不 include src/
/// （不碰 AppPaths、也不认识 src/ 里任何一条规矩，见上面那 NOTE）。所以那两个条件是
/// **照抄**的，抄的只有"两条一起判"这一件事；将来要改，两处一起改
/// （搜 `isLinkToElsewhere`）。
qint64 sumFilesUnder(const QString &path, int depth) {
    // 深度兜底：真能转起来的那两条路（符号链接、junction）上面已经堵死了，所以这一位
    // 理论上走不到 —— 留着是为了"万一还有第三种重解析点"时别把栈撑爆（崩了比少报更糟）。
    // 256 级远在真实目录树之外（Windows 路径上限 260 个字符），不会变成"少算"的常态。
    if (depth > 256) {
        return 0;
    }
    const QDir dir(path);
    // 只要目录和文件（不要 `QDir::Drives` —— 那不是"这个目录里面有什么"）。
    const QFileInfoList entries = dir.entryInfoList(
        QDir::Dirs | QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
        QDir::NoSort);
    qint64 total = 0;
    for (const QFileInfo &info : entries) {
        if (info.isSymLink() || info.isJunction()) {
            continue;  // 链接本身、以及它那头的一切，都不算（见上面那段）
        }
        if (info.isDir()) {
            total += sumFilesUnder(info.absoluteFilePath(), depth + 1);
        } else if (info.isFile()) {
            total += info.size();
        }
    }
    return total;
}

qint64 directoryBytes(const QString &dirPath) { return sumFilesUnder(dirPath, 0); }

/// 一个世界"上次是什么时候玩的"：`level.dat` 的修改时间。
///
/// 【为什么是 level.dat，不是目录自己的时间】
/// 目录的时间只在**增删条目**时变（存档里加了一个区块文件、删了一个），用户连着玩三个
/// 小时、存档内容一直在变，而那个时间可能一动不动 —— 拿它当"最后游玩时间"是错的。
/// `level.dat` 是游戏**每次保存世界**都会重写的那个文件（关卡名、种子、游戏规则都在
/// 里面），它的时间才是"上一次落盘"。
///
/// NOTE: `session.lock` 的时间戳在游玩期间一直在动，看起来更准，但**它只在玩的时候准**：
/// 退游戏时最后那一下不一定落在那上面，而且它是"锁"不是"存档"。用 level.dat 和
/// PCL 以及大多数启动器一致，用户拿它跟游戏自己的"选择世界"那一屏对得上。
/// NOTE: level.dat 不在（或者读不了时间）时退回**目录自己的时间** —— 给一个近似的值
/// 比给一个空的好，而且那种目录本来就"不是一个完整的存档"。
QDateTime worldLastPlayed(const QString &dirPath) {
    const QFileInfo level(dirPath + QStringLiteral("/level.dat"));
    if (level.exists()) {
        return level.lastModified();
    }
    return QFileInfo(dirPath).lastModified();
}

}  // namespace

void InstanceFilesPage::fillDirectoryStats() {
    // 一趟只算一行，算完自己排下一趟。**不是** while 循环一次算完：
    // - 一个世界要递归遍历整棵树（几十万个文件是真有的），十几二十个世界连着算就是
    // 十几秒 —— 而这时候行**已经摆在屏幕上了**，用户看到的是一个点了没反应的界面；
    // - 一次一行的话，事件循环在中间转过去，界面一直活着，数字一行一行地冒出来
    // （"在算"这件事本身也就看得见了）。
    if (m_statQueue.isEmpty()) {
        return;
    }
    const QString path = m_statQueue.takeFirst();

    // 行可能已经不在屏幕上了：上面那句 takeFirst 之后、这一格找到之前，用户完全可能点了
    // 「刷新」或者删掉了一条。**找不到不是错误**（那一行已经不在了，没什么可写的）。
    if (QListWidgetItem *item = itemForPath(path)) {
        const qint64 bytes = directoryBytes(path);
        const QDateTime played = worldLastPlayed(path);
        // 拼接方式**逐字和上面那个文件行一样**（「12.3 MB / 12:00」）：
        // 同一张表里两种行的小字长得一样，才不会让人以为这一列有两个意思。
        QString meta = formatSize(bytes);
        if (played.isValid()) {
            meta += QStringLiteral(" / ") + played.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
        }
        item->setData(metaRole(), meta);
        item->setData(Qt::AccessibleDescriptionRole, meta);
        m_statCache.insert(path, meta);
    }

    if (!m_statQueue.isEmpty()) {
        m_statTimer->start();
    }
}

