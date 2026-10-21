#include "Support.hpp"
int UiBackupActionsChecks() {
    // ==================================================================
    // §4.4 的**入口**分两处：世界设置页那一排（"对这个存档能做的几件事"）和实例概览那一排
    // （"对这个实例能做的几件事"）。这一层量的是"摆出来的是什么" —— objectName、那几个字、
    // 那两句解释说的是不是各自那件事、点一下发的是哪一发信号、以及**颜色那一档**。
    //
    // 干活的那一半不在这儿：打包 / 还原 / 安全快照 / 账本 / busy 守卫在 mw_check 的 T35，
    // 纯逻辑（清单、排除、校验值、文件名）在别处的 ZQ 段。
    {
        WorldSettingsPage page;
        page.resize(1000, 640);
        page.show();
        QApplication::processEvents();

        const auto widgetByName = [&page](const QString &name) {
            return page.findChild<QPushButton *>(name);
        };
        auto *rename = widgetByName(WorldSettingsPage::renameButtonName());
        auto *openFolder = widgetByName(WorldSettingsPage::openFolderButtonName());
        auto *fullBackup = widgetByName(WorldSettingsPage::fullBackupButtonName());
        auto *restore = widgetByName(WorldSettingsPage::restoreButtonName());
        auto *backupFolder = widgetByName(WorldSettingsPage::backupFolderButtonName());
        auto *remove = widgetByName(WorldSettingsPage::deleteButtonName());
        auto *backupOne = widgetByName(WorldSettingsPage::backupButtonName());

        check(fullBackup != nullptr && restore != nullptr && backupFolder != nullptr,
              "★★ U30.1 世界设置页那一排上三颗都在（立即备份 / 恢复备份… / 打开备份目录）");
        if (fullBackup != nullptr && restore != nullptr && backupFolder != nullptr) {
            check(fullBackup->objectName() == WorldSettingsPage::fullBackupButtonName() &&
                      restore->objectName() == WorldSettingsPage::restoreButtonName() &&
                      backupFolder->objectName() == WorldSettingsPage::backupFolderButtonName(),
                  "U30.1 objectName 挂的就是唯一出处里那三个（自动化只认得出这把钥匙）",
                  fullBackup->objectName());
            check(fullBackup->text() == WorldSettingsPage::fullBackupText() &&
                      restore->text() == WorldSettingsPage::restoreBackupText() &&
                      backupFolder->text() == WorldSettingsPage::openBackupFolderText(),
                  "U30.1 三颗的字来自唯一出处（.cpp 里那三个 getter）",
                  fullBackup->text() + QStringLiteral(" / ") + restore->text()
                      + QStringLiteral(" / ") + backupFolder->text());
            check(fullBackup->text() == QStringLiteral("立即备份") &&
                      restore->text() == QStringLiteral("恢复备份…") &&
                      backupFolder->text() == QStringLiteral("打开备份目录"),
                  "U30.1 而且就是那三个字（存档页上「备份」这个词已经有一颗沿用的了，"
                  "所以这一颗特意叫「立即备份」）",
                  fullBackup->text());
            check(fullBackup->isVisible() && restore->isVisible() && backupFolder->isVisible(),
                  "U30.1 这一页开着的时候三颗都看得见（不是建出来就藏着的）");
            // 三句话各说各的：一个说"整个文件夹打成一个包"，一个说"挑一份还原回来"，
            // 一个说"备份放在哪儿"。三颗同款按钮挤在一排上，说不清就会点错。
            check(fullBackup->toolTip().contains(QStringLiteral("压缩包")) &&
                      restore->toolTip().contains(QStringLiteral("挑一份")) &&
                      backupFolder->toolTip().contains(QStringLiteral("哪儿")),
                  "★★ U30.1 三句 tooltip 各说各的（打成一个包 / 挑一份还原 / 备份放在哪儿）"
                  "—— 同款按钮挤一排，说不清就会点错",
                  fullBackup->toolTip());
            // 和底下那颗「备份一份」**必须分得开**：那一颗抄的是 level.dat 一个文件
            // （救命稻草），这一颗打的是**整个存档目录**。名字差一个字、说明差一句话，
            // 用户就会以为是同一件事按两遍。
            check(backupOne != nullptr && backupOne->text() != fullBackup->text() &&
                      backupOne->toolTip() != fullBackup->toolTip(),
                  "★★ U30.1 「立即备份」和底下那颗「备份一份」名字与说明都**不是同一句**"
                  "（一颗是整个存档的压缩包、一颗只是 level.dat 那个文件）",
                  backupOne != nullptr ? backupOne->toolTip() : QStringLiteral("<没找到>"));

            // 次序：重命名 → 打开文件夹 → 立即备份 → 恢复备份… → 打开备份目录 → 删除世界。
            // 三颗新的夹在"打开文件夹"和"删除"中间：前面那几颗是"这个存档叫什么 / 在哪儿"，
            // 中间这三颗是"留一份 / 换回去 / 去看备份"，最后才是"删掉"。
            const auto orderOf = [](QWidget *w) {
                const QPoint p = w->mapToGlobal(QPoint(0, 0));
                return QPoint(p.x(), p.y());
            };
            const bool ordered = rename != nullptr && openFolder != nullptr && remove != nullptr
                                 && orderOf(rename).x() < orderOf(openFolder).x()
                                 && orderOf(openFolder).x() < orderOf(fullBackup).x()
                                 && orderOf(fullBackup).x() < orderOf(restore).x()
                                 && orderOf(restore).x() < orderOf(backupFolder).x()
                                 && orderOf(backupFolder).x() < orderOf(remove).x();
            check(ordered,
                  "★ U30.1 三颗新按钮夹在「打开文件夹」和「删除世界」中间（留一份 / 换回去 / "
                  "去看备份 是一档，删掉是另一档）",
                  QStringLiteral("x: %1 %2 %3 %4 %5 %6")
                      .arg(fullBackup->mapToGlobal(QPoint(0, 0)).x())
                      .arg(restore->mapToGlobal(QPoint(0, 0)).x())
                      .arg(backupFolder->mapToGlobal(QPoint(0, 0)).x())
                      .arg(openFolder->mapToGlobal(QPoint(0, 0)).x())
                      .arg(rename->mapToGlobal(QPoint(0, 0)).x())
                      .arg(remove->mapToGlobal(QPoint(0, 0)).x()));

            // ---- 颜色那一档（`Theme::ButtonKind` 的分法就是唯一出处）----
            const auto kindOf = [](QPushButton *button) {
                const QVariant value = button->property("wtfmlButtonKind");
                return value.isValid() ? value.toString()
                                       : Theme::buttonKindKey(Theme::ButtonKind::Accent);
            };
            const QString neutral = Theme::buttonKindKey(Theme::ButtonKind::Neutral);
            const QString ghost = Theme::buttonKindKey(Theme::ButtonKind::Ghost);
            const QString danger = Theme::buttonKindKey(Theme::ButtonKind::Danger);
            check(kindOf(fullBackup) == neutral && kindOf(backupFolder) == ghost,
                  "U30.1 「立即备份」保留一般性按钮，「打开备份目录」降为低频 Ghost",
                  kindOf(fullBackup) + QStringLiteral(" / ") + kindOf(backupFolder));
            // 「恢复备份…」是红的：它把**这个存档现在这一份**整个换掉。`Danger` 那一档的
            // 界定是"删了就拿不回来"，点名的几条里就有"还原"（见 Theme.hpp 里 ButtonKind
            // 那段）；还原之前自动留的那一份是兜底，不等于这一步没风险。
            check(kindOf(restore) == danger && kindOf(remove) == danger,
                  "★★ U30.1 「恢复备份…」和「删除世界」同一档（红）—— 它把现在这一份整个换掉",
                  kindOf(restore));

            // ---- 点一下：各发各的信号 ----
            int renameClicks = 0;
            int openClicks = 0;
            int fullClicks = 0;
            int restoreClicks = 0;
            int folderClicks = 0;
            int deleteClicks = 0;
            int backupOneClicks = 0;
            QObject::connect(&page, &WorldSettingsPage::renameRequested,
                             [&renameClicks] { ++renameClicks; });
            QObject::connect(&page, &WorldSettingsPage::openFolderRequested,
                             [&openClicks] { ++openClicks; });
            QObject::connect(&page, &WorldSettingsPage::fullBackupRequested,
                             [&fullClicks] { ++fullClicks; });
            QObject::connect(&page, &WorldSettingsPage::restoreBackupRequested,
                             [&restoreClicks] { ++restoreClicks; });
            QObject::connect(&page, &WorldSettingsPage::openBackupFolderRequested,
                             [&folderClicks] { ++folderClicks; });
            QObject::connect(&page, &WorldSettingsPage::deleteRequested,
                             [&deleteClicks] { ++deleteClicks; });
            QObject::connect(&page, &WorldSettingsPage::backupRequested,
                             [&backupOneClicks] { ++backupOneClicks; });
            fullBackup->click();
            restore->click();
            backupFolder->click();
            QApplication::processEvents();
            check(fullClicks == 1 && restoreClicks == 1 && folderClicks == 1,
                  "★★ U30.1 三颗各发各的信号，一发不多一发不少（串线的话用户按「立即备份」会"
                  "弹一个「要删掉这个世界吗」）",
                  QStringLiteral("备份 %1 / 恢复 %2 / 打开目录 %3")
                      .arg(fullClicks)
                      .arg(restoreClicks)
                      .arg(folderClicks));
            check(renameClicks == 0 && openClicks == 0 && deleteClicks == 0 &&
                      backupOneClicks == 0,
                  "U30.1 也**没顺手**把旁边那几颗（重命名 / 打开文件夹 / 备份一份 / 删除世界）"
                  "的信号带上");
            // 这三颗**都只发信号**：真打包 / 挑包 / 开目录全在 MainWindow（那几步要任务
            // 账本、后台线程、覆盖层）。这一页自己要是动了盘，实例设置那边就没法统一管了。
        }

        page.close();
        QApplication::processEvents();
    }
    return 0;
}


