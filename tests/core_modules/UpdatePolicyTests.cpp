#include "Support.hpp"

void runUpdatePolicyTests() {
    section("ZT. 更新中心：忽略 / 固定那一本账");

    const QString originalDir = AppPaths::minecraftDir();
    QTemporaryDir temp;
    check(temp.isValid(), "ZT0 临时数据根目录建出来了");
    if (!temp.isValid()) {
        return;
    }
    AppPaths::setMinecraftDir(temp.path());

    const QString instanceDir = temp.path() + QStringLiteral("/versions/1.20.1-forge-47.2.0");
    const QString instanceId = QStringLiteral("1.20.1-forge-47.2.0");
    const QString otherId = QStringLiteral("1.19.2-fabric-0.15.11");
    QDir().mkpath(instanceDir + QStringLiteral("/mods"));

    // ---------- ZT1. 那把钥匙：相对实例目录、`/` 分隔 ----------
    //
    // 绝对路径一搬走（改数据目录 / 复制实例）就全废 —— 这一条是那本账能在盘上待住的根。
    check(UpdatePolicy::storeKey(instanceDir, instanceDir + QStringLiteral("/mods/jei.jar")) ==
              QStringLiteral("mods/jei.jar"),
          "★ ZT1 键是**实例内相对路径**（绝对路径搬一次家就全废）",
          UpdatePolicy::storeKey(instanceDir, instanceDir + QStringLiteral("/mods/jei.jar")));
    // 大小写 / 反斜杠：产品给的路径是 QFileInfo 出来的（在 Windows 上就是 `/`），但别的
    // 调用点万一塞了个反斜杠进来，键不能变成两个。
    check(UpdatePolicy::storeKey(instanceDir, instanceDir + QStringLiteral("/mods/jei.jar")) ==
              UpdatePolicy::storeKey(instanceDir, instanceDir + QStringLiteral("\\mods\\jei.jar")),
          "ZT1 反斜杠进来的那一条拼出**同一把**钥匙（不许一个文件记成两条）");
    // 不在这个实例底下的路径：回空串、调用方必须跳过（空键会把不相干的记录混成一条）。
    check(UpdatePolicy::storeKey(instanceDir, temp.path() + QStringLiteral("/other/mod.jar"))
              .isEmpty(),
          "★★ ZT1 不在这个实例底下的路径回**空键**（空键会把好几条混成一条、而且删不掉）");

    // ---------- ZT2. 两档的判据 ----------
    const QString shaNew = QStringLiteral("1111111111111111111111111111111111111111");
    const QString shaOld = QStringLiteral("2222222222222222222222222222222222222222");
    {
        UpdatePolicy::Entry ignored;
        ignored.ignoreVersionId = QStringLiteral("ver-2.0");
        check(UpdatePolicy::suppressed(ignored, shaNew, QStringLiteral("ver-2.0")) ==
                  UpdatePolicy::Suppress::Ignored,
              "ZT2 记着的那一版就是要换上的那一版 ⇒ 忽略生效");
        // 这一条是这一档存在的理由：作者发了更新的一版，它自己回来。
        check(UpdatePolicy::suppressed(ignored, shaNew, QStringLiteral("ver-3.0")) ==
                  UpdatePolicy::Suppress::None,
              "★★ ZT2 要换上的换成了**另一版** ⇒ 忽略**自己失效**（用户讨厌的是那一版，"
              "不是「别看更新」）");
        // 拼不出目标版本（空）：判不了 ⇒ 按"没记过"算，提示照出。
        check(UpdatePolicy::suppressed(ignored, shaNew, QString()) == UpdatePolicy::Suppress::None,
              "★★ ZT2 拼不出要换上的那一版 ⇒ **不拦**（判不了的事不许当成「忽略了」）");

        UpdatePolicy::Entry pinned;
        pinned.pinnedSha1 = shaOld;
        check(UpdatePolicy::suppressed(pinned, shaOld, QStringLiteral("ver-3.0")) ==
                  UpdatePolicy::Suppress::Pinned,
              "ZT2 盘上还是固定那一刻那一份 ⇒ 固定生效（后面有新版本也不提示）");
        check(UpdatePolicy::suppressed(pinned, shaNew, QStringLiteral("ver-3.0")) ==
                  UpdatePolicy::Suppress::None,
              "★★ ZT2 盘上那一份**被换过了** ⇒ 固定自动失效（「停在那一份上」不是「别看更新」）");
        check(UpdatePolicy::suppressed(pinned, QString(), QStringLiteral("ver-3.0")) ==
                  UpdatePolicy::Suppress::None,
              "★★ ZT2 拿不到盘上那一份的 sha1 ⇒ 固定那一档判不了 ⇒ **不拦**");
        // 两档同时记着（先忽略、后来固定）：固定那一档先说话（它是更"死"的那一条）。
        UpdatePolicy::Entry both;
        both.ignoreVersionId = QStringLiteral("ver-2.0");
        both.pinnedSha1 = shaOld;
        check(UpdatePolicy::suppressed(both, shaOld, QStringLiteral("ver-2.0")) ==
                  UpdatePolicy::Suppress::Pinned,
              "ZT2 两档都记着 ⇒ 报「已固定」（用户两个都按过，他最后按的那个意思更具体）");

        // 空记录 = 没记过。
        check(UpdatePolicy::suppressed(UpdatePolicy::Entry(), shaNew, QStringLiteral("ver-2.0")) ==
                  UpdatePolicy::Suppress::None,
              "ZT2 一条记录都没有 ⇒ 照常提示");
    }

    // ---------- ZT3. 过期固定的清理 ----------
    //
    // 「固定」过期了要**清掉**，不能留着：留着的话界面上一直写着"已固定"，而它其实早就
    // 什么都不管了（用户会以为"我固定住了"，实际每趟都在提示）。
    {
        UpdatePolicy::Store store;
        UpdatePolicy::setPinned(store, instanceId, QStringLiteral("mods/jei.jar"), shaOld);
        UpdatePolicy::setPinned(store, instanceId, QStringLiteral("mods/other.jar"), shaNew);
        UpdatePolicy::setIgnored(store, instanceId, QStringLiteral("mods/ignored.jar"),
                                 QStringLiteral("ver-2.0"));
        check(store.countFor(instanceId) == 3, "ZT3 三笔账记进去了",
              QString::number(store.countFor(instanceId)));

        QHash<QString, QString> now;
        now.insert(QStringLiteral("mods/jei.jar"), shaNew);      // 被换过了 ⇒ 这条固定过期
        now.insert(QStringLiteral("mods/other.jar"), shaNew);    // 还是那一份 ⇒ 留着
        now.insert(QStringLiteral("mods/ignored.jar"), shaNew);
        const int dropped = UpdatePolicy::dropStalePins(store, instanceId, now);
        check(dropped == 1, "★★ ZT3 只清掉过期的那一条固定", QString::number(dropped));
        check(UpdatePolicy::entryOf(store, instanceId, QStringLiteral("mods/jei.jar")).isEmpty(),
              "ZT3 过期的那条**整条没了**（不是留一个空壳）");
        check(!UpdatePolicy::entryOf(store, instanceId, QStringLiteral("mods/other.jar")).isEmpty(),
              "ZT3 没过期的那条还在");
        // 忽略那一档**不清**：它记的是"那一版"，只要那一版还在，用户的意思就还成立。
        check(UpdatePolicy::entryOf(store, instanceId, QStringLiteral("mods/ignored.jar"))
                  .ignoreVersionId == QStringLiteral("ver-2.0"),
              "★★ ZT3 忽略那一档**不清**（它记的是「那一版」，和盘上现在是什么内容无关）");
        // 盘上已经**没有**这个文件了（这一趟的清单里根本没有这个键）⇒ 那条记录指不到任何
        // 东西了，清掉。
        QHash<QString, QString> emptyMap;
        check(UpdatePolicy::dropStalePins(store, instanceId, emptyMap) == 1,
              "★ ZT3 盘上见不到这个文件（清单里连这个键都没有）⇒ 那条固定也清掉");
        // 而"键在、sha1 空"是**另一回事**：那是"读不动 / 没算出来"，**不许**当成"换了"
        // —— 清一条记录是不可逆的，用户会发现固定自己没了、而且不知道为什么。
        UpdatePolicy::Store keep;
        UpdatePolicy::setPinned(keep, instanceId, QStringLiteral("mods/jei.jar"), shaOld);
        QHash<QString, QString> unknown;
        unknown.insert(QStringLiteral("mods/jei.jar"), QString());
        check(UpdatePolicy::dropStalePins(keep, instanceId, unknown) == 0 &&
                  UpdatePolicy::entryOf(keep, instanceId, QStringLiteral("mods/jei.jar"))
                          .pinnedSha1 == shaOld,
              "★★ ZT3 键在、但这一趟没算出 sha1 ⇒ **不许清**（「不知道」不能当成「换了」）");
    }

    // ---------- ZT4. 取消：传空就把那一档抹掉，两条都空了整条不留 ----------
    {
        UpdatePolicy::Store store;
        UpdatePolicy::setIgnored(store, instanceId, QStringLiteral("mods/jei.jar"),
                                 QStringLiteral("ver-2.0"));
        UpdatePolicy::setPinned(store, instanceId, QStringLiteral("mods/jei.jar"), shaOld);
        UpdatePolicy::setIgnored(store, instanceId, QStringLiteral("mods/jei.jar"), QString());
        check(UpdatePolicy::entryOf(store, instanceId, QStringLiteral("mods/jei.jar")).pinnedSha1
                  == shaOld,
              "ZT4 取消忽略之后固定那一档还在（两档互不牵连）");
        UpdatePolicy::setPinned(store, instanceId, QStringLiteral("mods/jei.jar"), QString());
        check(UpdatePolicy::entryOf(store, instanceId, QStringLiteral("mods/jei.jar")).isEmpty(),
              "★★ ZT4 两档都取消 ⇒ 这一条**整条不留**");
        check(store.isEmpty(), "★★ ZT4 一个文件的记录都没有了 ⇒ 这个实例也不留在表里"
                               "（不留空壳，盘上那份文件也就跟着空了）");
    }

    // ---------- ZT5. 真存真读 ----------
    {
        UpdatePolicy::Store store;
        UpdatePolicy::setIgnored(store, instanceId, QStringLiteral("mods/jei.jar"),
                                 QStringLiteral("ver-2.0"));
        UpdatePolicy::setPinned(store, instanceId, QStringLiteral("shaderpacks/光影.zip"), shaOld);
        UpdatePolicy::setIgnored(store, otherId, QStringLiteral("datapacks/包.zip"),
                                 QStringLiteral("ver-9.9"));
        QString problem;
        check(UpdatePolicy::save(store, UpdatePolicy::storePath(), &problem),
              "ZT5 账本写进盘了", problem);
        // 落点：`<数据目录>/wtfml.updates`（挂在数据根目录下、可以整个拷走）。
        check(QFileInfo::exists(UpdatePolicy::storePath()),
              "ZT5 落在 `<数据目录>/wtfml.updates`", UpdatePolicy::storePath());

        bool ok = false;
        QString readProblem;
        const UpdatePolicy::Store back = UpdatePolicy::load(UpdatePolicy::storePath(), &readProblem);
        check(readProblem.isEmpty(), "ZT5 读回来没报毛病", readProblem);
        check(back.countFor(instanceId) == 2 && back.countFor(otherId) == 1,
              "ZT5 两个实例的账都对得上",
              QStringLiteral("%1 / %2").arg(back.countFor(instanceId)).arg(back.countFor(otherId)));
        const UpdatePolicy::Entry jei =
            UpdatePolicy::entryOf(back, instanceId, QStringLiteral("mods/jei.jar"));
        check(jei.ignoreVersionId == QStringLiteral("ver-2.0"),
              "ZT5 逐字一样地回来了（忽略那一档）", jei.ignoreVersionId);
        check(UpdatePolicy::entryOf(back, instanceId, QStringLiteral("shaderpacks/光影.zip"))
                  .pinnedSha1 == shaOld,
              "★ ZT5 路径里的中文原样回来（**不是**拼一把转义过的键去查 —— 那会一条都查不到）");

        // 格式标记：认不出的文件按"这份账读不懂"处理，**绝不半信半疑地往里加记录**。
        bool tagOk = false;
        const UpdatePolicy::Store foreign =
            UpdatePolicy::fromJson(QByteArrayLiteral("{\"format\":\"别的启动器\",\"version\":1}"),
                                   &tagOk);
        check(!tagOk && foreign.isEmpty(),
              "★★ ZT5 别人写的 json（格式标记不认）⇒ 空表 + **读不懂**：半信半疑地读进来，"
              "最坏的结果是把用户手上那几十个文件按一份莫名其妙的名单判成「已忽略」");
        // 这一条是"读不懂"要**说出来**（`load()` 那句毛病说的是"不是我们写的那种格式"）。
        // 读不懂却回一句"什么都没有"，用户下次打开会发现忽略全没了、而日志里一个字都没有。
        const QString foreignPath = temp.path() + QStringLiteral("/foreign.updates");
        {
            QFile file(foreignPath);
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                file.write(QByteArrayLiteral("{\"format\":\"别的启动器\",\"version\":1}"));
            }
        }
        QString foreignProblem;
        const UpdatePolicy::Store fromDisk = UpdatePolicy::load(foreignPath, &foreignProblem);
        check(fromDisk.isEmpty() && !foreignProblem.isEmpty(),
              "★★ ZT5 读盘那一趟把这件事**说出来**了（日志里要看得见）", foreignProblem);
        bool badOk = false;
        const UpdatePolicy::Store bad = UpdatePolicy::fromJson(QByteArrayLiteral("{ 这不是 json"),
                                                               &badOk);
        check(!badOk, "★★ ZT5 根本不是 json ⇒ `ok = false`（和「不是我们写的」是两句话）");
        check(UpdatePolicy::fromJson(UpdatePolicy::toJson(UpdatePolicy::Store())).isEmpty(),
              "ZT5 空表写出去再读回来还是空的");

        // 读盘：文件不在**不是错误**（从来没忽略过谁就是这样）。
        QString missingProblem;
        const UpdatePolicy::Store none =
            UpdatePolicy::load(temp.path() + QStringLiteral("/wtfml.updates.nosuch"),
                               &missingProblem);
        check(missingProblem.isEmpty() && none.isEmpty(),
              "★★ ZT5 文件不在 ⇒ 空表、而且**不算错**（从来没忽略过谁就是这个样子）",
              missingProblem);
    }

    AppPaths::setMinecraftDir(originalDir);
    check(AppPaths::minecraftDir() == originalDir, "ZT9 数据根目录换回来了");
}

