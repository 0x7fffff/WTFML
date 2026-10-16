/**
 * @file MalilibKeys.hpp
 * @brief MaLiLib 那一族的热键（`config/malilib.json` / `litematica.json` / `tweakeroo.json` /
 *        `minihud.json` / `itemscroller.json` ……）的**字段级**适配层（coding plan §6 P4）。
 * @author 0x7fffff
 * @date 2026-10-17
 *
 * 【为什么这一族值得单独一层，而不是并进通用文件组】
 *
 * 通用文件组（P3）把一份配置当**一坨字节**：合并一坨字节等于拿用户的配置去赌（§6 P3 那条
 * 已经写死了），所以那一层只会整份拷或者整份换。可 MaLiLib 这一族的文件**是结构化的、而且
 * 用户真正想搬走的常常只有热键那几行** —— "把这台电脑上这一套按键搬过去"是玩家换机时最想要
 * 的一件事，而"把整份 `tweakeroo.json` 拷过去"会连他在这一台上调过的每一个开关一起搬走。
 * 所以这一层只做**字段级**的那一小块：一格一格地看、一格一格地写。
 *
 * 【真长什么样（2026-10-17 在本机真装出来的 236 份配置上量的）】
 *
 * 顶层是"类别 → 选项名 → 值"：
 *
 * ```json
 * {
 *   "Generic": {
 *     "actionbarHudTicks": 60,
 *     "enableActionbarMessages": { "enabled": true, "hotkey": { "keys": "" } }
 *   },
 *   "GenericHotkeys": { "openGuiConfigs": { "keys": "A,C" } },
 *   "TweakToggles":    { "tweakArmorStandSwap": false },
 *   "config_version": 5
 * }
 * ```
 *
 * 三条实测（别凭印象改）：
 *
 *  * **热键那一格是 `{"keys": "A,C"}`** —— 一个**逗号分隔**的键名串，空串 = 没绑。键名是
 *    GLFW 那一套大写词（`M` / `LEFT_CONTROL` / `PAGE_UP` / `KP_MULTIPLY` / `F3` /
 *    `BUTTON_3` / `MOUSE_MIDDLE` / `BACKSLASH` / 数字），修饰键写在前面。真数据里见过的
 *    41 个词全是这一套 —— 所以这一层**不去翻译**它们（没有一张权威的"哪个词是哪个物理键"
 *    表就别硬编一张），只把串按逗号拆开、原样带着。
 *  * **扇区文件里那一串 `*Toggles` / `*Hotkeys` 是一对**（`TweakToggles` + `TweakHotkeys`、
 *    `InfoTypeToggles` + `InfoHotkeys`、`DisableToggles` + `DisableHotkeys`、
 *    `RendererToggles` + `RendererHotkeys`、`StructureToggles` + `StructureHotkeys`、
 *    `Generic` + `GenericHotkeys`）。热键**不在**开关那一格里，是旁边单独一格；而
 *    `{"enabled": …, "hotkey": {…}}` 那种"带热键的开关"又是**一格装两样**。所以这一层认出
 *    三档来：`Hotkey`（整格就是热键）、`Hotkeyed`（一格装开关 + 热键）、`Value`（别的值）。
 *  * **文件是**可以不全的**：这一族读配置是"文件里写了哪几格就改哪几格、没写的就是模组自己
 *    的默认值"（实测本机那 236 份里最长 29 KB、最短 149 B，149 B 那份只有 3 格而模组照跑）。
 *    所以缺格、添格、只写一小片都是**正常文件**，不是我们造的怪东西 —— 这一层的合并因此
 *    可以放心地"只补缺的那几格"。
 *
 * 【★ 认这一族靠的是**结构**，不是文件名】
 *
 * §6 P4 说「并通过结构识别 TweakerMore、Masa Gadget、OMMC 等扩展」。落地就是
 * `malilibInspect()`：不看它叫什么，只看**它长不长这样** —— 顶层是对象、值也是对象、
 * 里面至少有一格是"热键那一格"（对象里有字符串 `keys`）或者"带热键的开关"（布尔 `enabled`
 * 加一个 `hotkey`）。这样一来：
 *
 *   · 没听说过的模组（TweakerMore / 谁以后新写的）**照样认得出来**，不需要维护一张名字表；
 *   · 而一个**恰好也叫 `litematica.json` 但根本不是这回事**的文件认不出来 —— 那正是我们要的
 *     （名字谁都能起，结构作不了假）；
 *   · 认得出来的那些，`modId` 就是**文件名那一截**（`config/<modId>.json`），写回去也用它。
 *
 * ★ 反向也成立：`config/litematica_<某个世界>_dim_minecraft_overworld.json` 那种**按世界/
 *   维度分身的文件**长的是"值"那一半（`{"tool_mode_data": {"delete": {"use_placement": …}}}`），
 *   一格热键都没有 ⇒ 认不出来 ⇒ 不进这一层（那本来也不是热键文件）。
 *
 * 【★ 只动该动的那一格，别的原样带着】
 *
 * 合并写回去的时候，这一步只改目标那一格里**我们看得懂的那几样**：
 *
 *   · `Hotkey`   —— 它的 `keys`；这一格里除 `keys` 以外的兄弟字段**只补缺、不覆盖**
 *     （我们没见它们出现过，但万一哪个版本写了 `activation` / `context` 之类，**照原样带着
 *     走**比丢掉强，而"覆盖一个我们看不懂的字段"比丢掉还糟）；
 *   · `Hotkeyed` —— 它的 `hotkey`（整块按上面那条走）；`enabled` **只有预设里带了才写**
 *     （这就是「仅合并热键」和「合并全部已知设置」的区别，见下）；
 *   · `Value`    —— 整格换成预设里那一份，但**类型必须一致**（§6 P4「类型变化时拒绝转换」）。
 *
 * ★ **不认识的**格子（模组自己的、别的版本的、我们这一层还没接的）一律**一个字节都不动**：
 *   合并是"把这几格搬过去"，不是"把文件改成我们的样子"。
 *
 * 【模式的语义（`ApplyMode` 见 `PresetTypes`）】
 *
 * | 档位 | 这一趟做什么 |
 * |---|---|
 * | `Merge`（默认） | 预设里那几格写进去；目标上其余的热键**一个字不动**（用户没说要换掉它们） |
 * | `Replace` | 预设里那几格写进去，**目标上其余认得出来的热键解绑**（`keys` 清空，条目留着 —— 见下） |
 * | `CopyIfMissing` | **按格**算：目标上**没有**这一格（或者这一项）才写，有这一格就一个字不动 |
 * | `Append` | 只在目标那一格**还没绑东西**（缺这一格、或者 `keys` 是空串）时才写 —— "别抢我已经绑好的" |
 * | `Skip` | 什么都不做 |
 *
 * ★ `CopyIfMissing` 按**格**算，不是按整份文件算 —— 和原版设置那一层是**同一把尺子**
 *   （`VanillaOptions.cpp`：「缺了才拷」这一档：目标上已经有这个键的，一个字节都不动）。
 *   要把"整份配置在目标上还没有的时候照搬一份"这件事办了，走**通用文件组**（P3，`files/` 里
 *   装着整份文件 + `缺失时才拷`），不是这一层 —— 这一层装的是"用户选了哪几格"，再存一份原始
 *   字节等于同一个东西两处记（§2.8）。
 *
 * ★ `Replace` 是**解绑 `keys`、不删格子**：`"keys": ""` 就是这一族自己的"没绑"（真数据里
 *   满地都是），而**删掉那一格**会让模组按它代码里的默认值重建 —— 用户看到的就不是"这个键
 *   被我解绑了"，而是"这个开关回到出厂设置"了。值那一格（数字/开关）「覆盖」档**一个都不删**：
 *   把一个键位解绑是能说清楚的一种改动，把用户调好的数字清空**没有可恢复的说法**。
 *
 * 【「仅热键」与「全部已知设置」：区别在**载荷里有没有那几格**，不在一个模式开关上】
 *
 * §6 P4 要的是「默认仅合并热键；**可选**合并全部已知设置、完整覆盖、缺失时复制」。落地：
 * 提取的时候用户勾没勾"连开关一起搬"，决定的是**载荷里写不写 `enabled` / `value`**；
 * 合并的时候照着载荷里有的写。这样"这一份预设要搬哪几样"这件事只有**一处**说法（那几格
 * 本身），不必再在模式和载荷之间对一次账。
 *
 * ★ 两条落地的规矩：**"一格什么都不说"就不收**（`{"keys": ""}` 那种没绑的空格子：真数据里
 *   满地都是，照着搬一圈等于把目标上所有绑好的键解绑一遍 —— 「合并」会当场变成「覆盖」）；
 *   而**值那一格在目标上没有时跳过并报告**（§6 P4「目标字段缺失时跳过并报告」），热键才凭空
 *   造格子（搬热键本来就是这一层的用处）。
 *
 * 【§6 P4 那几格 DTO 与"保存触发、上下文……"】
 *
 * `KeyGesture` 是 P4 点名的那张展示/冲突用的 DTO。真数据里**只见过 `keys`**（本机 236 份里
 * 一格 `activation` / `context` / `orderSensitive` 都没有），所以这一层的立场是：
 *
 *   · **写**回目标那一份时只写 `keys`（外加上面那条"只补缺"），**绝不凭空写**那些字段 ——
 *     凭空写一格等于我们伪造了一份格式，而模组下一版可能根本不认；
 *   · **读**的时候顺手把撞见的那些认进 DTO（按 DTO 自己那几个名字找：`trigger` / `context` /
 *     `orderSensitive` / `allowExtraKeys` / `exclusive` —— 这几个名字**没在真数据上验过**
 *     （本来就没数据），猜错了的代价只是界面上少显示一行字，不碰盘）；
 *   · 它们真正的"保存"是**连整块 `hotkey` 原样带着走**（见上条 `Hotkeyed` 那一行）。
 *
 * 【冲突怎么报】
 *
 * 两条热键**逐词相同**（拆开后词、序都一样）算撞车，报 `MalilibConflict`。★ 只认"逐词相同"
 * 这一种：真数据里修饰键永远写在前头（`LEFT_ALT,Z`），所以同一个键位在两个文件里就是**同一个
 * 串**；按"集合相同"去比会把 `Z,LEFT_ALT` 也算成撞车，而我们没有任何证据说这一族认这个写法
 * —— 报给用户的每一条冲突都得是说得死的（同 `optionsConflicts()` 那条立场）。
 */

#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

#include "PresetTypes.hpp"

namespace Presets {

// ---------------------------------------------------------------------------
// 认这一族：看结构，不看名字
// ---------------------------------------------------------------------------

/// 轻量版格式检查：`includes("config_version")` 能容忍，但 `{"foo": {"keys": 1}}` 不算。
struct MalilibFileFacts {
    /// 结构上像 MaLiLib 那一族（判据见文件头最后一段）。
    bool recognized = false;
    /// 顶层那几大格（`Generic` / `TweakHotkeys` ……）。`config_version` 那种非对象的**不算**
    /// 一格，但它照样原样留着。
    int categories = 0;
    /// `{"keys": "…"}` 那一档有几格。
    int hotkeys = 0;
    /// `{"enabled": …, "hotkey": {…}}` 那一档有几格。
    int hotkeyedValues = 0;
    /// 别的值有几格（认得出这一族、但这一遍只看热键时用得上）。
    int values = 0;
    /// 读不动 / 认不出这一族时的那句人话（`recognized == false` 时一定有）。**原样能显示**。
    QString problem;
};

MalilibFileFacts malilibInspect(const QByteArray &bytes);

/// 这一份字节是不是 MaLiLib 那一族的配置（= `malilibInspect().recognized`）。
/// ★ 通用文件组按它把"有结构适配器"的那几份标出来（见 `genericItemIsCopyOnly()`）。
bool malilibLooksLikeConfig(const QByteArray &bytes);

// ---------------------------------------------------------------------------
// 键位那一张（§6 P4 的 DTO：只用于展示和冲突分析）
// ---------------------------------------------------------------------------

/**
 * @brief 一组键位（§6 P4 点名的那张 DTO）。
 *
 * ★ `keys` 是**原样的词**（`LEFT_ALT` / `BUTTON_3` / `KP_MULTIPLY`）：这一层没有一张
 *   "哪个词是哪个物理键"的表，也**不编**一张（§6 P4 要的是把用户的绑定原样搬过去，不是翻译）。
 *   界面上怎么把它画得好看归 `ui/`。
 *
 * ★ `trigger` / `context` / 那三个布尔是本机真数据里**一次都没出现过**的字段（见文件头）——
 *   认得出就填、认不出就空着/默认值，**永远不写回去**。
 */
struct KeyGesture {
    QStringList keys;      ///< 原样的词，**保序**
    QString trigger;       ///< 激活方式（`press` / `release` / …）—— 没写就是空
    QString context;       ///< 作用上下文（`ingame` / `gui` / …）—— 没写就是空
    bool orderSensitive = false;
    bool allowExtraKeys = false;
    bool exclusive = false;

    /// 一个词都没有（= 没绑）。
    bool isEmpty() const { return keys.isEmpty(); }
    /// 盘上那一格的写法（`"A,C"`）；没绑就是空串。★ 拼法只有这一处。
    QString toKeysString() const;
    /// 从盘上那一格读（空串 ⇒ 空绑定）。
    static KeyGesture fromKeysString(const QString &text);
};

/// 一格热键对象（`{"keys": "…", …}`）→ DTO。
KeyGesture keyGestureFromHotkeyObject(const QJsonObject &hotkey);

/// 两张 DTO 是不是**逐词相同**（冲突的判据，见文件头最后一段）。空绑定之间**不算**相同。
bool keyGesturesEqual(const KeyGesture &left, const KeyGesture &right);

// ---------------------------------------------------------------------------
// 一格：这一层认得的三档
// ---------------------------------------------------------------------------

/// 这一格是什么（见文件头那段"扇区文件里那一串"）。
enum class MalilibEntryKind {
    Hotkey,    ///< 整格就是热键：`{"keys": "A,C"}`
    Hotkeyed,  ///< 一格装两样：`{"enabled": true, "hotkey": {"keys": "A,C"}}`
    Value,     ///< 别的值（数字 / 布尔 / 字符串 / 数组 / 不是热键的对象）
};

/// 落盘 / 界面翻译用的稳定键。
QString malilibEntryKindKey(MalilibEntryKind kind);
bool malilibEntryKindFromKey(const QString &key, MalilibEntryKind *out);

/// 它从哪儿来（§6 P4 最后一条「保存来源 Minecraft、加载器、模组和 MaLiLib 版本」）。
///
/// ★ **不知道的字段一个都不写**（空串 ⇒ 那一格根本不出现在 JSON 里）：
///   "我们没记上"和"记的是空串"是两件事，而前者才是真的（§8.1 那条同样的取舍）。
/// ★ 这几个值**由调用方给**（它知道那个实例是什么版本装的）：这一层不自己去猜 ——
///   模组版本得读 `mods/*.jar` 里的元数据，那是另一件事，没做就不写。
struct MalilibSourceVersions {
    QString minecraft;
    QString loader;
    QString mod;
    QString malilib;

    bool isEmpty() const {
        return minecraft.isEmpty() && loader.isEmpty() && mod.isEmpty() && malilib.isEmpty();
    }
    QJsonObject toJson() const;
    static MalilibSourceVersions fromJson(const QJsonObject &object);
};

/**
 * @brief 一格（= `config/<modId>.json` 里 `<groupId>.<optionName>` 那一格）。
 *
 * ★ 身份是 **`modId` / `groupId` / `optionName`**（§6 P4 点名的那个三元组）—— 不是翻译名、
 *   不是行号、不是下标。目标那一格就是按这三样找的。
 */
struct MalilibEntry {
    QString modId;       ///< `config/<modId>.json` 那个 `<modId>`（真数据里就是文件名的前半截）
    QString groupId;     ///< 顶层那一大格（`Generic` / `TweakHotkeys` / `Hotkeys` ……）
    QString optionName;  ///< 那一格的名字

    MalilibEntryKind kind = MalilibEntryKind::Hotkey;
    KeyGesture gesture;                  ///< `Hotkey` / `Hotkeyed` 才有
    QJsonValue value;                    ///< `Value` 才有（原样）
    /// `Hotkeyed` 那一档的开关值。★ **只有用户勾了"连开关一起搬"才有**（默认没有）——
    /// 于是"这一趟要不要动 `enabled`"这件事就写在载荷里，不必再给一个模式开关。
    bool hasEnabled = false;
    bool enabled = false;
    /// 这一格在文件里**除了上面认得的那几样以外**还带着什么（原样）。
    /// ★ 合并时按"只补缺、不覆盖"写过去（见文件头）。
    QJsonObject extra;
    /// 从哪个实例纳来的（可空）。
    MalilibSourceVersions source;

    /// 给人和报告看的那一行标签（`litematica/Generic/render`）。**唯一出处**。
    QString label() const;
};

// ---------------------------------------------------------------------------
// 文件 / 段 ⇄ 一格一格
// ---------------------------------------------------------------------------

/// `config/<modId>.json` 相对实例根的路径。★ 拼法只有这一处。
QString malilibConfigRelativePath(const QString &modId);

/// `modId` 能不能当文件名用（空串 = 行；否则是给用户看的一句原因）。
/// 用的是实例那一套路径段判据（`InstanceFiles::validatePathSegment()`），不另立一套。
QString malilibModIdProblem(const QString &modId);

/**
 * @brief 一份字节 → 一格一格。
 *
 * @param includeValues `false` = 只要热键那两档（§6 P4「**默认**仅合并热键」）。
 * @param problemOut 读不动时的一句人话（**认不出这一族**也走这儿：那不是"空文件"，是
 *        "我们不知道拿它怎么办"）。
 *
 * ★ "什么都不说的格子"**不收**（`{"keys": ""}` 那种没绑的空格子，还有除它以外什么都没带的
 *   格子）：它们搬过去只会解绑目标上那些绑好的键（见文件头）。
 */
bool malilibEntriesFromFile(const QString &modId, const QByteArray &bytes,
                            QList<MalilibEntry> *out, QString *problemOut = nullptr,
                            bool includeValues = false);

/// 载荷形状：`items` = 一格一个对象（**保序**）——
/// `{"mod": …, "group": …, "option": …, "type": "hotkey"|"hotkeyed"|"value",
///   "keys": ["LEFT_ALT","Z"], "enabled": …, "value": …, "extra": {…}, "source": {…}}`。
/// 空的那几样（没绑的 `keys`、假的三个布尔、空的 `extra`）**根本不写**。
///
/// ★ 一段 = **一个模组**（段的身份是 `sectionIdFor(MalilibKeys, modId)`，见 `malilibSectionId()`）。
///   一个模组一个文件，一段正好对上那一份文件 —— 于是"这一段落到哪儿"这一个问题没有第二种说法。
QList<PresetSection> sectionsFromMalilibEntries(const QList<MalilibEntry> &entries, ApplyMode mode);

/// 反过来：从段里把一格一格读回来。`ok = false` 的来路：`kind` 不归这一层管 / 项不是对象 /
/// `mod` / `group` / `option` / `type` 缺了、不是字符串、认不出来 / `modId` 当不了文件名 /
/// 同一段里出现了**两个模组**（一段=一个文件，这是它自己的规矩）/ **段 id 和里面的模组名不是
/// 一对**。读不回来的段**不许拿去改用户的实例**。
bool malilibEntriesFromSections(const QList<PresetSection> &sections, QList<MalilibEntry> *out,
                                QString *problemOut = nullptr);

/// 段自己的稳定 id（`sectionIdFor(SectionKind::MalilibKeys, modId)`）。
///
/// ★ `sectionIdFor()` 是 UUID v5 —— **单向**：算得出来、反推不回去。所以模组名写在**载荷**里
///   （每一项的 `mod`），段的 id 只用来"写的时候按模组名算一遍、读回来再算一遍对一次"。
QString malilibSectionId(const QString &modId);

/// 这一段的种类归不归这一层管（`MalilibKeys`）。应用那一层按它分派。
bool malilibHandlesSection(SectionKind kind);

/// 那一份配置里有哪些模组（`config/` 底下**结构上认得出来**的那些，按文件名字典序）。
/// 读不动、或者名字当不了文件的，逐个报在 @p skippedOut 里。
/// ★ **认不出来**的（`config/` 底下别的模组自己的 json）不进这个表、也**不算问题**：
///   那些本来就不归这一层管，一家一家报出来只会把真读不动的那几份淹掉。
QStringList malilibModIdsInInstance(const QString &instanceRoot,
                                    QStringList *skippedOut = nullptr);

// ---------------------------------------------------------------------------
// 冲突
// ---------------------------------------------------------------------------

/// 两条（或更多）热键抢同一个键位组合。
struct MalilibConflict {
    /// 那个一模一样的键位串（`LEFT_ALT,Z`）。
    QString keys;
    /// 抢它的那几条的标签（`litematica/Generic/render` 这种，≥2 条）。
    QStringList claims;

    int claimedBy() const { return static_cast<int>(claims.size()); }
};

/// 这一堆里谁跟谁撞车（空绑定不参与）。★ 调用方把"预设里要写的"和"目标上会被抢的那些"
/// **合成一个清单**递进来，跨模组那一层也就自动算进去了（真数据里两个模组绑同一个键位是
/// 常事，而它们落在两个文件里）。
QList<MalilibConflict> malilibKeyConflicts(const QList<MalilibEntry> &entries);

// ---------------------------------------------------------------------------
// 计划与执行
// ---------------------------------------------------------------------------

/// 目标那一侧的一份文件（**由调用方读好递进来**，同 `TargetSnapshot` 那条规矩）。
struct MalilibTargetFile {
    QString modId;
    bool present = false;
    /// 盘上那一份的**原始字节**（`present` 时才有意义）。
    QByteArray bytes;
    /// 有、但读不动 / 认不出这一族时的那句话（非空 ⇒ 这个模组这一段这一趟做不了）。
    QString problem;
};

struct MalilibTarget {
    QList<MalilibTargetFile> files;

    const MalilibTargetFile *find(const QString &modId) const;
};

/// 读目标那一侧的那几份（`<instanceRoot>/config/<modId>.json`）。@p modIds 空 = 一个都不读。
/// ★ 读不动的**照样进清单**（带 `problem`）："读不出来"和"没有这一份"是两件事，前者不许
///   被当成后者（那会让「缺了才拷」照着一个读不动的文件往下写）。
/// ★ @p problemOut 只有一种情况非空：没给实例目录（那会儿 `config/x.json` 会变成相对路径，
///   读写的都是当前目录 —— 整趟不办）。
MalilibTarget malilibReadTarget(const QString &instanceRoot, const QStringList &modIds,
                                QString *problemOut = nullptr);

/// 这一趟会做什么（界面按它摆「MaLiLib 热键 合并 12 项，跳过 2 项」，见 §7）。
struct MalilibApplyReport {
    bool ok = false;
    /// 走到「覆盖」而用户还没明确确认过（同 §6 P2 那条：覆盖必须明确确认）。
    bool needsConfirm = false;
    QString problem;

    int written = 0;       ///< 这一趟放行、要写进去的项数（**含"本来就是那样"的那些**，见下）
    int added = 0;         ///< 其中目标上原来没有、要添的
    int unchanged = 0;     ///< 其中目标上本来就是那个值的（照样算 `written`，和原版设置那边同一条）
    int removed = 0;       ///< 「覆盖」档要解绑的目标侧热键数
    int skipped = 0;       ///< 预设里有、这一趟不动的
    int typeMismatch = 0;  ///< 类型不符、**拒绝转换**的那些（§6 P4 点名）
    /// 「覆盖」档**没动**的那些格子（值那一格、还有认不出来是什么的）—— 覆盖只解绑热键，
    /// 一个值都不删。★ 这个数**只在覆盖档填**（别的档一个都不删，报出来只会让人以为我们在
    /// 权衡什么），和 `OptionsApplyReport::untouchable` 同一条。
    int untouchable = 0;

    QList<MalilibConflict> conflicts;
    /// 逐条的人话（类型不符 / 读不动 / 认不出——都在这儿，**原样能显示**）。
    QStringList details;

    bool hasConflicts() const { return !conflicts.isEmpty(); }
};

/// 一个文件的新内容（`changed == false` 的那些不必写盘）。
struct MalilibFileResult {
    QString modId;
    QByteArray bytes;
    bool changed = false;
};

/// 计划 + 新内容一起给（`report.ok` 为假时 `files` 是空的，盘上什么都没变）。
struct MalilibApplyResult {
    MalilibApplyReport report;
    QList<MalilibFileResult> files;
    /**
     * @brief 这一趟**之后**那几份文件里绑着的那些格子（值那一档不在里面）。
     *
     * ★ 它存在是为了**跨模组**的冲突分析：`report.conflicts` 算的是**这一趟交给它的那几份**
     *   文件（调用方一次只给一个模组的段，于是那只看得见"同一份文件里两格抢同一组键位"）。
     *   而真正常见的那一种抢法恰恰是**两个模组各占一个**（`GenericHotkeys.OpenConfigGui` 和
     *   `litematica.Hotkeys.something` 都绑 `M,C`）—— 要把那一种算出来，就得把各趟算完**之后**
     *   的格子攒到一处再算一遍（`PresetPlanner` 就是这么干的）。
     */
    QList<MalilibEntry> after;
};

/**
 * @brief 算出"把预设这一层落到目标上"之后的每一份文件（**不碰盘**）。
 *
 * 各档语义见文件头那张表。`report.ok = false` 只有**一条**来路：走到「覆盖」而没确认过
 * （`needsConfirm` 也置上，那会儿一个字节都不动）。
 *
 * ★ 单个模组那一边读不动 / 目标上这一份认不出来**不拦着别的模组**：那一份整个跳过、记进
 *   `skipped` 并在 `details` 里说清楚，`ok` 仍为真（和 `PresetPlanner.cpp` 那条"做不了的段
 *   不拦着别的段"同一个立场）。
 */
MalilibApplyResult applyMalilibEntries(const QList<MalilibEntry> &entries,
                                       const MalilibTarget &target, ApplyMode mode,
                                       bool replaceConfirmed = false);

/// 只要计划（= `applyMalilibEntries()` 的 `report`）。存在的意义是调用顺序只有一处。
MalilibApplyReport planMalilibApply(const QList<MalilibEntry> &entries,
                                    const MalilibTarget &target, ApplyMode mode,
                                    bool replaceConfirmed = false);

/**
 * @brief 真落盘：读目标 → 算 → 逐份 `QSaveFile` 原子替换。
 *
 * ★ 只写**内容真的变了的**那几份（`changed`）：这一族的文件里放着用户几百个开关，一次
 *   "什么都没改"的重写没有必要，而重写会动它的 mtime（模组那边有按 mtime 判"要不要重读"
 *   的做法）。★ 一份里第 2 个模组写失败就当场收手，前一份留着（和 `applyFileGroup()` 一样，
 *   整趟回滚是 §8.1 那一层备份的事）。
 */
MalilibApplyReport applyMalilibToInstance(const QString &instanceRoot,
                                          const QList<MalilibEntry> &entries, ApplyMode mode,
                                          bool replaceConfirmed = false);

// ---------------------------------------------------------------------------
// 提取：实例 → 预设
// ---------------------------------------------------------------------------

struct MalilibExtractRequest {
    /// `versions/<实例 id>`（`config/` 在它底下）。
    QString instanceRoot;
    /// 要收哪几个模组。★ 空 = **扫 `config/` 下所有认得出来的**（结构识别，见文件头）。
    QStringList modIds;
    /// 连开关和别的值一起收（§6 P4「可选合并全部已知设置」）。默认**只要热键**。
    bool includeValues = false;
    /// 收出来的那几段**记哪一档**（§6 P4 那四条："默认合并" / "完整覆盖" / "缺失时复制"）。
    /// ★ 这一档写进段里、由应用那一趟照着办 —— 提取这一趟不写盘，所以它不是"现在就覆盖"。
    ApplyMode mode = ApplyMode::Merge;
    /// 记进载荷的来源版本（不知道就空着，见 `MalilibSourceVersions`）。
    MalilibSourceVersions versions;
};

struct MalilibExtractReport {
    bool ok = false;
    QString problem;
    QList<PresetSection> sections;
    int files = 0;          ///< 收了几个模组
    int hotkeys = 0;        ///< 其中 `Hotkey` 那几格（**没绑的那些不收**，见文件头）
    int hotkeyedValues = 0; ///< `Hotkeyed` 那几格
    int values = 0;         ///< `Value` 那几格（`includeValues` 关着时必然是 0）
    /// 读不动 / 认不出来 / 这一份里没东西可收的那些（带一句原因）。原样能显示。
    QStringList skipped;
};

/**
 * @brief 把实例上的这一族收成预设的几段（一个模组一段）。
 *
 * ★ 一个模组**一段**、段里那些格子保序（`Hotkey` 在天生只有它自己那一档的扇区里就是文件
 *   本来的次序）。
 */
MalilibExtractReport extractMalilibSections(const MalilibExtractRequest &request);

}  // namespace Presets
