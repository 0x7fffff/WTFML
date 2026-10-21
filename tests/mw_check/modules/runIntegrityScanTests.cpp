#include "Support.hpp"
void runIntegrityScanTests() {
    const QString originalDir = AppPaths::minecraftDir();
    QTemporaryDir root;
    check(root.isValid(), "T33.0 临时数据根目录建出来了");
    if (!root.isValid()) {
        return;
    }

    // 数据根目录**两头都要改**（理由见 T19）：MainWindow 构造里会拿盘上那个「当前游戏
    // 文件夹」再把 AppPaths 改一次。顺手把「下载源」钉成官方（这一节的远端全是 file://）。
    QSettings sandbox;
    const QVariant foldersBefore = sandbox.value(QStringLiteral("Launcher/Folders"));
    const QVariant currentBefore = sandbox.value(QStringLiteral("Launcher/CurrentFolder"));
    const QVariant sourceBefore = sandbox.value(QStringLiteral("Download/Source"));
    sandbox.setValue(QStringLiteral("Launcher/Folders"), QStringList({root.path()}));
    sandbox.setValue(QStringLiteral("Launcher/CurrentFolder"), root.path());
    sandbox.setValue(QStringLiteral("Download/Source"),
                     LauncherSettings::downloadSourceKey(DownloadSource::Official));
    sandbox.sync();
    AppPaths::setMinecraftDir(root.path());
    QDir().mkpath(root.path() + QStringLiteral("/libraries"));
    QDir().mkpath(root.path() + QStringLiteral("/assets"));
    const QDir versionsDir(root.path() + QStringLiteral("/versions"));

    // ---------- 夹具：远端 + 一个「jar 坏、库缺」的实例 ----------
    const QDir remoteDir(root.filePath(QStringLiteral("remote")));
    remoteDir.mkpath(QStringLiteral("."));
    const auto makeRemote = [&remoteDir](const QString &relPath,
                                         const QByteArray &content) -> QJsonObject {
        const QString path = remoteDir.filePath(relPath);
        QDir().mkpath(QFileInfo(path).absolutePath());
        writeFileAt(path, content);
        return QJsonObject{
            {QStringLiteral("url"), QUrl::fromLocalFile(path).toString()},
            {QStringLiteral("sha1"),
             QString::fromLatin1(
                 QCryptographicHash::hash(content, QCryptographicHash::Sha1).toHex())},
            {QStringLiteral("size"), static_cast<qint64>(content.size())}};
    };
    const QByteArray realJarBytes = QByteArrayLiteral("T33-client-jar-body-0123456789");
    const QJsonObject jarRemote = makeRemote(QStringLiteral("maven/t33/client.jar"), realJarBytes);
    const QJsonObject libRemote =
        makeRemote(QStringLiteral("maven/t33/t33lib-1.0.jar"), QByteArrayLiteral("T33-lib-body"));

    const QString idScan = QStringLiteral("1.199.1");
    const QString nameScan = QStringLiteral("T33体检目标");
    const QString idB = QStringLiteral("1.199.2");

    const auto versionJsonFor = [&](const QString &id) {
        return QJsonObject{
            {QStringLiteral("id"), id},
            {QStringLiteral("type"), QStringLiteral("release")},
            {QStringLiteral("mainClass"), QStringLiteral("net.minecraft.client.main.Main")},
            {QStringLiteral("libraries"),
             QJsonArray{QJsonObject{
                 {QStringLiteral("name"), QStringLiteral("synth:t33lib:1.0")},
                 {QStringLiteral("downloads"),
                  QJsonObject{{QStringLiteral("artifact"),
                               QJsonObject{
                                   {QStringLiteral("url"), libRemote.value(QStringLiteral("url"))},
                                   {QStringLiteral("sha1"),
                                    libRemote.value(QStringLiteral("sha1"))},
                                   {QStringLiteral("size"),
                                    libRemote.value(QStringLiteral("size"))},
                                   {QStringLiteral("path"),
                                    QStringLiteral("synth/t33lib/1.0/t33lib-1.0.jar")}}}}}}}},
            {QStringLiteral("downloads"),
             QJsonObject{{QStringLiteral("client"), jarRemote}}}};
    };
    const auto installFake = [&](const QString &id, const QString &customName) {
        const QDir dir(versionsDir.filePath(id));
        dir.mkpath(QStringLiteral("."));
        writeFileAt(dir.filePath(id + QStringLiteral(".json")),
                    QJsonDocument(versionJsonFor(id)).toJson(QJsonDocument::Compact));
        // 坏的 jar：**字节数和远端一模一样、内容全是 'x'** —— 判据只能是 sha1（那一档叫「坏」）。
        writeFileAt(dir.filePath(id + QStringLiteral(".jar")),
                    QByteArray(static_cast<int>(jarRemote.value(QStringLiteral("size")).toInt()),
                               'x'));
        writeFileAt(dir.filePath(QStringLiteral("wtfml.meta")),
                    QJsonDocument(QJsonObject{{QStringLiteral("name"), customName}})
                        .toJson(QJsonDocument::Compact));
    };
    installFake(idScan, nameScan);
    installFake(idB, QStringLiteral("T33第二个"));
    // 库**先不摆**（⇒ 缺）。
    const QString libPath =
        root.path() + QStringLiteral("/libraries/synth/t33lib/1.0/t33lib-1.0.jar");
    check(!QFile::exists(libPath), "T33.0 那个库先不在盘上（「缺」的那一类）");

    // 身份：离线账号（体检一个请求都不发；顺带让构造末尾那次皮肤加载走本地）。
    TokenStore::clear(TokenStore::microsoftKey());
    TokenStore::clear(TokenStore::minecraftKey());
    TokenStore::clear(TokenStore::accountsKey());
    TokenStore::clear(TokenStore::currentUuidKey());
    const QString accountName = QStringLiteral("T33User");
    seedOfflineRandomSkin(accountName, kSeededSkinUrl, QColor(0x60, 0xA0, 0x40));
    QString offlineError;
    check(AccountStore::addOffline(accountName, &offlineError),
          "T33.0 建一个离线账号并设成当前身份", offlineError);
    const QString lastVersionBefore = LauncherSettings().lastLaunchedVersion();
    rememberLastVersion(idScan);

    bool ready = false;
    {
        MainWindow w;
        w.show();
        QApplication::processEvents();
        VersionManager *vm = w.findChild<VersionManager *>();
        QComboBox *combo = comboByObjectName(&w, MainWindowUI::installedVersionComboName());
        ready = vm != nullptr && combo != nullptr && selectedId(combo) == idScan;
        check(ready, "T33.0 主窗口起来了：版本管理器 / 已安装版本下拉框都在，选中的就是那个实例",
              QStringLiteral("vm=%1 combo=%2").arg(vm != nullptr).arg(combo != nullptr));
        if (ready) {
            // ---------- ① 清单：integrityPlan() 就是体检用的那一份 ----------
            {
                QString planError;
                const QList<VersionIntegrity::ExpectedFile> plan =
                    vm->integrityPlan(idScan, &planError);
                check(planError.isEmpty() && plan.size() == 2,
                      "T33.1 integrityPlan() 列出两个该在的文件（客户端 jar + 那一个库）",
                      planError.isEmpty() ? QString::number(plan.size()) : planError);
                check(plan.size() == 2 && plan.at(0).group == VersionIntegrity::Group::ClientJar
                          && plan.at(1).group == VersionIntegrity::Group::Libraries,
                      "★ T33.1 两条各自带着自己的**组**（界面按组摊结果就靠它，"
                      "不认 label 字符串）");
                check(plan.size() == 2 && QFileInfo(plan.at(1).path) == QFileInfo(libPath),
                      "T33.1 库那一条的路径是产品自己算的那一份（别处不另拼一遍）");

                // 同一份清单喂给判据层，结论必须和「一键体检」那条路一模一样（同一个出处）。
                const VersionIntegrity::Report report = vm->checkInstanceIntegrity(idScan);
                check(report.error.isEmpty() && report.checked == plan.size(),
                      "★ T33.1 checkInstanceIntegrity() 和 integrityPlan() 说的是同一份清单",
                      report.summary());

                // 读不出 version.json 那一档：只有 error 一句、清单是空的。
                QString missingError;
                const QList<VersionIntegrity::ExpectedFile> none =
                    vm->integrityPlan(QStringLiteral("1.199.9-nosuch"), &missingError);
                check(!missingError.isEmpty() && none.isEmpty(),
                      "★ T33.1 读不出 version.json ⇒ error 非空、清单空（和「跑了、"
                      "发现一堆问题」分开）",
                      missingError);
            }

            // 显示名（产品是拿"自定义名 + 目录名"拼的）：下面"那句话说的是谁"全靠它分辨。
            const int scanIndex = combo->findData(idScan);
            const QString displayScan = scanIndex >= 0 ? combo->itemText(scanIndex) : idScan;
            check(!displayScan.isEmpty() && displayScan != idScan,
                  "T33.1 那个实例在下拉框里显示的是用户认得的名字", displayScan);

            PageCloser closer;
            closer.start();

            // ---------- ② 闸口一：正在体检的实例**不许改名** ----------
            {
                QMetaObject::invokeMethod(&w, "submitIntegrityCheck", Qt::DirectConnection,
                                          Q_ARG(QString, idScan));
                bool scanning = false;
                QMetaObject::invokeMethod(&w, "integrityScanningVersion", Qt::DirectConnection,
                                          Q_RETURN_ARG(bool, scanning), Q_ARG(QString, idScan));
                check(scanning,
                      "★ T33.2 点下去那一刻「正在体检这个实例」就是真的（不 pump 事件 ⇒ 确定）");

                dismissNextPrompt();
                bool renamed = true;
                const bool invoked =
                    QMetaObject::invokeMethod(&w, "applyRename", Qt::DirectConnection,
                                              Q_RETURN_ARG(bool, renamed), Q_ARG(QString, idScan),
                                              Q_ARG(QString, QStringLiteral("1.199.1-改名")));
                check(invoked && !renamed,
                      "★ T33.2 正在体检的实例**不许改名**（§4.1 同一实例不允许同时扫描和改名）");
                check(QDir(versionsDir.filePath(idScan)).exists(),
                      "T33.2 被拒绝时磁盘一个字节都没动（老目录还在）");

                const bool closed =
                    pumpUntil([&closer] { return !closer.seen.isEmpty(); }, 10000);
                pumpUntil([] { return currentOverlay() == nullptr; }, 8000);
                check(closed && !closer.seen.isEmpty()
                          && closer.seen.at(0).title == QStringLiteral("完整性检查"),
                      "★ T33.2 扫完自己开出报告页（页眉「完整性检查」）—— 用户不用去哪儿找它",
                      closer.seen.isEmpty() ? QStringLiteral("没开") : closer.seen.at(0).title);
                check(!closer.seen.isEmpty()
                          && closer.seen.at(0).summary
                                 == QStringLiteral("「%1」2 个文件里 2 个要修：缺 1 个、坏 1 个")
                                        .arg(displayScan),
                      "★ T33.2 那一页顶上那句总述说的是**这个实例**、而且缺 / 坏分得开",
                      closer.seen.isEmpty() ? QString() : closer.seen.at(0).summary);
            }

            // ---------- ③ 闸口二：正在体检的实例**不许删**（deleteInstance）----------
            {
                const int mark = closer.seen.size();
                QMetaObject::invokeMethod(&w, "submitIntegrityCheck", Qt::DirectConnection,
                                          Q_ARG(QString, idScan));
                dismissNextPrompt();
                bool deleted = true;
                QMetaObject::invokeMethod(&w, "deleteInstance", Qt::DirectConnection,
                                          Q_RETURN_ARG(bool, deleted), Q_ARG(QString, idScan));
                check(!deleted, "★ T33.3 正在体检的实例**不许删**（deleteInstance 那一半）");
                check(QDir(versionsDir.filePath(idScan)).exists(),
                      "T33.3 被拒绝时那个目录还在（连着 jar / json 一起）");
                pumpUntil([&closer, mark] { return closer.seen.size() > mark; }, 10000);
                pumpUntil([] { return currentOverlay() == nullptr; }, 8000);
            }

            // ---------- ④ 闸口三：confirmDeleteInstance 那一半也一样 ----------
            {
                const int mark = closer.seen.size();
                QMetaObject::invokeMethod(&w, "submitIntegrityCheck", Qt::DirectConnection,
                                          Q_ARG(QString, idB));
                dismissNextPrompt();
                bool deleted = true;
                QMetaObject::invokeMethod(&w, "confirmDeleteInstance", Qt::DirectConnection,
                                          Q_RETURN_ARG(bool, deleted), Q_ARG(QString, idB));
                check(!deleted, "★ T33.4 confirmDeleteInstance() 那一半也挡住了（没走到问那句）");
                check(QDir(versionsDir.filePath(idB)).exists(), "T33.4 B 那个目录还在");
                pumpUntil([&closer, mark] { return closer.seen.size() > mark; }, 10000);
                pumpUntil([] { return currentOverlay() == nullptr; }, 8000);
            }

            // ---------- ⑤ 反证：体检结束之后，同一个改名就放行了 ----------
            // 这一条是 T33.2 "挡住它的是正在体检"的**反面证据**：同一发调用、同一个实例，
            // 这会儿（体检早完了）必须成功。
            {
                bool renamed = false;
                QMetaObject::invokeMethod(&w, "applyRename", Qt::DirectConnection,
                                          Q_RETURN_ARG(bool, renamed), Q_ARG(QString, idScan),
                                          Q_ARG(QString, QStringLiteral("1.199.1-改名")));
                check(renamed,
                      "★ T33.5 同一发现在放行了 —— 说明 T33.2 挡住它的是「正在体检」、不是别人",
                      renamed ? QString() : QStringLiteral("还是被拒"));
                check(QDir(versionsDir.filePath(QStringLiteral("1.199.1-改名"))).exists(),
                      "T33.5 这一下真动了盘（新目录在）");
                // 改回去（沙盒还原 / 后面收尾都指望原样）。
                bool back = false;
                QMetaObject::invokeMethod(&w, "applyRename", Qt::DirectConnection,
                                          Q_RETURN_ARG(bool, back),
                                          Q_ARG(QString, QStringLiteral("1.199.1-改名")),
                                          Q_ARG(QString, idScan));
                check(back, "T33.5 再改回原名");
            }
            closer.stop();
        }
    }

    TokenStore::clear(TokenStore::microsoftKey());
    TokenStore::clear(TokenStore::minecraftKey());
    TokenStore::clear(TokenStore::accountsKey());
    TokenStore::clear(TokenStore::currentUuidKey());
    check(AccountStore::accounts().isEmpty() && AccountStore::activeUuid().isEmpty(),
          "T33.6 这一节加的账号清干净了");

    LauncherSettings().setLastLaunchedVersion(lastVersionBefore);
    sandbox.setValue(QStringLiteral("Launcher/Folders"), foldersBefore);
    sandbox.setValue(QStringLiteral("Launcher/CurrentFolder"), currentBefore);
    sandbox.setValue(QStringLiteral("Download/Source"), sourceBefore);
    sandbox.sync();
    AppPaths::setMinecraftDir(originalDir);
    check(AppPaths::minecraftDir() == originalDir, "T33.6 数据根目录换回来了");
}

