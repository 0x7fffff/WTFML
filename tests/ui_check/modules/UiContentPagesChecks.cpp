#include "Support.hpp"
int UiContentPagesChecks(QFrame * &bar) {
    // ---- U10 「文件管理」那八页真正的内容----
    {
        // 自己起一份，不蹭上面 U 段那块作用域里的同名量（那一段已经收了）。
        using Dlg = InstanceSettingsDialog;
        const QString instanceDisplayName = QStringLiteral("我的整合包");
        const QString instanceDirName = QStringLiteral("我的整合包-1.20.1-Forge");
        const Dlg::GlobalValues global = [] {
            Dlg::GlobalValues g;
            g.javaPath = QStringLiteral("C:/全局/java.exe");
            g.xmx = 4096;
            g.xms = 1024;
            g.autoMemory = false;
            g.extraJvmArgs = QStringLiteral("-XX:+UseG1GC");
            return g;
        }();
    // 这一段验的是**显示**：目录列得对不对、三条开关（能不能启停 / 能不能删 / 只列文件夹）
    // 摆得对不对、"用不上"那一页说的是不是那句解释。**真正的删除与改名不在这儿验** ——
    // 那一层刻意不碰盘（见 InstanceFilesPage 的文件头），那两条端到端在 mw_check 里走。
    {
        // 一个"装了点东西"的实例目录：每样都放一份，好逐页量"这一页该有什么、不该有什么"。
        QTemporaryDir scratch;
        const QString root = scratch.path();
        const auto mkfile = [](const QString &path) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile file(path);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(QByteArray(4096, 'x'));
            }
        };
        const QString modsDir = root + QStringLiteral("/mods");
        const QString savesDir = root + QStringLiteral("/saves");
        mkfile(modsDir + QStringLiteral("/sodium.jar"));
        mkfile(modsDir + QStringLiteral("/iris-1.6.5.jar"));
        // 被禁用的那一条：盘上叫 `.jar.disabled`，界面上要显示成原名 + 一个记号。
        mkfile(modsDir + QStringLiteral("/禁用掉的.jar.disabled"));
        mkfile(modsDir + QStringLiteral("/.connector/config.json"));
        // 再放一个**不带点**的（`run/` 底下那个 mcef 目录就是这一种），这样这一条断言
        // 不靠"点开头的算不算隐藏"这档子平台差异 —— 无论 Qt 在 Windows 上认不认点开头，
        // 这个名字都实打实占着一行。
        mkfile(modsDir + QStringLiteral("/mcef-libraries/README.txt"));
        mkfile(root + QStringLiteral("/resourcepacks/材质.zip"));
        // 资源包 / 数据包 / 光影包都可以是一个**目录**（不是只有 zip）。
        mkfile(root + QStringLiteral("/resourcepacks/文件夹包/pack.mcmeta"));
        mkfile(root + QStringLiteral("/datapacks/数据.zip"));
        mkfile(root + QStringLiteral("/shaderpacks/光影.zip"));
        mkfile(root + QStringLiteral("/schematics/房子.litematic"));
        mkfile(savesDir + QStringLiteral("/我的世界/level.dat"));

        // 跳到某一页一律走 showPage(key)：左栏从此是**按实例算出来的一列**，
        // 行号**不等于**键表的下标 —— 照旧写 `setCurrentRow(filePageKeys().indexOf(key))`
        // 会静默跳到**别的**那一页上，而"跳错了页"在这一层是看不出来的（下面每一条断言
        // 读的都是页里的控件）。另外「配置文件」那一页在左栏里**根本没有条目**
        // （它没有任何入口，见 .hpp 里 optionalPageKeys() 那段）。
        const auto showPage = [](Dlg &d, const QString &key) { d.showPage(key); };
        // 「这一条叫什么」**不在 `item->text()` 上**了 —— 条目故意不带文字
        // （带了的话视图代理会在透明的行控件底下再画一遍同一个名字，屏幕上是重影；
        // 用户截图报的「模组名称出现了两次」就是它，见 InstanceFilesPage::refresh()）。
        // 名字的唯一出处是行里那一格标签，所以这里照着**界面上真正显示的东西**读。
        const auto itemText = [](InstanceFilesPage *page, int row) {
            QListWidgetItem *item = page != nullptr ? page->list()->item(row) : nullptr;
            QWidget *rowWidget =
                (page != nullptr && item != nullptr) ? page->list()->itemWidget(item) : nullptr;
            auto *label = rowWidget != nullptr
                              ? rowWidget->findChild<QLabel *>(InstanceFilesPage::nameLabelName())
                              : nullptr;
            return label != nullptr ? label->text() : QString();
        };
        const auto itemFlag = [](InstanceFilesPage *page, int row, int role) {
            QListWidgetItem *item = page != nullptr ? page->list()->item(row) : nullptr;
            return item != nullptr && item->data(role).toBool();
        };
        const auto itemPath = [](InstanceFilesPage *page, int row) {
            QListWidgetItem *item = page != nullptr ? page->list()->item(row) : nullptr;
            return item != nullptr ? item->data(InstanceFilesPage::pathRole()).toString()
                                   : QString();
        };

        Dlg dialog(instanceDisplayName, instanceDirName, Dlg::Override(), global, QStringList(),
                   true);
        QHash<QString, QString> dirs;
        dirs.insert(QStringLiteral("mod"), modsDir);
        dirs.insert(QStringLiteral("resourcepack"), root + QStringLiteral("/resourcepacks"));
        dirs.insert(QStringLiteral("datapack"), root + QStringLiteral("/datapacks"));
        dirs.insert(QStringLiteral("shader"), root + QStringLiteral("/shaderpacks"));
        dirs.insert(QStringLiteral("schematic"), root + QStringLiteral("/schematics"));
        dirs.insert(QStringLiteral("save"), savesDir);
        dirs.insert(QStringLiteral("logs"), root + QStringLiteral("/logs"));
        dirs.insert(QStringLiteral("screenshots"), root + QStringLiteral("/screenshots"));
        // 概览那排快捷方式里「游戏目录」那一路（不在文件管理那几页的表里，见
        // InstanceSettingsDialog::shortcutPathKeys() 那段）。
        dirs.insert(QStringLiteral("game"), root + QStringLiteral("/game"));
        dialog.setInstanceDir(root);
        dialog.setFileDirs(dirs);
        dialog.show();

        // ---- U10a 中间那几页都是同一个控件，各自落在自己那一格里 ----
        {
            const QStringList keys = Dlg::filePageKeys();
            bool pagesFound = true;
            int count = 0;
            for (const QString &key : keys) {
                // 「服务器列表」是棵表（不是 InstanceFilesPage），跳过。
                if (key == Dlg::serverPageKey()) {
                    continue;
                }
                ++count;
                InstanceFilesPage *page = dialog.filesPage(key);
                pagesFound = pagesFound && page != nullptr && page->list() != nullptr &&
                             page->objectName() == Dlg::filesPageName(key) &&
                             dialog.findChild<QWidget *>(Dlg::panelName(key)) != nullptr;
            }
            // 数量**从键表里算**，不写死（ 加了「配置文件」那一页时，写死的
            // `== 6` 正是这一条红的唯一原因）。"页数对不对"由 U0a 那两条接着管 ——
            // 它们量的是 pageCount()，而那个是从同一张键表算出来的。
            const int expected = keys.size() - 1;  // 减去 `server`
            check(pagesFound && count == expected,
                  "★ U10a 中间那几页目录列表都搭起来了、都在自己那一格里（filesPageName 与 "
                  "panelName 对得上）",
                  QStringLiteral("%1 页").arg(count));
            // 而且**「配置文件」那一页和别的几页不是一个形状**：它是一页
            // 两格子（列表 / 编辑器，见 InstanceSettingsDialog::configFilePage() 那段）
            // —— 那张列表不直接是 panel 的孩子，中间还隔着那层堆叠。上面那条用的是
            // findChild（递归），所以它照样找得到；这一条把"隔着的是谁"也钉住。
            QWidget *configPanel = dialog.findChild<QWidget *>(Dlg::panelName(
                QStringLiteral("config")));
            InstanceFilesPage *configFiles = dialog.filesPage(QStringLiteral("config"));
            // 那张列表**不是直接摆在这一页上的**：中间隔着"两格堆叠 → 第 0 格"。
            // 判据走"往上找得到这一页、但它不是这一页的直接孩子"—— 数层数（parentWidget()
            // 连几级）会把中间那层容器的实现细节写死，而那不是这一条要说的事。
            bool nestedOk = false;
            if (configFiles != nullptr && configPanel != nullptr &&
                configFiles->parentWidget() != configPanel) {
                for (QWidget *w = configFiles->parentWidget(); w != nullptr; w = w->parentWidget()) {
                    if (w == configPanel) {
                        nestedOk = true;
                        break;
                    }
                }
            }
            check(nestedOk,
                  "★ U10a 「配置文件」那张列表**嵌在两格堆叠的第 0 格里**（不是直接摆在这一页"
                  "上）—— 第 1 格是编辑器，见 configFilePage() 那段");

            // 编辑器那一格：**和列表同在那个堆叠里**，而且刚切过来时没露脸。
            //
            // NOTE: 得先把这一页摆到台面上再问 isVisible()（收着的堆叠页一律是 false，
            // 不摆的话这一条永远绿、什么都没量到 —— 和这一节别处量几何前先 showPage
            // 是同一条规矩）。
            showPage(dialog, QStringLiteral("config"));
            ConfigFileEditorPage *editor = dialog.configFilePage();
            check(editor != nullptr && editor->objectName() == ConfigFileEditorPage::pageName(),
                  "★ U10a 那一页第 1 格里有个编辑器（它自己的 objectName 走 pageName()，"
                  "里面那个编辑框是 editorName() —— 两个别混）");
            // 两个在同一个堆叠里：从列表往上找，**第一个同时也是编辑器祖先的那一层**
            // 就是那个堆叠（往上数几级是别人的实现细节，不写死）。
            QWidget *sharedParent = nullptr;
            for (QWidget *w = configFiles != nullptr ? configFiles->parentWidget() : nullptr;
                 w != nullptr; w = w->parentWidget()) {
                if (editor != nullptr && w->isAncestorOf(editor)) {
                    sharedParent = w;
                    break;
                }
            }
            check(sharedParent != nullptr && configFiles != nullptr && configFiles->isVisible() &&
                      editor != nullptr && !editor->isVisible(),
                  "★ U10a 列表和编辑器**在同一个堆叠的相邻两格里**，而且刚切到这一页时露着的"
                  "是**列表**那一格（编辑器要等点开一个文件 —— 一进来就露着的话用户看到的"
                  "是一片空白）");
        }

        // ---- U10b 常用目录那一排按钮（住在**概览**页里）----
        //
        // 这一排按钮在概览页里，不再有单独的「快捷方式」页（键、objectName、底下那句话
        // 一个字都没改，只是换了个父）。U0h 那两条钉的就是"它们真的落在概览里"。
        {
            const QList<QWidget *> buttons = dialog.shortcutButtons();
            const QStringList texts = Dlg::shortcutTexts();
            const QStringList keys = Dlg::shortcutPathKeys();
            check(buttons.size() == texts.size() && texts.size() == 5,
                  "★ U10b 概览页里是**五颗**常用目录按钮（版本目录 / 日志 / 游戏目录 / 崩溃"
                  "报告 / 配置 —— 左栏有入口的那几路不再重复摆）",
                  QStringLiteral("%1 颗").arg(buttons.size()));
            check(keys.size() == texts.size(),
                  "★ U10b 两张表按下标一一对齐（按钮上的字 / 路径键），少一条就串位");
            check(texts.value(0) == QStringLiteral("版本目录") &&
                      texts.value(1) == QStringLiteral("日志") &&
                      texts.value(2) == QStringLiteral("游戏目录") &&
                      texts.value(3) == QStringLiteral("崩溃报告") &&
                      texts.value(4) == QStringLiteral("配置") &&
                      keys.value(0) == QStringLiteral("dir") &&
                      keys.value(4) == QStringLiteral("config"),
                  "★ U10b 五条的顺序与键（用户点名「合理顺序」：版本目录 → 日志 → 游戏"
                  "目录 → 崩溃报告 → 配置）",
                  QStringLiteral("第 0 条 = %1 / 第 4 条 = %2")
                      .arg(texts.value(0), keys.value(4)));
            // 几何得先把这一页摆到台面上再量：QStackedLayout 在 StackOne 模式下**只给当前
            // 那一页设几何**，收着的页压根没被摆过 —— 直接量会读到一排 0。
            showPage(dialog, Dlg::overviewCategoryKey());
            // 三颗一排：前三颗同一条 y、第四颗换行。
            const bool sameRow = buttons.size() == texts.size() &&
                                 buttons.at(0)->y() == buttons.at(1)->y() &&
                                 buttons.at(1)->y() == buttons.at(2)->y();
            const bool wrapped = buttons.size() == texts.size() &&
                                 buttons.at(3)->y() > buttons.at(0)->y() &&
                                 buttons.at(0)->x() < buttons.at(1)->x();
            check(sameRow && wrapped,
                  "★ U10b 三颗一排、第四颗换行（尺寸照默认那颗，不给它抻）");

            QString lastPath;
            int opens = 0;
            // NOTE: 这条连接的**上下文必须是这一块里的东西**（一个栈上的 `QObject`），
            // 不能是 `&dialog`。`lastPath` / `opens` 是这一块里的栈变量，块一结束它们就
            // 没了，而 `&dialog` 活到整个 U10 块结束 —— 连接留着的话，后面任何一次
            // `openInstanceFolderRequested` 都会回来写 `lastPath`。而那一发信号在
            // `InstanceSettingsDialog` 里是**信号接信号**转出去的（每一页的
            // `openFolderRequested` → `&InstanceSettingsDialog::openInstanceFolderRequested`，
            // 见 `createFilePage()` 那一段），所以 U10e 点一下模组那一页的「打开文件夹」就
            // 会把它带回这里 —— 往一块已经析构的栈槽里做 `QString::operator=`，旧值那份
            // `QString` 的 `QArrayData` 早就被释放了。
            //
            // 症状很偏：PageHeap 下崩溃落在 `QArrayData::deref()` 写一页已释放的内存的
            // 写指令上，可那一块数据是「游戏目录」那条路径，于是崩点被报成
            // `InstanceSettingsDialog::~InstanceSettingsDialog()` 释放 `m_fileDirs` 那条
            // （和真正的写入点隔着一整节）。正常入口下它只是悄悄把那一块提前放掉，什么
            // 都不报 —— 这就是当年那条“偶发 `c0000374`”的另一半（见 docs/TESTING.md
            // 「历史崩溃复核」）。
            QObject scopeGuard;
            QObject::connect(&dialog, &Dlg::openInstanceFolderRequested, &scopeGuard,
                             [&lastPath, &opens](const QString &path) {
                                 lastPath = path;
                                 ++opens;
                             });
            auto *first = qobject_cast<QPushButton *>(buttons.at(0));
            auto *logsButton = qobject_cast<QPushButton *>(buttons.at(1));
            auto *gameButton = qobject_cast<QPushButton *>(buttons.at(2));
            check(first != nullptr && logsButton != nullptr && gameButton != nullptr,
                  "U10b 那五颗是普通按钮（不是自己画的那种）");
            if (first != nullptr && logsButton != nullptr && gameButton != nullptr) {
                first->click();
                const bool dirOk = lastPath == root;
                logsButton->click();
                const bool logsOk = lastPath == dirs.value(QStringLiteral("logs"));
                gameButton->click();
                const bool gameOk = lastPath == dirs.value(QStringLiteral("game"));
                check(opens == 3 && dirOk && logsOk && gameOk,
                      "★ U10b 每颗开的都是自己那个目录（版本目录 / 日志 / 游戏目录 —— 路径现场"
                      "问，不是构造那一刻的快照）",
                      QStringLiteral("收到 %1 次，最后一条 %2").arg(opens).arg(lastPath));
            }
        }

        // ---- U10b2：那一排分成两段（在盘的在外、其余收在「更多目录…」里）----
        //
        // 要求原话：「仅在**目录存在**或**相关模组可识别**时优先展示，其他项
        // 收在'更多目录'」。**判据不在这儿**（哪几路目录在盘上、哪个实例装了 ReplayMod 都
        // 只有 MainWindow 知道，见 MainWindow::instanceCommonFolderKeys()）—— 这一层只认
        // 递进来的那张表，所以这一条量的是"递进去一张只有三路的表之后，五颗分别在哪一段"。
        {
            showPage(dialog, Dlg::overviewCategoryKey());
            const QList<QWidget *> buttons = dialog.shortcutButtons();
            const QStringList keys = Dlg::shortcutPathKeys();
            // ① 默认这一档（没人喊过 setFolderPresentKeys）：五颗全在外面，**没有**
            // 「更多目录…」—— 不摆一颗按下去什么都不变的按钮。
            check(dialog.shortcutMoreButton() == nullptr &&
                      dialog.shortcutHiddenButtons().isEmpty() && !dialog.shortcutMoreExpanded(),
                  "★ U10b2 没人说过哪几路「现在有东西」时：五颗全摆在外面、也不摆"
                  "「更多目录…」（一个都没收起来 ⇒ 那颗开关没有存在的理由）");

            // ② 只在盘上三路（版本目录 / 游戏目录 / 配置）⇒ 外面三颗 + 第四格那颗开关，其余收着。
            // 收着的是 `setVisible(false)`（**还在表里**、下标一个没挪），不是删控件。
            const QStringList present = {QStringLiteral("dir"), QStringLiteral("game"),
                                         QStringLiteral("config")};
            dialog.setFolderPresentKeys(present);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
            QApplication::processEvents();
            auto *more = dialog.shortcutMoreButton();
            const QList<QWidget *> hidden = dialog.shortcutHiddenButtons();
            check(more != nullptr && more->text() == Dlg::shortcutMoreText() &&
                      !dialog.shortcutMoreExpanded(),
                  "★ U10b2 真有东西的只有三路 ⇒ 第四格是「更多目录…」（收着的时候写的就是这"
                  "四个字）");
            check(hidden.size() == keys.size() - present.size(),
                  "★ U10b2 另外那几路收在「更多目录」里（不是删掉 —— 它们照旧在表里）",
                  QStringLiteral("收着 %1 颗，应 %2 颗")
                      .arg(hidden.size())
                      .arg(keys.size() - present.size()));
            // 该在外面的 / 该收着的，按**位置**（表序）认：外面那三颗是 at(0)/at(2)/at(4)。
            const QList<int> outside = {0, 2, 4};
            bool seenOk = buttons.size() == keys.size();
            for (int i = 0; i < buttons.size(); ++i) {
                seenOk = seenOk &&
                         (buttons.at(i)->isVisibleTo(&dialog) == outside.contains(i));
            }
            check(seenOk,
                  "★ U10b2 该在外面的看得见、该收着的真看不见（按 isVisibleTo 数的，不是"
                  "「有没有这个控件」）");
            check(more != nullptr && buttons.value(0) != nullptr &&
                      more->y() > buttons.at(0)->y() && more->x() == buttons.at(0)->x(),
                  "★ U10b2 那颗开关接着外面那三颗往下排（第四格 —— 不是另起一块、也不是"
                  "甩到行尾）");

            // ③ 点一下 → 五颗全露出来（开关自己改叫「收起更多目录」）。
            if (more != nullptr) {
                more->click();
                QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
                QApplication::processEvents();
                bool allSeen = true;
                for (QWidget *button : buttons) {
                    allSeen = allSeen && button->isVisibleTo(&dialog);
                }
                check(dialog.shortcutMoreExpanded() && more->text() == Dlg::shortcutMoreLessText() &&
                          dialog.shortcutHiddenButtons().isEmpty() && allSeen,
                      "★ U10b2 点一下「更多目录…」⇒ 五颗全露出来，那颗开关改口叫"
                      "「收起更多目录」");
                // 再点一下 → 收回去（这一档必须是**可逆**的，否则用户点开就关不掉了）。
                more->click();
                QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
                QApplication::processEvents();
                bool backOk = buttons.size() == keys.size();
                for (int i = 0; i < buttons.size(); ++i) {
                    backOk = backOk &&
                             (buttons.at(i)->isVisibleTo(&dialog) == outside.contains(i));
                }
                check(!dialog.shortcutMoreExpanded() && more->text() == Dlg::shortcutMoreText() &&
                          backOk,
                      "★ U10b2 再点一下收回去，原样（「收起更多目录」是可逆的）");
            }

            // ④ 摆回"全都在"：后面那几节（U10c…U12 / U0j）还按五颗全在外面量。
            // NOTE: 这一发会把「更多目录…」那颗**连控件一起删掉**，所以 `more` 从那以后
            // 就是野指针了 —— 上面那几条一个都不能挪到这后面来。
            dialog.setFolderPresentKeys(keys);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
            QApplication::processEvents();
            check(dialog.shortcutMoreButton() == nullptr &&
                      dialog.shortcutHiddenButtons().isEmpty(),
                  "★ U10b2 摆回「全都在」⇒「更多目录…」那一颗整颗消失（不是留着不画）");
        }

        // ---- U10c 模组那一页：列目录、排序、被禁用那一条怎么显示 ----
        {
            showPage(dialog, QStringLiteral("mod"));
            InstanceFilesPage *page = dialog.filesPage(QStringLiteral("mod"));
            check(page != nullptr && page->list()->count() == 3,
                  "U10c 模组那一页把三条都列出来了（启用 / 启用 / 禁用）",
                  QStringLiteral("%1 条").arg(page != nullptr ? page->list()->count() : -1));
            check(itemText(page, 0) == QStringLiteral("iris-1.6.5.jar") &&
                      itemText(page, 1) == QStringLiteral("sodium.jar"),
                  "U10c 启用着的按名字排（目录在前那条规则用不上 —— 这里没有子目录）",
                  QStringLiteral("%1 | %2").arg(itemText(page, 0), itemText(page, 1)));
            check(itemText(page, 2) ==
                      QStringLiteral("禁用掉的.jar") + InstanceFilesPage::disabledMark(),
                  "★ U10c `.jar.disabled` 显示成**原名 + 一个记号**（盘上那个后缀不露给用户）",
                  itemText(page, 2));
            check(!itemFlag(page, 0, InstanceFilesPage::disabledRole()) &&
                      itemFlag(page, 2, InstanceFilesPage::disabledRole()),
                  "★ U10c 哪一条禁用着走角色位（不看文字 —— 文字是显示内容）");
            check(itemPath(page, 0) == modsDir + QStringLiteral("/iris-1.6.5.jar") &&
                      itemPath(page, 2) == modsDir + QStringLiteral("/禁用掉的.jar.disabled"),
                  "★ U10c 每一条记的是**盘上那个真路径**（禁用那条带着 .disabled）",
                  itemPath(page, 2));
        }

        // ---- U10P 顶上那条搜索 + 按钮下面那栏状态----
        //
        // 要求：「模组等的下载新/刷新/打开文件夹的**上方**加一个搜索，三个按钮的
        // **下方**一个选择 全部/启用/禁用/可更新」。这一段量四件事：两排控件在不在、
        // 摆的位置对不对（上下是拿 **y 坐标**量的，不是"我记得写在源码哪一行"）、
        // 筛得对不对（搜索 / 状态 / 两样同时）、以及"筛完一条不剩"和"目录本来就是空的"
        // 是不是两个状态。
        {
            showPage(dialog, QStringLiteral("mod"));
            InstanceFilesPage *page = dialog.filesPage(QStringLiteral("mod"));
            QLineEdit *search = page != nullptr ? page->searchEdit() : nullptr;
            const auto stateButton = [page](InstanceFilesPage::StateFilter filter) -> QPushButton * {
                return page != nullptr ? page->stateButtonAt(filter) : nullptr;
            };
            QPushButton *state = stateButton(InstanceFilesPage::StateFilter::All);
            check(search != nullptr && state != nullptr &&
                      search->objectName() == InstanceFilesPage::searchEditName() &&
                      state->objectName() == InstanceFilesPage::stateButtonName(
                                                InstanceFilesPage::StateFilter::All) &&
                      search->placeholderText() == InstanceFilesPage::searchPlaceholder(),
                  "U10P 模组那一页有一条搜索框、一排状态按钮（名字和那格灰字都走那几个唯一出处）");
            check(page->stateCombo() != nullptr && !page->stateCombo()->isVisibleTo(page) &&
                      page->stateLabel() != nullptr && !page->stateLabel()->isVisibleTo(page) &&
                      page->stateLabel()->objectName() == InstanceFilesPage::stateLabelName() &&
                      page->stateLabel()->text() == InstanceFilesPage::stateLabelText(),
                  "★ U10P 开了批量的这一页上**那个下拉框和那个「显示」标签收着**（文字还是走"
                  "唯一出处 stateLabelText() —— 没开批量的那几页照样看得到它）");

            // Compact mod header: search/maintenance share a row, filters below,
            // list below filters. The old dedicated directory toolbar is retired.
            const int searchY = search != nullptr ? search->mapTo(page, QPoint(0, 0)).y() : -1;
            auto *headerMore = page->findChild<QPushButton *>(QStringLiteral("modHeaderMore"));
            const int stateY = state != nullptr ? state->mapTo(page, QPoint(0, 0)).y() : -1;
            const int listY = page->list()->mapTo(page, QPoint(0, 0)).y();
            check(searchY >= 0 && searchY < stateY && stateY < listY && headerMore &&
                      headerMore->isVisibleTo(page) && headerMore->parentWidget() == search->parentWidget() &&
                      !page->refreshButton()->isVisibleTo(page),
                  "★★ U10P 搜索和维护菜单同排，筛选在下方，列表不被顶部工具覆盖",
                  QStringLiteral("搜索 y=%1 / 状态 y=%2 / 列表 y=%3")
                      .arg(searchY)
                      .arg(stateY)
                      .arg(listY));

            // ---- U10P-2 那四颗：字（含条数）、顺序、默认档 ----
            //
            // 顺序走 `selectionActionCount` 那一套的兄弟 —— 四颗是**按下标**摆的
            // （`static_cast<StateFilter>(i)`，见 .cpp 构造那段），所以"第几颗是哪个档"
            // 就是枚举自己的顺序。文字里带**条数**：这一页三条（两条启用、一条禁用、
            // 还没查过更新）⇒ 全部(3) 启用(2) 禁用(1) 可更新(0)。
            QStringList stateTexts;
            for (int i = 0; i < 4; ++i) {
                QPushButton *button =
                    stateButton(static_cast<InstanceFilesPage::StateFilter>(i));
                stateTexts.append(button != nullptr ? button->text() : QStringLiteral("<没有>"));
            }
            check(stateTexts ==
                      QStringList({InstanceFilesPage::stateFilterButtonText(
                                       InstanceFilesPage::StateFilter::All, 3),
                                   InstanceFilesPage::stateFilterButtonText(
                                       InstanceFilesPage::StateFilter::Enabled, 2),
                                   InstanceFilesPage::stateFilterButtonText(
                                       InstanceFilesPage::StateFilter::Disabled, 1),
                                   InstanceFilesPage::stateFilterButtonText(
                                       InstanceFilesPage::StateFilter::Update, 0)}),
                  "★ U10P 那四颗就是「全部(3) 启用(2) 禁用(1) 可更新(0)」—— 顺序和条数都对"
                  "（条数说的是**这个目录里有多少条是这样**，和筛没筛无关；还没查过的时候"
                  "那一颗整颗收着，见 U10P-6）",
                  stateTexts.join(QStringLiteral(" | ")));
            check(state->isChecked() &&
                      page->stateFilter() == InstanceFilesPage::StateFilter::All &&
                      page->list()->count() == 3,
                  "U10P 一进来亮着的是「全部」（三条原样列着，不筛）",
                  QStringLiteral("%1 条").arg(page->list()->count()));

            // ---- U10P-3 搜索：子串、不分大小写、盘上那个名字也认 ----
            //
            // 一律走 `setText()`（= 用户敲字那条路，textChanged 立刻生效）—— 直接调
            // setSearchText() 量不到"接没接上"，而那一句才是这一段的重点。
            search->setText(QStringLiteral("sod"));
            check(page->list()->count() == 1 &&
                      itemText(page, 0) == QStringLiteral("sodium.jar"),
                  "★ U10P 搜 `sod` 只剩 sodium.jar（子串就中）",
                  QStringLiteral("%1 条").arg(page->list()->count()));
            search->setText(QStringLiteral("SOD"));
            check(page->list()->count() == 1,
                  "★ U10P 不分大小写（`SOD` 照样中）—— 名字里那句话是怎么写的用户不一定记得");
            search->setText(QStringLiteral(".disabled"));
            check(page->list()->count() == 1 &&
                      itemPath(page, 0) == modsDir + QStringLiteral("/禁用掉的.jar.disabled"),
                  "★ U10P 盘上那个名字也认（禁用着的那条盘上叫 `.jar.disabled`，用户手里"
                  "那个文件就是这么叫的）",
                  itemPath(page, 0));
            search->setText(QStringLiteral("iris"));
            check(page->list()->count() == 1 &&
                      itemPath(page, 0) == modsDir + QStringLiteral("/iris-1.6.5.jar"),
                  "★ U10P 每一条搜出来的仍然是**盘上那个真路径**（行是重列出来的，不是藏起来的）");
            search->clear();
            check(page->list()->count() == 3 && page->searchText().isEmpty(),
                  "U10P 清掉搜索词，三条都回来");

            // ---- U10P-4 状态：选哪一档就只剩哪一档 ----
            //
            // 走 `setCurrentIndex()`（= 用户点那一栏的路，currentIndexChanged）—— 同上，
            // 直接调 setStateFilter() 量不到"接没接上"。
            // 走 `click()`（= 用户点那一颗的路）—— 直接调 setStateFilter() 量不到"接没接上"。
            const auto selectState = [&stateButton](InstanceFilesPage::StateFilter filter) {
                QPushButton *button = stateButton(filter);
                if (button == nullptr || !button->isEnabled()) {
                    return false;
                }
                button->click();
                return true;
            };
            check(selectState(InstanceFilesPage::StateFilter::Disabled) &&
                      page->list()->count() == 1 &&
                      itemFlag(page, 0, InstanceFilesPage::disabledRole()),
                  "★ U10P 点「禁用」只剩那一条禁用着的（走角色位判，不看文字）",
                  QStringLiteral("%1 条").arg(page->list()->count()));
            check(page->stateFilter() == InstanceFilesPage::StateFilter::Disabled &&
                      stateButton(InstanceFilesPage::StateFilter::Disabled) != nullptr &&
                      stateButton(InstanceFilesPage::StateFilter::Disabled)->isChecked() &&
                      !state->isChecked(),
                  "★ U10P 点上那一颗自己就亮起来、原来亮着的那一颗灭掉（四颗是一组单选）");
            check(selectState(InstanceFilesPage::StateFilter::Enabled) &&
                      page->list()->count() == 2,
                  "★ U10P 选「启用」剩那两条（`.jar.disabled` 那条不算）",
                  QStringLiteral("%1 条").arg(page->list()->count()));
            // 两样**同时**生效（是与关系）：名字里有 iris **而且**是启用着的。
            search->setText(QStringLiteral("iris"));
            check(page->list()->count() == 1 &&
                      itemText(page, 0) == QStringLiteral("iris-1.6.5.jar"),
                  "★ U10P 搜索和状态**同时**生效（换一档不把搜索词清掉）",
                  QStringLiteral("%1 条").arg(page->list()->count()));
            search->clear();

            // ---- U10P-5 筛完一条不剩 ≠ 目录是空的 ----
            search->setText(QStringLiteral("zzz-一个文件都不叫这个"));
            check(!page->list()->isVisibleTo(page) && page->noticeLabel()->isVisibleTo(page) &&
                      page->noticeLabel()->text() == InstanceFilesPage::noMatchText(),
                  "★ U10P 筛完一条不剩：列表收起来、说的是「没有符合条件的条目。」—— 和目录"
                  "本来就空那句（「这个文件夹还是空的。」）**不是同一句**",
                  page->noticeLabel()->text());
            check(search->isVisibleTo(page) && state->isVisibleTo(page) &&
                      headerMore && headerMore->isVisibleTo(page),
                  "★★ U10P 筛选为空时搜索、状态和维护菜单仍可访问，能清除条件和刷新");
            // 清搜索词（状态栏这会儿还停在上面选的「启用」上 ⇒ 回来的是那两条，不是三条 ——
            // 这正是"两样同时生效"的另一面）。
            search->clear();
            check(page->list()->count() == 2 && page->list()->isVisibleTo(page),
                  "U10P 搜索词清掉，列表立刻回来（状态栏那一档没跟着被清掉）",
                  QStringLiteral("%1 条").arg(page->list()->count()));
            selectState(InstanceFilesPage::StateFilter::All);

            // ---- U10P-6 「可更新」那一颗：靠的是那一趟检测的结果 ----
            //
            // 这一页这会儿是三条：两条启用、一条禁用、**还没查过更新**。
            check(stateButton(InstanceFilesPage::StateFilter::Update) != nullptr &&
                      stateButton(InstanceFilesPage::StateFilter::Update)->isHidden() &&
                      !stateButton(InstanceFilesPage::StateFilter::Disabled)->isHidden(),
                  "★★ U10P 还没查过更新时，「可更新」那一颗**整颗收着**（用户原话「没有检查"
                  "的时候先隐藏可更新按钮」）—— 旁边「禁用(1)」那颗照旧露着，说明收的是"
                  "它自己、不是整排都藏了",
                  stateButton(InstanceFilesPage::StateFilter::Update)->text());
            // 于是条件也**落不到**那一档上（`setStateFilter()` 撞上一颗摆不出来的档会把条件
            // 退回「全部」—— 两条路走的是同一个 `stateFilterVisible()`）。
            {
                selectState(InstanceFilesPage::StateFilter::Update);
                check(page->stateFilter() == InstanceFilesPage::StateFilter::All &&
                          page->list()->count() == 3,
                      "★★ U10P 那一颗收着的时候，条件**落不到「可更新」上**（退回「全部」）"
                      "—— 不然屏幕上就是一条都不剩的列表，而两套控件里没有一格亮着",
                      QStringLiteral("%1 条").arg(page->list()->count()));
            }
            check(selectState(InstanceFilesPage::StateFilter::All) &&
                      page->stateFilter() == InstanceFilesPage::StateFilter::All,
                  "U10P 收回「全部」上");
            // 喂一份结果：走对话框那条真路（`setFileUpdates` 只递给开得了这一格的那几页）。
            dialog.setFileUpdates({modsDir + QStringLiteral("/sodium.jar")}, true);
            check(!stateButton(InstanceFilesPage::StateFilter::Update)->isHidden() &&
                      stateButton(InstanceFilesPage::StateFilter::Update)->text() ==
                          InstanceFilesPage::stateFilterButtonText(
                              InstanceFilesPage::StateFilter::Update, 1),
                  "★ U10P 查出有新版本的那几条之后，那一颗**自己冒出来**、条数就是查出来的条数",
                  stateButton(InstanceFilesPage::StateFilter::Update)->text());
            check(selectState(InstanceFilesPage::StateFilter::Update) &&
                      page->list()->count() == 1 &&
                      itemPath(page, 0) == modsDir + QStringLiteral("/sodium.jar"),
                  "★ U10P 点上去「可更新」只剩查出来有新版本的那几条",
                  QStringLiteral("%1 条").arg(page->list()->count()));
            // 「一条都不剩」再喂一遍：那一颗**缩回去**，而条件必须**退回「全部」** ——
            // 留着的话屏幕上是一条都不剩的列表，而四颗里没有任何一颗亮着。
            dialog.setFileUpdates({}, true);
            check(stateButton(InstanceFilesPage::StateFilter::Update)->isHidden() &&
                      page->stateFilter() == InstanceFilesPage::StateFilter::All &&
                      page->list()->count() == 3 && page->list()->isVisibleTo(page),
                  "★★ U10P 那一份名单变空之后：那颗收起来、条件退回「全部」、三条原样回来"
                  "（不是留一个筛空了列表、却没有任何一颗亮着的界面）",
                  QStringLiteral("%1 条").arg(page->list()->count()));

            // ---- U10P-7 行里那个 ↑ 和禁用那一条的删除线（★★ ）----
            //
            // 要求：「**可更新的模组在名称后面显示一个上箭头（和名字一个字号），禁用的
            // 模组名字标灰，删除线覆盖**」。
            //
            // 这一页三条：`iris-1.6.5.jar`（启用）、`sodium.jar`（启用）、
            // `禁用掉的.jar.disabled`（禁用）。喂一份名单说 sodium 有新版本 ⇒ 只有它那一行
            // 该长出 ↑，而禁用那一行的名字该是划掉的。
            {
                dialog.setFileUpdates({modsDir + QStringLiteral("/sodium.jar")}, true);
                check(page->list()->count() == 3,
                      "U10P 前提：那份名单回来之后三条**照旧都列着**（↑ 是长在行上的，不筛"
                      "列表 —— 筛是上面那颗「可更新」的事）",
                      QStringLiteral("%1 条").arg(page->list()->count()));
                const auto rowOf = [page, &itemPath](const QString &path) {
                    for (int i = 0; i < page->list()->count(); ++i) {
                        if (itemPath(page, i) == path) {
                            return i;
                        }
                    }
                    return -1;
                };
                const int rowUpdate = rowOf(modsDir + QStringLiteral("/sodium.jar"));
                const int rowPlain = rowOf(modsDir + QStringLiteral("/iris-1.6.5.jar"));
                const int rowOff = rowOf(modsDir + QStringLiteral("/禁用掉的.jar.disabled"));
                check(rowUpdate >= 0 && rowPlain >= 0 && rowOff >= 0,
                      "U10P 前提：三条各按**盘上那个真路径**各找到一行（下面按行取控件）",
                      QStringLiteral("%1 / %2 / %3").arg(rowUpdate).arg(rowPlain).arg(rowOff));
                QLabel *name = rowUpdate >= 0 ? page->nameLabelAt(rowUpdate) : nullptr;
                QLabel *plainName = rowPlain >= 0 ? page->nameLabelAt(rowPlain) : nullptr;
                check(name != nullptr && plainName != nullptr &&
                          name->text() == InstanceFilesPage::nameTextWithUpdateMark(
                                              QStringLiteral("sodium.jar"), true) &&
                          plainName->text() ==
                              InstanceFilesPage::nameTextWithUpdateMark(
                                  QStringLiteral("iris-1.6.5.jar"), false),
                      "★★ U10P 那个 ↑ **只长在有新版本的那一行上**，而且就是名字那一格**同一串"
                      "字的尾巴**（用户原话「在名称后面显示一个上箭头」+「和名字一个字号」"
                      "—— 同一格同一个字体，「紧挨着」和「一样大」两条都是结构保证的，"
                      "不是调出来的）",
                      QStringLiteral("有新版本那行「%1」/ 没有的那行「%2」")
                          .arg(name != nullptr ? name->text() : QStringLiteral("<null>"),
                               plainName != nullptr ? plainName->text()
                                                    : QStringLiteral("<null>")));
                QLabel *offName = rowOff >= 0 ? page->nameLabelAt(rowOff) : nullptr;
                check(offName != nullptr && !offName->text().contains(
                                                InstanceFilesPage::updateArrowText()),
                      "★★ U10P 禁用着的那一条**没有**那个 ↑（这一趟压根不查它 —— "
                      "见 MainWindow::checkInstanceUpdates() 里「禁用着的不查」那条）",
                      offName != nullptr ? offName->text() : QStringLiteral("<null>"));
                const auto colorOf = [](QLabel *label) {
                    return label != nullptr
                               ? label->palette().color(QPalette::WindowText)
                               : QColor();
                };
                check(offName != nullptr && offName->font().strikeOut(),
                      "★★ U10P 被禁用那一条的名字**划了一道删除线**（用户原话「禁用的模组名字"
                      "标灰，删除线覆盖」）—— 走的是字体上的 strikeOut，不是画上去的一条线",
                      QStringLiteral("划没划：%1").arg(offName != nullptr
                                                          ? offName->font().strikeOut()
                                                          : false));
                check(name != nullptr && !name->font().strikeOut() && offName != nullptr &&
                          colorOf(offName) != colorOf(name),
                      "★★ U10P 而且**只有禁用那一条**名字是灰的、也只有它被划掉（旁边那条"
                      "启用的：正文色、没有删除线）—— 灰走的是和「大小 / 时间」同一档的次要"
                      "色（全仓唯一去处 QPalette::PlaceholderText）",
                      QStringLiteral("启用那条 %1 / 禁用那条 %2")
                          .arg(colorOf(name).name(), colorOf(offName).name()));
                // 再钉一条纯函数上的：没有新版本的行**一个字都不多**（别哪天在那儿也缀上一个
                // 空格 —— 那一格是拿 width 算省略号的，多一个空格就是另一串省略结果）。
                check(InstanceFilesPage::nameTextWithUpdateMark(QStringLiteral("iris-1.6.5.jar"),
                                                               false) ==
                          QStringLiteral("iris-1.6.5.jar"),
                      "★★ U10P 没查出来新版本的那些行原样就是那个名字（唯一出处那边一行都不加）");
                // 喂回去（这一节后面那一段量的另一页，别把这一页留在"查过"那一档上）。
                dialog.setFileUpdates({}, true);
            }

            // ---- U10P-8 同页筛选：**只淡新冒出来的那几行**，整页不重播（★ ）----
            //
            // 要求那一句：「同页筛选/列表变化：只动画**新增**、删除和状态，**不重播整页**」。
            // 这一段的判据全是**结构**上的（不是"看着好像淡了一下"）：
            // / 这一趟真**新冒出来**的行，每一个身上挂着一个效果、而且那一发动画的时长
            // 就是 `rowAppearMs()`（效果挂在行上，动画是效果的子对象 —— 这儿的够法
            // 和 ⑬ 那一段够幕布、够换页动画是同一招：只认公开类型 + `findChildren`）；
            // / 这一趟**本来就在**的行，一个效果都没有 —— 这就是「不重播整页」：旧的那几行
            // 要是也被淡了，屏幕上就是整页在闪；
            // / 整个过程里行和列表的**几何一个像素都不动**。
            //
            // 判据里"哪几行是新的"是**构造出来的**：先筛到一条不剩（`m_shownIdentities`
            // 清空），再筛回来 ⇒ 那三条在重建这一趟里全是新的。反过来，紧接着换到「禁用」
            // 那一档时那一条**本来就在名单里**⇒ 一条都不该淡。
            {
                const bool animWas = Anim::enabled();   // 这个工程平时关着，收尾要摆回去
                Anim::setEnabled(true);
                const auto rowWidgetAt = [page](int row) -> QWidget * {
                    QListWidgetItem *item = page != nullptr ? page->list()->item(row) : nullptr;
                    return item != nullptr ? page->list()->itemWidget(item) : nullptr;
                };
                // 「这一行正在被淡进来吗」= 身上挂着**我们那个效果**、而且那一发动画的时长
                // 就是 `rowAppearMs()`。★ 那一发动画挂在**效果**身上（`runProgress()` 的
                // `owner` 就是它），不挂在控件身上 —— 要够它得先够到那个效果（名字是全仓
                // 唯一那个字符串，和上面够幕布、够步进器是同一招）。
                const auto rowAnimIsFade = [](QWidget *row) {
                    QGraphicsEffect *effect = row != nullptr ? row->graphicsEffect() : nullptr;
                    if (effect == nullptr ||
                        effect->objectName() != QStringLiteral("wtfmlAnimEffect")) {
                        return false;
                    }
                    const QList<QVariantAnimation *> anims =
                        effect->findChildren<QVariantAnimation *>();
                    return anims.size() == 1 && anims.first()->duration() == Anim::rowAppearMs();
                };
                // 「不许有元素超出」在这一发上的样子：效果自己要回来的那个矩形**正好就是
                // 这一行自己**（`QGraphicsEffect::boundingRectFor()` 是公开的虚函数，
                // 够得到它不用把产品那边的类型露出来）。这一条才是"淡的时候会不会糊出
                // 行外面去"的正经判据 —— 列表整块被收起/放出来会挪位置（那是布局的活），
                // 拿列表的几何跟重列之前比是量的东西不对。
                const auto rowEffectKeepsBounds = [](QWidget *row) {
                    QGraphicsEffect *effect = row != nullptr ? row->graphicsEffect() : nullptr;
                    if (effect == nullptr) {
                        return false;
                    }
                    const QRectF own(row->rect());
                    return effect->boundingRectFor(own) == own;
                };
                // 前提：筛到一条不剩（一趟之后"上一趟摆过的身份"就是空的）
                search->setText(QStringLiteral("zzz-一个文件都不叫这个"));
                const bool emptied = page->list()->count() == 0;
                // 再筛回来：三条全是新的 ⇒ 三条都该在淡
                search->clear();
                const bool refilled = page->list()->count() == 3;
                int faded = 0;
                int boundsKept = 0;
                for (int row = 0; row < page->list()->count(); ++row) {
                    QWidget *w = rowWidgetAt(row);
                    if (rowAnimIsFade(w)) {
                        ++faded;
                    }
                    if (rowEffectKeepsBounds(w)) {
                        ++boundsKept;
                    }
                }
                // 这一趟**故意不把事件循环再跑起来**：要求的那两条（"新冒出来的在淡"
                // 和"本来就在的不淡"）都是**接线上**的事，重列那一发是同步的，当场就量得
                // 到。"动画跑起来那一段时间里几何不动"由 ⑬ 那一段自己量（它本来就有一整
                // 段真帧），这一段不必再往这一段 U10 里塞一发事件循环。
                check(emptied && refilled && faded == 3 && boundsKept == 3,
                      "★★ U10P 筛到一条不剩再筛回来：重新冒出来的那三条**都**在淡（一人一个"
                      "效果、时长就是 `rowAppearMs()`），而且效果要回来的那个矩形**正好就是"
                      "那一行自己**（多一个像素都不行 —— 用户点名的「不许有元素超出」）",
                      QStringLiteral("筛空%1 / 筛回来%2 条 / 在淡的 %3 条 / 框住自己的 %4 条")
                          .arg(emptied ? QStringLiteral("成") : QStringLiteral("没成"))
                          .arg(page->list()->count())
                          .arg(faded)
                          .arg(boundsKept));
                // 反面：换一档**不引入任何新行**（那一条本来就在上一趟的名单里）⇒ 一条都不淡。
                // 这一条正是「不重播整页」：列表整个重建过，可屏幕上没有哪一行在闪。
                selectState(InstanceFilesPage::StateFilter::Disabled);
                QWidget *keptRow = page->list()->count() == 1 ? rowWidgetAt(0) : nullptr;
                check(page->list()->count() == 1 && keptRow != nullptr &&
                          keptRow->graphicsEffect() == nullptr,
                      "★★ U10P 而换一档时**本来就在的那几行一个效果都不挂**（列表照样整个重"
                      "建过，可屏幕上没有哪一行在闪 —— 「不重播整页」量的就是这一条）",
                      QStringLiteral("换档后 %1 条、那一条身上%2")
                          .arg(page->list()->count())
                          .arg(keptRow != nullptr && keptRow->graphicsEffect() != nullptr
                                   ? QStringLiteral("挂着效果")
                                   : QStringLiteral("干净")));
                // 收尾把这一页摆回原样（下面那几段量的还是这三条、这一档）：动画开关也要
                // 摆回去 —— 这个工程往后整段都是按"关着"搭界面的。
                selectState(InstanceFilesPage::StateFilter::All);
                search->clear();
                Anim::setEnabled(animWas);
            }

            // ---- U10P-6b 换一页，把「收空档」的另一个方向也钉住 ----
            //
            // 上面那几条量的都是「可更新」那一档；这一条换成**状态**那两档：资源包那一页
            // 两条都是启用着的（一个 zip + 一个目录）、也没有查出来可更新的 ⇒ 「禁用」「可
            // 更新」两颗都收着，而「全部」恒在（用户点名的那半句：「…全部的默认显示」）。
            {
                showPage(dialog, QStringLiteral("resourcepack"));
                InstanceFilesPage *packs = dialog.filesPage(QStringLiteral("resourcepack"));
                const bool shapeOk = packs != nullptr && packs->list()->count() == 2;
                check(shapeOk, "U10P 前提：资源包那一页摆着两条（一个 zip + 一个目录），"
                               "而且都启用着",
                      packs != nullptr ? QStringLiteral("%1 条").arg(packs->list()->count())
                                       : QStringLiteral("<没这一页>"));
                if (shapeOk) {
                    check(!packs->stateButtonAt(InstanceFilesPage::StateFilter::All)->isHidden() &&
                              packs->stateButtonAt(InstanceFilesPage::StateFilter::Enabled)
                                  ->isHidden() &&
                              packs->stateButtonAt(InstanceFilesPage::StateFilter::Disabled)
                                  ->isHidden() &&
                              packs->stateButtonAt(InstanceFilesPage::StateFilter::Update)
                                  ->isHidden(),
                          "★★ U10P 一条都没有的那几档（启用 / 禁用 / 可更新）**整颗收着**，"
                          "只剩「全部(2)」—— 「启用」跟着「禁用」收，是因为这一页一条禁用都"
                          "没有（用户点名的那条）");
                }
                // 回到模组那一页（这一段剩下的几条都在它身上量）。
                showPage(dialog, QStringLiteral("mod"));
            }

            // ---- U10P-7 哪几页有「可更新」：判据和「下载新 X」同一张表 ----
            const QStringList withUpdate = {
                QStringLiteral("mod"), QStringLiteral("resourcepack"),
                QStringLiteral("datapack"), QStringLiteral("shader")};
            bool updateOk = true;
            QString updateNote;
            for (const QString &key : Dlg::filePageKeys()) {
                if (key == Dlg::serverPageKey()) {
                    continue;
                }
                InstanceFilesPage *other = dialog.filesPage(key);
                if (other == nullptr) {
                    updateOk = false;
                    updateNote += QStringLiteral("%1=<没这一页> ").arg(key);
                    continue;
                }
                // 开了批量的那四页：那一档在不在看 `canFilterUpdates()`；
                // 没开批量的那几页：还是数下拉框里几格（两套控件都留着）。
                const int got = other->batchMode() ? (other->canFilterUpdates() ? 4 : 3)
                                                   : (other->stateCombo() != nullptr
                                                          ? other->stateCombo()->count()
                                                          : -1);
                const int want = withUpdate.contains(key) ? 4 : 3;
                if (got != want) {
                    updateOk = false;
                    updateNote += QStringLiteral("%1=%2 ").arg(key).arg(got);
                }
            }
            check(updateOk,
                  "★ U10P 「可更新」只有下载页里真有那一栏的那四页有（模组/资源包/数据包/光影）"
                  "—— 原理图 / 存档 / 配置文件那几页上，多一格永远筛不出东西的选项等于摆一个坏键",
                  updateNote);
            // 选着「可更新」的时候把那一档拿掉（`setCanFilterUpdates(false)`）：条件必须
            // **退回「全部」** —— 留着的话屏幕上是一条都不剩的列表，而那一栏里没有任何
            // 一格亮着（列表和条件对不上，且没有任何东西会自己纠正它）。
            // 这一段得**先让那一档有内容**（不然它本来就被收着、点不动 ——
            // 收起来的档 `setStateFilter()` 也会落到「全部」，量不到"退档"这一发）。
            dialog.setFileUpdates({modsDir + QStringLiteral("/sodium.jar")}, true);
            check(selectState(InstanceFilesPage::StateFilter::Update) &&
                      page->stateFilter() == InstanceFilesPage::StateFilter::Update,
                  "U10P 先让「可更新」有内容、点上去（下面那一发才有得退）");
            page->setCanFilterUpdates(false);
            check(page->stateCombo()->count() == 3 &&
                      stateButton(InstanceFilesPage::StateFilter::Update)->isHidden() &&
                      page->stateFilter() == InstanceFilesPage::StateFilter::All &&
                      page->list()->count() == 3,
                  "★★ U10P 把「可更新」那一档拿掉时，正选着它会**退回「全部」**并把列表放回来"
                  "（不是留下一个筛空了列表、却没有任何一颗亮着的界面）");
            page->setCanFilterUpdates(true);
            check(!stateButton(InstanceFilesPage::StateFilter::Update)->isHidden() &&
                      page->list()->count() == 3,
                  "U10P 再打开：那一档回来、而**条数还在**（那份名单不会因为换了个开关就没了）");
            dialog.setFileUpdates({}, true);
            // 收尾：别把条件留在这一页上给后面那几段用（这一页是这一整段共用的那个 dialog）。
            search->clear();
            selectState(InstanceFilesPage::StateFilter::All);
        }

        // ---- U10d 三条开关：能不能启停 / 能不能删 / 只列文件夹 ----
        {
            // 「这一页有没有那种按钮」= **逐行扫一遍**，不是只看第 0 行：资源包那一页
            // 第一条是个文件夹（DirsFirst），而文件夹永远不给「禁用」。
            const auto rowsWith = [&dialog, &showPage](const QString &key, bool wantToggle) {
                showPage(dialog, key);
                InstanceFilesPage *page = dialog.filesPage(key);
                if (page == nullptr) {
                    return 0;
                }
                int found = 0;
                for (int row = 0; row < page->list()->count(); ++row) {
                    // 「启用 / 禁用」那一颗 起是一个 `ToggleSwitch`
                    // （`QAbstractButton`），而垃圾桶仍是 `QToolButton` —— 两条路
                    // 各自型别不同，所以这里分开取，不能合成一个 `QToolButton *`。
                    const QAbstractButton *button =
                        wantToggle ? page->toggleButtonAt(row) : page->deleteButtonAt(row);
                    found += button != nullptr ? 1 : 0;
                }
                return found;
            };
            const auto hasToggle = [&rowsWith](const QString &key) {
                return rowsWith(key, true) > 0;
            };
            const auto hasDelete = [&rowsWith](const QString &key) {
                return rowsWith(key, false) > 0;
            };
            check(hasToggle(QStringLiteral("mod")) && !hasToggle(QStringLiteral("resourcepack")) &&
                      hasToggle(QStringLiteral("shader")),
                  "★ U10d 文件改名开关仅用于模组和光影；资源包改用 options.txt 启用顺序管理");
            check(!hasToggle(QStringLiteral("datapack")) &&
                      !hasToggle(QStringLiteral("schematic")) && !hasToggle(QStringLiteral("save")),
                  "★ U10d 数据包 / 原理图 / 存档**没有**「启用·禁用」（改了名也不会生效，"
                  "给了反而是骗人）");
            check(hasDelete(QStringLiteral("mod")) && hasDelete(QStringLiteral("datapack")) &&
                      hasDelete(QStringLiteral("schematic")) &&
                      !hasDelete(QStringLiteral("save")),
                  "★ U10d 存档那一页**没有删除**（那是用户手里唯一那份，启动器没有副本）");

            // 存档只列文件夹：saves/我的世界/level.dat 不该占一行。
            showPage(dialog, QStringLiteral("save"));
            InstanceFilesPage *saves = dialog.filesPage(QStringLiteral("save"));
            check(saves != nullptr && saves->list()->count() == 1 &&
                      itemText(saves, 0) == QStringLiteral("我的世界"),
                  "★ U10d 存档那一页只列文件夹（一个文件夹就是一个存档，level.dat 不是存档）",
                  QStringLiteral("%1 条 / %2")
                      .arg(saves != nullptr ? saves->list()->count() : -1)
                      .arg(itemText(saves, 0)));
            // 资源包那一页反过来：**文件夹也算一个资源包**，要列出来。
            showPage(dialog, QStringLiteral("resourcepack"));
            InstanceFilesPage *packs = dialog.filesPage(QStringLiteral("resourcepack"));
            check(packs != nullptr && packs->list()->count() == 2 &&
                      itemText(packs, 0) == QStringLiteral("文件夹包"),
                  "★ U10d 资源包那一页文件夹和 .zip 都列（文件夹也是一种资源包）",
                  QStringLiteral("%1 条 / 第一条 %2")
                      .arg(packs != nullptr ? packs->list()->count() : -1)
                      .arg(itemText(packs, 0)));
            // ---- ★★ ：「模组」那一页**一个文件夹都不列**（用户点名）----
            //
            // 要求：「游戏模组文件夹里套的文件夹也会被列出来，而禁用对其的作用只是改个
            // 名」。模组目录底下那些子目录（用户盘上的 `mods/.connector`）游戏根本不把它们
            // 当模组看，列出来还给开关就是骗人 —— 所以这一页的 Mode 是 `FilesOnly`，
            // 而资源包 / 数据包 / 光影那三页照旧列目录（**目录本身就是一份资源**）。
            const QString nestDir = modsDir + QStringLiteral("/mcef-libraries");
            check(QFileInfo(nestDir).isDir(),
                  "★★ U10d 前提：模组目录里真的套着文件夹（那一页有得可漏）");
            showPage(dialog, QStringLiteral("mod"));
            InstanceFilesPage *mods = dialog.filesPage(QStringLiteral("mod"));
            QString modPaths;
            for (int row = 0; mods != nullptr && row < mods->list()->count(); ++row) {
                modPaths += itemPath(mods, row) + QStringLiteral(" | ");
            }
            check(mods != nullptr && mods->list()->count() == 3 &&
                      !modPaths.contains(QStringLiteral("mcef-libraries")) &&
                      !modPaths.contains(QStringLiteral(".connector")),
                  "★★ U10d 「模组」那一页**一个文件夹都不列**（用户原话「游戏模组文件夹里套的"
                  "文件夹也会被列出来」）—— 三个 .jar 照旧一条不少",
                  QStringLiteral("%1 条：%2")
                      .arg(mods != nullptr ? mods->list()->count() : -1)
                      .arg(modPaths));
            // 反过来钉一次「不是把这一页筛空了」：其它三页的目录还在。
            check(packs != nullptr && packs->list()->count() == 2, "U10d 资源包那一页没被连累");
        }

        // ---- U10O 「存档」那一页的三样新东西----
        //
        // 要求：「在实例设置里面存档那一栏每个世界都显示存档大小和最后游玩时间，右边
        // 画一支笔意为编辑，和基岩版一样，顶端加个导入世界按钮和批量编辑按钮（这个先做
        // 占位）」。这里量的全是**显示**：那一行上有没有那支笔、那行小字写的是什么、
        // 顶上那两颗露不露。真正动手的两条路（选框、拷盘）在 mw_check / verify 里走。
        //
        // 逃生口 `WTFML_UC_SKIP_U10O=1`：整段跳过。理由不是"这一段不对"，而是它
        // 后面那一发事件循环会把一个**跟这一段无关的**堆损坏戳爆（见下面
        // 「不跟 junction 走」那一节里那一段 NOTES，以及文件头）—— 而这一爆就带走了
        // 后面一千五百多条判据（⑬/⑭ 全线在内）。**默认必须是跑**，只在"这一台机器
        // 上今天就是过不去"时用它换一份后半段的结论。
        if (!qEnvironmentVariableIsSet("WTFML_UC_SKIP_U10O")) {
            showPage(dialog, QStringLiteral("save"));
            InstanceFilesPage *saves = dialog.filesPage(QStringLiteral("save"));
            check(saves != nullptr, "U10O 存档那一页拿得到");
            if (saves != nullptr) {
                // 那支笔只在**存档**这一页露 —— 判据是 setCanEdit()，而对话框只为
                // "save" 那一页调它。别的几页要是也露出来，用户会在一个 .jar 上看见
                // 一支"编辑"笔，点下去是"重命名这个世界"。
                QToolButton *pen = saves->editButtonAt(0);
                check(pen != nullptr, "★ U10O 存档那一行行尾有那支笔（「编辑」）");
                if (pen != nullptr) {
                    check(pen->objectName() == InstanceFilesPage::editButtonName(),
                          "U10O 那支笔挂的是那个唯一出处里的 objectName", pen->objectName());
                    check(pen->text() == InstanceFilesPage::editText(),
                          "U10O 笔上那几个字是「编辑」", pen->text());
                    check(!pen->toolTip().isEmpty(), "U10O 而且有 tooltip 说清点下去会怎样");
                    check(pen->isVisible(), "U10O 那一页开着的时候它是看得见的");
                    // 一支"画出来的笔"：图标不许是空的（空图标 + 空白字 = 行尾一小块真空）。
                    check(!pen->icon().isNull(), "U10O 笔的图标真的画出来了（不是一张空图）");
                }
                for (const QString &key : {QStringLiteral("mod"), QStringLiteral("resourcepack"),
                                           QStringLiteral("datapack"), QStringLiteral("shader"),
                                           QStringLiteral("schematic")}) {
                    InstanceFilesPage *other = dialog.filesPage(key);
                    check(other == nullptr || other->editButtonAt(0) == nullptr,
                          QStringLiteral("★ U10O 「%1」那一页**没有**那支笔（那儿的每一行不是"
                                         "世界，给它一支笔就是骗人）")
                              .arg(key));
                }

                // ---- 顶上那两颗 ----
                check(saves->importButton() != nullptr && saves->batchButton() != nullptr,
                      "★ U10O 存档页顶上那两颗按钮都建出来了");
                if (saves->importButton() != nullptr && saves->batchButton() != nullptr) {
                    check(saves->importButton()->text() ==
                              InstanceSettingsDialog::importWorldText(),
                          "U10O 「导入世界」那几个字来自唯一出处",
                          saves->importButton()->text());
                    check(saves->batchButton()->isHidden(),
                          "U10O 未实现的批量编辑不占用存档工具栏");
                    check(saves->importButton()->isVisible(),
                          "U10O 存档工具栏保留真实导入入口");
                    // 「批量编辑」今天是个**占位**：它的 tooltip 必须自己先说清"还没做"
                    // —— 只靠点下去弹的那句话的话，用户要先把鼠标移过去点一下才知道。
                    check(!saves->batchButton()->isVisibleTo(saves),
                          "U10O 占位批量编辑刷新后仍不显示");
                    // 导入那一颗是**真的**，tooltip 里不该出现"还没做"。
                    check(!saves->importButton()->toolTip().contains(QStringLiteral("还没做")),
                          "★ U10O 而「导入世界」是真的，tooltip 里没有那三个字",
                          saves->importButton()->toolTip());
                }
                for (const QString &key : {QStringLiteral("mod"), QStringLiteral("resourcepack"),
                                           QStringLiteral("datapack"), QStringLiteral("shader"),
                                           QStringLiteral("schematic")}) {
                    InstanceFilesPage *other = dialog.filesPage(key);
                    check(other == nullptr || other->importButton() == nullptr ||
                              !other->importButton()->isVisible(),
                          QStringLiteral("★ U10O 「%1」那一页顶上**没有**这两颗（导入世界是存档"
                                         "那一页的事）")
                              .arg(key));
                }

                // ---- 那行小字：先「正在统计…」，算完换成真数 ----
                //
                // 规矩和别的那几页一样：**先把行摆出来**，一个目录的字节数（要递归整个
                // saves/，几百个世界的话是几秒）排到事件循环上一趟一趟算（见 .cpp 里
                // fillDirectoryStats 那段）。所以这里量的是两态：**刚刷出来**那一刻必须是
                // 「正在统计…」，泵一次事件循环之后必须变成"大小 / 时间"。
                //
                // NOTE: 显式调 refresh() 而不是靠对话框构造时那一趟 —— 构造那一趟排的活儿
                // 可能已经被本段前面那些 processEvents 跑掉了，那时第一态就量不到，
                // 这一条会变成一条**看起来**偶尔红的断言。
                const auto metaTextOf = [&](InstanceFilesPage *page, int row) {
                    QListWidgetItem *item = page->list()->item(row);
                    QWidget *rowWidget = item != nullptr ? page->list()->itemWidget(item) : nullptr;
                    QLabel *meta = rowWidget != nullptr
                                       ? rowWidget->findChild<QLabel *>(
                                             InstanceFilesPage::metaLabelName())
                                       : nullptr;
                    return meta != nullptr ? meta->text() : QString();
                };

                check(metaTextOf(saves, 0) == InstanceFilesPage::statsPendingText(),
                      "★ U10O 刚刷出来那一刻那行小字是「正在统计…」（不是空白，也不是 0 B）",
                      metaTextOf(saves, 0));
                // 一个目录一趟，队列空了才停 —— 这里只有一个世界，一次事件循环就够。
                for (int i = 0; i < 8; ++i) {
                    QCoreApplication::processEvents();
                }
                const QString filled = metaTextOf(saves, 0);
                check(filled != InstanceFilesPage::statsPendingText(),
                      "★ U10O 泵过事件循环之后它换成真数了（不然就永远停在「正在统计…」）",
                      filled);
                // 那个世界里只有一个 level.dat（4 KB，见上面 mkfile 那个夹具）。
                check(filled.contains(InstanceFilesPage::formatSize(4096)),
                      "★ U10O 大小算的是**目录里所有东西加起来**（4 KB 那份 level.dat）",
                      filled);
                check(filled.contains(QStringLiteral(" / ")),
                      "U10O 大小和最后游玩时间是同一行里的两截（斜线分隔）", filled);
                // 「最后游玩时间」取的是 level.dat 的修改时刻（游戏每存一次档就写它一次），
                // 所以那一截必须长得像一个日期 —— 拿目录自己的 mtime 那种错法给出的也是
                // 日期，这一条只证明"有日期"，取值来源在 verify 那边（见 ZJ 节）。
                const QRegularExpression stamp(QStringLiteral(R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2})"));
                check(filled.contains(stamp),
                      "★ U10O 后面那一截是一个「yyyy-MM-dd HH:mm」的时刻（最后游玩时间）",
                      filled);

                // ---- ★ 不跟 junction 走 ----
                //
                // 这一条量的是一条**判据的洞**：那个数的判据原来是 `isSymLink()` 一条，而
                // `mklink /J` 造出来的目录 junction 在 Qt 里 `isSymLink()` 是 **false**
                // （只有 `isJunction()` 认得出它 —— Qt 那边是 `qfilesystemmetadata_p.h:364`
                // 分两个 tag：IO_REPARSE_TAG_SYMLINK → LinkType、IO_REPARSE_TAG_MOUNT_POINT
                // → 只有 JunctionType）。**而且**光"看见链接就 continue"也拦不住：QDirIterator
                // 拒绝下潜只看 `isSymLink()` 一条（Qt 6.10.1 `qdirlisting.cpp:384`），junction
                // 照进不误、里面的文件一条条都是独立结果。所以这一条必须拿**真 junction**
                // 来量（和 mw_check 里删除那一段同一个理由）。
                //
                // 摆法：在这个世界里挂一个 junction，指向另一个装了同样大小（4 KB）一份文件的
                // 目录。**跟过去的话这个数是 8.0 KB，不跟就是 4.0 KB** —— 两个渲染出来的
                // 字面量互不包含（`formatSize()` 那三行），所以判据不含糊。
                const QString worldDir = savesDir + QStringLiteral("/我的世界");
                const QString elsewhere = root + QStringLiteral("/别处的世界");
                mkfile(elsewhere + QStringLiteral("/level.dat"));
                const QString linkPath = worldDir + QStringLiteral("/那边");
                const bool madeJunction =
                    QProcess::execute(QStringLiteral("cmd"),
                                      {QStringLiteral("/c"), QStringLiteral("mklink"),
                                       QStringLiteral("/J"), QDir::toNativeSeparators(linkPath),
                                       QDir::toNativeSeparators(elsewhere)}) == 0 &&
                    QFileInfo(linkPath).isJunction();
                check(madeJunction, "★ U10O 先造出一个真 junction（mklink /J，不要管理员权限）",
                      QStringLiteral("isSymLink=%1 isJunction=%2")
                          .arg(QFileInfo(linkPath).isSymLink() ? 1 : 0)
                          .arg(QFileInfo(linkPath).isJunction() ? 1 : 0));
                // 逃生口 `WTFML_SKIP_JUNCTION=1`：跳过下面这一段（refresh + 泵 + 取小字）。
                //
                // 当天稍后又量了一轮：崩点在 `InstanceSettingsDialog` 的析构里，栈全是
                // Qt6Widgets / Qt6Gui、一行我们的代码都没有，而且一崩就是连着几十趟、
                // 一绿也是连着好几趟。详情见文件头那一段。
                // 所以这个逃生口治不了它（崩点在它前面），留着是为了别的事 —— 同样
                // **别拿它当常态**，默认必须是**跑**。
                if (madeJunction && !qEnvironmentVariableIsSet("WTFML_SKIP_JUNCTION")) {
                    // 顺带把"junction 不是符号链接"这条前提**现量一遍**：这条断言红了，
                    // 说明 Qt 那边的分类变了，上面那两处判据得跟着改。
                    check(!QFileInfo(linkPath).isSymLink(),
                          "★ U10O junction **不是**符号链接（isSymLink() 是 false）—— 只判"
                          "isSymLink() 的那条老判据在 Windows 上就是这么漏的",
                          QStringLiteral("isSymLink=%1")
                              .arg(QFileInfo(linkPath).isSymLink() ? 1 : 0));
                    saves->refresh();
                    for (int i = 0; i < 8; ++i) {
                        QCoreApplication::processEvents();
                    }
                    const QString withLink = metaTextOf(saves, 0);
                    check(withLink.contains(InstanceFilesPage::formatSize(4096)) &&
                              !withLink.contains(InstanceFilesPage::formatSize(8192)),
                          "★ U10O junction 那一头**不算进来**（世界里那一份 4 KB 照旧，"
                          "不是 8 KB）",
                          withLink);
                }
            }
        }

        // ---- U10e 行尾那两颗按下去报的是哪一条 ----
        {
            showPage(dialog, QStringLiteral("mod"));
            InstanceFilesPage *page = dialog.filesPage(QStringLiteral("mod"));
            QString lastPath;
            int toggles = 0;
            int deletes = 0;
            QObject::connect(page, &InstanceFilesPage::toggleRequested, page,
                             [&lastPath, &toggles](const QString &path) {
                                 lastPath = path;
                                 ++toggles;
                             });
            QObject::connect(page, &InstanceFilesPage::deleteRequested, page,
                             [&lastPath, &deletes](const QString &path) {
                                 lastPath = path;
                                 ++deletes;
                             });
            if (page != nullptr && page->toggleButtonAt(2) != nullptr) {
                page->toggleButtonAt(2)->click();
            }
            const bool toggleOk = toggles == 1 && lastPath.endsWith(QStringLiteral(".disabled"));
            if (page != nullptr && page->deleteButtonAt(0) != nullptr) {
                page->deleteButtonAt(0)->click();
            }
            check(toggleOk && deletes == 1 &&
                      lastPath == modsDir + QStringLiteral("/iris-1.6.5.jar"),
                  "★ U10e 「启用/禁用」报的是**这一条现在那个名字**（禁用着的带 .disabled）、"
                  "「删除」报的是这一条的绝对路径；两件事各自成一条信号",
                  QStringLiteral("改名 %1 次 / 删除 %2 次，最后一条 %3")
                      .arg(toggles)
                      .arg(deletes)
                      .arg(lastPath));

            // 「打开文件夹」那一颗也报同一个目录。
            lastPath.clear();
            int folderOpens = 0;
            QObject::connect(page, &InstanceFilesPage::openFolderRequested, page,
                             [&lastPath, &folderOpens](const QString &path) {
                                 lastPath = path;
                                 ++folderOpens;
                             });
            if (page != nullptr && page->openFolderButton() != nullptr) {
                page->openFolderButton()->click();
            }
            check(folderOpens == 1 && lastPath == modsDir,
                  "U10e 那一页的「打开文件夹」开的是这一页看的那个目录", lastPath);
        }

        // ---- U10f 空目录 / 用不上：两句不同的话 ----
        {
            Dlg other(instanceDisplayName, instanceDirName, Dlg::Override(), global, QStringList(),
                      true);
            QHash<QString, QString> emptyDirs;
            // 这个目录故意建都不建：不存在的目录和空目录应当是同一句话。
            emptyDirs.insert(QStringLiteral("datapack"), root + QStringLiteral("/还没有"));
            other.setFileDirs(emptyDirs);
            other.showPage(QStringLiteral("datapack"));
            InstanceFilesPage *page = other.filesPage(QStringLiteral("datapack"));
            // 「显不显示」一律问 **isVisibleTo(这一页自己)**。两个更直白的写法在这儿都是假的：
            // * `isHidden()` 量的是"这个控件自己有没有被显式藏起来"，父控件被藏起来不算
            // （「打开文件夹」那颗就是这样 —— 它自己从没被藏过，藏的是它爹那一排）。
            // * `isVisible()` 恒假：这一页这会儿收在堆叠里，父链上就断了。
            // isVisibleTo() 正好只看到"这一页为止"那一段。
            check(page != nullptr && !page->list()->isVisibleTo(page) &&
                      page->noticeLabel()->isVisibleTo(page) &&
                      page->noticeLabel()->text() == Dlg::filesEmptyText(QStringLiteral("datapack")),
                  "★ U10f 目录是空的（或者压根还没建）时列表收起来、换成一句话",
                  page != nullptr ? page->noticeLabel()->text() : QString());
            check(page != nullptr && page->refreshButton()->isVisibleTo(page) &&
                      page->openFolderButton()->isVisibleTo(page),
                  "U10f 空目录里那两颗按钮还在（用户就是要在空目录上点「打开文件夹」）");
            check(page != nullptr && page->searchEdit() != nullptr &&
                      page->stateCombo() != nullptr &&
                      page->stateButtonAt(InstanceFilesPage::StateFilter::All) != nullptr &&
                      !page->searchEdit()->isVisibleTo(page) &&
                      !page->stateButtonAt(InstanceFilesPage::StateFilter::All)->isVisibleTo(page),
                  "★ U10f 空目录里那两排筛选也收起来了（没东西可筛）");

            // 「用不上」那一档：列表**和那排按钮一起**收起来。
            other.setShaderUsable(false);
            other.showPage(QStringLiteral("shader"));
            InstanceFilesPage *shader = other.filesPage(QStringLiteral("shader"));
            check(shader != nullptr && !shader->list()->isVisibleTo(shader) &&
                      !shader->refreshButton()->isVisibleTo(shader) &&
                      !shader->openFolderButton()->isVisibleTo(shader) &&
                      shader->searchEdit() != nullptr &&
                      !shader->searchEdit()->isVisibleTo(shader) &&
                      shader->stateButtonAt(InstanceFilesPage::StateFilter::All) != nullptr &&
                      !shader->stateButtonAt(InstanceFilesPage::StateFilter::All)
                           ->isVisibleTo(shader) &&
                      shader->noticeLabel()->text() == Dlg::shaderUnavailableText(),
                  "★ U10f 装不了光影的实例：「光影包」那一页整页换成一句解释"
                  "（列表、那排按钮、以及那两排筛选一起收起来）",
                  shader != nullptr ? shader->noticeLabel()->text() : QString());
            // 取舍写在 `rebuildNavigationTree()` 的注释里 ——
            // 「保留用户明确关闭页面的选择；可用性不足在既有页面里解释，**不吞掉导航入口**」：
            // 只在树上认"用户自己勾掉的"（`m_pageOverrides`），不看 `m_pageAvailable`。
            // 于是"点得进去"这件事仍然成立：点进去那一页整幅是一句解释 + 那两颗按钮，
            // 而不是"树上少一条、用户不知道这一页去哪了"。
            //
            // NOTE: 量的是**树上那棵导航**（`navigationTree()` —— 界面导航的唯一出处），
            // 不再是 `pageList()` 那一列旧的、`hide()` 着的列表：它的内容早就不跟
            // 页面表走了（还是老的四组十三行），拿它量两个方向都会量错。
            const auto leftHasKey = [&other](const QString &key) {
                QTreeWidget *tree = other.navigationTree();
                if (tree == nullptr) {
                    return false;
                }
                for (QTreeWidgetItemIterator it(tree); *it; ++it) {
                    if ((*it)->data(0, Qt::UserRole).toString() == key) {
                        return true;
                    }
                }
                return false;
            };
            check(leftHasKey(QStringLiteral("shader")) &&
                      !other.shownEntryKeys().contains(QStringLiteral("shader")),
                  "★★ U10f 用不上的那一条**留在树上**（点得进去、由那一页自己解释）—— 而"
                  "shownEntryKeys() 里已经没它了（「能用」这份账仍然只算可用的页）",
                  QStringLiteral("树上有 %1 / shownEntryKeys 里 %2")
                      .arg(leftHasKey(QStringLiteral("shader")) ? QStringLiteral("有")
                                                                : QStringLiteral("<没有>"),
                           other.shownEntryKeys().contains(QStringLiteral("shader"))
                               ? QStringLiteral("有")
                               : QStringLiteral("<没有>")));
            // 反过来：这一条钉的是"树只认用户自己勾掉的" —— 可用性变了，树上一个条目都不动。
            const int shaderRowsBefore = leftHasKey(QStringLiteral("shader")) ? 1 : 0;
            other.setShaderUsable(true);
            check(shader != nullptr && shader->refreshButton()->isVisibleTo(shader) &&
                      shader->noticeLabel()->text() != Dlg::shaderUnavailableText() &&
                      (leftHasKey(QStringLiteral("shader")) ? 1 : 0) == shaderRowsBefore &&
                      other.shownEntryKeys().contains(QStringLiteral("shader")),
                  "★ U10f 装了 Iris / OptiFine 之后那一页回到正常（页里的解释收掉、树上那条"
                  "一直都在 —— 可用性不进树）");
            // 原理图那一档是同一个形状（谁来定"用得上"是 MainWindow 的事 —— 它去 mods
            // 里找 litematica；这个对话框自己一个字都不猜）。
            other.setSchematicUsable(false);
            other.showPage(QStringLiteral("schematic"));
            InstanceFilesPage *schematic = other.filesPage(QStringLiteral("schematic"));
            // 量的是**那排按钮**不是列表：这个实例那几页的目录压根没喂进来，列表本来就是空的
            // （空目录也是收起来）—— 只看列表分不出"用不上"和"还没有"。
            // NOTE: 左栏里那一条**还在树上**（上面 `leftHasKey` 那一对钉的就是这件事 ——
            // 树不跟可用性走），这里量的是**页里**那一半（整页换解释 + 收掉那排按钮）。
            check(schematic != nullptr &&
                      !schematic->refreshButton()->isVisibleTo(schematic) &&
                      schematic->noticeLabel()->text() == Dlg::schematicUnavailableText(),
                  "★ U10f 没装 litematica 时「原理图」那一页走同一套（整页换解释 + 那排按钮"
                  "一起收起来）",
                  schematic != nullptr ? schematic->noticeLabel()->text() : QString());
            other.setSchematicUsable(true);
            check(schematic != nullptr && schematic->refreshButton()->isVisibleTo(schematic) &&
                      schematic->noticeLabel()->text() != Dlg::schematicUnavailableText(),
                  "★ U10f 装了之后那一页回来（这一档是可逆的）");
        }

        // ---- U10g 服务器列表：三种状态 ----
        {
            showPage(dialog, QStringLiteral("server"));
            auto *tree = dialog.serverTree();
            auto *notice = dialog.serverNotice();
            check(tree != nullptr && notice != nullptr, "U10g 服务器那一页的表格和那句话都在");

            dialog.setServerList({qMakePair(QStringLiteral("我的服"),
                                            QStringLiteral("mc.example.com")),
                                  qMakePair(QString(), QStringLiteral("1.2.3.4:25565"))},
                                 QString());
            // NOTE: 地址那一格是**遮过**的（服务端地址不往屏幕上摆全，见 U10L-8）；完整的
            // 那一份在悬停提示里。所以这里核的是行数 + 名字 + 遮过的地址，别拿原始
            // 字符串来比 —— 那不是产品行为。
            check(!tree->isHidden() && tree->topLevelItemCount() == 2 &&
                      tree->topLevelItem(0)->text(0) == QStringLiteral("我的服") &&
                      tree->topLevelItem(0)->text(1) == QStringLiteral("m-.e-.c-"),
                  "U10g 读到几条就摆几行，名字和地址各占一列");
            check(tree->topLevelItem(1)->text(0) == Dlg::unnamedMark(),
                  "★ U10g 名字那一格是空串时顶上一个记号（不是留一栏空白）",
                  tree->topLevelItem(1)->text(0));

            dialog.setServerList({}, QString());
            check(tree->isHidden() && !notice->isHidden() &&
                      notice->text() == Dlg::serverEmptyText(),
                  "★ U10g 读到了但是一条都没有 ⇒ 「还没加过服务器」", notice->text());

            dialog.setServerList({}, QStringLiteral("servers.dat 读不动：它不是 NBT。"));
            check(tree->isHidden() && notice->text() ==
                                          QStringLiteral("servers.dat 读不动：它不是 NBT。"),
                  "★ U10g 读不动时说的是**为什么**（和「还没加过服务器」是两句不同的话 —— "
                  "说成一句用户会照着错的那句去修）",
                  notice->text());
        }

        // ---- U10h 大小怎么写 / 禁用的后缀是什么 ----
        {
            check(InstanceFilesPage::formatSize(512) == QStringLiteral("512 B") &&
                      InstanceFilesPage::formatSize(2048) == QStringLiteral("2.0 KB") &&
                      InstanceFilesPage::formatSize(3 * 1024 * 1024) == QStringLiteral("3.0 MB") &&
                      InstanceFilesPage::formatSize(1610612736) == QStringLiteral("1.5 GB"),
                  "U10h 大小按 B/KB/MB/GB 写，各带一位小数（文件夹那几行给的是破折号）",
                  QStringLiteral("%1 / %2 / %3 / %4")
                      .arg(InstanceFilesPage::formatSize(512),
                           InstanceFilesPage::formatSize(2048),
                           InstanceFilesPage::formatSize(3 * 1024 * 1024),
                           InstanceFilesPage::formatSize(1610612736)));
            check(InstanceFilesPage::disabledSuffix() == QStringLiteral(".disabled"),
                  "U10h 禁用的写法是**接在完整文件名后面**的 .disabled（不是改扩展名）",
                  InstanceFilesPage::disabledSuffix());
        }

        // ---- U10i refreshFilePages：MainWindow 动过盘之后要重列 ----
        {
            showPage(dialog, QStringLiteral("mod"));
            InstanceFilesPage *page = dialog.filesPage(QStringLiteral("mod"));
            const int before = page != nullptr ? page->list()->count() : -1;
            const QString added = modsDir + QStringLiteral("/新来的.jar");
            {
                QFile file(added);
                if (file.open(QIODevice::WriteOnly)) {
                    file.write(QByteArray(8, 'x'));
                }
            }
            dialog.refreshFilePages();
            const int after = page != nullptr ? page->list()->count() : -1;
            QFile::remove(added);
            dialog.refreshFilePages();
            const int back = page != nullptr ? page->list()->count() : -1;
            check(before == 3 && after == 4 && back == 3,
                  "★ U10i refreshFilePages() 重新列一遍（盘上多一条 / 少一条都跟得上）",
                  QStringLiteral("%1 → %2 → %3").arg(before).arg(after).arg(back));
        }

        // ---- U10j 底下那行小字：**开的页照样有、开批量管理的那四页一定没有** ----
        //
        // 改的：模组 / 资源包 / 数据包 / 光影包这四页现在**一个 QLabel 都不建**
        // （用户："把这个页面下方那段叙述删了来让框拉到底充分利用空间"）—— 所以判据从
        // "八页都有"改成**两头都钉**：那四页必须**找不到**那个控件（建了说明判据没生效），
        // 其余几页必须**找得到、而且文字和 filesHint(key) 逐字一致**（漏了谁一眼看出来）。
        // 判据就是产品那一句 `filesNameOpensResource(key)`（批量管理的那四页 = 不挂小字的
        // 那四页）；这儿不重抄一遍那四个键 —— 抄了就是第二份真相。
        {
            QStringList badHints;
            for (const QString &key : Dlg::filePageKeys()) {
                if (dialog.findChild<QLabel *>(Dlg::filesHintName(key))) badHints.append(key);
            }
            check(badHints.isEmpty(), "文件页不再显示常驻解释段落", badHints.join(QLatin1Char('|')));
            auto *configFiles = dialog.filesPage(QStringLiteral("config"));
            check(configFiles && configFiles->toolTip() == Dlg::filesHint(QStringLiteral("config")),
                  "配置编辑操作保留在简短悬停提示中");
            check(!dialog.findChild<QLabel *>(Dlg::filesHintName(QStringLiteral("shortcut"))),
                  "概览快捷目录不重复显示解释段落");
        }

        // ---- U10k 名字长到装不下时：**省略号收尾**，而且是从中间截 ----
        //
        // 用户 报的「实例设置里模组列表模组名称显示存在错误」：mods 底下那些
        // `yet_another_config_lib_v3-3.7.1+1.21.4-fabric.jar` 比行名那一格宽得多，而
        // 普通 QLabel 是**硬裁**的（最后一个字被切掉一半）—— 看着像这一条的名字就是那串
        // 半截的字。修法是行名换成 ElidedLabel（见 ui/ElidedLabel.hpp）。
        //
        // 这一节的目录**单独一份**（不改上面那份夹具的行序：往同一个 mods 目录里加一个
        // 文件会把它下面那些按下标取的断言全挪位）。
        {
            const QString longDir = root + QStringLiteral("/长名字/mods");
            // 名字照用户盘上那几条抄（`.jar.disabled` 的**长名字 + 禁用**，两头都占上）。
            // 底下这串得**真的**比行名那一格宽 —— 那是"会省略"这个判据的
            // 前提。用户盘上那一条（52 个字符）在真 windows 的字形下只占 337 px，而行名
            // 那一格在真机上排出来是 **444 px** ⇒ 装得下、根本不省略（offscreen 那套桩
            // 字体宽得多，所以这条一直只红在真机上）。这里把它接长到一百来个字符，
            // 真机（~650 px）和桩字体下都稳稳溢出。后缀保持 `.jar`（页面按后缀认它）。
            const QString longName = QStringLiteral(
                "yet_another_config_lib_v3-3.7.1+1.21.4-fabric"
                "-with-a-very-long-variant-suffix-for-elide-testing.jar");
            mkfile(longDir + QStringLiteral("/") + longName + QStringLiteral(".disabled"));

            Dlg longDlg(instanceDisplayName, instanceDirName, Dlg::Override(), global, QStringList(),
                        true);
            QHash<QString, QString> longDirs;
            longDirs.insert(QStringLiteral("mod"), longDir);
            longDlg.setFileDirs(longDirs);
            // 必须真 show()：这一节量的是**行名那一格有多宽**，而宽是布局排出来的
            // （没 show 过的控件宽度是 0，量出来永远是"装得下"）。show() 不是 exec()，
            // 离屏跑得动。
            longDlg.show();
            QApplication::processEvents();
            longDlg.showPage(QStringLiteral("mod"));
            QApplication::processEvents();
            InstanceFilesPage *page = longDlg.filesPage(QStringLiteral("mod"));
            QListWidgetItem *item = page != nullptr ? page->list()->item(0) : nullptr;
            QWidget *rowWidget =
                (page != nullptr && item != nullptr) ? page->list()->itemWidget(item) : nullptr;
            auto *nameLabel =
                rowWidget != nullptr
                    ? rowWidget->findChild<QLabel *>(InstanceFilesPage::nameLabelName())
                    : nullptr;
            const QString shownName =
                longName + InstanceFilesPage::disabledMark();

            check(nameLabel != nullptr && nameLabel->text() == shownName,
                  "U10k 名字那一格挂的是 `instanceFilesNameLabel`，文字是**完整**那串"
                  "（截的只是画出来的样子，数据一个字都不少）",
                  nameLabel != nullptr ? nameLabel->text() : QStringLiteral("<null>"));
            // 会省略那种标签：读它在构造时就写好的动态属性（普通 QLabel 上这个属性是 0 ——
            // Qt::ElideNone，所以"删掉 setElideMode()"这条会被当场抓住）。
            check(nameLabel != nullptr &&
                      nameLabel->property("elideMode").toInt() == int(Qt::ElideMiddle),
                  "★ U10k 名字那一格是**会省略**的标签，而且从**中间**截（尾巴上挂着"
                  "「（已禁用）」，从尾巴截等于先把「这一条是关着的」扔了）",
                  nameLabel != nullptr
                      ? QStringLiteral("elideMode=%1").arg(nameLabel->property("elideMode").toInt())
                      : QStringLiteral("<null>"));
            if (nameLabel != nullptr) {
                const QFontMetrics metrics = nameLabel->fontMetrics();
                auto *metaLabel =
                    rowWidget->findChild<QLabel *>(InstanceFilesPage::metaLabelName());

                // ---- U10k-1 **名字那一格真的拿到了宽度**（这一节的正题）----
                //
                // 这三样挤在**同一行**时，「4.0 KB / 00:12」在 150% 的字号下要 300 px ——
                // 名字那一格（横向是 Ignored，最小宽度 0）被挤成 **0 宽**，一整列名字一个
                // 字都看不见（不是"裁得难看"，是"根本没有"）。
                const bool wideEnough = nameLabel->width() >= InstanceFilesPage::nameLabelMinWidth();
                const bool moreThanHalf =
                    rowWidget != nullptr && nameLabel->width() * 2 > rowWidget->width();
                check(wideEnough && moreThanHalf,
                      "★ U10k 名字那一格**真的拿到了宽度**（不小于下限，而且超过行宽的一半）"
                      "—— 那个大小·时间不能跟它挤同一行（会把它挤成 0 宽）",
                      QStringLiteral("名字宽 %1（下限 %2、行宽 %3、要过半 %4）；列表宽 %5；"
                                     "小字「%6」宽 %7")
                          .arg(nameLabel->width())
                          .arg(InstanceFilesPage::nameLabelMinWidth())
                          .arg(rowWidget != nullptr ? rowWidget->width() : -1)
                          .arg(rowWidget != nullptr ? (rowWidget->width() + 1) / 2 : -1)
                          .arg(page->list()->width())
                          .arg(metaLabel != nullptr ? metaLabel->text() : QStringLiteral("<null>"))
                          .arg(metaLabel != nullptr ? metaLabel->width() : -1));

                // ---- U10k-2 那个大小·时间在**下一行**（同一个左边，名字底下）----
                //
                // 这才是修法本身：把"名字"和"大小 / 时间"摆成上下两行，名字那一格就再也不
                // 用跟谁抢宽度了。摆回同一行的改动会让这条当场红。
                const bool stacked =
                    metaLabel != nullptr && metaLabel->isVisible() &&
                    metaLabel->mapTo(rowWidget, QPoint(0, 0)).y() >=
                        nameLabel->mapTo(rowWidget, QPoint(0, 0)).y() + nameLabel->height() &&
                    metaLabel->mapTo(rowWidget, QPoint(0, 0)).x() ==
                        nameLabel->mapTo(rowWidget, QPoint(0, 0)).x();
                check(stacked,
                      "★ U10k 大小·时间在名字的**下一行**（同一个左边、名字脚底下）"
                      "—— 两样摆成上下两行，名字那一格才拿得到整幅宽度",
                      QStringLiteral("名字 y=%1 x=%2 h=%3；小字 y=%4 x=%5")
                          .arg(nameLabel->mapTo(rowWidget, QPoint(0, 0)).y())
                          .arg(nameLabel->mapTo(rowWidget, QPoint(0, 0)).x())
                          .arg(nameLabel->height())
                          .arg(metaLabel != nullptr
                                   ? metaLabel->mapTo(rowWidget, QPoint(0, 0)).y()
                                   : -1)
                          .arg(metaLabel != nullptr
                                   ? metaLabel->mapTo(rowWidget, QPoint(0, 0)).x()
                                   : -1));

                // ---- U10k-3 两行字**装得进这一行**（行高是常数 46）----
                //
                // 上面那条改动有个前提：46 px 那一格塞得下两行字。第二行用的是
                // versionDateFontDelta() 那一档小字（和版本树同一个做法），这儿量出来对一下
                // —— 字号配置一改（150% 缩放的机器上字更高），先红的是这一条，
                // 而不是等用户看见两行贴着边。
                const bool fitsVertically =
                    metaLabel != nullptr && nameLabel->height() + metaLabel->height() <=
                                                rowWidget->height();
                check(fitsVertically,
                      "★ U10k 名字那一行 + 小字那一行都装得进行高里（行高 46 = "
                      "versionTreeRowHeight()，装不下就该动行高）",
                      QStringLiteral("名字高 %1 + 小字高 %2 ≤ 行高 %3")
                          .arg(nameLabel->height())
                          .arg(metaLabel != nullptr ? metaLabel->height() : -1)
                          .arg(rowWidget != nullptr ? rowWidget->height() : -1));

                // ---- U10k-4 这么窄的一格里，省略出来的是什么 ----
                //
                // ① 这份夹具**真的**长到装不下 —— 不然这一节等于空转（夹具太短就是这样：
                // 名字全短得根本用不上省略，所以那个"硬裁"的毛病一直没人看见）。
                const bool tooLong = metrics.horizontalAdvance(shownName) > nameLabel->width();
                // ② 省略之后尾巴上那四个字还在（`…` 之后那一段）—— 硬裁恰好会把它切没。
                const QString elided =
                    metrics.elidedText(shownName, Qt::ElideMiddle, nameLabel->width());
                check(tooLong && elided.contains(InstanceFilesPage::disabledMark()),
                      "★ U10k 这条名字确实比那一格宽，而省略之后**尾巴上那四个字还在**"
                      "（硬裁出来的那串两头都看不全）",
                      QStringLiteral("宽 %1 < 文字 %2；画出来是「%3」")
                          .arg(nameLabel->width())
                          .arg(metrics.horizontalAdvance(shownName))
                          .arg(elided));

                // ---- U10k-5 这一条自己**不带文字** ----
                //
                // 补，用户截图报的「模组名称出现了两次」。
                // `new QListWidgetItem(文字, list)` 那一份文字是**视图代理**画的，而这一行
                // 的内容是一个用 setItemWidget() 摆上去的真控件、而且是**透明的**（要这样
                // 选中高亮才从底下透出来）—— 代理画的字从控件底下露出来，同一个名字在
                // 屏幕上画两遍，两遍的位置差着半行，看着像重影。
                // 这一条钉的是**修法本身**（名字的唯一出处是那一格标签）。
                check(item->text().isEmpty(),
                      "★ U10k 条目自己**不带文字**（名字只由行里那一格标签画）—— 带一份的话，"
                      "代理会在透明的行控件底下再画一遍同一个名字，就是屏幕上那个重影",
                      QStringLiteral("item->text()=「%1」").arg(item->text()));

                // ---- U10k-6 一行里**只有两段墨**（名字 + 大小·时间）----
                //
                // 上面那条钉的是"条目不给自己带文字"，这一条**不读那个实现细节**，它量的是
                // 屏幕上真的只剩两段字 —— 哪天有人改从别处把名字画回来，U10k-5 照样是绿的，
                // 只有这一条会红。
                //
                // 量法：把这一行的**可视区**截下来，在"名字那一格的横向范围"里逐条扫描线数
                // 有墨的像素（"有墨"= 和这一行的底色不一样，所以次要那一档的灰字也算得上），
                // 数连续有几段。两段 = 名字一行 + 小字一行。
                {
                    const QPixmap shot = page->list()->viewport()->grab();
                    const QImage shotImage = shot.toImage();
                    const qreal shotDpr = shot.devicePixelRatio();
                    const QRect rowRect = page->list()->visualItemRect(item);
                    const QPoint nameOrigin =
                        nameLabel->mapTo(page->list()->viewport(), QPoint(0, 0));
                    // 从名字那一格的左边起（**不含**行首那枚图标：它也算墨，混进来会把两段
                    // 并成一段），到它右边为止。
                    const int inkX0 = int(nameOrigin.x() * shotDpr);
                    const int inkX1
                        = qMin(shotImage.width(),
                               int((nameOrigin.x() + nameLabel->width()) * shotDpr));
                    const int inkY0 = int(rowRect.top() * shotDpr);
                    const int inkY1 = qMin(shotImage.height(),
                                           int((rowRect.top() + rowRect.height()) * shotDpr));
                    // 底色 = 这一块里出现次数最多的那个颜色（行控件是透明的，底色就是列表
                    // 自己的底色，一整行都是它）。取"最暗的那个阈值"这种写法在小字上会漏。
                    QHash<QRgb, int> histogram;
                    for (int y = inkY0; y < inkY1; ++y) {
                        for (int x = inkX0; x < inkX1; ++x) {
                            ++histogram[shotImage.pixel(x, y)];
                        }
                    }
                    QRgb background = 0;
                    int mostSeen = -1;
                    for (auto it = histogram.constBegin(); it != histogram.constEnd(); ++it) {
                        if (it.value() > mostSeen) {
                            mostSeen = it.value();
                            background = it.key();
                        }
                    }
                    int bands = 0;
                    int inkRun = 0;
                    for (int y = inkY0; y < inkY1; ++y) {
                        bool ink = false;
                        for (int x = inkX0; x < inkX1 && !ink; ++x) {
                            const QRgb pixel = shotImage.pixel(x, y);
                            ink = qAbs(qRed(pixel) - qRed(background)) +
                                      qAbs(qGreen(pixel) - qGreen(background)) +
                                      qAbs(qBlue(pixel) - qBlue(background)) >
                                  90;
                        }
                        if (ink) {
                            ++inkRun;
                        } else if (inkRun > 0) {
                            ++bands;
                            inkRun = 0;
                        }
                    }
                    if (inkRun > 0) {
                        ++bands;
                    }
                    check(inkX1 > inkX0 && inkY1 > inkY0 && bands == 2,
                          "★★ U10k 这一行里**只有两段字**（名字 + 大小·时间）—— 条目的文字"
                          "没清干净的话，中间会多出第三段（就是用户截图里那个重影）",
                          QStringLiteral("数到 %1 段墨（x %2..%3，y %4..%5）")
                              .arg(bands)
                              .arg(inkX0)
                              .arg(inkX1)
                              .arg(inkY0)
                              .arg(inkY1));
                }
            }
        }

        // ---- U10M 顶上那颗「下载新 X」：哪几页有、按下去报的是哪一栏----
        //
        // 要求：「在实例设置里模组 资源包 数据包 光影包里添加下载新光影，按了以后导向
        // 下载页对应类别」。这一节验的是**这一层能验的那一半**：哪几页有这颗按钮、上面的字
        // 是什么、按下去报的是哪个**分类键**（"切选项卡、挑那一行"在 MainWindow，端到端那
        // 一半在 mw_check 的 T20.12）。
        {
            // ① 纯函数：哪几页有（就是下载页里真有对应一栏的那四页）。
            const QStringList withDownload = {
                QStringLiteral("mod"), QStringLiteral("resourcepack"), QStringLiteral("datapack"),
                QStringLiteral("shader")};
            QStringList wrongPages;
            const QStringList allKeys = Dlg::filePageKeys();
            const QStringList categoryKeys = MainWindowUI::downloadCategoryKeys();
            for (const QString &key : allKeys) {
                const QString text = Dlg::filesDownloadText(key);
                const QString category = Dlg::filesDownloadCategoryKey(key);
                const bool wanted = withDownload.contains(key);
                if (wanted != !text.isEmpty() || wanted != !category.isEmpty()) {
                    wrongPages.append(key);
                    continue;
                }
                // 报出去的键**必须**是下载页左栏真有的那一栏。写歪一个字母的症状是
                // "按了那颗按钮没反应"（MainWindow 那边认不出这一栏、只说一句 qWarning）
                // —— 而界面本身看不出任何异常。
                if (wanted && !categoryKeys.contains(category)) {
                    wrongPages.append(key + QStringLiteral("→") + category);
                }
            }
            check(wrongPages.isEmpty(),
                  "★ U10M 只有模组 / 资源包 / 数据包 / 光影那四页有那颗按钮，报出去的是"
                  "下载页里真有的那一栏（分类键在 downloadCategoryKeys() 里）",
                  wrongPages.isEmpty() ? QStringLiteral("四页都对")
                                       : wrongPages.join(QLatin1Char(' ')));
            // 反过来：没有对应一栏的那几页**一个字都没有**（不是"建了再藏起来"那种有字的）。
            check(Dlg::filesDownloadText(QStringLiteral("schematic")).isEmpty() &&
                      Dlg::filesDownloadText(QStringLiteral("save")).isEmpty() &&
                      Dlg::filesDownloadText(Dlg::configPageKey()).isEmpty() &&
                      Dlg::filesDownloadText(Dlg::serverPageKey()).isEmpty() &&
                      Dlg::filesDownloadCategoryKey(QStringLiteral("schematic")).isEmpty(),
                  "★ U10M 原理图 / 存档 / 配置文件 / 服务器列表那四页没有这颗按钮"
                  "（原理图在下载页里没有对应的一栏，存档不是「下载」来的）");
            // 「光影包」那一页上的字是「下载新光影」—— 跟着**落地那一栏**的名字走
            // （左栏里这一页仍叫「光影包」，下载页那一栏叫「光影」）。
            check(Dlg::filesDownloadText(QStringLiteral("shader")) == QStringLiteral("下载新光影"),
                  "★ U10M 「光影包」那一页上写的是「下载新光影」（用户原话），不是「下载新光影包」",
                  Dlg::filesDownloadText(QStringLiteral("shader")));
            // 提示里点名"去哪儿 + 这一页会关掉"：按它是**换个地方**，用户按下之前得知道。
            const QString shaderTip = Dlg::filesDownloadTip(QStringLiteral("shader"));
            check(shaderTip.contains(MainWindowUI::downloadCategoryTexts().at(
                      categoryKeys.indexOf(QStringLiteral("shader")))) &&
                      shaderTip.contains(QStringLiteral("下载")),
                  "★ U10M 悬停提示里点名了落到哪一栏（不然用户不知道这一下会离开这一页）",
                  shaderTip);

            // ② 界面上：那四页露着、别的页藏着，字和 filesDownloadText() 一致。
            for (const QString &key : allKeys) {
                if (key == Dlg::serverPageKey()) {
                    continue;  // 那一页是棵树，不是 InstanceFilesPage。
                }
                showPage(dialog, key);
                InstanceFilesPage *page = dialog.filesPage(key);
                QPushButton *button =
                    page != nullptr ? page->downloadButton() : nullptr;
                const QString text = Dlg::filesDownloadText(key);
                if (button == nullptr || page == nullptr) {
                    check(false, QStringLiteral("U10M %1 那一页上找不到那颗按钮").arg(key));
                    continue;
                }
                check(button->text() == text &&
                          button->isVisibleTo(page) == !text.isEmpty(),
                      QStringLiteral("U10M %1 那一页那颗按钮：字对、露不露也对").arg(key),
                      QStringLiteral("字「%1」/ 露着=%2")
                          .arg(button->text())
                          .arg(button->isVisibleTo(page) ? 1 : 0));
            }

            // ③ 按下去：报的是这一页对应的那一栏，而且**只报一条**（点一下 = 一次）。
            // 四页各点一次，一页一发。
            for (const QString &key : withDownload) {
                showPage(dialog, key);
                InstanceFilesPage *page = dialog.filesPage(key);
                QStringList reported;
                // NOTE: 收完就把这条连接断掉 —— 它捕获的是**这一轮的局部量**，留着的话
                // 下一轮那一次 emit 会往一个已经没了的 QStringList 上写。
                const QMetaObject::Connection conn =
                    QObject::connect(&dialog, &Dlg::browseDownloadsRequested, page,
                                     [&reported](const QString &category) {
                                         reported.append(category);
                                     });
                if (page != nullptr && page->downloadButton() != nullptr) {
                    page->downloadButton()->click();
                }
                QObject::disconnect(conn);
                check(reported == QStringList{Dlg::filesDownloadCategoryKey(key)},
                      QStringLiteral("★ U10M 点一下「%1」：报的就是这一页对应的那一栏，"
                                     "而且只报一条")
                          .arg(Dlg::filesDownloadText(key)),
                      reported.join(QLatin1Char(' ')));
            }

            // ④ 「用不上」的那一页上它跟着那一排一起收起来（判据写在 .hpp 里：光影包那一页
            // 正在说「这个实例装不了光影」，此时把人送去下载页找光影包是指错路）。
            // 自己起一份：这一档要动"用不用得上"，不该拿上面那份 `dialog` 去翻。
            Dlg plain(instanceDisplayName, instanceDirName, Dlg::Override(), global, QStringList(),
                      true);
            plain.setShaderUsable(false);
            plain.showPage(QStringLiteral("shader"));
            InstanceFilesPage *shaderPage = plain.filesPage(QStringLiteral("shader"));
            check(shaderPage != nullptr && shaderPage->downloadButton() != nullptr &&
                      !shaderPage->downloadButton()->isVisibleTo(shaderPage) &&
                      !shaderPage->refreshButton()->isVisibleTo(shaderPage),
                  "★ U10M 「用不上」那一页上这颗按钮跟着那一排一起收起来（和「刷新」"
                  "「打开文件夹」同一个待遇）");
            check(Dlg::filesHint(QStringLiteral("mod")).isEmpty() &&
                      Dlg::filesHint(QStringLiteral("resourcepack")).isEmpty() &&
                      Dlg::filesHint(QStringLiteral("datapack")).isEmpty() &&
                      Dlg::filesHint(QStringLiteral("shader")).isEmpty(),
                  "★ U10M 批量管理那四页的 filesHint() 一律空串（那四页不再挂底下那行小字）",
                  Dlg::filesHint(QStringLiteral("mod")) + QStringLiteral("|") +
                      Dlg::filesHint(QStringLiteral("shader")));
            plain.setShaderUsable(true);
            shaderPage = plain.filesPage(QStringLiteral("shader"));
            check(shaderPage != nullptr && shaderPage->downloadButton() != nullptr &&
                      shaderPage->downloadButton()->isVisibleTo(shaderPage),
                  "U10M 那一页回到正常形态之后它又露出来（这一档是可逆的）");
        }

        // ---- U10N 行尾那颗「详情」⇒ 报出这一条是哪个文件（ / ）----
        //
        // 要求（ 上半句）：「除了模组以外的几个都是点击模组名称自动进入下载界面
        // （前提是匹配上了，**模组的待会再说**）」；（同一天的下半句）「对于模组，点进去和
        // 资源包差不多」⇒ **模组也接上了**。
        //
        // 这一层验的是**这一层能验的那一半**：哪几页有那一格、那一格在"认得出 / 认不出"
        // 两种行上分别什么样、点一下报的是什么。"收页 + 开那个资源自己的那一页"那一半在
        // MainWindow（端到端在 mw_check 的 T24）。
        {
            // ① 纯函数：哪几页接这件事。★ 起**模组也在表里** —— 那两张表（接哪几页 /
            // 顶上那颗「下载新 X」）从这一天起是同一张脸，可它们仍然是**两件事**，
            // 别合并（见 .hpp 里那条 NOTE）。
            check(Dlg::filesNameOpensResource(QStringLiteral("mod")) &&
                      Dlg::filesNameOpensResource(QStringLiteral("resourcepack")) &&
                      Dlg::filesNameOpensResource(QStringLiteral("datapack")) &&
                      Dlg::filesNameOpensResource(QStringLiteral("shader")),
                  "★★ U10N 模组 / 资源包 / 数据包 / 光影包**四页**都有那颗「详情」"
                  "（判据仍是 filesNameOpensResource()）");
            check(!Dlg::filesNameOpensResource(QStringLiteral("schematic")) &&
                      !Dlg::filesNameOpensResource(QStringLiteral("save")) &&
                      !Dlg::filesNameOpensResource(Dlg::configPageKey()) &&
                      !Dlg::filesNameOpensResource(Dlg::serverPageKey()) &&
                      !Dlg::filesNameOpensResource(QStringLiteral("shortcut")),
                  "★★ U10N 而原理图 / 存档 / 配置文件 / 服务器列表 / 快捷方式上**没有那一颗**"
                  "（前两个在下载页里没有「它自己的那一页」可去，配置文件根本不是个资源）");
            // 没认出来时那一句是**空串**：空串就是"这一行不变成链接"的唯一判据
            // （见 setResourceForPath），所以这一条钉的是"别把空标题也当认出来了"。
            check(Dlg::filesResourceTip(QString()).isEmpty() &&
                      !Dlg::filesResourceTip(QStringLiteral("材质包甲")).isEmpty(),
                  "★ U10N 没认出来（标题空）⇒ 一个字的提示都没有；认出来了才有那一句");

            // ③ ★★ ：**原来这一格是钉 `filesDetailHint()` 那句话的**
            // （「行尾那颗 ⓘ 能进它自己的那一页，单击选中一条，按住 Ctrl 能多选，
            // Ctrl+A 全选」—— 它当年存在的意义是"说出去的话和代码是一件事"：
            // 名字上那一发退了场，旧句子留着会让用户照着一个不存在的动线点）。
            // 现在**那句话连同 `filesDetailHint()` 一起删了**（用户："把这个页面下方那段
            // 叙述删了来让框拉到底充分利用空间"），那四页底下**一个 QLabel 都不建**。
            // ⇒ 这一格没有可钉的东西了，**它钉的两件事搬到了 U10j 和 U10M**：
            // / U10j：那四页**找不到** `filesHintName(key)` 那个控件、其余页找得到；
            // / U10M：那四页 `filesHint()` 一律回空串。
            // 留在这一格的教训，别再犯一遍：删一个界面元素的时候，**先问一句"哪条断言
            // 是钉它的"** —— 断言不比产品少写更值钱，钉着一个已删的东西的断言只会红，
            // 然后被人顺手注释掉（`wtfml-comment-vs-code`：字面声称的那一侧就是洞）。

            // ② 界面上：认出来的那一条是链接的样子，没认出来的那一条不是。
            QTemporaryDir scratch2;
            const QString root2 = scratch2.path();
            const auto mkfile2 = [](const QString &path) {
                QDir().mkpath(QFileInfo(path).absolutePath());
                QFile file(path);
                if (file.open(QIODevice::WriteOnly)) {
                    file.write(QByteArray(4096, 'y'));
                }
            };
            mkfile2(root2 + QStringLiteral("/resourcepacks/材质包甲.zip"));
            mkfile2(root2 + QStringLiteral("/resourcepacks/没认出来的.zip"));
            mkfile2(root2 + QStringLiteral("/mods/模组甲.jar"));
            QHash<QString, QString> dirs2;
            dirs2.insert(QStringLiteral("mod"), root2 + QStringLiteral("/mods"));
            dirs2.insert(QStringLiteral("resourcepack"), root2 + QStringLiteral("/resourcepacks"));
            dirs2.insert(QStringLiteral("datapack"), root2 + QStringLiteral("/datapacks"));
            dirs2.insert(QStringLiteral("shader"), root2 + QStringLiteral("/shaderpacks"));
            const QString packPath = root2 + QStringLiteral("/resourcepacks/材质包甲.zip");
            const QString unknownPath = root2 + QStringLiteral("/resourcepacks/没认出来的.zip");
            const QString modPath = root2 + QStringLiteral("/mods/模组甲.jar");

            Dlg linked(instanceDisplayName, instanceDirName, Dlg::Override(), global,
                       QStringList(), true);
            linked.setInstanceDir(root2);
            linked.setFileDirs(dirs2);
            linked.showPage(QStringLiteral("resourcepack"));
            linked.show();

            const auto rowForPath = [](Dlg &d, const QString &key, const QString &path) {
                InstanceFilesPage *page = d.filesPage(key);
                if (page == nullptr || page->list() == nullptr) {
                    return -1;
                }
                for (int row = 0; row < page->list()->count(); ++row) {
                    QListWidgetItem *item = page->list()->item(row);
                    if (item != nullptr &&
                        item->data(InstanceFilesPage::pathRole()).toString() == path) {
                        return row;
                    }
                }
                return -1;
            };
            const auto nameLabelFor = [&rowForPath](Dlg &d, const QString &key,
                                                    const QString &path) -> QLabel * {
                const int row = rowForPath(d, key, path);
                InstanceFilesPage *page = d.filesPage(key);
                return row < 0 || page == nullptr ? nullptr : page->nameLabelAt(row);
            };
            const auto detailButtonFor = [&rowForPath](Dlg &d, const QString &key,
                                                       const QString &path) -> QToolButton * {
                const int row = rowForPath(d, key, path);
                InstanceFilesPage *page = d.filesPage(key);
                return row < 0 || page == nullptr ? nullptr : page->detailButtonAt(row);
            };

            // 认出来的那一条，报"点得动"的方式就是**给一个标题**（这一层不知道资源长什么样，
            // 名字是 MainWindow 从反查那一趟带回来的）。
            linked.setFileResource(packPath, QStringLiteral("材质包甲"));
            QLabel *packLabel = nameLabelFor(linked, QStringLiteral("resourcepack"), packPath);
            QLabel *unknownLabel =
                nameLabelFor(linked, QStringLiteral("resourcepack"), unknownPath);
            check(packLabel != nullptr && unknownLabel != nullptr,
                  "U10N 前提：资源包那一页上那两个文件都列出来了");

            // 名字那一格**两种行都不再是链接**（用户 点名把这一件事挪到那颗
            // 按钮上，单击改成选中）。剩下的只是那张老底：普通文字色 + 箭头光标 + 完整
            // 路径那个提示（名字是省略过的，`ElidedLabel` 一直挂着那条路径）。
            const QString plainTip = QDir::toNativeSeparators(unknownPath);
            check(packLabel != nullptr && unknownLabel != nullptr &&
                      packLabel->cursor().shape() != Qt::PointingHandCursor &&
                      packLabel->palette().color(QPalette::WindowText) !=
                          packLabel->palette().color(QPalette::Link) &&
                      packLabel->toolTip() != plainTip &&
                      packLabel->toolTip().contains(
                          QDir::toNativeSeparators(packPath)) &&
                      unknownLabel->cursor().shape() != Qt::PointingHandCursor &&
                      unknownLabel->palette().color(QPalette::WindowText) !=
                          unknownLabel->palette().color(QPalette::Link) &&
                      unknownLabel->toolTip() == plainTip,
                  "★★ U10N 名字那一格**不再是链接**（认得出认不出都一样：普通文字色 + 箭头"
                  "光标）—— 它现在只负责「选中这一行」，详情在那颗按钮上",
                  packLabel != nullptr ? packLabel->toolTip() : QString());

            // ---- 那颗「详情」：认出来了才点得动 ----
            QToolButton *packDetail = detailButtonFor(linked, QStringLiteral("resourcepack"), packPath);
            QToolButton *unknownDetail =
                detailButtonFor(linked, QStringLiteral("resourcepack"), unknownPath);
            check(packDetail != nullptr && unknownDetail != nullptr,
                  "★★ U10N 资源包那一页**每一行尾巴上都有一颗「详情」**（认得出认不出都有"
                  "—— 一列下来那三格要对得齐）");
            check(packDetail != nullptr && packDetail->text() == InstanceFilesPage::detailText() &&
                      packDetail->isEnabled() &&
                      packDetail->toolTip() == Dlg::filesResourceTip(QStringLiteral("材质包甲")),
                  "★★ U10N 认出来的那一条：那颗「详情」**点得动**，提示就是这一条资源"
                  "（没这几样的话用户根本不知道它能点 —— 「看着像一行普通文字」和「点不动」"
                  "在屏幕上一模一样）",
                  packDetail != nullptr ? packDetail->toolTip() : QString());
            check(unknownDetail != nullptr && !unknownDetail->isEnabled() &&
                      !unknownDetail->toolTip().contains(
                          Dlg::filesResourceTip(QStringLiteral("材质包甲"))),
                  "★★ U10N 而**没认出来的那一条那颗按钮是灰的**（看得见、按不动 —— 和"
                  "「这一行没有这一格」不一样，一列下来三格要对得齐）",
                  unknownDetail != nullptr ? unknownDetail->toolTip() : QString());

            // 行尾那几格的**次序**：**`[详情][开关][删除]`**。
            // 资源包这一页的那颗「启用 / 禁用」**整个退场**了 —— 改名当启停
            // 对资源包已经不成立（`fileKeyCanToggle()` 只留给模组和光影，资源包改走
            // `options.txt` 的启用顺序页，见 U10d），所以这一页的行尾只剩
            // **`[详情][删除]`**。这条断言问的次序还在（详情最左、删除最右），多钉一个
            // "开关这一格**压根没有**"（`toggleButtonAt()` 回 nullptr）—— 只钉前两格的话，
            // 开关哪天被顺手加回来照样绿。
            InstanceFilesPage *packPage = linked.filesPage(QStringLiteral("resourcepack"));
            const int packRow = rowForPath(linked, QStringLiteral("resourcepack"), packPath);
            QWidget *packRowWidget =
                packRow < 0 ? nullptr : packPage->list()->itemWidget(packPage->list()->item(packRow));
            QLayout *packRowLayout = packRowWidget != nullptr ? packRowWidget->layout() : nullptr;
            QAbstractButton *packToggle =
                packRow < 0 ? nullptr : packPage->toggleButtonAt(packRow);
            QToolButton *packDelete = packRow < 0 ? nullptr : packPage->deleteButtonAt(packRow);
            check(packRowLayout != nullptr && packDetail != nullptr && packDelete != nullptr &&
                      packToggle == nullptr && packRowLayout->indexOf(packDetail) >= 0 &&
                      packRowLayout->indexOf(packDetail) < packRowLayout->indexOf(packDelete),
                  "★★ U10N 资源包那一页行尾是 **[详情][删除]**：改名开关退场（资源包走"
                  "options.txt 的启用顺序页），详情在最左、删除在最右",
                  QStringLiteral("详情=%1 开关=%2 删除=%3")
                      .arg(packRowLayout != nullptr ? packRowLayout->indexOf(packDetail) : -1)
                      .arg(packToggle != nullptr ? packRowLayout->indexOf(packToggle) : -1)
                      .arg(packRowLayout != nullptr ? packRowLayout->indexOf(packDelete) : -1));

            // ③ 点一下：报的**就是这一条文件的路径**，而且只报一次。
            QStringList reported;
            const QMetaObject::Connection conn = QObject::connect(
                &linked, &Dlg::fileResourceRequested, &linked,
                [&reported](const QString &pageKey, const QString &path) {
                    reported.append(pageKey + QStringLiteral(" ") + path);
                });
            if (packDetail != nullptr) {
                packDetail->click();
            }
            check(reported == QStringList{QStringLiteral("resourcepack ") + packPath},
                  "★★ U10N 点那颗「详情」：报的**就是这一页 + 这一条文件的路径**（只报一次）"
                  " —— 报错页或者报错文件，症状都是「点 A 开出 B」",
                  reported.join(QStringLiteral(" | ")));

            // 点**没认出来**的那一颗：一个字节都不该报（它本来就是 enable=false）。
            reported.clear();
            if (unknownDetail != nullptr) {
                unknownDetail->click();
            }
            check(reported.isEmpty(),
                  "★★ U10N 点那颗灰的：什么都不报（正确界面就是「按下去没反应」）",
                  reported.join(QStringLiteral(" | ")));

            // ④ 模组那一页： 下半句之后它也**接上了** —— 和资源包那一页逐字同一条
            // 路（同一个 setFileResource()、同一个 filesResourceTip()）。判据仍然只有
            // filesNameOpensResource() 一处。
            const QString modTitle = QStringLiteral("模组甲");
            linked.showPage(QStringLiteral("mod"));
            linked.setFileResource(modPath, modTitle);
            QToolButton *modDetail = detailButtonFor(linked, QStringLiteral("mod"), modPath);
            check(modDetail != nullptr && modDetail->isEnabled() &&
                      modDetail->toolTip() == Dlg::filesResourceTip(modTitle),
                  "★★ U10N 模组那一页的「详情」**也认得出**（原话："
                  "「对于模组，点进去和资源包差不多」）",
                  modDetail != nullptr ? modDetail->toolTip() : QString());
            reported.clear();
            if (modDetail != nullptr) {
                modDetail->click();
            }
            check(reported == QStringList{QStringLiteral("mod ") + modPath},
                  "★★ U10N 点模组那一颗：报的是「mod + 完整路径」（和资源包那一页一字不差）",
                  reported.join(QStringLiteral(" | ")));

            // ⑤ 同一把路径换了内容（大小 / 时间变了 ⇒ 那一趟会拿一个**空标题**过来）：
            // 上一趟记住的资源得作废 —— 那一格要从"点得动"**退回"点不动"**，不然点进去
            // 是另一个资源。
            linked.setFileResource(packPath, QString());
            QToolButton *unlinked =
                detailButtonFor(linked, QStringLiteral("resourcepack"), packPath);
            check(unlinked != nullptr && !unlinked->isEnabled() &&
                      !unlinked->toolTip().contains(
                          Dlg::filesResourceTip(QStringLiteral("材质包甲"))),
                  "★★ U10N 后来改回「没认出来」时那一颗要**退回灰的**（空标题不是"
                  "「没话说」，是「把上一趟那个作废」—— 留着的话点进去是另一个资源）");
            QObject::disconnect(conn);
        }

        // ---- U10Q 批量管理：多选 / Ctrl+A / 悬浮操作条 / 灰化----
        //
        // 要求：「单击改为选中，同时在该界面按 ctrl+a 有全选效果，在选中任意
        // 数量模组时在列表下方的位置（**不是表尾而是悬浮**）显示几个按钮：更新 禁用 启用 全选
        // 反选 取消 删除，如果选中的所有 mod 都没有更新 / 都已经启用 / 禁用，则对应的按钮变成
        // 灰色不可选状态」。
        //
        // 这一节量的是**这一层能验的那一半**：开关摆对了没有、选中集怎么攒、条子在不在、
        // 三颗的灰化规则算得对不对。**真动盘**那一半（更新 / 禁用 / 删除落到 MainWindow）在
        // mw_check 的 T28 一带。
        {
            QTemporaryDir batchScratch;
            const QString bRoot = batchScratch.path();
            const auto mkfileB = [](const QString &path) {
                QDir().mkpath(QFileInfo(path).absolutePath());
                QFile file(path);
                if (file.open(QIODevice::WriteOnly)) {
                    file.write(QByteArray(2048, 'z'));
                }
            };
            // 三条：两条启用着、一条禁用着（`.disabled` 后缀就是这个意思，见 disabledSuffix）。
            mkfileB(bRoot + QStringLiteral("/mods/甲.jar"));
            mkfileB(bRoot + QStringLiteral("/mods/乙.jar"));
            mkfileB(bRoot + QStringLiteral("/mods/丙.jar") + InstanceFilesPage::disabledSuffix());
            const QString firstMod = bRoot + QStringLiteral("/mods/甲.jar");
            const QString secondMod = bRoot + QStringLiteral("/mods/乙.jar");
            const QString disabledMod =
                bRoot + QStringLiteral("/mods/丙.jar") + InstanceFilesPage::disabledSuffix();
            // 再摆一个**不开批量**的页面（存档那一页）作对照。
            mkfileB(bRoot + QStringLiteral("/saves/世界甲/level.dat"));
            QHash<QString, QString> bDirs;
            bDirs.insert(QStringLiteral("mod"), bRoot + QStringLiteral("/mods"));
            bDirs.insert(QStringLiteral("save"), bRoot + QStringLiteral("/saves"));

            Dlg batch(instanceDisplayName, instanceDirName, Dlg::Override(), global,
                      QStringList(), true);
            batch.setInstanceDir(bRoot);
            batch.setFileDirs(bDirs);
            batch.showPage(QStringLiteral("mod"));
            batch.show();

            InstanceFilesPage *mods = batch.filesPage(QStringLiteral("mod"));
            check(mods != nullptr && mods->list() != nullptr && mods->batchMode(),
                  "U10Q 前提：模组那一页是「开了批量」的那一页");
            if (mods != nullptr && mods->list() != nullptr) {
                QListWidget *list = mods->list();
                const auto rowOf = [list](const QString &path) {
                    for (int row = 0; row < list->count(); ++row) {
                        QListWidgetItem *item = list->item(row);
                        if (item != nullptr &&
                            item->data(InstanceFilesPage::pathRole()).toString() == path) {
                            return row;
                        }
                    }
                    return -1;
                };
                const int modRowA = rowOf(firstMod);
                const int modRowB = rowOf(secondMod);
                const int modRowDisabled = rowOf(disabledMod);
                check(list->count() == 3 && modRowA >= 0 && modRowB >= 0 && modRowDisabled >= 0,
                      "U10Q 前提：模组那一页三条都列出来了（两条启用、一条禁用）",
                      QString::number(list->count()));

                // ---- ① 多选那一档：只有开了批量的页是 MultiSelection ----
                check(list->selectionMode() == QAbstractItemView::MultiSelection,
                      "★★ U10Q 开了批量的页是**多选**（MultiSelection：「点一下切换」那一种，"
                      "不是「按住 Ctrl 那一种」）—— 单选的话「选中任意数量」这句话根本不成立",
                      QString::number(static_cast<int>(list->selectionMode())));

                // ---- ①b 「点一下切换」那一套（要求「点击的时候处于选中，再点一次才是
                // 取消，在选中的状态下点击其它的就是选中多个文件」）----
                //
                // 这一发**合成真鼠标事件**（`QApplication::sendEvent` 直接发给 viewport：
                // 行控件是 viewport 的孩子、盖在行上面，走真命中测试的话事件会落到那个
                // `QWidget` 上而不是视图）。这一发是**这一档的唯一可验之处** —— 把
                // MultiSelection 换回 ExtendedSelection，下面第一条就会红（那种模式下发
                // 第一发是"清掉别的、只留这一条"：先点甲、再点甲**不会取消**，永远是选中的）。
                const auto clickRow = [list, &rowOf](const QString &path) {
                    const int row = rowOf(path);
                    if (row < 0 || list->item(row) == nullptr) {
                        return;
                    }
                    const QPoint at = list->visualItemRect(list->item(row)).center();
                    const QPointF local(at);
                    const QPointF global = list->viewport()->mapToGlobal(at);
                    QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton,
                                      Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(list->viewport(), &press);
                    QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton,
                                        Qt::NoButton, Qt::NoModifier);
                    QApplication::sendEvent(list->viewport(), &release);
                };
                list->clearSelection();
                clickRow(firstMod);
                check(list->selectedItems().size() == 1 &&
                          list->selectedItems().first()->data(InstanceFilesPage::pathRole())
                                  .toString() == firstMod,
                      "★★ U10Q 点第一发：**这一条选上了**（用户要的第一步）",
                      QString::number(list->selectedItems().size()));
                clickRow(firstMod);
                check(list->selectedItems().isEmpty(),
                      "★★ U10Q 在**同一条**上再点一发：**取消了**（用户要的第二步 —— "
                      "ExtendedSelection 在这一发上会保持选中，这正是它不行的原因）",
                      QString::number(list->selectedItems().size()));
                clickRow(firstMod);
                clickRow(secondMod);
                check(list->selectedItems().size() == 2 && mods->selectionCountLabel() != nullptr &&
                          mods->selectionCountLabel()->text() ==
                              InstanceFilesPage::selectionCountText(2),
                      "★★ U10Q 点另一条：**两个都在选中里**，而且那句计数跟着写 2"
                      "（用户原话「在选中的状态下点击其它的就是选中多个文件（已选择 2 个文件"
                      "等等）」）",
                      mods->selectionCountLabel() != nullptr ? mods->selectionCountLabel()->text()
                                                             : QString());
                list->clearSelection();

                // ---- ② 什么都没选时那条**不露** ----
                QWidget *bar = mods->selectionBar();
                QLabel *countLabel = mods->selectionCountLabel();
                check(bar != nullptr && countLabel != nullptr,
                      "U10Q 前提：那条悬浮操作条和它左边那句计数都在");
                check(bar != nullptr && !bar->isVisible(),
                      "★★ U10Q 一条都没选中时那条操作条**不露**（凭空压着列表下缘就是错的）");

                // ---- ③ Ctrl+A：合成一发（用户点名的那一条）----
                //
                // NOTE: 这一发走的是 `QAbstractItemView::keyPressEvent()` 里那条
                // `QKeySequence::SelectAll` —— 全仓没有哪一处自己写过 Ctrl+A。
                QKeyEvent selectAllPress(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier);
                QApplication::sendEvent(list, &selectAllPress);
                check(list->selectedItems().size() == list->count(),
                      "★★ U10Q 在列表里按 Ctrl+A **全选**（用户点名：「按 ctrl+a 有全选效果」）",
                      QStringLiteral("%1/%2").arg(list->selectedItems().size()).arg(list->count()));
                check(bar != nullptr && bar->isVisible() &&
                          countLabel != nullptr &&
                          countLabel->text() == InstanceFilesPage::selectionCountText(list->count()),
                      "★★ U10Q 选中之后那条操作条**露出来**，而且左边那句写的就是选中数",
                      countLabel != nullptr ? countLabel->text() : QString());

                // ---- ④ 灰化规则：拿一次只选一条来逐个量 ----
                //
                // 四条判据（见 .hpp 里 SelectionAction 那段 / .cpp 里 updateSelectionBar）：
                // 更新 = 选中的里**至少一条**在 m_outdatedPaths 里；
                // 禁用 = 至少一条是启用着的；启用 = 至少一条是禁用着的；
                // 全选 / 反选 / 取消 / 删除 恒可用。
                const auto buttonOf = [mods](InstanceFilesPage::SelectionAction action) {
                    return mods->selectionButtonAt(static_cast<int>(action));
                };
                const auto selectOnly = [list, &rowOf](const QString &path) {
                    list->clearSelection();
                    const int row = rowOf(path);
                    if (row >= 0) {
                        list->item(row)->setSelected(true);
                    }
                };

                // 只选那条**启用着**的、而且没有更新结果 ⇒ 更新灰、禁用亮、启用灰。
                mods->setUpdateResult(QStringList(), false);
                selectOnly(firstMod);
                check(countLabel != nullptr &&
                          countLabel->text() == InstanceFilesPage::selectionCountText(1),
                      "U10Q 只选一条时那句计数写 1",
                      countLabel != nullptr ? countLabel->text() : QString());
                check(buttonOf(InstanceFilesPage::SelectionAction::Update) != nullptr &&
                          !buttonOf(InstanceFilesPage::SelectionAction::Update)->isEnabled() &&
                          buttonOf(InstanceFilesPage::SelectionAction::Disable) != nullptr &&
                          buttonOf(InstanceFilesPage::SelectionAction::Disable)->isEnabled() &&
                          buttonOf(InstanceFilesPage::SelectionAction::Enable) != nullptr &&
                          !buttonOf(InstanceFilesPage::SelectionAction::Enable)->isEnabled(),
                      "★★ U10Q 选中的都是启用着的、且都没有更新 ⇒ 「更新」和「启用」是**灰的**、"
                      "「禁用」亮着（用户点名的灰化规则）");

                // 只选那条**禁用着**的 ⇒ 反过来了：启用亮、禁用灰。
                selectOnly(disabledMod);
                check(buttonOf(InstanceFilesPage::SelectionAction::Disable) != nullptr &&
                          !buttonOf(InstanceFilesPage::SelectionAction::Disable)->isEnabled() &&
                          buttonOf(InstanceFilesPage::SelectionAction::Enable) != nullptr &&
                          buttonOf(InstanceFilesPage::SelectionAction::Enable)->isEnabled(),
                      "★★ U10Q 选中的是那条**禁用着**的 ⇒ 「启用」亮、「禁用」灰（同一条规则反过来）");

                // 给它一个「这一条有新版」⇒ 「更新」就亮了。
                mods->setUpdateResult({disabledMod}, true);
                selectOnly(disabledMod);
                check(buttonOf(InstanceFilesPage::SelectionAction::Update) != nullptr &&
                          buttonOf(InstanceFilesPage::SelectionAction::Update)->isEnabled(),
                      "★★ U10Q 选中的那条被查出来**有新版本** ⇒ 「更新」亮起来（没有更新才是灰的）");
                // 而**没查出来的那一条**选上时「更新」仍旧是灰的（至少一条，不是全部）。
                selectOnly(firstMod);
                check(buttonOf(InstanceFilesPage::SelectionAction::Update) != nullptr &&
                          !buttonOf(InstanceFilesPage::SelectionAction::Update)->isEnabled(),
                      "★★ U10Q 换成那条**没有新版**的 ⇒ 「更新」又灰回去（判据是「选中的里有"
                      "至少一条有更新」）");

                // 全选 / 反选 / 取消 / 删除 四颗**恒可用**（前两颗只动选择集，后两颗按下去
                // 还会问一句 —— 灰着反倒像"删不了"）。
                check(buttonOf(InstanceFilesPage::SelectionAction::SelectAll) != nullptr &&
                          buttonOf(InstanceFilesPage::SelectionAction::SelectAll)->isEnabled() &&
                          buttonOf(InstanceFilesPage::SelectionAction::InvertAll) != nullptr &&
                          buttonOf(InstanceFilesPage::SelectionAction::InvertAll)->isEnabled() &&
                          buttonOf(InstanceFilesPage::SelectionAction::ClearAll) != nullptr &&
                          buttonOf(InstanceFilesPage::SelectionAction::ClearAll)->isEnabled() &&
                          buttonOf(InstanceFilesPage::SelectionAction::Delete) != nullptr &&
                          buttonOf(InstanceFilesPage::SelectionAction::Delete)->isEnabled(),
                      "★★ U10Q 全选 / 反选 / 取消 / 删除**恒可用**（它们不该跟着灰化规则走）");

                // 模组页开了「操作面板」那一档（`setActionPanelEnabled(true)`）：
                // 七颗按钮一个都没少（`selectionActionCount()` 仍是 7，选择/灰化规则不变），
                // 但**摆放换了地方**——
                // / 更新 / 删除在右侧那块操作面板里（名字也换成了「更新选中项 / 删除选中项」）；
                // / 全选 / 反选 / 清除选择在顶上那排工具里（面板外面）；
                // / 启用 / 禁用**收进「启用 / 禁用…」菜单**，不再并排两颗
                //   （菜单里两项各自按 `selectedTogglePaths()` 现算灰不灰 —— 见 U10R）。
                // 用户当初点名的并排顺序只在**没开面板**的那几页（资源包 / 数据包 / 光影）上
                // 还是原样；面板这一档是后来照用户要求改的，不是顺序丢失。
                // 判据用 `isAncestorOf()` 而不是 `isVisibleTo()`：后者只顺着**自己**那条
                // 可见链往上走，走到给定控件为止 —— 给它的那个控件根本不是祖先时，它照样
                // 可能回 true（"看不见"这件事它答不出来）。
                const auto inPanel = [bar](QPushButton *button) {
                    return button != nullptr && bar->isAncestorOf(button) && button->isVisible();
                };
                const auto outOfPanel = [bar](QPushButton *button) {
                    return button != nullptr && !bar->isAncestorOf(button);
                };
                check(InstanceFilesPage::selectionActionCount() == 7 &&
                          inPanel(buttonOf(InstanceFilesPage::SelectionAction::Update)) &&
                          inPanel(buttonOf(InstanceFilesPage::SelectionAction::Delete)),
                      "★★ U10Q 开了操作面板：七颗都还在，「更新」「删除」在右侧面板里",
                      QString::number(InstanceFilesPage::selectionActionCount()));
                check(buttonOf(InstanceFilesPage::SelectionAction::Enable) != nullptr &&
                          buttonOf(InstanceFilesPage::SelectionAction::Disable) != nullptr &&
                          !buttonOf(InstanceFilesPage::SelectionAction::Enable)->isVisible() &&
                          !buttonOf(InstanceFilesPage::SelectionAction::Disable)->isVisible() &&
                          outOfPanel(buttonOf(InstanceFilesPage::SelectionAction::SelectAll)) &&
                          outOfPanel(buttonOf(InstanceFilesPage::SelectionAction::InvertAll)) &&
                          outOfPanel(buttonOf(InstanceFilesPage::SelectionAction::ClearAll)),
                      "★★ U10Q 启用/禁用收进「启用 / 禁用…」菜单；全选/反选/清除选择在顶部工具行"
                      "（都不再挤在面板里）");
                check(mods->findChild<QPushButton *>(QStringLiteral("modStateAction")) != nullptr,
                      "★★ U10Q 面板上有那颗「启用 / 禁用…」（这一档的两颗开关就在它菜单里）");

                // ---- ⑤ 「更新」这一颗按下去报的**是选中那些的路径** ----
                QStringList reportedPaths;
                bool reportedDisable = true;
                const QMetaObject::Connection updConn = QObject::connect(
                    mods, &InstanceFilesPage::batchUpdateRequested, mods,
                    [&reportedPaths](const QStringList &paths) { reportedPaths = paths; });
                mods->setUpdateResult({firstMod}, true);
                selectOnly(firstMod);
                list->item(rowOf(secondMod))->setSelected(true);
                if (buttonOf(InstanceFilesPage::SelectionAction::Update) != nullptr) {
                    buttonOf(InstanceFilesPage::SelectionAction::Update)->click();
                }
                check(reportedPaths.size() == 2 && reportedPaths.contains(firstMod) &&
                          reportedPaths.contains(secondMod),
                      "★★ U10Q 点「更新」报的是**选中的那一批路径**（一条一条走，不是只报第一行）",
                      reportedPaths.join(QStringLiteral(" | ")));
                QObject::disconnect(updConn);

                const QMetaObject::Connection togConn = QObject::connect(
                    mods, &InstanceFilesPage::batchToggleRequested, mods,
                    [&reportedPaths, &reportedDisable](const QStringList &paths, bool disable) {
                        reportedPaths = paths;
                        reportedDisable = disable;
                    });
                reportedPaths.clear();
                selectOnly(firstMod);
                if (buttonOf(InstanceFilesPage::SelectionAction::Disable) != nullptr) {
                    buttonOf(InstanceFilesPage::SelectionAction::Disable)->click();
                }
                check(reportedPaths == QStringList{firstMod} && reportedDisable,
                      "★★ U10Q 点「禁用」报的是(路径, disable=true)（「启用」那一颗是 false）",
                      reportedPaths.join(QStringLiteral(" | ")));
                QObject::disconnect(togConn);

                // ---- ⑥ 「全选」/「取消」两颗自己动选择集（用户点名那两颗）----
                list->clearSelection();
                if (buttonOf(InstanceFilesPage::SelectionAction::SelectAll) != nullptr) {
                    buttonOf(InstanceFilesPage::SelectionAction::SelectAll)->click();
                }
                check(list->selectedItems().size() == list->count(),
                      "U10Q 点「全选」= 现在列出来的那些全选上",
                      QString::number(list->selectedItems().size()));
                // 「反选」= 现在列出来的那些里，没选的选上、选着的取消。全选之后再按一下
                // 必然是**一条都不剩** —— 只把"选着的取消"做了一半的实现会在这儿露出来。
                if (buttonOf(InstanceFilesPage::SelectionAction::InvertAll) != nullptr) {
                    buttonOf(InstanceFilesPage::SelectionAction::InvertAll)->click();
                }
                check(list->selectedItems().isEmpty(),
                      "★★ U10Q 全选之后点「反选」⇒ 一条都不剩（每一条都真的翻了个面）",
                      QString::number(list->selectedItems().size()));
                if (buttonOf(InstanceFilesPage::SelectionAction::ClearAll) != nullptr) {
                    buttonOf(InstanceFilesPage::SelectionAction::ClearAll)->click();
                }
                check(list->selectedItems().isEmpty() && bar != nullptr && !bar->isVisible(),
                      "★★ U10Q 点「取消」清空选择 ⇒ 那条操作条跟着收回去");

                // ---- ⑦ 行尾那颗「详情」：认得出资源的那一条亮着，认不出的那一条灰着 ----
                //
                // 要求：「把点击条目进入对应详情页的功能变成一个详情按钮，
                // 放到开关和删除中间」。「认不认得出」是这一格能不能点的**唯一**判据（见
                // setResourceForPath 那段：在 m_resourceTips 里 = 认出来了）。这一节只喂
                // 一条手工的账；真去问 Modrinth、真开那一页在 mw_check / 真机那两处。
                check(mods->detailButtonAt(rowOf(firstMod)) != nullptr &&
                          !mods->detailButtonAt(rowOf(firstMod))->isEnabled(),
                      "★★ U10Q 没认出来的那一行，那颗「详情」是**灰的**（认不出是哪个资源，"
                      "点进去只会是一张空页）");
                check(mods->setResourceForPath(firstMod, QStringLiteral("测试资源")) &&
                          mods->detailButtonAt(rowOf(firstMod)) != nullptr &&
                          mods->detailButtonAt(rowOf(firstMod))->isEnabled(),
                      "★★ U10Q 认出来的那一行那颗「详情」**亮起来**（setResourceForPath 的"
                      "返回值就是「认没认出来」这件事本身）");

                // ---- ⑧ refresh() 之后选择**按路径保住**（ 真机揪出来的缺陷） ----
                //
                // 症状：在真机上点「启用」把一条改回来之后，那一行**没选上**、底下那条操作条
                // 也一起消失。根子 = `refresh()` 里那句 `m_list->clear()` **没挡信号**，而
                // 它接的是「按当前选中重算 m_selectedPaths」—— 此刻表已经空了，于是那份账在
                // **自己的重列过程中**被抹平；紧接着 rebuildRows() 想按路径把选择还回去，
                // 手里已经什么都没有了。
                // 这正是 `wtfml-comment-vs-code` 那一类：rebuildRows() 的注释声称"重列
                // 这一步防住了"，漏的却是它外面那一发 clear()。两处都得挡。
                mods->setUpdateResult(QStringList(), false);
                selectOnly(secondMod);
                check(list->selectedItems().size() == 1, "U10Q 前提：先选中一条");
                mods->refresh();
                check(list->selectedItems().size() == 1 &&
                          list->selectedItems().first()->data(InstanceFilesPage::pathRole())
                                  .toString() == secondMod &&
                          countLabel != nullptr && bar != nullptr && bar->isVisible() &&
                          countLabel->text() == InstanceFilesPage::selectionCountText(1),
                      "★★ U10Q 走过一趟 `refresh()`（改名 / 刷新 / 筛一下都必经它）之后，"
                      "选择**按路径还在**、那条操作条也还露着",
                      QStringLiteral("refresh 之后选中 %1 条")
                          .arg(list->selectedItems().size()));

                // ---- ⑨ 不开批量的那一页：**单选** + 没有详情那一格 + 仍旧是下拉框 ----
                batch.showPage(QStringLiteral("save"));
                InstanceFilesPage *saves = batch.filesPage(QStringLiteral("save"));
                check(saves != nullptr && saves->list() != nullptr && !saves->batchMode(),
                      "U10Q 前提：存档那一页是**没开批量**的那一页");
                if (saves != nullptr && saves->list() != nullptr) {
                    check(saves->list()->selectionMode() == QAbstractItemView::SingleSelection,
                          "★★ U10Q 没开批量的页仍是**单选**（Ctrl+A 自然也就不通 —— 这正是"
                          "「这一套只铺那四页」的落法）",
                          QString::number(static_cast<int>(saves->list()->selectionMode())));
                    check(saves->list()->count() > 0 ? saves->detailButtonAt(0) == nullptr : true,
                          "★★ U10Q 没开批量的页上**没有**那颗「详情」（它只在开了批量的页上建）");
                    check(saves->stateCombo() != nullptr && saves->stateButtonAt(
                              InstanceFilesPage::StateFilter::All) != nullptr &&
                              !saves->stateCombo()->isVisibleTo(saves) &&
                              !saves->stateButtonAt(InstanceFilesPage::StateFilter::All)
                                   ->isVisibleTo(saves),
                          "U10Q 存档页不显示启用/禁用筛选，也不显示资源批量筛选按钮");
                    check(saves->selectionBar() != nullptr &&
                              !saves->selectionBar()->isVisible(),
                          "★★ U10Q 没开批量的页上那条操作条**不露**（哪怕把行选上）");
                }
            }

            // ---- ⑩ 那条操作条**滚轮搬不动它**（ 用户报的那个）----
            //
            // 要求：「选中以后出现下方框，但是滚动滚轮以后框会跟着跑了」。
            // 根子在 Qt：`QAbstractItemViewPrivate::scrollContentsBy()` 滚的是
            // `viewport->scroll(dx, dy)`，而 `QWidget::scroll()` 是**连孩子一起挪**的
            // （`QWidgetPrivate::scroll_sys()` 第一句 `scrollChildren(dx, dy)`）。所以只要
            // 这条浮层是 **viewport 的孩子**，它就会跟着滚。产品那边的修法是**换爹**：
            // 挂到 `m_list` 底下（viewport 的**兄弟**），于是被滚的那一棵里没有它。
            //
            // 这一节量两件事，**顺序不能反**：
            // ① 结构：`parentWidget() == list`（**不是 viewport**）—— 这是根因那一句；
            // ② 行为：滚到底之后 `geometry()` 一个像素都没动 —— 这是用户看见的那一句。
            // 两条都要，因为①可能被"再加一层中间容器"改对而②仍旧对，反过来②也可能因为
            // 产品在 scroll 之后顺手 reposition 一次而假绿（那样"滚轮一挪它就跳回原位"
            // 看不出区别，但下一帧还是会抖）。
            {
                QTemporaryDir scrollScratch;
                const QString sRoot = scrollScratch.path();
                const auto mkfileS = [](const QString &path) {
                    QDir().mkpath(QFileInfo(path).absolutePath());
                    QFile file(path);
                    if (file.open(QIODevice::WriteOnly)) {
                        file.write(QByteArray(64, 'w'));
                    }
                };
                // 够多行，才滚得动（每行 46，视口就那么高，给 60 条稳稳溢出）。
                //
                // NOTE: 这一节**故意用光影页**，不用模组页。模组页开了「操作面板」那一档
                // （setActionPanelEnabled(true)，见下面的 U10R 那一批），操作条被换爹到
                // 内容区里、还常驻着；而这一节要量的正是**没换爹**的那个形状 ——
                // 「浮在列表下缘、挂 list 底下、滚轮搬不动」。光影页是同一套批量管理，
                // 没有面板那一档，正好是这条判据该落的地方。
                for (int i = 0; i < 60; ++i) {
                    mkfileS(sRoot +
                            QStringLiteral("/shaderpacks/光影%1.zip").arg(i, 3, 10, QLatin1Char('0')));
                }
                QHash<QString, QString> sDirs;
                sDirs.insert(QStringLiteral("shader"), sRoot + QStringLiteral("/shaderpacks"));
                Dlg sc(instanceDisplayName, instanceDirName, Dlg::Override(), global, QStringList(),
                       true);
                sc.setInstanceDir(sRoot);
                sc.setFileDirs(sDirs);
                sc.resize(900, 620);
                sc.showPage(QStringLiteral("shader"));
                sc.show();
                InstanceFilesPage *sp = sc.filesPage(QStringLiteral("shader"));
                check(sp != nullptr && sp->list() != nullptr && sp->selectionBar() != nullptr,
                      "U10Q 前提：滚轮那一节的页 / 列表 / 那条操作条都在");
                if (sp != nullptr && sp->list() != nullptr && sp->selectionBar() != nullptr) {
                    QListWidget *slist = sp->list();
                    QWidget *sbar = sp->selectionBar();
                    check(slist->count() == 60 && slist->verticalScrollBar() != nullptr &&
                              slist->verticalScrollBar()->maximum() > 0,
                          "U10Q 前提：这一页有 60 条、而且**真的滚得动**"
                          "（滚不动的话下面那两条是空转的）",
                          QStringLiteral("count=%1 max=%2")
                              .arg(slist->count())
                              .arg(slist->verticalScrollBar() != nullptr
                                       ? slist->verticalScrollBar()->maximum()
                                       : -1));
                    // ① 结构那一句：**爹是视图本身，不是 viewport**。
                    check(sbar->parentWidget() == slist &&
                              sbar->parentWidget() != slist->viewport(),
                          "★★ U10Q 那条操作条挂在 **`list` 底下**（不是 viewport）—— "
                          "这就是「滚轮搬不动它」的根因那一句（viewport 的孩子会被 "
                          "`viewport->scroll()` 连人一起挪，见 qwidget.cpp 的 scroll_sys）",
                          sbar->parentWidget() != nullptr
                              ? sbar->parentWidget()->objectName()
                              : QStringLiteral("<null>"));
                    // 选一条 ⇒ 它露出来、位置算好。
                    slist->item(0)->setSelected(true);
                    check(sbar->isVisible(),
                          "U10Q 前提：选中一条之后那条操作条露出来了");
                    const QRect before = sp->selectionBarGeometry();
                    check(before.isValid() && before.height() > 0,
                          "U10Q 前提：那条操作条这会儿有真的大小（0×0 的话下面量不出动静）",
                          QStringLiteral("%1,%2 %3×%4")
                              .arg(before.x())
                              .arg(before.y())
                              .arg(before.width())
                              .arg(before.height()));
                    // ② 滚到底。
                    slist->verticalScrollBar()->setValue(slist->verticalScrollBar()->maximum());
                    const QRect after = sp->selectionBarGeometry();
                    check(after == before,
                          "★★ U10Q 滚轮滚到底：那条操作条的 `geometry()` **一个像素都没动**"
                          "（用户报的「滚动滚轮以后框会跟着跑了」）",
                          QStringLiteral("%1,%2 → %3,%4")
                              .arg(before.x())
                              .arg(before.y())
                              .arg(after.x())
                              .arg(after.y()));
                    // 而且它仍旧贴着**视口**下缘居中（换爹之后坐标要多加 viewport->pos()，
                    // 加错的话它整条会歪到视图左上角去）。
                    QWidget *sviewport = slist->viewport();
                    const int expectedLeft =
                        sviewport != nullptr ? sviewport->pos().x() +
                                                   (sviewport->width() - after.width()) / 2
                                             : after.x();
                    const int expectedTop = sviewport != nullptr
                                                ? sviewport->pos().y() + sviewport->height() -
                                                      after.height() - 12  // kSelectionBarMargin
                                                : after.y();
                    check(sviewport != nullptr && after.x() == expectedLeft &&
                              after.y() == expectedTop,
                          "★★ U10Q 而且它**仍旧贴着视口下缘、横向居中**（换爹之后那笔"
                          "`viewport->pos()` 加错了就会整条歪到左上角）",
                          QStringLiteral("x=%1(期望%2) y=%3(期望%4)")
                              .arg(after.x())
                              .arg(expectedLeft)
                              .arg(after.y())
                              .arg(expectedTop));
                }
            }
        }

        // ---- U10R 第四批：行尾次序 / 详情悬停才露 / 右键进详情 / 两颗开关的显隐 /
        // 拨开关不掉出选择----
        //
        // 要求：「详情和启用/禁用换个顺序，且详情只在悬停时显现，右键点击时也可以进入
        // 详情页，此时保留上一页的选择；修改禁用/启用状态时改成没有禁用的时候不显示启用，
        // 但是没有启用（全是禁用）的时候保持显示禁用，此外修复点击开关的时候会取消选中的
        // 问题」。
        {
            QTemporaryDir rScratch;
            const QString rRoot = rScratch.path();
            const auto mkfileR = [](const QString &path) {
                QDir().mkpath(QFileInfo(path).absolutePath());
                QFile file(path);
                if (file.open(QIODevice::WriteOnly)) {
                    file.write(QByteArray(1024, 'r'));
                }
            };
            // 三条：两条启用着、一条禁用着（和 U10Q 那一节的形状一样 —— 这一节要的正是
            // "两种状态都摆得出来"）。
            mkfileR(rRoot + QStringLiteral("/mods/甲.jar"));
            mkfileR(rRoot + QStringLiteral("/mods/乙.jar"));
            mkfileR(rRoot + QStringLiteral("/mods/丙.jar") + InstanceFilesPage::disabledSuffix());
            const QString modA = rRoot + QStringLiteral("/mods/甲.jar");
            const QString modB = rRoot + QStringLiteral("/mods/乙.jar");
            const QString modC =
                rRoot + QStringLiteral("/mods/丙.jar") + InstanceFilesPage::disabledSuffix();
            QHash<QString, QString> rDirs;
            rDirs.insert(QStringLiteral("mod"), rRoot + QStringLiteral("/mods"));

            Dlg fourth(instanceDisplayName, instanceDirName, Dlg::Override(), global, QStringList(),
                       true);
            fourth.setInstanceDir(rRoot);
            fourth.setFileDirs(rDirs);
            fourth.showPage(QStringLiteral("mod"));
            fourth.show();

            InstanceFilesPage *page = fourth.filesPage(QStringLiteral("mod"));
            check(page != nullptr && page->list() != nullptr && page->batchMode(),
                  "U10R 前提：模组那一页是**开了批量**的那一页");
            if (page != nullptr && page->list() != nullptr) {
                QListWidget *list = page->list();
                QWidget *bar = page->selectionBar();
                const auto rowOf = [list](const QString &path) {
                    for (int row = 0; row < list->count(); ++row) {
                        QListWidgetItem *item = list->item(row);
                        if (item != nullptr &&
                            item->data(InstanceFilesPage::pathRole()).toString() == path) {
                            return row;
                        }
                    }
                    return -1;
                };
                const auto selectOnly = [list, &rowOf](const QString &path) {
                    list->clearSelection();
                    const int row = rowOf(path);
                    if (row >= 0 && list->item(row) != nullptr) {
                        list->item(row)->setSelected(true);
                    }
                };
                const auto barButton = [page](InstanceFilesPage::SelectionAction action) {
                    return page->selectionButtonAt(static_cast<int>(action));
                };

                const int rowA = rowOf(modA);
                check(rowA >= 0 && rowOf(modB) >= 0 && rowOf(modC) >= 0 && list->count() == 3,
                      "U10R 前提：三条都列出来了（两条启用、一条禁用）",
                      QString::number(list->count()));

                // ---- ① 行尾三格的次序：`[详情][开关][删除]` ----
                QWidget *rowWidget = list->itemWidget(list->item(rowA));
                check(rowWidget != nullptr, "U10R 前提：那一行是个真控件（setItemWidget 挂上去的）");
                QToolButton *detail = page->detailButtonAt(rowA);
                QAbstractButton *toggle = page->toggleButtonAt(rowA);
                QToolButton *remove = page->deleteButtonAt(rowA);
                QLayout *rowLayout = rowWidget != nullptr ? rowWidget->layout() : nullptr;
                check(rowLayout != nullptr && detail != nullptr && toggle != nullptr &&
                          remove != nullptr && rowLayout->indexOf(detail) >= 0 &&
                          rowLayout->indexOf(detail) < rowLayout->indexOf(toggle) &&
                          rowLayout->indexOf(toggle) < rowLayout->indexOf(remove),
                      "★★ U10R 行尾三格是 **[详情][开关][删除]**（用户原话「详情和启用/禁用"
                      "换个顺序」）",
                      QStringLiteral("详情=%1 开关=%2 删除=%3")
                          .arg(rowLayout != nullptr ? rowLayout->indexOf(detail) : -1)
                          .arg(rowLayout != nullptr ? rowLayout->indexOf(toggle) : -1)
                          .arg(rowLayout != nullptr ? rowLayout->indexOf(remove) : -1));

                // ---- ② 「详情」只在鼠标悬停这一行时露着 ----
                //
                // 判据取 `isVisibleTo()`（不是 `isHidden()`）：这一格平时正是**显式隐藏**着的，
                // 而"显式隐藏位"恰恰是产品摆出来的那一半；要问的是"用户看不看得见"。
                check(detail != nullptr && !detail->isVisibleTo(page),
                      "★★ U10R 一进来那一格是**藏着的**（用户原话「详情只在悬停时显现」）");
                check(detail != nullptr && detail->sizePolicy().retainSizeWhenHidden(),
                      "★★ U10R 而且它藏起来时**占位留着**（RetainSizeWhenHidden）—— 不留的话"
                      "鼠标一进一出整行都要重排，而这一格正好夹在名字和开关中间，抖起来最扎眼");
                const int toggleXBefore =
                    toggle != nullptr ? toggle->mapTo(rowWidget, QPoint(0, 0)).x() : -1;
                // 合成一发 Enter 给**行控件**（真机上 Qt 是往祖先那一串发的：鼠标进了行里
                // 哪个孩子都算进了这一行 —— 所以行控件一定收得到，见 .hpp 里 eventFilter 那段）。
                QEvent enterEvent(QEvent::Enter);
                QApplication::sendEvent(rowWidget, &enterEvent);
                check(detail != nullptr && detail->isVisibleTo(page),
                      "★★ U10R 合成一发 Enter ⇒ 那一格**露出来**（悬停才露的那一半）");
                check(toggle != nullptr && toggleXBefore >= 0 &&
                          toggle->mapTo(rowWidget, QPoint(0, 0)).x() == toggleXBefore,
                      "★★ U10R 露出这一格**没有把别的挤走**（开关的 x 一动不动 —— 这就是"
                      "「占位留着」在屏幕上的意思）",
                      QStringLiteral("露出前 x=%1 / 露出后 x=%2")
                          .arg(toggleXBefore)
                          .arg(toggle != nullptr ? toggle->mapTo(rowWidget, QPoint(0, 0)).x() : -1));
                QEvent leaveEvent(QEvent::Leave);
                QApplication::sendEvent(rowWidget, &leaveEvent);
                check(detail != nullptr && !detail->isVisibleTo(page),
                      "★★ U10R 合成一发 Leave ⇒ 又**收回去**（是「悬停才露」，不是「露过一次"
                      "就一直留着」）");

                // ---- ③ 右键点一行 = 开这一条的详情页，而且那一发**被吃掉** ----
                //
                // 「认得出资源」的那一条才发（判据 = `m_resourceTips`，和那颗 ⓘ 的可用性
                // 逐字相同）。
                page->setResourceForPath(modA, QStringLiteral("甲的资源"));
                QStringList rightClicked;
                const QMetaObject::Connection rcConn =
                    QObject::connect(page, &InstanceFilesPage::detailRequested, page,
                                     [&rightClicked](const QString &path) {
                                         rightClicked.append(path);
                                     });
                // 先选着**另一条**：右键之后这一份选择必须一条都不动（用户点名「此时保留
                // 上一页的选择」）。
                selectOnly(modB);
                check(list->selectedItems().size() == 1,
                      "U10R 前提：右键之前先选着「乙」那一条");
                const QPointF localAt(8, 8);
                const QPointF globalAt = rowWidget->mapToGlobal(QPoint(8, 8));
                QMouseEvent rightPress(QEvent::MouseButtonPress, localAt, globalAt,
                                       Qt::RightButton, Qt::RightButton, Qt::NoModifier);
                // `ignore()` 那一句**不能省**：`QEvent` 生出来时 accepted 位**默认就是真**，
                // 直接断言 `isAccepted()` 是假绿。先把它摆成"还没人接受"，才问得出"产品到底
                // accept 了没有"。而这一位正是真机上"要不要往上冒"的开关：`QApplication::notify`
                // 那个循环判的是 `res && isAccepted()`，不 accept 的话这一发会继续冒到
                // viewport 上、视图顺手就把选中态改掉（见 .hpp 里 eventFilter 那段）。
                rightPress.ignore();
                QApplication::sendEvent(rowWidget, &rightPress);
                check(rightClicked == QStringList{modA},
                      "★★ U10R 右键点一行 ⇒ 发的是**这一条**的 detailRequested（用户原话"
                      "「右键点击时也可以进入详情页」）",
                      rightClicked.join(QStringLiteral(" | ")));
                check(rightPress.isAccepted(),
                      "★★ U10R 那一发被**吃掉**了（产品把 accepted 位翻回真）—— 不 accept 的"
                      "话它会冒到视图上，视图于是自己改选中态，「保留上一页的选择」就不成立");
                check(list->selectedItems().size() == 1 &&
                          list->selectedItems().first()->data(InstanceFilesPage::pathRole())
                                  .toString() == modB,
                      "★★ U10R 右键之后**选中的还是「乙」那一条**（用户点名「此时保留上一页"
                      "的选择」）",
                      QStringLiteral("选中 %1 条").arg(list->selectedItems().size()));
                // 认不出来的那一条：右键**一个字节都不发**（灰的那颗和这一发是同一条判据）。
                rightClicked.clear();
                const int rowB = rowOf(modB);
                QWidget *rowWidgetB =
                    rowB < 0 ? nullptr : list->itemWidget(list->item(rowB));
                QMouseEvent rightPressUnknown(QEvent::MouseButtonPress, localAt, globalAt,
                                              Qt::RightButton, Qt::RightButton, Qt::NoModifier);
                rightPressUnknown.ignore();
                QApplication::sendEvent(rowWidgetB, &rightPressUnknown);
                check(rightClicked.isEmpty(),
                      "★★ U10R 右键点那条**认不出来的** ⇒ 什么都不发（正确界面就是「按下去"
                      "没反应」）",
                      rightClicked.join(QStringLiteral(" | ")));
                QObject::disconnect(rcConn);

                // ---- ④ 两颗开关收进「启用 / 禁用…」菜单（灰化规则不变）----
                //
                // 用户当初的要求是「在禁用旁边增加启用」；模组页后来改成了右侧操作面板，
                // 那两颗并排按钮被收进一颗「启用 / 禁用…」菜单（用户后一轮点名的改法，
                // 见 U10Q 那一段）。菜单里两项的灰化规则和原来并排两颗**逐字相同**：
                // 选中里一条禁用着的都没有 ⇒「全部启用」灰；全是禁用 ⇒「全部禁用」灰；
                // 两种都有 ⇒ 两项都亮。判据仍然挂在两颗按钮的 enabled 上
                // （`updateSelectionBar()` 每趟现算），所以这里验的是那一半；
                // 可见性那一半反过来验：**两颗都不在面板里**（再并排摆着就是和菜单重复）。
                const auto enableButton = [&barButton] {
                    return barButton(InstanceFilesPage::SelectionAction::Enable);
                };
                const auto disableButton = [&barButton] {
                    return barButton(InstanceFilesPage::SelectionAction::Disable);
                };
                const auto stateSays = [&](const QString &name, QPushButton *button) {
                    return QStringLiteral("%1 在面板里=%2 可用于本次=%3")
                        .arg(name)
                        .arg(button != nullptr && button->isVisibleTo(bar) ? 1 : 0)
                        .arg(button != nullptr && button->isEnabled() ? 1 : 0);
                };
                const auto bothButtonsSaid = [&] {
                    return stateSays(QStringLiteral("启用"), enableButton()) +
                           QStringLiteral(" / ") +
                           stateSays(QStringLiteral("禁用"), disableButton());
                };
                const auto hiddenInPanel = [&] {
                    return enableButton() != nullptr && disableButton() != nullptr &&
                           !enableButton()->isVisibleTo(bar) && !disableButton()->isVisibleTo(bar);
                };
                selectOnly(modA);  // 只选那条**启用着**的
                check(bar != nullptr && bar->isVisible() && hiddenInPanel() &&
                          enableButton() != nullptr && !enableButton()->isEnabled() &&
                          disableButton() != nullptr && disableButton()->isEnabled(),
                      "★★ U10R 选中的里面**一条禁用着的都没有** ⇒ 菜单里「全部启用」灰、"
                      "「全部禁用」亮（两颗并排按钮本身收在菜单后面，不在面板里重复摆）",
                      bothButtonsSaid());
                selectOnly(modC);  // 只选那条**禁用着**的（全是禁用）
                check(hiddenInPanel() && enableButton() != nullptr &&
                          enableButton()->isEnabled() && disableButton() != nullptr &&
                          !disableButton()->isEnabled(),
                      "★★ U10R 选中的**全是禁用着的** ⇒ 菜单里「全部禁用」灰、"
                      "「全部启用」亮（同一套规则反过来）",
                      bothButtonsSaid());
                selectOnly(modB);
                if (rowOf(modC) >= 0) {
                    list->item(rowOf(modC))->setSelected(true);  // 一条启用 + 一条禁用
                }
                check(hiddenInPanel() && enableButton() != nullptr &&
                          enableButton()->isEnabled() && disableButton() != nullptr &&
                          disableButton()->isEnabled(),
                      "U10R 选中里两种都有 ⇒ 菜单两项都亮着（这一档谁都不该灰）",
                      bothButtonsSaid());
                list->clearSelection();

                // ---- ⑤ 点一下开关（改名）之后，这一行**还选着** ----
                //
                // 「点开关」在真机上是这条路：行尾那颗 `ToggleSwitch` 按下去 → 这一页的
                // `requestToggle()`（先记一笔"这是**我刚发出去的开关**"）→ `toggleRequested`
                // → MainWindow 把 `X.jar` 改成 `X.jar.disabled`，再回头喊一声
                // refreshFilePages()（见 toggleInstanceFile）。
                //
                // 这一档必须**走这一页**，不能直接 `QFile::rename` + `refresh()`：产品只把
                // "这一页刚发出去的开关"解释成改名（`m_pendingIdentityMigration`）—— 否则
                // 一条**被删掉**的文件也会把同名的 `.disabled` 那一行重新选上（见 ⑥ 那一档）。
                const auto simulateDiskToggle = [](const QString &path) {
                    const QString suffix = InstanceFilesPage::disabledSuffix();
                    const QString target = path.endsWith(suffix)
                                               ? path.left(path.size() - suffix.size())
                                               : path + suffix;
                    return QFile::rename(path, target);
                };
                const QMetaObject::Connection simConn = QObject::connect(
                    page, &InstanceFilesPage::toggleRequested, page,
                    [page, &simulateDiskToggle](const QString &path) {
                        check(simulateDiskToggle(path),
                              "U10R 前提：盘上改名必须成功（= 点一下开关）", path);
                        page->refresh();
                    });
                selectOnly(modA);
                check(list->selectedItems().size() == 1, "U10R 前提：先选中「甲」那一条");
                const QString modARenamed = modA + InstanceFilesPage::disabledSuffix();
                QAbstractButton *rowSwitch = page->toggleButtonAt(rowOf(modA));
                check(rowSwitch != nullptr, "U10R 前提：行尾那颗开关在（按得下去才谈得上这条路）");
                if (rowSwitch != nullptr) {
                    rowSwitch->click();
                }
                check(QFile::exists(modARenamed) && !QFile::exists(modA),
                      "U10R 前提：点完开关盘上真改成了「甲.jar.disabled」",
                      QFile::exists(modARenamed) ? QStringLiteral("在") : QStringLiteral("没有"));
                const int renamedRow = rowOf(modARenamed);
                check(renamedRow >= 0 && list->selectedItems().size() == 1 &&
                          list->item(renamedRow) != nullptr && list->item(renamedRow)->isSelected(),
                      "★★ U10R 改名（= 点一下开关）之后那一行**还在选中里**（账按 "
                      "`identityPath()` 对：盘上还是同一个文件，用户手里选的就是「这一条」）",
                      QStringLiteral("选中 %1 条").arg(list->selectedItems().size()));
                check(bar != nullptr && bar->isVisible() && page->selectionCountLabel() != nullptr &&
                          page->selectionCountLabel()->text() ==
                              InstanceFilesPage::selectionCountText(1),
                      "★★ U10R 那条操作条**也没有跟着消失**（唯一选中的一条掉出选择时，"
                      "整条就一起收起来了）",
                      page->selectionCountLabel() != nullptr ? page->selectionCountLabel()->text()
                                                             : QString());
                // 而账上记的是**盘上真那个名字**（发给 MainWindow 的就是它）—— 借「删除」
                // 那一颗读一眼那份账（它恒可用，而且只发信号、一个字节都不动盘）。
                QStringList account;
                const QMetaObject::Connection accConn =
                    QObject::connect(page, &InstanceFilesPage::batchDeleteRequested, page,
                                     [&account](const QStringList &paths) { account = paths; });
                if (barButton(InstanceFilesPage::SelectionAction::Delete) != nullptr) {
                    barButton(InstanceFilesPage::SelectionAction::Delete)->click();
                }
                QObject::disconnect(accConn);
                check(account == QStringList{modARenamed},
                      "★★ U10R 账上记的是**改过名之后那个名字**（`X.jar.disabled`）—— 报出去的"
                      "路径必须是盘上真那一个",
                      account.join(QStringLiteral(" | ")));
                // 再反过来（启用）：同一条规矩的另一面，同样走**真那条路**。
                QAbstractButton *rowSwitchBack = page->toggleButtonAt(rowOf(modARenamed));
                check(rowSwitchBack != nullptr, "U10R 前提：改名之后那条的行尾开关也在");
                if (rowSwitchBack != nullptr) {
                    rowSwitchBack->click();
                }
                check(QFile::exists(modA) && !QFile::exists(modARenamed),
                      "U10R 前提：再点一下开关，盘上改回「甲.jar」",
                      QFile::exists(modA) ? QStringLiteral("在") : QStringLiteral("没有"));
                check(rowOf(modA) >= 0 && list->selectedItems().size() == 1 &&
                          list->item(rowOf(modA)) != nullptr &&
                          list->item(rowOf(modA))->isSelected(),
                      "★★ U10R 改回来（启用）也一样：那一行照旧选着");
                QObject::disconnect(simConn);

                // ---- ⑥ 「同名的一对」不许被身份一起选中----
                //
                // `X.jar` 和 `X.jar.disabled` **同时在场**时两行的身份一样（`identityPath()`
                // 把后缀抹掉算）。还选择那一趟要是只按身份比，用户只点了一条、重列之后两条
                // 都选上 —— 紧接着一次「删除选中项」就把**用户从来没选过**的那一份也删了
                // （不可逆）。这一档钉的是"重列之后选中集只许不变或变小，不许自己变大"。
                const QString twinOn = rRoot + QStringLiteral("/mods/戊.jar");
                const QString twinOff = twinOn + InstanceFilesPage::disabledSuffix();
                mkfileR(twinOn);
                mkfileR(twinOff);
                page->refresh();
                check(rowOf(twinOn) >= 0 && rowOf(twinOff) >= 0,
                      "U10R 前提：同名的一对（`戊.jar` 与 `戊.jar.disabled`）两条都列出来了",
                      QString::number(list->count()));
                selectOnly(twinOn);
                check(list->selectedItems().size() == 1,
                      "U10R 前提：只点了**启用着的那一条**");
                // 再列一遍 —— 搜索框敲一个字、任一批量操作跑完之后都会走到这里。
                page->refresh();
                check(list->selectedItems().size() == 1 &&
                          list->item(rowOf(twinOn)) != nullptr &&
                          list->item(rowOf(twinOn))->isSelected() &&
                          list->item(rowOf(twinOff)) != nullptr &&
                          !list->item(rowOf(twinOff))->isSelected(),
                      "★★ U10R 同名的一对同时在场时，重列**不许把用户没点过的那一条也选上**"
                      "（身份抹掉 `.disabled` 算，只按身份还选择会把选中集悄悄放大 ⇒ 随后的"
                      "「删除选中项」多删一份）",
                      QStringLiteral("选中 %1 条").arg(list->selectedItems().size()));
            }
        }

        // ---- U10L 「服务器」那一页的"现在什么样"（：图标 / MOTD / 延迟）----
        //
        // 用户 问的「服务器列表能实际 ping 服务器并显示服务器图标和 motd 吗」。
        // **连上去问**那一半（握手、超时、拆包、图标那串 data URL）在 build/verify 的 ZH
        // 节里真连一次验完了；这一节验的是另一半 —— 问回来的东西画在哪儿：图标进哪一格、
        // MOTD 进哪一行、状态那一列写什么、那几颗按钮按下去发不发信。
        {
            showPage(dialog, QStringLiteral("server"));
            auto *tree = dialog.serverTree();
            auto *refresh = dialog.serverRefreshButton();
            auto *add = dialog.serverAddButton();
            auto *remove = dialog.serverDeleteButton();
            check(tree != nullptr && refresh != nullptr && add != nullptr && remove != nullptr &&
                      dialog.serverEditButton() != nullptr,
                  "U10L 「刷新」「编辑」「添加」「删除」四颗按钮和那张表都在");

            // ---- U10L-1 三列 + 每一行一开始就写着「还没测」 ----
            dialog.setServerList({qMakePair(QStringLiteral("我的服"),
                                            QStringLiteral("mc.example.com")),
                                  qMakePair(QStringLiteral("朋友的服"),
                                            QStringLiteral("1.2.3.4:25566")),
                                  qMakePair(QStringLiteral("挂了的服"),
                                            QStringLiteral("5.6.7.8"))},
                                 QString());
            check(tree->topLevelItemCount() == 3, "U10L 三台服务器摆了三行");
            QStringList headers;
            for (int i = 0; i < tree->columnCount(); ++i) {
                headers.append(tree->headerItem() != nullptr ? tree->headerItem()->text(i)
                                                             : QString());
            }
            check(headers == QStringList({Dlg::serverNameColumnText(), Dlg::serverAddressColumnText(),
                                          Dlg::serverStatusColumnText()}),
                  "★ U10L 多出来的是**第三列**「状态」（名称 / 地址 / 状态）", headers.join('/'));
            // 空白和"测了没通"在屏幕上是同一个样子，而这两件事对用户完全不是一回事。
            check(tree->topLevelItem(0)->text(2) == Dlg::serverUntestedText() &&
                      tree->topLevelItem(2)->text(2) == Dlg::serverUntestedText(),
                  "★ U10L 刚读出来时每一行的状态那一格写的是「还没测」（不是留一栏空白）",
                  tree->topLevelItem(0)->text(2));

            // ---- U10L-2 名字那一格挂的是那个两行式的代理（图标 + 名字 + MOTD）----
            auto *delegate = qobject_cast<VersionTreeDelegate *>(tree->itemDelegateForColumn(0));
            check(delegate != nullptr,
                  "★ U10L 第 0 列挂的是 VersionTreeDelegate（和版本树 / 下载页同一份实现）");
            check(delegate != nullptr && !delegate->leafArrow(),
                  "★ U10L 而且叶子那一层**不画**右边那个 `>` —— 服务器不是「点进去还有东西」",
                  delegate != nullptr ? QStringLiteral("leafArrow=%1").arg(delegate->leafArrow())
                                      : QStringLiteral("<null>"));
            check(delegate != nullptr && delegate->iconSize() == MainWindowUI::serverIconSize() &&
                      tree->iconSize() == QSize(MainWindowUI::serverIconSize(),
                                                MainWindowUI::serverIconSize()),
                  "★ U10L 图标那一格是 serverIconSize() 那么大（控件和代理两处得是同一个数）",
                  QStringLiteral("代理 %1、控件 %2x%3、期望 %4")
                      .arg(delegate != nullptr ? delegate->iconSize() : -1)
                      .arg(tree->iconSize().width())
                      .arg(tree->iconSize().height())
                      .arg(MainWindowUI::serverIconSize()));
            check(tree->itemDelegateForColumn(1) != delegate,
                  "U10L 地址那一列没挂它（那一列是给外人看的地址，不需要两行式的行）");

            // ---- U10L-3 状态那一格写什么（纯函数，四档）----
            {
                Dlg::ServerRowStatus untested;
                check(Dlg::serverStatusText(untested) == Dlg::serverUntestedText(),
                      "U10L 没测过 ⇒「还没测」", Dlg::serverStatusText(untested));

                Dlg::ServerRowStatus dead;
                dead.tested = true;
                dead.online = false;
                dead.error = QStringLiteral("没有回应（对面没开，或者端口不对）");
                check(Dlg::serverStatusText(dead) == dead.error,
                      "★ U10L 没通 ⇒ 直接把**它没通的那句话**摆出来（用户要的就是那个理由）",
                      Dlg::serverStatusText(dead));

                // 超时那一档**只写两个字**（要求：「超时的只写超时」）。
                // `error` 那两句仍是全的（两句都在这儿钉一遍）—— 判据是 timedOut 那一位，
                // 不是这句话的前缀：界面上**不许**拿字符串去认。
                Dlg::ServerRowStatus timedOut;
                timedOut.tested = true;
                timedOut.online = false;
                timedOut.timedOut = true;
                timedOut.error = QStringLiteral("超时（连上了，但服务器没有回答）");
                check(Dlg::serverStatusLines(timedOut) == QStringList({Dlg::serverTimeoutText()}),
                      "★ U10L 超时 ⇒ 那一格只写「超时」两个字（理由是给排查看的，不进这一格）",
                      Dlg::serverStatusLines(timedOut).join(QStringLiteral("︱")));
                Dlg::ServerRowStatus timedOutNet = timedOut;
                timedOutNet.error = QStringLiteral("超时（网络不通）");
                check(Dlg::serverStatusLines(timedOutNet) ==
                          QStringList({Dlg::serverTimeoutText()}),
                      "★ U10L 两种超时（网络不通 / 它没答话）在这一格上是同一句「超时」",
                      Dlg::serverStatusLines(timedOutNet).join(QStringLiteral("︱")));
                // 反过来那条同样要钉：**不是**超时的那些必须照旧把那句话摆出来（判据要是写成
                // "前缀是超时"之外的东西，这一条是唯一能拦住"全都缩成两个字"的断言）。
                Dlg::ServerRowStatus refused = timedOut;
                refused.timedOut = false;
                refused.error = QStringLiteral("没有回应（对面没开，或者端口不对）");
                check(Dlg::serverStatusLines(refused) == QStringList({refused.error}),
                      "U10L 不是超时的没通照旧写那句话（别把整档都缩成两个字）",
                      Dlg::serverStatusLines(refused).join(QStringLiteral("︱")));

                // 真要是空的也别留一格空白 —— 那就是"看着像没测过"。
                Dlg::ServerRowStatus blank;
                blank.tested = true;
                check(!Dlg::serverStatusText(blank).isEmpty() &&
                          Dlg::serverStatusText(blank) != Dlg::serverUntestedText(),
                      "★ U10L 通没通都没说、又没带理由时兜一句（空着和没测过长得一样）",
                      Dlg::serverStatusText(blank));

                Dlg::ServerRowStatus live;
                live.tested = true;
                live.online = true;
                live.latencyMs = 42;
                live.playersOnline = 7;
                live.playersMax = 20;
                // 「在线」那两个字 傍晚删了（要求：「把服务器那个 在线
                // 文本删了，只保留延迟和人数」）—— 通了的这一档第一行**就是数字**。
                check(Dlg::serverStatusText(live) == QStringLiteral("42 ms / 7/20 人"),
                      "★ U10L 通了 ⇒「延迟 / 人数」（没有「在线」两个字）",
                      Dlg::serverStatusText(live));

                // 服务器没说人数（-1）和"一个人都没有"（0）不是一回事：前者不许写出人数，
                // 后者要写出 0/20 —— 混起来的话"它没说"会被显示成"这服空着"。
                Dlg::ServerRowStatus noPlayers = live;
                noPlayers.playersOnline = -1;
                noPlayers.playersMax = -1;
                check(Dlg::serverStatusText(noPlayers) == QStringLiteral("42 ms"),
                      "U10L 服务器没说人数时不写那一截（不是写成 0/0 人）",
                      Dlg::serverStatusText(noPlayers));
                Dlg::ServerRowStatus zero = live;
                zero.playersOnline = 0;
                zero.playersMax = 0;
                check(Dlg::serverStatusText(zero) == QStringLiteral("42 ms / 0/0 人"),
                      "U10L 0/0 是**它说了**「一个人都没有」，照写", Dlg::serverStatusText(zero));
                // 延迟量不出（-1）那一档：人数照写，只少一行。**不许**因此冒出「在线」。
                Dlg::ServerRowStatus noLatency = live;
                noLatency.latencyMs = -1;
                check(Dlg::serverStatusLines(noLatency) == QStringList({QStringLiteral("7/20 人")}),
                      "U10L 没量到延迟时只剩人数那一行（不写「在线」占位）",
                      Dlg::serverStatusLines(noLatency).join(QStringLiteral("︱")));
                // 两个都没有 = 通了但一个数都没量到 ⇒ 兜一句「在线」（不然这一格空着，和
                // "还没测"在屏幕上是同一个样子）。生产里到不了，见 serverStatusLines 那段。
                Dlg::ServerRowStatus bare = noLatency;
                bare.playersOnline = -1;
                bare.playersMax = -1;
                check(Dlg::serverStatusLines(bare) == QStringList({QStringLiteral("在线")}),
                      "U10L 一个数都没有时才兜「在线」两个字（空着和没测过长得一样）",
                      Dlg::serverStatusLines(bare).join(QStringLiteral("︱")));
            }

            // ---- U10L-4 灌一茬结果进去：图标 / MOTD / 状态 / tooltip 各落各的格 ----
            QPixmap iconPixmap(MainWindowUI::serverIconSize(), MainWindowUI::serverIconSize());
            iconPixmap.fill(QColor(0x11, 0x88, 0x44));
            const QIcon icon(iconPixmap);
            const QString motd = QStringLiteral("欢迎来到我们的服 第二行");

            QList<Dlg::ServerRowStatus> statuses;
            Dlg::ServerRowStatus live;
            live.tested = true;
            live.online = true;
            live.latencyMs = 42;
            live.playersOnline = 7;
            live.playersMax = 20;
            live.motd = motd;
            live.icon = icon;
            statuses.append(live);
            Dlg::ServerRowStatus dead;
            dead.tested = true;
            dead.online = false;
            dead.error = QStringLiteral("没有回应（对面没开，或者端口不对）");
            statuses.append(dead);

            dialog.setServerStatuses(statuses);
            QTreeWidgetItem *liveItem = tree->topLevelItem(0);
            QTreeWidgetItem *deadItem = tree->topLevelItem(1);
            QTreeWidgetItem *untestedItem = tree->topLevelItem(2);

            check(!liveItem->icon(0).isNull() &&
                      liveItem->icon(0).pixmap(MainWindowUI::serverIconSize(),
                                               MainWindowUI::serverIconSize())
                              .toImage()
                              .pixelColor(MainWindowUI::serverIconSize() / 2,
                                          MainWindowUI::serverIconSize() / 2) ==
                          QColor(0x11, 0x88, 0x44),
                  "★ U10L 服务器图标落在 `Qt::DecorationRole` 上、而且还是**那一枚**"
                  "（代理就是读这一格画的）");
            check(deadItem->icon(0).isNull(),
                  "U10L 没通的那一台没有图标（不是留着上一次那一枚）");
            check(liveItem->data(0, MainWindowUI::rowSubTextRole()).toString() == motd,
                  "★ U10L MOTD 落在第二行小字那个角色位上（版本树放发布时间的那一格）",
                  liveItem->data(0, MainWindowUI::rowSubTextRole()).toString());
            check(liveItem->toolTip(0) == motd,
                  "U10L 名字那一格挂一句 tooltip：MOTD 装不下时是被省略号截掉的，"
                  "整行只有这一处能看全",
                  liveItem->toolTip(0));
            check(deadItem->data(0, MainWindowUI::rowSubTextRole()).toString().isEmpty() &&
                      deadItem->toolTip(0).isEmpty(),
                  "★ U10L 没通的那一台 MOTD 那一格和 tooltip 都是空的（不留别人的话）");
            // 状态那一格是**两行**（用户 ：「在线延迟和人数换到两行来」），
            // 而且通的那一档**不带「在线」两个字**（当天傍晚的要求）。
            check(liveItem->text(2) == QStringLiteral("42 ms\n7/20 人") &&
                      deadItem->text(2) == dead.error &&
                      untestedItem->text(2) == Dlg::serverUntestedText(),
                  "★ U10L 三行的状态那一格各是各的（通了是两行、没通一行、还没测一行）",
                  QStringLiteral("「%1」「%2」「%3」")
                      .arg(liveItem->text(2), deadItem->text(2), untestedItem->text(2)));
            check(liveItem->toolTip(2) == Dlg::serverStatusText(live),
                  "★ U10L 同一格还是那一句**单行**的 tooltip"
                  "（格子里拆成两行是排版，提示框里没有「两行」这回事）",
                  liveItem->toolTip(2));

            // ---- U10L-5 行数和状态数对不上时：多的丢掉、缺的回到「还没测」----
            dialog.setServerStatuses(statuses);  // 先把三行都写成有内容的样子
            QList<Dlg::ServerRowStatus> shorter;
            shorter.append(live);  // 只给第一台
            dialog.setServerStatuses(shorter);
            check(tree->topLevelItem(0)->text(2) == QStringLiteral("42 ms\n7/20 人") &&
                      tree->topLevelItem(1)->text(2) == Dlg::serverUntestedText() &&
                      tree->topLevelItem(2)->text(2) == Dlg::serverUntestedText(),
                  "★ U10L 状态比行**少**时，多出来的那几行回到「还没测」"
                  "（不是留着上一趟那几句 —— 那些话配的是上一份列表的行）",
                  QStringLiteral("「%1」「%2」「%3」")
                      .arg(tree->topLevelItem(0)->text(2), tree->topLevelItem(1)->text(2),
                           tree->topLevelItem(2)->text(2)));
            check(tree->topLevelItem(1)->data(0, MainWindowUI::rowSubTextRole()).toString().isEmpty(),
                  "U10L 而且那些行的 MOTD 那一格也一起清掉了");

            QList<Dlg::ServerRowStatus> longer = statuses;
            longer.append(live);
            longer.append(live);
            dialog.setServerStatuses(longer);
            check(tree->topLevelItemCount() == 3 &&
                      tree->topLevelItem(2)->text(2) == QStringLiteral("42 ms\n7/20 人"),
                  "★ U10L 状态比行**多**时多出来的直接丢掉（不越界、也不凭空长出一行）",
                  QStringLiteral("%1 行").arg(tree->topLevelItemCount()));

            // ---- U10L-6 换一份列表 ⇒ 上一份那些"现在什么样"一个字都不留 ----
            dialog.setServerList({qMakePair(QStringLiteral("换了一份"),
                                            QStringLiteral("mc.example.com"))},
                                 QString());
            check(tree->topLevelItemCount() == 1 &&
                      tree->topLevelItem(0)->text(2) == Dlg::serverUntestedText() &&
                      tree->topLevelItem(0)->icon(0).isNull() &&
                      tree->topLevelItem(0)->data(0, MainWindowUI::rowSubTextRole())
                          .toString()
                          .isEmpty(),
                  "★ U10L 「刷新」（重读 servers.dat）之后状态全回到「还没测」"
                  "—— 旧的那几句配的是旧的那些行");

            // ---- U10L-7 「刷新」发它那一条信号；重测那期间点不动 ----
            int refreshes = 0;
            int adds = 0;
            QObject::connect(&dialog, &Dlg::refreshServerListRequested, &dialog,
                             [&refreshes]() { ++refreshes; });
            QObject::connect(&dialog, &Dlg::addServerRequested, &dialog,
                             [&adds]() { ++adds; });

            check(refresh->text() == Dlg::serverRefreshText(), "U10L 「刷新」上写的是「刷新」",
                  refresh->text());
            refresh->click();
            check(refreshes == 1,
                  "★ U10L 点「刷新」发 refreshServerListRequested（那条路上重读 + 重测）",
                  QStringLiteral("refresh=%1").arg(refreshes));

            // 重读那一半是 MainWindow 的事、要等它回来；这一层手上还是**一行都没选中**
            // ⇒ 「删除」灰着，而「添加」照旧点得动（它不依赖选中行）。
            check(tree->currentItem() == nullptr && !remove->isEnabled() && add->isEnabled(),
                  "★ U10L 一行都没选中时：「删除」灰着、「添加」照旧点得动"
                  "（要加的那一台还没出现在表里，它没有「先选中」这一步）");
            add->click();
            check(adds == 1, "★ U10L 点「添加」发一条 addServerRequested（**不带参数**）",
                  QStringLiteral("add=%1").arg(adds));

            dialog.setServerPinging(true);
            check(refresh->text() == Dlg::serverPingingText() && !refresh->isEnabled(),
                  "★ U10L 重测期间「刷新」换成「刷新中…」而且**点不动**"
                  "（连点三下发三遍请求是用户没要的，每一遍都要等满超时）",
                  QStringLiteral("「%1」enabled=%2")
                      .arg(refresh->text())
                      .arg(refresh->isEnabled() ? 1 : 0));
            const int refreshesBefore = refreshes;
            refresh->click();
            check(refreshes == refreshesBefore, "U10L 灰着的时候点了也没反应");
            dialog.setServerPinging(false);
            check(refresh->text() == Dlg::serverRefreshText() && refresh->isEnabled(),
                  "U10L 重测完了按钮变回「刷新」、也点得动了", refresh->text());
            refresh->click();
            check(refreshes == refreshesBefore + 1, "U10L 变回来之后点得动");

            // ---- U10L-7b 那五颗各是哪一档（颜色就落在这儿，）----
            //
            // 「这个画出来是什么色」在**别处**量（`widgetStyleSheet()` 那几条选择器 ⇒
            // W12 那一段、以及主题那一节）。这一层该钉的是**这一页的哪一颗挂了哪一档**：
            // 挂错了不会报错、不会崩，只体现在"用户按下去之前心里那一句判断"上 ——
            // 「删除」要是没红，用户就分不出它和「刷新」有什么不一样。
            //
            // NOTE: 没挂属性 = `Accent`（见 `Theme::ButtonKind` 那条 NOTE：界面上一百来颗
            // 按钮绝大多数就是普通按钮，犯不着每建一颗都喊一遍）。
            {
                const auto kindOf = [](QPushButton *button) {
                    const QVariant value = button->property("wtfmlButtonKind");
                    return value.isValid() ? value.toString()
                                           : Theme::buttonKindKey(Theme::ButtonKind::Accent);
                };
                const QString neutral = Theme::buttonKindKey(Theme::ButtonKind::Neutral);
                const QString danger = Theme::buttonKindKey(Theme::ButtonKind::Danger);
                check(kindOf(refresh) == neutral && kindOf(dialog.serverEditButton()) == neutral &&
                          kindOf(add) == neutral,
                      "★ U10L 「刷新」「编辑」「添加」都是**一般性**那一档"
                      "（黑字黑边：看一眼 / 挪个地方，不是「该按的那一颗」）",
                      QStringLiteral("%1 / %2 / %3")
                          .arg(kindOf(refresh), kindOf(dialog.serverEditButton()), kindOf(add)));
                check(kindOf(remove) == danger,
                      "★★ U10L 「删除」是**不可撤销**那一档（红字红边）—— 删掉的那一台，"
                      "用户得在按下去之前就知道这件事回不来",
                      kindOf(remove));
                check(kindOf(dialog.serverJoinButton()) != kindOf(remove) &&
                          kindOf(dialog.serverJoinButton()) ==
                              Theme::buttonKindKey(Theme::ButtonKind::Accent),
                      "★★ U10L 「加入服务器」是**主题色**那一档，而且和「删除」**不是同一档**"
                      "（这两颗在屏幕上是挨着的两颗白底按钮，就靠字色和描边分 —— "
                      "同一档的话用户分不出哪一颗是「要开游戏」、哪一颗是「删掉」）",
                      QStringLiteral("加入=%1 删除=%2")
                          .arg(kindOf(dialog.serverJoinButton()), kindOf(remove)));
            }

            // ---- U10L-8 地址那一栏遮起来（悬停才给全）----
            //
            // 要求：「域名就显示首个字母+一条横杠（如 m-.h-.c- 来表示
            // mc.hyperlands.cloud），悬停在上方时才显示完整的，ipv4 的显示为 123.45.*.*，
            // ipv6 显示为 [123:123:*:*:*:*:*:*]」。
            {
                struct MaskCase {
                    const char *input;
                    const char *expected;
                    const char *what;
                };
                const MaskCase cases[] = {
                    {"mc.hyperlands.cloud", "m-.h-.c-", "域名：每段一个首字母 + 一条横杠"},
                    {"example.com", "e-.c-", "两段的域名"},
                    {"play.mc.example.com:25566", "p-.m-.e-.c-:25566",
                     "端口照抄（它不是身份，而且同一台机器上好几个服就靠它分）"},
                    {"123.45.67.89", "123.45.*.*", "IPv4：留头两段、后面打掉（用户给的样子）"},
                    {"123.45.67.89:25566", "123.45.*.*:25566", "IPv4 带端口"},
                    {"[2001:db8:1:2:3:4:5:6]", "[2001:db8:*:*:*:*:*:*]",
                     "IPv6：方括号留着、留头两段（用户给的样子）"},
                    {"[2001:db8::1]:25566", "[2001:db8::*]:25566",
                     "压缩写法的 IPv6：那个空段**留着空**（它就是 `::` 本身）—— 填成 `*` 的话"
                     "遮出来是 `[2001:db8:*:*]`，四个段、一个双冒号都没有，看着像另一个地址"},
                    {"[::1]", "[::*]", "回环那种 IPv6 也不会把自己露出来"},
                    {"  mc.example.com  ", "m-.e-.c-", "两头空白照旧去掉"},
                    // 认不出来就**原样回**：这一格只是个显示，猜出来的东西比不猜更坏。
                    {"", "", "空串回空串（不编一个假名字出来）"},
                    {"localhost", "l-", "没有点的名字也遮（留一个首字母）"},
                    {"a..b", "a-..b-", "坏地址里空的那一段保持空（不连成 a-.b- 那种假名字）"},
                };
                for (const MaskCase &item : cases) {
                    const QString got =
                        Dlg::maskedServerAddress(QString::fromUtf8(item.input));
                    check(got == QString::fromUtf8(item.expected),
                          QStringLiteral("★ U10L 遮地址：%1").arg(QString::fromUtf8(item.what)),
                          QStringLiteral("「%1」⇒「%2」，期望「%3」")
                              .arg(QString::fromUtf8(item.input), got,
                                   QString::fromUtf8(item.expected)));
                }
                check(Dlg::maskedServerAddress(QStringLiteral("mc.hyperlands.cloud"))
                          .contains(QStringLiteral("hyperlands")) == false,
                      "★★ U10L 遮过的写法里**不出现**原来那个域名 "
                      "（这一条是这一整段的要害：上面那些逐字对的是「遮成什么样」，"
                      "这一条对的是「真的没露」）",
                      Dlg::maskedServerAddress(QStringLiteral("mc.hyperlands.cloud")));

                // 表里那一格写的是遮过的、完整的那一串挂在悬停提示上（**而且那句提示还得
                // 告诉用户"右键能复制"** —— 用户 ：「这一点在悬停文本上也告知」）。
                const QString address = QStringLiteral("mc.hyperlands.cloud:25566");
                dialog.setServerList(
                    {qMakePair(QStringLiteral("我的服"), address)}, QString());
                QTreeWidgetItem *item = tree->topLevelItem(0);
                check(item->text(1) == QStringLiteral("m-.h-.c-:25566") &&
                          item->toolTip(1) ==
                              address + QLatin1Char('\n') + Dlg::serverAddressCopyHint(),
                      "★★ U10L 表里写遮过的那一串，**完整的只在悬停时**给，"
                      "并且那句悬停里写明右键能复制",
                      QStringLiteral("格「%1」提示「%2」")
                          .arg(item->text(1), item->toolTip(1)));
                // 右键要复制的就是那一串**完整的**（屏幕上那一格写的是遮过的 —— 复制一份
                // "屏幕上那样的"给用户，粘到游戏里连不上）。
                check(item->data(1, MainWindowUI::serverAddressRole()).toString() == address,
                      "★★ U10L 完整的地址另挂一份在第 1 列的 serverAddressRole 上"
                      "（右键复制的就是它，不许去拆 tooltip 那个字符串）",
                      item->data(1, MainWindowUI::serverAddressRole()).toString());

                // ---- 右键 = 发一条"复制这一串"（**不弹菜单**，要求「ip地址右键可以复制」）----
                {
                    QString copied;
                    int fired = 0;
                    const auto link = QObject::connect(
                        &dialog, &Dlg::copyServerAddressRequested,
                        [&copied, &fired](const QString &text) {
                            copied = text;
                            ++fired;
                        });

                    // 那一格的中心（viewport 坐标）—— 条目视图发的就是这一套坐标。
                    const QRect cell = tree->visualRect(tree->model()->index(0, 1));
                    check(cell.isValid() && cell.width() > 2,
                          "U10L 地址那一格量得出来", QStringLiteral("%1x%2")
                                                         .arg(cell.width()).arg(cell.height()));
                    QContextMenuEvent onAddress(QContextMenuEvent::Mouse, cell.center(),
                                                tree->viewport()->mapToGlobal(cell.center()));
                    QApplication::sendEvent(tree->viewport(), &onAddress);
                    check(fired == 1 && copied == address,
                          "★★ U10L 在**地址那一格**上按右键 ⇒ 发一条复制请求，给的是完整地址"
                          "（菜单一个都不弹）",
                          QStringLiteral("发了 %1 次、内容是「%2」").arg(fired).arg(copied));

                    // 别的格上按右键**什么都不发**：这一页上右键只对地址那一格有意义，
                    // 放行的话用户在想复制 MOTD 时会莫名其妙往剪贴板里塞一个服务器地址。
                    fired = 0;
                    copied.clear();
                    const QRect nameCell = tree->visualRect(tree->model()->index(0, 0));
                    QContextMenuEvent onName(QContextMenuEvent::Mouse, nameCell.center(),
                                             tree->viewport()->mapToGlobal(nameCell.center()));
                    QApplication::sendEvent(tree->viewport(), &onName);
                    check(fired == 0 && copied.isEmpty(),
                          "★ U10L 名字那一格上按右键一发都不发（右键只认地址那一格）",
                          QStringLiteral("发了 %1 次").arg(fired));
                    QObject::disconnect(link);
                }
            }

            // ---- U10L-9 状态那一格：两行是怎么拆的（纯函数）----
            {
                Dlg::ServerRowStatus live;
                live.tested = true;
                live.online = true;
                live.latencyMs = 42;
                live.playersOnline = 7;
                live.playersMax = 20;
                const QStringList lines = Dlg::serverStatusLines(live);
                check(lines == QStringList({QStringLiteral("42 ms"),
                                            QStringLiteral("7/20 人")}),
                      "★ U10L 通了 ⇒ 两行：第一行延迟、第二行人数（没有「在线」两个字）",
                      lines.join(QStringLiteral("︱")));
                check(Dlg::serverStatusText(live) == lines.join(QStringLiteral(" / ")),
                      "★ U10L 单行那份就是这两行用「 / 」拼起来的"
                      "（所以格子里拆成两行之后，悬停那句一个字都没变）",
                      Dlg::serverStatusText(live));

                Dlg::ServerRowStatus noPlayers = live;
                noPlayers.playersOnline = -1;
                noPlayers.playersMax = -1;
                check(Dlg::serverStatusLines(noPlayers).size() == 1,
                      "★ U10L 服务器没说人数时**只有一行**（不是空着第二行、也不是写成 0/0 人）",
                      Dlg::serverStatusLines(noPlayers).join(QStringLiteral("︱")));

                Dlg::ServerRowStatus dead;
                dead.tested = true;
                dead.online = false;
                dead.error = QStringLiteral("没有回应（对面没开，或者端口不对）");
                check(Dlg::serverStatusLines(dead) == QStringList({dead.error}),
                      "★ U10L 没通那一档只有**一行**（那句话本来就长，拆两行更难读）",
                      Dlg::serverStatusLines(dead).join(QStringLiteral("︱")));

                // 要求：「‘上次’两个字也不要」——缓存那一份和当场测出来
                // 那一份现在**逐字相同**，一个前缀都没有。
                Dlg::ServerRowStatus cached = live;
                cached.fromCache = true;
                check(Dlg::serverStatusLines(cached) == Dlg::serverStatusLines(live) &&
                          Dlg::serverStatusLines(cached) ==
                              QStringList({QStringLiteral("42 ms"), QStringLiteral("7/20 人")}),
                      "★★ U10L 缓存那一份通了 ⇒ 和当场测出来的**逐字相同**"
                      "（没有「上次：」那种过去时的前缀了）",
                      Dlg::serverStatusLines(cached).join(QStringLiteral("︱")));

                // 补，要求：「服务器那个会显示上次:没连上，也改成超时」，
                // 接着又说「‘上次’两个字也不要」。盘上那份"上次问到"的没通也带着"是不是
                // 超时"这一位（见 ServerStatusCache::Entry::timedOut），所以这一格和刚测完
                // 那一格**同一个字**。
                Dlg::ServerRowStatus cachedTimeout;
                cachedTimeout.tested = true;
                cachedTimeout.online = false;
                cachedTimeout.timedOut = true;
                cachedTimeout.fromCache = true;
                check(Dlg::serverStatusLines(cachedTimeout) ==
                          QStringList({Dlg::serverTimeoutText()}),
                      "★★ U10L 缓存里那一份「上次是超时」⇒ 写两个字「超时」"
                      "（和刚测完那一格一模一样）",
                      Dlg::serverStatusLines(cachedTimeout).join(QStringLiteral("︱")));
                // 反过来那条同样要钉：老缓存（没存这一位的那一份）读出来 timedOut 是
                // false ⇒ 那个词还是「没连上」，不是一律写「超时」。
                Dlg::ServerRowStatus cachedRefused = cachedTimeout;
                cachedRefused.timedOut = false;
                check(Dlg::serverStatusLines(cachedRefused) ==
                          QStringList({QStringLiteral("没连上")}),
                      "★ U10L 而老缓存（不知道上一次是怎么没通的）写「没连上」"
                      "—— 不是替它编一句「超时」",
                      Dlg::serverStatusLines(cachedRefused).join(QStringLiteral("︱")));
                // 最后一条：这一格上**任何时候**都不许再冒出那两个字（前缀是从这儿删的，
                // 别处又加回来就是同一句话复活）。
                check(!Dlg::serverStatusText(cachedTimeout).contains(QStringLiteral("上次")) &&
                          !Dlg::serverStatusText(cached).contains(QStringLiteral("上次")),
                      "★★ U10L 状态那一格从此不出现「上次」两个字（缓存那一份也不）",
                      Dlg::serverStatusText(cachedTimeout));
            }

            // ---- U10L-10 三栏的宽度是这么分的 ----
            //
            // 要求「三栏，让最右边（在线）那个固定靠右，保证左侧的 motd 能显示全」。
            // 默认那套是"最后一列拉伸"，多出来的宽度全给了状态那一栏 —— 左边两行 MOTD
            // 会被挤掉一大截。所以这里逐条钉住三列各自的伸缩方式。
            {
                QHeaderView *header = tree->header();
                check(header != nullptr, "U10L 那张表有表头");
                if (header != nullptr) {
                    check(!header->stretchLastSection(),
                          "★★ U10L 关掉了「最后一列拉伸」（不关的话多出来的宽度全归状态那一栏）");
                    check(header->sectionResizeMode(0) == QHeaderView::Stretch,
                          "★★ U10L 第 0 列（名字 + MOTD）**吃掉剩下的全部宽度** —— "
                          "这一条就是「保证左侧的 motd 能显示全」的落法");
                    check(header->sectionResizeMode(1) == QHeaderView::Interactive &&
                              header->sectionResizeMode(2) == QHeaderView::Interactive,
                          "U10L 地址和状态那两栏按内容定宽（地址是遮过的、本来就窄）");
                }
                check(tree->topLevelItemCount() > 0 &&
                          tree->topLevelItem(0)->textAlignment(2) ==
                              (Qt::AlignRight | Qt::AlignVCenter),
                      "★ U10L 状态那一格**靠右**（表头那一格也是，见下面那条）",
                      QStringLiteral("%1").arg(int(tree->topLevelItem(0)->textAlignment(2))));
                check(tree->headerItem() != nullptr &&
                          tree->headerItem()->textAlignment(2) ==
                              (Qt::AlignRight | Qt::AlignVCenter),
                      "★ U10L 表头的「状态」两个字也跟着靠右（不然它和底下的数字错开一个身位）");
            }

            // ---- U10L-11 两行**真的画成了两行**（量像素）----
            //
            // 上面那些只证明了"格子里那个字符串里有一个 `\\n`"。它在屏幕上会不会真换行，
            // 取决于控件开没开 wordWrap + 条目代理怎么摆 —— 不开的话那个 `\\n` 是一个
            // **看不见的字符**，两行会挤成一行，而这件事**一声不响**。
            {
                dialog.setServerList({qMakePair(QStringLiteral("我的服"),
                                                QStringLiteral("mc.example.com"))},
                                     QString());
                QList<Dlg::ServerRowStatus> rows;
                Dlg::ServerRowStatus live;
                live.tested = true;
                live.online = true;
                live.latencyMs = 42;
                live.playersOnline = 7;
                live.playersMax = 20;
                rows.append(live);
                dialog.setServerStatuses(rows);

                check(tree->wordWrap(),
                      "★ U10L 那张表开着 wordWrap（不开的话下面那条量出来会是一行）");

                const QModelIndex index = tree->model()->index(0, 2);
                const QRect cell = tree->visualRect(index);
                check(cell.isValid() && cell.width() > 4 && cell.height() > 4,
                      "U10L 状态那一格量得出来",
                      QStringLiteral("%1x%2").arg(cell.width()).arg(cell.height()));
                if (cell.isValid() && !cell.isEmpty()) {
                    // 量的是**控件自己画出来的样子**（`captureWidget` → `render()`），
                    // 不是我们自己手搓一个 QStyleOptionViewItem 去调代理 —— 后者会把
                    // "控件到底给代理喂了哪些位"这件事跳过去，而这一节要验的恰恰是那个
                    // （wordWrap 有没有传下去、对齐有没有传下去）。
                    const QImage shot = captureWidget(tree);
                    const QRect inShot = cell.translated(tree->viewport()->mapTo(tree, QPoint(0, 0)));

                    // 一行一行扫"这一行有没有墨"（字是深的、底是浅的）。
                    QVector<int> inkRows;
                    for (int y = inShot.top(); y <= inShot.bottom(); ++y) {
                        for (int x = inShot.left(); x <= inShot.right(); ++x) {
                            const QColor pixel = shot.pixelColor(x, y);
                            if (pixel.red() < 160 && pixel.green() < 160 && pixel.blue() < 160) {
                                inkRows.append(y - inShot.top());
                                break;
                            }
                        }
                    }
                    // 把连着的行并成"带"：两行字就是两条带，中间隔着一段空白。
                    QVector<QPair<int, int>> bands;
                    for (int y : inkRows) {
                        if (!bands.isEmpty() && y - bands.last().second <= 1) {
                            bands.last().second = y;
                        } else {
                            bands.append(qMakePair(y, y));
                        }
                    }
                    QStringList described;
                    for (const QPair<int, int> &band : bands) {
                        described.append(QStringLiteral("%1~%2").arg(band.first).arg(band.second));
                    }
                    check(bands.size() == 2,
                          "★★ U10L 状态那一格在屏幕上**真的是两行**（`\\n` 不是个看不见的字符）"
                          " —— 扫出来是两条墨迹带：一条「42 ms」、一条「7/20 人」",
                          QStringLiteral("%1 条带：%2")
                              .arg(bands.size())
                              .arg(described.join(QStringLiteral("、"))));
                    check(bands.size() == 2 && bands.at(0).second < bands.at(1).first,
                          "★ U10L 而且是**延迟在上、人数在下**（没有画反）",
                          described.join(QStringLiteral("、")));

                    // 靠右：最下面那条带的右端得**贴着这一格的右边缘**。
                    if (bands.size() == 2) {
                        int rightMost = -1;
                        for (int y = inShot.top() + bands.at(1).first;
                             y <= inShot.top() + bands.at(1).second; ++y) {
                            for (int x = inShot.right(); x >= inShot.left(); --x) {
                                const QColor pixel = shot.pixelColor(x, y);
                                if (pixel.red() < 160 && pixel.green() < 160 &&
                                    pixel.blue() < 160) {
                                    rightMost = qMax(rightMost, x - inShot.left());
                                    break;
                                }
                            }
                        }
                        check(rightMost >= 0 && rightMost >= cell.width() - 8,
                              "★ U10L 而且那两行**贴着右边**（靠右对齐真的生效了）",
                              QStringLiteral("最右那个墨点在 x=%1，这一格宽 %2")
                                  .arg(rightMost)
                                  .arg(cell.width()));
                    }
                }
            }
        }
        // ---- U10S 「编辑」/「加入服务器」/「加入选中的世界」那三颗----
        //
        // 要求：「在服务器列表那里做一个编辑地址和名称的功能，然后世界和
        // 服务器都做一个临时占位（加入选中的世界（仅限有这个参数的版本）/服务器）先试试看
        // 能不能生效」。
        //
        // 这一层的活是「**哪一颗在、写着什么、没选中时是什么样子、点下去报的是哪一条**」。
        // 「这个版本认不认那两条 `--quickPlay*` 参数」由 MainWindow 现算
        // （`instanceSupportsQuickPlay()`），递进来的是一句**空串** —— 那是"整颗收起来"的
        // 意思（判据层本身在 verify 的 ZL 节、真读写 `servers.dat` 那条路在 mw_check 的
        // T20.8 上）。
        {
            // ---- ① 那一页现在叫「结构/原理图」----
            const QStringList shortcuts = Dlg::shortcutTexts();
            const QStringList optionalTexts = Dlg::optionalPageTexts();
            check(!shortcuts.contains(QStringLiteral("结构/原理图")),
                  "★ U10S 「结构/原理图」不再在快捷方式那一串里重复摆一遍",
                  shortcuts.join(QStringLiteral(" / ")));
            check(optionalTexts.size() > 6 &&
                      optionalTexts.at(6) == QStringLiteral("结构/原理图"),
                  "★ U10S 勾选表里那条也是同一个名字（两处不许各叫一个）",
                  optionalTexts.size() > 6 ? optionalTexts.at(6) : QStringLiteral("<不够长>"));
            // 改的是**叫法**，不是身份：键没动 ⇒ 用户盘上那份 `settingsPages` 不会因此失效
            // （认不出键的会被丢掉，"勾掉的页"就悄悄回来了）。
            check(Dlg::optionalPageKeys().size() > 6 &&
                      Dlg::optionalPageKeys().at(6) == QStringLiteral("schematic"),
                  "★ U10S 键还是 `schematic`（改的只是叫什么）");

            // ---- ② 服务器那一页：一颗「编辑」、一颗「加入服务器」----
            showPage(dialog, QStringLiteral("server"));
            QTreeWidget *tree = dialog.serverTree();
            QPushButton *edit = dialog.serverEditButton();
            QPushButton *joinServer = dialog.serverJoinButton();
            check(tree != nullptr && edit != nullptr && joinServer != nullptr,
                  "★ U10S 服务器那一页上「编辑」和「加入服务器」两颗都建出来了");
            if (tree != nullptr && edit != nullptr && joinServer != nullptr) {
                check(edit->objectName() == Dlg::serverEditButtonName() &&
                          edit->text() == Dlg::serverEditText(),
                      "★ U10S 「编辑」挂的是唯一出处那个 objectName 和那几个字",
                      QStringLiteral("%1「%2」").arg(edit->objectName(), edit->text()));
                check(joinServer->objectName() == Dlg::serverJoinButtonName(),
                      "★ U10S 「加入服务器」也挂上了唯一出处那个 objectName",
                      joinServer->objectName());
                // 初值：`setServerJoinText()` 还没被喊过 ⇒ 藏着，而且**身上一个字都没有**
                // （那几个字和它一起递进来 —— 一颗"有字但发不出去"的按钮比它不在更坏；
                // 不支持的版本上 MainWindow 递进来的就是空串）。
                check(joinServer->isHidden() && joinServer->text().isEmpty(),
                      "★ U10S 没接过那句话的时候「加入服务器」藏着、身上也没字",
                      QStringLiteral("hidden=%1「%2」")
                          .arg(joinServer->isHidden() ? 1 : 0).arg(joinServer->text()));

                // 自己摆一份确定的表：一条有名字、一条名字是**空串**（屏幕上写成「（无名）」）。
                dialog.setServerList({qMakePair(QStringLiteral("有名字的"),
                                                QStringLiteral("one.example.com")),
                                      qMakePair(QString(), QStringLiteral("two.example.com"))},
                                     QString());
                check(tree->topLevelItemCount() == 2, "U10S 前提：两行摆好了",
                      QStringLiteral("%1 行").arg(tree->topLevelItemCount()));
                check(tree->topLevelItem(1)->text(0) == Dlg::unnamedMark(),
                      "★ U10S 没名字的那一条**屏幕上**写的是「（无名）」"
                      "（而下面那条要说的是：发出去的不是这三个字）",
                      tree->topLevelItem(1)->text(0));

                QString sentName;
                QString sentAddress;
                int editSent = 0;
                int joinSent = 0;
                QObject::connect(&dialog, &Dlg::editServerRequested, &dialog,
                                 [&](const QString &name, const QString &address) {
                                     sentName = name;
                                     sentAddress = address;
                                     ++editSent;
                                 });
                QObject::connect(&dialog, &Dlg::joinServerRequested, &dialog,
                                 [&](const QString &, const QString &address) {
                                     sentAddress = address;
                                     ++joinSent;
                                 });

                // 一条都没选中 ⇒ 两颗都灰着，点了什么都不发（"按钮那一道闸"之外信号里还有
                // 第二道 —— 灰掉只是屏幕上说得清，不是唯一的防线）。
                check(!edit->isEnabled() && !joinServer->isEnabled(),
                      "★ U10S 一条都没选中时那两颗都是灰的");
                check(edit->toolTip() == Dlg::serverSelectFirstHint(),
                      "★ U10S 灰着的时候提示换成了「先选中一行」"
                      "（灰按钮上写着「改它的名字」用户只会以为自己点错了地方）",
                      edit->toolTip());
                edit->click();
                joinServer->click();
                check(editSent == 0 && joinSent == 0,
                      "★ U10S 灰着的时候点它们一发都不发",
                      QStringLiteral("编辑 %1 次、加入 %2 次").arg(editSent).arg(joinSent));

                // 「加入服务器」接上那句文字之后：有名的那一条发的是**原名 + 完整地址**
                // （第 1 栏里写的是遮过的 `o-.e-.c-`，发出去的那一串不许是它）。
                dialog.setServerJoinText(Dlg::serverJoinText(), Dlg::serverJoinTip());
                check(!joinServer->isHidden() &&
                          joinServer->text() == Dlg::serverJoinText(),
                      "★ U10S 接过那句话之后「加入服务器」露出来了、字也挂上了",
                      QStringLiteral("hidden=%1「%2」")
                          .arg(joinServer->isHidden() ? 1 : 0).arg(joinServer->text()));
                check(tree->topLevelItem(0)->text(1) == QStringLiteral("o-.e-.c-"),
                      "U10S 前提：地址那一栏在屏幕上确实是**遮过**的",
                      tree->topLevelItem(0)->text(1));

                tree->setCurrentItem(tree->topLevelItem(0));
                check(edit->isEnabled() && joinServer->isEnabled(),
                      "★ U10S 选中一行之后两颗都点得动了");
                check(edit->toolTip() == Dlg::serverEditTip(),
                      "★ U10S 有选中时提示换回本来那句");
                edit->click();
                check(editSent == 1 && sentName == QStringLiteral("有名字的") &&
                          sentAddress == QStringLiteral("one.example.com"),
                      "★★ U10S 「编辑」发出去的是**原名 + 完整地址**"
                      "（地址不是第 1 栏里遮过的那一串 —— 拿着遮过的去写盘就把用户的"
                      "服务器改成了一台不存在的机器）",
                      QStringLiteral("「%1」/「%2」").arg(sentName, sentAddress));

                // 没名字的那一条：屏幕上「（无名）」，发出去的必须是**空串**。
                tree->setCurrentItem(tree->topLevelItem(1));
                sentName = QStringLiteral("<没发>");
                sentAddress = QStringLiteral("<没发>");
                edit->click();
                check(sentName.isEmpty() && sentAddress == QStringLiteral("two.example.com"),
                      "★★ U10S 名字是空的那一条：屏幕上写「（无名）」，发出去的是**空串**"
                      "（发成屏幕上那三个字的话，用户点一下确定就真多出一台叫「（无名）」的"
                      "服务器）",
                      QStringLiteral("屏幕上「%1」，发出去「%2」")
                          .arg(tree->topLevelItem(1)->text(0), sentName));

                joinServer->click();
                check(joinSent == 1 && sentAddress == QStringLiteral("two.example.com"),
                      "★ U10S 点「加入服务器」带的是这一行的**完整地址**",
                      QStringLiteral("发了 %1 次、「%2」").arg(joinSent).arg(sentAddress));

                // 空串 = 这个版本不认那条参数 ⇒ 收起来（MainWindow 走的就是这一句）。
                joinServer->setEnabled(true);
                dialog.setServerJoinText(QString(), QString());
                check(joinServer->isHidden(),
                      "★ U10S 递空串进来 ⇒ 「加入服务器」当场收起来");
            }

            // ---- ③ 存档那一页顶上那颗「加入选中的世界」----
            //
            // 和服务器那一颗同一套规矩（露不露 / 选中才点得动 / 点下去报的是哪一条），
            // 只是判据换成 `--quickPlaySingleplayer`、报出去的是一条**存档路径**。
            {
                QTemporaryDir worlds;
                const QString saveRoot = worlds.path();
                QDir().mkpath(saveRoot + QStringLiteral("/一个世界"));
                mkfile(saveRoot + QStringLiteral("/一个世界/level.dat"));
                QHash<QString, QString> saveDirs;
                saveDirs.insert(QStringLiteral("save"), saveRoot);
                Dlg saveDialog(instanceDisplayName, instanceDirName, Dlg::Override(), global,
                               QStringList(), true);
                saveDialog.setInstanceDir(saveRoot);
                saveDialog.setFileDirs(saveDirs);
                saveDialog.resize(900, 520);
                showPage(saveDialog, QStringLiteral("save"));

                InstanceFilesPage *saves = saveDialog.filesPage(QStringLiteral("save"));
                QPushButton *joinWorld = saves != nullptr ? saves->joinButton() : nullptr;
                QListWidget *saveList = saves != nullptr ? saves->list() : nullptr;
                check(saves != nullptr && joinWorld != nullptr,
                      "★ U10S 存档那一页拿得到、顶上那颗也建出来了");
                if (saves != nullptr && joinWorld != nullptr && saveList != nullptr) {
                    check(joinWorld->objectName() == InstanceFilesPage::joinButtonName(),
                          "★ U10S 那颗挂的是唯一出处里的 objectName",
                          joinWorld->objectName());
                    // 没接文字 ⇒ 藏着（露不露只看这一句）。身上那个字是存档页的固定叫法
                    // 「加入世界」—— `setWorldLayout()` 在搭这一页时就挂好了，不是"接文字"
                    // 那一路写进去的（所以它虽然写着字，用户也看不到；这一点和服务器那一颗
                    // 的判据不同，别拿"身上有没有字"当可见性）。
                    check(joinWorld->isHidden() &&
                              joinWorld->text() == QStringLiteral("加入世界"),
                          "★ U10S 没接文字时它藏着（身上挂着存档页那档的固定叫法「加入世界」）",
                          QStringLiteral("hidden=%1「%2」")
                              .arg(joinWorld->isHidden() ? 1 : 0).arg(joinWorld->text()));

                    // 别的几页上它**也是建着的**（同一个控件），但一定藏着 —— 一颗「加入
                    // 选中的世界」出现在模组页上，点下去是拿一个 .jar 的名字去开存档。
                    for (const QString &key :
                         {QStringLiteral("mod"), QStringLiteral("resourcepack"),
                          QStringLiteral("datapack"), QStringLiteral("shader"),
                          QStringLiteral("schematic")}) {
                        InstanceFilesPage *other = saveDialog.filesPage(key);
                        check(other == nullptr || other->joinButton() == nullptr ||
                                  other->joinButton()->isHidden(),
                              QStringLiteral("★ U10S 「%1」那一页上没有那颗「加入选中的世界」")
                                  .arg(key));
                    }

                    saves->setJoinText(InstanceFilesPage::joinText(),
                                       InstanceFilesPage::joinTip());
                    // 存档页那一档的字是**固定的「加入世界」**（`setWorldLayout()` 定的，
                    // 见上面那条）：`setJoinText()` 递进来的那句只决定"露不露"和悬停提示，
                    // 不换按钮上的字（服务器那一页才按递进来的文字写）。
                    check(!joinWorld->isHidden() &&
                              joinWorld->text() == QStringLiteral("加入世界"),
                          "★ U10S 递了文字之后它露出来（这个版本认 `--quickPlaySingleplayer`）",
                          QStringLiteral("hidden=%1「%2」")
                              .arg(joinWorld->isHidden() ? 1 : 0).arg(joinWorld->text()));
                    check(saveList->count() == 1 && !joinWorld->isEnabled(),
                          "★ U10S 一行都没选中时它是灰的",
                          QStringLiteral("%1 行 / enabled=%2")
                              .arg(saveList->count())
                              .arg(joinWorld->isEnabled() ? 1 : 0));
                    check(joinWorld->toolTip() == InstanceFilesPage::joinSelectFirstHint(),
                          "★ U10S 灰着时提示说的是「先选中一个世界」", joinWorld->toolTip());

                    QString sentPath;
                    int joinCalls = 0;
                    const QMetaObject::Connection link =
                        QObject::connect(saves, &InstanceFilesPage::joinRequested, saves,
                                         [&](const QString &path) {
                                             sentPath = path;
                                             ++joinCalls;
                                         });
                    joinWorld->click();
                    check(joinCalls == 0, "★ U10S 灰着的时候点了什么都不发");

                    if (saveList->count() == 1) {
                        saveList->setCurrentRow(0);
                        // 选中那一发是异步排过来的（`itemSelectionChanged`），转一圈再量。
                        QApplication::processEvents();
                        check(joinWorld->isEnabled(),
                              "★ U10S 选中一个世界之后它点得动了");
                        check(joinWorld->toolTip() == InstanceFilesPage::joinTip(),
                              "★ U10S 有选中时提示换回本来那句");
                        joinWorld->click();
                        check(joinCalls == 1 &&
                                  QFileInfo(sentPath).fileName() == QStringLiteral("一个世界"),
                              "★ U10S 点它带出去的是**这个世界那一条路径**"
                              "（MainWindow 从路径末尾那个目录名当 quick play 的参数）",
                              QStringLiteral("发了 %1 次、「%2」").arg(joinCalls).arg(sentPath));
                    }

                    saves->setJoinText(QString(), QString());
                    check(joinWorld->isHidden(),
                          "★ U10S 递空串进来 ⇒ 当场收起来（不支持的版本上就是这个样子）");
                    QObject::disconnect(link);
                }
            }
        }

        // ---------- U10M 拨一下启用/禁用之后，列表**不许跳回顶端**----------
        //
        // 要求：「选中了模组并修改了启用/禁用状态以后整个表会弹回顶端，修掉」。
        // 根子是那两个"重列"的地方都是 `m_list->clear()`（`refresh()` 一次、
        // `rebuildRows()` 再一次）—— 那一下把滚动条连人一起送回了 0，用户翻了半天的那
        // 一屏就没了。修法见 `InstanceFilesPage::rememberScrollAnchor()`（记"顶上那一条
        // 是谁 + 它露了多少"，重列完再按身份把它摆回去）。
        //
        // 这一节量的就是那件事，分四层：这个目录真的撑出了滚动条 → 滚动条的值没掉回 0 →
        // 顶上还是原来那一条 → 用户选中的那一条**没有被踢出选择**（改名之后按**身份路径**
        // 认得回来，见 `InstanceFilesPage` 文件头那一段）。
        {
            QTemporaryDir rows;
            const QString rowsRoot = rows.path();
            const QString manyMods = rowsRoot + QStringLiteral("/mods");
            // 六十条：不够一屏的话"跳回顶端"这件事在屏幕上根本看不出来。
            for (int i = 0; i < 60; ++i) {
                mkfile(manyMods +
                       QStringLiteral("/mod-%1.jar").arg(i, 2, 10, QLatin1Char('0')));
            }
            QHash<QString, QString> rowsDirs;
            rowsDirs.insert(QStringLiteral("mod"), manyMods);
            Dlg rowsDialog(instanceDisplayName, instanceDirName, Dlg::Override(), global,
                           QStringList(), true);
            rowsDialog.setInstanceDir(rowsRoot);
            rowsDialog.setFileDirs(rowsDirs);
            rowsDialog.resize(900, 520);
            rowsDialog.show();
            showPage(rowsDialog, QStringLiteral("mod"));

            InstanceFilesPage *many = rowsDialog.filesPage(QStringLiteral("mod"));
            QListWidget *rowsList = many != nullptr ? many->list() : nullptr;
            QScrollBar *bar = rowsList != nullptr ? rowsList->verticalScrollBar() : nullptr;
            check(rowsList != nullptr && bar != nullptr && rowsList->count() == 60,
                  "U10M 前提：摆好六十条（够撑出滚动条）",
                  rowsList != nullptr ? QStringLiteral("%1 条").arg(rowsList->count())
                                      : QStringLiteral("<没这一页>"));
            if (rowsList != nullptr && bar != nullptr && rowsList->count() == 60) {
                QApplication::processEvents();
                check(bar->maximum() > 0,
                      "★ U10M 前提：这一列**真的滚得动**（不然下面几条量的是个死东西）",
                      QStringLiteral("0..%1").arg(bar->maximum()));

                // 顶上看不见第一条的那个位置（中间偏后）—— "跳回顶端"才看得出来。
                bar->setValue(bar->maximum() / 2);
                QApplication::processEvents();
                const int beforeValue = bar->value();
                // 「现在顶上看不见的那一条是谁」——锚点记的就是它，判据也就照它写。
                const auto topIdentity = [rowsList]() {
                    for (int row = 0; row < rowsList->count(); ++row) {
                        QListWidgetItem *item = rowsList->item(row);
                        if (item != nullptr && rowsList->visualItemRect(item).bottom() > 0) {
                            return InstanceFilesPage::identityPath(
                                item->data(InstanceFilesPage::pathRole()).toString());
                        }
                    }
                    return QString();
                };
                const QString topBefore = topIdentity();
                check(beforeValue > 0 && !topBefore.isEmpty(),
                      "U10M 前提：这一列已经滚到中间了（顶上那条看得见、第一条看不见）",
                      QStringLiteral("value=%1 顶上=%2").arg(beforeValue).arg(topBefore));

                // 用户手里选着一条 —— 就是马上要拨开关的那一条（第 40 条，在第 27 行
                // 上下那一片里，离顶端足够远）。
                const QString target = manyMods + QStringLiteral("/mod-40.jar");
                int targetRow = -1;
                for (int row = 0; row < rowsList->count(); ++row) {
                    if (rowsList->item(row)->data(InstanceFilesPage::pathRole()).toString() ==
                        target) {
                        targetRow = row;
                    }
                }
                check(targetRow >= 0, "U10M 前提：要拨的那一条（mod-40.jar）在表里",
                      QStringLiteral("第 %1 行").arg(targetRow));
                if (targetRow >= 0) {
                    rowsList->item(targetRow)->setSelected(true);
                    QApplication::processEvents();

                    // 拨开关那件事**在盘上就是改个名**（`X.jar` → `X.jar.disabled`），
                    // 然后回头喊 `refresh()` —— 产品那一路（见 InstanceSettingsDialog /
                    // MainWindow::toggleInstanceFile）一字不差地这么走。
                    //
                    // 和 U10R ⑤ 同一个理由 —— 这一发必须**从这一页自己
                    // 走出去**（行尾开关 → `requestToggle()` 先记一笔"刚发出去的开关"），
                    // 不能绕开它手工改名。绕开的那一路产品里根本不存在，而"改名之后
                    // 还选着"那一笔账正是 `requestToggle()` 记下的（删除不许被当成改名，
                    // 见 `m_pendingIdentityMigration`）。
                    const QString renamed = target + QStringLiteral(".disabled");
                    const QMetaObject::Connection simConn = QObject::connect(
                        many, &InstanceFilesPage::toggleRequested, many,
                        [many](const QString &path) {
                            const QString suffix = InstanceFilesPage::disabledSuffix();
                            const QString dest =
                                path.endsWith(suffix)
                                    ? path.left(path.size() - suffix.size())
                                    : path + suffix;
                            check(QFile::rename(path, dest), "U10M 前提：盘上改好名了", path);
                            many->refresh();
                        });
                    QAbstractButton *rowSwitch = many->toggleButtonAt(targetRow);
                    check(rowSwitch != nullptr, "U10M 前提：那一行行尾的开关在");
                    if (rowSwitch != nullptr) {
                        rowSwitch->click();
                    }
                    // 还原锚点那一发排在 0 毫秒的定时器里（见 rebuildRows() 末尾那段）。
                    QEventLoop settle;
                    QTimer::singleShot(50, &settle, &QEventLoop::quit);
                    settle.exec();
                    QApplication::processEvents();

                    check(rowsList->count() == 60,
                          "U10M 拨完开关之后那一条还在表里（六十条一条不少）",
                          QStringLiteral("%1 条").arg(rowsList->count()));
                    check(bar->value() == beforeValue,
                          "★★ U10M 滚动条**停在原地**（不是被那一发 clear() 送回 0）",
                          QStringLiteral("拨之前 %1 → 拨之后 %2（0 就是用户报的那个毛病）")
                              .arg(beforeValue)
                              .arg(bar->value()));
                    check(topIdentity() == topBefore,
                          "★★ U10M 顶上那一条**还是原来那一条**（不是整条列表从头再来一遍）",
                          QStringLiteral("%1 → %2").arg(topBefore, topIdentity()));
                    // 改名之后按**身份路径**认得回来 ⇒ 那一条还选着（用户的手还在上面）。
                    int stillSelected = -1;
                    for (int row = 0; row < rowsList->count(); ++row) {
                        if (InstanceFilesPage::identityPath(rowsList->item(row)
                                                                ->data(InstanceFilesPage::
                                                                           pathRole())
                                                                .toString()) ==
                                InstanceFilesPage::identityPath(target) &&
                            rowsList->item(row)->isSelected()) {
                            stillSelected = row;
                        }
                    }
                    check(stillSelected >= 0,
                          "★★ U10M 而且**拨过开关的那一条还选着**（盘上还是同一个文件，"
                          "只是名字尾巴上多了 `.disabled`）",
                          QStringLiteral("选中的是第 %1 行").arg(stillSelected));
                    // 名字还是原来那个（`.disabled` 不在界面上露出来 —— 它是个记号，
                    // 不是文件名的一部分），开关也翻过去了。
                    QListWidgetItem *toggled = rowsList->item(targetRow);
                    check(toggled != nullptr &&
                              toggled->data(InstanceFilesPage::pathRole()).toString() == renamed,
                          "U10M 那一行的路径换成了盘上那个新名字",
                          toggled != nullptr
                              ? toggled->data(InstanceFilesPage::pathRole()).toString()
                              : QString());
                    QObject::disconnect(simConn);
                }

                // ---- U10M-2 从详情页退回来：位置**跨过那一页本身**活下来----
                //
                // 要求：「点进模组详情以后退出来保留刚才的滚动位置」。这一趟和上面那条
                // 不是一回事：实例设置这个对话框是**每次现开的栈上对象**
                // （`MainWindow::openInstanceSettingsNow()`），那一页整套是重建出来的 ——
                // 页面自己记着的那一份位置跟着一起没了。修法是把那一对数字提到页面**外面**
                // 存（`InstanceFilesPage::ScrollPosition`）：离开那一刻量下来
                // （`fileResourceRequested` 那一发），回来时在 `showPage()` **之前**塞回去
                // （`MainWindow::m_fileScrollReturn` → `setScrollPosition()`）。
                //
                // 这里仿的正是那一趟：**另开一个对话框**（= 另一批 InstanceFilesPage），把
                // 老那一页现量出来的位置塞给新的那一页，再看它列完之后停在哪儿。
                {
                    bar->setValue(bar->maximum() / 2);
                    QApplication::processEvents();
                    const InstanceFilesPage::ScrollPosition carried = many->scrollPosition();
                    check(!carried.identity.isEmpty() && bar->value() > 0,
                          "U10M-2 前提：离开那一刻量得出一份位置（最上面那一行是谁）",
                          QStringLiteral("顶上是 %1（value=%2）").arg(carried.identity).arg(bar->value()));

                    Dlg returnDialog(instanceDisplayName, instanceDirName, Dlg::Override(), global,
                                     QStringList(), true);
                    returnDialog.setInstanceDir(rowsRoot);
                    returnDialog.setFileDirs(rowsDirs);
                    returnDialog.resize(900, 520);
                    returnDialog.show();
                    InstanceFilesPage *reopened = returnDialog.filesPage(QStringLiteral("mod"));
                    check(reopened != nullptr && reopened->scrollPosition().identity.isEmpty(),
                          "U10M-2 刚开出来那一页还没列过盘 ⇒ 它**自己**那份位置是空的"
                          "（所以下面塞进去的那一份才作数）");
                    if (reopened != nullptr) {
                        // 塞进去的时机必须是**列盘之前**（产品那一句就摆在 `showPage()`
                        // 前面）—— 摆到后面会被 `rememberScrollAnchor()` 在第一次
                        // `refresh()` 里现量一份（空表上它是空操作，可位置已经被换成
                        // 新量的那一份）盖掉。
                        reopened->setScrollPosition(carried);
                        showPage(returnDialog, QStringLiteral("mod"));
                        QEventLoop settleBack;
                        QTimer::singleShot(50, &settleBack, &QEventLoop::quit);
                        settleBack.exec();
                        QApplication::processEvents();

                        QListWidget *backList = reopened->list();
                        const auto topOf = [backList]() {
                            for (int row = 0; row < backList->count(); ++row) {
                                QListWidgetItem *item = backList->item(row);
                                if (item != nullptr && backList->visualItemRect(item).bottom() > 0) {
                                    return InstanceFilesPage::identityPath(
                                        item->data(InstanceFilesPage::pathRole()).toString());
                                }
                            }
                            return QString();
                        };
                        check(backList->count() == 60 && topOf() == carried.identity &&
                                  backList->verticalScrollBar()->value() > 0,
                              "★★ U10M-2 退回来之后**顶上还是离开前那一条**（不是回到顶端）—— "
                              "换了一个页面对象也认得回来，靠的是「那一行是谁」，不是「滚动条"
                              "停在几」",
                              QStringLiteral("顶上 %1（该 %2）")
                                  .arg(topOf(), carried.identity));
                    }
                }
            }
        }
    }
        // ---- U10T 「结构/原理图」顶上那两排「来源」----
        //
        // 要求：「那就像版本树一样根据存档来列，一个存档来一个下拉框，存档类来一个
        // 总下拉，原理图文件夹和存档类并列，没有结构的存档不显示在列表里面，没有装投影图
        // 则不显示投影图下拉」；摆法另有一句钉死：「横着并排，即**上面一排是存档的，下面
        // 一排是投影的**」。
        //
        // 为什么是**两个**来源：结构方块存下来的那些 `.nbt` **跟着存档走**
        // （`saves/<存档>/generated/minecraft/structures/`），而投影模组读的 `.litematic`
        // 在这个实例自己的文件夹里（`schematics/`）。上面那几排说的是"这一页正在看哪几个
        // 目录"，下面那份列表是这几个目录**合起来**的东西（不是"选中谁就只列谁"）。
        //
        // 这一层只量「摆几排、排头写着什么、按选中的那些目录列出什么」。**哪些存档真
        // 有结构 / 这个实例装没装投影模组，是 MainWindow 算好了递进来的**
        // （`structureSourcesFor()` —— 那两句判据在 ui/ 这一层说不出口）："没有结构的
        // 存档根本不进名单"、"没装投影就不摆投影那一排"都在那里。这里量的最后一档是：
        // 调用方递空名单 ⇒ 一个框都不摆，退回到别的七页那种样子。
        {
            QTemporaryDir structureScratch;
            const QString sRoot = structureScratch.path();
            const auto mkfileT = [](const QString &path) {
                QDir().mkpath(QFileInfo(path).absolutePath());
                QFile file(path);
                if (file.open(QIODevice::WriteOnly)) {
                    file.write(QByteArray(4096, 't'));
                }
            };
            const QString sStructuresA =
                sRoot + QStringLiteral("/saves/存档甲/generated/minecraft/structures");
            const QString sStructuresB =
                sRoot + QStringLiteral("/saves/存档乙/generated/minecraft/structures");
            const QString sSchematics = sRoot + QStringLiteral("/schematics");
            mkfileT(sStructuresA + QStringLiteral("/房子.nbt"));
            mkfileT(sStructuresB + QStringLiteral("/桥.nbt"));
            mkfileT(sSchematics + QStringLiteral("/大厅.litematic"));

            QHash<QString, QString> sDirs;
            sDirs.insert(QStringLiteral("schematic"), sSchematics);
            Dlg structureDlg(instanceDisplayName, instanceDirName, Dlg::Override(), global,
                             QStringList(), true);
            structureDlg.setInstanceDir(sRoot);
            structureDlg.setFileDirs(sDirs);
            structureDlg.resize(900, 520);
            structureDlg.showPage(QStringLiteral("schematic"));
            structureDlg.show();
            InstanceFilesPage *structurePage =
                structureDlg.filesPage(QStringLiteral("schematic"));
            check(structurePage != nullptr && structurePage->list() != nullptr,
                  "U10T 前提：找得到「结构/原理图」那一页");
            if (structurePage != nullptr && structurePage->list() != nullptr) {
                // 这一页上"现在列着哪几条" —— 按**路径**报（屏幕上那个名字是二次加工过
                // 的：禁用过的会在尾巴上多长出一截记号）。
                const auto listed = [structurePage]() {
                    QStringList out;
                    for (int row = 0; row < structurePage->list()->count(); ++row) {
                        QListWidgetItem *item = structurePage->list()->item(row);
                        if (item != nullptr) {
                            out.append(item->data(InstanceFilesPage::pathRole()).toString());
                        }
                    }
                    return out;
                };
                const auto hasFile = [](const QStringList &paths, const QString &name) {
                    for (const QString &path : paths) {
                        if (path == name) {
                            return true;
                        }
                    }
                    return false;
                };

                // ---- ① 没人摆过「来源」：这一页保持原样 ----
                check(structurePage->structureSourceCombos().isEmpty(),
                      "★ U10T 没摆过「来源」的时候那一页上一个下拉框都没有（别的七页走的"
                      "就是这条早退路 —— 它们谁也走不到那两排上去）");
                check(listed().size() == 1 &&
                          hasFile(listed(), sSchematics + QStringLiteral("/大厅.litematic")),
                      "★ U10T 那时候列的还是 setDirectory() 给的那一个目录",
                      listed().join(QStringLiteral(" / ")));
                InstanceFilesPage *siblingPage = structureDlg.filesPage(QStringLiteral("mod"));
                check(siblingPage != nullptr && siblingPage->structureSourceCombos().isEmpty(),
                      "★ U10T 别的页（模组）上也没有那两排 —— 这一套只给「结构/原理图」用");

                // ---- ② 摆上两排：**上面存档、下面投影** ----
                InstanceFilesPage::StructureSource sWorlds;
                sWorlds.title = InstanceFilesPage::structureWorldTitle();
                sWorlds.labels << QStringLiteral("存档甲") << QStringLiteral("存档乙");
                sWorlds.paths << sStructuresA << sStructuresB;
                InstanceFilesPage::StructureSource sProjections;
                sProjections.title = InstanceFilesPage::structureSchematicTitle();
                sProjections.labels << InstanceFilesPage::structureSchematicText();
                sProjections.paths << sSchematics;
                structurePage->setStructureSources({sWorlds, sProjections});

                const QList<QComboBox *> combos = structurePage->structureSourceCombos();
                check(combos.size() == 2,
                      "★ U10T 摆了两排就是两个下拉框（不是一个，也不是四颗按钮）",
                      QStringLiteral("%1 个").arg(combos.size()));
                if (combos.size() == 2) {
                    for (int row = 0; row < 2; ++row) {
                        check(combos.at(row)->objectName() ==
                                  InstanceFilesPage::structureComboName(row),
                              "U10T 两个下拉框各挂一个唯一出处的 objectName（验证工程不解析"
                              "文字）",
                              combos.at(row)->objectName());
                    }
                    auto *title0 = structurePage->findChild<QLabel *>(
                        InstanceFilesPage::structureTitleName(0));
                    auto *title1 = structurePage->findChild<QLabel *>(
                        InstanceFilesPage::structureTitleName(1));
                    const QString titleText0 =
                        title0 != nullptr ? title0->text() : QStringLiteral("<没有>");
                    const QString titleText1 =
                        title1 != nullptr ? title1->text() : QStringLiteral("<没有>");
                    check(titleText0 == InstanceFilesPage::structureWorldTitle() &&
                              titleText1 == InstanceFilesPage::structureSchematicTitle(),
                          "★★ U10T **上面那一排是存档的、下面那一排是投影的**（用户点名的那"
                          "句话；两个排头的字都走唯一出处，不在这儿另抄一遍）",
                          QStringLiteral("「%1」「%2」").arg(titleText0, titleText1));
                    check(combos.at(0)->count() == 2 &&
                              combos.at(0)->itemText(0) == QStringLiteral("存档甲") &&
                              combos.at(0)->itemText(1) == QStringLiteral("存档乙"),
                          "★ U10T 存档那一排就是递进来的那几个（**没有结构的存档根本不在"
                          "名单里** —— 那一句是 MainWindow 筛的）",
                          QStringLiteral("%1 / %2").arg(combos.at(0)->itemText(0),
                                                       combos.at(0)->itemText(1)));
                    check(combos.at(1)->count() == 1 &&
                              combos.at(1)->itemText(0) ==
                                  InstanceFilesPage::structureSchematicText(),
                          "★ U10T 投影那一排就是那个文件夹本身（今天只有一个投影来源）",
                          combos.at(1)->itemText(0));

                    // ---- ③ 列表 = 两排**现在选中的那几个目录合起来** ----
                    check(listed().size() == 2 &&
                              hasFile(listed(), sStructuresA + QStringLiteral("/房子.nbt")) &&
                              hasFile(listed(),
                                      sSchematics + QStringLiteral("/大厅.litematic")),
                          "★★ U10T 列表是两排**合起来**的（存档甲里那份结构 + 投影那一份），"
                          "不是「选中谁就列谁」",
                          listed().join(QStringLiteral(" / ")));

                    // ---- ④ 换一个存档：跟着换的只有它那半边 ----
                    combos.at(0)->setCurrentIndex(1);
                    check(listed().size() == 2 &&
                              hasFile(listed(), sStructuresB + QStringLiteral("/桥.nbt")) &&
                              !hasFile(listed(), sStructuresA + QStringLiteral("/房子.nbt")) &&
                              hasFile(listed(),
                                      sSchematics + QStringLiteral("/大厅.litematic")),
                          "★★ U10T 换一个存档：属于它的那半边当场换掉，另一个来源纹丝不动",
                          listed().join(QStringLiteral(" / ")));
                    combos.at(0)->setCurrentIndex(0);
                }

                // ---- ⑤ 一个存档都没有结构：存档那一排摆一行灰字 ----
                InstanceFilesPage::StructureSource sNoWorlds;
                sNoWorlds.title = InstanceFilesPage::structureWorldTitle();
                structurePage->setStructureSources({sNoWorlds, sProjections});
                const QList<QComboBox *> greyCombos = structurePage->structureSourceCombos();
                check(greyCombos.size() == 2 && greyCombos.at(0)->count() == 1 &&
                          greyCombos.at(0)->itemText(0) == InstanceFilesPage::structureNoneText(),
                      "★★ U10T 一个存档都没有结构的时候，存档那一排摆的是那行灰字"
                      "（用户原话：「如果没有任何存档含有结构那么存档总下拉打开显示无可用的"
                      "结构」）",
                      greyCombos.isEmpty() ? QStringLiteral("<没有>")
                                           : greyCombos.at(0)->itemText(0));
                check(!greyCombos.isEmpty() && !greyCombos.at(0)->isEnabled(),
                      "★ U10T 那行灰字是**点不动**的（它不是一个能被选中的值：选中它就等于"
                      "没选，这一排对一个条目都不贡献）");
                check(listed().size() == 1 &&
                          hasFile(listed(), sSchematics + QStringLiteral("/大厅.litematic")),
                      "★ U10T 存档那一排空着的时候，列表里只剩投影那一份（不多一条，也没把"
                      "那个空目录当成一个来源）",
                      listed().join(QStringLiteral(" / ")));

                // ---- ⑥ 两样都没有：调用方递空名单 ⇒ 一个框都不摆 ----
                structurePage->setStructureSources({});
                check(structurePage->structureSourceCombos().isEmpty(),
                      "★★ U10T 两样都没有的时候（调用方递空名单）一个框都不摆 —— 用户原话："
                      "「就直接不显示任何框，只保留没有结构的提示文本」");
                structurePage->setDirectory(sRoot + QStringLiteral("/还没有这个目录"));
                check(structurePage->noticeLabel()->isVisibleTo(structurePage) &&
                          structurePage->noticeLabel()->text() ==
                              Dlg::filesEmptyText(QStringLiteral("schematic")),
                      "★★ U10T 上一条说的「只保留那句提示文本」就是这一句（目录也空着的一"
                      "页，走的是别的七页一字不差的那条路）",
                      structurePage->noticeLabel()->text());
            }
        }
    }  // U10
 return 0;
}


