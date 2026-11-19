#include "Support.hpp"

void runGenericFilesTests() {
    section("PD. 通用文件组：config / defaultconfigs / 资源包 / 光影");

    section("PD0. 四个落点与路径归属（纯值）");
    {
        check(Presets::fileGroupDirName(Presets::FileGroup::Config) == QStringLiteral("config") &&
                  Presets::fileGroupDirName(Presets::FileGroup::DefaultConfigs) ==
                      QStringLiteral("defaultconfigs") &&
                  Presets::fileGroupDirName(Presets::FileGroup::ShaderPacks) ==
                      QStringLiteral("shaderpacks"),
              "PD0 那三个落点是 `config` / `defaultconfigs` / `shaderpacks`");
        check(Presets::fileGroupDirName(Presets::FileGroup::ResourcePacks) ==
                  ResourceCatalog::folderForKind(ResourceCatalog::Kind::ResourcePack) &&
                  Presets::fileGroupDirName(Presets::FileGroup::ShaderPacks) ==
                      ResourceCatalog::folderForKind(ResourceCatalog::Kind::Shader),
              "★★ PD0 资源包和光影那两个目录名**问的是 ResourceCatalog 那张表**（和下载页"
              "落到同一个地方，不是各写一遍）",
              Presets::fileGroupDirName(Presets::FileGroup::ResourcePacks));

        const QList<Presets::FileGroup> groups = Presets::allFileGroups();
        check(groups.size() == 4 && groups.at(0) == Presets::FileGroup::Config &&
                  groups.at(1) == Presets::FileGroup::DefaultConfigs &&
                  groups.at(2) == Presets::FileGroup::ResourcePacks &&
                  groups.at(3) == Presets::FileGroup::ShaderPacks,
              "PD0 四组的次序就是界面上那个次序");

        Presets::FileGroup got = Presets::FileGroup::Config;
        check(Presets::fileGroupFromDirName(QStringLiteral("shaderpacks"), &got) &&
                  got == Presets::FileGroup::ShaderPacks &&
                  Presets::fileGroupDirName(got) == QStringLiteral("shaderpacks") &&
                  !Presets::fileGroupFromDirName(QStringLiteral("mods"), &got) &&
                  !Presets::fileGroupFromDirName(QString(), &got),
              "PD0 目录名认回来是同一组（`mods` / 空串认不出来）");
        check(Presets::fileGroupOfPath(QStringLiteral("config/a.json"), &got) &&
                  got == Presets::FileGroup::Config &&
                  Presets::fileGroupOfPath(QStringLiteral("shaderpacks/BSL.zip"), &got) &&
                  got == Presets::FileGroup::ShaderPacks &&
                  !Presets::fileGroupOfPath(QStringLiteral("mods/x.jar"), &got) &&
                  !Presets::fileGroupOfPath(QStringLiteral("a.json"), &got) &&
                  !Presets::fileGroupOfPath(QStringLiteral("config"), &got) &&
                  !Presets::fileGroupOfPath(QStringLiteral("/config/a.json"), &got),
              "★★ PD0 一条路径属于哪一组 = **它的第一段**（`mods/x.jar`、没有第一层的 `a.json`、"
              "光一个 `config` 都不属于）");
    }

    section("PD1. 第 1 道闸：名字本身（纯值）");
    {
        QString why;
        check(Presets::genericPathIsSafe(QStringLiteral("config/sodium-options.json"), &why) &&
                  Presets::genericPathIsSafe(QStringLiteral("shaderpacks/BSL/内部.bin"), &why) &&
                  Presets::genericPathIsSafe(QStringLiteral("config/子目录/文件.toml"), &why),
              "PD1 正常的那几条过", why);
        check(!Presets::genericPathIsSafe(QString(), &why) &&
                  !Presets::genericPathIsSafe(QStringLiteral("C:/x/a.json"), &why) &&
                  !Presets::genericPathIsSafe(QStringLiteral("C:x/a.json"), &why) &&
                  !Presets::genericPathIsSafe(QStringLiteral("/etc/passwd"), &why) &&
                  !Presets::genericPathIsSafe(QStringLiteral("\\\\服务器\\共享\\a"), &why),
              "★★ PD1 绝对的路径（含 `C:x` 那种「看着是相对的」）一律拒", why);
        check(!Presets::genericPathIsSafe(QStringLiteral("config/../../windows/a.json"), &why) &&
                  !Presets::genericPathIsSafe(QStringLiteral("../config/a.json"), &why) &&
                  !Presets::genericPathIsSafe(QStringLiteral("config/./a.json"), &why) &&
                  !Presets::genericPathIsSafe(QStringLiteral("config//a.json"), &why) &&
                  !Presets::genericPathIsSafe(QStringLiteral("config/a.json/.."), &why),
              "★★ PD1 `..` / `.` / 空段一律拒（**路径穿越**那一条就是它们）", why);
        check(!Presets::genericPathIsSafe(QStringLiteral("config/a:b.json"), &why) &&
                  !Presets::genericPathIsSafe(QStringLiteral("config/CON"), &why) &&
                  !Presets::genericPathIsSafe(QStringLiteral("config/名字 "), &why) &&
                  !Presets::genericPathIsSafe(QStringLiteral("config/名字."), &why),
              "★★ PD1 每一段还要过实例名那套规矩（Windows 的禁区、保留设备名 CON、结尾的空格"
              "和点）—— 那一条**只有一处实现**（`validatePathSegment()`）", why);
    }

    section("PD2. 扫这一组");
    QTemporaryDir scratch;
    check(scratch.isValid(), "PD2 建得出一个临时目录来摆夹具");
    if (!scratch.isValid()) {
        return;
    }
    const QString sourceRoot = scratch.filePath(QStringLiteral("源实例"));
    const QString targetRoot = scratch.filePath(QStringLiteral("目标实例"));
    QDir().mkpath(sourceRoot + QStringLiteral("/config/子目录"));
    QDir().mkpath(sourceRoot + QStringLiteral("/shaderpacks"));
    check(writeBytesAt(sourceRoot + QStringLiteral("/config/b.txt"), QByteArrayLiteral("B")) &&
              writeBytesAt(sourceRoot + QStringLiteral("/config/a.json"),
                           QByteArrayLiteral("{\"a\":1}")) &&
              writeBytesAt(sourceRoot + QStringLiteral("/config/子目录/深.txt"),
                           QByteArrayLiteral("深")) &&
              writeBytesAt(sourceRoot + QStringLiteral("/config/无扩展名"), QByteArrayLiteral("x")),
          "PD2 夹具落盘了");

    QList<Presets::GenericItem> items;
    QStringList refused;
    QString problem;
    check(Presets::scanFileGroup(sourceRoot, Presets::FileGroup::Config, &items, &refused,
                                 &problem) &&
              refused.isEmpty(),
          "PD2 扫 `config/` 扫出来了", problem);
    check(items.size() == 4 && items.at(0).relativePath == QStringLiteral("config/a.json") &&
              items.at(1).relativePath == QStringLiteral("config/b.txt") &&
              items.at(2).relativePath == QStringLiteral("config/子目录") &&
              items.at(2).directory && items.at(3).relativePath == QStringLiteral("config/无扩展名"),
          "★★ PD2 一项 = 这一组目录**底下第一层**的某一个东西；次序是名字序（稳的），"
          "目录算一项",
          QStringLiteral("%1 项：%2")
              .arg(items.size())
              .arg(items.isEmpty() ? QString() : items.at(0).relativePath));
    check(items.at(2).sizeBytes == QFileInfo(sourceRoot + QStringLiteral("/config/子目录/深.txt"))
                                      .size(),
          "PD2 目录那一项的大小 = 整棵的和（只用于显示）",
          QString::number(items.at(2).sizeBytes));
    check(items.at(0).copyOnly && Presets::genericItemIsCopyOnly(Presets::FileGroup::Config,
                                                                 items.at(0).relativePath),
          "★★ PD2 每一项都带着「只能复制/覆盖」那个记号（这一组里"
          "全都不是 `false`）");

    {
        // 这一组不在 ⇒ 空清单（"刚建的实例没有 shaderpacks/" 是最正常的事）。
        QList<Presets::GenericItem> none;
        check(Presets::scanFileGroup(targetRoot, Presets::FileGroup::ShaderPacks, &none, nullptr,
                                     &problem) &&
                  none.isEmpty(),
              "PD2 这一组的目录不在 ⇒ 成功 + 空清单（不是失败）");
        // 这一组的位置上摆着一个**文件** ⇒ 拒。
        check(QDir().mkpath(targetRoot) &&
                  writeBytesAt(targetRoot + QStringLiteral("/defaultconfigs"),
                               QByteArrayLiteral("x")),
              "PD2 在 defaultconfigs 那个位置上摆了个文件");
        check(!Presets::scanFileGroup(targetRoot, Presets::FileGroup::DefaultConfigs, &none,
                                      nullptr, &problem) &&
                  !problem.isEmpty(),
              "PD2 那个位置上不是目录 ⇒ 拒（并说清是哪一个）", problem);
    }

    section("PD3. 计划：五档各是什么意思");
    {
        Presets::GenericPlan plan = Presets::planFileGroup(sourceRoot, items, targetRoot,
                                                          Presets::ApplyMode::Replace);
        check(plan.ok && plan.replaced == 4 && plan.copied == 0 && plan.skipped == 0 &&
                  plan.needsChoice == 0 && plan.writesAnything(),
              "PD3「覆盖」：四项全是覆盖、要动盘",
              QStringLiteral("覆盖 %1").arg(plan.replaced));

        plan = Presets::planFileGroup(sourceRoot, items, targetRoot,
                                      Presets::ApplyMode::CopyIfMissing);
        check(plan.ok && plan.copied == 4 && plan.skipped == 0,
              "PD3「缺了才拷」：目标上一个都没有 ⇒ 四项都要拷",
              QStringLiteral("拷 %1 / 跳 %2").arg(plan.copied).arg(plan.skipped));

        plan = Presets::planFileGroup(sourceRoot, items, targetRoot, Presets::ApplyMode::Skip);
        check(plan.ok && plan.skipped == 4 && !plan.writesAnything(),
              "PD3「跳过」：四项全跳过、**一个字都不写**");

        plan = Presets::planFileGroup(sourceRoot, items, targetRoot, Presets::ApplyMode::Merge);
        check(plan.ok && plan.needsChoice == 4 && !plan.writesAnything(),
              "★★ PD3「合并」：一坨字节谈不上合并 ⇒ **四项全是要你选、一个字节都不写**"
              "（悄悄按复制做的话，用户以为合并过的那份配置里其实有几格是旧的）");
        check(plan.items.at(0).reason.contains(QStringLiteral("复制或覆盖")),
              "★★ PD3 而且理由说的是「只有复制和覆盖两种做法」",
              plan.items.at(0).reason);

        plan = Presets::planFileGroup(sourceRoot, items, targetRoot, Presets::ApplyMode::Append);
        check(plan.ok && plan.needsChoice == 4 && !plan.writesAnything(),
              "★★ PD3「追加」也一样（对文件没有「追加一条」这回事）");

        // 路径不安全的那一项 ⇒ **整组不认**（不是"跳过那一项"）。
        QList<Presets::GenericItem> bad = items;
        bad.append(Presets::GenericItem{QStringLiteral("config/../外面.txt"), false, true, 0});
        plan = Presets::planFileGroup(sourceRoot, bad, targetRoot, Presets::ApplyMode::Replace);
        check(!plan.ok && !plan.problem.isEmpty() && !plan.writesAnything() && plan.total() == 0,
              "★★ PD3 清单里混进一条 `config/../外面.txt` ⇒ 整组不认、一个字节都不写",
              plan.problem);
        // 那一条坏项在**尾巴上**，所以前三项已经攒进 `replaced` 了 —— 不清干净的话
        // `writesAnything()` 会回 true，读者会以为"这一趟要写 4 项"。
        check(plan.replaced == 0 && plan.copied == 0 && plan.skipped == 0 &&
                  plan.needsChoice == 0,
              "★★ PD3 而且**四个计数一个都不留**（「整组不认」的计划里没有半截数字）",
              QStringLiteral("覆盖 %1 / 拷 %2 / 跳 %3 / 要你选 %4")
                  .arg(plan.replaced)
                  .arg(plan.copied)
                  .arg(plan.skipped)
                  .arg(plan.needsChoice));
    }

    section("PD4. 缺了才拷：逐文件判，已经有的一个都不动");
    {
        QDir().mkpath(targetRoot + QStringLiteral("/config"));
        check(writeBytesAt(targetRoot + QStringLiteral("/config/b.txt"),
                           QByteArrayLiteral("用户改过的 B")) &&
                  writeBytesAt(targetRoot + QStringLiteral("/config/用户自己的.txt"),
                               QByteArrayLiteral("别动我")),
              "PD4 目标上先摆两份（其中一份是用户自己加的）");

        const Presets::GenericReport report = Presets::applyFileGroup(
            sourceRoot, targetRoot, Presets::FileGroup::Config, Presets::ApplyMode::CopyIfMissing);
        check(report.ok && report.copied == 3 && report.skipped == 1 && report.filesWritten == 3,
              "★★ PD4 四项里三项拷了、`config/b.txt` 那一项跳过（目标上有它了）",
              QStringLiteral("拷 %1 / 跳 %2 / 写了 %3 份：%4")
                  .arg(report.copied)
                  .arg(report.skipped)
                  .arg(report.filesWritten)
                  .arg(report.problem));
        check(readBytesAt(targetRoot + QStringLiteral("/config/b.txt")) ==
                  QByteArrayLiteral("用户改过的 B") &&
                  readBytesAt(targetRoot + QStringLiteral("/config/a.json")) ==
                      QByteArrayLiteral("{\"a\":1}") &&
                  readBytesAt(targetRoot + QStringLiteral("/config/子目录/深.txt")) ==
                      QByteArrayLiteral("深") &&
                  readBytesAt(targetRoot + QStringLiteral("/config/用户自己的.txt")) ==
                      QByteArrayLiteral("别动我"),
              "★★ PD4 用户改过的那份原样、我们自己加的那份也原样、缺的补上了");

        // 再跑一遍：这一次**一份都不缺** ⇒ 四项全是跳过（不是"复制 0 个文件"）。
        const Presets::GenericReport again = Presets::applyFileGroup(
            sourceRoot, targetRoot, Presets::FileGroup::Config, Presets::ApplyMode::CopyIfMissing);
        check(again.ok && again.skipped == 4 && again.filesWritten == 0 && again.copied == 0,
              "★★ PD4 再跑一遍：四项全跳过、一份都没写（干跑那一趟就是为这个数的）",
              QStringLiteral("拷 %1 / 跳 %2").arg(again.copied).arg(again.skipped));
    }

    section("PD5. 覆盖：同名的那份换成我的，**多出来的不删**");
    {
        check(writeBytesAt(targetRoot + QStringLiteral("/config/a.json"), QByteArrayLiteral("旧")),
              "PD5 先把目标上那份改成「旧」");
        const Presets::GenericReport report = Presets::applyFileGroup(
            sourceRoot, targetRoot, Presets::FileGroup::Config, Presets::ApplyMode::Replace);
        check(report.ok && report.replaced == 4 && report.filesWritten == 4,
              "PD5 覆盖那一档：四项真的都写了", report.problem);
        check(readBytesAt(targetRoot + QStringLiteral("/config/a.json")) ==
                  QByteArrayLiteral("{\"a\":1}"),
              "PD5 同名的那一份换成了预设里的");
        check(readBytesAt(targetRoot + QStringLiteral("/config/用户自己的.txt")) ==
                  QByteArrayLiteral("别动我"),
              "★★ PD5 **目标上多出来的那份还在**（整棵子树替换会顺手删掉用户自己的东西，"
              "而那不可恢复 —— 「覆盖」在这儿的含义只有「同名的那份换成我的」）");
    }

    section("PD6. 提取：实例 → 预设的 files/");
    {
        const QString presetFiles = scratch.filePath(QStringLiteral("预设/files"));
        const Presets::GenericReport report = Presets::extractFileGroup(
            targetRoot, presetFiles, Presets::FileGroup::Config);
        // 5 项而不是 4 项：提取扫的是**实例上现在这一份**，而它比预设那 4 项多一份
        // `config/用户自己的.txt`（那一份正是我们刚刚证明过"覆盖不删"的那一个）。
        check(report.ok && report.replaced == 5 && report.filesWritten == 5,
              "★★ PD6 五份都收进去了（四项 + 用户自己加的那一份；`子目录` 那一项是 1 份文件）",
              QStringLiteral("项 %1 / 文件 %2：%3")
                  .arg(report.replaced)
                  .arg(report.filesWritten)
                  .arg(report.problem));
        check(readBytesAt(presetFiles + QStringLiteral("/config/a.json")) ==
                      QByteArrayLiteral("{\"a\":1}") &&
                  readBytesAt(presetFiles + QStringLiteral("/config/子目录/深.txt")) ==
                      QByteArrayLiteral("深") &&
                  readBytesAt(presetFiles + QStringLiteral("/config/用户自己的.txt")) ==
                      QByteArrayLiteral("别动我"),
              "PD6 收下来的就是实例里现在这一份（含用户自己加的那个文件）");

        // 收到一个干净的实例上：那一趟就是"第一次给这个实例配这一组"。
        const QString freshRoot = scratch.filePath(QStringLiteral("空实例"));
        QDir().mkpath(freshRoot);
        const Presets::GenericReport land = Presets::applyFileGroup(
            presetFiles, freshRoot, Presets::FileGroup::Config, Presets::ApplyMode::CopyIfMissing);
        check(land.ok && land.copied == 5 && land.filesWritten == 5 &&
                  readBytesAt(freshRoot + QStringLiteral("/config/子目录/深.txt")) ==
                      QByteArrayLiteral("深"),
              "★★ PD6 端到端：提取出来的一份落到空实例上，一个字不差", land.problem);
    }

    section("PD7. 第 3 道闸：源侧 / 目标侧的 junction");
    {
        // 窗外那一份（**绝对不该被拷进来、也绝对不该被写进去**）。
        const QString outsideDir = scratch.filePath(QStringLiteral("外面"));
        QDir().mkpath(outsideDir);
        check(writeBytesAt(outsideDir + QStringLiteral("/秘密.txt"), QByteArrayLiteral("外面")),
              "PD7 外面那一份落盘了");

        // ① 源侧：`config/链接` 是一根真 junction 指着外面。
        const QString sourceLink = sourceRoot + QStringLiteral("/config/链接");
        const bool madeSourceLink =
            QProcess::execute(QStringLiteral("cmd"),
                              {QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"),
                               QDir::toNativeSeparators(sourceLink),
                               QDir::toNativeSeparators(outsideDir)}) == 0 &&
            QFileInfo(sourceLink).isJunction();
        check(madeSourceLink, "★ PD7 先造出一根真 junction：源侧 `config/链接` → 外面（mklink /J）");
        if (madeSourceLink) {
            QList<Presets::GenericItem> scanned;
            QStringList refused;
            QString why;
            check(Presets::scanFileGroup(sourceRoot, Presets::FileGroup::Config, &scanned, &refused,
                                         &why),
                  "PD7 扫是成功的（拒的是那一项，不是整组）", why);
            check(refused.size() == 1 && refused.at(0).contains(QStringLiteral("链接")),
                  "★★ PD7 那根链接**不进清单**、而是报在 `refused` 里（悄悄略过的话，用户"
                  "看到的清单和盘上对不上）",
                  refused.join(QStringLiteral(" | ")));
            bool listed = false;
            for (const Presets::GenericItem &one : scanned) {
                listed = listed || one.relativePath == QStringLiteral("config/链接");
            }
            check(!listed, "★★ PD7 而且它真的不在清单里");

            const QString target2 = scratch.filePath(QStringLiteral("目标实例2"));
            QDir().mkpath(target2);
            const Presets::GenericReport report = Presets::applyFileGroup(
                sourceRoot, target2, Presets::FileGroup::Config, Presets::ApplyMode::Replace);
            check(report.ok && !QFileInfo::exists(target2 + QStringLiteral("/config/链接")) &&
                      !QFileInfo::exists(target2 + QStringLiteral("/config/链接/秘密.txt")),
                  "★★ PD7 落盘那一趟也没把那根链接拷过去（外面那份一个字都没进实例）",
                  report.problem);

            // ② 目标侧：`config/` 底下那一份**自己**是根链接 ⇒ 不往它里面写。
            const QString target3 = scratch.filePath(QStringLiteral("目标实例3"));
            QDir().mkpath(target3 + QStringLiteral("/config"));
            const QString targetLink = target3 + QStringLiteral("/config/b.txt");
            const bool madeTargetLink =
                QProcess::execute(QStringLiteral("cmd"),
                                  {QStringLiteral("/c"), QStringLiteral("mklink"),
                                   QStringLiteral("/J"), QDir::toNativeSeparators(targetLink),
                                   QDir::toNativeSeparators(outsideDir)}) == 0 &&
                QFileInfo(targetLink).isJunction();
            check(madeTargetLink, "★ PD7 再造一根：目标侧 `config/b.txt` → 外面");
            if (madeTargetLink) {
                const Presets::GenericReport hit = Presets::applyFileGroup(
                    sourceRoot, target3, Presets::FileGroup::Config, Presets::ApplyMode::Replace);
                check(!hit.ok && hit.problem.contains(QStringLiteral("链接")),
                    "★★ PD7 往一根链接里写 ⇒ **拒**（不然「覆盖一份配置」会变成「改掉链接"
                    "那头那个目录里的东西」，而用户看到的只是覆盖了一份配置）",
                    hit.problem);
                check(!QFileInfo::exists(outsideDir + QStringLiteral("/b.txt")) ||
                          readBytesAt(outsideDir + QStringLiteral("/b.txt")) !=
                              QByteArrayLiteral("B"),
                    "★★ PD7 而且外面那个目录里没有多出 `b.txt`（真·没写进去）");
            }

            // ③ 目标侧但**是目录那一层**：`config/子目录` 是根链接。★ 这一条是三道闸里
            // 最容易被漏掉的一种形状 —— 名字那两道判的是字面路径（`config/子目录/深.txt`
            // 看着完全正常），而递归一旦走下去，写的就是链接那头那个目录。
            const QString target4 = scratch.filePath(QStringLiteral("目标实例4"));
            QDir().mkpath(target4 + QStringLiteral("/config"));
            const QString targetDirLink = target4 + QStringLiteral("/config/子目录");
            const bool madeDirLink =
                QProcess::execute(QStringLiteral("cmd"),
                                  {QStringLiteral("/c"), QStringLiteral("mklink"),
                                   QStringLiteral("/J"), QDir::toNativeSeparators(targetDirLink),
                                   QDir::toNativeSeparators(outsideDir)}) == 0 &&
                QFileInfo(targetDirLink).isJunction();
            check(madeDirLink, "★ PD7 第三根：目标侧 `config/子目录`（**目录那一层**）→ 外面");
            if (madeDirLink) {
                const Presets::GenericReport through = Presets::applyFileGroup(
                    sourceRoot, target4, Presets::FileGroup::Config, Presets::ApplyMode::Replace);
                check(!through.ok && through.problem.contains(QStringLiteral("链接")),
                    "★★ PD7 那一项底下有文件 ⇒ 递归**当场停在那根链接上**（不然「覆盖一份"
                    "配置」会写到外面那个目录里去，而名字那两道闸看它一切正常）",
                    through.problem);
                check(!QFileInfo::exists(outsideDir + QStringLiteral("/深.txt")),
                    "★★ PD7 外面那个目录里确实没多出 `深.txt`");
                check(QDir().rmdir(targetDirLink), "PD7 第三根也摘掉了");
            }

            // 清掉那两根链接（**只摘链接本体**，不动它那一头）—— 见 ZJ5 那条 NOTE：
            // `QTemporaryDir::removeRecursively()` 是会跟着 junction 往下删的。
            check(QDir().rmdir(sourceLink) && QDir().rmdir(targetLink),
                  "PD7 两根链接都摘掉了（rmdir 只摘链接本体）");
        }
    }

    section("PD8. 预设段 ⇄ 这一组");
    {
        QList<Presets::GenericItem> items;
        QString problem;
        check(Presets::scanFileGroup(sourceRoot, Presets::FileGroup::Config, &items, nullptr,
                                     &problem),
              "PD8 再扫一遍（上一条那根链接已经摘掉了）", problem);

        QList<Presets::PresetSection> sections;
        check(Presets::sectionsFromFileGroup(Presets::FileGroup::Config, items,
                                             Presets::ApplyMode::CopyIfMissing, &sections,
                                             &problem) &&
                  sections.size() == 1 &&
                  sections.at(0).kind == Presets::SectionKind::GenericFiles &&
                  sections.at(0).mode == Presets::ApplyMode::CopyIfMissing &&
                  sections.at(0).items.size() == items.size(),
              "PD8 一段「通用文件组」：种类、模式、条数都对", problem);

        Presets::FileGroup fromId = Presets::FileGroup::Config;
        check(Presets::fileGroupFromSectionId(sections.at(0).id, &fromId) &&
                  fromId == Presets::FileGroup::Config,
              "★★ PD8 段自己那个 id 说得出来「这一段是哪一组」（界面要按它摆标题）");
        check(Presets::sectionIdFor(Presets::SectionKind::GenericFiles,
                                    Presets::fileGroupDirName(Presets::FileGroup::ShaderPacks)) !=
                  Presets::sectionIdFor(Presets::SectionKind::GenericFiles,
                                        Presets::fileGroupDirName(Presets::FileGroup::Config)) &&
                  Presets::sectionIdFor(Presets::SectionKind::GenericFiles) ==
                      Presets::sectionIdFor(Presets::SectionKind::GenericFiles, QString()),
              "★★ PD8 四组各有各的 id；而且**不加那半截名字时还是老算法**（加了参数没有动"
              "盘上任何一份预设的身份）");

        // 过一遍 JSON 字节再读回。
        const QByteArray sectionBytes =
            QJsonDocument(sections.at(0).items).toJson(QJsonDocument::Compact);
        Presets::PresetSection back;
        back.id = sections.at(0).id;
        back.kind = sections.at(0).kind;
        back.mode = sections.at(0).mode;
        back.items = QJsonDocument::fromJson(sectionBytes).array();
        QList<Presets::GenericItem> round;
        check(Presets::fileGroupFromSection(back, &round, &problem) && round.size() == 4 &&
                  round.at(0).relativePath == QStringLiteral("config/a.json") &&
                  round.at(0).copyOnly,
              "★★ PD8 过一遍 JSON 字节再读回来一字不差（含 `copyOnly` 那一格）", problem);

        // 读不回来的那几条。
        Presets::PresetSection bad = back;
        bad.items = QJsonArray{QStringLiteral("我不是对象")};
        check(!Presets::fileGroupFromSection(bad, &round, &problem) && !problem.isEmpty(),
              "★★ PD8 项不是对象 ⇒ 整段读不回来", problem);

        bad = back;
        bad.items = QJsonArray{QJsonObject{{QStringLiteral("path"), QStringLiteral("config/../x")}}};
        check(!Presets::fileGroupFromSection(bad, &round, &problem) && !problem.isEmpty(),
              "★★ PD8 路径带 `..` ⇒ 整段读不回来（读不回来的段不许拿去改用户的实例）", problem);

        bad = back;
        bad.items = QJsonArray{QJsonObject{{QStringLiteral("path"), QStringLiteral("mods/x.jar")}}};
        check(!Presets::fileGroupFromSection(bad, &round, &problem) && !problem.isEmpty(),
              "★★ PD8 路径不在这一层管的任何一个目录里 ⇒ 整段读不回来", problem);

        bad = back;
        bad.items = QJsonArray{
            QJsonObject{{QStringLiteral("path"), QStringLiteral("config/a.json")}},
            QJsonObject{{QStringLiteral("path"), QStringLiteral("shaderpacks/x.zip")}}};
        check(!Presets::fileGroupFromSection(bad, &round, &problem) && !problem.isEmpty(),
              "★★ PD8 同一段里的项落在不同的组上 ⇒ 整段读不回来（「一段只能是一组」）", problem);

        bad = back;
        bad.items = QJsonArray{QJsonObject{{QStringLiteral("copyOnly"), true}}};
        check(!Presets::fileGroupFromSection(bad, &round, &problem) && !problem.isEmpty(),
              "PD8 缺了 `path` ⇒ 整段读不回来", problem);

        Presets::PresetSection wrongKind;
        wrongKind.kind = Presets::SectionKind::ServerList;
        check(!Presets::fileGroupFromSection(wrongKind, &round, &problem) &&
                  Presets::genericFilesHandlesSection(Presets::SectionKind::GenericFiles) &&
                  !Presets::genericFilesHandlesSection(Presets::SectionKind::ServerList),
              "PD8 不归这一层管的段 ⇒ 拒绝（`genericFilesHandlesSection()` 是分派判据）");

        // 写出去的时候混进一条不属于这一组的 ⇒ 拒（不然盘上那份预设会自相矛盾）。
        QList<Presets::GenericItem> mixed = items;
        mixed.append(Presets::GenericItem{QStringLiteral("shaderpacks/x.zip"), false, true, 0});
        check(!Presets::sectionsFromFileGroup(Presets::FileGroup::Config, mixed,
                                              Presets::ApplyMode::Replace, &sections, &problem) &&
                  !problem.isEmpty(),
              "★★ PD8 写出去那一趟也判归属（`config` 那一段里混进一条 `shaderpacks/…` ⇒ 拒）",
              problem);
    }
}

