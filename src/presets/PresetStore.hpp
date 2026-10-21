/**
 * @file PresetStore.hpp
 * @brief 实例预设的**盘**那一层：落点、下标、读一份 / 写一份、以及那几件增删改（§5–§6 的 P1）。
 *
 * 【磁盘上的形状】（§5）
 *
 * ```
 * <启动器数据根>/presets/
 * index.json 顺序 + 那个唯一的默认项
 * <preset-id>/
 * manifest.json 这一份的全部内容（值那一层见 PresetTypes.hpp）
 * files/ 这一份带的那几个文件（P3 的通用文件组才往里放）
 * ```
 *
 * `<preset-id>` 是 **UUID**，显示名一个字节都不进路径 —— §6 P1 那条「ID 使用 UUID，显示名
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
 * 【默认落点为什么是启动器那一份，不是当前游戏文件夹】
 *
 * 预设是**用户攒下来的习惯**（按键、游戏设置、服务器列表），不是某一份 `.minecraft` 的内容：
 * 换一个游戏文件夹之后，"我昨天做的那份预设"不该跟着不见了。所以它和任务账本
 * （`Tasks::defaultHistoryPath()`）落在同一层 —— 启动器自带的数据根目录，可以整体拷走、
 * 不随当前游戏文件夹切换。
 *
 * 这是这一层里**唯一**一个"以后可能想改"的选择：要是产品上决定"一套游戏文件夹一套预设"，
 * 改 `presetsRoot()` 那一句就够了（别的函数都把 root 当参数收）。
 */

#pragma once

#include <QList>
#include <QString>

#include "PresetTypes.hpp"

namespace Presets {

// ---------------------------------------------------------------------------
// 落点
// ---------------------------------------------------------------------------

/// 预设的根目录：`<启动器数据根>/presets`。**唯一出处**（其余函数都把 root 当参数收）。
QString presetsRoot();

/// `<root>/index.json`
QString indexPath(const QString &root);
/// `<root>/<id>`
QString presetDir(const QString &root, const QString &id);
/// `<root>/<id>/manifest.json`
QString manifestPath(const QString &root, const QString &id);
/// `<root>/<id>/files`
QString filesDir(const QString &root, const QString &id);

// ---------------------------------------------------------------------------
// 名字、id，以及"这个名字会不会跑到目录外面去"
// ---------------------------------------------------------------------------

/// 造一个新 id（不带花括号的 UUID，和 `InstanceGroupStore` 那边同一个写法）。
QString newPresetId();

/// 这一串是不是一个能当目录名的 id。
///
/// 它在**每一个**动盘函数的门口都要过一遍：`presetDir(root, "../../..")` 拼出来的路径是
/// 能落到 root 外面的，而那条路的症状不是报错 —— 是"删掉一份预设"顺手删了别的东西。
/// §6 P1 那条验收（**非法名称无法逃逸目录**）指的就是这一道闸。
bool isValidPresetId(const QString &id);

/// 用户输入的显示名 → 一句能显示的：去掉首尾空白、把换行折成空格、长度钳到 80。
/// **不**做任何路径处理（显示名不进路径，见文件头）。清完是空串 = 用户什么都没输。
QString cleanDisplayName(const QString &raw);

/**
 * @brief 这一段**能不能**属于这一类预设（coding plan §4 / §6.0 P0「不串类别」）。
 *
 * 四类各自管什么（首版口径）：
 * · `GameSettings` —— `options.txt` 那两段、MaLiLib 热键、服务器列表，以及
 * `config/` / `defaultconfigs/` 这两个**高级内容**组（§6.3 明说它们不新增第五类，
 * 归到哪儿还没定案，首版先允许落在游戏设置里并如实显示）；
 * · `ResourcePacks` —— `resourcepacks/` 与 `shaderpacks/`（光影跟资源包同一摊文件）；
 * · `Mods` / `DataPacks` —— 首版还没有对应的段种类（模组引用与数据包载荷在 P4/P5），
 * 只放得下 `Custom`；空预设完全合法；
 * · `Unset`（旧版混合预设）—— 什么都放得下，老数据一个字节都不动。
 */
bool sectionFitsCategory(PresetCategory category, const PresetSection &section);

/// 一份预设里有没有"不属于它那一类"的段。空串 = 全都对得上（`Unset` 永远对得上）。
QString categoryMismatch(const PresetManifest &manifest);

/// 条目里那个**相对路径**安全吗（P3 的通用文件组要用，P1 先把闸立在这儿）。
///
/// 挡掉四样：空串 / 绝对路径（含盘符和 UNC）/ 任何一段是 `..` / 任何一段带冒号。
/// `\` 和 `/` 都当分隔符收（Windows 上两种都认，只挡一种等于没挡）。
bool isSafeRelativePath(const QString &path);

// ---------------------------------------------------------------------------
// 下标（顺序 + 唯一的默认项）
// ---------------------------------------------------------------------------

/// 追加到**末尾**（新做出来的一份排在最后，不抢用户排好的次序）。
PresetIndex appendToIndex(PresetIndex index, const QString &id);

/// 摘掉一条。★ 摘掉的正好是默认项时，**默认位空出来**（`defaultPresetId` 清空）——
/// 不偷偷换给列表里的下一份：那一下用户看不见，而"默认项"是个有后果的开关（§7 安装时
/// 那个预选框会跟着动）。
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
 */
QList<PresetSummary> list(const QString &root, QList<BrokenPreset> *broken = nullptr);

/// 那个唯一的默认项（没有 = 空串）。**不检查它还在不在** —— 默认项丢了这件事由列表那一侧
/// 回答（§8.2 的 UI 用例「默认项丢失」要的是这个组合，不是一个会撒谎的"有默认项"）。
QString defaultPresetId(const QString &root);

// ---------------------------------------------------------------------------
// 增删改
// ---------------------------------------------------------------------------

/// 新建一份（新 UUID + 目录 + `files/` + `manifest.json`，并追加进下标）。
///
/// @param name 显示名。清完是空串 ⇒ **不建**（回一个 `id` 为空的空壳）—— 名字是这一份的
/// 身份里给用户看的那一半，界面上"点新建"那一趟总会先问到名字。
/// @param category 这一份属于哪一类（§4 首版四类）。★ 界面新建时**必须**给一个四类里的；
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
/// 清完是空串回 false。§2.6 那条同样的取舍：普通重命名不改磁盘上的身份。
bool rename(const QString &root, const QString &id, const QString &newName);

/// 删一份（连它的目录），并从下标里摘掉（摘掉的是默认项时默认位跟着空出来）。
///
/// 目录那一趟**不追链接**（用 `removeTreeWithoutFollowingLinks`）：用户把 `files/` 挂到
/// 另一块盘上是常见做法，追进去删的是那一头的东西。
bool remove(const QString &root, const QString &id);

/// 设成那个唯一的默认项（`id` 空 = 取消默认）。不在下标里的回 false。
bool setDefault(const QString &root, const QString &id);

/// 在列表里挪一位（`delta` = -1 / +1）。挪不动回 false。
bool move(const QString &root, const QString &id, int delta);

}  // namespace Presets
