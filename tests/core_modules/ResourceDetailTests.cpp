#include "Support.hpp"

void runResourceDetailTests() {
    const QString metaDir = QStringLiteral(REPO_ROOT_PATH) + "/tests/meta";

    section("Z8a. 「多久以前」那几档（「现在」由调用方钉死，不然每一条都会漂）");

    {
        // 固定一个"现在"，然后每档各喂一条。用**同一个** now 去算，边界才不会随当下时间漂。
        const QDateTime now = QDateTime::fromString(QStringLiteral("2026-09-26T12:00:00Z"),
                                                    Qt::ISODate);
        check(now.isValid(), "Z8a 那个固定的「现在」读得出来");
        const auto ago = [&now](qint64 seconds) {
            return now.addSecs(-seconds).toUTC().toString(Qt::ISODate);
        };
        struct Row {
            qint64 seconds;
            const char *want;
        };
        const Row rows[] = {
            {30, "刚刚"},
            {5 * 60, "5 分钟前"},
            {3 * 3600, "3 小时前"},
            {5 * 86400, "5 天前"},
            {40LL * 86400, "1 个月前"},
            {400LL * 86400, "1 年前"},
        };
        int good = 0;
        QStringList seen;
        for (const Row &row : rows) {
            const QString got = ResourceCatalog::relativeTimeText(ago(row.seconds), now);
            seen.append(got);
            if (got == QString::fromUtf8(row.want)) {
                ++good;
            }
        }
        check(good == 6, "Z8a 六档各说各的话", seen.join(QStringLiteral(" / ")));

        // 空串 / 读不懂 / 是**将来**时间：一律回空串（调用点把空串理解成"这一截不显示"）。
        // 将来时间也回空串是刻意的 —— 那说明这台机器的钟或者接口的时间戳有一个是错的，
        // 说「-3 天前」比不说更让人困惑。
        const QString noTime = ResourceCatalog::relativeTimeText(QString());
        const QString garbage = ResourceCatalog::relativeTimeText(QStringLiteral("昨天"));
        const QString future =
            ResourceCatalog::relativeTimeText(now.addDays(3).toUTC().toString(Qt::ISODate), now);
        check(noTime.isEmpty() && garbage.isEmpty() && future.isEmpty(),
              "Z8a 空串 / 读不懂 / 将来时间都回空串（那一截就不显示）",
              QStringLiteral("[%1][%2][%3]").arg(noTime, garbage, future));
    }

    section("Z8b. 发布渠道：那两个字 + 那**一档**（详情页左边那个方块）");

    {
        check(ResourceCatalog::releaseChannelNote(QStringLiteral("beta")) ==
                      QStringLiteral("测试版") &&
                  ResourceCatalog::releaseChannelNote(QStringLiteral("alpha")) ==
                      QStringLiteral("早期测试版"),
              "Z8b 测试版 / 早期测试版各两个字");
        // release 和"认不出来"都回空串：正式版是绝大多数，那两个字不是信息。
        check(ResourceCatalog::releaseChannelNote(QStringLiteral("release")).isEmpty() &&
                  ResourceCatalog::releaseChannelNote(QString()).isEmpty() &&
                  ResourceCatalog::releaseChannelNote(QStringLiteral("rc")).isEmpty(),
              "Z8b 正式版和认不出来的都不写字");

        // 档位：0 绿 / 1 橙 / 2 红（详情页 ResourceDetailMarks::channelColor 按它取色）。
        const int release = ResourceCatalog::releaseChannelRank(QStringLiteral("release"));
        const int beta = ResourceCatalog::releaseChannelRank(QStringLiteral("beta"));
        const int alpha = ResourceCatalog::releaseChannelRank(QStringLiteral("alpha"));
        const int unknown = ResourceCatalog::releaseChannelRank(QStringLiteral("rc"));
        const int empty = ResourceCatalog::releaseChannelRank(QString());
        check(release == 0 && beta == 1 && alpha == 2,
              "Z8b 三档的次序就是 release / beta / alpha",
              QStringLiteral("%1 %2 %3").arg(release).arg(beta).arg(alpha));
        // 认不出来回 **-1**，不是 0。回 0 等于把一条不知道是什么渠道的版本画成绿色的
        // "正式版" —— 那是把"不知道"说成了"很稳"。
        check(unknown == -1 && empty == -1,
              "Z8b 认不出来回 -1（不是 0：那会把它画成绿色的正式版）",
              QStringLiteral("%1 / %2").arg(unknown).arg(empty));
        // 和上面那两个字必须是同一套判据（一个说字、一个说档；错开的话界面上会出现
        // "写着测试版、方块却是绿的"）。
        check((release == 0) == ResourceCatalog::releaseChannelNote(QStringLiteral("release"))
                                    .isEmpty(),
              "Z8b 档位和那两个字说的是同一件事（正式版：无字 + 第 0 档）");
    }

    section("Z8c. 加载器显示名（认不出来原样还回去）");

    check(ResourceCatalog::loaderDisplayName(QStringLiteral("fabric")) ==
                  QStringLiteral("Fabric") &&
              ResourceCatalog::loaderDisplayName(QStringLiteral("NeoForge")) ==
                  QStringLiteral("NeoForge") &&
              ResourceCatalog::loaderDisplayName(QStringLiteral("optifine")) ==
                  QStringLiteral("optifine"),
          "Z8c 四个加载器有自己的写法；没见过的原样还回去（不编、也不空）");

    section("Z8d. 版本行那一行小字（喂真夹具）");

    QByteArray versionsJson;
    {
        QFile fixture(metaDir + "/modrinth-versions-iris.json");
        if (fixture.open(QIODevice::ReadOnly)) {
            versionsJson = fixture.readAll();
            fixture.close();
        }
    }
    const QList<ResourceCatalog::VersionEntry> versions =
        ResourceCatalog::parseVersions(versionsJson);
    check(versions.size() == 12, "Z8d 真夹具读回来 12 条",
          QStringLiteral("%1 条").arg(versions.size()));

    if (versions.size() == 12) {
        // 那一行是**拼出来的**：加载器 / 文件名 / 下载量 / 更新于 / 渠道，各缺各的不写。
        // 逐条查一遍"每一截该在的都在、不该在的都不在"比对着一条写死那句话结实得多
        // （明天接口多一格、少一格，这里不会变成一条假的失败）。
        int complete = 0;
        bool betaHasNote = false;
        bool releaseHasNoNote = true;
        for (const ResourceCatalog::VersionEntry &version : versions) {
            const QString line = ResourceCatalog::versionRowSubText(version);
            const ResourceCatalog::FileEntry *file = version.primaryFile();
            const bool ok = !line.isEmpty() && file != nullptr &&
                            line.contains(file->filename) &&
                            line.contains(ResourceCatalog::loaderDisplayName(
                                version.loaders.value(0)));
            if (ok) {
                ++complete;
            }
            if (version.versionType == QStringLiteral("beta") && line.contains(QStringLiteral("测试版"))) {
                betaHasNote = true;
            }
            if (version.versionType == QStringLiteral("release") &&
                ResourceCatalog::releaseChannelNote(version.versionType).isEmpty()) {
                releaseHasNoNote = !line.contains(QStringLiteral("测试版")) &&
                                   !line.contains(QStringLiteral("早期测试版"));
            }
        }
        check(complete == 12, "Z8d 12 条的小字里都有加载器和文件名", QStringLiteral("%1/12").arg(complete));
        // 夹具里那一条 beta（Z6 钉过它就是最新那条）：「测试版」三个字必须真的出现在那一行。
        check(betaHasNote, "Z8d 测试版那一行的末尾确实写着「测试版」");
        check(releaseHasNoNote, "Z8d 正式版那一行**不写**渠道（那两个字不是信息）");
    }

    section("Z8e. 分组：一组 = 一个游戏版本 + 一个加载器");

    if (versions.size() == 12) {
        const QList<ResourceCatalog::VersionGroup> groups =
            ResourceCatalog::groupVersions(versions, false);
        check(!groups.isEmpty(), "Z8e 分得出组", QStringLiteral("%1 组").arg(groups.size()));

        // 每一个版本都**恰好**进一个组（不是"每个声明过的游戏版本各进一次"——那样同一条会在
        // 一屏里出现好几遍，用户数不清自己到底有几个版本）。
        int inGroups = 0;
        for (const ResourceCatalog::VersionGroup &group : groups) {
            inGroups += group.versions.size();
        }
        check(inGroups == versions.size(), "Z8e 12 条一条不多一条不少地全在组里",
              QStringLiteral("%1/12").arg(inGroups));

        // 组头那行字：加载器 + 游戏版本（两个都没有时是「其它版本」）。
        int titled = 0;
        for (const ResourceCatalog::VersionGroup &group : groups) {
            const QString title = group.title();
            const bool ok = !title.isEmpty() &&
                            (group.loader.isEmpty()
                                 ? title == (group.gameVersion.isEmpty() ? QStringLiteral("其它版本")
                                                                         : group.gameVersion)
                                 : title.endsWith(group.gameVersion) &&
                                       title.startsWith(
                                           ResourceCatalog::loaderDisplayName(group.loader)));
            if (ok) {
                ++titled;
            }
        }
        check(titled == groups.size(), "Z8e 每个组头都是「加载器 游戏版本」那两截",
              QStringLiteral("%1/%2").arg(titled).arg(groups.size()));

        // 组的顺序：游戏版本新的在前。夹具这一份是新在前排好的（Z6 钉过），而分组是按输入
        // 次序走一遍、遇到的第一个（游戏版本 + 加载器）对开第一组 —— 所以**最新那条版本
        // 落的那一组就是头一组**。（比大小的那个 compareVersionText 是 ResourceCatalog 内部
        // 的东西、不对外，所以这儿换一条等价的说法来钉顺序。）
        if (!groups.isEmpty()) {
            bool firstGroupHoldsNewest = false;
            for (const ResourceCatalog::VersionEntry &version : groups.first().versions) {
                if (version.id == versions.first().id) {
                    firstGroupHoldsNewest = true;
                }
            }
            check(firstGroupHoldsNewest, "Z8e 头一组装的就是最新那条版本（新的在前）",
                  groups.first().title());
        }
    }

    section("Z8f. ★ chip 筛的是**这一版声明过的游戏版本**，不是它落在哪个组里");

    if (versions.size() == 12) {
        const QList<ResourceCatalog::VersionGroup> groups =
            ResourceCatalog::groupVersions(versions, false);

        // 找一条"声明了不止一个游戏版本"的版本（夹具里有）：它只会落在其中一档的组里，
        // 而选了另一档时它照样该在。这正是那两步不能合并的理由。
        const ResourceCatalog::VersionEntry *multi = nullptr;
        for (const ResourceCatalog::VersionEntry &version : versions) {
            if (version.gameVersions.size() > 1) {
                multi = &version;
                break;
            }
        }
        check(multi != nullptr, "Z8f 夹具里有一条声明了多个游戏版本的版本");
        if (multi != nullptr) {
            // 它落的那一组是哪一档：**从分组结果里读**，不猜。声明表的顺序不等于新旧顺序
            // （夹具里就是旧的在前），谁是最新那一档由 compareVersionText 说了算。
            QString groupedUnder;
            for (const ResourceCatalog::VersionGroup &group : groups) {
                for (const ResourceCatalog::VersionEntry &version : group.versions) {
                    if (version.id == multi->id && groupedUnder.isEmpty()) {
                        groupedUnder = group.gameVersion;
                    }
                }
            }
            // 它声明过、但**不是**它落的那一档：拿这一档去筛。
            QString other;
            for (const QString &gameVersion : multi->gameVersions) {
                if (gameVersion != groupedUnder) {
                    other = gameVersion;
                }
            }
            check(!groupedUnder.isEmpty() && !other.isEmpty(),
                  "Z8f 它声明了两档、只落在其中一档（组归属 ≠ 全部声明）",
                  groupedUnder + " / " + other);
            if (!other.isEmpty()) {
                // 按"另一档"筛，它必须在结果里 —— 只看"落在哪个组里"的写法会把它漏掉，
                // 而界面上看起来只是"这个模组少列了几版"，没有人会报错。
                const QList<ResourceCatalog::VersionEntry> filtered =
                    ResourceCatalog::versionsForGameVersion(versions, other, false);
                bool found = false;
                for (const ResourceCatalog::VersionEntry &version : filtered) {
                    if (version.id == multi->id) {
                        found = true;
                    }
                }
                check(found, "★ Z8f 按另一档筛，它**在**（筛看 gameVersions，不看组归属）",
                      QStringLiteral("%1 档筛出 %2 条").arg(other).arg(filtered.size()));
                // 反过来：只筛出真的声明了那一档的（筛出来的每一条都声明过它）。
                int allDeclare = 0;
                for (const ResourceCatalog::VersionEntry &version : filtered) {
                    if (version.gameVersions.contains(other)) {
                        ++allDeclare;
                    }
                }
                check(allDeclare == filtered.size(),
                      "Z8f 筛出来的每一条都真的声明过那一档",
                      QStringLiteral("%1/%2").arg(allDeclare).arg(filtered.size()));
                // 而且这一档确实筛掉了一些（不然这条测试什么也没验到）。
                check(filtered.size() < versions.size(),
                      "Z8f 这一档确实筛掉了一些（不是「全都支持」）",
                      QStringLiteral("%1/%2").arg(filtered.size()).arg(versions.size()));
            }
        }

        // 空串 = 「全部」那一颗：原样还回去（不是"筛一个空串"——那会一条都不剩）。
        const QList<ResourceCatalog::VersionEntry> all =
            ResourceCatalog::versionsForGameVersion(versions, QString(), false);
        check(all.size() == versions.size() && all.first().id == versions.first().id,
              "Z8f chip「全部」= 不筛（原样那一份）");
        // 一个谁都没声明过的版本号 → 空表（界面上那一句「这个游戏版本上没有可下载的版本」）。
        check(ResourceCatalog::versionsForGameVersion(versions, QStringLiteral("26.2hyperland"),
                                                      false)
                  .isEmpty(),
              "Z8f 谁都没声明过的版本 → 空表");
    }

    section("Z8f2. ★ 当前实例那个加载器排前面（同一档游戏版本里的第三个排序键）");
    // 要求：「点开某个模组等的时候…对应的模组加载器的版本放在前面…展示为对应版本适用的」。
    // 排序键一共三档，**次序不能换**：
    // ① 游戏版本（新的在前）—— 换了的话，点名 neoforge 就会把 1.20.1 那一组顶到 1.21.4
    // 前面去，用户一眼看见的是"这个模组只支持老版本"；
    // ② 同一档游戏版本里，被点名的加载器在前（没点名时是 loaderOrderIndex 那个老次序）；
    // ③ 输入次序（`std::stable_sort` 保住的）。
    //
    // 这一节拿一份**合成**清单来钉，不借 Z8d 那份真夹具：要钉的是"哪一组的键是什么"，
    // 手写的六条比真夹具里那 12 条看得清（而且真夹具里 fabric 本来就排在前面 —— 拿它
    // 钉"点名生效了吗"是钉不住的，换个加载器才行）。
    {
        // 输入次序就是下面这个次序（分组是"按输入走一遍、遇到的第一个对开第一组"）。
        const auto entryOf = [](const QString &id, const QString &gameVersion,
                                const QString &loader) {
            ResourceCatalog::VersionEntry entry;
            entry.id = id;
            entry.gameVersions = QStringList{gameVersion};
            entry.loaders = QStringList{loader};
            return entry;
        };
        const QList<ResourceCatalog::VersionEntry> pool = {
            entryOf(QStringLiteral("forge-a"), QStringLiteral("1.21.4"), QStringLiteral("forge")),
            entryOf(QStringLiteral("forge-a2"), QStringLiteral("1.21.4"), QStringLiteral("forge")),
            entryOf(QStringLiteral("fabric-b"), QStringLiteral("1.21.4"), QStringLiteral("fabric")),
            entryOf(QStringLiteral("neo-c"), QStringLiteral("1.21.4"), QStringLiteral("neoforge")),
            entryOf(QStringLiteral("fabric-d"), QStringLiteral("1.20.1"), QStringLiteral("fabric")),
            entryOf(QStringLiteral("forge-e"), QStringLiteral("1.20.1"), QStringLiteral("forge")),
            entryOf(QStringLiteral("quilt-f"), QStringLiteral("1.21.4"), QStringLiteral("quilt")),
        };
        // 组的身份就写成「加载器/游戏版本」—— 两张表并排比一眼就能看出哪一组挪了位。
        const auto keysOf = [](const QList<ResourceCatalog::VersionGroup> &groups) {
            QStringList keys;
            for (const ResourceCatalog::VersionGroup &group : groups) {
                keys << (group.loader + QLatin1Char('/') + group.gameVersion);
            }
            return keys;
        };

        const QStringList plain = keysOf(ResourceCatalog::groupVersions(pool, false));
        check(plain == QStringList{QStringLiteral("fabric/1.21.4"), QStringLiteral("forge/1.21.4"),
                                   QStringLiteral("neoforge/1.21.4"), QStringLiteral("quilt/1.21.4"),
                                   QStringLiteral("fabric/1.20.1"), QStringLiteral("forge/1.20.1")},
              "Z8f2 前提：不点名时是那个老次序（游戏版本新的在前，同一档里按固定那张表）",
              plain.join(QStringLiteral(", ")));

        // 点名 forge：同一档游戏版本里它就得到最前面 —— 而**游戏版本那一档不动**。
        const QStringList wanted =
            keysOf(ResourceCatalog::groupVersions(pool, false, QStringLiteral("forge")));
        check(wanted ==
                  QStringList{QStringLiteral("forge/1.21.4"), QStringLiteral("fabric/1.21.4"),
                              QStringLiteral("neoforge/1.21.4"), QStringLiteral("quilt/1.21.4"),
                              QStringLiteral("forge/1.20.1"), QStringLiteral("fabric/1.20.1")},
              "★ Z8f2 点名 forge：它到了每一档游戏版本的最前面（别的组跟着顺、组数不多不少）",
              wanted.join(QStringLiteral(", ")));
        check(wanted.size() == plain.size(),
              "★ Z8f2 排前面**只是挪位置**，不是把别的组筛掉（用户要的是「放前面」）",
              QStringLiteral("%1 vs %2").arg(wanted.size()).arg(plain.size()));
        // 这条才是"三档键次序不能换"的判据：1.20.1 那个 forge 组**排在所有 1.21.4 组后面**。
        // 它要是跑到最前面，用户看见的第一屏就是"这个模组只支持 1.20.1"。
        const int oldForge = wanted.indexOf(QStringLiteral("forge/1.20.1"));
        const int newestQuilt = wanted.indexOf(QStringLiteral("quilt/1.21.4"));
        check(oldForge > newestQuilt && newestQuilt >= 0,
              "★★ Z8f2 而不许把**老那一档**的加载器组顶到新版本前面（游戏版本永远是第一排序键）",
              QStringLiteral("forge/1.20.1 在第 %1 位、quilt/1.21.4 在第 %2 位")
                  .arg(oldForge)
                  .arg(newestQuilt));

        // 传进来的是**显示名**（「Forge」）或带空白时也认。调用点那一侧写的是
        // loaderGroupKey(instanceLoaderName(...))，本来就是小写；这里钉的是"这一层自己
        // 兜得住"—— 哪天有人图省事把显示名直接递进来，不会静默地按"没点名"处理
        // （那种错的形状是"排前面没生效"，屏幕上一点异常都没有）。
        check(keysOf(ResourceCatalog::groupVersions(pool, false, QStringLiteral("Forge"))) == wanted,
              "Z8f2 点名那串大小写不一样也认（「Forge」和 forge 同一组）");
        check(keysOf(ResourceCatalog::groupVersions(pool, false, QStringLiteral("  forge  "))) ==
                  wanted,
              "Z8f2 两头带空白也认（调用点那边 trim 过，这一层自己也兜一下）");
        // 认不出来的加载器（比如实例装的是 OptiFine）⇒ 全组都按老次序，一个字都不动。
        check(keysOf(ResourceCatalog::groupVersions(pool, false, QStringLiteral("optifine"))) == plain,
              "★ Z8f2 点名一个这里没有的加载器 = 没点名（不许把整张表搅乱）");
        check(keysOf(ResourceCatalog::groupVersions(pool, false, QString())) == plain,
              "Z8f2 不传这一位（默认那个空串）也是没点名 —— 老调用点行为一个字不变");

        // 同一组里**条目的次序**不动（stable_sort 那个"稳"说的就是这一条）：两条同组时
        // 进来什么次序，出去还是什么次序（接口给的那份通常已经是新的在前）。
        for (const ResourceCatalog::VersionGroup &group : ResourceCatalog::groupVersions(
                 pool, false, QStringLiteral("forge"))) {
            if (group.loader == QStringLiteral("forge") && group.gameVersion == QStringLiteral("1.21.4")) {
                check(group.versions.size() == 2 && group.versions.first().id == QStringLiteral("forge-a"),
                      "Z8f2 同一组里两条的次序照旧（新的在前，排序不许把它翻过来）",
                      QStringLiteral("%1 条，头一条 %2")
                          .arg(group.versions.size())
                          .arg(group.versions.value(0).id));
            }
        }
    }

    section("Z8g. 头卡那行元信息 + 那两颗按钮的地址");

    {
        ResourceCatalog::Hit hit;
        hit.source = ResourceCatalog::Source::Modrinth;
        hit.projectId = QStringLiteral("AANobbMI");
        hit.slug = QStringLiteral("sodium");
        hit.title = QStringLiteral("Sodium");
        hit.explicitLoaders = QStringList{QStringLiteral("fabric"), QStringLiteral("neoforge")};
        hit.downloads = 231000000;
        // "5 天前"：和 Z8a 一样把"现在"钉死做不到（这一句读的是当下时间），所以喂一条
        // 相对当下算出来的时间戳 —— 差几毫秒不会跨档。
        hit.updatedIso = QDateTime::currentDateTimeUtc().addDays(-5).toString(Qt::ISODate);
        const QString line = ResourceCatalog::hitMetaLine(hit);
        check(line.contains(QStringLiteral("Fabric / NeoForge")) && line.contains(QStringLiteral("下载")) &&
                  line.contains(QStringLiteral("更新于 5 天前")) &&
                  line.endsWith(QStringLiteral("Modrinth")),
              "Z8g 四截都在、且按「 · 」拼起来", line);

        // 缺的那几截就不写：一个加载器都没有（数据包那类）、下载量 0（接口没给）、
        // 时间读不懂。**来源那一截总在** —— 这一行是"这是哪一家来的"。
        ResourceCatalog::Hit bare;
        bare.source = ResourceCatalog::Source::CurseForge;
        bare.title = QStringLiteral("某某");
        const QString bareLine = ResourceCatalog::hitMetaLine(bare);
        check(bareLine == QStringLiteral("CurseForge"),
              "Z8g 什么都没有时只剩来源那一截（不写「下载 0 次」，也不写「-3 天前」）", bareLine);

        // 那两颗「转到 XXX」的网址：两家形状不一样，类名错了回 404 而**不报错**。
        check(ResourceCatalog::projectPageUrl(ResourceCatalog::Source::Modrinth,
                                              ResourceCatalog::Kind::Mod,
                                              QStringLiteral("sodium")) ==
                  QStringLiteral("https://modrinth.com/mod/sodium"),
              "Z8g Modrinth 那条地址");
        check(ResourceCatalog::projectPageUrl(ResourceCatalog::Source::CurseForge,
                                              ResourceCatalog::Kind::Mod,
                                              QStringLiteral("jei")) ==
                  QStringLiteral("https://www.curseforge.com/minecraft/mc-mods/jei"),
              "Z8g CurseForge 那条地址（目录名是 mc-mods，不是我们那份 kindKey）");
        // slug 空 = 拼不出来 → 空串（那一页就不画那颗按钮：摆一颗点了没反应的比不摆糟）。
        check(ResourceCatalog::projectPageUrl(ResourceCatalog::Source::Modrinth,
                                              ResourceCatalog::Kind::Mod, QString())
                  .isEmpty(),
              "Z8g 没有 slug 就不拼地址（那颗按钮不画）");
        // MC 百科那边是按名字搜（它没有"按对面的 id 直接跳"的路）。
        const QString mcmod = ResourceCatalog::mcmodSearchUrl(QStringLiteral("钠 Sodium"));
        check(mcmod.startsWith(QStringLiteral("https://")) && mcmod.contains(QStringLiteral("%20")),
              "Z8g MC 百科那条是「按名字搜」的地址，名字照原样转义", mcmod);
    }

    section("Z8g2. ★★ 一条 Hit 写进 json 再读回来，一个字段都不许丢");

    // （要求：「模组图标做本地缓存功能，不要每次都拉取」）：盘上那份
    // 缓存（`<数据目录>/cache/instance-icons/<实例>.json`）里存的就是这条 Hit —— 它是
    // 「第一次查出来那个资源长什么样」的唯一载体。少写一栏的症状不是报错，而是**那一页
    // 顶着一个空标题 / 那一格退回占位拼图**（"缓存命中了但画不出来"），而且只在**第二次
    // 启动**才现形 —— 所以这一节逐个字段对着钉。
    {
        ResourceCatalog::Hit hit;
        hit.projectId = QStringLiteral("AANobbMI");
        hit.slug = QStringLiteral("sodium");
        hit.title = QStringLiteral("钠 Sodium");
        hit.description = QStringLiteral("渲染优化 · 帧数翻倍"); // 带空格 / 点 / 中文
        hit.author = QStringLiteral("jellysquid3");
        hit.downloads = 231000000LL;
        hit.categories = QStringList{QStringLiteral("optimization"),
                                     QStringLiteral("technology")};
        hit.gameVersions = QStringList{QStringLiteral("1.21.1"), QStringLiteral("1.21")};
        hit.source = ResourceCatalog::Source::CurseForge;
        hit.explicitLoaders = QStringList{QStringLiteral("fabric"), QStringLiteral("quilt")};
        hit.iconUrl = QStringLiteral("https://media.forgecdn.net/avatars/238/222/256/256.jpeg");
        hit.iconFallbackUrl = QStringLiteral("https://media.forgecdn.net/avatars/238/222/t.png");
        hit.updatedIso = QStringLiteral("2026-09-01T12:34:56Z");

        const ResourceCatalog::Hit back =
            ResourceCatalog::hitFromJson(ResourceCatalog::hitToJson(hit));
        check(back.projectId == hit.projectId && back.slug == hit.slug &&
                  back.title == hit.title && back.description == hit.description &&
                  back.author == hit.author && back.downloads == hit.downloads,
              "Z8g2 名字那一批字段往返不走样",
              QStringLiteral("%1 / %2 / %3").arg(back.projectId, back.title, back.description));
        check(back.categories == hit.categories && back.gameVersions == hit.gameVersions &&
                  back.explicitLoaders == hit.explicitLoaders,
              "Z8g2 三个清单（分类 / 游戏版本 / 加载器）**次序也照原样**",
              back.gameVersions.join(QStringLiteral(", ")));
        check(back.source == hit.source && back.iconUrl == hit.iconUrl &&
                  back.iconFallbackUrl == hit.iconFallbackUrl && back.updatedIso == hit.updatedIso,
              "Z8g2 来源 / 两条图标地址 / 更新时间也在",
              QStringLiteral("source=%1").arg(ResourceCatalog::sourceKey(back.source)));
        // **缓存键**（`<来源前缀>-<项目号>`）—— 图标那一格是按它找盘的。这一栏丢了的话，
        // 缓存里读出来的这条会去问一个不存在的键：每一格都变成重新下载。
        check(back.iconCacheId() == hit.iconCacheId() &&
                  back.iconCacheId() == QStringLiteral("curseforge-AANobbMI"),
              "Z8g2 而且缓存键一字不差（= 来源前缀 + 项目号）", back.iconCacheId());
    }
    {
        // 空的那一份（接口只给了个号、别的什么都没说）：往返之后还是空的 ——
        // **不许凭空长出东西来**（比如把 downloads 变成 0、或者给出一个野来源）。
        ResourceCatalog::Hit bare;
        bare.projectId = QStringLiteral("P7dR8mSH");
        const ResourceCatalog::Hit back =
            ResourceCatalog::hitFromJson(ResourceCatalog::hitToJson(bare));
        check(back.projectId == bare.projectId && back.slug.isEmpty() &&
                  back.title.isEmpty() && back.description.isEmpty() &&
                  back.author.isEmpty() && back.downloads == 0 && back.categories.isEmpty() &&
                  back.gameVersions.isEmpty() && back.explicitLoaders.isEmpty() &&
                  back.iconUrl.isEmpty() && back.iconFallbackUrl.isEmpty() &&
                  back.updatedIso.isEmpty(),
              "Z8g2 空的那一份往返之后还是空的（不凭空长字段出来）",
              back.title);
        check(back.source == ResourceCatalog::Source::Modrinth,
              "Z8g2 没写来源时按 Modrinth 读（老缓存 / 手写的那一份不该被认成别家）",
              ResourceCatalog::sourceKey(back.source));
    }
    {
        // 读一份**缺字段 / 字段类型不对**的 json：一律当"没有那一栏"，不许崩、不许抛。
        // （盘上那份是上一次运行的自己写的，但用户可能手改、也可能来自更老的版本。）
        const QJsonObject junk{
            {QStringLiteral("projectId"), QStringLiteral("AANobbMI")},
            {QStringLiteral("title"), 42},                       // 该是字符串，给了数字
            {QStringLiteral("gameVersions"), QStringLiteral("1.21")}, // 该是数组，给了字符串
            {QStringLiteral("downloads"), QStringLiteral("很多")},    // 该是数字，给了字符串
            {QStringLiteral("source"), QStringLiteral("no-such-source")}, // 认不出来的来源
        };
        const ResourceCatalog::Hit back = ResourceCatalog::hitFromJson(junk);
        check(back.projectId == QStringLiteral("AANobbMI") && back.title.isEmpty() &&
                  back.gameVersions.isEmpty() && back.downloads == 0,
              "Z8g2 类型对不上的那几栏一律当没有（读盘的东西不能因为一份坏 json 就炸）",
              QStringLiteral("title=%1 downloads=%2").arg(back.title).arg(back.downloads));
        check(back.source == ResourceCatalog::Source::Modrinth,
              "Z8g2 认不出来的来源退回 Modrinth（不是「没有来源」这种第三态）",
              ResourceCatalog::sourceKey(back.source));
    }

    section("Z8h. ★ 快照版归到对应的大版本（26.2-rc1 → 26.2）");

    // 要求：「把快照版本的模组放到对应的大版本里（如 26.2rc1 放在 26.2 里）」，外加
    // 详情页上方那颗「显示快照版」勾选框。这一节分三层钉：认不认得出是快照 → 折不折得动 →
    // 那排 chip / 按 chip 筛 / 组头三处说的**是不是同一个值**（三处传岔了的症状是"点了 26.2
    // 少了几个版本"，而那排 chip 本身看着完全正常，见 ResourceCatalog.hpp 那三段注释）。
    {
        // 认得出的两类：尾巴上带 -preN / -rcN 的（新老两套命名都长这样）、老式快照号
        // （两位年 + 'w' + 两位周 + 一个小写字母）。
        const QStringList snapshots = {QStringLiteral("26.2-rc1"),
                                       QStringLiteral("1.21.4-pre3"),
                                       QStringLiteral("24w14a"),
                                       QStringLiteral("13w41b"),
                                       QStringLiteral("26.2-snapshot-1"),
                                       QStringLiteral("1.14 Pre-Release 3")};
        int recognized = 0;
        for (const QString &version : snapshots) {
            if (ResourceCatalog::isSnapshotGameVersion(version)) {
                ++recognized;
            }
        }
        check(recognized == snapshots.size(), "Z8h 六颗快照都认得出",
              QStringLiteral("%1/%2").arg(recognized).arg(snapshots.size()));

        // **认不出来的一律不算快照**。判错的代价不对称：把一条正式版说成快照，它会被折进
        // 大版本（用户在那排 chip 里找不到它，而这件事在界面上没有任何提示）；反过来只是
        // 多摆一颗。愚人节/实验版（20w14∞、26.2hyperland）和那些带尾巴的正式版全在这儿。
        const QStringList notSnapshots = {QStringLiteral("1.21.4"),
                                          QStringLiteral("26.2"),
                                          QString(),
                                          QStringLiteral("1.14.3 - Combat Test"),
                                          QStringLiteral("26.2hyperland"),
                                          QStringLiteral("20w14∞"),
                                          // 愚人节版。折出来是 `1.RV` —— 一个谁也不是的
                                          // 大版本，摆在那一排里用户点进去什么都搜不到
                                          // （见 .cpp 里 isPlainVersionStem）。
                                          QStringLiteral("1.RV-Pre1"),
                                          // 尾巴不是"末尾那一段"：`pre3` 后面还挂着一截。
                                          QStringLiteral("1.21.4-pre3-1")};
        int wrongly = 0;
        QString wrongSample;
        for (const QString &version : notSnapshots) {
            if (ResourceCatalog::isSnapshotGameVersion(version)) {
                ++wrongly;
                if (wrongSample.isEmpty()) {
                    wrongSample = version;
                }
            }
        }
        check(wrongly == 0, "Z8h 正式版和愚人节版一颗都不许被认成快照（认错就藏起来了）",
              wrongSample.isEmpty() ? QString() : wrongSample);

        check(ResourceCatalog::majorGameVersionOf(QStringLiteral("26.2-rc1")) ==
                  QStringLiteral("26.2"),
              "Z8h 26.2-rc1 折成 26.2");
        check(ResourceCatalog::majorGameVersionOf(QStringLiteral("1.21.4-pre3")) ==
                  QStringLiteral("1.21.4"),
              "Z8h 1.21.4-pre3 折成 1.21.4");
        check(ResourceCatalog::majorGameVersionOf(QStringLiteral("1.21.4")) ==
                  QStringLiteral("1.21.4"),
              "Z8h 正式版原样还回去（调用方不必先判一次「这是不是快照」）");
        check(ResourceCatalog::majorGameVersionOf(QStringLiteral("24w14a")) ==
                  QStringLiteral("24w14a"),
              "Z8h 老式快照号折不动，原样还回去");
        // "折不动"这件事**只有一个判据**（折完还是快照），两处都得按它判 —— 谁要是改成
        // "看它有没有 '-'"，24w14a 就会被当成折过了，于是它会顶着 `24w14a` 这颗 chip 露在
        // 一个叫「显示快照版」的勾选框没勾的界面上。
        check(ResourceCatalog::isSnapshotGameVersion(
                  ResourceCatalog::majorGameVersionOf(QStringLiteral("24w14a"))),
              "Z8h 「折不动」的判据：折完还是快照");

        // ---- ：真清单说了算 ----
        //
        // 上面那几颗是**手写**的，而手写的永远只覆盖得到"作者想得到的写法" —— 新式快照号
        // （`26.2-snapshot-1`）漏掉就是这么漏的：它既没有 -pre/-rc、长度也不是 6，六年前
        // 写下的两条判据一条都套不上。所以这一条拿**真清单**（915 条）过：凡是长得像快照
        // 的那两族（`-snapshot-` / `pre-release`），每一条都必须认得出、折得出，而且折出来
        // 的那个大版本**必须在清单里真有一条**（不然等于凭空造一个版本摆给用户）。
        //
        // NOTE: 愚人节版（20w14∞、24w14potato、3D Shareware v1.34）天然不在这两族里 ——
        // 它们的名字里没有这两个词，所以"按设计不算快照"这件事不受这一条影响。
        {
            // 这里**自己读一遍**夹具（不复用上面 Y 那一节解析出来的那份）：它俩离得远，
            // 中间还隔着几十节，借一个"这一节还看得见吗"的变量比读一遍文件难查得多。
            const QJsonObject root =
                loadJson(QStringLiteral(REPO_ROOT_PATH) + "/tests/meta/manifest.json");
            const QJsonArray entries = root.value(QStringLiteral("versions")).toArray();
            QSet<QString> allIds;
            for (const QJsonValue &value : entries) {
                allIds.insert(value.toObject().value(QStringLiteral("id")).toString());
            }
            int newStyle = 0;
            QString badSample;
            for (const QJsonValue &value : entries) {
                const QJsonObject entry = value.toObject();
                if (entry.value(QStringLiteral("type")).toString() != QStringLiteral("snapshot")) {
                    continue;
                }
                const QString id = entry.value(QStringLiteral("id")).toString();
                const QString lower = id.toLower();
                if (!lower.contains(QStringLiteral("-snapshot-")) &&
                    !lower.contains(QStringLiteral("pre-release"))) {
                    continue;
                }
                ++newStyle;
                const QString folded = ResourceCatalog::majorGameVersionOf(id);
                const bool ok = ResourceCatalog::isSnapshotGameVersion(id) && folded != id &&
                                !ResourceCatalog::isSnapshotGameVersion(folded) &&
                                allIds.contains(folded);
                if (!ok && badSample.isEmpty()) {
                    badSample = QStringLiteral("%1 → %2").arg(id, folded);
                }
            }
            check(newStyle == 40, "Z8h 真清单里长得像新族快照的有 40 条",
                  QString::number(newStyle));
            check(badSample.isEmpty(),
                  "★ Z8h 这 40 条**每一条**都认得出、折得出、折出来的大版本清单里真有",
                  badSample);
        }

        // 那一排 chip。接口给的 `Hit::gameVersions` 是**旧的在前**（见 Z3），所以这一串写
        // 成旧的在前；倒着画出来的第一颗应该是最新的那个大版本。
        ResourceCatalog::Hit hit;
        hit.gameVersions = QStringList{
            QStringLiteral("1.20.1"),  QStringLiteral("1.21.4-pre3"), QStringLiteral("1.21.4"),
            QStringLiteral("26.2"),    QStringLiteral("26.2-rc1"),    QStringLiteral("26.2-rc2"),
            QStringLiteral("24w14a")};
        const QStringList folded = ResourceCatalog::gameVersionChoices(hit.gameVersions, false);
        check(folded == QStringList({QStringLiteral("26.2"), QStringLiteral("1.21.4"),
                                     QStringLiteral("1.20.1")}),
              "★ Z8h 不勾时：rc1 和 rc2 并成一颗 26.2、24w14a 不摆（它折不动）",
              folded.join(QLatin1Char(' ')));
        const QStringList raw = ResourceCatalog::gameVersionChoices(hit.gameVersions, true);
        check(raw.size() == hit.gameVersions.size() &&
                  raw.first() == hit.gameVersions.last() &&
                  raw.last() == hit.gameVersions.first(),
              "Z8h 勾上时：每一条声明过的各摆一颗，一条不多一条不少（新的在前）",
              QStringLiteral("%1/%2").arg(raw.size()).arg(hit.gameVersions.size()));

        // 按 chip 筛。`onlyRc` 只声明了 26.2-rc1 —— 它正是"折"这件事的样本。
        ResourceCatalog::VersionEntry onlyRc;
        onlyRc.id = QStringLiteral("only-rc");
        onlyRc.gameVersions = QStringList{QStringLiteral("26.2-rc1")};
        ResourceCatalog::VersionEntry onlyFinal;
        onlyFinal.id = QStringLiteral("only-final");
        onlyFinal.gameVersions = QStringList{QStringLiteral("26.2")};
        ResourceCatalog::VersionEntry oldStyle;
        oldStyle.id = QStringLiteral("old-style");
        oldStyle.gameVersions = QStringList{QStringLiteral("24w14a")};
        const QList<ResourceCatalog::VersionEntry> pool = {onlyRc, onlyFinal, oldStyle};

        check(ResourceCatalog::versionsForGameVersion(pool, QStringLiteral("26.2"), false).size() == 2,
              "★ Z8h 不勾时点 26.2：只声明了 26.2-rc1 的那一版**也在**（26.2 代表整条线）");
        check(ResourceCatalog::versionsForGameVersion(pool, QStringLiteral("26.2"), true).size() == 1,
              "Z8h 勾上时点 26.2：只剩真声明过 26.2 的那一版（rc 去点它自己那一颗）");
        check(ResourceCatalog::versionsForGameVersion(pool, QStringLiteral("26.2-rc1"), true).size() ==
                  1,
              "Z8h 勾上时 26.2-rc1 自己是摆出来的一颗，点得开");
        check(ResourceCatalog::versionsForGameVersion(pool, QString(), false).size() == pool.size(),
              "Z8h 「全部」那一颗两种模式下都是不筛（折不动的那条也照样在里面）");

        // 组头。分组就是"这个模组落在哪个大版本下"这件事的出处 —— chip 折了而组头不折的话，
        // 用户点 26.2 进去看见的是「Fabric 26.2-rc1」那一组，两处各说各的。
        const QList<ResourceCatalog::VersionGroup> groupsFolded =
            ResourceCatalog::groupVersions({onlyRc}, false);
        check(groupsFolded.size() == 1 &&
                  groupsFolded.first().gameVersion == QStringLiteral("26.2") &&
                  groupsFolded.first().title() == QStringLiteral("26.2"),
              "★ Z8h 不勾时组头也折（只声明到 26.2-rc1 的版本落在 26.2 那一组）",
              groupsFolded.isEmpty() ? QString() : groupsFolded.first().title());
        const QList<ResourceCatalog::VersionGroup> groupsRaw =
            ResourceCatalog::groupVersions({onlyRc}, true);
        check(groupsRaw.size() == 1 &&
                  groupsRaw.first().gameVersion == QStringLiteral("26.2-rc1"),
              "Z8h 勾上时组头原样写 26.2-rc1",
              groupsRaw.isEmpty() ? QString() : groupsRaw.first().title());

        // 拨那颗勾选框之后**该按哪一颗**。这一件事看着小，但它是这个功能里唯一会"把用户
        // 甩下车"的地方：用户在 26.2 上看着若干个版本，一勾/一摘之后那排 chip 是另一份表
        // 了 —— 要是直接把旧 key 塞回去，chip 一颗都不亮（`applyChipChecked` 找不到那一颗），
        // 列表却被新 key 筛过了，界面上就是"整排 chip 都不亮 + 列表少了一半"。
        //
        // 三种情形分开钉（① 还在、② 折一下、③ 挑最新那条快照），外加两种"对不上就回全部"。
        check(ResourceCatalog::gameVersionAfterToggle(hit.gameVersions, true,
                                                      QStringLiteral("26.2")) ==
                  QStringLiteral("26.2"),
              "★ Z8h 勾上前后的 26.2 都还在：这一颗不动（不必挑快照）");
        check(ResourceCatalog::gameVersionAfterToggle(hit.gameVersions, true,
                                                      QStringLiteral("1.20.1")) ==
                  QStringLiteral("1.20.1"),
              "Z8h 正式版的 key 两种模式下都在，原样留着");
        check(ResourceCatalog::gameVersionAfterToggle(hit.gameVersions, false,
                                                      QStringLiteral("26.2-rc1")) ==
                  QStringLiteral("26.2"),
              "★ Z8h 勾→不勾：26.2-rc1 折成 26.2（那条线还在他眼皮底下，不甩回全部）");
        check(ResourceCatalog::gameVersionAfterToggle(hit.gameVersions, false,
                                                      QStringLiteral("24w14a")) == QString(),
              "Z8h 不勾之后 24w14a 那颗根本不摆 ⇒ 回「全部」（不能停在一颗不存在的 chip 上）");
        check(ResourceCatalog::gameVersionAfterToggle(hit.gameVersions, false, QString()) == QString(),
              "Z8h 「全部」拨了还是「全部」");

        // ③ 不勾 → 勾：挑这条线上**最新的那一条快照**（他原来点的那颗 26.2 在不勾时代表
        // 整条线，"翻看快照"最想要的就是这条线里最新的那颗）。
        const QStringList onlyRcDeclared = {QStringLiteral("1.20.1"), QStringLiteral("26.2-rc1")};
        check(ResourceCatalog::gameVersionAfterToggle(onlyRcDeclared, true, QStringLiteral("26.2")) ==
                  QStringLiteral("26.2-rc1"),
              "★ Z8h 不勾→勾：只声明了 26.2-rc1 的那条线，勾上落在 26.2-rc1 这一颗上");
        const QStringList twoRcs = {QStringLiteral("26.2-rc1"), QStringLiteral("26.2-rc2")};
        check(ResourceCatalog::gameVersionAfterToggle(twoRcs, true, QStringLiteral("26.2")) ==
                  QStringLiteral("26.2-rc2"),
              "Z8h 这条线上有两颗快照时挑最新的那一颗（26.2-rc2）");
        // 反过来：这条线上**另有一个真·26.2** 的时候，26.2 这颗在勾上之后照样在 ⇒ 留在它
        // 身上（情形 ① 先于 ③ 判，不然会把用户从正式版拽到它旁边的 rc 上）。
        const QStringList withFinal = {QStringLiteral("26.2"), QStringLiteral("26.2-rc2")};
        check(ResourceCatalog::gameVersionAfterToggle(withFinal, true, QStringLiteral("26.2")) ==
                  QStringLiteral("26.2"),
              "★ Z8h 26.2 自己也声明过时留在 26.2（不许被「挑最新的快照」顶到 rc 上）");

        // 对不上的 key 一律回「全部」。这条路平时走不到（调用方传的是页面当下那一颗），但
        // 传岔了的话症状是"chip 全不亮 + 列表对着一个用户看不见的版本筛"。
        check(ResourceCatalog::gameVersionAfterToggle(hit.gameVersions, true, QStringLiteral("9.9.9"))
                  == QString(),
              "Z8h 对不上的 key 回「全部」");
        check(ResourceCatalog::gameVersionAfterToggle(QStringList(), false, QStringLiteral("26.2")) ==
                  QString(),
              "Z8h 清单是空的（接口没给版本）时也回「全部」，不崩");
    }
}

