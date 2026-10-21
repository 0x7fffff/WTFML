#include "Support.hpp"

void runAsciiAliasTests() {
    section("ZM. Java 8 的 ASCII 别名（非 ASCII 路径 ⇒ 联接 + 硬链接）");

    const QString originalDir = AppPaths::minecraftDir();

    // --- ZM1. 纯判据：谁要换、谁不换 ----------------------------------------
    check(AsciiPathAlias::hasNonAscii(QStringLiteral("20w14∞")), "ZM1 目录名里的 ∞ 算非 ASCII");
    check(AsciiPathAlias::hasNonAscii(QStringLiteral("魔改版ATT2-BC&A&IS(多周目)")),
          "ZM1 中文名算非 ASCII");
    check(!AsciiPathAlias::hasNonAscii(QStringLiteral("ATT2-BC&A&IS(1.8) [x] _ + ~ #")),
          "ZM1.5 ★ 判据是「> 0x7F」，不是「看着像特殊字符」—— 那一堆符号全是 ASCII，不许换");
    check(!AsciiPathAlias::hasNonAscii(QStringLiteral("1.21.4-Fabric 0.16.14")),
          "ZM1 纯 ASCII 的名字不算");
    check(!AsciiPathAlias::hasNonAscii(QString()), "ZM1 空串不算");

    const QString uniPath = QStringLiteral("D:/MC/versions/20w14∞");
    check(AsciiPathAlias::needed(8, {uniPath}), "ZM2 Java 8 + 非 ASCII ⇒ 要换");
    check(!AsciiPathAlias::needed(17, {uniPath}), "ZM2 Java 17 ⇒ 不换（它本来就认得 UTF-8）");
    check(!AsciiPathAlias::needed(0, {uniPath}),
          "ZM2 ★ 没探到 Java 版本 ⇒ **不换**（判错的代价不对称，见 .hpp）");
    check(!AsciiPathAlias::needed(8, {QStringLiteral("D:/MC/versions/1.20.1")}),
          "ZM2 全 ASCII ⇒ 不换（真机 70 个实例里 68 个走这条，行为必须逐字节不变）");
    check(!AsciiPathAlias::needed(8, {}), "ZM2 一条路径都没给 ⇒ 不换");

    // --- ZM3. 路径的形状（纯函数） -----------------------------------------
    QTemporaryDir temp;
    const QString root = temp.path() + QStringLiteral("/mc");
    AppPaths::setMinecraftDir(root);

    const QString unicodeId = QStringLiteral("20w14∞");
    const AsciiPathAlias::Paths alias = AsciiPathAlias::pathsFor(unicodeId);
    check(!AsciiPathAlias::hasNonAscii(alias.root),
          "ZM3 ★ 别名根是纯 ASCII 的（这不是讲究：数据目录自己带中文时整套就没用了，"
          "那种情况 ensure() 会直说）",
          alias.root);
    check(alias.root.startsWith(root + QStringLiteral("/cache/ascii/")),
          "ZM3 别名挂在 <数据目录>/cache/ascii 底下（不是 versions/ 里 —— 那底下每个子目录"
          "都会被当成实例，而且游戏自己会扫）",
          alias.root);
    const QString tail = alias.root.mid(alias.root.lastIndexOf(QLatin1Char('/')) + 1);
    check(tail.size() == 12 && !tail.contains(QRegularExpression(QStringLiteral("[^0-9a-f]"))),
          "ZM3 根目录名是 12 位十六进制（实例 id 的哈希）", tail);
    check(AsciiPathAlias::pathsFor(unicodeId).root == alias.root,
          "ZM3 ★ 同一个 id 每次算出来的根一样（所以不能用 qHash —— Qt 6 那个每次进程启动换种子）");
    check(AsciiPathAlias::pathsFor(QStringLiteral("另一份 1.8 包")).root != alias.root,
          "ZM3 两个 id 的根不一样（不会互相盖掉）");
    check(alias.jarFor(QStringLiteral("x.jar")) == alias.jarsDir + QStringLiteral("/x.jar"),
          "ZM3 jarFor() 就是 jarsDir + 文件名");

    // --- ZM4. 真建一遍：联接通不通、硬链接是不是同一份 ----------------------
    const QString instanceDir = AppPaths::versionDir(unicodeId);
    const QString realJar = instanceDir + QLatin1Char('/') + unicodeId + QStringLiteral(".jar");
    const QString realNatives = instanceDir + QStringLiteral("/natives");
    writeFileAt(realJar, QByteArray());
    writeFileAt(realNatives + QStringLiteral("/lwjgl64.dll"), QByteArray("dll"));
    check(!AsciiPathAlias::isReady(alias),
          "ZM4 还没建 ⇒ isReady() 是 false（命令行据此发原来那套路径）");
    QString problem = AsciiPathAlias::ensure(alias, unicodeId, realNatives);
    check(problem.isEmpty(), "ZM4 ensure() 建成功", problem);
    check(AsciiPathAlias::isReady(alias), "ZM4 建完 ⇒ isReady() 是 true");

    // 联接算不算通：**从别名里写一个文件**，看它出不出现在实例目录里。
    writeFileAt(alias.gameDir + QStringLiteral("/saves/probe.txt"), QByteArray("别名这条路是通的"));
    check(QFileInfo(instanceDir + QStringLiteral("/saves/probe.txt")).isFile(),
          "ZM4 ★★ --gameDir 那个联接真的通向实例目录（从别名写进去，实例目录里看得见）");
    check(QFileInfo(alias.gameDir + QStringLiteral("/natives/lwjgl64.dll")).isFile(),
          "ZM4 反过来也成立（实例自己的文件从别名里看得见）");
    check(QFileInfo(alias.nativesDir + QStringLiteral("/lwjgl64.dll")).isFile(),
          "ZM4 natives 那个联接也通（-Djava.library.path 指的就是它）");

    // 硬链接：同一份数据 —— 往别名那一份写，真 jar 跟着变。
    const QString aliasJar = alias.jarFor(unicodeId + QStringLiteral(".jar"));
    writeFileAt(aliasJar, QByteArray("abc"));
    check(QFileInfo(realJar).size() == 3,
          "ZM4 ★★ jars/ 里那份是**硬链接**（往别名写，真 jar 跟着变 —— 不是拷贝）");
    check(QFile::remove(aliasJar) && QFileInfo(realJar).size() == 3,
          "ZM4 删掉别名那一份，真 jar 还在（硬链接只删目录项）");

    // --- ZM5. 幂等 + 自愈 ---------------------------------------------------
    check(AsciiPathAlias::ensure(alias, unicodeId, realNatives).isEmpty() &&
              QFileInfo(aliasJar).isFile(),
          "ZM5 别名的 jar 被删掉之后，再 ensure() 一次就补回来");
    check(AsciiPathAlias::ensure(alias, unicodeId, realNatives).isEmpty(),
          "ZM5 连做两次 ensure() 都成功（幂等；每次启动都会走这条路）");
    check(!QDir(alias.jarsDir)
               .entryList(QStringList() << QStringLiteral("*.jar"), QDir::Files)
               .isEmpty(),
          "ZM5 jars/ 里至少有一个 jar（isReady() 的判据之一）");
    // 每次启动都会走一遍 ensure()，所以"已经在的那一份"必须**原地留着**：建一套新的
    // （删掉再建）会让联接的创建时间每次都变，而且万一那一刻有别的进程正拿着它、
    // 删除还会失败。判据是创建时间不变 —— 这不是讲究：改坏了不会有任何症状，
    // 只会在真机上留下"每次启动都重建一遍"的额外动作。
    {
        const QDateTime gameBorn = QFileInfo(alias.gameDir).birthTime();
        const QDateTime jarsBorn = QFileInfo(alias.jarsDir).birthTime();
        if (gameBorn.isValid() && jarsBorn.isValid()) {
            check(AsciiPathAlias::ensure(alias, unicodeId, realNatives).isEmpty(),
                  "ZM5' 第三趟 ensure() 也成功");
            check(QFileInfo(alias.gameDir).birthTime() == gameBorn,
                  "★ ZM5' 已经在的那套**原地留着**（没有删掉重建：game 联接的创建时间不变）",
                  QFileInfo(alias.gameDir).birthTime().toString(Qt::ISODate));
            check(QFileInfo(alias.jarsDir).birthTime() == jarsBorn,
                  "★ ZM5' jars/ 那个目录也没被重建（硬链接那一层同样只补缺的）",
                  QFileInfo(alias.jarsDir).birthTime().toString(Qt::ISODate));
        }
    }

    // --- ZM6. 过期的那一套会被清掉（改过名的实例留下的） --------------------
    // 拿**产品自己的** ensure() 造一套"别实例"的别名，再把那个实例删掉 —— 于是那套的
    // game 联接就悬空了（真机上"实例改名/被删"就是这个形状）。
    const QString otherId = QStringLiteral("另一个实例");
    const QString otherDir = AppPaths::versionDir(otherId);
    writeFileAt(otherDir + QLatin1Char('/') + otherId + QStringLiteral(".jar"), QByteArray("j"));
    const AsciiPathAlias::Paths otherAlias = AsciiPathAlias::pathsFor(otherId);
    check(AsciiPathAlias::ensure(otherAlias, otherId, QString()).isEmpty(),
          "ZM6 先给另一个实例建一套别名");
    QDir(otherDir).removeRecursively();
    check(AsciiPathAlias::ensure(alias, unicodeId, realNatives).isEmpty() &&
              !QFileInfo(otherAlias.root).exists(),
          "ZM6 ★ 另一套指向已消失实例的别名被顺手清掉了（不然会一直攒着）");
    check(QFileInfo(alias.root).isDir(), "ZM6 清的时候没碰自己这一套");

    // --- ZM6'. ★ 反方向：**还活着**的那一套不许被当成过期的清掉（ 修的那条） ---
    //
    // 判据 `liveTargetOfAlias()` 不能只读 `QFileInfo::symLinkTarget()`。我们建的是
    // `mklink /J` 那种目录联接（IO_REPARSE_TAG_MOUNT_POINT），Qt 对它的 `isSymLink()`
    // 恒为 false、`symLinkTarget()` 恒为空（读目标要用 `junctionTarget()`）—— 只读前者
    // 判据就恒回空，每一趟 `ensure()` 都会把兄弟目录里**正在跑的那个实例**的别名当过期
    // 删掉。
    // 后果不是报错：那场游戏之后每写一个文件（存档 / 日志轮转 / 还没加载过的类）都失败。
    // 这里用"目标目录还在"当"还活着"的替身（产品侧那一条判据就是这么写的）。
    {
        const QString liveId = QStringLiteral("还在跑的实例");
        const QString liveDir = AppPaths::versionDir(liveId);
        writeFileAt(liveDir + QLatin1Char('/') + liveId + QStringLiteral(".jar"), QByteArray("k"));
        const AsciiPathAlias::Paths liveAlias = AsciiPathAlias::pathsFor(liveId);
        check(AsciiPathAlias::ensure(liveAlias, liveId, QString()).isEmpty(),
              "ZM6' 先给一个活着的实例建一套别名");
        check(AsciiPathAlias::ensure(alias, unicodeId, realNatives).isEmpty(),
              "ZM6' 再启动一次本实例（ensure() 会顺手扫一遍兄弟目录）");
        check(QFileInfo(liveAlias.root).isDir() && QFileInfo(liveAlias.gameDir).isDir() &&
                  QFileInfo(liveAlias.gameDir).isJunction(),
              "★★ ZM6' 目标还活着的那一套别名**原地留着**（扫过期的时候不许把它当过期删掉 ——"
              "删了正在跑的那场游戏之后每个写盘都要失败）",
              QStringLiteral("root 在 %1 / game 在 %2")
                  .arg(QFileInfo(liveAlias.root).isDir() ? 1 : 0)
                  .arg(QFileInfo(liveAlias.gameDir).isDir() ? 1 : 0));
        // junctionTarget() 读得出目标（symLinkTarget() 对目录联接读不出来）—— 这条钉的是
        // 清理目录联接所依赖的 Qt 行为，Qt 那边分类一变就得回来改。
        check(QDir::cleanPath(QFileInfo(liveAlias.gameDir).junctionTarget())
                      .compare(QDir::cleanPath(liveDir), Qt::CaseInsensitive) == 0,
              "★ ZM6' 目录联接的目标读得出来（`junctionTarget()`），而且是那个实例目录",
              QFileInfo(liveAlias.gameDir).junctionTarget());
    }

    // --- ZM6''. ★ 普通空目录不许当成"联接已经建好了"（ 修的那条） ---
    //
    // `createJunction()` 是"先 mkpath、再设 reparse 点"两步。中间那一步失败（杀软、权限、
    // 盘突然没了）留下的正是一个同名普通目录。`linkUsable()` 在"读不出目标名字"时
    // 不能只判"通不通"——普通目录当然通，于是这一套会被判成可用：`--gameDir` 指向缓存里
    // 的空壳，
    // 游戏把存档、截图、logs 全写进 `cache/ascii/<hash>/game`，实例目录里一份都没有，
    // 清一次缓存就全没了。natives 那一份同理（空的 java.library.path ⇒ UnsatisfiedLinkError）。
    {
        check(QDir().rmdir(alias.gameDir), "ZM6'' 先把 game 那个联接摘掉（前置）");
        check(QDir().mkpath(alias.gameDir),
              "ZM6'' 再在原位置建一个普通空目录（模拟 mklink 那一步失败留下的形状）");
        check(!QFileInfo(alias.gameDir).isJunction(),
              "ZM6'' 现在它确实是个普通目录（前置条件）");
        check(AsciiPathAlias::ensure(alias, unicodeId, realNatives).isEmpty(),
              "ZM6'' ensure() 在这一套上照样成功");
        check(QFileInfo(alias.gameDir).isJunction(),
              "★★ ZM6'' 普通目录被换**回**联接（不然存档会写进缓存目录、清缓存就没了）",
              QFileInfo(alias.gameDir).junctionTarget());
        writeFileAt(alias.gameDir + QStringLiteral("/saves/probe2.txt"), QByteArray("x"));
        check(QFileInfo(instanceDir + QStringLiteral("/saves/probe2.txt")).isFile(),
              "ZM6'' 重建之后从别名写进去的东西真的落在实例目录里");
    }

    // --- ZM7. 数据目录自己带非 ASCII 时：宁可直说，不建一套没用的 ----------
    const QString unicodeRoot = temp.path() + QStringLiteral("/我的游戏");
    AppPaths::setMinecraftDir(unicodeRoot);
    const AsciiPathAlias::Paths impossible = AsciiPathAlias::pathsFor(unicodeId);
    const QString refusal = AsciiPathAlias::ensure(impossible, unicodeId, QString());
    check(!refusal.isEmpty() && refusal.contains(QStringLiteral("非 ASCII")),
          "ZM7 ★ 数据目录自己不是 ASCII ⇒ ensure() 返回一句人话（要弹窗），不是静默建一套"
          "没用的别名", refusal);
    check(!QFileInfo(impossible.root).exists(),
          "ZM7 而且一个字节都没往盘上写");

    AppPaths::setMinecraftDir(originalDir);  // 收摊：后面那些节读的还是原来那一份
}

