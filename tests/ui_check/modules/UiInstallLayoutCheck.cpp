#include "Support.hpp"
#include "PresetEditorPage.hpp"
#include "PresetInstallPicker.hpp"
#include <QJsonArray>
int UiInstallLayoutCheck(QApplication &app) {

        const int fontId = QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts/msyh.ttc")));
        const auto families = QFontDatabase::applicationFontFamilies(fontId);
        if (!families.isEmpty()) app.setFont(QFont(families.first(), 10));
        app.setStyle(Theme::createStyle());
        Theme::setSpec(Theme::defaultSpec());
        Theme::apply(app);
        Anim::setEnabled(false);
        QMainWindow window;
        MainWindowUI ui(&window);
        ui.getTabWidget()->setCurrentIndex(MainWindowUI::downloadTabIndex());
        ui.getDownloadStack()->setCurrentIndex(0);
        ui.getGameStack()->setCurrentIndex(1);
        int failures = 0;
        const auto expect = [&](bool ok, const char *label) {
            printf("%s %s\n", ok ? "PASS" : "FAIL", label);
            if (!ok) ++failures;
        };
        auto *tree = ui.getLoaderTree();
        int visibleRows = 0;
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            auto *row = tree->topLevelItem(i);
            if (!row->isHidden()) ++visibleRows;
            if (row->data(0, Qt::UserRole).toInt() == MainWindowUI::modLoaderNoneIndex())
                expect(row->isHidden(), "vanilla sentinel is not a duplicate visible option");
        }
        expect(visibleRows == 5, "four loaders and one preset entry are visible");
        const QIcon themedGear = VersionIcons::actionIcon(VersionIcons::Action::Settings);
        const QIcon themedDetails = VersionIcons::actionIcon(VersionIcons::Action::Details);
        const QIcon themedDelete = VersionIcons::actionIcon(VersionIcons::Action::Delete);
        QImage lastIcon;
        for (const auto &preset : Theme::presetIds()) {
            if (preset == QStringLiteral("system")) continue; // May legitimately match a named preset.
            auto spec = Theme::defaultSpec();
            spec.presetId = preset;
            Theme::setSpec(spec);
            Theme::apply(app);
            const QImage normal = themedGear.pixmap(24, 24, QIcon::Normal).toImage();
            const QImage disabled = themedGear.pixmap(24, 24, QIcon::Disabled).toImage();
            expect(!normal.isNull() && normal != disabled, "action icons preserve a disabled colour");
            if (!lastIcon.isNull()) expect(normal != lastIcon, "existing icon follows theme changes");
            lastIcon = normal;
            for (const auto &icon : {themedDetails, themedDelete}) {
                const QImage enabled = icon.pixmap(24, 24, QIcon::Normal).toImage();
                expect(!enabled.isNull() && enabled != icon.pixmap(24, 24, QIcon::Disabled).toImage(),
                       "details and delete have distinct enabled and disabled icons");
            }
        }
        Theme::setSpec(Theme::defaultSpec());
        Theme::apply(app);
        for (const QSize size : {QSize(900, 620), QSize(1200, 800)}) {
            window.resize(size);
            window.show();
            for (int pass = 0; pass < 5; ++pass) app.processEvents();
            auto *button = ui.getInstallButton();
            auto *scroll = ui.getInstallScroll();
            const QPoint origin = scroll->mapFromGlobal(button->mapToGlobal(QPoint()));
            expect(qAbs(origin.x() + button->width() / 2 - scroll->width() / 2) <= 2,
                   "install action is centred in the right pane");
            expect(origin.y() > 0 && origin.y() + button->height() <= scroll->height(),
                   "floating action remains inside the content pane");
            expect(!scroll->isAncestorOf(button), "action does not scroll with the form");
            auto *row = tree->topLevelItem(0);
            const int stableWidth = tree->viewport()->width();
            const QRect clearBefore = VersionTreeDelegate::clearRect(tree->visualItemRect(row));
            const QRect arrowBefore = VersionTreeDelegate::arrowRect(tree->visualItemRect(row));
            for (int i = 0; i < 20; ++i) new QTreeWidgetItem(row, {QString::number(i)});
            row->setExpanded(true);
            // Nested layouts post their own LayoutRequest events; one pass only
            // updates the inner tree, not necessarily the outer scroll range.
            for (int pass = 0; pass < 5; ++pass) app.processEvents();
            expect(tree->verticalScrollBar()->maximum() > 0, "expanded loader list exercises a real scrollbar");
            expect(tree->viewport()->width() == stableWidth &&
                       VersionTreeDelegate::clearRect(tree->visualItemRect(row)).x() == clearBefore.x() &&
                       VersionTreeDelegate::arrowRect(tree->visualItemRect(row)).x() == arrowBefore.x(),
                   "scrollbar appearance preserves text width and arrow/clear positions");
            scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
            tree->scrollToItem(tree->topLevelItem(4));
            for (int pass = 0; pass < 5; ++pass) app.processEvents();
            expect(scroll->mapFromGlobal(button->mapToGlobal(QPoint())) == origin, "expansion and scrolling do not move the action");
            const QPoint last = scroll->mapFromGlobal(tree->viewport()->mapToGlobal(tree->visualItemRect(tree->topLevelItem(4)).bottomLeft()));
            printf("layout last=%d action=%d outer=%d/%d tree=%d/%d\n", last.y(), origin.y(),
                   scroll->verticalScrollBar()->value(), scroll->verticalScrollBar()->maximum(),
                   tree->verticalScrollBar()->value(), tree->verticalScrollBar()->maximum());
            expect(last.y() < origin.y(), "last visible entry can scroll above the floating action");
            row->setExpanded(false);
            qDeleteAll(row->takeChildren());
            for (int pass = 0; pass < 5; ++pass) app.processEvents();
            if (app.arguments().contains(QStringLiteral("--install-layout-preview"))) {
                scroll->verticalScrollBar()->setValue(0);
                app.processEvents();
                QDir().mkpath(QStringLiteral("build/install-previews"));
                window.grab().save(QStringLiteral("build/install-previews/install-%1.png").arg(size.width()));
            }
        }
        {
            ui.setInstanceMode(MainWindowUI::InstanceMode::Preset);
            auto *instancePage=ui.getTabWidget()->widget(MainWindowUI::instancesTabIndex());
            auto *folderColumn=instancePage->findChild<QWidget *>(MainWindowUI::leftColumnName("instance"));
            expect(folderColumn&&folderColumn->isHidden(),"preset mode removes the entire folder column including its reserved width");
            ui.navigationGuard=[] { return false; };
            if (auto *client=ui.getInstanceModeButton(MainWindowUI::InstanceMode::Client))client->click();
            expect(ui.getInstanceModeStack()->currentIndex()==static_cast<int>(MainWindowUI::InstanceMode::Preset) && ui.getInstanceModeButton(MainWindowUI::InstanceMode::Preset)->isChecked(),"cancelled preset departure keeps the active mode indicator");
            ui.navigationGuard={};
            ui.setInstanceMode(MainWindowUI::InstanceMode::Client);
            expect(folderColumn&&!folderColumn->isHidden(),"returning to client instances restores the folder column");
            PresetEditorPage editor(QJsonObject{{"id","editor-test"},{"name","Original"}});
            if(app.arguments().contains(QStringLiteral("--install-layout-preview"))){
                editor.resize(980,640);editor.show();app.processEvents();QDir().mkpath("build/install-previews");editor.grab().save("build/install-previews/preset-editor.png");
            }
            editor.confirmReset=[](const QString &){return true;};
            auto *name=editor.findChild<QLineEdit *>("presetEditorName");
            expect(!editor.isDirty(),"new editor opens without false unsaved changes");
            name->setText("Changed");expect(editor.isDirty(),"name edits join the unsaved document");
            auto *nav=editor.findChild<QListWidget *>("presetEditorNavigation");int back=0;editor.backRequested=[&]{++back;};nav->setCurrentRow(2);
            expect(editor.isDirty()&&back==0,"internal page switches retain edits without leaving");
            editor.findChild<QPushButton *>("presetEditorRestore")->click();
            expect(!editor.isDirty()&&editor.document().value("name")=="Original","restore reloads the last saved document");
            bool draft=false;editor.saveRequested=[&](bool value){draft=value;};editor.findChild<QPushButton *>("presetEditorSaveDraft")->click();
            expect(draft,"draft button emits a draft save rather than publishing");
            editor.findChild<QPushButton *>("presetEditorClear")->click();
            expect(editor.document().value("name")=="Original","clear preserves the preset name");
            QJsonObject packs{{"id","pack-test"},{"name","Packs"},{"sections",QJsonArray{QJsonObject{{"id","resourcePacks"},{"kind","genericFiles"},{"items",QJsonArray{QJsonObject{{"path","resourcepacks/low.zip"},{"enabled",true},{"priority",0}},QJsonObject{{"path","resourcepacks/high.zip"},{"enabled",true},{"priority",1}}}}}}}};
            PresetEditorPage packEditor(packs);
            auto *packTable=packEditor.findChild<QTableWidget *>("presetEditorFiles_resourcePacks");
            expect(packTable&&packTable->item(0,0)->text().endsWith("high.zip"),"resource pack editor displays highest priority first");
            const auto storedPacks=packEditor.document().value("sections").toArray().first().toObject().value("items").toArray();
            expect(storedPacks.first().toObject().value("priority").toInt()>storedPacks.last().toObject().value("priority").toInt(),"saving the resource pack editor preserves effective pack priority");
            PresetEditorPage metadataEditor(QJsonObject{{"id","metadata"},{"name","Metadata"},{"sections",QJsonArray{QJsonObject{{"id","original-section"},{"kind","vanillaOptions"},{"mode","replace"},{"items",QJsonArray{QJsonObject{{"key","guiScale"},{"value","2"},{"extra","retained"}}}}}}}});
            const auto metadata=metadataEditor.document().value("sections").toArray().first().toObject();
            expect(metadata.value("mode")=="replace"&&metadata.value("items").toArray().first().toObject().value("extra")=="retained","editing preserves existing section policy and item metadata");
            PresetInstallPicker picker;
            PresetInstallPicker::Category game;game.key="gameSettings";game.title="Game";game.options.append({"game-child","Composite",true,true});
            PresetInstallPicker::Category mods;mods.key="mods";mods.title="Mods";mods.options.append({"mod-child","Composite",true,true});
            picker.setCategories({game,mods});picker.setComposites({{"parent","Composite",{{"gameSettings","game-child"},{"mods","mod-child"}}}});
            picker.findChild<QComboBox *>("presetCompositeCombo")->setCurrentIndex(1);
            expect(picker.selectedId("gameSettings")=="game-child"&&picker.selectedId("mods")=="mod-child","composite selection maps to independent category parts");
            picker.findChild<QCheckBox *>("presetCompositePart_mods")->setChecked(false);
            expect(picker.selectedId("mods").isEmpty()&&picker.selectedId("gameSettings")=="game-child","unchecking a composite part leaves other parts selected");
        }
        return failures == 0 ? 0 : 1;
    
 return 0;
}

