#include "Support.hpp"
int UiOverlayNavigationChecks() {
    {
        QWidget home;
        home.resize(600, 400);
        auto *navTabs = new QTabWidget(&home);
        navTabs->setGeometry(0, 0, 600, 400);
        auto *pageA = new QWidget;
        auto *pageB = new QWidget;
        navTabs->addTab(pageA, QStringLiteral("甲"));
        navTabs->addTab(pageB, QStringLiteral("乙"));
        OverlayHost host(navTabs, &home);
        // NOTE: 必须 show()。`isVisible()` 是"这一层现在摆没摆在眼前"的唯一判据
        // （见 OverlayHost::retargetCoveredPage / applyVisibility），父控件藏着的
        // 时候它恒假 —— 那几条断言会以"收着"的样子假绿。
        home.show();
        QApplication::processEvents();

        // 一页，里面摆一个比视口高的东西（滚到哪儿也才量得出来）。
        auto *probe = new QWidget;
        probe->setObjectName(QStringLiteral("overlayParkProbe"));
        probe->setFixedHeight(2000);
        QWidget *area = host.openPage(QStringLiteral("甲页"));
        area->layout()->addWidget(probe);
        QApplication::processEvents();
        check(host.pageOpen() && !host.parked() && host.isVisible(),
              "O2 开在「甲」上：这一页摆出来了（没人在别的选项卡上）");
        check(!pageA->isEnabled() && pageB->isEnabled(),
              "O2 被盖住的那一页按住了（另一页没被动过）");

        auto *pageScroll = host.findChild<QScrollArea *>(OverlayHost::overlayPageScrollName());
        // 多转两圈：内容的高矮是**下一轮**才定下来的（见 OverlayHost 里 `ChildAdded` 那段 ——
        // 塞进去的控件要排队 `show()`，而"看得见"这一位才决定它算不算进布局），一圈就量的话
        // 滚动条还是按"里面是空的"算的（可滚 0，看着像"这一页根本没长"）。
        for (int i = 0; i < 3; ++i) {
            QApplication::processEvents();
        }
        int scrolled = -1;
        if (pageScroll != nullptr) {
            auto *vbar = pageScroll->verticalScrollBar();
            vbar->setValue(vbar->maximum());
            QApplication::processEvents();
            scrolled = vbar->value();
        }
        check(pageScroll != nullptr && scrolled > 0, "O2 前提：那一页滚下去了（量得出来）",
              QStringLiteral("滚到 %1 ／ 内容 %2 ／ 视口 %3")
                  .arg(scrolled)
                  .arg(area->height())
                  .arg(pageScroll != nullptr ? pageScroll->viewport()->height() : -1));

        // ① 切到「乙」= 收着。
        navTabs->setCurrentIndex(1);
        QApplication::processEvents();
        check(host.pageOpen() && host.parked() && !host.isVisible(),
              "★★ O2 切到别的选项卡 = 这一页**收着**（开着、但不露面）",
              QStringLiteral("pageOpen %1 / parked %2 / 可见 %3")
                  .arg(host.pageOpen())
                  .arg(host.parked())
                  .arg(host.isVisible()));
        check(pageA->isEnabled() && pageB->isEnabled(),
              "★ O2 收着的时候两页都是活的（收起来的层没盖住任何东西 —— 用户正要用"
              "现在这一页）");
        check(pageScroll != nullptr && area->layout()->count() == 1 &&
                  probe->parentWidget() == area && pageScroll->verticalScrollBar()->value() == scrolled,
              "★ O2 页里的东西一样不少（滚到哪儿也留着 —— 用户原话「包括向下滑动了多少也保留」）",
              QStringLiteral("滚到 %1（原 %2）")
                  .arg(pageScroll != nullptr ? pageScroll->verticalScrollBar()->value() : -1)
                  .arg(scrolled));
        // 收着的那一页**不许**被当成"没人占着这一层"：那一层的活儿还没干完。
        check(host.openPage(QStringLiteral("另一页")) == nullptr,
              "★ O2 收着的时候再要一页照样被拒（那一位没被释放，只是看不见）");
        int backCount = 0;
        auto backSpy = QObject::connect(&host, &OverlayHost::backPressed, &host,
                                        [&backCount] { ++backCount; });
        check(!host.dismiss() && backCount == 0,
              "★ O2 收着的时候「回到首页」那一发**不落在它头上**（用户在别的选项卡上，看不见它）");

        // ② 切回「甲」= 原样摆出来。
        navTabs->setCurrentIndex(0);
        QApplication::processEvents();
        check(host.pageOpen() && !host.parked() && host.isVisible(),
              "★★ O2 切回来 = 原来那一页原样摆出来（用户原话「那就回到下载 sodium 界面」）");
        check(!pageA->isEnabled() && pageB->isEnabled(), "★ O2 摆回来之后底下那一页又被按住了");
        check(pageScroll != nullptr && pageScroll->verticalScrollBar()->value() == scrolled &&
                  area->layout()->count() == 1,
              "★ O2 滚到哪儿也一分不差（回来之后还是那一格）",
              QStringLiteral("滚到 %1（原 %2）")
                  .arg(pageScroll != nullptr ? pageScroll->verticalScrollBar()->value() : -1)
                  .arg(scrolled));

        // ③ 站在「甲」上再来一下 = 「回到这个功能的首页」：发的是返回箭头那一发。
        check(host.dismiss() && backCount == 1,
              "★★ O2 站在自己这一页上再点一下 = 发**返回箭头那一发**（`execPage()` 那一次"
              "才醒得过来）",
              QStringLiteral("发了 %1 次").arg(backCount));
        QObject::disconnect(backSpy);
        // NOTE: 这一节里**没人接那一发**（产品那边接的是 `execPage()` 里那个嵌套循环：收到
        // 就 `reject()`，再由 `execEmbedded` 收尾）。那一整条链在 mw_check 的 ZJ10 里
        // 按真按钮走过一遍；这里手动把这一层收掉，接着往下量别的。
        host.closeOverlay();
        QApplication::processEvents();
        check(!host.isOpen() && !host.isVisible(), "O2 收尾：那一页也收干净了");

        // ---------- 提示那一档不跟着选项卡走 ----------
        // 用户切页的时候，一条"下载失败"正开着 —— 收起来就等于让调用方在用户看不见的地方
        // 干等（`InlineMessage::ask()` 那些本来就靠"看得见才答得上"）。它只**换底图**。
        check(!host.dismiss() && !host.isOpen(),
              "O2 干干净净的时候「回到首页」那一发什么都不做");
        InlineMessageBox notice(MessageKind::Information, QStringLiteral("下载完成"),
                                QStringLiteral("装好了。"), QStringList(), 0, -1);
        QWidget *noticeArea = host.openPrompt(false);
        if (noticeArea != nullptr) {
            noticeArea->layout()->addWidget(&notice);
        }
        QApplication::processEvents();
        navTabs->setCurrentIndex(1);
        QApplication::processEvents();
        check(host.promptOpen() && host.isVisible() && !host.parked(),
              "★★ O2 提示那一档**不跟着选项卡收**（要人回答的那一句，收起来就是让调用方干等）",
              QStringLiteral("promptOpen %1 / 可见 %2")
                  .arg(host.promptOpen())
                  .arg(host.isVisible()));
        navTabs->setCurrentIndex(0);
        QApplication::processEvents();
        host.closeOverlay();
        QApplication::processEvents();
        check(!host.isOpen() && !host.isVisible(), "O2 收尾：提示收掉之后这一层干净了");
        check(pageA->isEnabled() && pageB->isEnabled(), "O2 收尾：两页都还活着");

        // ---------- 底下压着一页、而那一页正收在别的选项卡上 ----------
        // 用户切走之后后台冒出来一条提示：要变暗的是**用户正看着的那一页**，而提示收掉之后
        // 回到的是那一页（它只是收着）。这一段钉的就是整页那一层不许被掀出来盖在别人头上。
        QWidget *areaAgain = host.openPage(QStringLiteral("甲页"));
        if (areaAgain != nullptr) {
            auto *probeAgain = new QWidget;
            probeAgain->setFixedHeight(1200);
            areaAgain->layout()->addWidget(probeAgain);
        }
        QApplication::processEvents();
        navTabs->setCurrentIndex(1);
        QApplication::processEvents();
        check(host.parked(), "O2 前提：那一页先收在「甲」上");
        InlineMessageBox late(MessageKind::Warning, QStringLiteral("下载失败"),
                              QStringLiteral("对方把连接掐断了。"), QStringList(), 0, -1);
        QWidget *lateArea = host.openPrompt(false);
        if (lateArea != nullptr) {
            lateArea->layout()->addWidget(&late);
        }
        QApplication::processEvents();
        check(host.promptOpen() && host.isVisible() && !host.parked(),
              "★ O2 收着的那一页上面照样开得出提示（它是开着的那一层，不是关了的）");
        check(navTabs->currentWidget() == pageB && !pageB->isEnabled(),
              "★ O2 这一发按住的是**现在这一页**（提示盖住的是用户正看着的那块）");
        host.closeOverlay();
        QApplication::processEvents();
        check(host.pageOpen() && host.parked() && !host.isVisible(),
              "★ O2 提示收掉之后回到那一页 —— 它还是收着的（用户还在别的选项卡上）");
        check(pageA->isEnabled() && pageB->isEnabled(), "O2 收尾：两页又都活着了");
        // 摆回「甲」再手动收掉，别把开着的一层留给后面几节。
        navTabs->setCurrentIndex(0);
        QApplication::processEvents();
        check(host.pageOpen() && host.isVisible(), "O2 收尾前提：回到「甲」，那一页又露面了");
        host.closeOverlay();
        QApplication::processEvents();
        check(!host.isOpen() && !host.isVisible(), "O2 收尾：这一层收干净了");
        delete probe;
    }
    return 0;
}


