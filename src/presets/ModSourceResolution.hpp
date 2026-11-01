/**
 * @file ModSourceResolution.hpp
 * @brief 从实例提取模组清单时，「这一份 jar 是哪个平台上的哪一项」怎么定。
 *
 * 两条反查路各问一次（批量）都发生在调用点：Modrinth 用文件 sha1（`POST /v2/version_files`），
 * CurseForge 用 murmur2 指纹（`POST /v1/fingerprints`）。这一层只做**最后那一步合并** ——
 * 它是个纯函数，判据因此能离线断言，不用为"先看谁"起一次网络。
 *
 * ★ 两个平台都认得出来时**跟 Modrinth 走**：一份预设里一格身份就够用，没有理由为了
 * "更权威"再翻一次。这一句是这一层唯一一句判断，其余都是查表。
 */
#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QtGlobal>

namespace Presets {

/// 本地一个 jar 能拿来问的两把钥匙（`ModpackExport::hashFile()` 那一趟一起算出来的）。
struct LocalModIdentity {
    /// 小写十六进制的 sha1（Modrinth 那一侧的表键）。
    QString sha1;
    /// CurseForge 指纹（murmur2，剔空白后 `1 ^ 长度` 起算）。
    quint32 fingerprint = 0;
    /// false = 没算 / 算不出来。**这时不许拿 0 去查表**：0 是一个合法的指纹值，
    /// 撞上了会把别人的项目认到这一条头上。
    bool fingerprintOk = false;
};

/// 定完的一条。`projectId` 空 = 两个平台都没认出来（编辑页里显成「未知来源（请选择）」）。
struct ResolvedModSource {
    /// `modrinth` / `curseforge` / 空。
    QString platform;
    /// 平台上的稳定 id（CurseForge 是数字串，见「大整数 ID 保存为字符串」）。
    QString projectId;
};

/**
 * @brief 一条本地 jar 对一条结果：先看 sha1，没认出来的才看指纹。
 *
 * @param mods 本地扫出来的 jar，**顺序就是输出顺序**（调用方按名字对齐用）。
 * @param modrinthProjects sha1（小写） → 项目 id。
 * @param curseForgeProjects 指纹 → 项目 id（空串 = 这一条不算）。
 *
 * NOTE: 两张表都是"查不到就不在表里"，不是"不在 = 报错" —— 不认识才是常态
 * （手工改过的包、没传到平台的包）。
 */
inline QList<ResolvedModSource> resolveModSources(
    const QList<LocalModIdentity> &mods, const QHash<QString, QString> &modrinthProjects,
    const QHash<quint32, QString> &curseForgeProjects) {
    QList<ResolvedModSource> out;
    out.reserve(mods.size());
    for (const LocalModIdentity &mod : mods) {
        ResolvedModSource resolved;
        const QString modrinth = modrinthProjects.value(mod.sha1).trimmed();
        if (!modrinth.isEmpty()) {
            resolved.platform = QStringLiteral("modrinth");
            resolved.projectId = modrinth;
        } else if (mod.fingerprintOk) {
            const QString curseForge = curseForgeProjects.value(mod.fingerprint).trimmed();
            if (!curseForge.isEmpty()) {
                resolved.platform = QStringLiteral("curseforge");
                resolved.projectId = curseForge;
            }
        }
        out.append(resolved);
    }
    return out;
}

}  // namespace Presets
