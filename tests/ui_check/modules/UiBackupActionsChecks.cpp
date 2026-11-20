#include "Support.hpp"
int UiBackupActionsChecks() {
    // ==================================================================
    // **入口**分两处：世界设置页那一排（"对这个存档能做的几件事"）和实例概览那一排
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
                      restore->toolTip().contains(QStringLiteral("还原")) &&
                      backupFolder->toolTip().contains(QStringLiteral("备份目录")),
                  "★★ U30.1 三句 tooltip 各说各的（打成一个包 / 挑一份还原 / 备份放在哪儿）"
                  "—— 同款按钮挤一排，说不清就会点错",
                  fullBackup->toolTip());
            // 页面上没有只备份 level.dat 的按钮了：整份备份只有「立即备份」这一条路。
            check(page.findChild<QPushButton *>(QStringLiteral("worldSettingsBackupButton")) == nullptr,
                  "★★ U30.1 页面上不再有只备份 level.dat 的「创建备份」");

            // 次序：重命名 → 打开文件夹 → 立即备份 → 恢复备份… → 打开备份目录 → 删除世界，
            // 页面宽 1000 时放得下，必须在同一排。
            const auto gx = [](QWidget *w) { return w == nullptr ? -1 : w->mapToGlobal(QPoint(0, 0)).x(); };
            const auto gy = [](QWidget *w) { return w == nullptr ? -1 : w->mapToGlobal(QPoint(0, 0)).y(); };
            const bool rowsFound = rename != nullptr && fullBackup != nullptr && restore != nullptr &&
                                   openFolder != nullptr && backupFolder != nullptr && remove != nullptr;
            check(rowsFound && gy(rename) == gy(openFolder) && gy(openFolder) == gy(fullBackup) &&
                      gy(fullBackup) == gy(restore) && gy(restore) == gy(backupFolder) &&
                      gy(backupFolder) == gy(remove) && gx(rename) < gx(openFolder) &&
                      gx(openFolder) < gx(fullBackup) && gx(fullBackup) < gx(restore) &&
                      gx(restore) < gx(backupFolder) && gx(backupFolder) < gx(remove),
                  "★ U30.1 六颗操作按钮放在同一排：重命名 / 打开文件夹 / 立即备份 / 恢复备份… / "
                  "打开备份目录 / 删除世界",
                  QStringLiteral("y %1 %2 %3 %4 %5 %6")
                      .arg(gy(rename)).arg(gy(openFolder)).arg(gy(fullBackup))
                      .arg(gy(restore)).arg(gy(backupFolder)).arg(gy(remove)));

            // ---- 颜色那一档（`Theme::ButtonKind` 的分法就是唯一出处）----
            const auto kindOf = [](QPushButton *button) {
                const QVariant value = button->property("wtfmlButtonKind");
                return value.isValid() ? value.toString()
                                       : Theme::buttonKindKey(Theme::ButtonKind::Accent);
            };
            const QString neutral = Theme::buttonKindKey(Theme::ButtonKind::Neutral);
            const QString accent = Theme::buttonKindKey(Theme::ButtonKind::Accent);
            const QString danger = Theme::buttonKindKey(Theme::ButtonKind::Danger);
            // 档位照 453a074f 的实现：两颗「打开…」是确认性的 Accent，「立即备份」是一般性
            // 的 Neutral（视觉分层由同事定，这里只钉住"改没改过"）。
            const QString openFolderKind = openFolder != nullptr ? kindOf(openFolder) : QString();
            check(kindOf(fullBackup) == neutral && openFolderKind == accent &&
                      kindOf(backupFolder) == accent,
                  "U30.1 「立即备份」是一般性按钮，两颗「打开…」是确认性 Accent",
                  kindOf(fullBackup) + QStringLiteral(" / ") + openFolderKind +
                      QStringLiteral(" / ") + kindOf(backupFolder));
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
            check(renameClicks == 0 && openClicks == 0 && deleteClicks == 0,
                  "U30.1 也**没顺手**把旁边那几颗（重命名 / 打开文件夹 / 删除世界）的信号带上");
            // 这三颗**都只发信号**：真打包 / 挑包 / 开目录全在 MainWindow（那几步要任务
            // 账本、后台线程、覆盖层）。这一页自己要是动了盘，实例设置那边就没法统一管了。
        }

        page.close();
        QApplication::processEvents();
    }
    return 0;
}


