#include "Support.hpp"
void runThemeControlsTests() {
    const QString originalDir = AppPaths::minecraftDir();
    const Theme::Spec themeBefore = Theme::current();
    const QPalette paletteBefore = QApplication::palette();
    QTemporaryDir root;
    check(root.isValid(), "T39.0 临时数据根目录建出来了");
    if (!root.isValid()) {
        return;
    }

    QSettings sandbox;
    const QVariant foldersBefore = sandbox.value(QStringLiteral("Launcher/Folders"));
    const QVariant currentBefore = sandbox.value(QStringLiteral("Launcher/CurrentFolder"));
    const QVariant animationsBefore = sandbox.value(QStringLiteral("Look/Animations"));
    const bool animationsEnabledBefore = Anim::userEnabled();
    const auto restore = [&] {
        sandbox.setValue(QStringLiteral("Launcher/Folders"), foldersBefore);
        sandbox.setValue(QStringLiteral("Launcher/CurrentFolder"), currentBefore);
        // 动效那一把开关是这一节自己拨过的（沙盒 INI 每一跑都是新删过的，"整组删掉" =
        // 逐字还原）。
        if (animationsBefore.isValid()) {
            sandbox.setValue(QStringLiteral("Look/Animations"), animationsBefore);
        } else {
            sandbox.remove(QStringLiteral("Look/Animations"));
        }
        sandbox.remove(QStringLiteral("Theme"));
        sandbox.sync();
        // 主题要**连调色板一起**铺回去：这一节把预设拨到过深色，光 `setSpec()` 是收不回
        // 来的（深色那一份还挂在应用上，后面每一节量的都会是深底）。
        Theme::setSpec(themeBefore);
        Theme::apply(*qApp);
        Anim::setPlatformReducedMotion(false);
        Anim::setEnabled(animationsEnabledBefore);
        AppPaths::setMinecraftDir(originalDir);
    };
    sandbox.setValue(QStringLiteral("Launcher/Folders"), QStringList({root.path()}));
    sandbox.setValue(QStringLiteral("Launcher/CurrentFolder"), root.path());
    sandbox.sync();
    AppPaths::setMinecraftDir(root.path());

    {
        MainWindow w;
        w.show();
        QApplication::processEvents();

        MainWindowUI *ui = w.ui();
        check(ui != nullptr, "T39.1 设置页在（这一节整段都要用它的主题那一节）");
        if (ui == nullptr) {
            restore();
            return;
        }

        auto *presetCombo = byObjectName<QComboBox>(&w, MainWindowUI::themePresetComboName());
        auto *accentSwatch = byObjectName<QPushButton>(&w, MainWindowUI::themeAccentButtonName());
        auto *opacitySlider =
            byObjectName<QSlider>(&w, MainWindowUI::themeTextOpacitySliderName());
        auto *opacityPercent =
            byObjectName<QLabel>(&w, MainWindowUI::themeTextOpacityPercentName());
        auto *animationsCheck = byObjectName<QCheckBox>(&w, MainWindowUI::animationsCheckName());
        QVector<QPushButton *> semanticSwatches;
        for (int i = 0; i < MainWindowUI::themeSemanticCount(); ++i) {
            semanticSwatches.append(
                byObjectName<QPushButton>(&w, MainWindowUI::themeSemanticButtonName(i)));
        }
        int foundSwatches = 0;
        for (QPushButton *swatch : semanticSwatches) {
            if (swatch != nullptr) {
                ++foundSwatches;
            }
        }
        check(presetCombo != nullptr && accentSwatch != nullptr && opacitySlider != nullptr &&
                  opacityPercent != nullptr && animationsCheck != nullptr &&
                  foundSwatches == MainWindowUI::themeSemanticCount(),
              "T39.1 这一节那几颗控件都按 objectName 找得回来",
              QStringLiteral("语义色块 %1/%2")
                  .arg(foundSwatches)
                  .arg(MainWindowUI::themeSemanticCount()));
        if (presetCombo == nullptr || accentSwatch == nullptr || opacitySlider == nullptr ||
            animationsCheck == nullptr) {
            restore();
            return;
        }

        // ---------- ① 构造完那一刻：控件照着规格画出来了 ----------
        //
        // 这一条量的正是 `refreshThemeControls()` 那一发的产物 —— 出厂那一份下主题色是
        // 出厂那支绿、四个语义色是**按当前深浅现推**的那四个，预设那一格停在 "system"。
        //
        // 四块语义色**必须真的被涂过**（`wtfmlSwatchColor` 那条动态属性是
        // `Theme::markColorSwatch()` 挂的）：不涂的话它们是四颗空白按钮，而屏幕上只是
        // "四块白方块"，不报错也不崩（ui_check 那边量得到形状，涂色这一步只有这里有
        // `MainWindow` 才看得见）。
        const auto swatchColor = [](QPushButton *swatch) {
            return swatch == nullptr ? QColor()
                                     : swatch->property("wtfmlSwatchColor").value<QColor>();
        };
        const auto sameColor = [](const QColor &a, const QColor &b) {
            return a.isValid() && b.isValid() &&
                   a.name(QColor::HexArgb) == b.name(QColor::HexArgb);
        };
        const QPalette palette = QApplication::palette();
        const QColor expectedSemantic[] = {
            Theme::dangerColor(palette), Theme::warningColor(palette), Theme::okColor(palette),
            Theme::errorTextColor(palette)};
        int paintedOk = 0;
        for (int i = 0; i < semanticSwatches.size(); ++i) {
            if (sameColor(swatchColor(semanticSwatches.at(i)), expectedSemantic[i])) {
                ++paintedOk;
            }
        }
        check(paintedOk == MainWindowUI::themeSemanticCount() &&
                  sameColor(swatchColor(accentSwatch), Theme::current().accent),
              "★★ T39.2 建完那一刻五块色板都**涂上了色**：主题色是规格里那一支、四个语义色是"
              "按当前深浅现推的那四个（没涂的话它们是四颗空白按钮 —— 不报错、不崩）",
              QStringLiteral("对了 %1/%2 块；主题色那块 %3 / 要 %4")
                  .arg(paintedOk)
                  .arg(MainWindowUI::themeSemanticCount())
                  .arg(swatchColor(accentSwatch).name(), Theme::current().accent.name()));
        check(presetCombo->currentData().toString() == Theme::current().presetId &&
                  opacitySlider->value() == Theme::current().textOpacity &&
                  opacityPercent->text() ==
                      MainWindowUI::themePercentText(Theme::current().textOpacity),
              "T39.2 预设那一格停在规格里那一档、不透明度那根条和右边那个百分比也是",
              QStringLiteral("%1 / %2 / %3")
                  .arg(presetCombo->currentData().toString())
                  .arg(opacitySlider->value())
                  .arg(opacityPercent->text()));

        // ---------- ② 拨预设 → 规格 + 盘 + 界面三样一起动 ----------
        //
        // 这是这一节最要紧的一条：`MainWindow` 那一侧要是漏了"落盘"或者"铺开"，症状都是
        // "改了没生效 / 下次启动又变回去"，而屏幕上看着一切正常。
        //
        // 这一条里"调色板深吗"那半**必须真机跑**（这个工程平时就是真机跑的，别加
        // `QT_QPA_PLATFORM=offscreen`）：深浅那一档是 `styleHints()->setColorScheme()`
        // 往下传的，而 **offscreen 插件不认这一档**（实测：`standardPalette()` 照旧是浅的）
        // —— 在 offscreen 底下跑会得到一条看起来像产品坏了的**假红**。
        const int darkIndex = presetCombo->findData(QStringLiteral("dark"));
        check(darkIndex >= 0, "T39.3 配色那格里找得到「深色」那一档");
        if (darkIndex >= 0) {
            presetCombo->setCurrentIndex(darkIndex);
            QApplication::processEvents();
            QSettings written;
            // CTest 为了避免弹出真窗口固定使用 offscreen，而该平台插件不实现
            // styleHints()->setColorScheme()（上面的注释已说明）。规格和落盘在任何
            // 平台都必须验；调色板只在真正支持系统深浅提示的平台上验，避免伪失败。
            const bool canApplyColorScheme =
                QGuiApplication::platformName() != QLatin1String("offscreen");
            const bool paletteMatches = Theme::isDarkPalette(QApplication::palette());
            check(Theme::current().presetId == QStringLiteral("dark") &&
                      (!canApplyColorScheme || paletteMatches) &&
                      written.value(QStringLiteral("Theme/Preset")).toString() ==
                          QStringLiteral("dark"),
                  "★★ T39.3 拨一下「深色」：内存里那份规格、应用调色板、盘上那一位**三样"
                  "一起变**（少了任何一样都是「改了没生效」或「下次启动又变回去」）",
                  QStringLiteral("规格 %1 / 调色板深吗 %2 / 盘上 %3")
                      .arg(Theme::current().presetId)
                      .arg(paletteMatches ? 1 : 0)
                      .arg(written.value(QStringLiteral("Theme/Preset")).toString()));
            // 深色下那四个语义色**当场换成另一套**（`dangerColor()` 是按参数那份调色板算的），
            // 而色板是**画的**、不会自己跟着调色板走 ⇒ 得由 `refreshThemeControls()` 重涂。
            // 漏了这一涂，屏幕上就是"深色主题下那四块还是浅色档的红黄绿"。
            const QPalette darkPalette = QApplication::palette();
            const QColor darkDanger = Theme::dangerColor(darkPalette);
            check(sameColor(swatchColor(semanticSwatches.value(0)), darkDanger),
                  "★★ T39.3 换成深色之后那四块色板也**重涂过**（深底上要用深底那一档的红"
                  "—— 色块是画上去的，不跟着调色板走）",
                  QStringLiteral("块上是 %1 / 深底那一档是 %2")
                      .arg(swatchColor(semanticSwatches.value(0)).name(), darkDanger.name()));
            // 覆盖项叠加在预设上面：拨预设**不许**把用户自己改的那几项抹掉。
            QMetaObject::invokeMethod(&w, "onThemeAccentResetClicked", Qt::DirectConnection);
            QApplication::processEvents();
            check(presetCombo->currentData().toString() == QStringLiteral("dark") &&
                      Theme::current().presetId == QStringLiteral("dark"),
                  "★ T39.3 「主题色 → 恢复默认」只把那一个色还回去，**预设还是深色**"
                  "（覆盖项是叠在预设上面的，不是二选一）",
                  Theme::current().presetId);
        }

        // ---------- ③ 拖不透明度 → 规格 + 盘 + 那格百分比 ----------
        opacitySlider->setValue(60);
        QApplication::processEvents();
        {
            QSettings written;
            check(Theme::current().textOpacity == 60 &&
                      written.value(QStringLiteral("Theme/TextOpacity")).toInt() == 60 &&
                      opacityPercent->text() == MainWindowUI::themePercentText(60),
                  "★★ T39.4 拖不透明度：规格 / 盘上那一位 / 右边那格百分比三样一起跟着走",
                  QStringLiteral("规格 %1 / 盘上 %2 / 那格「%3」")
                      .arg(Theme::current().textOpacity)
                      .arg(written.value(QStringLiteral("Theme/TextOpacity")).toInt())
                      .arg(opacityPercent->text()));
        }

        // ---------- ④ 「恢复默认」把它们还回去 ----------
        //
        // 两条路各一句，都是**按名字调那一发槽**（产品里它们是那两颗按钮的槽，底下没有
        // 需要用户输入的东西，所以这一发就是用户点下去同一发）。
        QMetaObject::invokeMethod(&w, "onThemeAccentResetClicked", Qt::DirectConnection);
        QApplication::processEvents();
        const QColor presetBrand = Theme::tokens(Theme::current().presetId).primary;
        check(sameColor(Theme::current().accent, presetBrand) &&
                  sameColor(swatchColor(accentSwatch), presetBrand),
              "★★ T39.5 「主题色 → 恢复默认」：规格回到**当前预设那一支**，那块色也**当场重涂**"
              "（预设照旧是深色，不跟着一起跳回出厂）",
              QStringLiteral("规格 %1 / 块上 %2 / 预设那一支 %3")
                  .arg(Theme::current().accent.name(), swatchColor(accentSwatch).name(),
                       presetBrand.name()));

        // 语义色：先把四个色分别点成"用户挑过的"（直接改规格 + 重画，就是 `pickColor()`
        // 之后那一发），再按「跟随预设」把它们清掉。
        {
            Theme::Spec spec = Theme::current();
            spec.danger = QColor(0x10, 0x20, 0x30);
            spec.warning = QColor(0x40, 0x50, 0x60);
            spec.ok = QColor(0x70, 0x80, 0x90);
            spec.errorText = QColor(0xa0, 0xb0, 0xc0);
            Theme::setSpec(spec);
            Theme::save(Theme::current());
            Theme::apply(*qApp);
            check(QMetaObject::invokeMethod(&w, "refreshThemeControls", Qt::DirectConnection),
                  "T39.6 那一发重画调得动（产品里灌值和每次改完调的就是它）");
            check(sameColor(swatchColor(semanticSwatches.value(0)), QColor(0x10, 0x20, 0x30)) &&
                      sameColor(swatchColor(semanticSwatches.value(3)), QColor(0xa0, 0xb0, 0xc0)),
                  "★ T39.6 点名过的语义色，色板上就是那一个（不是「按现在深浅现推」那个）",
                  swatchColor(semanticSwatches.value(0)).name());

            QMetaObject::invokeMethod(&w, "onThemeSemanticResetClicked", Qt::DirectConnection);
            QApplication::processEvents();
            const Theme::Spec &afterReset = Theme::current();
            const QPalette nowPalette = QApplication::palette();
            check(!afterReset.danger.isValid() && !afterReset.warning.isValid() &&
                      !afterReset.ok.isValid() && !afterReset.errorText.isValid() &&
                      sameColor(swatchColor(semanticSwatches.value(0)),
                                Theme::dangerColor(nowPalette)),
                  "★★ T39.6 「跟随预设」把四项清成**没点名**（不是「设成今天这个色」——"
                  "那样换了深浅它就跟不动了），色板当场回到按当前深浅现推的那一套",
                  QStringLiteral("规格里还有几个：%1")
                      .arg((afterReset.danger.isValid() ? 1 : 0) +
                           (afterReset.warning.isValid() ? 1 : 0) +
                           (afterReset.ok.isValid() ? 1 : 0) +
                           (afterReset.errorText.isValid() ? 1 : 0)));
        }

        // ---------- ⑤ 动效那个总开关（明说要继续保留） ----------
        const bool wasChecked = animationsCheck->isChecked();
        const bool wasUserEnabled = Anim::userEnabled();
        check(wasChecked == wasUserEnabled,
              "T39.7 那个勾建出来的时候就是照着 `Anim::userEnabled()` 勾的",
              QStringLiteral("勾着 %1 / 那一位 %2")
                  .arg(wasChecked ? 1 : 0)
                  .arg(wasUserEnabled ? 1 : 0));
        animationsCheck->setChecked(!wasChecked);
        QApplication::processEvents();
        {
            QSettings written;
            const bool flipped =
                Anim::userEnabled() == !wasChecked &&
                written.value(QStringLiteral("Look/Animations"), wasChecked).toBool() ==
                    !wasChecked;
            animationsCheck->setChecked(wasChecked);
            QApplication::processEvents();
            const bool backOk = Anim::userEnabled() == wasUserEnabled;
            check(flipped && backOk,
                  "★★ T39.7 动效那个勾：拨到另一档就落到那一位上、也落了盘（下次启动还是"
                  "用户选的那一档）；拨回来照样",
                  QStringLiteral("拨过去 %1 / 拨回来 %2")
                      .arg(flipped ? QStringLiteral("对") : QStringLiteral("**不对**"))
                      .arg(backOk ? QStringLiteral("对") : QStringLiteral("**不对**")));
        }

        restore();
        check(Theme::current().presetId == themeBefore.presetId &&
                  Theme::current().textOpacity == themeBefore.textOpacity &&
                  AppPaths::minecraftDir() == originalDir &&
                  QApplication::palette() == paletteBefore,
              "T39.8 收尾：主题规格 / 调色板 / 数据根目录都还原了（这一节不留痕迹）",
              QStringLiteral("预设 %1（该是 %2）")
                  .arg(Theme::current().presetId, themeBefore.presetId));
    }
}

