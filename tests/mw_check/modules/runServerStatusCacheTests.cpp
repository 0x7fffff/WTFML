#include "Support.hpp"
void runServerStatusCacheTests() {
    const QString originalDir = AppPaths::minecraftDir();
    QTemporaryDir root;
    check(root.isValid(), "T23.0 临时数据根目录建出来了");
    if (!root.isValid()) {
        return;
    }
    AppPaths::setMinecraftDir(root.path());

    const QString address = QStringLiteral("mc.example.com:25566");
    const QString other = QStringLiteral("127.0.0.1:25565");

    // ---------- ① 文件在哪儿 ----------
    const QString path = ServerStatusCache::cachePath(address);
    check(!path.isEmpty() && path.startsWith(AppPaths::serverCacheDir() + QLatin1Char('/')),
          "★ T23.1 一台的缓存落在 <数据根>/cache/servers/ 下（整个启动器连带数据可以整体"
          "拷走 —— 和 cache/icons 同一条规矩，不挂 %APPDATA%）", path);
    check(path.endsWith(QStringLiteral(".json")) &&
              QFileInfo(path).fileName() != QFileInfo(ServerStatusCache::cachePath(other)).fileName(),
          "★ T23.1 两台各一个文件（一个文件装全部的话，两台机器同时开就互相盖），"
          "文件名是地址的 sha1（地址里有 `:` `/`，那些字符能不能进文件名要按平台问）",
          QFileInfo(path).fileName());
    check(ServerStatusCache::cachePath(QString()).isEmpty() &&
              ServerStatusCache::cachePath(QStringLiteral("   ")).isEmpty(),
          "★ T23.1 地址是空串（或者一片空白）时没有缓存文件 —— 不是拼出一个"
          "'空地址'的文件名把所有没地址的都塞在一起");
    check(ServerStatusCache::cachePath(QStringLiteral("  mc.example.com:25566  ")) == path,
          "T23.1 两头带空白的地址算同一台（用户在 servers.dat 里多半是复制粘贴进来的）");

    // ---------- ② 没缓存过 = 当没有，而且**不建文件** ----------
    check(!QFileInfo::exists(path), "T23.2 前提：这一台还没缓存过");
    const ServerStatusCache::Entry missing = ServerStatusCache::load(address);
    check(!missing.valid && missing.motdRaw.isEmpty() && missing.iconPng.isEmpty() &&
              missing.latencyMs == -1,
          "★ T23.2 没缓存过时给的是**空条目**（不是一条默认值 —— 那些默认值会在屏幕上"
              "显示成「这一台上次测过、一切正常」）",
          QStringLiteral("valid=%1 latency=%2").arg(missing.valid).arg(missing.latencyMs));
    check(!QFileInfo::exists(path),
          "★ T23.2 而读这一趟**一个字节都不写**（「读不动就不读」不该顺手在用户盘上"
          "留个文件）");

    // ---------- ③ 存下去再读回来：每一格都不许走样 ----------
    ServerStatusCache::Entry saved;
    saved.valid = true;
    // MOTD 带 `§` 码、带换行、带中文 —— 都是"存不住就走样"的东西。
    saved.motdRaw = QStringLiteral("§a欢迎来到 §l本地服§r\n§7第二行 §c红");
    saved.iconPng = QByteArrayLiteral("\x89PNG\r\n\x1a\n") + QByteArray(64, '\x00') +
                    QByteArrayLiteral("\xff\xfe\x00二进制");
    saved.online = true;
    saved.latencyMs = 42;
    saved.playersOnline = 7;
    saved.playersMax = 20;
    ServerStatusCache::save(address, saved);

    check(QFileInfo::exists(path), "★ T23.3 存下去了（连目录一起建出来 —— 第一次测速时"
                                   "cache/servers 多半还不存在）");
    const ServerStatusCache::Entry back = ServerStatusCache::load(address);
    check(back.valid, "★ T23.3 读得回来（valid 立着）");
    check(back.motdRaw == saved.motdRaw,
          "★★ T23.3 MOTD **一个字都不差**（`§` 码和换行都在 —— 少了哪一样，那一格就"
          "只剩一行灰字）",
          back.motdRaw);
    check(back.iconPng == saved.iconPng,
          "★ T23.3 图标那串**原始 PNG 字节**也一字节不差（它是 base64 过去的 —— "
          "按文本存回去的话 `\\xff` 那种字节会被换掉，图上会出现一块坏色）",
          QStringLiteral("%1 字节 → %2 字节")
              .arg(saved.iconPng.size())
              .arg(back.iconPng.size()));
    check(back.online && back.latencyMs == 42 && back.playersOnline == 7 && back.playersMax == 20,
          "★ T23.3 通没通 / 延迟 / 人数三样都在",
          QStringLiteral("online=%1 latency=%2 %3/%4")
              .arg(back.online)
              .arg(back.latencyMs)
              .arg(back.playersOnline)
              .arg(back.playersMax));

    // ---------- ④ `-1` 那个哨兵（见这一节的文件头） ----------
    ServerStatusCache::Entry unknown;
    unknown.valid = true;
    unknown.online = true;
    unknown.latencyMs = 42;
    unknown.playersOnline = -1;
    unknown.playersMax = -1;
    ServerStatusCache::save(other, unknown);
    const ServerStatusCache::Entry unknownBack = ServerStatusCache::load(other);
    check(unknownBack.valid && unknownBack.playersOnline == -1 && unknownBack.playersMax == -1,
          "★★ T23.4 「服务器没说人数」存回来还是 **-1**（不是 0）—— 那一列按 -1 才不发"
          "人数那一截；变成 0 的话，这台会在屏幕上写成「0/0 人」，"
          "那是一句**假话**而且看着完全正常",
          QStringLiteral("%1/%2").arg(unknownBack.playersOnline).arg(unknownBack.playersMax));
    check(unknownBack.latencyMs == 42 && unknownBack.motdRaw.isEmpty() &&
              unknownBack.iconPng.isEmpty(),
          "★ T23.4 而「没给图标 / MOTD」存回来是**空**（不是上一次那台的 —— 两台各有各的文件）",
          QStringLiteral("motd=%1 icon=%2 字节")
              .arg(unknownBack.motdRaw.size())
              .arg(unknownBack.iconPng.size()));

    // ---------- ⑤ 重测一次：上一次那份**整条**被盖掉 ----------
    ServerStatusCache::Entry again;
    again.valid = true;
    again.online = false;
    again.motdRaw = QStringLiteral("§c挂了的服");
    ServerStatusCache::save(address, again);
    const ServerStatusCache::Entry againBack = ServerStatusCache::load(address);
    check(againBack.valid && !againBack.online &&
              againBack.motdRaw == QStringLiteral("§c挂了的服") && againBack.iconPng.isEmpty() &&
              againBack.latencyMs == -1 && againBack.playersOnline == -1,
          "★ T23.5 第二次测的结果**整条盖掉**上一次（不许留上一次的人数/图标 —— 那些是"
          "上一台那时候的样子，而这一格写着的东西用户是会当真的）",
          QStringLiteral("motd=%1 icon=%2 字节 latency=%3")
              .arg(againBack.motdRaw)
              .arg(againBack.iconPng.size())
              .arg(againBack.latencyMs));

    // ---------- ⑤b 「上次是超时」这一位（ 补）----------
    //
    // 要求：「服务器那个会显示上次:没连上，也改成超时」，稍后又「‘上次’两个字也不要」。
    // 界面上那一格里"超时"和"没连上"是**两件事**（同一台服务器换个端口就从前者变成后者），
    // 而且现在连前缀都没有了 ⇒ 这一位必须**落盘**：不存的话，下次开这一页只能写「没连上」。
    ServerStatusCache::Entry timedOutEntry;
    timedOutEntry.valid = true;
    timedOutEntry.online = false;
    timedOutEntry.timedOut = true;
    ServerStatusCache::save(other, timedOutEntry);
    const ServerStatusCache::Entry timedOutBack = ServerStatusCache::load(other);
    check(timedOutBack.valid && !timedOutBack.online && timedOutBack.timedOut,
          "★★ T23.5b 「上次是超时」这一位存得住（盘上那一份也带着它）",
          QStringLiteral("online=%1 timedOut=%2")
              .arg(timedOutBack.online)
              .arg(timedOutBack.timedOut));
    // 老缓存（我们还没有这一位的时候写下去的那一份）里没有这个键 ⇒ 读出来是 false ⇒
    // 那一格写「没连上」。**这不是漏了一句**：那一份确实不知道上一次是怎么没通的，
    // 凭空说它超时才是错的。
    writeFileAt(path, QByteArrayLiteral("{\"online\":false,\"latencyMs\":-1,"
                                        "\"playersOnline\":-1,\"playersMax\":-1}"));
    const ServerStatusCache::Entry legacy = ServerStatusCache::load(address);
    check(legacy.valid && !legacy.online && !legacy.timedOut,
          "★ T23.5b 老缓存（没有这一位的那一份）读出来是 false —— 那一格写"
          "「没连上」，不是替它编一句「超时」");

    // ---------- ⑥ 写坏了（断电、手改）当没有，而且**不删** ----------
    writeFileAt(path, QByteArrayLiteral("{ 这不是 JSON"));
    const ServerStatusCache::Entry broken = ServerStatusCache::load(address);
    check(!broken.valid && broken.motdRaw.isEmpty(),
          "★ T23.6 盘上那份读不动时当**没有**（不是读成半条 —— 半条会把"
          "「上一次的样子」显示成一些零零碎碎的值）");
    check(QFileInfo::exists(path),
          "★ T23.6 而且**不删它**：读这一路上一个字节都不写（删文件是验证工程和用户的事）");
    writeFileAt(path, QByteArrayLiteral("[1,2,3]"));
    check(!ServerStatusCache::load(address).valid,
          "★ T23.6 合法 JSON 但不是对象也当没有（`[1,2,3]` 过得了 JSON 解析那一关）");
    ServerStatusCache::save(address, saved);
    check(ServerStatusCache::load(address).valid &&
              ServerStatusCache::load(address).motdRaw == saved.motdRaw,
          "T23.6 坏文件**盖得回去**（写那一头是 Truncate，不是往后接）");

    AppPaths::setMinecraftDir(originalDir);
    check(AppPaths::minecraftDir() == originalDir, "T23.9 数据根目录换回来了");
}

