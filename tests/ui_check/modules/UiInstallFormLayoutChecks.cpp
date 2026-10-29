#include "Support.hpp"
int UiInstallFormLayoutChecks(MainWindowUI &ui, QLineEdit * &installNameEdit, QLabel * &installNameHint, QLabel * &installNameError, QPushButton * &installBtn, QTreeWidget * &loaderTree, QWidget * &downloadPage, const QRect &rEdit, const QRect &rInstall, QFrame * &loaderCard, QFrame * &installFooter, const QByteArray &surfaceKey, const std::function<QRect(QWidget *)> &rectInPage) {

    {
        const QRect rTree = rectInPage(loaderTree);
        // 这一节是设置页那种「标题 + 小节卡片」（「右侧只有一个主要内容表面，不再用多个
        // QGroupBox 套起来」）。**这是产品界面上最后一个 QGroupBox**，所以第一条钉的
        // 就是"这里再也没有组了" —— 判据是"那张卡片在名称框下面"。
        loaderCard = loaderTree != nullptr ? qobject_cast<QFrame *>(loaderTree->parentWidget())
                                          : nullptr;
        const QString cardSurface =
            loaderCard != nullptr ? loaderCard->property(surfaceKey.constData()).toString()
                                  : QString();
        const QRect rGroup = rectInPage(loaderCard);

        check(nearestGroupBox(loaderTree) == nullptr &&
                  cardSurface == Theme::sectionSurfaceValue(),
              "加载器那一节是**小节卡片**（wtfmlSurface=\"section\"）—— 这一页上再也没有 QGroupBox",
              QStringLiteral("面=「%1」").arg(cardSurface));
        check(!rGroup.isNull() && rGroup.y() >= rEdit.y() + rEdit.height(),
              "加载器那一卡在名称框下面",
              QString("card.y=%1 edit.bottom=%2").arg(rGroup.y()).arg(rEdit.y() + rEdit.height()));
        // 树在组里、组里没有别的东西了（五条现在是同一棵树里的五行，"自上而下排开"
        // 这件事由树自己保证 —— 不用再数五个钮的 y 坐标）。
        check(!rTree.isNull() && !rGroup.isNull() && rTree.y() >= rGroup.y() &&
                  rTree.y() + rTree.height() <= rGroup.y() + rGroup.height(),
              "那棵树在「模组加载器」那张卡片里",
              QString("tree.y=%1 tree.bottom=%2 card=%3..%4")
                  .arg(rTree.y())
                  .arg(rTree.y() + rTree.height())
                  .arg(rGroup.y())
                  .arg(rGroup.y() + rGroup.height()));
        // 卡片里第一件东西是那个粗体小节标题（「标题 + 内容卡片」那个形状，和设置页同款）。
        {
            QLabel *cardTitle = nullptr;
            if (loaderCard != nullptr) {
                const QList<QLabel *> labels = loaderCard->findChildren<QLabel *>(
                    QString(), Qt::FindDirectChildrenOnly);
                if (!labels.isEmpty()) {
                    cardTitle = labels.first();
                }
            }
            check(cardTitle != nullptr && cardTitle->text() == MainWindowUI::modLoaderLabelText() &&
                      cardTitle->font().bold(),
                  "卡片里第一件东西是那个粗体小节标题「模组加载器」",
                  cardTitle != nullptr ? cardTitle->text() : QStringLiteral("<null>"));
        }
        check(loaderTree != nullptr && loaderTree->frameShape() == QFrame::NoFrame,
              "那棵树自己不画边框（四周已经是那张卡片，再套一圈就是框套框）",
              loaderTree != nullptr ? QString::number(int(loaderTree->frameShape()))
                                    : QStringLiteral("<null>"));
        // 树的高度是**按现在展开了几条**算出来的（收着的时候 = 四条加载器 + 预设行，
        // 共五行；「不装（纯原版）」只留内部状态、不画成重复选项）。
        // 写死成"铺满整页"的话，收着的时候底下空一大片、安装按钮还被顶到窗口最底下。
        const int expectedRows = 5;  // 刚搭好时五行全收着（见上面那节）
        check(!rTree.isNull() &&
                  rTree.height() >= expectedRows * MainWindowUI::versionTreeRowHeight() &&
                  rTree.height() <= expectedRows * MainWindowUI::versionTreeRowHeight() +
                                         2 * loaderTree->frameWidth() + 2,
              "收着的时候那棵树正好六条那么高（不多不少，底下不留一大片空白）",
              QString("tree.h=%1 6行+边框=%2")
                  .arg(rTree.height())
                  .arg(expectedRows * MainWindowUI::versionTreeRowHeight() +
                       2 * loaderTree->frameWidth()));
        // 底栏：`wtfmlSurface="footer"` 那一块（一条分隔线 + 里面右对齐的操作）。
        // NOTE: 用户那份文档里写的是 `[取消] [安装]` 两颗；产品上**没有**「取消」—— 这一页
        // 往回走的那颗是顶上左角那颗「← 返回版本列表」（"页面级操作永远放右上角"那条
        // 在这儿落成了它的对偶：返回在最左、确认在最右）。所以这里钉的是"底栏在、
        // 安装键在底栏里、且靠右下角"，不是"两颗键并排"。
        installFooter = downloadPage->findChild<QFrame *>(MainWindowUI::installFooterName());
        const QRect rFooter = rectInPage(installFooter);
        const QString footerSurface =
            installFooter != nullptr
                ? installFooter->property(surfaceKey.constData()).toString()
                : QString();
        check(installFooter != nullptr && footerSurface.isEmpty(),
              "安装操作悬浮容器不画横跨页面的底栏",
              QStringLiteral("面=「%1」").arg(footerSurface));
        check(installFooter != nullptr && installBtn != nullptr &&
                  installFooter->isAncestorOf(installBtn) && !installFooter->isAncestorOf(installNameEdit),
              "「安装」长在这条底栏里（名称框不在里面 —— 底栏只管确认那一颗）");
        // 中间那一层是**滚动区**：内容比正文面高的时候，底下几条加载器不能被卡片的
        // 下沿裁掉。于是判据是 **"底栏在滚动区下面"**：
        // · 名称框和卡片住在滚动区**里面**（内容再长也只是在里面滚）；
        // · 底栏钉在**页面最底下**（滚动区吃掉全部剩余高度），不会被内容顶走。
        // NOTE: 卡片未裁剪的几何在内容溢出时会伸到底栏下面去 —— 那不是回归，正是这里
        // 要的样子（`rectInPage()` 不会裁剪，所以不能拿 card.bottom 当界线）。
        QScrollArea *installScroll = ui.getInstallScroll();
        const QRect rScroll = rectInPage(installScroll);
        check(installScroll != nullptr &&
                  installScroll->objectName() == MainWindowUI::installScrollName(),
              "中间那一层滚动区在（按 objectName）",
              installScroll != nullptr ? installScroll->objectName() : QStringLiteral("<null>"));
        check(installScroll != nullptr && installScroll->isAncestorOf(loaderTree) &&
                  installScroll->isAncestorOf(installNameEdit) &&
                  !installScroll->isAncestorOf(installFooter),
              "名称框和加载器那一卡住在滚动区里（底栏留在外面）",
              installScroll != nullptr
                  ? QString("树=%1 名称框=%2 底栏=%3")
                        .arg(installScroll->isAncestorOf(loaderTree) ? 1 : 0)
                        .arg(installScroll->isAncestorOf(installNameEdit) ? 1 : 0)
                        .arg(!installScroll->isAncestorOf(installFooter) ? 1 : 0)
                  : QStringLiteral("<null>"));
        check(installScroll != nullptr &&
                  installScroll->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff,
              "滚动区**不横向滚**（内容只往竖里长，横向滚动条是「我没排好」的供词）",
              installScroll != nullptr
                  ? QString::number(int(installScroll->horizontalScrollBarPolicy()))
                  : QStringLiteral("<null>"));
        check(!rInstall.isNull() && !rFooter.isNull() && !rScroll.isNull() &&
                  rFooter.y() >= rScroll.y() && rFooter.bottom() <= rScroll.bottom(),
              "悬浮操作位于滚动区可视范围内",
              QString("footer.y=%1 scroll.bottom=%2")
                  .arg(rFooter.y())
                  .arg(rScroll.y() + rScroll.height()));
        check(rInstall.width() >= 96, "「安装」有最小宽度", QString::number(rInstall.width()));
        // 反面：它不该被 stretch 拉伸成整页宽的一条（那是"一个按钮占满一行"的样子）。
        check(!rInstall.isNull() && rInstall.width() < downloadPage->width() / 2,
              "「安装」只吃自己那点宽度（没被拉满一整行）",
              QString("btn=%1 page=%2").arg(rInstall.width()).arg(downloadPage->width()));
        // 「确认操作放右下角或固定底栏，不悬浮在页面中间」：**靠右**，不居中。
        // 居中会让上面是表单、下面是深浅不定的一截空白，这颗键落在哪儿全看那一栏
        // 当时多高。
        // 大 —— 字比同页那些控件大（字号加了两档）而且给了最小高度。
        QWidget *installPage = installBtn;
        while (installPage != nullptr &&
               installPage->objectName() != MainWindowUI::installPageName()) {
            installPage = installPage->parentWidget();
        }
        const QRect rPage = rectInPage(installPage);
        check(!rInstall.isNull() && installBtn != nullptr && installNameEdit != nullptr &&
                  installBtn->font().pixelSize() >=
                      installNameEdit->font().pixelSize() + MainWindowUI::installButtonFontStep() &&
                  installBtn->minimumHeight() >= MainWindowUI::installButtonMinHeight(),
              "「安装」的字比这一页那些控件大（installButtonFontStep 档）而且有最小高度",
              installBtn != nullptr
                  ? QString("btn=%1 edit=%2 minH=%3")
                        .arg(installBtn->font().pixelSize())
                        .arg(installNameEdit != nullptr ? installNameEdit->font().pixelSize() : -1)
                        .arg(installBtn->minimumHeight())
                  : QStringLiteral("<null>"));
        // 靠右：底栏里那颗键**右边**贴着底栏右边缘、左边空着一大截（前面垫着一条 stretch）。
        // 反面（"悬浮在页面中间"）就是两边差不多 —— 那一条现在必须不成立。
        const int footerLeftGap = rInstall.left() - rFooter.left();
        const int footerRightGap = rFooter.right() - rInstall.right();
        check(!rInstall.isNull() && !rFooter.isNull() &&
                  qAbs(footerLeftGap - footerRightGap) <= 2 &&
                  qAbs(rInstall.center().x() - rScroll.center().x()) <= 2,
              "「安装」在右侧内容区居中悬浮",
              QString("left=%1 right=%2 footer=%3..%4 btn=%5..%6")
                  .arg(footerLeftGap)
                  .arg(footerRightGap)
                  .arg(rFooter.left())
                  .arg(rFooter.right())
                  .arg(rInstall.left())
                  .arg(rInstall.right()));
        check(!rInstall.isNull() && !rFooter.isNull() && !rPage.isNull() &&
                  rInstall.left() >= rPage.left() && rInstall.right() <= rPage.right(),
              "「安装」整颗还在这一页的宽度里",
              QString("btn=%1..%2 page=%3..%4")
                  .arg(rInstall.left())
                  .arg(rInstall.right())
                  .arg(rPage.left())
                  .arg(rPage.right()));
        // 展开一条加载器之后，「安装」**还得在页面上**：那棵树的高度是跟着"展开了几条"
        // 长的（上限 loaderTreeMaxRows() 行），长到头会不会把按钮顶出这一页、或者让
        // 名称框被压扁，光看代码看不出来 —— 得让树真长到最高再量一次。
        //
        // NOTE: 底下那些是**临时**条目（用完就删回去）：这一节后面 E 段要验"开局五条
        // 底下都是空的"，留在这儿会让那条红。
        if (loaderTree != nullptr && loaderTree->topLevelItemCount() > 0) {
            QTreeWidgetItem *row = loaderTree->topLevelItem(0);
            const bool wasExpanded = row->isExpanded();
            for (int i = 0; i < MainWindowUI::loaderTreeMaxRows() + 2; ++i) {
                auto *child = new QTreeWidgetItem(row);
                child->setText(MainWindowUI::loaderTreeKeyColumn(),
                               QStringLiteral("47.%1.0").arg(i));
            }
            row->setExpanded(true);  // 展开 → 那棵树自己重算高度（挂在 expanded 上）
            QApplication::processEvents();
            const QRect rTreeFull = rectInPage(loaderTree);
            const QRect rInstallFull = rectInPage(installBtn);
            check(!rTreeFull.isNull() &&
                      rTreeFull.height() <=
                          MainWindowUI::loaderTreeMaxRows() * MainWindowUI::versionTreeRowHeight() +
                              2 * loaderTree->frameWidth() + 2,
                  "展开很多版本之后，那棵树长到 loaderTreeMaxRows() 行就停（不出滚动条就白长了）",
                  QString("树高=%1 上限=%2")
                      .arg(rTreeFull.height())
                      .arg(MainWindowUI::loaderTreeMaxRows() *
                               MainWindowUI::versionTreeRowHeight()));
            // （"遮挡"那条的**回归钉子**）：内容长到超过可视区之后，滚动区
            // 必须真有行程 —— 没有的话底下那几行还是"画不出来、也够不着"，正是用户
            // 报的那个症状。行程由内容最小高度算出来（不依赖窗口实际显示），offscreen
            // 上也量得准，不是布局几何那种假绿。
            QScrollArea *installScrollNow = ui.getInstallScroll();
            const int scrollRange =
                installScrollNow != nullptr ? installScrollNow->verticalScrollBar()->maximum()
                                            : -1;
            check(installScrollNow != nullptr && scrollRange > 0,
                  "★ 展开到头之后内容比滚动区高 ⇒ 竖向滚动条有行程（底下几行够得着）",
                  QString("行程=%1 视口=%2 内容=%3")
                      .arg(scrollRange)
                      .arg(installScrollNow != nullptr && installScrollNow->viewport() != nullptr
                               ? installScrollNow->viewport()->height()
                               : -1)
                      .arg(installScrollNow != nullptr && installScrollNow->widget() != nullptr
                               ? installScrollNow->widget()->height()
                               : -1));
            check(!rInstallFull.isNull() && !rPage.isNull() &&
                      rInstallFull.bottom() <= rPage.bottom() &&
                      rInstallFull.top() >= rPage.top() &&
                      rInstallFull.height() == installBtn->height(),
                  "树长到最高时，「安装」整颗还在这一页里（没被顶出去、也没被压扁）",
                  QString("btn=%1..%2 页=%3..%4")
                      .arg(rInstallFull.top())
                      .arg(rInstallFull.bottom())
                      .arg(rPage.top())
                      .arg(rPage.bottom()));
            row->setExpanded(wasExpanded);
            qDeleteAll(row->takeChildren());
            QApplication::processEvents();
        }

        // NOTE: 这一段**放在这一节的最末**：它会临时多显出来一行，把名称框底下那些控件
        // 整体往下推，插在中间量出来的几何就不是"平时那个样子"了（试过：放在名称框
        // 那一段后面，「安装」在加载器那一组下面那条当场红）。
        // 名字里有非法字符时底下新起的那一行**红字**（要求："如果版本名字存在
        // 非法字符，不要替换掉那个「装好后在实例...」，而是在下面添加一行红色字"）。
        //
        // 钉四件事：①它自己是谁（objectName，别处 findChild 靠它）；②位置 —— 在**灰字那句
        // 下面**、跟灰字**左对齐同一列**；③配色 —— 色值只有
        // MainWindowUI::installNameErrorColor() 这一个出处，这里比的就是它（这个红跟着调色板
        // 分深浅两档，两档都在下面钉）；④字号 —— 跟旁边
        // 那句灰字**同一档**（它俩是一对"说明 + 原因"，一个大一个小看着像两家人）。
        // 最后再拿**像素**验一遍"那一行上真有一片红墨"：配色/字号/位置全对、但根本没画出来
        // （setVisible 那根线断了、或者被别的东西盖住）的话，前面几条照样绿，只有像素这条会红。
    check(installNameError != nullptr, "找得到名字框底下那行红字（按 objectName）");
    if (installNameError != nullptr && installNameEdit != nullptr && installNameHint != nullptr) {
        // 这一段会临时动那句灰字的显隐，收尾要还原成进来时的样子。
        const bool hintWasHidden = installNameHint->isHidden();
        check(installNameError->isHidden() && installNameError->text().isEmpty(),
              "★ 刚搭好的时候这行红字是**收着的、而且是空的**（名字能用的那一刻它没话可说 —— "
              "占着一行空白也算错）",
              QStringLiteral("藏=%1 字=「%2」")
                  .arg(installNameError->isHidden() ? 1 : 0)
                  .arg(installNameError->text()));

        // NOTE: 下面这一段**手工**把那两行字摆成"名字里有非法字符"那一刻的样子。
        // 这一节刻意只链 ui/（纯界面工程，见 CMakeLists 顶上那段），没有 MainWindow
        // 里那根"名称框一变 → 刷新这两行"的线；产品那条线（谁在什么条件下把哪一行
        // 显出来、写什么字）在 mw_check 里验。这里量的是**摆成这样之后它长什么样**：
        // 配色、位置、字号，以及"那一行上真的画出了红墨"。
        installNameHint->setText(QStringLiteral("装好之后在「实例」页里能看到它，目录是 "
                                               "versions/a/b。"));
        installNameError->setText(QStringLiteral("名字里不能有这些字符。"));
        installNameError->setVisible(true);
        QApplication::processEvents();
        const QString badName = QStringLiteral("a/b");
        const QRect rHint2 = rectInPage(installNameHint);
        const QRect rError = rectInPage(installNameError);
        const int hintSize = installNameHint->font().pixelSize();
        const int errorSize = installNameError->font().pixelSize();

        // NOTE: 这里**不**比那句原因的原文 —— 这一节刻意只链 ui/（纯界面工程，见
        // CMakeLists 顶上那段），而原话是数据层 InstanceFiles::validateDirName()
        // 给的。钉"红字写的就是数据层那句"这件事在 mw_check 里（那边链 src/，
        // 比的是同一个函数）。这边管的是"它长什么样、显没显出来"。
        check(!installNameError->isHidden() && !installNameError->text().isEmpty() &&
                  !installNameError->text().contains(QStringLiteral("装好之后")) &&
                  installNameError->text() != installNameHint->text(),
              "★ 两行是**两行**：上面那句说明还在说它自己的事，底下的红字说的是原因"
              "（没被合成一行、也没互相顶掉）",
              QStringLiteral("显=%1 红=「%2」")
                  .arg(installNameError->isHidden() ? 0 : 1)
                  .arg(installNameError->text()));
        check(installNameError->palette().color(QPalette::WindowText) ==
                  MainWindowUI::installNameErrorColor(installNameError->palette()),
              "★ 那行字是**红的**（色值取自 installNameErrorColor() 这一个出处）",
              QStringLiteral("#%1")
                  .arg(installNameError->palette()
                           .color(QPalette::WindowText)
                           .rgb() & 0xffffff, 6, 16, QLatin1Char('0')));
        // 这个红**跟着调色板分两档**。写死一个值不行：浅底上配的红落到深色主题的底上
        // 只剩 2.26:1（WCAG 正文那条是 4.5:1），"名字写错了"那行原因就成了一团糊在
        // 底色里的暗影，用户只会以为界面坏了。
        // 判据和「删除」那一项、底栏那颗方块同出一处（MainWindowUI::paletteIsDark()），
        // 所以这里量两件事：①两档**真的不一样**；②浅底那一档**仍是那个红**（深色那一
        // 档的分档不该改掉浅底的外观）。
        {
            QPalette light;
            light.setColor(QPalette::Window, QColor(0xf0, 0xf0, 0xf0));
            QPalette dark;
            dark.setColor(QPalette::Window, QColor(0x20, 0x20, 0x20));
            const QColor lightError = MainWindowUI::installNameErrorColor(light);
            const QColor darkError = MainWindowUI::installNameErrorColor(dark);
            check(lightError == QColor(0xc0, 0x39, 0x2b),
                  "浅色主题下那行红字还是原来那个 #c0392b（没顺手把用户看过的那版改掉）",
                  lightError.name());
            check(darkError != lightError && darkError.lightness() > lightError.lightness(),
                  "★ 深色主题下换用亮一档的红（深红落到深底上只剩一团糊，等于没有）",
                  QString("%1 / %2").arg(lightError.name()).arg(darkError.name()));
            check(darkError == MainWindowUI::folderDangerColor(dark),
                  "深底那一档和「删除」那一项共用同一个红"
                  "（同一个界面里两种红各调各的，迟早有一处会看着是脏的）",
                  darkError.name());
        }
        check(!rHint2.isNull() && !rError.isNull() &&
                  rError.y() >= rHint2.y() + rHint2.height(),
              "★ 红字在灰字那句**下面**（用户原话：「在下面添加一行红色字」）",
              QString("hint.bottom=%1 error.y=%2")
                  .arg(rHint2.y() + rHint2.height())
                  .arg(rError.y()));
        check(!rHint2.isNull() && !rError.isNull() && rError.x() == rHint2.x(),
              "红字跟灰字左对齐（同一列起头，不是缩进也不是居中）",
              QString("hint.x=%1 error.x=%2").arg(rHint2.x()).arg(rError.x()));
        check(hintSize > 0 && errorSize == hintSize && errorSize < installNameEdit->font().pixelSize(),
              "红字跟旁边那句灰字**同号**、都比输入框小一档（一对说明 + 原因，看着是一家人）",
              QString("红=%1 灰=%2 输入框=%3")
                  .arg(errorSize)
                  .arg(hintSize)
                  .arg(installNameEdit->font().pixelSize()));

        // 像素这一关：那一行上得有**红**的墨，而上面那句灰字那一行一点红都没有
        // （红的只该出现在新加的那一行上，说明那句照旧是灰的）。
        {
            const QImage shot = captureWidget(downloadPage);
            const auto redInk = [&shot](const QRect &box) {
                QRect clipped = box.intersected(shot.rect());
                int red = 0;
                for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
                    for (int x = clipped.left(); x <= clipped.right(); ++x) {
                        const QColor c = shot.pixelColor(x, y);
                        // 红字本体 #c0392b：抗锯齿之后是"往那个色靠"的一串，红明显压过绿蓝。
                        // 灰字（PlaceholderText）三通道几乎相等，灰底也一样，都不会误判。
                        red += (c.red() > c.green() + 30 && c.red() > c.blue() + 30) ? 1 : 0;
                    }
                }
                return red;
            };
            const int redOnErrorRow = redInk(rError);
            const int redOnHintRow = redInk(rHint2);
            check(redOnErrorRow > 0,
                  "★ 新加的那一行上**真有红墨**（配色/位置全对但没画出来的那种毛病只有这条会红）",
                  QStringLiteral("红点 %1").arg(redOnErrorRow));
            check(redOnHintRow == 0,
                  "反面：上面那句灰字那一行**一点红都没有**（红只在新加的那一行上）",
                  QStringLiteral("红点 %1").arg(redOnHintRow));
        }

        // 「没话可说的时候那一行要**连行一起收掉**」在布局这一层的落点：上面那句灰字
        // 藏起来之后（产品在名字空着的时候就是这么做的），底下的红字要**顶上那一行**，
        // 正好落在灰字原来的 y 上。不收的话红字上面会凭空多出一个空行（"名字弄空了
        // 以后上面那行消失、下面那行空提示换行怪怪的"）。这条量的是**布局**这一层；
        // "产品什么时候收、收到什么程度"在 mw_check 那两条里。
        // 量的是**输入框跟红字之间的距离**（两个标签同在一个容器里，盒子整体挪不挪
        // 都不影响这个量 —— 拿绝对 y 比不行，盒子会跟着缩，差出来 8 px 的假红）。
        const int gapWithGray = rError.y() - (rEdit.y() + rEdit.height());
        installNameHint->setVisible(false);
        QApplication::processEvents();
        const QRect rErrorTight = rectInPage(installNameError);
        const QRect rEditTight = rectInPage(installNameEdit);
        const int gapWithoutGray = rErrorTight.y() - (rEditTight.y() + rEditTight.height());
        check(!rErrorTight.isNull() && !rEditTight.isNull() && gapWithoutGray < gapWithGray &&
                  gapWithoutGray < rHint2.height(),
              "★ 上面那行收掉之后底下的红字**顶上来**，中间只剩正常行间距"
              "（不再多出一整行空档）",
              QString("有灰字时相隔 %1，收掉之后相隔 %2，一行=%3")
                  .arg(gapWithGray)
                  .arg(gapWithoutGray)
                  .arg(rHint2.height()));
        installNameHint->setVisible(!hintWasHidden);

        // 收尾：收回去、字也清干净 —— 后面几节还要拿这一页量几何（多显出来一行会把
        // 底下的控件整体往下推，量出来的就不是"平时那个样子"了）。
        installNameError->setVisible(false);
        installNameError->setText(QString());
        installNameHint->setText(QString());
        QApplication::processEvents();
        check(installNameError->isHidden() && installNameError->text().isEmpty(),
              "收尾：收回去之后又是藏的、字也清干净了",
              QStringLiteral("藏=%1 字=「%2」")
                  .arg(installNameError->isHidden() ? 1 : 0)
                  .arg(installNameError->text()));
    }
    }
 return 0;
}


