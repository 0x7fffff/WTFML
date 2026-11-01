#include "Support.hpp"
int UiBeaconBrewingChecks() {
    // ------------------------------------------------------------------
    // 这两件和原来那五件同一个形状：控件只收输入、发信号，算的过程在 controller 那边
    // （纯算术在 `src/tools/Beacon` / `src/tools/Brewing`，判据在 verify 的 ZAI 一节）。
    // 这里只管**页面上那几格**：表里认得到、默认值是什么、改一下发的是哪几个参数、
    // 程序摆值时不发信号（否则 controller 会在填表途中白算一遍）。
    // ------------------------------------------------------------------
    {
        ToolboxPage page;
        check(ToolboxPage::tools().size() == 10,
              "★★ U39 工具箱那张表在 U39 之后又加了两件（这里只认「信标 / 酿造确实在表里」）",
              QString::number(ToolboxPage::tools().size()));

        int chips = 0;
        for (ToolboxPage::Tool tool : ToolboxPage::tools()) {
            if (tool == ToolboxPage::Tool::Download) {
                continue;  // 高速下载不出 chip（它开独立小窗）
            }
            if (page.findChild<QPushButton *>(ToolboxPage::toolChipName(tool)) != nullptr) {
                ++chips;
            }
        }
        check(chips == 9,
              "★ U39 chip 的颗数 = 表里除高速下载之外的九件",
              QString::number(chips));

        auto *beaconFrom = page.findChild<QSpinBox *>(ToolboxPage::beaconFromSpinName());
        auto *beaconLevel = page.findChild<QSpinBox *>(ToolboxPage::beaconLevelSpinName());
        auto *beaconResult = page.findChild<QLabel *>(ToolboxPage::beaconResultName());
        check(beaconFrom != nullptr && beaconLevel != nullptr && beaconResult != nullptr,
              "★★ U39 信标那一片三格都在（已有 / 目标 / 结果）");
        if (beaconFrom != nullptr && beaconLevel != nullptr && beaconResult != nullptr) {
            check(beaconFrom->minimum() == 0 && beaconFrom->maximum() == 4
                      && beaconLevel->minimum() == 1 && beaconLevel->maximum() == 4
                      && beaconFrom->value() == 0 && beaconLevel->value() == 4,
                  "★★ U39 两格转盘的取值范围：已有 0–4（0 = 还没搭）、目标 1–4；默认 0 → 4",
                  QStringLiteral("%1–%2 / %3–%4")
                      .arg(beaconFrom->minimum())
                      .arg(beaconFrom->maximum())
                      .arg(beaconLevel->minimum())
                      .arg(beaconLevel->maximum()));

            int beaconSignals = 0;
            int lastFrom = -1;
            int lastLevel = -1;
            QObject::connect(&page, &ToolboxPage::beaconRequested, &page,
                             [&beaconSignals, &lastFrom, &lastLevel](int from, int level, int, int) {
                                 ++beaconSignals;
                                 lastFrom = from;
                                 lastLevel = level;
                             });
            page.setBeaconLevels(2, 3);
            check(beaconSignals == 0 && beaconFrom->value() == 2 && beaconLevel->value() == 3,
                  "★★ U39 `setBeaconLevels()` 只摆两格、**不发信号**（程序摆的不算用户改了）",
                  QString::number(beaconSignals));
            beaconLevel->setValue(4);
            check(beaconSignals == 1 && lastFrom == 2 && lastLevel == 4,
                  "★ U39 用户把目标改成 4 ⇒ 发一发，带的是两格的当前值",
                  QStringLiteral("%1 发（%2→%3）").arg(beaconSignals).arg(lastFrom).arg(lastLevel));

            // 后补的两格：底座方块（材质）与共用底座的颗数。
            auto *beaconMaterial =
                page.findChild<QComboBox *>(ToolboxPage::beaconMaterialComboName());
            auto *beaconCount = page.findChild<QSpinBox *>(ToolboxPage::beaconCountSpinName());
            check(beaconMaterial != nullptr && beaconCount != nullptr,
                  "★★ U39 信标多出来的两格都在（底座方块 / 信标颗数）");
            if (beaconCount != nullptr) {
                check(beaconCount->minimum() == 1 && beaconCount->maximum() == 9
                          && beaconCount->value() == 1,
                      "★ U39 颗数 1–9（顶面 3 × 3 最多 9 颗）、默认 1",
                      QStringLiteral("%1–%2")
                          .arg(beaconCount->minimum())
                          .arg(beaconCount->maximum()));
                beaconSignals = 0;
                beaconCount->setValue(2);
                check(beaconSignals == 1,
                      "★ U39 改颗数也发一发（不是只有那两格转盘才算「用户改了」）",
                      QString::number(beaconSignals));
            }
            page.setBeaconResult(QStringLiteral("4 级信标：范围 50 格"));
            check(beaconResult->text() == QStringLiteral("4 级信标：范围 50 格"),
                  "★ U39 结果那一格逐字摆进来（怎么算的不在这一层）");
        }

        auto *brewingPotion =
            page.findChild<QComboBox *>(ToolboxPage::brewingPotionComboName());
        auto *brewingExtended =
            page.findChild<QCheckBox *>(ToolboxPage::brewingExtendedName());
        auto *brewingLevel2 = page.findChild<QCheckBox *>(ToolboxPage::brewingLevel2Name());
        auto *brewingForm = page.findChild<QComboBox *>(ToolboxPage::brewingFormComboName());
        auto *brewingResult = page.findChild<QLabel *>(ToolboxPage::brewingResultName());
        check(brewingPotion != nullptr && brewingExtended != nullptr
                  && brewingLevel2 != nullptr && brewingForm != nullptr
                  && brewingResult != nullptr,
              "★★ U39 酿造那一片五格都在（药水 / 延时 / II 级 / 形态 / 结果）");
        if (brewingPotion != nullptr && brewingExtended != nullptr && brewingLevel2 != nullptr
            && brewingForm != nullptr && brewingResult != nullptr) {
            page.setBrewingPotions({QStringLiteral("迅捷"), QStringLiteral("抗火")});
            page.setBrewingForms({QStringLiteral("普通"), QStringLiteral("喷溅型"),
                                  QStringLiteral("滞留型")});
            check(brewingPotion->count() == 2 && brewingPotion->itemText(1) == QStringLiteral("抗火")
                      && brewingForm->count() == 3
                      && brewingForm->itemText(2) == QStringLiteral("滞留型"),
                  "★ U39 两张表由 controller 摆进来（这一页不自带一份药水清单）",
                  QStringLiteral("%1 种药水 / %2 种形态")
                      .arg(brewingPotion->count())
                      .arg(brewingForm->count()));

            int brewSignals = 0;
            int lastPotion = -1;
            bool lastExtended = false;
            bool lastLevel2 = false;
            int lastForm = -1;
            int lastBottles = -1;
            QObject::connect(&page, &ToolboxPage::brewingRequested, &page,
                             [&](int potion, bool extended, bool level2, int form, int bottles) {
                                 ++brewSignals;
                                 lastPotion = potion;
                                 lastExtended = extended;
                                 lastLevel2 = level2;
                                 lastForm = form;
                                 lastBottles = bottles;
                             });
            brewingPotion->setCurrentIndex(1);
            check(brewSignals == 1 && lastPotion == 1 && !lastExtended && !lastLevel2
                      && lastForm == 0,
                  "★★ U39 换一种药水 ⇒ 发（新下标, 两个勾, 形态下标）",
                  QString::number(brewSignals));
            brewingExtended->setChecked(true);
            brewingExtended->setChecked(false);
            check(brewSignals == 3 && !lastExtended,
                  "★★ U39 两个勾各发一发（勾上 / 取消都算 —— 取消之后要把不用红石的那条路线摆回来）",
                  QString::number(brewSignals));
            brewingLevel2->setChecked(true);
            check(brewSignals == 4 && lastLevel2,
                  "★ U39 II 级那个勾也发自己的", QString::number(brewSignals));
            brewingForm->setCurrentIndex(2);
            check(brewSignals == 5 && lastForm == 2,
                  "★ U39 形态换成滞留型 ⇒ 发的下标是 2（controller 靠它补火药 + 龙息两步）",
                  QString::number(lastForm));

            // 补的「瓶数」那一格。
            auto *brewingBottles =
                page.findChild<QSpinBox *>(ToolboxPage::brewingBottlesSpinName());
            check(brewingBottles != nullptr && brewingBottles->minimum() == 1
                      && brewingBottles->maximum() == 64 && brewingBottles->value() == 1,
                  "★★ U39 瓶数是 1–64 的转盘、默认 1",
                  brewingBottles == nullptr
                      ? QString()
                      : QStringLiteral("%1–%2")
                            .arg(brewingBottles->minimum())
                            .arg(brewingBottles->maximum()));
            if (brewingBottles != nullptr) {
                brewingBottles->setValue(6);
                check(brewSignals == 6 && lastBottles == 6,
                      "★ U39 改瓶数也发一发（材料与酿造次数跟着变）",
                      QString::number(brewSignals));
            }

            // 后补的「目标版本」那一格：版本表由 controller 摆进来，程序摆值不发信号。
            auto *brewingVersion =
                page.findChild<QComboBox *>(ToolboxPage::brewingVersionComboName());
            check(brewingVersion != nullptr,
                  "★★ U39 酿造多出来的「目标版本」那一格在");
            if (brewingVersion != nullptr) {
                brewSignals = 0;
                page.setBrewingVersions(
                    {QString(), QStringLiteral("1.20.4"), QStringLiteral("1.21")});
                check(brewSignals == 0 && brewingVersion->count() == 3
                          && brewingVersion->itemText(0) == QStringLiteral("不限版本")
                          && brewingVersion->itemData(0).toString().isEmpty()
                          && brewingVersion->currentData().toString().isEmpty(),
                      "★★ U39 `setBrewingVersions()` 把空串那一项摆成「不限版本」、**不发信号**",
                      QString::number(brewSignals));
                brewingVersion->setCurrentIndex(2);
                check(brewSignals == 1 && page.brewingVersion() == QStringLiteral("1.21"),
                      "★ U39 换目标版本也发一发（controller 拿它判这一瓶在不在）",
                      QStringLiteral("%1 发 / %2")
                          .arg(brewSignals)
                          .arg(page.brewingVersion()));
            }
            page.setBrewingResult(QStringLiteral("一共 4 步"));
            check(brewingResult->text() == QStringLiteral("一共 4 步"),
                  "★ U39 酿造结果那一格逐字摆进来");
        }

        // 切到新加那两件：`showTool()` 真的换到它们各自那一片（不是只换了 chip 的勾）。
        page.showTool(ToolboxPage::Tool::Beacon);
        // 显隐落在 QStackedWidget 那一页（面板外面的滚动容器）上 —— 面板是它的子控件，
        // 拿 `panel->isHidden()` 量「前一片收起来」永远是绿的。
        auto *beaconPanel =
            page.findChild<QWidget *>(ToolboxPage::toolPanelName(ToolboxPage::Tool::Beacon));
        auto *brewPanel =
            page.findChild<QWidget *>(ToolboxPage::toolPanelName(ToolboxPage::Tool::Brewing));
        auto *beaconPage =
            page.findChild<QWidget *>(ToolboxPage::toolPageName(ToolboxPage::Tool::Beacon));
        auto *brewPage =
            page.findChild<QWidget *>(ToolboxPage::toolPageName(ToolboxPage::Tool::Brewing));
        check(page.currentTool() == ToolboxPage::Tool::Beacon && beaconPanel != nullptr
                  && brewPanel != nullptr && beaconPage != nullptr && brewPage != nullptr
                  && !beaconPage->isHidden(),
              "★★ U39 `showTool(Beacon)` 换到信标那一片（chip 与 QStackedWidget 的次序同源）");
        page.showTool(ToolboxPage::Tool::Brewing);
        check(page.currentTool() == ToolboxPage::Tool::Brewing && !brewPage->isHidden()
                  && beaconPage->isHidden(),
              "★★ U39 再换到酿造那一片：前一片收起来、这一片露出来");
        page.close();
        QApplication::processEvents();
    }
    return 0;
}


