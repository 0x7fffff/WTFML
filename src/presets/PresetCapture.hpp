/**
 * @file PresetCapture.hpp
 * @brief 从实例里**收**一份预设（「从实例提取」、清单的前半截）。
 *
 * 【这一层为什么单独存在】
 *
 * 四个适配器各自知道"自己那一摊怎么收"（`sectionsFromOptions()` /
 * `sectionsFromServerList()` / `sectionsFromFileGroup()` / `extractMalilibSections()`），
 * 但"一次收哪几样、按什么次序、收不出来的时候怎么报"是**一条流程**，不属于任何一个适配器。
 * 摆在这儿以后，管理页和以后"安装时顺便收一份"那条路走的是同一段代码 —— 两边各写一遍
 * 的下场是同一份实例在两处收出两份不一样的预设。
 *
 * 【收的次序是定死的】
 *
 * `options.txt` 的两段 → 服务器列表 → 通用文件组（`config` / `defaultconfigs` / 资源包 /
 * 光影包）→ MaLiLib。★ MaLiLib 排在通用文件组**之后**是有意的：`config/` 那一组会整份
 * 复制 `<实例>/config/` 底下的东西，而 MaLiLib 那几段只是同一些文件里**读得懂的那几格**。
 * 两段都进一份预设时，应用那一趟先落通用文件组、再落 MaLiLib，用户看到的结果就是
 * "原样搬过去，然后把认得出的热键按预设再对一遍"——这正是这种叠加，反过来
 * （先热键后整份覆盖）会把刚写进去的热键又盖回实例原来那一份。
 *
 * 【什么收不进来】
 *
 * 这一层**不读也不写 `manifest.json`**：收出来的是几段值，怎么并进一份预设、名字叫什么、
 * 落盘失败怎么办，全在 `PresetController` 那边。理由和 `PresetTypes` 文件头那段一样：
 * 值层和盘层各活各的。
 */

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "PresetTypes.hpp"

namespace Presets {

// ---------------------------------------------------------------------------
// 收哪几样
// ---------------------------------------------------------------------------

/**
 * @brief 提取那一趟勾了哪几样。
 *
 * 默认值是**产品上那个默认**（「基础设置与 `key_` 按键分组选择」、「通用文件组」
 * 与「默认仅合并热键」）：游戏设置 + 按键 + 服务器 + `config/` + MaLiLib 热键；
 * `defaultconfigs/`、资源包、光影包、MaLiLib 的"值"那几档**默认不勾** —— 它们要么很大
 * （资源包/光影包动辄几百 MB），要么会把用户按机器调好的东西搬走（MaLiLib 的值）。
 */
struct CaptureSelection {
    /// `options.txt` 里非 `key_` 的那些（基础设置 + 硬件那两档里默认选中的）。
    bool vanillaOptions = true;
    /// `options.txt` 里 `key_*` 的那些。
    bool vanillaKeys = true;
    /// `servers.dat`。
    bool servers = true;

    /// 四个通用文件组（`GenericFiles::FileGroup` 一一对应）。
    bool config = true;
    bool defaultConfigs = false;
    bool resourcePacks = false;
    bool shaderPacks = false;

    /// MaLiLib 那一族（`config/<modId>.json` 里认得出的那些）。
    bool malilib = true;
    /// 连开关和别的值一起收（「可选合并全部已知设置」）。默认**只要热键**。
    bool malilibValues = false;

    /// 世界级数据包（`saves/<世界>/datapacks/`）。默认不勾。
    bool dataPacks = false;

    /// 这一趟是不是一样都没勾（界面上那颗按钮该是灰的）。
    bool isEmpty() const {
        return !vanillaOptions && !vanillaKeys && !servers && !config && !defaultConfigs &&
               !resourcePacks && !shaderPacks && !malilib && !dataPacks;
    }
};

// ---------------------------------------------------------------------------
// 收
// ---------------------------------------------------------------------------

struct CaptureRequest {
    /// `versions/<id>` 那个目录名（记进 `PresetManifest::sourceInstanceId`，可空）。
    QString instanceId;
    /// 实例目录（`<数据目录>/versions/<id>`）。
    QString instanceRoot;
    /// 这一份预设的 `files/` 根（`PresetStore::filesDir(root, id)`）。
    QString presetFilesRoot;
    CaptureSelection selection;
};

/**
 * @brief 这一趟收的结果。
 *
 * `sections` 是**值**，不是盘上的东西：`config/` 那几段对应的文件已经被
 * `extractFileGroup()` 拷进 `presetFilesRoot` 了（这是提取唯一会写的盘），而
 * `manifest.json` 一个字都没动 —— 那一步归调用方。
 */
struct CaptureReport {
    /**
     * @brief 这一趟走完了没有。
     *
     * `false` 只有两条来路：请求本身不成立（实例目录不在 / 预设目录建不出来），或者
     * **勾了东西却一段都没收到**。单个来源读不动**不算**整趟失败 —— 那一条进 `problems`，
     * 别的照收（"一份坏掉的 servers.dat 不该让整份预设收不成"和 `PresetPlanner` 那条
     * "做不了的段不拦着别的段"是同一个立场）。
     */
    bool ok = false;
    /// 整趟失败时的一句人话（`ok = false` 时才有）。
    QString problem;

    QList<PresetSection> sections;

    int optionKeys = 0;        ///< 游戏设置那一档收了几项
    int keybindings = 0;       ///< 标准按键那一档收了几项
    int servers = 0;           ///< 服务器列表收了几条
    int configItems = 0;       ///< `config/` 收了几项
    int defaultConfigItems = 0;///< `defaultconfigs/` 收了几项
    int resourcePackItems = 0; ///< `resourcepacks/` 收了几项
    int shaderPackItems = 0;   ///< `shaderpacks/` 收了几项
    int malilibFiles = 0;      ///< MaLiLib 收了几个模组的配置
    int malilibHotkeys = 0;    ///< 其中热键（含 `hotkeyed`）几格
    int malilibValues = 0;     ///< 其中值几格（没勾"连值一起收"时必然是 0）
    int dataPackItems = 0;     ///< 数据包收了几个
    int dataPackWorlds = 0;    ///< 这几个包来自几个存档

    /// 收成了什么 / 哪一样本来是空的（原样能显示）。
    QStringList notes;
    /// 没收到的那些（读不动 / 名字不安全 / 文件是链接……原样能显示）。
    QStringList problems;

    int sectionCount() const { return static_cast<int>(sections.size()); }
    /// 收进来的项数总和（不含通用文件组，那几个在界面上一项一项列不动）。
    int totalItems() const {
        return optionKeys + keybindings + servers + configItems + defaultConfigItems +
               resourcePackItems + shaderPackItems + malilibHotkeys + malilibValues +
               dataPackItems;
    }
};

/// 收一份（**会写盘**：只有通用文件组那几段会往 `presetFilesRoot` 里拷文件）。
CaptureReport captureFromInstance(const CaptureRequest &request);

// ---------------------------------------------------------------------------
// 并进一份已有的预设
// ---------------------------------------------------------------------------

/**
 * @brief 把刚收到的几段并进一份已有的段表。
 *
 * 规矩（**按段的 id 替，不按种类**）：
 * · 新段和旧段 id 相同（= 同一段，见 `sectionIdFor()`）⇒ **就地替换**，位置照旧；
 * · 新段在旧段里找不到 ⇒ 追加在末尾（次序 = `captureFromInstance()` 产出的次序）；
 * · 旧段没有再被收到的那几样 ⇒ **留着不动**。
 *
 * 最后一条是有意的：用户这次只勾了"按键"，不代表他想把上次收的服务器列表从预设里删掉
 * —— 提取是"把这一份现在的样子收下来"，不是"把预设整个重写一遍"。要清掉某一段，得由
 * 用户明确删（管理页那一侧的事），不能靠"这次没勾"来暗地里办。
 */
QList<PresetSection> mergeCapturedSections(const QList<PresetSection> &existing,
                                           const QList<PresetSection> &captured);

}  // namespace Presets
