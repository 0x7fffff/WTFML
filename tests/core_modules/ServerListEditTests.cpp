#include "Support.hpp"

void runServerListEditTests() {
    QTemporaryDir dir;
    check(dir.isValid(), "ZK0 建得出一个临时目录来摆夹具");
    if (!dir.isValid()) {
        return;
    }
    const QString datPath = dir.filePath(QStringLiteral("servers.dat"));

    // 夹具三条：第一条是"游戏真写出来的那个形状"（hidden/ip/name/icon 四格全在）；第二条
    // 只有 name + ip；第三条**没有 name**（1.12.2 那种老 fixture）。
    const QList<NbtServerItem> fixture{
        NbtServerItem{QStringLiteral("A服"), QStringLiteral("a.example.com:25565"), true, true, true,
                      true, QStringLiteral("iVBORw0KGgo=")},
        NbtServerItem{QStringLiteral("B服"), QStringLiteral("b.example.com"), true, true, false,
                      false, QString()},
        NbtServerItem{QString(), QStringLiteral("c.example.com"), false, true, false, false,
                      QString()},
    };
    QList<NbtServerItem> current = fixture;

    check(writeBytesAt(datPath, nbtServersDat(fixture)), "ZK0 夹具落盘了");

    section("ZK1. 先证明夹具是「游戏会写的那种文件」");
    {
        QList<NbtUtil::ServerEntry> rows;
        QString problem;
        const NbtUtil::Status status = ServerListFile::read(datPath, &rows, &problem);
        check(status == NbtUtil::Status::Ok && rows.size() == 3,
              "ZK1 三条都读出来了（夹具是合法的 servers.dat）",
              QStringLiteral("status=%1 rows=%2 %3")
                  .arg(static_cast<int>(status))
                  .arg(rows.size())
                  .arg(problem));
        check(rows.size() == 3 && rows.at(0).name == QStringLiteral("A服") &&
                  rows.at(0).address == QStringLiteral("a.example.com:25565") &&
                  rows.at(1).name == QStringLiteral("B服") &&
                  rows.at(2).name.isEmpty() &&
                  rows.at(2).address == QStringLiteral("c.example.com"),
              "ZK1 名字 / 地址逐条对得上（第三条没有 name ⇒ 空串）");
    }

    section("ZK2. 改一条：成功，而且没碰的地方一个字节都不变");
    {
        QString problem;
        const bool ok = ServerListFile::updateEntry(datPath, QStringLiteral("A服"),
                                                    QStringLiteral("a.example.com:25565"),
                                                    QStringLiteral("新名字"),
                                                    QStringLiteral("new.example.com"), &problem);
        check(ok, "ZK2 改名 + 改地址成功", problem);

        current[0].name = QStringLiteral("新名字");
        current[0].ip = QStringLiteral("new.example.com");
        // 逐字节比一把：只有那两格的值变了 —— `hidden` 还是 Byte 0、`icon` 还是那一串、
        // 字段顺序一格都没动。这就是"整棵树原样写回去"那句话在盘上的样子。
        check(readBytesAt(datPath) == nbtServersDat(current),
              "ZK2 写回去的字节 = 只把那两格换掉的期望值（别的逐字节不变）");

        QList<NbtUtil::ServerEntry> rows;
        ServerListFile::read(datPath, &rows, nullptr);
        check(rows.size() == 3 && rows.at(0).name == QStringLiteral("新名字") &&
                  rows.at(0).address == QStringLiteral("new.example.com"),
              "ZK2 读回来就是改完的样子");
    }

    section("ZK3. 名字对不上 ⇒ 退一步按地址认");
    {
        QString problem;
        const bool ok = ServerListFile::updateEntry(datPath, QStringLiteral("B服改成别的了"),
                                                    QStringLiteral("b.example.com"),
                                                    QStringLiteral("B2"),
                                                    QStringLiteral("b.example.com"), &problem);
        check(ok, "ZK3 名字没对上、但地址在盘上只有一条 ⇒ 认那一条", problem);
        current[1].name = QStringLiteral("B2");

        QList<NbtUtil::ServerEntry> rows;
        ServerListFile::read(datPath, &rows, nullptr);
        check(rows.size() > 1 && rows.at(1).name == QStringLiteral("B2"),
              "ZK3 换掉的是那一条的名字",
              rows.size() > 1 ? rows.at(1).name : QStringLiteral("<条数不对>"));
        check(readBytesAt(datPath) == nbtServersDat(current), "ZK3 同样只有那两格变了");
    }

    section("ZK4 / ZK5. 拒写的两条：找不到那一条 / 新地址是空的");
    {
        const QByteArray before = readBytesAt(datPath);
        QString problem;
        const bool notFound = ServerListFile::updateEntry(
            datPath, QStringLiteral("谁"), QStringLiteral("nope.example.com"),
            QStringLiteral("算了"), QStringLiteral("nope2.example.com"), &problem);
        check(!notFound && !problem.isEmpty(), "ZK4 地址在盘上找不到 ⇒ 拒绝，并给一句话", problem);
        check(readBytesAt(datPath) == before, "ZK4 拒写之后盘上逐字节还是原来那份");

        problem.clear();
        const bool emptyAddress = ServerListFile::updateEntry(
            datPath, QStringLiteral("B2"), QStringLiteral("b.example.com"), QStringLiteral("B2"),
            QStringLiteral("   "), &problem);
        check(!emptyAddress && !problem.isEmpty(), "ZK5 地址空 ⇒ 拒绝（地址是这一条唯一的实质内容）",
              problem);
        check(readBytesAt(datPath) == before, "ZK5 拒写之后盘上逐字节还是原来那份");
    }

    section("ZK6. 同一个地址出现两条、名字又没对上 ⇒ 认不准，不猜");
    {
        const QByteArray dup = nbtServersDat({
            NbtServerItem{QStringLiteral("一"), QStringLiteral("dup.example.com"), true, true,
                          false, false, QString()},
            NbtServerItem{QStringLiteral("二"), QStringLiteral("dup.example.com"), true, true,
                          false, false, QString()},
        });
        check(writeBytesAt(datPath, dup), "ZK6 两条同地址的夹具落盘了");

        QString problem;
        const bool ok = ServerListFile::updateEntry(datPath, QStringLiteral("对不上的名字"),
                                                    QStringLiteral("dup.example.com"),
                                                    QStringLiteral("改"),
                                                    QStringLiteral("dup.example.com"), &problem);
        check(!ok && problem.contains(QStringLiteral("2")),
              "ZK6 地址重了 2 次、名字又没对上 ⇒ 拒绝，并说清是几次", problem);
        check(readBytesAt(datPath) == dup, "ZK6 拒写之后盘上逐字节还是那两条");

        check(writeBytesAt(datPath, nbtServersDat(current)), "ZK6 把夹具换回来");
    }

    section("ZK7. ★ 认的是值不是下标：没有 ip 的那一条排在最前面");
    {
        const QList<NbtServerItem> withGhost{
            NbtServerItem{QStringLiteral("我在界面上一行都不占"), QString(), true, false, false,
                          false, QString()},  // 没有 ip ⇒ readServerList 跳过它
            NbtServerItem{QStringLiteral("真服"), QStringLiteral("real.example.com"), true, true,
                          false, false, QString()},
            NbtServerItem{QStringLiteral("尾"), QStringLiteral("tail.example.com"), true, true,
                          false, false, QString()},
        };
        check(writeBytesAt(datPath, nbtServersDat(withGhost)), "ZK7 带一条没有 ip 的夹具落盘了");

        QList<NbtUtil::ServerEntry> rows;
        ServerListFile::read(datPath, &rows, nullptr);
        check(rows.size() == 2 && rows.at(0).name == QStringLiteral("真服"),
              "ZK7 界面上只有两行（没有 ip 的那条不出现）", QString::number(rows.size()));

        QString problem;
        // 界面上第 0 行是「真服」，而**盘上第 0 个元素是那条没有 ip 的** —— 照下标改就会
        // 改到它头上，它在界面上根本看不见，改完谁也不知道。
        const bool ok = ServerListFile::updateEntry(
            datPath, QStringLiteral("真服"), QStringLiteral("real.example.com"),
            QStringLiteral("真服II"), QStringLiteral("real2.example.com"), &problem);
        check(ok, "ZK7 改界面上的第 0 行（盘上是第 1 个元素）", problem);

        NbtTree::Document doc;
        QString why;
        const bool parsed = NbtTree::parse(readBytesAt(datPath), &doc, &why);
        check(parsed, "ZK7 写出来的东西还解得开", why);
        const NbtTree::Node *servers =
            parsed ? NbtTree::field(&doc.root, QStringLiteral("servers")) : nullptr;
        if (servers == nullptr || servers->items.size() != 3) {
            check(false, "ZK7 写出来的 servers 表还是三条");
        } else {
            check(nbtFieldNames(servers->items.at(0)) == QStringList{QStringLiteral("name")},
                  "ZK7 盘上第 0 个元素（没有 ip 的那条）**一格都没动**",
                  nbtFieldNames(servers->items.at(0)).join(QStringLiteral(", ")));
            check(nbtFieldText(servers->items.at(1), QStringLiteral("name")) ==
                          QStringLiteral("真服II") &&
                      nbtFieldText(servers->items.at(1), QStringLiteral("ip")) ==
                          QStringLiteral("real2.example.com"),
                  "ZK7 改的是盘上第 1 个元素（界面上第 0 行）");
            check(nbtFieldText(servers->items.at(2), QStringLiteral("name")) == QStringLiteral("尾"),
                  "ZK7 后面那一条也没被碰");
        }
    }

    section("ZK8. 盘上没有 name 那一格 ⇒ 按空串认，写的时候补建一格");
    {
        const QList<NbtServerItem> oldStyle{
            NbtServerItem{QString(), QStringLiteral("c.example.com"), false, true, false, true,
                          QString()},
        };
        check(writeBytesAt(datPath, nbtServersDat(oldStyle)),
              "ZK8 没有 name 那一格的老 fixture 落盘了");

        QString problem;
        const bool ok = ServerListFile::updateEntry(
            datPath, QString(), QStringLiteral("c.example.com"), QStringLiteral("起名字了"),
            QStringLiteral("c.example.com"), &problem);
        check(ok, "ZK8 名字是空串的那一条（盘上没有 name 格）也能改", problem);

        NbtTree::Document doc;
        QString why;
        const bool parsed = NbtTree::parse(readBytesAt(datPath), &doc, &why);
        const NbtTree::Node *servers =
            parsed ? NbtTree::field(&doc.root, QStringLiteral("servers")) : nullptr;
        check(parsed && servers != nullptr && servers->items.size() == 1,
              "ZK8 写出来的东西解得开、还是一条", why);
        if (servers != nullptr && servers->items.size() == 1) {
            const NbtTree::Node &only = servers->items.at(0);
            check(nbtFieldNames(only) == QStringList{QStringLiteral("hidden"),
                                                     QStringLiteral("ip"),
                                                     QStringLiteral("name")},
                  "ZK8 补建的 name 落在末尾（顺序 = hidden/ip/name —— 和真机那份一样）",
                  nbtFieldNames(only).join(QStringLiteral(", ")));
            check(nbtFieldText(only, QStringLiteral("name")) == QStringLiteral("起名字了"),
                  "ZK8 name 那一格真的写进去了");
            const NbtTree::Node *hidden = NbtTree::field(only, QStringLiteral("hidden"));
            check(hidden != nullptr && hidden->type == NbtTree::Type::Byte && hidden->number == 0,
                  "ZK8 旁边那一格 hidden 还活着、还是 Byte 0");
        }
    }

    section("ZK9. 原来是 gzip ⇒ 写回去还是 gzip");
    {
        // 游戏读的是 `NbtIo.readCompressed`，所以"顺手帮你压一下"或者"顺手解开"两种都不是
        // 用户要的 —— 换了形状会让游戏当场把这个文件当成坏的。
        const QString plainPath = dir.filePath(QStringLiteral("plain.dat"));
        const QString gzPath = dir.filePath(QStringLiteral("gzip.dat"));
        check(writeBytesAt(plainPath, nbtServersDat(fixture)), "ZK9 裸的那一份落盘了");
        QString why;
        check(ZipUtil::gzipFrom(plainPath, gzPath, &why), "ZK9 压成了 gzip", why);
        const QByteArray gzBefore = readBytesAt(gzPath);
        check(gzBefore.size() >= 2 && static_cast<uchar>(gzBefore.at(0)) == 0x1F &&
                  static_cast<uchar>(gzBefore.at(1)) == 0x8B,
              "ZK9 夹具确实是 gzip（1f 8b）");

        QString problem;
        const bool ok = ServerListFile::updateEntry(
            gzPath, QStringLiteral("B服"), QStringLiteral("b.example.com"), QStringLiteral("B压"),
            QStringLiteral("b.example.com"), &problem);
        check(ok, "ZK9 gzip 的那一份也能改", problem);

        const QByteArray gzAfter = readBytesAt(gzPath);
        check(gzAfter.size() >= 2 && static_cast<uchar>(gzAfter.at(0)) == 0x1F &&
                  static_cast<uchar>(gzAfter.at(1)) == 0x8B,
              "ZK9 写回去**还是 gzip**（不是顺手解压成裸 NBT）");

        const QString outPath = dir.filePath(QStringLiteral("out.dat"));
        check(ZipUtil::gunzipTo(gzPath, outPath, &why), "ZK9 解回来看看里面", why);
        QList<NbtUtil::ServerEntry> rows;
        const NbtUtil::Status status = ServerListFile::read(outPath, &rows, nullptr);
        check(status == NbtUtil::Status::Ok && rows.size() == 3 &&
                  rows.size() > 1 && rows.at(1).name == QStringLiteral("B压"),
              "ZK9 解开之后那一格真的变了，别的地方没动", QString::number(rows.size()));
    }

    section("ZK10. 拒写的另外两条：文件不在 / 根里没有 servers");
    {
        QString problem;
        const bool nowhere = ServerListFile::updateEntry(
            dir.filePath(QStringLiteral("nope/servers.dat")), QStringLiteral("A"),
            QStringLiteral("a"), QStringLiteral("B"), QStringLiteral("b"), &problem);
        check(!nowhere && problem.contains(QStringLiteral("servers.dat")),
              "ZK10 文件不在 ⇒ 拒绝，并说清是哪一个文件", problem);

        const QByteArray notServers = nbtTag(
            0x0A, QByteArray(),
            nbtCompoundBody({nbtTag(0x08, QByteArrayLiteral("foo"), nbtString(QStringLiteral("bar")))}));
        const QString oddPath = dir.filePath(QStringLiteral("odd.dat"));
        check(writeBytesAt(oddPath, notServers), "ZK10 一份没有 servers 那格的 NBT 落盘了");
        problem.clear();
        const bool ok = ServerListFile::updateEntry(oddPath, QString(), QStringLiteral("a"),
                                                    QStringLiteral("b"), QStringLiteral("c"),
                                                    &problem);
        check(!ok && !problem.isEmpty(), "ZK10 根里没有 servers 那一格 ⇒ 拒绝", problem);
        check(readBytesAt(oddPath) == notServers, "ZK10 拒写之后那一份逐字节没动");
    }

    section("ZK11. 读那一半就是 NbtUtil（不多不少）");
    {
        QList<NbtUtil::ServerEntry> rows;
        QString problem;
        check(ServerListFile::read(dir.filePath(QStringLiteral("missing/servers.dat")), &rows,
                                   &problem) == NbtUtil::Status::Missing,
              "ZK11 文件不在 ⇒ Missing（不是 Unreadable，也不是一张空的 Ok 表）");
    }

    section("ZK12. 往已有的那一份里加一条：接在表尾，别的条目逐字节不动");
    {
        const QString addPath = dir.filePath(QStringLiteral("add.dat"));
        check(writeBytesAt(addPath, nbtServersDat(fixture)), "ZK12 夹具落盘了");
        const int sizeBefore = readBytesAt(addPath).size();

        QString problem;
        const bool ok = ServerListFile::addEntry(addPath, QStringLiteral("D服"),
                                                 QStringLiteral("d.example.com:25565"), &problem);
        check(ok, "ZK12 加一条成功", problem);

        QList<NbtServerItem> expected = fixture;
        expected.append(
            NbtServerItem{QStringLiteral("D服"), QStringLiteral("d.example.com:25565")});
        check(readBytesAt(addPath) == nbtServersDat(expected),
              "ZK12 接在表尾，前三条逐字节没动（含 icon / hidden 那些没认的格）");
        check(readBytesAt(addPath).size() > sizeBefore, "ZK12 文件真的变长了");

        QList<NbtUtil::ServerEntry> rows;
        const NbtUtil::Status status = ServerListFile::read(addPath, &rows, nullptr);
        check(status == NbtUtil::Status::Ok && rows.size() == 4 &&
                  rows.at(3).name == QStringLiteral("D服") &&
                  rows.at(3).address == QStringLiteral("d.example.com:25565"),
              "ZK12 读回来多出来的就是最后那一条", QString::number(rows.size()));
    }

    section("ZK13. 文件不在 ⇒ 建一份新的，而且**是 gzip**");
    {
        // 这个实例从来没加过服务器时 `servers.dat` 就是不在的，而那正是"添加"最常见的
        // 第一次 —— 所以这一档**不是失败**（"改"和"删"那两条路上它是）。
        const QString freshPath = dir.filePath(QStringLiteral("brandnew.dat"));
        QFile::remove(freshPath);

        QString problem;
        const bool ok = ServerListFile::addEntry(freshPath, QStringLiteral("第一台"),
                                                 QStringLiteral("first.example.com"), &problem);
        check(ok, "ZK13 文件不在时也能加", problem);

        const QByteArray bytes = readBytesAt(freshPath);
        check(bytes.size() >= 2 && static_cast<uchar>(bytes.at(0)) == 0x1F &&
                  static_cast<uchar>(bytes.at(1)) == 0x8B,
              "ZK13 新写出来的那一份是 gzip（游戏读的是 NbtIo.readCompressed）");

        // 解开之后逐字节比：形状 = 无名根 + 一格 servers（夹具就是这么造的）。
        const QString outPath = dir.filePath(QStringLiteral("brandnew-out.dat"));
        QString why;
        check(ZipUtil::gunzipTo(freshPath, outPath, &why), "ZK13 解回来看看里面", why);
        const QList<NbtServerItem> want{NbtServerItem{QStringLiteral("第一台"),
                                                      QStringLiteral("first.example.com")}};
        check(readBytesAt(outPath) == nbtServersDat(want),
              "ZK13 解出来就是那份夹具的形状（无名根 + servers 一格）");

        QList<NbtUtil::ServerEntry> rows;
        const NbtUtil::Status status = ServerListFile::read(freshPath, &rows, nullptr);
        check(status == NbtUtil::Status::Ok && rows.size() == 1 &&
                  rows.at(0).name == QStringLiteral("第一台") &&
                  rows.at(0).address == QStringLiteral("first.example.com"),
              "ZK13 读回来就是刚加的那一条", QString::number(rows.size()));
    }

    section("ZK14. 删一条：剩下的逐条对得上；删到空表仍然合法");
    {
        const QString rmPath = dir.filePath(QStringLiteral("rm.dat"));
        check(writeBytesAt(rmPath, nbtServersDat(fixture)), "ZK14 夹具落盘了");

        QString problem;
        check(ServerListFile::removeEntry(rmPath, QStringLiteral("B服"),
                                          QStringLiteral("b.example.com"), &problem),
              "ZK14 删中间那一条", problem);
        const QList<NbtServerItem> afterMiddle{fixture.at(0), fixture.at(2)};
        check(readBytesAt(rmPath) == nbtServersDat(afterMiddle),
              "ZK14 少了那一条，别的逐字节不变");

        // 第三条**没有 name**（1.12.2 那种）：按地址认，照样删得掉 —— 这条钉的是
        // "认值不认下标"在**删**这边的等价物。
        problem.clear();
        check(ServerListFile::removeEntry(rmPath, QString(), QStringLiteral("c.example.com"),
                                          &problem),
              "ZK14 只有 ip 没有 name 的那一条也能按地址删掉", problem);

        problem.clear();
        check(ServerListFile::removeEntry(rmPath, QStringLiteral("A服"),
                                          QStringLiteral("a.example.com:25565"), &problem),
              "ZK14 删掉最后一条", problem);

        problem.clear();
        QList<NbtUtil::ServerEntry> rows;
        const NbtUtil::Status status = ServerListFile::read(rmPath, &rows, &problem);
        check(status == NbtUtil::Status::Ok && rows.isEmpty(),
              "ZK14 删到空 ⇒ 仍然读得动，只是 0 条（不是坏文件）", problem);

        // 空表**也带着元素类型** —— 游戏就是靠它认这张表的。
        NbtTree::Document doc;
        QString why;
        check(NbtTree::parse(readBytesAt(rmPath), &doc, &why), "ZK14 那份还能 parse 回来", why);
        const NbtTree::Node *servers = NbtTree::field(doc.root, QStringLiteral("servers"));
        check(servers != nullptr && servers->type == NbtTree::Type::List &&
                  servers->items.isEmpty() && servers->itemType == NbtTree::Type::Compound,
              "ZK14 留下的是「元素类型是复合标签的空表」（不是把文件删了）");
    }

    section("ZK15. 拒写的两条：地址是空的 / 那份读不懂");
    {
        const QString guardPath = dir.filePath(QStringLiteral("guard.dat"));
        check(writeBytesAt(guardPath, nbtServersDat(fixture)), "ZK15 夹具落盘了");
        const QByteArray before = readBytesAt(guardPath);

        QString problem;
        check(!ServerListFile::addEntry(guardPath, QStringLiteral("X"), QStringLiteral("   "),
                                        &problem),
              "ZK15 地址是空的 ⇒ 拒绝", problem);
        check(readBytesAt(guardPath) == before, "ZK15 拒写之后逐字节没动（一）");

        problem.clear();
        check(!ServerListFile::removeEntry(guardPath, QStringLiteral("不存在"),
                                           QStringLiteral("nowhere.example.com"), &problem),
              "ZK15 盘上没有那一条 ⇒ 拒绝", problem);
        check(readBytesAt(guardPath) == before, "ZK15 拒写之后逐字节没动（二）");

        // 一份读不懂的 servers.dat：**不覆盖**。覆盖等于把用户的东西扔掉。
        const QByteArray garbage = QByteArrayLiteral("this is not nbt at all");
        const QString brokenPath = dir.filePath(QStringLiteral("broken.dat"));
        check(writeBytesAt(brokenPath, garbage), "ZK15 一份坏文件落盘了");
        problem.clear();
        check(!ServerListFile::addEntry(brokenPath, QStringLiteral("X"),
                                        QStringLiteral("x.example.com"), &problem),
              "ZK15 读不懂的那一份 ⇒ 加不进去", problem);
        check(readBytesAt(brokenPath) == garbage, "ZK15 坏文件逐字节没动");
    }

    section("ZK16. gzip 的那一份：加 / 删之后**还是 gzip**");
    {
        const QString plainPath = dir.filePath(QStringLiteral("gz-plain.dat"));
        const QString gzPath = dir.filePath(QStringLiteral("gz-round.dat"));
        check(writeBytesAt(plainPath, nbtServersDat(fixture)), "ZK16 先造一份裸的");
        QString why;
        check(ZipUtil::gzipFrom(plainPath, gzPath, &why), "ZK16 压成 gzip", why);

        QString problem;
        check(ServerListFile::addEntry(gzPath, QStringLiteral("E服"),
                                       QStringLiteral("e.example.com"), &problem),
              "ZK16 gzip 的那一份也能加", problem);
        problem.clear();
        check(ServerListFile::removeEntry(gzPath, QStringLiteral("B服"),
                                          QStringLiteral("b.example.com"), &problem),
              "ZK16 gzip 的那一份也能删", problem);

        const QByteArray bytes = readBytesAt(gzPath);
        check(bytes.size() >= 2 && static_cast<uchar>(bytes.at(0)) == 0x1F &&
                  static_cast<uchar>(bytes.at(1)) == 0x8B,
              "ZK16 写回去**还是 gzip**（不是顺手解压成裸 NBT）");

        const QString outPath = dir.filePath(QStringLiteral("gz-round-out.dat"));
        check(ZipUtil::gunzipTo(gzPath, outPath, &why), "ZK16 解回来看看", why);
        const QList<NbtServerItem> want{fixture.at(0), fixture.at(2),
                                        NbtServerItem{QStringLiteral("E服"),
                                                      QStringLiteral("e.example.com")}};
        check(readBytesAt(outPath) == nbtServersDat(want),
              "ZK16 解开之后 = 加一条、删一条的样子（其余逐字节不变）");
    }

    section("R4. 空表是「元素类型 End」那一档：加完必须仍是一份读得回来的 servers.dat");
    {
        // 游戏里把服务器**删光**之后写出来的那个形状：`servers` 是 count=0、元素类型是
        // End 的空表。这是合法 NBT，但写盘那一步按 `itemType` 落 —— 不补元素类型的话，
        // 往里 append 一条之后写出去的是 `List(End, count=1)`，游戏和本启动器都读不回来。
        QByteArray emptyListPayload;
        emptyListPayload.append('\0');   // 元素类型 = TAG_End
        emptyListPayload.append(nbtInt(0));
        const QByteArray endEmpty =
            nbtTag(0x0A, QByteArray(),
                   nbtCompoundBody({nbtTag(0x09, QByteArrayLiteral("servers"), emptyListPayload)}));
        const QString endPath = dir.filePath(QStringLiteral("empty-end.dat"));
        check(writeBytesAt(endPath, endEmpty), "R4-1 一份「空表 + 元素类型 End」落盘了");

        QString problem;
        check(ServerListFile::addEntry(endPath, QStringLiteral("新服"),
                                       QStringLiteral("new.example.com"), &problem),
              "★ R4-1 addEntry 接受了这个形状（不是「这一格不是服务器表」）", problem);

        // 两个判据，缺一不可：
        //   ① 我们自己读得回来（`read()` 走的是同一个解析器）；
        //   ② 盘上那棵树里 `servers` 的元素类型是 Compound、而且有一条。
        QList<NbtUtil::ServerEntry> rows;
        QString readProblem;
        const NbtUtil::Status status = ServerListFile::read(endPath, &rows, &readProblem);
        check(status == NbtUtil::Status::Ok && rows.size() == 1 &&
                  rows.at(0).address == QStringLiteral("new.example.com"),
              "★ R4-1 写出去的那一份读得回来（List 的元素类型补成了 Compound）",
              QStringLiteral("status=%1 rows=%2 %3")
                  .arg(static_cast<int>(status))
                  .arg(rows.size())
                  .arg(readProblem));

        NbtTree::Document document;
        QString parseProblem;
        const bool parsed = NbtTree::parse(readBytesAt(endPath), &document, &parseProblem);
        const NbtTree::Node *servers =
            NbtTree::field(&document.root, QStringLiteral("servers"));
        check(parsed && servers != nullptr &&
                  servers->type == NbtTree::Type::List &&
                  servers->itemType == NbtTree::Type::Compound &&
                  servers->items.size() == 1,
              "R4-1 盘上那一格是 List<Compound>、一条（不是 List<End>）",
              parseProblem);
    }
}

