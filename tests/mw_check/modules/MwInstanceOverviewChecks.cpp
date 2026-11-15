#include "Support.hpp"

#include "InstanceUpgradePage.hpp"
int MwInstanceOverviewChecks(const QDir &root, const QDir &versionsDir) {
    // ------------------------------------------------------------------
    // 要求：「第一行 图标 名称 / 第二行 更改图标 分组… / 第三行 显示在置顶组
    // 中 / 第四行 导出」。
    //
    // 控件那一半（预览 + 八颗候选 + 两个浮层 + 勾选框的形状与初值）在 ui_check 的 U0h 里
    // 逐条验过；这里验**接线那一半**：页面上摆成什么样 → 磁盘上（wtfml.meta / wtfml.folders）
    // 变成什么样 → 那片实例树画成什么样。几行各有一处**只有这一层才可能错**的地方：
    //
    // · 分组：那一格的初值必须取**显示层**算出来的那一组（MainWindow::displayGroupIdOf），
    // 不能取"文件里存的那一格"（InstanceGroupStore::groupIdOf）—— 一个从来没表过态的
    // 实例，前者是它此刻显示在的那一组、后者是空的。取错的表现：用户点进设置页什么都
    // 不动就收页，它就被写进「未分组」，从此再也不会被自动归类。
    // · 置顶：勾上 / 取消那两条（**两边都显示**：老家那一条不动、置顶组里
    // 多一条同名的），以及"这一份 .minecraft 里根本没有置顶组"那一档 —— 那一档里勾选
    // **什么也不该发生**，尤其不该把用户明确选的那个组一起吞掉。
    // · 图标：三种形态（默认 / 内置候选 / 本地那张）在树上画的是不是同一张。判据一律拿
    // VersionIcons 现算出来比，并**同时要求它不等于原来那个默认图** —— 只比"等于某某"
    // 在两种形态恰好同图时会假绿。
    //
    // 驱动方式走**产品那条路**：openInstanceSettings → 在页面上摆控件（那一下就是写盘的
    // 那一下， 起这一页没有「确定」了）→ 点返回箭头收页（和 S2 那一节同一个
    // 形状）。每次都在**新开的窗**里读树：写盘之后数据层会发 groupsChanged 把
    // 树整个重灌（tree->clear()），同一扇窗里的旧指针是野的 —— 顺手也把"关掉再打开启动器"
    // 这条最接近用户真实动作的路一起验了。
    //
    // NOTE: 「选一张图片…」那一颗**故意不点**：它开的是 Windows 自己的文件选择框
    // （QFileDialog::getOpenFileName），模态、offscreen 下驱动不了 —— 和 ui_check 里
    // 「上传皮肤…」那一条逐字同一个理由。所以本地那张图这里验的是**读**侧（盘上躺着
    // 一张真的 PNG + meta 里写着 local ⇒ 树画的就是它；那张图坏了就退回默认），
    // 写侧（挑一张 → 拷进实例目录 → meta 写 local）只有人工过一遍才算验过。
    {
        const QString kId = QStringLiteral("1.20.1");
        const QString kModdable = QStringLiteral("default-moddable");  // 「可安装mod」
        const QString kVanilla = QStringLiteral("default-vanilla");    // 「原版」
        const QString kSnapshot = QStringLiteral("default-snapshot");  // 「快照版」
        const QString kPin = QStringLiteral("default-pinned");         // 「置顶」
        const QDir instanceDir(versionsDir.filePath(kId));
        const QString metaPath = instanceDir.filePath(QStringLiteral("wtfml.meta"));
        const QString iconPath = AppPaths::versionIconPath(kId);
        const QByteArray metaBefore = readBytesAt(metaPath);
        check(!metaBefore.isEmpty() && !QFile::exists(iconPath),
              "T18 前提：1.20.1 有一份 meta（里面写着 loaderName=Forge）、而且还没挑过图标");

        // 一份**五个默认组、一条归属都没有**的分组文件：不在这里自己拼 JSON，让产品种一遍
        // （文件不存在 ⇒ seedDefaultGroups()，T1 走的就是这条）。于是下面验的是**真的那一份**
        // 默认表，而不是这个文件里抄一遍的常量。
        const QString groupFile = AppPaths::instanceGroupFilePath();
        QDir().remove(groupFile);
        {
            MainWindow seeder;
            seeder.show();
            QApplication::processEvents();
        }
        check(QFile::exists(groupFile),
              "T18 前提：五个默认组种下去了（下面那个实例是**自动归类**进去的）");

        // 读盘那两把小尺子和 T17 里那两把逐字同款：**按值数、不按键找** —— 键是
        // instanceKey() 给的（有稳定 id 的按 id 存、没有的按目录名存），而"哪个实例已经有
        // 稳定 id"取决于它之前有没有被写过设置，这一节不该在键的形态上翻车。
        const auto membersInFile = [&groupFile]() -> QJsonObject {
            return QJsonDocument::fromJson(readBytesAt(groupFile))
                .object()
                .value(QStringLiteral("members"))
                .toObject();
        };
        const auto membersPointingAt = [&membersInFile](const QString &groupId) {
            int count = 0;
            const QJsonObject members = membersInFile();
            for (auto it = members.constBegin(); it != members.constEnd(); ++it) {
                if (it.value().toString() == groupId) {
                    ++count;
                }
            }
            return count;
        };
        const auto pinOriginInFile = [&groupFile]() -> QJsonObject {
            return QJsonDocument::fromJson(readBytesAt(groupFile))
                .object()
                .value(QStringLiteral("pinOrigin"))
                .toObject();
        };
        const auto originCount = [&pinOriginInFile](const QString &groupId) {
            int count = 0;
            const QJsonObject origins = pinOriginInFile();
            for (auto it = origins.constBegin(); it != origins.constEnd(); ++it) {
                if (it.value().toString() == groupId) {
                    ++count;
                }
            }
            return count;
        };

        // 新开一扇窗、读它那片树里**这个实例**那一行：返回行首那张图（headersOut 收组头）。
        // 边长取实例列表自己那一档（理由见 T17 那一段的 NOTE）。
        const int markSize = MainWindowUI::instanceTreeIconSize();
        const auto treeInNewWindow = [&](QStringList *headersOut) -> QImage {
            MainWindow w;
            w.show();
            QApplication::processEvents();
            QTreeWidget *tree = byObjectName<QTreeWidget>(&w, MainWindowUI::instanceListName());
            if (headersOut != nullptr) {
                *headersOut = groupHeadersOf(tree);
            }
            if (tree == nullptr) {
                return QImage();
            }
            for (QTreeWidgetItem *item : instanceItemsOf(tree)) {
                if (item->data(0, MainWindowUI::instanceIdRole()).toString() == kId) {
                    return item->icon(0).pixmap(markSize, markSize).toImage();
                }
            }
            return QImage();
        };
        // 这个实例**没挑过图标时**树上画的那一张（meta 里写着 loaderName=Forge）。下面凡是
        // "换了 / 没换"都拿它当参照物。
        const int forgeIndex =
            MainWindowUI::modLoaderOptionTexts().indexOf(QStringLiteral("Forge"));
        check(forgeIndex >= 0, "T18 前提：能问出 Forge 是第几档", QString::number(forgeIndex));
        const QImage forgeMark = VersionIcons::loaderMark(forgeIndex, markSize).toImage();
        const QImage grassMark = VersionIcons::grassBlock(markSize).toImage();
        check(!forgeMark.isNull() && !grassMark.isNull() && forgeMark != grassMark,
              "T18 前提：铁砧和草方块不是同一张图（下面那些「换了没有」才分得出来）");

        // 开这一页、在页面上摆一下、点返回箭头收页。`openInstanceSettings` 是**非阻塞**的
        // （见 OverlayHost::showPage），摆上去就返回；下面那一发 0 毫秒定时器排在
        // `processEvents()` 里跑，负责读初值、点浮层那几行、最后收页。tweak 拿到的是那一层
        // 浮层 + **那一页本身**（读初值、点浮层里那几行都从它上面走）+ 置顶勾选框。
        // 没找到的会是 nullptr。
        //
        // 这一页**没有「确定」了**（要求「那个取消和确定删了，修改了的
        // 设置实时写入」）—— 所以 tweak 里那一下**本身就是写盘的那一下**，下面那一发
        // 返回箭头只是把页面收掉。于是 tweak 必须用**真的会发信号**的动作：
        // `QCheckBox::setChecked()` 不是用户动作（只发 toggled、不发 clicked，接线接不到），
        // 要点就写 `click()`。S2b 那一条（改一格 → 恰好一发 settingsEdited）钉的就是
        // 这层因果。
        //
        // NOTE: 这一格是**一颗按钮 + 一个浮层**，读初值走 `chosenGroupId()`、改值走
        // **点浮层里那一行**（按名字找行，见 clickGroupRow）。那一页在 OverlayHost 里
        // 是 `QWidget *`，要往下转型才问得出这两件事。
        bool sawPage = false;
        QString comboValue;
        bool pinChecked = false;
        const auto editAndSave = [&](const QString &id, const auto &tweak) {
            // 三样先清掉（和上面那几个助手同一条规矩）：它们记的是"这一发弹了什么"，不清的话
            // 断言读到的是上一次留下的旧值 —— 那正是「多弹了一个」这条断言恒真的来路。
            g_modalTitle.clear();
            g_modalText.clear();
            g_modalWasWarning = false;
            sawPage = false;
            comboValue.clear();
            pinChecked = false;
            MainWindow w;
            w.show();
            QApplication::processEvents();
            QTimer::singleShot(0, [&] {
                OverlayHost *host = currentOverlay();
                if (host == nullptr) {
                    recordPrompt(nullptr);  // 没摆出那一页：下面那条断言会红，这里别崩
                    return;
                }
                sawPage = true;
                auto *page = host->findChild<InstanceSettingsDialog *>();
                auto *pin = host->findChild<QCheckBox *>(InstanceSettingsDialog::pinCheckName());
                if (page != nullptr) {
                    comboValue = page->chosenGroupId();
                }
                if (pin != nullptr) {
                    pinChecked = pin->isChecked();
                }
                tweak(host, page, pin);
                // 收页走**返回箭头**： 起这一页没有「确定」了（要求「那个
                // 取消和确定删了，修改了的设置实时写入」）—— 该写的东西在 tweak 里那一
                // 下就写完了，这一发只是把页面收掉（= reject，见 OverlayHost::showPage）。
                if (auto *back =
                        host->findChild<QPushButton *>(OverlayHost::overlayBackButtonName())) {
                    back->click();
                    return;
                }
                host->closeOverlay();  // 兜底：至少别让测试卡在这一层上
            });
            QMetaObject::invokeMethod(&w, "openInstanceSettings", Qt::DirectConnection,
                                      Q_ARG(QString, id));
            QApplication::processEvents();
        };
        // 点「分组…」那个浮层里**组名是 name 的那一行**（= 用户在这一格上改了一次归属）。
        //
        // NOTE: 那几行是**常驻**的（浮层一次建好、以后每次点按钮只是再弹一遍），所以不用
        // 先把浮层点开就能按名字找到它们；点了之后浮层自己会收起来。
        // NOTE: 当前那一行是**灰的**（点它等于什么都没做），`click()` 对禁用的按钮是空操作 ——
        // 所以"点了没换组"和"这一行本来就灰着"在下面撞不出来，断言要按这一条写。
        const auto clickGroupRow = [](InstanceSettingsDialog *page, const QString &name) {
            for (int i = 0; page != nullptr; ++i) {
                auto *row = page->findChild<QPushButton *>(
                    InstanceSettingsDialog::groupMenuRowName(i));
                if (row == nullptr) {
                    return false;
                }
                if (row->text() == name) {
                    row->click();
                    return true;
                }
            }
            return false;
        };

        // ---- ① 「分组…」那一格的初值 = **显示层**算出来的那一组；什么都没动 = 不写 ----
        editAndSave(kId, [](QWidget *, InstanceSettingsDialog *, QCheckBox *) {});
        check(sawPage, "T18 ① 实例设置页开出来了（下面那几条才有意义）");
        check(g_modalTitle.isEmpty(), "T18 ① 只有这一页，没有别的框跟着冒出来", g_modalTitle);
        check(comboValue == kModdable,
              "★ T18 ① 「分组…」那一格的初值是这个实例**此刻显示**在的那一组 —— 它 meta 里写着 "
              "loaderName=Forge，显示层把它归进了「可安装mod」，而文件里**一条归属都没有**。"
              "这一格要是取了 groupIdOf()（存的那一格），用户点进设置页什么都不动（连离开那一格"
              "都没做），就被替他说了一句「我就想待在未分组」，从此再也不会被自动归类",
              comboValue);
        check(!pinChecked, "T18 ① 它还没置顶过（那个勾选框是空的）");
        check(membersInFile().isEmpty(),
              "★ T18 ① 什么都没动：盘上**一条归属都没多出来**（看过一眼不等于表过态）",
              QString::fromUtf8(readBytesAt(groupFile)));

        // ---- ② 改挑「原版」：归属真的写进去了，树上跟着挪 ----
        {
            editAndSave(kId, [&](QWidget *, InstanceSettingsDialog *page, QCheckBox *) {
                clickGroupRow(page, QStringLiteral("原版"));
            });
            check(membersPointingAt(kVanilla) == 1 && membersInFile().size() == 1,
                  "★ T18 ② 在「分组…」浮层里挑「原版」：这一条归属就写进了文件（而且只多这一条）"
                  "—— 没有「确定」参与（那一行点下去就是写盘的那一下）",
                  QString::fromUtf8(readBytesAt(groupFile)));
            QStringList headers;
            const QImage afterMove = treeInNewWindow(&headers);
            check(headers == QStringList({QStringLiteral("原版 (3)")}),
                  "★ T18 ② 树上它真的挪到「原版」下面了（原来那个「可安装mod」空了、不占行）",
                  headers.join(QLatin1Char('|')));
            check(afterMove == forgeMark, "T18 ② 换组不动图标（行首还是那把铁砧）");
        }

        // ---- ③ 勾上「显示在置顶组中」：置顶组里多一条同名的，老家那一条**不动** ----
        {
            editAndSave(kId, [&](QWidget *, InstanceSettingsDialog *, QCheckBox *pin) {
                if (pin != nullptr) {
                    pin->click();
                }
            });
            check(membersPointingAt(kPin) == 1 && membersPointingAt(kVanilla) == 0 &&
                      membersInFile().size() == 1 && originCount(kVanilla) == 1,
                  "★ T18 ③ 勾上「置顶」：members 那格指着置顶组，而且**记下了「置顶之前"
                  "它待在原版那一组」**（pinOrigin）—— 那一条正是「老家在哪」和「取消置顶回哪儿」"
                  "这两件事共用的出处",
                  QString::fromUtf8(readBytesAt(groupFile)));
            QStringList headers;
            treeInNewWindow(&headers);
            check(headers == QStringList({QStringLiteral("收藏 (1)"), QStringLiteral("原版 (3)")}),
                  "★ T18 ③ 树上**两边各一条**：置顶组里多出那条同名的（原来是空的、不占行），"
                  "而老家「原版」里那一条**还在**（组头那个数把两条都数上了：原版本来就有 3 个"
                  "实例，其中一个此刻在两个组里各有一行）",
                  headers.join(QLatin1Char('|')));
        }

        // ---- ④ 取消勾选：置顶组里那一条没了，老家不动 ----
        {
            editAndSave(kId, [&](QWidget *, InstanceSettingsDialog *, QCheckBox *pin) {
                if (pin != nullptr) {
                    pin->click();
                }
            });
            check(membersPointingAt(kVanilla) == 1 && membersPointingAt(kPin) == 0 &&
                      pinOriginInFile().isEmpty(),
                  "★ T18 ④ 取消勾选：它**回到了置顶之前待的那一组**（原版），那条记录"
                  "也跟着用掉了（留着的话下一次置顶会把旧账当成「置顶前」再读一遍）—— "
                  "盘上那一格走了一圈回到原样，两边都显示的那条多出来的行也就没了",
                  QString::fromUtf8(readBytesAt(groupFile)));
            QStringList headers;
            treeInNewWindow(&headers);
            check(headers == QStringList({QStringLiteral("原版 (3)")}),
                  "T18 ④ 树上也跟着回了原版（置顶组空回去、又不占行了）",
                  headers.join(QLatin1Char('|')));
        }

        // ---- ⑤ 这一份 .minecraft 里**没有置顶组**：勾选什么也不该发生，但用户选的组要算数 ----
        {
            QJsonObject root = QJsonDocument::fromJson(readBytesAt(groupFile)).object();
            QJsonArray kept;
            const QJsonArray groups = root.value(QStringLiteral("groups")).toArray();
            for (const QJsonValue &value : groups) {
                if (value.toObject().value(QStringLiteral("id")).toString() != kPin) {
                    kept.append(value);
                }
            }
            root.insert(QStringLiteral("groups"), kept);
            writeFileAt(groupFile, QJsonDocument(root).toJson(QJsonDocument::Compact));
            check(kept.size() == groups.size() - 1,
                  "T18 前提：「置顶」那一组从文件里删掉了（默认组本来就可以删）",
                  QStringLiteral("%1 -> %2").arg(groups.size()).arg(kept.size()));

            // 顺序是"先勾上、再自己挑一个组"。**这一格在产品里是灰的**（没有置顶组时
            // setPinned() 把它灰掉并说清为什么），所以用户实际上勾不动它 —— 这里先把它
            // 解开（`setEnabled(true)`，产品不会这么做）再 `click()`，就是想"万一勾上了"
            // 那条路走一遍：结果必须是**什么都不发生**，而用户自己挑的那个组照旧算数。
            // （两件事现在本来就各管各的。）
            //
            // NOTE: 这一发必须是 `click()`：`setChecked()` 只发 toggled、不发 clicked，
            // 而"改了就写"那条线接的是 clicked —— 用 setChecked 的话这一格在**接线
            // 那侧**等于没动过，这一节就走了个空（S2b 钉住的就是这层因果）。
            editAndSave(kId, [&](QWidget *, InstanceSettingsDialog *page, QCheckBox *pin) {
                if (pin != nullptr) {
                    pin->setEnabled(true);
                    pin->click();
                }
                clickGroupRow(page, QStringLiteral("快照版"));
            });
            check(comboValue == kVanilla, "T18 前提：开页时那一格还是它现在待的「原版」",
                  comboValue);
            check(membersPointingAt(kSnapshot) == 1 && membersPointingAt(kVanilla) == 0,
                  "★ T18 ⑤ 没有置顶组时勾上「置顶」：什么也不会发生（它没有去处），但用户**自己"
                  "在浮层里挑的那个组必须算数**（快照版）—— 那一下勾选不该顺手把用户明确选的"
                  "那一格一起吞掉",
                  QString::fromUtf8(readBytesAt(groupFile)));
            QStringList headers;
            const QImage afterSnapshot = treeInNewWindow(&headers);
            check(headers == QStringList({QStringLiteral("原版 (2)"), QStringLiteral("快照版 (1)")}),
                  "T18 ⑤ 树上也挪过去了（这一份里没有置顶组，所以组头里也没有它）",
                  headers.join(QLatin1Char('|')));
            check(afterSnapshot == forgeMark, "T18 ⑤ 这一档换的还是只有归属");
        }

        // ---- ⑥ 图标：点一颗内置候选 → meta 里写 builtin:<键>，树上换成那一张 ----
        {
            const QStringList keys = VersionIcons::iconCandidateKeys();
            const int grassAt = keys.indexOf(QStringLiteral("grass"));
            check(grassAt >= 0, "T18 前提：八颗候选里有「草方块」那一档",
                  QString::number(grassAt));
            editAndSave(kId, [&](QWidget *host, InstanceSettingsDialog *, QCheckBox *) {
                if (auto *button = host->findChild<QToolButton *>(
                        InstanceSettingsDialog::iconCandidateButtonName(grassAt))) {
                    button->click();
                }
            });
            const QJsonObject meta = QJsonDocument::fromJson(readBytesAt(metaPath)).object();
            check(meta.value(QStringLiteral("icon")).toString() ==
                      VersionIcons::iconValueForKey(QStringLiteral("grass")),
                  "★ T18 ⑥ 点一颗内置候选：meta 里当场写上了 builtin:grass 那一句",
                  QString::fromUtf8(readBytesAt(metaPath)));
            QStringList headers;
            const QImage afterPick = treeInNewWindow(&headers);
            check(afterPick == grassMark && afterPick != forgeMark,
                  "★ T18 ⑥ 树上那一行真的换成了他点的那张（草方块），不是原来那把铁砧");
        }

        // ---- ⑦ 「恢复默认」：那个键**删掉**，不是写一个等于默认的值 ----
        {
            editAndSave(kId, [&](QWidget *host, InstanceSettingsDialog *, QCheckBox *) {
                if (auto *reset = host->findChild<QPushButton *>(
                        InstanceSettingsDialog::iconResetButtonName())) {
                    reset->click();
                }
            });
            const QJsonObject meta = QJsonDocument::fromJson(readBytesAt(metaPath)).object();
            check(!meta.contains(QStringLiteral("icon")),
                  "★ T18 ⑦ 「恢复默认」之后 meta 里**没有那个键**了 —— 「默认」是「这个键"
                  "不存在」，不是「写一个等于默认的值」（写死了的话，这个实例以后换版本分类"
                  "也不会跟着换标志了）",
                  QString::fromUtf8(readBytesAt(metaPath)));
            QStringList headers;
            const QImage afterReset = treeInNewWindow(&headers);
            check(afterReset == forgeMark, "★ T18 ⑦ 树上退回默认那张（照版本分类算的铁砧）");
        }

        // ---- ⑧⑨ 本地那张图的**读**侧 ----
        //
        // 写侧（挑一张 → 拷进实例目录 → meta 写 local）走不了产品那条路：挑图那一颗开的是
        // Windows 自己的文件选择框（见本节开头那条 NOTE）。所以这里直接从"挑过之后盘上该
        // 长什么样"接着验：实例目录下躺着一张 icon.png + meta 里写着 local。
        {
            QImage source(8, 4, QImage::Format_ARGB32);
            source.fill(QColor(0xE0, 0x20, 0x20));
            check(source.save(iconPath, "PNG") && QFile::exists(iconPath),
                  "T18 ⑧ 前提：往实例目录里放了一张真的 PNG");
            QJsonObject meta = QJsonDocument::fromJson(readBytesAt(metaPath)).object();
            meta.insert(QStringLiteral("icon"), InstanceSettingsDialog::localIconValue());
            writeFileAt(metaPath, QJsonDocument(meta).toJson(QJsonDocument::Compact));

            QStringList headers;
            const QImage localMark = VersionIcons::fileMark(iconPath, markSize).toImage();
            const QImage afterLocal = treeInNewWindow(&headers);
            check(!localMark.isNull() && afterLocal == localMark && afterLocal != forgeMark,
                  "★ T18 ⑧ meta 里写着 local、实例目录下躺着那张图的实例：树上画的就是**用户挑的"
                  "那一张**（不是「没挑过」那条路上的铁砧）");
            check(afterLocal != grassMark, "T18 ⑧ 而且不是刚才那张草方块（换了确实是换了）");

            // 图坏了 / 被人删了：退回默认那张，不是一块空白、也不崩。
            writeFileAt(iconPath, QByteArrayLiteral("这不是一张图"));
            QStringList headers9;
            const QImage afterBroken = treeInNewWindow(&headers9);
            check(afterBroken == forgeMark,
                  "★ T18 ⑨ 那张图坏了：树上**退回默认那张**（照着版本分类算的铁砧）—— "
                  "表现是「它没生效」，不是一块空白、也不是一张黑图");
        }

        // 收拾：这个实例的 meta 逐字节还原、多出来的那份 icon.png 和这一节自己那份分组文件
        // 都删掉 —— 后面 U 段起的每一个窗都会扫一遍 versions/，留一份改过的 meta 迟早撞上。
        QFile::remove(iconPath);
        writeFileAt(metaPath, metaBefore);
        QDir().remove(groupFile);
        check(readBytesAt(metaPath) == metaBefore && !QFile::exists(iconPath) &&
                  !QFile::exists(groupFile),
              "T18 收拾：1.20.1 的 meta 逐字节还原，icon.png 和这份 wtfml.folders 都删掉了");
    }
    // T18b：页眉那一行**文字里**的加载器名，和主页 / 管理列表 / 页眉那张图标是同一个出处。
    //
    // 这一条只在一种实例上分得开：加载器**只写在版本 json 的 `libraries` 里**（`net.fabricmc:
    // fabric-loader:0.16.14`），没有 `inheritsFrom`、`wtfml.meta` 里也没有 `loaderName` ——
    // PCL / HMCL 合并出来的实例大多长这样。那种实例上 `displayParts().loaderName` 是空的，
    // 而页眉图标（`defaultInstanceMark()`，同一个控制器第 234 行那一段）、主页那一行、
    // 管理列表都回扣版本 json 认得出 Fabric。页眉文字原来直接读 `parts.loaderName`，于是
    // 同一个实例图标是 Fabric、字却写「原版 · Minecraft …」。
    {
        const QString kId = QStringLiteral("T18b-merged-fabric");
        const QDir instanceDir(versionsDir.filePath(kId));
        QDir(versionsDir.filePath(kId)).removeRecursively();  // 上一跑崩在这里留下的残骸
        makeInstalledVersion(versionsDir, kId);
        writeFileAt(instanceDir.filePath(kId + QStringLiteral(".json")),
                    QByteArrayLiteral("{\"id\":\"T18b-merged-fabric\","
                                      "\"mainClass\":\"net.fabricmc.loader.impl.launch.knot.KnotClient\","
                                      "\"libraries\":[{\"name\":\"net.fabricmc:fabric-loader:0.16.14\"}]}"));
        check(!QFile::exists(instanceDir.filePath(QStringLiteral("wtfml.meta"))),
              "T18b 前提：这个实例没有 wtfml.meta（加载器只写在版本 json 的 libraries 里）");

        QString headerMeta;
        bool sawPage18b = false;
        {
            MainWindow w;
            w.show();
            QApplication::processEvents();
            QMetaObject::invokeMethod(&w, "openInstanceSettings", Qt::DirectConnection,
                                      Q_ARG(QString, kId));
            QApplication::processEvents();
            OverlayHost *host = currentPageOverlay();
            auto *page = host != nullptr ? host->findChild<InstanceSettingsDialog *>() : nullptr;
            sawPage18b = page != nullptr;
            auto *meta = page != nullptr
                             ? page->findChild<QLabel *>(InstanceSettingsDialog::headerMetaName())
                             : nullptr;
            if (meta != nullptr) {
                headerMeta = meta->text();
            }
            if (host != nullptr) {
                host->closeOverlay();
            }
        }
        check(sawPage18b && !headerMeta.isEmpty(),
              "T18b 这个实例的实例设置页开出来了，页眉那一行是写着的", headerMeta);
        // 版本号那一格也是**这一趟从 json 认出来的**（meta 里没有 loaderVersion），所以这条
        // 断言同时钉住了"名字和版本同一次读取的两半"。
        check(headerMeta.startsWith(QStringLiteral("Fabric 0.16.14 · Minecraft ")),
              "★ T18b 页眉那一行写「Fabric 0.16.14 · Minecraft …」—— 和主页 / 管理列表 / 页眉"
              "那张图标同一个出处（回扣版本 json 的 libraries 认加载器）。直接读 "
              "displayParts().loaderName 的话，这一行在 PCL 那种实例上会写「原版」",
              headerMeta);
        check(!headerMeta.startsWith(QStringLiteral("原版")),
              "★ T18b 而且它不再是「原版 · Minecraft …」", headerMeta);

        // 收拾：目录整个删掉。后面的段会重扫 versions/，留一个实例在那儿它们数的数就变了。
        QDir(versionsDir.filePath(kId)).removeRecursively();
        QApplication::processEvents();
        check(!QDir(versionsDir.filePath(kId)).exists(),
              "T18b 收拾：这个临时实例的目录删干净了");
    }
    // T18c：147 的同类残留 —— 升级 / 迁移 / 更新那几页和管理页的筛选、搜索里，
    // 加载器名一律走 `InstancePagesController::loaderName()`。
    //
    // 夹具和 T18b 同一个思路：加载器**只写在版本 json 的 `libraries` 里**
    // （`net.fabricmc:fabric-loader:0.16.14`），`wtfml.meta` 里没有 `loaderName` 那一格 ——
    // PCL / HMCL 合并出来的实例大多长这样。这种实例上 `displayParts().loaderName` 是空的，
    // 于是：迁移/升级的目标下拉框少一段「 / Fabric」、进 `TransferPlanner` 的源加载器是空
    // （兼容结论按「原版 ↔ 原版」写）、升级页那句「当前实例」写「原版」、新版更新页默认
    // 选中「纯原版」、管理页「模组」那一档筛不出它、搜「fabric」也搜不到。
    //
    // NOTE: 这个实例的目录名故意**不带 fabric**（叫 `T18c-merged`）：搜索那一条要红的正是
    // "只有加载器那一格能命中"，名字里带着那几个字母的话，撤掉修复也照样搜得到。
    {
        const QString kId = QStringLiteral("T18c-merged");
        const QDir instanceDir(versionsDir.filePath(kId));
        QDir(versionsDir.filePath(kId)).removeRecursively();  // 上一跑崩在这里留下的残骸
        makeInstalledVersion(versionsDir, kId);
        writeFileAt(instanceDir.filePath(kId + QStringLiteral(".json")),
                    QByteArrayLiteral("{\"id\":\"T18c-merged\","
                                      "\"mainClass\":\"net.fabricmc.loader.impl.launch.knot.KnotClient\","
                                      "\"libraries\":[{\"name\":\"net.fabricmc:fabric-loader:0.16.14\"}]}"));
        check(!QFile::exists(instanceDir.filePath(QStringLiteral("wtfml.meta"))),
              "T18c 前提：这个实例没有 wtfml.meta（加载器只写在版本 json 的 libraries 里）");

        QString upgradeContext;
        QString updateSourceText;
        QString updateLoaderKey;
        QString migrationPlan;
        QString mergedTargetLabel;
        QStringList withLoaderIds;
        QStringList searchedIds;
        {
            MainWindow w;
            w.show();
            QApplication::processEvents();

            // ---------- ① 升级页「当前实例」那句 + 新版更新页的来源那一行 / 默认加载器 ----------
            QMetaObject::invokeMethod(&w, "openInstanceSettings", Qt::DirectConnection,
                                      Q_ARG(QString, kId));
            QApplication::processEvents();
            OverlayHost *host = currentPageOverlay();
            auto *page = host != nullptr ? host->findChild<InstanceSettingsDialog *>() : nullptr;
            check(page != nullptr, "T18c 合并实例的实例设置页开出来了");
            if (page != nullptr) {
                page->showPage(QStringLiteral("upgrade"));
                QApplication::processEvents();
                if (auto *context =
                        page->findChild<QLabel *>(QStringLiteral("instanceUpgradeContext"))) {
                    upgradeContext = context->text();
                }
                if (auto *selectors = page->findChild<QWidget *>(
                        QStringLiteral("instanceUpdateSelectors"))) {
                    for (QLabel *label : selectors->findChildren<QLabel *>()) {
                        if (label->text().startsWith(QStringLiteral("当前实例："))) {
                            updateSourceText = label->text();
                        }
                    }
                }
                if (auto *loaderCombo =
                        page->findChild<QComboBox *>(QStringLiteral("instanceUpdateLoader"))) {
                    updateLoaderKey = loaderCombo->currentData().toString();
                }

                // ---------- ② 进 TransferPlanner 的源加载器 ----------
                //
                // 「迁移」那一页是唯一一处**把计划正文露出来**的地方（升级那一页在新版流程里
                // 由 `InstanceUpdateFlow` 接管，正文不再写旧计划）。目标选 1.20.1（meta 里写着
                // Forge）：源的游戏版本读不出来（这个实例的 id 就是它的"版本"），所以结论是
                // 「版本与加载器都不同」，那句话把**源加载器**也写出来了 —— 撤掉修复时那里是
                // 「原版」。
                page->showPage(QStringLiteral("migration"));
                QApplication::processEvents();
                auto *combo = page->findChild<QComboBox *>(
                    InstanceSettingsDialog::transferTargetComboName(QStringLiteral("migration")));
                auto *plan = page->findChild<QPlainTextEdit *>(
                    InstanceSettingsDialog::transferPlanTextName(QStringLiteral("migration")));
                if (combo != nullptr) {
                    const int target = combo->findData(QStringLiteral("1.20.1"));
                    if (target >= 0) {
                        combo->setCurrentIndex(target);
                    }
                }
                QApplication::processEvents();
                if (plan != nullptr) {
                    QElapsedTimer wait;
                    wait.start();
                    while (!plan->toPlainText().contains(QStringLiteral("迁移计划："))
                           && wait.elapsed() < 5000) {
                        QApplication::processEvents(QEventLoop::AllEvents, 10);
                        QThread::msleep(1);
                    }
                    migrationPlan = plan->toPlainText();
                }
                if (host != nullptr) {
                    host->closeOverlay();
                }
                QApplication::processEvents();
            }

            // ---------- ③ 管理页：「模组」那一档 + 搜「fabric」 ----------
            auto *tree = byObjectName<QTreeWidget>(&w, MainWindowUI::instanceListName());
            auto *search = byObjectName<QLineEdit>(&w, MainWindowUI::instanceSearchName());
            auto *chipModded = byObjectName<QPushButton>(
                &w, MainWindowUI::instanceFilterButtonName(
                        MainWindowUI::InstanceFilterButton::Modded));
            const auto shownIds = [tree] {
                QStringList ids;
                for (QTreeWidgetItem *item : instanceItemsOf(tree)) {
                    ids << item->data(0, MainWindowUI::instanceIdRole()).toString();
                }
                ids.sort();
                return ids;
            };
            if (chipModded != nullptr) {
                chipModded->click();
                QApplication::processEvents();
                withLoaderIds = shownIds();
                chipModded->click();  // 取消，别把状态留给后面的段
                QApplication::processEvents();
            }
            if (search != nullptr) {
                search->setText(QStringLiteral("fabric"));
                QApplication::processEvents();
                searchedIds = shownIds();
                search->setText(QString());
                QApplication::processEvents();
            }
        }

        // ---------- ④ 目标下拉框里那一条 label（这个合并实例当**目标**） ----------
        //
        // 换个窗开：同一扇窗上「上一页刚收、下一页马上开」这条路上覆盖层自己有一道闸
        // （`showPage()` 撞上还占着的覆盖层时静默无事发生），而这里量的是 label 怎么拼。
        {
            MainWindow w;
            w.show();
            QApplication::processEvents();
            QMetaObject::invokeMethod(&w, "openInstanceSettings", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("1.20.1")));
            QApplication::processEvents();
            OverlayHost *host = currentPageOverlay();
            auto *page = host != nullptr ? host->findChild<InstanceSettingsDialog *>() : nullptr;
            if (page != nullptr) {
                page->showPage(QStringLiteral("migration"));
                QApplication::processEvents();
                if (auto *combo = page->findChild<QComboBox *>(
                        InstanceSettingsDialog::transferTargetComboName(
                            QStringLiteral("migration")))) {
                    const int row = combo->findData(kId);
                    if (row >= 0) {
                        mergedTargetLabel = combo->itemText(row);
                    }
                }
                if (host != nullptr) {
                    host->closeOverlay();
                }
                QApplication::processEvents();
            }
        }

        check(upgradeContext.contains(QStringLiteral("Fabric 0.16.14")) &&
                  !upgradeContext.contains(QStringLiteral("原版")),
              "★ T18c 升级页那句「当前实例」写「Fabric 0.16.14」，不是「原版」"
              "（读 displayParts().loaderName 的话这一句在合并实例上是「原版」）",
              upgradeContext);
        check(!updateSourceText.isEmpty() &&
                  updateSourceText.contains(QStringLiteral("Fabric")) &&
                  !updateSourceText.contains(QStringLiteral("纯原版")),
              "★ T18c 新版更新页那句「当前实例」也写 Fabric，不是「纯原版」", updateSourceText);
        check(updateLoaderKey == QStringLiteral("fabric"),
              "★★ T18c 更新页默认选中的加载器是 fabric（不是「纯原版」）—— 下拉框那一位是拿"
              "源实例的加载器名去 findData 的", updateLoaderKey);
        check(migrationPlan.contains(QStringLiteral("/ Fabric")),
              "★★ T18c 进 TransferPlanner 的源加载器是 Fabric（计划正文里那句「源是 … / Fabric」）"
              "—— 空的话这一趟会按「原版」评估模组兼容",
              migrationPlan.left(200));
        check(!migrationPlan.contains(QStringLiteral("源是 T18c-merged / 原版")),
              "★ T18c 而且不再是「源是 T18c-merged / 原版」", migrationPlan.left(200));
        check(mergedTargetLabel.contains(QStringLiteral(" / Fabric")) &&
                  !mergedTargetLabel.contains(QStringLiteral("原版")),
              "★ T18c 目标下拉框里这一条 label 带着「 / Fabric」（漏了这一段就是读的空加载器）",
              mergedTargetLabel);
        check(withLoaderIds.contains(kId),
              "★★ T18c 管理页「模组」那一档筛得出这个合并实例（判据是加载器名，不是"
              "wtfml.meta 那一格）", withLoaderIds.join(QLatin1Char('|')));
        check(searchedIds.contains(kId),
              "★★ T18c 搜「fabric」搜得到它（可比对的串里那一段加载器名来自 loaderName()）",
              searchedIds.join(QLatin1Char('|')));

        // 收拾：目录、以及这几个窗顺手种下的分组文件（后面那些段都按"没分过组"数东西）。
        QDir(versionsDir.filePath(kId)).removeRecursively();
        QDir().remove(AppPaths::instanceGroupFilePath());
        QApplication::processEvents();
        check(!QDir(versionsDir.filePath(kId)).exists(),
              "T18c 收拾：这个临时实例的目录删干净了");
    }
    // UG6：维护页里的新版升级页 —— 勾选决定副本要复制哪些类别。
    //
    // 这是"页面 → 请求"那条线唯一能验的地方：页面上的九类勾选必须实时落到
    // `upgradeCopyRoots` 上（执行时 `upgradeToCopy(..., copyRoots)` 就取它）。
    {
        MainWindow w;
        w.show();
        QApplication::processEvents();
        QMetaObject::invokeMethod(&w, "openInstanceSettings", Qt::DirectConnection,
                                  Q_ARG(QString, QStringLiteral("1.20.1")));
        QApplication::processEvents();
        auto *host = currentOverlay();
        auto *page = host != nullptr ? host->findChild<InstanceSettingsDialog *>() : nullptr;
        check(page != nullptr, "UG6 实例设置页开出来了");
        // 页是第一次切过去才建的（见 InstanceSettingsDialog::ensurePage()）：升级页的东西要
        // 先切到那一页 —— 照用户的走法，别在开页那一刻就 findChild。
        if (page != nullptr) {
            page->showPage(QStringLiteral("upgrade"));
        }
        auto *upgrade = page != nullptr ? page->findChild<InstanceUpgradePage *>() : nullptr;
        check(upgrade != nullptr, "UG6 维护页里有新版升级页（不是只剩旧向导入口）");
        QStringList roots =
            page != nullptr ? page->property("upgradeCopyRoots").toStringList() : QStringList();
        check(roots.contains(QStringLiteral("saves")) && roots.contains(QStringLiteral("mods"))
                  && roots.contains(QStringLiteral("config"))
                  && roots.contains(QStringLiteral("defaultconfigs"))
                  && roots.contains(QStringLiteral("options.txt"))
                  && roots.contains(QStringLiteral("servers.dat")),
              "UG6 默认勾选覆盖运行必需的那几类", roots.join(QLatin1Char(',')));
        check(!roots.contains(QStringLiteral("screenshots"))
                  && !roots.contains(QStringLiteral("structures")),
              "UG6 截图 / 原理图默认不在复制清单里", roots.join(QLatin1Char(',')));
        auto *savesBox = upgrade != nullptr
                             ? upgrade->findChild<QAbstractButton *>(
                                   InstanceUpgradePage::categoryCheckName(QStringLiteral("saves")))
                             : nullptr;
        check(savesBox != nullptr, "UG6 找得到「存档」那一格");
        if (savesBox != nullptr) {
            savesBox->setChecked(false);
        }
        QApplication::processEvents();
        roots = page != nullptr ? page->property("upgradeCopyRoots").toStringList() : QStringList();
        check(!roots.contains(QStringLiteral("saves")),
              "UG6 勾掉存档之后复制清单里就没有 saves", roots.join(QLatin1Char(',')));
        if (host != nullptr) {
            host->closeOverlay();
        }
    }
    return 0;
}


