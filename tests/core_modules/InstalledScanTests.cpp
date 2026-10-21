#include "Support.hpp"

int runInstalledScanTests(const QString &sandbox) {
    section("J. 已安装版本扫描（认不出的要说）");

    const QString versionsRoot = sandbox + "/versions";
    // 目录名和 jar 名不一致（用户改过目录名）—— 现在要能认出来。
    const QString renamedDir = QStringLiteral("名字对不上");
    const QString renamedJar = QStringLiteral("旧名字.jar");
    // 目录名和 jar 名不一致，而且连 json 也是老名字 —— 真机上被外部改名的实例就长这样。
    const QString renamedJson = QStringLiteral("旧名字.json");
    // 有 jar 没有 json。json 缺席**不该**否决实例（半个版本、下载残骸的判据是 jar）。
    const QString noJsonDir = QStringLiteral("只有jar没json");
    const QString noJsonJar = QStringLiteral("随便起的名字.jar");
    // 顶层两个 jar 又认不出哪个是客户端 —— 这一类继续不认，且必须报出来。
    const QString twoJarsDir = QStringLiteral("两个jar");
    const QString twoJarsOne = QStringLiteral("aaa.jar");
    const QString twoJarsTwo = QStringLiteral("bbb.jar");
    const QString emptyDir = QStringLiteral("纯空目录");
    const QString strayZip = QStringLiteral("散落的整合包.zip");
    const QString goodDir = QStringLiteral("名字对得上");

    const auto writeFile = [](const QString &path) {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write("dummy");
        }
    };

    QDir().mkpath(versionsRoot + "/" + renamedDir);
    writeFile(versionsRoot + "/" + renamedDir + "/" + renamedJar);
    writeFile(versionsRoot + "/" + renamedDir + "/" + renamedJson);

    QDir().mkpath(versionsRoot + "/" + noJsonDir);
    writeFile(versionsRoot + "/" + noJsonDir + "/" + noJsonJar);

    QDir().mkpath(versionsRoot + "/" + twoJarsDir);
    writeFile(versionsRoot + "/" + twoJarsDir + "/" + twoJarsOne);
    writeFile(versionsRoot + "/" + twoJarsDir + "/" + twoJarsTwo);

    QDir().mkpath(versionsRoot + "/" + emptyDir);
    writeFile(versionsRoot + "/" + strayZip);

    QDir().mkpath(versionsRoot + "/" + goodDir);
    writeFile(versionsRoot + "/" + goodDir + "/" + goodDir + ".jar");

    VersionManager vm;

    // warnings 传 nullptr 时必须与加这个参数之前**完全一样**：不崩、不多事。
    const QStringList silent = vm.installedVersions();

    check(silent.contains(renamedDir), "J1 名字对不上的目录**进**列表（判据已放松）",
          renamedDir);
    check(silent.contains(noJsonDir), "J1 有 jar 没有 json 的目录也进列表（json 缺席不否决）",
          noJsonDir);
    check(silent.contains(goodDir), "J1 名字对得上的目录照常进列表", goodDir);
    check(!silent.contains(twoJarsDir), "J1 顶层两个 jar 又认不出客户端的目录不进列表",
          twoJarsDir);

    QStringList warnings;
    const QStringList reported = vm.installedVersions(&warnings);
    check(reported == silent, "J1 传不传 warnings，返回的列表逐项一致");
    check(!reported.contains(emptyDir) && !reported.contains(strayZip),
          "J2 纯空目录和散落的 zip 都不算版本");

    // 上报条数还是 1，但**来源全变了**：以前是 renamedDir（名字对不上），现在是 twoJarsDir
    // （两个 jar 认不出）。数字不变纯属巧合，别以为这条没动过。
    check(warnings.size() == 1, "J1 恰好上报一条（认得出的、空目录、zip 都不该报）",
          QString::number(warnings.size()));
    if (warnings.size() == 1) {
        const QString w = warnings.first();
        check(w.contains(twoJarsDir) && w.contains(twoJarsOne) && w.contains(twoJarsTwo),
              "J1 告警里点了目录名和两个 jar 的名字", w);
    }
    const QString joined = warnings.join(QStringLiteral("\n"));
    check(!joined.contains(emptyDir), "J2 纯空目录不产生告警");
    check(!joined.contains(strayZip), "J2 散落在 versions/ 下的 zip 不产生告警");
    check(!joined.contains(goodDir), "J1 名字对得上的目录不产生告警");
    // 判据放松之后这两类都认得出，所以都不该出现在告警里 —— 这是放松的收益，别又报回来。
    check(!joined.contains(renamedDir), "J1 名字对不上的目录不再产生告警（它已经认得出了）");
    check(!joined.contains(noJsonDir), "J1 缺 json 的目录不产生告警");

    // 清理：留着会让沙盒一直长下去，尤其 twoJarsDir 会污染下一次运行
    // （那时它会变成"预先存在的"一条告警）。
    for (const QString &dir : {renamedDir, noJsonDir, twoJarsDir, emptyDir, goodDir}) {
        QDir(versionsRoot + "/" + dir).removeRecursively();
    }
    QFile::remove(versionsRoot + "/" + strayZip);

    QStringList afterCleanup;
    vm.installedVersions(&afterCleanup);
    check(afterCleanup.isEmpty(), "J3 清理后不再有任何告警（下一次运行不会被污染）",
          QString::number(afterCleanup.size()));

    return 0;
}

