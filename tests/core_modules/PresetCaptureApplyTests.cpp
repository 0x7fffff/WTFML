#include "Support.hpp"
#include "presets/ModSourceResolution.hpp"
#include "presets/PresetExecutionReport.hpp"
#include "presets/PresetWriteLock.hpp"

void runPresetCaptureApplyTests() {
    QTemporaryDir scratch;
    check(scratch.isValid(), "PG 建得出一个临时目录来摆夹具");
    if (!scratch.isValid()) {
        return;
    }

    const QString instanceA = scratch.filePath(QStringLiteral("instanceA"));
    const QString instanceB = scratch.filePath(QStringLiteral("instanceB"));
    const QString presetsRoot = scratch.filePath(QStringLiteral("presets"));
    QDir().mkpath(instanceA + QStringLiteral("/config"));
    QDir().mkpath(instanceB);

    const auto writeBytes = [](const QString &path, const QByteArray &bytes) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return false;
        }
        return file.write(bytes) == bytes.size();
    };
    const auto readBytes = [](const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return QByteArray();
        }
        return file.readAll();
    };

    const QByteArray optionsA =
        QByteArrayLiteral("fov:0.5\nkey_attack:key.keyboard.a\n");
    // 目标上比预设**多一个** `gamma`：合并那一趟它必须留着，覆盖那一趟才该被拿掉。
    const QByteArray optionsB = QByteArrayLiteral("fov:0.0\ngamma:1.0\n");
    const QByteArray configA = QByteArrayLiteral("{\"quality\":\"high\"}");
    check(writeBytes(instanceA + QStringLiteral("/options.txt"), optionsA),
          "PG 夹具：源实例的 options.txt 写下去了");
    check(writeBytes(instanceA + QStringLiteral("/config/sodium-options.json"), configA),
          "PG 夹具：源实例的 config/ 写下去了");
    check(writeBytes(instanceB + QStringLiteral("/options.txt"), optionsB),
          "PG 夹具：目标实例的 options.txt 写下去了");

    section("PG1. 从实例收一份：三段、文件真的拷进预设的 files/");
    const QString presetId = Presets::newPresetId();
    Presets::CaptureSelection selection;
    selection.servers = false;
    selection.malilib = false;

    Presets::CaptureRequest capture;
    capture.instanceId = QStringLiteral("instanceA");
    capture.instanceRoot = instanceA;
    capture.presetFilesRoot = Presets::filesDir(presetsRoot, presetId);
    capture.selection = selection;
    const Presets::CaptureReport report = Presets::captureFromInstance(capture);

    check(report.ok, "PG1 收得回来", report.problem);
    check(report.sectionCount() == 3, "★★ PG1 收出三段：游戏设置 / 原样按键 / config/",
          QString::number(report.sectionCount()));
    check(report.optionKeys >= 1 && report.keybindings >= 1 && report.configItems == 1,
          "★★ PG1 三个计数各自对得上（设置 1 项、按键 1 项、config 1 个）",
          QStringLiteral("%1 / %2 / %3")
              .arg(report.optionKeys)
              .arg(report.keybindings)
              .arg(report.configItems));
    check(QFileInfo::exists(Presets::filesDir(presetsRoot, presetId) +
                            QStringLiteral("/config/sodium-options.json")),
          "★★ PG1 config/ 里那一份真的拷进了预设的 files/（这是提取唯一会写的一处盘）");

    Presets::CaptureSelection nothing;
    nothing.vanillaOptions = nothing.vanillaKeys = nothing.servers = false;
    nothing.config = nothing.defaultConfigs = nothing.resourcePacks = nothing.shaderPacks = false;
    nothing.malilib = false;
    Presets::CaptureRequest emptyCapture = capture;
    emptyCapture.selection = nothing;
    const Presets::CaptureReport emptyReport = Presets::captureFromInstance(emptyCapture);
    check(emptyReport.ok && emptyReport.sectionCount() == 0 && !emptyReport.notes.isEmpty(),
          "★★ PG1 一样都没勾 ⇒ 成功但一段都没有（不是失败，也不该存成一份空预设）",
          emptyReport.notes.join(QStringLiteral("；")));

    section("PG2. 合并落下去：目标上没点名的键还在");
    Presets::PresetManifest manifest;
    manifest.id = presetId;
    manifest.name = QStringLiteral("PG 夹具");
    manifest.sections = report.sections;
    check(Presets::saveManifest(presetsRoot, manifest), "PG2 这一份写下去了");
    bool manifestOk = false;
    const Presets::PresetManifest saved = Presets::loadManifest(presetsRoot, presetId, &manifestOk);
    check(manifestOk && saved.sections.size() == 3, "PG2 写下去读回来还是三段");
    if (!manifestOk) {
        return;
    }

    Presets::PresetApplyRequest applyRequest;
    applyRequest.instanceId = QStringLiteral("instanceB");
    applyRequest.instanceRoot = instanceB;
    {
        const bool enabledBefore = DirectorySessionLock::enabled;
        DirectorySessionLock::enabled = true;
        const QString key = DirectorySessionLock::keyFor(instanceB);
        QLockFile other(QDir::temp().filePath(QStringLiteral("wtfml-directory-%1.lock").arg(key)));
        other.setStaleLockTime(0);
        check(other.tryLock(0), "PG2 预设目录占用夹具取得锁");
        const auto blocked = Presets::applyPresetToInstance(presetsRoot, saved, applyRequest);
        DirectorySessionLock::enabled = enabledBefore;
        check(!blocked.ok && !blocked.wroteAnything() && blocked.backupDir.isEmpty()
                  && blocked.problem.contains(QStringLiteral("占用"))
                  && readBytes(instanceB + QStringLiteral("/options.txt")) == optionsB,
              "PG2 目录被其他启动器占用时应用预设不备份不写文件", blocked.problem);
    }
    const Presets::PresetApplyResult applied =
        Presets::applyPresetToInstance(presetsRoot, saved, applyRequest);

    check(applied.ok && !applied.needsConfirm, "★★ PG2 合并那一趟办完了", applied.problem);
    check(applied.wroteAnything(), "★★ PG2 说是写进去了东西");
    const QByteArray afterMerge = readBytes(instanceB + QStringLiteral("/options.txt"));
    check(afterMerge.contains("fov:0.5") && afterMerge.contains("gamma:1.0") &&
              afterMerge.contains("key_attack"),
          "★★★ PG2 目标上：`fov` 换成了预设的值、`gamma`（预设没点名）**还在**、按键也落下了",
          QString::fromUtf8(afterMerge));
    check(readBytes(instanceB + QStringLiteral("/config/sodium-options.json")) == configA,
          "★★ PG2 config/ 那一份「缺了才拷」也落到了目标上");

    section("PG3. 备份：改写过的那一份在备份目录里原样躺着一份");
    check(!applied.backupDir.isEmpty(), "★★ PG3 这一趟有备份目录");
    check(readBytes(applied.backupDir + QStringLiteral("/options.txt")) == optionsB,
          "★★★ PG3 备份里那份 `options.txt` 逐字节等于**动手之前**的那一份",
          QString::fromUtf8(readBytes(applied.backupDir + QStringLiteral("/options.txt"))));

    section("PG4. 什么都不用做的那一趟：不写、也不建空备份目录");
    const Presets::PresetApplyResult second =
        Presets::applyPresetToInstance(presetsRoot, saved, applyRequest);
    check(second.ok && !second.wroteAnything() && second.backupDir.isEmpty(),
          "★★ PG4 第二趟：目标已经是那个样子 ⇒ 没写、也没留下一个空备份目录",
          QStringLiteral("written=%1 removed=%2").arg(second.written).arg(second.removed));

    section("PG5. 「覆盖」两趟：不点头一个字节都不动，点头之后才真写");
    Presets::PresetManifest replaceManifest = saved;
    for (Presets::PresetSection &section : replaceManifest.sections) {
        if (section.kind == Presets::SectionKind::VanillaOptions) {
            section.mode = Presets::ApplyMode::Replace;
        }
    }
    writeBytes(instanceB + QStringLiteral("/options.txt"), optionsB);
    Presets::PresetApplyResult firstPass =
        Presets::applyPresetToInstance(presetsRoot, replaceManifest, applyRequest);
    check(firstPass.needsConfirm && !firstPass.ok,
          "★★★ PG5 第一趟回 `needsConfirm`（「覆盖」必须由用户明确确认）");
    check(readBytes(instanceB + QStringLiteral("/options.txt")) == optionsB,
          "★★★ PG5 第一趟之后目标那份 `options.txt` 一个字节都没动");

    Presets::PresetApplyRequest confirmed = applyRequest;
    confirmed.replaceConfirmed = true;
    const Presets::PresetApplyResult secondPass =
        Presets::applyPresetToInstance(presetsRoot, replaceManifest, confirmed);
    const QByteArray afterReplace = readBytes(instanceB + QStringLiteral("/options.txt"));
    check(secondPass.ok && !secondPass.needsConfirm, "★★ PG5 点头之后才真写", secondPass.problem);
    check(afterReplace.contains("fov:0.5") && !afterReplace.contains("gamma"),
          "★★ PG5 覆盖：预设点名的键换掉，目标上多出来的 `gamma` 被拿掉",
          QString::fromUtf8(afterReplace));
    check(secondPass.removed >= 1, "★★ PG5 报告里那个「拿掉了几项」的数对得上",
          QString::number(secondPass.removed));

    section("PG6. 游戏在跑：整趟不写");
    writeBytes(instanceB + QStringLiteral("/options.txt"), optionsB);
    Presets::PresetApplyRequest running = applyRequest;
    running.gameRunning = true;
    const Presets::PresetApplyResult blocked =
        Presets::applyPresetToInstance(presetsRoot, saved, running);
    check(!blocked.ok && !blocked.blockedReason.isEmpty(),
          "★★★ PG6 游戏在跑 ⇒ 整趟不许开始", blocked.blockedReason);
    check(readBytes(instanceB + QStringLiteral("/options.txt")) == optionsB,
          "★★★ PG6 而且目标那份 `options.txt` 一个字节都没动");

    section("PG7. 读目标快照：没有 / 读不动是两件事");
    const QString instanceC = scratch.filePath(QStringLiteral("instanceC"));
    QDir().mkpath(instanceC);
    QString snapshotProblem;
    const Presets::TargetSnapshot fresh =
        Presets::readTargetSnapshot(instanceC, &snapshotProblem);
    check(snapshotProblem.isEmpty(), "PG7 新实例没有 options.txt 是正常的",
          snapshotProblem);
    check(fresh.instanceRoot == instanceC || !fresh.instanceRoot.isEmpty(),
          "PG7 快照带着实例根");

    {
        const QString root = scratch.filePath(QStringLiteral("resume-instance"));
        QDir().mkpath(root);
        writeBytes(root + "/options.txt", "fov:0.91\nkey_attack:key.keyboard.z\n");
        Presets::PresetManifest resumeManifest = saved;
        resumeManifest.sections.clear();
        for (const auto &section : saved.sections)
            if (Presets::optionsHandlesSection(section.kind)) resumeManifest.sections.append(section);
        check(resumeManifest.sections.size() == 2, "recovery fixture has separate options and key sections");
        if (resumeManifest.sections.size() == 2) {
            Presets::PresetApplyResult partial;
            for (const auto &section : resumeManifest.sections) {
                Presets::PresetApplyRow row;
                row.sectionId = section.id; row.kind = section.kind;
                row.ok = section.kind == Presets::SectionKind::VanillaOptions;
                row.blocked = !row.ok; row.written = row.ok ? 1 : 0;
                partial.rows.append(row);
            }
            QString problem;
            check(Presets::writePresetExecutionReport(root, resumeManifest, partial, true, {}, &problem),
                  "partial application report persists completed and unfinished sections", problem);
            Presets::PresetApplyRequest resume;
            resume.instanceId = "resume-instance"; resume.instanceRoot = root; resume.resumeUnfinished = true;
            const auto resumed = Presets::applyPresetToInstance(presetsRoot, resumeManifest, resume);
            check(resumed.ok && readBytes(root + "/options.txt").contains("fov:0.91")
                      && readBytes(root + "/options.txt").contains("key_attack:key.keyboard.a"),
                  "resume only applies unfinished keys and preserves later edits to the completed settings section");
            auto changed = resumeManifest; changed.name += " changed";
            const auto before = readBytes(root + "/options.txt");
            const auto refused = Presets::applyPresetToInstance(presetsRoot, changed, resume);
            check(!refused.ok && readBytes(root + "/options.txt") == before,
                  "changed preset fingerprint refuses recovery before writing target files");
            auto held = Presets::acquirePresetWrite(root, &problem);
            const auto busy = Presets::applyPresetToInstance(presetsRoot, resumeManifest, resume);
            check(held && !busy.ok && readBytes(root + "/options.txt") == before,
                  "a second preset writer is refused while the instance write lock is held");
        }
    }

    // 名字是 options.txt、其实是**目录** ⇒ 读不动（不能当成"空的那一份"去铺预设）。
    QDir().mkpath(instanceC + QStringLiteral("/options.txt"));
    snapshotProblem.clear();
    Presets::readTargetSnapshot(instanceC, &snapshotProblem);
    check(!snapshotProblem.isEmpty(),
          "★★★ PG7 `options.txt` 读不动 ⇒ 快照带着一句人话（不然整份设置会被当成空的铺掉）",
          snapshotProblem);

    // ------------------------------------------------------------------
    // PG8 从实例提取模组：两个来源认出来的那一堆怎么合（纯函数，不联网）
    // ------------------------------------------------------------------
    {
        using Presets::LocalModIdentity;
        const QList<LocalModIdentity> mods{
            {QStringLiteral("aa"), 111u, true},   // 只有 Modrinth 认得
            {QStringLiteral("bb"), 222u, true},   // 两家都认得
            {QStringLiteral("cc"), 333u, true},   // 只有 CurseForge 认得
            {QStringLiteral("dd"), 444u, false},  // 指纹没算出来，而表里恰好有 444
            {QStringLiteral("ee"), 0u, false},    // 什么都没问出来
        };
        QHash<QString, QString> modrinth;
        modrinth.insert(QStringLiteral("aa"), QStringLiteral("sodium"));
        modrinth.insert(QStringLiteral("bb"), QStringLiteral("fabric-api"));
        QHash<quint32, QString> curseForge;
        curseForge.insert(222u, QStringLiteral("306612"));
        curseForge.insert(333u, QStringLiteral("238222"));
        curseForge.insert(444u, QStringLiteral("999999"));
        const QList<Presets::ResolvedModSource> resolved =
            Presets::resolveModSources(mods, modrinth, curseForge);
        check(resolved.size() == mods.size(), "PG8 一条本地 jar 对一条结果",
              QString::number(resolved.size()));
        check(resolved.value(0).platform == QLatin1String("modrinth") &&
                  resolved.value(0).projectId == QLatin1String("sodium"),
              "PG8 Modrinth 认得出就写它", resolved.value(0).platform);
        check(resolved.value(1).platform == QLatin1String("modrinth") &&
                  resolved.value(1).projectId == QLatin1String("fabric-api"),
              "★★ PG8 两家都认得出来时跟 Modrinth 走（CurseForge 那一格不盖过来）",
              resolved.value(1).platform);
        check(resolved.value(2).platform == QLatin1String("curseforge") &&
                  resolved.value(2).projectId == QLatin1String("238222"),
              "PG8 Modrinth 没认出来、指纹对上了 ⇒ 走 CurseForge");
        check(resolved.value(3).platform.isEmpty() && resolved.value(3).projectId.isEmpty(),
              "★★ PG8 指纹没算出来时，表里那个 444 不许拿来用（0 也是合法指纹）",
              resolved.value(3).platform);
        check(resolved.value(4).platform.isEmpty() && resolved.value(4).projectId.isEmpty(),
              "PG8 两边都没认出来 ⇒ 两格都空，等用户在编辑页补");
        const QList<Presets::LocalModIdentity> noMods;
        check(Presets::resolveModSources(noMods, modrinth, curseForge).isEmpty(),
              "PG8 没有本地 jar ⇒ 一条结果都没有");
    }
}

