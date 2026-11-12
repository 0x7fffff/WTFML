#include "Support.hpp"
int UiAccountSkinChecks() {
    {
        AccountSkinDialog dialog;
        dialog.show();
        QApplication::processEvents();

        // 这个窗里所有控件都靠 objectName 找回来 —— 不去认文字（文字会随数据变：
        // 账号名后面会挂「（当前）」）。
        const auto child = [&dialog](const QString &name) {
            return dialog.findChild<QWidget *>(name);
        };
        QListWidget *accountList =
            qobject_cast<QListWidget *>(child(AccountSkinDialog::accountListName()));
        QListWidget *capeList =
            qobject_cast<QListWidget *>(child(AccountSkinDialog::capeListName()));
        QPushButton *loginBtn =
            qobject_cast<QPushButton *>(child(AccountSkinDialog::loginButtonName()));
        QPushButton *switchBtn =
            qobject_cast<QPushButton *>(child(AccountSkinDialog::switchButtonName()));
        QPushButton *removeBtn =
            qobject_cast<QPushButton *>(child(AccountSkinDialog::removeButtonName()));
        QPushButton *refreshBtn =
            qobject_cast<QPushButton *>(child(AccountSkinDialog::refreshButtonName()));
        QPushButton *uploadBtn =
            qobject_cast<QPushButton *>(child(AccountSkinDialog::uploadButtonName()));
        QPushButton *applyCapeBtn =
            qobject_cast<QPushButton *>(child(AccountSkinDialog::applyCapeButtonName()));
        QPushButton *hideCapeBtn =
            qobject_cast<QPushButton *>(child(AccountSkinDialog::hideCapeButtonName()));
        QPushButton *closeBtn =
            qobject_cast<QPushButton *>(child(AccountSkinDialog::closeButtonName()));
        QLabel *statusLabel =
            qobject_cast<QLabel *>(child(AccountSkinDialog::statusLabelName()));
        QComboBox *variantCombo =
            qobject_cast<QComboBox *>(child(AccountSkinDialog::variantComboName()));
        QLineEdit *offlineEdit =
            qobject_cast<QLineEdit *>(child(AccountSkinDialog::offlineNameEditName()));
        QPushButton *addOfflineBtn =
            qobject_cast<QPushButton *>(child(AccountSkinDialog::addOfflineButtonName()));
        QPushButton *pickOfflineBtn = qobject_cast<QPushButton *>(
            child(AccountSkinDialog::offlineSkinButtonName()));
        QPushButton *resetOfflineBtn = qobject_cast<QPushButton *>(
            child(AccountSkinDialog::offlineSkinResetButtonName()));
        SkinViewer *viewer = qobject_cast<SkinViewer *>(child(AccountSkinDialog::viewerName()));

        const bool allThere = accountList != nullptr && capeList != nullptr &&
                              loginBtn != nullptr && switchBtn != nullptr &&
                              removeBtn != nullptr && refreshBtn != nullptr &&
                              uploadBtn != nullptr && applyCapeBtn != nullptr &&
                              hideCapeBtn != nullptr && closeBtn != nullptr &&
                              statusLabel != nullptr && variantCombo != nullptr &&
                              viewer != nullptr && offlineEdit != nullptr &&
                              addOfflineBtn != nullptr && pickOfflineBtn != nullptr &&
                              resetOfflineBtn != nullptr;
        check(allThere, "十七块控件都按 objectName 找得到");
        // NOTE: 下面整段都挂在这个 if 里 —— 少一块控件时直接解引用会段错误，
        // 那样后半套断言的结果就全看不到了。
        if (allThere) {
            // ---- X1 文案与初始状态 ----
            check(dialog.windowTitle() == AccountSkinDialog::windowTitle(),
                  "窗口标题是产品那句", dialog.windowTitle());
            check(loginBtn->text() == AccountSkinDialog::loginButtonText() &&
                      switchBtn->text() == AccountSkinDialog::switchButtonText() &&
                      removeBtn->toolTip() == AccountSkinDialog::removeButtonText() &&
                      removeBtn->text().isEmpty() && !removeBtn->icon().isNull() &&
                      refreshBtn->text() == AccountSkinDialog::refreshButtonText(),
                  "左边四个按钮的文字都是产品文案");
            // 披风是一个「显示 / 隐藏」分段：露出来的字是短的那两个，长的那两句是 tooltip。
            // 删除是行尾一颗垃圾桶（字只在 tooltip / 读屏名里）。
            check(uploadBtn->text() == AccountSkinDialog::uploadButtonText() &&
                      applyCapeBtn->text() == QStringLiteral("显示") &&
                      applyCapeBtn->toolTip() == AccountSkinDialog::applyCapeButtonText() &&
                      hideCapeBtn->text() == QStringLiteral("隐藏") &&
                      hideCapeBtn->toolTip() == AccountSkinDialog::hideCapeButtonText() &&
                      closeBtn->text() == AccountSkinDialog::closeButtonText(),
                  "右边三个 + 底的「关闭」也都是产品文案",
                  QStringLiteral("显示=%1 / 隐藏=%2 / 关闭=%3")
                      .arg(applyCapeBtn->text(), hideCapeBtn->text(), closeBtn->text()));
            check(offlineEdit->placeholderText() == AccountSkinDialog::offlineNamePlaceholder() &&
                      offlineEdit->text().isEmpty(),
                  "离线名字那个框一开始是空的，占位文字说明这里填什么",
                  offlineEdit->placeholderText());
            check(addOfflineBtn->toolTip() == AccountSkinDialog::addOfflineButtonText()
                      && !addOfflineBtn->isVisibleTo(&dialog) && !offlineEdit->isVisibleTo(&dialog),
                  "「添加离线账号」那一行先收着（点小链接才露），按钮说明仍是产品文案",
                  addOfflineBtn->toolTip());
            check(statusLabel->text() == AccountSkinDialog::notLoggedInNote(),
                  "一进来状态行说的是「还没登录」那句", statusLabel->text());
            // 这个控件任何一档状态都不该是空白的（和主页右栏那个小人同一条约定）。
            check(viewer->skin() == SkinModel::defaultSteveSkin() && viewer->cape().isNull(),
                  "预览一进来就是默认外观，不是一片空白");
            check(variantCombo->count() == 2 &&
                      variantCombo->itemText(0) == AccountSkinDialog::variantClassicText() &&
                      variantCombo->itemText(1) == AccountSkinDialog::variantSlimText(),
                  "模型下拉框两项：经典 / 纤细");
            check(variantCombo->itemData(0).toString() ==
                          AccountSkinDialog::variantClassicValue() &&
                      variantCombo->itemData(1).toString() ==
                          AccountSkinDialog::variantSlimValue(),
                  "两项带的值是**小写**的 classic / slim（传给官方就是这两个）",
                  variantCombo->itemData(0).toString() + " / " +
                      variantCombo->itemData(1).toString());
            check(dialog.selectedVariant() == AccountSkinDialog::variantClassicValue(),
                  "默认是经典模型");
            check(!switchBtn->isEnabled() && !removeBtn->isEnabled() &&
                      !applyCapeBtn->isEnabled(),
                  "还没喂数据时，三个「对所选做事」的按钮都灰着");

            // ---- X1b 离线账号的「换一张…」/「恢复默认外观」 ----
            // 两组按钮**互斥**：正版账号走「上传皮肤」（那是 Mojang 上传），离线账号走本地
            // 挑图 —— 两个一起露会让用户以为离线也能上传。
            check(pickOfflineBtn->text() == AccountSkinDialog::offlineSkinButtonText() &&
                      resetOfflineBtn->text() ==
                          AccountSkinDialog::offlineSkinResetButtonText(),
                  "离线那两颗的文字是产品文案",
                  pickOfflineBtn->text() + " / " + resetOfflineBtn->text());
            check(pickOfflineBtn->toolTip() ==
                          AccountSkinDialog::offlineSkinButtonToolTip() &&
                      resetOfflineBtn->toolTip() ==
                          AccountSkinDialog::offlineSkinResetButtonToolTip(),
                  "两颗的 tooltip 都在（说清只影响启动器里的预览）");
            check(!uploadBtn->isHidden() && pickOfflineBtn->isHidden() &&
                      resetOfflineBtn->isHidden(),
                  "默认（正版 / 还没喂状态）：只露「上传皮肤」");
            dialog.setOfflineSkinState(true, /*hasCustomSkin=*/false);
            QApplication::processEvents();
            check(uploadBtn->isHidden() && !pickOfflineBtn->isHidden() &&
                      !resetOfflineBtn->isHidden(),
                  "离线账号：「上传皮肤」收起来，露出「换一张…」「恢复默认外观」");
            check(pickOfflineBtn->isEnabled() && !resetOfflineBtn->isEnabled(),
                  "还没手动挑过时，「恢复默认外观」灰着（没东西可恢复）");
            dialog.setOfflineSkinState(true, /*hasCustomSkin=*/true);
            QApplication::processEvents();
            check(resetOfflineBtn->isEnabled(),
                  "手动挑过之后，「恢复默认外观」可点");
            dialog.setOfflineSkinState(false, /*hasCustomSkin=*/false);
            QApplication::processEvents();
            check(!uploadBtn->isHidden() && pickOfflineBtn->isHidden() &&
                      resetOfflineBtn->isHidden(),
                  "切回正版账号：又只剩「上传皮肤」（两组不共存）");

            // ---- X2 一个账号都没有：摆一句说明，而不是一片空白 ----
            // 空白让人以为"还没加载完"，于是一直等 —— 一行说明才是"就是没有"。
            dialog.setAccounts({});
            QApplication::processEvents();
            check(accountList->count() == 1, "空列表里摆了一行",
                  QString::number(accountList->count()));
            check(accountList->count() == 1 &&
                      accountList->item(0)->text() == AccountSkinDialog::noAccountsText(),
                  "那一行是「还没有保存的账号。」");
            check(accountList->count() == 1 &&
                      !accountList->item(0)->flags().testFlag(Qt::ItemIsSelectable),
                  "那一行**选不动**（它不是一个账号）");
            check(dialog.selectedUuid().isEmpty(), "所以 selectedUuid() 是空串");
            check(!switchBtn->isEnabled() && !removeBtn->isEnabled(),
                  "没有账号可选的时候那两个按钮照样灰着");

            // ---- X3 有账号：选中「当前」那一条，名字后面挂标记 ----
            QList<AccountSkinDialog::AccountRow> rows;
            rows.append(AccountSkinDialog::AccountRow{QStringLiteral("uuid-a"),
                                                      QStringLiteral("Alice"),
                                                      QStringLiteral("2026-09-22T10:00:00Z"),
                                                      true});
            rows.append(AccountSkinDialog::AccountRow{QStringLiteral("uuid-b"),
                                                      QStringLiteral("Bob"), QString(), false});
            rows.append(AccountSkinDialog::AccountRow{QStringLiteral("uuid-c"),
                                                      QStringLiteral("Carol"), QString(), false});
            dialog.setAccounts(rows);
            QApplication::processEvents();
            check(accountList->count() == 3, "三条账号都摆出来了",
                  QString::number(accountList->count()));
            check(accountList->currentRow() == 0, "默认选中「当前」那一条（第 0 条）",
                  QString::number(accountList->currentRow()));
            check(accountList->item(0)->text() ==
                      QStringLiteral("Alice") + AccountSkinDialog::currentMark(),
                  "当前账号那一行挂着「（当前）」", accountList->item(0)->text());
            check(accountList->item(1)->text() == QStringLiteral("Bob"),
                  "不是当前的那些**不挂**标记", accountList->item(1)->text());
            check(dialog.selectedUuid() == QStringLiteral("uuid-a"),
                  "selectedUuid() 给的是选中那一条的 uuid", dialog.selectedUuid());
            check(switchBtn->isEnabled() && removeBtn->isEnabled(),
                  "有选中项了，那两个按钮跟着可点");

            // savedAt 落在 tooltip 上（列表里写不下"什么时候存的"）。
            check(!accountList->item(0)->toolTip().isEmpty() &&
                      accountList->item(0)->toolTip().contains(QStringLiteral("2026")),
                  "带时间的账号有 tooltip，里面有那个日期",
                  accountList->item(0)->toolTip());
            check(accountList->item(1)->toolTip().isEmpty(),
                  "没有时间的账号不摆一个空 tooltip");

            // ---- X4 没有「当前」账号时选第一条 ----
            // 凭据被清掉了（比如删了当前账号）但列表还在，这时不能一行都不选 ——
            // 一行都不选的话三个按钮全是灰的，看着像坏了。
            {
                QList<AccountSkinDialog::AccountRow> noneCurrent;
                noneCurrent.append(AccountSkinDialog::AccountRow{QStringLiteral("uuid-b"),
                                                                 QStringLiteral("Bob"),
                                                                 QString(), false});
                noneCurrent.append(AccountSkinDialog::AccountRow{QStringLiteral("uuid-c"),
                                                                 QStringLiteral("Carol"),
                                                                 QString(), false});
                dialog.setAccounts(noneCurrent);
                QApplication::processEvents();
                check(accountList->currentRow() == 0,
                      "一个「当前」都没有时选中第一条",
                      QString::number(accountList->currentRow()));
            }

            // ---- X5 按钮发出来的 uuid 是**选中的那个**，不是第 0 条 ----
            // 这里选第 3 条就是钉这一件事：拿 currentRow() 当"第几条"用会退化成永远动
            // 第一条 —— 而那个症状（"我明明选的是 Carol，切过去变成 Alice 了"）很难查。
            {
                int switchCount = 0;
                int removeCount = 0;
                QString switchedUuid;
                QString removedUuid;
                QObject::connect(&dialog, &AccountSkinDialog::switchRequested, &dialog,
                                 [&](const QString &uuid) {
                                     ++switchCount;
                                     switchedUuid = uuid;
                                 });
                QObject::connect(&dialog, &AccountSkinDialog::removeRequested, &dialog,
                                 [&](const QString &uuid) {
                                     ++removeCount;
                                     removedUuid = uuid;
                                 });

                dialog.setAccounts(rows);
                QApplication::processEvents();
                accountList->setCurrentRow(2);
                QApplication::processEvents();
                check(dialog.selectedUuid() == QStringLiteral("uuid-c"),
                      "选到第 3 条（Carol）", dialog.selectedUuid());

                switchBtn->click();
                check(switchCount == 1 && switchedUuid == QStringLiteral("uuid-c"),
                      "点「切换到所选」发的就是 Carol 的 uuid", switchedUuid);
                removeBtn->click();
                check(removeCount == 1 && removedUuid == QStringLiteral("uuid-c"),
                      "点「删除所选」发的也是 Carol 的 uuid", removedUuid);
            }

            // ---- X6 披风列表 ----
            {
                int selectCount = 0;
                int hideCount = 0;
                QString selectedCape;
                QObject::connect(&dialog, &AccountSkinDialog::capeSelectRequested, &dialog,
                                 [&](const QString &id) {
                                     ++selectCount;
                                     selectedCape = id;
                                 });
                QObject::connect(&dialog, &AccountSkinDialog::hideCapeRequested, &dialog,
                                 [&] { ++hideCount; });

                QList<AccountSkinDialog::CapeRow> capes;
                capes.append(AccountSkinDialog::CapeRow{QStringLiteral("cape-1"),
                                                        QStringLiteral("Migrator"), true});
                capes.append(AccountSkinDialog::CapeRow{QStringLiteral("cape-2"),
                                                        QStringLiteral("MineCon 2016"), false,
                                                        QStringLiteral("https://textures.example/cape-2")});
                dialog.setCapes(capes);
                QApplication::processEvents();
                {
                    // 披风贴图到了：只补进地址对得上的那一格（另一格仍画占位色条）。
                    QImage texture(64, 32, QImage::Format_ARGB32);
                    texture.fill(Qt::red);
                    dialog.setCapeTexture(QStringLiteral("https://textures.example/cape-2"), texture);
                    dialog.setCapeTexture(QStringLiteral("https://textures.example/other"), texture);
                    check(capeList->item(1)->data(Qt::UserRole + 3).value<QImage>().size() == QSize(64, 32) &&
                              capeList->item(0)->data(Qt::UserRole + 3).isNull(),
                          "★ 披风贴图按地址认领：对得上的那一格换成贴图，对不上的不动");
                }
                check(capeList->count() == 2, "两件披风都摆出来了",
                      QString::number(capeList->count()));
                check(capeList->item(0)->text() ==
                          QStringLiteral("Migrator") + AccountSkinDialog::currentMark(),
                      "正显示着的那一件挂着「（当前）」", capeList->item(0)->text());
                check(capeList->item(1)->text() == QStringLiteral("MineCon 2016"),
                      "另一件不挂标记", capeList->item(1)->text());
                check(dialog.selectedCapeId().isEmpty() && !applyCapeBtn->isEnabled(),
                      "还没选披风时「显示所选披风」灰着");

                capeList->setCurrentRow(1);
                QApplication::processEvents();
                check(dialog.selectedCapeId() == QStringLiteral("cape-2"),
                      "选中第 2 件的 id 认得出来", dialog.selectedCapeId());
                check(applyCapeBtn->isEnabled(), "选中之后那个按钮可点");

                applyCapeBtn->click();
                check(selectCount == 1 && selectedCape == QStringLiteral("cape-2"),
                      "点「显示所选披风」发的是选中那件的 id", selectedCape);

                // 「不显示披风」**不看选中项**：它是一个独立的动作（取消），列表里选中
                // 哪一件都跟它没关系。
                capeList->setCurrentRow(-1);
                QApplication::processEvents();
                check(!applyCapeBtn->isEnabled(), "取消选中之后「显示所选披风」又灰回去");
                hideCapeBtn->click();
                check(hideCount == 1, "没选中任何披风时「不显示披风」照样发信号");

                dialog.setCapes({});
                QApplication::processEvents();
                check(capeList->count() == 0 && !applyCapeBtn->isEnabled(),
                      "一件披风都没有时列表是空的、按钮灰着（披风列表不摆占位行）");
            }

            // ---- X7 setLooks：同一份贴图进预览，下拉框跟着当前模型走 ----
            {
                QImage testSkin(64, 64, QImage::Format_ARGB32_Premultiplied);
                testSkin.fill(Qt::transparent);
                {
                    QPainter painter(&testSkin);
                    painter.fillRect(0, 0, 64, 64, QColor(20, 200, 60));
                }
                QImage testCape(64, 32, QImage::Format_ARGB32_Premultiplied);
                testCape.fill(Qt::transparent);
                {
                    QPainter painter(&testCape);
                    painter.fillRect(0, 0, 64, 32, QColor(200, 30, 30));
                }

                dialog.setLooks(testSkin, testCape, true);
                check(viewer->skin() == testSkin && viewer->cape() == testCape,
                      "喂进去的皮肤和披风都进了预览");
                check(dialog.selectedVariant() == AccountSkinDialog::variantSlimValue(),
                      "纤细模型 → 下拉框自动切到「纤细（Alex）」（用户多半是在当前模型上调）",
                      dialog.selectedVariant());

                dialog.setLooks(testSkin, QImage(), false);
                check(dialog.selectedVariant() == AccountSkinDialog::variantClassicValue(),
                      "经典模型 → 下拉框切回「经典（Steve）」");
                check(viewer->cape().isNull(), "披风传空图 → 预览上就没披风了");

                // 空皮肤不许把预览留成空白 —— 那个控件任何一档都得有东西可看。
                dialog.setLooks(QImage(), QImage(), false);
                check(viewer->skin() == SkinModel::defaultSteveSkin(),
                      "皮肤传空图 → 退回默认外观，不是空白");

                // 那个「模型」下拉框的滚轮防护：漏了后果比设置页那几个重 ——
                // 鼠标划过去静默换一档，接着点上传就传错模型（经典/纤细在游戏里是两套手臂）。
                {
                    const QString problem = wheelGuardProblem(
                        dialog.findChild<QComboBox *>(AccountSkinDialog::variantComboName()));
                    check(problem.isEmpty(), "X7b 那个「模型」下拉框也装了滚轮防护", problem);
                }
            }

            // ---- X8 忙碌态：按钮一起灰 ----
            // 这一段盯的是一个真缺陷：refreshButtonStates() 是按「有没有选中」定
            // 可用性的，而 setBusy() 末尾会调它一次 —— 于是忙的时候那三个按钮被重新点亮，
            // 登录还在飞（浏览器可能几十秒才回来）用户就能切账号，回来之后两条路各写一遍
            // 凭据。所以下面这条断言是**故意**盯着它们的。
            {
                dialog.setAccounts(rows);
                QList<AccountSkinDialog::CapeRow> capes;
                capes.append(AccountSkinDialog::CapeRow{QStringLiteral("cape-1"),
                                                        QStringLiteral("Migrator"), true});
                dialog.setCapes(capes);
                capeList->setCurrentRow(0);
                QApplication::processEvents();
                check(switchBtn->isEnabled() && removeBtn->isEnabled() &&
                          applyCapeBtn->isEnabled(),
                      "X8 前提：不忙而且都选中了，那三个按钮都是点得动的");

                dialog.setBusy(true, QStringLiteral("正在等浏览器完成登录…"));
                QApplication::processEvents();
                check(statusLabel->text() == QStringLiteral("正在等浏览器完成登录…"),
                      "忙的时候状态行说的是在等什么", statusLabel->text());
                check(!loginBtn->isEnabled() && !refreshBtn->isEnabled() &&
                          !uploadBtn->isEnabled(),
                      "登录 / 刷新外观 / 上传皮肤一起灰掉");
                check(!switchBtn->isEnabled() && !removeBtn->isEnabled() &&
                          !applyCapeBtn->isEnabled(),
                      "「切换到所选 / 删除所选 / 显示所选披风」也灰掉（不能一边登录一边切账号）");
                check(!accountList->isEnabled() && !capeList->isEnabled(),
                      "两个列表也灰掉（它们的内容正是正在被刷新的那两样）");

                dialog.setBusy(false);
                QApplication::processEvents();
                check(loginBtn->isEnabled() && refreshBtn->isEnabled() && uploadBtn->isEnabled(),
                      "忙态松开之后那三个按钮回来");
                check(switchBtn->isEnabled() && removeBtn->isEnabled() &&
                          applyCapeBtn->isEnabled(),
                      "按选中项恢复的那三个也回来（选中项还在）");
                check(statusLabel->text() == QStringLiteral("正在等浏览器完成登录…"),
                      "setBusy(false) 不碰状态行的文字 —— 写什么由调用方决定"
                      "（背景刷新失败要写失败原因，成功就写回当前身份）");
            }

            // ---- X9 离线账号那一行（输入框 / 添加按钮 / 列表里的「（离线）」） ----
            // 这一档钉的全是**交出去的东西**：名字合不合法**不在这个窗里判**（唯一出处是
            // AccountStore::addOffline()），在这里也判一遍就会出现两份判据、两套话术。
            {
                QStringList requested;
                QObject::connect(&dialog, &AccountSkinDialog::offlineAccountRequested, &dialog,
                                 [&](const QString &name) { requested.append(name); });

                offlineEdit->setText(QStringLiteral("  Steve  "));
                addOfflineBtn->click();
                check(requested.size() == 1 && requested.at(0) == QStringLiteral("Steve"),
                      "点「添加离线账号」把框里那个名字交出去，而且首尾空格已经去掉",
                      requested.join(QStringLiteral(" / ")));

                // 在框里回车 = 点那个按钮（少一次"打完名字还要去够鼠标"）。
                // NOTE: 这里直接把信号叫起来，模拟的是 QLineEdit 自己会做的事 —— 真按键盘
                // 要 QtTest 那套事件注入，这个工程里没有。
                offlineEdit->setText(QStringLiteral("Alex"));
                emit offlineEdit->returnPressed();
                check(requested.size() == 2 && requested.at(1) == QStringLiteral("Alex"),
                      "在框里回车也交一次（回车 = 点那个按钮）",
                      requested.join(QStringLiteral(" / ")));

                auto *offlineToggle = child(QStringLiteral("accountOfflineToggle"));
                if (auto *toggleBtn = qobject_cast<QPushButton *>(offlineToggle)) toggleBtn->click();
                check(offlineToggle != nullptr && offlineEdit->isVisibleTo(&dialog)
                          && addOfflineBtn->isVisibleTo(&dialog) && !offlineToggle->isVisibleTo(&dialog),
                      "点「添加离线账号」小链接：露出输入框 + 「添加」，小链接自己收起");
                offlineEdit->setText(QStringLiteral("留着没提交的名字"));
                dialog.clearOfflineNameDraft();
                check(offlineEdit->text().isEmpty() && !offlineEdit->isVisibleTo(&dialog)
                          && offlineToggle != nullptr && offlineToggle->isVisibleTo(&dialog),
                      "clearOfflineNameDraft() 把框清空并收回成小链接");

                // 忙的时候这一行也得灰：它一旦成功就会把活动槽清空（切到离线身份），而正飞着的
                // 那次登录回来还要往活动槽里写 —— 两个人会打架。
                dialog.setBusy(true, QStringLiteral("正在等浏览器完成登录…"));
                QApplication::processEvents();
                check(!offlineEdit->isEnabled() && !addOfflineBtn->isEnabled(),
                      "忙的时候那个框和按钮一起灰（不许一边登录一边加离线账号）");
                dialog.setBusy(false);
                QApplication::processEvents();
                check(offlineEdit->isEnabled() && addOfflineBtn->isEnabled(),
                      "忙态松开之后它们又点得动了");

                // 列表里那条离线账号：名字后面挂「（离线）」，和「（当前）」可以同时出现。
                QList<AccountSkinDialog::AccountRow> mixed;
                mixed.append(AccountSkinDialog::AccountRow{QStringLiteral("uuid-a"),
                                                           QStringLiteral("Alice"),
                                                           QStringLiteral("2026-09-22T10:00:00Z"),
                                                           false, true});
                mixed.append(AccountSkinDialog::AccountRow{QStringLiteral("uuid-s"),
                                                           QStringLiteral("Steve"), QString(),
                                                           true, true});
                dialog.setAccounts(mixed);
                QApplication::processEvents();
                check(accountList->item(0)->text() ==
                          QStringLiteral("Alice") + AccountSkinDialog::offlineMark(),
                      "离线账号那一行挂着「（离线）」", accountList->item(0)->text());
                check(accountList->item(1)->text() ==
                          QStringLiteral("Steve") + AccountSkinDialog::offlineMark() +
                              AccountSkinDialog::currentMark(),
                      "又离线又是当前的那一行两个标记都在（「（离线）」在前）",
                      accountList->item(1)->text());
                check(accountList->item(0)->toolTip() == AccountSkinDialog::offlineRowTooltip(),
                      "离线那一行的 tooltip 说清它是哪一种（不是空串、也不是「登录取自…」）",
                      accountList->item(0)->toolTip());
                check(accountList->currentRow() == 1,
                      "选中的还是「当前」那一条（离线账号同样可以是当前）",
                      QString::number(accountList->currentRow()));
                check(dialog.selectedUuid() == QStringLiteral("uuid-s"),
                      "selectedUuid() 给的是离线那一条", dialog.selectedUuid());
            }

            // ---- X10 信号接线 ----
            {
                int loginCount = 0;
                int refreshCount = 0;
                QObject::connect(&dialog, &AccountSkinDialog::loginRequested, &dialog,
                                 [&] { ++loginCount; });
                QObject::connect(&dialog, &AccountSkinDialog::refreshRequested, &dialog,
                                 [&] { ++refreshCount; });
                loginBtn->click();
                refreshBtn->click();
                check(loginCount == 1, "点「登录新账号…」发 loginRequested");
                check(refreshCount == 1, "点「刷新外观」发 refreshRequested");
                // NOTE: 「上传皮肤…」**故意不点** —— 它会开一个模态的 QFileDialog，
                // 在 offscreen 上会一直等下去（看着像死循环）。它那条路的两个半边
                // 各有落点：选项怎么读是 X1/X7，尺寸那条规矩在 verify 的 X4。
                dialog.close();
                QApplication::processEvents();
                check(!dialog.isVisible(), "点「关闭」之后窗口关掉了");
            }
        }
    }
    return 0;
}


