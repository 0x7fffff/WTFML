#include "Support.hpp"
#include "ListRowDelegate.hpp"
#include "ActivityPopover.hpp"
int MwIntegrityActionsChecks(const QDir &root) {
    // 全程一个外网请求都不发：原版那一档的地址取自**本地**那份版本 json，而那份 json 里
    // 写的是指向 127.0.0.1 上一台假服务器的地址（下载器把 URL 原样用，见
    // `DownloadManager::effectiveDownloadUrl` —— 只有「镜像降级且知道官方那条」才会改道）。
    //
    // 账本落在**启动器自己**那个固定位置（`TaskHistory::defaultHistoryPath()` 用的是
    // `defaultMinecraftDir()`，故意不跟当前游戏文件夹走），所以这一节只按**任务号**认
    // 自己那几条 —— 同一个 exe 里别的节也往这本账里写，位置和条数都靠不住。
    {
        const QString originalDir = AppPaths::minecraftDir();
        QTemporaryDir root;
        check(root.isValid(), "T40 前提：拿得到一个临时的数据根目录");
        // 数据根目录**两头都要改**（理由见 T19/T39）：MainWindow 构造里会拿盘上那个「当前
        // 游戏文件夹」再把 AppPaths 改一次。收尾**逐字还原**那两个键。
        QSettings sandbox;
        const QVariant foldersBefore = sandbox.value(QStringLiteral("Launcher/Folders"));
        const QVariant currentBefore = sandbox.value(QStringLiteral("Launcher/CurrentFolder"));
        sandbox.setValue(QStringLiteral("Launcher/Folders"), QStringList({root.path()}));
        sandbox.setValue(QStringLiteral("Launcher/CurrentFolder"), root.path());
        sandbox.sync();
        AppPaths::setMinecraftDir(root.path());

        // ---------- 一台「官方下载服务器」的替身 ----------
        // 只答一种话（200 + 正文）并记下被打过几次。`holdOpen` 那一档收下请求但**一个字都
        // 不答** —— 「取消」那条路要的就是一个「下不完」的下载（它要是下完了，取消就成了
        // 一场赛跑：赢的总是本地回环）。
        struct FakeFileServer {
            QTcpServer server;
            QByteArray body;
            bool holdOpen = false;
            int hits = 0;

            bool start() {
                QObject::connect(&server, &QTcpServer::newConnection, &server, [this]() {
                    while (QTcpSocket *socket = server.nextPendingConnection()) {
                        socket->setParent(&server);
                        QObject::connect(socket, &QTcpSocket::readyRead, socket,
                                         [this, socket]() {
                                             if (socket->property("seen").toBool()) {
                                                 return;  // 一个连接上只答一发
                                             }
                                             socket->setProperty("seen", true);
                                             socket->readAll();
                                             ++hits;
                                             if (holdOpen) {
                                                 return;  // 收下、不答：等对面自己掐
                                             }
                                             QByteArray response =
                                                 QByteArrayLiteral("HTTP/1.1 200 OK\r\n");
                                             response += QByteArrayLiteral(
                                                 "Content-Type: application/java-archive\r\n");
                                             response += QByteArrayLiteral("Content-Length: ") +
                                                         QByteArray::number(body.size()) +
                                                         QByteArrayLiteral("\r\n\r\n");
                                             response += body;
                                             socket->write(response);
                                             socket->flush();
                                         });
                    }
                });
                return server.listen(QHostAddress::LocalHost, 0);
            }

            /// 这一台上面某条路径的地址。
            QString urlFor(const QString &path) const {
                return QStringLiteral("http://127.0.0.1:%1%2").arg(server.serverPort()).arg(path);
            }
        };
        FakeFileServer files;
        FakeFileServer hanging;
        hanging.holdOpen = true;
        check(files.start() && hanging.start(),
              "T40 前提：两台假下载服务器都听上了（一台答话、一台收下不答）");

        // ---------- 本地那份版本 json ----------
        // 原版那一档的地址就在 `downloads.server` 里（`ServerInstallRun::needFor()` 说这一档
        // 要 `VersionJson`），而驱动层**先看本地**：盘上有这个版本就一个请求都不发。
        // 前四个字节必须是 zip 的本地文件头（`PK\x03\x04`）：`DownloadManager` 收尾那一段
        // 有一条"jar 就得是 zip"的判据（`shouldBeZipArchive` + `looksLikeZipArchive`，
        // 见 onDownloadReplyFinished 里那一支），**字节数对得上也一样会走它** —— 一段
        // `FAKE-SERVER-…` 开头的正文会被当场**从盘上删掉**，于是这一节看到的是"文件没了"
        // （报出来的就是 `Not a zip archive`）。
        // 我们造假的是"服务端的那份 jar"，所以它得像一份 jar。
        const QByteArray jarBody = QByteArrayLiteral("PK\x03\x04") +
                                   QByteArrayLiteral("FAKE-SERVER-JAR-") +
                                   QByteArray(2048, 'j');
        files.body = jarBody;
        const auto writeVersionJson = [](const QString &versionId, const QString &url,
                                         int size) {
            QJsonObject server;
            server.insert(QStringLiteral("url"), url);
            server.insert(QStringLiteral("size"), double(size));
            QJsonObject downloads;
            downloads.insert(QStringLiteral("server"), server);
            QJsonObject versionJson;
            versionJson.insert(QStringLiteral("id"), versionId);
            versionJson.insert(QStringLiteral("downloads"), downloads);
            writeFileAt(InstanceFiles::jsonPath(versionId),
                        QJsonDocument(versionJson).toJson(QJsonDocument::Compact));
        };
        writeVersionJson(QStringLiteral("1.20.1"), files.urlFor(QStringLiteral("/s.jar")),
                         jarBody.size());
        writeVersionJson(QStringLiteral("1.20.2"), hanging.urlFor(QStringLiteral("/s.jar")), 4096);
        check(QFile::exists(InstanceFiles::jsonPath(QStringLiteral("1.20.1"))),
              "T40 前提：1.20.1 的版本 json 摆上了（`downloads.server` 指向那台假服务器）");

        // ---------- 账本那两个小工具（按**号**认自己那几条） ----------
        const QString ledgerPath =
            AppPaths::defaultMinecraftDir() + QStringLiteral("/wtfml.tasks");
        const auto installIds = [&ledgerPath]() {
            QSet<quint64> ids;
            for (const Tasks::TaskSnapshot &task : Tasks::load(ledgerPath)) {
                if (task.kind == Tasks::TaskKind::Install) {
                    ids.insert(task.id);
                }
            }
            return ids;
        };
        const auto taskWithId = [&ledgerPath](quint64 id) {
            Tasks::TaskSnapshot found;
            for (const Tasks::TaskSnapshot &task : Tasks::load(ledgerPath)) {
                if (task.id == id) {
                    found = task;
                    break;
                }
            }
            return found;
        };
        const auto newInstallSince = [&ledgerPath](const QSet<quint64> &known) {
            Tasks::TaskSnapshot found;
            for (const Tasks::TaskSnapshot &task : Tasks::load(ledgerPath)) {
                if (task.kind == Tasks::TaskKind::Install && !known.contains(task.id)) {
                    found = task;
                    break;
                }
            }
            return found;
        };

        MainWindow w;
        w.show();
        QApplication::processEvents();

        auto *newServerBtn = byObjectName<QPushButton>(&w, MainWindowUI::newServerButtonName());
        auto *serverTree = byObjectName<QTreeWidget>(&w, MainWindowUI::serverListName());
        check(newServerBtn != nullptr && serverTree != nullptr,
              "T40.0 前提：「新建服务端…」那颗按钮 + 服务端那棵树都找得到");
        // 管理入口先到版本列表；安装专项接着选择夹具对应的版本。
        const auto openServerForm = [&w, newServerBtn] {
            newServerBtn->click();
            if (auto *download = byObjectName<QPushButton>(&w, MainWindowUI::downloadNewServerButtonName())) {
                download->setProperty("selectedVersion", QStringLiteral("1.20.1"));
                download->click();
            }
        };
        // 按 serverId 找列表里那一行（这一节前前后后会摆下不止一个）。
        const auto rowForId = [serverTree](const QString &id) -> QTreeWidgetItem * {
            if (serverTree == nullptr) {
                return nullptr;
            }
            for (int i = 0; i < serverTree->topLevelItemCount(); ++i) {
                if (serverTree->topLevelItem(i)->data(0, Qt::UserRole).toString() == id) {
                    return serverTree->topLevelItem(i);
                }
            }
            return nullptr;
        };

        // ---------- ① 点「创建」：账本上多一项，而且当趟就下 ----------
        const QString installedName = QStringLiteral("真装服务端");
        const QString installedId =
            ServerRegistry::uniqueServerId(ServerRegistry::sanitizeId(installedName));
        const QSet<quint64> before = installIds();
        {
            // 表单是嵌在「下载 → 服务端」那一栏第二层里的（不是覆盖层里那一页）：
            // 点开当场就在，填完再按「创建」—— 没有嵌套事件循环要排。
            openServerForm();
            QApplication::processEvents();
            auto *page = w.findChild<NewServerPage *>(MainWindowUI::newServerPageName());
            const bool pageSeen = page != nullptr && !page->isWindow();
            bool createEnabled = false;
            bool targetDirShown = false;
            if (page != nullptr) {
                // 用**真的控件**填（不是 `setDefaults()`）：那两个入口套了 QSignalBlocker，
                // 只有走 `setText()` 才会发 `fieldsChanged()`、让翻译点重算计划。
                if (auto *nameEdit =
                        page->findChild<QLineEdit *>(MainWindowUI::newServerNameEditName())) {
                    nameEdit->setText(installedName);
                }
                if (auto *verEdit = page->findChild<QLineEdit *>(
                        MainWindowUI::newServerGameVersionEditName())) {
                    verEdit->setText(QStringLiteral("1.20.1"));
                }
                // 「目录」那一行是**算出来的**：名字 → sanitize → unique，和真建的那一条同路。
                if (auto *dirLabel =
                        page->findChild<QLabel *>(MainWindowUI::newServerTargetDirName())) {
                    targetDirShown = dirLabel->text().contains(installedId);
                }
                if (auto *btn = page->findChild<QPushButton *>(
                        MainWindowUI::newServerCreateButtonName())) {
                    createEnabled = btn->isEnabled();
                    btn->click();  // = accept()，MainWindow 接住之后建目录 + 排安装任务
                }
            }
            QApplication::processEvents();
            check(pageSeen && createEnabled,
                  "T40.1 那一页开出来了；填了名字 + 版本之后「创建」能点（原版这一档计划成立）");
            check(targetDirShown,
                  "T40.1 「目录」那一行显示的就是按名字算出来的那个目录（和真建的是同一条路）");
        }

        const Tasks::TaskSnapshot started = newInstallSince(before);
        check(started.kind == Tasks::TaskKind::Install && started.targetId == installedId &&
                  started.title == QStringLiteral("安装服务端 %1").arg(installedId),
              "★★ T40.2 点「创建」⇒ 账本上立刻多出一项 Install，标题带 serverId、目标就是它"
              "（不是「建个空壳让用户自己再点一下」）",
              QStringLiteral("标题=%1 目标=%2").arg(started.title, started.targetId));

        // ---------- ② 装满：盘上真有那一份、字节一字不差 ----------
        // 这一节把产品自己写的那几行日志流出来（`pumpUntil` 的 `printedOut`）：一个卡住的
        // 安装和一个还没跑完的安装长得一模一样，而这四段走到哪儿只有那几行 message 说得清
        // （进度不进日志 —— 产品级硬约束）。游标从**这一节开始**算，别把前 39 节的日志倒一遍。
        int logCursor = messagesSnapshot().size();
        const bool installDone = pumpUntil(
            [&] {
                return started.id != Tasks::invalidTaskId() &&
                       taskWithId(started.id).state == Tasks::TaskState::Succeeded;
            },
            20000, &logCursor);
        const Tasks::TaskSnapshot afterInstall = taskWithId(started.id);
        // 没走完时把**步骤表**整张打出来：卡在哪一段、那一段的小字说了什么，一眼就能看。
        QString stepDump;
        for (const Tasks::TaskStep &step : afterInstall.steps) {
            stepDump += QStringLiteral("[%1 %2 %3] ")
                            .arg(step.name)
                            .arg(int(step.state))
                            .arg(step.note);
        }
        check(installDone,
              "★★ T40.3 那一趟走完了（四段全过：取元数据 → 下载 → 核对 → 收尾）",
              QStringLiteral("假服务器被打过 %1 次；账本上状态 = %2（%3）步骤：%4")
                  .arg(files.hits)
                  .arg(int(afterInstall.state))
                  .arg(afterInstall.errorSummary, stepDump));
        QByteArray onDisk;
        {
            QFile jar(AppPaths::serverDir(installedId) + QStringLiteral("/server.jar"));
            if (jar.open(QIODevice::ReadOnly)) {
                onDisk = jar.readAll();
            }
        }
        check(!onDisk.isEmpty() && onDisk == jarBody,
              "★★ T40.3 盘上那份 `Servers/<id>/server.jar` 就是假服务器发出来的那些字节"
              "（**真的下下来了**，不是一个空壳）",
              QStringLiteral("盘上 %1 字节 / 期望 %2 字节")
                  .arg(onDisk.size())
                  .arg(jarBody.size()));
        check(files.hits > 0,
              "★ T40.3 而且那一份是**从网上**拿到磁盘的（假服务器被打过一次）",
              QStringLiteral("%1 次").arg(files.hits));
        {
            QTreeWidgetItem *row = rowForId(installedId);
            const QString sub =
                row != nullptr ? row->data(0, ListRowDelegate::SubtitleRole).toString() : QString();
            check(row != nullptr && sub.contains(ServerModel::statusName(ServerModel::Status::Ready)) &&
                      row->data(0, ListRowDelegate::LaunchTextRole).toString() ==
                          MainWindowUI::serverStartText(),
                  "★★ T40.4 装完那一行变成「可启动」、第三格是「启动」"
                  "（判据和任务中心那句「装好了」是**同一个出处**：`ServerRegistry::describe()`）",
                  row != nullptr ? sub : QStringLiteral("<没有这一行>"));
        }

        // ---------- ③ Spigot：列着，但「创建」点不动 + 摘要就是那句话 ----------
        const QString spigotName = QStringLiteral("Spigot 试一下");
        const QString spigotId =
            ServerRegistry::uniqueServerId(ServerRegistry::sanitizeId(spigotName));
        {
            openServerForm();
            QApplication::processEvents();
            auto *page = w.findChild<NewServerPage *>(MainWindowUI::newServerPageName());
            const bool pageSeen = page != nullptr && !page->isWindow();
            bool comboSawSpigot = false;
            bool createDisabled = false;
            QString summary;
            if (page != nullptr) {
                if (auto *nameEdit =
                        page->findChild<QLineEdit *>(MainWindowUI::newServerNameEditName())) {
                    nameEdit->setText(spigotName);  // 名字非空，摘要才会走到「档」那一步
                }
                if (auto *flavors =
                        page->findChild<QListWidget *>(MainWindowUI::newServerFlavorListName())) {
                    const QString want = ServerInstall::flavorToken(ServerInstall::Flavor::Spigot);
                    for (int i = 0; i < flavors->count(); ++i) {
                        if (flavors->item(i)->data(Qt::UserRole).toString() == want) {
                            comboSawSpigot = true;
                            flavors->setCurrentRow(i);  // 发 currentRowChanged → 翻译点重算
                            break;
                        }
                    }
                }
                QApplication::processEvents();
                if (auto *btn = page->findChild<QPushButton *>(
                        MainWindowUI::newServerCreateButtonName())) {
                    createDisabled = !btn->isEnabled();
                }
                if (auto *label = page->findChild<QLabel *>(
                        MainWindowUI::newServerSummaryLabelName())) {
                    summary = label->text();
                }
                page->reject();  // 退回版本列表那一层（**不建**任何东西）
                QApplication::processEvents();
            }
            check(pageSeen && comboSawSpigot,
                  "T40.5 前提：下拉框里**列着** Spigot 这一档（用户得知道有它）");
            check(createDisabled,
                  "★★ T40.5 选中 Spigot：「创建」点不动"
                  "（不留一颗按下去什么都不发生的按钮）");
            check(summary == ServerInstallRun::unsupportedReason(ServerInstall::Flavor::Spigot),
                  "★★ T40.5 而且摘要**逐字**就是判据层那句「暂时不支持」"
                  "（界面不另写一句话，`unsupportedReason()` 是唯一出处）",
                  summary);
            check(!QDir(AppPaths::serverDir(spigotId)).exists(),
                  "★ T40.5 收页之后盘上什么都没建（「创建」点不动就是真的不动）");
        }

        // ---------- ④ 「取消」：收成 Cancelled（不是 Failed）、不留半份 ----------
        const QString cancelName = QStringLiteral("取消用服务端");
        const QString cancelId =
            ServerRegistry::uniqueServerId(ServerRegistry::sanitizeId(cancelName));
        {
            // 账本的"起点"必须在**按下创建之前**取：这一节这一趟本身就是新加的那一项，
            // 取晚了它就进了 `known`，`newInstallSince()` 从那儿往后什么新东西都找不到。
            const QSet<quint64> beforeCancel = installIds();
            openServerForm();
            QApplication::processEvents();
            auto *page = w.findChild<NewServerPage *>(MainWindowUI::newServerPageName());
            const bool pageSeen = page != nullptr && !page->isWindow();
            if (page != nullptr) {
                if (auto *nameEdit =
                        page->findChild<QLineEdit *>(MainWindowUI::newServerNameEditName())) {
                    nameEdit->setText(cancelName);
                }
                if (auto *verEdit = page->findChild<QLineEdit *>(
                        MainWindowUI::newServerGameVersionEditName())) {
                    // 这一版那份 json 指向「收下不答」的那台 ⇒ 这一趟的下载永远下不完。
                    verEdit->setText(QStringLiteral("1.20.2"));
                }
                if (auto *btn = page->findChild<QPushButton *>(
                        MainWindowUI::newServerCreateButtonName())) {
                    btn->click();
                }
            }
            QApplication::processEvents();
            check(pageSeen, "T40.6 前提：取消那一趟也排上了");

            const Tasks::TaskSnapshot pending = newInstallSince(beforeCancel);
            check(pending.targetId == cancelId,
                  "T40.6 前提：账本上那一项就是它（目标 = 取消用服务端）", pending.targetId);

            // 这一趟的下载**卡在**那台不答话的假服务器上 —— 走**真的那一条路**取消它：
            // 点开底栏中间那格（任务中心），按标题认出这一行，点它行尾那颗「取消」
            // （那一颗走的是 `onTaskDetailAction(key, Cancel)`，和详情页里那颗同一条路）。
            auto *summaryCell = byObjectName<QPushButton>(&w, MainWindowUI::taskSummaryButtonName());
            check(summaryCell != nullptr, "T40.6 前提：底栏中间那格（任务中心）找得到");
            bool cancelledByUi = false;
            if (summaryCell != nullptr) {
                summaryCell->click();
                ActivityPopover::openAllIn(&w);  // 摘要先弹浮动面板，从「全部任务」进整页
                QApplication::processEvents();
                if (QWidget *panel =
                        w.findChild<QWidget *>(MainWindowUI::taskCenterPanelName())) {
                    const QString wantTitle =
                        QStringLiteral("安装服务端 %1").arg(cancelId);
                    for (int i = 0; i < 20; ++i) {
                        QWidget *row =
                            panel->findChild<QWidget *>(MainWindowUI::taskCenterPanelRowName(i));
                        if (row == nullptr) {
                            break;
                        }
                        auto *text = row->findChild<QLabel *>(
                            MainWindowUI::taskCenterPanelRowTitleName(i));
                        if (text == nullptr || text->text() != wantTitle) {
                            continue;
                        }
                        if (auto *cancelBtn = panel->findChild<QPushButton *>(
                                MainWindowUI::taskCenterPanelRowCancelName(i))) {
                            cancelBtn->click();
                            cancelledByUi = true;
                        }
                        break;
                    }
                    panel->close();
                    QApplication::processEvents();
                }
            }
            check(cancelledByUi,
                  "T40.6 在跑的那一项：任务中心里那一行行尾有那颗「取消」，点得动");

            const bool cancelled = pumpUntil(
                [&] {
                    return pending.id != Tasks::invalidTaskId() &&
                           taskWithId(pending.id).state == Tasks::TaskState::Cancelled;
                },
                8000, &logCursor);
            const Tasks::TaskSnapshot afterCancel = taskWithId(pending.id);
            check(cancelled && afterCancel.state == Tasks::TaskState::Cancelled,
                  "★★ T40.6 取消 ⇒ 账本上那一项收成 **Cancelled**（不是 Failed —— "
                  "取消是一种正常收场，不能顺手点亮底栏那颗「没看过的失败」红点）",
                  QStringLiteral("状态=%1").arg(int(afterCancel.state)));
            check(!QFile::exists(AppPaths::serverDir(cancelId) + QStringLiteral("/server.jar")),
                  "★★ T40.6 而且盘上**没有半份 server.jar**（取消不留一个会被当成成品的半成品）");
            QTreeWidgetItem *row = rowForId(cancelId);
            const QString cancelSub =
                row != nullptr ? row->data(0, ListRowDelegate::SubtitleRole).toString() : QString();
            check(row != nullptr &&
                      cancelSub.contains(ServerModel::statusName(ServerModel::Status::NotInstalled)),
                  "★ T40.6 那一行照旧「未安装」（取消之后它还是个没装好的服务端）",
                  row != nullptr ? cancelSub : QStringLiteral("<没有这一行>"));
        }

        w.close();
        QApplication::processEvents();
        sandbox.setValue(QStringLiteral("Launcher/Folders"), foldersBefore);
        sandbox.setValue(QStringLiteral("Launcher/CurrentFolder"), currentBefore);
        sandbox.sync();
        AppPaths::setMinecraftDir(originalDir);
        check(AppPaths::minecraftDir() == originalDir, "T40.9 数据根目录换回来了");
    }
    return 0;
}


