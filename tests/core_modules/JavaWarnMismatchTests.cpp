#include "Support.hpp"

void runJavaWarnMismatchTests(const QString &sandbox) {
    section("S. 实例 Java 版本不符时的提醒开关（全局总闸 + 每实例记忆）");

    // ---- S1 全局总闸：默认提醒、改了立刻落盘 ----
    // 真值来源是 QSettings，照 L3 那套先把 QSettings 搬进沙盒（**必须在第一个
    // QSettings 对象出现之前**），跑完再搬回去。
    {
        const QString settingsDir = sandbox + "/warn-mismatch-settings";
        QDir(settingsDir).removeRecursively();
        QDir().mkpath(settingsDir);

        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir);

        // 盘上是个没有这个开关的老配置：有别的 Java 键，就是没有这一个
        {
            QSettings raw;
            raw.clear();
            raw.setValue(QStringLiteral("Java/Path"), QStringLiteral("C:/java/bin/java.exe"));
            raw.sync();
        }
        {
            GlobalJavaSettings fresh;
            check(fresh.warnVersionMismatch(),
                  "S1 老配置（盘上没有这个键）默认是「提醒」—— 那正是加它之前的行为");
        }
        {
            GlobalJavaSettings s;
            s.setWarnVersionMismatch(false);
            // 换个对象读：验的是真落了盘，不是留在成员变量里
            GlobalJavaSettings reopened;
            check(!reopened.warnVersionMismatch(), "S1 关掉之后立刻落盘，另一个对象读回来是关的");
            check(!QSettings().value(QStringLiteral("Java/WarnVersionMismatch")).toBool(),
                  "S1 盘上那个键真的写成了 false（不是只改了内存）");
            reopened.setWarnVersionMismatch(true);
            GlobalJavaSettings back;
            check(back.warnVersionMismatch(), "S1 再打开一次也一样落盘");
        }

        QSettings::setDefaultFormat(QSettings::NativeFormat);
        QDir(settingsDir).removeRecursively();
    }

    // ---- S2 每实例记忆：wtfml.meta 里的 skipJavaVersionWarning ----
    // 「缺省 = 提醒」是这一节的全部要害：盘上没有这个键和写着 false 是**两种不同的
    // 局面**（一个是从来没表过态，一个是表过态说别问了），而读侧对两者都返回 false
    // 是刻意的 —— 它问的是"要不要提醒"。所以反向那条（改回提醒）必须**把键删掉**，
    // 不能让文件里留一个 false 占位：那会让"这个实例表过态"看起来永远成立。
    {
        const QString versionId = QStringLiteral("版本提醒开关版本");
        const QString versionDir = AppPaths::versionDir(versionId);
        QDir(versionDir).removeRecursively();
        QDir().mkpath(versionDir);
        VersionManager vm;

        check(VersionManager::skipJavaVersionWarningField() ==
                  QStringLiteral("skipJavaVersionWarning"),
              "S2 键名是约定的那一个（读侧写侧共用这个出处）",
              VersionManager::skipJavaVersionWarningField());
        check(javaPathMetaField() == QStringLiteral("javaPath"),
              "S2 javaPath 那个键名的出处也是约定的（写侧要拿它写回匹配的 Java）",
              javaPathMetaField());
        check(!vm.skipJavaVersionWarning(versionId),
              "S2 盘上还没有这个键 → 缺省是**提醒**（= 加这个功能之前的行为）");
        check(!vm.skipJavaVersionWarning(QStringLiteral("根本没有这个实例")),
              "S2 连 meta 文件都没有的实例也当提醒，不炸");

        QString error;
        check(vm.updateVersionMeta(
                  versionId,
                  {{VersionManager::jvmArgsField(), QJsonArray{QStringLiteral("-Xss1M")}}}, &error),
              "S2 先往同一份 meta 里放一份追加参数", error);
        check(vm.updateVersionMeta(
                  versionId,
                  {{VersionManager::skipJavaVersionWarningField(), QJsonValue(true)}}, &error),
              "S2 写「以后不再提醒这个实例」", error);
        check(vm.skipJavaVersionWarning(versionId), "S2 读回来是 true（真的落盘了）");
        check(vm.versionJvmArgs(versionId) == QStringList{QStringLiteral("-Xss1M")},
              "S2 合并写没有把同一份 meta 里的追加参数抹掉",
              vm.versionJvmArgs(versionId).join(QStringLiteral(" | ")));

        // 手写文件会干的事：类型写坏。宁可多问一句，也不要因为一个坏值从此静默不问。
        check(vm.updateVersionMeta(
                  versionId, {{VersionManager::skipJavaVersionWarningField(), QJsonValue(1)}},
                  &error),
              "S2 把那个键写坏成数字", error);
        check(!vm.skipJavaVersionWarning(versionId),
              "S2 类型不对 → 当提醒（在下面那条 qWarning 之外静默地退回缺省）");

        check(vm.updateVersionMeta(
                  versionId, {{VersionManager::skipJavaVersionWarningField(),
                              QJsonValue(QJsonValue::Null)}}, &error),
              "S2 改回提醒（写 Null = 让写侧删键）", error);
        check(!vm.skipJavaVersionWarning(versionId), "S2 读回来是 false（= 提醒）");
        check(!vm.versionMeta(versionId).contains(VersionManager::skipJavaVersionWarningField()),
              "S2 那个键真的从文件里没了，不是留一个 false 在那儿占位",
              QString::fromUtf8(
                  QJsonDocument(vm.versionMeta(versionId)).toJson(QJsonDocument::Compact)));
        check(vm.versionJvmArgs(versionId) == QStringList{QStringLiteral("-Xss1M")},
              "S2 删键那一次也没碰追加参数");

        QDir(versionDir).removeRecursively();
    }

    // ---- S3 allowMismatchedPath：那个口子只可能由用户的明确授权打开 ----
    // 用一个**压根不存在**的路径来证这件事：它在 javaMatchesMajor() 那里必然为假
    // （那一步会去冷启一个 JVM，起不来），如果这个参数判在那个函数**之后**，这里
    // 就一定返回 false。返回 true 说明它判在前面、一条探测都没做 —— 而除了
    // 「Java 版本不符」那个框的「仍然启动」，没有任何调用点会把它置真。
    {
        GlobalJavaSettings settings;
        EffectiveJavaSettings io;
        io.javaPath = QStringLiteral("C:/这个目录不存在/java.exe");
        QString error;
        check(!settings.ensureJavaForVersion(io, 17, QString(), /*allowAutoSelect=*/false, &error),
              "S3 前景：手动档 + 一个探不出主版本的路径 → 拒绝（现有的硬错误原样成立）");
        error.clear();
        check(settings.ensureJavaForVersion(io, 17, QString(), /*allowAutoSelect=*/false, &error,
                                            /*allowMismatchedPath=*/true),
              "S3 同一个路径，用户明确授权之后放行（口子在探测之前）", error);
        check(io.javaPath == QStringLiteral("C:/这个目录不存在/java.exe"),
              "S3 放行时不动那条路径 —— 用户要的就是它", io.javaPath);
        check(error.isEmpty(), "S3 放行那一次没有留下失败原因", error);
    }
}

