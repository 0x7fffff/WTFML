#include "Support.hpp"

void runRandomSkinTests(const QString &sandbox) {
    section("ZE. 离线账号的随机皮肤（挑地址 + 记盘）");

    // 下面这两个哈希是**真的**： 从 `api.mineskin.org/v2/skins?size=3` 拿回来的
    // 那三行原样（连重复的那一条也照抄 —— 它正好拿来钉去重）。自己编一串 `aaaa…` 证不了
    // "64 位十六进制"这个形态是**从真响应里**看出来的这件事。
    const QString hashA = QStringLiteral(
        "ba61d4e3a112c1959cfc5c7811ee442f055334ef787eee1795f9d947d56bfa39");
    const QString hashB = QStringLiteral(
        "a48f69d77cb323c5bbe0250143759cd7abff5e08774f7888556fa641cfa75f12");

    // ---- ZE1 列表地址（唯一出处：想换源就改这一处） ----
    {
        const QUrl list = RandomSkinStore::listRequestUrl(RandomSkinStore::defaultListSize());
        check(list.host() == QStringLiteral("api.mineskin.org") &&
                  list.path() == QStringLiteral("/v2/skins"),
              "ZE1 列表端点还是那一个（NameMC 那条路走不通，理由见 RandomSkinStore.hpp）",
              list.toString());
        check(list.query() == QStringLiteral("size=128"),
              "ZE1 默认要满一页（列表只有 next 游标、没有「跳到第 N 页」，「随机」的范围就是这一页）",
              list.query());
        check(RandomSkinStore::listRequestUrl(100000).query() == QStringLiteral("size=128"),
              "ZE1 要得比上限多 → 钳到 128（官方对超限回 400，不是给个短列表）",
              RandomSkinStore::listRequestUrl(100000).query());
        check(RandomSkinStore::listRequestUrl(0).query() == QStringLiteral("size=1") &&
                  RandomSkinStore::listRequestUrl(-7).query() == QStringLiteral("size=1"),
              "ZE1 要 0 条 / 负数 → 钳到 1（不拼出一个 size=0 的地址）");
    }

    // ---- ZE2 拼官方贴图地址（那个 host 只有一处知道） ----
    {
        const QString built = PlayerSkinStore::textureUrlForHash(hashA);
        check(built == QStringLiteral("https://textures.minecraft.net/texture/%1").arg(hashA),
              "ZE2 拼出来的就是官方 CDN 上那一张（挑它的是别人、装它的是这里）", built);
        // 关键的一条：拼出来的东西必须过 textureRequestUrl() —— 那是"这个地址算不算数"的
        // **唯一**判据。过不去的话，"挑出来的地址"和"能不能取"就成了两套规矩。
        check(PlayerSkinStore::textureRequestUrl(built).isValid(),
              "ZE2 拼出来的地址过得了 textureRequestUrl()（白名单只有一套）");

        // 拒掉的形态：长度不对 / 有大写 / 混了非十六进制 / 空串。**不悄悄转小写** ——
        // 这是外面来的东西，把一个大写串"修好"成合法地址，等于让调用方以为它拿到的
        // 就是它要的那一张。
        const QStringList rejects = {
            hashA.left(63),  // 短一位
            hashA + QStringLiteral("0"),  // 长一位
            hashA.toUpper(),  // 大写（官方一律小写）
            hashA.left(63) + QStringLiteral("z"),  // 非十六进制
            QStringLiteral("1a2b3c"),  // 老档案里那种短 hash
            QString(),
        };
        QString firstAccepted;
        for (const QString &bad : rejects) {
            if (!PlayerSkinStore::textureUrlForHash(bad).isEmpty() && firstAccepted.isEmpty()) {
                firstAccepted = bad;
            }
        }
        check(firstAccepted.isEmpty(),
              "ZE2 长度不对 / 大写 / 非十六进制 / 空串 → 一律拼不出地址", firstAccepted);
    }

    // ---- ZE3 解那一页（真响应 + 每一条坏在哪） ----
    {
        // 真响应的形状（`name` 真的是 null）。里面混了六条，只有两条该活下来：
        // ① hashA 真的、能用
        // ② hashA 再来一次 同一张图连着出现 —— **去重**
        // ③ hashB 真的、能用
        // ④ 短一位的 hash 形态不对 → 丢（留着它只会在挑中之后才发现是坏的）
        // ⑤ 大写 hash 同上
        // ⑥ texture 是 null 没有贴图 → 跳过（另一条还是能挑）
        const QByteArray body =
            QByteArrayLiteral(R"JSON({"success":true,"skins":[)JSON") +
            QByteArrayLiteral(R"JSON({"uuid":"c1db371c68fe4c02a4069aed4aa44bbe",)JSON")
                .append(R"JSON("shortId":"055516fe","name":null,"texture":")JSON")
                .append(hashA.toUtf8())
                .append(R"JSON(","timestamp":1790216929364},)JSON")
                .append(R"JSON({"uuid":"dup","shortId":"x","name":null,"texture":")JSON")
                .append(hashA.toUtf8())
                .append(R"JSON(","timestamp":1790216929365},)JSON")
                .append(R"JSON({"uuid":"2cafb05fd3bb4639b2dff9cf7171eeea",)JSON")
                .append(R"JSON("shortId":"a8dd74ab","name":"某人","texture":")JSON")
                .append(hashB.toUtf8())
                .append(R"JSON(","timestamp":1790216919393},)JSON")
                .append(R"JSON({"uuid":"short","texture":")JSON")
                .append(hashA.left(63).toUtf8())
                .append(R"JSON("},)JSON")
                .append(R"JSON({"uuid":"upper","texture":")JSON")
                .append(hashA.toUpper().toUtf8())
                .append(R"JSON("},)JSON")
                .append(QByteArrayLiteral(R"JSON({"uuid":"notexture","texture":null}])JSON"))
                .append(QByteArrayLiteral("}"));
        QString error;
        const QStringList hashes = RandomSkinStore::parseTextureHashes(body, &error);
        check(hashes == QStringList{hashA, hashB},
              "ZE3 六条里只活下来两条：去重 + 形态不对的丢掉 + 没有贴图的跳过",
              hashes.join(QStringLiteral(", ")));
        check(error.isEmpty(), "ZE3 解得出来就不该有那句原因", error);

        // 解出来的每一条都拼得出地址 —— 和 ZE2 串起来，这条是"挑中的一定能取"的前提。
        QString unpickable;
        for (const QString &hash : hashes) {
            if (PlayerSkinStore::textureUrlForHash(hash).isEmpty()) {
                unpickable = hash;
            }
        }
        check(unpickable.isEmpty(), "ZE3 解出来的每一条都拼得出官方地址", unpickable);

        // 三种解不出来的情况，各有一句**不同**的原因（"接口变了"和"这个库现在空的"
        // 是两回事，混成一句就没法查）。
        QString e1, e2, e3;
        check(RandomSkinStore::parseTextureHashes(QByteArrayLiteral("<html>Just a moment...</html>"),
                                                  &e1)
                      .isEmpty() &&
                  !e1.isEmpty(),
              "ZE3 不是 JSON（比如代理塞了一张 Cloudflare 的拦截页）→ 空 + 一句原因", e1);
        check(RandomSkinStore::parseTextureHashes(
                  QByteArrayLiteral(R"JSON({"success":true,"nope":[]})JSON"), &e2)
                      .isEmpty() &&
                  !e2.isEmpty(),
              "ZE3 是 JSON 但没有 skins 这一项（接口形状变了？）→ 空 + 一句原因", e2);
        check(RandomSkinStore::parseTextureHashes(
                  QByteArrayLiteral(R"JSON({"success":true,"skins":[]})JSON"), &e3)
                      .isEmpty() &&
                  !e3.isEmpty(),
              "ZE3 skins 是空的（这次真的一条都没有）→ 空 + 一句原因", e3);
        check(e1 != e2 && e2 != e3 && e1 != e3,
              "ZE3 那三种情况说的是三句不同的话（不然查起来分不出是哪种）");
    }

    // ---- ZE4 挑下标（掷骰子和取值拆开，取值这半每条边界都能钉） ----
    {
        const QStringList hashes{hashA, hashB};
        check(RandomSkinStore::pickIndex(hashes, 0) == hashA, "ZE4 第 0 张");
        check(RandomSkinStore::pickIndex(hashes, 1) == hashB, "ZE4 最后一张");
        check(RandomSkinStore::pickIndex(hashes, 2).isEmpty() &&
                  RandomSkinStore::pickIndex(hashes, -1).isEmpty(),
              "ZE4 越界（多了 / 负数）→ 空串，走失败那条路，不去读一个不存在的下标");
        check(RandomSkinStore::pickIndex(QStringList(), 0).isEmpty(),
              "ZE4 空列表 → 空串");

        // 骰子那半：**不**断言"两次一定不一样"（那是在赌运气），只断言它真的在动。
        // 128 个桶里抽 64 次，全都落在**同一个**桶的概率是 (1/128)^63 ≈ 0 —— 这条钉的是
        // "bounded() 被写成 bounded(1)、或者干脆硬编成第一张"那类改法。
        QStringList many;
        for (int i = 0; i < 128; ++i) {
            many.append(QStringLiteral("h%1").arg(i));
        }
        QSet<QString> picked;
        bool inRange = true;
        for (int i = 0; i < 64; ++i) {
            const QString one = RandomSkinStore::pickIndex(
                many, QRandomGenerator::global()->bounded(many.size()));
            if (one.isEmpty()) {
                inRange = false;
                break;
            }
            picked.insert(one);
        }
        check(inRange, "ZE4 掷 64 次骰子，每一次都落在 [0, size) 里（没有一次空手而归）");
        check(picked.size() > 1,
              "ZE4 那 64 次挑出来**不止一张**（钉的是「骰子真的在动」，不是在赌两次不一样）",
              QString::number(picked.size()));
    }

    // ---- ZE5 盘上那一行（往返 + 坏件） ----
    {
        // 离线账号的 uuid 形态和正版一样（md5("OfflinePlayer:" + 名字)，见 AccountStore），
        // 所以这里拿一个正的 uuid 就够。
        const QString uuid = QStringLiteral("1f2e3d4c-5b6a-7988-9a0b-1c2d3e4f5a6b");
        const QString url = PlayerSkinStore::textureUrlForHash(hashA);

        check(RandomSkinStore::chosenPathFor(QStringLiteral("不是 uuid")).isEmpty(),
              "ZE5 uuid 认不出来 → 路径是空串（不拼一个野路径）");
        check(RandomSkinStore::loadChosenSkinUrl(uuid).isEmpty(),
              "ZE5 还没挑过 → 空串（**常态**：第一次用这个账号，不是错误）");

        const QString path = RandomSkinStore::chosenPathFor(uuid);
        check(path.endsWith(QStringLiteral("/random-1f2e3d4c5b6a79889a0b1c2d3e4f5a6b.txt")),
              "ZE5 记在 <skinsDir>/random-<32 位>.txt", QDir::toNativeSeparators(path));
        check(QDir::cleanPath(path).startsWith(QDir::cleanPath(sandbox) + QLatin1Char('/')),
              "ZE5 落在当前游戏目录下面（换目录各记一份）",
              QDir::toNativeSeparators(path));

        QString error;
        check(RandomSkinStore::saveChosenSkinUrl(uuid, url, &error),
              "ZE5 记下来（目录不存在也要自己建出来）", error);
        check(RandomSkinStore::loadChosenSkinUrl(uuid) == url, "ZE5 读回来就是那一张");

        // 盘上那份**也算外面来的**（文件是可以手改的），所以读的时候要过同一把尺子。
        const QList<QPair<QString, QString>> broken = {
            {QStringLiteral("https://evil.example.com/texture/x"),
             QStringLiteral("别人家的 host")},
            {QStringLiteral("file:///C:/Windows/win.ini"), QStringLiteral("不是 http(s)")},
            {QString(), QStringLiteral("空文件")},
            {QStringLiteral("   \n  "), QStringLiteral("只有空白")},
            {QStringLiteral("随便写的一句话"), QStringLiteral("一句废话")},
            {QStringLiteral(R"({"skin":"x"})"), QStringLiteral("一段 JSON")},
        };
        QString firstAccepted;
        for (const auto &one : broken) {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                firstAccepted = QStringLiteral("写不进 %1").arg(one.second);
                break;
            }
            file.write(one.first.toUtf8());
            file.close();
            if (!RandomSkinStore::loadChosenSkinUrl(uuid).isEmpty() && firstAccepted.isEmpty()) {
                firstAccepted = one.second;
            }
        }
        check(firstAccepted.isEmpty(),
              "ZE5 手改成别人家的 host / 别的协议 / 空白 / 废话 → 一律当没挑过（读侧重新挑一张）",
              firstAccepted);

        // 官方那个地址写成 http:// 时读回来是**原样那一行**：升级只发生在真发请求那一刻
        // （见 PlayerSkinStore::textureRequestUrl 的 NOTE）—— 这里要是顺手改了它，
        // 缓存路径和 textureReady() 发出去的 url 就对不上了，调用方认不出贴图。
        const QString httpUrl = QStringLiteral("http://textures.minecraft.net/texture/") + hashA;
        {
            QFile file(path);
            check(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
                  "ZE5 前提：那个文件写得进去");
            file.write(httpUrl.toUtf8());
            file.close();
        }
        check(RandomSkinStore::loadChosenSkinUrl(uuid) == httpUrl,
              "ZE5 盘上是 http:// 的官方地址 → 原样读回来，**不在这里升级**",
              RandomSkinStore::loadChosenSkinUrl(uuid));

        // 收拾自己写的东西。
        QFile::remove(path);
        check(RandomSkinStore::loadChosenSkinUrl(uuid).isEmpty(), "ZE5 删掉之后又回到「还没挑过」");
    }
}

