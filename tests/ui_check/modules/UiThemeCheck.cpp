#include "Support.hpp"
#include "ThemeAdvancedEditor.hpp"
#include "Glass.hpp"
#include <QToolButton>
#include <QSpinBox>
int UiThemeCheck(QApplication &app) {

        Anim::setEnabled(false);
        QTemporaryDir settingsDir(QDir::current().filePath(QStringLiteral(".theme-check-XXXXXX")));
        if (!settingsDir.isValid()) return 2;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
        app.setOrganizationName(QStringLiteral("WTFMLThemeCheck"));
        app.setApplicationName(QStringLiteral("ThemeCheck"));
        app.setStyle(Theme::createStyle());
        const QString fontPath = QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts/msyh.ttc"));
        const int fontId = QFile::exists(fontPath) ? QFontDatabase::addApplicationFont(fontPath) : -1;
        const QStringList fontFamilies = QFontDatabase::applicationFontFamilies(fontId);
        app.setFont(QFont(fontFamilies.isEmpty() ? QStringLiteral("Microsoft YaHei UI") : fontFamilies.first(), 9));
        int failures = 0;
        auto expect = [&failures](bool ok, const char *name) {
            printf("%s %s\n", ok ? "PASS" : "FAIL", name);
            if (!ok) ++failures;
        };
        // 换配色不再重挂整份应用样式表 —— 样式表字符串必须与主题无关。
        // 这一条直接钉住那个代价（6800 个控件重挂一次要 1.9 秒）：三套预设铺完，
        // 应用身上挂的那一份必须还是同一串。
        const QString themeIndependentSheet = Theme::widgetStyleSheet();
        QString installedSheet;
        for (const QString &id : {QStringLiteral("emerald"), QStringLiteral("ocean"), QStringLiteral("dark")}) {
            Theme::setSpec(Theme::presetSpec(id));
            Theme::apply(app);
            expect(app.font().pixelSize() == 14, "application body text uses the shared 14 px scale");
            QFont bold = app.font(); bold.setBold(true);
            expect(Theme::textFont(bold, Theme::TextRole::Caption).pixelSize() == 13 &&
                   Theme::textFont(bold, Theme::TextRole::Body).pixelSize() == 14 &&
                   Theme::textFont(bold, Theme::TextRole::Heading).pixelSize() == 16 &&
                   Theme::textFont(bold, Theme::TextRole::Display).pixelSize() == 26 &&
                   Theme::textFont(bold, Theme::TextRole::Empty).pixelSize() == 22 &&
                   Theme::textFont(bold, Theme::TextRole::Caption).bold(),
                   "tokens-r2 type scale: aux 13 / body 14 / section 16 / title 26 (+ empty 22)");
            {
                // 手写体只给页标题与空状态大字；小节标题是系统字半粗。
                const QStringList hand{Theme::handFamily(Theme::HandFont::Kai), Theme::handFamily(Theme::HandFont::KaiLight),
                                       Theme::handFamily(Theme::HandFont::Script)};
                const auto isHand = [&hand](const QFont &f) { return !f.families().isEmpty() && hand.contains(f.families().first()); };
                const QFont plain = app.font();
                expect(!isHand(Theme::textFont(plain, Theme::TextRole::Caption)) && !isHand(Theme::textFont(plain, Theme::TextRole::Body)) &&
                           !isHand(Theme::textFont(plain, Theme::TextRole::Heading)) &&
                           Theme::textFont(plain, Theme::TextRole::Heading).weight() == QFont::DemiBold &&
                           (hand.first().isEmpty() || (isHand(Theme::textFont(plain, Theme::TextRole::Display)) &&
                                                       isHand(Theme::textFont(plain, Theme::TextRole::Empty)))),
                       "hand font only on Display / Empty roles; section headings are system semibold");
                QWidget host;
                QPushButton plainButton(QStringLiteral("Enter 发送 Fabric 1.21.4"), &host);
                plainButton.ensurePolished();
                expect(!isHand(plainButton.font()) && plainButton.font().pixelSize() == 14,
                       "buttons are polished to the system font at 14 px");
            }
            {
                // 着色 alpha 自检：出厂浓度下，亚克力 / 云母（系统背板区间）每一档都落在对比度要的最薄着色，
                // 正文、次要字在背板最暗、最亮两端都 ≥ 4.5:1。
                const auto inks = Glass::bodyInks();
                const QList<QColor> range = Glass::systemBackdropRange();
                const qreal need = Glass::minimumTintAlpha(Theme::tokens().page, inks, range, 0.0);
                const qreal got = Glass::minimumTintAlpha(Theme::tokens().page, inks, range, Glass::tintStrength() / 100.0);
                QColor layer = Theme::tokens().page;
                layer.setAlphaF(got);
                double worst = 21.0;
                for (const QColor &bg : range)
                    for (const auto &ink : inks)
                        worst = qMin(worst, Glass::contrast(Glass::over(ink.first, Glass::over(layer, bg)), Glass::over(layer, bg)));
                printf("INFO tint %s alpha=%.2f need=%.2f worst=%.2f\n", qPrintable(id), got, need, worst);
                expect(qFuzzyCompare(got, need) && worst >= 4.5,
                       "default tint strength never binds: system backdrop tint is the thinnest readable alpha");
            }
            if (installedSheet.isEmpty()) {
                installedSheet = app.styleSheet();
            }
            expect(app.styleSheet() == installedSheet, "theme switch does not reinstall the app stylesheet");
            expect(Theme::widgetStyleSheet() == themeIndependentSheet,
                   "widget style sheet is independent of the current preset");
            const auto t = Theme::tokens();
            expect(app.palette().color(QPalette::Window) == t.page, "page palette follows preset");
            expect(Theme::current().accent == t.primary, "brand follows preset");
            expect(Theme::partBackground(Theme::Part::TitleBar) == t.deep, "title uses deep brand");
            expect(Theme::partBackground(Theme::Part::BottomBar) == t.subtle, "bottom bar uses subtle surface");
            expect(Theme::selectionFill(t.card) == t.selected, "selection uses specified color");
            expect(Theme::widgetStyleSheet().contains(QStringLiteral("palette(base)")) &&
                       app.palette().color(QPalette::Base).name() == t.input.name(),
                   "inputs use theme colors");
            {
                QWidget focusHost;
                focusHost.resize(180, 80);
                QPushButton button(QStringLiteral("任务中心"), &focusHost);
                button.setGeometry(20, 20, 140, 40);
                Theme::markButton(&button, Theme::ButtonKind::Neutral);
                focusHost.show();
                focusHost.activateWindow();
                app.processEvents();
                button.clearFocus();
                QKeyEvent firstTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
                QApplication::sendEvent(&focusHost, &firstTab);
                expect(button.hasFocus() && button.property("wtfmlKeyboardFocus").toBool(),
                       "first Tab enters a visible control and displays focus from an empty focus state");
                button.clearFocus();
                QKeyEvent firstBacktab(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
                QApplication::sendEvent(&focusHost, &firstBacktab);
                expect(button.hasFocus(), "first Shift-Tab enters the reverse focus chain");
                button.setFocus(Qt::TabFocusReason);
                // 「键盘在用」这一档现在只认**真的按键**（见 Theme.cpp 里
                // `ButtonFocusVisibility` 那段 —— 藏起焦点控件时 Qt 也报 TabFocusReason，
                // 不能拿它当"用户按了 Tab"）。这里先送一发普通按键把这一档摆上；
                // 不送 Tab 键：那一发会真的触发焦点链移动，把被测按钮换掉。
                QKeyEvent keyboardInput(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
                QApplication::sendEvent(&button, &keyboardInput);
                QFocusEvent keyboardFocus(QEvent::FocusIn, Qt::TabFocusReason);
                QApplication::sendEvent(&button, &keyboardFocus);
                expect(button.property("wtfmlKeyboardFocus").toBool(), "keyboard focus shows button focus ring");
                const QImage keyboardShot = button.grab().toImage();
                const QPointF position(button.rect().center());
                const QPointF global(button.mapToGlobal(button.rect().center()));
                QMouseEvent press(QEvent::MouseButtonPress, position, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QMouseEvent release(QEvent::MouseButtonRelease, position, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(&button, &press);
                QApplication::sendEvent(&button, &release);
                const QImage mouseShot = button.grab().toImage();
                expect(!button.property("wtfmlKeyboardFocus").toBool(), "mouse activation clears focus ring even on an already focused button");
                expect(keyboardShot.pixelColor(0, keyboardShot.height() / 2) !=
                           mouseShot.pixelColor(0, mouseShot.height() / 2),
                       "mouse click removes the rendered focus border");
                expect(button.hasFocus() && (button.focusPolicy() & Qt::TabFocus),
                       "hiding mouse focus ring preserves real focus and keyboard accessibility");
                QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
                QApplication::sendEvent(&button, &tab);
                expect(button.property("wtfmlKeyboardFocus").toBool(), "Tab restores keyboard focus feedback after mouse use");

                // 设置页分段页签（自绘焦点环）：真机反馈"点完页签整条外面一圈绿框"。鼠标点完后焦点由 Qt
                // 从藏起来那一页挪过来，理由同样是 TabFocusReason —— 判据得是"最后一次输入是不是键盘"。
                SettingsParts::SlidingTabs segments(&focusHost);
                segments.addTab(QStringLiteral("启动"));
                segments.addTab(QStringLiteral("下载"));
                segments.move(20, 70);
                segments.show();
                app.processEvents();
                const QPoint at = segments.tabRect(1).center();
                QMouseEvent tabPress(QEvent::MouseButtonPress, QPointF(at), QPointF(segments.mapToGlobal(at)),
                                     Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(&segments, &tabPress);
                segments.setFocus(Qt::TabFocusReason);
                app.processEvents();
                const QImage segMouse = segments.grab().toImage();
                expect(segments.hasFocus() && !segments.property("wtfmlKeyboardFocus").toBool(),
                       "segmented tabs: focus moved in after a mouse click draws no outer ring");
                QKeyEvent segKey(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
                QApplication::sendEvent(&segments, &segKey);
                QFocusEvent segTabIn(QEvent::FocusIn, Qt::TabFocusReason);
                QApplication::sendEvent(&segments, &segTabIn);
                const QImage segKeyboard = segments.grab().toImage();
                expect(segments.property("wtfmlKeyboardFocus").toBool() &&
                           segKeyboard.pixelColor(2, segKeyboard.height() / 2) != segMouse.pixelColor(2, segMouse.height() / 2),
                       "segmented tabs: keyboard arrival still draws the focus ring (F1)");
            }
            ToggleSwitch toggle;
            toggle.resize(toggle.sizeHint());
            toggle.setChecked(true);
            toggle.show();
            app.processEvents();
            QEvent leave(QEvent::Leave);
            QApplication::sendEvent(&toggle, &leave);
            const QPoint trackPoint(3, toggle.height() / 2);
            expect(toggle.grab().toImage().pixelColor(trackPoint) ==
                       t.primary,
                   "switch on state uses the solid accent fill in every theme");
            if (id == QLatin1String("emerald")) {
                QEnterEvent enter(QPointF(3, 3), QPointF(3, 3), QPointF(3, 3));
                QApplication::sendEvent(&toggle, &enter);
                expect(toggle.grab().toImage().pixelColor(trackPoint) == t.primaryHover,
                       "emerald switch hover uses palette brand hover");
            }
            toggle.hide();
            QSettings().setValue(QStringLiteral("Look/LogColors"), true);
            GameLogWindow log(QStringLiteral("Theme check"));
            log.appendText(QStringLiteral("[main/INFO]: info\n[main/WARN]: warn\n[main/ERROR]: error\n[main/FATAL]: fatal\n"));
            auto *view = log.findChild<QPlainTextEdit *>();
            expect(view != nullptr && view->palette().color(QPalette::Base) == t.logBase, "log body follows preset");
            if (view) {
                QTextCursor cursor(view->document()->firstBlock());
                cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
                expect(cursor.charFormat().foreground().color() ==
                       (id == QLatin1String("dark") ? QColor("#79D4A5") : QColor("#202020")), "INFO text color");
                const QTextBlock warning = view->document()->firstBlock().next();
                expect(warning.blockFormat().background().color() ==
                       (id == QLatin1String("dark") ? QColor("#171E23") : QColor("#FFF0BF")), "WARN background color");
                QTextCursor error(warning.next());
                error.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
                expect(error.charFormat().foreground().color() ==
                       (id == QLatin1String("dark") ? QColor("#FFB8BD") : QColor("#202020")), "ERROR text color");
                QTextCursor fatal(warning.next().next());
                fatal.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
                expect(fatal.charFormat().foreground().color() ==
                       (id == QLatin1String("dark") ? QColor("#FF858B") : QColor("#202020")), "FATAL text color");
            }
            QSettings().setValue(QStringLiteral("Look/LogColors"), false);
            GameLogWindow mono(QStringLiteral("Monochrome"));
            mono.appendText(QStringLiteral("[main/ERROR]: error\n"));
            auto *monoView = mono.findChild<QPlainTextEdit *>();
            if (monoView) {
                QTextCursor cursor(monoView->document()->firstBlock());
                cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
                expect(cursor.charFormat().foreground().color() ==
                       (id == QLatin1String("dark") ? QColor("#D5DEE3") : QColor("#202020")), "monochrome text color");
                expect(monoView->document()->firstBlock().blockFormat().background().style() == Qt::NoBrush,
                       "monochrome clears level backgrounds");
            } else expect(false, "monochrome log view exists");
            Theme::save(Theme::current());
            expect(!QSettings().value(QStringLiteral("Look/LogColors"), true).toBool(),
                   "saving theme preserves log color preference");
            expect(Theme::load().presetId == id, "theme selection survives save/load");
            if (app.arguments().contains(QStringLiteral("--theme-preview"))) {
                const QString directory = QDir::current().filePath(QStringLiteral("build/theme-preview"));
                QDir().mkpath(directory);
                QMainWindow preview;
                MainWindowUI previewUi(&preview);
                MainWindowUI::FolderRow currentFolder;
                currentFolder.path = QStringLiteral("D:/Games/Minecraft");
                currentFolder.title = QStringLiteral("当前文件夹");
                currentFolder.subtitle = currentFolder.path;
                currentFolder.assistant = true;
                currentFolder.current = true;
                MainWindowUI::FolderRow moddedFolder;
                moddedFolder.path = QStringLiteral("E:/Minecraft/Modpacks");
                moddedFolder.title = QStringLiteral("模组整合包");
                moddedFolder.subtitle = moddedFolder.path;
                previewUi.setFolderRows({currentFolder, moddedFolder});
                preview.resize(1000, 650);
                preview.show();
                app.processEvents();
                expect(preview.grab().save(directory + QLatin1Char('/') + id + QStringLiteral("-home.png")),
                       "save home preview");
                previewUi.getTabWidget()->setCurrentIndex(3);
                app.processEvents();
                expect(preview.grab().save(directory + QLatin1Char('/') + id + QStringLiteral("-settings.png")),
                       "save settings preview");
                expect(preview.findChild<QListWidget *>(QStringLiteral("settingsCategoryList")) == nullptr &&
                           preview.findChild<QFrame *>(QStringLiteral("settingsSidebarSurface")) == nullptr &&
                           preview.findChild<SettingsParts::SlidingTabs *>(MainWindowUI::settingsTabsName()) != nullptr,
                       "settings has top tabs and no left index sidebar");
                for (const auto &entry : {qMakePair(MainWindowUI::instancesTabIndex(), QStringLiteral("-management.png")),
                                          qMakePair(MainWindowUI::downloadTabIndex(), QStringLiteral("-download.png"))}) {
                    previewUi.getTabWidget()->setCurrentIndex(entry.first);
                    app.processEvents();
                    for (auto *list : {preview.findChild<QListWidget *>(MainWindowUI::folderListName()),
                                       preview.findChild<QListWidget *>(MainWindowUI::downloadCategoryListName())}) {
                        expect(list && list->frameShape() == QFrame::NoFrame
                                   && list->palette().color(QPalette::Base) == t.main,
                               "sidebar list uses seamless theme surface without inner frame");
                    }
                    expect(preview.grab().save(directory + QLatin1Char('/') + id + entry.second), "save sidebar preview");
                }
                log.show();
                app.processEvents();
                expect(view && view->viewport()->palette().color(QPalette::Base) == t.logBase,
                       "visible log viewport keeps specified background");
                expect(log.grab().save(directory + QLatin1Char('/') + id + QStringLiteral("-log.png")),
                       "save log preview");
            }
        }
        {
            Theme::Spec edited;
            int emitted = 0;
            ThemeAdvancedEditor editor;
            QObject::connect(&editor, &ThemeAdvancedEditor::specEdited, &editor,
                [&](const Theme::Spec &spec) { edited = spec; ++emitted; });
            auto *roles = editor.findChild<QComboBox *>("themeRoleCombo");
            auto *parts = editor.findChild<QComboBox *>("themePartCombo");
            auto *opacity = editor.findChild<QSpinBox *>("themePartTextOpacity");
            auto *body = editor.findChild<QWidget *>("themeAdvancedBody");
            expect(roles && roles->count() == Theme::roleCount() && parts && parts->count() == Theme::partCount(),
                   "advanced editor exposes every supported role and part");
            expect(body && body->isHidden(), "advanced editor starts collapsed");
            auto *fold = editor.findChild<QAbstractButton *>("themeAdvancedToggle");
            // 折叠行（8 号反馈）：整行一颗按钮、标题在 text()、右边那颗小字写「展开 / 收起」，
            // 不再有 ▸ / ▾ 箭头，也不再是 QToolButton 那套系统外观。
            const auto foldState = [fold]() {
                const auto labels = fold->findChildren<QLabel *>(QString(), Qt::FindDirectChildrenOnly);
                for (auto *label : labels) {
                    if (label->text() == QStringLiteral("展开") || label->text() == QStringLiteral("收起")) {
                        return label->text();
                    }
                }
                return QString();
            };
            expect(fold && qobject_cast<QToolButton *>(fold) == nullptr &&
                       fold->text() == QStringLiteral("细分区域与控件颜色") && foldState() == QStringLiteral("展开"),
                   "fold row is one plain button with a 展开/收起 word and no arrow");
            fold->click();
            expect(!body->isHidden(), "advanced editor expands without opening a new page");
            expect(foldState() == QStringLiteral("收起"), "fold row switches its word to 收起 once open");
            editor.setSpec(Theme::defaultSpec());
            expect(emitted == 0, "refreshing advanced editor never writes theme settings");
            editor.setRoleColor(int(Theme::Role::Base), QColor("#123456"));
            expect(edited.roles.value(int(Theme::Role::Base)) == QColor("#123456"), "advanced role color edited");
            editor.setRoleColor(int(Theme::Role::Base), {});
            expect(!edited.roles.contains(int(Theme::Role::Base)), "role follows preset after reset");
            auto spec = Theme::defaultSpec();
            auto &style = spec.parts[int(Theme::Part::PageBackground)];
            style.hasImage = true; style.imagePath = "C:/preview/background.png";
            style.imageName = "background.png"; style.imageOpacity = 61;
            editor.setSpec(spec);
            parts->setCurrentIndex(parts->findData(int(Theme::Part::PageBackground)));
            editor.setPartColor(int(Theme::Part::PageBackground), false, QColor("#abcdef"));
            opacity->setValue(42);
            expect(edited.parts.value(int(Theme::Part::PageBackground)).textOpacity == 42, "part text opacity edited");
            editor.resetPart(int(Theme::Part::PageBackground));
            const auto preserved = edited.parts.value(int(Theme::Part::PageBackground));
            expect(!preserved.hasBackground && !preserved.hasForeground && preserved.textOpacity == -1 &&
                       preserved.hasImage && preserved.imageOpacity == 61 && preserved.imageName == "background.png",
                   "resetting part colors preserves background image and image opacity");
            parts->setCurrentIndex(parts->findData(int(Theme::Part::Scrim)));
            expect(!opacity->isEnabled() && !editor.findChild<QPushButton *>("themePartForeground")->isEnabled(),
                   "scrim has no misleading text controls");
            const int before = emitted;
            editor.setRoleColor(-1, Qt::red);
            editor.setPartColor(Theme::partCount(), false, Qt::red);
            expect(emitted == before, "invalid role and part cannot alter spec");
        }
        printf("Theme check: %d failure(s)\n", failures);
        return failures == 0 ? 0 : 1;
    
 return 0;
}

