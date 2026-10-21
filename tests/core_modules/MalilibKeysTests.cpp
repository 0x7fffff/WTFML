#include "Support.hpp"

void runMalilibKeysTests() {
    section("PF. MaLiLib 那一族的热键：认、合、覆盖");

    const QByteArray malilibBytes = QByteArrayLiteral(
        "{\n"
        "  \"config_version\": 1,\n"
        "  \"Generic\": {\n"
        "    \"CarpetExtraDebug\": { \"enabled\": false, \"hotkey\": { \"keys\": \"\" } },\n"
        "    \"OpenConfigGui\": { \"enabled\": true, \"hotkey\": { \"keys\": \"M,C\" } }\n"
        "  },\n"
        "  \"GenericHotkeys\": {\n"
        "    \"ToggleHud\": { \"keys\": \"LEFT_ALT,H\" },\n"
        "    \"NothingHere\": { \"keys\": \"\" }\n"
        "  },\n"
        "  \"Visuals\": { \"RenderLines\": true, \"BoxColor\": \"#ff0000\" }\n"
        "}\n");

    // 夹具里找一格（按 `groupId/optionName`）—— 按名字找、不按下标：键序不是这一层的实现承诺。
    auto findEntry = [](const QList<Presets::MalilibEntry> &list, const QString &group,
                        const QString &option) -> const Presets::MalilibEntry * {
        for (const Presets::MalilibEntry &entry : list) {
            if (entry.groupId == group && entry.optionName == option) {
                return &entry;
            }
        }
        return nullptr;
    };

    section("PF0. 认这一族：看结构，不看名字");
    {
        const Presets::MalilibFileFacts facts = Presets::malilibInspect(malilibBytes);
        check(facts.recognized && facts.categories == 3 && facts.hotkeys == 2 &&
                  facts.hotkeyedValues == 2 && facts.values == 2,
              "★★★ PF0 认这一族的判据是**结构**：顶层一大格一大格的、其中至少有一格是热键（整格"
              "是热键，或者一格装开关 + 热键）。顶层那个 `config_version` 标量不算一格",
              QStringLiteral("大格 %1 / 热键 %2 / 开关+热键 %3 / 值 %4")
                  .arg(facts.categories)
                  .arg(facts.hotkeys)
                  .arg(facts.hotkeyedValues)
                  .arg(facts.values));

        // 我们**一个模组名都没写进代码**：TweakerMore / Masa Gadget / OMMC 这些扩展是照结构
        // 认出来的。
        const QStringList extensions = {QStringLiteral("tweakermore"),
                                        QStringLiteral("masagadget"), QStringLiteral("ommc"),
                                        QStringLiteral("minihud")};
        for (const QString &modId : extensions) {
            QList<Presets::MalilibEntry> entries;
            QString problem;
            check(Presets::malilibEntriesFromFile(modId, malilibBytes, &entries, &problem, false) &&
                      findEntry(entries, QStringLiteral("GenericHotkeys"),
                                QStringLiteral("ToggleHud")) != nullptr,
                  QStringLiteral("★★★ PF0 「%1」这个名字代码里一个字母都没有，可它的结构一眼就是这一"
                                 "族 —— 照样收得出它的热键").arg(modId),
                  problem);
        }

        // 只有一层值、一格热键都没有 ⇒ 不认。★ `litematica_<世界>_dim_0.json` 那种形状也是「顶层
        // 一大格一大格的」，光判「顶层是个对象」会把它当成热键配置收进来。
        const QByteArray valuesOnly = QByteArrayLiteral(
            "{\n  \"Visuals\": { \"RenderLines\": true, \"BoxColor\": \"#ff0000\" }\n}\n");
        check(!Presets::malilibLooksLikeConfig(valuesOnly),
              "★★★ PF0 一格热键都没有的那一份不认（不然一份纯值的配置会被当成热键预设收进预设里，"
              "铺过去时解绑一大片）",
              Presets::malilibInspect(valuesOnly).problem);

        const Presets::MalilibFileFacts garbage = Presets::malilibInspect(QByteArrayLiteral("这不是 JSON"));
        check(!garbage.recognized && garbage.problem.contains(QStringLiteral("JSON")),
              "★★ PF0 不是 JSON 的那一份：理由说清楚是「不是有效的 JSON」（不是「空的」）",
              garbage.problem);

        const Presets::MalilibFileFacts arrayish =
            Presets::malilibInspect(QByteArrayLiteral("[1,2]"));
        check(!arrayish.recognized && !arrayish.problem.isEmpty(),
              "★★ PF0 顶层是一张表的那一份也不认（一坨字节没法当配置读）", arrayish.problem);

        check(!Presets::malilibLooksLikeConfig(QByteArray()) &&
                  Presets::malilibModIdProblem(QStringLiteral("malilib")).isEmpty() &&
                  !Presets::malilibModIdProblem(QStringLiteral("..")).isEmpty() &&
                  !Presets::malilibModIdProblem(QStringLiteral("a/b")).isEmpty(),
              "★★ PF0 空字节不认；模组名要当得了文件名（`config/<模组名>.json`）—— `..` 和 `a/b` "
              "都拒（拼出来会跑到 `config/` 外面去）");
    }

    section("PF1. 三档格子 / 「什么都不说的格子不收」/ DTO 那几样");
    {
        QList<Presets::MalilibEntry> hotkeys;
        QString problem;
        check(Presets::malilibEntriesFromFile(QStringLiteral("malilib"), malilibBytes, &hotkeys,
                                              &problem, false),
              "PF1 读这一份（只收热键 = 默认那一档）", problem);

        const Presets::MalilibEntry *toggleHud =
            findEntry(hotkeys, QStringLiteral("GenericHotkeys"), QStringLiteral("ToggleHud"));
        const Presets::MalilibEntry *openGui =
            findEntry(hotkeys, QStringLiteral("Generic"), QStringLiteral("OpenConfigGui"));
        check(hotkeys.size() == 2 && toggleHud != nullptr && openGui != nullptr,
              "★★ PF1 三档格子里只有两档收：整格热键（`ToggleHud`）和「开关 + 热键」"
              "（`OpenConfigGui`）；值那一档（`Visuals` 底下那两格）默认不收",
              QStringLiteral("%1 格").arg(hotkeys.size()));

        if (toggleHud != nullptr) {
            check(toggleHud->kind == Presets::MalilibEntryKind::Hotkey &&
                      toggleHud->gesture.keys ==
                          QStringList({QStringLiteral("LEFT_ALT"), QStringLiteral("H")}) &&
                      toggleHud->gesture.toKeysString() == QStringLiteral("LEFT_ALT,H") &&
                      toggleHud->extra.isEmpty(),
                  "★★ PF1 `{\"keys\": \"LEFT_ALT,H\"}` → 键位拆成两张词（逗号分隔、修饰键在前，"
                  "就是这么写的）");
        }
        if (openGui != nullptr) {
            check(openGui->kind == Presets::MalilibEntryKind::Hotkeyed && !openGui->hasEnabled &&
                      openGui->label() == QStringLiteral("malilib/Generic/OpenConfigGui"),
                  "★★★ PF1 「热键 + 开关」那一档默认**只搬热键**（开关那一半不跟着走）；身份是 "
                  "`modId/groupId/optionName` —— 点名：**不用**翻译名（会随语言变）、不用"
                  "行号（会随模组版本变）",
                  openGui->label());
        }

        check(findEntry(hotkeys, QStringLiteral("GenericHotkeys"), QStringLiteral("NothingHere")) ==
                      nullptr &&
                  findEntry(hotkeys, QStringLiteral("Generic"),
                            QStringLiteral("CarpetExtraDebug")) == nullptr,
              "★★★ PF1 「什么都不说的格子」**不收**：`{\"keys\": \"\"}` 是这一格没绑 —— 把它当成"
              "预设的内容，等于宣布「目标上这一格也得解绑」");

        // ---- 连开关和值一起收（可选合并全部已知设置）----
        QList<Presets::MalilibEntry> all;
        check(Presets::malilibEntriesFromFile(QStringLiteral("malilib"), malilibBytes, &all,
                                              &problem, true),
              "PF1 再用「连值一起收」那一档读一遍", problem);
        const Presets::MalilibEntry *debug =
            findEntry(all, QStringLiteral("Generic"), QStringLiteral("CarpetExtraDebug"));
        const Presets::MalilibEntry *render =
            findEntry(all, QStringLiteral("Visuals"), QStringLiteral("RenderLines"));
        check(all.size() == 5 && debug != nullptr && debug->hasEnabled && !debug->enabled &&
                  debug->gesture.isEmpty(),
              "★★ PF1 这一档连开关一起收：`CarpetExtraDebug` 收成「关着的开关 + 没绑的热键」"
              "（写回去时**只碰开关、不碰键位**）",
              QStringLiteral("%1 格").arg(all.size()));
        check(render != nullptr && render->kind == Presets::MalilibEntryKind::Value &&
                  render->value.toBool(),
              "★★ PF1 值那一档收**原样**的 `QJsonValue`（不转成字符串：布尔写成 `\"true\"` 就是"
              "改了类型，模组会不认）");
        check(findEntry(all, QStringLiteral("GenericHotkeys"), QStringLiteral("NothingHere")) ==
                  nullptr,
              "★★ PF1 「连值一起收」那一档里，真正什么都不说的那一格（没绑、没开关、没别的）还是"
              "不收");

        const QByteArray dtoBytes = QByteArrayLiteral(
            "{\n  \"Hotkeys\": {\n    \"Fancy\": { \"keys\": \"LEFT_CTRL,LEFT_SHIFT,K\", "
            "\"trigger\": \"press\", \"context\": \"inGame\", \"orderSensitive\": true, "
            "\"allowExtraKeys\": true, \"exclusive\": true, \"onCancel\": \"reset\" }\n  }\n}\n");
        QList<Presets::MalilibEntry> dtoEntries;
        const bool dtoRead = Presets::malilibEntriesFromFile(QStringLiteral("malilib"), dtoBytes,
                                                             &dtoEntries, &problem, false);
        check(dtoRead && dtoEntries.size() == 1, "PF1 DTO 那一份夹具读得进来", problem);
        if (dtoRead && dtoEntries.size() == 1) {
            const Presets::MalilibEntry fancy = dtoEntries.at(0);
            check(fancy.gesture.trigger == QStringLiteral("press") &&
                      fancy.gesture.context == QStringLiteral("inGame") &&
                      fancy.gesture.orderSensitive && fancy.gesture.allowExtraKeys &&
                      fancy.gesture.exclusive,
                  "★★ PF1 触发 / 上下文 / 顺序 / 额外键 / 独占这五样都读进了 DTO",
                  QStringLiteral("%1/%2/%3/%4/%5")
                      .arg(fancy.gesture.trigger, fancy.gesture.context,
                           QString::number(fancy.gesture.orderSensitive),
                           QString::number(fancy.gesture.allowExtraKeys),
                           QString::number(fancy.gesture.exclusive)));
            check(fancy.extra.contains(QStringLiteral("onCancel")) &&
                      fancy.extra.value(QStringLiteral("onCancel")).toString() ==
                          QStringLiteral("reset"),
                  "★★ PF1 我们还没建模的那些兄弟（这一格里的 `onCancel`）进 `extra` —— 那是「取消"
                  "行为」这类东西能活着过去的唯一办法",
                  fancy.extra.keys().join(QStringLiteral(",")));

            // ---- 段 ⇄ 格子走一个来回 ----
            const QList<Presets::PresetSection> dtoSections =
                Presets::sectionsFromMalilibEntries(dtoEntries, Presets::ApplyMode::Merge);
            check(dtoSections.size() == 1 &&
                      dtoSections.at(0).id == Presets::malilibSectionId(QStringLiteral("malilib")) &&
                      dtoSections.at(0).kind == Presets::SectionKind::MalilibKeys &&
                      dtoSections.at(0).items.size() == 1,
                  "★★ PF1 一个模组一段（段 id 按模组名算出来，见 `malilibSectionId()`）",
                  QString::number(dtoSections.size()));

            QList<Presets::MalilibEntry> back;
            check(Presets::malilibEntriesFromSections(dtoSections, &back, &problem) &&
                      back.size() == 1 && back.at(0).gesture.keys == fancy.gesture.keys &&
                      back.at(0).gesture.trigger == fancy.gesture.trigger &&
                      back.at(0).gesture.exclusive && back.at(0).extra == fancy.extra &&
                      back.at(0).label() == fancy.label(),
                  "★★★ PF1 段 ⇄ 格子走一个来回：键位、那五样、`extra`、身份一个都不丢", problem);

            // 段的身份：载荷里那个模组名和段 id 必须对得上（UUID v5 单向 ⇒ 只能重算一遍再比）。
            QList<Presets::PresetSection> tampered = dtoSections;
            tampered[0].id = Presets::malilibSectionId(QStringLiteral("别的模组"));
            check(!Presets::malilibEntriesFromSections(tampered, &back, &problem) &&
                      !problem.isEmpty(),
                  "★★ PF1 段的 id 和载荷里那个模组名对不上 ⇒ 整段不认（这一份被谁动过）", problem);

            QList<Presets::PresetSection> mixed = dtoSections;
            QJsonObject second = mixed[0].items.at(0).toObject();
            second.insert(QStringLiteral("mod"), QStringLiteral("litematica"));
            mixed[0].items.append(second);
            check(!Presets::malilibEntriesFromSections(mixed, &back, &problem),
                  "★★ PF1 一段里两个模组 ⇒ 拒（一段 = 一个模组 = 一份文件，两个模组就没有落点了）",
                  problem);

            QList<Presets::PresetSection> badType = dtoSections;
            QJsonObject bad = badType[0].items.at(0).toObject();
            bad.insert(QStringLiteral("type"), QStringLiteral("热键"));
            badType[0].items = QJsonArray{bad};
            check(!Presets::malilibEntriesFromSections(badType, &back, &problem),
                  "★★ PF1 `type` 是**盘上的词**（`hotkey` / `hotkeyed` / `value`），认不出就拒 —— "
                  "不猜、也不按行号推断", problem);
        }
    }

    section("PF2. 一个真的实例上算：合并（目标上没被点名的那几格一个都不动）");
    QTemporaryDir scratch;
    check(scratch.isValid(), "PF 建得出一个临时目录来摆夹具");
    if (!scratch.isValid()) {
        return;
    }
    const QString sourceRoot = scratch.filePath(QStringLiteral("源实例"));
    const QString targetRoot = scratch.filePath(QStringLiteral("目标实例"));
    QDir().mkpath(sourceRoot + QStringLiteral("/config"));
    QDir().mkpath(targetRoot + QStringLiteral("/config"));
    check(writeBytesAt(sourceRoot + QStringLiteral("/config/malilib.json"), malilibBytes) &&
              writeBytesAt(sourceRoot + QStringLiteral("/config/litematica.json"),
                           QByteArrayLiteral("{\n  \"config_version\": 1,\n  \"Hotkeys\": {\n"
                                             "    \"OpenGui\": { \"keys\": \"M,C\" },\n"
                                             "    \"ToolPick\": { \"keys\": \"\" }\n  },\n"
                                             "  \"Visuals\": { \"RenderLines\": true }\n}\n")) &&
              writeBytesAt(targetRoot + QStringLiteral("/config/malilib.json"),
                           QByteArrayLiteral("{\n  \"config_version\": 1,\n  \"Generic\": {\n"
                                             "    \"OpenConfigGui\": { \"enabled\": false, \"hotkey\": "
                                             "{ \"keys\": \"M,B\" } },\n"
                                             "    \"SomeToggle\": { \"enabled\": true, \"hotkey\": "
                                             "{ \"keys\": \"M,T\" } }\n  },\n"
                                             "  \"GenericHotkeys\": {\n"
                                             "    \"ToggleHud\": { \"keys\": \"M,N\" },\n"
                                             "    \"BetaMod\": { \"keys\": \"M,B\" }\n  },\n"
                                             "  \"Visuals\": { \"RenderLines\": false, "
                                             "\"BoxColor\": \"#00ff00\" }\n}\n")) &&
              writeBytesAt(targetRoot + QStringLiteral("/config/litematica.json"),
                           QByteArrayLiteral("{\"Hotkeys\": {\"OpenGui\": { \"keys\": \"M,X\" },"
                                             " \"Other\": { \"keys\": \"M,X\" }}}")),
          "PF2 两侧的夹具都落盘了");

    Presets::MalilibExtractRequest extract;
    extract.instanceRoot = sourceRoot;
    extract.modIds = QStringList{QStringLiteral("malilib")};
    extract.mode = Presets::ApplyMode::Merge;
    extract.versions = Presets::MalilibSourceVersions{QStringLiteral("1.21.1"),
                                                     QStringLiteral("neoforge-21.1.9"),
                                                     QStringLiteral("malilib-0.21.0"),
                                                     QStringLiteral("neoforge-0.21.0")};
    const Presets::MalilibExtractReport extracted = Presets::extractMalilibSections(extract);
    check(extracted.ok && extracted.sections.size() == 1 && extracted.files == 1 &&
              extracted.hotkeys == 1 && extracted.hotkeyedValues == 1 && extracted.values == 0,
          "★★ PF2 从源实例收出这一段：1 个模组 / 1 格整热键 + 1 格开关+热键（值那一档默认不收）",
          extracted.problem);

    QList<Presets::MalilibEntry> presetEntries;
    QString problem;
    check(Presets::malilibEntriesFromSections(extracted.sections, &presetEntries, &problem) &&
              presetEntries.size() == 2,
          "PF2 那一段读回成一格一格", problem);
    check(!presetEntries.isEmpty() &&
              presetEntries.at(0).source.minecraft == QStringLiteral("1.21.1") &&
              presetEntries.at(0).source.loader == QStringLiteral("neoforge-21.1.9") &&
              presetEntries.at(0).source.mod == QStringLiteral("malilib-0.21.0") &&
              presetEntries.at(0).source.malilib == QStringLiteral("neoforge-0.21.0"),
          "★★ PF2 最后那句：来源的 Minecraft / 加载器 / 模组 / MaLiLib 版本记在每一格上",
          presetEntries.isEmpty() ? QString() : presetEntries.at(0).source.toJson().keys().join(','));

    const Presets::MalilibTarget target =
        Presets::malilibReadTarget(targetRoot, QStringList{QStringLiteral("malilib")}, &problem);
    check(problem.isEmpty() && target.files.size() == 1 && target.files.at(0).present &&
              target.files.at(0).problem.isEmpty(),
          "PF2 读目标那一份", problem);

    const Presets::MalilibApplyResult merged =
        Presets::applyMalilibEntries(presetEntries, target, Presets::ApplyMode::Merge, false);
    check(merged.report.ok && merged.report.written == 2 && merged.report.removed == 0 &&
              merged.report.skipped == 0 && merged.report.typeMismatch == 0 &&
              merged.files.size() == 1 && merged.files.at(0).changed,
          "★★ PF2 合并：预设点名的那两格写进去（动 2 项、没删东西、没跳过、没有类型不符）",
          QStringLiteral("动 %1 / 删 %2 / 跳过 %3")
              .arg(merged.report.written)
              .arg(merged.report.removed)
              .arg(merged.report.skipped));

    if (merged.files.size() == 1) {
        const QByteArray afterBytes = merged.files.at(0).bytes;
        QList<Presets::MalilibEntry> after;
        check(Presets::malilibEntriesFromFile(QStringLiteral("malilib"), afterBytes, &after,
                                              &problem, true),
              "PF2 读一遍这一趟之后的字节", problem);

        const Presets::MalilibEntry *beta =
            findEntry(after, QStringLiteral("GenericHotkeys"), QStringLiteral("BetaMod"));
        const Presets::MalilibEntry *someToggle =
            findEntry(after, QStringLiteral("Generic"), QStringLiteral("SomeToggle"));
        check(beta != nullptr && beta->gesture.toKeysString() == QStringLiteral("M,B") &&
                  someToggle != nullptr &&
                  someToggle->gesture.toKeysString() == QStringLiteral("M,T") &&
                  someToggle->hasEnabled && someToggle->enabled,
              "★★★ PF2 目标上**没被这一份点名**的那两格一个都没动（`BetaMod` 还在 `M,B`、"
              "`SomeToggle` 连它那个开关都在）—— 合并不许退化成覆盖",
              beta != nullptr ? beta->gesture.toKeysString() : QStringLiteral("没了"));

        const Presets::MalilibEntry *toggleHudAfter =
            findEntry(after, QStringLiteral("GenericHotkeys"), QStringLiteral("ToggleHud"));
        check(toggleHudAfter != nullptr &&
                  toggleHudAfter->gesture.toKeysString() == QStringLiteral("LEFT_ALT,H"),
              "★★ PF2 点名了的那一格换成预设里的键位",
              toggleHudAfter != nullptr ? toggleHudAfter->gesture.toKeysString() : QStringLiteral("没了"));

        const Presets::MalilibEntry *openAfter =
            findEntry(after, QStringLiteral("Generic"), QStringLiteral("OpenConfigGui"));
        check(openAfter != nullptr &&
                  openAfter->gesture.toKeysString() == QStringLiteral("M,C") &&
                  openAfter->hasEnabled && !openAfter->enabled,
              "★★★ PF2 预设只搬热键 ⇒ 目标上那一格的**开关不许被我们改**（它原来是 `false`，"
              "留着）",
              openAfter != nullptr ? openAfter->gesture.toKeysString() : QStringLiteral("没了"));

        check(afterBytes.contains("\"config_version\""),
              "★★ PF2 认不出来的顶层标量（`config_version`）原样留着 —— 我们不认识的东西不许"
              "被写没了");
    }

    section("PF3. 「追加」不抢已经绑好的 / 「缺了才写」是按格算的");
    {
        const Presets::MalilibApplyResult appended =
            Presets::applyMalilibEntries(presetEntries, target, Presets::ApplyMode::Append, false);
        check(appended.report.ok && appended.report.written == 0 && appended.report.skipped == 2 &&
                  !appended.files.isEmpty() && !appended.files.at(0).changed,
              "★★★ PF3 「追加」：目标上那两格都已经绑着别的键 ⇒ 一个都不抢（`skipped 2`），"
              "那一份文件一个字节都没改",
              QStringLiteral("动 %1 / 跳过 %2")
                  .arg(appended.report.written)
                  .arg(appended.report.skipped));

        const Presets::MalilibApplyResult copied =
            Presets::applyMalilibEntries(presetEntries, target, Presets::ApplyMode::CopyIfMissing,
                                        false);
        check(copied.report.ok && copied.report.written == 0 && copied.report.skipped == 2 &&
                  !copied.files.at(0).changed,
              "★★ PF3 「缺了才写」是按**格**算的（和目标上那一份在不在无关）：点名的两格都已有 ⇒ "
              "一格都不动（要「整份配置照搬一份」是通用文件组 P3 的活）",
              QStringLiteral("动 %1 / 跳过 %2")
                  .arg(copied.report.written)
                  .arg(copied.report.skipped));

        // 目标上那一格**没绑**：追加就该把它绑上（而不是当成「已经有了就不动」），而目标上压根
        // 没有的那一格就该造出来。
        Presets::MalilibTargetFile looseFile;
        looseFile.modId = QStringLiteral("malilib");
        looseFile.present = true;
        looseFile.bytes = QByteArrayLiteral(
            "{\"Generic\": {\"OpenConfigGui\": {\"enabled\": true, \"hotkey\": {\"keys\": \"\"}}}}");
        Presets::MalilibTarget loose;
        loose.files.append(looseFile);
        const Presets::MalilibApplyResult bindIt =
            Presets::applyMalilibEntries(presetEntries, loose, Presets::ApplyMode::Append, false);
        check(bindIt.report.ok && bindIt.report.written == 2 && bindIt.report.added == 1 &&
                  bindIt.files.at(0).changed,
              "★★ PF3 目标上那一格没绑 ⇒ 追加把它绑上（`keys` 从空串变成预设里的那两张词）；"
              "目标上压根没有的那一格（`GenericHotkeys.ToggleHud`）造出来 ⇒ `added 1`",
              QStringLiteral("动 %1 / 新添 %2")
                  .arg(bindIt.report.written)
                  .arg(bindIt.report.added));
    }

    section("PF4. 「覆盖」= 解绑但不删格子（+ 没确认就不动）");
    {
        const Presets::MalilibApplyResult refused =
            Presets::applyMalilibEntries(presetEntries, target, Presets::ApplyMode::Replace, false);
        check(!refused.report.ok && refused.report.needsConfirm && refused.files.isEmpty() &&
                  !refused.report.problem.isEmpty(),
              "★★ PF4 「覆盖」没被明确确认过 ⇒ 报 `needsConfirm`、`files` 是空的（盘上什么都没变）",
              refused.report.problem);

        const Presets::MalilibApplyResult swept =
            Presets::applyMalilibEntries(presetEntries, target, Presets::ApplyMode::Replace, true);
        check(swept.report.ok && swept.report.written == 2 && swept.report.removed == 2 &&
                  swept.report.untouchable == 2,
              "★★★ PF4 确认之后：点名那两格换成预设的（动 2）、目标上**其余绑着**的热键整个解绑"
              "（`BetaMod` 和 `SomeToggle` ⇒ 删 2），值那两格一个都没动（`untouchable 2`）",
              QStringLiteral("动 %1 / 删 %2 / 没动 %3")
                  .arg(swept.report.written)
                  .arg(swept.report.removed)
                  .arg(swept.report.untouchable));

        if (!swept.files.isEmpty() && swept.files.at(0).changed) {
            // 这一条**只能拿字节看**：`malilibEntriesFromFile()` 按设计**不收**"什么都不说的
            // 格子"（`{"keys": ""}`，PF1 钉过），而被解绑的格子长的正是那个样子 —— 拿格子那
            // 条路读，解绑和"删掉了"看起来一模一样。这一条问的恰好是那两者的分别。
            const QJsonObject sweptRoot =
                QJsonDocument::fromJson(swept.files.at(0).bytes).object();
            const QJsonObject sweptGeneric =
                sweptRoot.value(QStringLiteral("Generic")).toObject();
            const QJsonObject sweptHotkeys =
                sweptRoot.value(QStringLiteral("GenericHotkeys")).toObject();
            const QJsonValue betaCell = sweptHotkeys.value(QStringLiteral("BetaMod"));
            const QJsonObject someToggleCell =
                sweptGeneric.value(QStringLiteral("SomeToggle")).toObject();
            check(betaCell.isObject() &&
                      betaCell.toObject().value(QStringLiteral("keys")).toString().isEmpty() &&
                      someToggleCell.contains(QStringLiteral("hotkey")) &&
                      someToggleCell.value(QStringLiteral("hotkey"))
                          .toObject()
                          .value(QStringLiteral("keys"))
                          .toString()
                          .isEmpty() &&
                      someToggleCell.value(QStringLiteral("enabled")).toBool(),
                  "★★★ PF4 解绑**不是删格子**：那两格都还在（`keys` 变成空串）、`SomeToggle` 的"
                  "开关也留着 —— 删掉那一格，模组会按它代码里的默认值把它重建出来，用户看到的"
                  "就不是「这个键被解绑了」",
                  betaCell.isObject() ? QStringLiteral("keys=%1")
                                            .arg(betaCell.toObject()
                                                     .value(QStringLiteral("keys"))
                                                     .toString())
                                      : QStringLiteral("格子没了"));

            QList<Presets::MalilibEntry> after;
            check(Presets::malilibEntriesFromFile(QStringLiteral("malilib"), swept.files.at(0).bytes,
                                                  &after, &problem, true),
                  "PF4 读一遍覆盖之后的字节", problem);
            const Presets::MalilibEntry *render =
                findEntry(after, QStringLiteral("Visuals"), QStringLiteral("RenderLines"));
            check(findEntry(after, QStringLiteral("GenericHotkeys"), QStringLiteral("BetaMod")) ==
                          nullptr &&
                      render != nullptr && render->kind == Presets::MalilibEntryKind::Value &&
                      !render->value.toBool(),
                  "★★★ PF4 「覆盖」只解绑热键、**值那一格一个都不改**（`RenderLines` 还是目标上"
                  "那个 `false`，不许被预设里的 `true` 按默认重写）；那两格解绑之后在**格子**那"
                  "条路上就看不见了（这正是上面那一条必须拿字节看的原因）");
        }
    }

    section("PF5. 值那一格：类型变了就拒、目标上没有就跳过、一模一样就不写");
    {
        Presets::MalilibEntry valueEntry;
        valueEntry.modId = QStringLiteral("malilib");
        valueEntry.groupId = QStringLiteral("Visuals");
        valueEntry.optionName = QStringLiteral("RenderLines");
        valueEntry.kind = Presets::MalilibEntryKind::Value;
        valueEntry.value = QStringLiteral("yes");

        const Presets::MalilibApplyResult mismatch = Presets::applyMalilibEntries(
            {valueEntry}, target, Presets::ApplyMode::Merge, false);
        check(mismatch.report.ok && mismatch.report.typeMismatch == 1 &&
                  mismatch.report.written == 0 && !mismatch.files.at(0).changed &&
                  !mismatch.report.details.isEmpty() &&
                  mismatch.report.details.at(0).contains(QStringLiteral("类型对不上")),
              "★★★ PF5「类型变化时拒绝转换」：目标上那一格是开关、预设里是个字符串 ⇒ 这一项"
              "**没动**，而且理由说给用户听（原样能显示）",
              mismatch.report.details.join(QStringLiteral(" / ")));

        valueEntry.optionName = QStringLiteral("NotInTarget");
        const Presets::MalilibApplyResult absent = Presets::applyMalilibEntries(
            {valueEntry}, target, Presets::ApplyMode::Merge, false);
        check(absent.report.ok && absent.report.skipped == 1 && absent.report.written == 0 &&
                  absent.report.typeMismatch == 0 && !absent.files.at(0).changed &&
                  !absent.report.details.isEmpty() &&
                  absent.report.details.at(0).contains(QStringLiteral("还没有这一项")),
              "★★★ PF5「目标字段缺失时跳过并报告」：值那一档**不凭空造一格**（热键才造 —— "
              "搬热键本来就是这一层的用处，而一个值在目标上没有多半是模组版本不一样）",
              absent.report.details.join(QStringLiteral(" / ")));

        const Presets::MalilibApplyResult filled = Presets::applyMalilibEntries(
            {valueEntry}, target, Presets::ApplyMode::CopyIfMissing, false);
        check(filled.report.ok && filled.report.written == 1 && filled.report.added == 1 &&
                  filled.files.at(0).changed &&
                  filled.files.at(0).bytes.contains("\"NotInTarget\": \"yes\""),
              "★★ PF5 「缺了才写」那一档是例外：它的活就是把缺的补上",
              QString::fromUtf8(filled.files.at(0).bytes));

        Presets::MalilibEntry same;
        same.modId = QStringLiteral("malilib");
        same.groupId = QStringLiteral("Visuals");
        same.optionName = QStringLiteral("RenderLines");
        same.kind = Presets::MalilibEntryKind::Value;
        same.value = false;  // 目标上就是 `false`
        const Presets::MalilibApplyResult equal = Presets::applyMalilibEntries(
            {same}, target, Presets::ApplyMode::Merge, false);
        check(equal.report.ok && equal.report.written == 1 && equal.report.unchanged == 1 &&
                  !equal.files.at(0).changed,
              "★★ PF5 类型一样、值也一样 ⇒ 归在 `written` 里但标着 `unchanged`，**一个字节都不写**"
              "（不然每次应用都要重写整份配置、动它的 mtime）",
              QStringLiteral("动 %1 / 本来就是这样 %2")
                  .arg(equal.report.written)
                  .arg(equal.report.unchanged));
    }

    section("PF6. `extra`（认不出来的那些兄弟）：只补缺，不覆盖");
    {
        // 目标上那一格已经有 `trigger`（而且是别的值）⇒ 不覆盖；`onCancel` 缺 ⇒ 补上。
        Presets::MalilibTargetFile richFile;
        richFile.modId = QStringLiteral("malilib");
        richFile.present = true;
        richFile.bytes = QByteArrayLiteral(
            "{\"GenericHotkeys\": {\"ToggleHud\": {\"keys\": \"M,N\", \"trigger\": \"hold\"}}}");
        Presets::MalilibTarget richTarget;
        richTarget.files.append(richFile);

        Presets::MalilibEntry rich;
        rich.modId = QStringLiteral("malilib");
        rich.groupId = QStringLiteral("GenericHotkeys");
        rich.optionName = QStringLiteral("ToggleHud");
        rich.kind = Presets::MalilibEntryKind::Hotkey;
        rich.gesture.keys = QStringList({QStringLiteral("M"), QStringLiteral("K")});
        rich.extra.insert(QStringLiteral("trigger"), QStringLiteral("press"));
        rich.extra.insert(QStringLiteral("onCancel"), QStringLiteral("reset"));

        const Presets::MalilibApplyResult extraApplied =
            Presets::applyMalilibEntries({rich}, richTarget, Presets::ApplyMode::Merge, false);
        const QByteArray richBytes = extraApplied.files.isEmpty() ? QByteArray()
                                                                 : extraApplied.files.at(0).bytes;
        check(extraApplied.report.ok && extraApplied.files.at(0).changed &&
                  richBytes.contains("\"keys\": \"M,K\"") &&
                  richBytes.contains("\"onCancel\": \"reset\"") &&
                  richBytes.contains("\"trigger\": \"hold\"") &&
                  !richBytes.contains("\"trigger\": \"press\""),
              "★★★ PF6 `extra` **只补缺**：目标上没有的补上（`onCancel`）、已经有的一个都不改"
              "（`trigger` 还是用户那一格上的 `hold`）—— 反过来会让「我们还没建模的那些东西」被"
              "预设里的旧值盖掉",
              QString::fromUtf8(richBytes));
    }

    section("PF7. 冲突分析：抢同一组键位的那些");
    {
        auto claim = [](const QString &modId, const QString &group, const QString &option,
                        const QString &keys) {
            Presets::MalilibEntry entry;
            entry.modId = modId;
            entry.groupId = group;
            entry.optionName = option;
            entry.kind = Presets::MalilibEntryKind::Hotkey;
            entry.gesture = Presets::KeyGesture::fromKeysString(keys);
            return entry;
        };

        QList<Presets::MalilibEntry> claims;
        claims.append(claim(QStringLiteral("malilib"), QStringLiteral("Generic"),
                            QStringLiteral("OpenConfigGui"), QStringLiteral("M,C")));
        claims.append(claim(QStringLiteral("litematica"), QStringLiteral("Hotkeys"),
                            QStringLiteral("OpenGui"), QStringLiteral("M,C")));
        claims.append(claim(QStringLiteral("malilib"), QStringLiteral("GenericHotkeys"),
                            QStringLiteral("ToggleHud"), QStringLiteral("LEFT_ALT,H")));
        // 同一组键位、但 `exclusive` 不一样 ⇒ **不是同一件事**（两个都没绑的那些也一样不算）。
        Presets::MalilibEntry exclusive = claims.at(1);
        exclusive.gesture.exclusive = true;
        claims.append(exclusive);
        claims.append(claim(QStringLiteral("minihud"), QStringLiteral("GenericHotkeys"),
                            QStringLiteral("Nothing"), QString()));

        const QList<Presets::MalilibConflict> conflicts = Presets::malilibKeyConflicts(claims);
        check(conflicts.size() == 1 && conflicts.at(0).keys == QStringLiteral("M,C") &&
                  conflicts.at(0).claimedBy() == 2 &&
                  conflicts.at(0).claims.contains(QStringLiteral("litematica/Hotkeys/OpenGui")) &&
                  !conflicts.at(0).claims.contains(QStringLiteral("minihud/GenericHotkeys/Nothing")),
              "★★★ PF7 两个模组抢同一组键位 ⇒ 1 组冲突、`claims` 里把两个身份都列出来"
              "（这一层的 DTO「只用于展示和冲突分析」）；没绑的不参与、`exclusive` 不一样"
              "的那一条算**另一组**",
              conflicts.isEmpty()
                  ? QString()
                  : QStringLiteral("%1 × %2").arg(conflicts.at(0).keys).arg(conflicts.at(0).claimedBy()));
    }

    section("PF8. 计划表上那一行 + 真写一遍 + 跨模组的冲突");
    {
        Presets::PlanRequest planRequest;
        planRequest.target.instanceRoot = targetRoot;
        planRequest.sections = extracted.sections;
        const Presets::ApplyPlan plan = Presets::planPresetApplication(planRequest);
        check(plan.ok && plan.rows.size() == 1 && !plan.rows.at(0).blocked &&
                  plan.rows.at(0).kind == Presets::SectionKind::MalilibKeys &&
                  plan.rows.at(0).count == 2 && plan.rows.at(0).removed == 0,
              "★★ PF8 MaLiLib 那一段现在**有适配器接手了**（计划表上那一行是真算出来的，不是"
              "「做不了」）",
              Presets::applyPlanRowText(plan.rows.at(0)));
        check(Presets::applyPlanRowText(plan.rows.at(0)) == QStringLiteral("合并 2 项") &&
                  Presets::applyPlanUnit(Presets::SectionKind::MalilibKeys) == QStringLiteral("项"),
              "★★★ PF8 那一行的右栏就是那个形状，量词是「项」（一格热键 / 一格设置都是一项）",
              Presets::applyPlanRowText(plan.rows.at(0)));

        Presets::PlanRequest replaceRequest = planRequest;
        replaceRequest.sections[0].mode = Presets::ApplyMode::Replace;
        const Presets::ApplyPlan first = Presets::planPresetApplication(replaceRequest);
        check(first.needsConfirm() && !first.canApply() && first.rows.at(0).needsConfirm &&
                  first.rows.at(0).count == 2 && first.rows.at(0).removed == 2 &&
                  first.rows.at(0).problem.contains(QStringLiteral("解绑")),
              "★★★ PF8 「覆盖」那一行还在等用户点头，可**数照样给出来**（动 2、删掉 2 —— 删的那"
              "两个是目标上其余绑着的热键）",
              Presets::applyPlanRowText(first.rows.at(0)) + QStringLiteral("：") +
                  first.rows.at(0).problem);
        replaceRequest.replaceConfirmed = true;
        const Presets::ApplyPlan second = Presets::planPresetApplication(replaceRequest);
        check(second.canApply() && !second.rows.at(0).needsConfirm &&
                  second.rows.at(0).removed == 2,
              "★★ PF8 点头之后再算一趟：那一道闸放开，数一个字没变",
              Presets::applyPlanRowText(second.rows.at(0)));

        // ---- 跨模组：两个模组各占一份文件，抢同一组键位 ----
        Presets::MalilibExtractRequest both = extract;
        both.modIds = QStringList{QStringLiteral("malilib"), QStringLiteral("litematica")};
        const Presets::MalilibExtractReport twoMods = Presets::extractMalilibSections(both);
        check(twoMods.ok && twoMods.sections.size() == 2 && twoMods.files == 2,
              "PF8 两个模组的两段收出来了", twoMods.problem);
        Presets::PlanRequest crossRequest;
        crossRequest.target.instanceRoot = targetRoot;
        crossRequest.sections = twoMods.sections;
        const Presets::ApplyPlan cross = Presets::planPresetApplication(crossRequest);
        check(cross.keyConflicts.size() == 1 &&
                  cross.keyConflicts.at(0).keys == QStringLiteral("M,C") &&
                  cross.keyConflicts.at(0).claimedBy() == 2,
              "★★★ PF8 整份那一张单子看得见**跨模组**的抢法：一段 = 一个模组，行上那个数只看得见"
              "「同一份文件里两格抢同一组键位」，而真正常见的是两个模组各占一个 —— 那一种只有把"
              "各段算完**之后**的格子攒到一处才算得出来",
              cross.keyConflicts.isEmpty()
                  ? QString()
                  : QStringLiteral("%1 × %2")
                        .arg(cross.keyConflicts.at(0).keys)
                        .arg(cross.keyConflicts.at(0).claimedBy()));

        // ---- 真落盘 ----
        const Presets::MalilibApplyReport wrote = Presets::applyMalilibToInstance(
            targetRoot, presetEntries, Presets::ApplyMode::Merge, false);
        check(wrote.ok && wrote.written == 2 && wrote.problem.isEmpty(),
              "★★ PF8 真落到临时目录上", wrote.problem);

        const QString targetPath = targetRoot + QStringLiteral("/config/malilib.json");
        QByteArray onDisk;
        {
            QFile file(targetPath);
            check(file.open(QIODevice::ReadOnly), "PF8 读得回那一份");
            onDisk = file.readAll();
            file.close();
        }
        check(onDisk.contains("\"M,C\"") && onDisk.contains("\"BetaMod\"") &&
                  onDisk.contains("\"SomeToggle\""),
              "★★★ PF8 落盘之后：点名的那一格换掉了（`M,C`）、没点名的那两格原样还在");

        // 「这一个字节都没改」的那一趟**不许碰那一份文件**：把它设成只读，再拿一份和目标上
        // **一模一样**的格子去落一次 —— 真写了的话换不过去（`QSaveFile` 的原子换在只读的
        // 目标上会失败），`problem` 就会有话说。
        // 「本来就是这样」的那一趟**不许碰那一份文件**（不是"写进去一样的字节"，是**根本
        // 不许写**）：把用户的文件重写一遍，哪怕内容一样，mtime 也变了、他在外面拿别的工具
        // 比的时候会以为我们动过。证据两截：
        // ① 盘上的字节和 mtime 都要一模一样；
        // ② 再把那一份设成只读，同样的一趟照样得一路走到底 —— 真去写的话，`QSaveFile`
        // 那个原子换在只读的目标上会失败（Windows 上换不过去），`problem` 就有话说。
        QList<Presets::MalilibEntry> asIs;
        check(Presets::malilibEntriesFromFile(QStringLiteral("malilib"), onDisk, &asIs, &problem,
                                              false),
              "PF8 把盘上那一份读成格子（拿它当「一模一样」的那一份）", problem);
        const QDateTime beforeMtime = QFileInfo(targetPath).lastModified();
        QFile::setPermissions(targetPath, QFileDevice::ReadOwner);
        const Presets::MalilibApplyReport idle = Presets::applyMalilibToInstance(
            targetRoot, asIs, Presets::ApplyMode::Merge, false);
        QByteArray afterBytes;
        bool afterReadable = false;
        {
            QFile file(targetPath);
            afterReadable = file.open(QIODevice::ReadOnly);
            if (afterReadable) afterBytes = file.readAll();
            file.close();
        }
        check(afterReadable && idle.ok && idle.problem.isEmpty() && idle.unchanged == asIs.size() &&
                  afterBytes == onDisk && QFileInfo(targetPath).lastModified() == beforeMtime,
              "★★★ PF8 一格都没变的那一趟：报告说 `unchanged`、文件的字节和 mtime 一个字都没动，"
              "而且那一份**还是只读的也照样走完了**（真去写它的话原子换会失败）",
              idle.problem);
        QFile::setPermissions(targetPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }

    section("PF9. 通用文件组那一层：`config/<模组>.json` 不再是「只能复制/覆盖」");
    {
        check(writeBytesAt(sourceRoot + QStringLiteral("/config/valuesonly.json"),
                           QByteArrayLiteral("{\"Visuals\": {\"RenderLines\": true}}")),
              "PF9 再摆一份纯值的配置在 `config/` 底下");

        check(!Presets::genericItemIsCopyOnly(
                  Presets::FileGroup::Config, QStringLiteral("config/malilib.json"),
                  sourceRoot + QStringLiteral("/config/malilib.json")) &&
                  Presets::genericItemIsCopyOnly(
                      Presets::FileGroup::Config, QStringLiteral("config/valuesonly.json"),
                      sourceRoot + QStringLiteral("/config/valuesonly.json")) &&
                  Presets::genericItemIsCopyOnly(
                      Presets::FileGroup::ShaderPacks, QStringLiteral("shaderpacks/x.zip"),
                      sourceRoot + QStringLiteral("/shaderpacks/x.zip")),
              "★★★ PF9 那个记号（「未知配置明确标记为只能复制/覆盖」）现在**看内容**："
              "`config/malilib.json` 里有 P4 的适配器 ⇒ 不再是「只能复制/覆盖」；一格热键都没有的"
              "那一份、光影包那一档还是（名字那一截说了算）");

        QList<Presets::GenericItem> items;
        QString why;
        check(Presets::scanFileGroup(sourceRoot, Presets::FileGroup::Config, &items, nullptr, &why),
              "PF9 扫一遍源那一侧", why);
        bool sawMalilib = false;
        for (const Presets::GenericItem &item : items) {
            if (item.relativePath == QStringLiteral("config/malilib.json")) {
                sawMalilib = true;
                check(!item.copyOnly, "★★ PF9 扫盘那一趟也把那一格标成「不是只能复制/覆盖」");
            }
        }
        check(sawMalilib, "PF9 扫出来那一份了");

        const Presets::GenericPlan filePlan = Presets::planFileGroup(
            sourceRoot, items, targetRoot, Presets::ApplyMode::Merge);
        QString malilibReason;
        for (const Presets::GenericPlanItem &row : filePlan.items) {
            if (row.relativePath == QStringLiteral("config/malilib.json")) {
                malilibReason = row.reason;
            }
        }
        check(filePlan.ok && malilibReason.contains(QStringLiteral("模组热键")),
              "★★ PF9 「合并」那一档对它的说法说的是人话：这一份的结构我们认得、要搬它的热键得"
              "用。模组热键。那一段 —— 而不是「我们认不出它的结构」（那句话在这一份上是假的）",
              malilibReason);
    }
}

