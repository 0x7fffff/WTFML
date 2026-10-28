#include "Support.hpp"
int UiSettingsLayoutChecks(MainWindowUI &ui, QTabWidget * &tabs) {

    {
        tabs->setCurrentIndex(MainWindowUI::settingsTabIndex());
        QApplication::processEvents();
        QWidget *settingsPage = tabs->widget(MainWindowUI::settingsTabIndex());
        check(settingsPage != nullptr &&
                  settingsPage->objectName() == MainWindowUI::settingsTabName(),
              "最后一页就是设置页", settingsPage != nullptr ? settingsPage->objectName()
                                                             : QString());

        // 下面这一段钉的就是那条对应关系，以及"每一栏**各自**套一层滚动区"这件事 ——
        // 滚动归栏管（整页一层的话，左栏那五条分类会跟着右栏的内容一起滚走，
        // 而那五条是导航）。
        //
        // NOTE: 每栏各有一张 3:7 的表（管的是栏内那一层）。
        auto *categoryList =
            settingsPage->findChild<QListWidget *>(MainWindowUI::settingsCategoryListName());
        auto *categoryStack =
            settingsPage->findChild<QStackedWidget *>(MainWindowUI::settingsCategoryStackName());
        check(categoryList != nullptr && categoryStack != nullptr,
              "设置页左栏是那列分类、右栏是堆叠页");
        const QStringList categoryKeys = MainWindowUI::settingsCategoryKeys();
        const QStringList categoryTexts = MainWindowUI::settingsCategoryTexts();
        check(categoryKeys.size() == categoryTexts.size() && !categoryKeys.isEmpty(),
              "分类的键与显示文字一条一条对齐",
              QStringLiteral("%1 / %2").arg(categoryKeys.size()).arg(categoryTexts.size()));
        if (categoryList != nullptr && categoryStack != nullptr) {
            check(categoryList->count() == categoryKeys.size() &&
                      categoryStack->count() == categoryKeys.size(),
                  "左栏条数 == 右栏页数 == 分类表的条数（三边对得上）",
                  QStringLiteral("%1 / %2 / %3")
                      .arg(categoryList->count())
                      .arg(categoryStack->count())
                      .arg(categoryKeys.size()));
            // 身份在 Qt::UserRole（条目文字是显示内容、会随文案改），文字也得和那张表一样。
            bool itemsOk = categoryList->count() == categoryKeys.size();
            for (int i = 0; itemsOk && i < categoryList->count(); ++i) {
                itemsOk =
                    categoryList->item(i)->data(Qt::UserRole).toString() == categoryKeys.at(i) &&
                    categoryList->item(i)->text() == categoryTexts.at(i);
            }
            check(itemsOk, "每条分类的键（userData）与文字都和那张表一一对应");

            // 每一页：objectName == settingsPanelName(key)，它自己就是一层滚动区（跟着宽度变），
            // 里面那块 form 的 objectName == settingsFormName(key)、表格是 3:7 两列。
            int pagesOk = 0;
            int emptyPages = 0;
            int formWidthOk = 0;
            int scrollAlignOk = 0;
            for (int i = 0; i < categoryStack->count() && i < categoryKeys.size(); ++i) {
                const QString key = categoryKeys.at(i);
                QWidget *panel = categoryStack->widget(i);
                auto *pageScroll = qobject_cast<QScrollArea *>(panel);
                QWidget *pageForm = pageScroll != nullptr ? pageScroll->widget() : nullptr;
                auto *pageGrid = pageForm != nullptr
                                     ? qobject_cast<QGridLayout *>(pageForm->layout())
                                     : nullptr;
                const bool ok = pageScroll != nullptr &&
                                panel->objectName() == MainWindowUI::settingsPanelName(key) &&
                                pageScroll->widgetResizable() && pageForm != nullptr &&
                                pageForm->objectName() == MainWindowUI::settingsFormName(key) &&
                                pageGrid != nullptr &&
                                pageGrid->columnStretch(0) == MainWindowUI::leftColumnStretch() &&
                                pageGrid->columnStretch(1) == MainWindowUI::rightColumnStretch();
                pagesOk += ok ? 1 : 0;
                // 每栏都得真有东西：那一栏里至少摆着一个选项名（addSettingRow 落下的 QLabel）。
                emptyPages += (pageForm != nullptr && !pageForm->findChildren<QLabel *>().isEmpty())
                                  ? 0
                                  : 1;
                formWidthOk += (pageForm != nullptr &&
                                pageForm->maximumWidth() == MainWindowUI::settingsFormMaxWidth())
                                   ? 1
                                   : 0;
                scrollAlignOk += (pageScroll != nullptr &&
                                  pageScroll->alignment() == (Qt::AlignHCenter | Qt::AlignTop))
                                     ? 1
                                     : 0;
            }
            check(pagesOk == categoryKeys.size(),
                  "每一栏都是「一层滚动区 + 一张 3:7 的表格」，objectName 按键拼出来",
                  QStringLiteral("%1/%2").arg(pagesOk).arg(categoryKeys.size()));
            check(emptyPages == 0, "没有空栏（每一栏里都摆着东西）",
                  QStringLiteral("空的 %1 栏").arg(emptyPages));
            check(formWidthOk == categoryKeys.size(),
                  "★ 每一栏那张表单都限了宽（`settingsFormMaxWidth()`）—— 窗口再宽它也不跟着长，"
                  "「说明文字一行跨得太长」那条就是靠这个收住的",
                  QStringLiteral("%1/%2（上限 %3 px）")
                      .arg(formWidthOk)
                      .arg(categoryKeys.size())
                      .arg(MainWindowUI::settingsFormMaxWidth()));
            check(scrollAlignOk == categoryKeys.size(),
                  "★ 滚动区把表单**居上居中**（`AlignHCenter | AlignTop`）—— 限了宽之后它得"
                  "自己站中间，不然宽窗口下表格整块贴着左边、右边空一大片",
                  QStringLiteral("%1/%2").arg(scrollAlignOk).arg(categoryKeys.size()));

            // 每一栏里的排法（★ 起）：一栏 = 一串**小节卡片** ——
            // 用户那份改版文档原话：「小节使用"标题 + 内容卡片"，避免传统 `QGroupBox` 的
            // 缺口式标题」。于是"上下结构"这件事从"表格里相邻两行"变成了**卡片内部**的形状：
            // · 表格那一层：每一格都从列 0 起、跨两列（卡片整幅宽），没有一格只占一列；
            // · 卡片那一层：顶上第一行是**小节标题**（粗体 + sectionTitle 那一档角色），
            // 之后每一项是「行名标签 → 控件」上下相邻、左边缘对齐的一对。
            //
            // 判据一（形状）：**没有任何一格只占一列**（`getItemPosition` 拿到的是布局真正的
            // 位置与跨度，不是"谁挨着谁"推出来的）。一条这样的格都不许有 —— 有一格在列 1 上，
            // 就说明还有一行是老样子。
            // 判据二（几何）：卡片里每一对「名字 / 控件」的，名字的**下边缘不越过控件的上边缘**、
            // 且两者的左边缘对齐（光看列号看不出来"名字在上面那一行"）。
            int singleColumnItems = 0;
            int cardsChecked = 0;
            int cardShapeBad = 0;
            int pairsChecked = 0;
            int pairsBad = 0;
            for (int i = 0; i < categoryStack->count() && i < categoryKeys.size(); ++i) {
                QWidget *panel = categoryStack->widget(i);
                auto *pageScroll = qobject_cast<QScrollArea *>(panel);
                QWidget *pageForm = pageScroll != nullptr ? pageScroll->widget() : nullptr;
                auto *pageGrid = pageForm != nullptr
                                     ? qobject_cast<QGridLayout *>(pageForm->layout())
                                     : nullptr;
                if (pageGrid == nullptr) {
                    continue;
                }
                for (int idx = 0; idx < pageGrid->count(); ++idx) {
                    int r = 0;
                    int c = 0;
                    int rowSpan = 0;
                    int colSpan = 0;
                    pageGrid->getItemPosition(idx, &r, &c, &rowSpan, &colSpan);
                    if (c != 0 || colSpan != 2) {
                        ++singleColumnItems;
                    }
                }
                // 表格里每一格都得是**一张小节卡片**（`wtfmlSurface="section"`），而且那一格
                // 里除了这张卡片没有别的东西 —— 「正文只有一个面、小节才用卡片」那条就落在
                // 这儿：卡片是正文面里唯一那种"再画一层底"的东西。
                for (int r = 0; r < pageGrid->rowCount(); ++r) {
                    QLayoutItem *cardItem = pageGrid->itemAtPosition(r, 0);
                    auto *card = cardItem != nullptr ? qobject_cast<QFrame *>(cardItem->widget())
                                                     : nullptr;
                    if (card == nullptr) {
                        continue;
                    }
                    ++cardsChecked;
                    const bool isSection =
                        card->property(Theme::surfacePropertyName().toUtf8().constData())
                            .toString() == Theme::sectionSurfaceValue();
                    auto *cardLayout = qobject_cast<QVBoxLayout *>(card->layout());
                    // 卡片自己那一圈内边距 / 行距是视觉规格（16,16,16,16 / 10）——
                    // 它决定"这一节里的项离卡片边多远"，改了看起来只是松紧，只有断言能钉。
                    // 规格是（16,16,16,16 / 10），
                    // 随"页面外边距 20、区块间距 16"那一档一起。
                    const QMargins want(16, 16, 16, 16);
                    if (!isSection || cardLayout == nullptr ||
                        cardLayout->contentsMargins() != want || cardLayout->spacing() != 10) {
                        ++cardShapeBad;
                    }
                    if (cardLayout == nullptr) {
                        continue;
                    }
                    // 第一行是小节标题（粗体 + sectionTitle 角色），后面才是项。
                    bool titleOk = false;
                    QWidget *first = cardLayout->count() > 0
                                         ? cardLayout->itemAt(0)->widget()
                                         : nullptr;
                    if (auto *titleLabel = qobject_cast<QLabel *>(first)) {
                        const bool roleOk =
                            titleLabel->property(Theme::textRolePropertyName().toUtf8().constData())
                                .toString() == Theme::sectionTitleTextRoleValue();
                        titleOk = titleLabel->font().bold() && roleOk;
                    }
                    if (!titleOk) {
                        ++cardShapeBad;
                    }
                    // 之后是成对的「行名标签 → 控件」：名字在上面、控件在下面、左边缘对齐。
                    QLabel *pendingLabel = nullptr;
                    for (int k = 1; k < cardLayout->count(); ++k) {
                        QWidget *w = cardLayout->itemAt(k)->widget();
                        if (w == nullptr) {
                            continue;
                        }
                        if (auto *label = qobject_cast<QLabel *>(w)) {
                            // 一句说明（"说明文字"那种）也是 QLabel：它跟在上一个控件后面，
                            // 不是下一项的名字 —— 判据还是"名字的下边缘不越过控件的上边缘"，
                            // 所以这里一律把它当成"下一个可能的名字"，逐对去量。
                            pendingLabel = label;
                            continue;
                        }
                        if (pendingLabel != nullptr) {
                            ++pairsChecked;
                            const QRect labelRect = pendingLabel->geometry();
                            const QRect controlRect = w->geometry();
                            if (!(labelRect.bottom() <= controlRect.top() &&
                                  labelRect.left() == controlRect.left())) {
                                ++pairsBad;
                            }
                            pendingLabel = nullptr;
                        }
                    }
                }
            }
            check(singleColumnItems == 0,
                  "每一栏里的行都是上下结构（没有一格只占一列 ⇒ 没有「名字在左、内容在右」的行）",
                  QStringLiteral("有 %1 格只占一列").arg(singleColumnItems));
            check(cardsChecked > 0 && cardShapeBad == 0,
                  "每一栏里那一格格都是「标题 + 内容卡片」（section 面 + 18/17/18/18 + 13 行距 "
                  "+ 粗体标题）",
                  QStringLiteral("量了 %1 张卡片，形状不对的 %2 张")
                      .arg(cardsChecked)
                      .arg(cardShapeBad));
            check(pairsChecked > 0 && pairsBad == 0,
                  "卡片里行名那一行的下面就是内容，且两者左边缘对齐（真的「上下」）",
                  QStringLiteral("量了 %1 张卡片、%2 对里错了 %3 对")
                      .arg(cardsChecked)
                      .arg(pairsChecked)
                      .arg(pairsBad));

            // 点第 N 条 = 翻到第 N 页。挑一条**不是当前那条**的来点才量得出"真的换了"。
            const int probe = categoryList->count() > 1 ? categoryList->count() - 1 : 0;
            categoryList->setCurrentRow(probe);
            QApplication::processEvents();
            check(categoryStack->currentIndex() == probe, "点左栏第 N 条，右栏就翻到第 N 页",
                  QStringLiteral("点了第 %1 条 → 停在第 %2 页")
                      .arg(probe)
                      .arg(categoryStack->currentIndex()));
            // 量完还回去：后面的断言和镜头看的是第一栏。
            categoryList->setCurrentRow(0);
            QApplication::processEvents();

            check(categoryList->mapTo(settingsPage, QPoint(0, 0)).x() <
                      categoryStack->mapTo(settingsPage, QPoint(0, 0)).x(),
                  "分类列表在堆叠页左边");
        }

        // 二十四个设置控件都得能按 objectName 找回来（MainWindow 靠名字取它们，
        // 名字掉了就是"设置项点了没反应"）。
        // NOTE: 这一条是**递归**找的，所以设置页分栏对它没有影响 —— 控件
        // 换了哪一栏、套了几层容器都不管，只要还在设置页底下、名字没丢。真正受分栏
        // 影响的是上面那组"落在第 1 列"的断言（它们要认出所在那一栏）。
        const QStringList settingNames = {
            // 「外观」那一格。
            MainWindowUI::titleTextEditName(),
            MainWindowUI::accountSkinSettingsButtonName(),
            MainWindowUI::javaManageButtonName(),      MainWindowUI::javaPathLabelName(),
            MainWindowUI::javaWarnMismatchCheckName(),
            MainWindowUI::memoryHintName(),
            MainWindowUI::memoryMaxSpinName(),         MainWindowUI::memoryMaxSliderName(),
            MainWindowUI::memoryModeComboName(),
            MainWindowUI::launcherBehaviourComboName(), MainWindowUI::extraJvmArgsEditName(),
            MainWindowUI::applyExtraJvmArgsButtonName(),
            MainWindowUI::downloadSourceComboName(),
            // 「同时最多下几个」：和「下载源」同一组（这一批文件怎么走）。
            MainWindowUI::downloadConcurrencyComboName(),
            // CurseForge 的 API key：和「下载源」同一组，可留空。
            MainWindowUI::curseForgeApiKeyEditName(),
            MainWindowUI::versionNameCustomCheckName(),
            MainWindowUI::versionNameRealCheckName(),
            MainWindowUI::versionNameLoaderCheckName(),
            MainWindowUI::versionNameLoaderVersionCheckName(),
            // 「启动选项」那一组。
            MainWindowUI::showGameLogCheckName(),      MainWindowUI::processPriorityComboName(),
            MainWindowUI::gameWindowWidthSpinName(),   MainWindowUI::gameWindowHeightSpinName(),
            // 「窗口模式」那一格：和上面那两格是一组。
            MainWindowUI::gameWindowModeComboName(),
            // 「启动提醒」那一组：启动前那两条确认各配一个开关。
            MainWindowUI::warnDuplicateLaunchCheckName(), MainWindowUI::warnWorldLossCheckName(),
            // 「下载的文件」那一组：从实例设置里点「下载新模组」时要不要问存哪儿。
            MainWindowUI::downloadAutoIntoInstanceCheckName()};
        QStringList missing;
        for (const QString &name : settingNames) {
            if (settingsPage->findChild<QWidget *>(name) == nullptr) {
                missing << name;
            }
        }
        check(missing.isEmpty(), "二十六个设置控件都能按 objectName 找回来", missing.join(", "));

        // 「CurseForge API key」那一格（摆在下拉框那一组里）。它和别处
        // 不同的地方有两条，都值得单独钉住：
        // - 它是**明文**（不是 QLineEdit::Password）。理由是贴错了要能一眼看见：它本来
        // 就明文躺在配置文件里，用户看不见的输入框只会让他把 key 贴错两次，
        // 而查的人（他自己）连"贴进去的是什么"都看不到；
        // - 它**可以留空**（空 = 走社区镜像），所以占位文字说的必须是"留空会怎样"，
        // 不是必填项那种"请输入…"。
        if (auto *cfKey =
                settingsPage->findChild<QLineEdit *>(MainWindowUI::curseForgeApiKeyEditName())) {
            check(cfKey->echoMode() == QLineEdit::Normal,
                  "CurseForge 那一格是明文显示的（贴错了得看得见）",
                  QString::number(static_cast<int>(cfKey->echoMode())));
            check(cfKey->placeholderText() == MainWindowUI::curseForgeApiKeyPlaceholder() &&
                      !cfKey->placeholderText().isEmpty(),
                  "空着的时候写着「留空则用公共接口」（这一格不是必填项）",
                  cfKey->placeholderText());
            check(cfKey->text().isEmpty(),
                  "刚搭好时框里是空的（值由 MainWindow 从盘上灌，界面层不自己猜）",
                  cfKey->text());
        }
        // 「下载新模组」那颗勾选框。要求：「在设置里加一个选项 …… 勾选->
        // 下载新模组就自动放进当前实例文件夹，直接在下载界面下的照样弹explorer选择保存位置，
        // 不勾选->不论是在哪里进入的下载页都弹explorer选择保存位置」。
        //
        // 界面层这里只钉三件**界面层管得着**的：它在不在、默认是不是勾上（用户要的默认是
        // 勾上）、以及那两行字是不是把两种情形都说清了。「勾上之后到底问不问」是 MainWindow
        // 那边算的（见 ），这里量不了。
        if (auto *autoCheck = ui.getDownloadAutoIntoInstanceCheck()) {
            // **界面层不自己猜**（和上面 CurseForge 那一格同一条规矩）：刚搭好时是**没勾**的，
            // 值由 MainWindow 从盘上灌（见 applySettingsToUi）。"默认勾上"是**设置那一层**的
            // 默认值（LauncherSettings::downloadAutoIntoInstance()），在 mw_check 里钉着 ——
            // 在这一层断言"默认勾上"等于要求界面层自己编一个默认值出来。
            check(!autoCheck->isChecked(),
                  "刚搭好时那颗是没勾的（值由 MainWindow 灌，界面层不自己猜）");
            check(autoCheck->text() == MainWindowUI::downloadAutoIntoInstanceCheckText() &&
                      !autoCheck->text().isEmpty(),
                  "那一格的字（勾上管的是哪一件事）", autoCheck->text());
            // 两行说明：勾上和没勾**各自**会怎样都要写出来。只写一半的话，用户看到"自动放进
            // 当前实例"会以为"在下载页点也一样"，而实际上下载页那一栏照样问。
            //
            // 那两行是这一格**同一个盒子**里的 QLabel（`addHint` 塞进 makeHintedControl 的那个
            // 竖排里）—— 没有 objectName 可取，所以从勾选框的父控件往下捞。
            QString hint;
            if (QWidget *box = autoCheck->parentWidget()) {
                for (QLabel *label : box->findChildren<QLabel *>()) {
                    hint += label->text();
                    hint += QLatin1Char('\n');
                }
            }
            check(!hint.isEmpty() &&
                      hint.contains(MainWindowUI::downloadAutoIntoInstanceRowText()),
                  "说明里点到了「下载新模组」这件事", hint.trimmed());
            check(hint.contains(QStringLiteral("不管从哪儿")),
                  "说明把**没勾**那一半也写了（只写勾上的那一半会让人以为下载页也不问）",
                  hint.trimmed());
        } else {
            check(false, "找得到「下载新模组」那颗勾选框");
        }

        // 「同时最多下几个」那颗下拉框。界面层这里只钉一件**界面层管得着**
        // 的事：**刚搭好时是一个空框**。四档那个表在 src/ 的 `DownloadConcurrency.hpp` 里
        // （唯一出处），界面层不许 include src/ ⇒ 项只能由 MainWindow 灌（和「下载源」
        // 「窗口模式」同一个形状）。在这一层断言"四档都在"等于要求界面层自己抄一份表出来。
        // （"四档都在、选中的那一档是盘上那个"由 mw_check 盯。）
        if (auto *concCombo = ui.getDownloadConcurrencyCombo()) {
            check(concCombo->count() == 0,
                  "刚搭好时那颗「并发数」是**空框**（项由 MainWindow 灌，界面层不自己抄表）",
                  QString::number(concCombo->count()));
            check(concCombo->objectName() == MainWindowUI::downloadConcurrencyComboName(),
                  "它挂的就是那个 objectName（验证工程按名字找它）", concCombo->objectName());
        } else {
            check(false, "找得到「并发数」那颗下拉框");
        }

        // 行标题也找一份：这是**静态文字**（没有 objectName 可取），只能用文字找。
        // 钉它的理由是"两句话说的是同一件事的两面"：行标题写的是这一格管什么、
        // 占位文字写的是不填会怎样 —— 行标题被改成别的东西，用户就不知道该填什么了。
        {
            bool rowFound = false;
            for (QLabel *label : settingsPage->findChildren<QLabel *>()) {
                if (label->text() == MainWindowUI::curseForgeApiKeyRowText()) {
                    rowFound = true;
                    break;
                }
            }
            check(rowFound, "设置页上真的摆着那一行（行标题就是那一句）",
                  MainWindowUI::curseForgeApiKeyRowText());
        }

        // 「外观」那一格那个文本框。**长度上限那两边各写一遍**
        // （ui/ 不许 include src/），所以这条等号是唯一盯着它们的地方 —— 和上面那对窗口
        // 大小的范围同一个约定。不一致的症状是"打上去的字自己少了几个"。
        if (auto *titleEdit =
                settingsPage->findChild<QLineEdit *>(MainWindowUI::titleTextEditName())) {
            check(titleEdit->maxLength() == LauncherSettings::titleTextMaxLength() &&
                      MainWindowUI::titleTextMaxLength() == LauncherSettings::titleTextMaxLength(),
                  "★ 「标题文字」的输入长度上限 == LauncherSettings 的截断（两边是同一个数）",
                  QString("%1 vs %2").arg(titleEdit->maxLength())
                      .arg(LauncherSettings::titleTextMaxLength()));
            check(titleEdit->placeholderText() == TitleBar::defaultBrandText(),
                  "空着的时候占位文字就是默认那五个字母（不给一个不知所以的空框）",
                  titleEdit->placeholderText());
            check(titleEdit->text().isEmpty(),
                  "刚搭好时框里是空的（条上用的是默认值；值由 MainWindow 灌）",
                  titleEdit->text());
        }

        // 「提示」栏里那两条启动前的确认（分栏时并到一栏）。
        // 这里钉的是**这一组特有的东西** —— 两句文案各自只此一处、行标题是用户点名的
        // 那两个词（「重复启动」/「进度提醒」，提示框上那句小字就是照着它们指路的，
        // 所以这两个词一改，小字就指错地方了）。控件本身能不能找回来在上面那条
        // "二十四个设置控件"里；勾选状态由 MainWindow 从盘上灌，界面层不自己猜。
        {
            auto *duplicate = settingsPage->findChild<QCheckBox *>(
                MainWindowUI::warnDuplicateLaunchCheckName());
            auto *worldLoss = settingsPage->findChild<QCheckBox *>(
                MainWindowUI::warnWorldLossCheckName());
            check(duplicate != nullptr && worldLoss != nullptr,
                  "找得到「提示」栏里那两个勾选框");
            if (duplicate != nullptr && worldLoss != nullptr) {
                check(duplicate->text() == MainWindowUI::warnDuplicateLaunchCheckText(),
                      "「重复启动」那一格的字就是那唯一一处", duplicate->text());
                check(worldLoss->text() == MainWindowUI::warnWorldLossCheckText(),
                      "「进度提醒」那一格的字就是那唯一一处", worldLoss->text());
                check(!duplicate->isChecked() && !worldLoss->isChecked(),
                      "刚搭好时两个都没勾（值是 MainWindow 从盘上灌的，界面不自己猜）");
                // 两个勾选框都在右边那一列、各占自己那一行 —— 这一组的形状和「启动选项」
                // 那几格一样（控件在上、说明在下，装着它们的那一格在右列）。
                check(duplicate->parentWidget() != worldLoss->parentWidget(),
                      "两条各占一行（不是挤在同一格里）");
            }
        }
        // 那三个词得真在表单上画着：行标题由 `addSettingRow()` 落成一个 QLabel，
        // 这里按文字去找 —— 改行标题的人会当场看到这条红。
        //
        // NOTE: 分栏之后这三条并进「提示」栏，组标题也随之改名 —— 三条管的是同一件事
        // （"启动那一下要不要再问一句"）。
        auto *warnForm = settingsPage->findChild<QWidget *>(
            MainWindowUI::settingsFormName(QStringLiteral("warn")));
        check(warnForm != nullptr, "「提示」栏那张表单找得到",
              MainWindowUI::settingsFormName(QStringLiteral("warn")));
        if (warnForm != nullptr) {
            const QList<QLabel *> labelsInForm = warnForm->findChildren<QLabel *>();
            const auto hasLabelText = [&labelsInForm](const QString &text) {
                for (const QLabel *label : labelsInForm) {
                    if (label->text() == text) {
                        return true;
                    }
                }
                return false;
            };
            check(hasLabelText(MainWindowUI::javaWarnMismatchRowText()) &&
                      hasLabelText(MainWindowUI::warnDuplicateLaunchRowText()) &&
                      hasLabelText(MainWindowUI::warnWorldLossRowText()),
                  "三条的行标题都在表单上（提示框那行小字指的就是它们）",
                  MainWindowUI::javaWarnMismatchRowText() + QStringLiteral(" / ") +
                      MainWindowUI::warnDuplicateLaunchRowText() + QStringLiteral(" / ") +
                      MainWindowUI::warnWorldLossRowText());
            check(hasLabelText(QStringLiteral("安全提醒")),
                  "这一栏的组标题「安全提醒」也画出来了");
            check(!hasLabelText(QStringLiteral("启动提醒")),
                  "老名字「启动提醒」不再画（改名之后不许两处都在）");
        }

        // 设置项外面套了两层（"控件 + 说明文字"的容器、再外面一层小节卡片），所以要把控件
        // 往上提到**它所在那一栏**直接挂在表单上的那一层，才问得到它在第几列。
        // NOTE: 分栏之后"表单"不止一张（每栏一张，objectName = settingsForm-<键>），所以
        // 这几个 helper 都改成"先认出控件落在哪一栏，再问那一栏的表格"。
        const auto formOf = [](QWidget *w) -> QWidget * {
            const QString prefix = QStringLiteral("settingsForm-");
            for (QWidget *p = w; p != nullptr; p = p->parentWidget()) {
                if (p->objectName().startsWith(prefix)) {
                    return p;
                }
            }
            return nullptr;
        };
        const auto gridOf = [](QWidget *form) -> QGridLayout * {
            return form != nullptr ? qobject_cast<QGridLayout *>(form->layout()) : nullptr;
        };
        /// 设置项现在住在**小节卡片**里（`addSettingsSection()` 造的那张 `QFrame`，
        /// 面挂在属性 `wtfmlSurface="section"` 上）——「行名 + 内容」那一对是它那条竖排
        /// 布局里的相邻两项。所以"名字在控件上面"这句话，去卡片里数顺序才对得上。
        const auto cardOf = [](QWidget *w) -> QFrame * {
            const QByteArray key = Theme::surfacePropertyName().toUtf8();
            for (QWidget *p = w; p != nullptr; p = p->parentWidget()) {
                auto *frame = qobject_cast<QFrame *>(p);
                if (frame != nullptr &&
                    frame->property(key.constData()).toString() ==
                        Theme::sectionSurfaceValue()) {
                    return frame;
                }
            }
            return nullptr;
        };
        const auto cardLayoutOf = [](QFrame *card) -> QVBoxLayout * {
            return card != nullptr ? qobject_cast<QVBoxLayout *>(card->layout()) : nullptr;
        };
        /// 控件在卡片里那一条竖排布局上占的那一格（控件自己多半还套着"控件 + 说明"的容器）。
        const auto cardItemOf = [&cardOf](QWidget *w) -> QWidget * {
            QFrame *card = cardOf(w);
            if (card == nullptr) {
                return nullptr;
            }
            QWidget *item = w;
            while (item != nullptr && item->parentWidget() != card) {
                item = item->parentWidget();
            }
            return item;
        };
        /// 卡片里**紧挨在控件上面**的那一项，是标签就还回去 —— "行名在内容上面"的那一发。
        const auto labelAbove = [&cardOf, &cardLayoutOf, &cardItemOf](QWidget *w) -> QLabel * {
            QFrame *card = cardOf(w);
            QVBoxLayout *layout = cardLayoutOf(card);
            QWidget *item = cardItemOf(w);
            if (layout == nullptr || item == nullptr) {
                return nullptr;
            }
            const int index = layout->indexOf(item);
            if (index < 1) {
                return nullptr;
            }
            QLayoutItem *above = layout->itemAt(index - 1);
            return above != nullptr ? qobject_cast<QLabel *>(above->widget()) : nullptr;
        };

        {
            const QList<QWidget *> controls = {
                ui.getJavaManageButton(),          ui.getMemoryHint(),
                ui.getMemoryMaxSpin(),             ui.getMemoryMaxSlider(),
                ui.getLauncherBehaviourCombo(),    ui.getExtraJvmArgsEdit(),
                ui.getDownloadSourceCombo(),       ui.getVersionNameCustomCheck(),
                ui.getShowGameLogCheck(),          ui.getProcessPriorityCombo(),
                ui.getGameWindowWidthSpin(),       ui.getGameWindowHeightSpin(),
                ui.getWarnDuplicateLaunchCheck(),  ui.getWarnWorldLossCheck(),
                ui.getTitleTextEdit()};
            int inCard = 0;
            int orphaned = 0;
            QSet<QWidget *> seenCards;
            int cardSpanBad = 0;
            for (QWidget *control : controls) {
                if (control == nullptr) {
                    continue;
                }
                if (formOf(control) == nullptr) {
                    ++orphaned;
                }
                QFrame *card = cardOf(control);
                if (card != nullptr) {
                    ++inCard;
                }
                if (card == nullptr || seenCards.contains(card)) {
                    continue;
                }
                seenCards.insert(card);
                // 卡片自己按 `grid->addWidget(card, row, 0, 1, 2)` 摆：从列 0 起、跨两列。
                QGridLayout *grid = gridOf(formOf(card));
                const int index = grid != nullptr ? grid->indexOf(card) : -1;
                int row = -1;
                int col = -1;
                int rowSpan = 1;
                int colSpan = 1;
                if (index >= 0) {
                    grid->getItemPosition(index, &row, &col, &rowSpan, &colSpan);
                }
                if (!(col == 0 && colSpan == 2)) {
                    ++cardSpanBad;
                }
            }
            check(orphaned == 0,
                  "每个设置控件都落在某一栏那张按名字取的表格里（没有谁挂在没名字的格子里）",
                  QString("%1/%2").arg(orphaned).arg(controls.size()));
            check(inCard == controls.size(),
                  "每个设置控件都长在某一张小节卡片里（wtfmlSurface=\"section\"）",
                  QString("%1/%2").arg(inCard).arg(controls.size()));
            check(seenCards.size() >= 4 && cardSpanBad == 0,
                  "小节卡片都从列 0 起、跨两列（一页里的卡片各占整行一张）",
                  QString("cards=%1 bad=%2").arg(seenCards.size()).arg(cardSpanBad));

            // 「最大内存」那一行：条 / 数字框和它们下面那句说明装在**同一个容器**里
            // （说明在容器最后）。★ ：「分配方式」那一行撤掉之后，这句说明并到了
            // 这里 —— 它现在说的就是这根条（系统内存 + 当前给到多少）。
            check(ui.getMemoryHint() != nullptr && ui.getMemoryMaxSlider() != nullptr &&
                      ui.getMemoryHint()->parentWidget() ==
                          cardItemOf(ui.getMemoryMaxSlider()),
                  "「最大内存」下面的说明和那根条装在同一个容器里（说明跟着条走）",
                  ui.getMemoryHint() != nullptr
                      ? QString("hint.parent=%1 item=%2")
                            .arg(ui.getMemoryHint()->parentWidget() != nullptr
                                     ? ui.getMemoryHint()->parentWidget()->objectName()
                                     : QStringLiteral("<null>"))
                            .arg(cardItemOf(ui.getMemoryMaxSlider()) != nullptr
                                     ? cardItemOf(ui.getMemoryMaxSlider())->objectName()
                                     : QStringLiteral("<null>"))
                      : QStringLiteral("<null>"));
            if (ui.getMemoryHint() != nullptr && ui.getMemoryMaxSlider() != nullptr &&
                ui.getMemoryHint()->parentWidget() != nullptr) {
                // 条和说明的父控件不同（条在那一行的 line 里），所以先把条的下边沿换算到
                // 说明的父容器里再比 y —— 直接减两边的 y 是两套坐标。
                QWidget *hintParent = ui.getMemoryHint()->parentWidget();
                const QPoint sliderBottom = ui.getMemoryMaxSlider()->mapTo(
                    hintParent, QPoint(0, ui.getMemoryMaxSlider()->height()));
                check(ui.getMemoryHint()->y() >= sliderBottom.y(),
                      "那句说明量出来的几何在条**下面**（不是并排）",
                      QString("bar.bottom=%1 hint.y=%2")
                          .arg(sliderBottom.y())
                          .arg(ui.getMemoryHint()->y()));
            }

            // 拿「最大内存」那一行验几何：**行名在卡片里紧挨着控件上面那一项**，
            // 两者左边缘对齐 —— 这才是"上下结构"，并排的两列长得不是这样。
            QWidget *maxItem = cardItemOf(ui.getMemoryMaxSpin());
            auto *maxLabel = labelAbove(ui.getMemoryMaxSpin());
            check(cardOf(ui.getMemoryMaxSpin()) != nullptr && maxLabel != nullptr &&
                      maxLabel->text() == QStringLiteral("最大内存"),
                  "「最大内存」那一行的上面一项是选项名称、下面一项才是控件",
                  maxLabel != nullptr ? maxLabel->text() : QStringLiteral("<null>"));
            check(maxLabel != nullptr && maxItem != nullptr && maxLabel->x() == maxItem->x() &&
                      maxLabel->y() + maxLabel->height() <= maxItem->y(),
                  "选项名称量出来的几何在控件**上面**，且左边缘对齐",
                  maxLabel != nullptr
                      ? QString("label=(%1,%2 %3x%4) control=(%5,%6)")
                            .arg(maxLabel->x())
                            .arg(maxLabel->y())
                            .arg(maxLabel->width())
                            .arg(maxLabel->height())
                            .arg(maxItem != nullptr ? maxItem->x() : -1)
                            .arg(maxItem != nullptr ? maxItem->y() : -1)
                      : QString());
        }

        // 「最大内存」那一行现在是**一根条 + 一个数字框**：两件东西说的是
        // 同一个数，范围就必须是同一个，而且和 GlobalJavaSettings 的钳位对得上 ——
        // "填了不生效"和"拖到头和打得进去的不是一个数"都是同一件事两个答案。
        // 「最小内存」那一格从这一页撤掉了（用户要求），下限 128 那条跟着撤走的断言。
        check(ui.getMemoryMaxSpin()->minimum() == 256 &&
                  ui.getMemoryMaxSlider()->minimumMb() == 256,
              "最大内存的下限 256（数字框和条说的是同一个数）",
              QString("%1 / %2").arg(ui.getMemoryMaxSpin()->minimum())
                  .arg(ui.getMemoryMaxSlider()->minimumMb()));
        check(ui.getMemoryMaxSpin()->maximum() == MainWindowUI::memoryLimitMaxMb() &&
                  ui.getMemoryMaxSlider()->maximumMb() == MainWindowUI::memoryLimitMaxMb(),
              "上限都是 65536（界面单独搭出来时的天花板 —— 真跑起来 MainWindow 会按这台机器"
              "的物理内存再收一次口，见 MainWindow::applyMemoryLimits）",
              QString("%1 / %2").arg(ui.getMemoryMaxSpin()->maximum())
                  .arg(ui.getMemoryMaxSlider()->maximumMb()));
        check(ui.getMemoryMaxSpin()->singleStep() == MainWindowUI::memoryStepMb(),
              "步长是 256（拖一格不会拖出个奇怪的值）",
              QString::number(ui.getMemoryMaxSpin()->singleStep()));
        check(ui.getMemoryModeCombo() != nullptr &&
                  ui.getMemoryModeCombo()->count() == 2 &&
                  ui.getMemoryModeCombo()->itemText(0) == QStringLiteral("自动分配") &&
                  ui.getMemoryModeCombo()->itemText(1) == QStringLiteral("手动分配") &&
                  !ui.getMemoryModeCombo()->currentData().toBool(),
              "「内存分配」最右边那颗下拉框只有自动 / 手动两项，默认手动",
              ui.getMemoryModeCombo() != nullptr
                  ? QString("%1 项 / 当前 %2")
                        .arg(ui.getMemoryModeCombo()->count())
                        .arg(ui.getMemoryModeCombo()->currentText())
                  : QStringLiteral("<没有下拉框>"));

        // 档位表本身（纯函数，不用真去点一根条 —— 点条只能验到"拖动是通的"，验不到
        // "每一格都是该有的那个数"）。见 MemorySlider.hpp 里那段。
        {
            const QList<int> steps =
                MemorySlider::valueSteps(256, MainWindowUI::memoryLimitMaxMb());
            bool increasing = steps.size() >= 2;
            bool onGrid = true;
            for (int i = 0; i < steps.size(); ++i) {
                if (i > 0 && steps.at(i) <= steps.at(i - 1)) {
                    increasing = false;
                }
                // 每一格都落在 256 的整数倍上；**最右端那一格除外** —— 上限本身不在格上时
                // 它就停在那个数上（见 valueSteps 里那段：不然最右边够不到上限）。
                // 每一格都得落在**最细那一档**（128 MB）的整数倍上；最右端那一格除外
                // —— 上限本身不在格上时它就停在那个数上（见 valueSteps 里那段：不然最
                // 右边够不到上限）。NOTE: 判据不是 256 —— 2 GB 以下那一带的步长就是 128
                // （256 → 384 → 512…），拿 256 当判据是把"最小的那一档"当成"最大的那一档"
                // 去用了。
                if (i + 1 < steps.size() && steps.at(i) % 128 != 0) {
                    onGrid = false;
                }
            }
            check(increasing && steps.constFirst() == 256 &&
                      steps.constLast() == MainWindowUI::memoryLimitMaxMb(),
                  "档位表：升序、没有重复项、两头就是下限和上限",
                  QString("%1 格，%2 … %3").arg(steps.size()).arg(steps.constFirst())
                      .arg(steps.constLast()));
            check(onGrid, "档位表：每一格都是 128 的整数倍（最右端那一格可以是上限本身）");
            // 步长随值变大 —— 这才是"灵活变通"：小内存 128 一格（4 GB 的机器上 1152 /
            // 1280 那些档位是真有人用的），大内存 512 一格（32 GB 那台上不然要划两百多格）。
            check(MemorySlider::valueSteps(256, 2048).at(1) - 256 == 128 &&
                      MemorySlider::valueSteps(8192, 10240).at(1) - 8192 == 512,
                  "档位：2 GB 以下 128 一格、8 GB 以上 512 一格",
                  QString("%1 / %2").arg(MemorySlider::valueSteps(256, 2048).at(1) - 256)
                      .arg(MemorySlider::valueSteps(8192, 10240).at(1) - 8192));
            // 就近吸附：同样近时取**小**的那一格（往下压比往上顶安全：内存给大了 JVM
            // 直接起不来，给小了只是慢一点）。
            check(MemorySlider::nearestIndex(QList<int>{1024, 1280}, 1152) == 0,
                  "同样近时吸附到小的那一格（1152 → 1024）");
            check(MemorySlider::nearestIndex(QList<int>{1024, 1280}, 1280) == 1,
                  "正好在格上就停在那一格");
            check(MemorySlider::nearestIndex(QList<int>(), 4096) == -1, "空表回 -1");
            // 上限比下限高不到一格：表里就是两个数，不会退化成一串重复值
            const QList<int> narrow = MemorySlider::valueSteps(256, 300);
            check(narrow.size() == 2 && narrow.constFirst() == 256 && narrow.constLast() == 300,
                  "上下限之间不到一格时，表里就是两个数（下限、上限）",
                  QString("%1 格").arg(narrow.size()));
        }

        // 自动档展示值：进度条跟着自动值走，圆圈（valueMb）不许跟着动。
        {
            MemorySlider autoProbe;
            autoProbe.setRangeMb(256, 16384);
            autoProbe.setValueMb(4096);
            autoProbe.setAutoValueMb(8192);
            check(autoProbe.valueMb() == 4096 && autoProbe.hasAutoValue() &&
                      autoProbe.autoValueMb() == 8192,
                  "自动展示值不挪圆圈：valueMb 仍是手动位置，autoValueMb 是自动值",
                  QString("圆圈 %1 / 自动 %2").arg(autoProbe.valueMb()).arg(autoProbe.autoValueMb()));
            autoProbe.clearAutoValue();
            check(!autoProbe.hasAutoValue() && autoProbe.valueMb() == 4096,
                  "清掉自动展示值之后回到手动画法，圆圈位置不变",
                  QString("圆圈 %1 / 自动 %2").arg(autoProbe.valueMb()).arg(autoProbe.autoValueMb()));
        }

        // JVM 参数那一格是 QPlainTextEdit 而不是 QLineEdit：参数列表天然是多行的。
        check(ui.getExtraJvmArgsEdit() != nullptr &&
                  ui.getExtraJvmArgsEdit()->parentWidget() != nullptr,
              "全局 JVM 参数是个多行输入框");
        check(ui.getExtraJvmArgsEdit() != nullptr && ui.getExtraJvmArgsEdit()->height() <= 96,
              "它只有三四行高（这一格放的是顺手加的几条，不是一整份命令行）",
              QString::number(ui.getExtraJvmArgsEdit() != nullptr
                                  ? ui.getExtraJvmArgsEdit()->height()
                                  : -1));
        // 界面只搭壳：下拉框的条目和值由 MainWindow 灌（见 mw_check）
        // NOTE: 起「Java 版本」那一格是颗按钮（`javaManageButton`）而不是
        // 下拉框了，所以它不再出现在这一串"刚搭好时是空的"里。
        check(ui.getLauncherBehaviourCombo()->count() == 0 &&
                  ui.getDownloadSourceCombo()->count() == 0 &&
                  // 「进程优先级」尤其要在这儿：它是**界面里唯一一个条目由 MainWindow 灌的
                  // 下拉框**，而 applySettingsToUi() 里那句 addItem 是**不去重**的 ——
                  // 界面层哪天自己先灌一遍，用户看到的就是十档。
                  ui.getProcessPriorityCombo()->count() == 0 &&
                  ui.getMemoryMaxSpin()->value() == ui.getMemoryMaxSpin()->minimum(),
              "设置页刚搭好时全是空的（值由 MainWindow 从盘上灌进来）",
              QString("%1/%2/%3")
                  .arg(ui.getLauncherBehaviourCombo()->count())
                  .arg(ui.getDownloadSourceCombo()->count())
                  .arg(ui.getProcessPriorityCombo()->count()));
        check(settingsPage->findChild<QCheckBox *>(QStringLiteral("memoryAutoCheck")) == nullptr &&
                  ui.getMemoryModeCombo() != nullptr &&
                  ui.getMemoryModeCombo()->count() == 2,
              "设置页的自动/手动是最右边那颗两项下拉框（不是旧勾选框）",
              ui.getMemoryModeCombo() != nullptr
                  ? QString("%1 项").arg(ui.getMemoryModeCombo()->count())
                  : QStringLiteral("<没有下拉框>"));
        // 同一个形状，另一项：「实例指定的 Java 版本不符时提醒」这个总闸同样只搭壳。
        // NOTE: 产品里的默认值是"提醒"（true），但那条是**产品逻辑**、不是界面的事 ——
        // 界面层自己默认打开的话，MainWindow 万一忘了灌值就会显示一个假的"开"。
        check(ui.getJavaWarnMismatchCheck() != nullptr &&
                  !ui.getJavaWarnMismatchCheck()->isChecked(),
              "「实例指定的 Java 版本不符时提醒」刚搭好时也是未勾选的（值由 MainWindow 灌）");
        check(ui.getJavaWarnMismatchCheck() != nullptr &&
                  !ui.getJavaWarnMismatchCheck()->text().isEmpty(),
              "它也自带文字", ui.getJavaWarnMismatchCheck() != nullptr
                                  ? ui.getJavaWarnMismatchCheck()->text()
                                  : QString());
        // 那句说明要有占位文字：空标签会把行高压成 0，灌值的时候布局会跳一下。
        check(ui.getMemoryHint() != nullptr && !ui.getMemoryHint()->text().isEmpty(),
              "「最大内存」的说明有占位文字（免得布局跳）",
              ui.getMemoryHint() != nullptr ? ui.getMemoryHint()->text() : QString());
    }
 return 0;
}


