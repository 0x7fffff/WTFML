#include "Support.hpp"
#include "ModpackDomainChecks.hpp"
#include <QUuid>

void runModpackExportLookupTests(const QString &sandbox) {
    // --- V5i. 导出：枚举 → 算哈希 → 反查 → 拼清单 这一整趟 --------------------
    //
    // 反查那一发是**注入进来的**（产品里是 HttpClient，这里是假的），所以整趟离线可跑。
    // 这里要钉的三句话：①认出来的进清单、不进 overrides；②认不出来的一个不丢、
    // 全在 overrides 里；③反查没成**不是错误**（照旧导得出包，只是全进 overrides）。
    {
        const QString tree = sandbox + "/export-plan/我的实例";
        QDir(tree).removeRecursively();
        QDir().mkpath(tree);
        const QByteArray bytesMatched = QByteArrayLiteral("AAA 认得出");
        const QByteArray bytesUnknown = QByteArrayLiteral("BBB 认不出");
        writeFileAt(tree + "/mods/认得出来.jar", bytesMatched);
        writeFileAt(tree + "/mods/认不出来.jar", bytesUnknown);
        writeFileAt(tree + "/config/配置.toml", QByteArrayLiteral("a=1"));
        writeFileAt(tree + "/options.txt", QByteArrayLiteral("fov:70"));
        writeFileAt(tree + "/logs/不该进包.log", QByteArrayLiteral("日志"));
        writeFileAt(tree + "/我的实例.jar", QByteArrayLiteral("版本文件"));

        const auto sha1OfBytes = [](const QByteArray &bytes) {
            QCryptographicHash hash(QCryptographicHash::Sha1);
            hash.addData(QByteArrayView(bytes));
            return QString::fromLatin1(hash.result().toHex());
        };
        const auto sha512OfBytes = [](const QByteArray &bytes) {
            QCryptographicHash hash(QCryptographicHash::Sha512);
            hash.addData(QByteArrayView(bytes));
            return QString::fromLatin1(hash.result().toHex());
        };
        const QString sha1Matched = sha1OfBytes(bytesMatched);
        const QString sha1Unknown = sha1OfBytes(bytesUnknown);

        ModpackExport::ExportRequest request;
        request.format = ModpackExport::Format::Modrinth;
        request.instanceDir = tree;
        request.name = QStringLiteral("我的整合包");
        request.version = QStringLiteral("1.2.3");
        request.summary = QStringLiteral("一句话简介");
        request.gameVersion = QStringLiteral("1.21.1");
        request.loaderKey = ModpackImport::fabricLoaderKey();
        request.loaderVersion = QStringLiteral("0.16.14");

        QStringList askedSha1;
        int queryCalls = 0;
        const ModpackExport::ModrinthQuery fakeModrinth =
            [&](const QStringList &sha1s, QHash<QString, ModpackExport::ModrinthHit> *out,
                QString *problemOut) {
                ++queryCalls;
                askedSha1 = sha1s;
                if (problemOut != nullptr) {
                    problemOut->clear();
                }
                ModpackExport::ModrinthHit hit;
                hit.projectId = QStringLiteral("u6dRKJwZ");
                hit.versionNumber = QStringLiteral("1.2.3");
                hit.filename = QStringLiteral("认得出来.jar");
                hit.sha1 = sha1Matched;
                hit.sha512 = sha512OfBytes(bytesMatched);
                hit.url = QStringLiteral(
                    "https://cdn.modrinth.com/data/u6dRKJwZ/versions/abc/认得出来.jar");
                hit.size = bytesMatched.size();
                hit.environment = QStringLiteral("client_only");
                out->insert(sha1Matched, hit);
                return true;
            };

        // 进度：记下每一次（done, total, 当前那个文件）。最后一次是反查那一发
        // （没有具体文件，`current` 是空串）——界面靠它切到"正在问接口"。
        QList<QString> progressLines;
        int lastDone = -1;
        int lastTotal = -1;
        const ModpackExport::ExportProgress progress = [&](int done, int total,
                                                          const QString &current) {
            lastDone = done;
            lastTotal = total;
            progressLines.append(QStringLiteral("%1/%2 %3").arg(done).arg(total).arg(current));
        };

        const ModpackExport::ExportPlan plan =
            ModpackExport::buildExportPlan(request, fakeModrinth, ModpackExport::CurseForgeQuery(),
                                          progress);
        check(plan.problem.isEmpty() && !plan.cancelled && !plan.lookupFailed,
              "V5i 整趟跑完没有停下来（枚举/哈希/反查都成了）", plan.problem);
        check(queryCalls == 1, "V5i 反查是**一发**（不是每个文件一发）",
              QString::number(queryCalls));
        check(plan.queried == 2, "V5i 问的是那两个资源文件（config/options.txt 不该去问）",
              QString::number(plan.queried));
        QStringList askedSorted = askedSha1;
        askedSorted.sort();
        QStringList expectedAsked = {sha1Matched, sha1Unknown};
        expectedAsked.sort();
        check(askedSorted == expectedAsked,
              "V5i 发过去的就是本地算出来的**那两个 sha1**（一条不多一条不少）",
              askedSorted.join(QLatin1Char(',')));
        check(plan.matched == 1 && plan.notDistributable == 0,
              "V5i 认出来一条", QString::number(plan.matched));
        check(lastDone == lastTotal && lastTotal == 2,
              "V5i 进度走到了头（total 就是要算哈希的文件数）",
              QStringLiteral("%1/%2").arg(lastDone).arg(lastTotal));
        check(!progressLines.isEmpty() && progressLines.last().endsWith(QLatin1Char(' ')),
              "V5i 最后一发进度没有文件名（那是「正在问接口」那一档）",
              progressLines.join(QLatin1Char('|')));

        // ① 认出来的进清单，而且**不在** overrides 里
        check(plan.spec.files.size() == 1, "V5i 清单里就那一条",
              QString::number(plan.spec.files.size()));
        if (plan.spec.files.size() == 1) {
            const ModpackExport::MatchedFile &matched = plan.spec.files.first();
            check(matched.path == QStringLiteral("mods/认得出来.jar") &&
                      matched.sha1 == sha1Matched &&
                      matched.sha512 == sha512OfBytes(bytesMatched) &&
                      matched.size == bytesMatched.size() &&
                      matched.urls.size() == 1,
                  "V5i 认出来的那条：路径 / sha1 / sha512 / 字节数 / 地址都对得上",
                  matched.path);
            check(matched.envClient == QStringLiteral("required") &&
                      matched.envServer == QStringLiteral("unsupported"),
                  "V5i 接口说 `client_only` 时写 env（只写这一种，见 applyEnvironment）",
                  matched.envClient + QLatin1Char('|') + matched.envServer);
        }
        QStringList overridePaths;
        for (const ModpackExport::InstanceFile &file : plan.overrides) {
            overridePaths.append(file.path);
        }
        const QStringList expectedOverrides = {QStringLiteral("config/配置.toml"),
                                               QStringLiteral("mods/认不出来.jar"),
                                               QStringLiteral("options.txt")};
        check(overridePaths == expectedOverrides,
              "V5i 认不出来的**一个不丢**、全在 overrides 里，且按路径排过序（认出来的不在里面）",
              overridePaths.join(QLatin1Char(',')));
        check(!plan.collect.skippedDirs.isEmpty() &&
                  plan.collect.skippedDirs.contains(QStringLiteral("logs")),
              "V5i 被排掉的目录照实带出来（界面上要说哪些没进包）");

        // 清单往返：我们自己写得出来，自己也得读得回来
        const ModpackImport::Result back =
            ModpackImport::parseModrinthIndex(ModpackExport::buildModrinthIndex(plan.spec));
        check(back.ok && back.pack.fileCount() == 1 &&
                  back.pack.files.first().path == QStringLiteral("mods/认得出来.jar"),
              "V5i 这一趟的产物自己的解析器读得回来（导出/导入是同一件事的两个方向）",
              back.problem);
        check(ModpackExport::reimportable(plan.spec),
              "V5i 有清单条目 ⇒ 这是个我们自己装得回来的包");

        // ② 反查没成：**不是错误**，但一个文件都不许丢 —— 那些"本来能认出来"的现在
        // 也只能进 overrides（断网也能出包，这是 PCL 那套的取舍）。
        const ModpackExport::ModrinthQuery deadQuery =
            [](const QStringList &, QHash<QString, ModpackExport::ModrinthHit> *,
               QString *problemOut) {
                if (problemOut != nullptr) {
                    *problemOut = QStringLiteral("连不上 Modrinth。");
                }
                return false;
            };
        const ModpackExport::ExportPlan offline =
            ModpackExport::buildExportPlan(request, deadQuery, ModpackExport::CurseForgeQuery());
        check(offline.lookupFailed && offline.lookupProblem == QStringLiteral("连不上 Modrinth。") &&
                  offline.problem.isEmpty(),
              "V5i 反查没成如实报出来，但**不算错误**（清单/overrides 照旧拼得出来）",
              offline.lookupProblem);
        QStringList offlineOverrides;
        for (const ModpackExport::InstanceFile &file : offline.overrides) {
            offlineOverrides.append(file.path);
        }
        check(offline.matched == 0 && offline.spec.files.isEmpty() &&
                  offlineOverrides.contains(QStringLiteral("mods/认得出来.jar")) &&
                  offlineOverrides.size() == 4,
              "V5i 反查没成时那份「本来认得出」的文件也进 overrides（一份都不丢）",
              offlineOverrides.join(QLatin1Char(',')));
        check(!ModpackExport::reimportable(offline.spec),
              "V5i 反查没成之后清单是空的 ⇒ 调用点必须拿 reimportable() 拦一下"
              "（我们自己的解析器拒收空清单：ModpackImport.cpp:379）");

        // ③ 取消：一个文件都还没读就停 —— 反查那一发**不许发出去**，清单一条都没有。
        // 这时候整份 plan 作废（`overrides` 里可能有半截东西，见 .hpp 那句），
        // 所以这里不去数 overrides，只钉"叫停之后收手了没有"。
        int cancelQueryCalls = 0;
        const ModpackExport::ModrinthQuery countingQuery =
            [&cancelQueryCalls](const QStringList &, QHash<QString, ModpackExport::ModrinthHit> *,
                                QString *) {
                ++cancelQueryCalls;
                return true;
            };
        const ModpackExport::ExportPlan stopped =
            ModpackExport::buildExportPlan(request, countingQuery, ModpackExport::CurseForgeQuery(),
                                           ModpackExport::ExportProgress(),
                                           []() { return true; });
        check(stopped.cancelled && stopped.spec.files.isEmpty() && stopped.spec.refs.isEmpty() &&
                  stopped.queried == 0 && cancelQueryCalls == 0 && !stopped.lookupFailed,
              "V5i 用户叫停之后立刻收手（一个哈希都不算、反查不发、清单是空的），"
              "且这不是「反查失败」那一档",
              QStringLiteral("%1 / 问了 %2 个").arg(cancelQueryCalls).arg(stopped.queried));

        QDir(sandbox + "/export-plan").removeRecursively();
    }

    // --- V5j. 导出：CurseForge 那一条路（号 + 关分发 + 毒指纹） ---------------
    {
        const QString tree = sandbox + "/export-cf/我的实例";
        QDir(tree).removeRecursively();
        QDir().mkpath(tree);
        const QByteArray bytesOk = QByteArrayLiteral("CCC 能下");
        const QByteArray bytesClosed = QByteArrayLiteral("DDD 关了分发");
        writeFileAt(tree + "/mods/能下的.jar", bytesOk);
        writeFileAt(tree + "/mods/关了分发的.jar", bytesClosed);

        quint32 fpOk = 0;
        quint32 fpClosed = 0;
        QString hashProblem;
        check(ModpackExport::curseForgeFingerprint(tree + "/mods/能下的.jar", &fpOk, &hashProblem) &&
                  ModpackExport::curseForgeFingerprint(tree + "/mods/关了分发的.jar", &fpClosed,
                                                       &hashProblem),
              "V5j 两个文件的指纹都算出来了", hashProblem);

        ModpackExport::ExportRequest request;
        request.format = ModpackExport::Format::CurseForge;
        request.instanceDir = tree;
        request.name = QStringLiteral("我的整合包");
        request.version = QStringLiteral("1.2.3");
        request.author = QStringLiteral("某人");
        request.gameVersion = QStringLiteral("1.21.1");
        request.loaderKey = ModpackImport::neoforgeLoaderKey();
        request.loaderVersion = QStringLiteral("21.1.210");

        int queryCalls = 0;
        QList<quint32> askedFingerprints;
        const ModpackExport::CurseForgeQuery fakeCurseForge =
            [&](const QList<quint32> &fingerprints, QHash<quint32, ModpackExport::CurseForgeHit> *out,
                QString *problemOut) {
                ++queryCalls;
                askedFingerprints = fingerprints;
                if (problemOut != nullptr) {
                    problemOut->clear();
                }
                ModpackExport::CurseForgeHit ok;
                ok.projectId = 394468;
                ok.fileId = 5123456;
                ok.available = true;
                ok.fileName = QStringLiteral("能下的.jar");
                ok.downloadUrl = QStringLiteral("https://edge.forgecdn.net/files/5123/456/x.jar");
                out->insert(fpOk, ok);
                ModpackExport::CurseForgeHit closed;
                closed.projectId = 238222;
                closed.fileId = 8971390;
                closed.available = false;  // 作者关了第三方分发
                closed.availableKnown = true;
                closed.fileName = QStringLiteral("关了分发的.jar");
                out->insert(fpClosed, closed);
                return true;
            };

        const ModpackExport::ExportPlan plan = ModpackExport::buildExportPlan(
            request, ModpackExport::ModrinthQuery(), fakeCurseForge);
        check(queryCalls == 1 && askedFingerprints.size() == 2,
              "V5j CurseForge 也是一发问完（指纹按本地算出来的那两个）",
              QString::number(askedFingerprints.size()));
        check(plan.matched == 1 && plan.notDistributable == 1,
              "V5j 认出来一条、另一条「认出来了但作者关了分发」（分开报数）",
              QStringLiteral("%1 / %2").arg(plan.matched).arg(plan.notDistributable));
        check(plan.spec.refs.size() == 1 && plan.spec.refs.first().projectId == 394468 &&
                  plan.spec.refs.first().fileId == 5123456,
              "V5j 清单里只有两个号（CF 那边没有文件名/地址/哈希）");
        QStringList cfOverrides;
        for (const ModpackExport::InstanceFile &file : plan.overrides) {
            cfOverrides.append(file.path);
        }
        check(cfOverrides == QStringList{QStringLiteral("mods/关了分发的.jar")},
              "V5j 关了分发的那份退回 overrides（写进清单等于给出一个装不上的包）",
              cfOverrides.join(QLatin1Char(',')));
        // 往返：号是那两个号，加载器那一行也是 CF 认得的那一行
        const ModpackImport::Result back = ModpackImport::parseCurseForgeManifest(
            ModpackExport::buildCurseForgeManifest(plan.spec));
        check(back.ok && back.pack.pendingRefs.size() == 1 &&
                  back.pack.pendingRefs.first().projectId == 394468 &&
                  back.pack.pendingRefs.first().fileId == 5123456 &&
                  back.pack.loaderKey == ModpackImport::neoforgeLoaderKey() &&
                  back.pack.loaderVersion == QStringLiteral("21.1.210"),
              "V5j CF 清单往返：号与加载器那一行都对得上", back.problem);
        check(ModpackExport::reimportable(plan.spec),
              "V5j 有 refs ⇒ 我们自己装得回来");

        QDir(sandbox + "/export-cf").removeRecursively();
    }

    // --- V5s. 导出：名字长到清单写不下的那一条，必须退回 overrides ---------------
    //
    // 读侧对清单里**每一条**路径都跑 isSafeRelativePath()（ModpackImport.cpp:313），
    // 不过就是**整包拒绝**（"所以整个包都没有导入"）。所以产出这一侧不许写出那样一条。
    // 实例里有一个嵌套很深、拼起来超过 240 个字符的文件时，导出侧若照样写进清单，这个
    // 包连自己都装不回来，而且**当场看不出来**（清单写得出来、zip 打得出来）。
    // 退回 overrides 之后文件一个不少 —— 那一条路不经过这个判据。
    {
        const QString tree = sandbox + "/export-long/我的实例";
        QDir(tree).removeRecursively();
        QDir().mkpath(tree);
        const QByteArray bytesOk = QByteArrayLiteral("AAA 正常的那一个");
        const QByteArray bytesLong = QByteArrayLiteral("BBB 名字超长的那一个");
        writeFileAt(tree + "/mods/认得出来.jar", bytesOk);
        // 整条相对路径 = 5 + 120 + 1 + 120 + 1 + 6 = 253 个字符（> 240），而**每一段**
        // 都在名字允许的长度里 —— 超的是"拼起来以后"那一条，也就只有长度那一条判据拦得住
        // 它（段那几条都在 InstanceFiles::validatePathSegment 里，那里没有长度限制）。
        const QString longPath = QStringLiteral("mods/") + QString(120, QLatin1Char('x')) +
                                 QLatin1Char('/') + QString(120, QLatin1Char('y')) +
                                 QStringLiteral("/长名字.jar");
        writeFileAt(tree + QStringLiteral("/") + longPath, bytesLong);
        check(longPath.length() > 240, "V5s 造出来的那条路径真的超过 240 个字符",
              QString::number(longPath.length()));

        const auto sha1Of = [](const QByteArray &bytes) {
            QCryptographicHash hash(QCryptographicHash::Sha1);
            hash.addData(QByteArrayView(bytes));
            return QString::fromLatin1(hash.result().toHex());
        };
        const auto sha512Of = [](const QByteArray &bytes) {
            QCryptographicHash hash(QCryptographicHash::Sha512);
            hash.addData(QByteArrayView(bytes));
            return QString::fromLatin1(hash.result().toHex());
        };
        const QString sha1Ok = sha1Of(bytesOk);
        const QString sha1Long = sha1Of(bytesLong);

        ModpackExport::ExportRequest request;
        request.format = ModpackExport::Format::Modrinth;
        request.instanceDir = tree;
        request.name = QStringLiteral("我的整合包");
        request.version = QStringLiteral("1.2.3");
        request.gameVersion = QStringLiteral("1.21.1");
        request.loaderKey = ModpackImport::fabricLoaderKey();
        request.loaderVersion = QStringLiteral("0.16.14");

        // 反查**两条都答得出来** —— 这一节要钉的不是"认不认得出"，而是"认出来了也不许写"。
        const ModpackExport::ModrinthQuery query =
            [&](const QStringList &, QHash<QString, ModpackExport::ModrinthHit> *out, QString *) {
                const auto put = [&](const QString &sha1, const QString &sha512, qint64 size) {
                    ModpackExport::ModrinthHit hit;
                    hit.projectId = QStringLiteral("abcd1234");
                    hit.sha1 = sha1;
                    hit.sha512 = sha512;
                    hit.url = QStringLiteral("https://cdn.modrinth.com/data/abcd1234/versions/x/a.jar");
                    hit.size = size;
                    out->insert(sha1, hit);
                };
                put(sha1Ok, sha512Of(bytesOk), bytesOk.size());
                put(sha1Long, sha512Of(bytesLong), bytesLong.size());
                return true;
            };

        const ModpackExport::ExportPlan plan =
            ModpackExport::buildExportPlan(request, query, ModpackExport::CurseForgeQuery());
        check(plan.problem.isEmpty() && plan.lookupFailed == false,
              "V5s 整趟跑完没有停下来", plan.problem);
        check(plan.queried == 2 && plan.matched == 1 && plan.spec.files.size() == 1 &&
                  plan.spec.files.first().path == QStringLiteral("mods/认得出来.jar"),
              "V5s 只有那条短的写进清单（超长的那条**认出来了也不许写**）",
              QStringLiteral("%1 / %2").arg(plan.queried).arg(plan.matched));
        QStringList overridePaths;
        for (const ModpackExport::InstanceFile &file : plan.overrides) {
            overridePaths.append(file.path);
        }
        check(overridePaths == QStringList{longPath},
              "V5s 超长的那条退回 overrides（跟着包走，一个文件都不丢）",
              QString::number(overridePaths.size()));
        // 直接问那句判据本身（纯函数）：这两条一真一假，才是"退回去"这个动作的来路。
        ModpackExport::MatchedFile longEntry;
        longEntry.path = longPath;
        longEntry.sha1 = sha1Long;
        longEntry.sha512 = sha512Of(bytesLong);
        longEntry.urls.append(QStringLiteral("https://cdn.modrinth.com/data/abcd1234/versions/x/a.jar"));
        check(!ModpackExport::writableAsIndexFile(longEntry),
              "V5s writableAsIndexFile 说这条不能写进清单（路径过不了读侧那条判据）");
        // 反证：真写进去的话，**读侧就是整包拒绝** —— 这一条把"为什么必须退回"钉在读侧
        // 那句实现上（哪天读侧把长度那一条去掉了，这里会红，因为拒绝不再发生）。
        // NOTE: 是拿**这一趟真写出来的那份清单**改的（不是手搓一份）：手搓的话，
        // "被拒"可能来自别的缺项（少个 dependencies 之类），那就证明不了这一条。
        // 也不能拿 badSpec 走 buildModrinthIndex —— 那个函数自己也会先问
        // writableAsIndexFile，会把这条**跳过不写**（见它里面那段），于是清单里
        // 根本没有这一条，验的是一句空话。
        QJsonObject tampered =
            QJsonDocument::fromJson(ModpackExport::buildModrinthIndex(plan.spec)).object();
        QJsonArray tamperedFiles = tampered.value(QStringLiteral("files")).toArray();
        QJsonObject hashes;
        hashes.insert(QStringLiteral("sha1"), sha1Long);
        hashes.insert(QStringLiteral("sha512"), sha512Of(bytesLong));
        QJsonObject longEntryJson;
        longEntryJson.insert(QStringLiteral("path"), longPath);
        longEntryJson.insert(QStringLiteral("hashes"), hashes);
        longEntryJson.insert(
            QStringLiteral("downloads"),
            QJsonArray{QStringLiteral("https://cdn.modrinth.com/data/abcd1234/versions/x/a.jar")});
        tamperedFiles.append(longEntryJson);
        tampered.insert(QStringLiteral("files"), tamperedFiles);
        const ModpackImport::Result rejected = ModpackImport::parseModrinthIndex(
            QJsonDocument(tampered).toJson(QJsonDocument::Compact));
        check(!rejected.ok,
              "V5s 反证：这样的路径一旦写进清单，读侧整包拒绝（所以产出这一侧必须拦住）",
              rejected.problem);
        // 而真正导出的那一份清单读得回来（清单里没有那条超长路径）
        const ModpackImport::Result back =
            ModpackImport::parseModrinthIndex(ModpackExport::buildModrinthIndex(plan.spec));
        check(back.ok && back.pack.fileCount() == 1,
              "V5s 这一趟的产物自己的解析器读得回来", back.problem);

        QDir(sandbox + "/export-long").removeRecursively();
    }

    // --- V5t. 导出（CF）：没说能不能分发 ≠ 作者关了分发；改过名/禁用着的退回 overrides
    //
    // 三条都是"报告里那句话必须对得上盘上的事实"：
    // ① 回复里**没有** `isAvailable` 这一项时，`toBool(false)` 也是假，但那是"接口没说"，
    // 不是"作者关了第三方分发" —— 两种分开报；
    // ② 用户自己改过名（`改过名.jar`）或者禁用着（`禁用着的.jar.disabled`）的 mod，
    // 写进 CF 清单等于让收包的人那边落回**作者给的原名**：禁用着的那个会变成启用的。
    // 它们只有原样 copy 才留得住状态 ⇒ 退回 overrides；
    // ③ 接口根本没接上时 `queried` 是 **0**（一个字节都没发出去），不是"本来打算问几个"。
    {
        const QString tree = sandbox + "/export-cf-state/我的实例";
        QDir(tree).removeRecursively();
        QDir().mkpath(tree);
        const QByteArray bytesOk = QByteArrayLiteral("AAA 能下的");
        const QByteArray bytesClosed = QByteArrayLiteral("BBB 作者关了分发");
        const QByteArray bytesSilent = QByteArrayLiteral("CCC 接口没提这一项");
        const QByteArray bytesOff = QByteArrayLiteral("DDD 用户禁用着的");
        const QByteArray bytesRenamed = QByteArrayLiteral("EEE 用户改过名的");
        writeFileAt(tree + "/mods/能下的.jar", bytesOk);
        writeFileAt(tree + "/mods/关了分发的.jar", bytesClosed);
        writeFileAt(tree + "/mods/没说.jar", bytesSilent);
        writeFileAt(tree + "/mods/禁用着的.jar.disabled", bytesOff);
        writeFileAt(tree + "/mods/改过名.jar", bytesRenamed);

        const auto fingerprintOf = [&](const QByteArray &bytes, const QString &name) {
            quint32 value = 0;
            QString problem;
            if (!ModpackExport::curseForgeFingerprint(tree + QStringLiteral("/mods/") + name, &value,
                                                      &problem)) {
                check(false, "V5t 指纹算得出来", problem);
            }
            return value;
        };
        const quint32 fpOk = fingerprintOf(bytesOk, QStringLiteral("能下的.jar"));
        const quint32 fpClosed = fingerprintOf(bytesClosed, QStringLiteral("关了分发的.jar"));
        const quint32 fpSilent = fingerprintOf(bytesSilent, QStringLiteral("没说.jar"));
        const quint32 fpOff = fingerprintOf(bytesOff, QStringLiteral("禁用着的.jar.disabled"));
        const quint32 fpRenamed = fingerprintOf(bytesRenamed, QStringLiteral("改过名.jar"));

        ModpackExport::ExportRequest request;
        request.format = ModpackExport::Format::CurseForge;
        request.instanceDir = tree;
        request.name = QStringLiteral("我的整合包");
        request.version = QStringLiteral("1.2.3");
        request.gameVersion = QStringLiteral("1.21.1");
        request.loaderKey = ModpackImport::forgeLoaderKey();
        request.loaderVersion = QStringLiteral("47.2.0");

        int queryCalls = 0;
        const ModpackExport::CurseForgeQuery query =
            [&](const QList<quint32> &, QHash<quint32, ModpackExport::CurseForgeHit> *out, QString *) {
                ++queryCalls;
                const auto put = [&](quint32 key, int projectId, int fileId, const QString &fileName,
                                     bool available, bool availableKnown) {
                    ModpackExport::CurseForgeHit hit;
                    hit.projectId = projectId;
                    hit.fileId = fileId;
                    hit.fileName = fileName;
                    hit.available = available;
                    hit.availableKnown = availableKnown;
                    out->insert(key, hit);
                };
                put(fpOk, 394468, 5123456, QStringLiteral("能下的.jar"), true, true);
                // 作者关了分发：接口**明说**了（有 `isAvailable`，值是假）。
                put(fpClosed, 238222, 8971390, QStringLiteral("关了分发的.jar"), false, true);
                // 接口**没说**：回复里连这一项都没有（缺席 ⇒ toBool(false) 也是假）。
                put(fpSilent, 111111, 2222222, QStringLiteral("没说.jar"), false, false);
                // 这两条接口都说能下（`fileName` = 作者给的原名），但**用户盘上的名字不一样**：
                // 一个是禁用着（后缀），一个是改过名。
                put(fpOff, 333333, 4444444, QStringLiteral("禁用着的.jar"), true, true);
                put(fpRenamed, 555555, 6666666, QStringLiteral("原来的名字.jar"), true, true);
                return true;
            };

        const ModpackExport::ExportPlan plan =
            ModpackExport::buildExportPlan(request, ModpackExport::ModrinthQuery(), query);
        check(plan.problem.isEmpty() && !plan.lookupFailed && queryCalls == 1 && plan.queried == 5,
              "V5t 一发问完五个（queried 是**发出去那些**）",
              QStringLiteral("%1 / 问了 %2").arg(queryCalls).arg(plan.queried));
        check(plan.matched == 1 && plan.notDistributable == 1 &&
                  plan.notDistributableUnknown == 1 && plan.keptLocalState == 2,
              "V5t 四种原因分开报：认出来 1 / 作者关了 1 / 接口没说 1 / 名字带状态 2",
              QStringLiteral("%1 / %2 / %3 / %4")
                  .arg(plan.matched)
                  .arg(plan.notDistributable)
                  .arg(plan.notDistributableUnknown)
                  .arg(plan.keptLocalState));
        check(plan.spec.refs.size() == 1 && plan.spec.refs.first().projectId == 394468,
              "V5t 清单里只有那条能下的（另外四种都没写）",
              QString::number(plan.spec.refs.size()));
        QStringList overridePaths;
        for (const ModpackExport::InstanceFile &file : plan.overrides) {
            overridePaths.append(file.path);
        }
        check(overridePaths.size() == 4 &&
                  overridePaths.contains(QStringLiteral("mods/关了分发的.jar")) &&
                  overridePaths.contains(QStringLiteral("mods/没说.jar")) &&
                  overridePaths.contains(QStringLiteral("mods/禁用着的.jar.disabled")) &&
                  overridePaths.contains(QStringLiteral("mods/改过名.jar")) &&
                  !overridePaths.contains(QStringLiteral("mods/能下的.jar")),
              "V5t 那四种全退回 overrides（原样带着用户那个名字），认出来的那条不在里面",
              overridePaths.join(QLatin1Char(',')));
        // ③ 接口没接上：一个字节都没发出去 ⇒ queried 必须是 0，不是"本来打算问几个"
        // （写成 5 的话，报告里那句「问过 5 个文件，认出来 0 个」会让人去查网络、查限流，
        // 而真相是这一趟根本没联网）。
        const ModpackExport::ExportPlan noQuery = ModpackExport::buildExportPlan(
            request, ModpackExport::ModrinthQuery(), ModpackExport::CurseForgeQuery());
        check(noQuery.queried == 0 && noQuery.lookupFailed && noQuery.matched == 0 &&
                  noQuery.spec.refs.isEmpty() && noQuery.overrides.size() == 5,
              "V5t 接口没接上：queried 是 0（不是「本来打算问几个」），文件一个不丢",
              QStringLiteral("queried=%1 / overrides=%2")
                  .arg(noQuery.queried)
                  .arg(noQuery.overrides.size()));

        QDir(sandbox + "/export-cf-state").removeRecursively();
    }

}
