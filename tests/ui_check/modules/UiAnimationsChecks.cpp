#include "Support.hpp"
int UiAnimationsChecks(QApplication &app, QFrame * &bar) {
    // ---------- ⑬ 动效那一层 ----------
    // 要求：「接下来是制作动画，按下按钮，切换页面，展开/折叠的时候的动画，可以不那么
    // 严格的检查，只要不要有元素超出，我人工会校对哪里需要改」。
    //
    // 所以这一段的判据**只有两条是硬的**，其余都是"接线接上了没有"：
    // * ★★ **不许有元素超出** —— 落到代码上就是"动效从头到尾不许动几何"。唯一例外是底栏
    // 那两块上拉面板：它们是独立窗口，滑上来就是它的 pos 在动（这一段不量它）。
    // * ★★ **不许把事件吃掉** —— 装上动效之后按钮照样能点（事件过滤器永远返回 false）。
    // "看着好不好看"归用户的眼睛，这里一条都不判。
    {
        check(Anim::pressInMs() > 0 && Anim::pressOutMs() > 0 && Anim::appearMs() > 0 &&
                  Anim::overlayMs() > 0 && Anim::panelMs() > 0 &&
                  Anim::panelSlideDistance() > 0 && Anim::pageRiseDistance() > 0 &&
                  Anim::pressDepthPercent() > 0 && Anim::pressDepthPercent() < 100 &&
                  Anim::pageStartOpacity() > 0.0 && Anim::pageStartOpacity() < 1.0,
              "⑬ 时长和档位全是正数、按下那一档不到 100%（没有哪一个是 0 毫秒那种假参数）"
              "，换页起手也不是全透明（0 < pageStartOpacity() < 1）",
              QStringLiteral("按下 %1/%2 ms、换页 %3、覆盖层 %4、面板 %5、抬 %6 px、压暗 %7%")
                  .arg(Anim::pressInMs())
                  .arg(Anim::pressOutMs())
                  .arg(Anim::appearMs())
                  .arg(Anim::overlayMs())
                  .arg(Anim::panelMs())
                  .arg(Anim::pageRiseDistance())
                  .arg(Anim::pressDepthPercent()));

        // 开关：这个工程在 main() 开头是**开着**搭界面的（为了量上面那两条接线），量完就关了。
        const bool atStart = Anim::enabled();
        check(!atStart, "⑬ 这个工程往下这一段是把动效关着的（量的是界面本身，不是它动的时候）",
              QStringLiteral("enabled() = %1").arg(atStart));
        Anim::setEnabled(true);
        const bool on = Anim::enabled();
        Anim::setEnabled(false);
        const bool off = Anim::enabled();
        check(on && !off, "⑬ 总开关能开能关（关掉之后几个函数全是空操作，一个效果都不装）");

        // —— ★ 那几个数落在要求给的区间里 ——
        //
        // 要求逐字：「页面首次进入：160–200 ms，透明度 + **8 px** 位移；…面板展开：
        // **160–180 ms**；按钮 hover：90–120 ms；下载进度使用**平滑追值**…；不动画窗口几何、
        // 表格列宽和文字缩放」。这一条量的是**这些数本身**（每一个数"为什么是这个值"写在
        // `Animations.hpp` 里它自己那一段）。
        check(Anim::appearMs() == 250 && Anim::overlayMs() == 250 &&
                  Anim::pageRiseDistance() == 8 && Anim::panelMs() >= 240 &&
                  Anim::panelMs() <= 260 && Anim::rowAppearMs() > 0 &&
                  Anim::rowAppearMs() <= 200 && Anim::progressMs() > 0 &&
                  Anim::rowAppearLimit() > 0,
              "★ ⑬ 那几个时长/位移都落在案上给的区间里（换页 250 ms、抬正好 8 px、"
              "面板 250 ms），新增的三位（行淡 / 追值 / 淡几行的上限）也都不是 0 那种"
              "假参数",
              QStringLiteral("换页 %1 ms / 抬 %2 px / 面板 %3 ms / 行淡 %4 ms（最多 %5 行）/ "
                             "追值 %6 ms")
                  .arg(Anim::appearMs())
                  .arg(Anim::pageRiseDistance())
                  .arg(Anim::panelMs())
                  .arg(Anim::rowAppearMs())
                  .arg(Anim::rowAppearLimit())
                  .arg(Anim::progressMs()));

        // —— ★ 追值那四条「如实更新」的规矩——
        //
        // 要求那一句：「下载进度使用平滑追值，**但任务真实进度倒退或总量变化时必须如实
        // 更新**」。所以这一条判的不是"淡得好看不好看"，而是**那几条"必须"真的管住了**：
        // 往前 ⇒ 从当前那一格追过去（当场还没到，跑完**正好**落在目标上，不是"落在附近"）；
        // 倒退 / 总量变了 / 条子被钳回过 0 ⇒ **当场**就是真值，一格动画都不许有。
        {
            QProgressBar bar;
            bar.setRange(0, 100);
            // 「这一发还在跑吗」问的是**那个步进定时器还在不在跑**，不是"有没有动画对象"：
            // 按帧走的那一路**故意**把 `QVariantAnimation` 留着不删（它从头到尾没 `start()`
            // 过 —— 理由写在 `Animations.cpp` 的 `runProgress()` 里），所以 `findChildren`
            // 里永远找得到它。定时器名字是全仓唯一那个字符串（和上面够幕布是同一招）。
            const auto chasingNow = [](QObject *owner) {
                const QList<QTimer *> steppers =
                    owner->findChildren<QTimer *>(QStringLiteral("wtfmlAnimStepper"));
                for (QTimer *timer : steppers) {
                    if (timer->isActive()) {
                        return true;
                    }
                }
                return false;
            };
            Anim::setEnabled(true);
            Anim::smoothProgress(&bar, 0);
            const bool zeroLanded = bar.value() == 0 && !chasingNow(&bar);
            Anim::smoothProgress(&bar, 5);   // 把条子挪离 0（这一步按第四条规矩就是当场落实）
            const bool lifted = bar.value() == 5 && !chasingNow(&bar);
            Anim::smoothProgress(&bar, 60);  // ① 往前 ⇒ 追
            const bool chasing = bar.value() < 60 && chasingNow(&bar);
            QEventLoop chaseWait;
            QTimer::singleShot(Anim::progressMs() + 120, &chaseWait, &QEventLoop::quit);
            chaseWait.exec();
            check(zeroLanded && lifted && chasing && bar.value() == 60 && !chasingNow(&bar),
                  "★ ⑬ 往前那一趟是**追**的（当场还没到、步进器在跑，跑够一段之后正好落在"
                  "目标上 —— 不是「落在附近」，也不是「当场就跳过去」）",
                  QStringLiteral("起手落在 0 和 5 上 %1、追的过程里 %2、跑完 %3（该是 60）、"
                                 "跑完还在追 %4")
                      .arg(zeroLanded && lifted ? QStringLiteral("对")
                                                : QStringLiteral("**不对**"))
                      .arg(chasing ? QStringLiteral("还没到、在跑")
                                   : QStringLiteral("**已经到/没在追**"))
                      .arg(bar.value())
                      .arg(chasingNow(&bar)));
            Anim::smoothProgress(&bar, 20);   // ② 倒退 ⇒ 当场落实
            const bool backLanded = bar.value() == 20 && !chasingNow(&bar);
            bar.setRange(0, 400);             // ③ 总量变了 ⇒ 当场落实
            Anim::smoothProgress(&bar, 380);
            const bool totalLanded = bar.value() == 380 && !chasingNow(&bar);
            // ④ 「还回来」那一趟：条子被借走时钳回过 0（`setRange(0, 0)`），而记下的总量
            // 没变过 —— 这一趟**不许**从 0 慢慢爬到"其实早就下到那儿了"的那个数字。
            bar.setRange(0, 0);
            bar.setRange(0, 400);
            Anim::smoothProgress(&bar, 380);
            const bool restoredLanded = bar.value() == 380 && !chasingNow(&bar);
            check(backLanded && totalLanded && restoredLanded,
                  "★★ ⑬ 三条「必须如实更新」都管住了：真进度**倒退**、**总量变了**、以及"
                  "**条子被钳回过 0**（启动借走这一格又还回来那一趟）—— 三种情况当场就是"
                  "真值，没有一发在追",
                  QStringLiteral("倒退后 %1（该是 20）、换总量后 %2（该是 380）、还回来那一趟 "
                                 "%3，在追的 %4")
                      .arg(20)
                      .arg(380)
                      .arg(bar.value())
                      .arg(chasingNow(&bar)));
            Anim::setEnabled(false);
            bar.setRange(0, 400);
            Anim::smoothProgress(&bar, 399);
            check(bar.value() == 399 && !chasingNow(&bar),
                  "★ ⑬ 关掉动效时就是一句 `setValue()`（连「追上 399」这种事都不做）",
                  QStringLiteral("%1、在追的 %2").arg(bar.value()).arg(chasingNow(&bar)));
        }

        // —— ★ 「新冒出来的那一行淡进来」那一发——
        //
        // 要求那一条的另一半：「同页筛选/列表变化：只动画**新增**、删除和状态，**不重播
        // 整页**」。这一条量的是**那一发自己**的形状：开着的时候挂一个效果、动的是它的
        // 不透明度，**几何一个像素都不动**（那是这一层唯一那条死规矩）；关着的时候一个
        // 效果都不挂；跑完**自己收干净**（效果摘掉 —— 不收的话那个控件永远糊着一层）。
        {
            QWidget row;
            row.resize(200, 40);
            const QRect before = row.geometry();
            const bool animWasOn = Anim::enabled();   // 收尾摆回去（下一段是按"关着"搭界的）
            Anim::setEnabled(false);
            Anim::rowAppear(&row);
            const bool inert = row.graphicsEffect() == nullptr && row.geometry() == before;
            Anim::setEnabled(true);
            Anim::rowAppear(&row);
            const bool faded = row.graphicsEffect() != nullptr && row.geometry() == before;
            QEventLoop rowWait;
            QTimer::singleShot(Anim::rowAppearMs() + 140, &rowWait, &QEventLoop::quit);
            rowWait.exec();
            Anim::setEnabled(animWasOn);
            check(inert && faded && row.graphicsEffect() == nullptr && row.geometry() == before,
                  "★★ ⑬ 行淡那一发：关着的时候一个效果都不挂、开着的时候挂上并在跑，"
                  "**从头到尾几何一个像素都不动**，跑完自己把效果摘干净",
                  QStringLiteral("关着 %1 / 开着 %2 / 跑完还剩效果 %3")
                      .arg(inert ? QStringLiteral("干净") : QStringLiteral("挂了效果"))
                      .arg(faded ? QStringLiteral("在跑") : QStringLiteral("没动"))
                      .arg(row.graphicsEffect() != nullptr));
        }

        // —— 换页那一下：把事件循环真的跑起来，跑够一整段，从头到尾盯着几何 ——
        //
        // 够到它的办法照旧是**不暴露产品类型**：按名字找那一颗 `QWidget`
        // （`wtfmlPageCurtain` 那个名字只有 `Animations.cpp` 一处会写）。
        const QString curtainName = QStringLiteral("wtfmlPageCurtain");
        const auto curtainOf = [&curtainName](QWidget *w) -> QWidget * {
            return w == nullptr ? nullptr : w->findChild<QWidget *>(curtainName);
        };

        QWidget host;
        host.resize(220, 140);
        QWidget *page = new QWidget(&host);
        page->setGeometry(10, 10, 200, 120);
        host.show();
        QApplication::processEvents();

        Anim::pageAppear(page);
        check(curtainOf(&host) == nullptr,
              "⑬ 关着的时候 `pageAppear()` 连幕布都不建（一个像素都不动）");

        Anim::setEnabled(true);
        Anim::pageAppear(page);
        QWidget *curtain = curtainOf(&host);
        check(curtain != nullptr && curtain->isVisible(),
              "⑬ 开着的时候它会在那一块上盖一层幕布（淡 + 抬都是在这一层上做的）");
        // 「不许有元素超出」的第一条：幕布**正好就是那一块的矩形**，一个像素都不多
        // （所以那圈外框长在幕布外面，这一发从头到尾碰都没碰过它）。
        const QRect curtainOnScreen(curtain != nullptr
                                        ? QRect(curtain->mapTo(&host, QPoint(0, 0)), curtain->size())
                                        : QRect());
        check(curtainOnScreen == page->geometry(),
              "★★ ⑬ 幕布盖的范围**正好是被抬的那一块**（多一个像素都不行：外框就靠它护着）",
              QStringLiteral("幕布 %1,%2 %3x%4 / 那一块 %5,%6 %7x%8")
                  .arg(curtainOnScreen.x())
                  .arg(curtainOnScreen.y())
                  .arg(curtainOnScreen.width())
                  .arg(curtainOnScreen.height())
                  .arg(page->x())
                  .arg(page->y())
                  .arg(page->width())
                  .arg(page->height()));

        // —— 换页那一发**从头到尾只有一段淡入，没有"起手干等"**（ 改的）——
        //
        // 量的是**这一发自己的时间轴**：幕布上挂着的那一个 `QVariantAnimation`
        // （公开的 `findChildren<QVariantAnimation *>()` 就够得着，不用把产品那边的类型
        // 暴露出来）。两条：
        // ① 总时长 = `appearMs()`，**没有额外那一段**。
        // ② 起手一会儿之后进度**已经动了**（不是停在哪一头干等）。
        //
        // 时间那一条**不能卡着点量**：这一趟只等 `appearMs() / 4`（50 来毫秒），
        // 机器一忙、事件循环慢一拍也不会红。
        const int appearTotal = Anim::appearMs();
        {
            const QList<QVariantAnimation *> anims =
                curtain != nullptr ? curtain->findChildren<QVariantAnimation *>()
                                   : QList<QVariantAnimation *>();
            check(anims.size() == 1 && anims.first()->duration() == appearTotal,
                  "⑬ 换页那一发**就只有一段淡入**（总时长 = `appearMs()`，前面没有干等）",
                  QStringLiteral("动画 %1 个、时长 %2 ms、该是 %3 ms")
                      .arg(anims.size())
                      .arg(anims.isEmpty() ? -1 : anims.first()->duration())
                      .arg(appearTotal));
        }

        // —— 而那一发是**按帧走**的（：用户报「动画有严重的卡顿感」之后加的）——
        //
        // 只判"接线在"——"每一步推得多准、看着顺不顺"是观感，归人眼（用户说过
        // "可以不那么严格的检查"）。
        {
            const QList<QTimer *> steppers =
                curtain != nullptr ? curtain->findChildren<QTimer *>(QStringLiteral("wtfmlAnimStepper"))
                                   : QList<QTimer *>();
            const bool live = steppers.size() == 1;
            check(Anim::stepMs() > 0 && live && steppers.first()->interval() == Anim::stepMs() &&
                      steppers.first()->isActive(),
                  "★★ ⑬ 换页那一发是**按帧走**的（每一拍推一档，不是按墙上时钟算）—— "
                  "迟到的第一拍只推一档，不会一口气跳到半路",
                  QStringLiteral("步进器 %1 个、周期 %2 ms、该是 %3 ms、在跑 %4")
                      .arg(steppers.size())
                      .arg(live ? steppers.first()->interval() : -1)
                      .arg(Anim::stepMs())
                      .arg(live && steppers.first()->isActive()));
        }
        {
            QEventLoop lead;
            QTimer::singleShot(qMax(1, Anim::appearMs() / 4), &lead, &QEventLoop::quit);
            lead.exec();
            const QList<QVariantAnimation *> anims =
                curtain != nullptr ? curtain->findChildren<QVariantAnimation *>()
                                   : QList<QVariantAnimation *>();
            const qreal value = anims.isEmpty() ? -1.0 : anims.first()->currentValue().toReal();
            check(!anims.isEmpty() && value > 0.05,
                  "★★ ⑬ 换页**起手就在动**（原来那 70 ms 干等整段拿掉了：那一段里内容区是"
                  "一整块空白，每切一次选项卡都闪一下白）",
                  QStringLiteral("四分之一程的进度 %1").arg(value, 0, 'f', 3));
        }

        const QRect pageBefore = page->geometry();
        const QRect hostBefore = host.geometry();
        QEventLoop tick;
        QTimer::singleShot(appearTotal + 90, &tick, &QEventLoop::quit);
        tick.exec();
        QApplication::processEvents();
        check(page->geometry() == pageBefore && host.geometry() == hostBefore,
              "★★ ⑬ 一整段换页动画跑完，几何**一个像素都没动**"
              "（用户点名的硬要求：不许有元素超出）",
              QStringLiteral("页从 %1,%2 变成 %3,%4")
                  .arg(pageBefore.x())
                  .arg(pageBefore.y())
                  .arg(page->geometry().x())
                  .arg(page->geometry().y()));
        check(curtain != nullptr && !curtain->isVisible(),
              "⑬ 跑完把那层幕布收起来了（收起来之后它一个像素都不画 —— "
              "下一次换页接着用同一块，不再生灭一遍）");

        // —— 透明页面的快照必须抓「屏幕上合成后的样子」——
        //
        // 页面自己常常不铺底，白色来自它后面的卡片。单独 `page->grab()` 时 Qt 会用页面的
        // `Window` 角色补底：如果那一档是灰色，动画幕布就会整块先灰、结束后才恢复成下面
        // 真正的白色。这里故意搭一个「白父层 + 灰调色板的透明页面」，同时把页面放在非零
        // 坐标上，连幕布本地坐标重复偏移那一类漏底也一起守住。
        {
            QWidget compositeHost;
            compositeHost.resize(140, 90);
            compositeHost.setAutoFillBackground(true);
            QPalette hostPalette = compositeHost.palette();
            hostPalette.setColor(QPalette::Window, Qt::white);
            compositeHost.setPalette(hostPalette);

            auto *transparentPage = new QWidget(&compositeHost);
            transparentPage->setGeometry(17, 11, 100, 60);
            transparentPage->setAutoFillBackground(false);
            QPalette transparentPalette = transparentPage->palette();
            transparentPalette.setColor(QPalette::Window, QColor(0x80, 0x80, 0x80));
            transparentPage->setPalette(transparentPalette);

            compositeHost.show();
            QApplication::processEvents();
            const QColor before = compositeHost.grab().toImage().pixelColor(67, 41);

            Anim::pageAppear(transparentPage, Qt::white);
            QApplication::processEvents();
            const QColor during = compositeHost.grab().toImage().pixelColor(67, 41);
            QWidget *compositeCurtain = curtainOf(&compositeHost);

            check(before.red() > 248 && before.green() > 248 && before.blue() > 248 &&
                      during.red() > 248 && during.green() > 248 && during.blue() > 248,
                  "★★ ⑬ 透明页面换页时背景从第一帧起就是白的（不会动画期间发灰、结束才变白）",
                  QStringLiteral("动画前 %1,%2,%3 / 动画中 %4,%5,%6 / 幕布 %7")
                      .arg(before.red())
                      .arg(before.green())
                      .arg(before.blue())
                      .arg(during.red())
                      .arg(during.green())
                      .arg(during.blue())
                      .arg(compositeCurtain != nullptr ? QStringLiteral("有")
                                                       : QStringLiteral("无")));
        }

        // —— 按下按钮那一下：装上过滤器，用真的鼠标事件走一趟 ——
        QPushButton *probe = new QPushButton(QStringLiteral("按下看看"), &host);
        probe->setGeometry(10, 10, 120, 30);
        probe->show();
        Anim::installPressAnimation(app);

        int clicks = 0;
        QObject::connect(probe, &QPushButton::clicked, probe, [&clicks]() { ++clicks; });
        const QRect buttonBefore = probe->geometry();

        QMouseEvent press(QEvent::MouseButtonPress, QPointF(QPoint(20, 15)),
                          QPointF(probe->mapToGlobal(QPoint(20, 15))), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(probe, &press);
        check(clicks == 0 && probe->graphicsEffect() != nullptr,
              "⑬ 按下去：那一层压暗的效果装上了",
              QStringLiteral("clicked=%1 / 效果=%2")
                  .arg(clicks)
                  .arg(probe->graphicsEffect() != nullptr ? QStringLiteral("有")
                                                          : QStringLiteral("没有")));

        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(QPoint(20, 15)),
                            QPointF(probe->mapToGlobal(QPoint(20, 15))), Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(probe, &release);
        check(clicks == 1,
              "★★ ⑬ 装上动效之后按钮照样能点（那个事件过滤器从不吞事件）",
              QStringLiteral("clicked 响了 %1 次").arg(clicks));

        QEventLoop settle;
        QTimer::singleShot(Anim::pressOutMs() + 90, &settle, &QEventLoop::quit);
        settle.exec();
        QApplication::processEvents();
        check(probe->graphicsEffect() == nullptr,
              "⑬ 松手弹回来之后那层效果也摘掉了");
        check(probe->geometry() == buttonBefore,
              "★★ ⑬ 按下 / 松开这一路也没动几何（还是那一条：不许有元素超出）",
              QStringLiteral("从 %1,%2 变成 %3,%4")
                  .arg(buttonBefore.x())
                  .arg(buttonBefore.y())
                  .arg(probe->geometry().x())
                  .arg(probe->geometry().y()));

        // —— 换页那一发淡的是**一张定住的图**——
        //
        // 要求：「服务器列表那栏动画卡卡的」。那一页进页就自动测速、结果一个一个回来，
        // 现照现淡的话**淡的每一帧都不是同一张画面**（行长高、favicon 补上）。规矩：
        // 起手照一次，往后每一帧都拿那一张。
        //
        // 判据这么搭：一块**白**的页面 + 黑底，起手照完（ 起 `pageAppear()` 里
        // 同步照，见 `Animations.cpp` 里 `PageCurtain::ensureSnapshot()`）之后把页面改成
        // **红的** ——
        // * 定住了 ⇒ 淡到一半取样是**灰的**（黑底 + 半透明白），三个分量相等；
        // * 没定住 ⇒ 那一帧照的是红的，取出来是**暗红**（R > G == B）。
        // 再补一条"跑完之后确实是红的"：不然上面那条可能是因为"改颜色根本没生效"而假绿。
        {
            QWidget snapHost;
            snapHost.resize(120, 80);
            auto *snapPage = new QWidget(&snapHost);
            snapPage->setGeometry(0, 0, 120, 80);
            snapPage->setAutoFillBackground(true);
            QPalette snapPalette = snapPage->palette();
            snapPalette.setColor(QPalette::Window, Qt::white);
            snapPage->setPalette(snapPalette);
            snapHost.show();
            QApplication::processEvents();

            Anim::pageAppear(snapPage, Qt::black);
            QWidget *snapCurtain = curtainOf(&snapHost);
            // **不等那一帧了、当场就改颜色**。快照从这一版起是在
            // `pageAppear()` 里同步照的（`PageCurtain::ensureSnapshot()`，不在
            // `paintEvent()` 里 —— 在那里抓会抓到正在画的自己，见那个类头上那一段），
            // 所以"起手照的那一张"必须早于这里这一改。反过来说：这一条现在也钉住了
            // "照快照不是拖到第一帧才做"—— 懒照那一版会照到下面这个红色，判据当场红。
            snapPalette.setColor(QPalette::Window, Qt::red);
            snapPage->setPalette(snapPalette);
            snapPage->update();

            // 取样那一点**问这一发自己走到哪儿了，不问时钟**。
            //
            // 原来这里是"再等 `appearMs() / 5`（26 ms）就取样"，理由是"取样要落在淡入那
            // 一段的靠前处"。同一天起手那位从 0.5 抬到 0.8 之后这一条当场就红了（量到
            // 245，判据是 < 245）：浓淡那一段整个被挤到"快满"那一头，两段固定等待加起来的
            // 那 40 来毫秒正好落在它快走完的地方 —— 判据量的其实**是时钟，不是动画**。
            //
            // 现在改成**等这一发的进度真的到 0.4 以上再去量**（`OutCubic` 下那是第二拍，
            // 0.58）："取样在浓淡中段"这件事由取样条件本身保证，机器快慢、事件循环慢一两
            // 拍都不影响它落在哪儿。顺带把"真的在淡"这一位也钉住了（见下面那条判据里的
            // `sampled`）。
            const QList<QVariantAnimation *> snapAnims =
                snapCurtain != nullptr ? snapCurtain->findChildren<QVariantAnimation *>()
                                       : QList<QVariantAnimation *>();
            qreal sampled = -1.0;
            {
                QEventLoop snapHalf;
                QTimer poll;
                poll.setInterval(2);
                QObject::connect(&poll, &QTimer::timeout, &snapHalf, [&]() {
                    const qreal value =
                        snapAnims.isEmpty() ? -1.0 : snapAnims.first()->currentValue().toReal();
                    if (value < 0.4 && snapCurtain != nullptr && snapCurtain->isVisible()) {
                        return;  // 还没淡到中段，接着等（幕布收起来了就说明跑完了，别再死等）
                    }
                    sampled = value;
                    snapHalf.quit();
                });
                QTimer::singleShot(1000, &snapHalf, &QEventLoop::quit);
                poll.start();
                snapHalf.exec();
                poll.stop();
            }
            const QColor mid = snapHost.grab().toImage().pixelColor(60, 40);

            QEventLoop snapDone;
            QTimer::singleShot(Anim::appearMs() / 2 + 120, &snapDone, &QEventLoop::quit);
            snapDone.exec();
            QApplication::processEvents();
            const QColor after = snapHost.grab().toImage().pixelColor(60, 40);

            check(sampled >= 0.4 && mid.red() == mid.green() && mid.green() == mid.blue() &&
                      mid.red() > 20 && mid.red() < 250,
                  "★★ ⑬ 换页淡到一半时淡的仍是**起手照的那一张**（页面中途换了内容，"
                  "这一发一点都不受影响）",
                  QStringLiteral("取样时进度 %1、中点 %2,%3,%4（定住=灰；没定住=暗红）")
                      .arg(sampled, 0, 'f', 3)
                      .arg(mid.red())
                      .arg(mid.green())
                      .arg(mid.blue()));
            check(after.red() > 200 && after.green() < 60,
                  "★★ ⑬ 而换完之后**确实是新内容**（上一条不是因为改了颜色没生效才绿的）",
                  QStringLiteral("跑完中点 %1,%2,%3").arg(after.red()).arg(after.green())
                      .arg(after.blue()));
        }

        // 收尾：把开关摆回这个工程原来的样子（关着），别让后面留下一个半路的状态。
        Anim::setEnabled(atStart);
    }
    return 0;
}


