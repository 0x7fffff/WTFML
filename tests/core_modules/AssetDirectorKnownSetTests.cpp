#include "Support.hpp"
#include "download/AssetDirectorKnownSet.hpp"
#include "manage/VersionManager.hpp"

namespace {

/// 造一套小集合：内容全是编造的，只用来钉"解析、复用判据、上限、路径"这几条规则。
QByteArray minimalManifest(const QByteArray &extraSet = QByteArray()) {
    const QByteArray set = QByteArrayLiteral(R"({
   "id": "fixture-set",
   "note": "fixture",
   "consumerPatterns": ["^Fixture-Mod-1\\.0\\.jar$"],
   "minecraftVersion": "1.21",
   "versionJson": {"url": "https://piston-meta.mojang.com/v1/packages/09183e46ebe8641c217098341be7fe9a6b7940e3/1.21.json",
                   "sha1": "09183e46ebe8641c217098341be7fe9a6b7940e3", "size": 38401},
   "assetIndex": {"id": "17",
                  "url": "https://piston-meta.mojang.com/v1/packages/e61ea955de95de1f99d36616a8b46147e9f6f5d7/17.json",
                  "sha1": "e61ea955de95de1f99d36616a8b46147e9f6f5d7", "size": 449557},
   "totalBytes": 300,
   "objects": [
     {"name": "minecraft/sounds/a.ogg", "hash": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "size": 100},
     {"name": "minecraft/sounds/b.ogg", "hash": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", "size": 200}
   ]
 })");
    return QByteArray("{\"formatVersion\": 1, \"knownSets\": [") + set
           + (extraSet.isEmpty() ? QByteArray() : QByteArray(", ") + extraSet) + "]}";
}

QString hash(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
}

}  // namespace

void runAssetDirectorKnownSetTests(const QString &sandbox) {
    using namespace AssetDirectorKnownSet;

    section("AD1. 随产品发布的已知集合清单（与仓库里那份逐字节一致）");
    {
        QFile file(QStringLiteral(REPO_ROOT_PATH) + "/assets/assetdirector/known-sets.json");
        const bool opened = file.open(QIODevice::ReadOnly);
        check(opened, "AD1 仓库里的 known-sets.json 读得出来", opened ? QString() : file.errorString());
        const QByteArray fromDisk = file.readAll();
        file.close();
        check(!fromDisk.isEmpty(), "AD1 仓库里的清单不是空的");
        // 生成的那份（AssetDirectorKnownSetData.cpp）必须和人对得上的这一份逐字节相同：
        // 只改一边而不重跑生成脚本，是这份数据最容易出的错。
        check(embeddedManifest() == fromDisk, "AD1 嵌进产品的字节与 assets/assetdirector/known-sets.json 一致",
              QStringLiteral("embedded=%1 disk=%2").arg(embeddedManifest().size()).arg(fromDisk.size()));

        Manifest manifest;
        QString error;
        check(parse(fromDisk, &manifest, &error), "AD1 仓库里的清单能解析", error);
        check(manifest.sets.size() == 1, "AD1 只有一套已知集合",
              QString::number(manifest.sets.size()));
        if (!manifest.sets.isEmpty()) {
            const Set &set = manifest.sets.first();
            check(set.id == QLatin1String("gtnh-2.8.4-etfuturum"), "AD1 集合 id", set.id);
            check(set.minecraftVersion == QLatin1String("1.21"), "AD1 资产版本", set.minecraftVersion);
            check(set.objects.size() == 756, "AD1 对象数 756（755 个请求项 + sounds.json）",
                  QString::number(set.objects.size()));
            check(set.totalBytes == 115941197, "AD1 声明总量与实测一致（115,941,197 B）",
                  QString::number(set.totalBytes));
            qint64 sum = 0;
            QSet<QString> hashes;
            bool sizesOk = true;
            for (const Object &object : set.objects) {
                sum += object.size;
                hashes.insert(object.hash);
                if (object.size <= 0 || !object.name.startsWith(QLatin1String("minecraft/"))) sizesOk = false;
                if (object.hash.size() != 40 || object.hash != object.hash.toLower()) sizesOk = false;
            }
            check(sum == set.totalBytes, "AD1 逐项之和等于声明总量");
            check(hashes.size() == set.objects.size(), "AD1 没有重复哈希",
                  QStringLiteral("%1 / %2").arg(hashes.size()).arg(set.objects.size()));
            check(sizesOk, "AD1 每个对象都有合法的名字/哈希/字节数");
            check(set.versionJson.url.startsWith(QLatin1String("https://piston-meta.mojang.com/")),
                  "AD1 版本 JSON 走官方地址");
            check(set.assetIndex.url.startsWith(QLatin1String("https://piston-meta.mojang.com/")),
                  "AD1 资源索引走官方地址");
        }
        check(loadEmbedded(&manifest, &error), "AD1 loadEmbedded() 走的是同一份", error);
    }

    section("AD2. 消费者识别：只按 mods/ 里的文件名，认不出就不猜");
    {
        Manifest manifest;
        QString error;
        parse(minimalManifest(), &manifest, &error);
        check(manifest.sets.size() == 1, "AD2 夹具清单可用", error);
        check(matchConsumer(manifest, {QStringLiteral("Fixture-Mod-1.0.jar")}) != nullptr,
              "AD2 命中精确文件名");
        check(matchConsumer(manifest, {QStringLiteral("Jei-1.0.jar"),
                                       QStringLiteral("Fixture-Mod-1.0.jar")}) != nullptr,
              "AD2 一堆模组里认得出那个消费者");
        check(matchConsumer(manifest, {QStringLiteral("Fixture-Mod-1.1.jar")}) == nullptr,
              "AD2 版本号不对就不认");
        check(matchConsumer(manifest, {QStringLiteral("Fixture-Mod-1.0.jar.disabled")}) == nullptr,
              "AD2 改过扩展名的不认");
        check(matchConsumer(manifest, {}) == nullptr, "AD2 没有模组时不认");

        Manifest shipped;
        parse(embeddedManifest(), &shipped, &error);
        check(matchConsumer(shipped, {QStringLiteral("Et-Futurum-Requiem-2.6.2.25-GTNH.jar")}) != nullptr,
              "AD2 GTNH 2.8.4 的 EFR jar 名命中随产品发布的那一套");
        check(matchConsumer(shipped, {QStringLiteral("etfuturum-2.6.2.25-GTNH.jar")}) != nullptr,
              "AD2 全小写的别名也认（第二条件）");
        check(matchConsumer(shipped, {QStringLiteral("Et-Futurum-Requiem-2.6.2.26-GTNH.jar")}) == nullptr,
              "AD2 其它 EFR 版本不套用这一套（每套按模组版本钉死）");
    }

    section("AD3. 清单校验：坏数据一律拒绝，不'尽量读'");
    {
        Manifest manifest;
        QString error;
        check(!parse("{}", &manifest, &error), "AD3 缺 formatVersion");
        check(!parse(QByteArrayLiteral("{\"formatVersion\": 2, \"knownSets\": []}"), &manifest, &error),
              "AD3 格式版本不对");
        check(!parse(QByteArrayLiteral("{\"formatVersion\": 1, \"knownSets\": []}"), &manifest, &error),
              "AD3 空 knownSets");
        check(!parse(QByteArray(4 * 1024 * 1024 + 1, 'x'), &manifest, &error), "AD3 超过 4 MiB 的清单拒绝");

        const auto broken = [&](const QByteArray &setBody, const QString &what) {
            QString why;
            const bool ok = parse(QByteArray("{\"formatVersion\": 1, \"knownSets\": [") + setBody + "]}",
                                  &manifest, &why);
            check(!ok && !why.isEmpty(), what, why);
        };
        broken(QByteArrayLiteral(R"({"id": "x", "consumerPatterns": ["("], "minecraftVersion": "1.21"})"),
               "AD3 消费者正则写坏");
        broken(QByteArrayLiteral(R"({"id": "Fixture", "consumerPatterns": ["^a$"], "minecraftVersion": "1.21"})"),
               "AD3 id 必须是小写短名");
        broken(QByteArrayLiteral(R"({"id": "x", "consumerPatterns": [], "minecraftVersion": "1.21"})"),
               "AD3 没有匹配规则");
        broken(QByteArrayLiteral(R"({"id": "x", "consumerPatterns": ["^a$"], "minecraftVersion": "1.21",
            "versionJson": {"url": "http://piston-meta.mojang.com/x.json", "sha1": "09183e46ebe8641c217098341be7fe9a6b7940e3", "size": 1},
            "assetIndex": {"id": "17", "url": "https://piston-meta.mojang.com/x.json", "sha1": "09183e46ebe8641c217098341be7fe9a6b7940e3", "size": 1},
            "totalBytes": 1, "objects": [{"name": "a.ogg", "hash": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "size": 1}]})"),
               "AD3 只接受 HTTPS 官方地址");
        broken(QByteArrayLiteral(R"({"id": "x", "consumerPatterns": ["^a$"], "minecraftVersion": "1.21",
            "versionJson": {"url": "https://evil.example/x.json", "sha1": "09183e46ebe8641c217098341be7fe9a6b7940e3", "size": 1},
            "assetIndex": {"id": "17", "url": "https://piston-meta.mojang.com/x.json", "sha1": "09183e46ebe8641c217098341be7fe9a6b7940e3", "size": 1},
            "totalBytes": 1, "objects": [{"name": "a.ogg", "hash": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "size": 1}]})"),
               "AD3 非 Mojang 主机拒绝");
        broken(QByteArrayLiteral(R"({"id": "x", "consumerPatterns": ["^a$"], "minecraftVersion": "1.21",
            "versionJson": {"url": "https://piston-meta.mojang.com/x.json", "sha1": "09183e46ebe8641c217098341be7fe9a6b7940e3", "size": 1},
            "assetIndex": {"id": "17", "url": "https://piston-meta.mojang.com/x.json", "sha1": "09183e46ebe8641c217098341be7fe9a6b7940e3", "size": 1},
            "totalBytes": 1, "objects": [{"name": "../a.ogg", "hash": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "size": 1}]})"),
               "AD3 对象名里的 .. 拒绝");
        broken(QByteArrayLiteral(R"({"id": "x", "consumerPatterns": ["^a$"], "minecraftVersion": "1.21",
            "versionJson": {"url": "https://piston-meta.mojang.com/x.json", "sha1": "09183e46ebe8641c217098341be7fe9a6b7940e3", "size": 1},
            "assetIndex": {"id": "17", "url": "https://piston-meta.mojang.com/x.json", "sha1": "09183e46ebe8641c217098341be7fe9a6b7940e3", "size": 1},
            "totalBytes": 2, "objects": [{"name": "a.ogg", "hash": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "size": 1}]})"),
               "AD3 声明总量和逐项之和对不上");
        broken(QByteArrayLiteral(R"({"id": "x", "consumerPatterns": ["^a$"], "minecraftVersion": "1.21",
            "versionJson": {"url": "https://piston-meta.mojang.com/x.json", "sha1": "09183e46ebe8641c217098341be7fe9a6b7940e3", "size": 1},
            "assetIndex": {"id": "17", "url": "https://piston-meta.mojang.com/x.json", "sha1": "09183e46ebe8641c217098341be7fe9a6b7940e3", "size": 1},
            "totalBytes": 2, "objects": [
              {"name": "a.ogg", "hash": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "size": 1},
              {"name": "b.ogg", "hash": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "size": 1}]})"),
               "AD3 同一个哈希出现两次");
        broken(QByteArrayLiteral(R"({"id": "x", "consumerPatterns": ["^a$"], "minecraftVersion": "1.21",
            "versionJson": {"url": "https://piston-meta.mojang.com/x.json", "sha1": "zz", "size": 1},
            "assetIndex": {"id": "17", "url": "https://piston-meta.mojang.com/x.json", "sha1": "09183e46ebe8641c217098341be7fe9a6b7940e3", "size": 1},
            "totalBytes": 1, "objects": [{"name": "a.ogg", "hash": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "size": 1}]})"),
               "AD3 不是 40 位十六进制的哈希");
    }

    section("AD4. 落点就是 AssetDirector 自己的目录（不碰共享 assets/objects）");
    {
        const QString assets = QStringLiteral("D:/data/assets");
        check(overlayDir(assets) == assets + QStringLiteral("/asset_director"), "AD4 overlay 根",
              overlayDir(assets));
        check(overlayObjectPath(assets, QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"))
                  == assets + QStringLiteral("/asset_director/assets/objects/bb/bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
              "AD4 对象路径 = <ad>/assets/objects/<前两位>/<hash>");
        check(overlayVersionJsonPath(assets, QStringLiteral("1.21"))
                  == assets + QStringLiteral("/asset_director/versions/1.21/1.21.json"),
              "AD4 版本 JSON 路径");
        check(overlayAssetIndexPath(assets, QStringLiteral("17"))
                  == assets + QStringLiteral("/asset_director/assets/indexes/17.json"),
              "AD4 资源索引路径");
        check(objectUrl(QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"))
                  == QStringLiteral("https://resources.download.minecraft.net/bb/bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
              "AD4 对象下载地址");
    }

    section("AD5. 复用判据与下载计划（注入校验器，不碰磁盘）");
    {
        Manifest manifest;
        QString error;
        parse(minimalManifest(), &manifest, &error);
        const Set &set = manifest.sets.first();
        const QString assets = sandbox + QStringLiteral("/ad-plan/assets");
        const QString hashA = set.objects.at(0).hash;
        const QString hashB = set.objects.at(1).hash;
        const QString sharedA = assets + QStringLiteral("/objects/aa/") + hashA;
        const QString overlayB = overlayObjectPath(assets, hashB);
        const QString versionPath = overlayVersionJsonPath(assets, set.minecraftVersion);
        const QString indexPath = overlayAssetIndexPath(assets, set.assetIndexId);

        const auto validatorFor = [&](const QStringList &present) {
            return [present](const QString &path, qint64, const QString &) {
                return present.contains(path);
            };
        };

        Plan empty = plan(set, assets, validatorFor({}));
        check(empty.objects == 2, "AD5 对象总数", QString::number(empty.objects));
        check(empty.toDownload == 2 && empty.missing.size() == 2, "AD5 什么都没有时两个都要下");
        check(empty.bytesToDownload == 300 + set.versionJson.size + set.assetIndex.size,
              "AD5 待下载字节数含两份 JSON", QString::number(empty.bytesToDownload));
        check(empty.needVersionJson && empty.needAssetIndex, "AD5 两份 JSON 都要取");
        check(empty.refusal.isEmpty(), "AD5 300 B 远低于上限，不拒绝", empty.refusal);

        Plan shared = plan(set, assets, validatorFor({sharedA}));
        check(shared.reuseAssets == 1 && shared.reuseOverlay == 0,
              "AD5 共享 assets/objects 里那一份算复用（AD 自己也读它）");
        check(shared.toDownload == 1 && shared.missing.first().hash == hashB,
              "AD5 只剩另一件要下");

        Plan overlayFirst = plan(set, assets, validatorFor({sharedA, overlayB}));
        check(overlayFirst.reuseOverlay == 1 && overlayFirst.reuseAssets == 1,
              "AD5 overlay 与共享 objects 各算各的复用");
        check(overlayFirst.toDownload == 0 && overlayFirst.bytesToDownload == set.versionJson.size + set.assetIndex.size,
              "AD5 对象都齐了，只剩两份 JSON");
        check(overlayFirst.needVersionJson && overlayFirst.needAssetIndex, "AD5 两份 JSON 缺");

        Plan complete = plan(set, assets, validatorFor({sharedA, overlayB, versionPath, indexPath}));
        check(complete.toDownload == 0 && !complete.needVersionJson && !complete.needAssetIndex,
              "AD5 全都在盘上时一个字节都不用取");
        check(complete.bytesToDownload == 0, "AD5 全齐时待下载字节数为 0");

        // 上限：人工确认值 256 MiB。超一个字节就拒绝，而且**不返回任何要下的东西**。
        Set huge = set;
        huge.objects.last().size = kMaxKnownSetBytes;
        huge.totalBytes = huge.objects.first().size + huge.objects.last().size;
        Plan refused = plan(huge, assets, validatorFor({}));
        check(!refused.refusal.isEmpty(), "AD5 超上限时拒绝执行", refused.refusal);
        check(refused.bytesToDownload == 0 && refused.missing.isEmpty(),
              "AD5 拒绝时没有任何待下载项（一个字节都不写）");
        check(refused.bytesTotal > kMaxKnownSetBytes, "AD5 拒绝里带着那套集合的体积");

        // 校验器说"坏"的已有文件不算复用：一份坏的缓存在 AD 眼里和好的一模一样。
        writeFileAt(overlayB, QByteArray(set.objects.at(1).size, 'x'));
        Plan realValidator = plan(set, assets, {});
        check(realValidator.reuseOverlay == 0,
              "AD5 尺寸对、内容错的已有对象不算复用（默认校验器会算 SHA-1）",
              QStringLiteral("reuseOverlay=%1").arg(realValidator.reuseOverlay));
        check(realValidator.toDownload == 2, "AD5 坏文件仍然要重下");
        QFile::remove(overlayB);
    }

    section("AD6. VersionManager 只管认得出消费者的实例");
    {
        const QString previousRoot = AppPaths::minecraftDir();
        const QString root = sandbox + QStringLiteral("/ad-vm");
        QDir().mkpath(root);
        AppPaths::setMinecraftDir(root);
        VersionManager manager;
        manager.setUseMirror(false);

        const QString instance = QStringLiteral("gtnh-prefetch-fixture");
        QDir().mkpath(AppPaths::versionDir(instance));
        writeFileAt(AppPaths::versionDir(instance) + QStringLiteral("/mods/jei-1.0.jar"), QByteArrayLiteral("x"));
        VersionManager::AssetDirectorPrefetchPlan plan = manager.assetDirectorPrefetchPlan(instance);
        check(!plan.applicable, "AD6 没有消费者模组时不适用（连任务行都不该有）");
        check(plan.toDownload == 0 && plan.refusal.isEmpty(), "AD6 不适用时计划是空的");

        writeFileAt(AppPaths::versionDir(instance) + QStringLiteral("/mods/Et-Futurum-Requiem-2.6.2.25-GTNH.jar"),
                    QByteArrayLiteral("not-really-a-jar"));
        plan = manager.assetDirectorPrefetchPlan(instance);
        check(plan.applicable, "AD6 认出了 EFR 2.6.2.25-GTNH");
        check(plan.setId == QLatin1String("gtnh-2.8.4-etfuturum"), "AD6 认到的是 GTNH 2.8.4 那一套", plan.setId);
        check(plan.objects == 756, "AD6 计划里的对象数", QString::number(plan.objects));
        check(plan.refusal.isEmpty(), "AD6 111 MiB 低于 256 MiB 上限，不拒绝", plan.refusal);
        check(!plan.upToDate, "AD6 空缓存根上不是'已齐'");
        check(plan.toDownload == 756, "AD6 空缓存根上全都要下", QString::number(plan.toDownload));
        check(plan.bytesToDownload == 115941197 + 38401 + 449557,
              "AD6 待下载字节数 = 对象 + 版本 JSON + 索引", QString::number(plan.bytesToDownload));
        check(plan.bytesTotal == 115941197, "AD6 集合总量", QString::number(plan.bytesTotal));

        // 共享 assets/objects 里放一份**内容正确**的（这一个是 sounds.json 那种小文件，
        // 内容是编的但哈希按编的算不出来 —— 所以直接用真实集合里那件已知对象的字节数 +
        // 我们自己造的哈希不可能对上；这里只断言"计划不会把它当复用"）。
        const QString bogus = AppPaths::assetsDir() + QStringLiteral("/objects/bb/bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
        writeFileAt(bogus, QByteArray(16, 'x'));
        const VersionManager::AssetDirectorPrefetchPlan afterBogus = manager.assetDirectorPrefetchPlan(instance);
        check(afterBogus.reuseAssets == 0, "AD6 共享 assets/objects 里对不上的文件不算复用");
        check(afterBogus.toDownload == 756, "AD6 仍然全都要下");

        // 不适用/已经齐了这两条路**不发终态、不写盘**：这里直接调数据层确认它返回 false，
        // 而且没有在 overlay 里留下任何东西。
        check(!manager.prefetchAssetDirectorResources(QStringLiteral("no-such-instance")),
              "AD7 不存在的实例直接返回 false");
        check(!QDir(AssetDirectorKnownSet::overlayDir(AppPaths::assetsDir())).exists(),
              "AD7 不适用时连 overlay 目录都不会被建出来");

        AppPaths::setMinecraftDir(previousRoot);
    }
}
