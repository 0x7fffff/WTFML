/**
 * @file UiWindowMotionCheck.cpp
 * @brief 开关程序「雾散开」（`ui/WindowMotion`）：
 * 1. 开场：`armOpening()` 把四组内容藏起（占位）、柔光关掉、品牌标空着；时间线跑完各组回到原位且可见、
 *    占位标记还原、柔光颜色还原、品牌标写完、风景淡到 1、幕布都收掉。
 * 2. 关窗合雾：动效关着 / 不可见 / 最小化时直接放行；正常时第一次拦下、途中再关吞掉、播完自己关窗，
 *    Done 之后不再拦。
 * 3. `PageCurtain` 放开负偏移之后，换页那一发照旧：内容顶边从不被裁掉（偏移从不为负）。
 */

#include "Support.hpp"

#include "Animations.hpp"
#include "Glass.hpp"
#include "TitleBar.hpp"
#include "WindowMotion.hpp"

#include <QCloseEvent>
#include <QGraphicsDropShadowEffect>
#include <QMainWindow>
#include <QVariantAnimation>

namespace {

void wait(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

bool curtainShown(const QWidget *root) {
    for (const QWidget *w : root->findChildren<QWidget *>(QStringLiteral("wtfmlPageCurtain")))
        if (w->isVisible()) return true;
    return false;
}

/// 主页摆一份「当前实例」，启动行（含柔光）才建得出来。
void fillHome(MainWindowUI &ui) {
    MainWindowUI::HomeQuickItem item;
    item.key = QStringLiteral("demo");
    item.name = QStringLiteral("1.21.4 Fabric");
    item.detail = QStringLiteral("Fabric 0.16.9 · 1.21.4");
    item.current = true;
    ui.setHomeQuickRows({item}, {item}, {item});
    ui.setHomeQuickMode(MainWindowUI::HomeQuickMode::Current);
}

QStringList groupNames() {
    return {MainWindowUI::homeHeroTitleName(), MainWindowUI::homeHeroQuoteName(), MainWindowUI::skinViewerName(),
            MainWindowUI::homeCurrentPickerName(), QStringLiteral("homeCurrentLaunch"),
            QStringLiteral("homeCurrentEdit")};
}

}  // namespace

int UiWindowMotionCheck(QApplication &) {
    // ---- 1) 开场时间线 ----
    {
        // 对照：同样一扇主窗口，动效关着直接上屏，量各组的终态几何。
        QMainWindow reference;
        MainWindowUI referenceUi(&reference);
        fillHome(referenceUi);
        reference.resize(1100, 760);
        reference.show();
        wait(200);
        QHash<QString, QRect> expected;
        for (const QString &name : groupNames())
            if (QWidget *w = reference.findChild<QWidget *>(name); w != nullptr && w->isVisibleTo(&reference))
                expected.insert(name, w->geometry());
        reference.hide();

        Anim::setEnabled(true);
        QMainWindow window;
        MainWindowUI ui(&window);
        fillHome(ui);
        window.resize(1100, 760);
        QHash<QString, bool> retainBefore;
        for (const QString &name : expected.keys())
            if (QWidget *w = window.findChild<QWidget *>(name))
                retainBefore.insert(name, w->sizePolicy().retainSizeWhenHidden());
        auto *launch = window.findChild<QWidget *>(QStringLiteral("homeCurrentLaunch"));
        auto *glow = launch != nullptr ? qobject_cast<QGraphicsDropShadowEffect *>(launch->graphicsEffect()) : nullptr;
        const QColor glowColor = glow != nullptr ? glow->color() : QColor();
        auto *logo = window.findChild<TitleBarButton *>(QStringLiteral("titleBarLogo"));
        auto *backdrop = window.findChild<Glass::Backdrop *>();

        WindowMotion::armOpening(&window);
        QStringList notHidden;
        for (const QString &name : expected.keys())
            if (QWidget *w = window.findChild<QWidget *>(name); w == nullptr || w->isVisibleTo(&window)) notHidden << name;
        check(expected.contains(QStringLiteral("homeCurrentLaunch")) && expected.contains(MainWindowUI::homeHeroTitleName()) &&
                  glow != nullptr && backdrop != nullptr,
              "开场 夹具：主页有问候、启动行（带柔光），下面几条不是空过",
              QStringLiteral("%1 组：%2").arg(expected.size()).arg(expected.keys().join(QLatin1Char(','))));
        check(!expected.isEmpty() && notHidden.isEmpty(),
              "开场 首帧前四组内容都藏起来（名字和 MainWindowUI 对得上）",
              QStringLiteral("%1 组 / 没藏 %2").arg(expected.size()).arg(notHidden.join(QLatin1Char(','))));
        check(glow == nullptr || !glow->isEnabled(), "开场 首帧前启动按钮的柔光关着（幕布盖不住它）");
        check(logo != nullptr && logo->writeProgress() == 0.0, "开场 首帧前品牌标空着（写到 0）",
              logo != nullptr ? QString::number(logo->writeProgress()) : QStringLiteral("没有品牌标"));

        window.show();
        wait(1100);  // 时间线最长一条 620 ms（柔光），留足余量

        QStringList moved, hidden, retain;
        for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
            QWidget *w = window.findChild<QWidget *>(it.key());
            if (w == nullptr || !w->isVisible()) {
                hidden << it.key();
                continue;
            }
            if (w->geometry() != it.value())
                moved << QStringLiteral("%1 %2,%3→%4,%5").arg(it.key()).arg(it.value().x()).arg(it.value().y())
                             .arg(w->geometry().x()).arg(w->geometry().y());
            if (w->sizePolicy().retainSizeWhenHidden() != retainBefore.value(it.key())) retain << it.key();
        }
        check(hidden.isEmpty() && moved.isEmpty(), "开场 跑完各组都可见、几何和不播动效时一模一样",
              QStringLiteral("没出来 %1 / 挪了 %2").arg(hidden.join(QLatin1Char(',')), moved.join(QLatin1Char(';'))));
        check(retain.isEmpty(), "开场 跑完「藏起来也占位」标记还原成原来的值", retain.join(QLatin1Char(',')));
        check(!curtainShown(&window), "开场 跑完幕布都收掉了（露出的是真控件）");
        check(glow == nullptr || (glow->isEnabled() && qAbs(glow->color().alphaF() - glowColor.alphaF()) < 0.01 &&
                                  glow->color().rgb() == glowColor.rgb()),
              "开场 跑完柔光开着、颜色和透明度还原",
              glow != nullptr ? QStringLiteral("%1 → %2").arg(glowColor.name(QColor::HexArgb), glow->color().name(QColor::HexArgb))
                              : QStringLiteral("这扇窗没有柔光"));
        check(logo != nullptr && logo->writeProgress() == 1.0, "开场 跑完品牌标写完（平常的样子）",
              logo != nullptr ? QString::number(logo->writeProgress()) : QString());
        check(backdrop != nullptr && backdrop->heroShown() && backdrop->heroOpacity() == 1.0,
              "开场 跑完主页风景淡到 1",
              backdrop != nullptr ? QString::number(backdrop->heroOpacity()) : QStringLiteral("没有 Backdrop"));
        window.hide();
        Anim::setEnabled(false);
    }

    // ---- 2) 关窗合雾：拦下 / 放行 ----
    {
        QWidget window;
        window.resize(320, 200);
        WindowMotion::CloseFog state = WindowMotion::CloseFog::Idle;
        const auto intercept = [&]() {
            QCloseEvent event;
            event.accept();
            const bool held = WindowMotion::interceptClose(&window, &event, state);
            return held && !event.isAccepted();
        };

        window.show();
        wait(50);
        check(!intercept() && state == WindowMotion::CloseFog::Idle, "关窗 动效关着（测试截图）：直接放行");
        Anim::setEnabled(true);
        window.hide();
        check(!intercept() && state == WindowMotion::CloseFog::Idle, "关窗 窗口不可见：直接放行");
        window.showMinimized();
        wait(50);
        check(!window.isMinimized() || (!intercept() && state == WindowMotion::CloseFog::Idle),
              "关窗 最小化：直接放行", QStringLiteral("isMinimized %1").arg(window.isMinimized()));
        window.showNormal();
        wait(50);
        const bool first = intercept();
        check(first && state == WindowMotion::CloseFog::Running, "关窗 正常：第一次拦下，开始合雾");
        const bool again = intercept();
        check(again && state == WindowMotion::CloseFog::Running, "关窗 合雾途中再关一次：吞掉，等它播完");
        wait(500);
        check(state == WindowMotion::CloseFog::Done && !window.isVisible(), "关窗 合雾播完自己关窗（第二次 close 放行）",
              QStringLiteral("state %1 / visible %2").arg(int(state)).arg(window.isVisible()));
        window.show();
        wait(50);
        check(!intercept(), "关窗 Done 之后不再拦");
        Anim::setEnabled(false);
    }

    // ---- 3) 换页幕布：负偏移放开之后，换页的内容顶边从不被裁 ----
    {
        QWidget host;
        host.resize(120, 80);
        QPalette hostPalette = host.palette();
        hostPalette.setColor(QPalette::Window, Qt::black);
        host.setPalette(hostPalette);
        auto *page = new QWidget(&host);
        page->setGeometry(0, 0, 120, 80);
        page->setAutoFillBackground(true);
        QPalette pagePalette = page->palette();
        pagePalette.setColor(QPalette::Window, Qt::white);
        page->setPalette(pagePalette);
        auto *band = new QWidget(page);  // 顶边一条红：偏移若为负，它会被裁出幕布、一帧里找不到
        band->setGeometry(0, 0, 120, 3);
        band->setAutoFillBackground(true);
        QPalette bandPalette = band->palette();
        bandPalette.setColor(QPalette::Window, Qt::red);
        band->setPalette(bandPalette);
        host.show();
        wait(50);

        Anim::setEnabled(true);
        Anim::pageAppear(page);
        int frames = 0, missing = 0;
        QElapsedTimer clock;
        clock.start();
        while (curtainShown(&host) && clock.elapsed() < 2000) {
            const QImage shot = host.grab().toImage();
            bool red = false;
            for (int y = 0; y < shot.height() && !red; ++y) {
                const QColor c = shot.pixelColor(60, y);
                red = c.red() > c.green() + 20 && c.red() > c.blue() + 20;
            }
            ++frames;
            if (!red) ++missing;
            wait(8);
        }
        Anim::setEnabled(false);
        check(frames > 0 && missing == 0, "换页 幕布负偏移放开后：每一帧内容顶边那条都在（偏移从不为负）",
              QStringLiteral("取样 %1 帧 / 顶边丢了 %2 帧").arg(frames).arg(missing));
    }
    return g_fail == 0 ? 0 : 1;
}
