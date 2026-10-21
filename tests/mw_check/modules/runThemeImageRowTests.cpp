#include "Support.hpp"
void runThemeImageRowTests() {
    const QString originalDir = AppPaths::minecraftDir();
    const Theme::Spec themeBefore = Theme::current();
    QTemporaryDir root;
    check(root.isValid(), "T29.0 临时数据根目录建出来了");
    if (!root.isValid()) {
        return;
    }

    QSettings sandbox;
    const QVariant foldersBefore = sandbox.value(QStringLiteral("Launcher/Folders"));
    const QVariant currentBefore = sandbox.value(QStringLiteral("Launcher/CurrentFolder"));
    const auto restore = [&] {
        sandbox.setValue(QStringLiteral("Launcher/Folders"), foldersBefore);
        sandbox.setValue(QStringLiteral("Launcher/CurrentFolder"), currentBefore);
        // 主题那几个键是这一节自己写进去的（这个沙盒 INI 每一跑都是新删过的，所以
        // "整组删掉"就是逐字还原）。
        sandbox.remove(QStringLiteral("Theme"));
        sandbox.sync();
        Theme::setSpec(themeBefore);
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
        check(ui != nullptr && MainWindowUI::themeImageRowCount() == 2,
              "T29.1 设置页「背景图」那两行在（主页左栏 / 页面背景）");
        if (ui == nullptr) {
            restore();
            return;
        }

        // 两行的**槽位名**是判据层与界面之间唯一的接头（`Theme::partKey()` 那一串，也是
        // 收图那个落地文件名）。顺序也钉住：第 0 行是主页左栏、第 1 行是页面背景。
        const QString homeLeftKey = Theme::partKey(Theme::Part::HomeLeft);
        const QString pageKey = Theme::partKey(Theme::Part::PageBackground);
        check(ui->getThemeImageRow(0).partKey == homeLeftKey &&
                  ui->getThemeImageRow(1).partKey == pageKey,
              "T29.1 两行的槽位名对得上（第 0 行主页左栏 / 第 1 行页面背景）",
              ui->getThemeImageRow(0).partKey + QLatin1Char(' ') + ui->getThemeImageRow(1).partKey);

        // 六个控件都按 objectName 找得回来，而且**就是** `getThemeImageRow()` 里那几个指针
        // —— 两处各建一套（界面上摆一套、表里存另一套）是这一节最容易出的错。
        int wired = 0;
        for (int i = 0; i < MainWindowUI::themeImageRowCount(); ++i) {
            const MainWindowUI::ThemeImageRow &row = ui->getThemeImageRow(i);
            const QString key = row.partKey;
            const bool ok =
                row.choose != nullptr && row.clear != nullptr && row.fit != nullptr &&
                row.opacity != nullptr && row.percent != nullptr && row.state != nullptr &&
                byObjectName<QPushButton>(&w, MainWindowUI::themeImageChooseName(key)) ==
                    row.choose &&
                byObjectName<QPushButton>(&w, MainWindowUI::themeImageClearName(key)) == row.clear &&
                byObjectName<QComboBox>(&w, MainWindowUI::themeImageFitName(key)) == row.fit &&
                byObjectName<QSlider>(&w, MainWindowUI::themeImageOpacityName(key)) ==
                    row.opacity &&
                byObjectName<QLabel>(&w, MainWindowUI::themeImagePercentName(key)) ==
                    row.percent &&
                byObjectName<QLabel>(&w, MainWindowUI::themeImageStateName(key)) == row.state;
            if (ok) {
                ++wired;
            }
        }
        check(wired == 2, "T29.2 两行的六个控件都按 objectName 找得回来，且就是表里那一份",
              QString::number(wired));

        // 出厂那一档（这一节开头：规格里一块覆盖项都没有）：两行都是"还没选图"，后面那三样
        // 灰着 —— 摆法/不透明度没有图可管，亮着会让人以为点了有用；「清除」更是没有什么可清。
        const MainWindowUI::ThemeImageRow &row0 = ui->getThemeImageRow(0);
        const MainWindowUI::ThemeImageRow &row1 = ui->getThemeImageRow(1);
        check(row0.state->text() == MainWindowUI::themeImageNoneText() &&
                  row1.state->text() == MainWindowUI::themeImageNoneText() &&
                  !row0.clear->isEnabled() && !row0.fit->isEnabled() && !row0.opacity->isEnabled() &&
                  !row1.clear->isEnabled() && !row1.fit->isEnabled() && !row1.opacity->isEnabled(),
              "T29.3 没设图时两行都写「还没选图。」，清除/摆法/不透明度都灰着",
              row0.state->text());

        // ---------- 设上一张图之后 ----------
        //
        // 「选择图片…」底下是 QFileDialog（测试里点下去就是卡死，这一节一个原生框都不点），
        // 所以"用户挑完一张图之后"这一态**照产品那两步搭出来**：先真的把图存进数据目录
        // （产品挑完图干的就是这一件事，走的是同一个 `ThemeImage::store()`），再把规格写进去，
        // 然后按名字调那一发重画（它就是产品里"灌值 / 改完"两处调的那一条）。
        const QString sourcePng = root.path() + QStringLiteral("/素材.png");
        {
            QImage material(120, 60, QImage::Format_RGB32);
            material.fill(QColor(0x33, 0x66, 0x99));
            check(material.save(sourcePng, "PNG"), "T29.4 造出一张素材图（120x60）");
        }
        QString error;
        const QString stored = ThemeImage::store(homeLeftKey, sourcePng, &error);
        check(!stored.isEmpty() && QFileInfo::exists(stored), "T29.4 收进数据目录那一份到位了",
              error);
        {
            Theme::Spec spec = Theme::current();
            Theme::PartStyle style = spec.parts.value(static_cast<int>(Theme::Part::HomeLeft));
            style.imagePath = stored;
            style.hasImage = true;
            // 用户挑的那张原来叫什么（产品里这一位是 `QFileInfo(picked).fileName()`）——
            // 它和盘上那份的名字**故意不一样**：那份是槽位名，写它用户认不出来。
            style.imageName = QFileInfo(sourcePng).fileName();
            style.fit = Theme::ImageFit::Contain;
            style.imageOpacity = 70;
            spec.parts.insert(static_cast<int>(Theme::Part::HomeLeft), style);
            Theme::setSpec(spec);
            Theme::save(Theme::current());
        }
        check(QMetaObject::invokeMethod(&w, "refreshThemeImageRows", Qt::DirectConnection),
              "T29.4 那两行重画得动（这一条槽就是产品里灌值和改完调的那一个）");

        // 写的是**用户挑的那张的名字**（"素材.png"），不是盘上那份的（`homeLeft.jpg` ——
        // 槽位名）。这两者在这一节里故意不一样，就是为了让"写错了哪一个"当场露出来。
        check(row0.state->text().contains(QFileInfo(sourcePng).fileName()) &&
                  !row0.state->text().contains(QFileInfo(stored).fileName()) &&
                  row0.state->text() != MainWindowUI::themeImageNoneText(),
              "★ T29.5 状态那行写的是**用户挑的那张的名字**（不是槽位名、也不是那条长路径）",
              row0.state->text());
        {
            QSettings written;
            check(written.value(QStringLiteral("Theme/Parts/%1/ImageName").arg(homeLeftKey))
                      .toString() == QFileInfo(sourcePng).fileName(),
                  "T29.5 那个名字也落了盘（下次启动还是他挑的那张的名字）");
        }
        check(row0.fit->isEnabled() && row0.opacity->isEnabled() && row0.clear->isEnabled() &&
                  row0.fit->currentData().toString() ==
                      Theme::imageFitKey(Theme::ImageFit::Contain) &&
                  row0.opacity->value() == 70 &&
                  row0.percent->text() == MainWindowUI::themeImagePercentText(70),
              "T29.5 设了图之后三样亮起来，摆法/不透明度/百分比都照规格画出来",
              QStringLiteral("摆法=%1 不透明度=%2 百分比=%3")
                  .arg(row0.fit->currentData().toString())
                  .arg(row0.opacity->value())
                  .arg(row0.percent->text()));
        check(row1.state->text() == MainWindowUI::themeImageNoneText() && !row1.clear->isEnabled(),
              "T29.5 另一行没设图，照旧灰着（两行互不干扰）");

        // ---------- 控件 → 规格 → 盘（这一节真正要证的那条线） ----------
        //
        // 摆法：拨一下下拉框。这一下走的是 `currentIndexChanged` → MainWindow 那一条槽，
        // 量的是"用户拨了它 ⇒ 屏幕上那块底变了 + 下次启动还是它"，不是"我们直接改了规格"。
        const int tileIndex = row0.fit->findData(Theme::imageFitKey(Theme::ImageFit::Tile));
        check(tileIndex >= 0 && tileIndex != row0.fit->currentIndex(),
              "T29.6 下拉框里找得到「平铺」那一条");
        if (tileIndex >= 0) {
            row0.fit->setCurrentIndex(tileIndex);
            QApplication::processEvents();
            const Theme::PartImage after = Theme::partImage(Theme::Part::HomeLeft);
            check(after.fit == Theme::ImageFit::Tile,
                  "★ T29.6 拨了摆法之后内存里那份规格跟着变了（立刻生效那一半）",
                  Theme::imageFitKey(after.fit));
            QSettings written;
            check(written.value(QStringLiteral("Theme/Parts/%1/ImageFit").arg(homeLeftKey))
                      .toString() == Theme::imageFitKey(Theme::ImageFit::Tile),
                  "★ T29.6 盘上也跟着变了（下次启动读回来还是它）");
            // 整份写回去那条路的坑：只拨了摆法，另外两项（路径 / 不透明度）**和那个名字**
            // 都不许被带丢。
            check(after.path == stored && after.opacity == 70 &&
                      after.name == QFileInfo(sourcePng).fileName() &&
                      written.value(QStringLiteral("Theme/Parts/%1/ImageOpacity")
                                        .arg(homeLeftKey))
                              .toInt() == 70,
                  "★ T29.6 拨摆法没把另外两项带丢（规格是整份写回去的；那个名字也在）",
                  QStringLiteral("%1 / %2 / %3").arg(after.path).arg(after.opacity).arg(after.name));
        }

        // 不透明度：拖滑块。那一格上的百分比也要当场跟着变（用户就是看着那个数拖的）。
        row0.opacity->setValue(35);
        QApplication::processEvents();
        check(Theme::partImage(Theme::Part::HomeLeft).opacity == 35 &&
                  row0.percent->text() == MainWindowUI::themeImagePercentText(35),
              "★ T29.7 拖不透明度：规格跟着变，那格百分比也当场变",
              row0.percent->text());
        check(Theme::partImage(Theme::Part::HomeLeft).fit == Theme::ImageFit::Tile &&
                  Theme::partImage(Theme::Part::HomeLeft).path == stored,
              "T29.7 拖滑块没把摆法/路径带丢（同上，另一个方向）");

        // ---------- 清除 ----------
        row0.clear->click();
        QApplication::processEvents();
        check(!Theme::partImage(Theme::Part::HomeLeft).isSet(),
              "★ T29.8 点「清除」：规格里那一条整个撤掉（这一块一条覆盖项都不剩）");
        check(!QFileInfo::exists(stored),
              "★ T29.8 数据目录里那一份也删了（留着就是几张再也用不上的旧图）");
        QSettings cleared;
        check(cleared.value(QStringLiteral("Theme/Parts/%1/Image").arg(homeLeftKey))
                  .toString()
                  .isEmpty() &&
                  cleared.value(QStringLiteral("Theme/Parts/%1/ImageName").arg(homeLeftKey))
                      .toString()
                      .isEmpty() &&
                  !cleared.contains(QStringLiteral("Theme/Parts/%1").arg(homeLeftKey)),
              "★ T29.8 盘上那一块的覆盖项也清干净了（图、摆法、名字一样不剩）");
        check(row0.state->text() == MainWindowUI::themeImageNoneText() && !row0.clear->isEnabled() &&
                  !row0.fit->isEnabled() && !row0.opacity->isEnabled(),
              "T29.8 那一行当场变回「还没选图。」+ 三样又灰回去",
              row0.state->text());

        restore();
        check(Theme::current().parts.size() == themeBefore.parts.size() &&
                  AppPaths::minecraftDir() == originalDir &&
                  !sandbox.contains(QStringLiteral("Theme/Parts")),
              "T29.9 收尾：主题规格 / 数据根目录 / 沙盒里那几个键都还原了（这一节不留痕迹）");
    }
}

