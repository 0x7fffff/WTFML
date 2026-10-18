#pragma once
#include "InstanceFilesPage.hpp"
#include "Animations.hpp"
#include "download/ModDependencies.hpp"
#include <QApplication>
#include <QTemporaryDir>
#include <QListWidget>
#include <QPushButton>
#include <QFile>
#include <QDir>
#include <QFontDatabase>
#include <cstdio>

inline int runModActionPanelChecks() {
    Anim::setEnabled(false);
    int failed = 0;
    auto check = [&](bool ok, const char *name) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", name); if (!ok) ++failed;
    };
    QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyh.ttc"));
    QApplication::setFont(QFont(QStringLiteral("Microsoft YaHei"), 10));
    qApp->setStyleSheet(Theme::widgetStyleSheet());
    QTemporaryDir dir(QDir::current().filePath(QStringLiteral("build/mod-panel-fixture-XXXXXX")));
    for (const auto &name : {QStringLiteral("Sodium.jar"), QStringLiteral("Iris.jar.disabled")}) {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::WriteOnly) || file.write("fixture") < 0) return 1;
    }
    InstanceFilesPage page;
    page.setBatchMode(true); page.setCanToggle(true); page.setCanDelete(true);
    page.setActionPanelEnabled(true); page.setMode(InstanceFilesPage::Mode::FilesOnly);
    page.resize(1000, 640); page.show(); page.setDirectory(dir.path()); page.refresh();
    QApplication::processEvents();
    auto *list = page.findChild<QListWidget *>();
    auto *panel = page.selectionBar();
    check(list && list->count() == 2, "mod fixture displayed");
    check(panel->isHidden(), "no selection hides action panel");
    auto *all = page.findChild<QPushButton *>(InstanceFilesPage::selectionButtonName(InstanceFilesPage::SelectionAction::SelectAll));
    auto *clear = page.findChild<QPushButton *>(InstanceFilesPage::selectionButtonName(InstanceFilesPage::SelectionAction::ClearAll));
    check(all->isVisible() && !clear->isEnabled(), "selection tools visible without selection");
    all->click(); QApplication::processEvents();
    QStringList checkedPaths;
    QObject::connect(&page, &InstanceFilesPage::checkSelectedUpdatesRequested, &page,
                     [&](const QStringList &paths) { checkedPaths = paths; });
    page.findChild<QPushButton *>(QStringLiteral("modCheckSelectedUpdates"))->click();
    check(checkedPaths.size() == 2, "update check forwards selected paths only");
    check(list->selectedItems().size() == 2 && !panel->isHidden(), "multi-selection opens panel");
    check(panel->width() == 260 && list->width() < 740, "wide layout reserves panel space");
    page.findChild<QPushButton *>(QStringLiteral("modActionPanelClose"))->click();
    check(panel->isHidden() && list->selectedItems().size() == 2, "close preserves selection");
    page.findChild<QPushButton *>(QStringLiteral("modSelectedCount"))->click();
    check(!panel->isHidden(), "selected count reopens panel");
    int toggles = 0;
    QObject::connect(&page, &InstanceFilesPage::batchToggleRequested, &page,
        [&](const QStringList &paths, bool) { toggles += paths.size(); });
    // The original action buttons remain the executor endpoints for the state menu.
    page.findChild<QPushButton *>(InstanceFilesPage::selectionButtonName(InstanceFilesPage::SelectionAction::Enable))->click();
    check(toggles == 1 && list->selectedItems().size() == 2, "mixed selection enables only disabled file and keeps selection");
    const QString renamed = dir.filePath(QStringLiteral("Iris.jar"));
    QFile::rename(dir.filePath(QStringLiteral("Iris.jar.disabled")), renamed);
    page.refresh(); QApplication::processEvents();
    check(list->selectedItems().size() == 2, "refresh preserves stable file identities after rename");
    const auto preview = qEnvironmentVariable("WTFML_MOD_PREVIEW_DIR");
    if (!preview.isEmpty()) { QDir().mkpath(preview); page.grab().save(preview + QStringLiteral("/mods-wide.png")); }
    page.resize(560, 640); QApplication::processEvents();
    check(page.width() == 560 && list->width() > 500 && panel->geometry().right() < page.width(), "narrow drawer does not force minimum width");
    if (!preview.isEmpty()) page.grab().save(preview + QStringLiteral("/mods-narrow.png"));
    QFile::remove(renamed);
    page.refresh(); QApplication::processEvents();
    check(list->selectedItems().size() == 1 && !panel->isHidden(), "partial removal retains remaining selection");
    page.setSearchText(QStringLiteral("no-such-mod")); QApplication::processEvents();
    check(list->selectedItems().isEmpty() && panel->isHidden(), "filter cannot leave hidden selected operations");
    page.setSearchText(QString()); all->click();
    clear->click();
    check(list->selectedItems().isEmpty() && panel->isHidden(), "clear selection closes panel");
    ModDependencies::UpdateVerdict oldA, oldB, newA;
    oldA.item.path = QStringLiteral("a.jar"); oldA.latestVersionId = QStringLiteral("old");
    oldB.item.path = QStringLiteral("b.jar"); newA = oldA; newA.latestVersionId = QStringLiteral("new");
    const auto merged = ModDependencies::mergeScopedUpdates({oldA, oldB}, {newA}, {QStringLiteral("a.jar")});
    check(merged.size() == 2 && merged.first().latestVersionId == QStringLiteral("new") &&
          merged.last().item.path == QStringLiteral("b.jar"), "partial check replaces selected result but preserves unrelated result");
    check(ModDependencies::mergeScopedUpdates({oldA, oldB}, {}, {QStringLiteral("a.jar")}).size() == 1,
          "unknown or removed selected file clears its old update verdict only");
    check(ModDependencies::mergeScopedUpdates({oldA, oldB}, {newA}, {}).size() == 1,
          "full-instance check replaces entire result");
    return failed ? 1 : 0;
}
