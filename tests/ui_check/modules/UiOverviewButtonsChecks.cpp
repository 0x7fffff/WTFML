#include "Support.hpp"

#include "UiInstanceFilesTestHelpers.hpp"

// U10b 常用目录那一排按钮（住在概览页里）
//
// 这一段原来住在 `UiContentPagesChecks.cpp` 里、跟着那一大段共用的 `dialog`。分出来以后
// `dialog` 按引用传，其余夹具按显式形参传；生命周期见 docs/dev/TESTING.md
// 「夹具生命周期（ui_check 的大模块）」。
int UiOverviewButtonsChecks(InstanceSettingsDialog &dialog, const QString &root,
                                const QHash<QString, QString> &dirs) {
    using Dlg = InstanceSettingsDialog;
    const auto showPage = [](Dlg &d, const QString &key) { d.showPage(key); };
    const auto itemText = [](InstanceFilesPage *page, int row) { return uiItemText(page, row); };
    const auto itemPath = [](InstanceFilesPage *page, int row) { return uiItemPath(page, row); };
    const auto itemFlag = [](InstanceFilesPage *page, int row, int role) { return uiItemFlag(page, row, role); };
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
        // 都不报 —— 这就是当年那条“偶发 `c0000374`”的另一半（见 docs/dev/TESTING.md
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
    return 0;
}

