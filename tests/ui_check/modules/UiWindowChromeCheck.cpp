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
 * offscreen 下 2~4 跳过并明说，1 照量。
 */

#include "Support.hpp"

#include "ChromeDialog.hpp"
#include "DownloadToolDialog.hpp"
#include "GameLogWindow.hpp"
#include "InstanceUpdateWizard.hpp"
#include "ShellOpenWith.hpp"
#include "UpgradeHistoryDialog.hpp"
#include "UpgradeReportDialog.hpp"
#include "WindowChrome.hpp"
#include "chrome_native.hpp"

#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QTabBar>

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
    }
}

}  // namespace

int UiWindowChromeCheck(QApplication &) {
    QTemporaryDir fixture;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, fixture.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, fixture.filePath("system"));
    Anim::setEnabled(false);
    QApplication::setQuitOnLastWindowClosed(false);  // 下面反复 close() 窗口，不能把事件循环带走
    std::printf("平台插件：%s\n", qPrintable(QGuiApplication::platformName()));

    // 产品里这些窗口都以主窗口为 parent 开出来（带 owner 的顶层窗：关闭时 Qt 会销毁平台窗口，
    // 再打开是新建的 HWND）；这里照样挂在主窗口下，才测得到「第二次打开」。
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
    {
        DownloadToolDialog dialog(&window);
        checkWindow(QStringLiteral("高速下载窗"), &dialog);
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
