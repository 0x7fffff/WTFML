// 实例设置文件页的行是**代理画的**（步骤 139）：一页 60 条不该冒出 60 套行控件，
// 点行尾那颗开关要真把盘上的文件改名，刷新只换模型数据、视图和滚动位置都不动。
#include "Support.hpp"
#include "UiInstanceFilesTestHelpers.hpp"

#include <QKeyEvent>
#include <QScrollBar>
#include <QTemporaryDir>

int UiFilesRowsCheck(QApplication &app) {
    app.setStyle(Theme::createStyle());
    Theme::setSpec(Theme::defaultSpec());
    Theme::apply(app);
    int failures = 0;
    const auto expect = [&failures](bool ok, const char *message, const QString &detail = QString()) {
        printf("%s %s%s\n", ok ? "PASS" : "FAIL", message,
               detail.isEmpty() ? "" : qPrintable(QStringLiteral("  [") + detail + QLatin1Char(']')));
        if (!ok) ++failures;
    };

    QTemporaryDir scratch(QDir::current().filePath(QStringLiteral(".files-rows-XXXXXX")));
    if (!scratch.isValid()) return 1;
    const QString mods = scratch.path() + QStringLiteral("/mods");
    QDir().mkpath(mods);

    InstanceFilesPage page;
    page.setCanToggle(true);
    page.setCanDelete(true);
    page.resize(900, 620);
    page.setDirectory(mods);
    page.refresh();
    page.show();
    app.processEvents();
    const int emptyChildren = static_cast<int>(page.findChildren<QWidget *>().size());

    for (int i = 0; i < 60; ++i) {
        uiMkFile(mods + QStringLiteral("/mod-%1.jar").arg(i, 2, 10, QLatin1Char('0')));
    }
    QListWidget *list = page.list();
    QWidget *viewport = list->viewport();
    page.refresh();
    app.processEvents();

    // ---- 守卫 1：60 行不建 60 套控件 ----
    const int fullChildren = static_cast<int>(page.findChildren<QWidget *>().size());
    expect(list->count() == 60, "fixture lists 60 mods", QString::number(list->count()));
    expect(fullChildren - emptyChildren < 10 && viewport->findChildren<QWidget *>().size() < 10,
           "60 rows add no per-row child widgets (rows are painted by the delegate)",
           QStringLiteral("空页 %1 / 60 行 %2 / 视口 %3")
               .arg(emptyChildren)
               .arg(fullChildren)
               .arg(viewport->findChildren<QWidget *>().size()));

    // ---- 守卫 2：点开关 ⇒ 盘上真改名，那一行跟着翻 ----
    // 产品里 toggleRequested 由控制器改名再刷新；这里照同一个约定接一份最小的改名。
    QObject::connect(&page, &InstanceFilesPage::toggleRequested, &page, [&page](const QString &path) {
        const QString target = path.endsWith(QStringLiteral(".disabled"))
                                   ? path.chopped(int(sizeof(".disabled") - 1))
                                   : path + QStringLiteral(".disabled");
        QFile::rename(path, target);
        page.refresh();
    });
    const auto rowOf = [&page, list](const QString &fileName) {
        for (int row = 0; row < list->count(); ++row) {
            if (QFileInfo(uiItemPath(&page, row)).fileName() == fileName) return row;
        }
        return -1;
    };
    const QString jar = mods + QStringLiteral("/mod-30.jar");
    int row = rowOf(QStringLiteral("mod-30.jar"));
    const auto toggle = uiToggleAt(&page, row);
    expect(toggle != nullptr && toggle->isEnabled() && toggle->isChecked(),
           "enabled mod shows a switched-on toggle at the row end");

    QScrollBar *bar = list->verticalScrollBar();
    bar->setValue(bar->maximum() / 2);
    app.processEvents();
    const int scrollBefore = bar->value();
    if (toggle != nullptr) toggle->click();
    app.processEvents();
    row = rowOf(QStringLiteral("mod-30.jar.disabled"));
    expect(!QFileInfo::exists(jar) && QFileInfo::exists(jar + QStringLiteral(".disabled")),
           "clicking the toggle renames the file to .disabled on disk");
    expect(row >= 0 && list->item(row)->checkState() == Qt::Unchecked &&
               list->item(row)->data(InstanceFilesPage::disabledRole()).toBool(),
           "after refresh the row shows disabled (toggle off, disabled role set)");
    expect(page.list() == list && list->viewport() == viewport,
           "refresh replaces model data only: same list, same viewport");
    expect(qAbs(bar->value() - scrollBefore) <= list->sizeHintForRow(0),
           "scroll position does not jump after the refresh",
           QStringLiteral("%1 -> %2").arg(scrollBefore).arg(bar->value()));

    if (const auto back = uiToggleAt(&page, row)) back->click();
    app.processEvents();
    row = rowOf(QStringLiteral("mod-30.jar"));
    expect(QFileInfo::exists(jar) && row >= 0 && list->item(row)->checkState() == Qt::Checked,
           "clicking it again renames back and the toggle is on again");

    // ---- 读屏：名字和开关状态在条目上 ----
    expect(row >= 0 && list->item(row)->data(Qt::AccessibleTextRole).toString() == QStringLiteral("mod-30.jar") &&
               (list->item(row)->flags() & Qt::ItemIsUserCheckable),
           "screen readers get the name (AccessibleTextRole) and a checkable state");

    // ---- 空格不改盘（原来那套行控件也不响应空格切换）----
    list->setCurrentRow(row);
    list->setFocus();
    QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "));
    QApplication::sendEvent(list, &space);
    QKeyEvent spaceUp(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "));
    QApplication::sendEvent(list, &spaceUp);
    app.processEvents();
    expect(QFileInfo::exists(jar), "Space on a focused row does not rename the file");

    printf("%s files-rows: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
