#include "Support.hpp"

void runQuickPlayTests(const QString &metaDir) {
    const QuickPlay::Kind single = QuickPlay::Kind::Singleplayer;
    const QuickPlay::Kind multi = QuickPlay::Kind::Multiplayer;

    section("ZL1. 参数名 / 拼出来的 token");
    {
        check(QuickPlay::argumentName(single) == QStringLiteral("--quickPlaySingleplayer") &&
                  QuickPlay::argumentName(multi) == QStringLiteral("--quickPlayMultiplayer"),
              "ZL1 两位的参数名（唯一出处）");

        check(QuickPlay::arguments(QuickPlay::Target()).isEmpty(),
              "ZL1 没有这一发 ⇒ 命令行上多不出任何东西（绝大多数启动的处境）");

        const QStringList tokens =
            QuickPlay::arguments(QuickPlay::Target{multi, QStringLiteral("mc.example.com:25566")});
        check(tokens.size() == 2 && tokens.at(0) == QStringLiteral("--quickPlayMultiplayer") &&
                  tokens.at(1) == QStringLiteral("mc.example.com:25566"),
              "ZL1 有那一发 ⇒ 先名字后值，两个 token", tokens.join(QStringLiteral(" ")));
    }

    section("ZL2 / ZL3. 认哪几种写法");
    {
        // 照抄真 json（26.2 那份）里的原样。
        const QJsonObject entry = QJsonDocument::fromJson(R"({
            "rules": [{"action": "allow", "features": {"is_quick_play_singleplayer": true}}],
            "value": ["--quickPlaySingleplayer", "${quickPlaySingleplayer}"]
        })").object();
        const QJsonObject realShape{{QStringLiteral("arguments"),
                                     QJsonObject{{QStringLiteral("game"), QJsonArray{entry}}}}};
        check(QuickPlay::supports(realShape, single), "ZL2 真形状（对象 + rules + value 数组）⇒ 支持");
        check(!QuickPlay::supports(realShape, multi),
              "ZL2 只写了单人那一条 ⇒ 多人**不支持**（两位各算各的）");

        const auto withGame = [](const QJsonArray &game) {
            return QJsonObject{
                {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("game"), game}}}};
        };

        QJsonArray bare;
        bare.append(QStringLiteral("--quickPlayMultiplayer"));
        check(QuickPlay::supports(withGame(bare), multi), "ZL3 裸字符串条目也认");

        QJsonArray singleValue;
        singleValue.append(QJsonObject{{QStringLiteral("value"), QStringLiteral("--quickPlayMultiplayer")}});
        check(QuickPlay::supports(withGame(singleValue), multi), "ZL3 value 是单个字符串也认");

        QJsonArray inArray;
        inArray.append(QStringLiteral("--quickPlaySingleplayer"));
        check(QuickPlay::supports(
                  withGame({QJsonObject{{QStringLiteral("value"), inArray}}}), single),
              "ZL3 数组里混着别的写法也认");

        check(QuickPlay::supports(
                  QJsonObject{{QStringLiteral("minecraftArguments"),
                               QStringLiteral("-Xmx1G --quickPlayMultiplayer ${quickPlayMultiplayer}")}},
                  multi),
              "ZL3 老格式那串空格分隔的也扫一遍");
    }

    section("ZL4. 反面：空 json / 只有 feature 名");
    {
        check(!QuickPlay::supports(QJsonObject(), single) &&
                  !QuickPlay::supports(QJsonObject(), multi),
              "ZL4 空 json ⇒ 两位都不支持");

        // 判据是 **value 里那个名字**，不是 rules 里那个 feature 名。这一条故意只写
        // feature 名：把判据写成"rules 里出现过 is_quick_play_*"的实现会在这儿假绿。
        const QJsonObject onlyFeature = QJsonDocument::fromJson(R"({
            "arguments": {"game": [
                {"rules": [{"action": "allow", "features": {"is_quick_play_singleplayer": true}}],
                 "value": ["--somethingElse", "${somethingElse}"]}
            ]}
        })").object();
        check(!QuickPlay::supports(onlyFeature, single),
              "ZL4 只有 feature 名、value 里没有这个 flag ⇒ 不支持");
    }

    section("ZL5. 真 json：1.20 之后那份有、1.19 那份没有");
    {
        const QJsonObject modern = loadJson(metaDir + QStringLiteral("/26.2.json"));
        check(!modern.isEmpty(), "ZL5 读到了真 json 夹具（meta/26.2.json）");
        if (!modern.isEmpty()) {
            check(QuickPlay::supports(modern, single) && QuickPlay::supports(modern, multi),
                  "ZL5 1.20 之后那份真 json 里两位都在");
        }

        const QJsonObject old = loadJson(metaDir + QStringLiteral("/1.19.json"));
        check(!old.isEmpty(), "ZL5 读到了 1.19 那份夹具（quick play 之前的最后一个正式版）");
        if (!old.isEmpty()) {
            check(!QuickPlay::supports(old, single) && !QuickPlay::supports(old, multi),
                  "ZL5 1.19 ⇒ 两位都不支持（发了游戏会当场退出的那一档）");
        }
    }

    section("ZL6. ★ 要问**合并过**的那一份");
    {
        const QJsonObject vanilla = loadJson(metaDir + QStringLiteral("/26.2.json"));
        check(!vanilla.isEmpty(), "ZL6 父版本那份真 json 读到了");
        if (!vanilla.isEmpty()) {
            // 子 profile 照真 Forge 那份的形状造：有 id / mainClass / inheritsFrom，
            // 自己的 `arguments.game` 里**没有** quick play。
            const QJsonObject loader{
                {QStringLiteral("id"), QStringLiteral("26.2-forge-99.0.0")},
                {QStringLiteral("inheritsFrom"), QStringLiteral("26.2")},
                {QStringLiteral("mainClass"),
                 QStringLiteral("cpw.mods.bootstraplauncher.BootstrapLauncher")},
                {QStringLiteral("arguments"),
                 QJsonObject{{QStringLiteral("game"), QJsonArray{QStringLiteral("--fml.forgeVersion")}}}},
            };
            check(!QuickPlay::supports(loader, single),
                  "ZL6 子 profile 自己那一份 ⇒ 不认（quick play 不在它身上）");
            const QJsonObject merged = VersionInheritance::merge(loader, vanilla);
            check(QuickPlay::supports(merged, single) && QuickPlay::supports(merged, multi),
                  "ZL6 合并过的那一份两位都认 —— 这就是产品取的是合并后 json 的理由");
        }
    }

    section("ZL7 / ZL8. 那句话，以及这一对参数必须我们发");
    {
        const QString saySingle = QuickPlay::unsupportedText(single);
        const QString sayMulti = QuickPlay::unsupportedText(multi);
        check(!saySingle.isEmpty() && !sayMulti.isEmpty() && saySingle != sayMulti,
              "ZL7 两位各有一句人话，而且不是同一句");
        check(saySingle.contains(QuickPlay::argumentName(single)) &&
                  sayMulti.contains(QuickPlay::argumentName(multi)),
              "ZL7 那句话里点名了是哪一条参数");

        check(VersionArguments::isVanillaGameArgument(QuickPlay::argumentName(single)) &&
                  VersionArguments::isVanillaGameArgument(QuickPlay::argumentName(multi)),
              "ZL8 两位在 isVanillaGameArgument 名单里（json 自己声明的那条会被丢掉 ⇒ 必须我们发）");
    }

    section("ZL9. ★ 这一对参数**真的接进了命令行**（判据层写对了 ≠ 接上了）");
    // 上面 ZL1~ZL8 全在 `QuickPlay` 那个纯函数里 —— 它们全绿也**证明不了**命令行上真有
    // 那一条：`LauncherManager::generateJvmArguments()` 里少写一句 `args << …`，上面的
    // 断言一条都不会红（判据写好了、参数却没发出去正是这种形状）。
    // 所以这一节拿**真的那个函数**拼一次命令行。
    //
    // NOTE: 拿的是真 json 夹具（`meta/26.2.json`，真支持这两位），不是手搓的一份 ——
    // 手搓的会绕开"json 自己声明的那条在 isVanillaGameArgument 名单上、必须由我们发"
    // 那件事（见 ZL8），而那件事恰恰是这条接线存在的理由。
    {
        const QJsonObject vanilla = loadJson(metaDir + QStringLiteral("/26.2.json"));
        check(!vanilla.isEmpty(), "ZL9 前提：拿到那份真 json");
        if (!vanilla.isEmpty()) {
            LauncherManager mgr;
            EffectiveJavaSettings java;
            java.xms = 512;
            java.xmx = 2048;
            const QString assetIndex =
                vanilla.value(QStringLiteral("assetIndex")).toObject().value(QStringLiteral("id")).toString();
            // 同一份 json、同一套 Java 值，**只有最后一格不同** —— 这样"多出来的那两条"
            // 只可能来自 quick play 这一发。
            const auto commandLine = [&](const QuickPlay::Target &target) {
                return mgr.generateJvmArguments(QString(), QStringLiteral("26.2"), assetIndex,
                                                vanilla, java, /*offlineMode=*/true, 21,
                                                QStringList(), QStringLiteral("Tester"), QString(),
                                                GameWindowSettings(), ScreenWorkArea(), target);
            };

            const QStringList plain = commandLine(QuickPlay::Target());
            check(!plain.contains(QuickPlay::argumentName(single)) &&
                      !plain.contains(QuickPlay::argumentName(multi)),
                  "★ ZL9 没排这一发的时候，那两条**一条都不在命令行上**"
                  "（绝大多数启动的处境；json 自己声明的那条必须被丢掉）");

            // 存档：参数名后面**紧跟**的就是那个值（不是隔着别的 token）。
            const QString worldName = QStringLiteral("我的世界 A");
            const QStringList singleArgs = commandLine(
                QuickPlay::Target{single, worldName});
            const int atSingle = singleArgs.indexOf(QuickPlay::argumentName(single));
            check(atSingle >= 0 && atSingle + 1 < singleArgs.size() &&
                      singleArgs.at(atSingle + 1) == worldName,
                  "★★ ZL9 「加入选中的世界」真的接上了：`--quickPlaySingleplayer` 后面紧跟那个世界名",
                  QStringLiteral("下标 %1 / 共 %2 条 / 后一格「%3」")
                      .arg(atSingle)
                      .arg(singleArgs.size())
                      .arg(atSingle >= 0 && atSingle + 1 < singleArgs.size()
                               ? singleArgs.at(atSingle + 1)
                               : QStringLiteral("<没有>")));
            check(!singleArgs.contains(QuickPlay::argumentName(multi)),
                  "★ ZL9 单人那一发不该顺手把多人那条也带上（两位各算各的）");

            // 服务器：`host:port` 原样过去（游戏自己认得这个写法）。
            const QString address = QStringLiteral("mc.example.com:25566");
            const QStringList multiArgs =
                commandLine(QuickPlay::Target{multi, address});
            const int atMulti = multiArgs.indexOf(QuickPlay::argumentName(multi));
            check(atMulti >= 0 && atMulti + 1 < multiArgs.size() &&
                      multiArgs.at(atMulti + 1) == address,
                  "★★ ZL9 「加入服务器」真的接上了：`--quickPlayMultiplayer` 后面紧跟那个 host:port",
                  QStringLiteral("下标 %1 / 后一格「%2」")
                      .arg(atMulti)
                      .arg(atMulti >= 0 && atMulti + 1 < multiArgs.size()
                               ? multiArgs.at(atMulti + 1)
                               : QStringLiteral("<没有>")));
            check(multiArgs.size() == plain.size() + 2,
                  "★ ZL9 就多那两条，不多不少",
                  QStringLiteral("%1 → %2").arg(plain.size()).arg(multiArgs.size()));
        }
    }
}

