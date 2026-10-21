#include "Support.hpp"
int UiGameLogPlacementChecks() {
    {
        // 要求：「日志一定会显示在启动器前面，会遮挡」。病根有两个，这里各验一头：
        // ① 平台规则 —— 有主的窗（owned window）永远压在主窗上面，所以日志窗**不能有父窗口**；
        // ② 摆的位置 —— 就算没遮住，一开窗正好盖在主窗中间也是白搭，所以按右 → 左 → 居中
        // 挑一个不重叠的地方。
        // placementFor 是纯几何（不读屏幕、不读全局状态），所以这里拿一组假矩形直接量它 ——
        // 不用真开窗、也不受这台机器屏幕多大影响。
        const QSize win(400, 300);
        const QRect screen(0, 0, 1920, 1040);
        const QRect launcher(600, 120, 900, 620);

        // ---- (a) 右边放得下：贴右边，跟启动器一条水平线起头，一点不重叠 ----
        {
            const QPoint p = GameLogWindow::placementFor(launcher, win, screen, 0);
            const QRect placed(p, win);
            check(p.x() > launcher.right(),
                  "Z3c 右边放得下时贴**右边**（不叠在启动器上）",
                  QString("x=%1 启动器右沿=%2").arg(p.x()).arg(launcher.right()));
            check(!placed.intersects(launcher),
                  "Z3c 那个位置确实一点不重叠（不是「差一点点」）",
                  QString("%1,%2 %3x%4 vs %5")
                      .arg(p.x())
                      .arg(p.y())
                      .arg(win.width())
                      .arg(win.height())
                      .arg(QStringLiteral("%1,%2 %3x%4")
                               .arg(launcher.x())
                               .arg(launcher.y())
                               .arg(launcher.width())
                               .arg(launcher.height())));
            // 两扇窗的标题栏在一行上：扫过去是一行，不是一上一下。
            check(p.y() == launcher.top(), "Z3c 竖直方向跟启动器上沿对齐（标题栏一行）",
                  QString("%1 vs %2").arg(p.y()).arg(launcher.top()));
            check(screen.contains(placed), "Z3c 而且整个窗都在屏幕里（下沿没顶到任务栏外面去）",
                  QString("%1,%2").arg(p.x()).arg(p.y()));
        }

        // ---- (b) 右边挤不下：翻到左边，同样不重叠 ----
        {
            const QSize wider(500, 300);
            const QRect nearlyFull(900, 120, 1000, 620);  // 右沿 1899，放不下 500 宽的窗
            const QPoint p = GameLogWindow::placementFor(nearlyFull, wider, screen, 0);
            const QRect placed(p, wider);
            check(p.x() + wider.width() - 1 < nearlyFull.left(),
                  "Z3c 右边挤不下时翻到**左边**去（不是硬挤在右边叠上去）",
                  QString("x=%1 宽=%2 启动器左沿=%3").arg(p.x()).arg(wider.width()).arg(nearlyFull.left()));
            check(!placed.intersects(nearlyFull), "Z3c 翻到左边这一下同样一点不重叠");
            check(screen.contains(placed), "Z3c 左边那个位置也在屏幕里");
        }

        // ---- (c) 左右都塞不下（启动器拉满屏那种）：允许重叠，但**不许跑到屏幕外** ----
        {
            const QRect fullScreenLauncher(screen);       // 拉满屏：两边都没地方
            const QSize big(1500, 900);                   // 比屏幕还宽/还高
            const QPoint p = GameLogWindow::placementFor(fullScreenLauncher, big, screen, 0);
            const QRect placed(p, big);
            check(p.x() >= screen.left() && p.y() >= screen.top(),
                  "Z3c 躲不开重叠时也不往左上角外面跑（跑到外面就有一个窗用户再也点不到了）",
                  QString("%1,%2").arg(p.x()).arg(p.y()));
            // 居中（而不是贴某个边）：这种躲不掉的情况，"贴边"看着像画错了，"居中"看着像故意的。
            check(qAbs(placed.center().x() - fullScreenLauncher.center().x()) <= 1,
                  "Z3c 躲不开的时候**居中**（比贴边看着像故意的）",
                  QString("%1 vs %2").arg(placed.center().x()).arg(fullScreenLauncher.center().x()));
        }

        // ---- (d) 同时开两三份：错开，别叠成一摞 ----
        {
            const QPoint first = GameLogWindow::placementFor(launcher, win, screen, 0);
            const QPoint second = GameLogWindow::placementFor(launcher, win, screen, 1);
            const QRect firstRect(first, win);
            const QRect secondRect(second, win);
            // NOTE: 两扇窗**不会**互不重叠 —— 错开只有 28 像素，而窗有三百多像素高，这是
            // 故意的：屏幕上一共摆不下两三扇那种宽度时，能做到的是"后开的不**完全**
            // 盖住先开的"（先开的那扇还露出上面一条标题栏，点得到），而不是铺瓷砖。
            // 所以下面量的是"错开够点得到标题栏"，不是"不重叠"。
            check(second.y() > first.y() && second.y() - first.y() >= 20,
                  "Z3c 第二扇窗往下错开，而且错得够多（至少露出一条标题栏那么高）",
                  QString("%1,%2 -> %3,%4")
                      .arg(first.x())
                      .arg(first.y())
                      .arg(second.x())
                      .arg(second.y()));
            // 先开的那扇**上面那一条**没被盖住：这就是"点得到"的意思（点标题栏能把它拉到前面）。
            const QRect firstTopStrip(firstRect.left(), firstRect.top(), firstRect.width(),
                                      secondRect.top() - firstRect.top());
            check(!firstTopStrip.isEmpty() && !firstTopStrip.intersects(secondRect),
                  "Z3c 先开的那扇标题栏那一条还在外面（点得到、拖得动）");
            check(secondRect != firstRect && !secondRect.intersects(launcher),
                  "Z3c 第二扇窗同样不盖住启动器");
            // 这一条是**回归**：右半边只剩一条缝、错开那 28 像素放不下的时候，第二扇窗要
            // **钳在屏幕右沿上**（还在右边、还不盖启动器），不许因为少了这 28 像素就整个翻到
            // 左边去 —— 翻过去正好压在左边那扇上（错法就长这样，量到 x 从 1512 跳到 60）。
            check(second.x() > launcher.right(),
                  "Z3c 错开被屏幕右沿挡住时也不翻到左边（钳在边上，还在启动器右边）",
                  QString("x=%1 启动器右沿=%2").arg(second.x()).arg(launcher.right()));
            check(screen.contains(secondRect), "Z3c 钳完仍然整个窗都在屏幕里",
                  QString("%1,%2").arg(second.x()).arg(second.y()));
        }

        // ---- (e) 那个窗真的没有父窗口 ----
        {
            auto *log = new GameLogWindow(QStringLiteral("看日志"), nullptr);
            check(log->parentWidget() == nullptr,
                  "Z3c 日志窗是**没有父窗口**的顶层窗",
                  log->parentWidget() == nullptr ? QStringLiteral("<null>")
                                                 : QStringLiteral("有父窗口！"));
            // 这一条是整件事的关键，但**平台规则在 offscreen 上量不出来**（"有主的窗永远压在主窗
            // 上面"是 Windows 的规则，不是 Qt 的开关）。所以这里只能钉住我们自己的那一半：没父窗。
            // 另一半（MainWindow 那条路真的传了 nullptr）在 mw_check 里按源码路径验。
            check(log->isWindow() && !log->isModal(),
                  "Z3c 是个普通顶层窗（不是模态对话框：它不该把启动器冻住）");
            // ---- 最大化/最小化两颗钮（用户说的「允许这个窗口最大化最小化」）----
            //
            // QDialog 默认那一套 flags 里只有关闭钮，所以这两颗**必须是我们显式加的**。
            // 这里能钉的就是"那两位真的在" —— offscreen 上量不出真的标题栏长什么样，
            // 但"加没加"这件事由我们自己决定，正是该钉的那一格。
            check((log->windowFlags() & Qt::WindowMinimizeButtonHint) != 0,
                  "Z3c 允许最小化（那颗钮是我们显式加的，QDialog 默认不给）",
                  QString::number(int(log->windowFlags())));
            check((log->windowFlags() & Qt::WindowMaximizeButtonHint) != 0,
                  "Z3c 允许最大化（日志是宽行文本，拉到整屏看长崩栈是常态）",
                  QString::number(int(log->windowFlags())));
            delete log;
        }
    }
    return 0;
}


