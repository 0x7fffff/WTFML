#include "Support.hpp"

#include "UiInstanceFilesTestHelpers.hpp"

// U10k：名字长到装不下时 —— **省略号收尾**，而且是从中间截。
//
// 用户报的「实例设置里模组列表模组名称显示存在错误」：mods 底下那些
// `yet_another_config_lib_v3-3.7.1+1.21.4-fabric.jar` 比行名那一格宽得多，而普通 QLabel 是
// **硬裁**的（最后一个字被切掉一半）—— 看着像这一条的名字就是那串半截的字。修法是行名换成
// ElidedLabel（见 ui/ElidedLabel.hpp）。
//
// 这一节的目录**单独一份**（不改上面那份夹具的行序：往同一个 mods 目录里加一个文件会把它
// 下面那些按下标取的断言全挪位）。
//
// 这一段原来住在 `UiContentPagesChecks.cpp` 里、跟着那一大段的夹具。分出来以后要的四样
// （两个名字、全局那份值、临时目录根）都是显式形参；`mkfile` 用共享的 `uiMkFile()`。
int UiElidedNameChecks(const QString &instanceDisplayName, const QString &instanceDirName,
                       const InstanceSettingsDialog::GlobalValues &global, const QString &root) {
    using Dlg = InstanceSettingsDialog;
    const auto showPage = [](Dlg &d, const QString &key) { d.showPage(key); };
    const auto mkfile = [](const QString &path) { uiMkFile(path); };
    // ---- U10k 名字长到装不下时：**省略号收尾**，而且是从中间截 ----
    //
    // 用户 报的「实例设置里模组列表模组名称显示存在错误」：mods 底下那些
    // `yet_another_config_lib_v3-3.7.1+1.21.4-fabric.jar` 比行名那一格宽得多，而
    // 普通 QLabel 是**硬裁**的（最后一个字被切掉一半）—— 看着像这一条的名字就是那串
    // 半截的字。修法是行名换成 ElidedLabel（见 ui/ElidedLabel.hpp）。
    //
    // 这一节的目录**单独一份**（不改上面那份夹具的行序：往同一个 mods 目录里加一个
    // 文件会把它下面那些按下标取的断言全挪位）。
    {
        const QString longDir = root + QStringLiteral("/长名字/mods");
        // 名字照用户盘上那几条抄（`.jar.disabled` 的**长名字 + 禁用**，两头都占上）。
        // 底下这串得**真的**比行名那一格宽 —— 那是"会省略"这个判据的
        // 前提。用户盘上那一条（52 个字符）在真 windows 的字形下只占 337 px，而行名
        // 那一格在真机上排出来是 **444 px** ⇒ 装得下、根本不省略（offscreen 那套桩
        // 字体宽得多，所以这条一直只红在真机上）。这里把它接长到一百来个字符，
        // 真机（~650 px）和桩字体下都稳稳溢出。后缀保持 `.jar`（页面按后缀认它）。
        const QString longName = QStringLiteral(
            "yet_another_config_lib_v3-3.7.1+1.21.4-fabric"
            "-with-a-very-long-variant-suffix-for-elide-testing.jar");
        mkfile(longDir + QStringLiteral("/") + longName + QStringLiteral(".disabled"));

        Dlg longDlg(instanceDisplayName, instanceDirName, Dlg::Override(), global, QStringList(),
                    true);
        QHash<QString, QString> longDirs;
        longDirs.insert(QStringLiteral("mod"), longDir);
        longDlg.setFileDirs(longDirs);
        // 必须真 show()：这一节量的是**行名那一格有多宽**，而宽是布局排出来的
        // （没 show 过的控件宽度是 0，量出来永远是"装得下"）。show() 不是 exec()，
        // 离屏跑得动。
        longDlg.show();
        QApplication::processEvents();
        longDlg.showPage(QStringLiteral("mod"));
        QApplication::processEvents();
        InstanceFilesPage *page = longDlg.filesPage(QStringLiteral("mod"));
        QListWidgetItem *item = page != nullptr ? page->list()->item(0) : nullptr;
        QWidget *rowWidget =
            (page != nullptr && item != nullptr) ? page->list()->itemWidget(item) : nullptr;
        auto *nameLabel =
            rowWidget != nullptr
                ? rowWidget->findChild<QLabel *>(InstanceFilesPage::nameLabelName())
                : nullptr;
        const QString shownName =
            longName + InstanceFilesPage::disabledMark();

        check(nameLabel != nullptr && nameLabel->text() == shownName,
              "U10k 名字那一格挂的是 `instanceFilesNameLabel`，文字是**完整**那串"
              "（截的只是画出来的样子，数据一个字都不少）",
              nameLabel != nullptr ? nameLabel->text() : QStringLiteral("<null>"));
        // 会省略那种标签：读它在构造时就写好的动态属性（普通 QLabel 上这个属性是 0 ——
        // Qt::ElideNone，所以"删掉 setElideMode()"这条会被当场抓住）。
        check(nameLabel != nullptr &&
                  nameLabel->property("elideMode").toInt() == int(Qt::ElideMiddle),
              "★ U10k 名字那一格是**会省略**的标签，而且从**中间**截（尾巴上挂着"
              "「（已禁用）」，从尾巴截等于先把「这一条是关着的」扔了）",
              nameLabel != nullptr
                  ? QStringLiteral("elideMode=%1").arg(nameLabel->property("elideMode").toInt())
                  : QStringLiteral("<null>"));
        if (nameLabel != nullptr) {
            const QFontMetrics metrics = nameLabel->fontMetrics();
            auto *metaLabel =
                rowWidget->findChild<QLabel *>(InstanceFilesPage::metaLabelName());

            // ---- U10k-1 **名字那一格真的拿到了宽度**（这一节的正题）----
            //
            // 这三样挤在**同一行**时，「4.0 KB / 00:12」在 150% 的字号下要 300 px ——
            // 名字那一格（横向是 Ignored，最小宽度 0）被挤成 **0 宽**，一整列名字一个
            // 字都看不见（不是"裁得难看"，是"根本没有"）。
            const bool wideEnough = nameLabel->width() >= InstanceFilesPage::nameLabelMinWidth();
            const bool moreThanHalf =
                rowWidget != nullptr && nameLabel->width() * 2 > rowWidget->width();
            check(wideEnough && moreThanHalf,
                  "★ U10k 名字那一格**真的拿到了宽度**（不小于下限，而且超过行宽的一半）"
                  "—— 那个大小·时间不能跟它挤同一行（会把它挤成 0 宽）",
                  QStringLiteral("名字宽 %1（下限 %2、行宽 %3、要过半 %4）；列表宽 %5；"
                                 "小字「%6」宽 %7")
                      .arg(nameLabel->width())
                      .arg(InstanceFilesPage::nameLabelMinWidth())
                      .arg(rowWidget != nullptr ? rowWidget->width() : -1)
                      .arg(rowWidget != nullptr ? (rowWidget->width() + 1) / 2 : -1)
                      .arg(page->list()->width())
                      .arg(metaLabel != nullptr ? metaLabel->text() : QStringLiteral("<null>"))
                      .arg(metaLabel != nullptr ? metaLabel->width() : -1));

            // ---- U10k-2 那个大小·时间在**下一行**（同一个左边，名字底下）----
            //
            // 这才是修法本身：把"名字"和"大小 / 时间"摆成上下两行，名字那一格就再也不
            // 用跟谁抢宽度了。摆回同一行的改动会让这条当场红。
            const bool stacked =
                metaLabel != nullptr && metaLabel->isVisible() &&
                metaLabel->mapTo(rowWidget, QPoint(0, 0)).y() >=
                    nameLabel->mapTo(rowWidget, QPoint(0, 0)).y() + nameLabel->height() &&
                metaLabel->mapTo(rowWidget, QPoint(0, 0)).x() ==
                    nameLabel->mapTo(rowWidget, QPoint(0, 0)).x();
            check(stacked,
                  "★ U10k 大小·时间在名字的**下一行**（同一个左边、名字脚底下）"
                  "—— 两样摆成上下两行，名字那一格才拿得到整幅宽度",
                  QStringLiteral("名字 y=%1 x=%2 h=%3；小字 y=%4 x=%5")
                      .arg(nameLabel->mapTo(rowWidget, QPoint(0, 0)).y())
                      .arg(nameLabel->mapTo(rowWidget, QPoint(0, 0)).x())
                      .arg(nameLabel->height())
                      .arg(metaLabel != nullptr
                               ? metaLabel->mapTo(rowWidget, QPoint(0, 0)).y()
                               : -1)
                      .arg(metaLabel != nullptr
                               ? metaLabel->mapTo(rowWidget, QPoint(0, 0)).x()
                               : -1));

            // ---- U10k-3 两行字**装得进这一行**（行高是常数 46）----
            //
            // 上面那条改动有个前提：46 px 那一格塞得下两行字。第二行用的是
            // versionDateFontDelta() 那一档小字（和版本树同一个做法），这儿量出来对一下
            // —— 字号配置一改（150% 缩放的机器上字更高），先红的是这一条，
            // 而不是等用户看见两行贴着边。
            const bool fitsVertically =
                metaLabel != nullptr && nameLabel->height() + metaLabel->height() <=
                                            rowWidget->height();
            check(fitsVertically,
                  "★ U10k 名字那一行 + 小字那一行都装得进行高里（行高 46 = "
                  "versionTreeRowHeight()，装不下就该动行高）",
                  QStringLiteral("名字高 %1 + 小字高 %2 ≤ 行高 %3")
                      .arg(nameLabel->height())
                      .arg(metaLabel != nullptr ? metaLabel->height() : -1)
                      .arg(rowWidget != nullptr ? rowWidget->height() : -1));

            // ---- U10k-4 这么窄的一格里，省略出来的是什么 ----
            //
            // ① 这份夹具**真的**长到装不下 —— 不然这一节等于空转（夹具太短就是这样：
            // 名字全短得根本用不上省略，所以那个"硬裁"的毛病一直没人看见）。
            const bool tooLong = metrics.horizontalAdvance(shownName) > nameLabel->width();
            // ② 省略之后尾巴上那四个字还在（`…` 之后那一段）—— 硬裁恰好会把它切没。
            const QString elided =
                metrics.elidedText(shownName, Qt::ElideMiddle, nameLabel->width());
            check(tooLong && elided.contains(InstanceFilesPage::disabledMark()),
                  "★ U10k 这条名字确实比那一格宽，而省略之后**尾巴上那四个字还在**"
                  "（硬裁出来的那串两头都看不全）",
                  QStringLiteral("宽 %1 < 文字 %2；画出来是「%3」")
                      .arg(nameLabel->width())
                      .arg(metrics.horizontalAdvance(shownName))
                      .arg(elided));

            // ---- U10k-5 这一条自己**不带文字** ----
            //
            // 补，用户截图报的「模组名称出现了两次」。
            // `new QListWidgetItem(文字, list)` 那一份文字是**视图代理**画的，而这一行
            // 的内容是一个用 setItemWidget() 摆上去的真控件、而且是**透明的**（要这样
            // 选中高亮才从底下透出来）—— 代理画的字从控件底下露出来，同一个名字在
            // 屏幕上画两遍，两遍的位置差着半行，看着像重影。
            // 这一条钉的是**修法本身**（名字的唯一出处是那一格标签）。
            check(item->text().isEmpty(),
                  "★ U10k 条目自己**不带文字**（名字只由行里那一格标签画）—— 带一份的话，"
                  "代理会在透明的行控件底下再画一遍同一个名字，就是屏幕上那个重影",
                  QStringLiteral("item->text()=「%1」").arg(item->text()));

            // ---- U10k-6 一行里**只有两段墨**（名字 + 大小·时间）----
            //
            // 上面那条钉的是"条目不给自己带文字"，这一条**不读那个实现细节**，它量的是
            // 屏幕上真的只剩两段字 —— 哪天有人改从别处把名字画回来，U10k-5 照样是绿的，
            // 只有这一条会红。
            //
            // 量法：把这一行的**可视区**截下来，在"名字那一格的横向范围"里逐条扫描线数
            // 有墨的像素（"有墨"= 和这一行的底色不一样，所以次要那一档的灰字也算得上），
            // 数连续有几段。两段 = 名字一行 + 小字一行。
            {
                // 列表本身透明（底由背后的页面铺），所以垫在页面底色上量 —— 直接 grab
                // 的话底是透明黑，深色细字的抗锯齿边够不上阈值，一段字会断成两截。
                const QImage shotImage = captureWidgetOn(page->list()->viewport(), Theme::tokens().page);
                const qreal shotDpr = 1.0;
                const QRect rowRect = page->list()->visualItemRect(item);
                const QPoint nameOrigin =
                    nameLabel->mapTo(page->list()->viewport(), QPoint(0, 0));
                // 从名字那一格的左边起（**不含**行首那枚图标：它也算墨，混进来会把两段
                // 并成一段），到它右边为止。
                const int inkX0 = int(nameOrigin.x() * shotDpr);
                const int inkX1
                    = qMin(shotImage.width(),
                           int((nameOrigin.x() + nameLabel->width()) * shotDpr));
                const int inkY0 = int(rowRect.top() * shotDpr);
                const int inkY1 = qMin(shotImage.height(),
                                       int((rowRect.top() + rowRect.height()) * shotDpr));
                // 底色 = 这一块里出现次数最多的那个颜色（行控件和列表都是透明的，底色就是
                // 垫在下面的页面底色，一整行都是它）。取"最暗的那个阈值"这种写法在小字上会漏。
                QHash<QRgb, int> histogram;
                for (int y = inkY0; y < inkY1; ++y) {
                    for (int x = inkX0; x < inkX1; ++x) {
                        ++histogram[shotImage.pixel(x, y)];
                    }
                }
                QRgb background = 0;
                int mostSeen = -1;
                for (auto it = histogram.constBegin(); it != histogram.constEnd(); ++it) {
                    if (it.value() > mostSeen) {
                        mostSeen = it.value();
                        background = it.key();
                    }
                }
                int bands = 0;
                int inkRun = 0;
                for (int y = inkY0; y < inkY1; ++y) {
                    bool ink = false;
                    for (int x = inkX0; x < inkX1 && !ink; ++x) {
                        const QRgb pixel = shotImage.pixel(x, y);
                        ink = qAbs(qRed(pixel) - qRed(background)) +
                                  qAbs(qGreen(pixel) - qGreen(background)) +
                                  qAbs(qBlue(pixel) - qBlue(background)) >
                              90;
                    }
                    if (ink) {
                        ++inkRun;
                    } else if (inkRun > 0) {
                        ++bands;
                        inkRun = 0;
                    }
                }
                if (inkRun > 0) {
                    ++bands;
                }
                check(inkX1 > inkX0 && inkY1 > inkY0 && bands == 2,
                      "★★ U10k 这一行里**只有两段字**（名字 + 大小·时间）—— 条目的文字"
                      "没清干净的话，中间会多出第三段（就是用户截图里那个重影）",
                      QStringLiteral("数到 %1 段墨（x %2..%3，y %4..%5）")
                          .arg(bands)
                          .arg(inkX0)
                          .arg(inkX1)
                          .arg(inkY0)
                          .arg(inkY1));
            }
        }
    }
    return 0;
}
