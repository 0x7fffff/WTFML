#include "Support.hpp"
// Z11：自绘外框 + 左导航轨（ui/TitleBar）。这里验接线与摆位：轨在左边、宽 208 / 折叠 64、
// 窄窗（< 1024）自动折、右栏顶上那条 40 高的窗口按钮条（最小化 / 最大化 / 关闭）、
// 导航的颗数与字、点下去发什么信号、品牌字。"系统那条白条到底去掉了没有"是 Win32 那一层
// 的事（ui/WindowChrome），offscreen 里只验它认领哪些原生消息。
int UiTitleBarChecks(QMainWindow &window, MainWindowUI &ui) {
    TitleBar *rail = ui.titleBar();
    WindowChrome *chrome = ui.chrome();
    check(rail != nullptr && chrome != nullptr, "Z11 导航轨、外框挂件都搭出来了");
    if (rail == nullptr || chrome == nullptr) return 0;
    // 拖窗区两条：右上那条窗口按钮条（主条）+ 导航轨的空白处（addDragStrip）。
    check(chrome->strip() == rail->captionStrip(), "Z11 外框认的主拖窗条就是右上那条窗口按钮条");
    check((window.windowFlags() & Qt::WindowMaximizeButtonHint) != 0,
          "★ Z11 主窗口能最大化（设计稿右上是最小化 / 最大化 / 关闭三颗）");
    check(window.minimumSize() == QSize(900, 620), "★ Z11 主窗口最小 900×620",
          QStringLiteral("%1x%2").arg(window.minimumWidth()).arg(window.minimumHeight()));

    // ---------- 宽窗：轨展开 208，贴在左边整高 ----------
    window.resize(1280, 800);
    QApplication::processEvents();
    check(!rail->isCollapsed() && rail->width() == TitleBar::railWidth() && TitleBar::railWidth() == 208,
          "★ Z11 宽窗下轨展开、宽 208",
          QStringLiteral("轨 %1 / 窗 %2").arg(rail->width()).arg(window.width()));
    check(rail->x() == 0 && rail->y() == 0 && rail->height() == window.centralWidget()->height(),
          "Z11 轨贴在左边、上下顶满",
          QStringLiteral("%1,%2 %3 / %4").arg(rail->x()).arg(rail->y()).arg(rail->height())
              .arg(window.centralWidget()->height()));

    // ---------- 右栏顶上那条窗口按钮条 ----------
    QWidget *caption = rail->captionStrip();
    QAbstractButton *minBtn = rail->minimizeButton();
    QAbstractButton *maxBtn = rail->maximizeButton();
    QAbstractButton *closeBtn = rail->closeButton();
    check(caption != nullptr && caption->height() == TitleBar::barHeight() && TitleBar::barHeight() == 40,
          "★ Z11 顶部拖动区高 40", caption != nullptr ? QString::number(caption->height()) : QString());
    check(minBtn && maxBtn && closeBtn, "Z11 最小化 / 最大化 / 关闭三颗都找得到");
    if (caption && minBtn && maxBtn && closeBtn) {
        const QSize cell(TitleBar::windowButtonWidth(), TitleBar::buttonSize());
        check(minBtn->size() == cell && maxBtn->size() == cell && closeBtn->size() == cell && cell == QSize(46, 40),
              "★ Z11 三颗窗口按钮都是 46×40");
        const QPoint closeTopRight = closeBtn->mapTo(&window, closeBtn->rect().topRight());
        check(closeTopRight.y() == 0 && closeTopRight.x() == window.width() - 1,
              "★ Z11 关闭那颗贴在窗口右上角",
              QStringLiteral("%1,%2 / 窗宽 %3").arg(closeTopRight.x()).arg(closeTopRight.y()).arg(window.width()));
        check(minBtn->geometry().right() + 1 == maxBtn->x() && maxBtn->geometry().right() + 1 == closeBtn->x(),
              "Z11 最小化、最大化、关闭从左到右一个挨一个");
        check(minBtn->focusPolicy() == Qt::NoFocus && maxBtn->focusPolicy() == Qt::NoFocus &&
                  closeBtn->focusPolicy() == Qt::NoFocus,
              "Z11 窗口按钮不抢键盘焦点");
        check(caption->mapTo(&window, QPoint(0, 0)).x() == rail->width(),
              "Z11 按钮条摆在右栏最上面（从轨的右边开始）");
    }

    // ---------- 导航：颗数 / 字 / 摆位 / 点击 ----------
    QTabWidget *tabs = ui.getTabWidget();
    check(tabs != nullptr && !tabs->tabBar()->isVisible(), "★ Z11 标签页自己那条标签栏藏起来了（导航在轨上）");
    check(TitleBar::defaultBrandText() == QStringLiteral("WTFML") && rail->titleText() == TitleBar::defaultBrandText(),
          "★ Z11 品牌默认写那五个字母", rail->titleText());
    const int tabCount = tabs != nullptr ? tabs->count() : -1;
    check(tabCount == 5 && rail->navCount() == tabCount, "★ Z11 五个功能 = 五颗导航",
          QStringLiteral("%1 / %2").arg(tabCount).arg(rail->navCount()));
    if (tabs != nullptr && tabCount == rail->navCount() && tabCount > 0) {
        bool labelsOk = true, fontOk = true, sizeOk = true, orderOk = true;
        for (int i = 0; i < tabCount; ++i) {
            QAbstractButton *button = rail->navButton(i);
            labelsOk = labelsOk && button->text() == tabs->tabText(i);
            fontOk = fontOk && button->font().pixelSize() == TitleBar::navFontPixelSize();
            sizeOk = sizeOk && button->size() == QSize(TitleBar::navItemWidth(), TitleBar::navItemHeight());
            orderOk = orderOk && (i == 0 || button->y() > rail->navButton(i - 1)->geometry().bottom());
        }
        check(labelsOk, "★ Z11 导航的字就是标签页的字（唯一出处 tabText()）");
        check(fontOk && TitleBar::navFontPixelSize() == 14, "★ Z11 导航字号 14（系统字）");
        check(sizeOk && TitleBar::navItemHeight() == 42, "★ Z11 每颗导航 navItemWidth()×42",
              QStringLiteral("%1x%2").arg(rail->navButton(0)->width()).arg(rail->navButton(0)->height()));
        check(orderOk, "Z11 导航从上到下排、不叠");

        int lastNav = -1;
        const auto spy = QObject::connect(rail, &TitleBar::navRequested, rail, [&lastNav](int i) { lastNav = i; });
        const int before = tabs->currentIndex();
        const int target = (before + 1) % tabCount;
        rail->navButton(target)->click();
        QApplication::processEvents();
        QObject::disconnect(spy);
        check(lastNav == target, "Z11 点第几颗就发 navRequested(几)", QString::number(lastNav));
        check(tabs->currentIndex() == target && rail->currentNav() == target,
              "★ Z11 点了真的翻页，当前那一颗跟着走");
        tabs->setCurrentIndex(before);
        QApplication::processEvents();
    }

    // ---------- 折叠：手动折 64，窄窗自动折 ----------
    int lastCollapsed = -1;
    const auto collapseSpy = QObject::connect(rail, &TitleBar::collapsedChanged, rail,
                                              [&lastCollapsed](bool c) { lastCollapsed = c ? 1 : 0; });
    if (QAbstractButton *fold = rail->foldButton()) {
        check(fold->isVisible(), "Z11 展开时品牌旁有折叠钮");
        fold->click();
        QApplication::processEvents();
    }
    check(rail->isCollapsed() && rail->width() == TitleBar::collapsedRailWidth() && TitleBar::collapsedRailWidth() == 64,
          "★ Z11 点折叠钮折成 64 的图标轨", QString::number(rail->width()));
    check(lastCollapsed == 1, "Z11 折叠时发 collapsedChanged(true)");
    rail->setCollapsed(false);
    QApplication::processEvents();
    check(!rail->isCollapsed() && rail->width() == TitleBar::railWidth(), "Z11 再展开回 208");

    window.resize(1000, 700);
    QApplication::processEvents();
    check(TitleBar::narrowBreakpoint() == 1024 && rail->isCollapsed() && rail->width() == TitleBar::collapsedRailWidth(),
          "★ Z11 窗口宽 < 1024 时轨自动折叠", QString::number(rail->width()));
    check(rail->foldButton() == nullptr || !rail->foldButton()->isVisible(), "Z11 图标轨上没有折叠钮");
    window.resize(1280, 800);
    QApplication::processEvents();
    check(!rail->isCollapsed() && rail->width() == TitleBar::railWidth(), "★ Z11 窗口放宽回去轨自动展开");
    QObject::disconnect(collapseSpy);

    // ---------- 窗口按钮的信号（单独一份轨，不去真的收主窗口） ----------
    {
        TitleBar probe;
        int minimized = 0, maximized = 0, closed = 0;
        QObject::connect(&probe, &TitleBar::minimizeRequested, [&] { ++minimized; });
        QObject::connect(&probe, &TitleBar::maximizeRequested, [&] { ++maximized; });
        QObject::connect(&probe, &TitleBar::closeRequested, [&] { ++closed; });
        probe.minimizeButton()->click();
        check(minimized == 1 && maximized == 0 && closed == 0, "Z11 点「—」只发最小化那一条");
        probe.maximizeButton()->click();
        check(minimized == 1 && maximized == 1 && closed == 0, "Z11 点「□」只发最大化那一条");
        probe.closeButton()->click();
        check(minimized == 1 && maximized == 1 && closed == 1, "Z11 点「×」只发关闭那一条");
    }

    // ---------- 品牌字 ----------
    const QString longTitle(300, QLatin1Char('W'));
    window.setWindowTitle(longTitle);
    QApplication::processEvents();
    check(rail->titleText() == QStringLiteral("WTFML") && rail->width() == TitleBar::railWidth(),
          "★ Z11 窗口标题再长，轨上照旧那五个字母、轨宽不变");
    window.setWindowTitle(QStringLiteral("WTF Minecraft Launcher"));
    const QImage defaultShot = captureWidget(rail);
    rail->setBrandText(QStringLiteral("自定义的名字"));
    QApplication::processEvents();
    check(rail->titleText() == QStringLiteral("自定义的名字") && captureWidget(rail) != defaultShot,
          "★ Z11 setBrandText() 换掉了品牌字并真的重画");
    rail->setBrandText(QStringLiteral("  空格  "));
    check(rail->titleText() == QStringLiteral("空格"), "Z11 两头的空白被去掉了");
    rail->setBrandText(QString());
    QApplication::processEvents();
    check(rail->titleText() == TitleBar::defaultBrandText() && captureWidget(rail) == defaultShot,
          "Z11 清空 = 回到默认，画出来和一开始一模一样");

    // ---------- 外框认领的原生消息（系统背板那一版只认 WM_NCHITTEST，非客户区交给系统） ----------
    qintptr ncResult = -1;
    check(!chromeHandlesNcCalcsize(chrome, &ncResult), "Z11 WM_NCCALCSIZE 交给系统（系统背板要它）");
    qintptr otherResult = -1;
    check(!chromeHandlesPlainMessage(chrome, chromeLeftButtonDownMessage(), &otherResult),
          "Z11 别的原生消息一个字都不碰（只认 WM_NCHITTEST）");
    {
        GameLogWindow log(QStringLiteral("例外那一扇"));
        check((log.windowFlags() & Qt::FramelessWindowHint) == 0, "★ Z11 「游戏日志」窗照旧带系统标题栏");
    }

    check(window.isVisible(), "Z11 前提：主窗口这会儿还开着");
    if (closeBtn != nullptr) {
        closeBtn->click();
        QApplication::processEvents();
    }
    check(!window.isVisible(), "★ Z11 主窗口上那颗「×」接在窗口上（点它窗口真的关）");
    return 0;
}
