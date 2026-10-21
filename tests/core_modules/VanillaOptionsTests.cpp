#include "Support.hpp"

void runVanillaOptionsTests() {
    section("PB. 原版设置：options.txt 保序 / 未知行不许丢 / 按键冲突");

    // 一份"从用户那儿来的"预设底稿：里面**故意**混了四种东西 —— 基础设置、两条抢同一个键
    // 的标准按键、硬件相关项（默认不选）、还有我们认不出来的（`resourcePacks` 归预设那一块、
    // `lastServer` 是状态不是设置、模组写的那条谁也不知道是什么）。
    const QByteArray sourceBytes = QByteArrayLiteral(
        "fov:0.5\n"
        "renderDistance:8\n"
        "lang:zh_cn\n"
        "guiScale:3\n"
        "fullscreen:true\n"
        "key_forward:key.keyboard.w\n"
        "key_sneak:key.keyboard.w\n"
        "key_jump:key.keyboard.space\n"
        "resourcePacks:[\"vanilla\"]\n"
        "unknownModOption:42\n"
        "lastServer:127.0.0.1\n");
    // 目标实例此刻的那一份：**行序和上面完全不同**（真人手上的 file 就是这样），里面还有
    // 目标版本才有的新键、一行手改的、以及一条 `key_*`。
    const QByteArray targetBytes = QByteArrayLiteral(
        "key_forward:key.keyboard.up\n"
        "key_back:key.keyboard.s\n"
        "key_sprint:key.keyboard.space\n"
        "fov:1.0\n"
        "renderDistance:12\n"
        "fullscreenResolution:1920x1080@60:24\n"
        "resourcePacks:[\"file/x.zip\"]\n"
        "lastServer:192.168.0.9\n"
        "tutorialStep:none\n"
        "# 手改过的一行（没有冒号）\n"
        "someNewOptionFromFutureVersion:yes\n");

    const Presets::OptionsFile source = Presets::parseOptions(sourceBytes);
    const Presets::OptionsFile target = Presets::parseOptions(targetBytes);

    // ---------- PB0. 原地往返：这一层的地基 ----------
    check(source.toBytes() == sourceBytes,
          "★★ PB0 读进来再写出去**逐字节相同** —— 这就是这一层的门槛（行序、别人的行、"
          "值里那第二个冒号，一个都不许动）");
    check(target.toBytes() == targetBytes, "★★ PB0 目标那一份也一样");
    {
        // 手改过的文件长什么样都得原样过：CRLF、最后一行没有换行、空行、`:` 开头的空键行、
        // 以及一段**不是 UTF-8** 的字节（老版本 Java 在中文系统上写过平台编码）。
        QByteArray weird;
        weird += "fov:0.0\r\n";                                        // CRLF
        weird += "\n";                                                 // 空行
        weird += ":没有键的那一行\n";                                  // 冒号在第一位
        weird += "soundDevice:";
        weird.append('\xd1');                                          // 非 UTF-8 的两个字节
        weird.append('\xd2');                                          // （老版本 Java 在
        weird += "\n";                                                 //   中文系统上写过）
        weird += "lastLineNoNewline:1";                                // 最后一行没有换行
        const Presets::OptionsFile round = Presets::parseOptions(weird);
        check(round.toBytes() == weird,
              "★★ PB0 手改过的那一份（CRLF + 空行 + 空键行 + 非 UTF-8 字节 + 末尾没有换行）"
              "也逐字节往返 —— 没被改过的行写回去的就是**原来的字节**");
        check(Presets::parseOptions(QByteArray()).toBytes().isEmpty(), "PB0 空文件一样过");
    }

    // ---------- PB1. 同一个键出现两次：最后一条说了算（游戏是 put） ----------
    {
        const QByteArray twice = QByteArrayLiteral("fov:0.1\nfov:0.9\nrenderDistance:4\n");
        Presets::OptionsFile file = Presets::parseOptions(twice);
        check(file.value(QStringLiteral("fov")) == QStringLiteral("0.9"),
              "★★ PB1 重复的键**最后一条说了算**（游戏读它的时候是往表里 put，后写盖前写）",
              file.value(QStringLiteral("fov")));
        check(file.lastIndexOfKey(QStringLiteral("fov")) == 1,
              "PB1 那个下标也是最后一条", QString::number(file.lastIndexOfKey(QStringLiteral("fov"))));
        file.setValue(QStringLiteral("fov"), QStringLiteral("0.5"));
        const QByteArray after = file.toBytes();
        check(after == QByteArrayLiteral("fov:0.1\nfov:0.5\nrenderDistance:4\n"),
              "★★ PB1 改的是**最后那一条**（改前面那条等于白改：游戏看不到），前面那条"
              "原样留着（我们不知道它为什么在那儿）",
              QString::fromUtf8(after));
    }

    // ---------- PB2. 认哪些键、哪一档默认勾 ----------
    check(Presets::optionsFieldOf(QStringLiteral("key_forward")) == Presets::OptionsField::Hotkey,
          "PB2 `key_*` 是标准按键那一档");
    check(Presets::optionsFieldOf(QStringLiteral("fov")) == Presets::OptionsField::Game &&
              Presets::optionsFieldOf(QStringLiteral("soundCategory_music")) ==
                  Presets::OptionsField::Game &&
              Presets::optionsFieldOf(QStringLiteral("modelPart_cape")) ==
                  Presets::OptionsField::Game,
          "PB2 基础设置那一档认得出来（含 `soundCategory_*` / `modelPart_*` 两个**前缀家族**"
          "—— 它们的尾巴是游戏自己排的枚举，列不完）");
    check(Presets::optionsFieldOf(QStringLiteral("fullscreenResolution")) ==
                  Presets::OptionsField::Hardware &&
              Presets::optionsFieldOf(QStringLiteral("guiScale")) ==
                  Presets::OptionsField::Hardware &&
              Presets::optionsFieldOf(QStringLiteral("touchscreen")) ==
                  Presets::OptionsField::Hardware,
          "PB2 硬件相关项（点名的「显示器、全屏分辨率」那几条）");
    check(Presets::optionsFieldOf(QStringLiteral("resourcePacks")) ==
                  Presets::OptionsField::Unknown &&
              Presets::optionsFieldOf(QStringLiteral("lastServer")) ==
                  Presets::OptionsField::Unknown &&
              Presets::optionsFieldOf(QStringLiteral("someNewOptionFromFutureVersion")) ==
                  Presets::OptionsField::Unknown &&
              Presets::optionsFieldOf(QString()) == Presets::OptionsField::Unknown,
          "PB2 认不出来的那一档：资源包、`lastServer`（是状态不是设置）、"
          "别的版本写的、空键");
    check(Presets::optionsDefaultSelected(QStringLiteral("fov")) &&
              Presets::optionsDefaultSelected(QStringLiteral("key_forward")),
          "PB2 基础设置与标准按键**默认勾**（「分组选择」的默认档）");
    check(!Presets::optionsDefaultSelected(QStringLiteral("guiScale")) &&
              !Presets::optionsDefaultSelected(QStringLiteral("fullscreenResolution")) &&
              !Presets::optionsDefaultSelected(QStringLiteral("resourcePacks")) &&
              !Presets::optionsDefaultSelected(QStringLiteral("someNewOptionFromFutureVersion")),
          "★★ PB2 硬件相关项**默认不选**（点名），认不出来的也不选 —— 不认识的键的"
          "正确值只有用户知道，替他搬过去是拿猜出来的值改他的实例");
    check(Presets::optionsFieldKey(Presets::OptionsField::Game) == QStringLiteral("game") &&
              Presets::optionsFieldKey(Presets::OptionsField::Hotkey) == QStringLiteral("hotkey") &&
              Presets::optionsFieldKey(Presets::OptionsField::Hardware) ==
                  QStringLiteral("hardware") &&
              Presets::optionsFieldKey(Presets::OptionsField::Unknown) ==
                  QStringLiteral("unknown"),
          "PB2 每一档都有一个稳定的落盘/翻译键（中文名归 ui/ 那一层）");

    // ---------- PB3. 分组与默认勾选 ----------
    {
        const QList<Presets::OptionsEntryGroup> groups = Presets::groupOptions(source);
        check(groups.size() == 4 && groups.at(0).field == Presets::OptionsField::Game &&
                  groups.at(1).field == Presets::OptionsField::Hotkey &&
                  groups.at(2).field == Presets::OptionsField::Hardware &&
                  groups.at(3).field == Presets::OptionsField::Unknown,
              "PB3 四组，顺序固定（基础设置 / 标准按键 / 硬件相关 / 认不出来的）");
        QStringList counts;
        for (const Presets::OptionsEntryGroup &group : groups) {
            counts << QString::number(group.lines.size());
        }
        check(counts == QStringList({QStringLiteral("3"), QStringLiteral("3"),
                                     QStringLiteral("2"), QStringLiteral("3")}),
              "PB3 每一组里的条数（基础设置 3 / 按键 3 / 硬件 2 / 认不出 3）",
              counts.join(QLatin1Char('/')));

        const QSet<QString> selected = Presets::defaultSelection(source);
        QStringList picked;
        for (const QString &key : source.keys()) {
            if (selected.contains(key)) {
                picked << key;
            }
        }
        check(picked == QStringList({QStringLiteral("fov"), QStringLiteral("renderDistance"),
                                     QStringLiteral("lang"), QStringLiteral("key_forward"),
                                     QStringLiteral("key_sneak"), QStringLiteral("key_jump")}),
              "★★ PB3 默认勾上的就是那六个（硬件那两条、`resourcePacks`、模组那条、"
              "`lastServer` 一个都不在里面）—— 顺序照文件里的行序",
              picked.join(QLatin1Char('/')));
    }

    // ---------- PB4. 段：拆成"游戏设置"和"原版按键"两段 ----------
    {
        const QSet<QString> selected = Presets::defaultSelection(source);
        const QList<Presets::PresetSection> sections = Presets::sectionsFromOptions(source, selected);
        check(sections.size() == 2 &&
                  sections.at(0).kind == Presets::SectionKind::VanillaOptions &&
                  sections.at(1).kind == Presets::SectionKind::VanillaKeys,
              "PB4 拆成两段（「基础设置与 `key_` 按键分组选择」）");
        check(sections.at(0).items.size() == 3 && sections.at(1).items.size() == 3,
              "PB4 设置那一半 3 项、按键那一半 3 项（`guiScale` / `fullscreen` 默认没勾 ⇒ "
              "一条都不进段）",
              QStringLiteral("%1 / %2")
                  .arg(sections.at(0).items.size())
                  .arg(sections.at(1).items.size()));
        check(sections.at(0).mode == Presets::ApplyMode::Merge &&
                  sections.at(1).mode == Presets::ApplyMode::Merge,
              "★★ PB4 两段的默认档都是**合并**（「默认 Merge，Replace 必须明确确认」"
              "—— 覆盖档是用户在界面上点头之后才写进这一段的）");
        const QJsonObject first = sections.at(1).items.at(0).toObject();
        check(first.value(QStringLiteral("key")).toString() == QStringLiteral("key_forward") &&
                  first.value(QStringLiteral("value")).toString() ==
                      QStringLiteral("key.keyboard.w"),
              "PB4 载荷形状 `{\"key\":…,\"value\":…}` 且**保序**（第一条就是文件里第一条）");
        check(!sections.at(0).id.isEmpty() && !sections.at(1).id.isEmpty() &&
                  sections.at(0).id != sections.at(1).id,
              "PB4 两段各自带一个段 id", sections.at(0).id + QLatin1Char('/') + sections.at(1).id);
        check(Presets::optionsSectionId(Presets::SectionKind::VanillaOptions) == sections.at(0).id &&
                  Presets::optionsSectionId(Presets::SectionKind::VanillaKeys) == sections.at(1).id,
              "PB4 段 id 是**由段种类推出来的**（两次调用、两次读到的一样：冲突提示和"
              "「这一段是同一个东西」那个判断才有落脚点）");
        check(sections.at(0).id ==
                  Presets::optionsSectionId(Presets::SectionKind::VanillaOptions),
              "PB4 （再念一次，防止有人把它改成现搓的 UUID）");

        // 读回：两份段 → 一份"值"的文件（应用那一层的第一步）。
        bool readOk = false;
        const Presets::OptionsFile back = Presets::optionsFromSections(sections, &readOk);
        check(readOk && back.value(QStringLiteral("fov")) == QStringLiteral("0.5") &&
                  back.value(QStringLiteral("key_jump")) == QStringLiteral("key.keyboard.space"),
              "PB4 从这两段里读得回那六个值（`fov` / 按键都对得上）");

        // 坏掉的载荷：三条来路各自一条断言（读不回来的段不许拿去改用户的实例）。
        const auto brokenSection = [](const QByteArray &items) {
            Presets::PresetSection section;
            section.kind = Presets::SectionKind::VanillaOptions;
            section.items = QJsonDocument::fromJson(items).array();
            return QList<Presets::PresetSection>({section});
        };
        bool badOk = true;
        Presets::optionsFromSections(brokenSection(QByteArrayLiteral("[42]")), &badOk);
        check(!badOk, "★★ PB4 项不是对象 ⇒ 读不回来（不许拿去改用户的实例）");
        badOk = true;
        Presets::optionsFromSections(brokenSection(QByteArrayLiteral("[{\"key\":\"fov\"}]")),
                                     &badOk);
        check(!badOk,
              "★★ PB4 缺 `value` **不算空串** —— 这一份被谁动过（「区分缺失、`null` 与"
              "错误类型」）");
        badOk = true;
        Presets::optionsFromSections(
            brokenSection(QByteArrayLiteral("[{\"key\":\"\",\"value\":\"1\"}]")), &badOk);
        check(!badOk, "★★ PB4 空键也算读不回来");
        badOk = false;
        Presets::optionsFromSections(
            brokenSection(QByteArrayLiteral("[{\"key\":\"fov\",\"value\":\"0.5\"}]")), &badOk);
        check(badOk, "PB4 对照：好端端的那一条读得回来");
    }

    // ---------- PB5. 空值照样进段（"这一趟就是要把它清空"） ----------
    {
        const QByteArray unbound = QByteArrayLiteral("key_sneak:\nfov:0.5\n");
        const Presets::OptionsFile file = Presets::parseOptions(unbound);
        const QSet<QString> selected = Presets::defaultSelection(file);
        const QList<Presets::PresetSection> sections = Presets::sectionsFromOptions(file, selected);
        const QJsonObject item = sections.at(1).items.at(0).toObject();
        check(sections.at(1).items.size() == 1 &&
                  item.value(QStringLiteral("value")).toString().isEmpty(),
              "★★ PB5 空绑定（用户自己把它清成「未绑定」的那种）**照样写进段里** —— 「我这一趟"
              "要把它清空」和「我没有这一项」是两件事");
    }

    // ---------- PB6. 按键冲突：精确重复 ----------
    {
        const QSet<QString> selected = Presets::defaultSelection(source);
        const QList<Presets::OptionsConflict> conflicts =
            Presets::optionsConflicts(source, target, selected);
        check(conflicts.size() == 2,
              "★★ PB6 报**两条**：① 预设自己有两条抢同一个键（`key_forward` / `key_sneak` "
              "都是 `key.keyboard.w`）；② 预设的 `key_jump` 要空格，而目标上的 `key_sprint` "
              "此刻正绑着空格 —— 后者就是真机上「装完预设发现跳跃没了」的来路",
              QString::number(conflicts.size()));
        if (conflicts.size() == 2) {
            // 定序按绑定串（`space` < `w`）：界面上的行不会每次刷新都换位置。
            const Presets::OptionsConflict &space = conflicts.at(0);
            const Presets::OptionsConflict &w = conflicts.at(1);
            check(space.binding == QStringLiteral("key.keyboard.space") &&
                      space.sourceKeys == QStringList({QStringLiteral("key_jump")}) &&
                      space.targetKeys == QStringList({QStringLiteral("key_sprint")}) &&
                      space.claimedBy() == 2,
                  "PB6 空格那一条：预设这一侧是 `key_jump`、目标那一侧是 `key_sprint`",
                  space.sourceKeys.join(QLatin1Char('+')) + QLatin1Char('@') + space.binding);
            check(w.binding == QStringLiteral("key.keyboard.w") &&
                      w.sourceKeys == QStringList({QStringLiteral("key_forward"),
                                                   QStringLiteral("key_sneak")}) &&
                      w.targetKeys.isEmpty() && w.claimedBy() == 2,
                  "★★ PB6 而**目标上会被预设改掉的那条不算进目标侧**（`key_forward` 目标上"
                  "现在绑的是 up，预设这一趟会把它改成 w）—— 算进来的话等于报了一个应用完"
                  "就不存在的冲突",
                  w.sourceKeys.join(QLatin1Char('+')) + QLatin1Char('@') + w.binding);
        }

        // 精确 = 逐字节相同：两个都带 `shift` 的不同键不算冲突。
        const QByteArray nearMiss = QByteArrayLiteral("key_a:key.keyboard.left.shift\n"
                                                      "key_b:key.keyboard.right.shift\n");
        const Presets::OptionsFile nearFile = Presets::parseOptions(nearMiss);
        check(Presets::optionsConflicts(nearFile, Presets::OptionsFile(),
                                        Presets::defaultSelection(nearFile))
                  .isEmpty(),
              "★★ PB6 「精确重复」是**逐字节相同**：`left.shift` 和 `right.shift` 里都带着 "
              "`shift`，但它们是两个键 —— 真要做「物理上是不是同一个键」的判断得先会有修饰键"
              "的键位表，而那件事没有唯一答案");

        // 空绑定不算冲突（未绑定可以有一堆）。
        const QByteArray twoFree = QByteArrayLiteral("key_a:\nkey_b:\n");
        const Presets::OptionsFile freeFile = Presets::parseOptions(twoFree);
        check(Presets::optionsConflicts(freeFile, Presets::OptionsFile(),
                                        Presets::defaultSelection(freeFile))
                  .isEmpty(),
              "PB6 两条都空着（用户清掉的）不算冲突");

        // 目标上原来就重着的、预设不牵的那些：不是这一趟弄出来的，不许记到我们头上。
        const QByteArray targetDup = QByteArrayLiteral("key_x:key.keyboard.q\n"
                                                       "key_y:key.keyboard.q\n");
        check(Presets::optionsConflicts(source, Presets::parseOptions(targetDup),
                                        Presets::defaultSelection(source))
                  .size() == 1,
              "★★ PB6 目标上原来就重着的（`key_x` / `key_y` 都绑 q）、而预设一个字都没提它们"
              "⇒ 还是只报预设自己那一条（报出来只会让用户以为是我们干的）");
    }

    // ---------- PB7. 计划：合并 ----------
    const QSet<QString> selected = Presets::defaultSelection(source);
    {
        const Presets::OptionsApplyReport report =
            Presets::planOptionsApply(Presets::SectionKind::VanillaOptions, source, target,
                                      selected, Presets::ApplyMode::Merge);
        check(report.ok && !report.needsConfirm,
              "PB7 合并档不用确认", report.problem);
        check(report.written == 3 && report.added == 1,
              "PB7 计划里说得清要写几项、其中几项是**新加的**（`lang` 目标上没有 ⇒ 1 项要"
              "追加）",
              QStringLiteral("写 %1 / 加 %2").arg(report.written).arg(report.added));
        check(report.skipped == 5,
              "PB7 预设里这一段的 8 项里有 5 项没勾（硬件 2 + 认不出 3）",
              QString::number(report.skipped));
        check(report.removed == 0 && report.untouchable == 0,
              "PB7 合并档一项都不删（`untouchable` 那个数只在覆盖档填 —— 别的档报出来只会让人"
              "以为我们在权衡什么）");
        check(!report.hasConflicts(),
              "★★ PB7 设置那一段**不报按键冲突**（计划表里也只有「原版按键」那一行"
              "带冲突数：报在这儿等于把别的段的账记到它头上）");
    }

    // ---------- PB8. 执行：合并之后那两份文件长什么样 ----------
    {
        const Presets::OptionsApplyResult result =
            Presets::applyOptions(Presets::SectionKind::VanillaOptions, target, source, selected,
                                  Presets::ApplyMode::Merge);
        const QByteArray merged = result.bytes;
        check(QString::fromUtf8(merged).contains(QStringLiteral("fov:0.5\nrenderDistance:8\n")),
              "★★ PB8 改的是**那一行的值**、位置一个都不动（`fov` 还在它原来那一行上，后面"
              "紧跟的还是 `renderDistance`）",
              QString::fromUtf8(merged));
        // 最硬的那一条：整个文件逐字节比。多一行、少一行、把哪一行挪个位置、换行符被
        // 统一过 —— 全都会红。上面那几条 `contains` 负责在红了之后说清楚是哪一格。
        const QByteArray expectedMerge = QByteArrayLiteral(
            "key_forward:key.keyboard.up\n"
            "key_back:key.keyboard.s\n"
            "key_sprint:key.keyboard.space\n"
            "fov:0.5\n"
            "renderDistance:8\n"
            "fullscreenResolution:1920x1080@60:24\n"
            "resourcePacks:[\"file/x.zip\"]\n"
            "lastServer:192.168.0.9\n"
            "tutorialStep:none\n"
            "# 手改过的一行（没有冒号）\n"
            "someNewOptionFromFutureVersion:yes\n"
            "lang:zh_cn\n");
        check(merged == expectedMerge,
              "★★ PB8 合并完**逐字节就是这几行**：目标那 11 行一行没少、被勾中的两条在原地改"
              "成新值、`lang` 追加在最后",
              QString::fromUtf8(merged));
        check(QString::fromUtf8(merged).contains(
                  QStringLiteral("resourcePacks:[\"file/x.zip\"]")) &&
                  QString::fromUtf8(merged).contains(QStringLiteral("lastServer:192.168.0.9")) &&
                  QString::fromUtf8(merged).contains(QStringLiteral("tutorialStep:none")) &&
                  QString::fromUtf8(merged).contains(QStringLiteral("# 手改过的一行（没有冒号）")),
              "★★ PB8 认不出来的行、手改过的行、目标上那些状态键**一个都不许丢**（拿一张 map"
              "重写整个文件就坏在这儿）");
        check(QString::fromUtf8(merged).contains(
                  QStringLiteral("someNewOptionFromFutureVersion:yes")),
              "★★ PB8 目标版本新增的字段也在（点名的那一条）");
        check(QString::fromUtf8(merged).contains(
                  QStringLiteral("fullscreenResolution:1920x1080@60:24")),
              "★★ PB8 硬件那条这一趟**没勾** ⇒ 目标上原来那份分辨率原样留着（值里那个"
              "第二个冒号也原样）");
        check(QString::fromUtf8(merged).endsWith(QStringLiteral("someNewOptionFromFutureVersion:yes\n"
                                                               "lang:zh_cn\n")),
              "PB8 目标上没有的那一项**追加在文件末尾**（我们不知道游戏会把它写在第几行，"
              "硬插到「看着该在的位置」等于凭空造一种排序规则）",
              QString::fromUtf8(merged).right(60));
        check(QString::fromUtf8(merged).contains(QStringLiteral("key_forward:key.keyboard.up")) &&
                  QString::fromUtf8(merged).contains(QStringLiteral("key_sprint:key.keyboard.space")),
              "★★ PB8 设置那一半**管不着 `key_*`**：目标上那两条按键一个字节都没动");
        check(QString::fromUtf8(merged).contains(QStringLiteral("fov:1.0")) == false,
              "PB8 老的值确实被换掉了（不是「两份都在」）");
    }

    // ---------- PB9. 覆盖档：必须明确确认过才动手 ----------
    {
        const Presets::OptionsApplyResult refused =
            Presets::applyOptions(Presets::SectionKind::VanillaOptions, target, source, selected,
                                  Presets::ApplyMode::Replace);
        check(!refused.report.ok && refused.report.needsConfirm &&
                  !refused.report.problem.isEmpty(),
              "★★ PB9 覆盖档没确认过 ⇒ 不动手，而且**说得出为什么**（「Replace 必须明确"
              "确认」）", refused.report.problem);
        check(refused.bytes == targetBytes,
              "★★ PB9 没确认的那一趟**目标一个字节都没动**（不是「先写了再回滚」）");

        const Presets::OptionsApplyResult replaced =
            Presets::applyOptions(Presets::SectionKind::VanillaOptions, target, source, selected,
                                  Presets::ApplyMode::Replace, true);
        const QByteArray out = replaced.bytes;
        check(replaced.report.ok && replaced.report.removed == 1,
              "PB9 确认过之后：覆盖档要删掉目标上**这一类**里预设没有的那些键（"
              "`fullscreenResolution` 一条）",
              QStringLiteral("删 %1").arg(replaced.report.removed));
        check(!QString::fromUtf8(out).contains(QStringLiteral("fullscreenResolution")),
              "PB9 那一条真的没了");
        check(replaced.report.untouchable == 5,
              "★★ PB9 `untouchable` 数的是目标上我们**认不出来**的那几行（`resourcePacks` /"
              "`lastServer` / `tutorialStep` / 没冒号那行 / 目标版本那条 = 5）",
              QString::number(replaced.report.untouchable));
        check(QString::fromUtf8(out).contains(QStringLiteral("resourcePacks:[\"file/x.zip\"]")) &&
                  QString::fromUtf8(out).contains(QStringLiteral("lastServer:192.168.0.9")) &&
                  QString::fromUtf8(out).contains(QStringLiteral("# 手改过的一行（没有冒号）")) &&
                  QString::fromUtf8(out).contains(
                      QStringLiteral("someNewOptionFromFutureVersion:yes")) &&
                  QString::fromUtf8(out).contains(QStringLiteral("tutorialStep:none")),
              "★★ PB9 **覆盖档也不删认不出来的行**（那是别人写的东西，不是「多余的东西」——"
              "把不认识的键当成该被覆盖掉的，正是这一层要防的那个坑）");
        check(QString::fromUtf8(out).contains(QStringLiteral("key_forward:key.keyboard.up")) &&
                  QString::fromUtf8(out).contains(QStringLiteral("key_back:key.keyboard.s")),
              "PB9 覆盖档的**域**只有设置那几档：按键那一份原样在");
        check(QString::fromUtf8(out).contains(QStringLiteral("fov:0.5")) &&
                  QString::fromUtf8(out).contains(QStringLiteral("lang:zh_cn")),
              "PB9 预设里勾中的那些都写进去了（含新追加的 `lang`）");
    }

    // ---------- PB10. 其余几档 ----------
    {
        // 缺了才拷：目标上已经有的一个字节都不动。
        const Presets::OptionsApplyResult copy =
            Presets::applyOptions(Presets::SectionKind::VanillaOptions, target, source, selected,
                                  Presets::ApplyMode::CopyIfMissing);
        check(copy.report.ok && copy.report.written == 1 && copy.report.added == 1 &&
                  QString::fromUtf8(copy.bytes).contains(QStringLiteral("fov:1.0")),
              "★★ PB10「缺了才拷」：目标上已经有 `fov` / `renderDistance` ⇒ 只补那一条"
              "（「别动用户已经调好的」那一档）",
              QStringLiteral("写 %1").arg(copy.report.written));

        // 跳过：一个字节都不动，而且老实说"一项都没写"。
        const Presets::OptionsApplyResult skip =
            Presets::applyOptions(Presets::SectionKind::VanillaOptions, target, source, selected,
                                  Presets::ApplyMode::Skip);
        check(skip.report.ok && skip.report.written == 0 && skip.report.skipped == 8 &&
                  skip.bytes == targetBytes,
              "PB10「跳过」：一项都不写、8 项全算没动、目标原样");

        // 追加：对 options.txt 没有意义（键是唯一的）⇒ 按合并走（写在这儿免得以后有人
        // 以为漏了一档）。
        const Presets::OptionsApplyResult append =
            Presets::applyOptions(Presets::SectionKind::VanillaOptions, target, source, selected,
                                  Presets::ApplyMode::Append);
        check(append.bytes ==
                  Presets::applyOptions(Presets::SectionKind::VanillaOptions, target, source,
                                        selected, Presets::ApplyMode::Merge)
                      .bytes,
              "PB10「追加」和「合并」结果一样（这是**故意**的，不是漏了一档）");

        // 按键那一段：只写 `key_*`，而且冲突照冲突规则报在它自己头上。
        const Presets::OptionsApplyResult keys =
            Presets::applyOptions(Presets::SectionKind::VanillaKeys, target, source, selected,
                                  Presets::ApplyMode::Merge);
        check(keys.report.ok && keys.report.written == 3 && keys.report.hasConflicts(),
              "★★ PB10 按键那一段：写 3 项、而且**冲突数报在它自己这一行上**（计划表"
              "「原版按键 合并 23 项，冲突 1 项」）",
              QStringLiteral("写 %1 / 冲突 %2")
                  .arg(keys.report.written)
                  .arg(keys.report.conflicts.size()));
        check(QString::fromUtf8(keys.bytes).contains(QStringLiteral("key_forward:key.keyboard.w")) &&
                  QString::fromUtf8(keys.bytes).contains(QStringLiteral("fov:1.0")),
              "PB10 按键那一段管不着 `fov`（它原样在）");

        // 不归这一层管的段：目标原样回去 + 一句人话。
        const Presets::OptionsApplyResult wrong =
            Presets::applyOptions(Presets::SectionKind::ServerList, target, source, selected,
                                  Presets::ApplyMode::Merge);
        check(!wrong.report.ok && wrong.bytes == targetBytes &&
                  !wrong.report.problem.isEmpty(),
              "PB10 不归这一层管的段 ⇒ 不动目标、报告里说清楚",
              wrong.report.problem);
        check(Presets::optionsHandlesSection(Presets::SectionKind::VanillaOptions) &&
                  Presets::optionsHandlesSection(Presets::SectionKind::VanillaKeys) &&
                  !Presets::optionsHandlesSection(Presets::SectionKind::ServerList) &&
                  !Presets::optionsHandlesSection(Presets::SectionKind::Custom),
              "PB10 `optionsHandlesSection()` 就是应用那一层的分派判据");
    }
}

