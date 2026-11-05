#include "Support.hpp"
#include "UiInstanceFilesTestHelpers.hpp"
int UiContentPagesChecks() {
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
        // 读行里那几格的实现在 `UiInstanceFilesTestHelpers.hpp`（拆出去的模块也要用同一份，
        // 别各写一遍）。这里保留同名转发，调用点不动。
        const auto itemText = [](InstanceFilesPage *page, int row) { return uiItemText(page, row); };
        const auto itemFlag = [](InstanceFilesPage *page, int row, int role) {
            return uiItemFlag(page, row, role);
        };
        const auto itemPath = [](InstanceFilesPage *page, int row) { return uiItemPath(page, row); };

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
        //
        // 这一段已经分到 `UiPageIdentityChecks`（见 modules/）：共用 `dialog` 与其余夹具都按显式形参传。
        if (const int result = UiPageIdentityChecks(dialog); result != 0) {
            return result;
        }

        // ---- U10b 常用目录那一排按钮（住在**概览**页里）----
        //
        // 这一段已经分到 `UiOverviewButtonsChecks`（见 modules/）：共用 `dialog` 与其余夹具都按显式形参传。
        if (const int result = UiOverviewButtonsChecks(dialog, root, dirs); result != 0) {
            return result;
        }

        // ---- U10b2：那一排分成两段（在盘的在外、其余收在「更多目录…」里）----
        //
        // 这一段已经分到 `UiDirectoryButtonsChecks`（见 modules/）：共用 `dialog` 与其余夹具都按显式形参传。
        if (const int result = UiDirectoryButtonsChecks(dialog); result != 0) {
            return result;
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
        // 这一段已经分到 `UiSearchFilterChecks`（见 modules/）：共用 `dialog` 与其余夹具都按显式形参传。
        if (const int result = UiSearchFilterChecks(dialog, modsDir); result != 0) {
            return result;
        }

        // ---- U10d 三条开关：能不能启停 / 能不能删 / 只列文件夹 ----
        //
        // 这一段已经分到 `UiToggleChecks`（见 modules/）：共用 `dialog` 与其余夹具都按显式形参传。
        if (const int result = UiToggleChecks(dialog, modsDir); result != 0) {
            return result;
        }

        // ---- U10O 「存档」那一页的三样新东西----
        //
        // 这一段已经分到 `UiMediumPagesChecks`（见 modules/）：共用 `dialog` 与其余夹具都按显式形参传。
        if (const int result = UiMediumPagesChecks(dialog, root, savesDir); result != 0) {
            return result;
        }

        // ---- U10e 行尾那两颗按下去报的是哪一条 ----
        //
        // 这一段已经分到 `UiRowButtonsChecks`（见 modules/）：共用 `dialog` 与其余夹具都按显式形参传。
        if (const int result = UiRowButtonsChecks(dialog, modsDir); result != 0) {
            return result;
        }

        // ---- U10f 空目录 / 用不上：两句不同的话 ----
        //
        // 这一段已经分到 `UiEmptyPageChecks`（见 modules/）：共用 `dialog` 与其余夹具都按显式形参传。
        if (const int result = UiEmptyPageChecks(instanceDisplayName, instanceDirName, global, root); result != 0) {
            return result;
        }

        // ---- U10g 服务器列表：三种状态 ----
        //
        // 这一段已经分到 `UiServerListChecks`（见 modules/）：共用 `dialog` 与其余夹具都按显式形参传。
        if (const int result = UiServerListChecks(dialog); result != 0) {
            return result;
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
        // 这一段已经分到 `UiElidedNameChecks`（见 modules/）：共用 `dialog` 与其余夹具都按显式形参传。
        if (const int result = UiElidedNameChecks(instanceDisplayName, instanceDirName, global, root); result != 0) {
            return result;
        }

        // ---- U10M 顶上那颗「下载新 X」：哪几页有、按下去报的是哪一栏----
        //
        // 这一段已经分到 `UiDownloadNewChecks`（见 modules/）：共用 `dialog` 与其余夹具都按显式形参传。
        if (const int result = UiDownloadNewChecks(dialog, instanceDisplayName, instanceDirName, global); result != 0) {
            return result;
        }

        // ---- U10N 行尾那颗「详情」⇒ 报出这一条是哪个文件（ / ）----
        //
        // 这一段已经分到 `UiDetailEntryChecks`（见 modules/）：共用 `dialog` 与其余夹具都按显式形参传。
        if (const int result = UiDetailEntryChecks(instanceDisplayName, instanceDirName, global); result != 0) {
            return result;
        }

        // ---- U10Q 批量管理：多选 / Ctrl+A / 悬浮操作条 / 灰化----
        //
        // 这一段已经分到 `UiBatchManagementChecks`（见 modules/）：共用 `dialog` 与其余夹具都按显式形参传。
        if (const int result = UiBatchManagementChecks(instanceDisplayName, instanceDirName, global); result != 0) {
            return result;
        }

        // ---- U10R 第四批：行尾次序 / 详情悬停才露 / 右键进详情 / 两颗开关的显隐 /
        //
        // 这一段已经分到 `UiRowActionsChecks`（见 modules/）：共用 `dialog` 与其余夹具都按显式形参传。
        if (const int result = UiRowActionsChecks(instanceDisplayName, instanceDirName, global); result != 0) {
            return result;
        }

        // ---- U10L 「服务器」那一页的"现在什么样"（：图标 / MOTD / 延迟）----
        //
        // 这一段已经分到 `UiServerPageChecks`（见 modules/）：共用 `dialog` 与其余夹具都按显式形参传。
        if (const int result = UiServerPageChecks(dialog); result != 0) {
            return result;
        }
        // ---- U10S 「编辑」/「加入服务器」/「加入选中的世界」那三颗----
        //
        // 这一段已经分到 `UiQuickPlayChecks`（见 modules/）：共用 `dialog` 与其余夹具都按显式形参传。
        if (const int result = UiQuickPlayChecks(dialog, instanceDisplayName, instanceDirName, global); result != 0) {
            return result;
        }

        // ---------- U10M 拨一下启用/禁用之后，列表**不许跳回顶端**----------
        //
        // 这一段已经分到 `UiScrollAnchorChecks`（见 modules/）：共用 `dialog` 与其余夹具都按显式形参传。
        if (const int result = UiScrollAnchorChecks(instanceDisplayName, instanceDirName, global); result != 0) {
            return result;
        }
    }
        // ---- U10T 「结构/原理图」顶上那两排「来源」----
        //
        // 这一段已经分到 `UiStructureSourcesChecks`（见 modules/）：共用 `dialog` 与其余夹具都按显式形参传。
        if (const int result = UiStructureSourcesChecks(instanceDisplayName, instanceDirName, global); result != 0) {
            return result;
        }
    }  // U10
 return 0;
}
