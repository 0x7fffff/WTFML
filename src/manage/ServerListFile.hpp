/**
 * @file ServerListFile.hpp
 * @brief 服务器列表那一页的写侧：**改 / 加 / 删** `servers.dat` 里的条目（读那一半仍旧走
 *        `NbtUtil`）。
 * @author 0x7fffff
 * @date 2026-10-10
 * @note 用户 2026-10-10 原话：「在服务器列表那里做一个编辑地址和名称的功能」。
 *       2026-10-10 稍后补上同一页的另两件事，用户原话：「编辑要能改名字改地址以及删除，
 *       服务器那个页面里放一个添加服务器的按钮，允许添加」。
 *
 * 【为什么新开一个文件，而不是往 NbtUtil 里加一个写入端】
 *
 * `util/NbtUtil.hpp` 的文件头写着一条到今天仍然成立的设计决定：「**故意不做通用 NBT
 * 读写**：多写一个能改文件的写入器，就等于多一个能把用户的服务器列表写坏的入口」。
 * 加一条写入端到那个文件里，等于把那条决定**从文件名底下偷偷划掉** —— 而它防的正是
 * 这件事本身。所以：`NbtUtil` 一个字节不动（读照旧走它），写入端长在**另一个文件**里，
 * 连带把那条约束换一个形式写清楚（见下）。
 *
 * 【换过来的约束：不认识就不写】
 *
 * 写这一侧的纪律是「**读不懂就不动**」，具体三条：
 *   1. 解析走 `NbtTree`（整棵树，不是跳着读）—— 认不出的类型 / 越界的长度 / 末尾多出来
 *      一截，它一律当场拒绝，我们跟着一个字节都不写；
 *   2. **只动 `name` 和 `ip` 两格**，同一个复合标签里别的项（`icon` / `hidden` /
 *      `acceptTextures` / 将来新加的）原样带着走 —— `NbtTree` 保序，所以"没碰过的地方
 *      逐字节不变"这件事在盘上真成立；
 *   3. 认的是**值不是下标**（见 `updateEntry()` 的 @param）—— `NbtUtil::readServerList()`
 *      会**跳过**没有 `ip` 的元素，拿它的下标当行号去改盘上第 N 个元素，就会改错行。
 *
 * 这三条**改 / 加 / 删 三个入口一字不差地用同一套**（`addEntry` / `removeEntry` 就是照
 * `updateEntry` 的样子长出来的）：认不出整棵树就一个字节都不写，认准了那一条才动它，
 * 写回去的是**同一棵树**。这也就是为什么三个入口共用同一个"读进来 + 认出 `servers` 那一格"
 * 的开头 —— 那一段只有一份实现，"认不出就不写"这件事才只有一处会写错。
 *
 * 【写回去的形状跟着读进来那一份】
 *
 * 同一个文件两种落盘方式都存在（裸 NBT 和 gzip，见 `NbtUtil.hpp` 那段）：原来是 gzip 就
 * 还是 gzip，原来是裸的就还是裸的。我们有能力"顺手帮你压一下"，但那不是用户要的 ——
 * "启动器碰过之后这个文件换了形状"会让以后对不上账（和 `WorldFiles::writeLevelData()`
 * 里同一条规矩）。
 *
 * 唯一要自己挑形状的一处是 `addEntry()` 里**文件还不存在**的时候（那个实例从来没加过
 * 服务器）：新写出来的那一份按 **gzip** 落盘。理由是游戏那边读的是 `NbtIo.readCompressed`
 * —— 我们自己造一份裸的出来，游戏会当场把它当成坏的，而"启动器加的第一台服务器进游戏
 * 看不见"正是这条路最不该有的结果。（另一条路见下：文件在的时候，形状跟着原来那份。）
 *
 * 【没有备份】
 *
 * `level.dat` 那条路留了一代备份（那是"坏了就没了"的东西）。`servers.dat` **不留**：
 * 它小、能从游戏里重新加，而且这一条路的每一步都是**要么整份写成、要么一个字节不动**
 * （落盘走 `QSaveFile`，原子替换）—— 留一份 `.wtfml-` 开头的副本在用户的实例目录里，
 * 换来的安全感不如它带来的"这串东西是什么、能不能删"的疑问。
 */

#pragma once

#include <QList>
#include <QString>

#include "util/NbtTree.hpp"
#include "util/NbtUtil.hpp"

namespace ServerListFile {

/// 这个实例的 `servers.dat` 在哪儿 —— `AppPaths::versionDir(versionId) + "/servers.dat"`。
///
/// ★ 这串路径**只有这一份实现**：写侧三个入口的调用方（`MainWindow`）和读侧原来各拼各的
///   （`MainWindow.cpp` 里内联了三处），拼错一处就是"改了这份、读的是那份"。摆在这儿是因为
///   三个入口本来就要一个 `datPath`，而这是它在真实世界里唯一的那一个值。
QString pathFor(const QString &versionId);

/// 读一遍服务器列表。**就是 `NbtUtil::readServerList()`**，在这儿转一道手只是为了让
/// "这一份文件的读和写各在哪儿"一眼看得出来（调用点只 include 这一个头）。
NbtUtil::Status read(const QString &datPath, QList<NbtUtil::ServerEntry> *out,
                     QString *problemOut);

/**
 * @brief 把一条服务器的名字和地址改掉，原样写回去。
 *
 * @param datPath `servers.dat` 的绝对路径（`<实例目录>/servers.dat`）。
 * @param oldName 这一条现在的名字（取不到名字时是空串）。
 * @param oldAddress 这一条现在的地址（`ip` 那一格）。
 * @param newName 改成什么名字（空串 = 名字留空，游戏里显示成"未命名"）。
 * @param newAddress 改成什么地址。**空串直接拒绝** —— 地址是这一条唯一的实质内容
 *        （理由同 `NbtUtil::readServerList()` 跳过没有 `ip` 的元素那一条）。
 * @param problemOut 失败时给用户看的一句话。
 * @return 成功（真的换了盘上那一份）返回 true。
 *
 * ★ **认的是值不是下标**：先在 `servers` 那个 list 里找"`ip` 逐字等于 @p oldAddress 且
 *   `name` 逐字等于 @p oldName"的那一格；找不到再退一步只按 `ip` 找。名字和地址都没
 *   对上时**不写**（报「这一条已经不在文件里了」）—— 用户在这一页上点编辑的这段时间里，
 *   游戏那头（或者别的工具）可能已经把这份文件重写过了，照着下标去改就是改错行。
 *
 * NOTE: `name` 那一格在盘上**可以不存在**（1.12.2 那种老 fixture 里就只有 `ip`）——
 *       那一档按"名字是空串"认，改写成功时**补建**一格 `TAG_String`（类型和位置都照着
 *       原版自己写的形状来）。`ip` 那一格不存在时这一条根本不会出现在列表里（见
 *       `NbtUtil` 那条 NOTE），所以这里不补建 `ip`。
 */
bool updateEntry(const QString &datPath, const QString &oldName, const QString &oldAddress,
                 const QString &newName, const QString &newAddress, QString *problemOut);

/**
 * @brief 往列表里**加一条**服务器，原样写回去。
 *
 * @param datPath `servers.dat` 的绝对路径（一般就是 `pathFor()` 给的那个）。
 * @param name 这一条的名字。**允许空串**（盘上的 `name` 真能是空串，见下面那条 NOTE）。
 * @param address 这一条的地址（`ip` 那一格）。**空串直接拒绝** —— 同 `updateEntry`：
 *        地址是这一条唯一的实质内容，没有它这一行在游戏里是一条点不动的空气。
 * @param problemOut 失败时给用户看的一句话。
 * @return 成功（真的写进盘上那一份了）返回 true。
 *
 * ★ **文件不在 ≠ 失败**：这个实例从来没加过服务器时 `servers.dat` 就是不在的，而那正是
 *   "添加"最常见的第一次。这一档从零建一棵游戏认得出来的树（无名根 Compound + 一格
 *   `servers` 的 `TAG_List<TAG_Compound>`），并且**按 gzip 落盘**（理由见文件头）。
 *   文件在的时候形状跟着原来那份（gzip 还是 gzip、裸的还是裸的），一格都不重排。
 *
 * NOTE: **不查重**。同一个地址在盘上出现两条是合法的（原版就不拦），所以"再加一条一样的"
 *       会真的加出第二条 —— 拦它需要一个"要不要覆盖"的问题，而那是编辑那条路的事。
 */
bool addEntry(const QString &datPath, const QString &name, const QString &address,
              QString *problemOut);

/**
 * @brief 从列表里**删掉一条**服务器，原样写回去。
 *
 * @param datPath `servers.dat` 的绝对路径。
 * @param name 这一条现在的名字（取不到名字时是空串）。
 * @param address 这一条现在的地址。
 * @param problemOut 失败时给用户看的一句话。
 * @return 成功（真的从盘上那一份里拿掉了）返回 true。
 *
 * ★ 认哪一条用的是**和 `updateEntry` 同一套**"认值不认下标"（先名字 + 地址，退一步只按
 *   地址）—— 界面上第 N 行未必是盘上第 N 个元素（`NbtUtil` 跳过没有 `ip` 的元素），
 *   照下标删就会删错行。认不出（找不到 / 那个地址不唯一）时**一个字节都不写**。
 *
 * NOTE: 删掉**最后一条**之后留下的是一个**空列表**（不是把文件删掉）—— 原版里"一台都没有"
 *       就是这个形状，游戏读得动。删掉整个文件反而会让实例目录的形状和游戏预期对不上。
 */
bool removeEntry(const QString &datPath, const QString &name, const QString &address,
                 QString *problemOut);

// ---------------------------------------------------------------------------
// 预设那一段用的那几口（2026-10-15，coding plan §6 P3）
//
// 「实例预设」要把一批服务器**一次**落到这个文件上（§6 P3：Append / Replace / Skip、
// 按规范化地址去重、保留顺序）—— 那是**同一棵树上的多次改动**，上面那三个"改一条 / 加一条
// / 删一条"各走一趟的入口凑不出来。
//
// ★ 所以这里露出来的不是"再来一套写盘"，而是**同一条路的那几步**：上面那三个入口本来就是
//   "读进来 → 认出 servers 那一格 → 动它 → 写回去"这三步加一小段判断。把这几步露出来，
//   是为了让"这份文件怎么读进来、怎么写回去"**仍然只有一处实现** —— 预设那一段自己再写
//   一遍 gzip 检测和 QSaveFile 原子替换，就是第二个能把用户的服务器列表写坏的入口，而那
//   正是 `NbtUtil.hpp` 文件头那条设计决定要防的东西（见本文件头「为什么新开一个文件」）。
//
// ★ 判据（哪个地址算同一台、哪几条要跳过、Replace 要拿掉什么）**不在这儿** —— 在
//   `src/presets/ServerListPreset.hpp`。这一层只认识"值"，不认识"预设"。
// ---------------------------------------------------------------------------

/// 一份 `servers.dat` 在盘上的三种状态。★ 分开是有意义的：**没有这个文件**和**这个文件
/// 读不动**是两句话 —— 前者对"追加"这一档是正常开局（第一次加服务器就是这个状态），
/// 后者才是不许动（同 `NbtUtil::Status` 那三分）。
enum class FileState {
    Ok,          ///< 读到了（**空表也算**）
    Missing,     ///< 这个位置上没有东西
    Unreadable,  ///< 有东西但读不动（是目录 / 太大 / gzip 解不开 / 不是 NBT / 读不出来）
};

/**
 * @brief 把这个文件读成**整棵树**（gzip 那一层一起处理），一个字节都不改。
 *
 * @param gzipped 读到了就写"原来那份压没压"（`Missing` / `Unreadable` 时不动它）——
 *        写回去要跟着它（见本文件头「写回去的形状跟着读进来那一份」）。
 * @param problemOut `Unreadable` 那一档留一句给人看的话。
 */
FileState loadDocument(const QString &datPath, NbtTree::Document *out, bool *gzipped,
                       QString *problemOut);

/// 把一棵树按 @p gzipped 说的形状**原子写回**去（`QSaveFile`，见 .cpp 里 `commitBytes`）。
bool storeDocument(const QString &datPath, const NbtTree::Document &document, bool gzipped,
                   QString *problemOut);

/**
 * @brief 认出并校验 `servers` 那一格。
 *
 * @param out 认出来时指进 @p document 里的那一格（★ **两者必须一起用**：那个指针指向的东西
 *        跟着 `document` 走）。
 * @return 这一格不在 / 不是一张表（元素不是复合标签）⇒ false + 一句人话。
 */
bool serversListOf(NbtTree::Document *document, NbtTree::Node **out, QString *problemOut);

/// 从零建一棵游戏认得出来的树：**无名**根复合标签 + 一格 `servers` 的
/// `TAG_List<TAG_Compound>`（元素类型要带着走 —— 空的表也得有它，不然游戏读到的是坏的）。
NbtTree::Document emptyDocument();

/// 一条服务器在盘上的形状：一个复合标签、`ip` + `name` 两格字符串（顺序照原版自己写的
/// 那份夹具）。**唯一出处** —— "加一条"和"预设追加"两处都用它。
NbtTree::Node entryNode(const QString &name, const QString &address);

/// 新建一份 `servers.dat` 时按什么形状落盘（理由见 .cpp 里 `addEntry()` 那段 ★）：
/// gzip。★ 摆在头文件里是为了让"新建时不准落裸的"这条规矩**只有一处**。
constexpr bool kFreshFileGzipped = true;

}  // namespace ServerListFile
