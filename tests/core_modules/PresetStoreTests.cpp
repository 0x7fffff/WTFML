#include "Support.hpp"
#include "presets/PresetWorkspace.hpp"

void runPresetStoreTests() {
    {
        QTemporaryDir dir;
        QJsonObject doc{{"id",Presets::newPresetId()},{"name",QStringLiteral("常用")}};
        auto empty=Presets::saveEditorDocument(dir.path(),doc,false);
        check(!empty.ok,"Empty editor documents cannot be published");
        auto draft=Presets::saveEditorDocument(dir.path(),doc,true);
        check(draft.ok&&Presets::list(dir.path()).isEmpty()&&Presets::list(dir.path()+"/drafts").size()==1,"Blank drafts are stored separately from installation candidates");
        doc["sections"]=QJsonArray{QJsonObject{{"id","vanillaOptions"},{"kind","vanillaOptions"},{"items",QJsonArray{QJsonObject{{"key","guiScale"},{"value","2"}}}}}};
        doc["modReferences"]=QJsonArray{QJsonObject{{"platform","modrinth"},{"projectId","sodium"},{"name","Sodium"}}};
        auto composite=Presets::saveEditorDocument(dir.path(),doc,false);
        check(composite.ok&&composite.manifest.editorState.value("components").toObject().size()==2&&Presets::list(dir.path()).size()==1,"Composite publishes two private parts and one public preset");
        const auto parts=composite.manifest.editorState.value("components").toObject();bool ok=false;
        auto modPart=Presets::loadManifest(dir.path(),parts.value("mods").toString(),&ok);
        check(ok&&modPart.internalComponent&&modPart.sections.isEmpty()&&modPart.modReferences.size()==1,"Selecting composite Mods applies only its mod component");
        auto oldBytes=Presets::manifestToBytes(Presets::loadManifest(dir.path(),doc.value("id").toString()));
        doc["name"]=QStringLiteral("未发布修改");auto newerDraft=Presets::saveEditorDocument(dir.path(),doc,true);
        check(newerDraft.ok&&Presets::manifestToBytes(Presets::loadManifest(dir.path(),doc.value("id").toString()))==oldBytes,"Saving a draft preserves the currently published preset");
        doc["enabledParts"]=QJsonObject{{"mods",false}};auto disabled=Presets::saveEditorDocument(dir.path(),doc,false);
        check(disabled.ok&&disabled.manifest.category==Presets::PresetCategory::GameSettings&&disabled.manifest.modReferences.isEmpty()&&!disabled.document.value("modReferences").toArray().isEmpty(),"Disabled content remains editable but never enters the runtime payload");
        doc["enabledParts"]=QJsonObject{{"mods",false},{"gameSettings",false}};
        check(!Presets::saveEditorDocument(dir.path(),doc,false).ok,"Publishing requires at least one enabled populated part");
        auto unsafe=doc;unsafe["id"]="../outside";check(!Presets::saveEditorDocument(dir.path(),unsafe,true).ok,"Editor saves reject unsafe preset IDs");
    }
    {
        Presets::PresetManifest good; good.id = Presets::newPresetId(); good.name = QStringLiteral("边界");
        const auto json = Presets::manifestToJson(good);
        for (const auto *field : {"id", "name", "description", "createdAt", "updatedAt", "sourceInstanceId", "category"}) {
            for (const auto &value : {QJsonValue(QJsonValue::Null), QJsonValue(true), QJsonValue(42), QJsonValue(QJsonArray{}), QJsonValue(QJsonObject{})}) {
                auto malformed = json; malformed.insert(QLatin1String(field), value); bool ok = true;
                Presets::manifestFromJson(QJsonDocument(malformed).toJson(), &ok);
                check(!ok, "manifest scalar null/wrong type is rejected instead of silently becoming a legacy mixed preset", QLatin1String(field));
            }
        }
        auto legacy = json; legacy.remove("category"); bool ok = false;
        const auto restored = Presets::manifestFromJson(QJsonDocument(legacy).toJson(), &ok);
        check(ok && restored.category == Presets::PresetCategory::Unset, "missing category still preserves legitimate legacy mixed presets");
        const QString big = QStringLiteral("9007199254740993");
        auto ref = Presets::modReferenceFromJson(QJsonObject{{"platform", "curseforge"}, {"projectId", big}});
        check(ref.projectId == big && Presets::modReferenceToJson(ref).value("projectId").toString() == big,
            "large platform string identity round-trips without double rounding");
        ref = Presets::modReferenceFromJson(QJsonObject{{"platform", "curseforge"}, {"projectId", 9007199254740992.0}});
        check(ref.key().isEmpty(), "numeric platform identity is rejected rather than rounded into a different project");
        QTemporaryDir corruptRoot;
        auto corrupt = Presets::create(corruptRoot.path(), QStringLiteral("损坏样本"));
        QFile damaged(Presets::manifestPath(corruptRoot.path(), corrupt.id));
        const QByteArray broken("{\"id\": ]");
        check(damaged.open(QIODevice::WriteOnly) && damaged.write(broken) == broken.size(), "malformed JSON byte-position fixture is written");
        damaged.close(); QString reason; bool readable = true;
        Presets::loadManifest(corruptRoot.path(), corrupt.id, &readable, &reason);
        check(!readable && reason.contains(QStringLiteral("字节位置")), "malformed manifest reports parser byte offset");
        check(Presets::duplicate(corruptRoot.path(), corrupt.id).id.isEmpty() &&
            damaged.open(QIODevice::ReadOnly) && damaged.readAll() == broken,
            "corrupt manifest cannot be duplicated and original evidence is preserved byte-for-byte");
    }
    section("PA. 实例预设：UUID 当目录名 / 一份坏了不连坐 / 那五件增删改");

    QTemporaryDir temp;
    check(temp.isValid(), "PA0 临时目录建出来了");
    if (!temp.isValid()) {
        return;
    }
    // 预设那一层的每一个动盘函数都收一个 `root` 参数，所以这一整节连全局数据目录都不用
    // 改 —— 喂进来的就是 root（唯一那个"默认落点"单独在 PA4 念一次）。
    const QString root = temp.path() + QStringLiteral("/presets");
    const QString outside = temp.path() + QStringLiteral("/外面");

    // ---------- PA1. id 那一道闸 ----------
    const QString idA = Presets::newPresetId();
    const QString idB = Presets::newPresetId();
    check(idA != idB && idA.size() == 36 && Presets::isValidPresetId(idA)
              && Presets::isValidPresetId(idB),
          "PA1 造出来的 id 是规范 UUID（36 个字符、不带花括号），两次不一样", idA);

    const QStringList badIds = {
        QString(),                QStringLiteral(".."),
        QStringLiteral("../外面"), QStringLiteral("a/b"),
        QStringLiteral("C:/x"),   QStringLiteral("我的预设"),
        QStringLiteral("NUL"),    QStringLiteral("nul.txt"),
        QStringLiteral("{%1}").arg(idA), idA.toUpper(),
    };
    QStringList acceptedBadIds;
    for (const QString &bad : badIds) {
        if (Presets::isValidPresetId(bad)) {
            acceptedBadIds.append(bad.isEmpty() ? QStringLiteral("(空串)") : bad);
        }
    }
    check(acceptedBadIds.isEmpty(),
          "★★ PA1 不是规范 UUID 的一律不算 id —— 含 `..`、`C:/x`、中文、`NUL`（Windows 上那个"
          "『写下去就不见了』的设备名），以及花括号/大写那两种写法（同一个 id 有两种写法，"
          "在盘上就是两个目录）",
          acceptedBadIds.join(QStringLiteral(" / ")));

    // ---------- PA1. 显示名收一收 ----------
    const QString messyName = QStringLiteral("  我的  预设\n第二行\u3000尾巴  ");
    check(Presets::cleanDisplayName(messyName) == QStringLiteral("我的 预设 第二行 尾巴"),
          "★ PA1 显示名：去首尾空白 + 把换行/制表/全角空格都折成一个空格（名字要进任务标题，"
          "那边是按一行算的）",
          Presets::cleanDisplayName(messyName));
    check(Presets::cleanDisplayName(QString(120, QLatin1Char('x'))).size() == 80
              && Presets::cleanDisplayName(QStringLiteral("  \n\t ")).isEmpty(),
          "★ PA1 名字钳到 80 个字；清完是空串 = 用户什么都没输");

    // ---------- PA1. 条目里那个相对路径 ----------
    const QStringList safePaths = {
        QStringLiteral("config/foo.txt"),   QStringLiteral("config\\foo.txt"),
        QStringLiteral("控制台.txt"),       QStringLiteral("sub/dir/"),
        QStringLiteral("./a.txt"),          QStringLiteral("a/./b"),
        QStringLiteral("console.txt"),
    };
    const QStringList unsafePaths = {
        QString(),                          QStringLiteral("/绝对"),
        QStringLiteral("C:/绝对"),          QStringLiteral("\\\\服务器\\共享"),
        QStringLiteral(".."),               QStringLiteral("a/../../b"),
        QStringLiteral("a:b"),              QStringLiteral("a//b"),
        QStringLiteral("NUL"),              QStringLiteral("nul.txt"),
        QStringLiteral("COM1"),
    };
    const auto mismatched = [](const QStringList &list, bool want) {
        QStringList out;
        for (const QString &one : list) {
            if (Presets::isSafeRelativePath(one) != want) {
                out.append(one.isEmpty() ? QStringLiteral("(空串)") : one);
            }
        }
        return out;
    };
    const QStringList badSafe = mismatched(safePaths, true);
    const QStringList badUnsafe = mismatched(unsafePaths, false);
    check(badSafe.isEmpty(), "★ PA1 这几条条目路径放行（`\\` 和 `/` 都当分隔符认）",
          badSafe.join(QStringLiteral(" / ")));
    check(badUnsafe.isEmpty(),
          "★★ PA1 这五样挡掉：空串 / 绝对路径（含盘符和 UNC）/ 任何一段是 `..` / 任何一段带"
          "冒号 / Windows 那几个保留设备名（`nul.txt` 也算 —— 那个空设备跟后缀无关）",
          badUnsafe.join(QStringLiteral(" / ")));

    // ---------- PA2. 枚举 ⇄ 落盘用的词 ----------
    bool modeRoundTrip = true;
    for (Presets::ApplyMode mode :
         {Presets::ApplyMode::Merge, Presets::ApplyMode::Replace, Presets::ApplyMode::Append,
          Presets::ApplyMode::CopyIfMissing, Presets::ApplyMode::Skip}) {
        modeRoundTrip = modeRoundTrip && Presets::applyModeFromKey(Presets::applyModeKey(mode)) == mode;
    }
    check(modeRoundTrip, "★ PA2 五种应用方式：枚举 → 词 → 枚举，一格不差");

    bool kindRoundTrip = true;
    for (Presets::SectionKind kind :
         {Presets::SectionKind::VanillaOptions, Presets::SectionKind::VanillaKeys,
          Presets::SectionKind::ServerList, Presets::SectionKind::MalilibKeys,
          Presets::SectionKind::GenericFiles, Presets::SectionKind::Custom}) {
        kindRoundTrip = kindRoundTrip && Presets::sectionKindFromKey(Presets::sectionKindKey(kind)) == kind;
    }
    check(kindRoundTrip, "★ PA2 六种段：枚举 → 词 → 枚举，一格不差");

    bool modeKnown = true;
    bool kindKnown = true;
    const Presets::ApplyMode unknownMode =
        Presets::applyModeFromKey(QStringLiteral("以后加的档"), &modeKnown);
    const Presets::SectionKind unknownKind =
        Presets::sectionKindFromKey(QStringLiteral("以后加的档"), &kindKnown);
    check(!modeKnown && unknownMode == Presets::ApplyMode::Merge && !kindKnown
              && unknownKind == Presets::SectionKind::Custom,
          "★ PA2 认不出来的词：模式落 `Merge`（四个里最不容易弄坏用户东西的那一档）、种类落 "
          "`Custom`，而且都把 ok 按下去 —— 读的那一边要判得出『这一份坏了』");

    check(Presets::schemaVersion() == 1
              && Presets::manifestFormatTag() == QStringLiteral("wtfml-preset/1")
              && Presets::indexFormatTag() == QStringLiteral("wtfml-presets-index/1"),
          "PA2 版本号 1 和那两个格式标记（★ 这两个字面量就是『认不出来就整份不认』的门槛，"
          "改它们 = 改格式）");

    // ---------- PA3. 一份写下去读回来，逐格一样 ----------
    Presets::PresetSection options;
    options.id = Presets::newPresetId();
    options.kind = Presets::SectionKind::VanillaOptions;
    options.title = QStringLiteral("游戏设置");
    options.mode = Presets::ApplyMode::Merge;
    options.items = QJsonArray{QJsonObject{{QStringLiteral("key"), QStringLiteral("fov")},
                                           {QStringLiteral("value"), 80}}};

    Presets::PresetManifest full;
    full.id = Presets::newPresetId();
    full.name = QStringLiteral("我的习惯");
    full.description = QStringLiteral("视野和音量那一套");
    full.createdAt =
        QDateTime::fromString(QStringLiteral("2026-10-14T09:00:00.250"), Qt::ISODateWithMs);
    full.updatedAt =
        QDateTime::fromString(QStringLiteral("2026-10-14T10:30:00"), Qt::ISODateWithMs);
    full.sourceInstanceId = QStringLiteral("1.20.1-forge-47.2.0");
    full.sections = {options};

    bool valueOk = false;
    const Presets::PresetManifest readBack =
        Presets::manifestFromJson(Presets::manifestToBytes(full), &valueOk);
    check(valueOk && readBack.id == full.id && readBack.name == full.name
              && readBack.description == full.description
              && readBack.createdAt.toMSecsSinceEpoch() == full.createdAt.toMSecsSinceEpoch()
              && readBack.updatedAt.toMSecsSinceEpoch() == full.updatedAt.toMSecsSinceEpoch()
              && readBack.sourceInstanceId == full.sourceInstanceId,
          "★★ PA3 逐格往返：id / 名字 / 说明 / 两个时间（到毫秒）/ 从哪个实例提取来的");
    check(readBack.sections.size() == 1 && readBack.sections.first().id == options.id
              && readBack.sections.first().kind == options.kind
              && readBack.sections.first().title == options.title
              && readBack.sections.first().mode == options.mode
              && readBack.sections.first().items == options.items,
          "★★ PA3 段也逐格往返，载荷 `items` **原样**（这一层不解释它是什么）");
    check(readBack.section(Presets::SectionKind::VanillaOptions) != nullptr
              && readBack.section(Presets::SectionKind::ServerList) == nullptr,
          "PA3 `section(kind)` 按种类找那一截（没有回 nullptr）");

    // 空着的那几格**根本不写下去**。
    Presets::PresetManifest plain;
    plain.id = Presets::newPresetId();
    plain.name = QStringLiteral("手建的");
    const QJsonObject plainRoot = QJsonDocument::fromJson(Presets::manifestToBytes(plain)).object();
    check(!plainRoot.contains(QStringLiteral("description"))
              && !plainRoot.contains(QStringLiteral("sourceInstanceId")),
          "PA3 空着的字段根本不写下去（不是写一个空串 —— 写空串读的人会以为那是个开关）");
    check(plainRoot.contains(QStringLiteral("createdAt"))
              && plainRoot.contains(QStringLiteral("updatedAt"))
              && plainRoot.value(QStringLiteral("createdAt")).toString().isEmpty(),
          "★ PA3 两个时间**总是**写（没记上就写空串）：『字段不在』和『写了但认不出来』少一层歧义");
    bool plainOk = false;
    const Presets::PresetManifest plainBack =
        Presets::manifestFromJson(Presets::manifestToBytes(plain), &plainOk);
    check(plainOk && !plainBack.createdAt.isValid(),
          "★ PA3 缺时间**不算坏**（手写一份出来试的情况是存在的）");

    // 空段照样写下去（"我这一趟就是要覆盖成空"和"我没有这一段"是两件事）。
    Presets::PresetSection emptySection;
    emptySection.kind = Presets::SectionKind::ServerList;
    Presets::PresetManifest withEmpty = plain;
    withEmpty.sections = {emptySection};
    const QJsonObject sectionObject =
        Presets::sectionToJson(emptySection);
    check(!sectionObject.contains(QStringLiteral("items"))
              && !sectionObject.contains(QStringLiteral("title"))
              && sectionObject.value(QStringLiteral("mode")).toString() == QStringLiteral("merge")
              && withEmpty.sections.first().isEmpty(),
          "★ PA3 空段不写 `items`/`title`，但 `mode` 总是写；而『这一段是空的』由 `isEmpty()` 说");

    // ---------- PA3. 坏的那几档 ----------
    const auto bytesOf = [](const QString &json) { return json.toUtf8(); };
    const QByteArray goodBytes = Presets::manifestToBytes(full);

    QByteArray wrongTag = goodBytes;
    wrongTag.replace(QByteArrayLiteral("wtfml-preset/1"), QByteArrayLiteral("别人的/9"));
    bool tagOk = true;
    check(!Presets::manifestFromJson(wrongTag, &tagOk).isValid() && !tagOk,
          "★★ PA3 格式标记对不上 ⇒ 整份不认（不半信半疑地读成一堆空壳）");
    bool junkOk = true;
    check(!Presets::manifestFromJson(QByteArrayLiteral("这不是 json"), &junkOk).isValid() && !junkOk,
          "PA3 整个文件不是 JSON ⇒ 空壳 + ok=false");

    const auto versionAccepted = [&](const QString &version) {
        bool versionOk = true;
        Presets::manifestFromJson(
            bytesOf(QStringLiteral("{\"format\":\"wtfml-preset/1\",\"schemaVersion\":%1,"
                                   "\"id\":\"%2\",\"name\":\"x\"}")
                        .arg(version, full.id)),
            &versionOk);
        return versionOk;
    };
    check(!versionAccepted(QStringLiteral("\"1\"")),
          "★★ PA3 版本号写成字符串（\"1\"）⇒ 不认 —— 由着 Qt 读的话它会给出 0，于是"
          "『以后版本写的』被当成好的一路读下去");
    check(!versionAccepted(QStringLiteral("2")),
          "★★ PA3 版本比我们新 ⇒ 整份不认（猜着读 = 拿半懂的东西去改用户的实例）");
    check(!versionAccepted(QStringLiteral("0")) && !versionAccepted(QStringLiteral("-1")),
          "PA3 版本是 0 / 负数 ⇒ 也不认（『没写版本』不该被当成『第一代』）");
    check(versionAccepted(QStringLiteral("1")), "PA3 我们这一代（1）认");

    const auto sectionsAccepted = [&](const QJsonValue &sections) {
        QJsonObject object;
        object.insert(QStringLiteral("format"), Presets::manifestFormatTag());
        object.insert(QStringLiteral("schemaVersion"), 1);
        object.insert(QStringLiteral("id"), full.id);
        object.insert(QStringLiteral("name"), QStringLiteral("x"));
        object.insert(QStringLiteral("sections"), sections);
        bool sectionOk = true;
        Presets::manifestFromJson(QJsonDocument(object).toJson(), &sectionOk);
        return sectionOk;
    };
    check(!sectionsAccepted(QJsonArray{QJsonObject{
              {QStringLiteral("kind"), QStringLiteral("未来的种类")}}}),
          "★★ PA3 段的种类认不出来 ⇒ **整份**不认（读不懂还硬套 = 拿半懂的东西去改用户的实例）");
    check(!sectionsAccepted(QJsonArray{QJsonObject{{QStringLiteral("mode"), QStringLiteral("merge")}}}),
          "PA3 缺 `kind` 那一位走的是同一条（空串一样认不出来）");
    check(!sectionsAccepted(QJsonArray{QJsonObject{
              {QStringLiteral("kind"), QStringLiteral("serverList")},
              {QStringLiteral("mode"), QStringLiteral("以后加的档")}}}),
          "★★ PA3 `mode` 写了但我们不认识 ⇒ 也不认 —— 写侧那个词拼错是**不报错**的，"
          "只能靠读的时候挡住");
    check(sectionsAccepted(QJsonArray{QJsonObject{{QStringLiteral("kind"),
                                                   QStringLiteral("serverList")}}}),
          "★ PA3 `mode` **不在**的时候按默认档 `Merge` 算，而且不算坏（可选字段缺了不是错）");
    check(!sectionsAccepted(QJsonArray{QJsonObject{
              {QStringLiteral("kind"), QStringLiteral("serverList")},
              {QStringLiteral("items"), QStringLiteral("不是数组")}}}),
          "PA3 `items` 不是数组 ⇒ 不认");
    check(!sectionsAccepted(QJsonArray{QJsonValue(7)}) && !sectionsAccepted(QStringLiteral("不是数组")),
          "PA3 段不是对象 / `sections` 不是数组 ⇒ 不认");

    bool noNameOk = true;
    check(!Presets::manifestFromJson(
               bytesOf(QStringLiteral("{\"format\":\"wtfml-preset/1\",\"schemaVersion\":1,"
                                      "\"id\":\"%1\"}")
                           .arg(full.id)),
               &noNameOk)
               .isValid()
              && !noNameOk,
          "★ PA3 缺名字（或者缺 id）⇒ 不认：身份那两格是这一份拿不拿得出去的全部");

    // ---------- PA3. 下标往返 ----------
    Presets::PresetIndex index;
    index.order = QStringList{idA, idB};
    index.defaultPresetId = idB;
    bool indexOk = false;
    const Presets::PresetIndex indexBack =
        Presets::indexFromJson(Presets::indexToBytes(index), &indexOk);
    check(indexOk && indexBack.order == index.order && indexBack.defaultPresetId == idB
              && indexBack.contains(idA),
          "PA3 下标往返（次序 + 那个唯一的默认项）");
    check(!Presets::indexToJson(Presets::PresetIndex()).contains(QStringLiteral("default")),
          "PA3 没有默认项时 `default` 那一位根本不写");
    bool dupOk = false;
    const Presets::PresetIndex dupBack = Presets::indexFromJson(
        bytesOf(QStringLiteral("{\"format\":\"wtfml-presets-index/1\",\"schemaVersion\":1,"
                               "\"order\":[\"%1\",\"%1\",\"%2\"]}")
                    .arg(idA, idB)),
        &dupOk);
    check(dupOk && dupBack.order.size() == 2 && dupBack.order.first() == idA,
          "★ PA3 次序表里同一个 id 出现两遍只留第一处（留两遍界面就会多出一行）",
          QString::number(dupBack.order.size()));
    bool badIndexOk = true;
    check(!Presets::indexFromJson(QByteArrayLiteral("这不是 json"), &badIndexOk).contains(idA)
              && !badIndexOk,
          "PA3 下标读不回来 ⇒ 空下标 + ok=false（调用方那一下要知道）");
    for (const auto &fields : {QByteArray("\"order\":null"), QByteArray("\"order\":{}"),
                              QByteArray("\"order\":[42]"), QByteArray("\"default\":false")}) {
        bool typedOk = true;
        const auto malformed = QByteArray("{\"format\":\"wtfml-presets-index/1\",\"schemaVersion\":1,") + fields + '}';
        Presets::indexFromJson(malformed, &typedOk);
        check(!typedOk, "PA3 已存在的排序/默认字段类型错误不能当空值", QString::fromUtf8(fields));
    }

    // ---------- PA4. 落点 + 盘上那一趟 ----------
    const QString originalDir = AppPaths::minecraftDir();
    const QString presetRootDefault = Presets::presetsRoot();
    AppPaths::setMinecraftDir(temp.path() + QStringLiteral("/别的游戏文件夹"));
    check(Presets::presetsRoot() == presetRootDefault
              && presetRootDefault
                     == AppPaths::defaultMinecraftDir() + QStringLiteral("/presets"),
          "★★ PA4 预设跟着**启动器**走、不跟着当前游戏文件夹走（`<启动器数据根>/presets`）—— "
          "换一个 .minecraft，昨天攒的那份习惯还得在（和任务账本同一层）",
          presetRootDefault);
    AppPaths::setMinecraftDir(originalDir);

    check(Presets::list(root).isEmpty(),
          "PA4 一次都没做过预设的新用户：根目录都不在，列表还是空的（不崩）");

    const Presets::PresetManifest created =
        Presets::create(root, QStringLiteral("  我的习惯  "), QStringLiteral("视野那一套"));
    check(!created.id.isEmpty() && created.name == QStringLiteral("我的习惯")
              && created.description == QStringLiteral("视野那一套") && created.createdAt.isValid()
              && created.updatedAt.isValid(),
          "★ PA4 新建：新 id、名字收过（首尾空白去了）、两个时间都记上了", created.id);
    check(QFileInfo::exists(Presets::manifestPath(root, created.id))
              && QFileInfo(Presets::filesDir(root, created.id)).isDir()
              && QFileInfo::exists(Presets::indexPath(root)),
          "★★ PA4 落成的形状：`<root>/<id>/manifest.json` + `<root>/<id>/files/` + "
          "`index.json`");
    check(Presets::isValidPresetId(
              QFileInfo(Presets::presetDir(root, created.id)).fileName()),
          "★★ PA4 **显示名一个字节都不进路径**（目录名就是那个 UUID）");

    check(Presets::create(root, QStringLiteral("   \n\t ")).id.isEmpty(),
          "★ PA4 名字清完是空串 ⇒ 不建（回一个空壳，判据是 `id.isEmpty()`）");
    check(Presets::list(root).size() == 1, "PA4 那一次没建出东西来");

    const Presets::PresetManifest escaped =
        Presets::create(root, QStringLiteral("../../外面"));
    check(!escaped.id.isEmpty() && Presets::isValidPresetId(escaped.id)
              && QDir(Presets::presetDir(root, escaped.id)).exists() && !QDir(outside).exists(),
          "★★ PA4 名字叫 `../../外面` 也照建 —— 它只出现在界面上，路径里那一位还是 UUID，"
          "外面那个目录连建都没被建出来");
    check(Presets::remove(root, escaped.id), "PA4 把那一份收掉");

    const Presets::PresetManifest second = Presets::create(root, QStringLiteral("按键那一套"));
    const Presets::PresetManifest third = Presets::create(root, QStringLiteral("服务器那一套"));
    QList<Presets::PresetSummary> listed = Presets::list(root);
    check(listed.size() == 3 && listed.at(0).id == created.id && listed.at(1).id == second.id
              && listed.at(2).id == third.id,
          "★★ PA4 新建的排在**末尾**，列表次序 = 用户排的次序（不抢他排好的位置）",
          QString::number(listed.size()));
    // 夹具写入失败时列表为空，保留前面的失败记录，不再解引用空列表导致整套段错。
    check(!listed.isEmpty() && listed.first().name == QStringLiteral("我的习惯")
              && listed.first().description == QStringLiteral("视野那一套")
              && listed.first().sectionCount == 0 && !listed.first().isDefault,
          "PA4 列表那一栏要的那几样：显示名 / 说明 / 段数 / 是不是默认项");

    // ---------- PA4b. 四类（「不串类别」） ----------
    bool categoryRoundTrip = true;
    for (Presets::PresetCategory category :
         {Presets::PresetCategory::GameSettings, Presets::PresetCategory::Mods,
          Presets::PresetCategory::ResourcePacks, Presets::PresetCategory::DataPacks}) {
        categoryRoundTrip =
            categoryRoundTrip
            && Presets::presetCategoryFromKey(Presets::presetCategoryKey(category)) == category;
    }
    bool unknownCategoryOk = true;
    const Presets::PresetCategory unknownCategory =
        Presets::presetCategoryFromKey(QStringLiteral("futureCategory"), &unknownCategoryOk);
    check(categoryRoundTrip
              && Presets::presetCategoryKey(Presets::PresetCategory::Unset).isEmpty(),
          "★ PA4b 四类：枚举 → 词 → 枚举一格不差；`Unset`（旧数据）**不写下去**（空串）");
    check(!unknownCategoryOk && unknownCategory == Presets::PresetCategory::Unset,
          "★★ PA4b 以后版本写的类别词 ⇒ ok=false（读不懂那一类的载荷，不能猜着当成混合预设）");

    Presets::PresetManifest legacyManifest;
    legacyManifest.id = Presets::newPresetId();
    legacyManifest.name = QStringLiteral("旧版混合");
    const QJsonObject legacyRoot =
        QJsonDocument::fromJson(Presets::manifestToBytes(legacyManifest)).object();
    bool legacyOk = false;
    const Presets::PresetManifest legacyBack =
        Presets::manifestFromJson(Presets::manifestToBytes(legacyManifest), &legacyOk);
    check(legacyOk && legacyBack.category == Presets::PresetCategory::Unset
              && !legacyRoot.contains(QStringLiteral("category")),
          "★★ PA4b 旧数据（manifest 里没有 `category` 这一位）照读、算 `Unset`；"
          "`Unset` 也绝不写下去 —— 回退一版启动器时新做的预设还在，只是不认类别");

    Presets::PresetManifest modManifest;
    modManifest.id = Presets::newPresetId();
    modManifest.name = QStringLiteral("模组那一套");
    modManifest.category = Presets::PresetCategory::Mods;
    const QJsonObject modRoot =
        QJsonDocument::fromJson(Presets::manifestToBytes(modManifest)).object();
    bool modOk = false;
    check(modRoot.value(QStringLiteral("category")).toString() == QStringLiteral("mods")
              && Presets::manifestFromJson(Presets::manifestToBytes(modManifest), &modOk)
                         .category
                     == Presets::PresetCategory::Mods
              && modOk,
          "★ PA4b 写了类别的那一份：`category: \"mods\"` 落下去、读回来还是 `Mods`");

    QByteArray futureCategoryBytes = Presets::manifestToBytes(modManifest);
    futureCategoryBytes.replace(QByteArrayLiteral("\"mods\""), QByteArrayLiteral("\"future\""));
    bool futureCategoryOk = true;
    check(!Presets::manifestFromJson(futureCategoryBytes, &futureCategoryOk).isValid()
              && !futureCategoryOk,
          "★★ PA4b 盘上写着我们认不出的类别 ⇒ **整份**不认（和段种类那一条同一条理由）");

    Presets::PresetSection optionsSection;
    optionsSection.kind = Presets::SectionKind::VanillaOptions;
    Presets::PresetSection keysSection;
    keysSection.kind = Presets::SectionKind::VanillaKeys;
    Presets::PresetSection serversSection;
    serversSection.kind = Presets::SectionKind::ServerList;
    Presets::PresetSection malilibSection;
    malilibSection.kind = Presets::SectionKind::MalilibKeys;
    Presets::PresetSection configSection;
    configSection.kind = Presets::SectionKind::GenericFiles;
    configSection.id = Presets::sectionIdFor(Presets::SectionKind::GenericFiles,
                                             QStringLiteral("config"));
    Presets::PresetSection defaultConfigsSection;
    defaultConfigsSection.kind = Presets::SectionKind::GenericFiles;
    defaultConfigsSection.id = Presets::sectionIdFor(Presets::SectionKind::GenericFiles,
                                                     QStringLiteral("defaultconfigs"));
    Presets::PresetSection resourcePacksSection;
    resourcePacksSection.kind = Presets::SectionKind::GenericFiles;
    resourcePacksSection.id = Presets::sectionIdFor(Presets::SectionKind::GenericFiles,
                                                    QStringLiteral("resourcepacks"));
    Presets::PresetSection shaderPacksSection;
    shaderPacksSection.kind = Presets::SectionKind::GenericFiles;
    shaderPacksSection.id = Presets::sectionIdFor(Presets::SectionKind::GenericFiles,
                                                  QStringLiteral("shaderpacks"));
    Presets::PresetSection customSection;
    customSection.kind = Presets::SectionKind::Custom;

    using Cat = Presets::PresetCategory;
    check(Presets::sectionFitsCategory(Cat::GameSettings, optionsSection)
              && Presets::sectionFitsCategory(Cat::GameSettings, keysSection)
              && Presets::sectionFitsCategory(Cat::GameSettings, serversSection)
              && Presets::sectionFitsCategory(Cat::GameSettings, malilibSection)
              && Presets::sectionFitsCategory(Cat::GameSettings, configSection)
              && Presets::sectionFitsCategory(Cat::GameSettings, defaultConfigsSection)
              && !Presets::sectionFitsCategory(Cat::GameSettings, resourcePacksSection)
              && !Presets::sectionFitsCategory(Cat::GameSettings, shaderPacksSection),
          "★★ PA4b 「游戏设置」= 选项 / 按键 / 服务器列表 / MaLiLib 热键 + config 那一类"
          "高级内容；资源包 / 光影包**不属于**它");
    check(Presets::sectionFitsCategory(Cat::ResourcePacks, resourcePacksSection)
              && Presets::sectionFitsCategory(Cat::ResourcePacks, shaderPacksSection)
              && !Presets::sectionFitsCategory(Cat::ResourcePacks, optionsSection)
              && !Presets::sectionFitsCategory(Cat::ResourcePacks, configSection),
          "★★ PA4b 「资源包」= resourcepacks/ + shaderpacks/（光影跟资源包同一摊文件），"
          "别的一概不收");
    check(Presets::sectionFitsCategory(Cat::Mods, customSection)
              && Presets::sectionFitsCategory(Cat::DataPacks, customSection)
              && !Presets::sectionFitsCategory(Cat::Mods, optionsSection)
              && !Presets::sectionFitsCategory(Cat::DataPacks, resourcePacksSection),
          "★ PA4b 模组 / 数据包首版只放得下 `Custom`（模组引用与数据包载荷在 P4 / P5）");
    check(Presets::sectionFitsCategory(Cat::Unset, optionsSection)
              && Presets::sectionFitsCategory(Cat::Unset, resourcePacksSection),
          "★★ PA4b 旧版混合预设（`Unset`）什么都放得下 —— 老数据一个字节都不动");

    Presets::PresetManifest mismatchManifest;
    mismatchManifest.id = Presets::newPresetId();
    mismatchManifest.name = QStringLiteral("串了类");
    mismatchManifest.category = Cat::Mods;
    mismatchManifest.sections = {optionsSection};
    check(!Presets::categoryMismatch(mismatchManifest).isEmpty(),
          "★ PA4b 类别与段表对不上 ⇒ `categoryMismatch()` 给出一句人话");
    mismatchManifest.category = Cat::GameSettings;
    check(Presets::categoryMismatch(mismatchManifest).isEmpty()
              && Presets::categoryMismatch(legacyManifest).isEmpty(),
          "PA4b 对得上 / 旧数据 ⇒ 空串");

    const QString catRoot = temp.path() + QStringLiteral("/presets-cat");
    Presets::PresetManifest badWrite;
    badWrite.id = Presets::newPresetId();
    badWrite.name = QStringLiteral("写不进去的");
    badWrite.category = Cat::Mods;
    badWrite.sections = {optionsSection};
    check(!Presets::saveManifest(catRoot, badWrite)
              && !QFileInfo::exists(Presets::manifestPath(catRoot, badWrite.id)),
          "★★ PA4b 写侧那道闸：类别和内容对不上的一份**一个字节都不写**（宁可这一趟存不下去，"
          "也不要把串了类的预设写进盘里）");
    const Presets::PresetManifest catCreated =
        Presets::create(catRoot, QStringLiteral("资源包那一套"), QString(), Cat::ResourcePacks);
    check(!catCreated.id.isEmpty() && catCreated.category == Cat::ResourcePacks
              && Presets::loadManifest(catRoot, catCreated.id).category == Cat::ResourcePacks,
          "★ PA4b `create(..., category)`：新的一份带着类别落盘、读回来还在");
    const QList<Presets::PresetSummary> catListed = Presets::list(catRoot);
    check(catListed.size() == 1 && catListed.first().category == Cat::ResourcePacks,
          "★ PA4b 列表摘要里也带着类别（管理页靠它本地筛）");

    // 手写一份"类别说 A、内容其实是 B"的 manifest：读侧也必须报坏（不是只管写侧）。
    const QString tamperedId = Presets::newPresetId();
    QDir().mkpath(Presets::presetDir(catRoot, tamperedId));
    QJsonObject tampered;
    tampered.insert(QStringLiteral("format"), Presets::manifestFormatTag());
    tampered.insert(QStringLiteral("schemaVersion"), 1);
    tampered.insert(QStringLiteral("id"), tamperedId);
    tampered.insert(QStringLiteral("name"), QStringLiteral("手改过的"));
    tampered.insert(QStringLiteral("category"), QStringLiteral("mods"));
    tampered.insert(QStringLiteral("sections"),
                    QJsonArray{QJsonObject{{QStringLiteral("kind"),
                                            QStringLiteral("vanillaOptions")}}});
    writeFileAt(Presets::manifestPath(catRoot, tamperedId),
                QJsonDocument(tampered).toJson());
    bool tamperedOk = true;
    QString tamperedReason;
    Presets::loadManifest(catRoot, tamperedId, &tamperedOk, &tamperedReason);
    check(!tamperedOk && tamperedReason.contains(QStringLiteral("类别")),
          "★★ PA4b 读侧那道闸：盘上那份类别和内容对不上 ⇒ 报坏（拿『类别说 A、内容其实是 B』"
          "的预设去改用户实例，比少一份预设危险得多）",
          tamperedReason);

    // 这一整段用的是自己的 root，不碰 PA4 那三份。
    Presets::remove(catRoot, catCreated.id);
    QDir(catRoot).removeRecursively();

    // ---------- PA5. 那一道逃逸闸 ----------
    QDir().mkpath(outside);
    writeFileAt(outside + QStringLiteral("/重要的东西.txt"), QByteArrayLiteral("别删我"));
    check(!Presets::remove(root, QStringLiteral("../外面")),
          "★★ PA5 `remove(root, \"../外面\")` 回 false —— 那道闸在动盘之前先把它挡了");
    check(QFileInfo::exists(outside + QStringLiteral("/重要的东西.txt")),
          "★★ PA5 外面那份东西**还在**（这条路的症状不是报错，是『删掉一份预设』顺手删了别的东西）");
    check(!Presets::remove(root, QStringLiteral("../../.."))
              && !Presets::remove(root, QString()),
          "PA5 别的几种写法（`../../..`、空串）一样挡在门口");

    bool escapeOk = true;
    QString escapeReason;
    Presets::loadManifest(root, QStringLiteral("../外面"), &escapeOk, &escapeReason);
    check(!escapeOk && escapeReason.contains(QStringLiteral("不是一份预设")),
          "★ PA5 读那一边也一样，而且说得出为什么", escapeReason);

    Presets::PresetManifest forged;
    forged.id = QStringLiteral("../外面");
    forged.name = QStringLiteral("伪造的");
    check(!Presets::saveManifest(root, forged)
              && !QFileInfo::exists(outside + QStringLiteral("/manifest.json")),
          "★★ PA5 写那一边也挡（`presetDir()` 拼出来的路径正好落在外面那个目录里，"
          "而它一个字节都没写进去）");
    Presets::PresetManifest nameless = created;
    nameless.name = QStringLiteral("   ");
    check(!Presets::saveManifest(root, nameless),
          "★ PA5 名字清完是空的 manifest 写不下去");
    check(Presets::loadManifest(root, created.id).name == QStringLiteral("我的习惯"),
          "PA5 刚才那一发一个字都没落到盘上");

    // ---------- PA6. 改名只改显示名 ----------
    writeFileAt(Presets::filesDir(root, created.id) + QStringLiteral("/config/keys.txt"),
                QByteArrayLiteral("W=前进"));
    const Presets::PresetManifest before = Presets::loadManifest(root, created.id);
    check(Presets::rename(root, created.id, QStringLiteral("  新名字  ")), "PA6 改名成功");
    const Presets::PresetManifest renamed = Presets::loadManifest(root, created.id);
    check(renamed.name == QStringLiteral("新名字") && renamed.id == before.id
              && renamed.createdAt == before.createdAt && renamed.updatedAt >= before.updatedAt,
          "★★ PA6 改名**只改显示名**：id 和创建时间一个字都不动，目录名还是那个 UUID");
    check(QFileInfo::exists(Presets::manifestPath(root, created.id))
              && QFileInfo::exists(Presets::filesDir(root, created.id)
                                   + QStringLiteral("/config/keys.txt")),
          "★★ PA6 目录、`manifest.json`、`files/` 里用户放的东西**一个都没动**（普通重命名不改"
          "磁盘上的身份）");
    check(!Presets::rename(root, created.id, QStringLiteral("   ")), "PA6 清完是空串回 false");
    check(Presets::rename(root, created.id, QStringLiteral("新名字"))
              && Presets::loadManifest(root, created.id).updatedAt == renamed.updatedAt,
          "★ PA6 改成同一个名字算成功，但『改了一次』这个动作没发生过 ⇒ 时间也不动");
    check(!Presets::rename(root, QStringLiteral("../外面"), QStringLiteral("x"))
              && !Presets::rename(root, Presets::newPresetId(), QStringLiteral("x")),
          "PA6 非法 / 不存在的 id 回 false");

    // ---------- PA7. 排序 + 那个唯一的默认项 ----------
    check(Presets::move(root, third.id, -1), "PA7 往上挪一位");
    listed = Presets::list(root);
    check(listed.size() == 3 && listed.at(0).id == created.id && listed.at(1).id == third.id
              && listed.at(2).id == second.id,
          "★★ PA7 挪动真的换了位（次序表的次序就是列表的次序）");
    check(!Presets::move(root, third.id, -5) && !Presets::move(root, second.id, +1)
              && !Presets::move(root, Presets::newPresetId(), -1),
          "★ PA7 挪不动的那一头回 false（界面按这个判『那一侧的三角画不画』）");

    check(Presets::setDefault(root, third.id), "PA7 设默认项");
    check(Presets::defaultPresetId(root) == third.id, "PA7 那个唯一的默认项就是它");
    listed = Presets::list(root);
    check(listed.size() == 3 && listed.at(1).isDefault && !listed.at(0).isDefault && !listed.at(2).isDefault,
          "★★ PA7 列表里**只有一份**顶着默认项（『设为唯一默认项』）");
    check(!Presets::setDefault(root, Presets::newPresetId()),
          "PA7 不在下标里的 id 设不成默认");
    check(Presets::setDefault(root, QString()) && Presets::defaultPresetId(root).isEmpty(),
          "PA7 空串 = 取消默认");
    check(Presets::setDefault(root, third.id), "PA7 再设回来");

    check(Presets::remove(root, third.id), "PA7 删掉那一份");
    check(Presets::defaultPresetId(root).isEmpty(),
          "★★ PA7 删掉的正好是默认项 ⇒ **默认位空出来**，不偷偷换给列表里的下一份"
          "（那一下用户看不见，而默认项是个有后果的开关）");
    check(Presets::list(root).size() == 2 && !QDir(Presets::presetDir(root, third.id)).exists(),
          "★ PA7 目录连同它的东西一起收掉了，别的两份还在");
    check(Presets::remove(root, third.id), "PA7 再删一次：目录已经不在了也算删掉了（幂等）");

    // ---------- PA8. 复制一份 ----------
    Presets::PresetManifest sourceWithSection = Presets::loadManifest(root, created.id);
    sourceWithSection.sections = {options};
    check(Presets::saveManifest(root, sourceWithSection), "PA8 把源那一份改成一个带段的");

    const Presets::PresetManifest copy = Presets::duplicate(root, created.id);
    check(!copy.id.isEmpty() && copy.id != created.id
              && copy.name == QStringLiteral("新名字 副本"),
          "★★ PA8 复制出来的是**新 id**（副本是另一份，不是同一份的两个名字）", copy.name);
    check(copy.sections.size() == 1
              && copy.sections.first().kind == Presets::SectionKind::VanillaOptions
              && copy.sections.first().items == options.items,
          "★★ PA8 段原样搬过去（载荷也是原样）");
    check(QFileInfo(Presets::filesDir(root, copy.id) + QStringLiteral("/config/keys.txt")).size()
              == QByteArrayLiteral("W=前进").size(),
          "★ PA8 `files/` 里用户放的东西也搬过来了（那份目录是这一份的一部分）");
    check(Presets::list(root).last().id == copy.id, "PA8 复制出来的排在末尾");
    const Presets::PresetManifest named = Presets::duplicate(root, created.id, QStringLiteral("  另一个  "));
    check(named.name == QStringLiteral("另一个"), "PA8 给了名字就用给的（一样要收一遍）");
    check(Presets::duplicate(root, QStringLiteral("../外面")).id.isEmpty()
              && Presets::duplicate(root, Presets::newPresetId()).id.isEmpty(),
          "★ PA8 源读不回来 ⇒ 空壳（不拿半懂的东西去造一份新的）");

    // ---------- PA9. 一份坏了不许连坐别的（P1 点名的验收）----------
    const Presets::PresetManifest doomed = Presets::create(root, QStringLiteral("坏掉的那一份"));
    const int aliveBefore = Presets::list(root).size();
    writeFileAt(Presets::manifestPath(root, doomed.id), QByteArrayLiteral("{ 这不是 json"));
    QList<Presets::BrokenPreset> broken;
    const QList<Presets::PresetSummary> survivors = Presets::list(root, &broken);
    check(survivors.size() == aliveBefore - 1 && broken.size() == 1
              && broken.first().id == doomed.id,
          "★★ PA9 一份坏了**只丢它自己**：别的照旧列得出来",
          QStringLiteral("活了 %1 份、坏了 %2 份").arg(survivors.size()).arg(broken.size()));
    check(!broken.isEmpty() && broken.first().reason.contains(QStringLiteral("manifest.json")),
          "★ PA9 坏的那一份说得出**是哪个文件**（这句人话原样能显示给用户，界面不再加工）",
          broken.isEmpty() ? QStringLiteral("未生成损坏记录，检查前置夹具失败") : broken.first().reason);
    check(!Presets::loadManifest(root, doomed.id).isValid(), "PA9 直接读它也是空壳");

    // 下标里有、盘上已经没有了（「默认项丢失」那个组合要的正是这一档）。
    const QString ghostId = Presets::newPresetId();
    check(Presets::saveIndex(root, Presets::appendToIndex(Presets::loadIndex(root), ghostId)),
          "PA9 往下标里塞一个盘上没有的 id");
    QList<Presets::BrokenPreset> withGhost;
    const QList<Presets::PresetSummary> survivors2 = Presets::list(root, &withGhost);
    QString ghostReason;
    for (const Presets::BrokenPreset &one : withGhost) {
        if (one.id == ghostId) {
            ghostReason = one.reason;
        }
    }
    check(survivors2.size() == survivors.size() && !ghostReason.isEmpty()
              && ghostReason.contains(QStringLiteral("已经不在了")),
          "★★ PA9 下标里有、盘上没了的 ⇒ 进 broken **而不进列表**", ghostReason);
    check(Presets::loadIndex(root).contains(ghostId),
          "★★ PA9 读的那一趟**没有替用户改下标**（读的时候顺手改盘是最难查的那一类）");

    // ---------- PA10. 盘上有、下标里没有的 → 照样列出来 ----------
    Presets::PresetManifest loose;
    loose.id = Presets::newPresetId();
    loose.name = QStringLiteral("用户自己拷进来的");
    check(Presets::saveManifest(root, loose), "PA10 直接在盘上放一份（不进下标）");
    listed = Presets::list(root);
    check(listed.size() == survivors.size() + 1 && listed.last().id == loose.id,
          "★★ PA10 『盘上有、下标里没有』的那一份**照样列出来、排在末尾** —— 不丢东西是这一条"
          "的全部理由",
          listed.last().name);

    // ---------- PA11. 下标本身坏了：读不回来就不许写回去 ----------
    {
        const QString inaccessibleRoot = temp.path() + QStringLiteral("/unreadable-index");
        QDir().mkpath(Presets::indexPath(inaccessibleRoot));
        bool readable = true;
        Presets::loadIndex(inaccessibleRoot, &readable);
        check(!readable, "PA11 存在但不可读的索引不能冒充新用户空索引");
        const QString oversizedRoot = temp.path() + QStringLiteral("/oversized-index");
        QDir().mkpath(oversizedRoot);
        writeFileAt(Presets::indexPath(oversizedRoot), QByteArray(4 * 1024 * 1024 + 1, ' '));
        readable = true;
        Presets::loadIndex(oversizedRoot, &readable);
        check(!readable && !Presets::setDefault(oversizedRoot, created.id) &&
                  QFileInfo(Presets::indexPath(oversizedRoot)).size() == 4 * 1024 * 1024 + 1,
              "PA11 超大索引拒绝读取且默认项操作不覆盖原文件");
        QDir().mkpath(Presets::presetDir(oversizedRoot, created.id));
        const QString hugeManifest = Presets::manifestPath(oversizedRoot, created.id);
        writeFileAt(hugeManifest, QByteArray(16 * 1024 * 1024 + 1, ' '));
        QString reason;
        readable = true;
        Presets::loadManifest(oversizedRoot, created.id, &readable, &reason);
        check(!readable && reason.contains(QStringLiteral("16 MiB")) &&
                  QFileInfo(hugeManifest).size() == 16 * 1024 * 1024 + 1,
              "PA11 超大预设清单给出大小限制且不修改原文件", reason);
    }
    const QByteArray rottenIndex = QByteArrayLiteral("{\"format\":\"别人的/9\"}");
    writeFileAt(Presets::indexPath(root), rottenIndex);
    bool rottenOk = true;
    const Presets::PresetIndex rotten = Presets::loadIndex(root, &rottenOk);
    check(!rottenOk && rotten.order.isEmpty() && rotten.defaultPresetId.isEmpty(),
          "★ PA11 下标读不回来 ⇒ 空下标 + ok=false（那一位是给调用方看的：这一趟别写回去）");
    check(Presets::list(root).size() >= survivors.size(),
          "★★ PA11 下标整个废掉之后，盘上那些预设**还是列得出来**（读不回来的下标不连坐用户"
          "的东西）",
          QString::number(Presets::list(root).size()));
    check(Presets::remove(root, loose.id), "PA11 下标坏着也能删一份（目录那一趟照做）");
    QFile rottenFile(Presets::indexPath(root));
    const bool rottenOpened = rottenFile.open(QIODevice::ReadOnly);
    check(rottenOpened, "PA11 那份坏下标读得出来（接下来比的是**字节**，不是『能读回一点』）");
    const QByteArray afterRemove = rottenFile.readAll();
    rottenFile.close();
    check(afterRemove == rottenIndex,
          "★★ PA11 那一趟**没有把空下标写回去** —— 写回去 = 把用户排好的顺序抹平，而屏幕上"
          "什么都不说");
    check(!Presets::move(root, created.id, 1) && !Presets::setDefault(root, created.id),
          "PA11 次序和默认项那两件在下标坏着时**回 false**（不动盘、也不假装成功）");
}

