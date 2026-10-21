#include "Support.hpp"

void runDefaultSkinTests(const QString &sandbox) {
    section("ZF. 离线名字 → 官方默认皮肤（Java 哈希 + 两套规矩 + 真 JAR）");

    // 前提：这一节直接拼 sandbox 底下的路径，而 cachePathFor() 走 AppPaths::skinsDir()。
    // 两边必须指的是同一份 .minecraft —— 前面哪一节要是忘了把数据根目录换回来，
    // 那一条得先红，不然下面的失败全都会指向错的地方。
    check(AppPaths::minecraftDir() == sandbox, "ZF0 前提：数据根目录就是本工程的沙箱",
          AppPaths::minecraftDir());

    // 名字 | uuid（去横线的 32 位） | Java 的 hashCode | 老规矩走 alex 吗 | 18 元表下标
    struct Truth {
        const char *name;
        const char *uuid;
        int hash;
        bool oldSlim;
        int index;
    };
    const Truth truths[] = {
        {"mwcheck", "812a1f97218b36f699f24550d02d507b", -377602998, false, 12},
        {"RandSkin", "e65be28925ff3a6f85e13db168f27f93", 783784644, false, 6},
        {"Steve", "5627dd98e6be3c21b8a8e92344183641", 1277771483, true, 11},
        {"Zed", "15e4f325c74836a38c9ae454e9162eca", -1222635752, false, 10},
        // 下标 0 和 17 是**表的两端**：分开覆盖才证得了"名字是按 index % 9 取的、
        // 窄宽是按 index < 9 判的"，只测中间那几个的话两边都对得上就看不出来。
        {"Mine", "f2de408889f93162b33450698f1c6a53", 1192184784, false, 0},
        {"有 空格", "6d120cb0cacc3730959a60976be407a6", 1503681713, true, 17},
        {"玩家", "511a6918f6693b23ba72decf60a9cb59", 2108180397, true, 3},
        {"测试玩家", "74fa8980f33435d2ad59af30e3c2f0cb", -917118039, true, 15},
        {"Notch", "b50ad385829d3141a2167e7d7539ba7f", -524802362, false, 4},
    };
    const QStringList names = DefaultSkin::skinNames();

    // ---- ZF1 名字表（新规矩那 18 条的名字顺序，唯一出处） ----
    check(names == QStringList({QStringLiteral("alex"), QStringLiteral("ari"), QStringLiteral("efe"),
                                QStringLiteral("kai"), QStringLiteral("makena"),
                                QStringLiteral("noor"), QStringLiteral("steve"),
                                QStringLiteral("sunny"), QStringLiteral("zuri")}),
          "ZF1 名字表那 9 个、顺序和游戏里那个数组一样（1.20.1 常量池里逐个抄的）",
          names.join(QStringLiteral(", ")));
    check(DefaultSkin::eighteenCount() == 18,
          "ZF1 一次要点 18 张 = 9 个名字 × {窄, 宽}（18 不是另一个常量，是从表长出来的）",
          QString::number(DefaultSkin::eighteenCount()));

    // ---- ZF2 Java 那个 UUID.hashCode()（**逐位**要和 JVM 一样） ----
    {
        QString mismatch;
        for (const Truth &t : truths) {
            const QString name = QStringLiteral("%1").arg(QString::fromUtf8(t.name));
            // 游戏算 uuid 那一句：md5("OfflinePlayer:" + 名字) 再摆版本位/变体位。
            // **中文名字也要对**（所以真值表里放了两个）：错一个字节就是另一张脸。
            const QString uuid = AccountStore::offlineUuidFor(name);
            if (uuid != QString::fromLatin1(t.uuid)) {
                mismatch = QStringLiteral("%1 的 uuid 是 %2，JVM 给的是 %3")
                               .arg(name, uuid, QString::fromLatin1(t.uuid));
                break;
            }
            int hash = 0;
            if (!DefaultSkin::uuidHashCode(uuid, &hash) || hash != t.hash) {
                mismatch = QStringLiteral("%1 的 hashCode 是 %2，JVM 给的是 %3")
                               .arg(name)
                               .arg(hash)
                               .arg(t.hash);
                break;
            }
            // 带横线的写法（AuthManager::playerUuid() 给的那种）必须是同一个哈希。
            int dashedHash = 0;
            const QString dashed = uuid.left(8) + QLatin1Char('-') + uuid.mid(8, 4) +
                                   QLatin1Char('-') + uuid.mid(12, 4) + QLatin1Char('-') +
                                   uuid.mid(16, 4) + QLatin1Char('-') + uuid.mid(20);
            if (!DefaultSkin::uuidHashCode(dashed, &dashedHash) || dashedHash != t.hash) {
                mismatch = QStringLiteral("%1 带横线的 uuid 算出来是 %2（不带横线是 %3）")
                               .arg(name)
                               .arg(dashedHash)
                               .arg(t.hash);
                break;
            }
        }
        check(mismatch.isEmpty(),
              "ZF2 9 个名字（含两个中文名）的 uuid 和 hashCode 与 JVM 逐位一致", mismatch);

        int ignored = 0;
        check(!DefaultSkin::uuidHashCode(QStringLiteral("不是uuid"), &ignored) &&
                  !DefaultSkin::uuidHashCode(QString(), &ignored) &&
                  !DefaultSkin::uuidHashCode(QStringLiteral("812a1f97-218b-36f6-99f2-4550d02d50"), &ignored),
              "ZF2 认不出形状的一律返回 false（少一位也不行）—— 不悄悄补零凑一个数出来");
    }

    // ---- ZF3 新规矩：18 元表的下标 ----
    {
        QString wrong;
        for (const Truth &t : truths) {
            const QString uuid = QString::fromLatin1(t.uuid);
            const DefaultSkin::Prediction p = DefaultSkin::predict(uuid, DefaultSkin::Rule::Eighteen);
            const QString expectedEntry =
                QStringLiteral("assets/minecraft/textures/entity/player/%1/%2.png")
                    .arg(t.index < 9 ? QStringLiteral("slim") : QStringLiteral("wide"),
                         names.at(t.index % 9));
            if (!p.valid || p.index != t.index || p.entryPath != expectedEntry ||
                p.slim != (t.index < 9) || p.name != names.at(t.index % 9)) {
                wrong = QStringLiteral("%1: 下标 %2（JVM 说 %3）、路径 %4")
                            .arg(QString::fromUtf8(t.name))
                            .arg(p.index)
                            .arg(t.index)
                            .arg(p.entryPath);
                break;
            }
        }
        check(wrong.isEmpty(), "ZF3 9 个名字的下标 / 名字 / 窄宽 / 条目路径全对（下界 0 和上界 17 都在里面）",
              wrong);

        // 负数哈希要**掰回** [0, 18)：Java 的 Math.floorMod 是这么定义的，C++ 的 % 不是。
        // mwcheck 的哈希是 -377602998，直接取 % 会得到 -12。
        const DefaultSkin::Prediction mwcheck =
            DefaultSkin::predict(QString::fromLatin1("812a1f97218b36f699f24550d02d507b"),
                                 DefaultSkin::Rule::Eighteen);
        check(mwcheck.index == 12 && mwcheck.entryPath.endsWith(QStringLiteral("wide/kai.png")),
              "ZF3 负数哈希按 floorMod 掰回正数（mwcheck → 12 → wide/kai，不是 -12）",
              QString::number(mwcheck.index));

        check(!DefaultSkin::predict(QString::fromLatin1("812a1f97218b36f699f24550d02d507b"),
                                    DefaultSkin::Rule::Unknown)
                   .valid,
              "ZF3 没定下规矩（Unknown）时不算 —— 不当成「随便挑一套」");
    }

    // ---- ZF4 老规矩：只有两张，按哈希的最低位 ----
    {
        QString wrong;
        for (const Truth &t : truths) {
            const DefaultSkin::Prediction p =
                DefaultSkin::predict(QString::fromLatin1(t.uuid), DefaultSkin::Rule::Two);
            const QString expectedEntry = QStringLiteral("assets/minecraft/textures/entity/%1.png")
                                              .arg(t.oldSlim ? QStringLiteral("alex")
                                                             : QStringLiteral("steve"));
            if (!p.valid || p.slim != t.oldSlim || p.entryPath != expectedEntry || p.index != -1) {
                wrong = QStringLiteral("%1: %2").arg(QString::fromUtf8(t.name), p.entryPath);
                break;
            }
        }
        check(wrong.isEmpty(),
              "ZF4 老规矩只按最低位分 alex(窄) / steve(宽)，下标固定 -1（字节码里 alex 就是 slim）",
              wrong);
    }

    // ---- ZF5 缓存文件名（读和写的唯一出处） ----
    {
        const QString uuid = QString::fromLatin1("f2de408889f93162b33450698f1c6a53");
        const QString modern = DefaultSkin::cachePathFor(
            uuid, QStringLiteral("1.20.1"),
            QStringLiteral("assets/minecraft/textures/entity/player/slim/alex.png"));
        const QString old = DefaultSkin::cachePathFor(
            uuid, QStringLiteral("1.18.2"), QStringLiteral("assets/minecraft/textures/entity/alex.png"));
        check(QFileInfo(modern).absolutePath() == QDir::cleanPath(sandbox + QStringLiteral("/skins")),
              "ZF5 落在 <数据目录>/skins 底下（**只进缓存，不缝进资源**）",
              QFileInfo(modern).absolutePath());
        check(QFileInfo(modern).fileName() ==
                  QStringLiteral("default-f2de408889f93162b33450698f1c6a53-") +
                      QString::fromLatin1(QCryptographicHash::hash(QByteArrayLiteral("1.20.1"),
                                                                   QCryptographicHash::Md5)
                                              .toHex()
                                              .left(12)) +
                      QStringLiteral("-player-slim-alex.png"),
              "ZF5 名字里有账号键 + 版本键 + 条目别名三段",
              QFileInfo(modern).fileName());
        // 版本键那一段**必须有**：换到低版本算出来的是另一条路径的另一张图，
        // 没有它两次算出来的缓存会撞在同一个文件名上（旧那张会被当成"算过了"）。
        check(modern != old && old.endsWith(QStringLiteral("-alex.png")),
              "ZF5 换了版本 → 换一个文件名（老规矩那张的别名是 alex，不是同一个）", old);
        check(DefaultSkin::cachePathFor(QStringLiteral("不是uuid"), QStringLiteral("1.20.1"),
                                        QStringLiteral("assets/minecraft/textures/entity/alex.png"))
                      .isEmpty() &&
                  DefaultSkin::cachePathFor(uuid, QString(), QStringLiteral("x.png")).isEmpty(),
              "ZF5 账号认不出来 / 没选中版本 → 不拼路径（不拼一个野名字出来）");
    }

    // ---- ZF6 真 JAR：新规矩命中，而且**第二次不碰 JAR** ----
    {
        const QString playerName = QStringLiteral("Mine");  // 下标 0 → slim/alex
        const QByteArray png = makePngBytes(QColor(0x20, 0x80, 0xC0));
        const QString versionId = QStringLiteral("1.20.1");
        const QString jar = sandbox + QStringLiteral("/versions/") + versionId + QLatin1Char('/') +
                            versionId + QStringLiteral(".jar");
        const bool made =
            makeFakeJar(jar, {{QStringLiteral("assets/minecraft/textures/entity/player/slim/alex.png"),
                               png}});
        check(made, "ZF6 前提：假 JAR 造出来了（真 zip，条目名带斜杠）", jar);
        if (made) {
            QString path;
            bool slim = false;
            QString reason;
            const DefaultSkin::Result first =
                DefaultSkin::resolve(playerName, versionId, &path, &slim, &reason);
            check(first == DefaultSkin::Result::Resolved && slim && !path.isEmpty(),
                  "ZF6 新规矩那条从 JAR 里取到了，而且模型是窄的（下标 0）",
                  QStringLiteral("%1 / %2").arg(int(first)).arg(reason));
            // 取出来的**就是那张图**：不是"文件在那儿"就算数（尺寸和像素都量一遍）。
            const QImage written(path);
            check(!written.isNull() && written.size() == QSize(64, 64) &&
                      written.pixel(8, 8) == QColor(0x20, 0x80, 0xC0).rgba(),
                  "ZF6 落盘的那张图就是 JAR 里那张（64×64、像素值也对）",
                  QStringLiteral("%1 字节 / %2x%3")
                      .arg(QFileInfo(path).size())
                      .arg(written.width())
                      .arg(written.height()));

            // **把 JAR 整个删掉**再算一次。这一条是"缓存命中不碰 JAR"的真证据 ——
            // 按耗时去断言是不稳的，把源抽掉则是确定的。
            QDir(sandbox + QStringLiteral("/versions/") + versionId).removeRecursively();
            QString again, againReason;
            bool againSlim = false;
            const DefaultSkin::Result second =
                DefaultSkin::resolve(playerName, versionId, &again, &againSlim, &againReason);
            check(second == DefaultSkin::Result::Resolved && again == path && againSlim,
                  "ZF6 JAR 删掉之后照样算得出来（缓存命中 = 两次 QFile::exists，不起进程）",
                  againReason);
        }
    }

    // ---- ZF7 真 JAR：老规矩命中，而且换版本**不会**拿上一份缓存顶上 ----
    {
        const QString playerName = QStringLiteral("Mine");  // 老规矩：哈希偶数 → steve（宽）
        const QString versionId = QStringLiteral("1.18.2");
        const QString jar = sandbox + QStringLiteral("/versions/") + versionId + QLatin1Char('/') +
                            versionId + QStringLiteral(".jar");
        const bool made = makeFakeJar(
            jar, {{QStringLiteral("assets/minecraft/textures/entity/steve.png"),
                   makePngBytes(QColor(0x90, 0x60, 0x30))},
                  // 顺带放一个"新规矩那条路径"的条目，但**不是**这个玩家要的那一条
                  // （每个名字要哪一条是算出来的，所以这一条必须与他无关）。
                  {QStringLiteral("assets/minecraft/textures/entity/player/wide/zuri.png"),
                   makePngBytes(QColor(0xFF, 0x00, 0xFF))}});
        check(made, "ZF7 前提：老规矩那个假 JAR 造出来了", jar);
        if (made) {
            QString path;
            bool slim = true;
            QString reason;
            const DefaultSkin::Result result =
                DefaultSkin::resolve(playerName, versionId, &path, &slim, &reason);
            const QString modernPath = DefaultSkin::cachePathFor(
                AccountStore::offlineUuidFor(playerName), QStringLiteral("1.20.1"),
                QStringLiteral("assets/minecraft/textures/entity/player/slim/alex.png"));
            check(result == DefaultSkin::Result::Resolved && !slim &&
                      path.endsWith(QStringLiteral("-steve.png")),
                  "ZF7 新规矩那条取不到 → 换老规矩那条（steve，宽模型）",
                  QStringLiteral("%1 / %2").arg(path, reason));
            // 上一节那份现代的缓存**还在盘上**，但这一趟绝不能拿它顶上：它算的是另一个版本。
            check(QFile::exists(modernPath) && path != modernPath,
                  "ZF7 同一账号换到低版本 → 拿到的是低版本那张，上一份缓存还在但没被拿去顶");
            // 这一条把"判据是 JAR"钉死：这个 JAR 里**有一个**新规矩的条目（wide/zuri），
            // 但按名字算出来要的不是它 —— 判据是"有没有**这一条**"，不是"有没有新规矩的目录"。
            check(!path.endsWith(QStringLiteral("zuri.png")),
                  "ZF7 不是「JAR 里有新规矩的条目就算新规矩」：要的是**这个名字对应的那一条**");
        }
    }

    // ---- ZF8 取不到 / 坏掉，各是哪一种 ----
    {
        // ① 两条都没有（不是原版客户端：第三方安装器做的薄壳 JAR 就长这样）→ Unavailable，
        // 这是**常态**：调用方会退回"随机挑一张"，不报错。
        const QString emptyId = QStringLiteral("1.19.9-noskin");
        const QString emptyJar = sandbox + QStringLiteral("/versions/") + emptyId + QLatin1Char('/') +
                                 emptyId + QStringLiteral(".jar");
        const bool emptyMade =
            makeFakeJar(emptyJar, {{QStringLiteral("README.txt"), QByteArrayLiteral("nope")}});
        QString path;
        bool slim = false;
        QString reason;
        const DefaultSkin::Result none =
            DefaultSkin::resolve(QStringLiteral("Mine"), emptyId, &path, &slim, &reason);
        check(emptyMade && none == DefaultSkin::Result::Unavailable && path.isEmpty() &&
                  !reason.isEmpty(),
              "ZF8 JAR 里两条都没有 → Unavailable（**常态**：退回随机那张，不记日志不弹窗）", reason);

        // ② 没有选中版本 → Unavailable（用户一个版本都没装，或者正在装）。
        const DefaultSkin::Result noVersion =
            DefaultSkin::resolve(QStringLiteral("Mine"), QString(), &path, &slim, &reason);
        check(noVersion == DefaultSkin::Result::Unavailable && path.isEmpty(),
              "ZF8 没选中任何已安装版本 → Unavailable", reason);

        // ③ 那个 JAR 根本不是 zip（文件坏了 / 半截）→ Failed。和"没有这一条"分开：
        // 前者值得记一行日志，后者是常态。
        const QString brokenId = QStringLiteral("1.20.6-notzip");
        const QString brokenJar = sandbox + QStringLiteral("/versions/") + brokenId +
                                  QLatin1Char('/') + brokenId + QStringLiteral(".jar");
        QDir().mkpath(QFileInfo(brokenJar).absolutePath());
        bool brokenWritten = false;
        {
            QFile file(brokenJar);
            brokenWritten = file.open(QIODevice::WriteOnly);
            if (brokenWritten) {
                file.write(QByteArrayLiteral("这不是 zip"));
                file.close();
            }
        }
        const DefaultSkin::Result broken =
            DefaultSkin::resolve(QStringLiteral("Mine"), brokenId, &path, &slim, &reason);
        check(brokenWritten && broken == DefaultSkin::Result::Failed && path.isEmpty(),
              "ZF8 那个 JAR 打不开 → Failed（和「里面没有这一条」是两件事）", reason);
        // 那句话**不许是空的**：它最后会落成日志里那行「算离线默认皮肤失败：…」，而这句话
        // 的全部意义就是"怎么老是随机那张"有个交代。NotAnArchive 这一档 ZipUtil 不给
        // 原因（退出码 4 就是终点，连异常文字都没有），照抄就把那行日志留成一个悬空的冒号
        // —— 这一条正是为它写的（改之前这里 reason 就是空串）。
        check(broken != DefaultSkin::Result::Failed || !reason.trimmed().isEmpty(),
              "ZF8 而且 Failed 那句原因不能是空的（日志那行不许是个悬空的冒号）",
              QStringLiteral("「%1」").arg(reason));

        // ④ 条目在、但不是一张图 → Failed，而且**盘上不许留下东西**：写下去的话
        // "文件存在"就等于"已经算好了"，那张坏图会永远躺在缓存里（每次开启动器都拿它
        // 解码、每次失败，而且再没有哪一趟来纠正它）。
        const QString badPngId = QStringLiteral("1.20.7-badpng");
        const QString badPngJar = sandbox + QStringLiteral("/versions/") + badPngId +
                                  QLatin1Char('/') + badPngId + QStringLiteral(".jar");
        const bool badMade = makeFakeJar(
            badPngJar, {{QStringLiteral("assets/minecraft/textures/entity/player/slim/alex.png"),
                         QByteArrayLiteral("<html>Just a moment...</html>")}});
        const DefaultSkin::Result badPng =
            DefaultSkin::resolve(QStringLiteral("Mine"), badPngId, &path, &slim, &reason);
        const QString badPath = DefaultSkin::cachePathFor(
            AccountStore::offlineUuidFor(QStringLiteral("Mine")), badPngId,
            QStringLiteral("assets/minecraft/textures/entity/player/slim/alex.png"));
        check(badMade && badPng == DefaultSkin::Result::Failed && !QFile::exists(badPath),
              "ZF8 那条不是 PNG → Failed，而且一个字都不落盘（坏图进了缓存就再也修不回来）",
              reason);
    }

    // 收拾自己造的东西：假版本目录 + 这个账号的缓存图。
    for (const QString &id : {QStringLiteral("1.20.1"), QStringLiteral("1.18.2"),
                              QStringLiteral("1.19.9-noskin"), QStringLiteral("1.20.6-notzip"),
                              QStringLiteral("1.20.7-badpng")}) {
        QDir(sandbox + QStringLiteral("/versions/") + id).removeRecursively();
    }
    for (const QString &entry : QDir(sandbox + QStringLiteral("/skins"))
                                    .entryList({QStringLiteral("default-*")}, QDir::Files)) {
        QFile::remove(sandbox + QStringLiteral("/skins/") + entry);
    }
    check(!QFile::exists(sandbox + QStringLiteral("/versions/1.20.1")),
          "ZF9 假版本目录收干净了（后面那些节扫的是同一份沙箱）");
}

