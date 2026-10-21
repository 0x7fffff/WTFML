/**
 * @file ResourcePackPreset.hpp
 * @brief 资源包预设的**启用顺序**那一半。
 *
 * 【文件和启用是两件事】
 *
 * `resourcepacks/` 里有那一份，不等于游戏里**开着**它。开没开、排第几，全在
 * `options.txt` 的 `resourcePacks:` 那一行（JSON 数组，**低优先级在前**，最后一个是
 * 游戏里排最上面的那个）。所以一份资源包预设要真的"还原当时的样子"，光把 zip 拷过去
 * 不够，还得把那一行按当时的顺序写回去。
 *
 * 这一层**只碰那一行**：`manage/ResourcePackOptions.hpp` 已经把"只改最后一条
 * resourcePacks、其余字节原样"的那套规则做完了（内置项不许动、坏 JSON 不写、写锁、
 * 冲突复核、`cache/resource-pack-backups/` 备份）。这里不另写一遍读写器，只负责
 * "预设里记的是什么"与"目标上现在是什么"之间的对应。
 *
 * 提取的时候没读到 `options.txt`（实例还没跑过 / 那份文件读不动）⇒ 这一份预设**没有**
 * 启用信息。应用时照样把文件部署过去，但**一个字都不写进 options.txt**，报告里明说
 * "只部署、没启用" —— 部署成功不能被说成"游戏里已经开了"。
 *
 * 目标那一侧的老版本（`options.txt` 里根本没有 `resourcePacks` 这一行）同样只部署：
 * 给一个它不认识的键写一行下去，游戏会不会理它我们不知道，所以不冒充"启用成功"。
 */

#pragma once

#include <QString>
#include <QStringList>

#include "PresetTypes.hpp"

namespace Presets {

/// 提取时从实例读到的启用状态。
struct ResourcePackOrderState {
    /// `options.txt` 里有可解析的 `resourcePacks` 行。
    bool hasList = false;
    /// options.txt 里的顺序（**低优先级在前**），元素是 `file/<文件名>` 那种标识。
    QStringList enabledOrder;
    /// 那一行读不动时的原因（非空 ⇒ 这一趟没有启用信息，只部署）。
    QString problem;
};

/**
 * @brief 读一个实例的资源包启用状态（**只读**）。
 *
 * `options.txt` 不在 ⇒ `hasList = false`、`problem` 为空（新实例最正常的样子）。
 * 文件在但那一行坏了 ⇒ `problem` 非空（调用方据此说"只部署、不启用"）。
 */
ResourcePackOrderState readResourcePackOrderState(const QString &instanceRoot);

/**
 * @brief 把启用状态写进资源包那一段的载荷（`enabled` / `priority` 两格）。
 *
 * 每一项只写两格：
 * · `enabled` —— 提取那一刻它在不在 `resourcePacks` 里；
 * · `priority` —— 在的话，它在那一行里的下标（越大优先级越高）。
 *
 * 没有 `hasList` 时**一格都不加**：没读到的顺序不许拿"全是关着"去冒充。
 *
 * @return 真的加进去了没有（= 这一份预设从此带着启用信息）。
 */
bool annotateResourcePackSection(PresetSection &section, const ResourcePackOrderState &state);

/// 这一段里有没有启用信息（每一项都看 `enabled` 那一格）。
bool resourcePackSectionHasOrdering(const PresetSection &section);

// ---------------------------------------------------------------------------
// 应用
// ---------------------------------------------------------------------------

struct ResourcePackOrderReport {
    bool ok = true;
    /// 这一份预设有没有启用信息（没有 ⇒ 只部署、不写 options.txt）。
    bool hasOrdering = false;
    /// 真把 `options.txt` 改写了没有（顺序本来就对 ⇒ false）。
    bool wrote = false;
    int enabled = 0;   ///< 最后被打开的项数（只数预设点名的）
    int disabled = 0;  ///< 被关掉的项数（预设里明写着"当时是关的"）
    QString problem;
    /// 给人看的几句话（原样能显示）。
    QStringList details;
};

/**
 * @brief 按预设把目标实例的 `resourcePacks` 那一行摆好。
 *
 * @param backupRoot `ResourcePackOptions::save()` 自己那份备份的落点（和实例级备份分开：
 * 那一份是"只改这一行"那一路的既有约定）。
 *
 * 规则：
 * · 预设点名 `enabled: true` 的那些 ⇒ 打开；`enabled: false` 的 ⇒ 关掉；
 * · 目标上**没被预设点名**的资源包与内置项 ⇒ 原样保留（原本禁用的继续禁用）；
 * · 打开的预设包按 `priority` 从小到大排在列表末尾（= 比目标上原有的包优先）；
 * · 文件根本没部署上的那些**不打开**，并在 `details` 里点名。
 */
ResourcePackOrderReport applyResourcePackOrdering(const QString &instanceRoot,
                                                  const PresetSection &section,
                                                  const QString &backupRoot);

}  // namespace Presets
