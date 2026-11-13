/**
 * @file UiWindowChromeCheck.cpp
 * @brief 窗口外框在真 HWND 上的行为：主窗口和每一扇独立窗口都量同一组事。
 *
 * 平台插件必须是 `windows`（CTest 里 wtfml.window-chrome 已设；窗口摆在屏外、不激活，不碰前台）：
 * 1. 空白处判定（`WindowChrome::isDragBlank`）：按钮 / 输入框 / 列表 / 滚动条等控件上永远不是空白，
 *    每扇窗口里都找得到空白点；
 * 2. 顶部条：WM_NCHITTEST 在空白格回 HTCAPTION、在控件格不回；
 * 3. 没有原生子窗口盖住客户区（对子控件调 winId() 会造出来，HTCAPTION 就到不了顶层窗）；
 * 4. 窗口隐藏再显示（HWND 重建）后系统标题栏没有回来（高速下载窗第二次打开带系统标题栏的根因）。
 * 5. 背板：DWM 背板类型跟着档位（亚克力 3、云母 2、别的 1），HWND 重建后照样；亚克力 / 云母档模拟一次失焦
 *    （发 WM_NCACTIVATE FALSE）后非客户区仍是激活态 —— DWM 只在激活态画材质，这就是「失焦不退色」；
 *    别的档、`WTFML_KEEP_BACKDROP_ACTIVE=0` 时照系统默认退成未激活。
 *    另钉住系统材质模型的合成色 = 本机量的那几个数（纯黑 / 纯白 / 棋盘，见 SystemBackdrop.cpp）。
 * 6. 主页风景进出主页时淡入 / 淡出（`Glass::Backdrop::heroOpacity()`）：中途 0<α<1 且单调，终值恰为 0 / 1，
 *    动效关着时直接落到终值。
 * offscreen 下 2~5 跳过并明说，1、6 和配方照量。
 */

#include "Support.hpp"

#include "Animations.hpp"
#include "ChromeDialog.hpp"
#include "GameLogWindow.hpp"
#include "Glass.hpp"
#include "SystemBackdrop.hpp"

#include "InstanceUpdateWizard.hpp"
#include "ShellOpenWith.hpp"
#include "UpgradeHistoryDialog.hpp"
#include "UpgradeReportDialog.hpp"
#include "WindowChrome.hpp"
#include "chrome_native.hpp"

#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QTabBar>
#include <QElapsedTimer>
#include <QVariantAnimation>

namespace {

/// 主窗口在 ui_check 里是裸 QMainWindow：补上产品 MainWindow::nativeEvent 那一句转发。
class HostWindow : public QMainWindow {
public:
    WindowChrome *chrome = nullptr;

protected:
    bool nativeEvent(const QByteArray &type, void *message, qintptr *result) override {
        if (chrome != nullptr && chrome->handleNativeEvent(message, result)) return true;
        return QMainWindow::nativeEvent(type, message, result);
    }
};

void settle() {
    QEventLoop loop;
    QTimer::singleShot(250, &loop, &QEventLoop::quit);
    loop.exec();
}

void showQuiet(QWidget *window) {
    window->setAttribute(Qt::WA_ShowWithoutActivating);
    window->move(-20000, 200);  // 屏外：不盖用户的屏幕
    window->show();
    settle();
}

bool underControl(const QWidget *under, const QWidget *window) {
    for (const QWidget *w = under; w != nullptr && w != window; w = w->parentWidget()) {
        if (qobject_cast<const QAbstractButton *>(w) || qobject_cast<const QLineEdit *>(w) ||
            qobject_cast<const QAbstractSpinBox *>(w) || qobject_cast<const QComboBox *>(w) ||
            qobject_cast<const QAbstractItemView *>(w) || qobject_cast<const QAbstractSlider *>(w) ||
            qobject_cast<const QTextEdit *>(w) || qobject_cast<const QPlainTextEdit *>(w) ||
            qobject_cast<const QTabBar *>(w)) {
            return true;
        }
    }
    return false;
}

QString className(const QWidget *w) { return QString::fromLatin1(w->metaObject()->className()); }

/// 从 `w` 往上到窗口的类名链，排错时一眼看出点在谁身上。
QString chain(const QWidget *w, const QWidget *window) {
    QStringList names;
    for (; w != nullptr && w != window; w = w->parentWidget()) names << className(w);
    return names.join(QLatin1Char('<'));
}

/// 整窗按 10px 网格扫：控件上永远不是空白，窗口里找得到空白点。
void scanBlank(const QString &name, QWidget *window) {
    int blank = 0;
    QStringList wrong;
    for (int y = 6; y < window->height(); y += 10) {
        for (int x = 6; x < window->width(); x += 10) {
            QWidget *hit = window->childAt(x, y);
            QWidget *under = hit != nullptr ? hit : window;
            const bool isBlank = WindowChrome::isDragBlank(under, window);
            if (underControl(under, window)) {
                if (isBlank) wrong << QStringLiteral("%1(%2,%3)").arg(chain(under, window)).arg(x).arg(y);
            } else if (isBlank) {
                ++blank;
            }
        }
    }
    check(wrong.isEmpty(), QStringLiteral("WC %1 控件上不是空白（点了不会拖窗）").arg(name), wrong.mid(0, 6).join(QLatin1Char(' ')));
    check(blank > 0, QStringLiteral("WC %1 窗口里找得到空白点").arg(name), QString::number(blank));
}

/// 5) 背板：DWM 背板类型跟着档位；模拟一次失焦后，亚克力 / 云母档（开关没关）非客户区仍是激活态，别的情况退成未激活。
/// 切到 `tab`，边等风景淡完边每 8 ms 记一次 `heroOpacity()`（含切之前那一刻和淡完那一刻）。
QList<qreal> sweepHeroFade(QTabWidget *tabs, Glass::Backdrop *backdrop, int tab) {
    auto *fade = backdrop->findChild<QVariantAnimation *>(QStringLiteral("glassHeroFade"));
    QList<qreal> seen{backdrop->heroOpacity()};
    tabs->setCurrentIndex(tab);
    QElapsedTimer clock;
    clock.start();
    while (fade != nullptr && fade->state() != QAbstractAnimation::Stopped && clock.elapsed() < 2000) {
        seen << backdrop->heroOpacity();
        QEventLoop loop;
        QTimer::singleShot(8, &loop, &QEventLoop::quit);
        loop.exec();
    }
    seen << backdrop->heroOpacity();
    return seen;
}

/// 风景淡变那一串采样：有中间值、单调（`down` = 往 0 走）、终值恰为 `end`。
void checkHeroSweep(const QList<qreal> &seen, bool down, qreal end, const char *name) {
    bool monotonic = true;
    int middle = 0;
    QStringList trace;
    for (int i = 0; i < seen.size(); ++i) {
        trace << QString::number(seen.at(i), 'f', 3);
        if (seen.at(i) > 0.0 && seen.at(i) < 1.0) ++middle;
        if (i > 0 && (down ? seen.at(i) > seen.at(i - 1) : seen.at(i) < seen.at(i - 1))) monotonic = false;
    }
    check(middle > 0 && monotonic && seen.last() == end, name,
          QStringLiteral("中间值 %1 个 / 单调 %2 / %3").arg(middle).arg(monotonic).arg(trace.join(QLatin1Char(' '))));
}

void checkBackdrop(const QString &name, QWidget *window, const QString &when) {
    const Glass::Mode mode = Glass::effectiveMode();
    const bool system = mode == Glass::Mode::Acrylic || mode == Glass::Mode::Mica;
    const bool keep = system && qEnvironmentVariable("WTFML_KEEP_BACKDROP_ACTIVE") != QLatin1String("0");
    const int dwm = mode == Glass::Mode::Acrylic ? 3 : (mode == Glass::Mode::Mica ? 2 : 1);
    const int type = chromeNativeBackdropType(window);
    chromeNativeSendNcActivate(window, false);
    const int frame = chromeNativeFrameActive(window);
    chromeNativeSendNcActivate(window, true);  // 还原成激活态（真实的键盘焦点不受这两条影响）
    check(type == dwm && frame == (keep ? 1 : 0),
          QStringLiteral("WC %1 %2：背板类型跟档位、失焦后%3").arg(name, when, keep ? QStringLiteral("仍是激活态（不退色）") : QStringLiteral("照常退成未激活")),
          QStringLiteral("%1 DWM=%2 失焦后激活态=%3").arg(Glass::modeName(mode)).arg(type).arg(frame));
}

/// 配方合成色和本机对着系统材质量的数差不过 `tolerance`（每通道）。
bool closeEnough(unsigned got, unsigned want, int tolerance) {
    for (int shift : {16, 8, 0}) {
        if (qAbs(int((got >> shift) & 0xff) - int((want >> shift) & 0xff)) > tolerance) return false;
    }
    return true;
}

void checkRecipes() {
    using SystemBackdrop::Material;
    struct Case {
        Material material;
        bool dark;
        unsigned behind, measured;
        const char *what;
    };
    // 2026-11-12 本机 26200：同一扇窗、DWM 系统背板聚焦态，背后垫纯黑 / 纯白 / 红蓝棋盘（模糊后 #7f007f）量的平均色。
    const Case cases[] = {
        {Material::Acrylic, false, 0x000000, 0x878787, "浅色亚克力 背后纯黑"},  {Material::Acrylic, false, 0xffffff, 0xe3e3e3, "浅色亚克力 背后纯白"},
        {Material::Acrylic, false, 0x7f007f, 0xcd75cd, "浅色亚克力 背后棋盘"},  {Material::Acrylic, true, 0x000000, 0x363636, "深色亚克力 背后纯黑"},
        {Material::Acrylic, true, 0xffffff, 0x929292, "深色亚克力 背后纯白"},   {Material::Acrylic, true, 0x7f007f, 0x931493, "深色亚克力 背后棋盘"},
        {Material::Mica, false, 0x000000, 0xf3f3f3, "浅色云母 明度整个换掉"}, {Material::Mica, true, 0xffffff, 0x202020, "深色云母 明度整个换掉"},
    };
    QStringList off;
    for (const Case &c : cases) {
        const unsigned got = SystemBackdrop::composite(SystemBackdrop::recipe(c.material, c.dark), c.behind);
        if (!closeEnough(got, c.measured, 5)) off << QStringLiteral("%1 #%2≠#%3").arg(QString::fromUtf8(c.what)).arg(got, 6, 16, QLatin1Char('0')).arg(c.measured, 6, 16, QLatin1Char('0'));
    }
    check(off.isEmpty(), "WC 系统材质模型合成色 = 本机量的数（每通道 ±5）", off.join(QLatin1Char(' ')));
    // 对比度现算用的区间要包住两端：浅色亚克力背后纯黑那一下（#878787）是最暗的一档。
    Glass::setMode(Glass::Mode::Acrylic);
    bool darkest = false;
    for (const QColor &c : Glass::systemBackdropRange()) darkest = darkest || c.lightness() <= 0x90;
    check(darkest, "WC 背板区间含背后纯黑那一档（浅色亚克力 #878787，不是旧估计的 #b4）");
}

void checkWindow(const QString &name, QWidget *window, bool cycleByClose = true) {
    showQuiet(window);
    scanBlank(name, window);

    if (!chromeNativeAvailable()) {
        std::printf("  SKIP  WC %s 真 HWND 那几项（平台插件不是 windows）\n", qPrintable(name));
        return;
    }
    // ---- 2) 顶部一行：控件格不回 HTCAPTION、空白格回 HTCAPTION ----
    int caption = 0;
    QStringList stolen;
    for (int x = 8; x < window->width() - 8; x += 6) {
        const QPoint p(x, 14);
        QWidget *hit = window->childAt(p);
        const int ht = chromeNativeHitTest(window, p);
        if (hit != nullptr && underControl(hit, window)) {
            if (ht == chromeHtCaption()) stolen << QStringLiteral("%1(%2)").arg(className(hit)).arg(x);
        } else if (ht == chromeHtCaption()) {
            ++caption;
        }
    }
    check(stolen.isEmpty(), QStringLiteral("WC %1 顶部条上的按钮格不被当成标题栏（点得动）").arg(name), stolen.mid(0, 6).join(QLatin1Char(' ')));
    check(caption >= 5, QStringLiteral("WC %1 顶部条的空白格回 HTCAPTION（拖得动）").arg(name), QString::number(caption));
    // ---- 3) 没有原生子窗口盖住 ----
    check(chromeNativeChildCount(window) == 0 && chromeNativeMouseReachesWindow(window, QPoint(window->width() / 2, 14)) == 1,
          QStringLiteral("WC %1 没有原生子窗口盖住客户区").arg(name), QString::number(chromeNativeChildCount(window)));
    // ---- 4) 标题栏收掉，隐藏再显示两次也一样 ----
    check(chromeNativeCaptionHeight(window) == 0, QStringLiteral("WC %1 第一次显示没有系统标题栏").arg(name),
          QString::number(chromeNativeCaptionHeight(window)));
    checkBackdrop(name, window, QStringLiteral("第一次显示"));
    for (int round = 2; round <= 3; ++round) {
        // 对话框关窗口走 close / reject：Qt 会销毁平台窗口，再 show 时 HWND 是新建的。
        // 主窗口 close 会走产品的 closeEvent 一整套，这里的 HostWindow 只是壳，用 hide。
        cycleByClose ? static_cast<void>(window->close()) : window->hide();
        settle();
        window->show();
        window->raise();  // 产品里 open() 是 show + raise + activateWindow
        settle();
        check(chromeNativeCaptionHeight(window) == 0, QStringLiteral("WC %1 第 %2 次显示没有系统标题栏").arg(name).arg(round),
              QString::number(chromeNativeCaptionHeight(window)));
        checkBackdrop(name, window, QStringLiteral("第 %1 次显示").arg(round));
    }
}

}  // namespace

int UiWindowChromeCheck(QApplication &) {
    // 产品里这些窗口都以主窗口为 parent 开出来（带 owner 的顶层窗：关闭时 Qt 会销毁平台窗口，
    // 再打开是新建的 HWND）；这里照样挂在主窗口下，才测得到「第二次打开」。
    checkRecipes();  // 顺带把档位定在亚克力：下面每扇窗口都按亚克力量背板
    HostWindow window;
    MainWindowUI ui(&window);
    window.chrome = ui.chrome();
    window.resize(1100, 760);
    checkWindow(QStringLiteral("主窗口"), &window, false);
    // 主窗口的五页（主页 / 管理 / 下载 / 设置 / 智能体）各扫一遍。
    if (QTabWidget *tabs = ui.getTabWidget()) {
        for (int i = 1; i < tabs->count(); ++i) {
            tabs->setCurrentIndex(i);
            settle();
            scanBlank(QStringLiteral("主窗口·%1页").arg(tabs->tabText(i)), &window);
        }
        tabs->setCurrentIndex(0);
        settle();
    }
    // 导航轨：空白格回 HTCAPTION、导航钮不回。
    if (chromeNativeAvailable() && ui.titleBar() != nullptr) {
        TitleBar *rail = ui.titleBar();
        int railCaption = 0;
        QStringList stolen;
        for (int y = 60; y < rail->height() - 4; y += 8) {
            const QPoint p = rail->mapTo(&window, QPoint(rail->width() / 2, y));
            QWidget *hit = window.childAt(p);
            const int ht = chromeNativeHitTest(&window, p);
            if (hit != nullptr && underControl(hit, &window)) {
                if (ht == chromeHtCaption()) stolen << QString::number(y);
            } else if (ht == chromeHtCaption()) {
                ++railCaption;
            }
        }
        check(stolen.isEmpty() && railCaption > 0, QStringLiteral("WC 主窗口 导航轨：空白回 HTCAPTION、导航钮不回"),
              QStringLiteral("空白 %1 / 误吞 %2").arg(railCaption).arg(stolen.join(QLatin1Char(','))));
    }
    // ---- 6) 主页风景淡入 / 淡出（#68）----
    if (QTabWidget *tabs = ui.getTabWidget(); tabs != nullptr) {
        auto *backdrop = window.findChild<Glass::Backdrop *>();
        const int home = MainWindowUI::homeTabIndex();
        const int away = MainWindowUI::settingsTabIndex();
        tabs->setCurrentIndex(home);
        settle();
        // 上面隐藏再显示过；摆在屏外的窗口重显后 Windows 不一定送 WM_PAINT，没画过的 Backdrop 按「还没上屏」
        // 直接跳终值（启动 / 从托盘回来不淡）。先画一帧 = 已经上过屏。
        window.grab();
        check(backdrop != nullptr && backdrop->heroOpacity() == 1.0, "WC 风景淡变 起点：停在主页时风景不透明度恰为 1",
              backdrop != nullptr ? QString::number(backdrop->heroOpacity()) : QStringLiteral("没有 Backdrop"));
        if (backdrop != nullptr) {
            Anim::setEnabled(true);
            checkHeroSweep(sweepHeroFade(tabs, backdrop, away), true, 0.0,
                           "WC 风景淡变 主页→设置：中途 0<α<1、单调变小、终值恰为 0");
            check(!window.property(Anim::kBackdropFadingProperty).toBool(), "WC 风景淡变 淡完撤掉「正在淡」窗口标记（幕布不再每帧重拍底板）");
            checkHeroSweep(sweepHeroFade(tabs, backdrop, home), false, 1.0,
                           "WC 风景淡变 设置→主页：中途 0<α<1、单调变大、终值恰为 1");
            Anim::setEnabled(false);
            tabs->setCurrentIndex(away);
            const qreal off = backdrop->heroOpacity();
            tabs->setCurrentIndex(home);
            const qreal on = backdrop->heroOpacity();
            check(off == 0.0 && on == 1.0, "WC 风景淡变 动效关着：换页当下直接落到终值（0 / 1，不经中间值）",
                  QStringLiteral("出 %1 / 回 %2").arg(off).arg(on));
            settle();
        }
    }
    {
        UpgradeHistoryDialog dialog(&window);
        checkWindow(QStringLiteral("升级历史窗"), &dialog);
    }
    {
        UpgradeReportDialog dialog(&window);
        checkWindow(QStringLiteral("升级报告窗"), &dialog);
    }
    {
        GameLogWindow log(QStringLiteral("check"), &window);
        checkWindow(QStringLiteral("游戏日志窗"), &log, false);
    }
    {
        InstanceUpdateWizard wizard(new QWidget, new QWidget, new QWidget, new QWidget);
        checkWindow(QStringLiteral("实例更新向导"), &wizard);
    }
    // 原理图信息窗 / 模组预设确认窗是 src/ 里的 ChromeDialog 子类，ui 验证工程不链它们；
    // 它们和下面这个基类同壳，TL1（UiTopLevelDialogChecks）钉住继承关系。
    {  // 基类本身（所有 ChromeDialog 子类的共同壳）
        ChromeDialog dialog(&window);
        checkWindow(QStringLiteral("ChromeDialog"), &dialog);
    }
    // ---- 5b) 换档跟着换背板；回退开关 WTFML_KEEP_BACKDROP_ACTIVE=0；窗口正失焦时换进亚克力要补回激活态 ----
    if (chromeNativeAvailable()) {
        const QString main = QStringLiteral("主窗口");
        Glass::setMode(Glass::Mode::Mica);
        settle();
        checkBackdrop(main, &window, QStringLiteral("换到云母"));
        Glass::setMode(Glass::Mode::Scene);
        settle();
        checkBackdrop(main, &window, QStringLiteral("换到风景底"));
        qputenv("WTFML_KEEP_BACKDROP_ACTIVE", "0");
        Glass::setMode(Glass::Mode::Acrylic);
        settle();
        checkBackdrop(main, &window, QStringLiteral("WTFML_KEEP_BACKDROP_ACTIVE=0"));
        chromeNativeSendNcActivate(&window, false);  // 开关关着时失焦：DWM 按未激活画平色
        qunsetenv("WTFML_KEEP_BACKDROP_ACTIVE");
        window.chrome->applyFrame();  // 撤掉开关后重贴（档位没变 Theme 不一定重贴，手动贴一次）
        settle();
        if (Glass::systemBackdrop()) {  // 没有系统背板（Win10、Server）时亚克力落到风景底，本来就不补
            check(chromeNativeFrameActive(&window) == 1, "WC 主窗口 失焦时重贴外框补回激活态（不等下一次得焦点）",
                  QString::number(chromeNativeFrameActive(&window)));
        }
        checkBackdrop(main, &window, QStringLiteral("撤掉开关"));
    }

    // ---- 原生子窗口的来源：对子控件调 winId() 的后果，和「打开方式」挂的那个 owner ----
    {
        HostWindow plain;
        auto *central = new QWidget;
        auto *child = new QLabel(QStringLiteral("子控件"), central);
        plain.setCentralWidget(central);
        plain.resize(500, 300);
        showQuiet(&plain);
        QWidget *owner = ShellOpenWith::ownerWindow(child);
        check(owner == &plain, "WC 「打开方式」挂在顶层窗上（不是子控件）");
        if (chromeNativeAvailable()) {
            owner->winId();
            settle();
            check(chromeNativeChildCount(&plain) == 0, "WC 对顶层窗取 winId() 不造原生子窗口");
            child->winId();  // 对照：旧写法
            settle();
            check(chromeNativeChildCount(&plain) > 0 && chromeNativeMouseReachesWindow(&plain, QPoint(250, 150)) == 0,
                  "WC 对照：对子控件取 winId() 会造出盖住客户区的原生子窗口（本组断言抓的就是它）");
        }
    }
    return g_fail == 0 ? 0 : 1;
}


