#include "Support.hpp"
int UiBannerArmorChecks() {
    // ------------------------------------------------------------------
    // 这两件和 U39 那两件同一个形状：页面只收输入、发信号、摆结果；图案表、层数上限、
    // 材料账、海龟壳那一条限制全在 `src/tools/Banner` / `src/tools/ArmorTrim`（判据在
    // verify 的 ZAJ 一节）。这里钉的是**页面上那几格**：认得到、默认值、加了 / 挪了 /
    // 删了 / 撤了之后发的那一份 `图案:颜色` 列表对不对。
    // ------------------------------------------------------------------
    {
        ToolboxPage page;
        check(ToolboxPage::tools().size() == 10,
              "★★ U40 工具箱那张表现在是十件（加上旗帜设计 / 纹饰搭配）",
              QString::number(ToolboxPage::tools().size()));

        int cells = 0;
        for (ToolboxPage::Tool tool : ToolboxPage::tools()) {
            if (page.findChild<QPushButton *>(ToolboxPage::toolCellName(tool)) != nullptr) {
                ++cells;
            }
        }
        check(cells == 10, "★ U40 目录页有十格（每件工具一格，高速下载也在里面）",
              QString::number(cells));

        auto *base = page.findChild<QComboBox *>(ToolboxPage::bannerBaseComboName());
        auto *pattern = page.findChild<QComboBox *>(ToolboxPage::bannerPatternComboName());
        auto *color = page.findChild<QComboBox *>(ToolboxPage::bannerColorComboName());
        auto *add = page.findChild<QPushButton *>(ToolboxPage::bannerAddName());
        auto *list = page.findChild<QListWidget *>(ToolboxPage::bannerListName());
        auto *up = page.findChild<QPushButton *>(ToolboxPage::bannerUpName());
        auto *down = page.findChild<QPushButton *>(ToolboxPage::bannerDownName());
        auto *remove = page.findChild<QPushButton *>(ToolboxPage::bannerRemoveName());
        auto *undo = page.findChild<QPushButton *>(ToolboxPage::bannerUndoName());
        auto *bannerResult = page.findChild<QLabel *>(ToolboxPage::bannerResultName());
        check(base != nullptr && pattern != nullptr && color != nullptr && add != nullptr
                  && list != nullptr && up != nullptr && down != nullptr && remove != nullptr
                  && undo != nullptr && bannerResult != nullptr,
              "★★ U40 旗帜那一片十格都在（三张表 / 加一层 / 列表 / 四颗按钮 / 结果）");
        if (base != nullptr && pattern != nullptr && color != nullptr && add != nullptr
            && list != nullptr && up != nullptr && down != nullptr && remove != nullptr
            && undo != nullptr && bannerResult != nullptr) {
            int bannerSignals = 0;
            int lastBase = -1;
            QStringList lastLayers;
            QObject::connect(&page, &ToolboxPage::bannerRequested, &page,
                             [&](int baseColor, const QStringList &layers) {
                                 ++bannerSignals;
                                 lastBase = baseColor;
                                 lastLayers = layers;
                             });
            page.setBannerColors({QStringLiteral("红色"), QStringLiteral("白色")});
            page.setBannerPatterns({QStringLiteral("十字"), QStringLiteral("边框")});
            check(bannerSignals == 0 && base->count() == 2 && color->count() == 2
                      && pattern->count() == 2 && base->currentIndex() == 0,
                  "★★ U40 三张表由 controller 摆进来、**不发信号**（开页前 controller 自己算一次）",
                  QString::number(bannerSignals));

            add->click();
            check(bannerSignals == 1 && lastLayers == QStringList{QStringLiteral("0:0")}
                      && list->count() == 1,
                  "★★ U40 「加一层」把当前选的图案与颜色并进列表，发一份 `图案:颜色`",
                  lastLayers.join(QChar(',')));
            pattern->setCurrentIndex(1);
            color->setCurrentIndex(1);
            add->click();
            check(bannerSignals == 2
                      && lastLayers
                          == QStringList{QStringLiteral("0:0"), QStringLiteral("1:1")}
                      && list->count() == 2,
                  "★★ U40 换图案/颜色只改「下一层」，不改已经排好的那几层",
                  lastLayers.join(QChar(',')));

            list->setCurrentRow(0);
            check(!up->isEnabled() && down->isEnabled() && remove->isEnabled()
                      && undo->isEnabled(),
                  "★ U40 选中第一层 ⇒ 上移灰着、下移/删除/撤销能按");
            down->click();
            check(lastLayers == QStringList{QStringLiteral("1:1"), QStringLiteral("0:0")}
                      && list->currentRow() == 1,
                  "★★ U40 下移只交换相邻两层，选中跟着那一行走",
                  lastLayers.join(QChar(',')));
            remove->click();
            check(lastLayers == QStringList{QStringLiteral("1:1")} && list->count() == 1,
                  "★ U40 删除这一层", lastLayers.join(QChar(',')));
            undo->click();
            check(lastLayers == QStringList{QStringLiteral("1:1"), QStringLiteral("0:0")}
                      && list->count() == 2,
                  "★★ U40 撤销把上一步整份退回来（不是只退最后一层）",
                  lastLayers.join(QChar(',')));

            page.setBannerLayers({QStringLiteral("0:0")});
            check(page.bannerLayers() == QStringList{QStringLiteral("0:0")}
                      && list->count() == 1 && !undo->isEnabled(),
                  "★ U40 程序摆层不发信号、也不留撤销点（那是恢复草稿，不是用户动作）");
            check(bannerSignals == 5, "★ U40 程序摆层那一发没有多出来",
                  QString::number(bannerSignals));
            page.setBannerResult(QStringLiteral("底色 红色 · 1 层 / 6"));
            check(bannerResult->text() == QStringLiteral("底色 红色 · 1 层 / 6"),
                  "★ U40 结果那一格逐字摆进来（怎么算的不在这一层）");
        }

        auto *trimArmor = page.findChild<QComboBox *>(ToolboxPage::trimArmorComboName());
        auto *trimPart = page.findChild<QComboBox *>(ToolboxPage::trimPartComboName());
        auto *trimPattern = page.findChild<QComboBox *>(ToolboxPage::trimPatternComboName());
        auto *trimMaterial = page.findChild<QComboBox *>(ToolboxPage::trimMaterialComboName());
        auto *trimPieces = page.findChild<QSpinBox *>(ToolboxPage::trimPiecesSpinName());
        auto *trimVersion = page.findChild<QComboBox *>(ToolboxPage::trimVersionComboName());
        auto *trimResult = page.findChild<QLabel *>(ToolboxPage::trimResultName());
        check(trimArmor != nullptr && trimPart != nullptr && trimPattern != nullptr
                  && trimMaterial != nullptr && trimPieces != nullptr && trimResult != nullptr,
              "★★ U40 纹饰那一片六格都在（四张表 / 件数 / 结果）");
        if (trimArmor != nullptr && trimPart != nullptr && trimPattern != nullptr
            && trimMaterial != nullptr && trimPieces != nullptr && trimResult != nullptr) {
            page.setTrimArmorMaterials({QStringLiteral("皮革"), QStringLiteral("钻石")});
            page.setTrimParts({QStringLiteral("头盔"), QStringLiteral("胸甲")});
            page.setTrimPatterns({QStringLiteral("海岸"), QStringLiteral("哨兵")});
            page.setTrimMaterials({QStringLiteral("紫水晶"), QStringLiteral("铜")});
            check(trimVersion != nullptr,
                  "★★ U40 纹饰那一页有「目标版本」下拉框（与旗帜 / 酿造同一套版本表）");
            if (trimVersion != nullptr) {
                page.setTrimVersions({QString(), QStringLiteral("1.20"), QStringLiteral("1.21"),
                                      QStringLiteral("1.21.4")});
                check(trimVersion->count() == 4 && trimVersion->itemText(0) == QStringLiteral("不限版本")
                          && page.trimVersion().isEmpty(),
                      "★★ U40 版本表照摆：空串那一项写「不限版本」，默认就是它",
                      trimVersion->itemText(0));
            }
            check(trimPieces->minimum() == 1 && trimPieces->maximum() == 4
                      && trimPieces->value() == 1,
                  "★★ U40 件数 1–4、默认 1（整套四件自己填 4）",
                  QStringLiteral("%1–%2 默认 %3")
                      .arg(trimPieces->minimum())
                      .arg(trimPieces->maximum())
                      .arg(trimPieces->value()));

            int trimSignals = 0;
            int lastArmor = -1;
            int lastPart = -1;
            int lastPattern = -1;
            int lastMaterial = -1;
            int lastPieces = -1;
            bool lastWholeSet = false;
            QObject::connect(&page, &ToolboxPage::trimRequested, &page,
                             [&](int armorMaterial, int part, int patternIndex, int trimMaterialIndex,
                                 int pieces, bool wholeSet) {
                                 ++trimSignals;
                                 lastArmor = armorMaterial;
                                 lastPart = part;
                                 lastPattern = patternIndex;
                                 lastMaterial = trimMaterialIndex;
                                 lastPieces = pieces;
                                 lastWholeSet = wholeSet;
                             });
            trimArmor->setCurrentIndex(1);
            check(trimSignals == 1 && lastArmor == 1 && lastPart == 0 && lastPattern == 0
                      && lastMaterial == 0 && lastPieces == 1,
                  "★★ U40 换盔甲材质 ⇒ 五个参数一起发（这一层不裁「海龟壳只能头盔」那一刀）",
                  QString::number(trimSignals));
            trimPart->setCurrentIndex(1);
            trimPattern->setCurrentIndex(1);
            trimMaterial->setCurrentIndex(1);
            check(trimSignals == 4 && lastPart == 1 && lastPattern == 1 && lastMaterial == 1,
                  "★ U40 另外三张表各发各的", QString::number(trimSignals));
            trimPieces->setValue(4);
            check(trimSignals == 5 && lastPieces == 4,
                  "★ U40 件数改成 4 ⇒ 发（模板也要四份）", QString::number(lastPieces));

            // 补的「整套」那个勾。
            auto *trimWholeSet =
                page.findChild<QCheckBox *>(ToolboxPage::trimWholeSetName());
            check(trimWholeSet != nullptr, "★★ U40 「整套（四件）」那个勾在");
            if (trimWholeSet != nullptr) {
                trimWholeSet->setChecked(true);
                check(trimSignals == 6 && lastWholeSet && !trimPart->isEnabled()
                          && !trimPieces->isEnabled(),
                      "★★ U40 勾整套：发一发（wholeSet = true），部位与件数两格收起来",
                      QString::number(trimSignals));
                trimWholeSet->setChecked(false);
                check(trimSignals == 7 && !lastWholeSet && trimPart->isEnabled()
                          && trimPieces->isEnabled(),
                      "★ U40 取消整套：两格原样回来（不是偷偷忽略用户摆的设置）");
            }
            // 换目标版本也要重算（版本边界那一句提示在结果里）。
            if (trimVersion != nullptr) {
                trimVersion->setCurrentIndex(3);
                check(trimSignals == 8 && page.trimVersion() == QStringLiteral("1.21.4"),
                      "★★ U40 换目标版本 ⇒ 照发一发（判据层 / 结果区按它重算）",
                      QStringLiteral("%1 次 / %2").arg(trimSignals).arg(page.trimVersion()));
            }
            page.setTrimResult(QStringLiteral("紫水晶 · 海岸纹饰的钻石胸甲 × 1"));
            check(trimResult->text()
                      == QStringLiteral("紫水晶 · 海岸纹饰的钻石胸甲 × 1"),
                  "★ U40 纹饰结果那一格逐字摆进来");
        }

        page.showTool(ToolboxPage::Tool::Banner);
        auto *bannerPanel =
            page.findChild<QWidget *>(ToolboxPage::toolPanelName(ToolboxPage::Tool::Banner));
        auto *trimPanel =
            page.findChild<QWidget *>(ToolboxPage::toolPanelName(ToolboxPage::Tool::Trim));
        auto *bannerPage =
            page.findChild<QWidget *>(ToolboxPage::toolPageName(ToolboxPage::Tool::Banner));
        auto *trimPage =
            page.findChild<QWidget *>(ToolboxPage::toolPageName(ToolboxPage::Tool::Trim));
        check(page.currentTool() == ToolboxPage::Tool::Banner && bannerPanel != nullptr
                  && trimPanel != nullptr && bannerPage != nullptr && trimPage != nullptr
                  && !bannerPage->isHidden(),
              "★★ U40 `showTool(Banner)` 换到旗帜那一片");
        page.showTool(ToolboxPage::Tool::Trim);
        check(page.currentTool() == ToolboxPage::Tool::Trim && !trimPage->isHidden()
                  && bannerPage->isHidden(),
              "★★ U40 再换到纹饰那一片：前一片收起来、这一片露出来");
        page.close();
        QApplication::processEvents();
    }
    return 0;
}


