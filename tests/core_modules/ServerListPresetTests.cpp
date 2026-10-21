#include "Support.hpp"

void runServerListPresetTests() {
    section("PC. 服务器列表那一段：追加 / 覆盖 / 跳过与去重");

    section("PC0. 「同一台服务器」怎么判（纯值）");
    {
        check(Presets::normalizeServerAddress(QStringLiteral("MC.Example.com")) ==
                  QStringLiteral("mc.example.com:25565"),
              "★★ PC0 主机折小写 + 补上默认端口 —— `MC.Example.com` 就是 `mc.example.com:25565`",
              Presets::normalizeServerAddress(QStringLiteral("MC.Example.com")));
        check(Presets::sameServerAddress(QStringLiteral("mc.example.com"),
                                         QStringLiteral("MC.Example.com:25565")) &&
                  Presets::sameServerAddress(QStringLiteral("  a.example.com:25566 "),
                                             QStringLiteral("A.EXAMPLE.com:25566")) &&
                  !Presets::sameServerAddress(QStringLiteral("mc.example.com"),
                                              QStringLiteral("mc.example.com:25566")),
              "★★ PC0 这三种写法是同一台、换个端口就不是（两头空白也照去）");
        check(Presets::normalizeServerAddress(QStringLiteral("mc.example.com:不是数字")) ==
                  QStringLiteral("mc.example.com:不是数字"),
              "★★ PC0 **认不出来的地址原样回去**（不给它补 25565）—— 把看不懂的地址悄悄"
              "「修」一下，换来的是一句假的「这两条是同一台」",
              Presets::normalizeServerAddress(QStringLiteral("mc.example.com:不是数字")));
        check(Presets::normalizeServerAddress(QStringLiteral("[::1]")) ==
                  Presets::normalizeServerAddress(QStringLiteral("[::1]:25565")),
              "PC0 IPv6 字面量：`[::1]` 和 `[::1]:25565` 归一成同一个串",
              Presets::normalizeServerAddress(QStringLiteral("[::1]")));
        check(Presets::normalizeServerAddress(QString()).isEmpty() &&
                  !Presets::sameServerAddress(QStringLiteral(""), QStringLiteral("a.com")),
              "PC0 空地址归一成空串，而且**不和任何一条**算同一台");
    }

    section("PC1. 计划：追加只添目标上没有的，条数一条都不少报");
    const QList<NbtUtil::ServerEntry> target{
        NbtUtil::ServerEntry{QStringLiteral("A服"), QStringLiteral("a.example.com")},
        NbtUtil::ServerEntry{QStringLiteral("B服"), QStringLiteral("b.example.com:25565")},
    };
    const QList<NbtUtil::ServerEntry> source{
        NbtUtil::ServerEntry{QStringLiteral("家"), QStringLiteral("home.example.com")},
        NbtUtil::ServerEntry{QStringLiteral("A 的另一种写法"), QStringLiteral("A.EXAMPLE.com:25565")},
        NbtUtil::ServerEntry{QStringLiteral("又来一遍"), QStringLiteral("home.example.com:25565")},
        NbtUtil::ServerEntry{QStringLiteral("B 服"), QStringLiteral("b.example.com")},
    };
    {
        const Presets::ServerListPlan plan =
            Presets::planServerList(target, 0, source, Presets::ApplyMode::Append);
        check(plan.ok && plan.acceptedCount() == 1 && plan.targetEntryCount == 2,
              "★★ PC1 四台里只有 1 台是要添的（另外三台的地址在目标上 / 在源里已经有了）",
              QStringLiteral("写 %1 / 目标 %2")
                  .arg(plan.acceptedCount())
                  .arg(plan.targetEntryCount));
        check(plan.acceptedCount() == 1 &&
                  plan.accepted.at(0).address == QStringLiteral("home.example.com"),
              "★★ PC1 添的是**源里第一条**（保序 —— 排在后面的不能因为「没被跳过」就插到前面）");
        check(plan.skippedCount() == 3 &&
                  plan.skippedAddresses.contains(QStringLiteral("A.EXAMPLE.com:25565")) &&
                  plan.skippedAddresses.contains(QStringLiteral("b.example.com")),
              "★★ PC1 没写的那 3 条**一条都不许少报**（这就是界面上那句「这 3 台已经在"
              "列表里了」的出处）",
              plan.skippedAddresses.join(QStringLiteral(",")));
        check(plan.addedCount == 1, "PC1 报告里写的都一样：四条源、一条真添");

        // 「合并」和「缺了才拷」对一张列表来说就是「追加」（写在这儿免得以后有人以为漏了两档）。
        const Presets::ServerListPlan merged =
            Presets::planServerList(target, 0, source, Presets::ApplyMode::Merge);
        const Presets::ServerListPlan copyIfMissing =
            Presets::planServerList(target, 0, source, Presets::ApplyMode::CopyIfMissing);
        check(merged.acceptedCount() == plan.acceptedCount() &&
                  copyIfMissing.acceptedCount() == plan.acceptedCount(),
              "PC1「合并」「缺了才拷」和「追加」算出来是同一件事（**故意**的，不是漏了一档）");

        const Presets::ServerListPlan skipped =
            Presets::planServerList(target, 0, source, Presets::ApplyMode::Skip);
        check(skipped.ok && skipped.acceptedCount() == 0 && skipped.skippedCount() == 4,
              "PC1「跳过」：一项都不写、源里那四条全算没动");
    }

    section("PC2. 计划：源里有空地址 ⇒ 整份不认");
    {
        const Presets::ServerListPlan plan = Presets::planServerList(
            target, 0,
            {NbtUtil::ServerEntry{QStringLiteral("好的"), QStringLiteral("ok.example.com")},
             NbtUtil::ServerEntry{QStringLiteral("坏的"), QString()}},
            Presets::ApplyMode::Append);
        check(!plan.ok && plan.acceptedCount() == 0 && !plan.problem.isEmpty(),
              "★★ PC2 源里有一条没有地址 ⇒ 整份都不认（地址是这一条唯一的实质内容），"
              "报告里说清楚",
              plan.problem);
    }

    section("PC3. 真落盘：追加，保序、去重、没碰过的条目逐字节不变");
    QTemporaryDir dir;
    check(dir.isValid(), "PC3 建得出一个临时目录来摆夹具");
    if (!dir.isValid()) {
        return;
    }
    const QString datPath = dir.filePath(QStringLiteral("servers.dat"));

    // 目标上那两条：第一条是"游戏真写出来的那个形状"（hidden / ip / name / icon 四格全在），
    // 第二条只有 ip + name。★ 那两格 extra 是这一节后半段（覆盖）最要紧的东西。
    QList<NbtServerItem> current{
        NbtServerItem{QStringLiteral("A服"), QStringLiteral("a.example.com"), true, true, false,
                      true, QStringLiteral("iVBORw0KGgo=")},
        NbtServerItem{QStringLiteral("B服"), QStringLiteral("b.example.com"), true, true, false,
                      false, QString()},
    };
    check(writeBytesAt(datPath, nbtServersDat(current)), "PC3 夹具落盘了");
    {
        QList<NbtUtil::ServerEntry> rows;
        QString problem;
        const NbtUtil::Status status = ServerListFile::read(datPath, &rows, &problem);
        check(status == NbtUtil::Status::Ok && rows.size() == 2,
              "PC3 夹具是合法的 servers.dat（读侧认得出来）", problem);
    }

    {
        const QList<NbtUtil::ServerEntry> add{
            NbtUtil::ServerEntry{QStringLiteral("家"), QStringLiteral("home.example.com")},
            NbtUtil::ServerEntry{QStringLiteral("A 的另一种写法"),
                                 QStringLiteral("A.EXAMPLE.com:25565")},
        };
        const Presets::ServerListReport report = Presets::applyServerListToInstance(
            datPath, Presets::ApplyMode::Append, add);
        check(report.ok && report.written == 1 && report.added == 1 && report.skipped == 1,
              "★★ PC3 写进去 1 条、跳过 1 条（那条是 `A.EXAMPLE.com:25565` = 目标上的 A服）",
              QStringLiteral("写 %1 / 添 %2 / 跳 %3：%4")
                  .arg(report.written)
                  .arg(report.added)
                  .arg(report.skipped)
                  .arg(report.problem));

        current.append(NbtServerItem{QStringLiteral("家"), QStringLiteral("home.example.com")});
        check(readBytesAt(datPath) == nbtServersDat(current),
              "★★ PC3 盘上逐字节 = 「原来那两条一个字节没动 + 新的接在表尾」（顺序没被重排，"
              "A服那两格 extra 还在）");

        QList<NbtUtil::ServerEntry> rows;
        const NbtUtil::Status status = ServerListFile::read(datPath, &rows, nullptr);
        check(status == NbtUtil::Status::Ok && rows.size() == 3 &&
                  rows.at(0).name == QStringLiteral("A服") &&
                  rows.at(1).name == QStringLiteral("B服") &&
                  rows.at(2).name == QStringLiteral("家"),
              "PC3 读回来就是那三条，次序也是那个次序");

        // 再追加一遍同一批：这次一条都不用动 ⇒ **连写都不写**（mtime 不许变）。
        const QByteArray before = readBytesAt(datPath);
        const qint64 stampBefore = QFileInfo(datPath).lastModified().toMSecsSinceEpoch();
        QThread::msleep(20);
        const Presets::ServerListReport again = Presets::applyServerListToInstance(
            datPath, Presets::ApplyMode::Append, add);
        check(again.ok && again.written == 0 && again.skipped == 2,
              "★★ PC3 再追加一遍：一条都不用写（两条都算跳过）",
              QStringLiteral("写 %1 / 跳 %2").arg(again.written).arg(again.skipped));
        check(readBytesAt(datPath) == before &&
                  QFileInfo(datPath).lastModified().toMSecsSinceEpoch() == stampBefore,
              "★★ PC3 而且**没碰盘**：字节和 mtime 都没变（把一个没变的文件重写一遍，只会在"
              "用户的实例里留下一个变了时间的文件）");
    }

    section("PC4. 目标上还没有 servers.dat：建一份游戏认得出的、而且是 gzip");
    {
        const QString freshPath = dir.filePath(QStringLiteral("fresh/servers.dat"));
        // 目录都不在：这一档游戏自己也是这个行为（实例目录本来就有，这里造的是最狠的一档）。
        QDir().mkpath(dir.filePath(QStringLiteral("fresh")));
        const QList<NbtUtil::ServerEntry> add{
            NbtUtil::ServerEntry{QStringLiteral("第一台"), QStringLiteral("first.example.com")},
        };
        const Presets::ServerListReport report = Presets::applyServerListToInstance(
            freshPath, Presets::ApplyMode::Append, add);
        check(report.ok && report.written == 1 && report.removed == 0 &&
                  QFileInfo::exists(freshPath),
              "PC4 文件不在 ⇒ 不是失败，照建一份出来", report.problem);

        const QByteArray bytes = readBytesAt(freshPath);
        check(bytes.size() >= 2 && static_cast<uchar>(bytes.at(0)) == 0x1F &&
                  static_cast<uchar>(bytes.at(1)) == 0x8B,
              "★★ PC4 新建的那一份**是 gzip**（游戏读的是 `NbtIo.readCompressed`，裸的会被"
              "当场当成坏的 —— 「启动器加的第一台服务器进游戏看不见」正是这条路最不该有的"
              "结果）");
        QList<NbtUtil::ServerEntry> rows;
        QString problem;
        const NbtUtil::Status status = ServerListFile::read(freshPath, &rows, &problem);
        check(status == NbtUtil::Status::Ok && rows.size() == 1 &&
                  rows.at(0).name == QStringLiteral("第一台") &&
                  rows.at(0).address == QStringLiteral("first.example.com"),
              "★★ PC4 游戏那一侧（`NbtUtil` 读的）真读得出来", problem);

        // 一台上都没有的时候连文件都不建（"跳过"这一档最典型）。
        const QString nonePath = dir.filePath(QStringLiteral("none/servers.dat"));
        QDir().mkpath(dir.filePath(QStringLiteral("none")));
        const Presets::ServerListReport nothing = Presets::applyServerListToInstance(
            nonePath, Presets::ApplyMode::Skip, add);
        check(nothing.ok && !QFileInfo::exists(nonePath),
              "★★ PC4「跳过」+ 文件不在 ⇒ **连文件都不建**（用户让跳过，我们就什么都别做）");
    }

    section("PC5. 「覆盖」= 就地改名 + 拿掉对不上的，不是清空重建");
    {
        // 先把夹具摆回那两条（上一条 PC3 末尾它已经是三条了）。
        current = {
            NbtServerItem{QStringLiteral("A服"), QStringLiteral("a.example.com"), true, true, false,
                          true, QStringLiteral("iVBORw0KGgo=")},
            NbtServerItem{QStringLiteral("B服"), QStringLiteral("b.example.com"), true, true, false,
                          false, QString()},
            NbtServerItem{QStringLiteral("C服"), QStringLiteral("c.example.com"), true, true, false,
                          false, QString()},
        };
        check(writeBytesAt(datPath, nbtServersDat(current)), "PC5 夹具重新落盘");

        // 预设：改名 A（地址写法换一个，仍是同一台）、添一台新的 D。B / C 对不上 ⇒ 拿掉。
        const QList<NbtUtil::ServerEntry> replaceWith{
            NbtUtil::ServerEntry{QStringLiteral("A 改过名"), QStringLiteral("A.example.com:25565")},
            NbtUtil::ServerEntry{QStringLiteral("D服"), QStringLiteral("d.example.com")},
        };

        const QByteArray before = readBytesAt(datPath);
        const Presets::ServerListReport asked = Presets::applyServerListToInstance(
            datPath, Presets::ApplyMode::Replace, replaceWith, false);
        check(!asked.ok && asked.needsConfirm && readBytesAt(datPath) == before,
              "★★ PC5 没确认的「覆盖」：**一个字节都不写**（不是「先写了再回滚」）",
              asked.problem);
        check(asked.written == 2 && asked.added == 1 && asked.removed == 2,
              "★★ PC5 而且报告里说得出这一下要动什么：写 2 条（其中添 1 条新的）、拿掉 2 条",
              QStringLiteral("写 %1 / 添 %2 / 拿 %3")
                  .arg(asked.written)
                  .arg(asked.added)
                  .arg(asked.removed));

        // 确认了：A 就地改名（**icon 那两格还在**），B / C 拿掉，D 接在表尾。
        const Presets::ServerListReport report = Presets::applyServerListToInstance(
            datPath, Presets::ApplyMode::Replace, replaceWith, true);
        check(report.ok && report.written == 2 && report.removed == 2 && report.untouchable == 0,
              "PC5 确认之后真的动了", report.problem);

        QList<NbtServerItem> want{
            NbtServerItem{QStringLiteral("A 改过名"), QStringLiteral("A.example.com:25565"), true,
                          true, false, true, QStringLiteral("iVBORw0KGgo=")},
            NbtServerItem{QStringLiteral("D服"), QStringLiteral("d.example.com")},
        };
        check(readBytesAt(datPath) == nbtServersDat(want),
              "★★ PC5 盘上逐字节 = 「A 那条**就地改名**（`hidden` 和 `icon` 两格原样带着走）"
              "+ D 接在表尾」（清空重建会把那两格抹掉）");

        // 认不出来的元素：任何一档都不动（连「覆盖」也不动），而且报数。
        QList<NbtServerItem> withGhost = want;
        withGhost.append(NbtServerItem{QString(), QString(), false, false, false, false, QString()});
        check(writeBytesAt(datPath, nbtServersDat(withGhost)), "PC5 带一条没有 ip 的夹具落盘");
        const QList<NbtUtil::ServerEntry> onlyA{
            NbtUtil::ServerEntry{QStringLiteral("A 又改名"), QStringLiteral("a.example.com")},
        };
        const Presets::ServerListReport ghost = Presets::applyServerListToInstance(
            datPath, Presets::ApplyMode::Replace, onlyA, true);
        check(ghost.ok && ghost.untouchable == 1,
              "★★ PC5 认不出来的那一条**留着**（我们看不懂它，就没有资格替用户抹掉），"
              "条数报在 `untouchable` 上",
              QStringLiteral("untouchable=%1").arg(ghost.untouchable));
        check(QFileInfo::exists(datPath) &&
                  readBytesAt(datPath) ==
                      nbtServersDat({NbtServerItem{QStringLiteral("A 又改名"),
                                                   QStringLiteral("a.example.com"), true, true,
                                                   false, true, QStringLiteral("iVBORw0KGgo=")},
                                     NbtServerItem{QString(), QString(), false, false, false, false,
                                                   QString()}}),
              "★★ PC5 而且它就地改名之后那两格 extra 还在、认不出的那条位置也没动");
    }

    section("PC6. 读不动的那些：拒写，一个字节都不动");
    {
        const QString brokenPath = dir.filePath(QStringLiteral("broken.dat"));
        const QByteArray garbage = QByteArrayLiteral("\x0a\x00\x00\xff\xff not nbt at all");
        check(writeBytesAt(brokenPath, garbage), "PC6 一份坏文件落盘了");
        const Presets::ServerListReport report = Presets::applyServerListToInstance(
            brokenPath, Presets::ApplyMode::Append,
            {NbtUtil::ServerEntry{QStringLiteral("X"), QStringLiteral("x.example.com")}});
        check(!report.ok && !report.problem.isEmpty() && readBytesAt(brokenPath) == garbage,
              "★★ PC6 看不懂的那一份：报错 + **逐字节没动**", report.problem);

        // `servers` 那一格不是一张表。
        const QString oddPath = dir.filePath(QStringLiteral("odd.dat"));
        check(writeBytesAt(oddPath,
                           nbtTag(0x0A, QByteArray(),
                                  nbtCompoundBody({nbtTag(0x08, QByteArrayLiteral("servers"),
                                                          nbtString(QStringLiteral("我不是表")))}))),
              "PC6 一份 `servers` 是字符串的落盘了");
        const QByteArray oddBefore = readBytesAt(oddPath);
        const Presets::ServerListReport odd = Presets::applyServerListToInstance(
            oddPath, Presets::ApplyMode::Append,
            {NbtUtil::ServerEntry{QStringLiteral("X"), QStringLiteral("x.example.com")}});
        check(!odd.ok && readBytesAt(oddPath) == oddBefore,
              "★★ PC6 `servers` 那一格不是表 ⇒ 拒写（一个字节都不动）", odd.problem);

        // 路径是空串：连找哪个文件都不知道。
        const Presets::ServerListReport nowhere = Presets::applyServerListToInstance(
            QString(), Presets::ApplyMode::Append, {});
        check(!nowhere.ok && !nowhere.problem.isEmpty(),
              "PC6 没给路径 ⇒ 拒（报告里说清楚）", nowhere.problem);
    }

    section("PC7. 预设段 ⇄ 这一份列表");
    {
        const QList<NbtUtil::ServerEntry> entries{
            NbtUtil::ServerEntry{QStringLiteral("家"), QStringLiteral("home.example.com")},
            NbtUtil::ServerEntry{QString(), QStringLiteral("c.example.com")},
        };
        const QList<Presets::PresetSection> sections =
            Presets::sectionsFromServerList(entries, Presets::ApplyMode::Append);
        check(sections.size() == 1 && sections.at(0).kind == Presets::SectionKind::ServerList &&
                  sections.at(0).id == Presets::sectionIdFor(Presets::SectionKind::ServerList) &&
                  sections.at(0).mode == Presets::ApplyMode::Append &&
                  sections.at(0).items.size() == 2,
              "PC7 一段「服务器列表」：种类、id、模式、条数都对");
        check(sections.at(0).items.at(1).toObject().value(QStringLiteral("name")).isString() &&
                  sections.at(0).items.at(1).toObject().value(QStringLiteral("name")).toString()
                      .isEmpty(),
              "★★ PC7 名字是空串的那一条**照样写一格 `\"name\": \"\"`**（「省掉的 name」和"
              "「名字是空的」在读回来那一头是两句话）");

        // 真的过一遍 JSON 文本再读回（段要能过一遍字节才是真的能用）。
        const QByteArray sectionBytes =
            QJsonDocument(sections.at(0).items).toJson(QJsonDocument::Compact);
        Presets::PresetSection back;
        back.id = sections.at(0).id;
        back.kind = sections.at(0).kind;
        back.mode = sections.at(0).mode;
        back.items = QJsonDocument::fromJson(sectionBytes).array();
        QList<NbtUtil::ServerEntry> round;
        QString problem;
        check(back.items.size() == 2 &&
                  Presets::serverListFromSection(back, &round, &problem) && round.size() == 2 &&
                  round.at(0).name == QStringLiteral("家") &&
                  round.at(0).address == QStringLiteral("home.example.com") &&
                  round.at(1).name.isEmpty() &&
                  round.at(1).address == QStringLiteral("c.example.com"),
              "★★ PC7 过一遍 JSON 字节再读回来一字不差（含那条没有名字的）", problem);

        // 读不回来的那些：一项都不许放过去。
        Presets::PresetSection bad;
        bad.kind = Presets::SectionKind::ServerList;
        bad.items.append(QStringLiteral("我不是对象"));
        check(!Presets::serverListFromSection(bad, &round, &problem) && !problem.isEmpty(),
              "★★ PC7 项不是对象 ⇒ 整段读不回来", problem);

        Presets::PresetSection missingAddress;
        missingAddress.kind = Presets::SectionKind::ServerList;
        missingAddress.items.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("没有地址")}});
        check(!Presets::serverListFromSection(missingAddress, &round, &problem) &&
                  !problem.isEmpty(),
              "★★ PC7 缺了 `address` ⇒ 整段读不回来（读不回来的段**不许拿去改用户的实例**）",
              problem);

        Presets::PresetSection emptyAddress;
        emptyAddress.kind = Presets::SectionKind::ServerList;
        emptyAddress.items.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("空地址")},
                                              {QStringLiteral("address"), QStringLiteral("  ")}});
        check(!Presets::serverListFromSection(emptyAddress, &round, &problem) &&
                  !problem.isEmpty(),
              "PC7 地址是空白 ⇒ 一样不认（和 `ServerListFile::addEntry()` 同一条）", problem);

        Presets::PresetSection wrongKind;
        wrongKind.kind = Presets::SectionKind::VanillaOptions;
        check(!Presets::serverListFromSection(wrongKind, &round, &problem) &&
                  !Presets::serverListHandlesSection(Presets::SectionKind::VanillaOptions) &&
                  Presets::serverListHandlesSection(Presets::SectionKind::ServerList),
              "PC7 不归这一层管的段 ⇒ 拒绝（`serverListHandlesSection()` 就是分派判据）");
    }

    section("PC8. 端到端：造一份预设、提取、落到另一个实例上");
    {
        const QString fromPath = dir.filePath(QStringLiteral("e2e-from.dat"));
        const QString toPath = dir.filePath(QStringLiteral("e2e-to.dat"));
        check(writeBytesAt(fromPath,
                           nbtServersDat({NbtServerItem{QStringLiteral("家"),
                                                        QStringLiteral("home.example.com")},
                                          NbtServerItem{QStringLiteral("服二"),
                                                        QStringLiteral("two.example.com:25566")}})),
              "PC8 源实例那一份落盘了");
        check(writeBytesAt(toPath,
                           nbtServersDat({NbtServerItem{QStringLiteral("已有的"),
                                                        QStringLiteral("two.example.com:25566")}})),
              "PC8 目标实例那一份落盘了");

        // 提取（走这一层自己的读）→ 一段预设 → 读回来 → 落到目标上。
        NbtTree::Document document;
        bool gzipped = false;
        QString why;
        const ServerListFile::FileState state =
            ServerListFile::loadDocument(fromPath, &document, &gzipped, &why);
        QList<NbtUtil::ServerEntry> extracted;
        int untouchable = 0;
        check(state == ServerListFile::FileState::Ok &&
                  Presets::serverEntriesOf(&document, &extracted, &untouchable, &why) &&
                  extracted.size() == 2 && untouchable == 0,
              "PC8 从源实例里把那两条读出来了", why);
        const QList<Presets::PresetSection> sections =
            Presets::sectionsFromServerList(extracted, Presets::ApplyMode::Append);
        QList<NbtUtil::ServerEntry> payload;
        check(Presets::serverListFromSection(sections.at(0), &payload, &why),
              "PC8 段读回来（这一趟和真落盘走的是同一段）", why);

        const Presets::ServerListReport report = Presets::applyServerListToInstance(
            toPath, sections.at(0).mode, payload);
        check(report.ok && report.written == 1 && report.skipped == 1 &&
                  readBytesAt(toPath) ==
                      nbtServersDat({NbtServerItem{QStringLiteral("已有的"),
                                                   QStringLiteral("two.example.com:25566")},
                                     NbtServerItem{QStringLiteral("家"),
                                                   QStringLiteral("home.example.com")}}),
              "★★ PC8 端到端：目标上已有的那台没被重复添进去、源里另一台接在表尾",
              QStringLiteral("写 %1 / 跳 %2：%3")
                  .arg(report.written)
                  .arg(report.skipped)
                  .arg(report.problem));
    }
}

