#include "Support.hpp"
void runInstanceIconLookupTests() {
    const QString originalDir = AppPaths::minecraftDir();
    QTemporaryDir root;
    check(root.isValid(), "T22.0 临时数据根目录建出来了");
    if (!root.isValid()) {
        return;
    }

    FakeLookupApi api;
    check(api.start(), "T22.0 假接口服务器起来了");

    const QString workDir = root.path() + QStringLiteral("/fake");
    QDir().mkpath(workDir);
    AppPaths::setMinecraftDir(root.path());

    // ---------- 夹具：三个**内容不同**的文件 ----------
    //
    // 内容是什么不重要，重要的是三个文件算出**三个不同的哈希**（哈希一样的话"对回哪个
    // 路径"这一条就验不出来了）。
    const QString modJar = workDir + QStringLiteral("/sodium.jar");
    const QString offJar = workDir + QStringLiteral("/old-mod.jar.disabled");
    const QString resZip = workDir + QStringLiteral("/材质.zip");
    writeFileAt(modJar, QByteArray(4096, 'A'));
    writeFileAt(offJar, QByteArray(1024, 'B'));
    writeFileAt(resZip, QByteArray(2048, 'C'));

    // 期望值用**产品自己那份实现**算（`ModpackExport::hashFile`）——这一节验的不是
    // "哈希算法对不对"（V5 段钉着它），而是"这条路有没有把它算对、又有没有送对"。
    ModpackExport::FileHashes hMod;
    ModpackExport::FileHashes hOff;
    ModpackExport::FileHashes hRes;
    QString hashError;
    const bool hashed =
        ModpackExport::hashFile(modJar, true, &hMod, &hashError) &&
        ModpackExport::hashFile(offJar, true, &hOff, &hashError) &&
        ModpackExport::hashFile(resZip, true, &hRes, &hashError);
    check(hashed && hMod.fingerprintOk && hRes.fingerprintOk,
          "T22.0 三个夹具文件的哈希算出来了（sha1 + 指纹）", hashError);
    if (!hashed) {
        return;
    }
    check(hMod.sha1 != hOff.sha1 && hMod.sha1 != hRes.sha1 && hOff.sha1 != hRes.sha1,
          "T22.0 三个文件的 sha1 互不相同（不然「对回哪个路径」这一条验不出来）");

    // ---------- 假答复：两个来源各一半 ----------
    //
    // 前两个 jar 走 Modrinth（那个接口只给"哪一版"，**不给图标地址** —— 兜底那条由
    // `Hit::iconPngUrl()` 自己按项目号拼），那个 zip 走 CurseForge（那边两条地址都在
    // `logo` 里）。
    api.modrinthReply = QByteArrayLiteral("{") + QByteArrayLiteral("\"") + hMod.sha1.toUtf8() +
                        QByteArrayLiteral("\":{\"project_id\":\"AANobbMI\","
                                          "\"version_number\":\"mc1.21.1-0.6.13\","
                                          "\"files\":[{\"hashes\":{\"sha1\":\"") +
                        hMod.sha1.toUtf8() +
                        QByteArrayLiteral("\",\"sha512\":\"deadbeef\"},"
                                          "\"url\":\"https://cdn.modrinth.com/data/sodium.jar\","
                                          "\"filename\":\"sodium.jar\",\"size\":4096}]},") +
                        QByteArrayLiteral("\"") + hOff.sha1.toUtf8() +
                        QByteArrayLiteral("\":{\"project_id\":\"P7dR8mSH\","
                                          "\"version_number\":\"1.0.0\","
                                          "\"files\":[{\"hashes\":{\"sha1\":\"") +
                        hOff.sha1.toUtf8() +
                        QByteArrayLiteral("\",\"sha512\":\"cafebabe\"},"
                                          "\"url\":\"https://cdn.modrinth.com/data/old.jar\","
                                          "\"filename\":\"old-mod.jar\",\"size\":1024}]}}");
    api.curseForgeReply =
        QByteArrayLiteral("{\"data\":{\"exactMatches\":[{\"id\":12345,\"file\":{"
                          "\"id\":12345,\"modId\":238222,\"displayName\":\"JEI\","
                          "\"fileName\":\"jei.jar\",\"fileFingerprint\":") +
        QByteArray::number(hRes.fingerprint) +
        QByteArrayLiteral(",\"fileLength\":2048,\"isAvailable\":true,\"downloadUrl\":null,"
                          "\"hashes\":[]}}],\"unmatchedFingerprints\":[]}}");
    api.curseForgeModsReply =
        QByteArrayLiteral("{\"data\":[{\"id\":238222,\"name\":\"Just Enough Items\","
                          "\"slug\":\"jei\",\"summary\":\"看配方用的\","
                          "\"downloadCount\":1234567,\"dateModified\":\"2026-09-01T00:00:00Z\","
                          "\"latestFilesIndexes\":[{\"gameVersion\":\"1.21.1\",\"modLoader\":4}],"
                          "\"logo\":{"
                          "\"thumbnailUrl\":\"https://media.forgecdn.net/avatars/thumbnails/"
                          "238/222/256/256.jpeg\","
                          "\"url\":\"https://media.forgecdn.net/avatars/238/222/t.png\"}}]}");
    // Modrinth 那一支**还要多问一发**才拿得到图标地址（`GET /v2/projects?ids=…`，答复是
    // 一个数组）。这一条正是那个 bug 的落点：`<id>/icon.png` 那条自己拼的
    // 地址只是**老项目的别名**，回 404 的项目一个图标都画不出来。
    //
    // 这一份回复里**本来就有**名字 / slug / 简介 / 支持的游戏版本，
    // 「点名字进它自己的那一页」要的就是那几样（见 `Found::hit`），所以下面几条断言
    // 盯它们 —— 它们和图标走的是**同一份字节**，少读一栏的症状不是"没反应"而是那一页
    // 顶着一个空标题。
    api.modrinthProjectsReply =
        QByteArrayLiteral("[{\"id\":\"AANobbMI\",\"icon_url\":\"https://cdn.modrinth.com/data/"
                          "AANobbMI/295862f4724dc3f78df3447ad6072b2dcd3ef0c9_96.webp\","
                          "\"slug\":\"sodium\",\"title\":\"Sodium\","
                          "\"description\":\"渲染优化\",\"downloads\":40000000,"
                          "\"updated\":\"2026-08-20T00:00:00Z\","
                          "\"categories\":[\"optimization\"],"
                          "\"game_versions\":[\"1.21.1\"],\"loaders\":[\"fabric\"]},"
                          "{\"id\":\"P7dR8mSH\",\"icon_url\":\"https://cdn.modrinth.com/data/"
                          "P7dR8mSH/icon.png\","
                          "\"slug\":\"fabric-api\",\"title\":\"Fabric API\","
                          "\"description\":\"基础库\",\"game_versions\":[\"1.21.1\"],"
                          "\"loaders\":[\"fabric\"]}]");

    // ---------- ① 「哪些文件值得拿去问」 ----------
    //
    // 判据错了的症状是**白等几发请求**（拿一个 .litematic 去问 Modrinth，它当然不认），
    // 而不是报错 —— 所以这几条一眼看得出来的断言值钱。
    check(InstanceIconLookup::isLookupCandidate(modJar) &&
              InstanceIconLookup::isLookupCandidate(offJar) &&
              InstanceIconLookup::isLookupCandidate(resZip),
          "★ T22.1 .jar / .zip / `.jar.disabled` 都值得问（禁用着的那一份内容还是那个 jar）");
    check(!InstanceIconLookup::isLookupCandidate(workDir + QStringLiteral("/房子.litematic")) &&
              !InstanceIconLookup::isLookupCandidate(workDir + QStringLiteral("/sodium.jar.txt")) &&
              !InstanceIconLookup::isLookupCandidate(workDir + QStringLiteral("/mods")),
          "★ T22.1 原理图 / 改了扩展名的 / 目录都不问（问了也是白发一发）");
    check(InstanceIconLookup::isLookupCandidate(workDir + QStringLiteral("/SODIUM.JAR")),
          "T22.1 大写扩展名照样认（Windows 上那本来就是一回事）");

    // ---------- ② 真发一发 ----------
    InstanceIconLookup::Options options;
    options.modrinthApiBaseOverride = api.modrinthBase();
    options.curseForgeApiBaseOverride = api.curseForgeBase();

    const QStringList asked = {modJar, offJar, resZip};
    // 这一发的请求从第几行开始（下面 ④b 量"这一趟问了什么"时只看这一段 —— 后面还有
    // 好几趟 lookup，混进来就分不清是谁发的了）。
    const int beforeMain = api.requests.size();
    const InstanceIconLookup::Result result = InstanceIconLookup::lookup(asked, options);

    check(result.problem.isEmpty(), "★ T22.2 这一趟跑完了（三发都成）", result.problem);
    check(result.hashed == 3 && result.unreadable == 0,
          "T22.2 三个文件都算出来了",
          QStringLiteral("hashed=%1 unreadable=%2").arg(result.hashed).arg(result.unreadable));

    // 发出去的那两发正文：Modrinth 要的是 sha1 表（**字符串**）、CurseForge 要的是指纹表
    // （**数字**，见下），而且都得是**真算出来的那几个**。
    QSet<QString> sentSha1;
    QSet<QString> sentFingerprints;
    bool seenFingerprintValue = false;
    bool fingerprintsAllNumeric = true;
    bool sawModrinth = false;
    bool sawFingerprints = false;
    bool sawMods = false;
    for (int i = 0; i < api.requests.size(); ++i) {
        const QString line = api.requests.at(i);
        const QJsonObject body = QJsonDocument::fromJson(api.bodies.at(i)).object();
        if (line.contains(QStringLiteral("/version_files"))) {
            sawModrinth = true;
            for (const QJsonValue &value : body.value(QStringLiteral("hashes")).toArray()) {
                sentSha1.insert(value.toString());
            }
        } else if (line.contains(QStringLiteral("/fingerprints"))) {
            sawFingerprints = true;
            for (const QJsonValue &value : body.value(QStringLiteral("fingerprints")).toArray()) {
                seenFingerprintValue = true;
                fingerprintsAllNumeric = fingerprintsAllNumeric && value.isDouble();
                sentFingerprints.insert(QString::number(
                    static_cast<quint32>(value.toDouble())));
            }
        } else if (line.contains(QStringLiteral("/mods"))) {
            sawMods = true;
        }
    }
    check(sawModrinth && sawFingerprints && sawMods,
          "★ T22.2 三条路各问了一发（sha1 → Modrinth、指纹 → CurseForge、项目号 → logo）",
          QStringLiteral("requests=%1").arg(api.requests.join(QStringLiteral(" | "))));
    // 两个来源都收**全部三个**：这一层在算哈希之前并不知道哪个文件属于哪个来源
    // （`sodium.jar` 和 `材质.zip` 长得一样都是个压缩包），所以每个候选都两样都发一遍
    // —— 那边不认就不认，代价是一发里多带几个哈希，比"猜错了来源就永远认不出来"划算。
    check(sentSha1 == QSet<QString>({hMod.sha1, hOff.sha1, hRes.sha1}),
          "★★ T22.2 发给 Modrinth 的那三个 sha1 就是**这三个文件真算出来的那三个**"
          "（算错一个的症状是这一个文件永远只有占位图，而屏幕上一点异常都没有）",
          QStringList(sentSha1.values()).join(QStringLiteral(", ")));
    check(seenFingerprintValue && fingerprintsAllNumeric,
          "★★ T22.2 指纹是当**数学意义上的数字**发出去的（int64），不是字符串 —— 字符串那一支"
          "CurseForge 直接回 400（正文是 `invalid type: string …，expected i64`），而这一条路"
          "整个是静默的：字节看着没问题、只是那几格永远没有图标，唯一会说话的就是那 400");
    check(sentFingerprints == QSet<QString>({QString::number(hMod.fingerprint),
                                             QString::number(hOff.fingerprint),
                                             QString::number(hRes.fingerprint)}),
          "★★ T22.2 发给 CurseForge 的那三个指纹也是真算出来的"
          "（它是「剔除空白字节 + seed 1」那套 murmur2，不是 sha1 的前几位）",
          QStringList(sentFingerprints.values()).join(QStringLiteral(", ")));

    // ---------- ③ 结果对回了正确的路径 ----------
    auto foundFor = [&result](const QString &path) -> InstanceIconLookup::Found {
        for (const InstanceIconLookup::Found &found : result.found) {
            if (found.path == path) {
                return found;
            }
        }
        return InstanceIconLookup::Found();
    };
    const InstanceIconLookup::Found fMod = foundFor(modJar);
    const InstanceIconLookup::Found fOff = foundFor(offJar);
    const InstanceIconLookup::Found fRes = foundFor(resZip);

    check(result.found.size() == 3,
          "★ T22.3 三个文件都认出来了（一个一条，不是一哈希一条）",
          QStringLiteral("%1 条").arg(result.found.size()));
    check(fMod.hit.projectId == QStringLiteral("AANobbMI") &&
              fMod.hit.source == ResourceCatalog::Source::Modrinth,
          "★ T22.3 sodium.jar → 接口给的那个 Modrinth 项目号", fMod.hit.projectId);
    check(fOff.hit.projectId == QStringLiteral("P7dR8mSH") &&
              fOff.hit.source == ResourceCatalog::Source::Modrinth,
          "★ T22.3 禁用着的那一份**查得出来**，而且回的路径是**带 .disabled 的那个**"
          "（行上写的就是它；按去掉后缀那条路径回的话，界面上一条都换不上）",
          fOff.path);
    check(fRes.hit.projectId == QStringLiteral("238222") &&
              fRes.hit.source == ResourceCatalog::Source::CurseForge,
          "★ T22.3 材质.zip → CurseForge 那个项目号（**数字**那个来源）",
          fRes.hit.projectId);
    check(fRes.hit.iconUrl == QStringLiteral("https://media.forgecdn.net/avatars/thumbnails/"
                                             "238/222/256/256.jpeg") &&
              fRes.hit.iconFallbackUrl ==
                  QStringLiteral("https://media.forgecdn.net/avatars/238/222/t.png"),
          "★ T22.3 CurseForge 那两条图标地址（小的那条先试、原图那条兜底）—— "
          "少了它们，那一条就只能画占位图（那边拼不出地址来，见 Hit::iconPngUrl）",
          fRes.hit.iconUrl);

    check(fMod.hit.title == QStringLiteral("Sodium") &&
              fMod.hit.slug == QStringLiteral("sodium") &&
              fMod.hit.description == QStringLiteral("渲染优化") &&
              fMod.hit.gameVersions == QStringList({QStringLiteral("1.21.1")}),
          "★★ T22.3 Modrinth 那一条带着**整个项目对象**（名字 / slug / 简介 / 游戏版本）——"
          "`GET /projects?ids=` 的回复里本来就有，多读几栏、不多发一个请求",
          QStringLiteral("title=%1 slug=%2 gv=%3")
              .arg(fMod.hit.title, fMod.hit.slug,
                   fMod.hit.gameVersions.join(QStringLiteral(","))));
    check(fOff.hit.title == QStringLiteral("Fabric API"),
          "★ T22.3 同一个资源摆两份时两个路径上**都有名字**（两行都点得动）",
          fOff.hit.title);
    // CurseForge 那一份走的是 `/mods`（对象形状和 `/mods/search` **逐字同一套**，所以那一
    // 份解析只有一处实现）—— 名字在 `name` 里、简介在 `summary` 里、游戏版本要从
    // `latestFilesIndexes` 推。这三样读歪了的症状和上面那条一样：标题空着。
    check(fRes.hit.title == QStringLiteral("Just Enough Items") &&
              fRes.hit.slug == QStringLiteral("jei") &&
              fRes.hit.description == QStringLiteral("看配方用的") &&
              fRes.hit.gameVersions == QStringList({QStringLiteral("1.21.1")}),
          "★★ T22.3 CurseForge 那一条也是完整的一份（`name` / `slug` / `summary` / "
          "`latestFilesIndexes` ⇒ 游戏版本）—— 那一家的字段名和 Modrinth 一个都不一样",
          QStringLiteral("title=%1 slug=%2 gv=%3")
              .arg(fRes.hit.title, fRes.hit.slug,
                   fRes.hit.gameVersions.join(QStringLiteral(","))));
    // NOTE: Modrinth 那两条图标地址**不能**靠 `Hit::iconPngUrl()` 按项目号自己拼：
    // `cdn.modrinth.com/data/<id>/icon.png` 只是"图标当年就叫 icon.png"那批**老项目的
    // 别名**（拿用户真实 `mods/` 里 20 个模组一个一个要过，11 个回 404）。症状是一页
    // 模组里绝大多数只有占位拼图，而且**屏幕上一点异常都没有**（那一发 404 被当成
    // "这个项目没传过图标"，那本来是完全正常的一件事）。真正那条地址在带哈希的文件名
    // 底下，除了接口没人说得出来，所以要多问一发 `GET /v2/projects?ids=…`（见 T22.4b）。
    //
    // 兜底那一条（`iconFallbackUrl`）**照旧空着** —— 它仍然是 `Hit::iconPngUrl()` 的事，
    // 在这里也填一份就是第二处出处。
    check(fMod.hit.iconUrl == QStringLiteral("https://cdn.modrinth.com/data/AANobbMI/"
                                             "295862f4724dc3f78df3447ad6072b2dcd3ef0c9_96.webp") &&
              fOff.hit.iconUrl ==
                  QStringLiteral("https://cdn.modrinth.com/data/P7dR8mSH/icon.png") &&
              fMod.hit.iconFallbackUrl.isEmpty() && fOff.hit.iconFallbackUrl.isEmpty(),
          "★★ T22.3 Modrinth 那一条图标地址**是接口给的**（`icon_url`，各项目一个哈希，"
          "自己拼不出来）—— 这一格空着的症状是「这个模组只有占位拼图」，"
          "而且是**绝大多数**都空着",
          fMod.hit.iconUrl);

    // ---------- ④b 图标地址那一发本身（`GET /v2/projects?ids=…`）----------
    //
    // 三样都要对：**发的是 GET**（发成 POST 的话接口回 405，而屏幕上只是"没图"）、
    // 地址上带的是**刚认出来的那几个项目号**（不多不少）、同一个号**只出现一次**
    // （`pathsBySha1` 是"哈希 → 路径表"，见下面那一段）。
    QString projectsLine;
    for (int i = beforeMain; i < api.requests.size(); ++i) {
        const QString line = api.requests.at(i);
        if (line.startsWith(QStringLiteral("GET ")) &&
            line.contains(QStringLiteral("/projects?"))) {
            projectsLine = line;
        }
    }
    check(!projectsLine.isEmpty(),
          "★★ T22.4b 认出来之后**多问了一发**图标地址（少了它，这一页上绝大多数模组只有"
          "占位拼图 —— 那条自己拼的 `<id>/icon.png` 只是老项目的别名）",
          api.requests.join(QStringLiteral(" | ")));
    QSet<QString> askedProjects;
    bool projectsAskedAsGet = false;
    if (!projectsLine.isEmpty()) {
        const QUrl projectsUrl(projectsLine.split(QLatin1Char(' ')).value(1));
        projectsAskedAsGet = projectsUrl.path() == QStringLiteral("/v2/projects");
        const QJsonArray ids =
            // 必须显式给 `FullyDecoded`：`QUrlQuery::queryItemValue` 默认是
            // `PrettyDecoded`，而 `[` `"` `,` 这几个**不属于"好看"的那一批**，会被原样
            // 留着（实测拿到的是字面 `%5B%22AANobbMI%22…`）—— 拿去 `fromJson` 只会得到
            // 一个空数组，于是"发出去的号"永远看着是空的。
            QJsonDocument::fromJson(QUrlQuery(projectsUrl)
                                        .queryItemValue(QStringLiteral("ids"),
                                                        QUrl::FullyDecoded)
                                        .toUtf8())
                .array();
        for (const QJsonValue &value : ids) {
            askedProjects.insert(value.toString());
        }
    }
    check(projectsAskedAsGet && askedProjects == QSet<QString>({QStringLiteral("AANobbMI"),
                                                              QStringLiteral("P7dR8mSH")}),
          "★★ T22.4b 那一发问的**就是刚认出来的那两个项目号**（那个 zip 是 CurseForge 的，"
          "不该出现在这一发里——两个来源的号空间不同，问错了不报错、只是白问）",
          QStringList(askedProjects.values()).join(QStringLiteral(", ")));

    // 同一个资源摆了两份：同一个号问两遍就是白带一截地址（见上面那段）。
    const QString copyJar = workDir + QStringLiteral("/sodium-copy.jar");
    writeFileAt(copyJar, QByteArray(4096, 'A'));  // 和 modJar **逐字节一样**
    const int beforeCopy = api.requests.size();
    const InstanceIconLookup::Result copyResult =
        InstanceIconLookup::lookup({copyJar, modJar}, options);
    QSet<QString> copyAsked;
    int copyAskedCount = 0;
    for (int i = beforeCopy; i < api.requests.size(); ++i) {
        const QString line = api.requests.at(i);
        if (!line.startsWith(QStringLiteral("GET ")) ||
            !line.contains(QStringLiteral("/projects?"))) {
            continue;
        }
        const QUrl url(line.split(QLatin1Char(' ')).value(1));
        const QJsonArray ids =
            QJsonDocument::fromJson(
                QUrlQuery(url).queryItemValue(QStringLiteral("ids"), QUrl::FullyDecoded).toUtf8())
                .array();
        copyAskedCount += ids.size();
        for (const QJsonValue &value : ids) {
            copyAsked.insert(value.toString());
        }
    }
    check(copyResult.found.size() == 2 && copyAskedCount == 1 &&
              copyAsked == QSet<QString>({QStringLiteral("AANobbMI")}),
          "★ T22.4b 同一个资源摆了两份（内容逐字节一样）时：两个路径**都认出来**了，而项目号"
          "**只问一次**（两份 → 两个路径 → 同一个哈希 → 同一个号）",
          QStringLiteral("found=%1 问过的号共 %2 个：%3")
              .arg(copyResult.found.size())
              .arg(copyAskedCount)
              .arg(QStringList(copyAsked.values()).join(QStringLiteral(", "))));

    // ---------- ④ 认不出来的那些 ----------
    //
    // "没认出来" = 不在 found 里（不是"给了一条空的"）：接的人按"这一批里没有它"记负缓存。
    const QString unknownJar = workDir + QStringLiteral("/unknown.jar");
    writeFileAt(unknownJar, QByteArray(512, 'Z'));
    const InstanceIconLookup::Result miss =
        InstanceIconLookup::lookup({unknownJar}, options);
    check(miss.problem.isEmpty() && miss.found.isEmpty() && miss.hashed == 1,
          "★ T22.4 接口不认的那个文件：不报错、也不给一条空的（就是「这一批里没有它」）",
          QStringLiteral("found=%1 problem=%2").arg(miss.found.size()).arg(miss.problem));

    const QString ghost = workDir + QStringLiteral("/不存在的.jar");
    const InstanceIconLookup::Result ghostResult =
        InstanceIconLookup::lookup({ghost, modJar}, options);
    check(ghostResult.unreadable == 1 && ghostResult.hashed == 1 &&
              ghostResult.found.size() == 1,
          "★ T22.4 盘上没有的那个文件只记一笔「读不动」，**不打断别的**"
          "（它不在那一批里，别的照旧认出来）",
          QStringLiteral("unreadable=%1 hashed=%2 found=%3")
              .arg(ghostResult.unreadable)
              .arg(ghostResult.hashed)
              .arg(ghostResult.found.size()));

    // ---------- ⑤ 接口挂了：problem 立起来，但**认出来的那些留着** ----------
    //
    // 这一条管的是负缓存：接的人按 problem 决定"没认出来的那些先别记成'问过了、没有'"。
    // 记了的后果是用户这一整个会话都再也看不到图标 —— 而那本来是"网好了就会有"。
    api.modrinthStatus = 500;
    const InstanceIconLookup::Result broken = InstanceIconLookup::lookup({modJar}, options);
    api.modrinthStatus = 200;
    check(!broken.problem.isEmpty() && broken.found.isEmpty(),
          "★★ T22.5 Modrinth 回 500 时：一趟没跑成（problem 非空）而且**不给结果**"
          "（半真半假的一趟会把负缓存写坏）",
          broken.problem);
    check(broken.problem.contains(QStringLiteral("500")),
          "T22.5 那句话里带着状态码（写给日志的那一句）", broken.problem);

    // 判据是"iconProblem 立起来、problem 照旧空"。两者混在一起的代价见
    // `Result::iconProblem`：写进 problem 会让接的人把**没认出来的那些**也留着不记负缓存，
    // 于是用户每开一次这一页就把几十兆 jar 从头重读重算一遍，换来的只是"图标可能来了"。
    api.modrinthProjectsStatus = 500;
    const InstanceIconLookup::Result noIcon = InstanceIconLookup::lookup({modJar}, options);
    api.modrinthProjectsStatus = 200;
    check(noIcon.problem.isEmpty() && noIcon.iconProblem.contains(QStringLiteral("500")),
          "★★ T22.5 图标地址那一发回 500 时：**problem 照旧空着**（这一趟是完整的 —— 两家"
          "的回复都问完了）、那句话落在 iconProblem 里",
          QStringLiteral("problem=%1 iconProblem=%2")
              .arg(noIcon.problem, noIcon.iconProblem));
    check(noIcon.found.size() == 1 && noIcon.found.first().hit.iconUrl.isEmpty(),
          "★ T22.5 而**身份照样认出来了**（图标那一格退回自己拼的兜底地址、画占位拼图 ——"
          "「认出来了但没图」和「压根没认出来」在屏幕上是一个样子，但下一步不一样）",
          QStringLiteral("found=%1 iconUrl=%2")
              .arg(noIcon.found.size())
              .arg(noIcon.found.isEmpty() ? QString() : noIcon.found.first().hit.iconUrl));
    // 同一趟里**名字也一起没了**（那一发就是唯一给名字的地方）⇒ 那两个文件的名字点不动。
    // 这是正确的界面（不是降级）：名字空着的时候那一行照旧只是一行字，点上去什么都不会
    // 发生（见 InstanceFilesPage::setResourceForPath 的空 tip 那一支）。
    check(noIcon.found.first().hit.title.isEmpty(),
          "★ T22.5 那一发没成时**名字也是空的** ⇒ 那一行的名字不该变成链接"
          "（点一下开一页空标题比点不动糟）",
          noIcon.found.first().hit.title);

    // 没人听的那个端口：连都连不上（`problem` 是"没连上"那一档）。
    const InstanceIconLookup::Options deadOptions = [&api]() {
        InstanceIconLookup::Options opts;
        opts.modrinthApiBaseOverride =
            QStringLiteral("http://127.0.0.1:%1/v2").arg(api.server.serverPort() + 1);
        opts.curseForgeApiBaseOverride = api.curseForgeBase();
        return opts;
    }();
    const InstanceIconLookup::Result offline = InstanceIconLookup::lookup({modJar}, deadOptions);
    check(!offline.problem.isEmpty() && offline.found.isEmpty(),
          "★ T22.5 压根没人听的时候也是「没跑成」（不是静默地回一条空结果）", offline.problem);

    // ---------- ⑥ 盘上留一份：同一批文件不查第二遍 ----------
    //
    // （要求：「模组图标做本地缓存功能，不要每次都拉取」）。
    // 量的是**这一趟真的算出/问出多少个文件**（`hashed`）和**发出去的请求条数**：
    // 第二趟该是 0 和 0 —— 全在盘上那份里，`lookupNetwork()` 那一趟**压根不该被调起来**
    // （不是"调了但它快"）。少了这一条，"每次都拉取"那个毛病可以原样回来而全绿。
    {
        InstanceIconLookup::Options cached = options;
        cached.cacheDir = AppPaths::instanceIconCacheDir();
        cached.cacheKey = QStringLiteral("T22cachedInstance");
        const QString cachePath = InstanceIconLookup::cachePath(cached);
        check(!cachePath.isEmpty() && cachePath.startsWith(AppPaths::instanceIconCacheDir()),
              "T22.6 缓存文件名拼出来了（就在那一个缓存目录底下）",
              cachePath);
        // 先清干净：上一次跑留下的那一份会让下面"第一趟"直接变成 0 发（假绿）。
        QFile::remove(cachePath);

        const QStringList batch = {modJar, offJar, resZip, unknownJar};
        const int beforeFirst = api.requests.size();
        const InstanceIconLookup::Result first = InstanceIconLookup::lookup(batch, cached);
        const int firstSent = api.requests.size() - beforeFirst;
        check(first.problem.isEmpty() && first.found.size() == 3 &&
                  first.hashed == batch.size() && firstSent > 0,
              "T22.6 第一趟（盘上还没有那份）：照旧真算真问（四个文件都读了一遍）",
              QStringLiteral("found=%1 hashed=%2 problem=%3 问了 %4 发")
                  .arg(first.found.size())
                  .arg(first.hashed)
                  .arg(first.problem)
                  .arg(firstSent));

        QByteArray cacheBytes;
        {
            QFile file(cachePath);
            if (file.open(QIODevice::ReadOnly)) {
                cacheBytes = file.readAll();
            }
        }
        check(!cacheBytes.isEmpty(),
              "★ T22.6 这一趟跑完，盘上真的留下了一份（就是「下次不用再拉」的那一份）",
              QStringLiteral("%1 字节").arg(cacheBytes.size()));
        // 认出来的那三个+没认出来的那一个都在里面：键是**身份路径**（禁用着的那一份不带
        // `.disabled` —— 用户把开关拨一下，文件名变了但内容一个字没变，不该为此重查一遍）。
        {
            const QJsonObject files = QJsonDocument::fromJson(cacheBytes)
                                          .object()
                                          .value(QStringLiteral("files"))
                                          .toObject();
            const QString offIdentity = workDir + QStringLiteral("/old-mod.jar");
            check(files.contains(offIdentity) && !files.contains(offJar),
                  "★ T22.6 键是**身份路径**：禁用着的那一份记的是 `old-mod.jar` 而不是 "
                  "`old-mod.jar.disabled`（拨一下开关不该让缓存作废）",
                  QStringList(files.keys()).join(QStringLiteral(", ")));
        }

        // 第二趟：一模一样的这一批。
        const int beforeSecond = api.requests.size();
        const InstanceIconLookup::Result second = InstanceIconLookup::lookup(batch, cached);
        check(api.requests.size() == beforeSecond,
              "★★ T22.6 第二趟**一个请求都不发**（全在盘上那份里）",
              QStringLiteral("又问了 %1 发").arg(api.requests.size() - beforeSecond));
        check(second.hashed == 0,
              "★★ T22.6 而且**一个字节的 jar 都没重读重算**（这正是用户说的「每次都拉取」"
              "里最贵的那一半）",
              QStringLiteral("hashed=%1").arg(second.hashed));
        check(second.problem.isEmpty() && second.found.size() == 3,
              "★★ T22.6 结果和第一趟**逐条一样**（没认出来的那一个照旧不出现）",
              QStringLiteral("found=%1 problem=%2")
                  .arg(second.found.size())
                  .arg(second.problem));
        {
            // 逐条比"认出来的那三个是不是同一批"（不是只比条数）。
            QSet<QString> firstIds;
            for (const InstanceIconLookup::Found &found : first.found) {
                firstIds.insert(found.hit.projectId + QStringLiteral("|") + found.path);
            }
            QSet<QString> secondIds;
            for (const InstanceIconLookup::Found &found : second.found) {
                secondIds.insert(found.hit.projectId + QStringLiteral("|") + found.path);
            }
            check(firstIds == secondIds && !firstIds.isEmpty(),
                  "T22.6 第二趟认出来的就是第一趟那几条（项目号 + 路径都对得上）",
                  QStringList(secondIds.values()).join(QStringLiteral(", ")));
            // **整个 hit 都还回来了**（不是只回一个项目号）：从缓存里读出来的那条也得带着
            // 名字 / 简介 / 图标地址 —— 少一栏的症状是那一页顶着一个空标题。
            bool titleKept = false;
            for (const InstanceIconLookup::Found &found : second.found) {
                if (!found.hit.title.isEmpty() && !found.hit.iconUrl.isEmpty()) {
                    titleKept = true;
                }
            }
            check(titleKept, "★ T22.6 缓存里读出来的那条带着名字和图标地址（不是只剩一个号）");
        }

        // 内容被换过的那个文件**要重算重问**（键里带着大小 + mtime + 算出来的 sha1）：
        // 只在"文件没动过"时才省那一步。
        writeFileAt(modJar, QByteArray(700, 'Y'));
        const int beforeThird = api.requests.size();
        const InstanceIconLookup::Result third =
            InstanceIconLookup::lookup({modJar}, cached);
        check(third.hashed == 1 && api.requests.size() > beforeThird,
              "★ T22.6 被换掉的那个文件**重新算了一遍、也重新问了一发**（缓存不是按路径瞎认人）",
              QStringLiteral("hashed=%1 问了 %2 发")
                  .arg(third.hashed)
                  .arg(api.requests.size() - beforeThird));
        check(third.problem.isEmpty() && third.found.isEmpty(),
              "T22.6 换过内容之后那一份就认不出来了（它已经不是原来那个 jar 了）",
              QStringLiteral("found=%1").arg(third.found.size()));

        QFile::remove(cachePath);
    }

    AppPaths::setMinecraftDir(originalDir);
    check(AppPaths::minecraftDir() == originalDir, "T22.9 数据根目录换回来了");
}

