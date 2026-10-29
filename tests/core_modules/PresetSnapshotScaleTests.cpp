// 「装到一个新实例里」那一趟，**第一份下载一开始**就要把目标实例的配置目录整棵拍个快照
// （`Presets::snapshotInitializationTargets`）：options.txt / servers.dat / config /
// defaultconfigs / resourcepacks / shaderpacks / 每个存档的 level.dat 和它的数据包……
// 每一份**整文件**算一遍 SHA-256。
//
// 这一拍原先是**同步跑在界面线程上**的（`DownloadBatchController` 的 `batchStarted`
// 那一句里，拿到第一份文件要下的时候就会走到），所以"用户点了安装之后界面会不会顿住"
// 取决于这棵树有多大 —— 而 resourcepacks/ 和 shaderpacks/ 里的文件是几十上百 MB 的，
// 存档里也可能躺着几个大世界。
//
// 这一节**只量、不拿耗时当断言**（那是这台机器的磁盘速度，不是产品的性质）。
// 钉形状的只有那几条：快照里有那几格、取消之后立刻回空、第二次拍同一棵树结果不变。
//
// 量出来的数决定"要不要挪到后台"（见 `docs/P2_RECOVERY_PERFORMANCE_20261028.md`）。

#include "Support.hpp"
#include "presets/PresetTargetSnapshot.hpp"

#include <cstdio>

namespace {

/// 往 path 塞一份 size 字节的文件。内容按块填，避免 64 MB 都在内存里拼一遍。
void fillFile(const QString &path, qint64 size) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    QByteArray block(64 * 1024, Qt::Uninitialized);
    for (int i = 0; i < block.size(); ++i) block[i] = char('a' + i % 26);
    for (qint64 written = 0; written < size; written += block.size()) {
        const auto chunk = qMin<qint64>(block.size(), size - written);
        file.write(block.constData(), chunk);
    }
    file.close();
}

/// 量一趟快照：返回耗时（微秒）和结果的格数。
struct Walk {
    qint64 us = -1;
    int entries = 0;
};

Walk timedSnapshot(const QString &root) {
    QElapsedTimer clock;
    clock.start();
    const auto snapshot = Presets::snapshotInitializationTargets(root);
    Walk walk;
    walk.us = clock.nsecsElapsed() / 1000;
    walk.entries = snapshot.size();
    return walk;
}

}  // namespace

void runPresetSnapshotScaleTests(const QString &sandbox) {
    section(QStringLiteral("PSS 配置目录快照的代价（首装那一趟原来同步跑在界面线程上）"));
    const QString base = sandbox + QStringLiteral("/preset-snapshot");
    QDir(base).removeRecursively();
    QDir().mkpath(base);

    // ------------------------------------------------------------------
    // PSS1. 一棵"常见"的实例配置树
    //
    // 形状照真机：一个整合包实例的 config/ 里几百份小 json，几张几 MB 的资源包，
    // 一个带数据包的存档。没有这些东西的时候这一拍几乎不花时间（PSS3），
    // **花时间的就是这几格**。
    // ------------------------------------------------------------------
    // NOTE: 这一棵树摆在 `versions/<实例名>` 下面，因为"接着装"那条判据是用
    // `root + "/versions/" + instance` 去重的（`unchangedInitializationContinuation`），
    // 摆在别处就拍的是另一棵树了。
    const QString common = base + QStringLiteral("/versions/common");
    writeFileAt(common + QStringLiteral("/options.txt"), QByteArray(4096, 'o'));
    writeFileAt(common + QStringLiteral("/optionsof.txt"), QByteArray(2048, 'f'));
    writeFileAt(common + QStringLiteral("/servers.dat"), QByteArray(1024, 's'));
    for (int i = 0; i < 400; ++i) {
        writeFileAt(common + QStringLiteral("/config/mod-%1.json").arg(i, 3, 10, QChar(QLatin1Char('0'))),
                    QByteArray(2048, 'c'));
    }
    for (int i = 0; i < 40; ++i) {
        writeFileAt(common + QStringLiteral("/defaultconfigs/pack-%1.toml").arg(i, 2, 10, QChar(QLatin1Char('0'))),
                    QByteArray(1024, 'd'));
    }
    for (int i = 0; i < 2; ++i) {
        fillFile(common + QStringLiteral("/resourcepacks/pack-%1.zip").arg(i), 4LL * 1024 * 1024);
    }
    fillFile(common + QStringLiteral("/shaderpacks/complementary.zip"), 2LL * 1024 * 1024);
    writeFileAt(common + QStringLiteral("/saves/world/level.dat"), QByteArray(8192, 'w'));
    for (int i = 0; i < 80; ++i) {
        writeFileAt(common + QStringLiteral("/saves/world/datapacks/pack/data/ns/fn-%1.mcfunction")
                                  .arg(i, 3, 10, QChar(QLatin1Char('0'))),
                    QByteArray(512, 'm'));
    }
    for (int i = 0; i < 20; ++i) {
        writeFileAt(common + QStringLiteral("/wtfml.datapacks/installed-%1.json").arg(i), QByteArray(256, 'p'));
    }

    const auto commonWalk = timedSnapshot(common);
    check(commonWalk.entries > 500,
          QStringLiteral("PSS1 常见的整棵树拍得下来，每一份都进表"),
          QStringLiteral("entries=%1").arg(commonWalk.entries));

    // 同一棵树再拍一次：两次结果必须一样（这一层是"值不变就能接着装"的判据，
    // 拍两次拍出两个答案就等于判据本身在抖）。
    const auto first = Presets::snapshotInitializationTargets(common);
    const auto second = Presets::snapshotInitializationTargets(common);
    check(!first.isEmpty() && first == second,
          QStringLiteral("PSS2 同一棵树连拍两次，结果一模一样"),
          QStringLiteral("entries=%1").arg(second.size()));

    // ------------------------------------------------------------------
    // PSS2b. 恢复那条路要再走一遍同样的树
    //
    // `Presets::unchangedInitializationContinuation()` 在"接着装"的时候会重拍一遍
    // 再跟记录里的比 —— 所以这一笔代价**每恢复一次就要付一次**，不是只付一次。
    // ------------------------------------------------------------------
    {
        // `originals` 是另一道闸（要装的那几份核心文件的原件指纹），这里给它一份真的：
        // 这一节量的是"预设目标那一棵树的第二遍"，不是别的闸。
        const QJsonObject originals{
            {common + QStringLiteral("/options.txt"),
             DownloadCheckpoint::fingerprint(common + QStringLiteral("/options.txt"))}};
        const QJsonObject intent{{QStringLiteral("kind"), QStringLiteral("named-vanilla")},
                                 {QStringLiteral("root"), base},
                                 {QStringLiteral("instance"), QStringLiteral("common")},
                                 {QStringLiteral("originals"), originals},
                                 {QStringLiteral("presets"), QJsonArray{}}};
        QElapsedTimer clock;
        clock.start();
        const bool unchanged = Presets::unchangedInitializationContinuation(intent);
        const qint64 us = clock.nsecsElapsed() / 1000;
        check(unchanged, QStringLiteral("PSS2b 没写预设的那份意图不重拍这棵树（只核对 originals）"),
              QStringLiteral("us=%1").arg(us));
    }
    {
        const QJsonObject originals{
            {common + QStringLiteral("/options.txt"),
             DownloadCheckpoint::fingerprint(common + QStringLiteral("/options.txt"))}};
        const QJsonObject intent{{QStringLiteral("kind"), QStringLiteral("named-vanilla")},
                                 {QStringLiteral("root"), base},
                                 {QStringLiteral("instance"), QStringLiteral("common")},
                                 {QStringLiteral("originals"), originals},
                                 {QStringLiteral("presets"), QJsonArray{QJsonObject{{QStringLiteral("id"),
                                                                                     QStringLiteral("x")}}}},
                                 {QStringLiteral("presetTargets"), first}};
        QElapsedTimer clock;
        clock.start();
        const bool unchanged = Presets::unchangedInitializationContinuation(intent);
        const qint64 us = clock.nsecsElapsed() / 1000;
        check(unchanged, QStringLiteral("PSS2c 写了预设的那份意图：重拍一遍、跟记录里的比，没变就放行"),
              QStringLiteral("us=%1").arg(us));
        printf("MEASURE preset-snapshot-unchanged entries=%d unchanged_us=%lld\n",
               int(first.size()), static_cast<long long>(us));
    }

    // ------------------------------------------------------------------
    // PSS2d. 拿一个已经取消的令牌进去：**当场回空**，不许先把整棵树走完
    // ------------------------------------------------------------------
    {
        std::atomic_bool stopped{true};
        check(Presets::snapshotInitializationTargets(common, &stopped).isEmpty(),
              QStringLiteral("PSS2d 令牌已经置位时快照当场回空（不白走一遍树）"));
    }

    // ------------------------------------------------------------------
    // PSS3. 一个"还没有这些东西"的实例目录：这一拍几乎不花时间
    //
    // 反过来说：常规首装（新实例、还没装过模组）**不该**为这一条去起后台线程。
    // ------------------------------------------------------------------
    const QString empty = base + QStringLiteral("/empty");
    QDir().mkpath(empty);
    const auto emptyWalk = timedSnapshot(empty);
    check(emptyWalk.entries > 0,
          QStringLiteral("PSS3 空实例也拍得出那几格（缺的文件记成 exists:false）"),
          QStringLiteral("entries=%1").arg(emptyWalk.entries));

    // ------------------------------------------------------------------
    // PSS4. 一个大资源包：代价是**按字节线性**的
    //
    // 这一格是"要不要挪到后台"的真正理由：几 MB 的时候谁都不在乎，一百 MB 的
    // 光影包/资源包在界面线程上算一整遍 SHA-256 就是能感觉到的顿。
    // ------------------------------------------------------------------
    const QString big = base + QStringLiteral("/big");
    fillFile(big + QStringLiteral("/resourcepacks/huge.zip"), 64LL * 1024 * 1024);
    const auto bigWalk = timedSnapshot(big);
    check(bigWalk.entries > 0, QStringLiteral("PSS4 一百 MB 级的资源包也拍得下来"),
          QStringLiteral("entries=%1").arg(bigWalk.entries));

    printf("MEASURE preset-snapshot common_files=500 common_us=%lld empty_us=%lld big_bytes=%lld big_us=%lld\n",
           static_cast<long long>(commonWalk.us), static_cast<long long>(emptyWalk.us),
           static_cast<long long>(64LL * 1024 * 1024), static_cast<long long>(bigWalk.us));

    QDir(base).removeRecursively();
}
