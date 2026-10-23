#include "Support.hpp"

void runModpackCompletionTests(const QString &sandbox) {
    section("ZG8. 缺项导入、手动补齐与继续安装（真实状态机，本地文件源）");
    const QString previousRoot = AppPaths::minecraftDir();
    const QString root = sandbox + QStringLiteral("/completion-engine");
    QDir().mkpath(root);
    const QByteArray clientBytes("client-fixture"), modBytes("manual-mod");
    const auto hash = [](const QByteArray &bytes) {
        return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
    };
    writeFileAt(root + "/client.bin", clientBytes);
    const QJsonObject client{{"url", QUrl::fromLocalFile(root + "/client.bin").toString()},
        {"sha1", hash(clientBytes)}, {"size", clientBytes.size()}};
    const QJsonObject base{{"id", "1.99.76"}, {"type", "release"},
        {"mainClass", "net.minecraft.client.main.Main"}, {"libraries", QJsonArray{}},
        {"downloads", QJsonObject{{"client", client}}}};
    writeFileAt(root + "/base.json", QJsonDocument(base).toJson());
    writeFileAt(root + "/index.json", QJsonDocument(QJsonObject{{"versions", QJsonArray{
        QJsonObject{{"id", "1.99.76"}, {"type", "release"},
        {"url", QUrl::fromLocalFile(root + "/base.json").toString()}}}}}).toJson());
    writeFileAt(root + "/config.txt", "keep-config");
    QString error;
    const QString archive = root + "/pack.zip";
    check(ZipUtil::createZip(archive, {{"overrides/config/test.txt", root + "/config.txt"}}, &error),
          "ZG8 准备覆盖内容测试包", error);
    ModpackImport::Pack pack;
    pack.format = ModpackImport::Format::CurseForge;
    pack.gameVersion = QStringLiteral("1.99.76"); pack.name = QStringLiteral("需要补齐的包");
    pack.overridesDir = QStringLiteral("overrides"); pack.declaredFileCount = 1;
    pack.filesNotDistributable = 1;
    pack.fileIssues.append({{123,456}, QStringLiteral("manual.jar"), QStringLiteral("mods/manual.jar"),
        hash(modBytes), modBytes.size(), QStringLiteral("平台标记不可用")});
    AppPaths::setMinecraftDir(root);
    const QString target = root + QStringLiteral("/versions/completion-test");
    const QString fingerprint(64, QLatin1Char('a'));
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt > 0) {
            writeFileAt(target + "/mods/manual.jar", attempt == 1 ? QByteArray(modBytes.size(),'x') : modBytes);
        }
        VersionManager manager;
        manager.setUseMirror(false);
        manager.setVersionManifestUrlOverride(QUrl::fromLocalFile(root + "/index.json").toString());
        QEventLoop loop;
        bool done = false, success = false; QString message;
        QObject::connect(&manager, &VersionManager::versionDownloadFinished, &loop,
            [&](const QString &id, bool ok, const QString &text) {
                if (id != QLatin1String("completion-test")) return;
                done = true; success = ok; message = text; loop.quit();
            });
        QTimer::singleShot(15000, &loop, &QEventLoop::quit);
        QTimer::singleShot(0, &manager, [&] {
            manager.importModpack(archive, QStringLiteral("completion-test"), pack, {}, fingerprint);
        });
        loop.exec();
        check(done && success == (attempt == 2), "ZG8 只有补齐且哈希一致时才发成功终态", message);
        QFile report(target + QStringLiteral("/wtfml.import-report.json"));
        check(report.open(QIODevice::ReadOnly), "ZG8 每次收尾都有持久报告");
        const auto json = QJsonDocument::fromJson(report.readAll()).object(); report.close();
        check(json.value(QStringLiteral("complete")).toBool() == (attempt == 2),
              "ZG8 报告完成状态与实际结果一致");
        check(QFileInfo::exists(target + "/config/test.txt"), "ZG8 缺项不删除已解压内容");
        if (attempt < 2) {
            check(QFileInfo::exists(InstanceFiles::importMarkerPath("completion-test")) &&
                      !QFileInfo::exists(AppPaths::versionMetaPath("completion-test")) &&
                      InstanceFiles::isUnfinishedImport("completion-test"),
                  "ZG8 未完成标记保留、不写成功 meta，读侧仍识别为半成品");
            check(InstanceFiles::checkImportTargetName("completion-test", &error, fingerprint) ==
                      InstanceFiles::ImportNameVerdict::OkResume,
                  "ZG8 同一包与同一实例名可继续导入", error);
        } else {
            check(!QFileInfo::exists(InstanceFiles::importMarkerPath("completion-test")) &&
                      QFileInfo::exists(AppPaths::versionMetaPath("completion-test")),
                  "ZG8 真正补齐后才写成功 meta 并清除未完成标记");
        }
    }
    AppPaths::setMinecraftDir(previousRoot);
}
