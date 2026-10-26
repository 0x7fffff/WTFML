/**
 * @file AssetDirectorKnownSet.hpp
 * @brief GTNH 首启资源：MCLib / AssetDirector 的"已知集合"清单、复用判据与下载计划。
 *
 * 【为什么要有这一层】
 *
 * MCLib 的 AssetDirector 在**游戏进程里**按模组代码声明的 `ADConfig` 去 Mojang 取资源
 * （1.7.10 的 GTNH 2.8.4 由 Et Futurum Requiem 2.6.2.25-GTNH 发起）。请求集合不在整合包
 * 清单里、也不在资源索引里 —— 前者只声明原版 `assetIndex`，后者只给"某个 MC 版本的
 * 全集"。所以启动器唯一能稳定拿到的形态是**按整合包＋模组版本固定的一份清单**
 * （见 docs/MODPACK_IMPORT.md 的 2.1.6a 结论）。
 *
 * 这一层只做**纯逻辑**：解析并严格校验那份清单、认出消费者、算出"哪些已经在盘上、
 * 哪些要下"，以及 AD 自己的落点路径。网络、批次、进度、取消都在调用方
 * （VersionManager::prefetchAssetDirectorResources），这里一个字节都不写盘。
 *
 * 【写在哪、凭什么复用】
 *
 * AD 的目录是 `<--assetsDir>/asset_director`（本启动器传 `<数据目录>/assets`），
 * 对象在 `<ad>/assets/objects/<hash 前两位>/<hash>`，版本 JSON 与索引 JSON 分别在
 * `<ad>/versions/<版本>/<版本>.json` 与 `<ad>/assets/indexes/<索引 id>.json`。
 *
 * AD 读对象时的顺序是 **它自己那份 → 启动器的 `assets/objects`**，写只有前者；
 * 而且它**只判"文件存在"，不重算哈希**。所以：
 * - 写只写 `<ad>`（共享 `assets/objects` 一个字节都不动，和 AD 自己的边界一致）；
 * - 复用一个已存在的文件之前必须自己按清单里的字节数 + SHA-1 验过 —— 一份坏的缓存
 *   在 AD 眼里和好的一模一样，会一直被用下去。
 */
#pragma once

#include <functional>

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace AssetDirectorKnownSet {

/// 清单里的一件资源对象（索引里的 name→hash）。
struct Object {
    QString name;   ///< 资源索引里的名字（`minecraft/sounds/…`），只用于日志
    QString hash;   ///< 40 位小写 SHA-1，也是盘上的文件名
    qint64 size = 0;
};

/// 清单里的一份 JSON（版本 JSON / 资源索引）。
struct FileRef {
    QString url;    ///< 官方 HTTPS 地址
    QString sha1;   ///< 40 位小写 SHA-1
    qint64 size = 0;
};

/// 一套已知集合：认得出的消费者模组 + 它要的那个 MC 版本 + 全部对象。
struct Set {
    QString id;
    QString note;                    ///< 给人看的说明（不参与判据）
    QString minecraftVersion;        ///< AD 会请求的 MC 版本（例如 `1.21`）
    QStringList consumerPatterns;    ///< 正则：实例 `mods/` 下哪些文件名算这个消费者
    FileRef versionJson;
    QString assetIndexId;            ///< 资源索引 id（AD 按它拼 `<ad>/assets/indexes/<id>.json`）
    FileRef assetIndex;
    QList<Object> objects;
    qint64 totalBytes = 0;           ///< 清单自己声明的总量，必须等于逐项之和
};

struct Manifest {
    QList<Set> sets;
};

/// 单套已知集合的体积上限。人工确认：允许自动下载，但超过这个数就拒绝执行。
/// GTNH 2.8.4 的实测集合是 111.04 MiB（见文档），离上限还有余量。
constexpr qint64 kMaxKnownSetBytes = 256LL * 1024 * 1024;

/// 清单格式版本（只认这一个）。
constexpr int kFormatVersion = 1;

/// 严格解析并按需校验（非法字段一律拒绝，不"尽量读"）。
/// @param error 非空时写入给用户看的一句话。
bool parse(const QByteArray &json, Manifest *out, QString *error);

/// 编译进产品的那一份（由 `AssetDirectorKnownSetData.cpp` 提供，生成自
/// `assets/assetdirector/known-sets.json`；验证工程会断言两边字节一致）。
QByteArray embeddedManifest();

/// `embeddedManifest()` + `parse()`。读不到/解析失败都返回 false。
bool loadEmbedded(Manifest *out, QString *error);

/**
 * @brief 按实例 `mods/` 里的文件名认出适用的那一套。
 * @param modFileNames 只要**文件名**（不含路径）。
 * @return 认不出返回 nullptr；认得出多套时取清单里的第一套。
 *
 * NOTE: 判据只有**文件名匹配** —— 不去读 jar 里的 `mcmod.info`：那要么为这一次检查
 * 起一个解压进程（几百个 jar），要么自己写一遍 zip 解析。名字匹配认不出来时**不做任何
 * 推断**，直接当"不适用"，回到今天的行为（让 AD 自己在游戏里下）。
 */
const Set *matchConsumer(const Manifest &manifest, const QStringList &modFileNames);

/// AD 的目录：`<assets>/asset_director`。
QString overlayDir(const QString &assetsRoot);
/// `<ad>/assets/objects/<hash 前两位>/<hash>`。
QString overlayObjectPath(const QString &assetsRoot, const QString &hash);
/// `<ad>/versions/<mcVersion>/<mcVersion>.json`（AD 自己那一条路径）。
QString overlayVersionJsonPath(const QString &assetsRoot, const QString &minecraftVersion);
/// `<ad>/assets/indexes/<assetIndexId>.json`。
QString overlayAssetIndexPath(const QString &assetsRoot, const QString &assetIndexId);
/// 官方资源对象地址：`https://resources.download.minecraft.net/<前两位>/<hash>`。
QString objectUrl(const QString &hash);

/**
 * @brief 一个文件够不够格被当成"已经有了"。**纯函数**，验证工程直接喂它。
 * 默认实现 = 字节数对得上再算 SHA-1（和 DownloadManager 的 `SizeAndSha1` 同一套判据）。
 */
using Validator = std::function<bool(const QString &path, qint64 expectedSize, const QString &expectedSha1)>;

/// 下载计划。**不写盘**：调用方拿它决定"下不下、下什么"。
struct Plan {
    int objects = 0;             ///< 清单里的对象总数
    int reuseOverlay = 0;        ///< `<ad>` 里已有且校验通过的
    int reuseAssets = 0;         ///< 启动器共享 `assets/objects` 里已有且校验通过的
    int toDownload = 0;          ///< 要联网取的（= missing.size()）
    qint64 bytesToDownload = 0;  ///< 含要取的两份 JSON
    qint64 bytesTotal = 0;       ///< 这套集合声明的总量（不含两份 JSON）
    bool needVersionJson = false;
    bool needAssetIndex = false;
    QList<Object> missing;       ///< 要下的对象（清单原顺序）
    QString refusal;             ///< 非空 = 不执行，一个字节都不写
};

/**
 * @brief 算一遍计划。
 * @param assetsRoot `AppPaths::assetsDir()`（`<数据目录>/assets`）。
 * @param valid 复用判据（默认按字节数 + SHA-1）。验证工程注一个不碰磁盘的版本。
 *
 * 复用顺序和 AD 自己一致：`<ad>/assets/objects` 优先，其次共享 `assets/objects`。
 * 共享那一份只是**读**（AD 也读它），不复制、不硬链接，更不往里写。
 */
Plan plan(const Set &set, const QString &assetsRoot, const Validator &valid = {});

}  // namespace AssetDirectorKnownSet
