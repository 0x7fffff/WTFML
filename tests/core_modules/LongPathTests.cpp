#include "Support.hpp"
#include "LongPathTests.hpp"
#include "manage/WorldFiles.hpp"
#include "operations/TransferApply.hpp"
#include "util/ZipUtil.hpp"
#include "util/PowerShell.hpp"
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#ifdef Q_OS_WIN
namespace {
bool writeFixture(const QString &path, const QByteArray &bytes) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray readFixture(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
}

#endif

void runLongPathTests() {
#ifdef Q_OS_WIN
    section("P2 long paths: real file operations beyond MAX_PATH");
    {
        // 起 PowerShell 不许拿裸名：`CreateProcess` 找程序时会看**当前目录**和 PATH，
        // 便携版从下载目录里跑起来的时候，同目录放一个 powershell.exe 就会被我们执行。
        // SystemRoot 读不到（构造出来的环境）时才允许回退裸名 —— 判定和实现同一份。
        const QFileInfo powerShell(PowerShell::executable());
        const bool systemCopy = qEnvironmentVariableIsEmpty("SystemRoot")
            || (powerShell.isAbsolute() && powerShell.isFile()
                && powerShell.fileName().compare(QStringLiteral("powershell.exe"),
                                                 Qt::CaseInsensitive) == 0);
        check(systemCopy,
              "PowerShell is resolved to the system copy, not a bare name searched in the working directory",
              PowerShell::executable());
    }
    QTemporaryDir fixture;
    check(fixture.isValid(), "long-path fixture uses temporary storage");
    if (!fixture.isValid()) return;
    QString deep = fixture.path();
    const QString component = QStringLiteral("长路径 目录-") + QString(24, QLatin1Char('x'));
    while (deep.size() < 360) deep += QLatin1Char('/') + component;
    const QString source = deep + QStringLiteral("/source");
    const QString target = deep + QStringLiteral("/target");
    const QByteArray payload("long-path\0payload", 17);
    const QString filename = QStringLiteral("测试模组 ' 空格.jar");
    const QString mod = source + QStringLiteral("/mods/") + filename;
    const QString copied = target + QStringLiteral("/mods/") + filename;
    const bool prepared = QDir().mkpath(target) && writeFixture(mod, payload);
    check(prepared && mod.size() > 260 && copied.size() > 260, "source and destination paths really exceed 260 characters");
    if (!prepared) return;
    check(InstanceFiles::pathIsSameOrInside(fixture.path(), source) &&
          !InstanceFiles::pathIsSameOrInside(source, target), "long-path canonical boundary recognizes sibling instances");
    auto transferred = TransferApply::transferMods(source, target, {mod}, false);
    check(transferred.copied == 1 && transferred.failed == 0 && readFixture(copied) == payload && readFixture(mod) == payload,
          "mod copy publishes verified content on long Unicode paths", transferred.problems.join(QStringLiteral("; ")));
    transferred = TransferApply::transferMods(source, target, {mod}, true);
    check(transferred.skipped == 1 && QFileInfo::exists(mod) && readFixture(copied) == payload,
          "existing long-path destination is skipped without deleting source");
    const QString moveSource = source + QStringLiteral("/mods/move.jar");
    check(writeFixture(moveSource, payload), "long-path move fixture is written");
    transferred = TransferApply::transferMods(source, target, {moveSource}, true);
    check(transferred.moved == 1 && transferred.failed == 0 && !QFileInfo::exists(moveSource) &&
          readFixture(target + QStringLiteral("/mods/move.jar")) == payload,
          "mod move verifies long-path publication before removing source", transferred.problems.join(QStringLiteral("; ")));

    const QString lockedPath = source + QStringLiteral("/mods/locked.jar");
    check(writeFixture(lockedPath, payload), "locked long-path fixture is written");
    QString native = QDir::toNativeSeparators(lockedPath);
    native = native.startsWith(QStringLiteral("\\\\")) ? QStringLiteral("\\\\?\\UNC\\") + native.mid(2)
                                                             : QStringLiteral("\\\\?\\") + native;
    const HANDLE locked = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), GENERIC_READ, 0,
                                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(locked != INVALID_HANDLE_VALUE, "exclusive Windows handle actually holds the long-path source");
    if (locked != INVALID_HANDLE_VALUE) {
        transferred = TransferApply::transferMods(source, target, {lockedPath}, true);
        CloseHandle(locked);
        check(transferred.failed == 1 && transferred.copied == 0 && !transferred.problems.isEmpty() &&
              transferred.problems.join(QString()).contains(QStringLiteral("locked.jar")) &&
              readFixture(lockedPath) == payload && !QFileInfo::exists(target + QStringLiteral("/mods/locked.jar")),
              "occupied source reports a file-specific failure and preserves unfinished move");
    }

    const QString world = source + QStringLiteral("/saves/源世界");
    check(writeFixture(world + QStringLiteral("/level.dat"), QByteArrayLiteral("level fixture")) &&
          writeFixture(world + QStringLiteral("/region/r.0.0.mca"), payload), "long-path world fixture is written");
    const QString saves = target + QStringLiteral("/saves");
    check(QDir().mkpath(saves), "destination saves directory exists before world import");
    QString problem;
    const bool worldCopied = WorldFiles::copyWorldInto(world, saves, QStringLiteral("复制世界"), &problem);
    check(worldCopied && readFixture(saves + QStringLiteral("/复制世界/region/r.0.0.mca")) == payload,
          "world copy preserves content through long paths", problem);
    if (worldCopied) {
        problem.clear();
        check(WorldFiles::renameWorld(saves + QStringLiteral("/复制世界"), QStringLiteral("改名世界"), &problem) &&
              readFixture(saves + QStringLiteral("/改名世界/level.dat")) == QByteArrayLiteral("level fixture"),
              "world rename works without truncating long parent paths", problem);
    }

    QString entry = QStringLiteral("assets");
    while (entry.size() < 280) entry += QLatin1Char('/') + component;
    entry += QStringLiteral("/信息 ' 文件.bin");
    const QString archive = deep + QStringLiteral("/archives/测试 ' 归档.zip");
    problem.clear();
    const bool zipped = ZipUtil::createZip(archive, {{entry, mod}, {QStringLiteral("../../escape.bin"), mod},
        {QStringLiteral("nested/.. /trimmed.bin"), mod}, {fixture.path() + QStringLiteral("/absolute-escape.bin"), mod}}, &problem);
    check(zipped, "ZIP creation accepts long source/archive paths and quoted Unicode names", problem);
    if (zipped) {
        QByteArray bytes;
        problem.clear();
        check(ZipUtil::readEntry(archive, entry, &bytes, &problem) == ZipUtil::EntryStatus::Ok && bytes == payload,
              "ZIP entry can be read from a long archive path", problem);
        const auto metadata = ZipUtil::readMetadataBatch({archive}, {entry}, &problem);
        check(metadata.contains(archive) && metadata.value(archive).entries.value(entry) == payload,
              "batch metadata keeps original archive identity while opening long paths", problem);
        QStringList names;
        check(ZipUtil::listEntries(archive, &names, &problem) == ZipUtil::EntryStatus::Ok && names.contains(entry),
              "archive listing preserves logical entry names", problem);
        const QString single = deep + QStringLiteral("/single/entry.bin");
        check(ZipUtil::extractEntryToFile(archive, entry, single, &problem) == ZipUtil::EntryStatus::Ok && readFixture(single) == payload,
              "single-entry extraction supports a long output path", problem);
        const QString extracted = deep + QStringLiteral("/extract");
        problem.clear();
        const bool unpacked = ZipUtil::extractArchives({archive}, extracted, {}, &problem);
        check(unpacked && readFixture(extracted + QLatin1Char('/') + entry) == payload,
              "ZIP extraction supports nested output paths exceeding 600 characters", problem);
        check(!QFileInfo::exists(QDir::cleanPath(extracted + QStringLiteral("/../../escape.bin"))) &&
              !QFileInfo::exists(fixture.path() + QStringLiteral("/absolute-escape.bin")) &&
              !QFileInfo::exists(extracted + QStringLiteral("/nested/.. /trimmed.bin")),
              "extended I/O paths do not bypass ZIP traversal and trailing-space guards");
        const QString subtree = deep + QStringLiteral("/subtree");
        check(ZipUtil::extractSubtrees({archive}, subtree, {QStringLiteral("assets/")}, nullptr, &problem) &&
              readFixture(subtree + QLatin1Char('/') + entry.mid(7)) == payload,
              "subtree extraction strips logical prefixes without changing them into I/O paths", problem);
    }
    const QString gz = deep + QStringLiteral("/gzip/fixture.gz");
    const QString restored = deep + QStringLiteral("/gunzip/fixture.bin");
    check(ZipUtil::gzipFrom(mod, gz, &problem) && ZipUtil::gunzipTo(gz, restored, &problem) && readFixture(restored) == payload,
          "gzip roundtrip accepts long input and output paths", problem);
    const QString blocking = deep + QStringLiteral("/blocked-parent");
    check(writeFixture(blocking, payload), "archive failure fixture uses an existing file as parent");
    problem.clear();
    check(!ZipUtil::createZip(blocking + QStringLiteral("/archive.zip"), {{QStringLiteral("entry.bin"), mod}}, &problem) &&
          !problem.isEmpty() && !problem.contains(QStringLiteral("PowerShell 退出码")) && !problem.contains(QStringLiteral("CLIXML")) &&
          readFixture(blocking) == payload, "archive setup failure reports its actual cause without damaging existing file", problem);
#else
    out() << "SKIP Windows long-path acceptance on a non-Windows host\n";
#endif
}
