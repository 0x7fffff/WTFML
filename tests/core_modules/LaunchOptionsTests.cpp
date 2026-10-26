#include "Support.hpp"

void runLaunchOptionsTests(const QString &sandbox) {
    const QString metaDir = QStringLiteral(REPO_ROOT_PATH) + "/tests/meta";
    const QString settingsDir = sandbox + "/wtfml-launch-options-settings";
    QDir(settingsDir).removeRecursively();
    QDir().mkpath(settingsDir);

    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir);
    const QString iniPath = settingsDir + QStringLiteral("/WTFML/WTFML.ini");

    section("ZD1. 三项设置的默认值（全新沙盒）");
    {
        LauncherSettings fresh;
        check(!fresh.showGameLog(), "ZD1 默认不开「显示游戏日志」");
        // 854x480 是**原版自己的默认窗口大小**（1.12.2 的 Main 里那对 defaultsTo(854, 480)）。
        // 钉它是因为这条默认值承担着一个承诺：谁都不动这一项时，启动器发出去的两个参数
        // 就等于游戏本来要用的值 —— 默认这一档不该让任何人的窗口大小变一下。
        check(fresh.gameWindowWidth() == 854 && fresh.gameWindowHeight() == 480,
              "ZD1 默认窗口大小就是原版那个 854x480",
              QStringLiteral("%1x%2").arg(fresh.gameWindowWidth()).arg(fresh.gameWindowHeight()));
        check(fresh.processPriority() == ProcessPriority::Normal,
              "ZD1 默认优先级是 Normal（不是 High —— 猜高的代价是拖慢用户的机器）");
        // 顶上那行字：**空串是默认档**（= 条上那五个字母），不是"没有名字"。
        // 所以这里钉的是"全新沙盒读出来是空的" —— 里面要是被塞了个 "WTFML"，
        // 用户以后改名字就分不出"我改过"和"我没改过"了（条上那个默认值只有一个出处：
        // TitleBar::defaultBrandText()，不在这一层抄一份）。
        check(fresh.titleText().isEmpty(),
              "ZD1 默认没改过名字（空串 = 条上用默认那五个字母）",
              QStringLiteral("[%1]").arg(fresh.titleText()));
    }

    section("ZD2. 三项跨实例读得回来；手改 ini 也得兜住");
    {
        LauncherSettings settings;
        settings.setShowGameLog(true);
        settings.setGameWindowSize(1280, 720);
        settings.setProcessPriority(ProcessPriority::BelowNormal);
        settings.setTitleText(QStringLiteral("我的启动器"));

        // 换个实例读：验的是真落了盘，不是留在成员变量里
        LauncherSettings reopened;
        check(reopened.showGameLog(), "ZD2 「显示游戏日志」跨实例读得回来");
        check(reopened.gameWindowWidth() == 1280 && reopened.gameWindowHeight() == 720,
              "ZD2 窗口大小跨实例读得回来",
              QStringLiteral("%1x%2")
                  .arg(reopened.gameWindowWidth())
                  .arg(reopened.gameWindowHeight()));
        check(reopened.processPriority() == ProcessPriority::BelowNormal,
              "ZD2 优先级跨实例读得回来（不是退回 Normal —— 那会让用户以为设置没生效）");
        check(reopened.titleText() == QStringLiteral("我的启动器"),
              "ZD2 顶上那行字跨实例读得回来", reopened.titleText());

        // 这一项的两条整理（去空白 / 截长度）在 setter 和 load 两处都要做 —— 因为它会被
        // **画在条上**：带空格的" WTFML "看着就是没对齐，超长的名字会撞上中间那四颗导航
        // 按钮。下面三组：setter 侧、load 侧（手改 ini）。
        settings.setTitleText(QStringLiteral("   两头有空格   "));
        check(settings.titleText() == QStringLiteral("两头有空格"),
              "ZD2 setter 去掉两头的空白（名字是从条最左边开始画的）",
              QStringLiteral("[%1]").arg(settings.titleText()));
        settings.setTitleText(QString(40, QLatin1Char('W')));
        check(settings.titleText().size() == LauncherSettings::titleTextMaxLength(),
              "ZD2 setter 按上限截断",
              QStringLiteral("%1 个字").arg(settings.titleText().size()));
        settings.setTitleText(QStringLiteral("我的启动器"));

        // 存进去的值也可能是手改的，所以**读回来那一侧也要钳位**：这两个数会原样进游戏
        // 命令行，放一个 0 或者 99999 出去，游戏那边要么当没给、要么拿一个荒唐的窗口。
        {
            QSettings raw;
            raw.setValue(QStringLiteral("Launcher/WindowWidth"), 100);      // 低于下限
            raw.setValue(QStringLiteral("Launcher/WindowHeight"), 99999);  // 高于上限
            raw.setValue(QStringLiteral("Launcher/ProcessPriority"), QStringLiteral("turbo"));
            // 顶上那行字：手改成一个又长又带空格的名字（这条会原样画到条上，不能信）。
            raw.setValue(QStringLiteral("Launcher/TitleText"),
                         QStringLiteral("   ") + QString(40, QLatin1Char('X')) + QStringLiteral("  "));
            raw.sync();
        }
        LauncherSettings clamped;
        check(clamped.gameWindowWidth() == LauncherSettings::gameWindowMinWidth(),
              "ZD2 手改成过小的宽度后 load 会钳到下限",
              QString::number(clamped.gameWindowWidth()));
        check(clamped.gameWindowHeight() == LauncherSettings::gameWindowMaxHeight(),
              "ZD2 手改成过大的高度后 load 会钳到上限",
              QString::number(clamped.gameWindowHeight()));
        check(clamped.processPriority() == ProcessPriority::Normal,
              "ZD2 手改成认不出来的优先级后兜到 Normal");
        check(clamped.titleText() == QString(LauncherSettings::titleTextMaxLength(),
                                             QLatin1Char('X')),
              "ZD2 手改的名字读回来先去掉空白、再截到上限",
              QStringLiteral("[%1] %2 个字")
                  .arg(clamped.titleText())
                  .arg(clamped.titleText().size()));

        // setter 那一侧同样钳：界面上是 SpinBox 管着，但 setter 不能假设调用方是界面。
        settings.setGameWindowSize(1, 1);
        check(settings.gameWindowWidth() == LauncherSettings::gameWindowMinWidth() &&
                  settings.gameWindowHeight() == LauncherSettings::gameWindowMinHeight(),
              "ZD2 setter 也钳（1x1 → 下限那一对）",
              QStringLiteral("%1x%2")
                  .arg(settings.gameWindowWidth())
                  .arg(settings.gameWindowHeight()));
        settings.setGameWindowSize(99999, 99999);
        check(settings.gameWindowWidth() == LauncherSettings::gameWindowMaxWidth() &&
                  settings.gameWindowHeight() == LauncherSettings::gameWindowMaxHeight(),
              "ZD2 上限那一侧也钳",
              QStringLiteral("%1x%2")
                  .arg(settings.gameWindowWidth())
                  .arg(settings.gameWindowHeight()));

        check(QFileInfo::exists(iniPath), "ZD2 沙盒那份 ini 真的写出来了", iniPath);
    }

    section("ZD3. 优先级的持久化字符串：五个值往返 + 认不出来兜 Normal");
    {
        // 这几个串是**持久化格式的一部分**：改掉它们，用户盘上已经存好的值就会被当成
        // "认不出来"、静默退回 Normal —— 用户看到的是"设了高，重启又变回正常"。
        const QList<QPair<ProcessPriority, QString>> table = {
            {ProcessPriority::Low, QStringLiteral("low")},
            {ProcessPriority::BelowNormal, QStringLiteral("belowNormal")},
            {ProcessPriority::Normal, QStringLiteral("normal")},
            {ProcessPriority::AboveNormal, QStringLiteral("aboveNormal")},
            {ProcessPriority::High, QStringLiteral("high")},
        };
        QStringList seenKeys;
        for (const auto &row : table) {
            const QString key = LauncherSettings::processPriorityKey(row.first);
            check(key == row.second, QString("ZD3 %1 存进去就是 %2").arg(row.second, row.second),
                  key);
            check(LauncherSettings::processPriorityFromKey(row.second) == row.first,
                  QString("ZD3 %1 读得回来").arg(row.second));
            seenKeys << key;
        }
        // 「两两不同」这条是house rule：枚举到字符串这一层，两个用途共用一个持久化字符串时，
        // 症状是"看起来全绿、界面上却少了一个东西"。
        check(QSet<QString>(seenKeys.begin(), seenKeys.end()).size() == table.size(),
              "ZD3 五个持久化字符串两两不同", seenKeys.join(QStringLiteral(" | ")));

        // 认不出来的值：空串、大小写不对的、彻底没见过的。
        for (const QString &junk : {QString(), QStringLiteral("HIGH"),
                                    QStringLiteral("turbo"), QStringLiteral("0")}) {
            check(LauncherSettings::processPriorityFromKey(junk) == ProcessPriority::Normal,
                  QString("ZD3 认不出来的值（%1）兜到 Normal").arg(junk.isEmpty() ? QStringLiteral("<空>")
                                                                            : junk));
        }
    }

    section("ZD4. 窗口大小真的会进命令行（--width / --height）");
    {
        LauncherManager launcher;
        GlobalJavaSettings jvm;

        const auto countFlag = [](const QStringList &list, const QString &flag) {
            int n = 0;
            for (const QString &a : list) {
                if (a == flag) {
                    ++n;
                }
            }
            return n;
        };
        const auto valueOfFlag = [](const QStringList &list, const QString &flag) {
            const int i = list.indexOf(flag);
            return (i >= 0 && i + 1 < list.size()) ? list.at(i + 1) : QString();
        };

        const QJsonObject modern = loadJson(metaDir + "/1.19.json");
        check(!modern.isEmpty(), "ZD4 前提：读到了 1.19 那份夹具");

        // ---- (a) 真夹具（那条规则挂在 has_custom_resolution 上，我们传空 feature）----
        {
            // 前提也要钉：这条规则**在夹具里**。夹具哪天换了，红的应该是这一条，
            // 而不是让下面几条悄悄变成"在验一段没人走的代码"。
            bool hasRule = false;
            const QJsonArray game =
                modern.value(QStringLiteral("arguments")).toObject().value(QStringLiteral("game")).toArray();
            for (const QJsonValue &v : game) {
                const QJsonObject entry = v.toObject();
                if (!entry.value(QStringLiteral("rules")).toArray().isEmpty() &&
                    entry.value(QStringLiteral("value")).toArray().contains(
                        QStringLiteral("--width"))) {
                    hasRule = true;
                }
            }
            check(hasRule,
                  "ZD4 前提：1.19 夹具里确实有一条挂在规则上的 --width");
        }

        {
            const QStringList args = launcher.generateJvmArguments(
                QString(), QStringLiteral("1.19"), QStringLiteral("1"), modern, jvm, true, 17,
                QStringList(),                 QString(), QString(),
                GameWindowSettings{GameWindowMode::Windowed, 1280, 720});
            check(countFlag(args, QStringLiteral("--width")) == 1,
                  "ZD4 传了窗口大小 → 命令线上恰好一条 --width",
                  QString::number(countFlag(args, QStringLiteral("--width"))));
            check(countFlag(args, QStringLiteral("--height")) == 1,
                  "ZD4 --height 也恰好一条");
            check(valueOfFlag(args, QStringLiteral("--width")) == QStringLiteral("1280") &&
                      valueOfFlag(args, QStringLiteral("--height")) == QStringLiteral("720"),
                  "ZD4 两个值就是我们给的那一对",
                  valueOfFlag(args, QStringLiteral("--width")) + QStringLiteral("x") +
                      valueOfFlag(args, QStringLiteral("--height")));
        }

        // ---- (b) 没传窗口设置：发的是**原版默认那一对**（854×480）----
        //
        // NOTE: 默认构造就是原版那个默认窗口大小（854×480），所以默认那一趟是**照发**。
        // 两条都对用户是同一件事（游戏开出来还是那个 854×480），但"什么都不发"和"发一对
        // 854×480"在命令行上不是一回事 —— 而 json 自己带的
        // 那对 --width 只在 has_custom_resolution 那类规则成立时才出现，靠它兜底不
        // 可靠，所以这一档我们**明着发**。
        {
            const QStringList args = launcher.generateJvmArguments(
                QString(), QStringLiteral("1.19"), QStringLiteral("1"), modern, jvm, true, 17);
            check(valueOfFlag(args, QStringLiteral("--width")) == QStringLiteral("854") &&
                      valueOfFlag(args, QStringLiteral("--height")) == QStringLiteral("480"),
                  "ZD4 没传窗口设置 → 发的是原版默认那一对（854×480）",
                  valueOfFlag(args, QStringLiteral("--width")) + QStringLiteral("x") +
                      valueOfFlag(args, QStringLiteral("--height")));
        }

        // ---- (b2) 两格不是正数：一条都不发 ----
        //
        // 这是"调用方明确说不给"的那一档。发一个 0
        // 出去的后果是 joptsimple 抛异常、游戏根本起不来 —— 宁可不发。
        {
            const QStringList args = launcher.generateJvmArguments(
                QString(), QStringLiteral("1.19"), QStringLiteral("1"), modern, jvm, true, 17,
                QStringList(), QString(), QString(),
                GameWindowSettings{GameWindowMode::Windowed, 0, 0});
            check(countFlag(args, QStringLiteral("--width")) == 0 &&
                      countFlag(args, QStringLiteral("--height")) == 0,
                  "ZD4 调用方给的是 0×0 → 一条都不发（发一个 0 出去游戏起不来）");
        }

        // ---- (c) 把 json 那一对**写死成别的数字**：还是一条，而且是我们那条 ----
        // 这一条才是真正分得清"谁发的"的那条：json 那条真被丢掉的话，留下的值只可能是
        // 1280；它要是漏出来，值就是 640/480（或者两条 --width）。
        {
            QJsonArray game =
                modern.value(QStringLiteral("arguments")).toObject().value(QStringLiteral("game")).toArray();
            for (int i = 0; i < game.size(); ++i) {
                const QJsonObject entry = game.at(i).toObject();
                if (!entry.value(QStringLiteral("rules")).toArray().isEmpty() &&
                    entry.value(QStringLiteral("value")).toArray().contains(
                        QStringLiteral("--width"))) {
                    QJsonObject literal = entry;
                    QJsonArray literalValue;
                    literalValue << QStringLiteral("--width") << QStringLiteral("640")
                                 << QStringLiteral("--height") << QStringLiteral("480");
                    literal[QStringLiteral("value")] = literalValue;
                    game.replace(i, literal);
                }
            }
            QJsonObject arguments = modern.value(QStringLiteral("arguments")).toObject();
            arguments[QStringLiteral("game")] = game;
            QJsonObject literalJson = modern;
            literalJson[QStringLiteral("arguments")] = arguments;

            const QStringList args = launcher.generateJvmArguments(
                QString(), QStringLiteral("1.19"), QStringLiteral("1"), literalJson, jvm, true, 17,
                QStringList(),                 QString(), QString(),
                GameWindowSettings{GameWindowMode::Windowed, 1280, 720});
            check(countFlag(args, QStringLiteral("--width")) == 1,
                  "ZD4 json 里写死一对 --width 640 时，最终仍只有一条（同一个开关发两次 "
                  "joptsimple 会直接报错退出）",
                  QString::number(countFlag(args, QStringLiteral("--width"))));
            check(valueOfFlag(args, QStringLiteral("--width")) == QStringLiteral("1280"),
                  "ZD4 留下的是**我们**那条（json 里那条被 isVanillaGameArgument 丢掉了）",
                  valueOfFlag(args, QStringLiteral("--width")));
        }

        // ---- (d) 老格式（minecraftArguments）：1.12.2 那一代真机走的就是这条路 ----
        // 1.12.2 的 minecraftArguments 里压根没有 width/height，游戏自己的 main 从 1.6 起
        // 就认 --width/--height（那对 defaultsTo(854, 480) 就是默认窗口大小的出处）。
        {
            const QJsonObject legacy = loadJson(metaDir + "/1.12.2.json");
            const QString legacyArgs = legacy.value(QStringLiteral("minecraftArguments")).toString();
            check(!legacyArgs.isEmpty(),
                  "ZD4 前提：1.12.2 那份夹具确实走老格式的 minecraftArguments");
            // 这条前提才是下面那条断言的用处所在：这份 json **没有**声明窗口大小，所以
            // 命令线上那一对只可能是启动器自己发的（不是 json 那条漏出来了）。
            check(!legacyArgs.contains(QStringLiteral("--width")),
                  "ZD4 前提：而且它自己没声明 --width（那一条就只可能是我们发的）");

            const QStringList args = launcher.generateJvmArguments(
                QString(), QStringLiteral("1.12.2"), QStringLiteral("1"), legacy, jvm, true, 8,
                QStringList(),                 QString(), QString(),
                GameWindowSettings{GameWindowMode::Windowed, 1024, 768});
            check(countFlag(args, QStringLiteral("--width")) == 1 &&
                      valueOfFlag(args, QStringLiteral("--width")) == QStringLiteral("1024"),
                  "ZD4 老格式也发我们那一对",
                  valueOfFlag(args, QStringLiteral("--width")));
        }

        // ---- (e) 老格式且 json 自己带了一对：加进去的必须是**另一对数字** ----
        // 这是 contains("--width") 那个守卫的用处：json 里已经有的话就不能再加一遍。
        // 数字故意取得和我们要发的不同，这样"到底留下谁"是有区别的。
        {
            QJsonObject legacy = loadJson(metaDir + "/1.12.2.json");
            const QString base =
                legacy.value(QStringLiteral("minecraftArguments")).toString();
            legacy[QStringLiteral("minecraftArguments")] =
                base + QStringLiteral(" --width 1280 --height 720");

            const QStringList args = launcher.generateJvmArguments(
                QString(), QStringLiteral("1.12.2"), QStringLiteral("1"), legacy, jvm, true, 8,
                QStringList(),                 QString(), QString(),
                GameWindowSettings{GameWindowMode::Windowed, 1024, 768});
            check(countFlag(args, QStringLiteral("--width")) == 1,
                  "ZD4 json 自带 --width 时不会发第二遍（joptsimple 见重复选项直接退出）",
                  QString::number(countFlag(args, QStringLiteral("--width"))));
            check(valueOfFlag(args, QStringLiteral("--width")) == QStringLiteral("1024"),
                  "ZD4 而且留下的是我们那一对，不是 json 里那个 1280",
                  valueOfFlag(args, QStringLiteral("--width")));
        }

        // ---- (f) 那两个 resolution_* 占位符真的是填进去的 ----
        // LauncherManager 里填它们是为了**万一**有版本的 minecraftArguments 直接写着
        // `${resolution_width}`（那种拼法见过）：留一个没替换的 ${...} 在命令行上，
        // 游戏拿到的就是一个字面量的 "${resolution_width}"。
        //
        // 这里只能拿一个**我们自己编的**参数名来探（`--wtfmlProbe`）：原版那几个名字
        // （--width/--height）会被 isVanillaGameArgument 连同占位符一起丢掉，那条路正是
        // 上面 (e) 验的。也就是说这条探针验的是"占位符填进去了没有"，不是"原版那条会生效"。
        {
            QJsonObject legacy = loadJson(metaDir + "/1.12.2.json");
            const QString base =
                legacy.value(QStringLiteral("minecraftArguments")).toString();
            legacy[QStringLiteral("minecraftArguments")] =
                base + QStringLiteral(" --wtfmlProbe ${resolution_width}");

            const QStringList args = launcher.generateJvmArguments(
                QString(), QStringLiteral("1.12.2"), QStringLiteral("1"), legacy, jvm, true, 8,
                QStringList(),                 QString(), QString(),
                GameWindowSettings{GameWindowMode::Windowed, 1024, 768});
            const int probeIndex = args.indexOf(QStringLiteral("--wtfmlProbe"));
            check(probeIndex >= 0 && probeIndex + 1 < args.size() &&
                      args.at(probeIndex + 1) == QStringLiteral("1024"),
                  "ZD4 ${resolution_width} 被替换成了我们给的宽度",
                  probeIndex >= 0 && probeIndex + 1 < args.size()
                      ? args.at(probeIndex) + QLatin1Char(' ') + args.at(probeIndex + 1)
                      : QStringLiteral("<没有 --wtfmlProbe 这一条>"));
            // 顺带把"一个 ${...} 都不许漏"钉死：漏一个的症状是游戏拿到一句字面量。
            QString unresolved;
            for (const QString &a : args) {
                if (a.contains(QStringLiteral("${resolution_"))) {
                    unresolved = a;
                    break;
                }
            }
            check(unresolved.isEmpty(),
                  "ZD4 命令线上不留没替换的 ${resolution_*} 占位符", unresolved.left(160));
        }
    }

    section("ZD5. 窗口那三档：代号往返、合成、以及各档发什么参数");
    // ---------------------------------------------------------------------
    // 这一节全是**纯函数**（除了最后那几条 LauncherSettings 的持久化），所以断的是
    // 「哪一档发哪几个参数」这一条契约本身。它和 ZD4 的关系：ZD4 验的是"那些参数最后
    // 有没有真的拼进命令行、会不会和 json 自带的那条打架"，这一节验的是"该发哪几个"。
    {
        // ---- (a) 代号（持久化格式的一部分）往返 ----
        const GameWindowMode modes[] = {GameWindowMode::Windowed, GameWindowMode::Maximized,
                                        GameWindowMode::Fullscreen};
        bool roundTripOk = true;
        QStringList tokens;
        for (GameWindowMode mode : modes) {
            const QString token = gameWindowModeToken(mode);
            tokens << token;
            GameWindowMode back = GameWindowMode::Windowed;
            if (!gameWindowModeFromToken(token, &back) || back != mode) {
                roundTripOk = false;
            }
        }
        check(roundTripOk, "ZD5 三档的代号各自往返得回来", tokens.join(QStringLiteral(",")));
        check(tokens == QStringList({QStringLiteral("windowed"), QStringLiteral("maximized"),
                                     QStringLiteral("fullscreen")}),
              "ZD5 代号就是盘上约定那三个串（改掉它们等于让用户存好的档位静默退回默认）",
              tokens.join(QStringLiteral(",")));
        {
            // 认不出来时**一个字节都不许动 out** —— 调用方是靠"没动"来判断要不要退回兜底值
            // 的。写成"顺手把 out 设成小窗"也能过大多数断言，但那让调用方失去这个判断能力。
            GameWindowMode sentinel = GameWindowMode::Fullscreen;
            check(!gameWindowModeFromToken(QStringLiteral("最大窗口"), &sentinel) &&
                      sentinel == GameWindowMode::Fullscreen,
                  "ZD5 认不出来的代号返回 false，而且不碰 out（调用方自己给兜底）");
            check(!gameWindowModeFromToken(QString(), &sentinel),
                  "ZD5 空串也认不出来");
            check(!gameWindowModeFromToken(QStringLiteral("windowed"), nullptr),
                  "ZD5 out 传空指针时不炸、回 false");
        }

        // ---- (b) meta 里那三个键名（读侧写侧的唯一出处）----
        check(gameWindowModeMetaField() == QStringLiteral("windowMode") &&
                  gameWindowWidthMetaField() == QStringLiteral("windowWidth") &&
                  gameWindowHeightMetaField() == QStringLiteral("windowHeight"),
              "ZD5 meta 里那三个键名是约定的那一个",
              gameWindowModeMetaField() + QStringLiteral("/") + gameWindowWidthMetaField() +
                  QStringLiteral("/") + gameWindowHeightMetaField());

        // ---- (c) meta ↔ 覆盖层：读得出来、写得回去、垃圾当没写 ----
        {
            QJsonObject meta;
            meta[gameWindowModeMetaField()] = QStringLiteral("fullscreen");
            meta[gameWindowWidthMetaField()] = 1920;
            meta[gameWindowHeightMetaField()] = 1080;
            const GameWindowOverride read = gameWindowOverrideFromJson(meta);
            check(read.mode.has_value() && *read.mode == GameWindowMode::Fullscreen &&
                      read.width.value_or(0) == 1920 && read.height.value_or(0) == 1080,
                  "ZD5 三个键都写了 → 三项覆盖都读出来了");
            check(gameWindowOverrideToJson(read) == meta,
                  "ZD5 读出来再写回去，逐键相同（读侧写侧是同一个键名表）");

            // 一半写一半没写：没写的那一半必须是"跟随全局"，不是"0"。
            QJsonObject half;
            half[gameWindowWidthMetaField()] = 1024;
            const GameWindowOverride halfRead = gameWindowOverrideFromJson(half);
            check(!halfRead.mode.has_value(),
                  "ZD5 只写了宽 → 模式那一项没有值（= 跟随全局）");
            check(halfRead.width.has_value() && *halfRead.width == 1024 &&
                      !halfRead.height.has_value(),
                  "ZD5 只写了宽 → 高那一项也没有值（不会被当成 0）");

            // 类型不对/认不出来的，一律当没写。
            QJsonObject junk;
            junk[gameWindowModeMetaField()] = QStringLiteral("最大窗口");
            junk[gameWindowWidthMetaField()] = QStringLiteral("1920");  // 字符串不是数
            junk[gameWindowHeightMetaField()] = 1080.5;                 // 半个像素
            const GameWindowOverride junkRead = gameWindowOverrideFromJson(junk);
            check(!junkRead.mode.has_value() && !junkRead.width.has_value() &&
                      !junkRead.height.has_value(),
                  "ZD5 认不出来的档位 / 字符串宽度 / 小数高度，三样都当没写");

            // 没值的那几项写成 Null（= 删键），这正是「跟随全局」要的结果。
            const QJsonObject empty = gameWindowOverrideToJson(GameWindowOverride());
            check(empty.value(gameWindowModeMetaField()).isNull() &&
                      empty.value(gameWindowWidthMetaField()).isNull() &&
                      empty.value(gameWindowHeightMetaField()).isNull(),
                  "ZD5 一个覆盖都没有时三格都是 Null（updateVersionMeta 把它当删键）");
        }

        // ---- (d) 合成：覆盖压全局，缺的那一项跟随 ----
        {
            GameWindowSettings global;
            global.mode = GameWindowMode::Windowed;
            global.width = 854;
            global.height = 480;

            const GameWindowSettings untouched = resolveGameWindow(global, GameWindowOverride());
            check(untouched.mode == GameWindowMode::Windowed && untouched.width == 854 &&
                      untouched.height == 480,
                  "ZD5 一个键都不覆盖 → 逐项等于全局（加这一档之前的行为）");

            GameWindowOverride partial;
            partial.mode = GameWindowMode::Fullscreen;
            partial.width = 1920;  // 高度故意不给
            const GameWindowSettings merged = resolveGameWindow(global, partial);
            check(merged.mode == GameWindowMode::Fullscreen && merged.width == 1920 &&
                      merged.height == 480,
                  "ZD5 覆盖了模式和宽 → 那两项用覆盖、没覆盖的高跟随全局",
                  QStringLiteral("%1x%2").arg(merged.width).arg(merged.height));

            // 覆盖里的数**要钳位**（meta 是手写的文件，这两个数会原样进命令行）。
            GameWindowOverride wild;
            wild.width = -1920;
            wild.height = 999999;
            const GameWindowSettings clamped = resolveGameWindow(global, wild);
            check(clamped.width == LauncherSettings::gameWindowMinWidth() &&
                      clamped.height == LauncherSettings::gameWindowMaxHeight(),
                  "ZD5 覆盖里的负数/天文数字被钳进合法区间（那两个数会原样进命令行）",
                  QStringLiteral("%1x%2").arg(clamped.width).arg(clamped.height));
        }

        // ---- (e) 各档发什么参数 ----
        {
            const ScreenWorkArea screen{1920, 1040};

            const QStringList windowed = gameWindowArguments(
                GameWindowSettings{GameWindowMode::Windowed, 1280, 720}, screen);
            check(windowed == QStringList({QStringLiteral("--width"), QStringLiteral("1280"),
                                           QStringLiteral("--height"), QStringLiteral("720")}),
                  "ZD5 小窗：只发 --width/--height，不发 --fullscreen",
                  windowed.join(QStringLiteral(" ")));

            const QStringList fullscreen = gameWindowArguments(
                GameWindowSettings{GameWindowMode::Fullscreen, 1280, 720}, screen);
            check(fullscreen.size() == 5 && fullscreen.first() == QStringLiteral("--fullscreen"),
                  "ZD5 全屏：--fullscreen 排在最前面（原版真参数）",
                  fullscreen.join(QStringLiteral(" ")));
            check(fullscreen.contains(QStringLiteral("--width")) &&
                      fullscreen.contains(QStringLiteral("1280")),
                  "ZD5 全屏**照样**发那一对大小（那是 F11 退出全屏后回到多大）",
                  fullscreen.join(QStringLiteral(" ")));

            // 最大化：没有屏幕可量（<= 0 是"调用方没给"的哨兵）时退回用户那两格 ——
            // 宁可开一个用户填过大小的窗口，也不要因为取不到屏幕就发一对 0 出去。
            const QStringList noScreen = gameWindowArguments(
                GameWindowSettings{GameWindowMode::Maximized, 1280, 720}, ScreenWorkArea());
            check(noScreen == QStringList({QStringLiteral("--width"), QStringLiteral("1280"),
                                           QStringLiteral("--height"),
                                           QStringLiteral("720")}),
                  "ZD5 最大化但拿不到屏幕：退回用户填的那一对（不发 0 出去）",
                  noScreen.join(QStringLiteral(" ")));
            check(!noScreen.contains(QStringLiteral("--fullscreen")),
                  "ZD5 最大化**不是**全屏（原版没有最大化参数，见 .hpp 那一节）");

            // 两格不是正数时一条都不发（约定）。
            check(gameWindowArguments(GameWindowSettings{GameWindowMode::Windowed, 0, 0}, screen)
                      .isEmpty(),
                  "ZD5 宽高是 0 → 一条都不发（宁可不发，也不发一个 0）");
            const QStringList onlyFullscreen = gameWindowArguments(
                GameWindowSettings{GameWindowMode::Fullscreen, 0, 0}, screen);
            check(onlyFullscreen == QStringList({QStringLiteral("--fullscreen")}),
                  "ZD5 全屏 + 宽高为 0：只剩 --fullscreen 一条（全屏那一档本身不靠那两个数）",
                  onlyFullscreen.join(QStringLiteral(" ")));
        }

        // ---- (f) 「最大化」要照屏幕工作区算：量不出边框就原样 ----
        {
            const ScreenWorkArea same = maximizedContentSize(ScreenWorkArea{1920, 1040});
            check(same.width <= 1920 && same.width > 0 && same.height <= 1040 && same.height > 0,
                  "ZD5 量不出边框时最多退回工作区原样（不会变成 0 或者负数）",
                  QStringLiteral("%1x%2").arg(same.width).arg(same.height));
            const ScreenWorkArea none = maximizedContentSize(ScreenWorkArea());
            check(none.width == 0 && none.height == 0,
                  "ZD5 没给工作区（哨兵 0）→ 原样返回 0，调用方据此退回用户那两格");
        }

        // ---- (g) 这一档设置存得下来（和 ZD2 那三项同一套）----
        {
            // 先把盘上那份清成默认：这一节排在 ZD2/ZD3 之后，而那两节已经写过别的值了。
            LauncherSettings settings;
            settings.setGameWindowMode(GameWindowMode::Maximized);
            settings.setGameWindowSize(1600, 900);
            LauncherSettings reopened;
            check(reopened.gameWindowMode() == GameWindowMode::Maximized,
                  "ZD5 窗口模式跨实例读得回来");
            check(reopened.gameWindowSettings().mode == GameWindowMode::Maximized &&
                      reopened.gameWindowSettings().width == 1600 &&
                      reopened.gameWindowSettings().height == 900,
                  "ZD5 gameWindowSettings() 把模式和分辨率捆成一份给出来");
            // 手改 ini 成一个认不出来的值 ⇒ 退回默认档（小窗），不猜也不报错。
            {
                QSettings ini(iniPath, QSettings::IniFormat);
                ini.setValue(QStringLiteral("Launcher/WindowMode"), QStringLiteral("最大窗口"));
                ini.sync();
            }
            LauncherSettings afterEdit;
            check(afterEdit.gameWindowMode() == GameWindowMode::Windowed,
                  "ZD5 盘上那个串认不出来 → 退回默认档（小窗 = 加这一档之前的行为）");
        }
    }

    // ---------- ZD6. 两份启动器共用同一份全局设置：各改各的，互不覆盖 ----------
    //
    // 场景（计划里 P0-2 那一半）：装在两个目录的启动器各有自己的数据目录、可以同时开着，
    // 但 `HKCU\Software\WTFML\WTFML` 是同一份。两边启动时各读了一份快照，之后甲只改标题、
    // 乙只改「显示游戏日志」。以前 save() 整份写回 ⇒ 谁后存谁赢，另一项被打回启动时读到
    // 的旧值（用户看到的是"我改的那份没了"）。这里用两个对象造出"两个进程各持一份快照"，
    // 逐项验"对方没动的那一项还在"——两个方向都验，不能只验先写的那一份。
    //
    // NOTE: 这一节靠的是 `LauncherSettings::save()` 只写与 load() 快照不同的项；
    // 整份写回的话下面第一条就会红（probe 念出来是空串）。
    section("ZD6. 两份启动器共用全局设置：各改各的，谁也不把对方刚改的那一项打回去");
    {
        // 先把这一节用到的那两项摆回默认（前面的节已经写过别的值）。
        {
            LauncherSettings reset;
            reset.setTitleText(QString());
            reset.setShowGameLog(false);
        }

        // 甲、乙都在**任何一次保存之前**读盘 —— 这是整个场景的关键：两份快照都已经
        // 在内存里了，谁也看不见对方之后改的东西。
        LauncherSettings first;    // 第一个安装目录里那份启动器
        LauncherSettings second;   // 第二个安装目录里那份启动器

        first.setTitleText(QStringLiteral("甲改的标题"));
        second.setShowGameLog(true);

        LauncherSettings probe;
        check(probe.titleText() == QStringLiteral("甲改的标题"),
              "★★ ZD6 乙保存它那一项时，没有把甲刚改的标题打回去",
              QStringLiteral("盘上标题=[%1]").arg(probe.titleText()));
        check(probe.showGameLog(),
              "★★ ZD6 甲保存时也没有把乙那一项打回去（两条对称）");

        // 反方向再来一次：甲改另一项、乙改标题。
        first.setProcessPriority(ProcessPriority::High);
        second.setTitleText(QStringLiteral("乙改的标题"));
        LauncherSettings probe2;
        check(probe2.processPriority() == ProcessPriority::High &&
                  probe2.titleText() == QStringLiteral("乙改的标题"),
              "★★ ZD6 反方向同样互不覆盖",
              QStringLiteral("优先级=%1 标题=[%2]")
                  .arg(static_cast<int>(probe2.processPriority()))
                  .arg(probe2.titleText()));

        // 收尾：这一节写过的三项也摆回默认，别给后面的节留值。
        LauncherSettings cleanup;
        cleanup.setTitleText(QString());
        cleanup.setShowGameLog(false);
        cleanup.setProcessPriority(ProcessPriority::Normal);
    }

    section("ZD server favorites: independent persistence and endpoint identity");
    {
        LauncherSettings settings;
        check(settings.setServerFavorite(QStringLiteral(" Play.Example.COM "), true), "favorite address saves");
        LauncherSettings reopened;
        check(reopened.isServerFavorite(QStringLiteral("play.example.com")), "favorite survives settings reopen and host case changes");
        check(!reopened.isServerFavorite(QStringLiteral("play.example.com:25565")), "explicit default port retains distinct SRV semantics");
        check(settings.setServerFavorite(QStringLiteral("[2001:0db8:0:0:0:0:0:1]:25566"), true) &&
              settings.isServerFavorite(QStringLiteral("[2001:db8::1]:25566")), "IPv6 notation variants share favorite identity");
        check(!settings.setServerFavorite(QStringLiteral("example.com:0"), true) &&
              !settings.isServerFavorite(QString()), "invalid endpoints cannot become favorites");
        check(!settings.setServerFavorite(QStringLiteral("example.com:"), true) &&
              !settings.setServerFavorite(QStringLiteral("bad host"), true), "malformed address is not silently normalized into a favorite");
        check(settings.setServerFavorite(QStringLiteral("PLAY.EXAMPLE.COM"), false) &&
              settings.setServerFavorite(QStringLiteral("[2001:db8::1]:25566"), false), "favorites can be removed independently");
        LauncherSettings cleared;
        check(!cleared.isServerFavorite(QStringLiteral("play.example.com")) &&
              !cleared.isServerFavorite(QStringLiteral("[2001:db8::1]:25566")), "favorite removal survives reopen");
    }
    {
        const QString blocked = settingsDir + QStringLiteral("/blocked");
        QDir().mkpath(blocked + QStringLiteral("/WTFML/WTFML.ini"));
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, blocked);
        LauncherSettings settings;
        check(!settings.setServerFavorite(QStringLiteral("write-failure.example.com"), true) &&
              !settings.isServerFavorite(QStringLiteral("write-failure.example.com")),
              "failed favorite persistence is rejected without claiming saved state");
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir);
    }

    // 收摊：换回原生形态并把沙盒删掉 —— 后面那些节读的还是真实注册表。
    QSettings::setDefaultFormat(QSettings::NativeFormat);
    QDir(settingsDir).removeRecursively();
}

