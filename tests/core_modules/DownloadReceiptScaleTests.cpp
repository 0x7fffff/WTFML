// 恢复记录在**大批量**下的两笔账：存一趟、读一趟。
//
// 为什么单开一节：`DownloadCheckpoint::save()` 是**同步跑在 GUI 线程上**的
// （`DownloadManager::pumpDownloadQueue()` 那一段就是它），而恢复记录的上限是
// 50,000 条 / 32 MiB。一张 5 万条的记录到底多大、存一趟要多久，只有量出来才知道
// 值不值得挪到后台；拍脑袋说"很慢"或"没事"都没根据。
//
// 这一节**只量、不拿耗时当断言** —— 那是这台机器的磁盘和 DPAPI 的速度，不是产品的
// 性质（同一份代码在开发机上和 CI runner 上差几倍都正常）。钉形状的那几条才是断言：
// 5 万条往返之后逐格还在、第 50001 条拒收、投毒那份文件拒读且**一个字节都不动**、
// 升级前写的那一档（未压缩）照样读得回来。
//
// 量出来的头一条结论已经落进产品：老那一档（明文 JSON 直接 DPAPI）在 5 万条上要 1.3 秒，
// 其中八成是加密；同一份清单 zlib 压一遍只要 35 毫秒、体积缩到十六分之一，压完再加密
// 一共 342 ms。所以盘上现在写的是 `WTFML-PROTECTED-2`（见 `encodeReceipt`），
// 读侧两档都认。
//
// 为什么在这里而不是 mw_check：这一节一个 QWidget 都不碰、一个网络都不开，
// 只要一个临时目录。放核心模块里它跟 `wtfml.core-downloads` 一起跑，谁改了
// `save()`/`load()` 的代价或形状都当场看得见。

#include "Support.hpp"
#include "download/DownloadCheckpoint.hpp"

#include <cstdio>

namespace {

/// 造第 index 条恢复记录该有的样子 —— **照真批次里的那份填**。
///
/// 五格都得是 `load()` 认的形状（`add()` 不校验，`load()` 才校验）：
/// 绝对且已 cleanPath 的路径、带 host 的 http(s) 地址、40 位 sha1、
/// 以及一份 `{"exists":true,"size":"…","sha256":"…"}` 的原件指纹。
DownloadCheckpoint::File receiptEntry(int index, const QString &root) {
    const QString number = QStringLiteral("%1").arg(index, 5, 10, QChar(QLatin1Char('0')));
    const QString stem = QStringLiteral("artifact-%1-1.0-%1.jar").arg(number);
    const QString relative = QStringLiteral("net/example/artifact-%1/1.0/%2").arg(number, stem);
    DownloadCheckpoint::File file;
    file.url = QStringLiteral("https://libraries.example.net/") + relative;
    file.path = QDir::cleanPath(root + QStringLiteral("/libraries/") + relative);
    file.sha1 = QString(QStringLiteral("%1").arg(index, 40, 16, QLatin1Char('0'))).left(40);
    file.size = 4096 + index;
    file.original = QJsonObject{{QStringLiteral("exists"), true},
                                {QStringLiteral("size"), QString::number(file.size)},
                                {QStringLiteral("sha256"), QString(64, QChar('a' + index % 6))}};
    return file;
}

/// 照 `save()` 那五格搭一份明文 JSON（一份 Array，不是整份文档）。
///
/// 两处用它：拆开计时那一段（"搭 Array" 和 "转字节"分开量），以及"老那一档读得回来"
/// 那条夹具（要手写一份 `WTFML-PROTECTED-1` 的战前文件）。
QJsonArray receiptRows(const QList<DownloadCheckpoint::File> &files) {
    QJsonArray rows;
    for (const auto &f : files) {
        rows.append(QJsonObject{{QStringLiteral("url"), f.url}, {QStringLiteral("path"), f.path},
                                {QStringLiteral("sha1"), f.sha1},
                                {QStringLiteral("size"), QString::number(f.size)},
                                {QStringLiteral("official"), f.official},
                                {QStringLiteral("original"), f.original}});
    }
    return rows;
}

/// 把一份记录里某一条的五格压成一行，用来做"往返之后逐格还在"的比较。
/// NOTE: `QJsonObject` 没有值语义的 `==`，比它是同一件事，而且失败时能直接打印出来。
QString entryText(const DownloadCheckpoint::File &file) {
    return QStringList{file.url, file.path, file.sha1, QString::number(file.size),
                       QString::fromUtf8(QJsonDocument(file.original).toJson(QJsonDocument::Compact))}
        .join(QLatin1Char('|'));
}

}  // namespace

void runDownloadReceiptScaleTests(const QString &sandbox) {
    section(QStringLiteral("DRS 恢复记录的大批量代价与形状"));
    const QString root = sandbox + QStringLiteral("/receipt-scale");
    QDir(root).removeRecursively();
    QDir().mkpath(root);

    // ------------------------------------------------------------------
    // DRS1. 上限那一格：第 50001 条必须当场拒收
    //
    // 先钉这一条再量，是因为下面那 5 万条就是**照着上限填满**的 —— 上限要是哪天
    // 松了（比如 50000 写成 500000），这一节量的就变成另一个东西了。
    // ------------------------------------------------------------------
    DownloadCheckpoint full;
    full.path = root + QStringLiteral("/full.receipt");
    QElapsedTimer addClock;
    addClock.start();
    int accepted = 0;
    for (int i = 0; i < DownloadCheckpoint::maximumFiles; ++i) {
        if (full.add(receiptEntry(i, root))) {
            ++accepted;
        }
    }
    const qint64 addUs = addClock.nsecsElapsed() / 1000;
    check(accepted == DownloadCheckpoint::maximumFiles,
          QStringLiteral("DRS1 前 %1 条一条不少地收下").arg(DownloadCheckpoint::maximumFiles),
          QStringLiteral("accepted=%1").arg(accepted));
    check(!full.add(receiptEntry(DownloadCheckpoint::maximumFiles, root)) &&
              full.files.size() == DownloadCheckpoint::maximumFiles,
          QStringLiteral("DRS2 第 %1 条被上限挡下、表里不多不少")
              .arg(DownloadCheckpoint::maximumFiles + 1));

    // ------------------------------------------------------------------
    // DRS3. 写一趟：序列化 + DPAPI 加密 + 原子写，全在调用它的那条线程上
    //
    // 量的是**整一趟**，不是其中一段：真机上 GUI 卡住的时长就是它。
    // ------------------------------------------------------------------
    QElapsedTimer saveClock;
    saveClock.start();
    const bool saved = full.save();
    const qint64 saveUs = saveClock.nsecsElapsed() / 1000;
    const qint64 receiptBytes = QFileInfo(full.path).size();
    check(saved && receiptBytes > 0,
          QStringLiteral("DRS3 %1 条的恢复记录写得进去").arg(DownloadCheckpoint::maximumFiles),
          QStringLiteral("bytes=%1").arg(receiptBytes));
    check(!full.dirty, QStringLiteral("DRS4 写成功之后脏标记清掉"));

    // 盘上那一份必须是**密文**（DPAPI 之后的那一版），而且 5 万条这么大的必须走
    // **压缩过的那一档**（`WTFML-PROTECTED-2`）：DPAPI 是按时长里最大的一块，压完再加密
    // 才不会被这 22 MB 拖上一秒多（见下面 `receipt-breakdown` 那几格）。
    {
        QFile stored(full.path);
        check(stored.open(QIODevice::ReadOnly) &&
                  stored.read(18) == QByteArray(DownloadCheckpoint::compressedMagic,
                                                int(sizeof(DownloadCheckpoint::compressedMagic)) - 1),
              QStringLiteral("DRS5 5 万条那一份写的是压缩过的那一档（密文，不是明文 JSON）"));
    }

    // ------------------------------------------------------------------
    // DRS6. 读一趟：读文件 + DPAPI 解密 + JSON 解析 + 逐条校验
    //
    // 跨重启「接着下」走的就是它（`restoreBatch()` → `load()`），也是同步的。
    // ------------------------------------------------------------------
    // 读之前把写它那一位手里的锁放开：`load()` 要的正是同一把 `<路径>.lock`
    // （产品的跨重启那一趟也是"旧进程早没了、新对象重新拿锁"）。
    full.lock.reset();
    QElapsedTimer loadClock;
    loadClock.start();
    DownloadCheckpoint reloaded;
    const bool loaded = reloaded.load(full.path);
    const qint64 loadUs = loadClock.nsecsElapsed() / 1000;
    check(loaded && reloaded.files.size() == full.files.size(),
          QStringLiteral("DRS6 %1 条读得回来").arg(DownloadCheckpoint::maximumFiles),
          QStringLiteral("files=%1").arg(reloaded.files.size()));
    if (loaded) {
        // 逐格对：开头、中间、最后各一条 —— 这三个下标正好能抓到"只存了头尾"
        // 和"表头数对了但内容错位"这两类错法。
        const int picks[] = {0, DownloadCheckpoint::maximumFiles / 2,
                             DownloadCheckpoint::maximumFiles - 1};
        bool same = true;
        QString firstMismatch;
        for (const int index : picks) {
            const QString expected = entryText(full.files.at(index));
            const QString actual = entryText(reloaded.files.at(index));
            if (expected != actual && same) {
                same = false;
                firstMismatch = QStringLiteral("index=%1").arg(index);
            }
        }
        check(same, QStringLiteral("DRS7 开头 / 中间 / 最后三条逐格读回来一模一样"), firstMismatch);
        const auto *found = reloaded.find(full.files.at(1).path);
        check(found != nullptr && entryText(*found) == entryText(full.files.at(1)),
              QStringLiteral("DRS8 按路径查得回那一条（`find()` 的下标表也重建了）"));
        check(reloaded.context.isEmpty() && full.context.isEmpty(),
              QStringLiteral("DRS9 这一份没写 context，读回来也是空的"));
    }

    // ------------------------------------------------------------------
    // DRS10. 一份投毒的文件：拒读，而且**一个字节都不动**
    //
    // 「原件保留」是这一层的一条硬约束：读不懂的恢复记录不许被"顺手清掉" ——
    // 用户那一份才是出事之后唯一能拿来对的东西。
    // ------------------------------------------------------------------
    const QString poisonedPath = root + QStringLiteral("/poisoned.receipt");
    const QByteArray poisoned = QByteArrayLiteral("WTFML-PROTECTED-1\nnot really a receipt");
    writeFileAt(poisonedPath, poisoned);
    DownloadCheckpoint poisonedReceipt;
    check(!poisonedReceipt.load(poisonedPath),
          QStringLiteral("DRS10 解不开的恢复记录拒读"));
    check(readBytesAt(poisonedPath) == poisoned,
          QStringLiteral("DRS11 拒读之后盘上那份一个字节都没变"));

    // ------------------------------------------------------------------
    // DRS11b. 压缩头里那个"解压后多长"跟着一起被投毒：**按它先挡下来**，不许真去解
    //
    // `qCompress` 在正文前面写了 4 个字节的"解压后长度"，`qUncompress` 会照着它要内存。
    // 一份记录被改过（或者别的东西伪装成记录）的时候，那个数可能是 4 GB —— 先比
    // `maximumBytes` 再解，代价是四个字节的读取。
    // ------------------------------------------------------------------
    {
        const QString bombPath = root + QStringLiteral("/bomb.receipt");
        QByteArray header(QByteArrayLiteral("\xff\xff\xff\xff"));
        header.append(QByteArray(64, 'x'));
        const auto blob = TokenStore::protect(header);
        check(!blob.isEmpty(), QStringLiteral("DRS11b 前提：那份投毒文件造得出来"));
        const QByteArray protectPrefix(DownloadCheckpoint::plainMagic,
                                       int(sizeof(DownloadCheckpoint::plainMagic)) - 1);
        writeFileAt(bombPath, QByteArray(DownloadCheckpoint::compressedMagic,
                                         int(sizeof(DownloadCheckpoint::compressedMagic)) - 1) +
                                  blob.mid(protectPrefix.size()));
        DownloadCheckpoint bombReceipt;
        check(!bombReceipt.load(bombPath),
              QStringLiteral("DRS11b 解压长度报天文数字的一份当场拒读（不去真解）"));
        check(QFileInfo::exists(bombPath), QStringLiteral("DRS11c 拒读之后那份还在"));
    }

    // ------------------------------------------------------------------
    // DRS12. 老那一档（`WTFML-PROTECTED-1`，未压缩）照样读得回来
    //
    // 用户盘上那些正在暂停中的批次就是那个形状（这一版之前写的）。换了写侧就得把读侧
    // 的旧形状一起留着，否则升级启动器 = 那些批次全部"恢复记录不可读"。
    // ------------------------------------------------------------------
    {
        const QString legacyPath = root + QStringLiteral("/legacy.receipt");
        QList<DownloadCheckpoint::File> sample;
        for (int i = 0; i < 3; ++i) {
            sample.append(receiptEntry(i, root));
        }
        const auto legacyPlain = QJsonDocument(QJsonObject{{QStringLiteral("format"), 1},
                                                           {QStringLiteral("context"), QJsonObject()},
                                                           {QStringLiteral("files"), receiptRows(sample)}})
                                     .toJson(QJsonDocument::Compact);
        writeFileAt(legacyPath, TokenStore::protect(legacyPlain));  // 手写的战前文件
        DownloadCheckpoint legacyReceipt;
        const bool legacyLoaded = legacyReceipt.load(legacyPath);
        check(legacyLoaded && legacyReceipt.files.size() == 3 &&
                  entryText(legacyReceipt.files.at(2)) == entryText(sample.at(2)),
              QStringLiteral("DRS12 升级前写的那一档（未压缩）读得回来"),
              QStringLiteral("files=%1").arg(legacyReceipt.files.size()));
    }

    // ------------------------------------------------------------------
    // 报数：量出来的东西。
    //
    // 用 `printf` 而不是 `qInfo()`：这一套测试工程里 qInfo 会被吞掉（见
    // `tests/mw_check/DownloadBatchChecks.cpp` 里那几条 MEASURE 的同一句说明），
    // 量了个看不见就等于没量。
    // ------------------------------------------------------------------
    printf("MEASURE receipt-scale files=%d add_us=%lld save_us=%lld load_us=%lld bytes=%lld\n",
           int(DownloadCheckpoint::maximumFiles), static_cast<long long>(addUs),
           static_cast<long long>(saveUs), static_cast<long long>(loadUs),
           static_cast<long long>(receiptBytes));

    // ------------------------------------------------------------------
    // 拆开量：老那一档（明文 JSON 直接加密）的 1.3 秒里"哪一段最贵"。
    //
    // 这一段量的是**没有压缩**的那条路 —— 正是它促成了现在盘上那个
    // `WTFML-PROTECTED-2`（压完再加密）：DPAPI 按时长里占了八成，而 22 MB 的 JSON
    // 压成 1.3 MB 只要 35 毫秒，压完之后同一趟加密只剩几毫秒。上面 `save()` 那 342 ms
    // 就是换掉它之后的数。
    //
    // 只量、不断言（这台机器的速度不是产品的性质），但它把"为什么换格式"钉在案上：
    // 谁哪天想把这 35 毫秒的压缩省掉，先看这几格。
    //
    // NOTE: 这里**照 `save()` 那五格另搭一份同样的 JSON**。它不是第二份产品逻辑，
    // 只是把这一趟拆成几块。
    // ------------------------------------------------------------------
    {
        QElapsedTimer partClock;
        partClock.start();
        const auto lap = [&partClock] {
            const qint64 us = partClock.nsecsElapsed() / 1000;
            partClock.restart();
            return us;
        };
        const QJsonArray rows = receiptRows(full.files);
        const qint64 buildUs = lap();
        const auto plain = QJsonDocument(QJsonObject{{QStringLiteral("format"), 1},
                                                     {QStringLiteral("context"), QJsonObject()},
                                                     {QStringLiteral("files"), rows}})
                               .toJson(QJsonDocument::Compact);
        const qint64 toJsonUs = lap();
        const auto protectedBytes = TokenStore::protect(plain);
        const qint64 protectUs = lap();
        const QString probePath = root + QStringLiteral("/probe.receipt");
        QSaveFile probe(probePath);
        probe.setDirectWriteFallback(false);
        const bool wrote = !protectedBytes.isEmpty() && probe.open(QIODevice::WriteOnly) &&
                           probe.write(protectedBytes) == protectedBytes.size() && probe.commit();
        const qint64 writeUs = lap();
        check(wrote, QStringLiteral("DRS14 拆开量的那一份也写得进去"));

        // 读回来那三段：读文件 / DPAPI 解密 / JSON 解析。
        QFile input(probePath);
        const bool opened = input.open(QIODevice::ReadOnly);
        const QByteArray stored = opened ? input.readAll() : QByteArray();
        const qint64 readUs = lap();
        const QByteArray decoded = TokenStore::unprotect(stored);
        const qint64 unprotectUs = lap();
        const auto reparsed = QJsonDocument::fromJson(decoded);
        const qint64 parseUs = lap();
        check(reparsed.object().value(QStringLiteral("files")).toArray().size()
                  == DownloadCheckpoint::maximumFiles,
              QStringLiteral("DRS15 拆开读回来的条数还是满的"));

        // 顺手量一下"压一遍再加密"值不值：DPAPI 那 1 秒多全按字节数收钱，而这一份
        // 22 MB 的 JSON 里九成是重复的键名。压缩本身也在 GUI 线程上，所以两边一起量。
        const QByteArray squeezed = qCompress(plain, 1);
        const qint64 compressUs = lap();
        const auto squeezedProtected = TokenStore::protect(squeezed);
        const qint64 squeezedProtectUs = lap();
        const QByteArray squeezedBack = qUncompress(TokenStore::unprotect(squeezedProtected));
        const qint64 squeezedRoundUs = lap();
        check(squeezedBack == plain, QStringLiteral("DRS16 压一遍再加密、解回来还是原来那份字节"));

        printf("MEASURE receipt-breakdown files=%d build_us=%lld tojson_us=%lld protect_us=%lld "
               "write_us=%lld read_us=%lld unprotect_us=%lld parse_us=%lld plain_bytes=%lld "
               "compress_us=%lld squeezed_bytes=%lld squeezed_protect_us=%lld squeezed_round_us=%lld\n",
               int(DownloadCheckpoint::maximumFiles), static_cast<long long>(buildUs),
               static_cast<long long>(toJsonUs), static_cast<long long>(protectUs),
               static_cast<long long>(writeUs), static_cast<long long>(readUs),
               static_cast<long long>(unprotectUs), static_cast<long long>(parseUs),
               static_cast<long long>(plain.size()), static_cast<long long>(compressUs),
               static_cast<long long>(squeezed.size()),
               static_cast<long long>(squeezedProtectUs),
               static_cast<long long>(squeezedRoundUs));
        QFile::remove(probePath);
    }

    // 一条"真实形状"的对照：一个 5,000 文件的大整合包批次（Forge + 一堆库 + 资源）。
    // 5 万条是上限、平时见不到；这个数才是真机上一趟会碰到的。
    {
        const QString moderatePath = root + QStringLiteral("/moderate.receipt");
        DownloadCheckpoint moderate;
        moderate.path = moderatePath;
        for (int i = 0; i < 5000; ++i) {
            moderate.add(receiptEntry(i, root));
        }
        QElapsedTimer saveClock2;
        saveClock2.start();
        const bool saved2 = moderate.save();
        const qint64 saveUs2 = saveClock2.nsecsElapsed() / 1000;
        const qint64 bytes2 = QFileInfo(moderatePath).size();
        moderate.lock.reset();  // 同上：读它之前先放开那把锁
        QElapsedTimer loadClock2;
        loadClock2.start();
        DownloadCheckpoint moderateLoaded;
        const bool loaded2 = moderateLoaded.load(moderatePath);
        const qint64 loadUs2 = loadClock2.nsecsElapsed() / 1000;
        check(saved2 && loaded2 && moderateLoaded.files.size() == 5000,
              QStringLiteral("DRS13 5,000 条那档也存得进、读得回"),
              QStringLiteral("files=%1").arg(moderateLoaded.files.size()));
        printf("MEASURE receipt-scale files=5000 save_us=%lld load_us=%lld bytes=%lld\n",
               static_cast<long long>(saveUs2), static_cast<long long>(loadUs2),
               static_cast<long long>(bytes2));
    }

    QDir(root).removeRecursively();
}
