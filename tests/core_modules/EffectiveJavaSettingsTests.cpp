#include "Support.hpp"

void runEffectiveJavaSettingsTests(const QString &sandbox) {
    section("R. 每实例设置的合成（resolveJavaSettings / 覆盖的读写）");

    // 全局那一层的样板值。五项各不相同、都不是边界值 —— 这样"某一项被改错了"
    // 在下面每一条断言里都看得出来。
    const EffectiveJavaSettings global = [] {
        EffectiveJavaSettings g;
        g.javaPath = QStringLiteral("C:/java/global/bin/java.exe");
        g.xmx = 4096;
        g.xms = 1024;
        g.autoMemory = false;
        g.extraJvmArgs = QStringLiteral("-XX:+UseG1GC");
        return g;
    }();

    /// 两份额值的差异（字段名列表）。用来钉"只动了这一项"。
    const auto diff = [](const EffectiveJavaSettings &a, const EffectiveJavaSettings &b) {
        QStringList fields;
        if (a.javaPath != b.javaPath) {
            fields << QStringLiteral("javaPath");
        }
        if (a.xmx != b.xmx) {
            fields << QStringLiteral("xmx");
        }
        if (a.xms != b.xms) {
            fields << QStringLiteral("xms");
        }
        if (a.autoMemory != b.autoMemory) {
            fields << QStringLiteral("autoMemory");
        }
        if (a.extraJvmArgs != b.extraJvmArgs) {
            fields << QStringLiteral("extraJvmArgs");
        }
        return fields;
    };
    const auto onlyChanged = [&](const JavaSettingsOverride &ov, const QString &field) {
        return diff(resolveJavaSettings(global, ov), global) == QStringList{field};
    };

    // ---- R1 一项都不覆盖：有效值就是全局那一份 ----
    {
        const EffectiveJavaSettings got = resolveJavaSettings(global, JavaSettingsOverride());
        check(diff(global, got).isEmpty(), "R1 没有任何覆盖时有效值 == 全局逐字段",
              diff(global, got).join(QStringLiteral(" | ")));
    }

    // ---- R2 逐项覆盖：只动被覆盖的那一项 ----
    {
        JavaSettingsOverride ov;
        ov.javaPath = QStringLiteral("D:/jdk8/bin/java.exe");
        check(onlyChanged(ov, QStringLiteral("javaPath")),
              "R2 只覆盖 javaPath 时其余四项一个字不动");
    }
    {
        JavaSettingsOverride ov;
        ov.xmx = 8192;
        check(onlyChanged(ov, QStringLiteral("xmx")), "R2 只覆盖 Xmx（8192，在范围内）");
    }
    {
        JavaSettingsOverride ov;
        ov.xms = 512;
        check(onlyChanged(ov, QStringLiteral("xms")), "R2 只覆盖 Xms");
    }
    {
        // 覆盖里的 autoMemory **不再参与合成**（界面上「内存分配」那一档
        // 撤掉了）。老实例的 meta 里留着 true 时，有效值必须逐字段等于全局 —— 认了它，
        // 用户拖出来的数每次启动都会被重算掉，而界面上没有关掉它的入口。
        JavaSettingsOverride ov;
        ov.autoMemory = true;
        const EffectiveJavaSettings got = resolveJavaSettings(global, ov);
        check(diff(global, got).isEmpty(),
              "R2 覆盖里的 autoMemory 被忽略：有效值 == 全局逐字段",
              diff(global, got).join(QStringLiteral(" | ")));
    }
    {
        JavaSettingsOverride ov;
        ov.extraJvmArgs = QString();
        const EffectiveJavaSettings got = resolveJavaSettings(global, ov);
        check(onlyChanged(ov, QStringLiteral("extraJvmArgs")),
              "R2 只覆盖额外 JVM 参数时其余四项不动");
        check(got.extraJvmArgs.isEmpty(),
              "R2 空串是**真覆盖**：有效值是空的，不是全局那份 -XX:+UseG1GC",
              got.extraJvmArgs);
    }

    // ---- R3 钳位：覆盖层要把全局那条规则重做一遍 ----
    // 用户只覆盖一项、另一项还是全局的，两个数凑在一起就可能越界。
    {
        const auto resolved = [&](const JavaSettingsOverride &ov) {
            return resolveJavaSettings(global, ov);
        };

        JavaSettingsOverride ov;
        ov.xmx = 128;
        check(resolved(ov).xmx == 256, "R3 覆盖的 Xmx 低于 256 → 抬到 256",
              QString::number(resolved(ov).xmx));

        ov = JavaSettingsOverride();
        ov.xms = 64;
        check(resolved(ov).xms == 128, "R3 覆盖的 Xms 低于 128 → 抬到 128",
              QString::number(resolved(ov).xms));

        // 只覆盖 Xms、而它比有效 Xmx 大：JVM 会拒绝启动（Initial heap size ... larger
        // than the maximum heap size），所以必须压到 Xmx 上。
        ov = JavaSettingsOverride();
        ov.xms = 8192;
        check(resolved(ov).xms == 4096 && resolved(ov).xmx == 4096,
              "R3 只覆盖 Xms 且大于有效 Xmx → 压到 Xmx（Xmx 本身不动）",
              QStringLiteral("%1 / %2").arg(resolved(ov).xms).arg(resolved(ov).xmx));

        // 反方向：只覆盖 Xmx、比全局那个 Xms 还小 —— 全局的 Xms 要跟着退下来
        ov = JavaSettingsOverride();
        ov.xmx = 512;
        check(resolved(ov).xms == 512,
              "R3 覆盖的 Xmx 比全局 Xms 还小时，Xms 跟着退到 Xmx（不是留着 1024）",
              QString::number(resolved(ov).xms));

        // 两个都覆盖、方向相反：以 Xmx 为准
        ov = JavaSettingsOverride();
        ov.xmx = 512;
        ov.xms = 2048;
        check(resolved(ov).xms == 512 && resolved(ov).xmx == 512,
              "R3 两个都覆盖且 Xms > Xmx → Xms 被压到有效 Xmx",
              QStringLiteral("%1 / %2").arg(resolved(ov).xms).arg(resolved(ov).xmx));

        // 正好压在下限上：不该被抬走
        ov = JavaSettingsOverride();
        ov.xmx = 256;
        ov.xms = 128;
        check(resolved(ov).xmx == 256 && resolved(ov).xms == 128,
              "R3 正好在下限上（256 / 128）不动它");
    }

    // ---- R4 从 meta 读覆盖：类型不对当没写，null 当没有 ----
    {
        check(javaOverrideFromJson(QJsonObject()).isEmpty(), "R4 空对象 → 一项覆盖都没有");
        check(javaOverrideFromJson(QJsonObject{{QStringLiteral("javaPath"), QJsonValue::Null}})
                  .isEmpty(),
              "R4 值是 null → 当没有（手写或删键留下的形态，不是空路径覆盖）");
        check(javaOverrideFromJson(QJsonObject{{QStringLiteral("xmx"), QStringLiteral("8192")}})
                  .isEmpty(),
              "R4 xmx 写成了字符串 → 当没有（**不是**当成 0）");
        check(javaOverrideFromJson(QJsonObject{{QStringLiteral("autoMemory"), 1}}).isEmpty(),
              "R4 autoMemory 写成数字 → 当没有");
        // 非整数的 double：**自己截断**。这一条钉的是一个实测过的 Qt 行为：
        // QJsonValue::toInt() 遇到 4096.5 返回的是**默认值 0**（它不截断，探针见
        // tmp/probe_toint），而那个 0 一路走到夹取那一段就变成"用户设了 4096 MB、起步内存
        // 256 MB"这种**没人报错**的错值。所以截断是我们自己做的，而且下限以下/上限以上
        // 一律当"没写"（下游认得的意思只有这一个）。
        const JavaSettingsOverride truncated = javaOverrideFromJson(QJsonObject{
            {QStringLiteral("xmx"), 4096.5}, {QStringLiteral("xms"), 1024.9}});
        check(truncated.xmx.has_value() && *truncated.xmx == 4096 &&
                  truncated.xms.has_value() && *truncated.xms == 1024,
              "R4 4096.5 → 4096（截断，不是丢掉、更不是变成 0）",
              QStringLiteral("%1 / %2")
                  .arg(truncated.xmx.has_value() ? *truncated.xmx : -1)
                  .arg(truncated.xms.has_value() ? *truncated.xms : -1));
        check(javaOverrideFromJson(QJsonObject{{QStringLiteral("xmx"), 1e30}}).isEmpty() &&
                  javaOverrideFromJson(QJsonObject{{QStringLiteral("xmx"), -1e30}}).isEmpty(),
              "R4 越界的 double（1e30 / -1e30）→ 当没写（强转是未定义行为）");

        // 空串在这一层是**有值**的：这一层照文件里写的读，"空 = 跟随全局"那条归一是
        // 对话框那一层的事（见 InstanceSettingsDialog::override()）。
        const JavaSettingsOverride emptyPath =
            javaOverrideFromJson(QJsonObject{{QStringLiteral("javaPath"), QString()}});
        check(emptyPath.javaPath.has_value() && emptyPath.javaPath->isEmpty(),
              "R4 空串是有值 —— 这一层不做归一，照读");
        check(!emptyPath.isEmpty(), "R4 有值时 isEmpty() 为假");

        const JavaSettingsOverride numbers = javaOverrideFromJson(QJsonObject{
            {QStringLiteral("xmx"), 8192},
            {QStringLiteral("xms"), 2048.0},
            {QStringLiteral("autoMemory"), true}});
        check(numbers.xmx.has_value() && *numbers.xmx == 8192 && numbers.xms.has_value() &&
                  *numbers.xms == 2048 && numbers.autoMemory.has_value() && *numbers.autoMemory,
              "R4 正常的数字 / 布尔读得出来");
    }

    // ---- R5 写回 meta 的那一份 JSON ----
    {
        const QJsonObject none = javaOverrideToJson(JavaSettingsOverride());
        QStringList nullKeys;
        for (const QString &key : none.keys()) {
            if (none.value(key).isNull()) {
                nullKeys << key;
            }
        }
        nullKeys.sort();
        check(nullKeys == QStringList({QStringLiteral("autoMemory"), QStringLiteral("extraJvmArgs"),
                                       QStringLiteral("javaPath"), QStringLiteral("xms"),
                                       QStringLiteral("xmx")}),
              "R5 一项覆盖都没有时五个键都在、值都是 null（= 让写侧把键删掉）",
              nullKeys.join(QStringLiteral(",")));

        JavaSettingsOverride ov;
        ov.xmx = 8192;
        ov.extraJvmArgs = QString();
        const QJsonObject json = javaOverrideToJson(ov);
        check(json.value(QStringLiteral("xmx")).toInt() == 8192 &&
                  json.value(QStringLiteral("javaPath")).isNull(),
              "R5 有值的写值、没值的写 null");
        check(json.value(QStringLiteral("extraJvmArgs")).isString() &&
                  json.value(QStringLiteral("extraJvmArgs")).toString().isEmpty(),
              "R5 空串写成**空字符串**而不是 null —— 它是一项真的覆盖");

        const JavaSettingsOverride back = javaOverrideFromJson(json);
        check(back.xmx.has_value() && *back.xmx == 8192 && back.extraJvmArgs.has_value() &&
                  back.extraJvmArgs->isEmpty() && !back.xms.has_value(),
              "R5 写出去再读回来是同一份覆盖");
    }

    // ---- R6 真盘一趟：覆盖写进 wtfml.meta，再读回来 ----
    {
        const QString versionId = QStringLiteral("实例设置版本");
        const QString versionDir = AppPaths::versionDir(versionId);
        QDir(versionDir).removeRecursively();
        QDir().mkpath(versionDir);
        VersionManager vm;

        JavaSettingsOverride ov;
        ov.xmx = 8192;
        ov.autoMemory = false;
        ov.extraJvmArgs = QStringLiteral("-XX:+PerfDisableSharedMem");
        QString error;
        check(vm.updateVersionMeta(versionId, javaOverrideToJson(ov), &error),
              "R6 覆盖写进 wtfml.meta", error);

        const JavaSettingsOverride back = javaOverrideFromJson(vm.versionMeta(versionId));
        check(back.xmx.has_value() && *back.xmx == 8192 && back.autoMemory.has_value() &&
                  !*back.autoMemory && back.extraJvmArgs.has_value(),
              "R6 读回来是写进去的那一份");
        {
            const EffectiveJavaSettings got = resolveJavaSettings(global, back);
            check(got.xmx == 8192 && got.xms == 1024 && got.javaPath == global.javaPath &&
                      got.extraJvmArgs == QStringLiteral("-XX:+PerfDisableSharedMem"),
                  "R6 合成之后：覆盖的两项听实例的，其余三项还是全局的",
                  QStringLiteral("%1 / %2 / %3").arg(got.xmx).arg(got.xms).arg(got.extraJvmArgs));
        }

        // 追加参数（jvmArgs）和覆盖是同一个文件里**两个不同的键**，各管各的：写覆盖
        // 不能把它抹掉。（这两件事在真机上会同时出现在一个 wtfml.meta 里。）
        check(vm.updateVersionMeta(
                  versionId,
                  {{VersionManager::jvmArgsField(), QJsonArray{QStringLiteral("-Xss1M")}}}, &error),
              "R6 往同一个文件里写追加参数", error);
        check(vm.updateVersionMeta(versionId, javaOverrideToJson(JavaSettingsOverride()), &error),
              "R6 把它全部切回「跟随全局」", error);
        check(vm.versionJvmArgs(versionId) == QStringList{QStringLiteral("-Xss1M")},
              "R6 写覆盖时没有把追加参数抹掉",
              vm.versionJvmArgs(versionId).join(QStringLiteral(" | ")));
        {
            const QJsonObject raw = vm.versionMeta(versionId);
            check(!raw.contains(QStringLiteral("xmx")) &&
                      !raw.contains(QStringLiteral("extraJvmArgs")) &&
                      !raw.contains(QStringLiteral("autoMemory")),
                  "R6 「跟随全局」真的把键从文件里删了（不是留一个等于全局的值）",
                  QString::fromUtf8(QJsonDocument(raw).toJson(QJsonDocument::Compact)));
            check(javaOverrideFromJson(raw).isEmpty(), "R6 再读就是一项覆盖都没有");
        }

        QDir(versionDir).removeRecursively();
        check(!QDir(versionDir).exists(), "R6 沙盒里那个版本目录删干净了");
    }

    // ---- R7 「值 → 要写进 meta 的那批字段」整批（javaOverrideMetaFields） ----
    // 这是写侧真正交给 updateVersionMeta() 的东西：五项加追加参数一起。它收的是纯值，
    // 验证工程能直接喂值、比对 JSON —— 收对话框引用的话自定义类型进不了 Q_ARG，
    // "清空追加参数之后那个键还在不在"这种事就没有断言盯得住。
    {
        const QString jvmKey = VersionManager::jvmArgsField();

        // ① 全部「跟随全局」+ 没有追加参数：六个键都在，值全是 Null（= 让写侧删键）。
        {
            const QJsonObject fields =
                javaOverrideMetaFields(JavaSettingsOverride(), QStringList(), jvmKey);
            check(fields.size() == 6, "R7 六个键一个都不漏（五项 + 追加参数）",
                  QString::number(fields.size()));
            bool allNull = true;
            for (const QString &key : fields.keys()) {
                allNull = allNull && fields.value(key).isNull();
            }
            check(allNull, "R7 一项都没覆盖时六个键全是 Null（写侧照它删键）",
                  QString::fromUtf8(QJsonDocument(fields).toJson(QJsonDocument::Compact)));
        }

        // ② 有覆盖 + 有追加参数：覆盖写值，追加参数写数组（条数和顺序都要对）。
        {
            JavaSettingsOverride ov;
            ov.javaPath = QStringLiteral("D:/jdk8/java.exe");
            ov.xms = 512;
            const QJsonObject fields = javaOverrideMetaFields(
                ov, QStringList({QStringLiteral("-Xss1M"), QStringLiteral("-Dfoo=bar baz")}),
                jvmKey);
            check(fields.value(QStringLiteral("javaPath")).toString() ==
                          QStringLiteral("D:/jdk8/java.exe") &&
                      fields.value(QStringLiteral("xms")).toInt() == 512,
                  "R7 覆盖了的那两项写的是值");
            check(fields.value(QStringLiteral("xmx")).isNull(),
                  "R7 没覆盖的那几项还是 Null（不是顺手写一个等于全局的值）");
            const QJsonArray args = fields.value(jvmKey).toArray();
            check(args.size() == 2 && args.at(0).toString() == QStringLiteral("-Xss1M") &&
                      args.at(1).toString() == QStringLiteral("-Dfoo=bar baz"),
                  "R7 追加参数写成数组，含空格的那一条**没有被切开**",
                  QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact)));
        }

        // ③ 真盘一趟：清空追加参数之后那个键要**从文件里消失**（不是留一个空数组）。
        // 这条是 R7 存在的理由：留着一个空数组时读侧当空、界面上也看不出来，但手写
        // 这个文件的人会以为该往里填 —— 而"这个实例没有追加参数"就是键不存在。
        {
            const QString versionId = QStringLiteral("实例设置字段版本");
            const QString versionDir = AppPaths::versionDir(versionId);
            QDir(versionDir).removeRecursively();
            QDir().mkpath(versionDir);
            VersionManager vm;
            QString error;

            check(vm.updateVersionMeta(
                      versionId,
                      javaOverrideMetaFields(JavaSettingsOverride(),
                                             QStringList({QStringLiteral("-Xss1M")}), jvmKey),
                      &error),
                  "R7 先写一份带追加参数的", error);
            check(vm.versionJvmArgs(versionId) == QStringList{QStringLiteral("-Xss1M")},
                  "R7 读回来就是那一条");
            check(vm.updateVersionMeta(
                      versionId,
                      javaOverrideMetaFields(JavaSettingsOverride(), QStringList(), jvmKey),
                      &error),
                  "R7 再把追加参数清空重写一遍", error);
            const QJsonObject raw = vm.versionMeta(versionId);
            check(!raw.contains(jvmKey), "R7 那个键真的从文件里没了（不是留一个空数组）",
                  QString::fromUtf8(QJsonDocument(raw).toJson(QJsonDocument::Compact)));
            check(vm.versionJvmArgs(versionId).isEmpty(), "R7 读侧也当它没有");

            QDir(versionDir).removeRecursively();
        }
    }
}

