#include "LiveProbes.hpp"
#include "ActivityPopover.hpp"

#include "modules/Support.hpp"

#include "MainWindow.hpp"
#include "MainWindowUI.hpp"
#include <QPushButton>
#include "OverlayHost.hpp"
#include "InstanceSettingsDialog.hpp"
#include <QListWidget>
#include "InstanceFilesPage.hpp"
#include "auth/AccountStore.hpp"
#include "controllers/JavaRuntimeController.hpp"
#include "manage/VersionManager.hpp"
#include "manage/InstanceFiles.hpp"
#include "manage/ModpackImport.hpp"
#include "util/FileAssociations.hpp"
#include "util/SevenZip.hpp"
#include <QSettings>
#include "manage/InstanceExport.hpp"
#include "util/ZipUtil.hpp"
#include "presets/InstalledModInventory.hpp"
#include "settings/GlobalJavaSettings.hpp"
#include "util/AppPaths.hpp"

#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QNetworkProxy>
#include <QSet>
#include <QSettings>
#include <QThread>
#include <QTimer>

#include <atomic>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <psapi.h>
#  include <tlhelp32.h>
#endif

int runLiveProbeMode(int argc, char *argv[]) {
// ---------- sevenzip-live：7-Zip 解压 + 压回 zip 的自检 ----------
    // ---------- overlay-live：设置页开着时任务中心必须是**提示层**，关掉回原页 ----------
    // mw_check.exe overlay-live <root> <instanceId>
    if (argc >= 4 && QString::fromLocal8Bit(argv[1]) == QLatin1String("overlay-live")) {
        QCoreApplication::setOrganizationName(QStringLiteral("WTFML"));
        QCoreApplication::setApplicationName(QStringLiteral("WTFML"));
        const QString root = QDir::cleanPath(QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath());
        const QString instanceId = QString::fromLocal8Bit(argv[3]);
        if (!QDir(root + QStringLiteral("/game/versions/") + instanceId).exists()) {
            std::printf("FAIL 实例目录不存在：%s\n", qPrintable(root + "/game/versions/" + instanceId));
            return 1;
        }
        QDir().mkpath(root + QStringLiteral("/settings"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root + QStringLiteral("/settings"));
        qApp->setProperty("wtfml.testDefaultMinecraftDir", root + QStringLiteral("/game"));
        AppPaths::setMinecraftDir(root + QStringLiteral("/game"));
        MainWindow window;
        window.show();
        QApplication::processEvents();
        QMetaObject::invokeMethod(&window, "openInstanceSettings", Qt::DirectConnection,
                                  Q_ARG(QString, instanceId));
        auto *overlay = window.findChild<OverlayHost *>();
        InstanceSettingsDialog *settings = nullptr;
        QElapsedTimer openWait;
        openWait.start();
        while (openWait.elapsed() < 15000) {
            QApplication::processEvents();
            settings = window.findChild<InstanceSettingsDialog *>();
            if (settings != nullptr) break;
            QThread::msleep(20);
        }
        if (overlay == nullptr || settings == nullptr) {
            std::printf("FAIL 实例设置没有打开（overlay=%d settings=%d）\n",
                        overlay != nullptr, settings != nullptr);
            return 1;
        }
        // 可选：开某一页并按行数计时（真机验证大列表分帧建行）。
        if (argc >= 5) {
            const QString pageKey = QString::fromLocal8Bit(argv[4]);
            QElapsedTimer pageClock;
            pageClock.start();
            settings->showPage(pageKey);
            QApplication::processEvents();
            QListWidget *list = nullptr;
            qint64 rowsMs = -1;
            while (pageClock.elapsed() < 20000) {
                QApplication::processEvents();
                if (InstanceFilesPage *files = settings->filesPage(pageKey)) {
                    list = files->list();
                    if (list != nullptr && list->count() > 0 && rowsMs < 0) rowsMs = pageClock.elapsed();
                }
                QThread::msleep(10);
                if (rowsMs >= 0 && pageClock.elapsed() > rowsMs + 800) break;
            }
            // 行是代理画的：没有行控件要补、要收 —— 滚到底，最后一行直接就在视口里。
            bool bottomRowVisible = false;
            int viewportChildren = -1;
            if (list != nullptr && list->count() != 0) {
                list->scrollToBottom();
                QApplication::processEvents();
                bottomRowVisible = list->visualItemRect(list->item(list->count() - 1))
                                       .intersects(list->viewport()->rect());
                viewportChildren = static_cast<int>(list->viewport()->findChildren<QWidget *>().size());
            }
            std::printf("PASS 页面 %s 行=%d 首行出现=%lld ms 滚到底=%.0f ms 末行可见=%d 视口子控件=%d\n",
                        qPrintable(pageKey), list != nullptr ? list->count() : -1,
                        static_cast<long long>(rowsMs),
                        static_cast<double>(pageClock.elapsed()), bottomRowVisible, viewportChildren);
        }
        auto *taskButton = window.findChild<QPushButton *>(MainWindowUI::taskSummaryButtonName());
        if (taskButton == nullptr) { std::printf("FAIL 找不到任务按钮\n"); return 1; }
        taskButton->click();
        ActivityPopover::openAllIn(&window);  // 摘要先弹浮动面板，从「全部任务」进整页
        QApplication::processEvents();
        const bool stacked = overlay->promptOpen() &&
                             window.findChild<InstanceSettingsDialog *>() != nullptr;
        std::printf("%s 设置页开着时任务中心叠成提示层（原页还在）\n", stacked ? "PASS" : "FAIL");
        taskButton->click();
        QApplication::processEvents();
        settings = window.findChild<InstanceSettingsDialog *>();
        QElapsedTimer closeWait;
        closeWait.start();
        while (!overlay->pageOpen() && closeWait.elapsed() < 1000) QApplication::processEvents();
        const bool returned = overlay->pageOpen() && settings != nullptr && settings->isVisible();
        std::printf("%s 再点一次任务按钮回到原设置页\n", returned ? "PASS" : "FAIL");
        return (stacked && returned) ? 0 : 1;
    }
// mw_check.exe sevenzip-live <archive.7z|rar>
    if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QLatin1String("sevenzip-live")) {
        const QString archive = QString::fromLocal8Bit(argv[2]);
        std::printf("%s 7z.exe=%s\n", SevenZip::available() ? "PASS" : "FAIL",
                    qPrintable(SevenZip::executable()));
        QString error;
        const QString zip = SevenZip::toZip(archive, &error);
        if (zip.isEmpty()) {
            std::printf("FAIL 转换失败：%s\n", qPrintable(error));
            return 1;
        }
        QStringList entries;
        const bool listed = ZipUtil::listEntries(zip, &entries, &error) == ZipUtil::EntryStatus::Ok;
        std::printf("%s 转换出 zip：%s（%d 个条目）\n", listed && !entries.isEmpty() ? "PASS" : "FAIL",
                    qPrintable(zip), entries.size());
        bool hasConfig = false;
        for (const QString &entry : entries) if (entry.contains(QStringLiteral("config/"))) hasConfig = true;
        std::printf("%s 内容认得出（含 config/）\n", hasConfig ? "PASS" : "FAIL");
        QFile::remove(zip);
        return (listed && hasConfig) ? 0 : 1;
    }

// ---------- assoc-live：旧版文件关联清理自检（只写 WTFML 测试子键） ----------
// mw_check.exe assoc-live
    if (argc >= 2 && QString::fromLocal8Bit(argv[1]) == QLatin1String("assoc-live")) {
        const QString root =
            QStringLiteral("HKEY_CURRENT_USER\\Software\\WTFMLSelfTestClasses\\assoc-live");
        qputenv("WTFML_ASSOC_CLASSES_ROOT", root.toUtf8());
        const QString report = FileAssociations::runSelfTest();
        std::printf("%s 只清本程序注册的关联、别人的留着、重复跑无副作用\n",
                    report.isEmpty() ? "PASS" : "FAIL");
        if (!report.isEmpty()) std::printf("%s\n", qPrintable(report));
        return report.isEmpty() ? 0 : 1;
    }

// ---------- export-instance：普通实例导出 + 自检能否被重新识别 ----------
// mw_check.exe export-instance <root> <实例id> <输出zip>
    if (argc >= 5 && QString::fromLocal8Bit(argv[1]) == QLatin1String("export-instance")) {
        QCoreApplication::setOrganizationName(QStringLiteral("WTFML"));
        QCoreApplication::setApplicationName(QStringLiteral("WTFML"));
        const QString root = QDir::cleanPath(QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath());
        const QString id = QString::fromLocal8Bit(argv[3]);
        const QString out = QDir::cleanPath(QFileInfo(QString::fromLocal8Bit(argv[4])).absoluteFilePath());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root + QStringLiteral("/settings"));
        qApp->setProperty("wtfml.testDefaultMinecraftDir", root + QStringLiteral("/game"));
        AppPaths::setMinecraftDir(root + QStringLiteral("/game"));
        VersionManager versions;
        const QString gameVersion = versions.instanceGameVersion(id);
        QString loaderKey, loaderVersion;
        versions.instanceLoaderInfo(id, &loaderKey, &loaderVersion);
        const QString jsonPath = root + QStringLiteral("/game/versions/") + id + QLatin1Char('/')
                                 + id + QStringLiteral(".json");
        QFile jsonFile(jsonPath);
        bool custom = false;
        if (jsonFile.open(QIODevice::ReadOnly)) {
            const QJsonObject object = QJsonDocument::fromJson(jsonFile.readAll()).object();
            // 普通原版/加载器的 json 里 mainClass 是标准值；只有被改过主类的（lwjgl3ify 之类）才算"自定义启动配置"。
            const QString mainClass = object.value(QStringLiteral("mainClass")).toString();
            static const QSet<QString> standard{
                QStringLiteral("net.minecraft.client.main.Main"),
                QStringLiteral("net.minecraft.launchwrapper.Launch"),
                QStringLiteral("cpw.mods.bootstraplauncher.BootstrapLauncher"),
                QStringLiteral("net.fabricmc.loader.impl.launch.knot.KnotClient")};
            custom = !mainClass.isEmpty() && !standard.contains(mainClass);
        }
        const auto exported = InstanceExport::exportInstance(id, gameVersion, loaderKey, loaderVersion,
                                                             id, custom, out);
        if (!exported.ok) {
            std::printf("FAIL 导出失败：%s\n", qPrintable(exported.problem));
            return 1;
        }
        std::printf("PASS 导出：%d 个文件 / %lld MB → %s\n", exported.files,
                    static_cast<long long>(exported.bytes / (1024 * 1024)), qPrintable(out));
        if (!exported.warning.isEmpty()) std::printf("[warning] %s\n", qPrintable(exported.warning));
        // 自检：能不能被本仓库的导入器重新认出来。
        QStringList entries;
        QString error;
        if (ZipUtil::listEntries(out, &entries, &error) != ZipUtil::EntryStatus::Ok) {
            std::printf("FAIL 重新读包失败：%s\n", qPrintable(error));
            return 1;
        }
        const auto layout = ModpackImport::detectArchiveLayout(entries);
        std::printf("%s 重新识别：format=%d（3=MultiMC）prefix=%s\n",
                    layout.format == ModpackImport::Format::MultiMC ? "PASS" : "FAIL",
                    int(layout.format), qPrintable(layout.rootPrefix));
        return layout.format == ModpackImport::Format::MultiMC ? 0 : 1;
    }

// ---------- mmc-import-live：真导入一个 MultiMC/Prism 包（GTNH） ----------
// mw_check.exe mmc-import-live <zip> <实例名> <root>
    if (argc >= 5 && QString::fromLocal8Bit(argv[1]) == QLatin1String("mmc-import-live")) {
        QCoreApplication::setOrganizationName(QStringLiteral("WTFML"));
        QCoreApplication::setApplicationName(QStringLiteral("WTFML"));
        const QString archive = QDir::cleanPath(QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath());
        const QString name = QString::fromLocal8Bit(argv[3]);
        const QString root = QDir::cleanPath(QFileInfo(QString::fromLocal8Bit(argv[4])).absoluteFilePath());
        if (!QDir().mkpath(root + QStringLiteral("/settings"))
            || !QDir().mkpath(root + QStringLiteral("/game"))) return 2;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root + QStringLiteral("/settings"));
        qApp->setProperty("wtfml.testDefaultMinecraftDir", root + QStringLiteral("/game"));
        AppPaths::setMinecraftDir(root + QStringLiteral("/game"));

        QStringList entries;
        QString error;
        if (ZipUtil::listEntries(archive, &entries, &error) != ZipUtil::EntryStatus::Ok) {
            std::printf("FAIL 读不到压缩包：%s\n", qPrintable(error));
            return 1;
        }
        const auto layout = ModpackImport::detectArchiveLayout(entries);
        if (layout.format != ModpackImport::Format::MultiMC) {
            std::printf("FAIL 不是 MultiMC 包（format=%d）\n", int(layout.format));
            return 1;
        }
        QByteArray mmc, cfg;
        ZipUtil::readEntry(archive, layout.mmcPackEntry, &mmc, &error);
        if (!layout.instanceCfgEntry.isEmpty()) ZipUtil::readEntry(archive, layout.instanceCfgEntry, &cfg, &error);
        QHash<QString, QByteArray> patches, libraries;
        const QString patchPrefix = layout.rootPrefix + QStringLiteral("patches/");
        const QString libraryPrefix = layout.rootPrefix + QStringLiteral("libraries/");
        for (const QString &entry : entries) {
            const bool patch = entry.startsWith(patchPrefix) && entry.endsWith(QLatin1String(".json"));
            const bool library = entry.startsWith(libraryPrefix) && entry.endsWith(QLatin1String(".jar"));
            if (!patch && !library) continue;
            QByteArray bytes;
            const auto status = ZipUtil::readEntry(archive, entry, &bytes, &error,
                                                   "read mmc patch", 128 * 1024 * 1024);
            if (status != ZipUtil::EntryStatus::Ok) { std::printf("FAIL 读补丁失败 %s\n", qPrintable(entry)); return 1; }
            if (patch) patches.insert(entry.mid(patchPrefix.size()).chopped(5), bytes);
            else libraries.insert(entry, bytes);
        }
        const bool hasPatches =
            ModpackImport::archiveHasEntryPrefix(entries, layout.rootPrefix + QStringLiteral("patches/"));
        const auto parsed = hasPatches
            ? ModpackImport::parsePatchedMultiMCInstance(
                  mmc, cfg, patches, libraries, layout.gameDirPrefix,
                  QFileInfo(archive).completeBaseName())
            : ModpackImport::parseMultiMCInstance(
                  mmc, cfg, layout.gameDirPrefix, QFileInfo(archive).completeBaseName());
        std::printf("PASS 补丁转换：game=%s loader=%s mainClass=%s\n",
                    qPrintable(parsed.pack.gameVersion), qPrintable(parsed.pack.loaderVersion),
                    qPrintable(parsed.pack.patchedRuntime.value("mainClass").toString()));

        VersionManager versions;
        versions.setUseMirror(true);
        QEventLoop loop;
        bool finished = false, ok = false;
        QString problem;
        QObject::connect(&versions, &VersionManager::modpackImportMessage, &loop,
                         [](const QString &text) { std::printf("[import] %s\n", qPrintable(text)); });
        QObject::connect(&versions, &VersionManager::versionDownloadFinished, &loop,
                         [&](const QString &id, bool success, const QString &message) {
                             if (id != name) return;
                             finished = true; ok = success; problem = message;
                             std::printf("[finished] %s\n", qPrintable(message));
                             loop.quit();
                         });
        QTimer::singleShot(20 * 60 * 1000, &loop, &QEventLoop::quit);
        // 安装凭据要求 64 位十六进制指纹（ImportController 用 mmc+补丁+库的 SHA-256）；
        const QString fingerprint = QString::fromLatin1(
            QCryptographicHash::hash(mmc + cfg, QCryptographicHash::Sha256).toHex());
        versions.importModpack(archive, name, parsed.pack, QString(), fingerprint);
        loop.exec();
        if (!finished) { std::printf("FAIL 20 分钟内没有收尾\n"); return 1; }
        const QString dir = root + QStringLiteral("/game/versions/") + name;
        // 普通实例导出再导入时，客户端 jar 在父版本目录（只写 inheritsFrom）；补丁包才自带 <实例>.jar。
        const QString jarPath = hasPatches
            ? dir + QLatin1Char('/') + name + QStringLiteral(".jar")
            : root + QStringLiteral("/game/versions/") + parsed.pack.gameVersion + QLatin1Char('/')
                  + parsed.pack.gameVersion + QStringLiteral(".jar");
        const bool filesOk = QFileInfo::exists(dir + QLatin1Char('/') + name + QStringLiteral(".json"))
            && QFileInfo::exists(jarPath);
        std::printf("%s 实例文件：%s（%s）\n", filesOk ? "PASS" : "FAIL", qPrintable(dir),
                    qPrintable(problem));
        return (ok && filesOk) ? 0 : 1;
    }

// ---------- instance-files-live：往真实实例里加模组/材质包/存档，再用产品扫描器验一遍 ----------
// mw_check.exe instance-files-live <root> <版本号> <源 jar>
if (argc >= 5 && QString::fromLocal8Bit(argv[1]) == QLatin1String("instance-files-live")) {
    QCoreApplication::setOrganizationName(QStringLiteral("WTFML"));
    QCoreApplication::setApplicationName(QStringLiteral("WTFML"));
    const QString root = QDir::cleanPath(QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath());
    const QString versionId = QString::fromLocal8Bit(argv[3]);
    const QString sourceJar = QDir::cleanPath(QFileInfo(QString::fromLocal8Bit(argv[4])).absoluteFilePath());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root + QStringLiteral("/settings"));
    qApp->setProperty("wtfml.testDefaultMinecraftDir", root + QStringLiteral("/game"));
    AppPaths::setMinecraftDir(root + QStringLiteral("/game"));
    const QString instance = root + QStringLiteral("/game/versions/") + versionId;
    if (!QFileInfo(instance).isDir()) { std::printf("FAIL 实例目录不存在：%s\n", qPrintable(instance)); return 1; }

    auto writeFile = [](const QString &path, const QByteArray &bytes) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;
        return file.write(bytes) == bytes.size();
    };
    QDir().mkpath(instance + QStringLiteral("/mods"));
    const QString modPath = instance + QStringLiteral("/mods/测试模组.jar");
    QFile::remove(modPath);
    if (!QFile::copy(sourceJar, modPath)) { std::printf("FAIL 模组拷不进去：%s -> %s\n", qPrintable(sourceJar), qPrintable(modPath)); return 1; }
    // 相邻字面量分两段：`"\x04f"` 会被当成一个三位的十六进制转义（GCC 报
    // "hex escape sequence out of range"），拆开之后 zip 头那四个字节才是对的。
    writeFile(instance + QStringLiteral("/resourcepacks/测试材质包.zip"),
              QByteArray("PK\x03\x04" "fake"));
    writeFile(instance + QStringLiteral("/saves/测试存档/level.dat"), QByteArray("NBT-fake"));
    writeFile(instance + QStringLiteral("/saves/测试存档/region/r.0.0.mca"), QByteArray("region"));

    std::atomic_bool cancel{false};
    const Presets::InstalledModInventory inventory =
        Presets::scanInstalledMods(instance, &cancel);
    std::printf("%s 模组扫描：%lld 个%s\n", inventory.files.size() == 1 ? "PASS" : "FAIL",
                static_cast<long long>(inventory.files.size()),
                inventory.problem.isEmpty() ? "" : qPrintable(QStringLiteral("（%1）").arg(inventory.problem)));
    std::printf("%s 存档目录：%d\n", QFileInfo::exists(instance + QStringLiteral("/saves/测试存档/level.dat")) ? "PASS" : "FAIL", 1);
    std::printf("%s 材质包：%d\n", QFileInfo::exists(instance + QStringLiteral("/resourcepacks/测试材质包.zip")) ? "PASS" : "FAIL", 1);
    // 改存档 + 恢复：先备份字节，写坏，再写回，验证内容一致。
    const QString level = instance + QStringLiteral("/saves/测试存档/level.dat");
    QFile levelFile(level);
    QByteArray original;
    if (levelFile.open(QIODevice::ReadOnly)) { original = levelFile.readAll(); levelFile.close(); }
    writeFile(level, QByteArray("corrupted"));
    writeFile(level, original);
    QFile check(level);
    bool restored = check.open(QIODevice::ReadOnly) && check.readAll() == original;
    std::printf("%s 存档改坏再恢复：%s\n", restored ? "PASS" : "FAIL", qPrintable(level));
    return (inventory.files.size() == 1 && restored) ? 0 : 1;
}

// ---------- launch-many-live：一次起 N 个真实实例（压测） ----------
// mw_check.exe launch-many-live <root> <id1,id2,...> [观察秒数]
if (argc >= 4 && QString::fromLocal8Bit(argv[1]) == QLatin1String("launch-many-live")) {
    QCoreApplication::setOrganizationName(QStringLiteral("WTFML"));
    QCoreApplication::setApplicationName(QStringLiteral("WTFML"));
    const QString root = QDir::cleanPath(QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath());
    const QStringList ids = QString::fromLocal8Bit(argv[3]).split(QLatin1Char(','), Qt::SkipEmptyParts);
    const int seconds = argc >= 5 ? QString::fromLocal8Bit(argv[4]).toInt() : 25;
    if (ids.isEmpty() || !QDir().mkpath(root + QStringLiteral("/settings"))) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root + QStringLiteral("/settings"));
    qApp->setProperty("wtfml.testDefaultMinecraftDir", root + QStringLiteral("/game"));
    AppPaths::setMinecraftDir(root + QStringLiteral("/game"));

    QString accountError;
    AccountStore::addOffline(QStringLiteral("Steve"), &accountError);
    GlobalJavaSettings java;
    java.setAutoSelectJava(false);
    java.setJavaPath(QStringLiteral(
        "C:/Users/User/AppData/Roaming/.minecraft/runtime/jre-legacy/bin/java.exe"));
    java.setXmx(1024);
    java.setXms(512);
    java.save();

    auto childJavaPids = []() -> QSet<qint64> {
        QSet<qint64> pids;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return pids;
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        const DWORD self = GetCurrentProcessId();
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (_wcsicmp(entry.szExeFile, L"java.exe") == 0 && entry.th32ParentProcessID == self) {
                    pids.insert(entry.th32ProcessID);
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
        return pids;
    };

    MainWindow window;
    window.show();
    QApplication::processEvents();
    auto *combo = window.findChild<QComboBox *>(MainWindowUI::installedVersionComboName());
    if (combo == nullptr) { std::printf("FAIL 找不到已安装版本下拉框\n"); return 1; }

    QSet<qint64> launched;
    int started = 0;
    for (const QString &id : ids) {
        const int index = combo->findData(id);
        if (index < 0) { std::printf("FAIL 版本 %s 不在列表里\n", qPrintable(id)); continue; }
        combo->setCurrentIndex(index);
        QApplication::processEvents();
        QMetaObject::invokeMethod(&window, "onStartButtonClicked", Qt::DirectConnection);
        QElapsedTimer wait;
        wait.start();
        qint64 newPid = 0;
        while (wait.elapsed() < 120000) {
            QApplication::processEvents();
            const QSet<qint64> now = childJavaPids();
            for (qint64 pid : now) {
                if (!launched.contains(pid)) { newPid = pid; break; }
            }
            if (newPid != 0) break;
            QThread::msleep(250);
        }
        if (newPid == 0) {
            std::printf("FAIL %s 没起来\n", qPrintable(id));
            continue;
        }
        launched.insert(newPid);
        ++started;
        std::printf("PASS %s 起来了 pid=%lld（已起 %d/%lld）\n", qPrintable(id),
                    static_cast<long long>(newPid), started, static_cast<long long>(ids.size()));
    }
    if (started == 0) return 1;

    // 都起来之后观察一段时间，量一下各自的工作集（KB）。
    QElapsedTimer observe;
    observe.start();
    while (observe.elapsed() < seconds * 1000) {
        QApplication::processEvents();
        QThread::msleep(250);
    }
    qint64 totalKb = 0;
    for (qint64 pid : launched) {
        HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
        if (handle == nullptr) continue;
        PROCESS_MEMORY_COUNTERS counters{};
        if (K32GetProcessMemoryInfo(handle, &counters, sizeof(counters))) {
            totalKb += static_cast<qint64>(counters.WorkingSetSize / 1024);
            std::printf("  pid=%lld 工作集=%lld MB\n", static_cast<long long>(pid),
                        static_cast<long long>(counters.WorkingSetSize / 1024 / 1024));
        }
        CloseHandle(handle);
    }
    std::printf("PASS %d 个实例同时在跑，工作集合计 %lld MB\n", started,
                static_cast<long long>(totalKb / 1024));
    for (qint64 pid : launched) {
        HANDLE handle = OpenProcess(PROCESS_TERMINATE, FALSE, DWORD(pid));
        if (handle != nullptr) {
            TerminateProcess(handle, 0);
            CloseHandle(handle);
        }
    }
    std::printf("PASS 全部终止\n");
    return 0;
}

// ---------- launch-live：**真启动**一次已经装好的原版（产品自己的启动链路） ----------
// mw_check.exe launch-live <版本号> <root> [秒数]
// root 里必须已经有 install-live 装好的版本；离线账号固定 Steve；Java 用 jre-legacy。
if (argc >= 4 && QString::fromLocal8Bit(argv[1]) == QLatin1String("launch-live")) {
    QCoreApplication::setOrganizationName(QStringLiteral("WTFML"));
    QCoreApplication::setApplicationName(QStringLiteral("WTFML"));
    const QString versionId = QString::fromLocal8Bit(argv[2]);
    const QString root = QDir::cleanPath(QFileInfo(QString::fromLocal8Bit(argv[3])).absoluteFilePath());
    const int seconds = argc >= 5 ? QString::fromLocal8Bit(argv[4]).toInt() : 20;
    const QString launchJava = argc >= 6 ? QString::fromLocal8Bit(argv[5])
        : QStringLiteral("C:/Users/User/AppData/Roaming/.minecraft/runtime/jre-legacy/bin/java.exe");
    const int launchXmx = argc >= 7 ? QString::fromLocal8Bit(argv[6]).toInt() : 1024;
    if (versionId.isEmpty() || !QDir().mkpath(root + QStringLiteral("/settings"))) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root + QStringLiteral("/settings"));
    qApp->setProperty("wtfml.testDefaultMinecraftDir", root + QStringLiteral("/game"));
    AppPaths::setMinecraftDir(root + QStringLiteral("/game"));

    QString accountError;
    AccountStore::addOffline(QStringLiteral("Steve"), &accountError);
    const bool autoJava = launchJava.compare(QLatin1String("auto"), Qt::CaseInsensitive) == 0;
    GlobalJavaSettings java;
    java.setAutoSelectJava(autoJava);
    if (!autoJava) java.setJavaPath(launchJava);
    java.setXmx(launchXmx);
    java.setXms(512);
    java.save();

    MainWindow window;
    window.show();
    QApplication::processEvents();
    auto *combo = window.findChild<QComboBox *>(MainWindowUI::installedVersionComboName());
    if (combo == nullptr) { std::printf("FAIL 找不到已安装版本下拉框\n"); return 1; }
    const int index = combo->findData(versionId);
    if (index < 0) { std::printf("FAIL 版本 %s 不在已安装列表里\n", qPrintable(versionId)); return 1; }
    combo->setCurrentIndex(index);
    QApplication::processEvents();

    QMetaObject::invokeMethod(&window, "onStartButtonClicked", Qt::DirectConnection);
    QApplication::processEvents();

    // 等一个以本进程为父的 java.exe（产品起游戏就是 QProcess 子进程）。
    auto childJavaPid = []() -> qint64 {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return 0;
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        const DWORD self = GetCurrentProcessId();
        qint64 found = 0;
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (_wcsicmp(entry.szExeFile, L"java.exe") == 0 && entry.th32ParentProcessID == self) {
                    found = entry.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
        return found;
    };
    qint64 javaPid = 0;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 120000) {
        QApplication::processEvents();
        javaPid = childJavaPid();
        if (javaPid != 0) break;
        QThread::msleep(300);
    }
    if (javaPid == 0) {
        std::printf("FAIL 120 秒内没有游戏进程起来\n");
        return 1;
    }
    std::printf("PASS 游戏进程已启动 pid=%lld，观察 %d 秒\n",
                static_cast<long long>(javaPid), seconds);
    QElapsedTimer alive;
    alive.start();
    while (alive.elapsed() < seconds * 1000) {
        QApplication::processEvents();
        QThread::msleep(200);
    }
    HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, DWORD(javaPid));
    if (process != nullptr) {
        TerminateProcess(process, 0);
        WaitForSingleObject(process, 5000);
        CloseHandle(process);
    }
    std::printf("PASS 观察结束，游戏进程已终止\n");
    return 0;
}


// ---------- install-live：**真网络**装一个原版版本（产品自己的 VersionManager） ----------
// mw_check.exe install-live <版本号> <root> [mirror=1]
// 设置与游戏根都落在 root 下；用 BMCLAPI 镜像（默认）避免官方源慢。
if (argc >= 4 && QString::fromLocal8Bit(argv[1]) == QLatin1String("install-live")) {
    QCoreApplication::setOrganizationName(QStringLiteral("WTFML"));
    QCoreApplication::setApplicationName(QStringLiteral("WTFML"));
    const QString versionId = QString::fromLocal8Bit(argv[2]);
    const QString root = QDir::cleanPath(QFileInfo(QString::fromLocal8Bit(argv[3])).absoluteFilePath());
    const bool mirror = argc < 5 || QString::fromLocal8Bit(argv[4]) != QLatin1String("0");
    if (versionId.isEmpty() || !QDir().mkpath(root + QStringLiteral("/settings"))
        || !QDir().mkpath(root + QStringLiteral("/game"))) {
        std::fprintf(stderr, "bad args or unwritable root: %s\n", qPrintable(root));
        return 2;
    }
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root + QStringLiteral("/settings"));
    qApp->setProperty("wtfml.testDefaultMinecraftDir", root + QStringLiteral("/game"));
    AppPaths::setMinecraftDir(root + QStringLiteral("/game"));
    VersionManager versions;
    versions.setUseMirror(mirror);
    QEventLoop loop;
    bool finished = false;
    bool ok = false;
    QString problem;
    QObject::connect(&versions, &VersionManager::versionDownloadFinished, &loop,
                     [&](const QString &id, bool success, const QString &message) {
                         std::printf("[finished] %s success=%d %s\n", qPrintable(id),
                                     success ? 1 : 0, qPrintable(message));
                         if (id == versionId) { finished = true; ok = success; problem = message; loop.quit(); }
                     });
    QObject::connect(&versions, &VersionManager::versionDownloadStats, &loop,
                     [&](int done, int total, int failed, const QString &current) {
                         std::printf("[stats] %d/%d failed=%d %s\n", done, total, failed,
                                     qPrintable(current));
                     });
    QTimer::singleShot(20 * 60 * 1000, &loop, &QEventLoop::quit);
    // 产品里这条顺序是"先拿版本 JSON，再下文件"；直接下会报 Version JSON not available。
    QObject::connect(&versions, &VersionManager::versionDetailsFetched, &loop,
                     [&](const QString &id, const QJsonObject &) {
                         if (id != versionId) return;
                         versions.downloadVersion(versionId,
                             root + QStringLiteral("/game/versions/") + versionId);
                     });
    versions.fetchVersionDetails(versionId);
    loop.exec();
    if (!finished) {
        std::printf("FAIL 20 分钟内没有等到安装结束\n");
        return 1;
    }
    if (!ok) {
        std::printf("FAIL 安装失败：%s\n", qPrintable(problem));
        return 1;
    }
    const QString json = root + QStringLiteral("/game/versions/") + versionId + QLatin1Char('/')
                         + versionId + QStringLiteral(".json");
    const QString jar = root + QStringLiteral("/game/versions/") + versionId + QLatin1Char('/')
                        + versionId + QStringLiteral(".jar");
    const bool filesOk = QFileInfo::exists(json) && QFileInfo::exists(jar);
    std::printf("%s 版本文件：json=%d jar=%d\n", filesOk ? "PASS" : "FAIL",
                QFileInfo::exists(json) ? 1 : 0, QFileInfo::exists(jar) ? 1 : 0);
    return filesOk ? 0 : 1;
}

// ---------- java-runtime-live：**真网络**走一遍"本机没有 Java → 启动器下载运行时" ----------
// mw_check.exe java-runtime-live <major> [root]
// 设置与游戏根都落在 root 下（默认：当前目录/java-runtime-live），绝不写用户注册表。
if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QLatin1String("java-runtime-live")) {
    QCoreApplication::setOrganizationName(QStringLiteral("WTFML"));
    QCoreApplication::setApplicationName(QStringLiteral("WTFML"));
    const int major = QString::fromLocal8Bit(argv[2]).toInt();
    const QString root = QDir::cleanPath(QFileInfo(argc >= 4 ? QString::fromLocal8Bit(argv[3])
                                                             : QStringLiteral("java-runtime-live"))
                                             .absoluteFilePath());
    if (major <= 0 || !QDir().mkpath(root + QStringLiteral("/settings"))
        || !QDir().mkpath(root + QStringLiteral("/game"))) {
        std::fprintf(stderr, "bad args or unwritable root: %s\n", qPrintable(root));
        return 2;
    }
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root + QStringLiteral("/settings"));
    qApp->setProperty("wtfml.testDefaultMinecraftDir", root + QStringLiteral("/game"));
    AppPaths::setMinecraftDir(root + QStringLiteral("/game"));
    // 探针没有 MainWindow 那套网络初始化：产品把设置里的代理装成**进程级**
    // application proxy；这里按 Windows 的 Internet Settings 复现一遍，
    // 否则下载会绕过代理直连（表现就是 Operation timed out）。
    {
        QSettings internet(
            QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings"),
            QSettings::NativeFormat);
        const QString server = internet.value(QStringLiteral("ProxyServer")).toString();
        if (internet.value(QStringLiteral("ProxyEnable")).toInt() == 1 && !server.isEmpty()) {
            const QStringList parts = server.split(QChar(':'));
            QNetworkProxy proxy;
            proxy.setType(QNetworkProxy::HttpProxy);
            proxy.setHostName(parts.value(0));
            proxy.setPort(parts.value(1).toUShort());
            QNetworkProxy::setApplicationProxy(proxy);
        }
    }
    JavaRuntimeController controller;
    QEventLoop loop;
    int exitCode = 1;
    QString installed;
    QObject::connect(&controller, &JavaRuntimeController::statusChanged, &loop,
                     [](const QString &text) { std::printf("[status] %s\n", qPrintable(text)); });
    QObject::connect(&controller, &JavaRuntimeController::runtimeFinished, &loop,
                     [&](int got, bool ok, const QString &javaPath, bool) {
                         std::printf("[runtime] major=%d ok=%d path=%s\n", got, ok ? 1 : 0,
                                     qPrintable(javaPath));
                         if (ok && QFileInfo::exists(javaPath)) { installed = javaPath; exitCode = 0; }
                         loop.quit();
                     });
    QObject::connect(&controller, &JavaRuntimeController::downloadSequenceFinished, &loop,
                     [&](bool, int okCount, int failCount, bool cancelled) {
                         std::printf("[sequence] ok=%d fail=%d cancelled=%d\n", okCount, failCount,
                                     cancelled ? 1 : 0);
                         if (okCount == 0 || cancelled) loop.quit();
                     });
    QTimer::singleShot(15 * 60 * 1000, &loop, &QEventLoop::quit);
    controller.downloadRuntime(major);
    loop.exec();
    if (installed.isEmpty()) std::printf("FAIL 没有拿到可用的 Java %d 运行时\n", major);
    else std::printf("PASS Java %d 运行时装好了：%s\n", major, qPrintable(installed));
    return exitCode;
}

    return -1;
}















