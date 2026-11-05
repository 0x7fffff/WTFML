#include "Support.hpp"
#include "manage/InstanceMetaFile.hpp"

int runVersionMetaWriteTests(const QString &sandbox) {
    section("O. wtfml.meta 写侧（合并写、坏文件拒写、稳定 id）");

    const QString versionsRoot = sandbox + "/versions";
    const QString id = QStringLiteral("O段-元数据");
    const QString metaPath = versionsRoot + "/" + id + "/wtfml.meta";
    writeFileAt(versionsRoot + "/" + id + "/" + id + ".jar", "dummy");

    const auto readMetaFile = [&]() {
        QFile f(metaPath);
        if (!f.open(QIODevice::ReadOnly)) {
            return QJsonObject();
        }
        return QJsonDocument::fromJson(f.readAll()).object();
    };
    const auto writeMetaFile = [&](const QByteArray &bytes) { writeFileAt(metaPath, bytes); };

    VersionManager vm;

    // ---- 合并写：只动点名要改的键，用户手写的那几个原样留着 ----
    // 用真字段名（name / loaderName / …）。NOTE: 显示名在文件里叫 `name`，
    // 而 VersionDisplayParts 里那个成员叫 `customName` —— 这一段的重点之一就是
    // "写进去的东西读侧真的看得见"，两边用不同的名字正是最容易写错的地方。
    writeMetaFile(R"({"name":"我的整合包","loaderName":"Forge","loaderVersion":"47.2.0",
                      "jvmArgs":["-Dfoo=bar baz"],"realVersion":"1.20.1"})");
    {
        QJsonObject fields;
        fields.insert(QStringLiteral("name"), QStringLiteral("改过的名字"));
        QString error;
        check(vm.updateVersionMeta(id, fields, &error), "O1 合并写成功", error);
    }
    {
        const QJsonObject after = readMetaFile();
        check(after.value(QStringLiteral("name")).toString() == QStringLiteral("改过的名字"),
              "O1 点名的那个键被改掉了");
        // 写侧 → 读侧的往返：displayParts() 是这套字段真正的消费者，它读得到才算数。
        check(vm.displayParts(id).customName == QStringLiteral("改过的名字"),
              "O1 写进去的显示名，读侧（displayParts）真的看得见",
              vm.displayParts(id).customName);
        check(vm.displayParts(id).loaderName == QStringLiteral("Forge") &&
                  vm.displayParts(id).loaderVersion == QStringLiteral("47.2.0"),
              "O1 读侧看到的加载器字段还是老样子");
        check(after.value(QStringLiteral("loaderName")).toString() == QStringLiteral("Forge") &&
                  after.value(QStringLiteral("loaderVersion")).toString() ==
                      QStringLiteral("47.2.0"),
              "O1 没点名的加载器字段原样保留（没被整体覆盖）");
        check(after.value(QStringLiteral("realVersion")).toString() == QStringLiteral("1.20.1"),
              "O1 realVersion 也留着");
        const QJsonArray jvm = after.value(QStringLiteral("jvmArgs")).toArray();
        check(jvm.size() == 1 && jvm.first().toString() == QStringLiteral("-Dfoo=bar baz"),
              "O1 用户手写的 jvmArgs 逐字保留（含空格那条）");
    }

    // ---- 传 null 是"删掉这个键"，不是"写成 null" ----
    {
        QJsonObject fields;
        fields.insert(QStringLiteral("loaderName"), QJsonValue::Null);
        QString error;
        check(vm.updateVersionMeta(id, fields, &error), "O2 传 null 删字段成功", error);
        const QJsonObject after = readMetaFile();
        check(!after.contains(QStringLiteral("loaderName")),
              "O2 那个键真的不在了（不是留了个 null）");
        check(after.contains(QStringLiteral("loaderVersion")),
              "O2 旁边的键没被牵连");
    }

    // ---- 坏文件：拒绝写，且一个字节都不动 ----
    writeMetaFile("{ 这不是 JSON");
    {
        QByteArray before;
        {
            QFile f(metaPath);
            if (f.open(QIODevice::ReadOnly)) {
                before = f.readAll();
            }
        }
        QJsonObject fields;
        fields.insert(QStringLiteral("name"), QStringLiteral("不该写进去"));
        QString error;
        check(!vm.updateVersionMeta(id, fields, &error), "O3 坏文件时拒绝写");
        check(!error.isEmpty(), "O3 拒绝写时给出了能看懂的原因", error);
        QByteArray after;
        {
            QFile f(metaPath);
            if (f.open(QIODevice::ReadOnly)) {
                after = f.readAll();
            }
        }
        check(after == before, "O3 被拒绝时原文件逐字节不变（用户写坏的内容还在，没被吞掉）");
    }

    // ---- 没有这个文件时可以新建 ----
    QFile::remove(metaPath);
    {
        QJsonObject fields;
        fields.insert(QStringLiteral("name"), QStringLiteral("新建的"));
        QString error;
        check(vm.updateVersionMeta(id, fields, &error), "O4 文件不存在时能建出来", error);
        check(vm.displayParts(id).customName == QStringLiteral("新建的"),
              "O4 建出来的内容对（读侧看得见）", vm.displayParts(id).customName);
    }

    // ---- 稳定 id：只生成一次，第二次读回来的必须是同一个 ----
    QFile::remove(metaPath);
    {
        check(vm.instanceId(id).isEmpty(), "O5 没写过时没有稳定 id（读侧不生成）");
        check(vm.instanceId(id).isEmpty(), "O5 再读一次还是没有（没有偷偷写盘）");
        check(!QFile::exists(metaPath), "O5 只读不会把文件造出来");

        const QString first = vm.ensureInstanceId(id);
        check(!first.isEmpty(), "O5 ensureInstanceId 拿到了一个 id", first);
        check(QFile::exists(metaPath), "O5 这时文件真的落盘了");
        check(vm.instanceId(id) == first, "O5 从盘上读得回来（不是只在内存里）");
        check(vm.ensureInstanceId(id) == first, "O5 再 ensure 一次是同一个 id（幂等）");

        // 换一个 VersionManager 读：它不知道内存里的任何东西，只能靠盘上的文件。
        VersionManager other;
        check(other.instanceId(id) == first, "O5 换一个对象读到的还是同一个（真的落盘了）");
    }
    {
        // 稳定 id 和显示名能共存，互不干扰。
        QJsonObject fields;
        fields.insert(QStringLiteral("name"), QStringLiteral("带 id 的实例"));
        check(vm.updateVersionMeta(id, fields), "O6 有 id 之后再写别的字段照样成功");
        check(!vm.instanceId(id).isEmpty(), "O6 稳定 id 没被后续写入挤掉");
        check(vm.displayParts(id).customName == QStringLiteral("带 id 的实例"),
              "O6 显示名也写进去了（读侧看得见）", vm.displayParts(id).customName);
    }
    {
        // 手改坏的 instanceId（类型不对）当没有处理 —— 下次 ensure 会生成一个新的覆盖它。
        writeMetaFile(R"({"instanceId": 12345, "name":"id 被写坏了"})");
        check(vm.instanceId(id).isEmpty(), "O7 instanceId 不是字符串时当没有（不返回垃圾）");
        const QString regenerated = vm.ensureInstanceId(id);
        check(!regenerated.isEmpty() && !regenerated.contains(QStringLiteral("12345")),
              "O7 坏值被一个新的 UUID 顶掉", regenerated);
        check(vm.instanceId(id) == regenerated, "O7 换回来的新 id 读得回来");
        check(readMetaFile().value(QStringLiteral("name")).toString() ==
                  QStringLiteral("id 被写坏了"),
              "O7 顶掉坏 id 时没把旁边的显示名一起抹掉");
    }

    // ---- 格式版本：写出来的文件带 schemaVersion，老的（没这个键）按 1 认 ----
    // 加这个键的意义是**限住更新的启动器写过的文件**：老启动器按 1 的规矩合并一份 2
    // 的文件，可能把它新加的字段解释错、再原样盖回去。
    {
        writeMetaFile(R"({"name":"没有版本号的老文件","jvmArgs":["-Dkeep=me"]})");
        QJsonObject fields;
        fields.insert(QStringLiteral("realVersion"), QStringLiteral("1.20.1"));
        QString error;
        check(vm.updateVersionMeta(id, fields, &error), "O9 没有版本号的老文件照样能写", error);
        const QJsonObject after = readMetaFile();
        check(after.value(QStringLiteral("schemaVersion")).toInt() == 1,
              "O9 写一次之后文件里带上了 schemaVersion = 1",
              QString::number(after.value(QStringLiteral("schemaVersion")).toInt()));
        check(after.value(QStringLiteral("name")).toString() == QStringLiteral("没有版本号的老文件") &&
                  after.value(QStringLiteral("jvmArgs")).toArray().size() == 1,
              "O9 补版本号时没把老内容一起换掉");
        check(vm.displayParts(id).customName == QStringLiteral("没有版本号的老文件"),
              "O9 补了版本号读侧照样看得见");
    }

    // ---- 版本比这个启动器新：只读不写，一个字节都不动 ----
    writeMetaFile(R"({"schemaVersion":2,"name":"更新版写的","newField":{"a":1}})");
    {
        QByteArray before;
        {
            QFile f(metaPath);
            if (f.open(QIODevice::ReadOnly)) {
                before = f.readAll();
            }
        }
        QJsonObject fields;
        fields.insert(QStringLiteral("name"), QStringLiteral("不该写进去"));
        QString error;
        check(!vm.updateVersionMeta(id, fields, &error), "O10 版本比这个新时拒绝写");
        check(error.contains(QStringLiteral("2")), "O10 拒绝时把那个版本号说出来了", error);
        QByteArray after;
        {
            QFile f(metaPath);
            if (f.open(QIODevice::ReadOnly)) {
                after = f.readAll();
            }
        }
        check(after == before, "O10 被拒绝时原文件逐字节不变（新版本写的字段还在）");
        // 读侧不跟着瞎：显示名这种字段的意思没变过，2 的文件照样要读得出来。
        check(vm.displayParts(id).customName == QStringLiteral("更新版写的"),
              "O10 版本新不影响读（读侧照旧看得见显示名）",
              vm.displayParts(id).customName);
    }

    // ---- 版本号本身写坏了（不是整数）：同样拒写 ----
    writeMetaFile(R"({"schemaVersion":"2","name":"版本号是字符串"})");
    {
        QByteArray before;
        {
            QFile f(metaPath);
            if (f.open(QIODevice::ReadOnly)) {
                before = f.readAll();
            }
        }
        QJsonObject fields;
        fields.insert(QStringLiteral("name"), QStringLiteral("不该写进去"));
        QString error;
        check(!vm.updateVersionMeta(id, fields, &error), "O11 版本号不是整数时拒绝写");
        check(!error.isEmpty(), "O11 拒绝时给出了原因", error);
        QByteArray after;
        {
            QFile f(metaPath);
            if (f.open(QIODevice::ReadOnly)) {
                after = f.readAll();
            }
        }
        check(after == before, "O11 被拒绝时原文件逐字节不变");
    }

    // ---- 读侧缓存：盘上那份被**外面**改过，下一次读必须看得见 ----
    //
    // 实例树每重灌一次就对同一个实例问三四遍（显示名一次、稳定 id 一次、"按加载器
    // 归到哪个默认组"两次），`readVersionMeta()` 因此按"存在与否 + 修改时刻 + 字节数"
    // 缓存一格。这一节钉的就是失效条件 —— 缓存最坏的错法是"改了名不生效"：
    // 用户手改了盘上那份（或者别的启动器写了它），界面上却还是老名字，而且一句话
    // 都不报。
    QFile::remove(metaPath);
    {
        writeMetaFile(R"({"name":"第一份"})");
        check(vm.displayParts(id).customName == QStringLiteral("第一份"), "O12 先读到第一份");
        writeMetaFile(R"({"name":"外面改的（比第一份长）"})");
        check(vm.displayParts(id).customName == QStringLiteral("外面改的（比第一份长）"),
              "★ O12 盘上那份被外面改大：下一次读就是新值（缓存没把它蒙住）",
              vm.displayParts(id).customName);
        writeMetaFile(R"({"name":"短"})");
        check(vm.displayParts(id).customName == QStringLiteral("短"),
              "★ O12 改成更小的一份也看得见（判据不是只认变大了）",
              vm.displayParts(id).customName);
        // 稳定 id 和显示名走的是同一格缓存：一次读命中要给两边同一个答案。
        const QString stableId = vm.ensureInstanceId(id);
        check(!stableId.isEmpty() && vm.instanceId(id) == stableId
                  && vm.displayParts(id).customName == QStringLiteral("短"),
              "O12 缓存命中的那一份对 instanceId / displayParts 是同一份（没各读各的）");
        {
            QJsonObject fields;
            fields.insert(QStringLiteral("name"), QStringLiteral("走写侧改的"));
            QString error;
            check(vm.updateVersionMeta(id, fields, &error), "O12 走写侧改写成功", error);
        }
        check(vm.displayParts(id).customName == QStringLiteral("走写侧改的"),
              "★ O12 写侧写完当场看得见（写的那一刻就把缓存那一格丢了）",
              vm.displayParts(id).customName);
        check(vm.instanceId(id) == stableId,
              "O12 写侧那一格丢掉之后，稳定 id 还在（重读的是真文件）");
        QFile::remove(metaPath);
        check(vm.displayParts(id).customName.isEmpty(),
              "★ O12 文件被删掉之后读侧回到「没有」（不是继续念缓存）");
        check(vm.instanceId(id).isEmpty(), "O12 删掉之后稳定 id 也回到空");
    }

    // ---------- O13. 「这批字段和盘上那份一样吗」= 要不要写盘的判据 ----------
    //
    // 实例设置是"改一格落一格"：打开看一眼、按确定，或者焦点移开触发一次
    // `editingFinished` 都会走一遍写盘。要写的每个键和盘上那份一字不差时再写一次只有坏处
    // （推 mtime、重扫树、把别的进程刚写的值按旧读数盖回去）。判据收在
    // `InstanceMetaFile::fieldsAlreadyEqual()`：null 只在盘上本来没有这个键时算相等。
    {
        const QJsonObject current{{QStringLiteral("javaPath"), QStringLiteral("C:/java")},
                                  {QStringLiteral("xmx"), 4096},
                                  {QStringLiteral("pages"),
                                   QJsonObject{{QStringLiteral("save"), false}}}};
        using InstanceMetaFile::fieldsAlreadyEqual;
        check(fieldsAlreadyEqual(current, current), "O13 完全一样 ⇒ 不用写");
        check(fieldsAlreadyEqual(current, QJsonObject{{QStringLiteral("xmx"), 4096}}),
              "O13 只比列出来的键（盘上多出来的键不算改动）");
        check(!fieldsAlreadyEqual(current, QJsonObject{{QStringLiteral("xmx"), 8192}}),
              "★ O13 改了一个数 ⇒ 要写");
        check(fieldsAlreadyEqual(current, QJsonObject{{QStringLiteral("autoMemory"), QJsonValue::Null}}),
              "★ O13 Null 且盘上本来没有这个键 ⇒ 不用写");
        check(!fieldsAlreadyEqual(current, QJsonObject{{QStringLiteral("xmx"), QJsonValue::Null}}),
              "★ O13 Null 但要删一个盘上真有的键 ⇒ 要写");
        check(!fieldsAlreadyEqual(current, QJsonObject{{QStringLiteral("xmx"), QStringLiteral("4096")}}),
              "★ O13 类型不同（数字 vs 字符串）⇒ 要写");
        check(!fieldsAlreadyEqual(current,
                                  QJsonObject{{QStringLiteral("pages"),
                                               QJsonObject{{QStringLiteral("save"), true}}}}),
              "★ O13 嵌套对象不同 ⇒ 要写");
        check(fieldsAlreadyEqual(current,
                                 QJsonObject{{QStringLiteral("pages"),
                                              QJsonObject{{QStringLiteral("save"), false}}}}),
              "★ O13 嵌套对象一样 ⇒ 不用写");
        check(!fieldsAlreadyEqual(current, QJsonObject{{QStringLiteral("missing"), 5}}),
              "★ O13 盘上没有、草稿有值 ⇒ 要写");
    }

    const bool cleaned = QDir(versionsRoot + "/" + id).removeRecursively();
    check(cleaned && !QDir(versionsRoot + "/" + id).exists(),
          "O8 用完的沙盒目录删干净了（句柄没被自己攥着）");
    return 0;
}


