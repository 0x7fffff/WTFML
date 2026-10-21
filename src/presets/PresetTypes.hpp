/**
 * @file PresetTypes.hpp
 * @brief 实例预设的**值**那一层（coding plan §4–§8、里程碑 M1 的 P1）。
 *
 * 【一份预设长什么样】
 *
 * 一份预设 = 一份 `PresetManifest`：一个 UUID（磁盘目录名）、一个显示名、一句话说明，
 * 外加一串**段**（`PresetSection`）—— 游戏设置、原版按键、服务器列表、通用文件组……
 * 每一段自己带着 `mode`（合并 / 覆盖 / 追加 / 缺了才拷 / 跳过，见 §5 的 `ApplyMode`）
 * 和一小块载荷 `items`。
 *
 * **这一层不解释 `items` 里是什么。** 那是各个**适配器**的活（P2 的
 * `VanillaOptionsAdapter` 管 `options.txt` 那一段、P3 的 `ServerListAdapter` 管
 * `servers.dat` 那一段……见 §5 那张图）。把"怎么解释"提前搬进这一层，等于一开始就把
 * 四个适配器焊死在一个文件里；而 §5 那张图存在的全部理由就是让它们各活各的。
 *
 * 【为什么先立值、再立盘】
 *
 * §6 的 P1 要把预设做成**先把账做对**的东西，而"做对"的四条全是值层和盘层的事，跟界面
 * 无关：① id 用 UUID（**显示名不当路径**）；② 一份坏了不许连坐别的；③ 写盘一律原子替换；
 * ④ 读不回来的段不许拿去改用户的实例。所以这一份（连同 `PresetStore`）先落地，管理页那一
 * 半等 P1 的界面上线时再接 —— 界面只拿 `PresetSummary` 摆字。
 *
 * 【三层格式标记】
 *
 * 落盘的东西有两种：**下标**（`presets/index.json`，管顺序和那个唯一的默认项）和
 * **单份**（`presets/<id>/manifest.json`）。两份各有各的格式标记，认不出来就整份不认 ——
 * 和 `LaunchHistory::formatTag()` / `BackupFiles::formatTag()` 同一套：以后改格式时旧文件
 * 在这里被挡住，而不是半信半疑地读成一堆空壳。
 */

#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUuid>

namespace Presets {

/// 落盘的 schema 版本。★ 加在**每一份**里（下标和单份都有），读的时候比一次：
/// 比这个数大的我们读不懂（那是以后版本的启动器写的），**整份不认**而不是猜着读。
///
/// NOTE: 它和格式标记是两件事，两个都要留 —— 标记回答"这是不是我们这一类文件"，版本回答
/// "是哪一代的我们"。改动字段含义时**只动这个数**（§2.6 那条同样的取舍）。
/// 常数和那个函数**两个名字**是必须的：`PresetManifest` 里有一个同名字段
/// （`int schemaVersion`），拿函数给它做默认值的话，类作用域里的名字查找会先撞上那个
/// 字段本身（`= schemaVersion()` 于是成了"拿一个 int 去调用"），而这是编译期就报的。
constexpr int kSchemaVersion = 1;

inline int schemaVersion() {
    return kSchemaVersion;
}

/// 单份的格式标记（`presets/<id>/manifest.json` 顶上那一位）。
inline QString manifestFormatTag() {
    return QStringLiteral("wtfml-preset/1");
}

/// 下标的格式标记（`presets/index.json` 顶上那一位）。
inline QString indexFormatTag() {
    return QStringLiteral("wtfml-presets-index/1");
}

// ---------------------------------------------------------------------------
// 落盘用的词（枚举 ⇄ 字符串）
//
// 这几张表是**唯一出处**：写和读都走它。改一个字要一起改 —— 单靠"读的那边认不出来就
// 落回默认档"，一个拼错的选择器在写侧是**不报错**的（症状是"这一段的模式每次读完都
// 变回合并"）。
// ---------------------------------------------------------------------------

/**
 * @brief 一段预设**怎么落到**实例上（§5）。
 *
 * 默认是 `Merge`：P2 点名的「基础设置与 `key_` 按键分组选择；**默认 Merge**，Replace 必须
 * 明确确认」。所以不认识的词也落到这一档 —— 那是四个里最不容易弄坏用户东西的一个。
 */
enum class ApplyMode {
    Merge,          ///< 合并（保留目标上已有的、只动这一段管得着的那些）
    Replace,        ///< 覆盖（目标上那一样整个换成预设里的）—— **必须用户明确确认**
    Append,         ///< 追加（服务器列表那种"添进去"，不是替换）
    CopyIfMissing,  ///< 目标上没有才拷（配置文件那种"别动用户改过的"）
    Skip,           ///< 这一段这一趟不动
};

/// 一份预设**属于哪一类**（coding plan §4 / §6.0 P0）。
///
/// 首版四类独立创建、展示和管理；**不再保存"跨类总预设"**。旧版本写的那些混合 manifest
/// 读回来落进 `Unset`（界面上叫「旧版（混合）」）—— 它们照样能看、能用、能提取，只是
/// 新建的那一档必须从四类里挑一个。
///
/// 这一位是**加在 v1 上的可选字段**，没有动 `schemaVersion`：旧启动器读到新的 manifest
/// 只会无视 `category`（照旧当混合预设处理），不会因为"版本比认识的新"整份拒绝。
/// 加字段而不是换版本，是为了"旧数据可恢复"那一侧 —— 用户回退一版启动器时，新做的预设
/// 还在，只是不认类别。`Unset` **不写下去**（缺这一位就是旧数据）。
enum class PresetCategory {
    Unset,         ///< 没写类别（旧版混合预设 / 从旧预设复制出来的）
    GameSettings,  ///< 游戏设置：选项、按键、热键，以及 config 一类高级内容
    Mods,          ///< 模组
    ResourcePacks, ///< 资源包（含光影包）
    DataPacks,     ///< 数据包（世界级）
};

inline QString presetCategoryKey(PresetCategory category) {
    switch (category) {
    case PresetCategory::Unset:
        return QString();
    case PresetCategory::GameSettings:
        return QStringLiteral("gameSettings");
    case PresetCategory::Mods:
        return QStringLiteral("mods");
    case PresetCategory::ResourcePacks:
        return QStringLiteral("resourcePacks");
    case PresetCategory::DataPacks:
        return QStringLiteral("dataPacks");
    }
    return QString();
}

/// 认不出来的词 ⇒ `Unset` + `ok = false`：那是以后版本的启动器写的类别，我们不知道它
/// 那一类的载荷该怎么解释。★ 缺这一位**不算错**（旧版 manifest 就是这样）。
inline PresetCategory presetCategoryFromKey(const QString &key, bool *ok = nullptr) {
    if (ok != nullptr) {
        *ok = true;
    }
    if (key.isEmpty()) {
        return PresetCategory::Unset;
    }
    if (key == QLatin1String("gameSettings")) {
        return PresetCategory::GameSettings;
    }
    if (key == QLatin1String("mods")) {
        return PresetCategory::Mods;
    }
    if (key == QLatin1String("resourcePacks")) {
        return PresetCategory::ResourcePacks;
    }
    if (key == QLatin1String("dataPacks")) {
        return PresetCategory::DataPacks;
    }
    if (ok != nullptr) {
        *ok = false;
    }
    return PresetCategory::Unset;
}

/**
 * @brief 平台名归一（`Modrinth` / `mr` / `curseforge` / `CF` 都收）。
 *
 * 认不出来 ⇒ 空串。★ 这一层**只归一大小写和两种别名**，不猜"可能是哪个平台" ——
 * 猜错的后果是拿一个项目的 id 去另一个平台查，然后把"查无此项"报成"缺失"。
 */
inline QString normalizeModPlatform(const QString &raw) {
    const QString value = raw.trimmed().toLower();
    if (value == QLatin1String("modrinth") || value == QLatin1String("mr")) {
        return QStringLiteral("modrinth");
    }
    if (value == QLatin1String("curseforge") || value == QLatin1String("cf")) {
        return QStringLiteral("curseforge");
    }
    return QString();
}

/**
 * @brief 一条**模组引用**（coding plan §6.0 P2 的检查对象，§5「目标资源条目」的前两格）。
 *
 * 身份是 `platform + projectId`，**不是显示名**（§5 那句「不以展示名称作为项目身份」）。
 * `name` 只是快照时记下来给界面兜底用的；改名、翻译、重名都不影响这一条是谁。
 *
 * `gameVersion` / `loader` 非空 = 这一项自己钉死了版本/加载器；空 = 跟着这一次安装走。
 * 钉死的那种在**本地**就能判（§7.2「先做已知约束筛选」），不必等网络。
 */
struct ModReference {
    /// `normalizeModPlatform()` 之后的词（`modrinth` / `curseforge`）；空 = 认不出来。
    QString platform;
    /// 平台上的稳定项目 id。★ **一律字符串**（§8.1「大整数 ID 保存为字符串」）。
    QString projectId;
    /// 快照时的显示名（只作提示；不作身份、不参与去重）。
    QString name;
    /// 必要项（缺失 ⇒ 安装结果要明确标出来）。默认是。
    bool required = true;
    /// 这一项自己钉死的游戏版本（空 = 跟随安装上下文）。
    QString gameVersion;
    /// 这一项自己钉死的加载器（空 = 跟随安装上下文）。
    QString loader;

    /// 查询 / 缓存的键。空串 = 这一条本地就判"引用无法识别"。
    /// 平台名在这里也过一遍归一：内存里手搭的引用可能是 `Modrinth`，而缓存 / 去重键
    /// 必须只有一种写法（否则同一项目会被问两遍、缓存也命不中）。
    QString key() const {
        const QString normalized = normalizeModPlatform(platform);
        if (normalized.isEmpty() || projectId.isEmpty()) {
            return QString();
        }
        return normalized + QLatin1Char(':') + projectId;
    }
};

inline QJsonObject modReferenceToJson(const ModReference &reference) {
    QJsonObject object;
    // 下面两格**总是**写（哪怕空）：「缺这一位」和「写了但认不出来」在检查那一侧是
    // 两种结论（前者是坏引用，后者也是），写出来才能让用户看见是哪一格坏。
    object.insert(QStringLiteral("platform"), reference.platform);
    object.insert(QStringLiteral("projectId"), reference.projectId);
    if (!reference.name.isEmpty()) {
        object.insert(QStringLiteral("name"), reference.name);
    }
    if (!reference.required) {
        object.insert(QStringLiteral("required"), false);
    }
    if (!reference.gameVersion.isEmpty()) {
        object.insert(QStringLiteral("gameVersion"), reference.gameVersion);
    }
    if (!reference.loader.isEmpty()) {
        object.insert(QStringLiteral("loader"), reference.loader);
    }
    return object;
}

/// 读一条引用。**不返回失败**：认不出来的字段留空，由检查层判 `Invalid` 并如实计数 ——
/// 一条坏引用不该让整份预设读不回来（§8.1「区分缺失、`null` 与错误类型」）。
inline ModReference modReferenceFromJson(const QJsonObject &object) {
    ModReference reference;
    reference.platform = normalizeModPlatform(object.value(QStringLiteral("platform")).toString());
    reference.projectId = object.value(QStringLiteral("projectId")).toString().trimmed();
    reference.name = object.value(QStringLiteral("name")).toString();
    if (object.contains(QStringLiteral("required"))) {
        // 写了但不是 bool ⇒ 按"必要"算（默认那一档最保守：不会把必要项静默降级成可选）。
        reference.required = object.value(QStringLiteral("required")).toBool(true);
    }
    reference.gameVersion = object.value(QStringLiteral("gameVersion")).toString().trimmed();
    reference.loader = object.value(QStringLiteral("loader")).toString().trimmed();
    return reference;
}

/// 段的**种类**（谁负责解释 `items`）。
///
/// 认不出来的种类 ⇒ **整份读不回来**（见 `sectionFromJson`）：那是以后版本的启动器写的，
/// 而"读不懂还硬套"的后果是拿一份半懂的东西去改用户的实例。
enum class SectionKind {
    VanillaOptions,  ///< 原版游戏设置（`options.txt`，P2）
    VanillaKeys,     ///< 原版按键（同一个文件里的 `key_*`，P2）
    ServerList,      ///< 服务器列表（`servers.dat`，P3）
    MalilibKeys,     ///< MaLiLib 那一族的热键（P4）
    GenericFiles,    ///< 通用文件组（`config/`、资源包、光影包……，P3）
    Custom,          ///< 以后新加的种类落在这儿之前先得改这一层
};

inline QString applyModeKey(ApplyMode mode) {
    switch (mode) {
    case ApplyMode::Merge:
        return QStringLiteral("merge");
    case ApplyMode::Replace:
        return QStringLiteral("replace");
    case ApplyMode::Append:
        return QStringLiteral("append");
    case ApplyMode::CopyIfMissing:
        return QStringLiteral("copyIfMissing");
    case ApplyMode::Skip:
        return QStringLiteral("skip");
    }
    return QStringLiteral("merge");
}

/// 认不出来的词回 `Merge`，并且把 `ok` 按下去（调用方要判"这一份是不是坏了"时用得上）。
inline ApplyMode applyModeFromKey(const QString &key, bool *ok = nullptr) {
    if (key == QLatin1String("merge")) {
        return ApplyMode::Merge;
    }
    if (key == QLatin1String("replace")) {
        return ApplyMode::Replace;
    }
    if (key == QLatin1String("append")) {
        return ApplyMode::Append;
    }
    if (key == QLatin1String("copyIfMissing")) {
        return ApplyMode::CopyIfMissing;
    }
    if (key == QLatin1String("skip")) {
        return ApplyMode::Skip;
    }
    if (ok != nullptr) {
        *ok = false;
    }
    return ApplyMode::Merge;
}

inline QString sectionKindKey(SectionKind kind) {
    switch (kind) {
    case SectionKind::VanillaOptions:
        return QStringLiteral("vanillaOptions");
    case SectionKind::VanillaKeys:
        return QStringLiteral("vanillaKeys");
    case SectionKind::ServerList:
        return QStringLiteral("serverList");
    case SectionKind::MalilibKeys:
        return QStringLiteral("malilibKeys");
    case SectionKind::GenericFiles:
        return QStringLiteral("genericFiles");
    case SectionKind::Custom:
        return QStringLiteral("custom");
    }
    return QStringLiteral("custom");
}

inline SectionKind sectionKindFromKey(const QString &key, bool *ok = nullptr) {
    if (key == QLatin1String("vanillaOptions")) {
        return SectionKind::VanillaOptions;
    }
    if (key == QLatin1String("vanillaKeys")) {
        return SectionKind::VanillaKeys;
    }
    if (key == QLatin1String("serverList")) {
        return SectionKind::ServerList;
    }
    if (key == QLatin1String("malilibKeys")) {
        return SectionKind::MalilibKeys;
    }
    if (key == QLatin1String("genericFiles")) {
        return SectionKind::GenericFiles;
    }
    if (key == QLatin1String("custom")) {
        return SectionKind::Custom;
    }
    if (ok != nullptr) {
        *ok = false;
    }
    return SectionKind::Custom;
}

/**
 * @brief 段自己的稳定键：由**段的种类**推出来的 UUID v5（命名空间写死）。
 *
 * 定死的，不是每写一次现搓一个：同一段的 id 在两份预设里一样、两次读回来也一样 ——
 * §6 P4 那种"这一段是同一个东西吗"的判断、以及界面上"这一段的身份没变"才有落脚点
 * （`PresetSection::id` 就是干这个的）。
 *
 * 和 `sectionKindKey()` 是**两件事**：后者是给盘上那份 JSON 看的**词**（改一个词就是改
 * 格式），前者是给人和别的段看的**身份**。所以这两个字面量宁可各写一遍 —— 加一个适配器
 * 的时候，只有它自己知道该不该动这一位。
 *
 * 命名空间那个字面量**改了等于所有段的 id 全变一遍**（盘上那些预设的身份会一起失配）。
 * 要动它之前先想清楚：这件事没有回头路。
 *
 * @param subKey 同一个种类底下再分几段时的那半截名字（ 加的：通用文件组那一段是
 * 一个 `SectionKind` 底下四组 —— `config` / `defaultconfigs` / 资源包 / 光影，各自
 * 得有自己的身份）。★ **默认是空** ⇒ 所有老调用点算出来的 id 一个字都不变（空
 * subKey 拼出来的名字就是 `sectionKindKey(kind)` 本身），也就是说这次加参数没有动
 * 盘上任何一份预设的身份。
 */
inline QString sectionIdFor(SectionKind kind, const QString &subKey = QString()) {
    static const QUuid nameSpace(QStringLiteral("7f2a1b3c-4d5e-4f60-8a9b-0c1d2e3f4a5b"));
    const QString name = subKey.isEmpty()
                             ? sectionKindKey(kind)
                             : sectionKindKey(kind) + QLatin1Char('/') + subKey;
    return QUuid::createUuidV5(nameSpace, name).toString(QUuid::WithoutBraces);
}

// ---------------------------------------------------------------------------
// 值
// ---------------------------------------------------------------------------

/// 预设里的一段。
///
/// `items` 是**原样的载荷**：这一层只保证"它是一个数组、写下去读回来一模一样"，至于
/// 里面每一项是什么意思，归那个 `kind` 的适配器。
struct PresetSection {
    /// 段自己的稳定键（UUID）。★ **不是给用户看的**，也不参与冲突判断 —— 冲突判断按
    /// `kind` + 适配器自己的身份（比如 P4 那条 `modId/groupId/optionName`）走，见 §6 P4。
    QString id;
    SectionKind kind = SectionKind::Custom;
    /// 界面上那一段的标题（`游戏设置` / `原版按键`……）。空的由界面按 `kind` 现翻。
    QString title;
    ApplyMode mode = ApplyMode::Merge;
    QJsonArray items;

    /// 这一段有没有东西。★ 空段**照样写下去**（"我这一趟就是要覆盖成空"和"我没有这一段"
    /// 是两件事，见 §8.1「区分缺失、`null` 与错误类型」）。
    bool isEmpty() const { return items.isEmpty(); }
};

/// 一份预设（= 磁盘上的一个目录 + 那个目录里的 `manifest.json`）。
struct PresetManifest {
    /// 落盘时写一遍、读的时候对一次（和目录名不一致 = 这一份被谁动过，见 `PresetStore`）。
    int schemaVersion = kSchemaVersion;

    /// 稳定 UUID。**磁盘目录名就是它** —— §6 P1「ID 使用 UUID，显示名不作为磁盘路径」。
    QString id;
    /// 显示名（用户输入的）。★ 改名**只改这一个**，目录和 id 一个字都不动（§2.6 同一条）。
    QString name;
    QString description;
    QDateTime createdAt;
    QDateTime updatedAt;
    /// 从哪个实例提取来的（`versions/<id>` 那个目录名）。空 = 手建的、或者提取那一路还没记。
    /// 只作参考：那个实例后来改了名 / 被删了，这一份照样用。
    QString sourceInstanceId;
    /// 这一份属于哪一类（见 `PresetCategory`）。`Unset` = 旧版混合预设。
    PresetCategory category = PresetCategory::Unset;
    /// 模组引用（P2 的检查对象；P5 的下载依据）。★ 只写在 `Mods` 那类上
    /// （旧混合预设也能带 —— 见 `PresetStore::categoryMismatch()`）。
    QList<ModReference> modReferences;
    QList<PresetSection> sections;

    /// 拿得出去吗。**只判身份那两格**：载荷坏不坏归 `PresetStore::loadManifest()` 说。
    bool isValid() const { return !id.isEmpty() && !name.isEmpty(); }

    /// 按 `kind` 找一段（没有回 `nullptr`）。适配器接应用计划时按这个取。
    const PresetSection *section(SectionKind kind) const {
        for (const PresetSection &one : sections) {
            if (one.kind == kind) {
                return &one;
            }
        }
        return nullptr;
    }
};

/// 下标：**只管两件事** —— 列表的次序、和那个唯一的默认项（§5）。
///
/// 这里**故意不存草稿**（名字、说明、段数一个都不在这儿）：一份预设的全部内容只写在它
/// 自己的 `manifest.json` 里。存两份的后果不是"省一次读盘"，而是**两份会漂**（§2.8 那
/// 条同样的取舍：不新增 `quick_launch.json` 那样的第二本账）。
struct PresetIndex {
    int schemaVersion = kSchemaVersion;
    /// 列表顺序 = 用户排的顺序。里面只放**合法 UUID**。
    QStringList order;
    /// 那个唯一的默认项（§4「将任意一个设为唯一默认项」）。空 = 没有默认项。
    QString defaultPresetId;

    bool contains(const QString &id) const { return order.contains(id); }
};

/// 列表那一栏要的那几样（`PresetStore::list()` 的产物）。
struct PresetSummary {
    QString id;
    QString name;
    QString description;
    QDateTime updatedAt;
    int sectionCount = 0;
    /// 这一份属于哪一类（`Unset` = 旧版混合预设，界面另作标注）。
    PresetCategory category = PresetCategory::Unset;
    /// 是不是那个唯一的默认项。
    bool isDefault = false;
};

/// 读不回来的那一份（`PresetStore::list()` 的第二个产物）。
struct BrokenPreset {
    QString id;
    /// 一句人话（`presets/<id>/manifest.json 读不出来（…）`）。**原样能显示**，界面不再加工。
    QString reason;
};

// ---------------------------------------------------------------------------
// 一段 ⇄ JSON
// ---------------------------------------------------------------------------

/// 段写下去的形状：`{id, kind, title?, mode, items?}`。
///
/// 空的 `title` / `items` **根本不写下去**（不是一个空串、一个空数组）—— 和在
/// `LaunchHistory` 那边同一条：写空串的人会以为"那是个开关"（§8.1）。
inline QJsonObject sectionToJson(const PresetSection &section) {
    QJsonObject object;
    if (!section.id.isEmpty()) {
        object.insert(QStringLiteral("id"), section.id);
    }
    object.insert(QStringLiteral("kind"), sectionKindKey(section.kind));
    if (!section.title.isEmpty()) {
        object.insert(QStringLiteral("title"), section.title);
    }
    object.insert(QStringLiteral("mode"), applyModeKey(section.mode));
    if (!section.items.isEmpty()) {
        object.insert(QStringLiteral("items"), section.items);
    }
    return object;
}

/// 读一段。**认不出来 `kind` ⇒ ok = false**：这一段我们读不懂，而读不懂还硬套的后果是拿
/// 一份半懂的东西去改用户的实例（整份读不回来，见 `manifestFromJson`）。
inline PresetSection sectionFromJson(const QJsonObject &object, bool *ok = nullptr) {
    PresetSection section;
    if (ok != nullptr) {
        *ok = true;
    }
    section.id = object.value(QStringLiteral("id")).toString();

    bool kindOk = true;
    const QString kindKey = object.value(QStringLiteral("kind")).toString();
    section.kind = sectionKindFromKey(kindKey, &kindOk);
    if (!kindOk) {
        // 缺 `kind` 那一位也走这儿（空串认不出来）：两种都是"这一段我们不知道拿它怎么办"。
        if (ok != nullptr) {
            *ok = false;
        }
    }

    section.title = object.value(QStringLiteral("title")).toString();

    bool modeOk = true;
    // `mode` **不在**的时候不能按"认不出来的词"算（那会把一份好端端的预设判坏）：
    // 它是可选字段，缺了就是 `Merge`（默认档），只有"写了但我们不认识"才算错。
    if (object.contains(QStringLiteral("mode"))) {
        section.mode = applyModeFromKey(object.value(QStringLiteral("mode")).toString(), &modeOk);
        if (!modeOk && ok != nullptr) {
            *ok = false;
        }
    }

    if (object.contains(QStringLiteral("items"))) {
        const QJsonValue items = object.value(QStringLiteral("items"));
        if (!items.isArray()) {
            if (ok != nullptr) {
                *ok = false;
            }
        } else {
            section.items = items.toArray();
        }
    }
    return section;
}

// ---------------------------------------------------------------------------
// 一份 ⇄ JSON
// ---------------------------------------------------------------------------

inline QJsonObject manifestToJson(const PresetManifest &manifest) {
    QJsonObject root;
    root.insert(QStringLiteral("format"), manifestFormatTag());
    root.insert(QStringLiteral("schemaVersion"), manifest.schemaVersion);
    root.insert(QStringLiteral("id"), manifest.id);
    root.insert(QStringLiteral("name"), manifest.name);
    if (!manifest.description.isEmpty()) {
        root.insert(QStringLiteral("description"), manifest.description);
    }
    // 时间**总是**写（哪怕无效那一位也写一个空串）："字段不在"和"写了但认不出来"少一层
    // 歧义，读的人一眼看得出哪一份的时间我们没记上。
    root.insert(QStringLiteral("createdAt"), manifest.createdAt.isValid()
                                                 ? manifest.createdAt.toString(Qt::ISODateWithMs)
                                                 : QString());
    root.insert(QStringLiteral("updatedAt"), manifest.updatedAt.isValid()
                                                 ? manifest.updatedAt.toString(Qt::ISODateWithMs)
                                                 : QString());
    if (!manifest.sourceInstanceId.isEmpty()) {
        root.insert(QStringLiteral("sourceInstanceId"), manifest.sourceInstanceId);
    }
    // `Unset` 不写：缺这一位就是"旧版混合预设"，和 v1 的盘上形状逐字节兼容。
    if (manifest.category != PresetCategory::Unset) {
        root.insert(QStringLiteral("category"), presetCategoryKey(manifest.category));
    }
    // 模组引用：空**不写**。旧启动器读到这一位会无视它（v1 里没有这个名字）⇒ 回退一版
    // 启动器时那些引用还在盘上，只是不被检查 / 下载。
    if (!manifest.modReferences.isEmpty()) {
        QJsonArray references;
        for (const ModReference &reference : manifest.modReferences) {
            references.append(modReferenceToJson(reference));
        }
        root.insert(QStringLiteral("modReferences"), references);
    }
    QJsonArray sections;
    for (const PresetSection &section : manifest.sections) {
        sections.append(sectionToJson(section));
    }
    root.insert(QStringLiteral("sections"), sections);
    return root;
}

inline QByteArray manifestToBytes(const PresetManifest &manifest) {
    return QJsonDocument(manifestToJson(manifest)).toJson(QJsonDocument::Indented);
}

/**
 * @brief 一段字节 ⇄ 一份预设。
 *
 * `ok = false` 的四种来路（各自的理由都在那一行上）：
 * ① 不是 JSON / 不是对象；
 * ② 顶上那个格式标记不是我们的；
 * ③ `schemaVersion` 比我们认得的大（以后版本写的）；
 * ④ 缺 id / 缺名字，或者某一段的 `kind` / `mode` / `items` 类型不对。
 *
 * 缺 `createdAt` / `updatedAt` **不算坏**（手写一份出来试的情况是存在的），读回来那一位
 * 是无效时间，界面自己决定怎么显示。
 */
inline PresetManifest manifestFromJson(const QByteArray &bytes, bool *ok = nullptr) {
    if (ok != nullptr) {
        *ok = false;
    }
    PresetManifest manifest;
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return manifest;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != manifestFormatTag()) {
        return manifest;
    }
    // 这一位必须是**数字**：写成一个字符串（"1"）在 Qt 里 `toInt()` 会悄悄给出 0，
    // 于是比大小那一步成了"0 > 1 不成立"⇒ 一份以后版本写的预设被当成好的一路读下去。
    if (!root.value(QStringLiteral("schemaVersion")).isDouble()) {
        return manifest;
    }
    const int version = root.value(QStringLiteral("schemaVersion")).toInt();
    if (version <= 0 || version > schemaVersion()) {
        return manifest;
    }
    manifest.schemaVersion = version;
    manifest.id = root.value(QStringLiteral("id")).toString();
    manifest.name = root.value(QStringLiteral("name")).toString();
    manifest.description = root.value(QStringLiteral("description")).toString();
    manifest.createdAt =
        QDateTime::fromString(root.value(QStringLiteral("createdAt")).toString(), Qt::ISODateWithMs);
    manifest.updatedAt =
        QDateTime::fromString(root.value(QStringLiteral("updatedAt")).toString(), Qt::ISODateWithMs);
    manifest.sourceInstanceId = root.value(QStringLiteral("sourceInstanceId")).toString();

    // 类别：**缺这一位 = 旧版混合预设**（`Unset`），不算坏。写了但不认识 = 以后版本的
    // 启动器写的一类我们解释不了 ⇒ 整份不认（和"不认识的段种类"同一条）。
    if (root.contains(QStringLiteral("category"))) {
        bool categoryOk = true;
        manifest.category =
            presetCategoryFromKey(root.value(QStringLiteral("category")).toString(), &categoryOk);
        if (!categoryOk) {
            return PresetManifest();
        }
    }

    // 模组引用：缺这一位 = 没有引用。写了但不是数组 = 这一份的载荷坏了（整份不认）；
    // 数组里某一项不是对象 ⇒ 留成一条空引用（检查层报 `Invalid`），不连坐整份。
    if (root.contains(QStringLiteral("modReferences"))) {
        const QJsonValue references = root.value(QStringLiteral("modReferences"));
        if (!references.isArray()) {
            return PresetManifest();
        }
        for (const QJsonValue &value : references.toArray()) {
            manifest.modReferences.append(
                value.isObject() ? modReferenceFromJson(value.toObject()) : ModReference());
        }
    }

    if (root.contains(QStringLiteral("sections"))) {
        const QJsonValue sections = root.value(QStringLiteral("sections"));
        if (!sections.isArray()) {
            return PresetManifest();
        }
        for (const QJsonValue &value : sections.toArray()) {
            if (!value.isObject()) {
                return PresetManifest();
            }
            bool sectionOk = true;
            const PresetSection section = sectionFromJson(value.toObject(), &sectionOk);
            if (!sectionOk) {
                return PresetManifest();
            }
            manifest.sections.append(section);
        }
    }

    if (!manifest.isValid()) {
        return PresetManifest();
    }
    if (ok != nullptr) {
        *ok = true;
    }
    return manifest;
}

// ---------------------------------------------------------------------------
// 下标 ⇄ JSON
// ---------------------------------------------------------------------------

inline QJsonObject indexToJson(const PresetIndex &index) {
    QJsonObject root;
    root.insert(QStringLiteral("format"), indexFormatTag());
    root.insert(QStringLiteral("schemaVersion"), index.schemaVersion);
    root.insert(QStringLiteral("order"), QJsonArray::fromStringList(index.order));
    if (!index.defaultPresetId.isEmpty()) {
        root.insert(QStringLiteral("default"), index.defaultPresetId);
    }
    return root;
}

inline QByteArray indexToBytes(const PresetIndex &index) {
    return QJsonDocument(indexToJson(index)).toJson(QJsonDocument::Indented);
}

/// 读下标。★ 这一位**故意比单份松**：下标是"顺序 + 默认项"，丢掉它不丢用户的预设本体
/// —— 所以里面的 id 认不出来只是**那一条**被摘掉，整份下标照读（见 `PresetStore::list()`
/// 那一段：读不回来的下标照样能把盘上每一份列出来）。
inline PresetIndex indexFromJson(const QByteArray &bytes, bool *ok = nullptr) {
    if (ok != nullptr) {
        *ok = false;
    }
    PresetIndex index;
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return index;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != indexFormatTag()) {
        return index;
    }
    if (!root.value(QStringLiteral("schemaVersion")).isDouble()) {
        return index;
    }
    const int version = root.value(QStringLiteral("schemaVersion")).toInt();
    if (version <= 0 || version > schemaVersion()) {
        return index;
    }
    index.schemaVersion = version;

    const QJsonValue order = root.value(QStringLiteral("order"));
    if (order.isArray()) {
        for (const QJsonValue &value : order.toArray()) {
            const QString id = value.toString();
            // 重复的 id 只留第一处（顺序表里同一个东西出现两遍，界面就会多出一行）。
            if (!id.isEmpty() && !index.order.contains(id)) {
                index.order.append(id);
            }
        }
    }
    index.defaultPresetId = root.value(QStringLiteral("default")).toString();
    if (ok != nullptr) {
        *ok = true;
    }
    return index;
}

}  // namespace Presets
