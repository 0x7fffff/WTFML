/**
 * @file PresetStore.hpp
 * @brief 实例预设的**盘**那一层：落点、下标、读一份 / 写一份、以及那几件增删改。
 *
 * 【磁盘上的形状】
 *
 * ```
 * <当前游戏文件夹>/presets/
 * index.json 顺序 + 那个唯一的默认项
 * <preset-id>/
 * manifest.json 这一份的全部内容（值那一层见 PresetTypes.hpp）
 * files/ 这一份带的那几个文件（通用文件组才往里放）
 * ```
 *
 * `<preset-id>` 是 **UUID**，显示名一个字节都不进路径 —— 「ID 使用 UUID，显示名
 * 不作为磁盘路径」就是这一层的存在理由之一：用户把一份预设叫作 `../../windows` 也不会有
 * 任何后果（那个名字只会出现在界面上）。
 *
 * 【为什么每个动盘的函数都收一个 `root` 参数】
 *
 * 和 `LaunchHistory::load/save` / `Tasks::load` 同一条：**路径是参数**。验证工程拿一个临时
 * 目录一喂就能逐格念（"写下去读回来一样不一样"、"坏了一份另外几份还在不在"），不必去改
 * 全局状态 —— 改全局状态来测盘的后果是几个用例之间互相串台，而串台出来的红是最难查的
 * 那一种。默认落点只有一处，`presetsRoot()`。
 *
 * 【默认落点：跟着当前游戏文件夹走】
 *
 * 预设服务的是**这个文件夹里的实例**（「从实例提取」「应用到实例」两头都在这份文件夹里），
 * 所以它和实例同属一个文件夹：`<当前游戏文件夹>/presets`，一换文件夹就是另一批。
 * 全局预设（跨文件夹共享的那一类）是以后才考虑的事，不是现在这一份的形状。
 *
 * 这一层仍然只在一个地方决定落点（`presetsRoot()`），别的函数都把 root 当参数收 ——
 * 以后真要加全局预设，改这一句、或者多喂一个 root 就够了。
 */

#pragma once

#include <QList>
#include <QString>

#include "PresetTypes.hpp"

namespace Presets {

// ---------------------------------------------------------------------------
// 落点
// ---------------------------------------------------------------------------

/// 预设的根目录：`<当前游戏文件夹>/presets`。**唯一出处**（其余函数都把 root 当参数收）。
/// 跟着游戏文件夹走（和实例同一个归属），见 `PresetStore.cpp` 里那一段。
QString presetsRoot();

/// `<root>/index.json`
QString indexPath(const QString &root);
/// `<root>/<id>`
QString presetDir(const QString &root, const QString &id);
/// `<root>/<id>/manifest.json`
QString manifestPath(const QString &root, const QString &id);
/// `<root>/<id>/files`
QString filesDir(const QString &root, const QString &id);
/// `<root>/.preset-summaries.json` —— `list()` 那一张摘要缓存落在盘上的那一份。
///
/// 它**不是数据**：删掉只会让下一趟 `list()` 多读一遍各份 manifest，别的什么都不变。
/// 布局见 `PresetStore.cpp` 里那张表的注释；验证工程按这个路径造/读它。
QString summaryCachePath(const QString &root);

// ---------------------------------------------------------------------------
// 名字、id，以及"这个名字会不会跑到目录外面去"
// ---------------------------------------------------------------------------

/// 造一个新 id（不带花括号的 UUID，和 `InstanceGroupStore` 那边同一个写法）。
QString newPresetId();

/// 这一串是不是一个能当目录名的 id。
///
/// 它在**每一个**动盘函数的门口都要过一遍：`presetDir(root, "../../..")` 拼出来的路径是
/// 能落到 root 外面的，而那条路的症状不是报错 —— 是"删掉一份预设"顺手删了别的东西。
/// 验收（**非法名称无法逃逸目录**）指的就是这一道闸。
bool isValidPresetId(const QString &id);

/// 用户输入的显示名 → 一句能显示的：去掉首尾空白、把换行折成空格、长度钳到 80。
/// **不**做任何路径处理（显示名不进路径，见文件头）。清完是空串 = 用户什么都没输。
QString cleanDisplayName(const QString &raw);

/**
 * @brief 这一段**能不能**属于这一类预设（「不串类别」）。
 *
 * 四类各自管什么（首版口径）：
 * · `GameSettings` —— `options.txt` 那两段、MaLiLib 热键、服务器列表，以及
 * `config/` / `defaultconfigs/` 这两个**高级内容**组（要求它们不新增第五类，
 * 归到哪儿还没定案，首版先允许落在游戏设置里并如实显示）；
 * · `ResourcePacks` —— `resourcepacks/` 与 `shaderpacks/`（光影跟资源包同一摊文件）；
 * · `Mods` / `DataPacks` —— 首版还没有对应的段种类，
 * 只放得下 `Custom`；空预设完全合法；
 * · `Unset`（旧版混合预设）—— 什么都放得下，老数据一个字节都不动。
 */
bool sectionFitsCategory(PresetCategory category, const PresetSection &section);

/// 一份预设里有没有"不属于它那一类"的段。空串 = 全都对得上（`Unset` 永远对得上）。
QString categoryMismatch(const PresetManifest &manifest);

/// 条目里那个**相对路径**安全吗（通用文件组要用，先把闸立在这儿）。
///
/// 挡掉四样：空串 / 绝对路径（含盘符和 UNC）/ 任何一段是 `..` / 任何一段带冒号。
/// `\` 和 `/` 都当分隔符收（Windows 上两种都认，只挡一种等于没挡）。
bool isSafeRelativePath(const QString &path);

// ---------------------------------------------------------------------------
// 下标（顺序 + 唯一的默认项）
// ---------------------------------------------------------------------------

/// 追加到**末尾**（新做出来的一份排在最后，不抢用户排好的次序）。
PresetIndex appendToIndex(PresetIndex index, const QString &id);

/// 摘掉一条。★ 摘掉的正好是默认项时，**默认位空出来**（`defaultPresetId` 清空；
/// 哪一类的默认项是它，那一格留键清成空串）—— 不偷偷换给列表里的下一份，也不让旧版那个
/// 单默认项顶上来：那一下用户看不见，而"默认项"是个有后果的开关（安装页那个预选项会跟着动）。
PresetIndex dropFromIndex(PresetIndex index, const QString &id);

/// 在列表里挪 `delta` 位（-1 / +1）。挪不动（在边上、或者压根不在表里）就原样回。
PresetIndex moveInIndex(PresetIndex index, const QString &id, int delta);

/// 读下标。**文件不在 = 空下标**（一次都没做过预设的新用户就是这样，不是错误）。
/// 读不回来（坏的 JSON / 格式标记不对 / 版本比我们新）⇒ 空下标 + `ok = false`：调用方
/// 那一下**不许**把这个空下标写回去（那会把用户的顺序抹平）。
PresetIndex loadIndex(const QString &root, bool *ok = nullptr);

/// 写下标（原子替换）。目录建不出来回 false。
bool saveIndex(const QString &root, const PresetIndex &index);

// ---------------------------------------------------------------------------
// 单份
// ---------------------------------------------------------------------------

/**
 * @brief 读一份预设。
 *
 * @param ok 读回来了没有。**每一次"读不回来"都要说得出为什么**，所以下面还有 `reason`。
 * @param reason 一句人话（原样能显示给用户）：文件不在 / 不是合法 JSON / 格式标记不对 /
 * schema 版本不认识 / 某一段的种类不认识 / 缺 id 或缺名字。
 *
 * **id 和目录名对不上也算读不回来**：这一份被谁动过、或者两份被拷串了。这种时候宁可把它
 * 报成坏的，也不要拿着"A 目录里的内容属于 B"去改用户的实例。判据就是逐字相等 ——
 * 缺 id 的那一种（`manifestFromJson` 要求 id 非空）根本到不了这一步，它在那儿就被挡了。
 */
PresetManifest loadManifest(const QString &root, const QString &id, bool *ok = nullptr,
                            QString *reason = nullptr);

/// 写一份（原子替换）。`manifest.id` 不合法回 false —— 这一道闸的后果同 `isValidPresetId()`。
bool saveManifest(const QString &root, const PresetManifest &manifest);

// ---------------------------------------------------------------------------
// 列表
// ---------------------------------------------------------------------------

/**
 * @brief 按下标次序列出**读得回来的**那些。
 *
 * 两处补充（都是"别让用户丢东西"）：
 * · 盘上有、下标里没有的（用户自己拷进来一份 / 下标被写坏过）→ **追加在末尾**，不丢；
 * · 下标里有、盘上已经没有的 → 进 `broken`，**而且不替用户改下标**（读的那一趟不写盘）。
 *
 * 这里说的"不写盘"是**用户数据**（下标、manifest、files/）。`list()` 会顺手更新自己那份
 * 摘要缓存（`summaryCachePath()`，见下）：内容全是从 manifest 读出来的、删了也没有后果，
 * 作用是让下一次启动的第一趟列表不用再把每份 manifest 读一遍。
 */
QList<PresetSummary> list(const QString &root, QList<BrokenPreset> *broken = nullptr);

/// 把 `root` 下所有预设的**摘要缓存**丢掉（进程里那张 + 盘上那份），让下一趟 `list()`
/// 真的去读盘。
///
/// 只给测量用（`PerfProbe` 的"后台目录加载"那一步要量的是**冷读**；缓存热着的时候它
/// 11 ms 就回来了，量不到东西）。缓存本身只是加速，不是数据，丢一次没有任何后果。
/// 产品代码里不该有人调它 —— 盘上变了 `list()` 自己会发现，本进程写的会自己作废。
///
/// @param keepDiskCache `true` = 只清进程里那张、盘上那份留着：量"**下一次启动**第一趟"
/// 走的就是这一档（盘上的摘要 + 每份的一次 stat）。本进程写过的那一串也一起清掉 ——
/// 新进程里它本来就是空的。
void forgetSummaryCache(const QString &root, bool keepDiskCache = false);

/// **旧版**那个唯一的默认项（没有 = 空串）。**不检查它还在不在** —— 默认项丢了这件事由
/// 列表那一侧回答（UI 用例「默认项丢失」要的是这个组合，不是一个会撒谎的"有默认项"）。
///
/// 新代码问"这一类默认是哪一份"请用 `defaultFor()`；这个入口只剩读旧数据的用途。
QString defaultPresetId(const QString &root);

// ---------------------------------------------------------------------------
// 每类一个默认项
// ---------------------------------------------------------------------------

/// 下标里"哪一类默认是哪一份"这一格的读法。
///
/// 两份来源：旧版那个全局单默认项（`legacyId`）和新的四类各一格（`byCategory`）。
/// ★ `byCategory` 里**键在 = 这一类显式表过态**，值是空串 = 用户明确说"这一类不要默认"。
struct PresetDefaults {
    QString legacyId;
    QMap<QString, QString> byCategory;
};

/// 一趟把两份读出来（各读一次 `index.json` 就是白读一遍同一个文件）。
PresetDefaults defaults(const QString &root);

/// 某一类的默认项（没有 = 空串）。
///
/// 规则只有两层：显式那格优先（空串就是空串，不让旧默认顶上来）；没表过态时，旧版单默认项
/// **自己的类别**恰好是这一类才顶上来。
///
/// @param legacyCategory 旧版那一份**自己的**类别（调用方读它那份 manifest 得到）。
/// 传 `Unset` = 它没有类别（混合预设 / 读不回来）⇒ **不推导**：不静默重组混合默认。
QString defaultFor(const PresetDefaults &defaults, PresetCategory category,
                   PresetCategory legacyCategory = PresetCategory::Unset);

// ---------------------------------------------------------------------------
// 增删改
// ---------------------------------------------------------------------------

/// 新建一份（新 UUID + 目录 + `files/` + `manifest.json`，并追加进下标）。
///
/// @param name 显示名。清完是空串 ⇒ **不建**（回一个 `id` 为空的空壳）—— 名字是这一份的
/// 身份里给用户看的那一半，界面上"点新建"那一趟总会先问到名字。
/// @param category 这一份属于哪一类（首版四类）。★ 界面新建时**必须**给一个四类里的；
/// 默认 `Unset` 只给"旧数据/测试"留门，新做的跨类总预设不在产品里。
/// @return 建出来的那一份（失败时空壳，判据是 `id.isEmpty()`）。
PresetManifest create(const QString &root, const QString &name,
                      const QString &description = QString(),
                      PresetCategory category = PresetCategory::Unset);

/// 复制一份：新 id、新目录，段和文件原样搬过去。
///
/// @param newName 新名字。空的 ⇒ 用「<原名> 副本」。
/// @return 新那一份（源读不回来 / 名字清完是空的 ⇒ 空壳）。
PresetManifest duplicate(const QString &root, const QString &id, const QString &newName = QString());

/// 改名 = **只改显示名**（目录、id、`files/` 里那些文件名一个字都不动）。
/// 清完是空串回 false。同样的取舍：普通重命名不改磁盘上的身份。
bool rename(const QString &root, const QString &id, const QString &newName);

/// 删一份（连它的目录），并从下标里摘掉（摘掉的是默认项时默认位跟着空出来）。
///
/// 目录那一趟**不追链接**（用 `removeTreeWithoutFollowingLinks`）：用户把 `files/` 挂到
/// 另一块盘上是常见做法，追进去删的是那一头的东西。
bool remove(const QString &root, const QString &id);

/// 设成**旧版**那个唯一的默认项（`id` 空 = 取消默认）。不在下标里的回 false。
///
/// 新代码给某一类设默认请用下面那个按类别的重载：混合预设（`Unset`）没有"这一类"，
/// 走这个老入口才有意义。
bool setDefault(const QString &root, const QString &id);

/// 给**某一类**设默认项（`id` 空 = 这一类明确不要默认）。混合预设（`Unset`）回 false。
///
/// 不要求在 `order` 里：综合预设的子项也是"一份预设"（安装页里它单独一格），而子项不出现在
/// 列表下标里。盘上在不在**这儿不查** —— 默认项被删/读不回来时由调用方落成"这一类没有默认"。
bool setDefault(const QString &root, PresetCategory category, const QString &id);

/// 在列表里挪一位（`delta` = -1 / +1）。挪不动回 false。
bool move(const QString &root, const QString &id, int delta);

}  // namespace Presets
