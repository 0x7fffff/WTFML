#include "Support.hpp"

int runRenameInstanceTests(const QString &sandbox) {
    section("P. 重命名实例（名字校验 + 真改目录名）");

    const QString versionsRoot = sandbox + "/versions";

    // 造文件走 writeFileAt()（文件级那个 helper）：它写完就关句柄，理由见那儿的 NOTE。
    const auto writeFile = [](const QString &path, const QByteArray &bytes) {
        writeFileAt(path, bytes);
    };

    // ---------- P1 名字校验的真值表 ----------
    // 合法的那一半里，前几个是**真机上真实存在**的实例名，不是编的。
    {
        const QStringList legal = {
            QStringLiteral("我的整合包"),
            QStringLiteral("原版ATT2地图（无模组）"),
            QStringLiteral("26.2-Fabric 0.19.5"),
            QStringLiteral("1.18.2-Forge_40.3.12"),
            QStringLiteral("a"),
            QStringLiteral("带 空格 的 名字"),
            QStringLiteral("版本.1"),
            QStringLiteral("v1-测试_02"),
            QString(QStringLiteral("长").repeated(64)),
        };
        for (const QString &name : legal) {
            const QString problem = InstanceFiles::validateDirName(name);
            check(problem.isEmpty(), QStringLiteral("P1 合法：%1").arg(name), problem);
        }
    }
    {
        // 每一条都写清"为什么必须非法"，将来有人放松规则时这些会红给他看。
        const QList<QPair<QString, QString>> illegal = {
            {QString(), QStringLiteral("空名字")},
            {QStringLiteral("   "), QStringLiteral("全是空白")},
            {QStringLiteral("a/b"), QStringLiteral("斜杠（路径穿越）")},
            {QStringLiteral("a\\b"), QStringLiteral("反斜杠（路径穿越）")},
            {QStringLiteral("../跑出去"), QStringLiteral("相对路径（路径穿越）")},
            {QStringLiteral("."), QStringLiteral("单个点")},
            {QStringLiteral(".."), QStringLiteral("两个点")},
            {QStringLiteral("a:b"), QStringLiteral("冒号")},
            {QStringLiteral("a*b"), QStringLiteral("星号")},
            {QStringLiteral("a?b"), QStringLiteral("问号")},
            {QStringLiteral("a\"b"), QStringLiteral("双引号")},
            {QStringLiteral("a<b"), QStringLiteral("小于号")},
            {QStringLiteral("a>b"), QStringLiteral("大于号")},
            {QStringLiteral("a|b"), QStringLiteral("竖线")},
            {QStringLiteral("名字."), QStringLiteral("结尾的点（Windows 会静默吃掉）")},
            {QStringLiteral("a\tb"), QStringLiteral("制表符")},
            {QStringLiteral("CON"), QStringLiteral("保留设备名 CON")},
            {QStringLiteral("con"), QStringLiteral("保留设备名 con（不区分大小写）")},
            {QStringLiteral("CON.txt"), QStringLiteral("保留设备名带扩展名")},
            {QStringLiteral("COM1"), QStringLiteral("保留设备名 COM1")},
            {QStringLiteral("LPT9"), QStringLiteral("保留设备名 LPT9")},
            {QStringLiteral("NUL"), QStringLiteral("保留设备名 NUL")},
            {QStringLiteral("CONIN$"), QStringLiteral("保留设备名 CONIN$（控制台输入口）")},
            {QStringLiteral("conout$"), QStringLiteral("保留设备名 conout$（大小写不区分）")},
            {QStringLiteral("COM¹"), QStringLiteral("保留设备名 COM¹（U+00B9，Windows 认）")},
            {QStringLiteral("LPT³"), QStringLiteral("保留设备名 LPT³（U+00B3）")},
            {QString(QStringLiteral("长").repeated(65)), QStringLiteral("65 个字符（超一个）")},
        };
        for (const auto &entry : illegal) {
            const QString problem = InstanceFiles::validateDirName(entry.first);
            check(!problem.isEmpty(), QStringLiteral("P1 非法：%1").arg(entry.second));
        }
    }
    {
        // 反过来那一侧：**不是**保留名的不能被误判。COM0 / LPT0 看着像
        // 设备名，实测**不是**（GetFullPathName 不把它们映射进设备命名空间，建目录照建）；
        // 上标那一族里只有 ¹²³ 三个算设备名，`COM´`（U+00B4，重音符）不算。
        // 判据要是写成"以 COM 开头就拒"，用户给实例起名 COM0 会收到一句"这个名字不能用"
        // 而完全不知道为什么 —— 过严和过松一样是错的，两条边界都得有人钉。
        check(InstanceFiles::validateDirName(QStringLiteral("COM0")).isEmpty(),
              "P1 合法：COM0（设备名只到 COM9，0 不在里面）");
        check(InstanceFiles::validateDirName(QStringLiteral("LPT0")).isEmpty(),
              "P1 合法：LPT0 同上");
        check(InstanceFiles::validateDirName(QStringLiteral("COM´")).isEmpty(),
              "P1 合法：COM´（U+00B4 是重音符，不是上标一）");
    }
    {
        // 首尾空白先 trim 再判 —— 用户多敲一个空格不是错误，规整掉正是他想要的。
        // 所以"名字 "是**合法**的（会被改名叫"名字"），这一点和"名字."（非法）
        // 的不对称是刻意的：trim 管得着空格，管不着点。
        check(InstanceFiles::validateDirName(QStringLiteral(" 我的整合包 ")).isEmpty(),
              "P1 首尾空白先 trim 再判");
        check(InstanceFiles::validateDirName(QStringLiteral("我的整合包 ")).isEmpty(),
              "P1 尾随空格不拒绝（trim 会处理掉它，不该逼用户重打一遍）");
    }

    // ---------- P2 重名比较 ----------
    {
        const QString a = QStringLiteral("P2-版本A");
        const QString b = QStringLiteral("P2-版本B");
        writeFile(versionsRoot + "/" + a + "/" + a + ".jar", "dummy");

        check(!InstanceFiles::validateRenameTarget(a, b).isEmpty(),
              "P2 撞上已有实例的名字会被挡下");
        check(!InstanceFiles::validateRenameTarget(QStringLiteral("p2-版本a"), b).isEmpty(),
              "P2 重名比较不区分大小写（Windows 的文件系统本来就不区分）");
        check(!InstanceFiles::validateRenameTarget(a, a).isEmpty(),
              "P2 名字一个字没改会被挡下");
        // 只差大小写：合法。它改的是磁盘上那个名字的显示大小写，目标"已存在"的
        // 那个其实就是它自己。
        check(InstanceFiles::validateRenameTarget(QStringLiteral("p2-版本a"), a).isEmpty(),
              "P2 只改大小写是合法的（那不是重名，是同一个目录）",
              InstanceFiles::validateRenameTarget(QStringLiteral("p2-版本a"), a));
        check(!InstanceFiles::validateRenameTarget(QStringLiteral("a/b"), a).isEmpty(),
              "P2 形状问题照样先报出来");

        QDir(versionsRoot + "/" + a).removeRecursively();
    }

    // ---------- P3 真改磁盘 ----------
    const auto makeInstance = [&](const QString &id, const QString &jarName) {
        writeFile(versionsRoot + "/" + id + "/" + jarName, "dummy");
        writeFile(versionsRoot + "/" + id + "/" + id + ".json", "{}");
    };

    VersionManager vm;
    {
        const QString oldId = QStringLiteral("P3-老名");
        const QString newId = QStringLiteral("P3-新名");
        makeInstance(oldId, oldId + ".jar");
        // 用户手写的字段：改名是**合并**写，它必须活下来。
        writeFile(AppPaths::versionMetaPath(oldId),
                  R"({"jvmArgs":["-Dkeep=me"],"realVersion":"1.20.1"})");

        QString error;
        check(vm.renameInstance(oldId, newId, &error), "P3 改名成功", error);
        check(!QDir(AppPaths::versionDir(oldId)).exists(), "P3 老目录不在了");
        check(QDir(AppPaths::versionDir(newId)).exists(), "P3 新目录在");
        check(QFile::exists(versionsRoot + "/" + newId + "/" + newId + ".jar"),
              "P3 jar 跟着改了名");
        check(QFile::exists(versionsRoot + "/" + newId + "/" + newId + ".json"),
              "P3 json 也跟着改了名");
        check(!QFile::exists(versionsRoot + "/" + newId + "/" + oldId + ".jar"),
              "P3 老名字的 jar 没留下");

        check(vm.displayParts(newId).customName == newId,
              "P3 显示名写成了新名字", vm.displayParts(newId).customName);
        check(vm.displayParts(newId).realVersion == QStringLiteral("1.20.1"),
              "P3 用户手写的 realVersion 活着（改名是合并写）",
              vm.displayParts(newId).realVersion);
        check(vm.versionJvmArgs(newId).contains(QStringLiteral("-Dkeep=me")),
              "P3 用户手写的 jvmArgs 也活着",
              vm.versionJvmArgs(newId).join(QLatin1Char(' ')));
        check(!vm.instanceId(newId).isEmpty(), "P3 顺手定下了稳定 id（分组归属要用它）",
              vm.instanceId(newId));

        check(InstanceFiles::resolveInstance(newId).recognized(), "P3 改完名之后判据认得出来");
        const QStringList installed = vm.installedVersions();
        check(installed.contains(newId) && !installed.contains(oldId),
              "P3 列表里新名字在、老名字不在了");

        QDir(versionsRoot + "/" + newId).removeRecursively();
    }

    // ---------- P4 jar 名和目录名本来就不一致的实例 ----------
    // 判据认得出这种目录，改名时顺带把它**规范**回 <目录名>.jar。
    {
        const QString oldId = QStringLiteral("P4-改过名的");
        const QString newId = QStringLiteral("P4-规范了");
        makeInstance(oldId, QStringLiteral("老jar名.jar"));
        QString error;
        check(vm.renameInstance(oldId, newId, &error), "P4 能改（判据认得出它）", error);
        check(QFile::exists(versionsRoot + "/" + newId + "/" + newId + ".jar"),
              "P4 改完名 jar 就叫 <新目录名>.jar 了（顺手规范回来）");
        check(!QFile::exists(versionsRoot + "/" + newId + "/老jar名.jar"), "P4 老 jar 名没留下");
        QDir(versionsRoot + "/" + newId).removeRecursively();
    }

    // ---------- P5 没有 json 的实例 ----------
    // 半个版本（只有 jar）：改名不能因为它没有 json 就失败，也不许凭空造一个 json 出来。
    {
        const QString oldId = QStringLiteral("P5-没有json");
        const QString newId = QStringLiteral("P5-改完了");
        writeFile(versionsRoot + "/" + oldId + "/" + oldId + ".jar", "dummy");
        QString error;
        check(vm.renameInstance(oldId, newId, &error), "P5 没有 json 也能改名", error);
        check(QFile::exists(versionsRoot + "/" + newId + "/" + newId + ".jar"), "P5 jar 改好了");
        check(!QFile::exists(versionsRoot + "/" + newId + "/" + newId + ".json"),
              "P5 没有凭空造出一个 json（原来没有就该还是没有）");
        QDir(versionsRoot + "/" + newId).removeRecursively();
    }

    // ---------- P6 只改大小写 ----------
    // 走的是 MoveFileEx，改的只是磁盘上那个名字的显示大小写。真机上"用户想把
    // 小写开头改成大写开头"是很正常的要求，而这一条最容易在实现里被"目标已存在"
    // 那道检查误伤。
    {
        const QString oldId = QStringLiteral("P6-case");
        const QString newId = QStringLiteral("P6-Case");
        makeInstance(oldId, oldId + ".jar");
        QString error;
        const bool ok = vm.renameInstance(oldId, newId, &error);
        check(ok, "P6 只改大小写能成功", error);
        if (ok) {
            const QStringList entries =
                QDir(versionsRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            check(entries.contains(newId) && !entries.contains(oldId),
                  "P6 磁盘上的目录名真的换成了新的大小写", entries.join(QLatin1Char('|')));
            check(QFile::exists(versionsRoot + "/" + newId + "/" + newId + ".jar"),
                  "P6 jar 的大小写也跟着改了");
            QDir(versionsRoot + "/" + newId).removeRecursively();
        } else {
            QDir(versionsRoot + "/" + oldId).removeRecursively();
        }
    }

    // ---------- P7 失败路径：磁盘不能被动过 ----------
    {
        const QString kept = QStringLiteral("P7-别动我");
        const QString other = QStringLiteral("P7-已经在了");
        makeInstance(kept, kept + ".jar");
        makeInstance(other, other + ".jar");
        writeFile(versionsRoot + "/" + other + "/内容是别的.txt", "xx");

        QString error;
        check(!vm.renameInstance(kept, other, &error), "P7 撞上已有实例时拒绝改名");
        check(!error.isEmpty(), "P7 给了原因", error);

        // 一个字节都不能动：老目录还在、老 jar 还在、目标目录里的东西也没被盖掉。
        check(QFile::exists(versionsRoot + "/" + kept + "/" + kept + ".jar"),
              "P7 失败之后老 jar 还在");
        check(QFile::exists(versionsRoot + "/" + other + "/" + other + ".jar"),
              "P7 目标目录里原来的 jar 还在（没被静默盖掉）");
        check(QFile::exists(versionsRoot + "/" + other + "/内容是别的.txt"),
              "P7 目标目录里别的东西也没被动过");
        check(!QFile::exists(AppPaths::versionMetaPath(kept)),
              "P7 前置检查阶段就退出了：连 meta 都没建出来");

        // 目录不存在的实例
        check(!vm.renameInstance(QStringLiteral("P7-压根没有"), QStringLiteral("P7-x"), &error),
              "P7 目录不存在时拒绝改名");
        check(!error.isEmpty(), "P7 也给了原因", error);

        // 名字不合法
        check(!vm.renameInstance(kept, QStringLiteral("a/b"), &error), "P7 名字非法时拒绝改名");
        check(QFile::exists(versionsRoot + "/" + kept + "/" + kept + ".jar"),
              "P7 非法名字没碰到磁盘");

        QDir(versionsRoot + "/" + kept).removeRecursively();
        QDir(versionsRoot + "/" + other).removeRecursively();
    }

    // ---------- P8 坏 meta 时拒绝改名 ----------
    // 和写侧同一条规矩：往一份读不懂的内容上合并 = 丢用户手写的东西。改名走的是
    // 同一条写路径，所以这里必须一样地拒绝，而且**目录名一个字节都不能动**。
    {
        const QString oldId = QStringLiteral("P8-坏meta");
        makeInstance(oldId, oldId + ".jar");
        writeFile(AppPaths::versionMetaPath(oldId), "{ 这不是 JSON");
        QString error;
        check(!vm.renameInstance(oldId, QStringLiteral("P8-新名"), &error),
              "P8 meta 坏掉时拒绝改名");
        check(QDir(AppPaths::versionDir(oldId)).exists(),
              "P8 目录名没动（失败发生在改名字之前）");
        check(!QDir(AppPaths::versionDir(QStringLiteral("P8-新名"))).exists(),
              "P8 也没留下一个新目录");
        QDir(versionsRoot + "/" + oldId).removeRecursively();
    }

    // ---------- P9 沙盒收拾干净 ----------
    // 各分支自己都删过，但"到底删掉没有"要有一条统一的断言兜着：N / O 两段就因为
    // 攥着文件句柄，removeRecursively() 一直静默失败，每跑一次在沙盒里留一个目录，
    // 而所有断言照样全绿。那种漏法只能靠这种"回头看一眼"的断言抓住。
    {
        const QStringList left = QDir(versionsRoot).entryList(
            QStringList() << QStringLiteral("P*"), QDir::Dirs | QDir::NoDotAndDotDot);
        check(left.isEmpty(), "P9 P 段造出来的目录一个都没剩下", left.join(QLatin1Char('|')));
    }

    return 0;
}

