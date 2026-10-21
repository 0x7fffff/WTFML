/**
 * @file InstancePagesController.cpp
 * @brief `InstancePagesController` 的实现。
 *
 * 这一份里的判断全是 `MainWindow` 里那几处**逐字**搬过来的（那几处的注释
 * 也一字未动地跟了过来：它们记的是"为什么是这个判据"，和判据本身一样是要留下来的东西）。
 * 只有两处**形状**不同：
 *
 * ① `instanceCommonFolderKeys()` 不直接问 `InstanceSettingsDialog::shortcutPathKeys()`，
 * 而是按参数收。**有哪几条是界面的事**（这一层只说"哪几条现在真有东西"），
 * 而且这样一来这一层一个 ui 头都不用拉进来。
 * ② `worldStructureSources()` 不把两排的**排头**（「存档」/「投影」）也填上 ——
 * 那几个字是 `ui/InstanceFilesPage` 上的静态函数（唯一出处），这一层不该认字，
 * 所以它只说"哪几个存档有结构"，排头由摆控件那边补（见 .hpp 里那段边界）。
 */

#include "controllers/InstancePagesController.hpp"

#include "download/ResourceCatalog.hpp"
#include "manage/VersionDisplay.hpp"
#include "manage/VersionManager.hpp"
#include "util/AppPaths.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QList>
#include <QPair>

namespace {

/// 一个存档目录里**有没有结构方块存下来的东西**（`generated/minecraft/structures/…` 下的 nbt）。
///
/// 判据是"有没有"，所以一找到第一条就停 —— 一个存档存过几百次结构的话，把整棵子树走完
/// 是白走的（这一趟在**每一次**开实例设置时都要跑：每个存档一次）。
bool dirHasStructureFiles(const QString &structuresDir) {
    QDirIterator it(structuresDir, QStringList{QStringLiteral("*.nbt")}, QDir::Files,
                    QDirIterator::Subdirectories);
    return it.hasNext();
}

}  // namespace

QHash<QString, QString> InstancePagesController::fileDirs(const QString &versionId) {
    const QString root = AppPaths::versionDir(versionId);
    QHash<QString, QString> dirs;

    // 能下载的那四类走 ResourceCatalog 那张表 —— 那是目录名的**唯一出处**
    // （整合包那一路 curseForgeFilePath() 也照它写，注释里点名要求一致）。
    const QList<QPair<QString, ResourceCatalog::Kind>> downloadable = {
        {QStringLiteral("mod"), ResourceCatalog::Kind::Mod},
        {QStringLiteral("resourcepack"), ResourceCatalog::Kind::ResourcePack},
        {QStringLiteral("datapack"), ResourceCatalog::Kind::DataPack},
        {QStringLiteral("shader"), ResourceCatalog::Kind::Shader},
    };
    for (const QPair<QString, ResourceCatalog::Kind> &entry : downloadable) {
        dirs.insert(entry.first,
                    root + QLatin1Char('/') + ResourceCatalog::folderForKind(entry.second));
    }

    // 这五样下载页里没有对应的"类"（没有"存档"/"配置文件"这一类资源），目录名直接写在这儿。
    // 「原理图」用的是 Litematica 的默认目录名；另外四个是游戏自己建的。
    //
    // `config` 是 加的（实例设置里那一页「配置文件」）。**它不是游戏建的** ——
    // 是模组自己建的（原版客户端根本没有这个目录，那时候那一页会说"进一次游戏"）。
    const QList<QPair<QString, QString>> plain = {
        {QStringLiteral("schematic"), QStringLiteral("schematics")},
        {QStringLiteral("config"), QStringLiteral("config")},
        {QStringLiteral("save"), QStringLiteral("saves")},
        {QStringLiteral("screenshots"), QStringLiteral("screenshots")},
        {QStringLiteral("logs"), QStringLiteral("logs")},
    };
    for (const QPair<QString, QString> &entry : plain) {
        dirs.insert(entry.first, root + QLatin1Char('/') + entry.second);
    }
    return dirs;
}

QStringList InstancePagesController::commonFolderKeys(const QString &versionId,
                                                     const QHash<QString, QString> &dirs,
                                                     const QStringList &shortcutKeys) {
    // 要求「仅在**目录存在**或**相关模组可识别**时优先展示，其他项收在
    // "更多目录"」—— 这个函数就是那句话的那两条判据，一条不多。
    //
    // NOTE: **没有第二张"哪些一定得有"的表** —— 表里每一条的判据都是同一个
    // `QDir(path).exists()`，唯一一条例外是「录像」（下面说明）。加一条新的常用目录时
    // 只该往 shortcutPathKeys() 那张表里加一行，不该在这儿再加一个分支。
    const QString instanceDir = AppPaths::versionDir(versionId);
    // 「录像」是唯一一条"目录还没有、也算有"的：没装 ReplayMod 的用户这辈子看不到这个
    // 目录，装了但还没录过的实例上它往往也还没建（第一次开录像时才建）。所以**装了那个
    // 模组就算有** —— 这正是"相关模组可识别"。识别的办法和仓里别的几处一样，
    // 走 hasMod()（mods/ 里文件名的那个整段词，见它上面那段说明）。
    //
    // NOTE: 目录名 `replay_recordings` 的出处是 AppPaths::replayRecordingsDir()（那个模组
    // 定的名字），这一层不自己拼。
    const bool hasReplayMod = hasMod(versionId, QStringLiteral("replaymod"));

    QStringList present;
    for (const QString &key : shortcutKeys) {
        // 「版本目录」开的是 setInstanceDir() 那一份、不在 dirs 里 —— 判据照旧是同一个
        // （见 InstanceSettingsDialog::setFileDirs() 那条 NOTE）。
        const QString path = key == QStringLiteral("dir") ? instanceDir : dirs.value(key);
        bool ok = !path.isEmpty() && QDir(path).exists();
        if (key == QStringLiteral("replay")) {
            ok = ok || hasReplayMod;
        }
        if (ok) {
            present.append(key);
        }
    }
    return present;
}

InstancePagesController::WorldStructures
InstancePagesController::worldStructures(const QString &versionId) {
    // 「结构」那一排的全部内容。
    //
    // 要求：「那就像版本树一样根据存档来列…两个下拉框并排」+「**上面一排是存档的，
    // 下面一排是投影的**」。这两句话**只有这一层说得出口**（AppPaths 和"装没装那个模组"
    // 都在它手上，ui/ 那一层两样都够不着）。
    //
    // 摆出来的规矩（逐条都是用户点名的）：
    // - **没有结构的存档一个都不列**（「没有结构的存档不显示在列表里面」）；
    // - **没装投影模组就没有「投影」那一排**（「没有装投影图则不显示投影图下拉」）；
    // - 一个存档都没有、可投影装着 ⇒ 存档那一排照旧露着，里面是那**一行灰字**
    // （「如果没有任何存档含有结构那么存档总下拉打开显示无可用的结构」）；
    // - 两样都没有 ⇒ 空表，那一页只剩"这个实例还没有结构 / 原理图"那句提示
    // （「如果存档里面没有且投影也没装的话就直接不显示任何框，只保留没有结构的提示
    // 文本」）。
    WorldStructures worlds;
    const QDir saves(AppPaths::versionDir(versionId) + QStringLiteral("/saves"));
    const QFileInfoList saveDirs = saves.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                       QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &save : saveDirs) {
        const QString structures =
            save.absoluteFilePath() + QStringLiteral("/generated/minecraft/structures");
        if (!dirHasStructureFiles(structures)) {
            continue;
        }
        worlds.labels.append(save.fileName());
        worlds.paths.append(structures);
    }
    return worlds;
}

InstancePagesController::Usability InstancePagesController::usability(VersionManager *versions,
                                                                      const QString &versionId) {
    Usability out;
    out.mods = modsUsable(versions, versionId);
    out.shader = shaderUsable(versions, versionId);
    out.litematica = hasMod(versionId, QStringLiteral("litematica"));
    out.worlds = worldStructures(versionId);
    // 这一页「用不上」的判据从"装没装 Litematica"放宽成"两半里有一半有东西"：
    // 结构那一半是**游戏自己**认的（结构方块就是原版的东西），一个实例里真有结构、
    // 却因为没装投影模组把整页判成"没人会读"，那是把半页东西藏起来了。
    // 而且用户对那一档的规矩正是「**没有装投影图则不显示投影图下拉**」—— 藏的是
    // **那一排**，不是整页。
    // NOTE: "这个版本认不认结构方块"在盘上读不出（没有那种记录）⇒ 拿"**这个实例里
    // 真有结构**"当判据；两半都空的那一档本来就不该露（见上面那段规矩的最后一条），
    // 所以这个判据和用户那句话是同一条线。
    out.schematic = out.litematica || !out.worlds.labels.isEmpty();
    out.datapack = versions != nullptr && versions->instanceSupportsDatapacks(versionId);
    return out;
}

QString InstancePagesController::loaderName(const VersionDisplayParts &parts,
                                            const VersionManager *versions, QString *versionOut) {
    if (versionOut != nullptr) {
        versionOut->clear();
    }
    if (!parts.loaderName.isEmpty()) {
        if (versionOut != nullptr) {
            *versionOut = parts.loaderVersion;
        }
        return parts.loaderName;
    }
    QString name;
    QString version;
    if (versions != nullptr && versions->instanceModLoader(parts.id, &name, &version)) {
        if (versionOut != nullptr) {
            *versionOut = version;
        }
        return name;
    }
    return QString();
}

bool InstancePagesController::hasMod(const QString &versionId, const QString &nameToken) {
    const QString modsDir =
        AppPaths::versionDir(versionId) + QLatin1Char('/') +
        ResourceCatalog::folderForKind(ResourceCatalog::Kind::Mod);
    // 只认 `.jar`：被禁用的那些现在叫 `.jar.disabled`，扫不到 —— 那正是禁用的意思。
    const QStringList jars = QDir(modsDir).entryList({QStringLiteral("*.jar")}, QDir::Files);
    for (const QString &jar : jars) {
        if (jarNameHasToken(jar, nameToken)) {
            return true;
        }
    }
    return false;
}

bool InstancePagesController::modsUsable(VersionManager *versions, const QString &versionId) {
    // ① 基于一个模组加载器。判据走 loaderName()（**不是** displayParts().loaderName ——
    // 那一格只有我们自己的安装器会写，理由写在那个函数的说明里）。
    if (versions != nullptr &&
        !loaderName(versions->displayParts(versionId), versions).isEmpty()) {
        return true;
    }
    // ② mods/ 里已经有 jar。和 hasMod() 一样只认 `.jar`：被禁用的那些现在叫
    // `.jar.disabled`，扫不到 —— 那正是禁用的意思。
    const QDir modsDir(AppPaths::versionDir(versionId) + QLatin1Char('/') +
                       ResourceCatalog::folderForKind(ResourceCatalog::Kind::Mod));
    return !modsDir.entryList({QStringLiteral("*.jar")}, QDir::Files).isEmpty();
}

bool InstancePagesController::shaderUsable(VersionManager *versions, const QString &versionId) {
    // ① 加载器**就是** OptiFine（它自己就认 shaderpacks/）。加载器名走 loaderName()
    // 而不是 `displayParts().loaderName`：后者只有我们自己的安装器会写，而用户那两个
    // OptiFine 实例（`1.20.1-OptiFine_I6` / `1.20.2-OptiFine_I7_pre1`）是 PCL 装的、
    // meta 里没有那一格 —— 它们又没有 mods/ 目录，于是①和②两条同时落空，光影包那一页
    // 在**真的装了 OptiFine** 的实例上说"用不上"（ 连同「模组」一起修的）。
    // 比对基准走 VersionManager::loaderDisplayName()，不写一遍字面量。
    if (versions != nullptr) {
        const QString loader = loaderName(versions->displayParts(versionId), versions);
        if (loader == VersionManager::loaderDisplayName(VersionManager::LoaderKind::OptiFine)) {
            return true;
        }
    }
    // ② mods/ 里装了一个"加载光影的模组"。这三个名字**都是用户盘上真见过的**：
    // `iris`（Fabric / NeoForge / Quilt 上那个）、`oculus`（Forge 上那个 Iris 移植 ——
    // 「落幕曲」「香草纪元」「魔改版ATT2」三个整合包用的都是它）、`optifine`
    // （1.12.2 及以前 OptiFine 是**当普通模组装进 mods/ 下**的，
    // `OptiFine_1.12.2_HD_U_G5.jar`）。
    // NOTE: 没有 `optifabric`（Fabric 上那个把 OptiFine 塞进来的桥）：它要求 OptiFine
    // 的 jar 也在 mods/ 里，所以上面那条 `optifine` 本来就盖住了它。
    static const QStringList loaders = {
        QStringLiteral("iris"),
        QStringLiteral("oculus"),
        QStringLiteral("optifine"),
    };
    for (const QString &token : loaders) {
        if (hasMod(versionId, token)) {
            return true;
        }
    }
    return false;
}

bool InstancePagesController::jarNameHasToken(const QString &fileName, const QString &token) {
    if (token.isEmpty()) {
        return false;
    }
    const QString wanted = token.toLower();
    const auto hit = [&wanted](const QString &word) {
        return word == wanted || word.startsWith(wanted);
    };
    QString word;
    for (const QChar character : fileName.toLower()) {
        if (character.isLetterOrNumber() && character.unicode() < 128) {
            word += character;
            continue;
        }
        if (hit(word)) {
            return true;
        }
        word.clear();
    }
    return hit(word);
}
