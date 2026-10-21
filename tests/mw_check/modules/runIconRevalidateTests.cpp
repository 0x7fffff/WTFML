#include "Support.hpp"
void runIconRevalidateTests() {
    const QString originalDir = AppPaths::minecraftDir();
    QTemporaryDir root;
    check(root.isValid(), "ZB8 临时数据根目录建出来了");

    if (!ImageDecode::webpAvailable()) {
        std::printf("     （这台机器解不了 webp：地址不由测试定，这一节跳过）\n");
        AppPaths::setMinecraftDir(originalDir);
        return;
    }
    if (!root.isValid()) {
        AppPaths::setMinecraftDir(originalDir);
        return;
    }
    AppPaths::setMinecraftDir(root.path());
    QDir().mkpath(AppPaths::iconCacheDir());

    // 盘上那份：一张 32x32 的红图（**真的能解码** —— 解不动的话 requestIcon() 会把它当成
    // "盘上没有"，那就走到另一个分支去了，这一节就白验了）。
    const QByteArray redPng = solidPngBytes(QColor(0xC0, 0x20, 0x20));
    const QByteArray greenPng = solidPngBytes(QColor(0x20, 0xA0, 0x20));
    check(!redPng.isEmpty() && redPng != greenPng, "ZB8 两份字节造出来了（红 / 绿，能解码）");

    // 把盘上那份 meta 的**第四行**（上一次核对的时间）改掉 —— 那一行是「攒够 7 天才核一遍」
    // 的唯一判据（`IconValidators::checkedAtMs`）。要测「核过很久了」就得自己往回拨；
    // 不拨的话 `saveIconValidators()` 盖的是"刚才"，下面那几发连问都不问（`fake.requests`
    // 恒 0），红的理由看起来会跟被测代码毫无关系。
    const auto stampIconMeta = [](const QString &cacheId, qint64 checkedAtMs) {
        const QString path = ResourceStore::iconMetaPath(cacheId);
        QFile in(path);
        if (!in.open(QIODevice::ReadOnly)) {
            return;
        }
        QList<QByteArray> lines = in.readAll().split('\n');
        in.close();
        while (lines.size() < 4) {
            lines.append(QByteArray());
        }
        lines[3] = QByteArray::number(checkedAtMs);
        writeFileAt(path, lines.join('\n'));
    };

    // 每个用例一个 project_id、一份自己干净的盘上缓存（互相不串）。
    int caseNo = 0;
    const auto runCase = [&](int httpStatus, const QByteArray &served, const QByteArray &onDisk,
                             const QString &label) {
        ++caseNo;
        const QString id = QStringLiteral("ZB8case") + QString::number(caseNo);
        FakeIconServer fake;
        fake.status = httpStatus;
        fake.body = served;
        // 200 的时候带一条 ETag（这一发之后盘上那份凭据要从"旧"变成"新"）。
        fake.headers = QByteArrayLiteral("ETag: \"new-tag\"\r\nLast-Modified: Tue, 23 Sep 2026 "
                                         "04:11:07 GMT\r\n");
        const bool up = fake.start();
        check(up, QStringLiteral("ZB8 %1：本地假图标服务器起来了").arg(label));
        if (!up) {
            return;
        }

        // 这一格在被测代码那边的名字是**缓存键**（`Hit::iconCacheId()`，带来源前缀），
        // 不是裸 project_id —— 下面凡是碰盘上那份东西（图 / 凭据）的地方都得用它，
        // 用 `id` 的话摆下去的这份根本没被看见（症状是"凭据一条都没带出去"）。
        ResourceCatalog::Hit hit;
        hit.projectId = id;
        hit.source = ResourceCatalog::Source::Modrinth;
        const QString cacheId = hit.iconCacheId();
        hit.iconUrl = QStringLiteral("http://127.0.0.1:%1/icon_96.webp")
                          .arg(fake.server.serverPort());

        // 盘上先摆好那份 + 它的凭据（就是"上一次打开时存下来的"）。
        check(ResourceStore::saveIconToCache(cacheId, onDisk),
              QStringLiteral("ZB8 %1：盘上那份摆好了").arg(label));
        // 凭据是**跟着一条地址**的（meta 第三行）：这一节全程问的是同一条地址，盘上那条
        // 凭据就得按**同一条**记下来 —— 地址对不上时读出来是空的，这一发就成了"没有
        // 凭据"，条件头一个都发不出去（那几条断言红的理由会看起来毫无关系）。
        const QString iconUrl = hit.iconUrl;
        ResourceStore::IconValidators old;
        old.etag = QStringLiteral("\"old-tag\"");
        old.lastModified = QStringLiteral("Mon, 01 Sep 2026 00:00:00 GMT");
        ResourceStore::saveIconValidators(cacheId, old, iconUrl);
        // 这一份凭据是"很久以前核过的"（1998 年）⇒ 这一趟该核一遍。
        stampIconMeta(cacheId, 900000000000LL);

        // 一个**新的** store = 一趟新的运行（内存里没有它）。
        ResourceStore store;
        QList<QPixmap> arrived;
        QObject::connect(&store, &ResourceStore::iconReady, &store,
                         [&arrived](const QString &, const QPixmap &icon) {
                             arrived.append(icon);
                         });
        store.requestIcon(hit);
        // 等到"那一发真的到了假服务器上"（最多 5 秒，loopback 上不该等这么久），
        // 再转一拍让这一发的结果处理完 —— **不能**只等一个固定时长：固定等在慢一点的
        // 机器上会变成"请求还没到、断言先红"，而那种红跟被测代码毫无关系。
        QElapsedTimer wait;
        wait.start();
        while (fake.requests == 0 && wait.elapsed() < 5000) {
            QEventLoop tick;
            QTimer::singleShot(20, &tick, &QEventLoop::quit);
            tick.exec();
        }
        {
            QEventLoop settle;
            QTimer::singleShot(150, &settle, &QEventLoop::quit);
            settle.exec();
        }

        check(fake.requests == 1,
              QStringLiteral("ZB8 %1：这一趟就问了**一发**（一次运行只核一遍）").arg(label),
              QStringLiteral("问了 %1 次").arg(fake.requests));
        // 条件头真的带出去了：没有它就没有 304，只有整张。
        check(fake.lastIfNoneMatch == old.etag && fake.lastIfModifiedSince == old.lastModified,
              QStringLiteral("ZB8 %1：那一发带的就是盘上那份的凭据").arg(label),
              QStringLiteral("If-None-Match=%1").arg(fake.lastIfNoneMatch));

        QByteArray finalBytes;
        ResourceStore::loadIconFromCache(cacheId, &finalBytes);
        const ResourceStore::IconValidators finalValidators =
            ResourceStore::loadIconValidators(cacheId, iconUrl);
        if (httpStatus == HttpClient::kStatusNotModified) {
            // **没变**：盘上一个字节不动，界面上一格不动 —— 已经画上去的那张（"盘上那份先
            // 顶上"发的那一次）就是最终结果，**不能再发第二发**。
            check(arrived.size() == 1 && !arrived.at(0).isNull(),
                  QStringLiteral("ZB8 %1：只发了盘上那张（304 没有第二次 iconReady，"
                                 "不会把那一格擦掉）")
                      .arg(label),
                  QStringLiteral("发了 %1 次").arg(arrived.size()));
            check(finalBytes == onDisk, QStringLiteral("ZB8 %1：盘上那份一个字节都没动").arg(label));
            check(finalValidators.etag == old.etag &&
                      finalValidators.lastModified == old.lastModified,
                  QStringLiteral("ZB8 %1：凭据也没动（主机一句新的都没说，就不该凭空换）").arg(label),
                  finalValidators.etag);
        } else if (served == onDisk) {
            // 主机**不认**条件头：把整张又发了一遍，字节一模一样 = 没变。同上，不发第二发。
            check(arrived.size() == 1 && finalBytes == onDisk,
                  QStringLiteral("ZB8 %1：整张发回来了但字节一样 → 不写盘、不发第二发").arg(label),
                  QStringLiteral("发了 %1 次").arg(arrived.size()));
            // 但**凭据要更新**：这一发给了新的 ETag，不留下来下次就白问了。
            check(finalValidators.etag == QStringLiteral("\"new-tag\""),
                  QStringLiteral("ZB8 %1：凭据换成了这一发给的那条（下一趟才问得出 304）").arg(label),
                  finalValidators.etag);
        } else {
            // **作者换了图标**：写盘 + 再发一次，界面上那一格跟着换成新的。
            check(arrived.size() == 2,
                  QStringLiteral("ZB8 %1：换新图时发了**两次**（后发的那次＝新图）").arg(label),
                  QStringLiteral("发了 %1 次").arg(arrived.size()));
            check(finalBytes == served, QStringLiteral("ZB8 %1：盘上那份被换成了新图").arg(label));
            // 收到的第二张真的**是新那张**（像素级：不是"发了两次同一个"）。
            const bool isGreen = arrived.size() == 2 && !arrived.at(1).isNull() &&
                                 arrived.at(1).toImage().pixel(16, 16) ==
                                     QColor(0x20, 0xA0, 0x20).rgba();
            check(isGreen, QStringLiteral("ZB8 %1：第二张真的是新那张（绿的那张）").arg(label));
            check(finalValidators.etag == QStringLiteral("\"new-tag\""),
                  QStringLiteral("ZB8 %1：凭据跟着新那份字节一起换").arg(label));
        }
    };

    runCase(HttpClient::kStatusNotModified, QByteArray(), redPng, QStringLiteral("304 没变"));
    runCase(200, redPng, redPng, QStringLiteral("200 但一模一样"));
    runCase(200, greenPng, redPng, QStringLiteral("200 换了新图"));

    // （要求：「模组图标做本地缓存功能，不要每次都拉取」）：
    // `.meta` 第四行那个时间戳是**最近**的（就是 `saveIconValidators()` 刚盖的"现在"）
    // ⇒ 这一趟**一发都不许问**；盘上那张照旧要画出来（不问 ≠ 不显示）。
    // ⚠ 这一条和上面三个用例是一条规矩的两面：上面把时间戳拨到 1998 年 ⇒ 核；
    // 这里不动它 ⇒ 不核。少了任何一面，那次改动都可能被改成"永远核"或"永远不核"
    // 而全绿。
    {
        FakeIconServer fake;
        fake.status = 200;
        fake.body = greenPng;
        const bool up = fake.start();
        check(up, "ZB8 刚核过：本地假图标服务器起来了");
        if (up) {
            ResourceCatalog::Hit hit;
            hit.projectId = QStringLiteral("ZB8fresh");
            hit.source = ResourceCatalog::Source::Modrinth;
            const QString cacheId = hit.iconCacheId();
            hit.iconUrl = QStringLiteral("http://127.0.0.1:%1/icon_96.webp")
                              .arg(fake.server.serverPort());
            check(ResourceStore::saveIconToCache(cacheId, redPng),
                  "ZB8 刚核过：盘上那份摆好了");
            ResourceStore::IconValidators fresh;
            fresh.etag = QStringLiteral("\"old-tag\"");
            fresh.lastModified = QStringLiteral("Mon, 01 Sep 2026 00:00:00 GMT");
            ResourceStore::saveIconValidators(cacheId, fresh, hit.iconUrl);
            // **不拨时间** —— 就是那句 `saveIconValidators()` 刚盖上的"现在"。
            check(ResourceStore::loadIconValidators(cacheId, hit.iconUrl).checkedAtMs > 0,
                  "ZB8 刚核过：那一行时间戳真的是刚盖上的（不是 0）");

            ResourceStore store;
            QList<QPixmap> arrived;
            QObject::connect(&store, &ResourceStore::iconReady, &store,
                             [&arrived](const QString &, const QPixmap &icon) {
                                 arrived.append(icon);
                             });
            store.requestIcon(hit);
            // 这里**没有"等它到假服务器"那一趟**：这一版的正确行为就是永远不到。
            // 只等一个固定的宽限（够一发 loopback 来回还绰绰有余）。
            QEventLoop settle;
            QTimer::singleShot(300, &settle, &QEventLoop::quit);
            settle.exec();

            check(fake.requests == 0,
                  "ZB8 刚核过：这一趟**一发都不问**（7 天之内不再核对同一个图标）",
                  QStringLiteral("问了 %1 次").arg(fake.requests));
            check(arrived.size() == 1 && !arrived.at(0).isNull(),
                  "ZB8 刚核过：界面照旧拿到盘上那张（不问 ≠ 不显示）",
                  QStringLiteral("发了 %1 次").arg(arrived.size()));
            check(arrived.size() == 1 &&
                      arrived.at(0).toImage().pixel(16, 16) == QColor(0xC0, 0x20, 0x20).rgba(),
                  "ZB8 刚核过：画的就是盘上那张红图（不是服务器的绿图）");
            check(ResourceStore::loadIconValidators(cacheId, hit.iconUrl).etag == fresh.etag,
                  "ZB8 刚核过：那一条凭据也没被动过（没问就不该换）");
        }
    }

    AppPaths::setMinecraftDir(originalDir);
    check(AppPaths::minecraftDir() == originalDir, "ZB8 数据根目录换回来了");
}

