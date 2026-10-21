#include "Support.hpp"
int MwRenameStateChecks(const QDir &versionsDir) {
    // ------------------------------------------------------------------
    // 这一节验的是改名之后**要跟着动的那些状态**，那是这件事里最容易漏的地方：
    // - Version/LastLaunched 还记着老 id → 下次开窗选中项空着，用户以为实例丢了；
    // - 下拉框 / 实例列表还停在老 id → 刚改完名，界面上什么都没选中。
    //
    // 走不了按钮：QInputDialog::getText 是模态的，offscreen 下驱动不了它，测试会一直等。
    // 所以直接驱动按钮背后那一半 applyRename()（产品代码故意把它和"问名字"拆开了，
    // 就是为了让这一半能被证伪）。
    {
        const QString oldId = QStringLiteral("26.3");
        const QString newId = QStringLiteral("26.3-改过名");

        // 先让盘上那份"上次启动过的版本"就是它，**然后**才开窗。窗口只在构造时读一次
        // 这份设置（LauncherSettings 把值缓存在成员里，lastLaunchedVersion() 返回的是
        // 缓存），所以"改完名之后它跟着走"这件事，前提本来就是"它一开始指着这个实例"
        // —— 真机上就是这么来的：启动成功那一刻由同一个对象写进去的。
        rememberLastVersion(oldId);
        check(LauncherSettings().lastLaunchedVersion() == oldId,
              "S1 改名前：盘上记着的是老 id", LauncherSettings().lastLaunchedVersion());

        MainWindow w;
        w.show();
        QApplication::processEvents();
        QComboBox *combo = comboByObjectName(&w, MainWindowUI::installedVersionComboName());
        check(combo != nullptr && combo->currentData().toString() == oldId,
              "S1 开窗时按盘上那份把选中项恢复成了老 id",
              combo != nullptr ? combo->currentData().toString() : QString());

        bool ok = false;
        const bool invoked =
            QMetaObject::invokeMethod(&w, "applyRename", Qt::DirectConnection,
                                      Q_RETURN_ARG(bool, ok), Q_ARG(QString, oldId),
                                      Q_ARG(QString, newId));
        check(invoked, "S1 applyRename 这个槽驱动得起来（按名字）");
        check(ok, "S1 改名成功");

        // 磁盘：老的不在了、新的在，而且 jar 跟着改了名。
        check(!QDir(versionsDir.filePath(oldId)).exists(), "S1 老目录不在盘上了");
        check(QFile::exists(versionsDir.filePath(newId + QLatin1Char('/') + newId +
                                                 QStringLiteral(".jar"))),
              "S1 新目录里有 <新名>.jar");
        check(!QFile::exists(versionsDir.filePath(oldId + QLatin1Char('/') + oldId +
                                                  QStringLiteral(".jar"))),
              "S1 老 jar 没留下");

        check(combo != nullptr && combo->currentData().toString() == newId,
              "S1 下拉框跟着挪到新名字上",
              combo != nullptr ? combo->currentData().toString() : QString());
        {
            QTreeWidget *instances =
                byObjectName<QTreeWidget>(&w, MainWindowUI::instanceListName());
            bool foundNew = false;
            bool foundOld = false;
            for (QTreeWidgetItem *item : instanceItemsOf(instances)) {
                const QString id = item->data(0, Qt::UserRole).toString();
                foundNew = foundNew || id == newId;
                foundOld = foundOld || id == oldId;
            }
            check(instances != nullptr && foundNew && !foundOld,
                  "S1 实例页那张列表里也是新名字（老名字没了）");
        }

        check(LauncherSettings().lastLaunchedVersion() == newId,
              "S1 Version/LastLaunched 跟着改了（不改的话下次开窗选中项是空的）",
              LauncherSettings().lastLaunchedVersion());

        // 唯一能证伪"忘了改 LastLaunched"的办法：**另开一个窗口**，看它自己恢复出什么。
        // 上面那条断言是拿"这个对象应该写出去的值"去比，而这条是让产品真的再走一遍恢复流程。
        {
            MainWindow second;
            second.show();
            QApplication::processEvents();
            QComboBox *combo2 =
                comboByObjectName(&second, MainWindowUI::installedVersionComboName());
            check(combo2 != nullptr && combo2->currentData().toString() == newId,
                  "S1 新开的窗口把选中项恢复成了改名后的那个实例",
                  combo2 != nullptr ? combo2->currentData().toString() : QString());
        }

        // 负例：名字一个字没改 —— 必须在动磁盘之前就被挡下，而且**弹窗说清楚**
        // （产品约定：挡住操作的错误必须让用户看见，不能只写日志）。
        {
            dismissNextPrompt();
            bool ok2 = true;
            QMetaObject::invokeMethod(&w, "applyRename", Qt::DirectConnection,
                                      Q_RETURN_ARG(bool, ok2), Q_ARG(QString, newId),
                                      Q_ARG(QString, newId));
            check(!ok2, "S1 名字一个字没改时拒绝");
            check(!g_modalText.isEmpty(), "S1 而且弹了窗", g_modalText);
            check(QFile::exists(versionsDir.filePath(newId + QLatin1Char('/') + newId +
                                                     QStringLiteral(".jar"))),
                  "S1 被拒绝时磁盘一个字节都没动");
        }
        // 负例：撞上一个已经存在的实例名。目标目录里的东西尤其不能被盖掉。
        {
            dismissNextPrompt();
            bool ok3 = true;
            QMetaObject::invokeMethod(&w, "applyRename", Qt::DirectConnection,
                                      Q_RETURN_ARG(bool, ok3), Q_ARG(QString, newId),
                                      Q_ARG(QString, QStringLiteral("1.18.2")));
            check(!ok3, "S1 撞上已有实例名时拒绝");
            check(!g_modalText.isEmpty(), "S1 也弹了窗", g_modalText);
            check(QFile::exists(versionsDir.filePath(QStringLiteral("1.18.2/1.18.2.jar"))),
                  "S1 目标实例一点没被碰过");
            check(QFile::exists(versionsDir.filePath(newId + QLatin1Char('/') + newId +
                                                     QStringLiteral(".jar"))),
                  "S1 自己的文件也还在");
        }

        // 改回去。后面「沙盒回到 3 个版本」和收尾清理都指望沙盒恢复原样。
        bool back = false;
        QMetaObject::invokeMethod(&w, "applyRename", Qt::DirectConnection,
                                  Q_RETURN_ARG(bool, back), Q_ARG(QString, newId),
                                  Q_ARG(QString, oldId));
        check(back, "S1 还能再改回原来的名字（改名是可逆的）");
        check(LauncherSettings().lastLaunchedVersion() == oldId,
              "S1 改回去时 LastLaunched 也跟着回老 id",
              LauncherSettings().lastLaunchedVersion());
        check(combo != nullptr && combo->findData(oldId) >= 0 && combo->findData(newId) < 0,
              "S1 下拉框里恢复成老 id，新名字没了");
    }
    return 0;
}


