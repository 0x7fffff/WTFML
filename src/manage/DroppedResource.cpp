/**
 * @file DroppedResource.cpp
 * @brief DroppedResource 的实现（见 .hpp 里那几条判据）。
 */

#include "DroppedResource.hpp"

#include <QFileInfo>

#include "../util/ZipUtil.hpp"
#include "ModpackImport.hpp"

namespace {

/// 条目名归一到"能拿来比"的形状：`\` 当 `/`，整体小写。
///
/// 为什么要转小写：zip 的**条目名是区分大小写的**，而"这个包里有没有 pack.mcmeta"
/// 这件事不该因为作者把文件写成了 `Pack.MCMeta` 就答错 —— 那个包在游戏里照样是资源包。
/// 为什么要管 `\`：zip 规范里条目名只用 `/`，但真实世界里有一批工具（尤其是国内那些
/// "一键压缩"）会写进反斜杠。不去管它的话，一个用反斜杠写的资源包在我们眼里就是
/// "里面什么都没有"。
QString normalized(const QString &entryName) {
    QString name = entryName;
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return name.toLower();
}

/// 这份（已归一的）条目表里有没有 @p segment 这一段目录（传进去的是**不带斜杠**的那一段，
/// 比如 `assets`）。
bool hasSegment(const QStringList &folded, const QString &segment) {
    const QChar slashChar = QLatin1Char('/');
    const QString slash(slashChar);
    for (const QString &entry : folded) {
        // 四种写法都算：`assets/…`、目录条目自己 `assets`（zip 里一条以 `/` 结尾的记录）、
        // 被套在一层目录里的 `MyPack/assets`，以及 `MyPack/assets/…`。
        if (entry == segment || entry.startsWith(segment + slash) || entry.endsWith(slashChar + segment)
            || entry.contains(slash + segment + slash)) {
            return true;
        }
    }
    return false;
}

/// 这份表里有没有这一份文件（按整条名字比）。
bool hasEntry(const QStringList &folded, const QString &name) {
    return folded.contains(name);
}

/// 有没有一个**叫这个名字的文件**（允许它被套在一层目录里：`MyPack/pack.mcmeta`）。
///
/// 为什么要放宽到"套一层也行"：用户手边那种包十有八九是"整个目录一起压"的
/// （GitHub 的 zipball、`右键 → 压缩`），根目录是 `<包名>/…`。只认根目录的话，
/// 那种包会被判成"认不出来"，而它其实一点毛病都没有。
/// NOTE: 反过来（把嵌套也算进去）是安全的：这一条只在**已经排除了整合包**之后才问。
bool hasFileNamed(const QStringList &folded, const QString &fileName) {
    for (const QString &entry : folded) {
        if (entry == fileName || entry.endsWith(QLatin1Char('/') + fileName)) {
            return true;
        }
    }
    return false;
}

}  // namespace

DroppedResource::Result DroppedResource::classify(const QString &path) {
    Result result;
    result.path = path;

    const QFileInfo info(path);
    if (!info.isFile()) {
        // 拖进来的可能是个目录（有人习惯拖文件夹）或者干脆是不存在的东西。两种都算
        // "认不出来"，但**理由不一样**，detail 里说清楚。
        result.detail = info.isDir() ? QStringLiteral("那是一个文件夹，不是文件")
                                     : QStringLiteral("找不到这份文件");
        return result;
    }

    const QString suffix = info.suffix().toLower();
    if (suffix == QLatin1String("jar")) {
        // 见 .hpp 第 1 条：jar 只有一个意思，连盘都不用读。
        result.known = true;
        result.kind = ResourceCatalog::Kind::Mod;
        result.detail = QStringLiteral("后缀是 .jar");
        return result;
    }
    if (suffix != QLatin1String("zip") && suffix != QLatin1String("mrpack")) {
        result.detail = suffix.isEmpty()
                            ? QStringLiteral("它没有后缀名，认不出是哪一类")
                            : QStringLiteral("`.%1` 不是能装进来的东西").arg(suffix);
        return result;
    }

    // ---------- 到这儿都是压缩包：只能看里面有什么 ----------
    QStringList entries;
    QString error;
    const ZipUtil::EntryStatus status =
        ZipUtil::listEntries(path, &entries, &error, QStringLiteral("读这个压缩包"));
    if (status == ZipUtil::EntryStatus::NotAnArchive) {
        result.detail = QStringLiteral("它打不开 —— 不是 zip，也可能文件坏了");
        return result;
    }
    if (status != ZipUtil::EntryStatus::Ok) {
        result.detail = error.isEmpty() ? QStringLiteral("它读不动") : error;
        return result;
    }

    QStringList folded;
    folded.reserve(entries.size());
    for (const QString &entry : entries) {
        folded.append(normalized(entry));
    }

    // ① 整合包：两份清单，谁在根上就是谁（名字只问 ModpackImport，不在这儿再抄一遍）。
    const QString modrinthIndex = ModpackImport::modrinthIndexName().toLower();
    const QString curseForgeManifest = ModpackImport::curseForgeManifestName().toLower();
    if (hasEntry(folded, modrinthIndex)) {
        result.known = true;
        result.kind = ResourceCatalog::Kind::Modpack;
        result.detail = QStringLiteral("里面有一份 %1").arg(ModpackImport::modrinthIndexName());
        return result;
    }
    if (hasEntry(folded, curseForgeManifest)) {
        result.known = true;
        result.kind = ResourceCatalog::Kind::Modpack;
        result.detail =
            QStringLiteral("里面有一份 %1").arg(ModpackImport::curseForgeManifestName());
        return result;
    }

    // ② 资源包 / 数据包：**先说 pack.mcmeta**，再说里面装的是 assets 还是 data。
    if (hasFileNamed(folded, QStringLiteral("pack.mcmeta"))) {
        if (hasSegment(folded, QStringLiteral("assets"))) {
            result.known = true;
            result.kind = ResourceCatalog::Kind::ResourcePack;
            result.detail = QStringLiteral("里面有 pack.mcmeta 和 assets/");
            return result;
        }
        if (hasSegment(folded, QStringLiteral("data"))) {
            result.known = true;
            result.kind = ResourceCatalog::Kind::DataPack;
            result.detail = QStringLiteral("里面有 pack.mcmeta 和 data/");
            return result;
        }
    }

    // ③ 光影：**没有 pack.mcmeta**，只有 shaders/ —— 所以它必须排在资源包那一条之后，
    // 否则资源包里那份 `assets/.../shaders/` 会把资源包认成光影（见 .hpp 那句 ★）。
    if (hasSegment(folded, QStringLiteral("shaders"))) {
        result.known = true;
        result.kind = ResourceCatalog::Kind::Shader;
        result.detail = QStringLiteral("里面有 shaders/");
        return result;
    }

    result.detail = entries.isEmpty()
                        ? QStringLiteral("这个压缩包是空的")
                        : QStringLiteral("里面既没有整合包清单、也没有 pack.mcmeta 或 shaders/，"
                                         "认不出是哪一类");
    return result;
}
